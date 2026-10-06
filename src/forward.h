// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "lwip/ip_addr.h"
void forward_init(const ip_addr_t *ap,const ip_addr_t *hu);
void forward_poll(bool up);
bool forward_manual(void);
unsigned forward_active(void);
unsigned forward_listeners(void);
