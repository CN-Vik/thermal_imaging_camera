#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LANG_ZH = 0,
    LANG_EN,
    LANG_RU,
    LANG_JA,
    LANG_KO,
    LANG_COUNT
} app_lang_t;

typedef enum {
    STR_LANG_TITLE = 0,
    STR_MODE_CONFIG,
    STR_COLOR_MODE,
    STR_POWER_OFF,
    STR_BACK,
    STR_OK,
    STR_POWER,
    STR_SELECT_FMT,
    STR_CALIBRATING,
    STR_LANGUAGE,
    STR_CAPTURE,
    STR_GALLERY,
    STR_DELETE,
    STR_DELETE_ALL,
    STR_SAVED,
    STR_NO_PHOTOS,
    STR_CLOSE,
    STR_PHOTO,
    STR_FULL,
    STR_COUNT
} lang_str_id_t;

const char *lang_name(app_lang_t l);
const char *lang_str(lang_str_id_t id);
const lv_font_t *lang_font(void);
const lv_font_t *lang_font_for(app_lang_t l);

esp_err_t lang_init(void);
app_lang_t lang_get(void);
void lang_set(app_lang_t l);
bool lang_configured(void);

#ifdef __cplusplus
}
#endif
