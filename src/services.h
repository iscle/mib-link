// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lwip/ip_addr.h"
void services_init(const ip_addr_t *ap_address);
void services_poll(void);
