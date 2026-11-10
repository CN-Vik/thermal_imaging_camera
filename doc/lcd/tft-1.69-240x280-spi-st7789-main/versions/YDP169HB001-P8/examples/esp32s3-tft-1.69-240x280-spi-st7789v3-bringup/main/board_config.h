#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

/* 1.69" IPS 240x280, ST7789V3, SPI */
#define LCD_SPI_HOST        SPI2_HOST

#define LCD_PIN_BL          GPIO_NUM_9
#define LCD_PIN_RST         GPIO_NUM_10
#define LCD_PIN_DC          GPIO_NUM_11
#define LCD_PIN_CS          GPIO_NUM_12
#define LCD_PIN_SCLK        GPIO_NUM_13
#define LCD_PIN_MOSI        GPIO_NUM_14

#define LCD_H_RES           240
#define LCD_V_RES           280
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)

/* 竖屏 240(宽) x 280(高)；INVON 来自 examples/ST7789 */
#define LCD_X_GAP           0
#define LCD_Y_GAP           20
#define LCD_MIRROR_X        false
#define LCD_MIRROR_Y        false
#define LCD_SWAP_XY         false
#define LCD_INVERT_COLOR    true

#define LCD_BL_ON_LEVEL     1
