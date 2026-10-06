// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "asix_protocol.h"
#include "lwip/netif.h"
extern struct asix_state adapter;
extern struct netif usb_netif;
extern unsigned usb_tx_frames, usb_dropped_frames;
void usb_network_init(void);
void usb_network_poll(void);
