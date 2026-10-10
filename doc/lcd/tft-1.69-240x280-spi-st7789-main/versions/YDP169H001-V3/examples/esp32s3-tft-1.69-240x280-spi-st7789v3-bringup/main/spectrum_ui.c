#include "spectrum_ui.h"

#include "board_config.h"
#include "esp_random.h"
#include "lvgl.h"

#define BAR_COUNT       20
#define BAR_AREA_H      190
#define BAR_W           8
#define BAR_GAP         4
#define BAR_MIN_H       12
#define TICK_MS         40

static lv_obj_t *s_bars[BAR_COUNT];
static int32_t s_cur[BAR_COUNT];
static int32_t s_tgt[BAR_COUNT];
static int32_t s_wait[BAR_COUNT];
static int32_t s_bar_x[BAR_COUNT];
static int32_t s_base_y;

static lv_color_t bar_color(int32_t h)
{
    const int32_t range = BAR_AREA_H - BAR_MIN_H;
    const int32_t t = ((h - BAR_MIN_H) * 255) / (range > 0 ? range : 1);

    if (t < 85) {
        return lv_color_mix(lv_color_hex(0x00E5FF), lv_color_hex(0x7C4DFF), (uint8_t)(t * 3));
    }
    if (t < 170) {
        return lv_color_mix(lv_color_hex(0x7C4DFF), lv_color_hex(0xFF4081), (uint8_t)((t - 85) * 3));
    }
    return lv_color_mix(lv_color_hex(0xFF4081), lv_color_hex(0xFFAB40), (uint8_t)((t - 170) * 3));
}

static void style_bar(lv_obj_t *bar, int32_t h)
{
    lv_obj_set_height(bar, h);
    lv_obj_set_y(bar, s_base_y - h);
    lv_obj_set_style_bg_color(bar, bar_color(h), 0);
    lv_obj_set_style_bg_grad_color(bar, lv_color_lighten(bar_color(h), 60), 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(bar, bar_color(h), 0);
    lv_obj_set_style_shadow_width(bar, h > 60 ? 8 : 0, 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_40, 0);
}

static int32_t rand_range(int32_t lo, int32_t hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + (int32_t)(esp_random() % (uint32_t)(hi - lo + 1));
}

static int32_t clamp_h(int32_t h)
{
    if (h < BAR_MIN_H) {
        return BAR_MIN_H;
    }
    if (h > BAR_AREA_H) {
        return BAR_AREA_H;
    }
    return h;
}

static void renew_bar_target(int i)
{
    const int band = (i * 4) / BAR_COUNT;
    const uint32_t roll = esp_random() % 100;

    int32_t lo = BAR_MIN_H + band * 12;
    int32_t hi = BAR_MIN_H + 35 + band * 38 + (int32_t)(esp_random() % 40);
    if (hi > BAR_AREA_H) {
        hi = BAR_AREA_H;
    }

    if (roll < 10) {
        s_tgt[i] = rand_range(BAR_MIN_H, BAR_MIN_H + 18);
    } else if (roll < 22) {
        s_tgt[i] = rand_range(hi - 20, hi);
    } else if (roll < 30) {
        s_tgt[i] = rand_range(BAR_AREA_H - 50, BAR_AREA_H);
    } else {
        s_tgt[i] = rand_range(lo, hi);
    }

    s_tgt[i] = clamp_h(s_tgt[i]);
    s_wait[i] = rand_range(1, 4 + band * 2);
}

static void spectrum_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if ((esp_random() % 14) == 0) {
        const int center = rand_range(2, BAR_COUNT - 3);
        for (int d = -2; d <= 2; d++) {
            const int idx = center + d;
            if (idx < 0 || idx >= BAR_COUNT) {
                continue;
            }
            s_tgt[idx] = clamp_h(rand_range(BAR_AREA_H - 70 + d * 8, BAR_AREA_H - 10));
            s_wait[idx] = rand_range(1, 3);
        }
    }

    for (int i = 0; i < BAR_COUNT; i++) {
        if (s_wait[i] <= 0) {
            renew_bar_target(i);
        } else {
            s_wait[i]--;
        }

        if ((esp_random() % 5) == 0) {
            s_tgt[i] = clamp_h(s_tgt[i] + rand_range(-22, 22));
        }

        const int32_t diff = s_tgt[i] - s_cur[i];
        if (diff > 0) {
            s_cur[i] += rand_range(4, 14);
            if (s_cur[i] > s_tgt[i]) {
                s_cur[i] = s_tgt[i];
            }
        } else if (diff < 0) {
            s_cur[i] -= rand_range(2, 8);
            if (s_cur[i] < s_tgt[i]) {
                s_cur[i] = s_tgt[i];
            }
        }

        s_cur[i] = clamp_h(s_cur[i]);
        style_bar(s_bars[i], s_cur[i]);
    }
}

void spectrum_ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x08080F), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x101020), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Spectrum");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0F0), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "audio visualizer");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x606080), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 34);

    const int total_w = BAR_COUNT * BAR_W + (BAR_COUNT - 1) * BAR_GAP;
    const int start_x = (LCD_H_RES - total_w) / 2;
    s_base_y = LCD_V_RES - 28;

    for (int i = 0; i < BAR_COUNT; i++) {
        s_cur[i] = BAR_MIN_H;
        s_tgt[i] = rand_range(BAR_MIN_H, BAR_MIN_H + 80);
        s_wait[i] = rand_range(0, 3);
        s_bar_x[i] = start_x + i * (BAR_W + BAR_GAP);

        s_bars[i] = lv_obj_create(scr);
        lv_obj_remove_flag(s_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_bars[i], BAR_W, s_cur[i]);
        lv_obj_set_style_radius(s_bars[i], 4, 0);
        lv_obj_set_style_border_width(s_bars[i], 0, 0);
        lv_obj_set_pos(s_bars[i], s_bar_x[i], s_base_y - s_cur[i]);
        style_bar(s_bars[i], s_cur[i]);
    }

    lv_timer_create(spectrum_timer_cb, TICK_MS, NULL);
}
