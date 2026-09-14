#include "lvgl.h"
#include "ir_mlx90640_app.h"

lv_obj_t *img_thermal = NULL;
lv_obj_t *label_title = NULL;
lv_obj_t *label_fps_float = NULL;
lv_obj_t *label_info = NULL;
lv_obj_t *tag_max = NULL;
lv_obj_t *tag_min = NULL;

void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // 1. 全屏底图：热成像图像作为最底层基底铺满整个屏幕 (240x280)
    img_thermal = lv_img_create(scr);
    lv_img_set_src(img_thermal, &s_thermal_img_dsc);
    lv_img_set_zoom(img_thermal, 256);
    lv_img_set_antialias(img_thermal, false);
    lv_obj_set_pos(img_thermal, 0, 0);
    lv_obj_set_size(img_thermal, 240, 280);

    // 2. 顶部居中半透明 HUD 标题栏 (胶囊磨砂透明，透出背景热像图)
    lv_obj_t *top_capsule = lv_obj_create(scr);
    lv_obj_remove_style_all(top_capsule);
    lv_obj_set_size(top_capsule, 180, 24);
    lv_obj_align(top_capsule, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(top_capsule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_capsule, LV_OPA_50, LV_PART_MAIN); // 50% 半透明
    lv_obj_set_style_border_width(top_capsule, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(top_capsule, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_style_border_opa(top_capsule, LV_OPA_30, LV_PART_MAIN); // 微弱玻璃边缘
    lv_obj_set_style_radius(top_capsule, LV_RADIUS_CIRCLE, LV_PART_MAIN);

    label_title = lv_label_create(top_capsule);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(label_title, "THERMAL CAMERA");
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 0);

    // 3. 左上角悬浮帧率小药丸 (半透明绿字 HUD)
    label_fps_float = lv_label_create(scr);
    lv_obj_set_style_bg_color(label_fps_float, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label_fps_float, LV_OPA_50, LV_PART_MAIN); // 50% 半透明
    lv_obj_set_style_border_width(label_fps_float, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(label_fps_float, lv_color_make(0, 230, 118), LV_PART_MAIN);
    lv_obj_set_style_border_opa(label_fps_float, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_fps_float, lv_color_make(0, 230, 118), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_fps_float, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(label_fps_float, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(label_fps_float, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(label_fps_float, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_pos(label_fps_float, 8, 36);
    lv_label_set_text(label_fps_float, "-- FPS");

    // 4. 右上角发射率悬浮小药丸
    lv_obj_t *tag_emissivity = lv_label_create(scr);
    lv_obj_set_style_bg_color(tag_emissivity, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tag_emissivity, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(tag_emissivity, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tag_emissivity, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_style_border_opa(tag_emissivity, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag_emissivity, lv_color_make(220, 220, 220), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_emissivity, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tag_emissivity, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tag_emissivity, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tag_emissivity, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_align(tag_emissivity, LV_ALIGN_TOP_RIGHT, -8, 36);
    lv_label_set_text(tag_emissivity, "e:0.95");

    // 5. 动态最高温指示气泡 (半透明红色微光胶囊)
    tag_max = lv_label_create(scr);
    lv_obj_set_style_bg_color(tag_max, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tag_max, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(tag_max, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tag_max, lv_color_make(255, 80, 80), LV_PART_MAIN);
    lv_obj_set_style_border_opa(tag_max, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag_max, lv_color_make(255, 80, 80), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_max, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tag_max, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tag_max, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tag_max, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_label_set_text(tag_max, "");

    // 6. 动态最低温指示气泡 (半透明青蓝微光胶囊)
    tag_min = lv_label_create(scr);
    lv_obj_set_style_bg_color(tag_min, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tag_min, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(tag_min, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tag_min, lv_color_make(60, 200, 255), LV_PART_MAIN);
    lv_obj_set_style_border_opa(tag_min, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag_min, lv_color_make(60, 200, 255), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_min, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tag_min, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tag_min, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tag_min, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_label_set_text(tag_min, "");

    // 7. 底部仪表盘：长条大椭圆半透明悬浮玻璃卡片
    lv_obj_t *bottom_capsule = lv_obj_create(scr);
    lv_obj_remove_style_all(bottom_capsule);
    lv_obj_set_size(bottom_capsule, 226, 56);
    lv_obj_align(bottom_capsule, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(bottom_capsule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bottom_capsule, LV_OPA_50, LV_PART_MAIN); // 55% 磨砂灰透明
    lv_obj_set_style_border_width(bottom_capsule, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bottom_capsule, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_style_border_opa(bottom_capsule, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_radius(bottom_capsule, 16, LV_PART_MAIN);

    label_info = lv_label_create(bottom_capsule);
    lv_obj_set_style_text_color(label_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label_info, "Calibrating Sensor...");
}