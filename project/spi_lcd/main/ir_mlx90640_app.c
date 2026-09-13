#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90640_API.h"
#include "ir_mlx90640_app.h"
#include "esp_timer.h"

static const char *TAG = "mlx_app";



TaskHandle_t ir_task_Handle = NULL;


static float emissivity = 0.95f;

// 全局图像显存
uint16_t s_raw_rgb565[MLX_W * MLX_H];

// 映射表
const float rate_hz_table[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};


/**
 * @brief 白热模式（White-Hot）：低温纯黑 -> 高温纯白（自动适配 LVGL 字节序）
 */
static inline uint16_t temp_to_white_hot_rgb565(float val, float min_t, float max_t) 
{
    if (max_t <= min_t) max_t = min_t + 0.1f;
    float norm = (val - min_t) / (max_t - min_t);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    uint8_t gray = (uint8_t)(norm * 255.0f);

    // 使用 LVGL 官方宏构建颜色，自动处理 16 位大端/小端字节交换
    return lv_color_make(gray, gray, gray).full;
}

/**
 * @brief 黑热模式（Black-Hot）：高温纯黑 -> 低温纯白（自动适配 LVGL 字节序）
 */
static inline uint16_t temp_to_black_hot_rgb565(float val, float min_t, float max_t) 
{
    if (max_t <= min_t) max_t = min_t + 0.1f;
    float norm = (val - min_t) / (max_t - min_t);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    uint8_t gray = (uint8_t)((1.0f - norm) * 255.0f);

    return lv_color_make(gray, gray, gray).full;
}

/**
 * @brief 经典彩虹伪彩色：纯蓝 -> 青 -> 绿 -> 黄 -> 纯红
 */
static inline uint16_t temp_to_rainbow_rgb565(float val, float min_t, float max_t) 
{
    if (max_t <= min_t) max_t = min_t + 0.1f;
    float norm = (val - min_t) / (max_t - min_t);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    uint8_t r = 0, g = 0, b = 0;
    if (norm < 0.25f) { // 蓝 -> 青
        r = 0;
        g = (uint8_t)(norm * 4.0f * 255);
        b = 255;
    } else if (norm < 0.5f) { // 青 -> 绿
        r = 0;
        g = 255;
        b = (uint8_t)((1.0f - (norm - 0.25f) * 4.0f) * 255);
    } else if (norm < 0.75f) { // 绿 -> 黄
        r = (uint8_t)((norm - 0.5f) * 4.0f * 255);
        g = 255;
        b = 0;
    } else { // 黄 -> 红
        r = 255;
        g = (uint8_t)((1.0f - (norm - 0.75f) * 4.0f) * 255);
        b = 0;
    }

    // 核心：使用 lv_color_make 保证字节序 100% 正确
    return lv_color_make(r, g, b).full;
}

void ir_camera_task_main(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "IR camera task running on core %d", xPortGetCoreID());

    MLX90640_I2CInit();
    MLX90640_SetRefreshRate(MLX90640_ADDR, RefreshRate); // 8Hz
    MLX90640_SetChessMode(MLX90640_ADDR);

    uint16_t *ir_frame_data = (uint16_t*)calloc(834,sizeof(uint16_t));
    if (ir_frame_data == NULL) {
        // 分配失败，处理错误
        ESP_LOGE(TAG, "ir_frame_data calloc failed!");
        return;
    }

    uint16_t *eerom_ir_data = (uint16_t*)calloc(832,sizeof(uint16_t));
    if (eerom_ir_data == NULL) {
        // 分配失败，处理错误
        ESP_LOGE(TAG, "eerom_ir_data calloc failed!");
        return;
    }

    float *ir_temp_data = (float*)calloc(768,sizeof(float));
    if (ir_temp_data == NULL) {
        // 分配失败，处理错误
        ESP_LOGE(TAG, "ir_temp_data calloc failed!");
        return;
    }


    paramsMLX90640 *mlx90640_v = (paramsMLX90640 *)calloc(1, sizeof(paramsMLX90640));
    if (!mlx90640_v) {
        ESP_LOGE(TAG, "mlx90640_v calloc failed!");
        vTaskDelete(NULL);
    }

    MLX90640_DumpEE(MLX90640_ADDR, eerom_ir_data);
    MLX90640_ExtractParameters(eerom_ir_data, mlx90640_v);

    char text_buf[64]; // 用于标准 snprintf 格式化

    uint32_t frame_count = 0;
    float real_fps = 0.0f;
    int64_t last_time_us = 0;

    while (1) 
    {
        // 1. 读取单帧数据
        if (MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data) >= 0) {
            float ta = MLX90640_GetTa(ir_frame_data, mlx90640_v);
            MLX90640_CalculateTo(ir_frame_data, mlx90640_v, emissivity, ta - TA_SHIFT, ir_temp_data);
            frame_count++;
        }

        // 2. 统计极值（彻底过滤掉 1.0°C 以下的未更新异常点）
        float min_t = 100.0f, max_t = -40.0f;
        for (int i = 0; i < 768; i++) {
            if (ir_temp_data[i] > 1.0f) { // 核心：过滤掉未更新的 0.00
                if (ir_temp_data[i] < min_t) min_t = ir_temp_data[i];
                if (ir_temp_data[i] > max_t) max_t = ir_temp_data[i];
            }
        }

        // 如果全场温差过小（全在室温环境），强制拉开至少 4°C 动态范围，压制本底热噪声
        if ((max_t - min_t) < 4.0f) {
            max_t = min_t + 4.0f;
        }

        float mid_t = ir_temp_data[12 * MLX_W + 16];

        // 3. 颜色映射：只有 > 1.0°C 的像素才更新颜色，其余保留上一帧颜色（双子页无缝缝合）
        for (int i = 0; i < 768; i++) {
            if (ir_temp_data[i] > 1.0f) {
                s_raw_rgb565[i] = temp_to_rainbow_rgb565(ir_temp_data[i], min_t, max_t);
                // s_raw_rgb565[i] = temp_to_black_hot_rgb565(ir_temp_data[i], min_t, max_t);
                // s_raw_rgb565[i] = temp_to_white_hot_rgb565(ir_temp_data[i], min_t, max_t);

                // s_raw_rgb565[i] = (ir_temp_data[i]);
            }
        }

        int rate_code = MLX90640_GetRefreshRate(MLX90640_ADDR);

        float set_hz = (rate_code >= 0 && rate_code <= 7) ? rate_hz_table[rate_code] : 0.0f;

        // 4. 计算实际帧率 (每经过 1 秒计算一次平均 FPS)
        int64_t now_us = esp_timer_get_time();
        int64_t diff_us = now_us - last_time_us;
        if (diff_us >= 1000000) { // 满 1 秒 (1000000 微秒)
            real_fps = (float)frame_count * 1000000.0f / (float)diff_us;
            frame_count = 0;
            last_time_us = now_us;
        }

        // 4. 标准 snprintf 解决浮点数打印为 f 的问题
        snprintf(
            text_buf,
            sizeof(text_buf), 
            "Max:%.1fC\nMin:%.1fC\nCenter:%.1fC,\nset_FPS:%.1f,FPS:%.1f",
            max_t, 
            min_t, 
            mid_t,
            set_hz,
            real_fps
        );

        if (example_lvgl_lock(10)) {
            if (img_thermal) {
                lv_obj_invalidate(img_thermal);
            }
            if (label_info) {
                lv_label_set_text(label_info, text_buf);
            }
            example_lvgl_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if ( mlx90640_v && ir_frame_data && eerom_ir_data && ir_temp_data)
    {
        free(mlx90640_v);
        free(ir_frame_data);
        free(eerom_ir_data);
        free(ir_temp_data);

        eerom_ir_data = NULL;
        mlx90640_v = NULL;
        ir_frame_data = NULL;
        ir_temp_data = NULL;
    }
    
}