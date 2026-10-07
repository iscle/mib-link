// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"
#include <string.h>
#include <stddef.h>
#ifndef MST_HOST_TEST
#ifdef MST_SETTINGS_TEST
#include "settings_test_platform.h"
#else
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#endif
#endif
#ifndef MST_HOST_TEST
/* Two alternating erase sectors survive a power loss while saving. The linker
 * reserves the final 8 KiB; no code, credentials or payload assets occupy it. */
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)

struct record {
    uint32_t magic, version, sequence;
    struct mst_settings value;
    uint32_t crc;
};

_Static_assert(sizeof(struct record) <= FLASH_PAGE_SIZE, "settings fit one flash page");
static unsigned active_slot = 1;
static uint32_t sequence, reboot_at;

static uint32_t crc32(const void *v, size_t n)
{
    const uint8_t *p = v;
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0xedb88320u);
    }
    return ~crc;
}

static bool record_valid(const struct record *r)
{
    return r->magic == 0x4d53544c && r->version == 1 &&
           crc32(r, offsetof(struct record, crc)) == r->crc && settings_valid(&r->value);
}

void settings_init(void)
{
    settings_defaults(&settings);
    sequence = 0;
    active_slot = 1;
    reboot_at = 0;
    bool loaded = false;
    for (unsigned i = 0; i < 2; i++) {
        const struct record *r = (const void *)(XIP_BASE + SETTINGS_OFFSET + i * FLASH_SECTOR_SIZE);
        if (record_valid(r) && (!loaded || (int32_t)(r->sequence - sequence) > 0)) {
            settings = r->value;
            sequence = r->sequence;
            active_slot = i;
            loaded = true;
        }
    }
    /* Optional physical recovery: hold GP15 to GND at power-on. No flash write;
     * connect using defaults, then Save to replace inaccessible settings. */
    gpio_init(15);
    gpio_set_dir(15, GPIO_IN);
    gpio_pull_up(15);
    sleep_ms(2);
    if (!gpio_get(15))
        settings_defaults(&settings);
}

bool settings_save(const struct mst_settings *s)
{
    if (!settings_valid(s) || reboot_at)
        return false;

    union {
        uint8_t bytes[FLASH_PAGE_SIZE];
        struct record r;
    } page;

    memset(&page, 0xff, sizeof(page));
    memset(&page.r, 0, sizeof(page.r));
    page.r.magic = 0x4d53544c;
    page.r.version = 1;
    page.r.sequence = sequence + 1;
    page.r.value = *s;
    page.r.crc = crc32(&page.r, offsetof(struct record, crc));
    unsigned slot = active_slot ^ 1u, offset = SETTINGS_OFFSET + slot * FLASH_SECTOR_SIZE;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, page.bytes, sizeof(page));
    restore_interrupts(irq);
    if (memcmp((const void *)(XIP_BASE + offset), page.bytes, sizeof(page)))
        return false;
    reboot_at = to_ms_since_boot(get_absolute_time()) + 2000;
    return true;
}

void settings_poll(void)
{
    if (reboot_at && (int32_t)(to_ms_since_boot(get_absolute_time()) - reboot_at) >= 0)
        watchdog_reboot(0, 0, 0);
}
#endif
