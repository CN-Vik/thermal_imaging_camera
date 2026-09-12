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

static uint16_t eeMLX90640[832];
static float mlx90640To[768];
static uint16_t frame[834];
static float emissivity = 0.95f;

// 全局图像显存（去掉 static，共 1536 字节）
uint16_t s_raw_rgb565[MLX_W * MLX_H];

// 最简冷暖色映射：低温纯蓝 -> 常温绿 -> 高温纯红
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

    // ST7789 如果后续上屏发现色彩发蓝/反相，可在此处加上 __builtin_bswap16()
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void ir_camera_task_main(void *pvParameters)
{
    (void)pvParameters; // 避免未使用参数警告
    ESP_LOGI(TAG, "IR camera task running on core %d", xPortGetCoreID());

    MLX90640_I2CInit();
    MLX90640_SetRefreshRate(MLX90640_ADDR, 0x04); // 8Hz
    MLX90640_SetChessMode(MLX90640_ADDR);

    paramsMLX90640 *mlx90640 = (paramsMLX90640 *)calloc(1, sizeof(paramsMLX90640));
    if (!mlx90640) {
        ESP_LOGE(TAG, "mlx90640 calloc failed!");
        vTaskDelete(NULL);
    }

    MLX90640_DumpEE(MLX90640_ADDR, eeMLX90640);
    MLX90640_ExtractParameters(eeMLX90640, mlx90640);

    while (1) {
        // 1. 读满两个子页消除 0.00 棋盘格噪点
        for (int page = 0; page < 2; page++) {
            if (MLX90640_GetFrameData(MLX90640_ADDR, frame) >= 0) {
                float ta = MLX90640_GetTa(frame, mlx90640);
                MLX90640_CalculateTo(frame, mlx90640, emissivity, ta - TA_SHIFT, mlx90640To);
            }
        }

        // 2. 统计动态极值与中心点温度
        float min_t = 100.0f, max_t = -40.0f;
        for (int i = 0; i < 768; i++) {
            if (mlx90640To[i] < min_t) min_t = mlx90640To[i];
            if (mlx90640To[i] > max_t) max_t = mlx90640To[i];
        }
        float mid_t = mlx90640To[12 * MLX_W + 16];

        // 3. 将 768 个原始温度直接映射为 RGB565 颜色
        for (int i = 0; i < 768; i++) {
            s_raw_rgb565[i] = simple_temp_to_rgb565(mlx90640To[i], min_t, max_t);
        }

        // 4. 调用本工程的互斥锁通知 LVGL 重绘
        if (example_lvgl_lock(30)) {
            if (img_thermal) {
                lv_obj_invalidate(img_thermal); // 标记图像脏，底层自动将 s_raw_rgb565 刷上屏幕
            }
            if (label_info) {
                lv_label_set_text_fmt(label_info, "Max:%.1f C  Min:%.1f C\nCenter:%.1f C", max_t, min_t, mid_t);
            }
            example_lvgl_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}