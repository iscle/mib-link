// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MIB_FORWARD_COUNT 3

struct mib_forward {
    uint16_t local, remote;
};

struct mib_settings {
    char ssid[33], password[64];
    struct mib_forward forwards[MIB_FORWARD_COUNT];
};
extern struct mib_settings settings;
void settings_defaults(struct mib_settings *s);
bool settings_valid(const struct mib_settings *s);
void settings_init(void);
bool settings_save(const struct mib_settings *s);
void settings_poll(void);
