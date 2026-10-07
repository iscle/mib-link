// SPDX-License-Identifier: GPL-3.0-or-later
#include "lwip/init.h"
#include "lwip/ip4.h"
#include "lwip/timeouts.h"
#include "usb_asix.h"
#include "services.h"
#include "settings.h"
#include "dhcpserver.h"
#include <assert.h>
#include <string.h>
struct asix_state adapter;
struct netif usb_netif;
unsigned usb_tx_frames, usb_dropped_frames;
static struct netif ap;
static uint32_t millis;

static struct {
    uint8_t data[1600];
    unsigned n, iface;
} packets[128];

static unsigned tail, head;

const char *mst_board_name(void)
{
    return "Host test";
}

uint32_t mst_now_ms(void)
{
    return millis;
}

uint32_t sys_now(void)
{
    return millis;
}

uint32_t cyw43_hal_ticks_ms(void)
{
    return millis;
}

uint64_t get_absolute_time(void)
{
    return (uint64_t)millis * 1000;
}

bool tud_mounted(void)
{
    return netif_is_link_up(&usb_netif);
}

static err_t output(struct netif *netif, struct pbuf *p, const ip4_addr_t *ip)
{
    assert(head - tail < 128 && p->tot_len <= 1600);
    unsigned slot = head++ % 128;
    packets[slot].iface = netif == &ap ? 0 : 1;
    packets[slot].n = pbuf_copy_partial(p, packets[slot].data, p->tot_len, 0);
    return ERR_OK;
}

static err_t init_netif(struct netif *n)
{
    n->output = output;
    n->mtu = 1500;
    n->flags = NETIF_FLAG_BROADCAST;
    return ERR_OK;
}

void test_init(void)
{
    settings_init();
    lwip_init();
    ip4_addr_t a, m, g;
    IP4_ADDR(&m, 255, 255, 255, 0);
    ip4_addr_set_zero(&g);
    IP4_ADDR(&a, 192, 168, 4, 1);
    netif_add(&ap, &a, &m, &g, NULL, init_netif, ip4_input);
    netif_set_up(&ap);
    netif_set_link_up(&ap);
    IP4_ADDR(&a, 172, 16, 250, 1);
    netif_add(&usb_netif, &a, &m, &g, NULL, init_netif, ip4_input);
    netif_set_up(&usb_netif);
    services_init(netif_ip_addr4(&ap));
    static dhcp_server_t dhcp;
    IP4_ADDR(&a, 192, 168, 4, 1);
    dhcp_server_init(&dhcp, &ap, &a, &m);
}

void test_up(int up)
{
    if (up)
        netif_set_link_up(&usb_netif);
    else
        netif_set_link_down(&usb_netif);
    services_poll();
}

void test_tick(unsigned ms)
{
    millis += ms;
    sys_check_timeouts();
    services_poll();
}

int test_pop(void *buffer, unsigned *iface)
{
    if (tail == head)
        return 0;
    unsigned slot = tail++ % 128;
    memcpy(buffer, packets[slot].data, packets[slot].n);
    *iface = packets[slot].iface;
    return packets[slot].n;
}

void test_inject(unsigned iface, const void *buffer, unsigned n)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, n, PBUF_POOL);
    assert(p);
    assert(pbuf_take(p, buffer, n) == ERR_OK);
    ip4_input(p, iface ? &usb_netif : &ap);
}
