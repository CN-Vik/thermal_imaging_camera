#include "lvgl.h"
#include "ir_mlx90640_app.h"

lv_obj_t *img_thermal = NULL;
lv_obj_t *label_info = NULL;
lv_obj_t *label_status_left = NULL;
lv_obj_t *label_status_right = NULL;
lv_obj_t *tag_max = NULL;
lv_obj_t *tag_min = NULL;

void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // 1. 设置全黑底色
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // 2. 顶部状态栏容器 (高 24px)
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, 240, 24);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_make(25, 25, 25), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, LV_PART_MAIN);

    // 状态栏左侧：设备与发射率
    label_status_left = lv_label_create(top_bar);
    lv_obj_set_style_text_color(label_status_left, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_status_left, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text(label_status_left, "MLX90640  e:0.95");
    lv_obj_align(label_status_left, LV_ALIGN_LEFT_MID, 6, 0);

    // 状态栏右侧：FPS / 状态
    label_status_right = lv_label_create(top_bar);
    lv_obj_set_style_text_color(label_status_right, lv_color_make(0, 230, 118), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_status_right, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text(label_status_right, "FPS: --");
    lv_obj_align(label_status_right, LV_ALIGN_RIGHT_MID, -6, 0);

    // 3. 热成像图像层 (高 180px，紧随状态栏下方 y = 24)
    img_thermal = lv_img_create(scr);
    lv_img_set_src(img_thermal, &s_thermal_img_dsc);
    lv_img_set_zoom(img_thermal, 256);
    lv_img_set_antialias(img_thermal, false);
    lv_obj_set_pos(img_thermal, 0, 24);

    // 4. 动态最高温指示标签 (红色文本)
    tag_max = lv_label_create(scr);
    lv_obj_set_style_text_color(tag_max, lv_color_make(255, 68, 68), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_max, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text(tag_max, "");

    // 5. 动态最低温指示标签 (青蓝色文本)
    tag_min = lv_label_create(scr);
    lv_obj_set_style_text_color(tag_min, lv_color_make(64, 196, 255), LV_PART_MAIN);
    lv_obj_set_style_text_font(tag_min, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text(tag_min, "");

    // 6. 底部数据信息面板 (高 76px，y = 204 开始)
    label_info = lv_label_create(scr);
    lv_obj_set_style_text_color(label_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_info, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_label_set_text(label_info, "Initializing Sensor...");
}