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

/* 降发热优化：正常显示保持 16Hz（流畅度不变），仅热像被遮挡/息屏时降频降耗。
 *  - 设置菜单/调色板/相册/照片查看器 -> 4Hz（功耗约 15mA，退出恢复快）
 *  - 息屏                             -> 1Hz（功耗约 10mA）
 * 退出遮挡后丢弃 IR_RESYNC_FRAMES 帧，避免"半旧半新"错图。 */
#define IR_IDLE_REFRESH_MENU   FPS4HZ
#define IR_IDLE_REFRESH_SLEEP  FPS1HZ
#define IR_RESYNC_FRAMES       2

#define IR_SCREEN_W     280
#define IR_SCREEN_H     240
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
                    if (!isfinite(value) || value <= 1.0f || value >= 200.0f) {
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
    case IR_FILTER_BSPLINE:
    default:
        /* 始终基于 3x3 鲁棒中值滤波后的 median 进行双边保边滤波，与原版流水线完全一致 */
        spatial_bilateral_denoise(median, dst, MLX_RAW_W, MLX_RAW_H);
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
    if (g_ir_settings.reboot_cnt >= 4U) {
        
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

/*
 * 全屏双线性插值（恢复原版外层行预计算优化，同时兼容 0/90/180/270 度旋转与镜像）
 */
static void bilinear_scale_fullscreen_1to1(const uint8_t *src, int src_w, int src_h,
                                           uint16_t *dst, int dst_w, int dst_h,
                                           const uint16_t *lut)
{
    if (!s_transform_valid) ir_camera_update_transform();

    const ir_rotate_t rot = g_ir_settings.rotate;
    const bool mirror_x = ir_horizontal_mirror_enabled();
    const bool flip_x = (rot == IR_ROT_90 || rot == IR_ROT_180)
                        ? !mirror_x
                        : mirror_x;
    const bool flip_y = (rot == IR_ROT_180 || rot == IR_ROT_270)
                        ? !g_ir_settings.mirror_vertical
                        : g_ir_settings.mirror_vertical;

    const uint32_t max_col_fp = (uint32_t)(src_w - 1) << 16;
    const uint32_t max_row_fp = (uint32_t)(src_h - 1) << 16;

    if (rot == IR_ROT_90 || rot == IR_ROT_270) {
        for (int y = 0; y < dst_h; y++) {
            int eff_y = flip_y ? (dst_h - 1 - y) : y;
            uint32_t c_fp = (uint32_t)eff_y * s_source_col_step_fp;
            if (c_fp > max_col_fp) c_fp = max_col_fp;

            int c0 = c_fp >> 16;
            int c1 = (c0 < src_w - 1) ? (c0 + 1) : c0;
            uint32_t c_diff = c_fp & 0xFFFFU;
            uint32_t c_diff_inv = 65536U - c_diff;
            uint16_t *dst_row = &dst[y * dst_w];

            for (int x = 0; x < dst_w; x++) {
                int eff_x = flip_x ? (dst_w - 1 - x) : x;
                uint32_t r_fp = (uint32_t)eff_x * s_source_row_step_fp;
                if (r_fp > max_row_fp) r_fp = max_row_fp;

                int r0 = r_fp >> 16;
                int r1 = (r0 < src_h - 1) ? (r0 + 1) : r0;
                uint32_t r_diff = r_fp & 0xFFFFU;
                uint32_t r_diff_inv = 65536U - r_diff;

                const uint8_t *p_r0 = &src[r0 * src_w];
                const uint8_t *p_r1 = &src[r1 * src_w];

                uint32_t row0 = (p_r0[c0] * c_diff_inv + p_r0[c1] * c_diff) >> 16;
                uint32_t row1 = (p_r1[c0] * c_diff_inv + p_r1[c1] * c_diff) >> 16;
                uint32_t val  = (row0 * r_diff_inv + row1 * r_diff) >> 16;
                if (val > 255U) val = 255U;

                dst_row[x] = lut[val];
            }
        }
    } else {
        for (int y = 0; y < dst_h; y++) {
            int eff_y = flip_y ? (dst_h - 1 - y) : y;
            uint32_t r_fp = (uint32_t)eff_y * s_source_row_step_fp;
            if (r_fp > max_row_fp) r_fp = max_row_fp;

            int r0 = r_fp >> 16;
            int r1 = (r0 < src_h - 1) ? (r0 + 1) : r0;
            uint32_t r_diff = r_fp & 0xFFFFU;
            uint32_t r_diff_inv = 65536U - r_diff;

            const uint8_t *p_r0 = &src[r0 * src_w];
            const uint8_t *p_r1 = &src[r1 * src_w];
            uint16_t *dst_row = &dst[y * dst_w];

            for (int x = 0; x < dst_w; x++) {
                int eff_x = flip_x ? (dst_w - 1 - x) : x;
                uint32_t c_fp = (uint32_t)eff_x * s_source_col_step_fp;
                if (c_fp > max_col_fp) c_fp = max_col_fp;

                int c0 = c_fp >> 16;
                int c1 = (c0 < src_w - 1) ? (c0 + 1) : c0;
                uint32_t c_diff = c_fp & 0xFFFFU;
                uint32_t c_diff_inv = 65536U - c_diff;

                uint32_t top = (p_r0[c0] * c_diff_inv + p_r0[c1] * c_diff) >> 16;
                uint32_t bot = (p_r1[c0] * c_diff_inv + p_r1[c1] * c_diff) >> 16;
                uint32_t val = (top * r_diff_inv + bot * r_diff) >> 16;
                if (val > 255U) val = 255U;

                dst_row[x] = lut[val];
            }
        }
    }
}

/* 把归一化矩阵渲染为 280x240 RGB565（用指定色彩模式） */
void ir_camera_render_norm(const uint8_t *norm, ir_color_mode_t mode, uint16_t *dst)
{
    if (!norm || !dst) return;
    uint16_t lut[256];
    ir_color_generate_lut(mode, lut);
    if (g_ir_settings.color_reverse) {
        for (int i = 0; i < 128; i++) {
            uint16_t tmp = lut[i];
            lut[i] = lut[255 - i];
            lut[255 - i] = tmp;
        }
    }
    bilinear_scale_fullscreen_1to1(norm, MLX_RAW_W, MLX_RAW_H,
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
    if (!isfinite(temp_c) || temp_c <= 1.0f || temp_c >= 200.0f) return;

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
    float gamma = clampf_local(g_ir_settings.gamma, 0.5f, 3.0f);
    bool use_default_gamma = (fabsf(gamma - 1.5f) < 0.01f);

    for (int i = 0; i < MLX_RAW_PIXELS; i++) {
        float norm = (src[i] - map_min) * range_inv;
        norm = clampf_local(norm, 0.0f, 1.0f);

        /*
         * 恢复原版温和对比度曲线 (0.65 * norm^gamma + 0.35 * norm)：
         * 默认 gamma=1.5 时使用快速 norm * sqrtf(norm)，与 origin_ir_mlx90640_app.c 完全一致；
         * 既压低背景暗部噪点，又保留手掌高温区和边缘的细节层次。
         */
        float gamma_curve = use_default_gamma ? (norm * sqrtf(norm)) : powf(norm, gamma);
        norm = 0.65f * gamma_curve + 0.35f * norm;

        dst[i] = (uint8_t)(norm * 255.0f + 0.5f);
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

    MLX90640_DumpEE(MLX90640_ADDR, eerom_ir_data);
    MLX90640_ExtractParameters(eerom_ir_data, mlx_params);
    free(eerom_ir_data);
    eerom_ir_data = NULL;

    ESP_LOGI(TAG, "Warming up MLX90640 sensor...");
    for (int i = 0; i < 4; i++) {
        MLX90640_GetFrameData(MLX90640_ADDR, ir_frame_data);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    char text_info[128];
    char text_fps[32];
    char text_max_tag[32];
    char text_min_tag[32];
    char text_fix_tag[32];

    uint32_t frame_count = 0;
    float real_fps = 0.0f;
    int64_t last_time_us = esp_timer_get_time();
    int64_t last_chart_us = 0;

    float smooth_min_t = THERMAL_FIXED_MIN_C;
    float smooth_max_t = THERMAL_FIXED_MAX_C;
    float smooth_ta    = 25.0f;
    bool ta_initialized = false;
    bool range_initialized = false;

    bool subpage_init[2] = {false, false};
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
            frame_count++;

            if (resync_left > 0) {
                /* 丢弃遮挡前残留的半帧，两个子场都刷新成新数据后再显示 */
                if (--resync_left == 0) {
                    memcpy(filtered_temp, raw_temp_matrix, MLX_RAW_PIXELS * sizeof(float));
                    range_initialized = false;   /* 让自动量程重新平滑，避免色阶跳变 */
                }
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

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

            // 4. 按设置页选择的滤波算法生成最终的测温/显示矩阵（默认与原版双边滤波完全一致）
            apply_spatial_filter(filtered_temp, median_temp, spatial_temp,
                                 (int)g_ir_settings.filter_mode);

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
            float disp_dt = disp_max - disp_min;

            snprintf(text_fps, sizeof(text_fps), "%.1f FPS", real_fps);
            snprintf(text_info, sizeof(text_info),
                     "%s %.1f %s   %s %.1f %s\n%s %.1f %s   %s %.1f %s",
                     lang_str(STR_MAX), disp_max, unit,
                     lang_str(STR_MIN), disp_min, unit,
                     g_ir_settings.label_fixture ? lang_str(STR_FIX) : lang_str(STR_CENTER),
                     disp_record, unit, lang_str(STR_DT), disp_dt, unit);
            snprintf(text_max_tag, sizeof(text_max_tag), "%s %.1f %s",
                     lang_str(STR_MAX), disp_max, unit);
            snprintf(text_min_tag, sizeof(text_min_tag), "%s %.1f %s",
                     lang_str(STR_MIN), disp_min, unit);
            snprintf(text_fix_tag, sizeof(text_fix_tag), "%s %.1f %s",
                     lang_str(STR_FIX),
                     ir_camera_convert_temp(fixture_t, NULL), unit);

            // 7. 映射并进行双线性插值送显（热像被遮挡/曲线页时不送显）
            if (!ui_blocked && valid_pts > 400) {
                temperature_to_norm_u8(spatial_temp, norm_matrix, smooth_min_t, smooth_max_t);
                memcpy(s_norm_snapshot, norm_matrix, sizeof(s_norm_snapshot));
                s_snapshot_min_t = smooth_min_t;
                s_snapshot_max_t = smooth_max_t;
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
                    if (label_fps_float) lv_label_set_text(label_fps_float, text_fps);
                    if (label_info) lv_label_set_text(label_info, text_info);
                    if (tag_emissivity) {
                        char ebuf[24];
                        snprintf(ebuf, sizeof(ebuf), "e:%.2f",
                                 (double)g_ir_settings.emissivity);
                        lv_label_set_text(tag_emissivity, ebuf);
                    }

                    /* 温度气泡夹在屏幕内，防止超出 280x240 */
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
        }

        vTaskDelay(pdMS_TO_TICKS(status >= 0 ? 2 : 10));
    }
}
