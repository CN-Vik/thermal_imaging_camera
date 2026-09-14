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

    // 1. 全黑沉浸底色
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // 2. 图像居中放置 (y: 28 ~ 207，高 180)，与 240x280 完美无缝契合
    img_thermal = lv_img_create(scr);
    lv_img_set_src(img_thermal, &s_thermal_img_dsc);
    lv_img_set_zoom(img_thermal, 256);
    lv_img_set_antialias(img_thermal, false);
    lv_obj_set_pos(img_thermal, 0, 28);
    lv_obj_set_size(img_thermal, 240, 180);

    // 3. 顶部标题栏：长条椭圆灰透明胶囊 (Pill Capsule)
    lv_obj_t *top_capsule = lv_obj_create(scr);
    lv_obj_remove_style_all(top_capsule);
    lv_obj_set_size(top_capsule, 220, 22);
    lv_obj_align(top_capsule, LV_ALIGN_TOP_MID, 0, 3);
    lv_obj_set_style_bg_color(top_capsule, lv_color_make(45, 45, 45), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_capsule, LV_OPA_80, LV_PART_MAIN); // 80% 灰透明磨砂
    lv_obj_set_style_radius(top_capsule, LV_RADIUS_CIRCLE, LV_PART_MAIN);

    label_title = lv_label_create(top_capsule);
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(label_title, "THERMAL IMAGING");
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 0);

    // 4. 左上部独立悬浮 FPS：迷你灰透明椭圆胶囊
    label_fps_float = lv_label_create(scr);
    lv_obj_set_style_bg_color(label_fps_float, lv_color_make(30, 30, 30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label_fps_float, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_fps_float, lv_color_make(0, 230, 118), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_fps_float, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(label_fps_float, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(label_fps_float, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(label_fps_float, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_pos(label_fps_float, 6, 34);
    lv_label_set_text(label_fps_float, "-- FPS");

    // 5. 动态最高温标签：灰透明椭圆胶囊
    tag_max = lv_label_create(scr);
    lv_obj_set_style_bg_color(tag_max, lv_color_make(30, 30, 30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tag_max, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag_max, lv_color_make(255, 82, 82), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_max, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tag_max, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tag_max, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tag_max, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_label_set_text(tag_max, "");

    // 6. 动态最低温标签：灰透明椭圆胶囊
    tag_min = lv_label_create(scr);
    lv_obj_set_style_bg_color(tag_min, lv_color_make(30, 30, 30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tag_min, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_text_color(tag_min, lv_color_make(64, 196, 255), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_min, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tag_min, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tag_min, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(tag_min, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_label_set_text(tag_min, "");

    // 7. 底部仪表盘：长条椭圆灰透明 HUD 卡片
    lv_obj_t *bottom_capsule = lv_obj_create(scr);
    lv_obj_remove_style_all(bottom_capsule);
    lv_obj_set_size(bottom_capsule, 226, 62);
    lv_obj_align(bottom_capsule, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(bottom_capsule, lv_color_make(35, 35, 35), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bottom_capsule, LV_OPA_80, LV_PART_MAIN); // 80% 灰透明
    lv_obj_set_style_radius(bottom_capsule, 16, LV_PART_MAIN);        // 大圆角椭圆轮廓

    label_info = lv_label_create(bottom_capsule);
    lv_obj_set_style_text_color(label_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(label_info, "Calibrating Sensor...");
}