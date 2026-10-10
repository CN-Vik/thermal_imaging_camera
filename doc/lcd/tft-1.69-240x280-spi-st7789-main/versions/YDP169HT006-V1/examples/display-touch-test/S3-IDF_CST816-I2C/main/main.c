/*
 * SPDX-FileCopyrightText: Copyright 2026 OSPTEK
 * SPDX-License-Identifier: CC-BY-4.0
 *
 * https://github.com/osptek
 */

#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "esp_lcd_touch_cst816s.h"

#define CST816S_SDA 9
#define CST816S_SCL 10
#define CST816S_RST 11
#define CST816S_INT 12

#define CONFIG_LCD_HRES 240
#define CONFIG_LCD_VRES 280

static const char *TAG = "example";

void app_main(void)
{
    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    esp_lcd_touch_handle_t tp = NULL;
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CST816S_SDA,
        .scl_io_num = CST816S_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000, // 100kHz
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0, 0, 0));

    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS, 
        .control_phase_bytes = 1,           
        .dc_bit_offset = 0,                 
        .lcd_cmd_bits = 8,                 
        .flags =                            
        {                                   
            .disable_control_phase = 1,     
        }                                   
    };

    ESP_LOGI(TAG, "Initialize touch IO (I2C)");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = CONFIG_LCD_HRES,
        .y_max = CONFIG_LCD_VRES,
        .rst_gpio_num = CST816S_RST,
        .int_gpio_num = CST816S_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "Initialize touch controller CST816S");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp));

    while (1)
    {
        esp_lcd_touch_read_data(tp);
        bool touchpad_pressed = esp_lcd_touch_get_coordinates(tp, touch_x, touch_y, touch_strength, &touch_cnt, 1);
        if (touchpad_pressed)
        {
            printf("x:%d,y:%d\n", touch_x[0], touch_y[0]);
        }
    }
}