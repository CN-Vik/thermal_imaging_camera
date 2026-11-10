#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"
#include "ir_color_modes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MLX_RAW_W               32
#define MLX_RAW_H               24
#define MLX_RAW_PIXELS          (MLX_RAW_W * MLX_RAW_H)

#define MLX_W                   MLX_RAW_W
#define MLX_H                   MLX_RAW_H

// 热像图作为全屏底图铺满整块 ST7789 240x280 物理屏（UI 已旋转为横屏 280x240）
#define MLX_INTERP_W            280
#define MLX_INTERP_H            240
#define MLX_INTERP_PIXELS       (MLX_INTERP_W * MLX_INTERP_H)

#define MLX90640_ADDR           0x33
#define TA_SHIFT                8

#define FPS1HZ                  0x01
#define FPS2HZ                  0x02
#define FPS4HZ                  0x03
#define FPS8HZ                  0x04
#define FPS16HZ                 0x05
#define FPS32HZ                 0x06

/* MLX90640 刷新率（用户要求保持流畅度，固定 16Hz）。
 * 注意：刷新率是 MLX90640 自发热的主因（16Hz 连续测量功耗约 33mA），
 * 这是物理权衡——流畅度与传感器温升成正比，软件无法同时兼顾。
 */
#define RefreshRate             FPS16HZ

typedef enum {
    IR_SCALE_CELSIUS = 0,
    IR_SCALE_FAHRENHEIT,
    IR_SCALE_KELVIN,
    IR_SCALE_RANKINE,
    IR_SCALE_MAX
} ir_temp_scale_t;

typedef enum {
    IR_ROT_0 = 0,
    IR_ROT_90,
    IR_ROT_180,
    IR_ROT_270,
    IR_ROT_MAX
} ir_rotate_t;

typedef enum {
    IR_FILTER_BSPLINE = 0,
    IR_FILTER_GAUSSIAN,
    IR_FILTER_MEDIAN,
    IR_FILTER_BILATERAL,
    IR_FILTER_OFF,
    IR_FILTER_MAX
} ir_filter_mode_t;

typedef enum {
    IR_CHART_SRC_CENTER = 0,
    IR_CHART_SRC_FIXTURE,
    IR_CHART_SRC_MAX,
    IR_CHART_SRC_MIN,
    IR_CHART_SRC_COUNT
} ir_chart_source_t;

#define IR_CHART_HISTORY_LEN    60
/* 曲线采样周期（ms）：60 点 × 500ms = 30 秒时间窗口，曲线页 X 轴刻度由此换算 */
#define IR_CHART_SAMPLE_MS      500

typedef struct {
    ir_color_mode_t color_mode;
    bool color_reverse;
    bool label_coldest;
    bool label_hottest;
    bool label_fixture;
    int fixture_px_x;
    int fixture_px_y;
    ir_rotate_t rotate;
    bool mirror_vertical;
    bool mirror_horizontal;
    ir_temp_scale_t temp_scale;
    int brightness;
    int auto_sleep_min;
    ir_filter_mode_t filter_mode;
    float emissivity;
    float gamma;
    float temperature_offset_c;
    ir_chart_source_t chart_source;
    uint32_t reboot_cnt;
} ir_app_settings_t;

extern ir_app_settings_t g_ir_settings;

// 导出底图描述符与半透明 HUD 控件
extern lv_img_dsc_t s_thermal_img_dsc;
extern lv_obj_t *img_thermal;
extern lv_obj_t *label_title;
extern lv_obj_t *label_fps_float;
extern lv_obj_t *label_info;
extern lv_obj_t *tag_max;
extern lv_obj_t *tag_min;
extern lv_obj_t *tag_fix;
extern lv_obj_t *tag_emissivity;

esp_err_t ir_camera_app_init(void);
esp_err_t ir_camera_settings_load(void);
esp_err_t ir_camera_settings_save(void);
void ir_camera_settings_reset(void);
bool example_lvgl_lock(int timeout_ms);
void example_lvgl_unlock(void);
void ir_camera_task_main(void *pvParameters);
void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp);
bool thermal_ui_view_blocking(void);
void ir_camera_set_color_mode(ir_color_mode_t mode);
ir_color_mode_t ir_camera_get_color_mode(void);
const uint8_t *ir_camera_get_frame(void);
void ir_camera_get_norm_matrix(uint8_t *out);
void ir_camera_get_snapshot_range(float *min_t, float *max_t);
void ir_camera_render_norm(const uint8_t *norm, ir_color_mode_t mode, uint16_t *dst);
void ir_camera_update_transform(void);
float ir_camera_convert_temp(float temp_c, const char **unit_str);
void ir_camera_chart_clear(void);
void ir_camera_chart_get_data(float *out_buf, int *out_count);

#ifdef __cplusplus
}
#endif
