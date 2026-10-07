// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"
#include "mst_platform.h"
#ifdef MST_NVS_TEST
#include "nvs_test_platform.h"
#else
#include "nvs.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include <string.h>

/* NVS supplies CRC checking and power-loss recovery. All related settings live
 * in one versioned blob, so a torn save cannot mix an SSID with another key. */
struct record {
    uint32_t version;
    struct mst_settings value;
};

static uint32_t reboot_at;
static bool reboot_pending;
_Static_assert(CONFIG_MST_RECOVERY_GPIO != 0 && CONFIG_MST_RECOVERY_GPIO != 19 &&
                   CONFIG_MST_RECOVERY_GPIO != 20,
               "Recovery pin must not be a boot strap or USB pin");

void settings_init(void)
{
    settings_defaults(&settings);
    reboot_pending = false;
    nvs_handle_t handle;
    if (nvs_open("mst-link", NVS_READONLY, &handle) == ESP_OK) {
        struct record r;
        size_t size = sizeof(r);
        if (nvs_get_blob(handle, "settings", &r, &size) == ESP_OK && size == sizeof(r) &&
            r.version == 1 && settings_valid(&r.value))
            settings = r.value;
        nvs_close(handle);
    }
#if CONFIG_MST_RECOVERY_GPIO >= 0
    gpio_config_t pin = {.pin_bit_mask = 1ULL << CONFIG_MST_RECOVERY_GPIO,
                         .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&pin));
    vTaskDelay(pdMS_TO_TICKS(2));
    if (!gpio_get_level(CONFIG_MST_RECOVERY_GPIO))
        settings_defaults(&settings);
#endif
}

bool settings_save(const struct mst_settings *s)
{
    if (!settings_valid(s) || reboot_pending)
        return false;
    struct record r = {.version = 1, .value = *s};
    nvs_handle_t handle;
    if (nvs_open("mst-link", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    esp_err_t result = nvs_set_blob(handle, "settings", &r, sizeof(r));
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    memset(&r, 0, sizeof(r));
    if (result != ESP_OK)
        return false;
    reboot_at = mst_now_ms() + 2000;
    reboot_pending = true;
    return true;
}

void settings_poll(void)
{
    if (reboot_pending && (int32_t)(mst_now_ms() - reboot_at) >= 0)
        esp_restart();
}
