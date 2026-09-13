#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90640_API.h"
#include "ir_mlx90640_app.h"
#include <math.h>

static const char *TAG = "mlx_app";

TaskHandle_t ir_task_Handle = NULL;

static float emissivity = 0.95f;

// 高清送显显存（240x180 = 43200 像素）
uint16_t s_display_rgb565[MLX_INTERP_PIXELS];

// LVGL 图像描述符
lv_img_dsc_t s_thermal_img_dsc = {
    .header.always_zero = 0,
    .header.w = MLX_INTERP_W,
    .header.h = MLX_INTERP_H,
    .data_size = MLX_INTERP_PIXELS * sizeof(uint16_t),
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = (const uint8_t *)s_display_rgb565,
};

// 预计算的铁红（Ironbow）调色板
static uint16_t s_ironbow_lut[256];

const float rate_hz_table[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};

/**
 * @brief 初始化 256 阶铁红查找表：蓝黑 -> 玫红 -> 橙黄 -> 亮白
 */
static void init_ironbow_lut(void)
{
    for (int i = 0; i < 256; i++) {
        float norm = (float)i / 255.0f;
        uint8_t r = 0, g = 0, b = 0;

        if (norm < 0.25f) { // 蓝黑 -> 蓝紫
            float t = norm * 4.0f;
            r = (uint8_t)(t * 40.0f);
            g = 0;
            b = (uint8_t)(t * 160.0f);
        } else if (norm < 0.5f) { // 蓝紫 -> 玫红
            float t = (norm - 0.25f) * 4.0f;
            r = (uint8_t)(40.0f + t * 180.0f);
            g = 0;
            b = (uint8_t)(160.0f - t * 100.0f);
        } else if (norm < 0.75f) { // 玫红 -> 橙黄
            float t = (norm - 0.5f) * 4.0f;
            r = 255;
            g = (uint8_t)(t * 180.0f);
            b = (uint8_t)(60.0f - t * 60.0f);
        } else { // 橙黄 -> 亮白
            float t = (norm - 0.75f) * 4.0f;
            r = 255;
            g = (uint8_t)(180.0f + t * 75.0f);
            b = (uint8_t)(t * 255.0f);
        }
        s_ironbow_lut[i] = lv_color_make(r, g, b).full;
    }
}

/**
 * @brief 16位定点数双线性插值引擎（32x24 -> 240x180）
 */
static void bilinear_scale_to_rgb565(const uint8_t *src, int src_w, int src_h,
                                     uint16_t *dst, int dst_w, int dst_h,
                                     const uint16_t *lut)
{
    uint32_t x_ratio = ((src_w - 1) << 16) / (dst_w - 1);
    uint32_t y_ratio = ((src_h - 1) << 16) / (dst_h - 1);

    for (int y = 0; y < dst_h; y++) {
        uint32_t y_fp = y * y_ratio;
        int y0 = y_fp >> 16;
        int y1 = (y0 < src_h - 1) ? y0 + 1 : y0;
        uint32_t y_diff = y_fp & 0xFFFF;
        uint32_t y_diff_inv = 65536 - y_diff;

        const uint8_t *p_y0 = &src[y0 * src_w];
        const uint8_t *p_y1 = &src[y1 * src_w];
        uint16_t *dst_row = &dst[y * dst_w];

        for (int x = 0; x < dst_w; x++) {
            uint32_t x_fp = x * x_ratio;
            int x0 = x_fp >> 16;
            int x1 = (x0 < src_w - 1) ? x0 + 1 : x0;
            uint32_t x_diff = x_fp & 0xFFFF;
            uint32_t x_diff_inv = 65536 - x_diff;

            uint32_t top = (p_y0[x0] * x_diff_inv + p_y0[x1] * x_diff) >> 16;
            uint32_t bot = (p_y1[x0] * x_diff_inv + p_y1[x1] * x_diff) >> 16;

            uint32_t pixel_val = (top * y_diff_inv + bot * y_diff) >> 16;
            if (pixel_val > 255) pixel_val = 255;

            dst_row[x] = lut[pixel_val];
        }
    }
}

/**
 * @brief 在画面中心打上白色十字准星
 */
static inline void draw_center_crosshair(uint16_t *buf, int w, int h, uint16_t color)
{
    int cx = w / 2;
    int cy = h / 2;
    for (int d = -6; d <= 6; d++) {
        if (d == 0) continue;
        if (cx + d >= 0 && cx + d < w) buf[cy * w + (cx + d)] = color;
        if (cy + d >= 0 && cy + d < h) buf[(cy + d) * w + cx] = color;
    }
}

void ir_camera_task_main(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "IR camera task started on Core %d", xPortGetCoreID());

    init_ironbow_lut();

    MLX90640_I2CInit();
    MLX90640_SetRefreshRate(MLX90640_ADDR, RefreshRate);
    MLX90640_SetChessMode(MLX90640_ADDR);

    // 核心改动 1：全部使用 calloc 强制内存清零，严防野数据打爆浮点范围！
    uint16_t *ir_frame_data = (uint16_t *)calloc(834, sizeof(uint16_t));
    uint16_t *eerom_ir_data = (uint16_t *)calloc(832, sizeof(uint16_t));
    float *ir_temp_data     = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    paramsMLX90640 *mlx_params = (paramsMLX90640 *)calloc(1, sizeof(paramsMLX90640));

    float *filtered_temp    = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    uint8_t *norm_matrix    = (uint8_t *)calloc(MLX_RAW_PIXELS, sizeof(uint8_t));

    if (!ir_frame_data || !eerom_ir_data || !ir_temp_data || !mlx_params || !filtered_temp || !norm_matrix) {
        ESP_LOGE(TAG, "Critical: calloc failed, Out of memory!");
        vTaskDelete(NULL);
    }

    MLX90640_DumpEE(MLX90640_ADDR, eerom_ir_data);
    MLX90640_ExtractParameters(eerom_ir_data, mlx_params);
    free(eerom_ir_data); // 释放 EEPROM 数组

    char text_buf[80];
    uint32_t frame_count = 0;
    float real_fps = 0.0f;
    int64_t last_time_us = esp_timer_get_time();

    float smooth_min_t = 24.0f;
    float smooth_max_t = 34.0f;
    bool range_initialized = false;

    while (1) 
    {
        // 1. 读取单子页数据
        int status = MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);
        if (status >= 0) {
            // 获取当前返回的是 Subpage 0 还是 Subpage 1
            int subpage = ir_frame_data[833]; 

            float ta = MLX90640_GetTa(ir_frame_data, mlx_params);
            MLX90640_CalculateTo(ir_frame_data, mlx_params, emissivity, ta - TA_SHIFT, ir_temp_data);
            frame_count++;

            // 核心修复 1：只对当前硬件实际测出来的子页点做滤波！隔壁点绝不乱动！
            // Chess 模式下：(行 + 列) % 2 严格对应当前子页
            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                int r = i / MLX_RAW_W;
                int c = i % MLX_RAW_W;

                if (((r + c) & 1) == subpage) {
                    if (ir_temp_data[i] > 1.0f && ir_temp_data[i] < 200.0f) {
                        if (filtered_temp[i] < 1.0f) {
                            filtered_temp[i] = ir_temp_data[i];
                        } else {
                            // 核心修复 2：统一黄金比例（0.65 新数据 + 0.35 历史），干掉残影且无撕裂
                            filtered_temp[i] = 0.65f * ir_temp_data[i] + 0.35f * filtered_temp[i];
                        }
                    }
                }
            }

            // 2. 统计极值
            float cur_min = 200.0f, cur_max = -50.0f;
            int valid_pts = 0;
            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                if (filtered_temp[i] > 1.0f && filtered_temp[i] < 200.0f) {
                    if (filtered_temp[i] < cur_min) cur_min = filtered_temp[i];
                    if (filtered_temp[i] > cur_max) cur_max = filtered_temp[i];
                    valid_pts++;
                }
            }

            // 等两帧交织凑满全屏数据后再送显
            if (valid_pts > 400) {
                if ((cur_max - cur_min) < 3.0f) {
                    cur_max = cur_min + 3.0f;
                }

                // 核心修复 3：极值跟手度提升至 0.35，避免镜头晃动时明暗反应迟钝
                if (!range_initialized) {
                    smooth_min_t = cur_min;
                    smooth_max_t = cur_max;
                    range_initialized = true;
                } else {
                    smooth_min_t = 0.35f * cur_min + 0.65f * smooth_min_t;
                    smooth_max_t = 0.35f * cur_max + 0.65f * smooth_max_t;
                }

                float delta = smooth_max_t - smooth_min_t;
                if (delta < 1.0f) delta = 1.0f;
                float range_inv = 255.0f / delta;

                for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                    float val = (filtered_temp[i] - smooth_min_t) * range_inv;
                    if (val < 0.0f)   val = 0.0f;
                    if (val > 255.0f) val = 255.0f;
                    norm_matrix[i] = (uint8_t)val;
                }

                // 双线性插值直接输出到 240x180 显存
                bilinear_scale_to_rgb565(norm_matrix, MLX_RAW_W, MLX_RAW_H,
                                         s_display_rgb565, MLX_INTERP_W, MLX_INTERP_H,
                                         s_ironbow_lut);

                // 中央白色十字准星
                draw_center_crosshair(s_display_rgb565, MLX_INTERP_W, MLX_INTERP_H, 0xFFFF);
            }

            // 7. 计算帧率与中心温度
            int64_t now_us = esp_timer_get_time();
            int64_t diff_us = now_us - last_time_us;
            if (diff_us >= 1000000) {
                real_fps = (float)frame_count * 1000000.0f / (float)diff_us;
                frame_count = 0;
                last_time_us = now_us;
            }

            float center_t = filtered_temp[12 * MLX_RAW_W + 16];
            int rate_code = MLX90640_GetRefreshRate(MLX90640_ADDR);
            float set_hz = (rate_code >= 0 && rate_code <= 7) ? rate_hz_table[rate_code] : 0.0f;

            snprintf(text_buf, sizeof(text_buf),
                     "Max:%.1f Min:%.1f Cen:%.1f\nFPS:%.1f (Set:%.0fHz)",
                     cur_max, cur_min, center_t, real_fps, set_hz);

            // 8. 刷新 LVGL 画面
            if (example_lvgl_lock(100)) {
                if (img_thermal) {
                    lv_obj_invalidate(img_thermal);
                }
                if (label_info) {
                    lv_label_set_text(label_info, text_buf);
                }
                example_lvgl_unlock();
            }
        }

        // vTaskDelay(pdMS_TO_TICKS(1));
        taskYIELD();
    }
}