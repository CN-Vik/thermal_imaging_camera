/**
 * @file boot_ui.c
 * @brief 开机画面 + 首次开机语言选择（阻塞式）
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lang.h"
#include "ir_mlx90640_app.h"

extern const lv_img_dsc_t power_on_start_img;

#define BOOT_W 240
#define BOOT_H 280

void boot_show_splash(lv_disp_t *disp)
{
    example_lvgl_lock(-1);

    lv_obj_t *scr = lv_disp_get_scr_act(disp);
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

/* 创建一个语言条目按钮 */
static lv_obj_t *create_lang_btn(lv_obj_t *parent, app_lang_t l, int y)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 200, 34);
    lv_obj_set_pos(btn, (BOOT_W - 200) / 2, y);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B1E24), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x004D61), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn, LV_OPA_50, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, lang_pick_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)l);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
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

    /* 标题（英文，作为中性提示） */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(title, "Select Language");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    const app_lang_t order[LANG_COUNT] = { LANG_ZH, LANG_EN, LANG_RU, LANG_JA, LANG_KO };
    int y = 60;
    for (int i = 0; i < LANG_COUNT; i++) {
        create_lang_btn(scr, order[i], y);
        y += 42;
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
