/**
 * @file boot_ui.c
 * @brief 开机画面 + 首次开机语言选择（阻塞式）
 *
 * 横屏版：逻辑分辨率 280x240（LVGL 软件旋转，见 app_main.c）。
 * 开机 Logo 按比例缩放居中显示在白色屏幕背景上。
 * 主题：黑白灰风格。
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lang.h"
#include "ir_mlx90640_app.h"

extern const lv_img_dsc_t VIK;

/* Monochrome interface palette */
#define T_PRIMARY      0x383838
#define T_PRIMARY_DEEP 0x606060
#define T_TEXT_DARK    0xFFFFFF
#define T_TEXT_MID     0xB0B0B0
#define T_ITEM_BG      0x242424
#define T_ITEM_BORDER  0x303030

void boot_show_splash(lv_disp_t *disp)
{
    example_lvgl_lock(-1);

    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, &VIK);
    lv_obj_center(img);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);

    example_lvgl_unlock();

    /* 停留 2 秒后进入下一画面 */
    vTaskDelay(pdMS_TO_TICKS(2000));

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

/* 创建一个语言条目按钮 */
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
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
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

    /* 深色背景，匹配主设置界面 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x121212), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    /* 标题（英文，作为中性提示） */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(T_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(title, "Select Language");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* 竖排可滑动列表：5 种语言（屏幕不够就上下滑动） */
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 244, 176);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x121212), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, lv_color_hex(T_ITEM_BORDER), LV_PART_MAIN);
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
