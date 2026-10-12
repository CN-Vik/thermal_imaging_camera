file(GLOB srcs "*.c" "bsp/*.c" "mlx90640/*.c")

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "."
        "bsp"
        "mlx90640"
    REQUIRES 
        freertos
        esp_driver_i2c
        esp_lcd   
        driver
        log
        lvgl
        esp_lcd_touch
        esp_lcd_touch_cst816s
)