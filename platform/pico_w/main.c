// SPDX-License-Identifier: GPL-3.0-or-later
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "dhcpserver.h"
#include "usb_asix.h"
#include "services.h"
#include "settings.h"

int main(void)
{
    settings_init();
    if (cyw43_arch_init())
        return 1;
    cyw43_arch_enable_ap_mode(settings.ssid, settings.password, CYW43_AUTH_WPA2_AES_PSK);
    ip_addr_t ip, mask;
    IP_ADDR4(&ip, 192, 168, 4, 1);
    IP_ADDR4(&mask, 255, 255, 255, 0);
    struct netif *ap = &cyw43_state.netif[CYW43_ITF_AP];
    netif_set_addr(ap, ip_2_ip4(&ip), ip_2_ip4(&mask), ip_2_ip4(&ip));
    static dhcp_server_t dhcp;
    dhcp_server_init(&dhcp, ap, &ip, &mask);
    usb_network_init();
    services_init(&ip);
    uint32_t last_led = 0;
    bool led = false;
    while (true) {
        cyw43_arch_poll();
        usb_network_poll();
        services_poll();
        settings_poll();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_led >= (netif_is_link_up(&usb_netif) ? 1000u : 250u)) {
            last_led = now;
            led = !led;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
        }
        sleep_ms(1);
    }
}
