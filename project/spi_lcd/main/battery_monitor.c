#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "battery_monitor.h"
#include "dev_sys.h"
#include "lvgl_demo_ui.h"

#define BATTERY_ADC_GPIO              GPIO_NUM_4
#define BATTERY_ADC_ATTENUATION       ADC_ATTEN_DB_12
#define BATTERY_DIVIDER_HIGH_OHMS     56000.0f
#define BATTERY_DIVIDER_LOW_OHMS      150000.0f
#define BATTERY_MIN_VOLTAGE           3.30f
#define BATTERY_MAX_VOLTAGE           4.20f
#define BATTERY_ADC_SAMPLE_COUNT      16
#define BATTERY_SAMPLE_PERIOD_MS      1000
#define BATTERY_LOW_SHUTDOWN_DELAY_S  5

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_adc_calibration;
static adc_channel_t s_adc_channel;
static bool s_started;

static esp_err_t battery_adc_init(void)
{
    adc_unit_t unit = ADC_UNIT_1;
    esp_err_t err = adc_oneshot_io_to_channel(BATTERY_ADC_GPIO, &unit, &s_adc_channel);
    if (err != ESP_OK) {
        return err;
    }
    if (unit != ADC_UNIT_1) {
        return ESP_ERR_INVALID_ARG;
    }

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    err = adc_oneshot_new_unit(&unit_config, &s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &channel_config);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return err;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .chan = s_adc_channel,
        .atten = BATTERY_ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&calibration_config, &s_adc_calibration);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return err;
    }
#else
    adc_oneshot_del_unit(s_adc_handle);
    s_adc_handle = NULL;
    return ESP_ERR_NOT_SUPPORTED;
#endif

    ESP_LOGI(TAG, "ADC GPIO%d channel=%d, divider=%.1fk/%.1fk",
             BATTERY_ADC_GPIO, s_adc_channel,
             BATTERY_DIVIDER_HIGH_OHMS / 1000.0f,
             BATTERY_DIVIDER_LOW_OHMS / 1000.0f);
    return ESP_OK;
}

static esp_err_t battery_read_voltage(float *voltage, int *adc_pin_mv, int *average_raw)
{
    int total_adc_mv = 0;
    int total_raw = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLE_COUNT; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
        if (err != ESP_OK) {
            return err;
        }

        int adc_mv = 0;
        err = adc_cali_raw_to_voltage(s_adc_calibration, raw, &adc_mv);
        if (err != ESP_OK) {
            return err;
        }
        total_adc_mv += adc_mv;
        total_raw += raw;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    const float average_adc_mv = (float)total_adc_mv / BATTERY_ADC_SAMPLE_COUNT;
    const float divider_ratio =
        (BATTERY_DIVIDER_HIGH_OHMS + BATTERY_DIVIDER_LOW_OHMS) /
        BATTERY_DIVIDER_LOW_OHMS;
    *voltage = average_adc_mv * divider_ratio / 1000.0f;
    *adc_pin_mv = (int)(average_adc_mv + 0.5f);
    *average_raw = total_raw / BATTERY_ADC_SAMPLE_COUNT;
    return ESP_OK;
}

static int battery_voltage_to_percentage(float voltage)
{
    static const struct {
        float voltage;
        int percentage;
    } discharge_curve[] = {
        {3.30f, 0},
        {3.45f, 5},
        {3.55f, 10},
        {3.60f, 15},
        {3.65f, 20},
        {3.70f, 30},
        {3.75f, 40},
        {3.80f, 50},
        {3.85f, 60},
        {3.90f, 70},
        {3.95f, 80},
        {4.00f, 88},
        {4.10f, 95},
        {4.20f, 100},
    };
    const size_t point_count = sizeof(discharge_curve) / sizeof(discharge_curve[0]);

    if (voltage <= BATTERY_MIN_VOLTAGE) {
        return 0;
    }
    if (voltage >= BATTERY_MAX_VOLTAGE) {
        return 100;
    }

    for (size_t i = 1; i < point_count; i++) {
        if (voltage <= discharge_curve[i].voltage) {
            const float segment =
                (voltage - discharge_curve[i - 1].voltage) /
                (discharge_curve[i].voltage - discharge_curve[i - 1].voltage);
            const float percentage =
                (float)discharge_curve[i - 1].percentage +
                segment * (float)(discharge_curve[i].percentage -
                                  discharge_curve[i - 1].percentage);
            return (int)(percentage + 0.5f);
        }
    }

    return 100;
}

static void battery_monitor_task(void *arg)
{
    (void)arg;
    bool shutdown_pending = false;
    int shutdown_countdown = 0;
    uint32_t log_counter = 0;

    while (true) {
        float voltage = 0.0f;
        int adc_pin_mv = 0;
        int average_raw = 0;
        esp_err_t err = battery_read_voltage(&voltage, &adc_pin_mv, &average_raw);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Battery ADC read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_PERIOD_MS));
            continue;
        }

        const int percentage = battery_voltage_to_percentage(voltage);
        if (percentage < 1) {
            ESP_LOGW(TAG, "Battery critically low: %.3fV (%d%%), powering off now",
                     (double)voltage, percentage);
            thermal_ui_update_battery(percentage, true);
            sys_power_off();
            vTaskDelete(NULL);
            return;
        }

        if (percentage < 5 && !shutdown_pending) {
            shutdown_pending = true;
            shutdown_countdown = BATTERY_LOW_SHUTDOWN_DELAY_S;
            ESP_LOGW(TAG, "Battery low: %.3fV (%d%%), powering off in %d seconds",
                     (double)voltage, percentage, shutdown_countdown);
        } else if (percentage >= 8 && shutdown_pending) {
            shutdown_pending = false;
            shutdown_countdown = 0;
            ESP_LOGI(TAG, "Battery voltage recovered: %.3fV (%d%%)",
                     (double)voltage, percentage);
        }

        thermal_ui_update_battery(percentage, shutdown_pending);

        if ((log_counter++ % 10U) == 0U) {
            ESP_LOGI(TAG, "Battery: %.3fV (%d%%), ADC pin=%dmV raw=%d",
                     (double)voltage, percentage, adc_pin_mv, average_raw);
        }

        if (shutdown_pending) {
            if (shutdown_countdown == 0) {
                ESP_LOGW(TAG, "Low-battery shutdown timer expired");
                sys_power_off();
                vTaskDelete(NULL);
                return;
            }
            shutdown_countdown--;
        }

        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_PERIOD_MS));
    }
}

esp_err_t battery_monitor_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = battery_adc_init();
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreatePinnedToCore(battery_monitor_task, "battery_monitor", 3072,
                                NULL, 3, NULL, 0) != pdPASS) {
        adc_cali_delete_scheme_curve_fitting(s_adc_calibration);
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_calibration = NULL;
        s_adc_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}
