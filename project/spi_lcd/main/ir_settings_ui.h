#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ir_settings_ui_init(void);
void ir_settings_ui_create(lv_obj_t *parent);
void ir_settings_ui_open(void);
void ir_settings_ui_close(void);
bool ir_settings_ui_is_open(void);
void ir_settings_ui_refresh_language(void);
void ir_settings_ui_note_activity(void);

#ifdef __cplusplus
}
#endif
