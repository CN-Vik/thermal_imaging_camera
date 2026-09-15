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

/*
 * Thermal display tuning
 *
 * IMPORTANT:
 * The image color mapping is intentionally FIXED by default.
 * This prevents tiny MLX90640 measurement noise from being amplified
 * by a frame-by-frame min/max auto-range.
 *
 * For a normal indoor scene, 20~35C is a good starting window.
 * Change only these two values when you need another measurement range.
 */
#define THERMAL_USE_AUTO_RANGE          0
#define THERMAL_FIXED_MIN_C            20.0f
#define THERMAL_FIXED_MAX_C            35.0f

/* Very small changes are mostly sensor noise; smooth them strongly. */
#define TEMP_FILTER_ALPHA_SMALL         0.12f
#define TEMP_FILTER_ALPHA_MEDIUM        0.35f
#define TEMP_FILTER_ALPHA_LARGE         0.80f
#define TEMP_FILTER_MEDIUM_THRESHOLD     0.50f
#define TEMP_FILTER_LARGE_THRESHOLD      1.50f

/* If auto-range is enabled, it moves very slowly and keeps a wide margin. */
#define AUTO_RANGE_MIN_SPAN_C            6.0f
#define AUTO_RANGE_PADDING_C             1.0f
#define AUTO_RANGE_ALPHA                 0.03f

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

/* Pre-calculated fixed-point parameters for 1:1 center crop scaling. */
static int32_t s_x_start_fp = 0;
static uint32_t s_scale_fp = 0;

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

    s_scale_fp = ((MLX_RAW_H - 1) << 16) / (MLX_INTERP_H - 1);
    s_x_start_fp = ((MLX_RAW_W - 1) << 15)
                 - (int32_t)(((MLX_INTERP_W - 1) * s_scale_fp) / 2);

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
        } else if (norm < 0.50f) {
            float t = (norm - 0.25f) * 4.0f;
            r = (uint8_t)(40.0f + t * 180.0f);
            g = 0;
            b = (uint8_t)(160.0f - t * 100.0f);
        } else if (norm < 0.75f) {
            float t = (norm - 0.50f) * 4.0f;
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

/*
 * Natural spatial filter.
 * Center 8, 4-neighbor 1.5, diagonal 0.5 => total weight 16.
 */
static void spatial_filter_natural(const float *src, float *dst, int w, int h)
{
    for (int r = 0; r < h; r++) {
        int r_prev = (r > 0) ? (r - 1) : r;
        int r_next = (r < h - 1) ? (r + 1) : r;

        for (int c = 0; c < w; c++) {
            /* Fixed boundary handling: never use c-1 when c==0,
             * and never use c-1 as a fake right neighbor at the last column. */
            int c_prev = (c > 0) ? (c - 1) : c;
            int c_next = (c < w - 1) ? (c + 1) : c;

            float sum = src[r * w + c] * 8.0f
                      + (src[r_prev * w + c]
                       + src[r_next * w + c]
                       + src[r * w + c_prev]
                       + src[r * w + c_next]) * 1.5f
                      + (src[r_prev * w + c_prev]
                       + src[r_prev * w + c_next]
                       + src[r_next * w + c_prev]
                       + src[r_next * w + c_next]) * 0.5f;

            dst[r * w + c] = sum * (1.0f / 16.0f);
        }
    }
}

static void bilinear_scale_fullscreen_1to1(const uint8_t *src, int src_w, int src_h,
                                           uint16_t *dst, int dst_w, int dst_h,
                                           const uint16_t *lut)
{
    for (int y = 0; y < dst_h; y++) {
        uint32_t y_fp = y * s_scale_fp;
        int y0 = y_fp >> 16;
        int y1 = (y0 < src_h - 1) ? (y0 + 1) : y0;
        uint32_t y_diff = y_fp & 0xFFFF;
        uint32_t y_diff_inv = 65536U - y_diff;

        const uint8_t *p_y0 = &src[y0 * src_w];
        const uint8_t *p_y1 = &src[y1 * src_w];
        uint16_t *dst_row = &dst[y * dst_w];

        int32_t x_fp = s_x_start_fp;

        for (int x = 0; x < dst_w; x++) {
            int x0 = x_fp >> 16;
            int x1 = (x0 < src_w - 1) ? x0 + 1 : x0;

            /* The current crop geometry keeps x0 inside the sensor width.
             * Clamp defensively so an edge case can never read out of range. */
            if (x0 < 0) x0 = 0;
            if (x0 >= src_w) x0 = src_w - 1;
            if (x1 < 0) x1 = 0;
            if (x1 >= src_w) x1 = src_w - 1;

            uint32_t x_diff = x_fp & 0xFFFF;
            uint32_t x_diff_inv = 65536U - x_diff;

            uint32_t top = (p_y0[x0] * x_diff_inv + p_y0[x1] * x_diff) >> 16;
            uint32_t bot = (p_y1[x0] * x_diff_inv + p_y1[x1] * x_diff) >> 16;

            uint32_t val = (top * y_diff_inv + bot * y_diff) >> 16;
            if (val > 255U) val = 255U;

            dst_row[x] = lut[val];
            x_fp += s_scale_fp;
        }
    }
}

static inline void draw_diamond_marker(uint16_t *buf, int cx, int cy,
                                       int w, int h, int r, uint16_t color)
{
    for (int i = 0; i <= r; i++) {
        int j = r - i;

        if (cx + i < w && cy + j < h && cx + i >= 0 && cy + j >= 0)
            buf[(cy + j) * w + (cx + i)] = color;

        if (cx - i < w && cy + j < h && cx - i >= 0 && cy + j >= 0)
            buf[(cy + j) * w + (cx - i)] = color;

        if (cx + i < w && cy - j < h && cx + i >= 0 && cy - j >= 0)
            buf[(cy - j) * w + (cx + i)] = color;

        if (cx - i < w && cy - j < h && cx - i >= 0 && cy - j >= 0)
            buf[(cy - j) * w + (cx - i)] = color;
    }

    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
        buf[cy * w + cx] = color;
    }
}

static inline float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/*
 * Convert temperature to an 8-bit color index.
 * Default mode is fixed range to prevent color breathing/flicker.
 */
static void temperature_to_norm_u8(const float *src, uint8_t *dst,
                                   float smooth_min_t, float smooth_max_t)
{
    float map_min;
    float map_max;

#if THERMAL_USE_AUTO_RANGE
    map_min = smooth_min_t;
    map_max = smooth_max_t;
#else
    (void)smooth_min_t;
    (void)smooth_max_t;
    map_min = THERMAL_FIXED_MIN_C;
    map_max = THERMAL_FIXED_MAX_C;
#endif

    float delta = map_max - map_min;
    if (delta < 1.0f) {
        delta = 1.0f;
    }

    float range_inv = 255.0f / delta;

    for (int i = 0; i < MLX_RAW_PIXELS; i++) {
        float val = (src[i] - map_min) * range_inv;
        val = clampf_local(val, 0.0f, 255.0f);
        dst[i] = (uint8_t)(val + 0.5f);
    }
}

void ir_camera_task_main(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "IR camera task started on Core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "Thermal map: fixed range %.1fC ~ %.1fC, auto_range=%d",
             THERMAL_FIXED_MIN_C, THERMAL_FIXED_MAX_C, THERMAL_USE_AUTO_RANGE);

    init_ironbow_lut();

    MLX90640_I2CInit();
    MLX90640_SetRefreshRate(MLX90640_ADDR, RefreshRate);
    MLX90640_SetChessMode(MLX90640_ADDR);

    uint16_t *ir_frame_data = (uint16_t *)calloc(834, sizeof(uint16_t));
    uint16_t *eerom_ir_data = (uint16_t *)calloc(832, sizeof(uint16_t));
    float *ir_temp_data = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    paramsMLX90640 *mlx_params = (paramsMLX90640 *)calloc(1, sizeof(paramsMLX90640));

    /* Two true subpage buffers.  A displayed frame is built only after both
     * subpages have been received, instead of mixing a new half-frame with
     * an older half-frame on every I2C read. */
    float *subpage_temp[2] = {
        (float *)calloc(MLX_RAW_PIXELS, sizeof(float)),
        (float *)calloc(MLX_RAW_PIXELS, sizeof(float))
    };

    float *raw_full_matrix = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    float *filtered_temp = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    float *spatial_temp = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    uint8_t *norm_matrix = (uint8_t *)calloc(MLX_RAW_PIXELS, sizeof(uint8_t));

    if (!ir_frame_data || !eerom_ir_data || !ir_temp_data || !mlx_params ||
        !subpage_temp[0] || !subpage_temp[1] || !raw_full_matrix ||
        !filtered_temp || !spatial_temp || !norm_matrix) {
        ESP_LOGE(TAG, "Critical: calloc failed, Out of memory!");
        free(ir_frame_data);
        free(eerom_ir_data);
        free(ir_temp_data);
        free(mlx_params);
        free(subpage_temp[0]);
        free(subpage_temp[1]);
        free(raw_full_matrix);
        free(filtered_temp);
        free(spatial_temp);
        free(norm_matrix);
        vTaskDelete(NULL);
        return;
    }

    MLX90640_DumpEE(MLX90640_ADDR, eerom_ir_data);
    MLX90640_ExtractParameters(eerom_ir_data, mlx_params);
    free(eerom_ir_data);
    eerom_ir_data = NULL;

    char text_info[96];
    char text_fps[32];
    char text_max_tag[16];
    char text_min_tag[16];

    uint32_t frame_count = 0;
    float real_fps = 0.0f;
    int64_t last_time_us = esp_timer_get_time();

    float smooth_min_t = THERMAL_FIXED_MIN_C;
    float smooth_max_t = THERMAL_FIXED_MAX_C;
    bool range_initialized = false;

    bool subpage_ready[2] = {false, false};

    while (1) {
        int status = MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);

        if (status >= 0) {
            int subpage = ir_frame_data[833];

            if (subpage < 0 || subpage > 1) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            float ta = MLX90640_GetTa(ir_frame_data, mlx_params);
            MLX90640_CalculateTo(ir_frame_data, mlx_params,
                                 emissivity, ta - TA_SHIFT, ir_temp_data);
            frame_count++;

            /* ------------------------------------------------------------
             * 1. Save only the currently refreshed physical subpage.
             * ------------------------------------------------------------ */
            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                int r = i / MLX_RAW_W;
                int c = i % MLX_RAW_W;

                if (((r + c) & 1) == subpage) {
                    float t = ir_temp_data[i];
                    if (t > 0.0f && t < 300.0f && isfinite(t)) {
                        subpage_temp[subpage][i] = t;
                    }
                }
            }

            subpage_ready[subpage] = true;

            /* Do not process/display a frame until both subpages are fresh. */
            if (!(subpage_ready[0] && subpage_ready[1])) {
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            /* ------------------------------------------------------------
             * 2. Assemble a truly complete 32x24 frame from two fresh
             *    subpages. No more "current subpage + previous frame"
             *    blending here.
             * ------------------------------------------------------------ */
            for (int r = 0; r < MLX_RAW_H; r++) {
                for (int c = 0; c < MLX_RAW_W; c++) {
                    int idx = r * MLX_RAW_W + c;
                    int page = (r + c) & 1;
                    raw_full_matrix[idx] = subpage_temp[page][idx];
                }
            }

            /* The two subpages have now both contributed to one complete
             * displayed frame. The next display frame must receive a fresh
             * sample from both pages again. */
            subpage_ready[0] = false;
            subpage_ready[1] = false;

            /* ------------------------------------------------------------
             * 3. Mild checkerboard mismatch suppression.
             *    Keep this conservative; its job is only to suppress a
             *    subpage boundary outlier, not to blur real hot spots.
             * ------------------------------------------------------------ */
            for (int r = 0; r < MLX_RAW_H; r++) {
                for (int c = 0; c < MLX_RAW_W; c++) {
                    int idx = r * MLX_RAW_W + c;
                    float sum = 0.0f;
                    int count = 0;

                    if (r > 0) {
                        sum += raw_full_matrix[(r - 1) * MLX_RAW_W + c];
                        count++;
                    }
                    if (r < MLX_RAW_H - 1) {
                        sum += raw_full_matrix[(r + 1) * MLX_RAW_W + c];
                        count++;
                    }
                    if (c > 0) {
                        sum += raw_full_matrix[r * MLX_RAW_W + (c - 1)];
                        count++;
                    }
                    if (c < MLX_RAW_W - 1) {
                        sum += raw_full_matrix[r * MLX_RAW_W + (c + 1)];
                        count++;
                    }

                    if (count > 0) {
                        float avg = sum / (float)count;
                        float diff = fabsf(raw_full_matrix[idx] - avg);

                        if (diff > 1.0f) {
                            float blend = (diff - 1.0f) / 2.0f;
                            if (blend > 0.50f) blend = 0.50f;
                            raw_full_matrix[idx] =
                                (1.0f - blend) * raw_full_matrix[idx]
                                + blend * avg;
                        }
                    }
                }
            }

            /* ------------------------------------------------------------
             * 4. Temporal filtering.
             *    - Tiny changes: strong smoothing.
             *    - Medium changes: moderate response.
             *    - Large changes: fast response.
             * ------------------------------------------------------------ */
            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                if (filtered_temp[i] < 1.0f || !isfinite(filtered_temp[i])) {
                    filtered_temp[i] = raw_full_matrix[i];
                    continue;
                }

                float d = fabsf(raw_full_matrix[i] - filtered_temp[i]);
                float alpha;

                if (d > TEMP_FILTER_LARGE_THRESHOLD) {
                    alpha = TEMP_FILTER_ALPHA_LARGE;
                } else if (d > TEMP_FILTER_MEDIUM_THRESHOLD) {
                    alpha = TEMP_FILTER_ALPHA_MEDIUM;
                } else {
                    alpha = TEMP_FILTER_ALPHA_SMALL;
                }

                filtered_temp[i] =
                    alpha * raw_full_matrix[i]
                    + (1.0f - alpha) * filtered_temp[i];
            }

            /* ------------------------------------------------------------
             * 5. Mild spatial denoise.
             * ------------------------------------------------------------ */
            spatial_filter_natural(filtered_temp, spatial_temp,
                                   MLX_RAW_W, MLX_RAW_H);

            /* ------------------------------------------------------------
             * 6. Statistics for HUD / markers only.
             *    These values DO NOT control the color mapping in the
             *    default fixed-range mode.
             * ------------------------------------------------------------ */
            float cur_min = 200.0f;
            float cur_max = -50.0f;
            int max_idx = 0;
            int min_idx = 0;
            int valid_pts = 0;

            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                float val = spatial_temp[i];

                if (val > 1.0f && val < 200.0f && isfinite(val)) {
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

            if (valid_pts <= 0) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            /* ------------------------------------------------------------
             * 7. FPS
             * ------------------------------------------------------------ */
            int64_t now_us = esp_timer_get_time();
            int64_t diff_us = now_us - last_time_us;

            if (diff_us >= 1000000) {
                real_fps = (float)frame_count * 1000000.0f / (float)diff_us;
                frame_count = 0;
                last_time_us = now_us;
            }

            float center_t = spatial_temp[12 * MLX_RAW_W + 16];

            snprintf(text_fps, sizeof(text_fps), "%.1f FPS", real_fps);
            snprintf(text_info, sizeof(text_info),
                     "MAX: %.1f C    MIN: %.1f C\nCEN: %.1f C    DT: %.1f C",
                     cur_max, cur_min, center_t, (cur_max - cur_min));
            snprintf(text_max_tag, sizeof(text_max_tag), "MAX %.1f", cur_max);
            snprintf(text_min_tag, sizeof(text_min_tag), "MIN %.1f", cur_min);

            /* ------------------------------------------------------------
             * 8. Optional slow auto-range.
             *    Disabled by default because fixed-range rendering is much
             *    more stable for a static indoor scene.
             * ------------------------------------------------------------ */
#if THERMAL_USE_AUTO_RANGE
            float target_min = cur_min - AUTO_RANGE_PADDING_C;
            float target_max = cur_max + AUTO_RANGE_PADDING_C;

            float target_span = target_max - target_min;
            if (target_span < AUTO_RANGE_MIN_SPAN_C) {
                float center = 0.5f * (target_min + target_max);
                target_min = center - 0.5f * AUTO_RANGE_MIN_SPAN_C;
                target_max = center + 0.5f * AUTO_RANGE_MIN_SPAN_C;
            }

            if (!range_initialized) {
                smooth_min_t = target_min;
                smooth_max_t = target_max;
                range_initialized = true;
            } else {
                smooth_min_t += AUTO_RANGE_ALPHA * (target_min - smooth_min_t);
                smooth_max_t += AUTO_RANGE_ALPHA * (target_max - smooth_max_t);
            }
#endif

            if (valid_pts > 400) {
                /* --------------------------------------------------------
                 * 9. Temperature -> 0..255 mapping.
                 * -------------------------------------------------------- */
                temperature_to_norm_u8(spatial_temp, norm_matrix,
                                       smooth_min_t, smooth_max_t);

                uint16_t *cur_write_buf = s_display_buf[s_back_idx];

                /* --------------------------------------------------------
                 * 10. 32x24 -> 240x280 bilinear scaling and color LUT.
                 * -------------------------------------------------------- */
                bilinear_scale_fullscreen_1to1(
                    norm_matrix,
                    MLX_RAW_W,
                    MLX_RAW_H,
                    cur_write_buf,
                    MLX_INTERP_W,
                    MLX_INTERP_H,
                    s_ironbow_lut);

                /* --------------------------------------------------------
                 * 11. Markers.
                 * -------------------------------------------------------- */
                int max_raw_r = max_idx / MLX_RAW_W;
                int max_raw_c = max_idx % MLX_RAW_W;
                int max_px_y = (max_raw_r * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);
                int max_px_x = (int)(((max_raw_c << 16) - s_x_start_fp) / s_scale_fp);

                if (max_px_x < 0) max_px_x = 0;
                if (max_px_x >= MLX_INTERP_W) max_px_x = MLX_INTERP_W - 1;

                int min_raw_r = min_idx / MLX_RAW_W;
                int min_raw_c = min_idx % MLX_RAW_W;
                int min_px_y = (min_raw_r * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);
                int min_px_x = (int)(((min_raw_c << 16) - s_x_start_fp) / s_scale_fp);

                if (min_px_x < 0) min_px_x = 0;
                if (min_px_x >= MLX_INTERP_W) min_px_x = MLX_INTERP_W - 1;

                draw_diamond_marker(cur_write_buf, max_px_x, max_px_y,
                                    MLX_INTERP_W, MLX_INTERP_H, 5, 0xF800);
                draw_diamond_marker(cur_write_buf, min_px_x, min_px_y,
                                    MLX_INTERP_W, MLX_INTERP_H, 5, 0x07FF);
                draw_diamond_marker(cur_write_buf,
                                    MLX_INTERP_W / 2,
                                    MLX_INTERP_H / 2,
                                    MLX_INTERP_W,
                                    MLX_INTERP_H,
                                    3,
                                    0xFFFF);

                /* --------------------------------------------------------
                 * 12. LVGL update.
                 * -------------------------------------------------------- */
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

                    if (tag_max) {
                        int pos_x = (max_px_x > 175) ? (max_px_x - 58) : (max_px_x + 8);
                        int pos_y = (max_px_y < 45)
                                  ? (max_px_y + 10)
                                  : ((max_px_y > 215) ? (max_px_y - 18) : (max_px_y - 8));
                        lv_label_set_text(tag_max, text_max_tag);
                        lv_obj_set_pos(tag_max, pos_x, pos_y);
                    }

                    if (tag_min) {
                        int pos_x = (min_px_x > 175) ? (min_px_x - 58) : (min_px_x + 8);
                        int pos_y = (min_px_y < 45)
                                  ? (min_px_y + 10)
                                  : ((min_px_y > 215) ? (min_px_y - 18) : (min_px_y - 8));
                        lv_label_set_text(tag_min, text_min_tag);
                        lv_obj_set_pos(tag_min, pos_x, pos_y);
                    }

                    if (s_display_buf[0] != s_display_buf[1]) {
                        s_back_idx = 1U - s_back_idx;
                    }

                    example_lvgl_unlock();
                }
            }
        }

        /* MLX90640 is configured for 16Hz in ir_mlx90640_app.h.
         * A short task delay avoids hammering I2C while keeping the response
         * fast enough for a thermal display. */
        if (status >= 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
