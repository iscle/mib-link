// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#define MST_SERIAL_SIZE 17
uint32_t mst_now_ms(void);
const char *mst_board_name(void);
void mst_usb_identity(uint8_t mac[6],char *serial,size_t size);

#ifdef ESP_PLATFORM
#define MST_USB_POWER_UNITS 250
#else
#define MST_USB_POWER_UNITS 125
#endif
