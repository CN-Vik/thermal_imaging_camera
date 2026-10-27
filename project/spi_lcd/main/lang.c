/**
 * @file lang.c
 * @brief 语言设置持久化（NVS Flash）
 */
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lang.h"
#include "lvgl.h"

#define LANG_NVS_NAMESPACE "thermal"
#define LANG_NVS_KEY       "lang"

static const char *TAG = "lang";

static app_lang_t s_lang = LANG_EN;
static bool s_configured = false;



const uint8_t * app_font_get_bitmap_fix_odd_width(const lv_font_t * font, uint32_t unicode_letter)
{
    const uint8_t * src = lv_font_get_bitmap_fmt_txt(font, unicode_letter);
    if (!src) return NULL;

    lv_font_glyph_dsc_t gdsc;
    if (!font->get_glyph_dsc(font, &gdsc, unicode_letter, 0)) return src;

    /* 偶数宽度无需处理，直接返回原位图指针 */
    if ((gdsc.box_w & 1) == 0) {
        return src;
    }

    /* 奇数宽度：将“按行字节对齐”的 4-bpp 数据重排为 LVGL 需要的“跨行连续紧凑位流” */
    static uint8_t s_packed_buf[256]; /* 足够容纳 20x20 4bpp 字形 */
    uint16_t row_bytes = (gdsc.box_w + 1) >> 1;
    uint32_t total_px = (uint32_t)gdsc.box_w * gdsc.box_h;
    if (((total_px + 1) >> 1) > sizeof(s_packed_buf)) {
        return src;
    }

    uint32_t out_nibble_idx = 0;
    for (uint16_t y = 0; y < gdsc.box_h; y++) {
        const uint8_t * row_ptr = src + y * row_bytes;
        for (uint16_t x = 0; x < gdsc.box_w; x++) {
            uint8_t val = (x & 1) ? (row_ptr[x >> 1] & 0x0F) : (row_ptr[x >> 1] >> 4);
            if ((out_nibble_idx & 1) == 0) {
                s_packed_buf[out_nibble_idx >> 1] = (val << 4);
            } else {
                s_packed_buf[out_nibble_idx >> 1] |= val;
            }
            out_nibble_idx++;
        }
    }
    return s_packed_buf;
}

esp_err_t lang_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init failed (%s), use default language", esp_err_to_name(err));
        s_lang = LANG_EN;
        s_configured = false;
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(LANG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        s_configured = false;
        return err;
    }

    int32_t val = LANG_EN;
    err = nvs_get_i32(handle, LANG_NVS_KEY, &val);
    nvs_close(handle);

    if (err == ESP_OK) {
        s_lang = (app_lang_t)val;
        if (s_lang >= LANG_COUNT) {
            s_lang = LANG_EN;
        }
        s_configured = true;
        ESP_LOGI(TAG, "loaded language: %s", lang_name(s_lang));
    } else {
        s_lang = LANG_EN;
        s_configured = false;
        ESP_LOGI(TAG, "no saved language, default EN");
    }
    return err;
}

app_lang_t lang_get(void)
{
    return s_lang;
}

void lang_set(app_lang_t l)
{
    if (l >= LANG_COUNT) {
        l = LANG_EN;
    }
    s_lang = l;
    s_configured = true;

    nvs_handle_t handle;
    if (nvs_open(LANG_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i32(handle, LANG_NVS_KEY, (int32_t)l);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "set language: %s", lang_name(l));
}

bool lang_configured(void)
{
    return s_configured;
}
