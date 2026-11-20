#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90640_API.h"
#include "ir_mlx90640_app.h"
#include "lang.h"
#include "ir_settings_ui.h"

static const char *TAG = "mlx_app";

/* 本机安装方向：上下方向保持现状，只强制左右翻转。
 * 若以后传感器与屏幕的安装方向改变，再将其改为 0。 */
#define IR_FORCE_HORIZONTAL_MIRROR  1

static inline bool ir_horizontal_mirror_enabled(void)
{
    return (IR_FORCE_HORIZONTAL_MIRROR != 0);
}

TaskHandle_t ir_task_Handle = NULL;

#define IR_SETTINGS_NVS_NAMESPACE "ir_settings"
#define IR_SETTINGS_NVS_KEY       "app"
#define IR_SETTINGS_MAGIC         0x49525354U
#define IR_SETTINGS_VERSION       4U
#define IR_TEMP_MIN_VALID_C       (-40.0f)
#define IR_TEMP_MAX_VALID_C       300.0f
#define IR_TEMP_CAL_ANCHOR_C      30.0f
#define IR_TEMP_CAL_SENSOR_HIGH_C 92.675f
#define IR_TEMP_CAL_REFERENCE_C   100.0f

typedef struct {
    uint32_t magic;
    uint32_t version;
    ir_app_settings_t settings;
} ir_settings_nvs_blob_t;

static const ir_app_settings_t s_default_settings = {
    .color_mode = IR_COLOR_IRONBOW,
    .color_reverse = false,
    .label_coldest = true,
    .label_hottest = true,
    .label_fixture = false,
    .fixture_px_x = MLX_INTERP_W / 2,
    .fixture_px_y = MLX_INTERP_H / 2,
    .rotate = IR_ROT_0,
    .mirror_vertical = false,
    .mirror_horizontal = false,
    .temp_scale = IR_SCALE_CELSIUS,
    .brightness = 50,
    .auto_sleep_min = 30,
    .filter_mode = IR_FILTER_BSPLINE,
    .emissivity = 0.95f,
    .gamma = 1.50f,
    .temperature_offset_c = 0.0f,
    .chart_source = IR_CHART_SRC_CENTER,
    .reboot_cnt = 0,
};

ir_app_settings_t g_ir_settings = {
    .color_mode = IR_COLOR_IRONBOW,
    .color_reverse = false,
    .label_coldest = true,
    .label_hottest = true,
    .label_fixture = false,
    .fixture_px_x = MLX_INTERP_W / 2,
    .fixture_px_y = MLX_INTERP_H / 2,
    .rotate = IR_ROT_0,
    .mirror_vertical = false,
    .mirror_horizontal = false,
    .temp_scale = IR_SCALE_CELSIUS,
    .brightness = 50,
    .auto_sleep_min = 30,
    .filter_mode = IR_FILTER_BSPLINE,
    .emissivity = 0.95f,
    .gamma = 1.50f,
    .temperature_offset_c = 0.0f,
    .chart_source = IR_CHART_SRC_CENTER,
    .reboot_cnt = 0,
};

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
#define TEMP_FILTER_ALPHA_MIN           0.08f
#define TEMP_FILTER_ALPHA_MAX           0.88f
#define TEMP_FILTER_NOISE_FLOOR_C       0.85f
#define TEMP_FILTER_MOTION_SAT_C        2.40f
#define TEMP_FILTER_SPIKE_GUARD_C        2.10f

/* 3x3 孤立噪点抑制阈值：平坦背景区收紧，结构边缘继续保护连通性。 */
#define THERMAL_OUTLIER_THRESH_C        0.30f

/* 较宽的自动量程与按温差增加的余量，避免大面积热区挤进色表顶端。 */
#define AUTO_RANGE_MIN_SPAN_C           8.0f
#define AUTO_RANGE_LOW_PADDING_C        0.5f
#define AUTO_RANGE_LOW_PADDING_FRAC     0.03f
#define AUTO_RANGE_HEADROOM_MIN_C       1.0f
#define AUTO_RANGE_HEADROOM_FRAC        0.06f
#define AUTO_RANGE_ALPHA                0.04f
#define AUTO_RANGE_FALL_ALPHA           0.30f
#define AUTO_RANGE_RISE_ALPHA           0.25f
#define THERMAL_PERCENTILE_LOW           5

/* 降发热优化：正常显示保持 16Hz（流畅度不变），仅热像被遮挡/息屏时降频降耗。
 *  - 设置菜单/调色板/相册/照片查看器 -> 4Hz（功耗约 15mA，退出恢复快）
 *  - 息屏                             -> 1Hz（功耗约 10mA）
 * 退出遮挡后丢弃 IR_RESYNC_FRAMES 帧，避免"半旧半新"错图。 */
#define IR_IDLE_REFRESH_MENU   FPS4HZ
#define IR_IDLE_REFRESH_SLEEP  FPS1HZ
#define IR_RESYNC_FRAMES       2

#define IR_SCREEN_W     280
#define IR_SCREEN_H     MLX_VISIBLE_H
#define IR_TAG_GUESS_W  108
#define IR_TAG_GUESS_H  26


static void place_temp_tag(lv_obj_t *tag, int px, int py, const char *text)
{
    if (tag == NULL) return;

    lv_label_set_text(tag, text);

    int x = px + 8;
    int y = py + 8;

    /* 右边放不下就放左边，下边放不下就放上边 */
    if (x + IR_TAG_GUESS_W > IR_SCREEN_W) x = px - IR_TAG_GUESS_W - 8;
    if (y + IR_TAG_GUESS_H > IR_SCREEN_H) y = py - IR_TAG_GUESS_H - 8;

    /* 最后再夹一次，保证不出屏 */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > IR_SCREEN_W - IR_TAG_GUESS_W) x = IR_SCREEN_W - IR_TAG_GUESS_W;
    if (y > IR_SCREEN_H - IR_TAG_GUESS_H) y = IR_SCREEN_H - IR_TAG_GUESS_H;

    lv_obj_set_pos(tag, x, y);
}

/* 当前旋转/镜像生效后的源图像缩放步进 */
static uint32_t s_source_col_step_fp = 0;
static uint32_t s_source_row_step_fp = 0;
static bool s_transform_valid = false;

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
static ir_color_mode_t s_cur_color_mode = THERMAL_COLOR_MODE;

/* 归一化温度快照（供拍照压缩存储），每帧刷新 */
static uint8_t s_norm_snapshot[MLX_RAW_PIXELS];
static uint16_t s_norm16_frame[MLX_RAW_PIXELS];
static float   s_snapshot_min_t = 0.0f;
static float   s_snapshot_max_t = 0.0f;

static float s_chart_buf[IR_CHART_HISTORY_LEN];
static int s_chart_head = 0;
static int s_chart_count = 0;
static portMUX_TYPE s_chart_mux = portMUX_INITIALIZER_UNLOCKED;

static inline float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int clamp_int_local(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline bool ir_temperature_is_valid(float temp_c)
{
    return isfinite(temp_c) &&
           temp_c >= IR_TEMP_MIN_VALID_C &&
           temp_c <= IR_TEMP_MAX_VALID_C;
}

static inline float ir_temperature_calibrate(float temp_c)
{
    if (!ir_temperature_is_valid(temp_c) || temp_c <= IR_TEMP_CAL_ANCHOR_C) {
        return temp_c;
    }

    const float gain = (IR_TEMP_CAL_REFERENCE_C - IR_TEMP_CAL_ANCHOR_C) /
                       (IR_TEMP_CAL_SENSOR_HIGH_C - IR_TEMP_CAL_ANCHOR_C);
    return IR_TEMP_CAL_ANCHOR_C + (temp_c - IR_TEMP_CAL_ANCHOR_C) * gain;
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

    // 阶段 A：更新当前子场，并用异场邻域验证中等幅度的孤立跳变。
    for (int r = 0; r < h; r++) {
        int first_c = ((r & 1) == subpage) ? 0 : 1;
        for (int c = first_c; c < w; c += 2) {
            int idx = r * w + c;
            float raw_v = raw_src[idx];
            float prev_v = filt_state[idx];

            if (!ir_temperature_is_valid(raw_v)) {
                continue;
            }
            if (!ir_temperature_is_valid(prev_v)) {
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
                t = t * t * (3.0f - 2.0f * t);
                alpha = TEMP_FILTER_ALPHA_MIN + t * (TEMP_FILTER_ALPHA_MAX - TEMP_FILTER_ALPHA_MIN);
            }

            if (d > TEMP_FILTER_NOISE_FLOOR_C && d < TEMP_FILTER_SPIKE_GUARD_C) {
                float neighbor_min = IR_TEMP_MAX_VALID_C;
                float neighbor_max = IR_TEMP_MIN_VALID_C;
                int neighbor_count = 0;
                const int dr[4] = {-1, 1, 0, 0};
                const int dc[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4; k++) {
                    int rr = r + dr[k];
                    int cc = c + dc[k];
                    if (rr < 0 || rr >= h || cc < 0 || cc >= w) continue;

                    float neighbor = filt_state[rr * w + cc];
                    if (!ir_temperature_is_valid(neighbor)) continue;
                    if (neighbor < neighbor_min) neighbor_min = neighbor;
                    if (neighbor > neighbor_max) neighbor_max = neighbor;
                    neighbor_count++;
                }

                if (neighbor_count >= 2 &&
                    (raw_v < neighbor_min - 0.55f ||
                     raw_v > neighbor_max + 0.55f)) {
                    alpha = TEMP_FILTER_ALPHA_MIN +
                            0.25f * (alpha - TEMP_FILTER_ALPHA_MIN);
                }
            }

            filt_state[idx] = prev_v + alpha * (raw_v - prev_v);
        }
    }

    // 阶段 B：检查上一子场的旧像素，并在平坦区消除奇偶场微差。
    for (int r = 0; r < h; r++) {
        int r_prev = (r > 0) ? (r - 1) : r;
        int r_next = (r < h - 1) ? (r + 1) : r;

        int first_c = ((r & 1) == subpage) ? 1 : 0;
        for (int c = first_c; c < w; c += 2) {
            int idx = r * w + c;
            float val = filt_state[idx];

            int c_prev = (c > 0) ? (c - 1) : c;
            int c_next = (c < w - 1) ? (c + 1) : c;

            // 这 4 个正交邻居全都是刚刚在阶段 A 更新完的【最新子场像素】
            float n0 = filt_state[r_prev * w + c];
            float n1 = filt_state[r_next * w + c];
            float n2 = filt_state[r * w + c_prev];
            float n3 = filt_state[r * w + c_next];

            if (!ir_temperature_is_valid(val) ||
                !ir_temperature_is_valid(n0) || !ir_temperature_is_valid(n1) ||
                !ir_temperature_is_valid(n2) || !ir_temperature_is_valid(n3)) {
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

            bool is_edge = (ortho_span >= 0.90f);
            float lo_ref = is_edge ? n0 : n1;
            float hi_ref = is_edge ? n3 : n2;

            float out_of_bounds = 0.0f;
            if (val < lo_ref) {
                out_of_bounds = lo_ref - val;
            } else if (val > hi_ref) {
                out_of_bounds = val - hi_ref;
            }

            float trigger = is_edge ? 0.45f : 0.35f;
            if (out_of_bounds > trigger) {
                float m_weight = clampf_local((out_of_bounds - trigger) * 0.72f,
                                              0.25f, 0.90f);
                filt_state[idx] = val + m_weight * (ortho_med - val);
            } else if (!is_edge) {
                filt_state[idx] = val + 0.22f * (ortho_med - val);
            }
        }
    }
}

/*
 * 2. 3x3 鲁棒异常点抑制（带指缝/指尖连通性保护）
 *    - 在平坦区（v[n-2] - v[1] < 1.5℃）：与中间三个样本的均值比较，积极滤除背景噪点
 *    - 在手指/指缝结构区（v[n-2] - v[1] >= 1.5℃）：1 像素宽的指缝在 3x3 窗口内只有 2~3 个低温点，
 *      中间样本可能来自两侧的高温手指，因此边缘区改为与 [v[1], v[n-2]] 比较，
 *      尽量避免把连通的低温指缝填平或削断。
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
                    if (ir_temperature_is_valid(t)) {
                        v[n++] = t;
                    }
                }
            }

            int idx = r * w + c;
            float center = src[idx];
            if (!ir_temperature_is_valid(center) || n < 5) {
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

            int mid = n / 2;
            float mid_avg = (v[mid - 1] + v[mid] + v[mid + 1]) * (1.0f / 3.0f);
            float second_low  = v[1];
            float second_high = v[n - 2];
            float struct_span = second_high - second_low;

            if (struct_span < 1.50f) {
                // 平坦背景或手心内部：以中位附近三个样本的均值压制孤立噪点。
                float diff = fabsf(center - mid_avg);
                if (diff > THERMAL_OUTLIER_THRESH_C) {
                    float strength = clampf_local(
                        (diff - THERMAL_OUTLIER_THRESH_C) / 0.85f,
                        0.25f, 0.90f);
                    dst[idx] = center + strength * (mid_avg - center);
                } else {
                    dst[idx] = center;
                }
            } else {
                // 结构边缘仅削弱孤立毛刺，保留连通的手指与指缝。
                if (center > second_high + 0.60f) {
                    dst[idx] = second_high + 0.30f * (center - second_high);
                } else if (center < second_low - 0.60f) {
                    dst[idx] = second_low - 0.30f * (second_low - center);
                } else {
                    dst[idx] = center;
                }
            }
        }
    }
}

static void select_nth_float(float *values, int count, int nth)
{
    int lo = 0;
    int hi = count;

    while (hi - lo > 1) {
        int mid = lo + (hi - lo) / 2;
        float a = values[lo];
        float b = values[mid];
        float c = values[hi - 1];
        float pivot = (a < b) ? ((b < c) ? b : ((a < c) ? c : a))
                              : ((a < c) ? a : ((b < c) ? c : b));

        int less = lo;
        int scan = lo;
        int greater = hi;
        while (scan < greater) {
            if (values[scan] < pivot) {
                float tmp = values[less];
                values[less++] = values[scan];
                values[scan++] = tmp;
            } else if (values[scan] > pivot) {
                float tmp = values[--greater];
                values[greater] = values[scan];
                values[scan] = tmp;
            } else {
                scan++;
            }
        }

        if (nth < less) {
            hi = less;
        } else if (nth >= greater) {
            lo = greater;
        } else {
            return;
        }
    }
}

static float max_valid_temperature(const float *values)
{
    float max_temp = IR_TEMP_MIN_VALID_C;
    for (int i = 0; i < MLX_RAW_PIXELS; i++) {
        float value = values[i];
        if (ir_temperature_is_valid(value) &&
            value > max_temp) {
            max_temp = value;
        }
    }
    return max_temp;
}

static float min_valid_temperature(const float *values)
{
    float min_temp = IR_TEMP_MAX_VALID_C;
    for (int i = 0; i < MLX_RAW_PIXELS; i++) {
        float value = values[i];
        if (ir_temperature_is_valid(value) &&
            value < min_temp) {
            min_temp = value;
        }
    }
    return min_temp;
}

/* 3x3 双边滤波：自适应中心权重、平坦区奇偶均衡和冷热边缘非对称保护。 */
static void spatial_bilateral_pass(const float *src, float *dst, int w, int h,
                                   bool equalize_subpage, float thresh_flat)
{
    const float thresh_hot_bleed = 0.60f;
    const float thresh_cold_edge = 0.85f;

    for (int r = 0; r < h; r++) {
        int r_prev = (r > 0) ? r - 1 : r;
        int r_next = (r < h - 1) ? r + 1 : r;
        for (int c = 0; c < w; c++) {
            int c_prev = (c > 0) ? c - 1 : c;
            int c_next = (c < w - 1) ? c + 1 : c;
            float center = src[r * w + c];
            if (!ir_temperature_is_valid(center)) {
                dst[r * w + c] = center;
                continue;
            }

            float ortho[4] = {
                src[r_prev * w + c], src[r_next * w + c],
                src[r * w + c_prev], src[r * w + c_next]
            };
            float diag[4] = {
                src[r_prev * w + c_prev], src[r_prev * w + c_next],
                src[r_next * w + c_prev], src[r_next * w + c_next]
            };

            float ortho_min = center;
            float ortho_max = center;
            float ortho_sum = 0.0f;
            int ortho_count = 0;
            for (int i = 0; i < 4; i++) {
                if (!ir_temperature_is_valid(ortho[i])) continue;
                if (ortho[i] < ortho_min) ortho_min = ortho[i];
                if (ortho[i] > ortho_max) ortho_max = ortho[i];
                ortho_sum += ortho[i];
                ortho_count++;
            }
            float ortho_span = ortho_max - ortho_min;

            if (equalize_subpage && ortho_count > 0 && ortho_span < 1.20f) {
                float ortho_avg = ortho_sum / (float)ortho_count;
                if (fabsf(center - ortho_avg) < 0.85f) {
                    float eq_weight = 0.45f * (1.0f - ortho_span / 1.20f);
                    center += eq_weight * (ortho_avg - center);
                }
            }

            float edge_factor =
                clampf_local((ortho_span - 0.60f) / 1.40f, 0.0f, 1.0f);
            float center_weight = 1.6f + 2.4f * edge_factor;
            float sum = center * center_weight;
            float weight_sum = center_weight;
            bool is_gradient = ortho_span >= 0.90f;

            for (int i = 0; i < 4; i++) {
                if (ir_temperature_is_valid(ortho[i])) {
                    float difference = ortho[i] - center;
                    float threshold = thresh_flat;
                    if (is_gradient) {
                        threshold = (difference > 0.0f)
                                  ? thresh_hot_bleed : thresh_cold_edge;
                    }
                    float distance = fabsf(difference);
                    if (distance < threshold) {
                        float t = 1.0f - distance / threshold;
                        float weight = t * t;
                        sum += ortho[i] * weight;
                        weight_sum += weight;
                    }
                }

                if (ir_temperature_is_valid(diag[i])) {
                    float difference = diag[i] - center;
                    float threshold = thresh_flat;
                    if (is_gradient) {
                        threshold = (difference > 0.0f)
                                  ? thresh_hot_bleed : thresh_cold_edge;
                    }
                    float distance = fabsf(difference);
                    if (distance < threshold) {
                        float t = 1.0f - distance / threshold;
                        float weight = 0.50f * t * t;
                        sum += diag[i] * weight;
                        weight_sum += weight;
                    }
                }
            }

            dst[r * w + c] = sum / weight_sum;
        }
    }
}

static void spatial_bilateral_denoise(const float *src, float *dst,
                                      int w, int h, int passes)
{
    if (passes <= 1) {
        spatial_bilateral_pass(src, dst, w, h, true, 1.15f);
        return;
    }

    static float bilateral_tmp[MLX_RAW_PIXELS];
    spatial_bilateral_pass(src, bilateral_tmp, w, h, true, 1.15f);
    spatial_bilateral_pass(bilateral_tmp, dst, w, h, false, 0.95f);
}

static void spatial_gaussian_denoise(const float *src, float *dst, int w, int h)
{
    static const float kernel[3][3] = {
        {1.0f, 2.0f, 1.0f},
        {2.0f, 4.0f, 2.0f},
        {1.0f, 2.0f, 1.0f},
    };

    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            float sum = 0.0f;
            float weight_sum = 0.0f;
            float center = src[r * w + c];

            for (int dy = -1; dy <= 1; dy++) {
                int rr = r + dy;
                if (rr < 0) rr = 0;
                if (rr >= h) rr = h - 1;

                for (int dx = -1; dx <= 1; dx++) {
                    int cc = c + dx;
                    if (cc < 0) cc = 0;
                    if (cc >= w) cc = w - 1;

                    float value = src[rr * w + cc];
                    if (!ir_temperature_is_valid(value)) {
                        continue;
                    }

                    /* 同样加入非对称防漏热保护，防止高斯模式把高温手指扩散到低温指缝 */
                    float diff = value - center;
                    float thresh = (diff > 0.0f) ? 0.65f : 1.65f;
                    float weight = kernel[dy + 1][dx + 1];
                    if (fabsf(diff) > thresh) {
                        weight *= 0.05f;
                    }
                    sum += value * weight;
                    weight_sum += weight;
                }
            }

            dst[r * w + c] = (weight_sum > 0.0f) ? (sum / weight_sum) : center;
        }
    }
}

static void apply_spatial_filter(const float *filtered, const float *median,
                                 float *dst, int mode)
{
    switch (mode) {
    case IR_FILTER_OFF:
        memcpy(dst, filtered, MLX_RAW_PIXELS * sizeof(float));
        break;
    case IR_FILTER_MEDIAN:
        memcpy(dst, median, MLX_RAW_PIXELS * sizeof(float));
        break;
    case IR_FILTER_GAUSSIAN:
        spatial_gaussian_denoise(median, dst, MLX_RAW_W, MLX_RAW_H);
        break;
    case IR_FILTER_BILATERAL:
        spatial_bilateral_denoise(median, dst, MLX_RAW_W, MLX_RAW_H, 1);
        break;
    case IR_FILTER_BSPLINE:
    default:
        spatial_bilateral_denoise(median, dst, MLX_RAW_W, MLX_RAW_H, 2);
        break;
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

    ir_camera_update_transform();
    s_thermal_img_dsc.data = (const uint8_t *)s_display_buf[0];
    return ESP_OK;
}

/*
 * 初始化热成像颜色查找表（直接复用 ir_color_modes.c 的颜色配置逻辑）
 */
static void init_thermal_color_lut(ir_color_mode_t mode)
{
    s_cur_color_mode = mode;
    ir_color_generate_lut(mode, s_color_lut);
    if (g_ir_settings.color_reverse) {
        for (int i = 0; i < 128; i++) {
            uint16_t tmp = s_color_lut[i];
            s_color_lut[i] = s_color_lut[255 - i];
            s_color_lut[255 - i] = tmp;
        }
    }
}

/**
 * @brief 配置IR颜色模式
 * 
 * @param mode 
 */
void ir_camera_set_color_mode(ir_color_mode_t mode)
{
    if (mode >= IR_COLOR_MODE_MAX) return;
    g_ir_settings.color_mode = mode;
    init_thermal_color_lut(mode);
}

ir_color_mode_t ir_camera_get_color_mode(void)
{
    return s_cur_color_mode;
}

static void settings_clamp_to_valid_ranges(void)
{
    if (g_ir_settings.color_mode >= IR_COLOR_MODE_MAX) {
        g_ir_settings.color_mode = IR_COLOR_IRONBOW;
    }
    if (g_ir_settings.rotate >= IR_ROT_MAX) {
        g_ir_settings.rotate = IR_ROT_0;
    }
    if (g_ir_settings.temp_scale >= IR_SCALE_MAX) {
        g_ir_settings.temp_scale = IR_SCALE_CELSIUS;
    }
    if (g_ir_settings.filter_mode >= IR_FILTER_MAX) {
        g_ir_settings.filter_mode = IR_FILTER_BSPLINE;
    }
    if (g_ir_settings.chart_source >= IR_CHART_SRC_COUNT) {
        g_ir_settings.chart_source = IR_CHART_SRC_CENTER;
    }

    g_ir_settings.fixture_px_x = (g_ir_settings.fixture_px_x < 0)
                               ? 0 : g_ir_settings.fixture_px_x;
    g_ir_settings.fixture_px_y = (g_ir_settings.fixture_px_y < 0)
                               ? 0 : g_ir_settings.fixture_px_y;
    if (g_ir_settings.fixture_px_x >= MLX_INTERP_W) {
        g_ir_settings.fixture_px_x = MLX_INTERP_W - 1;
    }
    if (g_ir_settings.fixture_px_y >= MLX_INTERP_H) {
        g_ir_settings.fixture_px_y = MLX_INTERP_H - 1;
    }

    if (g_ir_settings.brightness < 5) g_ir_settings.brightness = 5;
    if (g_ir_settings.brightness > 100) g_ir_settings.brightness = 100;
    if (g_ir_settings.auto_sleep_min < 0) g_ir_settings.auto_sleep_min = 0;
    if (g_ir_settings.auto_sleep_min > 9999) g_ir_settings.auto_sleep_min = 9999;

    g_ir_settings.emissivity = clampf_local(g_ir_settings.emissivity, 0.10f, 1.00f);
    g_ir_settings.gamma = clampf_local(g_ir_settings.gamma, 0.50f, 3.00f);
    g_ir_settings.temperature_offset_c =
        clampf_local(g_ir_settings.temperature_offset_c, -10.0f, 10.0f);
}

esp_err_t ir_camera_settings_load(void)
{
    g_ir_settings = s_default_settings;
    bool need_save_defaults = false;
    bool need_save_settings = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(IR_SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        ir_settings_nvs_blob_t blob = {0};
        size_t size = sizeof(blob);
        err = nvs_get_blob(handle, IR_SETTINGS_NVS_KEY, &blob, &size);
        nvs_close(handle);

        const size_t legacy_size = sizeof(blob) - sizeof(blob.settings.reboot_cnt);
        const bool current_blob = (size == sizeof(blob) &&
                                   blob.version == IR_SETTINGS_VERSION);
        const bool legacy_blob = (size == legacy_size &&
                                  (blob.version == 2U || blob.version == 3U));

        if (err == ESP_OK && blob.magic == IR_SETTINGS_MAGIC &&
            (current_blob || legacy_blob)) {
            g_ir_settings = blob.settings;
            /* v2/v3 没有 reboot_cnt；blob 预先清零使其从 0 开始，其他设置原样保留。 */
            if (legacy_blob) {
                need_save_settings = true;
            }
        } else {
            /* 版本号升级或首次无数据时，使用最新默认配置并覆写 NVS */
            need_save_defaults = true;
            err = ESP_OK;
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        need_save_defaults = true;
        err = ESP_OK;
    }

    /* 固定本机热像左右方向，防止 NVS 中旧值或设置页旧值覆盖。 */
    if (g_ir_settings.mirror_horizontal != ir_horizontal_mirror_enabled()) {
        need_save_settings = true;
    }
    g_ir_settings.mirror_horizontal = ir_horizontal_mirror_enabled();
    ESP_LOGI(TAG, "Display transform: rot=%d mirror_x=%d mirror_y=%d (horizontal mirror forced)",
             (int)g_ir_settings.rotate,
             (int)g_ir_settings.mirror_horizontal,
             (int)g_ir_settings.mirror_vertical);

    settings_clamp_to_valid_ranges();

    /* 每次启动加载设置时计数一次，并立即持久化；溢出时保持最大值。 */
    if (g_ir_settings.reboot_cnt < UINT32_MAX) {
        g_ir_settings.reboot_cnt++;
    }
    need_save_settings = true;

    ir_camera_update_transform();
    ir_camera_set_color_mode(g_ir_settings.color_mode);

    if (need_save_defaults || need_save_settings) {
        esp_err_t save_err = ir_camera_settings_save();
        if (save_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save reboot count: %s", esp_err_to_name(save_err));
        }
    }

    ESP_LOGI(TAG, "ESP32_IR_Reboot count: %u", (unsigned int)g_ir_settings.reboot_cnt);
    if (g_ir_settings.reboot_cnt > 8U) {
        
        while (1)
        {
            ESP_LOGE(TAG, "ESP32_IR_Reboot count exceeded 3: %u",
                     (unsigned int)g_ir_settings.reboot_cnt);
        }
        
    }
    return err;
}

esp_err_t ir_camera_settings_save(void)
{
    g_ir_settings.mirror_horizontal = ir_horizontal_mirror_enabled();
    settings_clamp_to_valid_ranges();

    ir_settings_nvs_blob_t blob = {
        .magic = IR_SETTINGS_MAGIC,
        .version = IR_SETTINGS_VERSION,
        .settings = g_ir_settings,
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(IR_SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, IR_SETTINGS_NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

void ir_camera_settings_reset(void)
{
    g_ir_settings = s_default_settings;
    ir_camera_settings_save();
    ir_camera_update_transform();
    ir_camera_set_color_mode(g_ir_settings.color_mode);
    ir_camera_chart_clear();
}

/* 获取当前正在显示的热成像帧（280x240 RGB565），未就绪时返回 NULL */
const uint8_t *ir_camera_get_frame(void)
{
    return s_thermal_img_dsc.data;
}

/* 复制当前归一化温度矩阵（32x24，每像素 0-255），供拍照压缩存储 */
void ir_camera_get_norm_matrix(uint8_t *out)
{
    if (out) memcpy(out, s_norm_snapshot, sizeof(s_norm_snapshot));
}

/* 获取当前量程（供后续重归一化） */
void ir_camera_get_snapshot_range(float *min_t, float *max_t)
{
    if (min_t) *min_t = s_snapshot_min_t;
    if (max_t) *max_t = s_snapshot_max_t;
}

void ir_camera_update_transform(void)
{
    if (g_ir_settings.rotate == IR_ROT_90 || g_ir_settings.rotate == IR_ROT_270) {
        s_source_col_step_fp =
            ((uint32_t)(MLX_RAW_W - 1) << 16) / (uint32_t)(MLX_INTERP_H - 1);
        s_source_row_step_fp =
            ((uint32_t)(MLX_RAW_H - 1) << 16) / (uint32_t)(MLX_INTERP_W - 1);
    } else {
        s_source_col_step_fp =
            ((uint32_t)(MLX_RAW_W - 1) << 16) / (uint32_t)(MLX_INTERP_W - 1);
        s_source_row_step_fp =
            ((uint32_t)(MLX_RAW_H - 1) << 16) / (uint32_t)(MLX_INTERP_H - 1);
    }
    s_transform_valid = true;
}

static inline void screen_xy_to_source_fp(int x, int y, int src_w, int src_h,
                                          int dst_w, int dst_h,
                                          uint32_t *col_fp, uint32_t *row_fp)
{
    int mx = ir_horizontal_mirror_enabled() ? (dst_w - 1 - x) : x;
    int my = g_ir_settings.mirror_vertical ? (dst_h - 1 - y) : y;

    switch (g_ir_settings.rotate) {
    case IR_ROT_90:
        *col_fp = (uint32_t)my * s_source_col_step_fp;
        *row_fp = (uint32_t)(dst_w - 1 - mx) * s_source_row_step_fp;
        break;
    case IR_ROT_180:
        *col_fp = (uint32_t)(dst_w - 1 - mx) * s_source_col_step_fp;
        *row_fp = (uint32_t)(dst_h - 1 - my) * s_source_row_step_fp;
        break;
    case IR_ROT_270:
        *col_fp = (uint32_t)(dst_h - 1 - my) * s_source_col_step_fp;
        *row_fp = (uint32_t)mx * s_source_row_step_fp;
        break;
    case IR_ROT_0:
    default:
        *col_fp = (uint32_t)mx * s_source_col_step_fp;
        *row_fp = (uint32_t)my * s_source_row_step_fp;
        break;
    }

    if (*col_fp >= (uint32_t)(src_w - 1) << 16) {
        *col_fp = (uint32_t)(src_w - 1) << 16;
    }
    if (*row_fp >= (uint32_t)(src_h - 1) << 16) {
        *row_fp = (uint32_t)(src_h - 1) << 16;
    }
}

static inline void raw_rc_to_screen_xy(int raw_r, int raw_c, int *px_x, int *px_y)
{
    int x;
    int y;

    switch (g_ir_settings.rotate) {
    case IR_ROT_90:
        x = (MLX_INTERP_W - 1) -
            (raw_r * (MLX_INTERP_W - 1)) / (MLX_RAW_H - 1);
        y = (raw_c * (MLX_INTERP_H - 1)) / (MLX_RAW_W - 1);
        break;
    case IR_ROT_180:
        x = (MLX_INTERP_W - 1) -
            (raw_c * (MLX_INTERP_W - 1)) / (MLX_RAW_W - 1);
        y = (MLX_INTERP_H - 1) -
            (raw_r * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);
        break;
    case IR_ROT_270:
        x = (raw_r * (MLX_INTERP_W - 1)) / (MLX_RAW_H - 1);
        y = (MLX_INTERP_H - 1) -
            (raw_c * (MLX_INTERP_H - 1)) / (MLX_RAW_W - 1);
        break;
    case IR_ROT_0:
    default:
        x = (raw_c * (MLX_INTERP_W - 1)) / (MLX_RAW_W - 1);
        y = (raw_r * (MLX_INTERP_H - 1)) / (MLX_RAW_H - 1);
        break;
    }

    if (ir_horizontal_mirror_enabled()) x = (MLX_INTERP_W - 1) - x;
    if (g_ir_settings.mirror_vertical) y = (MLX_INTERP_H - 1) - y;

    *px_x = clamp_int_local(x, 0, MLX_INTERP_W - 1);
    *px_y = clamp_int_local(y, 0, MLX_INTERP_H - 1);
}

static inline int screen_xy_to_raw_idx(int px_x, int px_y)
{
    uint32_t col_fp;
    uint32_t row_fp;

    px_x = clamp_int_local(px_x, 0, MLX_INTERP_W - 1);
    px_y = clamp_int_local(px_y, 0, MLX_INTERP_H - 1);
    screen_xy_to_source_fp(px_x, px_y, MLX_RAW_W, MLX_RAW_H,
                           MLX_INTERP_W, MLX_INTERP_H, &col_fp, &row_fp);

    int raw_c = (int)(col_fp >> 16);
    int raw_r = (int)(row_fp >> 16);
    raw_c = clamp_int_local(raw_c, 0, MLX_RAW_W - 1);
    raw_r = clamp_int_local(raw_r, 0, MLX_RAW_H - 1);
    return raw_r * MLX_RAW_W + raw_c;
}

static inline void catmull_rom_weights_q12(float t, int16_t weights[4])
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    const int w0 = (int)lrintf((-0.5f * t + t2 - 0.5f * t3) * 4096.0f);
    const int w2 = (int)lrintf((0.5f * t + 2.0f * t2 - 1.5f * t3) * 4096.0f);
    const int w3 = (int)lrintf((-0.5f * t2 + 0.5f * t3) * 4096.0f);

    weights[0] = (int16_t)w0;
    weights[1] = (int16_t)(4096 - w0 - w2 - w3);
    weights[2] = (int16_t)w2;
    weights[3] = (int16_t)w3;
}

static inline void bspline_weights_q12(float t, int16_t weights[4])
{
    const float inv_six = 4096.0f / 6.0f;
    const float it = 1.0f - t;
    const float t2 = t * t;
    const float t3 = t2 * t;
    const int w0 = (int)lrintf(it * it * it * inv_six);
    const int w2 = (int)lrintf((-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) *
                               inv_six);
    const int w3 = (int)lrintf(t3 * inv_six);

    weights[0] = (int16_t)w0;
    weights[1] = (int16_t)(4096 - w0 - w2 - w3);
    weights[2] = (int16_t)w2;
    weights[3] = (int16_t)w3;
}

static inline void interp_weights_q12(float t, bool use_bspline,
                                      int16_t weights[4])
{
    if (use_bspline) {
        bspline_weights_q12(t, weights);
    } else {
        catmull_rom_weights_q12(t, weights);
    }
}

static inline int32_t round_shift_signed(int32_t value, unsigned int shift)
{
    const int32_t half = 1 << (shift - 1);
    return value >= 0 ? (value + half) >> shift
                      : -(((-value) + half) >> shift);
}

/*
 * Separable Catmull-Rom scaling. Intermediate samples stay 16-bit and are
 * clamped after each pass to prevent spline overshoot from creating halos.
 */
static void catmull_rom_scale_fullscreen_1to1(const uint16_t *src, int src_w, int src_h,
                                              uint16_t *dst, int dst_w, int dst_h,
                                              const uint16_t *lut)
{
    static uint16_t tmp[MLX_RAW_W * MLX_INTERP_W];
    static uint8_t x_idx[MLX_INTERP_W][4];
    static int16_t x_weights[MLX_INTERP_W][4];
    static uint16_t transposed[MLX_RAW_PIXELS];

    if (!src || !dst || !lut || src_w <= 1 || src_h <= 1 ||
        src_w > MLX_RAW_W || src_h > MLX_RAW_H ||
        dst_w <= 0 || dst_h <= 0 ||
        dst_w > MLX_INTERP_W || dst_h > MLX_INTERP_H) {
        return;
    }
    if (!s_transform_valid) ir_camera_update_transform();

    const ir_rotate_t rot = g_ir_settings.rotate;
    const bool swap = (rot == IR_ROT_90 || rot == IR_ROT_270);
    const bool mirror_x = ir_horizontal_mirror_enabled();
    const bool flip_x = (rot == IR_ROT_90 || rot == IR_ROT_180)
                        ? !mirror_x : mirror_x;
    const bool flip_y = (rot == IR_ROT_180 || rot == IR_ROT_270)
                        ? !g_ir_settings.mirror_vertical
                        : g_ir_settings.mirror_vertical;
    const bool use_bspline = (g_ir_settings.filter_mode == IR_FILTER_BSPLINE);

    const uint16_t *input = src;
    int input_w = src_w;
    int input_h = src_h;
    uint32_t step_x = s_source_col_step_fp;
    uint32_t step_y = s_source_row_step_fp;
    if (swap) {
        for (int r = 0; r < src_h; r++) {
            for (int c = 0; c < src_w; c++) {
                transposed[c * src_h + r] = src[r * src_w + c];
            }
        }
        input = transposed;
        input_w = src_h;
        input_h = src_w;
        step_x = s_source_row_step_fp;
        step_y = s_source_col_step_fp;
    }

    const uint32_t max_x_fp = (uint32_t)(input_w - 1) << 16;
    const uint32_t max_y_fp = (uint32_t)(input_h - 1) << 16;

    for (int x = 0; x < dst_w; x++) {
        int eff_x = flip_x ? dst_w - 1 - x : x;
        uint32_t pos = (uint32_t)eff_x * step_x;
        if (pos > max_x_fp) pos = max_x_fp;
        int base = (int)(pos >> 16);
        float frac = (float)(pos & 0xFFFFU) * (1.0f / 65536.0f);
        interp_weights_q12(frac, use_bspline, x_weights[x]);
        for (int k = 0; k < 4; k++) {
            x_idx[x][k] = (uint8_t)clamp_int_local(base - 1 + k, 0, input_w - 1);
        }
    }

    for (int r = 0; r < input_h; r++) {
        const uint16_t *row = &input[r * input_w];
        uint16_t *tmp_row = &tmp[r * dst_w];
        for (int x = 0; x < dst_w; x++) {
            int32_t value =
                (int32_t)x_weights[x][0] * row[x_idx[x][0]] +
                (int32_t)x_weights[x][1] * row[x_idx[x][1]] +
                (int32_t)x_weights[x][2] * row[x_idx[x][2]] +
                (int32_t)x_weights[x][3] * row[x_idx[x][3]];
            tmp_row[x] = (uint16_t)clamp_int_local(round_shift_signed(value, 12),
                                                   0, UINT16_MAX);
        }
    }

    for (int y = 0; y < dst_h; y++) {
        int eff_y = flip_y ? dst_h - 1 - y : y;
        uint32_t pos = (uint32_t)eff_y * step_y;
        if (pos > max_y_fp) pos = max_y_fp;
        int base = (int)(pos >> 16);
        float frac = (float)(pos & 0xFFFFU) * (1.0f / 65536.0f);
        int16_t y_weights[4];
        interp_weights_q12(frac, use_bspline, y_weights);

        const uint16_t *rows[4];
        for (int k = 0; k < 4; k++) {
            int row = clamp_int_local(base - 1 + k, 0, input_h - 1);
            rows[k] = &tmp[row * dst_w];
        }

        uint16_t *dst_row = &dst[y * dst_w];
        for (int x = 0; x < dst_w; x++) {
            int32_t value =
                (int32_t)y_weights[0] * rows[0][x] +
                (int32_t)y_weights[1] * rows[1][x] +
                (int32_t)y_weights[2] * rows[2][x] +
                (int32_t)y_weights[3] * rows[3][x];
            int32_t sample = round_shift_signed(value, 12);
            sample = clamp_int_local(sample, 0, UINT16_MAX);
            dst_row[x] = lut[(uint32_t)sample >> 8];
        }
    }
}

/* 把归一化矩阵渲染为 280x240 RGB565（用指定色彩模式） */
void ir_camera_render_norm(const uint8_t *norm, ir_color_mode_t mode, uint16_t *dst)
{
    if (!norm || !dst) return;
    uint16_t lut[256];
    static uint16_t norm16[MLX_RAW_PIXELS];
    ir_color_generate_lut(mode, lut);
    if (g_ir_settings.color_reverse) {
        for (int i = 0; i < 128; i++) {
            uint16_t tmp = lut[i];
            lut[i] = lut[255 - i];
            lut[255 - i] = tmp;
        }
    }
    for (int i = 0; i < MLX_RAW_PIXELS; i++) {
        norm16[i] = (uint16_t)(norm[i] * 257U);
    }
    catmull_rom_scale_fullscreen_1to1(norm16, MLX_RAW_W, MLX_RAW_H,
                                      dst, MLX_INTERP_W, MLX_INTERP_H, lut);
}

float ir_camera_convert_temp(float temp_c, const char **unit_str)
{
    switch (g_ir_settings.temp_scale) {
    case IR_SCALE_FAHRENHEIT:
        if (unit_str) *unit_str = "F";
        return temp_c * 1.8f + 32.0f;
    case IR_SCALE_KELVIN:
        if (unit_str) *unit_str = "K";
        return temp_c + 273.15f;
    case IR_SCALE_RANKINE:
        if (unit_str) *unit_str = "R";
        return (temp_c + 273.15f) * 1.8f;
    case IR_SCALE_CELSIUS:
    default:
        if (unit_str) *unit_str = "C";
        return temp_c;
    }
}

void ir_camera_chart_clear(void)
{
    portENTER_CRITICAL(&s_chart_mux);
    s_chart_head = 0;
    s_chart_count = 0;
    portEXIT_CRITICAL(&s_chart_mux);
}

static void ir_camera_chart_push(float temp_c)
{
    if (!ir_temperature_is_valid(temp_c)) return;

    portENTER_CRITICAL(&s_chart_mux);
    s_chart_buf[s_chart_head] = temp_c;
    s_chart_head = (s_chart_head + 1) % IR_CHART_HISTORY_LEN;
    if (s_chart_count < IR_CHART_HISTORY_LEN) {
        s_chart_count++;
    }
    portEXIT_CRITICAL(&s_chart_mux);
}

void ir_camera_chart_get_data(float *out_buf, int *out_count)
{
    portENTER_CRITICAL(&s_chart_mux);
    int count = s_chart_count;
    int first = (s_chart_head + IR_CHART_HISTORY_LEN - count) % IR_CHART_HISTORY_LEN;
    if (out_count) *out_count = count;
    if (out_buf) {
        for (int i = 0; i < count; i++) {
            out_buf[i] = s_chart_buf[(first + i) % IR_CHART_HISTORY_LEN];
        }
    }
    portEXIT_CRITICAL(&s_chart_mux);
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

static void temperature_to_norm(const float *src, uint8_t *dst8, uint16_t *dst16,
                                float smooth_min_t, float smooth_max_t)
{
    float map_min = THERMAL_USE_AUTO_RANGE ? smooth_min_t : THERMAL_FIXED_MIN_C;
    float map_max = THERMAL_USE_AUTO_RANGE ? smooth_max_t : THERMAL_FIXED_MAX_C;

    float delta = map_max - map_min;
    if (delta < 1.5f) {
        delta = 1.5f;
    }

    const float range_inv = 1.0f / delta;
    const float noise_gate = clampf_local(0.55f * range_inv, 0.02f, 0.15f);
    const float inv_above_gate = 1.0f / (1.0f - noise_gate);
    const float low_delta_blend =
        clampf_local((15.0f - delta) / 9.0f, 0.0f, 1.0f);
    float user_gamma = clampf_local(g_ir_settings.gamma, 0.5f, 3.0f);
    const float auto_gamma = 1.12f - 0.40f * low_delta_blend;
    const float effective_gamma =
        clampf_local(auto_gamma * (user_gamma / 1.5f), 0.55f, 2.2f);
    const bool use_linear = fabsf(effective_gamma - 1.0f) < 0.03f;

    for (int i = 0; i < MLX_RAW_PIXELS; i++) {
        if (!ir_temperature_is_valid(src[i])) {
            dst8[i] = 0;
            dst16[i] = 0;
            continue;
        }
        float norm = (src[i] - map_min) * range_inv;
        norm = clampf_local(norm, 0.0f, 1.0f);

        if (norm <= noise_gate) {
            norm = (norm / noise_gate) * 0.03f;
        } else {
            float t = (norm - noise_gate) * inv_above_gate;
            float mapped = use_linear ? t : powf(t, effective_gamma);
            norm = 0.03f + 0.97f * mapped;
        }

        dst8[i] = (uint8_t)(norm * 255.0f + 0.5f);
        dst16[i] = (uint16_t)(norm * 65535.0f + 0.5f);
    }
}

/* 当前实际生效的传感器刷新率，避免重复写寄存器 */
static uint8_t s_cur_refresh_rate = RefreshRate;

/* 仅在目标刷新率变化时写 CTRL（写刷新率会重启传感器测量周期） */
static void ir_set_refresh_rate(uint8_t rate)
{
    if (rate != s_cur_refresh_rate) {
        MLX90640_SetRefreshRate(MLX90640_ADDR, rate);
        s_cur_refresh_rate = rate;
    }
}

void ir_camera_task_main(void *pvParameters)
{
    (void)pvParameters;

    esp_pm_lock_handle_t cpu_freq_lock = NULL;
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0,
                                      "ir_frame", &cpu_freq_lock));

    ESP_LOGI(TAG, "IR camera task started on Core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "Thermal map: fixed range %.1fC ~ %.1fC, auto_range=%d",
             THERMAL_FIXED_MIN_C, THERMAL_FIXED_MAX_C, THERMAL_USE_AUTO_RANGE);

    init_thermal_color_lut(g_ir_settings.color_mode);

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

    int ee_status = MLX90640_DumpEE(MLX90640_ADDR, eerom_ir_data);
    if (ee_status != 0) {
        ESP_LOGE(TAG, "Failed to read MLX90640 calibration EEPROM: %d", ee_status);
        vTaskDelete(NULL);
        return;
    }
    int params_status = MLX90640_ExtractParameters(eerom_ir_data, mlx_params);
    if (params_status < 0) {
        ESP_LOGE(TAG, "Invalid MLX90640 calibration data, bad pixel count: %d",
                 -params_status);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "MLX90640 calibration data loaded (bad pixels: %d)",
             params_status);
    free(eerom_ir_data);
    eerom_ir_data = NULL;
    ESP_LOGI(TAG, "Temperature settings: emissivity=%.2f offset=%+.1fC TaShift=%dC",
             (double)g_ir_settings.emissivity,
             (double)g_ir_settings.temperature_offset_c, TA_SHIFT);
    ESP_LOGI(TAG,
             "Device temperature calibration: hot %.1fC->%.1fC, %.3fC->%.1fC; cold range unadjusted",
             (double)IR_TEMP_CAL_ANCHOR_C,
             (double)IR_TEMP_CAL_ANCHOR_C,
             (double)IR_TEMP_CAL_SENSOR_HIGH_C,
             (double)IR_TEMP_CAL_REFERENCE_C);

    ESP_LOGI(TAG, "Warming up MLX90640 sensor...");
    for (int i = 0; i < 4; i++) {
        MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    char text_max_info[32];
    char text_min_info[32];
    char text_fix_info[32];
    char text_max_tag[32];
    char text_min_tag[32];
    char text_fix_tag[32];

    int64_t last_temp_diag_us = esp_timer_get_time();
    int64_t last_chart_us = 0;

    float smooth_min_t = THERMAL_FIXED_MIN_C;
    float smooth_max_t = THERMAL_FIXED_MAX_C;
    float smooth_ta    = 25.0f;
    bool ta_initialized = false;
    bool range_initialized = false;

    bool subpage_init[2] = {false, false};
    bool temperature_initialized = false;
    bool ui_was_blocked = false;
    int  resync_left = 0;

    while (1) {
        /* 降发热优化：正常显示保持 16Hz（流畅度不变），仅热像被遮挡/息屏时降频。
         * UI 状态查询必须持 LVGL 锁，避免读到被删除重建的菜单/相册对象。 */
        bool ui_blocked = false;
        bool chart_active = false;
        bool screen_sleeping = false;
        if (example_lvgl_lock(50)) {
            screen_sleeping = ir_settings_ui_screen_sleeping();
            ui_blocked = ir_settings_ui_is_open() ||
                         thermal_ui_view_blocking() ||
                         screen_sleeping;
            chart_active = ir_settings_ui_chart_page_active();
            example_lvgl_unlock();
        }

        if (ui_blocked && !chart_active) {
            /* 完全遮挡：降频 + 只读帧清 data-ready，不跑滤波流水线 */
            uint8_t idle_rate = screen_sleeping ? IR_IDLE_REFRESH_SLEEP : IR_IDLE_REFRESH_MENU;
            ir_set_refresh_rate(idle_rate);
            ui_was_blocked = true;
            MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (ui_was_blocked) {
            /* 刚退出遮挡：恢复 16Hz + 重新同步，避免半旧半新错图 */
            ui_was_blocked = false;
            ir_set_refresh_rate(RefreshRate);
            MLX90640_I2CWrite(MLX90640_ADDR, MLX90640_STATUS_REG,
                              MLX90640_INIT_STATUS_VALUE);
            resync_left = IR_RESYNC_FRAMES;
        }

        int status = MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);

        if (status >= 0) {
            int subpage = ir_frame_data[833];
            if (subpage < 0 || subpage > 1) {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            ESP_ERROR_CHECK(esp_pm_lock_acquire(cpu_freq_lock));

            float raw_ta = MLX90640_GetTa(ir_frame_data, mlx_params);
            if (!ta_initialized || !isfinite(smooth_ta)) {
                smooth_ta = raw_ta;
                ta_initialized = true;
            } else if (isfinite(raw_ta)) {
                smooth_ta += 0.12f * (raw_ta - smooth_ta);
            }

            // 1. 官方原生计算：增量更新当前 subpage 的 384 个点
            MLX90640_CalculateTo(ir_frame_data, mlx_params,
                                 g_ir_settings.emissivity,
                                 smooth_ta - TA_SHIFT, raw_temp_matrix);

            int mode = (ir_frame_data[832] & MLX90640_CTRL_MEAS_MODE_MASK) >> 5;
            MLX90640_BadPixelsCorrection(mlx_params->brokenPixels, raw_temp_matrix, mode, mlx_params);
            MLX90640_BadPixelsCorrection(mlx_params->outlierPixels, raw_temp_matrix, mode, mlx_params);

            subpage_init[subpage] = true;
            if (resync_left > 0) {
                /* 丢弃遮挡前残留的半帧，两个子场都刷新成新数据后再显示 */
                if (--resync_left == 0) {
                    memcpy(filtered_temp, raw_temp_matrix, MLX_RAW_PIXELS * sizeof(float));
                    temperature_initialized = true;
                    range_initialized = false;   /* 让自动量程重新平滑，避免色阶跳变 */
                }
                ESP_ERROR_CHECK(esp_pm_lock_release(cpu_freq_lock));
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            if (!subpage_init[0] || !subpage_init[1]) {
                ESP_ERROR_CHECK(esp_pm_lock_release(cpu_freq_lock));
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            if (!temperature_initialized) {
                /* 等两个子场都采到数据后初始化，避免把0℃当成未初始化标记。 */
                memcpy(filtered_temp, raw_temp_matrix, MLX_RAW_PIXELS * sizeof(float));
                temperature_initialized = true;
                ESP_ERROR_CHECK(esp_pm_lock_release(cpu_freq_lock));
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            // 2. 分场时域平滑 + 防趾蹼消交错
            update_temporal_and_deinterlace(raw_temp_matrix, filtered_temp,
                                            MLX_RAW_W, MLX_RAW_H, subpage);

            // 3. 中值异常点抑制：平坦区滤噪，手指/指缝区保护连通特征
            spatial_median_outlier_filter(filtered_temp, median_temp,
                                          MLX_RAW_W, MLX_RAW_H);

            bool temperature_diag_due =
                esp_timer_get_time() - last_temp_diag_us >= 1000000;
            float raw_stage_max = IR_TEMP_MIN_VALID_C;
            float temporal_stage_max = IR_TEMP_MIN_VALID_C;
            float median_stage_max = IR_TEMP_MIN_VALID_C;
            float raw_stage_min = IR_TEMP_MAX_VALID_C;
            float temporal_stage_min = IR_TEMP_MAX_VALID_C;
            float median_stage_min = IR_TEMP_MAX_VALID_C;
            if (temperature_diag_due) {
                raw_stage_max = max_valid_temperature(raw_temp_matrix);
                temporal_stage_max = max_valid_temperature(filtered_temp);
                median_stage_max = max_valid_temperature(median_temp);
                raw_stage_min = min_valid_temperature(raw_temp_matrix);
                temporal_stage_min = min_valid_temperature(filtered_temp);
                median_stage_min = min_valid_temperature(median_temp);
            }

            // 4. 按设置页选择的滤波算法生成测温/显示矩阵；默认双边滤波优先保留中心像素与边缘
            apply_spatial_filter(filtered_temp, median_temp, spatial_temp,
                                 (int)g_ir_settings.filter_mode);

            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                if (ir_temperature_is_valid(spatial_temp[i])) {
                    spatial_temp[i] = ir_temperature_calibrate(spatial_temp[i]);
                }
            }

            /* 出厂校准偏移叠加到最终输出，不破坏原始滤波状态。 */
            float calibration_offset = g_ir_settings.temperature_offset_c;
            if (fabsf(calibration_offset) > 0.001f) {
                for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                    if (isfinite(spatial_temp[i])) {
                        spatial_temp[i] += calibration_offset;
                    }
                }
            }

            // 5. 统计极值
            float cur_min = IR_TEMP_MAX_VALID_C;
            float cur_max = IR_TEMP_MIN_VALID_C;
            int max_idx = 0;
            int min_idx = 0;
            int valid_pts = 0;

            for (int i = 0; i < MLX_RAW_PIXELS; i++) {
                float val = spatial_temp[i];
                if (ir_temperature_is_valid(val)) {
                    if (val < cur_min) { cur_min = val; min_idx = i; }
                    if (val > cur_max) { cur_max = val; max_idx = i; }
                    median_temp[valid_pts++] = val;
                }
            }

            if (valid_pts <= 0) {
                ESP_ERROR_CHECK(esp_pm_lock_release(cpu_freq_lock));
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

#if THERMAL_USE_AUTO_RANGE
            float robust_min = cur_min;
            if (valid_pts >= 100) {
                int low_idx = (valid_pts - 1) * THERMAL_PERCENTILE_LOW / 100;
                select_nth_float(median_temp, valid_pts, low_idx);
                robust_min = median_temp[low_idx];
            }

            float span_est = cur_max - robust_min;
            float low_padding = fmaxf(AUTO_RANGE_LOW_PADDING_C,
                                      AUTO_RANGE_LOW_PADDING_FRAC * span_est);
            float headroom = fmaxf(AUTO_RANGE_HEADROOM_MIN_C,
                                   AUTO_RANGE_HEADROOM_FRAC * span_est);
            float target_min = robust_min - low_padding;
            float target_max = cur_max + headroom;

            if ((target_max - target_min) < AUTO_RANGE_MIN_SPAN_C) {
                float mid = 0.5f * (target_min + target_max);
                target_min = mid - 0.5f * AUTO_RANGE_MIN_SPAN_C;
                target_max = mid + 0.5f * AUTO_RANGE_MIN_SPAN_C;
            }

            if (!range_initialized) {
                smooth_min_t = target_min;
                smooth_max_t = target_max;
                range_initialized = true;
            } else {
                /* 降温时快速下移色阶，避免高温场景结束后低温画面长时间发暗。 */
                float alpha_min = (target_min < smooth_min_t)
                                ? AUTO_RANGE_FALL_ALPHA : AUTO_RANGE_ALPHA;
                float alpha_max = (target_max < smooth_max_t)
                                ? AUTO_RANGE_FALL_ALPHA : AUTO_RANGE_RISE_ALPHA;
                smooth_min_t += alpha_min * (target_min - smooth_min_t);
                smooth_max_t += alpha_max * (target_max - smooth_max_t);
            }
#endif

            // 6. 帧率统计与 HUD 字符串格式化
            int64_t now_us = esp_timer_get_time();

            int center_idx = screen_xy_to_raw_idx(MLX_INTERP_W / 2,
                                                  MLX_INTERP_H / 2);
            int fixture_idx = screen_xy_to_raw_idx(g_ir_settings.fixture_px_x,
                                                   g_ir_settings.fixture_px_y);
            float center_t = spatial_temp[center_idx];
            float fixture_t = spatial_temp[fixture_idx];
            float record_t = g_ir_settings.label_fixture ? fixture_t : center_t;

            float chart_t = center_t;
            switch (g_ir_settings.chart_source) {
            case IR_CHART_SRC_FIXTURE: chart_t = fixture_t; break;
            case IR_CHART_SRC_MAX: chart_t = cur_max; break;
            case IR_CHART_SRC_MIN: chart_t = cur_min; break;
            default: chart_t = center_t; break;
            }

            int64_t chart_now_us = esp_timer_get_time();
            if (last_chart_us == 0 ||
                chart_now_us - last_chart_us >= (int64_t)IR_CHART_SAMPLE_MS * 1000) {
                ir_camera_chart_push(chart_t);
                last_chart_us = chart_now_us;
            }

            const char *unit = "C";
            float disp_max = ir_camera_convert_temp(cur_max, &unit);
            float disp_min = ir_camera_convert_temp(cur_min, NULL);
            float disp_record = ir_camera_convert_temp(record_t, NULL);
            if (temperature_diag_due) {
                ESP_LOGI(TAG,
                         "Temperature stages min: raw=%.1f temporal=%.1f median=%.1f final=%.1fC; max: raw=%.1f temporal=%.1f median=%.1f final=%.1fC; Ta=%.1f Tr=%.1fC",
                         (double)raw_stage_min, (double)temporal_stage_min,
                         (double)median_stage_min, (double)cur_min,
                         (double)raw_stage_max, (double)temporal_stage_max,
                         (double)median_stage_max, (double)cur_max,
                         (double)smooth_ta, (double)(smooth_ta - TA_SHIFT));
                last_temp_diag_us = now_us;
            }

            snprintf(text_max_info, sizeof(text_max_info), "MAX %.1f°%s",
                     disp_max, unit);
            snprintf(text_min_info, sizeof(text_min_info), "MIN %.1f°%s",
                     disp_min, unit);
            snprintf(text_fix_info, sizeof(text_fix_info), "%s %.1f°%s",
                     g_ir_settings.label_fixture ? "FIX" : "CEN",
                     disp_record, unit);
            snprintf(text_max_tag, sizeof(text_max_tag), "%s %.1f %s",
                     lang_str(STR_MAX), disp_max, unit);
            snprintf(text_min_tag, sizeof(text_min_tag), "%s %.1f %s",
                     lang_str(STR_MIN), disp_min, unit);
            snprintf(text_fix_tag, sizeof(text_fix_tag), "%s %.1f %s",
                     lang_str(STR_FIX),
                     ir_camera_convert_temp(fixture_t, NULL), unit);

            // 7. 保留 16-bit 归一化精度并用 Catmull-Rom 放大送显
            if (!ui_blocked && valid_pts > 400) {
                temperature_to_norm(spatial_temp, norm_matrix, s_norm16_frame,
                                    smooth_min_t, smooth_max_t);
                memcpy(s_norm_snapshot, norm_matrix, sizeof(s_norm_snapshot));
                s_snapshot_min_t = smooth_min_t;
                s_snapshot_max_t = smooth_max_t;
                uint16_t *cur_write_buf = s_display_buf[s_back_idx];

                catmull_rom_scale_fullscreen_1to1(
                    s_norm16_frame,
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

                if (g_ir_settings.label_hottest) {
                    draw_diamond_marker(cur_write_buf, max_px_x, max_px_y,
                                        MLX_INTERP_W, MLX_INTERP_H, 5, 0xF800);
                }
                if (g_ir_settings.label_coldest) {
                    draw_diamond_marker(cur_write_buf, min_px_x, min_px_y,
                                        MLX_INTERP_W, MLX_INTERP_H, 5, 0x07FF);
                }
                draw_diamond_marker(cur_write_buf,
                                    g_ir_settings.label_fixture
                                        ? g_ir_settings.fixture_px_x : MLX_INTERP_W / 2,
                                    g_ir_settings.label_fixture
                                        ? g_ir_settings.fixture_px_y : MLX_INTERP_H / 2,
                                    MLX_INTERP_W, MLX_INTERP_H,
                                    g_ir_settings.label_fixture ? 5 : 3,
                                    g_ir_settings.label_fixture ? 0x07E0 : 0xFFFF);

                // 8. 刷新到 LVGL 显存
                if (example_lvgl_lock(100)) {
                    s_thermal_img_dsc.data = (const uint8_t *)cur_write_buf;
                    if (img_thermal) {
                        lv_img_set_src(img_thermal, &s_thermal_img_dsc);
                        lv_obj_invalidate(img_thermal);
                    }
                    if (label_max_info) lv_label_set_text(label_max_info, text_max_info);
                    if (label_min_info) lv_label_set_text(label_min_info, text_min_info);
                    if (label_fix_info) lv_label_set_text(label_fix_info, text_fix_info);

                    /* 温度气泡限制在热像区域内，避免进入底部温度栏 */
                    if (tag_max) {
                        if (g_ir_settings.label_hottest) {
                            lv_obj_clear_flag(tag_max, LV_OBJ_FLAG_HIDDEN);
                            place_temp_tag(tag_max, max_px_x, max_px_y, text_max_tag);
                        } else {
                            lv_obj_add_flag(tag_max, LV_OBJ_FLAG_HIDDEN);
                        }
                    }
                    if (tag_min) {
                        if (g_ir_settings.label_coldest) {
                            lv_obj_clear_flag(tag_min, LV_OBJ_FLAG_HIDDEN);
                            place_temp_tag(tag_min, min_px_x, min_px_y, text_min_tag);
                        } else {
                            lv_obj_add_flag(tag_min, LV_OBJ_FLAG_HIDDEN);
                        }
                    }
                    if (tag_fix) {
                        if (g_ir_settings.label_fixture) {
                            lv_obj_clear_flag(tag_fix, LV_OBJ_FLAG_HIDDEN);
                            place_temp_tag(tag_fix,
                                           g_ir_settings.fixture_px_x,
                                           g_ir_settings.fixture_px_y,
                                           text_fix_tag);
                        } else {
                            lv_obj_add_flag(tag_fix, LV_OBJ_FLAG_HIDDEN);
                        }
                    }

                    if (s_display_buf[0] != s_display_buf[1]) {
                        s_back_idx = 1U - s_back_idx;
                    }
                    example_lvgl_unlock();
                }
            }
            ESP_ERROR_CHECK(esp_pm_lock_release(cpu_freq_lock));
        }

        vTaskDelay(pdMS_TO_TICKS(status >= 0 ? 2 : 10));
    }
}
