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
            g = 0;
            b = (uint8_t)(t * 160.0f);
        } else if (norm < 0.5f) {
            float t = (norm - 0.25f) * 4.0f;
            r = (uint8_t)(40.0f + t * 180.0f);
            g = 0;
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
 * @brief 绘制专业空心菱形（Diamond ◆）追踪点，中心保留清晰单点
 */
static inline void draw_diamond_marker(uint16_t *buf, int cx, int cy, int w, int h, int r, uint16_t color)
{
    for (int i = 0; i <= r; i++) {
        int j = r - i;
        if (cx + i < w && cy + j < h && cx + i >= 0 && cy + j >= 0) buf[(cy + j) * w + (cx + i)] = color;
        if (cx - i < w && cy + j < h && cx - i >= 0 && cy + j >= 0) buf[(cy + j) * w + (cx - i)] = color;
        if (cx + i < w && cy - j < h && cx + i >= 0 && cy - j >= 0) buf[(cy - j) * w + (cx + i)] = color;
        if (cx - i < w && cy - j < h && cx - i >= 0 && cy - j >= 0) buf[(cy - j) * w + (cx - i)] = color;
    }
    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
        buf[cy * w + cx] = color;
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

    float *raw_full_matrix     = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    float *filtered_temp       = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    uint8_t *norm_matrix       = (uint8_t *)calloc(MLX_RAW_PIXELS, sizeof(uint8_t));

    if (!ir_frame_data || !eerom_ir_data || !ir_temp_data || !mlx_params || !raw_full_matrix || !filtered_temp || !norm_matrix) {
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

            // 1. 刷新当前子页真实测得的温度
            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                int r = i / MLX_RAW_W;
                int c = i % MLX_RAW_W;

                if (((r + c) & 1) == subpage) {
                    if (ir_temp_data[i] > 0.0f && ir_temp_data[i] < 300.0f) {
                        raw_full_matrix[i] = ir_temp_data[i];
                    }
                }
            }

            // 2. 运动自适应棋盘解交织（彻底铲除移动边缘的拉链马赛克）
            // 对于未更新的子页像素，提取其四周 4 个正交邻居（这 4 个邻居必然属于当前子页的最新测量值）
            for (int r = 0; r < MLX_RAW_H; r++) {
                for (int c = 0; c < MLX_RAW_W; c++) {
                    if (((r + c) & 1) != subpage) {
                        int idx = r * MLX_RAW_W + c;
                        float sum = 0.0f;
                        int count = 0;
                        if (r > 0)             { sum += raw_full_matrix[(r - 1) * MLX_RAW_W + c]; count++; }
                        if (r < MLX_RAW_H - 1) { sum += raw_full_matrix[(r + 1) * MLX_RAW_W + c]; count++; }
                        if (c > 0)             { sum += raw_full_matrix[r * MLX_RAW_W + (c - 1)]; count++; }
                        if (c < MLX_RAW_W - 1) { sum += raw_full_matrix[r * MLX_RAW_W + (c + 1)]; count++; }

                        float avg_n = (count > 0) ? (sum / (float)count) : raw_full_matrix[idx];
                        float diff = fabsf(raw_full_matrix[idx] - avg_n);

                        // 温差大于 1.0°C 说明发生了镜头晃动或边缘位移，立刻向最新邻居相位动态过渡！
                        if (diff > 1.0f) {
                            float blend = (diff - 1.0f) / 2.0f;
                            if (blend > 0.85f) blend = 0.85f;
                            raw_full_matrix[idx] = (1.0f - blend) * raw_full_matrix[idx] + blend * avg_n;
                        }
                    }
                }
            }

            // 3. 极速响应自适应时域降噪（移动时瞬间跟手，静止时深度降噪）
            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                if (filtered_temp[i] < 1.0f) {
                    filtered_temp[i] = raw_full_matrix[i];
                } else {
                    float d = fabsf(raw_full_matrix[i] - filtered_temp[i]);
                    float alpha = (d > 1.8f) ? 0.90f : ((d > 0.6f) ? 0.55f : 0.20f);
                    filtered_temp[i] = alpha * raw_full_matrix[i] + (1.0f - alpha) * filtered_temp[i];
                }
            }

            // 4. 统计极值与定位（直接在未被模糊破坏的矩阵上计算，五指分离，杜绝长蹼）
            float cur_min = 200.0f, cur_max = -50.0f;
            int max_idx = 0, min_idx = 0;
            int valid_pts = 0;

            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                float val = filtered_temp[i];
                if (val > 1.0f && val < 200.0f) {
                    if (val < cur_min) {
                        cur_min = val;
                        min_idx = i;
                    }
                    if (val > cur_max) {
                        cur_max = val;
                        max_idx = i;
                    }
                    valid_pts++;
                }
            }

            // 5. 统计刷新率
            int64_t now_us = esp_timer_get_time();
            int64_t diff_us = now_us - last_time_us;
            if (diff_us >= 1000000) {
                real_fps = (float)frame_count * 1000000.0f / (float)diff_us;
                frame_count = 0;
                last_time_us = now_us;
            }

            float center_t = filtered_temp[12 * MLX_RAW_W + 16];

            // 格式化文本
            snprintf(text_fps, sizeof(text_fps), "%.1f FPS", real_fps);
            snprintf(text_info, sizeof(text_info),
                     "MAX: %.1f°C    MIN: %.1f°C\nCEN: %.1f°C    ΔT: %.1f°C",
                     cur_max, cur_min, center_t, (cur_max - cur_min));
            snprintf(text_max_tag, sizeof(text_max_tag), "◆ %.1f", cur_max);
            snprintf(text_min_tag, sizeof(text_min_tag), "◆ %.1f", cur_min);

            // 映射到 240x180 画面像素坐标（严格 1:1，杜绝脸部拉伸）
            int max_px_x = ((max_idx % MLX_RAW_W) * (MLX_INTERP_W - 1)) / (MLX_RAW_W - 1);
            int max_px_y = ((max_idx / MLX_RAW_W) * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);

            int min_px_x = ((min_idx % MLX_RAW_W) * (MLX_INTERP_W - 1)) / (MLX_RAW_W - 1);
            int min_px_y = ((min_idx / MLX_RAW_W) * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);

            if (valid_pts > 400) {
                // 动态温差视距保护（温差小于 3.5°C 强制拉开，防止对准白墙噪点翻滚）
                if ((cur_max - cur_min) < 3.5f) {
                    cur_max = cur_min + 3.5f;
                }

                if (!range_initialized) {
                    smooth_min_t = cur_min;
                    smooth_max_t = cur_max;
                    range_initialized = true;
                } else {
                    smooth_min_t = 0.20f * cur_min + 0.80f * smooth_min_t;
                    smooth_max_t = 0.20f * cur_max + 0.80f * smooth_max_t;
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

                // 6. 双线性插值输出到后台缓冲
                bilinear_scale_to_rgb565(norm_matrix, MLX_RAW_W, MLX_RAW_H,
                                         cur_write_buf, MLX_INTERP_W, MLX_INTERP_H,
                                         s_ironbow_lut);

                // 7. 在显存中绘制工业空心菱形（Diamond ◆）
                draw_diamond_marker(cur_write_buf, max_px_x, max_px_y, MLX_INTERP_W, MLX_INTERP_H, 5, 0xF800); // 红色最高温
                draw_diamond_marker(cur_write_buf, min_px_x, min_px_y, MLX_INTERP_W, MLX_INTERP_H, 5, 0x07FF); // 蓝色最低温
                draw_diamond_marker(cur_write_buf, MLX_INTERP_W / 2, MLX_INTERP_H / 2, MLX_INTERP_W, MLX_INTERP_H, 3, 0xFFFF); // 白色中心

                // 8. 原子切换显存并刷新 UI
                if (example_lvgl_lock(100)) {
                    s_thermal_img_dsc.data = (const uint8_t *)cur_write_buf;
                    if (img_thermal) {
                        lv_img_set_src(img_thermal, &s_thermal_img_dsc);
                        lv_obj_invalidate(img_thermal);
                    }
                    if (label_fps_float) {
                        lv_label_set_text(label_fps_float, text_fps);
                    }
                    if (label_info) {
                        lv_label_set_text(label_info, text_info);
                    }

                    // 动态跟随气泡：画面物理起始 y = 28
                    if (tag_max) {
                        int pos_x = (max_px_x > 175) ? (max_px_x - 56) : (max_px_x + 8);
                        int pos_y = 28 + ((max_px_y < 20) ? (max_px_y + 8) : (max_px_y - 14));
                        lv_label_set_text(tag_max, text_max_tag);
                        lv_obj_set_pos(tag_max, pos_x, pos_y);
                    }

                    if (tag_min) {
                        int pos_x = (min_px_x > 175) ? (min_px_x - 56) : (min_px_x + 8);
                        int pos_y = 28 + ((min_px_y < 20) ? (min_px_y + 8) : (min_px_y - 14));
                        lv_label_set_text(tag_min, text_min_tag);
                        lv_obj_set_pos(tag_min, pos_x, pos_y);
                    }

                    example_lvgl_unlock();

                    // 双缓冲交替
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