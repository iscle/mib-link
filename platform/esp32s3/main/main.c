// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"
#include "services.h"
#include "usb_asix.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_private/usb_phy.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/tcpip.h"
#include <string.h>
#include <assert.h>

void app_main(void){
    /* Never erase saved credentials automatically on an NVS version/error. */
    ESP_ERROR_CHECK(nvs_flash_init());settings_init();
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap=esp_netif_create_default_wifi_ap();assert(ap);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap));
    esp_netif_ip_info_t info={0};
    IP4_ADDR(&info.ip,192,168,4,1);IP4_ADDR(&info.netmask,255,255,255,0);
    IP4_ADDR(&info.gw,192,168,4,1);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap,&info));
    uint8_t no_offer=0;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap,ESP_NETIF_OP_SET,ESP_NETIF_ROUTER_SOLICITATION_ADDRESS,&no_offer,sizeof(no_offer)));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&no_offer,sizeof(no_offer)));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi={0};memcpy(wifi.ap.ssid,settings.ssid,strlen(settings.ssid));
    memcpy(wifi.ap.password,settings.password,strlen(settings.password));
    wifi.ap.ssid_len=strlen(settings.ssid);wifi.ap.authmode=WIFI_AUTH_WPA2_PSK;
    wifi.ap.max_connection=4;wifi.ap.channel=1;wifi.ap.pmf_cfg.capable=true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Use the internal USB-OTG PHY, not the USB serial/JTAG peripheral. */
    usb_phy_config_t phy={.controller=USB_PHY_CTRL_OTG,.target=USB_PHY_TARGET_INT,
        .otg_mode=USB_OTG_MODE_DEVICE,.otg_speed=USB_PHY_SPEED_FULL};
    usb_phy_handle_t handle;ESP_ERROR_CHECK(usb_new_phy(&phy,&handle));
    ip_addr_t address;IP_ADDR4(&address,192,168,4,1);
    LOCK_TCPIP_CORE();usb_network_init();services_init(&address);UNLOCK_TCPIP_CORE();
    for(;;){
        /* Both cores remain enabled. lwIP's core mutex serializes every raw
         * API call and TinyUSB callback with the Wi-Fi TCP/IP task. TinyUSB
         * is polled with a zero timeout, never blocking while holding it. */
        LOCK_TCPIP_CORE();usb_network_poll();services_poll();settings_poll();UNLOCK_TCPIP_CORE();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
