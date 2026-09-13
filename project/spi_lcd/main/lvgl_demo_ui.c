#include "lvgl.h"
#include "ir_mlx90640_app.h"

lv_obj_t *img_thermal = NULL;
lv_obj_t *label_info = NULL;

void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // 1. 设置纯黑背景底色
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // 2. 创建热像图控件，直接绑定算法生成的 240x180 图像描述符
    img_thermal = lv_img_create(scr);
    lv_img_set_src(img_thermal, &s_thermal_img_dsc);

    // 核心改动：由于算法已在后台完成双线性插值放大，此处不再由 LVGL 消耗算力缩放（zoom=256 即 1.0倍原图）
    lv_img_set_zoom(img_thermal, 256);
    lv_img_set_antialias(img_thermal, false);

    // 在 240x280 屏幕上，将 240x180 画面居中略微偏上，留出底部给信息栏
    lv_obj_align(img_thermal, LV_ALIGN_TOP_MID, 0, 10);

    // 3. 底部信息数据显示标签
    label_info = lv_label_create(scr);
    lv_obj_set_style_text_color(label_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_info, LV_ALIGN_BOTTOM_MID, 0, -10);
}