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
 */
#define THERMAL_USE_AUTO_RANGE          1
#define THERMAL_FIXED_MIN_C            24.0f
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

/*
 * 图像方向校准开关（解决竖拍手从左侧横着出来的问题）：
 * IR_ROTATE_90 = 1 : 将传感器坐标系旋转 90°，适配竖屏 PCB 安装 (24宽 -> 240, 32高 -> 280)
 * IR_MIRROR_X  = 0 : 若发现屏幕里左右手方向反了（大拇指和小拇指左右颠倒），在 0 和 1 之间切换
 * IR_MIRROR_Y  = 1 : 默认 1，将原本在屏幕左侧(c=0)的手腕旋转到屏幕底部(y=279)
 */
#define IR_ROTATE_90                    1
#define IR_MIRROR_X                     0
#define IR_MIRROR_Y                     1

/* X/Y 独立全视野定点数缩放步进（不再裁切任何边缘像素） */
static uint32_t s_scale_x_fp = 0;
static uint32_t s_scale_y_fp = 0;

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

/*
 * 1. 纯净版棋盘格抑制（绝不原地修改 raw_temp_matrix，且只在平坦区消除奇偶场微差）
 */
static void despike_checkerboard(const float *src, float *dst, int w, int h)
{
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            int idx = r * w + c;
            float val = src[idx];

            if (r == 0 || r == h - 1 || c == 0 || c == w - 1) {
                dst[idx] = val;
                continue;
            }

            float n_up    = src[(r - 1) * w + c];
            float n_down  = src[(r + 1) * w + c];
            float n_left  = src[r * w + (c - 1)];
            float n_right = src[r * w + (c + 1)];

            // 计算四邻域自身的最大最小温差（判断当前是否处于物体边缘/指缝）
            float n_max = fmaxf(fmaxf(n_up, n_down), fmaxf(n_left, n_right));
            float n_min = fminf(fminf(n_up, n_down), fminf(n_left, n_right));

            // 只有当四邻域自身很平坦（温差 < 1.2℃），说明不是手指边缘，才进行棋盘格平滑！
            // 如果处于手指与背景/指缝的交界处，100% 保留原始锐利度！
            if ((n_max - n_min) < 1.2f) {
                float avg = 0.25f * (n_up + n_down + n_left + n_right);
                float diff = fabsf(val - avg);
                if (diff > 0.3f && diff < 1.5f) {
                    dst[idx] = 0.65f * val + 0.35f * avg;
                    continue;
                }
            }
            dst[idx] = val;
        }
    }
}

/*
 * 2. 双边保边滤波 + 指缝微锐化 (Edge-Preserving Bilateral + Unsharp Mask)
 *    - 温差小 (< 0.8℃)：平滑背景热噪
 *    - 温差大 (> 1.2℃)：判定为手指边缘/指缝，不但不模糊，反而做微弱反相锐化拉开差距！
 */
static void spatial_filter_sharp_hand(const float *src, float *dst, int w, int h)
{
    const float edge_thresh = 1.0f;   // 超过 1.0℃ 视为手指边界，禁止跨界模糊
    const float sharpen_gain = 0.22f; // 边缘锐化强度，帮助切开指缝趾蹼

    for (int r = 0; r < h; r++) {
        int r_prev = (r > 0) ? (r - 1) : r;
        int r_next = (r < h - 1) ? (r + 1) : r;

        for (int c = 0; c < w; c++) {
            int c_prev = (c > 0) ? (c - 1) : c;
            int c_next = (c < w - 1) ? (c + 1) : c;

            float center = src[r * w + c];
            float sum_smooth = center * 8.0f;
            float w_total = 8.0f;

            // 四邻域坐标值
            float nb[4] = {
                src[r_prev * w + c],
                src[r_next * w + c],
                src[r * w + c_prev],
                src[r * w + c_next]
            };

            float nb_avg = 0.25f * (nb[0] + nb[1] + nb[2] + nb[3]);

            for (int i = 0; i < 4; i++) {
                float diff = fabsf(nb[i] - center);
                if (diff < edge_thresh) {
                    // 同一物体内部（如手掌心或纯背景）：正常降噪平滑
                    float w_nb = 1.5f * (1.0f - diff / edge_thresh);
                    sum_smooth += nb[i] * w_nb;
                    w_total += w_nb;
                }
            }

            float base_val = sum_smooth / w_total;

            // 高频分量（当前点与四邻域均值的落差）：如果落差大（处于手指边缘或指缝谷底），进行反锐化掩模增强
            float high_freq = center - nb_avg;
            if (fabsf(high_freq) > 0.6f) {
                base_val += high_freq * sharpen_gain;
            }

            dst[r * w + c] = base_val;
        }
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

            float sum = src[r * w + c] * 12.0f
                    + (src[r_prev * w + c]
                    + src[r_next * w + c]
                    + src[r * w + c_prev]
                    + src[r * w + c_next]) * 0.75f
                    + (src[r_prev * w + c_prev]
                    + src[r_prev * w + c_next]
                    + src[r_next * w + c_prev]
                    + src[r_next * w + c_next]) * 0.25f;

            dst[r * w + c] = sum * (1.0f / 16.0f);
        }
    }
}


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

    // 将两个显存彻底清零为纯黑，防止开机雪花杂波
    memset(s_display_buf[0], 0, buf_size);
    if (s_display_buf[1] != s_display_buf[0]) {
        memset(s_display_buf[1], 0, buf_size);
    }

#if IR_ROTATE_90
    // 旋转 90° 后：传感器 24 行对应屏幕 240 宽，32 列对应屏幕 280 高
    s_scale_x_fp = ((MLX_RAW_H - 1) << 16) / (MLX_INTERP_W - 1);
    s_scale_y_fp = ((MLX_RAW_W - 1) << 16) / (MLX_INTERP_H - 1);
#else
    // 未旋转时：传感器 32 列完整对应屏幕 240 宽（放大 7.5 倍，不裁切），24 行对应 280 高
    s_scale_x_fp = ((MLX_RAW_W - 1) << 16) / (MLX_INTERP_W - 1);
    s_scale_y_fp = ((MLX_RAW_H - 1) << 16) / (MLX_INTERP_H - 1);
#endif

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
 * 将传感器原始行列 (raw_r, raw_c) 转换为旋转/镜像后的屏幕像素坐标 (px_x, px_y)
 * 确保 MAX / MIN 菱形准星和浮动标签与手部热像位置 100% 同步
 */
static inline void raw_rc_to_screen_xy(int raw_r, int raw_c, int *px_x, int *px_y)
{
#if IR_ROTATE_90
    int x = (raw_r * (MLX_INTERP_W - 1)) / (MLX_RAW_H - 1);
    int y = (raw_c * (MLX_INTERP_H - 1)) / (MLX_RAW_W - 1);
#else
    int x = (raw_c * (MLX_INTERP_W - 1)) / (MLX_RAW_W - 1);
    int y = (raw_r * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);
#endif

    if (IR_MIRROR_X) x = (MLX_INTERP_W - 1) - x;
    if (IR_MIRROR_Y) y = (MLX_INTERP_H - 1) - y;

    if (x < 0) x = 0;
    if (x >= MLX_INTERP_W) x = MLX_INTERP_W - 1;
    if (y < 0) y = 0;
    if (y >= MLX_INTERP_H) y = MLX_INTERP_H - 1;

    *px_x = x;
    *px_y = y;
}

/*
 * 全视野无裁切双线性插值（内置 90° 旋转与 X/Y 镜像校准）
 */
static void bilinear_scale_fullscreen_1to1(const uint8_t *src, int src_w, int src_h,
                                           uint16_t *dst, int dst_w, int dst_h,
                                           const uint16_t *lut)
{
    for (int y = 0; y < dst_h; y++) {
        int eff_y = IR_MIRROR_Y ? (dst_h - 1 - y) : y;
        uint16_t *dst_row = &dst[y * dst_w];

#if IR_ROTATE_90
        // 屏幕 Y 轴对应传感器列 c (0..31)
        uint32_t c_fp = eff_y * s_scale_y_fp;
        int c0 = c_fp >> 16;
        int c1 = (c0 < src_w - 1) ? (c0 + 1) : c0;
        uint32_t c_diff = c_fp & 0xFFFF;
        uint32_t c_diff_inv = 65536U - c_diff;

        for (int x = 0; x < dst_w; x++) {
            // 屏幕 X 轴对应传感器行 r (0..23)
            int eff_x = IR_MIRROR_X ? (dst_w - 1 - x) : x;
            uint32_t r_fp = eff_x * s_scale_x_fp;
            int r0 = r_fp >> 16;
            int r1 = (r0 < src_h - 1) ? (r0 + 1) : r0;
            uint32_t r_diff = r_fp & 0xFFFF;
            uint32_t r_diff_inv = 65536U - r_diff;

            const uint8_t *p_r0 = &src[r0 * src_w];
            const uint8_t *p_r1 = &src[r1 * src_w];

            uint32_t row0 = (p_r0[c0] * c_diff_inv + p_r0[c1] * c_diff) >> 16;
            uint32_t row1 = (p_r1[c0] * c_diff_inv + p_r1[c1] * c_diff) >> 16;
            uint32_t val  = (row0 * r_diff_inv + row1 * r_diff) >> 16;
            if (val > 255U) val = 255U;

            dst_row[x] = lut[val];
        }
#else
        // 未旋转模式：屏幕 Y 轴对应传感器行 r (0..23)，屏幕 X 轴对应列 c (0..31)
        uint32_t r_fp = eff_y * s_scale_y_fp;
        int r0 = r_fp >> 16;
        int r1 = (r0 < src_h - 1) ? (r0 + 1) : r0;
        uint32_t r_diff = r_fp & 0xFFFF;
        uint32_t r_diff_inv = 65536U - r_diff;

        const uint8_t *p_r0 = &src[r0 * src_w];
        const uint8_t *p_r1 = &src[r1 * src_w];

        for (int x = 0; x < dst_w; x++) {
            int eff_x = IR_MIRROR_X ? (dst_w - 1 - x) : x;
            uint32_t c_fp = eff_x * s_scale_x_fp;
            int c0 = c_fp >> 16;
            int c1 = (c0 < src_w - 1) ? (c0 + 1) : c0;
            uint32_t c_diff = c_fp & 0xFFFF;
            uint32_t c_diff_inv = 65536U - c_diff;

            uint32_t top = (p_r0[c0] * c_diff_inv + p_r0[c1] * c_diff) >> 16;
            uint32_t bot = (p_r1[c0] * c_diff_inv + p_r1[c1] * c_diff) >> 16;
            uint32_t val = (top * r_diff_inv + bot * r_diff) >> 16;
            if (val > 255U) val = 255U;

            dst_row[x] = lut[val];
        }
#endif
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

static void temperature_to_norm_u8(const float *src, uint8_t *dst,
                                   float smooth_min_t, float smooth_max_t)
{
    float map_min = THERMAL_USE_AUTO_RANGE ? smooth_min_t : THERMAL_FIXED_MIN_C;
    float map_max = THERMAL_USE_AUTO_RANGE ? smooth_max_t : THERMAL_FIXED_MAX_C;

    float delta = map_max - map_min;
    if (delta < 2.0f) {
        delta = 2.0f;
    }

    float range_inv = 1.0f / delta;

    for (int i = 0; i < MLX_RAW_PIXELS; i++) {
        float norm = (src[i] - map_min) * range_inv;
        norm = clampf_local(norm, 0.0f, 1.0f);

        // Gamma = 1.5 非线性对比度拉伸 (norm * sqrt(norm))
        // 压低中低温过渡带（指缝热辐射区），突出高温主体（手指核心区），消除趾蹼粘连感
        norm = norm * sqrtf(norm);

        dst[i] = (uint8_t)(norm * 255.0f + 0.5f);
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
    float *raw_temp_matrix  = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    paramsMLX90640 *mlx_params = (paramsMLX90640 *)calloc(1, sizeof(paramsMLX90640));

    float *filtered_temp = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    float *spatial_temp  = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    uint8_t *norm_matrix = (uint8_t *)calloc(MLX_RAW_PIXELS, sizeof(uint8_t));

    if (!ir_frame_data || !eerom_ir_data || !raw_temp_matrix || !mlx_params ||
        !filtered_temp || !spatial_temp || !norm_matrix) {
        ESP_LOGE(TAG, "Critical: calloc failed, Out of memory!");
        vTaskDelete(NULL);
        return;
    }

    MLX90640_DumpEE(MLX90640_ADDR, eerom_ir_data);
    MLX90640_ExtractParameters(eerom_ir_data, mlx_params);
    free(eerom_ir_data);
    eerom_ir_data = NULL;

    // 开机丢弃前 4 帧哑数据，等待传感器内部硬件 PTAT 和 VDD 稳定
    ESP_LOGI(TAG, "Warming up MLX90640 sensor...");
    for (int i = 0; i < 4; i++) {
        MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

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

    // 记录两个子场是否各自都完成过至少一次测量
    bool subpage_init[2] = {false, false};

    while (1) {
        int status = MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);

        if (status >= 0) {
            int subpage = ir_frame_data[833];
            if (subpage < 0 || subpage > 1) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            float ta = MLX90640_GetTa(ir_frame_data, mlx_params);

            // 1. 官方原生计算：每次只增量更新当前 subpage 的 384 个点，另一半保留上一帧的真实点
            MLX90640_CalculateTo(ir_frame_data, mlx_params,
                                 emissivity, ta - TA_SHIFT, raw_temp_matrix);

            // 调用官方坏点校准算法
            int mode = (ir_frame_data[832] & MLX90640_CTRL_MEAS_MODE_MASK) >> 5;
            MLX90640_BadPixelsCorrection(mlx_params->brokenPixels, raw_temp_matrix, mode, mlx_params);
            MLX90640_BadPixelsCorrection(mlx_params->outlierPixels, raw_temp_matrix, mode, mlx_params);

            subpage_init[subpage] = true;
            frame_count++;

            // 开机首轮：必须等 0 和 1 两个场全部采过一遍（确保 768 个点都有初始测量值）才送显
            if (!subpage_init[0] || !subpage_init[1]) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            // 2. 棋盘格抑制（输出到 spatial_temp 作为临时缓冲，绝不污染 raw_temp_matrix！）
            despike_checkerboard(raw_temp_matrix, spatial_temp, MLX_RAW_W, MLX_RAW_H);

            // 3. 时域响应平滑（输入改用刚去除了棋盘格的 spatial_temp）
            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                if (filtered_temp[i] < 1.0f || !isfinite(filtered_temp[i])) {
                    filtered_temp[i] = spatial_temp[i];
                    continue;
                }
                float d = fabsf(spatial_temp[i] - filtered_temp[i]);
                float alpha = (d > TEMP_FILTER_LARGE_THRESHOLD)  ? TEMP_FILTER_ALPHA_LARGE :
                              (d > TEMP_FILTER_MEDIUM_THRESHOLD) ? TEMP_FILTER_ALPHA_MEDIUM :
                                                                   TEMP_FILTER_ALPHA_SMALL;
                filtered_temp[i] = alpha * spatial_temp[i] + (1.0f - alpha) * filtered_temp[i];
            }

            // 4. 双边保边与指缝锐化滤波
            spatial_filter_sharp_hand(filtered_temp, spatial_temp, MLX_RAW_W, MLX_RAW_H);
            spatial_filter_natural(filtered_temp, spatial_temp, MLX_RAW_W, MLX_RAW_H); //[cite: 3]

            // 5. 统计极值
            float cur_min = 200.0f;
            float cur_max = -50.0f;
            int max_idx = 0;
            int min_idx = 0;
            int valid_pts = 0;

            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                float val = spatial_temp[i];
                if (val > 1.0f && val < 200.0f && isfinite(val)) {
                    if (val < cur_min) { cur_min = val; min_idx = i; }
                    if (val > cur_max) { cur_max = val; max_idx = i; }
                    valid_pts++;
                }
            }

            if (valid_pts <= 0) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

#if THERMAL_USE_AUTO_RANGE
            float target_min = cur_min - 0.5f;
            float target_max = cur_max + 0.5f;
            if ((target_max - target_min) < AUTO_RANGE_MIN_SPAN_C) {
                float mid = 0.5f * (target_max + target_min);
                target_min = mid - 0.5f * AUTO_RANGE_MIN_SPAN_C;
                target_max = mid + 0.5f * AUTO_RANGE_MIN_SPAN_C;
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

            // 6. 帧率统计与 HUD 字符串格式化
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

            // 7. 映射并进行双线性插值送显
            if (valid_pts > 400) {
                temperature_to_norm_u8(spatial_temp, norm_matrix, smooth_min_t, smooth_max_t);
                uint16_t *cur_write_buf = s_display_buf[s_back_idx];

                bilinear_scale_fullscreen_1to1(
                    norm_matrix, MLX_RAW_W, MLX_RAW_H,
                    cur_write_buf, MLX_INTERP_W, MLX_INTERP_H,
                    s_ironbow_lut);

                // 【已修改】：计算准星位置并绘制标记（自动跟随旋转与镜像设置）
                int max_px_x, max_px_y, min_px_x, min_px_y;
                raw_rc_to_screen_xy(max_idx / MLX_RAW_W, max_idx % MLX_RAW_W, &max_px_x, &max_px_y);
                raw_rc_to_screen_xy(min_idx / MLX_RAW_W, min_idx % MLX_RAW_W, &min_px_x, &min_px_y);

                draw_diamond_marker(cur_write_buf, max_px_x, max_px_y, MLX_INTERP_W, MLX_INTERP_H, 5, 0xF800);
                draw_diamond_marker(cur_write_buf, min_px_x, min_px_y, MLX_INTERP_W, MLX_INTERP_H, 5, 0x07FF);
                draw_diamond_marker(cur_write_buf, MLX_INTERP_W / 2, MLX_INTERP_H / 2, MLX_INTERP_W, MLX_INTERP_H, 3, 0xFFFF);

                // 8. 刷新到 LVGL 显存
                if (example_lvgl_lock(100)) {
                    s_thermal_img_dsc.data = (const uint8_t *)cur_write_buf;
                    if (img_thermal) {
                        lv_img_set_src(img_thermal, &s_thermal_img_dsc);
                        lv_obj_invalidate(img_thermal);
                    }
                    if (label_fps_float) lv_label_set_text(label_fps_float, text_fps);
                    if (label_info) lv_label_set_text(label_info, text_info);

                    if (tag_max) {
                        int pos_x = (max_px_x > 175) ? (max_px_x - 58) : (max_px_x + 8);
                        int pos_y = (max_px_y < 45) ? (max_px_y + 10) : ((max_px_y > 215) ? (max_px_y - 18) : (max_px_y - 8));
                        lv_label_set_text(tag_max, text_max_tag);
                        lv_obj_set_pos(tag_max, pos_x, pos_y);
                    }
                    if (tag_min) {
                        int pos_x = (min_px_x > 175) ? (min_px_x - 58) : (min_px_x + 8);
                        int pos_y = (min_px_y < 45) ? (min_px_y + 10) : ((min_px_y > 215) ? (min_px_y - 18) : (min_px_y - 8));
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

        vTaskDelay(pdMS_TO_TICKS(status >= 0 ? 2 : 10));
    }
}