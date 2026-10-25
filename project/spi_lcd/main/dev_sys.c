/**
 * @file dev_sys.c
 * @author vik (ufo281@outlook.com)
 * @brief 设备系统
 * @version 0.1
 * @date 2026-10-25
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "dev_sys.h"

/* 电源/按键引脚定义（与 app_main.c 保持一致）。
   这两个宏和 TAG 原本只写在 app_main.c 里，dev_sys.c 引用不到，这里补上。 */
#define PIN_PWR_HOLD    GPIO_NUM_36  // 电源自锁输出
#define PIN_KEY_DET     GPIO_NUM_37  // 按键检测输入
static const char *TAG = "dev_sys";


void power_on(void)
{
    // 1. 最优先任务：拉高 IO36，锁定电源！松手不掉电！
    gpio_config_t hold_cfg = {
        .pin_bit_mask = (1ULL << PIN_PWR_HOLD),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&hold_cfg);
    gpio_set_level(PIN_PWR_HOLD, 1);  // 立即输出高电平锁定电源

    // 2. 配置 IO37 为带上拉的输入，用于开机后检测按键
    gpio_config_t key_cfg = {
        .pin_bit_mask = (1ULL << PIN_KEY_DET),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&key_cfg);

    // 3. 正常打印 LOG
    ESP_LOGI(TAG,"HELLO_ESP32-S3\r\n");
    ESP_LOGI(TAG, "Power_successfully_latched_via_IO36!");

    // 测试按键检测：按下按键时 IO37 会变为 0
    // 如果后续想关机，直接执行：gpio_set_level(PIN_PWR_HOLD, 0); 即可彻底断电
}


void power_off_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    sys_power_off();
}

/* 彻底断电：拉低 IO36，松手即断电 */
void sys_power_off(void)
{
    gpio_set_level(PIN_PWR_HOLD, 0);
    ESP_LOGI(TAG, "Power_Off_via_IO36!");
}


/* 系统重启按钮回调 */
void restart_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    /* TODO: 如有需要，先弹出“确认重启”对话框 */

    esp_restart();
}