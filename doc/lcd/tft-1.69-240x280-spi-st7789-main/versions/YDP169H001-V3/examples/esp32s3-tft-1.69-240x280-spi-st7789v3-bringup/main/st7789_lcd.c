#include "st7789_lcd.h"

#include "board_config.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

static const char *TAG = "st7789";

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;

static void backlight_on(void)
{
    gpio_config_t bl_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_PIN_BL, LCD_BL_ON_LEVEL);
}

esp_lcd_panel_io_handle_t st7789_lcd_io(void)
{
    return s_io;
}

esp_lcd_panel_handle_t st7789_lcd_panel(void)
{
    return s_panel;
}

esp_err_t st7789_lcd_init(void)
{
    backlight_on();

    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG,
                        "spi_bus_initialize failed");

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &s_io), TAG,
        "panel_io_spi failed");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel), TAG,
                        "new_panel_st7789 failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, LCD_Y_GAP), TAG, "set_gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY), TAG, "swap_xy failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y), TAG,
                        "mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR), TAG,
                        "invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp_on failed");

    ESP_LOGI(TAG, "ST7789V3 %dx%d ready", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}
