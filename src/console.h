// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "lwip/ip_addr.h"
void console_init(const ip_addr_t *hu);
bool console_action(const char *action, const char *id, const char *data);
size_t console_json(char *out, size_t size, const char *cursor);
bool console_active(void);
void console_poll(bool up);
