#pragma once
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define CONFIG_MIB_RECOVERY_GPIO 4
#define ESP_OK 0
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_ENABLE 1
#define pdMS_TO_TICKS(n) (n)
typedef int esp_err_t;
typedef unsigned nvs_handle_t;

typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en;
} gpio_config_t;

esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
esp_err_t gpio_config(const gpio_config_t *);
int gpio_get_level(int);
void vTaskDelay(unsigned);
void esp_restart(void);
