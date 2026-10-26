/**
 * @file dev_sys.h
 * @author vik (ufo281@outlook.com)
 * @brief 设备系统接口（电源 / 按键 / 系统重启）
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 开机上电：拉高 IO36 锁定电源，并配置 IO37 按键检测 */
void power_on(void);

/* 系统重启按钮回调（可直接绑定到 LVGL 按钮的 LV_EVENT_CLICKED） */
void restart_button_event_cb(lv_event_t *e);
void power_off_event_cb(lv_event_t *e);

/* 彻底断电：拉低 IO36（可被 LVGL 开关等非 CLICKED 事件直接调用） */
void sys_power_off(void);

/* 启动自动关机定时器：seconds 秒后自动彻底断电（一次性） */
void sys_start_auto_poweroff(uint32_t seconds);


#ifdef __cplusplus
}
#endif
