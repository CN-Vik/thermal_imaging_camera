#include "lvgl.h"
#include "ir_mlx90640_app.h"

// 去掉 static，让采集任务可以访问并刷新
lv_obj_t *img_thermal = NULL;
lv_obj_t *label_info = NULL;

// 图像描述符：绑定原始 32x24 数组
static lv_img_dsc_t raw_img_dsc = {
    .header.always_zero = 0,
    .header.w = MLX_W,
    .header.h = MLX_H,
    .data_size = MLX_W * MLX_H * sizeof(uint16_t),
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = (const uint8_t *)s_raw_rgb565,
};

void lvgl_dispaly_ir_mlx90640(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // 1. 设置黑色纯净背景
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // 2. 创建热像图控件
    img_thermal = lv_img_create(scr);
    lv_img_set_src(img_thermal, &raw_img_dsc);

    // 屏幕是 240x280，热图像素 32x24 放大 6 倍即 192x144，比例正好
    // LVGL v8 zoom: 256 为 1.0 倍，256 * 6 = 1536
    lv_img_set_zoom(img_thermal, 1536);
    lv_obj_align(img_thermal, LV_ALIGN_CENTER, 0, -25);

    // 3. 创建温度数据显示标签
    label_info = lv_label_create(scr);
    lv_obj_set_style_text_color(label_info, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_info, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_label_set_text(label_info, "MLX90640 Initializing...");
}