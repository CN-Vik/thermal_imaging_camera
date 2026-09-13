#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90640_API.h"
#include "ir_mlx90640_app.h"

static const char *TAG = "mlx_app";

#define MLX90640_ADDR 0x33
#define TA_SHIFT 8

TaskHandle_t ir_task_Handle = NULL;


static float emissivity = 0.95f;

// 全局图像显存
uint16_t s_raw_rgb565[MLX_W * MLX_H];



// 最简冷暖色映射：低温纯蓝 -> 中温绿 -> 高温纯红
static inline uint16_t simple_temp_to_rgb565(float val, float min_t, float max_t) 
{
    if (max_t <= min_t) max_t = min_t + 0.1f;
    float norm = (val - min_t) / (max_t - min_t);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    uint8_t r = 0, g = 0, b = 0;
    if (norm < 0.5f) {
        b = (uint8_t)((1.0f - norm * 2.0f) * 255);
        g = (uint8_t)(norm * 2.0f * 255);
    } else {
        g = (uint8_t)((1.0f - (norm - 0.5f) * 2.0f) * 255);
        r = (uint8_t)((norm - 0.5f) * 2.0f * 255);
    }

    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void ir_camera_task_main(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "IR camera task running on core %d", xPortGetCoreID());

    MLX90640_I2CInit();
    MLX90640_SetRefreshRate(MLX90640_ADDR, 0x04); // 8Hz
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

    while (1) 
    {
        // 1. 读取单帧数据
        if (MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data) >= 0) {
            float ta = MLX90640_GetTa(ir_frame_data, mlx90640_v);
            MLX90640_CalculateTo(ir_frame_data, mlx90640_v, emissivity, ta - TA_SHIFT, ir_temp_data);
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
                s_raw_rgb565[i] = simple_temp_to_rgb565(ir_temp_data[i], min_t, max_t);
            }
        }

        // 4. 标准 snprintf 解决浮点数打印为 f 的问题
        snprintf(text_buf, sizeof(text_buf), "Max:%.1f C  Min:%.1f C\nCenter:%.1f C", max_t, min_t, mid_t);

        if (example_lvgl_lock(10)) {
            if (img_thermal) {
                lv_obj_invalidate(img_thermal);
            }
            if (label_info) {
                lv_label_set_text(label_info, text_buf);
            }
            example_lvgl_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(15));
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