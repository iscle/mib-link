// SPDX-License-Identifier: GPL-3.0-or-later
#include "services.h"
#include "settings.h"
#include "forward.h"
#include "console.h"
#include <stdlib.h>
#include "manager.h"
#include "payload.h"
#include "usb_asix.h"
#include "lwip/tcp.h"
#include "mib_platform.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>
#include "manager.h"
#include <strings.h>

static uint32_t now_ms(void)
{
    return mib_now_ms();
}

static ip_addr_t hu_address;
static struct tcp_pcb *probe;
static uint32_t probe_start;
static unsigned probe_attempts;
static const char *probe_status = "waiting for USB network";
static bool was_up;
static unsigned banner_bytes;
// Hex preserves Telnet negotiation without putting untrusted HTML in the page.
static char banner_hex[2 * 128 + 1];

static void probe_close(void)
{
    if (probe) {
        struct tcp_pcb *p = probe;
        probe = NULL;
        tcp_arg(p, NULL);
        tcp_err(p, NULL);
        tcp_recv(p, NULL);
        tcp_abort(p);
    }
}

static void probe_error(void *arg, err_t error)
{
    probe = NULL;
    probe_status = "connection failed or closed";
}

static err_t probe_recv(void *arg, struct tcp_pcb *p, struct pbuf *b, err_t error)
{
    if (!b) {
        probe_status = "peer closed the connection";
        probe_close();
        return ERR_ABRT;
    }
    uint8_t data[128];
    unsigned n = pbuf_copy_partial(b, data, sizeof(data), 0);
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < n; i++) {
        banner_hex[2 * i] = hex[data[i] >> 4];
        banner_hex[2 * i + 1] = hex[data[i] & 15];
    }
    banner_hex[n * 2] = 0;
    banner_bytes = n;
    tcp_recved(p, b->tot_len);
    pbuf_free(b);
    probe_status = "TCP 23 responded; authentication not tested";
    probe_close();
    return ERR_ABRT;
}

static err_t probe_connected(void *arg, struct tcp_pcb *p, err_t error)
{
    probe_status = "TCP 23 connected; waiting for banner";
    tcp_recv(p, probe_recv);
    return ERR_OK;
}

static void start_probe(void)
{
    probe_close();
    banner_bytes = 0;
    banner_hex[0] = 0;
    probe_start = now_ms();
    probe_attempts++;
    probe = tcp_new();
    if (!probe) {
        probe_status = "out of TCP connections";
        return;
    }
    probe_status = "connecting to 172.16.250.248:23";
    tcp_err(probe, probe_error);
    if (tcp_bind(probe, netif_ip_addr4(&usb_netif), 0) != ERR_OK ||
        tcp_connect(probe, &hu_address, 23, probe_connected) != ERR_OK) {
        probe_status = "connection setup failed";
        probe_close();
    }
}

#include "manager_page.h"

/* No request bodies: credentials/tokens are bounded header values, discarded
 * once handled. Duplicate values and control bytes are rejected. */
static bool header_value(const char *request, const char *name, char *out, size_t size)
{
    bool found = false;
    out[0] = 0;
    const char *p = strstr(request, "\r\n");
    if (!p)
        return false;
    p += 2;
    while (*p && strncmp(p, "\r\n", 2)) {
        const char *end = strstr(p, "\r\n");
        if (!end)
            return false;
        size_t key = strlen(name);
        if ((size_t)(end - p) > key && !strncasecmp(p, name, key) && p[key] == ':') {
            if (found)
                return false;
            found = true;
            const char *value = p + key + 1;
            if (value < end && *value == ' ')
                value++;
            size_t n = end - value;
            if (n >= size)
                return false;
            for (size_t i = 0; i < n; i++)
                if ((unsigned char)value[i] < 32 || (unsigned char)value[i] > 126)
                    return false;
            memcpy(out, value, n);
            out[n] = 0;
        }
        p = end + 2;
    }
    return true;
}

static bool port_pair(const char *text, struct mib_forward *f)
{
    unsigned values[2] = {0, 0}, part = 0, digits = 0;
    for (; *text; text++) {
        if (*text == ':' && part == 0 && digits) {
            part = 1;
            digits = 0;
            continue;
        }
        if (*text < '0' || *text > '9' || ++digits > 5)
            return false;
        values[part] = values[part] * 10 + (*text - '0');
        if (values[part] > 65535)
            return false;
    }
    if (part != 1 || !digits)
        return false;
    f->local = values[0];
    f->remote = values[1];
    return true;
}

static void json_string(char *out, const char *in)
{
    *out++ = '"';
    for (; *in; in++) {
        if (*in == '"' || *in == '\\')
            *out++ = '\\';
        *out++ = *in;
    }
    *out++ = '"';
    *out = 0;
}

struct http_client {
    struct tcp_pcb *pcb;
    char request[1280];
    unsigned used;
    uint32_t started;
    bool closing;
    const uint8_t *asset;
    size_t asset_size, asset_sent;
    char response[7168];
    bool asset_copy;
};
static struct http_client http_clients[3];

static void http_error(void *arg, err_t error)
{
    ((struct http_client *)arg)->pcb = NULL;
}

static err_t http_recv(void *, struct tcp_pcb *, struct pbuf *, err_t);
static err_t http_poll(void *, struct tcp_pcb *);
static err_t http_sent(void *, struct tcp_pcb *, u16_t);

static err_t http_close(struct http_client *c)
{
    c->closing = true;
    struct tcp_pcb *p = c->pcb;
    tcp_arg(p, NULL);
    tcp_err(p, NULL);
    tcp_recv(p, NULL);
    tcp_poll(p, NULL, 0);
    tcp_sent(p, NULL);
    if (tcp_close(p) == ERR_OK) {
        c->pcb = NULL;
        return ERR_OK;
    }
    tcp_arg(p, c);
    tcp_err(p, http_error);
    tcp_recv(p, http_recv);
    tcp_poll(p, http_poll, 2);
    return ERR_OK; // poll retries when lwIP frees a segment
}

static err_t http_pump(struct http_client *c)
{
    while (c->asset_sent < c->asset_size) {
        size_t n = c->asset_size - c->asset_sent;
        if (n > tcp_sndbuf(c->pcb))
            n = tcp_sndbuf(c->pcb);
        if (n > 1460)
            n = 1460;
        if (!n)
            break;
        // Immutable flash is valid until lwIP acknowledges it; no giant RAM copy.
        err_t result = tcp_write(c->pcb, c->asset + c->asset_sent, (u16_t)n,
                                 c->asset_copy ? TCP_WRITE_FLAG_COPY : 0);
        if (result == ERR_MEM)
            break;
        if (result != ERR_OK) {
            struct tcp_pcb *p = c->pcb;
            c->pcb = NULL;
            tcp_abort(p);
            return ERR_ABRT;
        }
        c->asset_sent += n;
    }
    tcp_output(c->pcb);
    if (c->asset_sent == c->asset_size)
        return http_close(c);
    return ERR_OK;
}

static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t count)
{
    struct http_client *c = arg;
    c->started = now_ms();
    return c->closing ? http_close(c) : http_pump(c);
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error)
{
    struct http_client *c = arg;
    if (!p)
        return c->asset && c->asset_sent < c->asset_size ? ERR_OK : http_close(c);
    if (c->closing || c->asset) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }
    if (p->tot_len >= sizeof(c->request) - c->used) {
        pbuf_free(p);
        c->pcb = NULL;
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    unsigned n = p->tot_len;
    pbuf_copy_partial(p, c->request + c->used, n, 0);
    c->used += n;
    c->request[c->used] = 0;
    tcp_recved(pcb, n);
    pbuf_free(p);
    if (!strstr(c->request, "\r\n\r\n"))
        return ERR_OK;
    const char *body = NULL, *type = "text/plain", *status = "200 OK";
    static char json[1400];
    char host[64], origin[64];
    bool ap =
        ip4_addr_get_u32(ip_2_ip4(&pcb->local_ip)) != ip4_addr_get_u32(netif_ip4_addr(&usb_netif));
    bool host_ok =
        header_value(c->request, "Host", host, sizeof(host)) &&
        ((!ap && !*host) || !strcmp(host, "192.168.4.1") || !strcmp(host, "192.168.4.1:80") ||
         (!ap && (!strcmp(host, "172.16.250.1") || !strcmp(host, "172.16.250.1:80"))));
    bool origin_ok = header_value(c->request, "Origin", origin, sizeof(origin)) &&
                     (!*origin || !strcmp(origin, "http://192.168.4.1") ||
                      !strcmp(origin, "http://192.168.4.1:80"));
    bool control = strstr(c->request, " /manage/") || strstr(c->request, " /api/") ||
                   strstr(c->request, " /probe ");
    if (!host_ok || !origin_ok || (control && !ap)) {
        status = "403 Forbidden";
        body = "Connect directly to the MIB-Link Wi-Fi address\n";
    } else if (!strncmp(c->request, "GET /api/settings HTTP/1.", 24)) {
        char ssid[68];
        json_string(ssid, settings.ssid);
        snprintf(
            c->response, sizeof(c->response),
            "{\"ssid\":%s,\"forwards\":[[%u,%u],[%u,%u],[%u,%u]],\"active\":%u,\"listeners\":%u}",
            ssid, settings.forwards[0].local, settings.forwards[0].remote,
            settings.forwards[1].local, settings.forwards[1].remote, settings.forwards[2].local,
            settings.forwards[2].remote, forward_active(), forward_listeners());
        c->asset = (const uint8_t *)c->response;
        c->asset_size = strlen(c->response);
        c->asset_copy = true;
        type = "application/json";
    } else if (!strncmp(c->request, "POST /api/settings HTTP/1.", 25)) {
        struct mib_settings next = settings;
        char pass[64] = {0}, pairs[3][16];
        bool valid = header_value(c->request, "X-MIB-SSID", next.ssid, sizeof(next.ssid)) &&
                     header_value(c->request, "X-MIB-Password", pass, sizeof(pass));
        if (*pass)
            strcpy(next.password, pass);
        for (unsigned i = 0; i < 3; i++) {
            char key[16];
            snprintf(key, sizeof(key), "X-MIB-Forward%u", i + 1);
            valid = valid && header_value(c->request, key, pairs[i], sizeof(pairs[i])) &&
                    port_pair(pairs[i], &next.forwards[i]);
        }
        if (valid && !manager_busy() && !console_active() && !forward_active() &&
            settings_save(&next))
            body = "Saved. Reconnecting with the new Wi-Fi settings is required.\n";
        else {
            status = "409 Conflict";
            body = "Invalid settings or an active session. Disconnect consoles and wait for SD "
                   "management to finish.\n";
        }
        memset(pass, 0, sizeof(pass));
        memset(&next, 0, sizeof(next));
    } else if (!strncmp(c->request, "GET /api/console HTTP/1.", 23)) {
        char cursor[16];
        if (!header_value(c->request, "X-MIB-Cursor", cursor, sizeof(cursor)) ||
            !console_json(c->response, sizeof(c->response), cursor)) {
            status = "400 Bad Request";
            body = "Invalid cursor\n";
        } else {
            c->asset = (const uint8_t *)c->response;
            c->asset_size = strlen(c->response);
            c->asset_copy = true;
            type = "application/json";
        }
    } else if (!strncmp(c->request, "POST /api/console HTTP/1.", 24)) {
        char action[16], id[16], data[513];
        bool valid = header_value(c->request, "X-MIB-Action", action, sizeof(action)) &&
                     header_value(c->request, "X-MIB-Session", id, sizeof(id)) &&
                     header_value(c->request, "X-MIB-Data", data, sizeof(data));
        if (valid && (!strcmp(action, "open") ? !manager_busy() && !forward_manual() : true) &&
            console_action(action, id, data))
            body = "OK\n";
        else {
            status = "409 Conflict";
            body = "Console busy, disconnected or input queue full\n";
        }
        memset(data, 0, sizeof(data));
    } else if (!strncmp(c->request, "GET / HTTP/1.", 13)) {
        c->asset = (const uint8_t *)index_page;
        c->asset_size = sizeof(index_page) - 1;
        type = "text/html; charset=utf-8";
    } else if (!strncmp(c->request, "GET /manage/status HTTP/1.", 25) ||
               !strncmp(c->request, "POST /manage/action HTTP/1.", 26)) {
        if (!strncmp(c->request, "GET ", 4)) {
            if (!manager_json(c->response, sizeof(c->response))) {
                status = "500 Internal Server Error";
                body = "Status too large\n";
            } else {
                c->asset = (const uint8_t *)c->response;
                c->asset_size = strlen(c->response);
                c->asset_copy = true;
                type = "application/json";
            }
        } else {
            char action[16], user[65], password[65], slot[48], digest[65];
            bool valid = header_value(c->request, "X-MHI2-Action", action, sizeof(action)) &&
                         header_value(c->request, "X-HU-User", user, sizeof(user)) &&
                         header_value(c->request, "X-HU-Password", password, sizeof(password)) &&
                         header_value(c->request, "X-MHI2-Slot", slot, sizeof(slot)) &&
                         header_value(c->request, "X-MHI2-Digest", digest, sizeof(digest));
            if (valid && !console_active() && !forward_manual() &&
                manager_request(action, user, password, slot, digest))
                body = "Accepted; watch HU state and log\n";
            else {
                status = "409 Conflict";
                body = "Request rejected: check connection, credentials, selection or busy state\n";
            }
            memset(password, 0, sizeof(password));
        }
    } else if (!strncmp(c->request, "GET /status HTTP/1.", 19)) {
        snprintf(json, sizeof(json),
                 "{\"build\":\"1.2.0-mib-link\",\"board\":\"%s\",\"android_auto_payload\":false,"
                 "\"usb_mounted\":%s,"
                 "\"usb_network_up\":%s,\"control_requests\":%lu,\"rejected_requests\":%lu,"
                 "\"last_rejected_request\":%u,\"frames_from_hu\":%lu,\"frames_to_hu\":%u,"
                 "\"malformed_frames\":%lu,\"dropped_frames\":%u,\"service_probe\":\"%s\","
                 "\"banner_bytes\":%u,\"banner_hex\":\"%s\",\"deployment\":\"%s\"}\n",
                 mib_board_name(), tud_mounted() ? "true" : "false",
                 netif_is_link_up(&usb_netif) ? "true" : "false", (unsigned long)adapter.controls,
                 (unsigned long)adapter.rejected_controls, adapter.last_rejected,
                 (unsigned long)adapter.rx_frames, usb_tx_frames,
                 (unsigned long)adapter.malformed_frames, usb_dropped_frames, probe_status,
                 banner_bytes, banner_hex, manager_status());
        body = json;
        type = "application/json";
    } else if (!strncmp(c->request, "POST /probe HTTP/1.", 19) &&
               strstr(c->request, "\r\nX-MHI2-Request: 1\r\n")) {
        if (netif_is_link_up(&usb_netif)) {
            probe_attempts = 0;
            start_probe();
            body = "Probe started\n";
        } else {
            status = "503 Service Unavailable";
            body = "USB network is not ready\n";
        }
    } else {
        status = "404 Not Found";
        body = "Not found\n";
        for (size_t i = 0; i < payload_asset_count; i++) {
            char request[160];
            int n = snprintf(request, sizeof(request), "GET %s HTTP/1.", payload_assets[i].path);
            if (n > 0 && n < (int)sizeof(request) && !strncmp(c->request, request, (size_t)n)) {
                c->asset = payload_assets[i].data;
                c->asset_size = payload_assets[i].size;
                type = payload_assets[i].type;
                status = "200 OK";
                break;
            }
        }
    }
    char header[384];
    int h = snprintf(header, sizeof(header),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: "
                     "close\r\nCache-Control: no-store\r\nX-Content-Type-Options: "
                     "nosniff\r\nX-Frame-Options: DENY\r\nReferrer-Policy: no-referrer\r\n\r\n",
                     status, type, (unsigned)(c->asset ? c->asset_size : strlen(body)));
    memset(c->request, 0, sizeof(c->request));
    if (c->asset) {
        if (h <= 0 || h >= (int)sizeof(header) ||
            tcp_write(pcb, header, h, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            c->pcb = NULL;
            tcp_abort(pcb);
            return ERR_ABRT;
        }
        tcp_sent(pcb, http_sent);
        return http_pump(c);
    }
    if (h <= 0 || h >= (int)sizeof(header) ||
        tcp_write(pcb, header, h, TCP_WRITE_FLAG_COPY) != ERR_OK ||
        tcp_write(pcb, body, strlen(body), TCP_WRITE_FLAG_COPY) != ERR_OK) {
        c->pcb = NULL;
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    tcp_output(pcb);
    return http_close(c);
}

static err_t http_poll(void *arg, struct tcp_pcb *pcb)
{
    struct http_client *c = arg;
    if (now_ms() - c->started > 15000) {
        c->pcb = NULL;
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    if (c->closing)
        return http_close(c);
    if (c->asset)
        return http_pump(c);
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t error)
{
    for (unsigned i = 0; i < 3; i++)
        if (!http_clients[i].pcb) {
            struct http_client *c = &http_clients[i];
            memset(c, 0, sizeof(*c));
            c->pcb = pcb;
            c->started = now_ms();
            tcp_arg(pcb, c);
            tcp_recv(pcb, http_recv);
            tcp_err(pcb, http_error);
            tcp_poll(pcb, http_poll, 2);
            return ERR_OK;
        }
    tcp_abort(pcb);
    return ERR_ABRT;
}

static void listen_on(const ip_addr_t *address, uint16_t port, tcp_accept_fn accept)
{
    struct tcp_pcb *p = tcp_new();
    if (!p)
        return;
    if (tcp_bind(p, address, port) != ERR_OK) {
        tcp_abort(p);
        return;
    }
    struct tcp_pcb *listener = tcp_listen(p);
    if (!listener) {
        tcp_abort(p);
        return;
    }
    tcp_accept(listener, accept);
}

void services_init(const ip_addr_t *address)
{
    IP_ADDR4(&hu_address, 172, 16, 250, 248);
    manager_init(&hu_address);
    forward_init(address, &hu_address);
    console_init(&hu_address);
    listen_on(address, 80, http_accept);
    listen_on(netif_ip_addr4(&usb_netif), 80, http_accept);
}

void services_poll(void)
{
    bool up = netif_is_link_up(&usb_netif);
    if (up && !was_up) {
        probe_attempts = 0;
        start_probe();
    }
    if (!up && was_up) {
        probe_close();
        probe_status = "USB network disconnected";
    }
    was_up = up;
    forward_poll(up);
    console_poll(up);
    manager_poll(up, forward_manual() || console_active());
    if (manager_busy() || forward_manual() || console_active())
        probe_close();
    if (probe && now_ms() - probe_start > 10000) {
        probe_status = "probe timed out";
        probe_close();
    }
    // Enumeration can precede inetd by minutes during a cold HU boot. Retry
    // only the non-writing banner probe, never a login or activation command.
    // Manual relay sessions take priority; stop after ten attempts or a reply.
    if (up && !probe && !banner_bytes && probe_attempts < 10 && !manager_busy() &&
        !forward_manual() && !console_active() && now_ms() - probe_start >= 30000)
        start_probe();
}
