/**
 * @file boot_ui.h
 * @brief 开机画面 + 首次开机语言选择
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 显示开机 Logo，阻塞约 1.5 秒 */
void boot_show_splash(lv_disp_t *disp);

/* 显示语言选择界面，阻塞直到用户选择并保存 */
void boot_show_language_select(lv_disp_t *disp);

#ifdef __cplusplus
}
#endif
