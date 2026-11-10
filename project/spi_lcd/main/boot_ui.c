/**
 * @file boot_ui.c
 * @brief 开机画面 + 首次开机语言选择（阻塞式）
 *
 * 横屏版：逻辑分辨率 280x240（LVGL 软件旋转，见 app_main.c）。
 * 开机图 C 数组为 280x240（白底 + 居中 logo，已放大 2 倍），直接全屏显示。
 * 主题：蓝白风格（白底 + 主题蓝 #1976D2 + 深蓝文字 #123A66）。
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lang.h"
#include "ir_mlx90640_app.h"

extern const lv_img_dsc_t power_on_start_img;

#define BOOT_W 280
#define BOOT_H 240

/* 蓝白主题色（与 lvgl_demo_ui.c 保持一致） */
#define T_PRIMARY      0x1976D2
#define T_PRIMARY_DEEP 0x0D47A1
#define T_TEXT_DARK    0x123A66
#define T_TEXT_MID     0x3A5B85
#define T_ITEM_BG      0xEAF3FF
#define T_ITEM_BORDER  0xBDD7F8

void boot_show_splash(lv_disp_t *disp)
{
    example_lvgl_lock(-1);

    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, &power_on_start_img);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_set_size(img, BOOT_W, BOOT_H);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);

    example_lvgl_unlock();

    /* 停留约 1.5 秒，让 Logo 显示出来 */
    vTaskDelay(pdMS_TO_TICKS(1500));

    example_lvgl_lock(-1);
    lv_obj_del(img);
    example_lvgl_unlock();
}

/* 语言选择回调 */
static volatile bool s_lang_picked = false;

static void lang_pick_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    app_lang_t l = (app_lang_t)(uintptr_t)lv_event_get_user_data(e);
    lang_set(l);
    s_lang_picked = true;
}

/* 创建一个语言条目按钮（蓝白主题） */
static lv_obj_t *create_lang_btn(lv_obj_t *parent, app_lang_t l)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 228, 40);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(T_ITEM_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(T_PRIMARY), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(T_ITEM_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, lang_pick_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)l);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, lv_color_hex(T_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, lang_font_for(l), LV_PART_MAIN);
    lv_label_set_text(lbl, lang_name(l));
    lv_obj_center(lbl);

    return btn;
}

void boot_show_language_select(lv_disp_t *disp)
{
    example_lvgl_lock(-1);

    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_clean(scr);

    /* 白底 */
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* 标题（英文，作为中性提示） */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(T_PRIMARY_DEEP), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(title, "Select Language");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* 竖排可滑动列表：5 种语言（屏幕不够就上下滑动） */
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 244, 176);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(list, lv_color_hex(0xF4F8FE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

    const app_lang_t order[LANG_COUNT] = { LANG_ZH, LANG_EN, LANG_RU, LANG_JA, LANG_KO };
    for (int i = 0; i < LANG_COUNT; i++) {
        create_lang_btn(list, order[i]);
    }

    s_lang_picked = false;
    example_lvgl_unlock();

    /* 等待用户在 LVGL 任务里点击选择 */
    while (!s_lang_picked) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    example_lvgl_lock(-1);
    lv_obj_clean(scr); /* 清空，为正式主界面腾出干净屏幕 */
    example_lvgl_unlock();
}
