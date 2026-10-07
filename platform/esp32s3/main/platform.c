// SPDX-License-Identifier: GPL-3.0-or-later
#include "mst_platform.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>
uint32_t mst_now_ms(void){return (uint32_t)(esp_timer_get_time()/1000);}
const char *mst_board_name(void){return "ESP32-S3";}
void mst_usb_identity(uint8_t mac[6],char *serial,size_t size){
    uint8_t id[6];ESP_ERROR_CHECK(esp_efuse_mac_get_default(id));
    memcpy(mac,id,6);mac[0]=(mac[0]&0xfc)|0x02;
    snprintf(serial,size,"%02X%02X%02X%02X%02X%02X",id[0],id[1],id[2],id[3],id[4],id[5]);
}
