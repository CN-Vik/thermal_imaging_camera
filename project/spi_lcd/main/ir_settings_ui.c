#include "ir_settings_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dev_sys.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "ir_mlx90640_app.h"
#include "lang.h"
#include "lvgl_demo_ui.h"
#include "nvs.h"

static const char *TAG = "ir_settings";

#define SETTINGS_W              280
#define SETTINGS_H              240
#define SETTINGS_HEADER_H        40
#define SETTINGS_BODY_H          (SETTINGS_H - SETTINGS_HEADER_H)

#define UI_DANGER                0xE53935
#define UI_MENU_BG               0x121212
#define UI_MENU_HEADER           0x181818
#define UI_MENU_CARD             0x242424
#define UI_MENU_PRESSED          0x383838
#define UI_MENU_BORDER           0x303030
#define UI_MENU_TEXT             0xFFFFFF
#define UI_MENU_MUTED            0xB3B3B3
#define UI_MENU_ACCENT           0xFFFFFF
#define UI_BLUE                  0x383838
#define UI_BLUE_DARK             0x505050
#define UI_BLUE_LIGHT            0x242424
#define UI_TEXT_DARK             UI_MENU_TEXT
#define UI_TEXT_MID              UI_MENU_MUTED
#define UI_LINE_GRAY             UI_MENU_BORDER
#define UI_WHITE                 0xFFFFFF

#define MENU_ROW_H               60
#define MENU_LIST_W              256
#define MENU_CARD_H              54

#define BACKLIGHT_GPIO           GPIO_NUM_18
#define BACKLIGHT_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_TIMER     LEDC_TIMER_0
#define BACKLIGHT_LEDC_BITS      LEDC_TIMER_10_BIT

typedef enum {
    PAGE_MAIN = 0,
    PAGE_BASIC,
    PAGE_ADVANCED,
    PAGE_OTHER,
    PAGE_COLOR,
    PAGE_LABELS,
    PAGE_CHART,
    PAGE_ROTATE,
    PAGE_MIRROR,
    PAGE_SCALE,
    PAGE_SCREEN,
    PAGE_FILTER,
    PAGE_EMISSIVITY,
    PAGE_GAMMA,
    PAGE_CALIBRATION,
    PAGE_LANGUAGE,
    PAGE_FACTORY_RESET,
    PAGE_ABOUT,
} settings_page_t;

static const char * const s_filter_names[IR_FILTER_MAX] = {
    "B-Spline", "Gaussian", "Median", "Bilateral", "Off"
};

static const lang_str_id_t s_scale_name_ids[IR_SCALE_MAX] = {
    STR_CELSIUS, STR_FAHRENHEIT, STR_KELVIN, STR_RANKINE
};

static lv_obj_t *s_root;
static lv_obj_t *s_header;
static lv_obj_t *s_header_title;
static lv_obj_t *s_header_back;
static lv_obj_t *s_body;
#define CHART_SRC_BTN_CNT 4

static lv_obj_t *s_chart;
static lv_chart_series_t *s_chart_series;
#define CHART_AXIS_LBL_CNT 3
static lv_obj_t *s_chart_ylbl[CHART_AXIS_LBL_CNT];
static lv_obj_t *s_chart_xlbl[CHART_AXIS_LBL_CNT];
static lv_obj_t *s_chart_save_btn;
static lv_timer_t *s_chart_save_hint;
static lv_obj_t *s_chart_src_btn[CHART_SRC_BTN_CNT];
static lv_obj_t *s_chart_src_lbl[CHART_SRC_BTN_CNT];

static const ir_chart_source_t s_chart_src_order[CHART_SRC_BTN_CNT] = {
    IR_CHART_SRC_CENTER, IR_CHART_SRC_FIXTURE, IR_CHART_SRC_MAX, IR_CHART_SRC_MIN
};
static lv_obj_t *s_scale_box;
static lv_obj_t *s_sleep_value;
static lv_timer_t *s_ui_timer;
static lv_timer_t *s_activity_timer;
static lv_indev_t *s_pointer_indev;
static uint32_t s_last_activity_ms;
static settings_page_t s_current_page = PAGE_MAIN;
static bool s_is_open;
static bool s_backlight_ready;
static bool s_screen_sleeping;
static uint32_t s_open_ms = 0;
static settings_page_t s_parent_page = PAGE_MAIN;

static void render_main_page(void);
static void render_page(settings_page_t page);
static bool is_category_page(settings_page_t page);

static void render_page_async_cb(void *user_data)
{
    render_page((settings_page_t)(uintptr_t)user_data);
}

static void schedule_page(settings_page_t page)
{
    lv_async_call(render_page_async_cb, (void *)(uintptr_t)page);
}

static inline int clamp_int(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static inline float clamp_float(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void style_reset(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_blue_button(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_BLUE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_BLUE_DARK), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 2, LV_PART_MAIN);
}

static void style_dark_switch(lv_obj_t *sw)
{
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_MENU_PRESSED),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_MENU_TEXT), LV_PART_KNOB);
}

static lv_obj_t *create_blue_button(lv_obj_t *parent, const char *text,
                                    lv_coord_t width, lv_coord_t height,
                                    lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, height);
    style_blue_button(btn);
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static void create_page_title(const char *title)
{
    lv_obj_t *label = lv_label_create(s_body);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, lang_font(), LV_PART_MAIN);
    lv_label_set_text(label, title);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 7);
}

/* 文本按钮：与 create_blue_button 相同，但标签使用当前语言字体 */
static lv_obj_t *create_blue_text_button(lv_obj_t *parent, const char *text,
                                         lv_coord_t width, lv_coord_t height,
                                         lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = create_blue_button(parent, text, width, height, cb, user_data);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_obj_set_style_text_font(label, lang_font(), LV_PART_MAIN);
    }
    return btn;
}

static void save_settings(void)
{
    esp_err_t err = ir_camera_settings_save();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to save settings: %s", esp_err_to_name(err));
    }
}

static void apply_backlight(void)
{
    if (!s_backlight_ready) return;
    if (s_screen_sleeping) {
        ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, 0);
        ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
        return;
    }
    int brightness = clamp_int(g_ir_settings.brightness, 5, 100);
    uint32_t max_duty = (1U << 10) - 1U;
    uint32_t duty = (max_duty * (uint32_t)brightness + 50U) / 100U;
    ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
}

static void screen_wake(void)
{
    s_last_activity_ms = lv_tick_get();
    if (s_screen_sleeping) {
        s_screen_sleeping = false;
        apply_backlight();
    }
}

static esp_err_t init_backlight_pwm(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_LEDC_BITS,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;

    ledc_channel_config_t channel = {
        .gpio_num = BACKLIGHT_GPIO,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    err = ledc_channel_config(&channel);
    if (err == ESP_OK) {
        s_backlight_ready = true;
        apply_backlight();
    }
    return err;
}

/* 菜单刚打开后的短时间内忽略点击，避免长按松手时误触第一项进入二级菜单 */
static bool menu_click_guard(void)
{
    return (lv_tick_elaps(s_open_ms) < 500);
}

static void header_back_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (menu_click_guard()) return;
    if (s_current_page == PAGE_MAIN) {
        ir_settings_ui_close();
    } else if (is_category_page(s_current_page)) {
        render_page(PAGE_MAIN);
    } else {
        render_page(s_parent_page);
    }
}

static void menu_item_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (menu_click_guard()) return;
    s_parent_page = s_current_page;
    settings_page_t page = (settings_page_t)(uintptr_t)lv_event_get_user_data(event);
    schedule_page(page);
}

static void settings_power_off_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (menu_click_guard()) return;
    sys_power_off();
}

static void gallery_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (menu_click_guard()) return;
    ir_settings_ui_close();
    thermal_ui_show_gallery();
}

static bool is_category_page(settings_page_t page)
{
    return page == PAGE_BASIC || page == PAGE_ADVANCED || page == PAGE_OTHER;
}

static const char *category_title(settings_page_t page)
{
    switch (page) {
    case PAGE_BASIC: return lang_str(STR_BASIC_SETTINGS);
    case PAGE_ADVANCED: return lang_str(STR_ADVANCED_SETTINGS);
    case PAGE_OTHER: return lang_str(STR_OTHER_SETTINGS);
    default: return "";
    }
}

static void update_header(settings_page_t page)
{
    bool main = page == PAGE_MAIN;
    bool category = is_category_page(page);

    lv_obj_set_style_bg_color(s_header, lv_color_hex(UI_MENU_HEADER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_header, lv_color_hex(UI_MENU_PRESSED), LV_STATE_PRESSED);
    if (main) lv_obj_add_flag(s_header_back, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(s_header_back, LV_OBJ_FLAG_HIDDEN);

    if (main || category) {
        lv_label_set_text(s_header_title,
                          main ? lang_str(STR_MAIN_MENU) : category_title(page));
        lv_obj_set_style_text_font(s_header_title, lang_menu_font(), LV_PART_MAIN);
        lv_obj_align(s_header_title, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(s_header_title, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_header_title, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_style_bg_color(s_body, lv_color_hex(UI_MENU_BG), LV_PART_MAIN);
}

static lv_obj_t *create_category_list(void)
{
    lv_obj_t *list = lv_obj_create(s_body);
    style_reset(list);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(list, MENU_LIST_W, SETTINGS_BODY_H);
    lv_obj_set_pos(list, (SETTINGS_W - MENU_LIST_W) / 2, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(UI_MENU_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_bg_color(list, lv_color_hex(UI_MENU_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    return list;
}

static void add_menu_row(lv_obj_t *list, const char *text, settings_page_t page)
{
    lv_obj_t *row = lv_btn_create(list);
    lv_obj_set_size(row, MENU_LIST_W - 8, MENU_ROW_H);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_MENU_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(row, menu_item_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)page);

    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_width(label, MENU_LIST_W - 48);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_MENU_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_MENU_TEXT), LV_STATE_PRESSED);
    lv_obj_set_style_text_font(label, lang_menu_font(), LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *arrow = lv_label_create(row);
    lv_obj_set_style_text_color(arrow, lv_color_hex(UI_MENU_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, lv_color_hex(UI_MENU_TEXT), LV_STATE_PRESSED);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
}

static void add_custom_menu_row(lv_obj_t *list, const char *text,
                                lv_event_cb_t cb, void *user_data, bool danger)
{
    lv_obj_t *row = lv_btn_create(list);
    lv_obj_set_size(row, MENU_LIST_W - 8, MENU_ROW_H);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_MENU_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_width(label, MENU_LIST_W - 48);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(danger ? UI_DANGER : UI_MENU_TEXT),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_MENU_TEXT), LV_STATE_PRESSED);
    lv_obj_set_style_text_font(label, lang_menu_font(), LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *arrow = lv_label_create(row);
    lv_obj_set_style_text_color(arrow,
                                lv_color_hex(danger ? UI_DANGER : UI_MENU_ACCENT),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(arrow, lv_color_hex(UI_MENU_TEXT), LV_STATE_PRESSED);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
}

static void create_menu_card(const char *text, const char *symbol, int x, int y,
                             lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *card = lv_btn_create(s_body);
    lv_obj_set_size(card, MENU_LIST_W, MENU_CARD_H);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_MENU_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(card, callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t *icon = lv_label_create(card);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_MENU_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(icon, symbol);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 18, 0);

    lv_obj_t *label = lv_label_create(card);
    lv_obj_set_width(label, MENU_LIST_W - 62);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_MENU_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, lang_menu_font(), LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 52, 0);
}

static void render_main_page(void)
{
    s_current_page = PAGE_MAIN;
    s_chart = NULL;
    s_chart_series = NULL;
    s_chart_save_btn = NULL;
    s_sleep_value = NULL;
    update_header(PAGE_MAIN);
    lv_obj_clean(s_body);
    lv_obj_add_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_body, LV_SCROLLBAR_MODE_ON);

    create_menu_card(lang_str(STR_BASIC_SETTINGS), LV_SYMBOL_SETTINGS,
                     12, 10, menu_item_event_cb,
                     (void *)(uintptr_t)PAGE_BASIC);
    create_menu_card(lang_str(STR_ADVANCED_SETTINGS), LV_SYMBOL_LIST,
                     12, 73, menu_item_event_cb,
                     (void *)(uintptr_t)PAGE_ADVANCED);
    create_menu_card(lang_str(STR_GALLERY), LV_SYMBOL_IMAGE,
                     12, 136, gallery_event_cb, NULL);
    create_menu_card(lang_str(STR_OTHER), LV_SYMBOL_POWER,
                     12, 199, menu_item_event_cb,
                     (void *)(uintptr_t)PAGE_OTHER);
}

static void render_category_page(settings_page_t page)
{
    s_current_page = page;
    update_header(page);
    lv_obj_clean(s_body);
    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_body, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(s_body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *list = create_category_list();

    if (page == PAGE_BASIC) {
        add_menu_row(list, lang_str(STR_LANGUAGE), PAGE_LANGUAGE);
        add_menu_row(list, lang_str(STR_COLOR_STYLE), PAGE_COLOR);
        add_menu_row(list, lang_str(STR_TEMP_LABEL), PAGE_LABELS);
        add_menu_row(list, lang_str(STR_CHART), PAGE_CHART);
        add_menu_row(list, lang_str(STR_ROTATE), PAGE_ROTATE);
        add_menu_row(list, lang_str(STR_MIRROR), PAGE_MIRROR);
        add_menu_row(list, lang_str(STR_TEMP_SCALE), PAGE_SCALE);
        add_menu_row(list, lang_str(STR_SCREEN), PAGE_SCREEN);
    } else if (page == PAGE_ADVANCED) {
        add_menu_row(list, lang_str(STR_FILTER), PAGE_FILTER);
        add_menu_row(list, lang_str(STR_EMISSIVITY), PAGE_EMISSIVITY);
        add_menu_row(list, lang_str(STR_GAMMA), PAGE_GAMMA);
        add_menu_row(list, lang_str(STR_CALIBRATION), PAGE_CALIBRATION);
    } else if (page == PAGE_OTHER) {
        add_menu_row(list, lang_str(STR_ABOUT), PAGE_ABOUT);
        add_custom_menu_row(list, lang_str(STR_FACTORY_RESET), menu_item_event_cb,
                            (void *)(uintptr_t)PAGE_FACTORY_RESET, true);
        add_custom_menu_row(list, lang_str(STR_POWER_OFF), settings_power_off_event_cb,
                            NULL, true);
    }

    lv_obj_t *scroll_hint = lv_label_create(s_body);
    lv_obj_set_style_text_color(scroll_hint, lv_color_hex(UI_MENU_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(scroll_hint, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(scroll_hint, LV_SYMBOL_DOWN);
    lv_obj_align(scroll_hint, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void color_apply(void)
{
    ir_camera_set_color_mode(g_ir_settings.color_mode);
    save_settings();
}

static void color_cycle_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    int direction = (int)(intptr_t)lv_event_get_user_data(event);
    int mode = (int)g_ir_settings.color_mode;
    mode = (mode + direction + IR_COLOR_MODE_MAX) % IR_COLOR_MODE_MAX;
    g_ir_settings.color_mode = (ir_color_mode_t)mode;
    color_apply();
    schedule_page(PAGE_COLOR);
}

static void color_reverse_event_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    g_ir_settings.color_reverse = lv_obj_has_state(sw, LV_STATE_CHECKED);
    color_apply();
}

static void render_color_page(void)
{
    create_page_title(lang_str(STR_COLOR_STYLE));

    lv_obj_t *mode_label = lv_label_create(s_body);
    lv_obj_set_style_text_color(mode_label, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
    lv_obj_set_style_text_font(mode_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(mode_label, ir_color_get_mode_name(g_ir_settings.color_mode));
    lv_obj_align(mode_label, LV_ALIGN_TOP_MID, 0, 33);

    lv_obj_t *reverse = lv_label_create(s_body);
    lv_obj_set_style_text_color(reverse, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(reverse, lang_font(), LV_PART_MAIN);
    lv_label_set_text(reverse, lang_str(STR_REVERSE));
    lv_obj_align(reverse, LV_ALIGN_TOP_MID, 0, 56);

    lv_obj_t *sw = lv_switch_create(s_body);
    lv_obj_set_size(sw, 62, 30);
    lv_obj_align(sw, LV_ALIGN_TOP_MID, 0, 78);
    style_dark_switch(sw);
    if (g_ir_settings.color_reverse) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, color_reverse_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *gradient = lv_obj_create(s_body);
    style_reset(gradient);
    lv_obj_set_size(gradient, 212, 27);
    lv_obj_align(gradient, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_style_radius(gradient, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(gradient, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(gradient, lv_color_hex(UI_LINE_GRAY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(gradient, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(gradient, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(gradient, lv_color_hex(0xD0D0D0), LV_PART_MAIN);

    lv_obj_t *prev = create_blue_button(s_body, LV_SYMBOL_LEFT, 92, 38,
                                        color_cycle_event_cb,
                                        (void *)(intptr_t)-1);
    lv_obj_align(prev, LV_ALIGN_BOTTOM_MID, -52, -15);
    lv_obj_t *next = create_blue_button(s_body, LV_SYMBOL_RIGHT, 92, 38,
                                        color_cycle_event_cb,
                                        (void *)(intptr_t)1);
    lv_obj_align(next, LV_ALIGN_BOTTOM_MID, 52, -15);
}

static void label_switch_event_cb(lv_event_t *event)
{
    int which = (int)(intptr_t)lv_event_get_user_data(event);
    bool checked = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (which == 0) g_ir_settings.label_coldest = checked;
    if (which == 1) g_ir_settings.label_hottest = checked;
    if (which == 2) g_ir_settings.label_fixture = checked;
    save_settings();
}

static void add_label_switch(lv_obj_t *parent, const char *text, int which,
                             bool checked, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, lang_font(), LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);

    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 62, 30);
    lv_obj_align(sw, LV_ALIGN_TOP_MID, 0, y + 20);
    style_dark_switch(sw);
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, label_switch_event_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)which);
}

static void render_labels_page(void)
{
    create_page_title(lang_str(STR_TEMP_LABEL));
    add_label_switch(s_body, lang_str(STR_COLDEST), 0, g_ir_settings.label_coldest, 36);
    add_label_switch(s_body, lang_str(STR_HOTTEST), 1, g_ir_settings.label_hottest, 90);
    add_label_switch(s_body, lang_str(STR_FIXTURE), 2, g_ir_settings.label_fixture, 144);
}

static const char *chart_source_name(ir_chart_source_t s)
{
    switch (s) {
    case IR_CHART_SRC_FIXTURE: return lang_str(STR_FIX);
    case IR_CHART_SRC_MAX:     return lang_str(STR_MAX);
    case IR_CHART_SRC_MIN:     return lang_str(STR_MIN);
    default:                   return lang_str(STR_CENTER);
    }
}

static int chart_axis_round_to_integer(lv_coord_t value_tenths)
{
    int value = (int)value_tenths;
    return value >= 0 ? (value + 5) / 10 : (value - 5) / 10;
}

/* Y 轴三档刻度内部按 0.1 单位存，显示时四舍五入到整数且不带单位。 */
static void chart_axis_update(lv_coord_t lo, lv_coord_t hi)
{
    if (!s_chart_ylbl[0]) return;

    char buf[CHART_AXIS_LBL_CNT][16];
    snprintf(buf[0], sizeof(buf[0]), "%d", chart_axis_round_to_integer(hi));
    snprintf(buf[1], sizeof(buf[1]), "%d",
             chart_axis_round_to_integer((lv_coord_t)((hi + lo) / 2)));
    snprintf(buf[2], sizeof(buf[2]), "%d", chart_axis_round_to_integer(lo));

    for (int i = 0; i < CHART_AXIS_LBL_CNT; i++) {
        if (s_chart_ylbl[i]) lv_label_set_text(s_chart_ylbl[i], buf[i]);
    }
}

static void chart_refresh(void)
{
    if (s_current_page != PAGE_CHART || !s_chart || !s_chart_series) return;

    float samples[IR_CHART_HISTORY_LEN];
    int count = 0;
    ir_camera_chart_get_data(samples, &count);
    if (count < 0) count = 0;
    if (count > IR_CHART_HISTORY_LEN) count = IR_CHART_HISTORY_LEN;

    if (count == 0) {
        lv_chart_set_all_value(s_chart, s_chart_series, LV_CHART_POINT_NONE);
        lv_chart_refresh(s_chart);
        return;
    }

    /* 先换算并限幅成 lv_coord_t，避免 NaN/Inf 转整型得到垃圾值后破坏量程 */
    lv_coord_t pts[IR_CHART_HISTORY_LEN];
    lv_coord_t lo = 0;
    lv_coord_t hi = 0;
    for (int i = 0; i < count; i++) {
        float value = ir_camera_convert_temp(samples[i], NULL);
        if (!isfinite(value)) value = 0.0f;
        int scaled = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
        if (scaled < -30000) scaled = -30000;
        if (scaled > 30000) scaled = 30000;
        pts[i] = (lv_coord_t)scaled;
        if (i == 0 || pts[i] < lo) lo = pts[i];
        if (i == 0 || pts[i] > hi) hi = pts[i];
    }
    /* 量程至少 2.0 个单位、上下各留 0.5，保证曲线不贴边且 ymax > ymin */
    if (hi - lo < 20) hi = lo + 20;
    lo -= 5;
    hi += 5;

    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
    chart_axis_update(lo, hi);
    lv_chart_set_all_value(s_chart, s_chart_series, LV_CHART_POINT_NONE);
    int first = IR_CHART_HISTORY_LEN - count;
    for (int i = 0; i < count; i++) {
        s_chart_series->y_points[first + i] = pts[i];
    }
    lv_chart_refresh(s_chart);
}

static void chart_clear_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    ir_camera_chart_clear();
    chart_refresh();
}

/* 保存后的确认反馈：临时把按钮图标换成 ✓ / ✕，1.2s 后恢复。
   原来的状态文字行已删掉（给曲线让出高度），反馈改由按钮自身承担。 */
static void chart_save_hint_reset_cb(lv_timer_t *timer)
{
    (void)timer;
    s_chart_save_hint = NULL;
    if (!s_chart_save_btn) return;
    lv_obj_t *lbl = lv_obj_get_child(s_chart_save_btn, 0);
    if (lbl) lv_label_set_text(lbl, LV_SYMBOL_DOWNLOAD);
}

static void chart_save_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    float samples[IR_CHART_HISTORY_LEN];
    int count = 0;
    ir_camera_chart_get_data(samples, &count);

    nvs_handle_t handle;
    esp_err_t err = nvs_open("ir_chart", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "count", count);
        if (err == ESP_OK && count > 0) {
            err = nvs_set_blob(handle, "samples", samples,
                               (size_t)count * sizeof(float));
        }
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }

    if (s_chart_save_btn) {
        lv_obj_t *lbl = lv_obj_get_child(s_chart_save_btn, 0);
        if (lbl) {
            lv_label_set_text(lbl, err == ESP_OK ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        }
        if (s_chart_save_hint) {
            lv_timer_del(s_chart_save_hint);
            s_chart_save_hint = NULL;
        }
        s_chart_save_hint = lv_timer_create(chart_save_hint_reset_cb, 1200, NULL);
        if (s_chart_save_hint) lv_timer_set_repeat_count(s_chart_save_hint, 1);
    }
}

static void chart_source_restyle(void)
{
    for (int i = 0; i < CHART_SRC_BTN_CNT; i++) {
        if (!s_chart_src_btn[i]) continue;
        bool selected = (g_ir_settings.chart_source == s_chart_src_order[i]);
        lv_obj_set_style_bg_color(s_chart_src_btn[i],
                                  lv_color_hex(selected ? UI_BLUE : UI_MENU_CARD),
                                  LV_PART_MAIN);
        if (s_chart_src_lbl[i]) {
            lv_obj_set_style_text_color(s_chart_src_lbl[i],
                                        lv_color_hex(selected ? UI_WHITE : UI_TEXT_DARK),
                                        LV_PART_MAIN);
        }
    }
}

/* 只在原地切换高亮，不重建页面：避免重建过程中的内存分配与对象删除风险 */
static void chart_source_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    ir_chart_source_t src = (ir_chart_source_t)(uintptr_t)lv_event_get_user_data(event);
    if (src >= IR_CHART_SRC_COUNT) return;
    if (g_ir_settings.chart_source == src) return;

    g_ir_settings.chart_source = src;
    /* 换了采样源，旧采样值不再可比，直接清空历史 */
    ir_camera_chart_clear();
    chart_source_restyle();
    save_settings();
    chart_refresh();
}

/* 曲线页几何：整页只留曲线 + 轴刻度 + 采样源按钮，绘图区尽量吃满可用高度 */
#define CHART_AXIS_LEFT_W   40
#define CHART_PLOT_W       232
#define CHART_PLOT_H       100
#define CHART_TOP           12

static void render_chart_page(void)
{
    /* 不再画页面标题，把顶栏下方的空间全部让给曲线 */
    for (int i = 0; i < CHART_SRC_BTN_CNT; i++) {
        s_chart_src_btn[i] = NULL;
        s_chart_src_lbl[i] = NULL;
    }
    for (int i = 0; i < CHART_AXIS_LBL_CNT; i++) {
        s_chart_ylbl[i] = NULL;
        s_chart_xlbl[i] = NULL;
    }
    s_chart_save_btn = NULL;

    s_chart = lv_chart_create(s_body);
    if (!s_chart) return;
    lv_obj_set_size(s_chart, CHART_PLOT_W, CHART_PLOT_H);
    lv_obj_set_pos(s_chart, CHART_AXIS_LEFT_W, CHART_TOP);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_chart, lv_color_hex(UI_MENU_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(UI_MENU_BORDER), LV_PART_MAIN);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, IR_CHART_HISTORY_LEN);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    /* 3 条横向分隔线 = 25% / 50% / 75%，中间那条正好落在 Y 轴中点刻度上 */
    lv_chart_set_div_line_count(s_chart, 3, 6);
    lv_obj_set_style_pad_all(s_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, LV_PART_INDICATOR);
    s_chart_series = lv_chart_add_series(s_chart, lv_color_hex(UI_MENU_ACCENT),
                                         LV_CHART_AXIS_PRIMARY_Y);
    if (!s_chart_series) {
        lv_obj_del(s_chart);
        s_chart = NULL;
        return;
    }
    lv_chart_set_all_value(s_chart, s_chart_series, LV_CHART_POINT_NONE);

    /* Y 轴刻度：与绘图区上 / 中 / 下三档分隔线对齐，显示整数且不带单位 */
    static const lv_coord_t y_axis_y[CHART_AXIS_LBL_CNT] = {
        CHART_TOP - 8,
        CHART_TOP + CHART_PLOT_H / 2 - 8,
        CHART_TOP + CHART_PLOT_H - 8
    };
    for (int i = 0; i < CHART_AXIS_LBL_CNT; i++) {
        lv_obj_t *lbl = lv_label_create(s_body);
        lv_obj_set_style_text_color(lbl, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_width(lbl, CHART_AXIS_LEFT_W - 4);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_label_set_text(lbl, "--");
        lv_obj_set_pos(lbl, 0, y_axis_y[i]);
        s_chart_ylbl[i] = lbl;
    }

    /* X 轴时间刻度：按采样周期换算整条曲线覆盖的秒数（60 点 × 500ms = 30s） */
    int span_s = (IR_CHART_HISTORY_LEN * IR_CHART_SAMPLE_MS) / 1000;
    char xbuf[CHART_AXIS_LBL_CNT][10];
    snprintf(xbuf[0], sizeof(xbuf[0]), "-%ds", span_s);
    snprintf(xbuf[1], sizeof(xbuf[1]), "-%ds", span_s / 2);
    snprintf(xbuf[2], sizeof(xbuf[2]), "0s");

    static const lv_coord_t x_axis_x[CHART_AXIS_LBL_CNT] = {
        CHART_AXIS_LEFT_W,
        CHART_AXIS_LEFT_W + CHART_PLOT_W / 2 - 20,
        CHART_AXIS_LEFT_W + CHART_PLOT_W - 40
    };
    static const lv_text_align_t x_axis_align[CHART_AXIS_LBL_CNT] = {
        LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_RIGHT
    };
    for (int i = 0; i < CHART_AXIS_LBL_CNT; i++) {
        lv_obj_t *lbl = lv_label_create(s_body);
        lv_obj_set_style_text_color(lbl, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_align(lbl, x_axis_align[i], LV_PART_MAIN);
        lv_obj_set_width(lbl, 40);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_label_set_text(lbl, xbuf[i]);
        lv_obj_set_pos(lbl, x_axis_x[i], CHART_TOP + CHART_PLOT_H + 2);
        s_chart_xlbl[i] = lbl;
    }

    for (int i = 0; i < CHART_SRC_BTN_CNT; i++) {
        ir_chart_source_t src = s_chart_src_order[i];
        bool selected = (g_ir_settings.chart_source == src);

        lv_obj_t *btn = lv_btn_create(s_body);
        lv_obj_set_size(btn, 62, 26);
        lv_obj_set_pos(btn, 10 + i * 66, 134);
        lv_obj_set_style_radius(btn, 5, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(selected ? UI_BLUE : UI_MENU_CARD),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, chart_source_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)src);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, lang_font(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl,
                                    lv_color_hex(selected ? UI_WHITE : UI_TEXT_DARK),
                                    LV_PART_MAIN);
        lv_label_set_text(lbl, chart_source_name(src));
        lv_obj_center(lbl);

        s_chart_src_btn[i] = btn;
        s_chart_src_lbl[i] = lbl;
    }

    /* 不再显示底部状态行（曲线来源 + 温度 + n=xx），把高度让给绘图区 */
    lv_obj_t *clear = create_blue_button(s_body, LV_SYMBOL_CLOSE, 92, 30,
                                         chart_clear_event_cb, NULL);
    lv_obj_align(clear, LV_ALIGN_BOTTOM_MID, -50, -6);
    lv_obj_t *save = create_blue_button(s_body, LV_SYMBOL_DOWNLOAD, 92, 30,
                                        chart_save_event_cb, NULL);
    lv_obj_align(save, LV_ALIGN_BOTTOM_MID, 50, -6);
    s_chart_save_btn = save;
    chart_refresh();
}

static void rotate_event_cb(lv_event_t *event)
{
    lv_obj_t *roller = lv_event_get_target(event);
    uint16_t selected = lv_roller_get_selected(roller);
    if (selected >= IR_ROT_MAX) return;
    g_ir_settings.rotate = (ir_rotate_t)selected;
    ir_camera_update_transform();
    save_settings();
}

static void render_rotate_page(void)
{
    create_page_title(lang_str(STR_ROTATE));

    lv_obj_t *roller = lv_roller_create(s_body);
    lv_roller_set_options(roller, "0°\n90°\n180°\n270°", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_obj_set_width(roller, 116);
    lv_obj_align(roller, LV_ALIGN_TOP_MID, 0, 31);
    lv_obj_set_style_bg_color(roller, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, lv_color_hex(UI_BLUE),
                              LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_border_width(roller, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(roller, lv_color_hex(UI_LINE_GRAY),
                                  LV_PART_MAIN);
    lv_roller_set_selected(roller, (uint16_t)g_ir_settings.rotate, LV_ANIM_OFF);
    lv_obj_add_event_cb(roller, rotate_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *hint = lv_label_create(s_body);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(hint, "Scroll to rotate 0°, 90°, 180° or 270°");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void mirror_event_cb(lv_event_t *event)
{
    int horizontal = (int)(intptr_t)lv_event_get_user_data(event);
    bool checked = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (horizontal) g_ir_settings.mirror_horizontal = checked;
    else g_ir_settings.mirror_vertical = checked;
    ir_camera_update_transform();
    save_settings();
}

static void add_mirror_switch(const char *text, bool horizontal, bool checked,
                              lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(s_body);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, lang_font(), LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);

    lv_obj_t *sw = lv_switch_create(s_body);
    lv_obj_set_size(sw, 62, 30);
    lv_obj_align(sw, LV_ALIGN_TOP_MID, 0, y + 23);
    style_dark_switch(sw);
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, mirror_event_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)horizontal);
}

static void render_mirror_page(void)
{
    create_page_title(lang_str(STR_MIRROR));
    add_mirror_switch(lang_str(STR_VERTICAL), false, g_ir_settings.mirror_vertical, 36);
    add_mirror_switch(lang_str(STR_HORIZONTAL), true, g_ir_settings.mirror_horizontal, 120);
}

static void scale_event_cb(lv_event_t *event)
{
    lv_obj_t *selected = lv_event_get_target(event);
    uint32_t count = lv_obj_get_child_cnt(s_scale_box);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(s_scale_box, i);
        if (child == selected) lv_obj_add_state(child, LV_STATE_CHECKED);
        else lv_obj_clear_state(child, LV_STATE_CHECKED);
    }
    g_ir_settings.temp_scale =
        (ir_temp_scale_t)(uintptr_t)lv_event_get_user_data(event);
    save_settings();
}

static void render_scale_page(void)
{
    create_page_title(lang_str(STR_TEMP_SCALE));

    s_scale_box = lv_obj_create(s_body);
    style_reset(s_scale_box);
    lv_obj_set_size(s_scale_box, 208, 160);
    lv_obj_align(s_scale_box, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_color(s_scale_box, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scale_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_scale_box, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_scale_box, lv_color_hex(UI_LINE_GRAY),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(s_scale_box, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_scale_box, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_scale_box, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_scale_box, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < IR_SCALE_MAX; i++) {
        lv_obj_t *checkbox = lv_checkbox_create(s_scale_box);
        lv_checkbox_set_text(checkbox, lang_str(s_scale_name_ids[i]));
        lv_obj_set_style_text_color(checkbox, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
        lv_obj_set_style_text_font(checkbox, lang_menu_font(), LV_PART_MAIN);
        lv_obj_set_style_radius(checkbox, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(checkbox, 2, LV_PART_INDICATOR);
        lv_obj_set_style_border_color(checkbox, lv_color_hex(UI_BLUE),
                                      LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(checkbox, lv_color_hex(UI_BLUE),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(checkbox, lv_color_hex(UI_MENU_ACCENT),
                                    LV_PART_INDICATOR | LV_STATE_CHECKED);
        if ((int)g_ir_settings.temp_scale == i) {
            lv_obj_add_state(checkbox, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(checkbox, scale_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }
}

static void brightness_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    g_ir_settings.brightness = (int)lv_slider_get_value(slider);
    apply_backlight();
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
    if (label) {
        lv_label_set_text_fmt(label, "%s: %d", lang_str(STR_BRIGHTNESS),
                              g_ir_settings.brightness);
    }
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) save_settings();
}

static void sleep_event_cb(lv_event_t *event)
{
    int step = (int)(intptr_t)lv_event_get_user_data(event);
    g_ir_settings.auto_sleep_min =
        clamp_int(g_ir_settings.auto_sleep_min + step, 0, 9999);
    screen_wake();
    save_settings();

    if (s_sleep_value) {
        lv_label_set_text_fmt(s_sleep_value, "%04d min",
                              g_ir_settings.auto_sleep_min);
    }
}

static void render_screen_page(void)
{
    create_page_title(lang_str(STR_SCREEN));

    lv_obj_t *brightness = lv_label_create(s_body);
    lv_obj_set_style_text_color(brightness, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(brightness, lang_font(), LV_PART_MAIN);
    lv_label_set_text_fmt(brightness, "%s: %d", lang_str(STR_BRIGHTNESS),
                          g_ir_settings.brightness);
    lv_obj_align(brightness, LV_ALIGN_TOP_MID, 0, 33);

    lv_obj_t *slider = lv_slider_create(s_body);
    lv_obj_set_size(slider, 190, 18);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, g_ir_settings.brightness, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_event_cb, LV_EVENT_VALUE_CHANGED,
                        brightness);
    lv_obj_add_event_cb(slider, brightness_event_cb, LV_EVENT_RELEASED,
                        brightness);

    lv_obj_t *sleep = lv_label_create(s_body);
    lv_obj_set_style_text_color(sleep, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(sleep, lang_font(), LV_PART_MAIN);
    lv_label_set_text(sleep, lang_str(STR_AUTO_SLEEP));
    lv_obj_align(sleep, LV_ALIGN_TOP_MID, 0, 89);

    lv_obj_t *minus = create_blue_button(s_body, LV_SYMBOL_MINUS, 44, 38,
                                         sleep_event_cb, (void *)(intptr_t)-1);
    lv_obj_align(minus, LV_ALIGN_TOP_MID, -68, 114);

    s_sleep_value = lv_label_create(s_body);
    lv_obj_set_size(s_sleep_value, 82, 34);
    lv_obj_set_style_bg_color(s_sleep_value, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sleep_value, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sleep_value, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_sleep_value, lv_color_hex(UI_LINE_GRAY),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(s_sleep_value, 5, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_sleep_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_sleep_value, lv_color_hex(UI_TEXT_DARK),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_sleep_value, &lv_font_montserrat_18,
                               LV_PART_MAIN);
    lv_label_set_text_fmt(s_sleep_value, "%04d min",
                          g_ir_settings.auto_sleep_min);
    lv_obj_align(s_sleep_value, LV_ALIGN_TOP_MID, 0, 114);

    lv_obj_t *plus = create_blue_button(s_body, LV_SYMBOL_PLUS, 44, 38,
                                        sleep_event_cb, (void *)(intptr_t)1);
    lv_obj_align(plus, LV_ALIGN_TOP_MID, 68, 114);

    lv_obj_t *hint = lv_label_create(s_body);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(hint, "0 min disables auto power off");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

static lv_point_t s_filter_points[41];

static void filter_update_graph(void)
{
    for (int i = 0; i < 41; i++) {
        float x = (float)(i - 20) / 8.0f;
        float sigma = 1.0f;
        switch (g_ir_settings.filter_mode) {
        case IR_FILTER_GAUSSIAN: sigma = 1.2f; break;
        case IR_FILTER_MEDIAN: sigma = 0.55f; break;
        case IR_FILTER_OFF: sigma = 0.18f; break;
        default: sigma = 0.90f; break;
        }
        float y = expf(-(x * x) / (2.0f * sigma * sigma));
        s_filter_points[i].x = (lv_coord_t)(i * 4);
        s_filter_points[i].y = (lv_coord_t)(55 - (int)(y * 40.0f));
    }
}

static void filter_change_event_cb(lv_event_t *event)
{
    int direction = (int)(intptr_t)lv_event_get_user_data(event);
    int mode = (int)g_ir_settings.filter_mode;
    mode = (mode + direction + IR_FILTER_MAX) % IR_FILTER_MAX;
    g_ir_settings.filter_mode = (ir_filter_mode_t)mode;
    save_settings();
    schedule_page(PAGE_FILTER);
}

static void render_filter_page(void)
{
    create_page_title(s_filter_names[g_ir_settings.filter_mode]);

    lv_obj_t *graph_frame = lv_obj_create(s_body);
    style_reset(graph_frame);
    lv_obj_set_size(graph_frame, 176, 70);
    lv_obj_align(graph_frame, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(graph_frame, lv_color_hex(UI_MENU_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(graph_frame, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(graph_frame, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(graph_frame, lv_color_hex(UI_LINE_GRAY),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(graph_frame, 6, LV_PART_MAIN);

    filter_update_graph();
    lv_obj_t *line = lv_line_create(graph_frame);
    lv_line_set_points(line, s_filter_points, 41);
    lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(line, lv_color_hex(UI_MENU_ACCENT), LV_PART_MAIN);
    lv_obj_align(line, LV_ALIGN_CENTER, 0, -2);

    lv_obj_t *prev = create_blue_button(s_body, LV_SYMBOL_LEFT, 92, 38,
                                        filter_change_event_cb,
                                        (void *)(intptr_t)-1);
    lv_obj_align(prev, LV_ALIGN_BOTTOM_MID, -50, -17);
    lv_obj_t *next = create_blue_button(s_body, LV_SYMBOL_RIGHT, 92, 38,
                                        filter_change_event_cb,
                                        (void *)(intptr_t)1);
    lv_obj_align(next, LV_ALIGN_BOTTOM_MID, 50, -17);
}

static void emissivity_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    g_ir_settings.emissivity = (float)lv_slider_get_value(slider) / 100.0f;
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
    if (label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s: %.2f", lang_str(STR_EMISSIVITY),
                 (double)g_ir_settings.emissivity);
        lv_label_set_text(label, buf);
    }
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) save_settings();
}

static void render_emissivity_page(void)
{
    create_page_title(lang_str(STR_EMISSIVITY));

    lv_obj_t *label = lv_label_create(s_body);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, lang_font(), LV_PART_MAIN);
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s: %.2f", lang_str(STR_EMISSIVITY),
                 (double)g_ir_settings.emissivity);
        lv_label_set_text(label, buf);
    }
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 78);

    lv_obj_t *slider = lv_slider_create(s_body);
    lv_obj_set_size(slider, 194, 18);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider,
                        (int)(g_ir_settings.emissivity * 100.0f + 0.5f),
                        LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 105);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, emissivity_event_cb, LV_EVENT_VALUE_CHANGED, label);
    lv_obj_add_event_cb(slider, emissivity_event_cb, LV_EVENT_RELEASED, label);

    lv_obj_t *hint = lv_label_create(s_body);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(hint, "Typical skin / water: 0.95 - 0.98");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void gamma_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    g_ir_settings.gamma = (float)lv_slider_get_value(slider) / 100.0f;
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
    if (label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s: %.2f", lang_str(STR_GAMMA),
                 (double)g_ir_settings.gamma);
        lv_label_set_text(label, buf);
    }
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) save_settings();
}

static void render_gamma_page(void)
{
    create_page_title(lang_str(STR_GAMMA));

    lv_obj_t *label = lv_label_create(s_body);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, lang_font(), LV_PART_MAIN);
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s: %.2f", lang_str(STR_GAMMA),
                 (double)g_ir_settings.gamma);
        lv_label_set_text(label, buf);
    }
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 58);

    lv_obj_t *slider = lv_slider_create(s_body);
    lv_obj_set_size(slider, 194, 18);
    lv_slider_set_range(slider, 50, 300);
    lv_slider_set_value(slider,
                        (int)(g_ir_settings.gamma * 100.0f + 0.5f),
                        LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, gamma_event_cb, LV_EVENT_VALUE_CHANGED, label);
    lv_obj_add_event_cb(slider, gamma_event_cb, LV_EVENT_RELEASED, label);

    lv_obj_t *hint = lv_label_create(s_body);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(hint, "Lower values reveal cooler details");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);
}

static void calibration_event_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    g_ir_settings.temperature_offset_c =
        (float)lv_slider_get_value(slider) / 10.0f;
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(event);
    if (label) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s: %+.1f C", lang_str(STR_OFFSET),
                 (double)g_ir_settings.temperature_offset_c);
        lv_label_set_text(label, buf);
    }
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) save_settings();
}

static void calibration_reset_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    g_ir_settings.temperature_offset_c = 0.0f;
    save_settings();
    schedule_page(PAGE_CALIBRATION);
}

static void render_calibration_page(void)
{
    create_page_title(lang_str(STR_CALIBRATION));

    lv_obj_t *hint = lv_label_create(s_body);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(hint, 260, 38);
    lv_label_set_text(hint, "Temperature offset applies to all\ndisplayed readings.");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t *label = lv_label_create(s_body);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, lang_font(), LV_PART_MAIN);
    lv_obj_set_size(label, 260, 26);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s: %+.1f C", lang_str(STR_OFFSET),
                 (double)g_ir_settings.temperature_offset_c);
        lv_label_set_text(label, buf);
    }
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 76);

    lv_obj_t *slider = lv_slider_create(s_body);
    lv_obj_set_size(slider, 194, 16);
    lv_slider_set_range(slider, -100, 100);
    lv_slider_set_value(slider,
                        (int)(g_ir_settings.temperature_offset_c * 10.0f),
                        LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE_LIGHT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_BLUE), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, calibration_event_cb, LV_EVENT_VALUE_CHANGED, label);
    lv_obj_add_event_cb(slider, calibration_event_cb, LV_EVENT_RELEASED, label);

    lv_obj_t *reset = create_blue_text_button(s_body, lang_str(STR_RESET_OFFSET), 128, 38,
                                              calibration_reset_event_cb, NULL);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static void language_async_cb(void *user_data)
{
    app_lang_t language = (app_lang_t)(uintptr_t)user_data;
    lang_set(language);
    thermal_ui_refresh_language();
    render_page(PAGE_LANGUAGE);
}

static void language_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    app_lang_t language = (app_lang_t)(uintptr_t)lv_event_get_user_data(event);
    lv_async_call(language_async_cb, (void *)(uintptr_t)language);
}

static void render_language_page(void)
{
    create_page_title(lang_str(STR_LANGUAGE));

    /* 整行铺满屏宽、行高 34px，点击面积比原来的小按钮大得多；
       当前语言用深灰高亮 + 右侧勾选标记，一眼就能看出选了哪个。 */
    lv_obj_t *list = lv_obj_create(s_body);
    style_reset(list);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(list, SETTINGS_W, SETTINGS_BODY_H - 28);
    lv_obj_set_pos(list, 0, 28);
    lv_obj_set_style_bg_color(list, lv_color_hex(UI_MENU_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(list, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, lv_color_hex(UI_LINE_GRAY), LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    const app_lang_t order[LANG_COUNT] = { LANG_ZH, LANG_EN, LANG_RU, LANG_JA, LANG_KO };
    for (int i = 0; i < LANG_COUNT; i++) {
        app_lang_t language = order[i];
        bool selected = (lang_get() == language);

        lv_obj_t *row = lv_btn_create(list);
        lv_obj_set_size(row, SETTINGS_W, 34);
        lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(UI_LINE_GRAY), LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, lv_color_hex(selected ? UI_BLUE : UI_MENU_CARD),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_BLUE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, language_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)language);

        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_style_text_font(label, lang_font_for(language), LV_PART_MAIN);
        lv_obj_set_style_text_color(label,
                                    lv_color_hex(selected ? UI_WHITE : UI_TEXT_DARK),
                                    LV_PART_MAIN);
        lv_label_set_text(label, lang_name(language));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 16, 0);

        if (selected) {
            lv_obj_t *check = lv_label_create(row);
            lv_obj_set_style_text_font(check, &lv_font_montserrat_18, LV_PART_MAIN);
            lv_obj_set_style_text_color(check, lv_color_white(), LV_PART_MAIN);
            lv_label_set_text(check, LV_SYMBOL_OK);
            lv_obj_align(check, LV_ALIGN_RIGHT_MID, -16, 0);
        }
    }
}

static void factory_reset_async_cb(void *unused)
{
    (void)unused;
    ir_camera_settings_reset();
    nvs_handle_t handle;
    if (nvs_open("ir_chart", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "samples");
        nvs_erase_key(handle, "count");
        nvs_commit(handle);
        nvs_close(handle);
    }
    lang_set(LANG_EN);
    thermal_ui_refresh_language();
    apply_backlight();
    screen_wake();
    render_page(s_parent_page);
}

static void factory_reset_confirm_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_async_call(factory_reset_async_cb, NULL);
}

static void render_factory_reset_page(void)
{
    create_page_title(lang_str(STR_FACTORY_RESET));

    lv_obj_t *hint = lv_label_create(s_body);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_DANGER), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(hint,
                      "Reset all display settings, calibration\n"
                      "and language?");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *button = lv_btn_create(s_body);
    lv_obj_set_size(button, 164, 44);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_radius(button, 7, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_BLUE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_BLUE_DARK), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, factory_reset_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, lang_font(), LV_PART_MAIN);
    lv_label_set_text(label, lang_str(STR_RESET_NOW));
    lv_obj_center(label);
}

static void render_about_page(void)
{
    create_page_title(lang_str(STR_ABOUT));

    lv_obj_t *title = lv_label_create(s_body);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(title, "RT Thermal Camera V3.1");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 49);

    lv_obj_t *version = lv_label_create(s_body);
    lv_obj_set_style_text_color(version, lv_color_hex(UI_TEXT_MID), LV_PART_MAIN);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(version,
                      "LVGL 8  |  280 x 240 Landscape\n"
                      "Sensor: 32 x 24, 16 Hz");
    lv_obj_align(version, LV_ALIGN_TOP_MID, 0, 76);
}

static void render_page(settings_page_t page)
{
    if (!s_body) return;
    s_current_page = page;
    update_header(page);
    s_chart = NULL;
    s_chart_series = NULL;
    s_chart_save_btn = NULL;
    /* 页面即将重建：先回收“保存成功”提示定时器，避免它回调到已删除的按钮 */
    if (s_chart_save_hint) {
        lv_timer_del(s_chart_save_hint);
        s_chart_save_hint = NULL;
    }
    s_scale_box = NULL;
    s_sleep_value = NULL;
    lv_obj_clean(s_body);

    switch (page) {
    case PAGE_MAIN: render_main_page(); break;
    case PAGE_BASIC:
    case PAGE_ADVANCED:
    case PAGE_OTHER: render_category_page(page); break;
    case PAGE_COLOR: render_color_page(); break;
    case PAGE_LABELS: render_labels_page(); break;
    case PAGE_CHART: render_chart_page(); break;
    case PAGE_ROTATE: render_rotate_page(); break;
    case PAGE_MIRROR: render_mirror_page(); break;
    case PAGE_SCALE: render_scale_page(); break;
    case PAGE_SCREEN: render_screen_page(); break;
    case PAGE_FILTER: render_filter_page(); break;
    case PAGE_EMISSIVITY: render_emissivity_page(); break;
    case PAGE_GAMMA: render_gamma_page(); break;
    case PAGE_CALIBRATION: render_calibration_page(); break;
    case PAGE_LANGUAGE: render_language_page(); break;
    case PAGE_FACTORY_RESET: render_factory_reset_page(); break;
    case PAGE_ABOUT: render_about_page(); break;
    default: render_main_page(); break;
    }
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_is_open && s_current_page == PAGE_CHART) chart_refresh();
}

static void activity_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_pointer_indev &&
        s_pointer_indev->proc.state == LV_INDEV_STATE_PRESSED) {
        screen_wake();
    }

    if (!s_screen_sleeping && g_ir_settings.auto_sleep_min > 0) {
        uint32_t timeout_ms =
            (uint32_t)g_ir_settings.auto_sleep_min * 60U * 1000U;
        if (lv_tick_elaps(s_last_activity_ms) >= timeout_ms) {
            sys_power_off();
        }
    }
}

esp_err_t ir_settings_ui_init(void)
{
    esp_err_t err = ir_camera_settings_load();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "settings load: %s, using defaults",
                 esp_err_to_name(err));
        g_ir_settings = (ir_app_settings_t){
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
        ir_camera_update_transform();
        ir_camera_set_color_mode(g_ir_settings.color_mode);
    }

    err = init_backlight_pwm();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight PWM init failed: %s", esp_err_to_name(err));
    }
    s_pointer_indev = lv_indev_get_next(NULL);
    while (s_pointer_indev &&
           s_pointer_indev->driver->type != LV_INDEV_TYPE_POINTER) {
        s_pointer_indev = lv_indev_get_next(s_pointer_indev);
    }
    s_last_activity_ms = lv_tick_get();
    return ESP_OK;
}

void ir_settings_ui_create(lv_obj_t *parent)
{
    if (!parent || s_root) return;

    s_root = lv_obj_create(parent);
    style_reset(s_root);
    lv_obj_set_size(s_root, SETTINGS_W, SETTINGS_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(UI_MENU_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_root, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

    s_header = lv_btn_create(s_root);
    lv_obj_set_size(s_header, SETTINGS_W, SETTINGS_HEADER_H);
    lv_obj_set_pos(s_header, 0, 0);
    lv_obj_set_style_radius(s_header, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_header, lv_color_hex(UI_MENU_HEADER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_header, lv_color_hex(UI_MENU_PRESSED),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(s_header, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_header, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_header, header_back_event_cb, LV_EVENT_CLICKED, NULL);

    s_header_back = lv_label_create(s_header);
    lv_obj_set_style_text_color(s_header_back, lv_color_hex(UI_TEXT_DARK), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_header_back, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(s_header_back, LV_SYMBOL_LEFT);
    lv_obj_align(s_header_back, LV_ALIGN_LEFT_MID, 14, 0);

    s_header_title = lv_label_create(s_header);
    lv_obj_set_style_text_color(s_header_title, lv_color_hex(UI_MENU_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_header_title, lang_menu_font(), LV_PART_MAIN);
    lv_label_set_text(s_header_title, "");

    s_body = lv_obj_create(s_root);
    style_reset(s_body);
    lv_obj_set_size(s_body, SETTINGS_W, SETTINGS_BODY_H);
    lv_obj_set_pos(s_body, 0, SETTINGS_HEADER_H);
    lv_obj_set_style_bg_color(s_body, lv_color_hex(UI_MENU_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_body, 0, LV_PART_MAIN);

    s_ui_timer = lv_timer_create(ui_timer_cb, 500, NULL);
    s_activity_timer = lv_timer_create(activity_timer_cb, 150, NULL);
    render_main_page();

    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_is_open = false;
}

void ir_settings_ui_open(void)
{
    if (!s_root) return;
    render_main_page();
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
    s_is_open = true;
    s_open_ms = lv_tick_get();
    screen_wake();
    ESP_LOGI(TAG, "settings menu opened");
}

void ir_settings_ui_close(void)
{
    if (!s_root) return;
    ir_camera_settings_save();
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_is_open = false;
    screen_wake();
}

bool ir_settings_ui_is_open(void)
{
    return s_is_open && s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

bool ir_settings_ui_screen_sleeping(void)
{
    return s_screen_sleeping;
}

bool ir_settings_ui_chart_page_active(void)
{
    return s_is_open && s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN) &&
           s_current_page == PAGE_CHART;
}

void ir_settings_ui_refresh_language(void)
{
    if (!s_root) return;
    render_page(s_current_page);
}

void ir_settings_ui_note_activity(void)
{
    screen_wake();
}
