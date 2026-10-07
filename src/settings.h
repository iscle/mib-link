// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MST_FORWARD_COUNT 3

struct mst_forward {
    uint16_t local, remote;
};

struct mst_settings {
    char ssid[33], password[64];
    struct mst_forward forwards[MST_FORWARD_COUNT];
};
extern struct mst_settings settings;
void settings_defaults(struct mst_settings *s);
bool settings_valid(const struct mst_settings *s);
void settings_init(void);
bool settings_save(const struct mst_settings *s);
void settings_poll(void);
