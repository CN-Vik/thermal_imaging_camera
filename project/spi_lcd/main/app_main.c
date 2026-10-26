/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ir_mlx90640_app.h"
#include "dev_sys.h"
#include "lang.h"
#include "boot_ui.h"
#include "photo.h"
#include "esp_lcd_touch_cst816s.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/gpio.h"

// #if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
// #include "esp_lcd_touch_stmpe610.h"
// #endif

static const char *TAG = "thermal imaging_eg";

#define CONFIG_EXAMPLE_LCD_TOUCH_ENABLED    1

#define PIN_PWR_HOLD    GPIO_NUM_36  // 电源自锁输出
#define PIN_KEY_DET     GPIO_NUM_37  // 按键检测输入

// Using SPI2 in the example
#define LCD_HOST SPI2_HOST

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL

// 屏幕 SPI 引脚
#define EXAMPLE_PIN_NUM_SCLK 13        // 原 12 -> 改为 13 (对应原理图 SCL)
#define EXAMPLE_PIN_NUM_MOSI 12        // 原 11 -> 改为 12 (对应原理图 SDA)
#define EXAMPLE_PIN_NUM_MISO -1        // 无需 MISO
#define EXAMPLE_PIN_NUM_LCD_DC 17      // 原 10 -> 改为 17 (对应原理图 D/C#)
#define EXAMPLE_PIN_NUM_LCD_RST 9      // 保持 9 (对应原理图 RESET)
#define EXAMPLE_PIN_NUM_LCD_CS 14      // 原 8  -> 改为 14 (对应原理图 CS)

// 背光引脚（原理图 Pin 17 为 LEDK 阴极，通常外接 MOS 管控制）
#define EXAMPLE_PIN_NUM_BK_LIGHT 18    // 若你的背光控制 GPIO 未变，保持 18

// 触摸屏 I2C 引脚（若后续启用触摸）
#define EXAMPLE_PIN_NUM_TOUCH_SCL 11   // 对应原理图 TP_SCL
#define EXAMPLE_PIN_NUM_TOUCH_SDA 10   // 对应原理图 TP_SDA
#define EXAMPLE_PIN_NUM_TOUCH_RST 9    // 对应原理图 TP-RST (与屏幕共用 IO9)
#define EXAMPLE_PIN_NUM_TOUCH_INT 8    // 对应原理图 TP_INT

#define EXAMPLE_LCD_H_RES 240
#define EXAMPLE_LCD_V_RES 280
#define LCD_BUF_LINES 45 // 增大单次切片行数

// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8

#define EXAMPLE_LVGL_TICK_PERIOD_MS 2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE (8 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY 2

static SemaphoreHandle_t lvgl_mux = NULL;

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED

esp_lcd_touch_handle_t tp = NULL;
#define TOUCH_I2C_PORT    I2C_NUM_1



static esp_err_t app_touch_init(void)
{
    // 1. 初始化独立的 I2C_NUM_1 总线 (用于触摸 CST816)
    i2c_master_bus_config_t bus_config = {
        .i2c_port = TOUCH_I2C_PORT,              // 独立使用 I2C_NUM_1
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA, // IO10
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL, // IO11
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t tp_i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &tp_i2c_bus));

    // 2. 创建 Touch Panel IO (传入新驱动的 bus 句柄)
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(tp_i2c_bus, &tp_io_config, &tp_io_handle));

    // 3. 配置 CST816 驱动
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,            // 240
        .y_max = EXAMPLE_LCD_V_RES,            // 280
        .rst_gpio_num = -1,                    // 共用屏幕复位 IO9，填 -1 防止冲刷屏幕
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT, // IO8
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

    ESP_LOGI(TAG, "Initialize CST816 touch driver on I2C_NUM_1");
    return esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp);
}


#endif

extern TaskHandle_t ir_task_Handle;


static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
static void example_lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;

    switch (drv->rotated)
    {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        if (tp)
        {
            // Rotate LCD touch
            esp_lcd_touch_set_mirror_y(tp, false);
            esp_lcd_touch_set_mirror_x(tp, false);
        }
        
#endif
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
        // Rotate LCD touch
        esp_lcd_touch_set_mirror_y(tp, false);
        esp_lcd_touch_set_mirror_x(tp, false);
#endif
        break;
    }
}

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    /* Read touch controller data */
    /* 读取 CST816 触摸数据 */
    esp_lcd_touch_read_data(drv->user_data);

    /* Get coordinates */
    /* 获取触控坐标 */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0)
    {
        data->point.x = touchpad_x[0];
        // 扣除屏幕控制器的 20px GAP 偏移（根据实际点按校准）
        int16_t y = touchpad_y[0] - 20;
        data->point.y = (y < 0) ? 0 : y;
        // data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    
        // 临时测试打印
        ESP_LOGI(TAG, "Touch_raw:[%d, %d]->mapped:[%d, %d]", 
             touchpad_x[0], touchpad_y[0], data->point.x, data->point.y);
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

bool example_lvgl_lock(int timeout_ms)
{
    // Convert timeout in milliseconds to FreeRTOS ticks
    // If `timeout_ms` is set to -1, the program will block until the condition is met
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void example_lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mux);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task,LVGL_Version:%d.%d.%d\n",
             LVGL_VERSION_MAJOR,
             LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH);

    ESP_LOGI(TAG, "example_lvgl_port_task on core %d", xPortGetCoreID());

    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;

    while (1)
    {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (example_lvgl_lock(-1))
        {
            task_delay_ms = lv_timer_handler();
            // Release the mutex
            example_lvgl_unlock();
        }

        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        }
        else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}



void app_main(void)
{
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions

    power_on();

    ESP_LOGI(TAG, "Turn_off_LCD_backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_SCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
        .miso_io_num = EXAMPLE_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = example_notify_lvgl_flush_ready,
        .user_ctx = &disp_drv,
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ESP_LOGI(TAG, "Install ST7789V2 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_ERROR_CHECK(
        esp_lcd_panel_invert_color(
            panel_handle,
            true
        )
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_set_gap(
            panel_handle,
            0,
            20
        )
    );

    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));

    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // #if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    //     esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    //     esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_STMPE610_CONFIG(EXAMPLE_PIN_NUM_TOUCH_CS);
    //     // Attach the TOUCH to the SPI bus
    //     ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &tp_io_config, &tp_io_handle));

    //     esp_lcd_touch_config_t tp_cfg = {
    //         .x_max = EXAMPLE_LCD_H_RES,
    //         .y_max = EXAMPLE_LCD_V_RES,
    //         .rst_gpio_num = -1,
    //         .int_gpio_num = -1,
    //         .flags = {
    //             .swap_xy = 0,
    //             .mirror_x = 0,
    //             .mirror_y = 0,
    //         },
    //     };

    // #if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
    //     ESP_LOGI(TAG, "Initialize touch controller STMPE610");
    //     ESP_ERROR_CHECK(esp_lcd_touch_new_spi_stmpe610(tp_io_handle, &tp_cfg, &tp));
    // #endif // CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
    // #endif // CONFIG_EXAMPLE_LCD_TOUCH_ENABLED

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = heap_caps_malloc(EXAMPLE_LCD_H_RES * LCD_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(EXAMPLE_LCD_H_RES * LCD_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * LCD_BUF_LINES);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.drv_update_cb = example_lvgl_port_update_callback;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    // 注册显示设备
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    // --- 初始化触控并接入 LVGL ---
    ESP_ERROR_CHECK(app_touch_init());

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    indev_drv.long_press_time = 600;// 长按 600ms 触发菜单（默认 400ms）
    indev_drv.user_data = tp;
    lv_indev_drv_register(&indev_drv);
    // ----------------------------

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

// #if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
//     // static lv_indev_drv_t indev_drv; // Input device driver (Touch)
//     lv_indev_drv_init(&indev_drv);
//     indev_drv.type = LV_INDEV_TYPE_POINTER;
//     indev_drv.disp = disp;
//     indev_drv.read_cb = example_lvgl_touch_cb;
//     indev_drv.user_data = tp;

//     lv_indev_drv_register(&indev_drv);
// #endif

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);
    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreatePinnedToCore(
        example_lvgl_port_task,
        "LVGL",
        EXAMPLE_LVGL_TASK_STACK_SIZE,
        NULL,
        EXAMPLE_LVGL_TASK_PRIORITY,
        NULL,
        0
    );

    /* ============ 开机 Logo + 首次开机语言选择 ============ */
    lang_init();
    photo_init();
    boot_show_splash(disp);
    if (!lang_configured()) {
        boot_show_language_select(disp);
    }

    ESP_LOGI(TAG, "Init thermal buffer");
    ESP_ERROR_CHECK(ir_camera_app_init()); // 务必在创建 UI 前分配显存！

    ESP_LOGI(TAG, "Display LVGL Meter Widget");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (example_lvgl_lock(-1))
    {
        // example_lvgl_demo_ui(disp);
        lvgl_dispaly_ir_mlx90640(disp);
        // Release the mutex
        example_lvgl_unlock();
    }

    /*创建热成像图像数据采集任务*/
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        ir_camera_task_main,   /* Function that implements the task. */
        "ir_camera_task_main", /* Text name for the task. */
        (6 * 1024),            /* Stack size in words, not bytes. */
        NULL,                  /* Parameter passed into the task. */
        5,                     /* Priority at which the task is created. */
        &ir_task_Handle,
        1
    ); /* Used to pass out the created task's handle. */

    if (xReturned != pdPASS)
    {
        ESP_LOGE(TAG, "ir_camera_task_main creat failed!");
    }
}
