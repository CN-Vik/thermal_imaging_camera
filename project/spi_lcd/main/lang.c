/**
 * @file lang.c
 * @brief 语言设置持久化（NVS Flash）
 */
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lang.h"

#define LANG_NVS_NAMESPACE "thermal"
#define LANG_NVS_KEY       "lang"

static const char *TAG = "lang";

static app_lang_t s_lang = LANG_EN;
static bool s_configured = false;

esp_err_t lang_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init failed (%s), use default language", esp_err_to_name(err));
        s_lang = LANG_EN;
        s_configured = false;
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(LANG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        s_configured = false;
        return err;
    }

    int32_t val = LANG_EN;
    err = nvs_get_i32(handle, LANG_NVS_KEY, &val);
    nvs_close(handle);

    if (err == ESP_OK) {
        s_lang = (app_lang_t)val;
        if (s_lang >= LANG_COUNT) {
            s_lang = LANG_EN;
        }
        s_configured = true;
        ESP_LOGI(TAG, "loaded language: %s", lang_name(s_lang));
    } else {
        s_lang = LANG_EN;
        s_configured = false;
        ESP_LOGI(TAG, "no saved language, default EN");
    }
    return err;
}

app_lang_t lang_get(void)
{
    return s_lang;
}

void lang_set(app_lang_t l)
{
    if (l >= LANG_COUNT) {
        l = LANG_EN;
    }
    s_lang = l;
    s_configured = true;

    nvs_handle_t handle;
    if (nvs_open(LANG_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i32(handle, LANG_NVS_KEY, (int32_t)l);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "set language: %s", lang_name(l));
}

bool lang_configured(void)
{
    return s_configured;
}
