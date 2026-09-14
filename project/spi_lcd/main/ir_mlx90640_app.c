#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90640_API.h"
#include "ir_mlx90640_app.h"

static const char *TAG = "mlx_app";

TaskHandle_t ir_task_Handle = NULL;
static float emissivity = 0.95f;

static uint16_t *s_display_buf[2] = {NULL, NULL};
static uint8_t  s_back_idx = 0; 

lv_img_dsc_t s_thermal_img_dsc = {
    .header.always_zero = 0,
    .header.w = MLX_INTERP_W,
    .header.h = MLX_INTERP_H,
    .data_size = MLX_INTERP_PIXELS * sizeof(uint16_t),
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};

static uint16_t s_ironbow_lut[256];
const float rate_hz_table[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};

esp_err_t ir_camera_app_init(void)
{
    size_t buf_size = MLX_INTERP_PIXELS * sizeof(uint16_t);

#if CONFIG_SPIRAM
    s_display_buf[0] = (uint16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    s_display_buf[1] = (uint16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
#else
    s_display_buf[0] = (uint16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    s_display_buf[1] = (uint16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
#endif

    if (!s_display_buf[0]) {
        ESP_LOGE(TAG, "Buffer 0 allocation failed!");
        return ESP_ERR_NO_MEM;
    }
    if (!s_display_buf[1]) {
        ESP_LOGW(TAG, "RAM tight: Fallback to single buffer!");
        s_display_buf[1] = s_display_buf[0];
    }

    s_thermal_img_dsc.data = (const uint8_t *)s_display_buf[0];
    return ESP_OK;
}

static void init_ironbow_lut(void)
{
    for (int i = 0; i < 256; i++) {
        float norm = (float)i / 255.0f;
        uint8_t r = 0, g = 0, b = 0;

        if (norm < 0.25f) {
            float t = norm * 4.0f;
            r = (uint8_t)(t * 40.0f);
            b = (uint8_t)(t * 160.0f);
        } else if (norm < 0.5f) {
            float t = (norm - 0.25f) * 4.0f;
            r = (uint8_t)(40.0f + t * 180.0f);
            b = (uint8_t)(160.0f - t * 100.0f);
        } else if (norm < 0.75f) {
            float t = (norm - 0.5f) * 4.0f;
            r = 255;
            g = (uint8_t)(t * 180.0f);
            b = (uint8_t)(60.0f - t * 60.0f);
        } else {
            float t = (norm - 0.75f) * 4.0f;
            r = 255;
            g = (uint8_t)(180.0f + t * 75.0f);
            b = (uint8_t)(t * 255.0f);
        }
        s_ironbow_lut[i] = lv_color_make(r, g, b).full;
    }
}

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
 * @brief 在指定坐标打上带色彩的十字准星
 */
static inline void draw_marker_crosshair(uint16_t *buf, int cx, int cy, int w, int h, uint16_t color)
{
    for (int d = -5; d <= 5; d++) {
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

    uint16_t *ir_frame_data    = (uint16_t *)calloc(834, sizeof(uint16_t));
    uint16_t *eerom_ir_data    = (uint16_t *)calloc(832, sizeof(uint16_t));
    float *ir_temp_data        = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    paramsMLX90640 *mlx_params = (paramsMLX90640 *)calloc(1, sizeof(paramsMLX90640));

    float *filtered_temp       = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    uint8_t *norm_matrix       = (uint8_t *)calloc(MLX_RAW_PIXELS, sizeof(uint8_t));

    if (!ir_frame_data || !eerom_ir_data || !ir_temp_data || !mlx_params || !filtered_temp || !norm_matrix) {
        ESP_LOGE(TAG, "Critical: calloc failed, Out of memory!");
        vTaskDelete(NULL);
    }

    MLX90640_DumpEE(MLX90640_ADDR, eerom_ir_data);
    MLX90640_ExtractParameters(eerom_ir_data, mlx_params);
    free(eerom_ir_data);

    char text_info[96];
    char text_fps[32];
    char text_max_tag[16];
    char text_min_tag[16];

    uint32_t frame_count = 0;
    float real_fps = 0.0f;
    int64_t last_time_us = esp_timer_get_time();

    float smooth_min_t = 24.0f;
    float smooth_max_t = 34.0f;
    bool range_initialized = false;

    while (1) 
    {
        int status = MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);
        if (status >= 0) {
            int subpage = ir_frame_data[833]; 

            float ta = MLX90640_GetTa(ir_frame_data, mlx_params);
            MLX90640_CalculateTo(ir_frame_data, mlx_params, emissivity, ta - TA_SHIFT, ir_temp_data);
            frame_count++;

            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                int r = i / MLX_RAW_W;
                int c = i % MLX_RAW_W;

                if (((r + c) & 1) == subpage) {
                    if (ir_temp_data[i] > 1.0f && ir_temp_data[i] < 200.0f) {
                        if (filtered_temp[i] < 1.0f) {
                            filtered_temp[i] = ir_temp_data[i];
                        } else {
                            filtered_temp[i] = 0.65f * ir_temp_data[i] + 0.35f * filtered_temp[i];
                        }
                    }
                }
            }

            // 统计极值与定位坐标
            float cur_min = 200.0f, cur_max = -50.0f;
            int max_idx = 0, min_idx = 0;
            int valid_pts = 0;

            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                if (filtered_temp[i] > 1.0f && filtered_temp[i] < 200.0f) {
                    if (filtered_temp[i] < cur_min) {
                        cur_min = filtered_temp[i];
                        min_idx = i;
                    }
                    if (filtered_temp[i] > cur_max) {
                        cur_max = filtered_temp[i];
                        max_idx = i;
                    }
                    valid_pts++;
                }
            }

            // 计算 FPS
            int64_t now_us = esp_timer_get_time();
            int64_t diff_us = now_us - last_time_us;
            if (diff_us >= 1000000) {
                real_fps = (float)frame_count * 1000000.0f / (float)diff_us;
                frame_count = 0;
                last_time_us = now_us;
            }

            float center_t = filtered_temp[12 * MLX_RAW_W + 16];

            // 格式化各栏目文本
            snprintf(text_fps, sizeof(text_fps), "FPS: %.1f", real_fps);
            snprintf(text_info, sizeof(text_info),
                     "Max:%.1f  Min:%.1f\nCen:%.1f  ΔT:%.1f",
                     cur_max, cur_min, center_t, (cur_max - cur_min));
            snprintf(text_max_tag, sizeof(text_max_tag), "▲%.1f", cur_max);
            snprintf(text_min_tag, sizeof(text_min_tag), "▼%.1f", cur_min);

            // 将 32x24 矩阵索引映射为 240x180 画面像素坐标
            int max_raw_r = max_idx / MLX_RAW_W;
            int max_raw_c = max_idx % MLX_RAW_W;
            int max_px_x = (max_raw_c * (MLX_INTERP_W - 1)) / (MLX_RAW_W - 1);
            int max_px_y = (max_raw_r * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);

            int min_raw_r = min_idx / MLX_RAW_W;
            int min_raw_c = min_idx % MLX_RAW_W;
            int min_px_x = (min_raw_c * (MLX_INTERP_W - 1)) / (MLX_RAW_W - 1);
            int min_px_y = (min_raw_r * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);

            if (valid_pts > 400) {
                if ((cur_max - cur_min) < 3.0f) {
                    cur_max = cur_min + 3.0f;
                }

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

                uint16_t *cur_write_buf = s_display_buf[s_back_idx];

                // 双线性插值
                bilinear_scale_to_rgb565(norm_matrix, MLX_RAW_W, MLX_RAW_H,
                                         cur_write_buf, MLX_INTERP_W, MLX_INTERP_H,
                                         s_ironbow_lut);

                // 绘制 3 组指示十字准星
                draw_marker_crosshair(cur_write_buf, MLX_INTERP_W / 2, MLX_INTERP_H / 2, MLX_INTERP_W, MLX_INTERP_H, 0xFFFF); // 白色中心
                draw_marker_crosshair(cur_write_buf, max_px_x, max_px_y, MLX_INTERP_W, MLX_INTERP_H, 0xF800); // 红色最高温
                draw_marker_crosshair(cur_write_buf, min_px_x, min_px_y, MLX_INTERP_W, MLX_INTERP_H, 0x07FF); // 青蓝最低温

                // 刷新 UI 与动态坐标追踪
                if (example_lvgl_lock(100)) {
                    s_thermal_img_dsc.data = (const uint8_t *)cur_write_buf;
                    if (img_thermal) {
                        lv_img_set_src(img_thermal, &s_thermal_img_dsc);
                        lv_obj_invalidate(img_thermal);
                    }
                    if (label_status_right) {
                        lv_label_set_text(label_status_right, text_fps);
                    }
                    if (label_info) {
                        lv_label_set_text(label_info, text_info);
                    }

                    // 最高温标签防溢出贴靠 (图像 y 起始于 24)
                    if (tag_max) {
                        int pos_x = (max_px_x > 185) ? (max_px_x - 45) : (max_px_x + 6);
                        int pos_y = 24 + ((max_px_y < 12) ? (max_px_y + 6) : (max_px_y - 12));
                        lv_label_set_text(tag_max, text_max_tag);
                        lv_obj_set_pos(tag_max, pos_x, pos_y);
                    }

                    // 最低温标签防溢出贴靠
                    if (tag_min) {
                        int pos_x = (min_px_x > 185) ? (min_px_x - 45) : (min_px_x + 6);
                        int pos_y = 24 + ((min_px_y < 12) ? (min_px_y + 6) : (min_px_y - 12));
                        lv_label_set_text(tag_min, text_min_tag);
                        lv_obj_set_pos(tag_min, pos_x, pos_y);
                    }

                    example_lvgl_unlock();

                    if (s_display_buf[0] != s_display_buf[1]) {
                        s_back_idx = 1 - s_back_idx;
                    }
                }
            }
        }

        if (status >= 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}