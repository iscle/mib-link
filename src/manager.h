/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Head-unit login is held in RAM only. */
bool manager_request(const char *action, const char *user, const char *password, const char *slot,
                     const char *digest);
size_t manager_json(char *out, size_t size);

#include "lwip/ip_addr.h"
void manager_init(const ip_addr_t *hu);
void manager_poll(bool usb_up, bool manual_session);
bool manager_busy(void);
const char *manager_status(void);
