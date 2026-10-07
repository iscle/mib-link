// SPDX-License-Identifier: GPL-3.0-or-later
#include "usb_asix.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "mib_platform.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include <string.h>

// Deliberately match the archive's service-adapter allowlist. This is a local
// compatibility device, not an assigned USB identity or a D-Link product.
#define EP_IN 0x81
#define EP_OUT 0x02
#define EP_STATUS 0x83
#define QUEUE_COUNT 4
struct asix_state adapter;
struct netif usb_netif;
unsigned usb_tx_frames, usb_dropped_frames;
static uint8_t initial_mac[6];
static bool endpoints_open;
static uint8_t out_packet[64] __attribute__((aligned(4)));
static uint8_t ctrl_data[64] __attribute__((aligned(4)));
static uint8_t interrupt_data[8] __attribute__((aligned(4))) = {0, 0, 1, 0, 0, 0, 0, 0};

static struct {
    uint8_t data[ASIX_FRAME_MAX + 10];
    uint16_t length;
} tx[QUEUE_COUNT];

static unsigned tx_head, tx_count;
static bool tx_pending;
static uint32_t last_status;
static char serial[MIB_SERIAL_SIZE];

static const tusb_desc_device_t device_descriptor = {.bLength = sizeof(tusb_desc_device_t),
                                                     .bDescriptorType = TUSB_DESC_DEVICE,
                                                     .bcdUSB = 0x0200,
                                                     .bDeviceClass = 0,
                                                     .bDeviceSubClass = 0,
                                                     .bDeviceProtocol = 0,
                                                     .bMaxPacketSize0 = 64,
                                                     .idVendor = 0x2001,
                                                     .idProduct = 0x3c05,
                                                     .bcdDevice = 0x0001,
                                                     .iManufacturer = 1,
                                                     .iProduct = 2,
                                                     .iSerialNumber = 3,
                                                     .bNumConfigurations = 1};
static const uint8_t configuration[] = {9,
                                        TUSB_DESC_CONFIGURATION,
                                        39,
                                        0,
                                        1,
                                        1,
                                        0,
                                        0x80,
                                        MIB_USB_POWER_UNITS,
                                        9,
                                        TUSB_DESC_INTERFACE,
                                        0,
                                        0,
                                        3,
                                        0xff,
                                        0xff,
                                        0,
                                        0,
                                        7,
                                        TUSB_DESC_ENDPOINT,
                                        EP_IN,
                                        2,
                                        64,
                                        0,
                                        0,
                                        7,
                                        TUSB_DESC_ENDPOINT,
                                        EP_OUT,
                                        2,
                                        64,
                                        0,
                                        0,
                                        7,
                                        TUSB_DESC_ENDPOINT,
                                        EP_STATUS,
                                        3,
                                        8,
                                        0,
                                        10};
_Static_assert(sizeof(configuration) == 39, "USB descriptor length");

uint8_t const *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&device_descriptor;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    return index == 0 ? configuration : NULL;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t buffer[64];
    const char *value;
    if (index == 0) {
        buffer[0] = 0x0304;
        buffer[1] = 0x0409;
        return buffer;
    }
    switch (index) {
    case 1:
        value = "MIB-Link";
        break;
    case 2:
        value = "MIB-Link USB Ethernet";
        break;
    case 3:
        value = serial;
        break;
    default:
        return NULL;
    }
    size_t n = strlen(value);
    if (n > 63)
        n = 63;
    buffer[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
    for (size_t i = 0; i < n; i++)
        buffer[i + 1] = (unsigned char)value[i];
    return buffer;
}

static void receive_frame(void *ctx, const uint8_t *frame, unsigned n)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, n, PBUF_POOL);
    if (!p) {
        usb_dropped_frames++;
        return;
    }
    if (pbuf_take(p, frame, n) != ERR_OK || usb_netif.input(p, &usb_netif) != ERR_OK) {
        pbuf_free(p);
        usb_dropped_frames++;
    }
}

static void start_tx(void)
{
    if (!endpoints_open || tx_pending || !tx_count || !(adapter.rx_control & 0x80))
        return;
    tx_pending = usbd_edpt_xfer(0, EP_IN, tx[tx_head].data, tx[tx_head].length);
}

static err_t link_output(struct netif *netif, struct pbuf *p)
{
    if (!endpoints_open || !tud_mounted() || !(adapter.rx_control & 0x80))
        return ERR_IF;
    if (tx_count == QUEUE_COUNT)
        return ERR_MEM;
    unsigned slot = (tx_head + tx_count) % QUEUE_COUNT;
    static uint8_t frame[ASIX_FRAME_MAX];
    if (p->tot_len > sizeof(frame))
        return ERR_BUF;
    pbuf_copy_partial(p, frame, p->tot_len, 0);
    size_t n = asix_encode(tx[slot].data, sizeof(tx[slot].data), frame, p->tot_len);
    if (!n)
        return ERR_BUF;
    tx[slot].length = n;
    tx_count++;
    start_tx();
    return ERR_OK;
}

static err_t netif_init_cb(struct netif *netif)
{
    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->hostname = "mib-link";
    netif->output = etharp_output;
    netif->linkoutput = link_output;
    netif->mtu = 1500;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, initial_mac, 6);
    netif->hwaddr[5] ^= 1;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    return ERR_OK;
}

static void driver_init(void)
{
}

static void driver_reset(uint8_t rhport)
{
    endpoints_open = false;
    tx_count = tx_head = 0;
    tx_pending = false;
    asix_init(&adapter, initial_mac);
    netif_set_link_down(&usb_netif);
}

static uint16_t driver_open(uint8_t rhport, const tusb_desc_interface_t *desc, uint16_t length)
{
    if (desc->bInterfaceClass != 0xff || desc->bInterfaceNumber != 0 || desc->bNumEndpoints != 3 ||
        length < 30)
        return 0;
    const uint8_t *p = (const uint8_t *)desc + 9;
    for (unsigned i = 0; i < 3; i++, p += 7) {
        if (p[0] != 7 || p[1] != TUSB_DESC_ENDPOINT ||
            !usbd_edpt_open(rhport, (const tusb_desc_endpoint_t *)p))
            return 0;
    }
    endpoints_open = true;
    if (!usbd_edpt_xfer(rhport, EP_OUT, out_packet, sizeof(out_packet)))
        return 0;
    return 30;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r)
{
    bool in = (r->bmRequestType & 0x80) != 0;
    if ((r->bmRequestType & 0x7f) != 0x40 || r->wLength > sizeof(ctrl_data))
        return false;
    if (stage == CONTROL_STAGE_SETUP) {
        if (in || !r->wLength) {
            int n = asix_control(&adapter, in, r->bRequest, r->wValue, r->wIndex, ctrl_data,
                                 r->wLength);
            if (n < 0)
                return false;
            return r->wLength ? tud_control_xfer(rhport, r, ctrl_data, n)
                              : tud_control_status(rhport, r);
        }
        // Validate OUT shape before accepting its data, without mutating state.
        static struct asix_state check;
        check = adapter;
        memset(ctrl_data, 0, sizeof(ctrl_data));
        if (asix_control(&check, false, r->bRequest, r->wValue, r->wIndex, ctrl_data, r->wLength) <
            0) {
            adapter.rejected_controls++;
            adapter.last_rejected = r->bRequest;
            return false;
        }
        return tud_control_xfer(rhport, r, ctrl_data, r->wLength);
    }
    if (stage == CONTROL_STAGE_DATA && !in && r->wLength)
        return asix_control(&adapter, false, r->bRequest, r->wValue, r->wIndex, ctrl_data,
                            r->wLength) >= 0;
    return true;
}

static bool class_control(uint8_t rhport, uint8_t stage, const tusb_control_request_t *r)
{
    return false;
}

static bool driver_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t count)
{
    if (ep == EP_OUT) {
        if (result == XFER_RESULT_SUCCESS)
            asix_receive(&adapter, out_packet, count, count < 64, receive_frame, NULL);
        else {
            adapter.header_used = adapter.frame_used = adapter.frame_size = adapter.skip_pad = 0;
            usb_dropped_frames++;
        }
        return usbd_edpt_xfer(rhport, EP_OUT, out_packet, sizeof(out_packet));
    }
    if (ep == EP_IN) {
        if (tx_count) {
            tx_head = (tx_head + 1) % QUEUE_COUNT;
            tx_count--;
        }
        if (result == XFER_RESULT_SUCCESS)
            usb_tx_frames++;
        else
            usb_dropped_frames++;
        tx_pending = false;
        start_tx();
    }
    return true;
}

static const usbd_class_driver_t driver = {.name = "ASIX compatibility",
                                           .init = driver_init,
                                           .reset = driver_reset,
                                           .open = driver_open,
                                           .control_xfer_cb = class_control,
                                           .xfer_cb = driver_xfer};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count)
{
    *count = 1;
    return &driver;
}

void usb_network_init(void)
{
    mib_usb_identity(initial_mac, serial, sizeof(serial));
    asix_init(&adapter, initial_mac);
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip, 172, 16, 250, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    ip4_addr_set_zero(&gw);
    netif_add(&usb_netif, &ip, &mask, &gw, NULL, netif_init_cb, ethernet_input);
    netif_set_up(&usb_netif);
    tusb_rhport_init_t config = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &config);
}

void usb_network_poll(void)
{
    tud_task_ext(0, false);
    bool up = endpoints_open && tud_mounted() && !tud_suspended() && (adapter.rx_control & 0x80);
    if (up != !!netif_is_link_up(&usb_netif)) {
        if (up)
            netif_set_link_up(&usb_netif);
        else
            netif_set_link_down(&usb_netif);
    }
    uint32_t now = mib_now_ms();
    if (endpoints_open && tud_mounted() && now - last_status >= 250 &&
        !usbd_edpt_busy(0, EP_STATUS)) {
        last_status = now;
        usbd_edpt_xfer(0, EP_STATUS, interrupt_data, sizeof(interrupt_data));
    }
    start_tx();
}
