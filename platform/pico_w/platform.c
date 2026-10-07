// SPDX-License-Identifier: GPL-3.0-or-later
#include "mst_platform.h"
#include "pico/time.h"
#include "pico/unique_id.h"
#include <string.h>

uint32_t mst_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

const char *mst_board_name(void)
{
    return "Pico W (RP2040)";
}

void mst_usb_identity(uint8_t mac[6], char *serial, size_t size)
{
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    mac[0] = 0x02;
    memcpy(mac + 1, id.id + PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 5, 5);
    pico_get_unique_board_id_string(serial, size);
}
