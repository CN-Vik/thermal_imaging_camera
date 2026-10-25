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
 * 当前颜色模式：
 * IR_COLOR_IRONBOW    铁红伪彩（默认）
 * IR_COLOR_WHITE_HOT  白热
 * IR_COLOR_BLACK_HOT  黑热
 *
 * 修改此处并重新编译、烧录即可切换模式。
 */
#define THERMAL_COLOR_MODE    IR_COLOR_IRONBOW

/*
 * Thermal display tuning
 */
#define THERMAL_USE_AUTO_RANGE          1
#define THERMAL_FIXED_MIN_C             24.0f
#define THERMAL_FIXED_MAX_C             35.0f

/*
 * 时域降噪参数（针对 16Hz 下 MLX90640 底噪优化）：
 */
#define TEMP_FILTER_ALPHA_MIN           0.04f
#define TEMP_FILTER_ALPHA_MAX           0.85f
#define TEMP_FILTER_NOISE_FLOOR_C       0.75f
#define TEMP_FILTER_MOTION_SAT_C        2.20f

/* 3x3 孤立噪点抑制阈值：平坦背景区使用 0.58℃，手指边缘区自动切换为二阶连通保护 */
#define THERMAL_OUTLIER_THRESH_C        0.58f

/*
 * 自动量程参数（完全保持原版参数）：
 * - AUTO_RANGE_MIN_SPAN_C 设为 10.0℃，防止无热源时把 5℃ 的背景底噪拉伸成满屏花斑
 * - AUTO_RANGE_HIGHLIGHT_HEADROOM_C 预留 1.3℃ 顶部余量，防止手掌核心区过曝惨白
 */
#define AUTO_RANGE_MIN_SPAN_C           10.0f
#define AUTO_RANGE_LOW_PADDING_C        0.4f
#define AUTO_RANGE_HIGHLIGHT_HEADROOM_C 1.3f
#define AUTO_RANGE_ALPHA                0.04f
#define THERMAL_PERCENTILE_LOW           5
#define THERMAL_PERCENTILE_HIGH          95

/*
 * 图像方向校准开关：
 * IR_ROTATE_90 = 1 : 将传感器坐标系旋转 90°，适配竖屏 PCB 安装 (24宽 -> 240, 32高 -> 280)
 * IR_MIRROR_X  = 0 : 若发现屏幕里左右手方向反了，在 0 和 1 之间切换
 * IR_MIRROR_Y  = 1 : 默认 1，将手腕旋转到屏幕底部
 */
#define IR_ROTATE_90                    1
#define IR_MIRROR_X                     0
#define IR_MIRROR_Y                     1

/* X/Y 独立全视野定点数缩放步进 */
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

const float rate_hz_table[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};

/* 当前启用的颜色查找表 */
static uint16_t s_color_lut[256];

static inline float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/*
 * 1. 分场时域滤波 + 防趾蹼动态消交错
 *    - 阶段 A：仅对本帧新测量的 subpage 像素做连续自适应 IIR 时域降噪
 *    - 阶段 B：对未测量的另一半旧像素：
 *              * 平坦背景区（正交极差 < 0.90℃）：用 [n1, n2] 区间抑制奇偶场棋盘格噪点
 *              * 手指边缘与指根区（正交极差 >= 0.90℃）：改用 [n0, n3] 极值区间判断，
 *                防止指根处（3 个高温邻居 + 1 个低温指缝邻居）把低温指缝拉成高温造成“趾蹼粘连”
 */
static void update_temporal_and_deinterlace(const float *raw_src, float *filt_state,
                                            int w, int h, int subpage)
{
    const float inv_motion_span = 1.0f / (TEMP_FILTER_MOTION_SAT_C - TEMP_FILTER_NOISE_FLOOR_C);

    // 阶段 A：仅更新当前 subpage 新采样的 384 个真实像素
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            if (((r + c) & 1) != subpage) {
                continue;
            }
            int idx = r * w + c;
            float raw_v = raw_src[idx];
            float prev_v = filt_state[idx];

            if (prev_v < 1.0f || !isfinite(prev_v)) {
                filt_state[idx] = raw_v;
                continue;
            }

            float d = fabsf(raw_v - prev_v);
            float alpha;
            if (d <= TEMP_FILTER_NOISE_FLOOR_C) {
                alpha = TEMP_FILTER_ALPHA_MIN;
            } else if (d >= TEMP_FILTER_MOTION_SAT_C) {
                alpha = TEMP_FILTER_ALPHA_MAX;
            } else {
                float t = (d - TEMP_FILTER_NOISE_FLOOR_C) * inv_motion_span;
                alpha = TEMP_FILTER_ALPHA_MIN + t * (TEMP_FILTER_ALPHA_MAX - TEMP_FILTER_ALPHA_MIN);
            }

            filt_state[idx] = prev_v + alpha * (raw_v - prev_v);
        }
    }

    // 阶段 B：检查上一场的 384 个旧像素是否因热源运动而滞后
    for (int r = 0; r < h; r++) {
        int r_prev = (r > 0) ? (r - 1) : r;
        int r_next = (r < h - 1) ? (r + 1) : r;

        for (int c = 0; c < w; c++) {
            if (((r + c) & 1) == subpage) {
                continue;
            }
            int idx = r * w + c;
            float val = filt_state[idx];

            int c_prev = (c > 0) ? (c - 1) : c;
            int c_next = (c < w - 1) ? (c + 1) : c;

            // 这 4 个正交邻居全都是刚刚在阶段 A 更新完的【最新子场像素】
            float n0 = filt_state[r_prev * w + c];
            float n1 = filt_state[r_next * w + c];
            float n2 = filt_state[r * w + c_prev];
            float n3 = filt_state[r * w + c_next];

            if (val < 1.0f || !isfinite(val)) {
                filt_state[idx] = 0.25f * (n0 + n1 + n2 + n3);
                continue;
            }

            // 快速排序网络: n0 <= n1 <= n2 <= n3
            if (n0 > n1) { float t = n0; n0 = n1; n1 = t; }
            if (n2 > n3) { float t = n2; n2 = n3; n3 = t; }
            if (n0 > n2) { float t = n0; n0 = n2; n2 = t; }
            if (n1 > n3) { float t = n1; n1 = n3; n3 = t; }
            if (n1 > n2) { float t = n1; n1 = n2; n2 = t; }

            float ortho_med = 0.5f * (n1 + n2);
            float ortho_span = n3 - n0;

            // 关键区分：
            // - 若正交 4 邻居存在明显温差（ortho_span >= 0.90℃，如指根夹角或手指边缘），
            //   使用外部极值 [n0, n3] 作为越界基准，只要有 1 个方向连着低温指缝 (n0)，就绝不把指缝拉成高温！
            // - 若正交 4 邻居平坦（ortho_span < 0.90℃，如背景区），使用内部区间 [n1, n2] 消除奇偶噪点。
            float lo_ref = (ortho_span >= 0.90f) ? n0 : n1;
            float hi_ref = (ortho_span >= 0.90f) ? n3 : n2;

            float out_of_bounds = 0.0f;
            if (val < lo_ref) {
                out_of_bounds = lo_ref - val;
            } else if (val > hi_ref) {
                out_of_bounds = val - hi_ref;
            }

            if (out_of_bounds > 0.50f) {
                float m_weight = clampf_local((out_of_bounds - 0.50f) * 0.65f, 0.25f, 0.90f);
                filt_state[idx] = val + m_weight * (ortho_med - val);
            }
        }
    }
}

/*
 * 2. 3x3 鲁棒中值异常点抑制（带指缝/指尖连通性保护）
 *    - 在平坦区（v[n-2] - v[1] < 1.5℃）：与中位数 v[n/2] 比较，积极滤除背景噪点
 *    - 在手指/指缝结构区（v[n-2] - v[1] >= 1.5℃）：1 像素宽的指缝在 3x3 窗口内只有 2~3 个低温点，
 *      中位数 v[4] 必然是两侧的高温手指！此时改为与 [v[1], v[n-2]] 比较，
 *      确保只要指缝或指尖有至少 2 个像素相连，就绝不会被中值滤波填平或削断。
 */
static void spatial_median_outlier_filter(const float *src, float *dst, int w, int h)
{
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            float v[9];
            int n = 0;

            for (int dy = -1; dy <= 1; dy++) {
                int rr = r + dy;
                if (rr < 0) rr = 0;
                if (rr >= h) rr = h - 1;

                for (int dx = -1; dx <= 1; dx++) {
                    int cc = c + dx;
                    if (cc < 0) cc = 0;
                    if (cc >= w) cc = w - 1;

                    float t = src[rr * w + cc];
                    if (isfinite(t) && t > 1.0f && t < 200.0f) {
                        v[n++] = t;
                    }
                }
            }

            int idx = r * w + c;
            float center = src[idx];
            if (!isfinite(center) || center <= 1.0f || center >= 200.0f || n < 5) {
                dst[idx] = center;
                continue;
            }

            // 9 个以内元素，插入排序开销很小。
            for (int i = 1; i < n; i++) {
                float key = v[i];
                int j = i - 1;
                while (j >= 0 && v[j] > key) {
                    v[j + 1] = v[j];
                    j--;
                }
                v[j + 1] = key;
            }

            float median = v[n / 2];
            float second_low  = v[1];
            float second_high = v[n - 2];
            float struct_span = second_high - second_low;

            if (struct_span < 1.50f) {
                // 平坦背景或手心内部：使用中位数强力滤除孤立噪点
                float diff = fabsf(center - median);
                if (diff > THERMAL_OUTLIER_THRESH_C) {
                    float strength = clampf_local(
                        (diff - THERMAL_OUTLIER_THRESH_C) / 1.1f,
                        0.15f, 0.78f);
                    dst[idx] = center + strength * (median - center);
                } else {
                    dst[idx] = center;
                }
            } else {
                // 手指边缘、指尖或指缝区：仅当像素孤立超出次高/次低范围 [v[1], v[n-2]] 时才修正，
                // 彻底杜绝 3x3 中位数把低温指缝拉向高温手指造成“趾蹼”！
                if (center > second_high + 0.80f) {
                    dst[idx] = second_high + 0.35f * (center - second_high);
                } else if (center < second_low - 0.80f) {
                    dst[idx] = second_low - 0.35f * (second_low - center);
                } else {
                    dst[idx] = center;
                }
            }
        }
    }
}

static int compare_float_ascending(const void *a, const void *b)
{
    float x = *(const float *)a;
    float y = *(const float *)b;
    return (x > y) - (x < y);
}

/*
 * 3. 防指缝漏热的 3x3 双边保边平滑滤波
 *    - 平坦区消除固定图案噪声 (FPN) 与奇偶微差
 *    - 非对称防漏热保护：当邻居比中心像素更热（例如两侧手指高于中间指缝）时，
 *      将截断阈值收紧至 0.65℃，严防两侧高温手指向中间低温指缝扩散热量导致手指粘连
 */
static void spatial_bilateral_denoise(const float *src, float *dst, int w, int h)
{
    const float edge_thresh_flat = 1.65f;
    const float edge_thresh_hot_bleed = 0.65f; // 阻止高温邻居把低温指缝拉高

    for (int r = 0; r < h; r++) {
        int r_prev = (r > 0) ? (r - 1) : r;
        int r_next = (r < h - 1) ? (r + 1) : r;

        for (int c = 0; c < w; c++) {
            int c_prev = (c > 0) ? (c - 1) : c;
            int c_next = (c < w - 1) ? (c + 1) : c;

            float center = src[r * w + c];

            // 正交 4 邻域
            float ortho[4] = {
                src[r_prev * w + c],
                src[r_next * w + c],
                src[r * w + c_prev],
                src[r * w + c_next]
            };

            // 对角 4 邻域
            float diag[4] = {
                src[r_prev * w + c_prev],
                src[r_prev * w + c_next],
                src[r_next * w + c_prev],
                src[r_next * w + c_next]
            };

            // 计算正交邻域极差，判断当前是否处于平坦区
            float o_min = ortho[0], o_max = ortho[0];
            for (int i = 1; i < 4; i++) {
                if (ortho[i] < o_min) o_min = ortho[i];
                if (ortho[i] > o_max) o_max = ortho[i];
            }
            float ortho_span = o_max - o_min;

            // 仅在非边缘/非指缝的平坦区域（ortho_span < 1.0℃）消除中心点与正交邻域的静态奇偶微差
            if (ortho_span < 1.0f) {
                float ortho_avg = 0.25f * (ortho[0] + ortho[1] + ortho[2] + ortho[3]);
                if (fabsf(center - ortho_avg) < 0.80f) {
                    center = 0.65f * center + 0.35f * ortho_avg;
                }
            }

            float sum_smooth = center * 4.0f;
            float w_total = 4.0f;

            for (int i = 0; i < 4; i++) {
                // 正交邻居：如果邻居比中心点热且处于有梯度的区域（如手指高于指缝），使用 0.65℃ 严苛阈值阻断漏热
                float diff_o = ortho[i] - center;
                float d_o = fabsf(diff_o);
                float thresh_o = (diff_o > 0.0f && ortho_span >= 0.90f)
                                 ? edge_thresh_hot_bleed
                                 : edge_thresh_flat;
                if (d_o < thresh_o) {
                    float t = 1.0f - (d_o / thresh_o);
                    float w_o = 2.0f * t * t;
                    sum_smooth += ortho[i] * w_o;
                    w_total += w_o;
                }

                // 对角邻居：同样阻止高温对角像素向低温指缝漏热
                float diff_d = diag[i] - center;
                float d_d = fabsf(diff_d);
                float thresh_d = (diff_d > 0.0f && ortho_span >= 0.90f)
                                 ? edge_thresh_hot_bleed
                                 : edge_thresh_flat;
                if (d_d < thresh_d) {
                    float t = 1.0f - (d_d / thresh_d);
                    float w_d = 1.0f * t * t;
                    sum_smooth += diag[i] * w_d;
                    w_total += w_d;
                }
            }

            dst[r * w + c] = sum_smooth / w_total;
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

    memset(s_display_buf[0], 0, buf_size);
    if (s_display_buf[1] != s_display_buf[0]) {
        memset(s_display_buf[1], 0, buf_size);
    }

#if IR_ROTATE_90
    s_scale_x_fp = ((MLX_RAW_H - 1) << 16) / (MLX_INTERP_W - 1);
    s_scale_y_fp = ((MLX_RAW_W - 1) << 16) / (MLX_INTERP_H - 1);
#else
    s_scale_x_fp = ((MLX_RAW_W - 1) << 16) / (MLX_INTERP_W - 1);
    s_scale_y_fp = ((MLX_RAW_H - 1) << 16) / (MLX_INTERP_H - 1);
#endif

    s_thermal_img_dsc.data = (const uint8_t *)s_display_buf[0];
    return ESP_OK;
}

/*
 * 初始化热成像颜色查找表
 *
 * IR_COLOR_IRONBOW:
 *     保留原有铁红伪彩效果
 *
 * IR_COLOR_WHITE_HOT:
 *     低温黑色 -> 高温白色
 *
 * IR_COLOR_BLACK_HOT:
 *     低温白色 -> 高温黑色
 */
/*
 * 初始化热成像颜色查找表（直接复用 ir_color_modes.c 的颜色配置逻辑）
 */
static void init_thermal_color_lut(ir_color_mode_t mode)
{
    ir_color_generate_lut(mode, s_color_lut);
}

/**
 * @brief 配置IR颜色模式
 * 
 * @param mode 
 */
void ir_camera_set_color_mode(ir_color_mode_t mode)
{
    ir_color_generate_lut(mode, s_color_lut);
}

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

static void bilinear_scale_fullscreen_1to1(const uint8_t *src, int src_w, int src_h,
                                           uint16_t *dst, int dst_w, int dst_h,
                                           const uint16_t *lut)
{
    for (int y = 0; y < dst_h; y++) {
        int eff_y = IR_MIRROR_Y ? (dst_h - 1 - y) : y;
        uint16_t *dst_row = &dst[y * dst_w];

#if IR_ROTATE_90
        uint32_t c_fp = eff_y * s_scale_y_fp;
        int c0 = c_fp >> 16;
        int c1 = (c0 < src_w - 1) ? (c0 + 1) : c0;
        uint32_t c_diff = c_fp & 0xFFFF;
        uint32_t c_diff_inv = 65536U - c_diff;

        for (int x = 0; x < dst_w; x++) {
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

        // 温和对比度曲线 (0.65 * norm^1.5 + 0.35 * norm)：
        // 既压低背景暗部噪点，又保留手掌高温区的细节层次，防止手心一片惨白过曝
        float gamma_15 = norm * sqrtf(norm);
        norm = 0.65f * gamma_15 + 0.35f * norm;

        dst[i] = (uint8_t)(norm * 255.0f + 0.5f);
    }
}

void ir_camera_task_main(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "IR camera task started on Core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "Thermal map: fixed range %.1fC ~ %.1fC, auto_range=%d",
             THERMAL_FIXED_MIN_C, THERMAL_FIXED_MAX_C, THERMAL_USE_AUTO_RANGE);

    // init_ironbow_lut();
    init_thermal_color_lut(THERMAL_COLOR_MODE);

    MLX90640_I2CInit();
    MLX90640_SetRefreshRate(MLX90640_ADDR, RefreshRate);
    MLX90640_SetChessMode(MLX90640_ADDR);

    uint16_t *ir_frame_data = (uint16_t *)calloc(834, sizeof(uint16_t));
    uint16_t *eerom_ir_data = (uint16_t *)calloc(832, sizeof(uint16_t));
    float *raw_temp_matrix  = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    paramsMLX90640 *mlx_params = (paramsMLX90640 *)calloc(1, sizeof(paramsMLX90640));

    float *filtered_temp = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    float *median_temp   = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    float *spatial_temp  = (float *)calloc(MLX_RAW_PIXELS, sizeof(float));
    uint8_t *norm_matrix = (uint8_t *)calloc(MLX_RAW_PIXELS, sizeof(uint8_t));

    if (!ir_frame_data || !eerom_ir_data || !raw_temp_matrix || !mlx_params ||
        !filtered_temp || !median_temp || !spatial_temp || !norm_matrix) {
        ESP_LOGE(TAG, "Critical: calloc failed, Out of memory!");
        vTaskDelete(NULL);
        return;
    }

    MLX90640_DumpEE(MLX90640_ADDR, eerom_ir_data);
    MLX90640_ExtractParameters(eerom_ir_data, mlx_params);
    free(eerom_ir_data);
    eerom_ir_data = NULL;

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
    float smooth_ta    = 25.0f;
    bool ta_initialized = false;
    bool range_initialized = false;

    bool subpage_init[2] = {false, false};

    while (1) {
        int status = MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);

        if (status >= 0) {
            int subpage = ir_frame_data[833];
            if (subpage < 0 || subpage > 1) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            float raw_ta = MLX90640_GetTa(ir_frame_data, mlx_params);
            if (!ta_initialized || !isfinite(smooth_ta)) {
                smooth_ta = raw_ta;
                ta_initialized = true;
            } else if (isfinite(raw_ta)) {
                smooth_ta += 0.12f * (raw_ta - smooth_ta);
            }

            // 1. 官方原生计算：增量更新当前 subpage 的 384 个点
            MLX90640_CalculateTo(ir_frame_data, mlx_params,
                                 emissivity, smooth_ta - TA_SHIFT, raw_temp_matrix);

            int mode = (ir_frame_data[832] & MLX90640_CTRL_MEAS_MODE_MASK) >> 5;
            MLX90640_BadPixelsCorrection(mlx_params->brokenPixels, raw_temp_matrix, mode, mlx_params);
            MLX90640_BadPixelsCorrection(mlx_params->outlierPixels, raw_temp_matrix, mode, mlx_params);

            subpage_init[subpage] = true;
            frame_count++;

            if (!subpage_init[0] || !subpage_init[1]) {
                // 首轮先将 raw_temp_matrix 同步到 filtered_temp
                memcpy(filtered_temp, raw_temp_matrix, MLX_RAW_PIXELS * sizeof(float));
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            // 2. 分场时域平滑 + 防趾蹼消交错
            update_temporal_and_deinterlace(raw_temp_matrix, filtered_temp,
                                            MLX_RAW_W, MLX_RAW_H, subpage);

            // 3. 中值异常点抑制：平坦区滤噪，手指/指缝区保护连通特征
            spatial_median_outlier_filter(filtered_temp, median_temp,
                                          MLX_RAW_W, MLX_RAW_H);

            // 4. 双边保边平滑：压制背景颗粒，阻断高温手指向低温指缝漏热
            spatial_bilateral_denoise(median_temp, spatial_temp,
                                      MLX_RAW_W, MLX_RAW_H);

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
            // 用 5%/95% 分位数决定颜色映射范围，避免单个异常像素拉伸整幅图像。
            // median_temp 可复用为排序工作区；spatial_temp 保留原始滤波图用于显示和测温。
            int n_valid = 0;
            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                float t = spatial_temp[i];
                if (isfinite(t) && t > 1.0f && t < 200.0f) {
                    median_temp[n_valid++] = t;
                }
            }

            float robust_min = cur_min;
            float robust_max = cur_max;
            if (n_valid >= 100) {
                qsort(median_temp, n_valid, sizeof(float), compare_float_ascending);
                robust_min = median_temp[(n_valid - 1) * THERMAL_PERCENTILE_LOW / 100];
                robust_max = median_temp[(n_valid - 1) * THERMAL_PERCENTILE_HIGH / 100];
            }

            float target_min = robust_min - AUTO_RANGE_LOW_PADDING_C;
            float target_max = robust_max + AUTO_RANGE_HIGHLIGHT_HEADROOM_C;

            // 当画面中没有高温物体（温差小于 10℃）时，优先向下扩展量程上限，将背景固定在深蓝暗部
            if ((target_max - target_min) < AUTO_RANGE_MIN_SPAN_C) {
                target_max = target_min + AUTO_RANGE_MIN_SPAN_C;
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
                    norm_matrix,
                    MLX_RAW_W, 
                    MLX_RAW_H,
                    cur_write_buf,
                    MLX_INTERP_W, 
                    MLX_INTERP_H,
                    s_color_lut
                );

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