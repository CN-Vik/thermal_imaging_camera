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
#include <stdbool.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "dev_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 电源/按键引脚定义（与 app_main.c 保持一致）。
   这两个宏和 TAG 原本只写在 app_main.c 里，dev_sys.c 引用不到，这里补上。 */
#define PIN_PWR_HOLD    GPIO_NUM_36  // 电源自锁输出
#define PIN_KEY_DET     GPIO_NUM_37  // 按键检测输入
#define POWER_KEY_ENABLE_DELAY_MS 3000
#define POWER_KEY_HOLD_MS 2000
#define POWER_KEY_POLL_MS   10
#define POWER_KEY_TASK_PRIORITY 5
static const char *TAG = "dev_sys";

static TaskHandle_t s_power_key_task = NULL;

/* Count consecutive active-low samples; releasing the key resets the timer. */
static void power_key_monitor_task(void *arg)
{
    (void)arg;
    const TickType_t poll_ticks = pdMS_TO_TICKS(POWER_KEY_POLL_MS);
    const TickType_t hold_ticks = pdMS_TO_TICKS(POWER_KEY_HOLD_MS);
    TickType_t pressed_at = 0;
    bool timing_press = false;

    vTaskDelay(pdMS_TO_TICKS(POWER_KEY_ENABLE_DELAY_MS));

    for (;;) {
        int key_level = gpio_get_level(PIN_KEY_DET);
        TickType_t now = xTaskGetTickCount();

        if (key_level == 0) {
            if (!timing_press) {
                pressed_at = now;
                timing_press = true;
                ESP_LOGI(TAG, "Power key pressed (GPIO37=0)");
            } else if ((TickType_t)(now - pressed_at) >= hold_ticks) {
                ESP_LOGI(TAG, "Power key held for 2 seconds; powering off");
                sys_power_off();
                s_power_key_task = NULL;
                vTaskDelete(NULL);
            }
        } else {
            if (timing_press) {
                ESP_LOGI(TAG, "Power key released before shutdown (GPIO37=1)");
            }
            timing_press = false;
        }

        vTaskDelay(poll_ticks);
    }
}

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
    esp_err_t key_err = gpio_config(&key_cfg);
    if (key_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power-key input: %s",
                 esp_err_to_name(key_err));
        return;
    }

    if (s_power_key_task == NULL &&
        xTaskCreatePinnedToCore(power_key_monitor_task, "power_key", 2048, NULL,
                                POWER_KEY_TASK_PRIORITY, &s_power_key_task, 0) != pdPASS) {
        s_power_key_task = NULL;
        ESP_LOGE(TAG, "Failed to create power-key monitor task");
    }

    // 3. 正常打印 LOG
    ESP_LOGI(TAG,"HELLO_ESP32-S3\r\n");
    ESP_LOGI(TAG, "Power_successfully_latched_via_IO36!");

    // 按键持续按下 2 秒后由监测任务调用 sys_power_off()
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

/* 自动关机定时器（esp_timer 实现，一次性） */
static esp_timer_handle_t s_auto_off_timer = NULL;

static void auto_poweroff_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "auto power off timer fired");
    sys_power_off();
}

void sys_start_auto_poweroff(uint32_t seconds)
{
    if (s_auto_off_timer) {
        return; /* 已启动，避免重复 */
    }
    const esp_timer_create_args_t args = {
        .callback = auto_poweroff_cb,
        .name = "auto_off",
        .dispatch_method = ESP_TIMER_TASK,
    };
    if (esp_timer_create(&args, &s_auto_off_timer) == ESP_OK) {
        esp_timer_start_once(s_auto_off_timer, (uint64_t)seconds * 1000000ULL);
        ESP_LOGI(TAG, "auto power off in %lu s", (unsigned long)seconds);
    }
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
