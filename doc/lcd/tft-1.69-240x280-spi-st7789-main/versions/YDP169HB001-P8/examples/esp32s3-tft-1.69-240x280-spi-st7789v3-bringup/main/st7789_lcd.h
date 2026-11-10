#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

esp_err_t st7789_lcd_init(void);
esp_lcd_panel_io_handle_t st7789_lcd_io(void);
esp_lcd_panel_handle_t st7789_lcd_panel(void);
