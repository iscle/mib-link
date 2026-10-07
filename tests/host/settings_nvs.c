#include "nvs_test_platform.h"
#include "settings.h"
#include <string.h>
#include <stdio.h>
static uint8_t storage[256], pending[256];
static size_t length, pending_length;
static unsigned millis, writes, reboots;
static int open_error, set_error, commit_error, recovery;

uint32_t mib_now_ms(void)
{
    return millis;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{
    assert(!strcmp(name, "mib-link"));
    *h = 1;
    return open_error;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *n)
{
    if (!length || length > *n)
        return -1;
    memcpy(out, storage, length);
    *n = length;
    return 0;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t n)
{
    assert(n <= sizeof(pending));
    if (set_error)
        return set_error;
    memcpy(pending, value, n);
    pending_length = n;
    return 0;
}

esp_err_t nvs_commit(nvs_handle_t h)
{
    if (commit_error)
        return commit_error;
    memcpy(storage, pending, pending_length);
    length = pending_length;
    writes++;
    return 0;
}

void nvs_close(nvs_handle_t h)
{
}

esp_err_t gpio_config(const gpio_config_t *p)
{
    assert(p->pin_bit_mask == (1ULL << 4) && p->pull_up_en);
    return 0;
}

int gpio_get_level(int p)
{
    assert(p == 4);
    return !recovery;
}

void vTaskDelay(unsigned n)
{
    millis += n;
}

void esp_restart(void)
{
    reboots++;
}

int main(void)
{
    settings_init();
    assert(!strcmp(settings.ssid, "MIB-Link"));
    struct mib_settings next = settings;
    strcpy(next.ssid, "S3 network");
    assert(settings_save(&next));
    assert(writes == 1 && !settings_save(&next));
    settings_poll();
    assert(!reboots);
    millis += 2001;
    settings_poll();
    assert(reboots == 1);
    settings_init();
    assert(!strcmp(settings.ssid, "S3 network"));
    strcpy(next.ssid, "Replacement");
    open_error = -1;
    assert(!settings_save(&next));
    open_error = 0;
    set_error = -1;
    assert(!settings_save(&next));
    set_error = 0;
    commit_error = -1;
    assert(!settings_save(&next));
    commit_error = 0;
    settings_init();
    assert(!strcmp(settings.ssid, "S3 network") && writes == 1);
    recovery = 1;
    settings_init();
    assert(!strcmp(settings.ssid, "MIB-Link"));
    recovery = 0;
    settings_init();
    assert(!strcmp(settings.ssid, "S3 network"));
    storage[0] = 2;
    settings_init();
    assert(!strcmp(settings.ssid, "MIB-Link"));
    assert(settings_save(&next));
    length--;
    settings_init();
    assert(!strcmp(settings.ssid, "MIB-Link"));
    strcpy(next.password, "short");
    assert(!settings_save(&next));
    puts("PASS: ESP32-S3 NVS defaults, version/length validation, failed saves, delayed restart, "
         "recovery and persistence");
}
