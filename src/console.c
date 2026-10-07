// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.h"
#include "usb_asix.h"
#include "lwip/tcp.h"
#include "mst_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RING 8192
static ip_addr_t target;
static struct tcp_pcb *connection;
static uint8_t ring[RING], pending[8192];
static unsigned queued, state, verb;
static uint32_t end, session, started, activity;
static bool ready;
static const char *status = "Disconnected";

static uint32_t now(void)
{
    return mst_now_ms();
}

static void close_console(const char *message)
{
    status = message;
    ready = false;
    if (connection) {
        struct tcp_pcb *p = connection;
        connection = NULL;
        tcp_arg(p, NULL);
        tcp_err(p, NULL);
        tcp_recv(p, NULL);
        tcp_abort(p);
    }
    memset(pending, 0, sizeof(pending));
    queued = 0;
}

static void failed(void *arg, err_t error)
{
    connection = NULL;
    close_console("Connection closed or refused");
}

static bool queue(const uint8_t *data, unsigned n)
{
    if (n > sizeof(pending) - queued)
        return false;
    memcpy(pending + queued, data, n);
    queued += n;
    return true;
}

static void pump(void)
{
    if (!connection || !ready || !queued)
        return;
    unsigned n = queued < tcp_sndbuf(connection) ? queued : tcp_sndbuf(connection);
    if (n && tcp_write(connection, pending, n, TCP_WRITE_FLAG_COPY) == ERR_OK) {
        memmove(pending, pending + n, queued - n);
        queued -= n;
        memset(pending + queued, 0, n);
        tcp_output(connection);
    }
}

static err_t receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error)
{
    if (!p) {
        close_console("Head unit closed the console");
        return ERR_ABRT;
    }
    /* Reserve worst-case negotiation replies before consuming any input. */
    if ((unsigned)p->tot_len + 2 > sizeof(pending) - queued)
        return ERR_MEM;
    for (struct pbuf *b = p; b; b = b->next)
        for (unsigned i = 0; i < b->len; i++) {
            uint8_t ch = ((uint8_t *)b->payload)[i];
            if (state == 0) {
                if (ch == 255)
                    state = 1;
                else
                    ring[end++ % RING] = ch;
            } else if (state == 1) {
                if (ch == 255) {
                    ring[end++ % RING] = ch;
                    state = 0;
                } else if (ch >= 251 && ch <= 254) {
                    verb = ch;
                    state = 2;
                } else
                    state = ch == 250 ? 3 : 0;
            } else if (state == 2) {
                uint8_t reply[3] = {255, 0, ch};
                if (verb == 251)
                    reply[1] = (ch == 1 || ch == 3) ? 253 : 254;
                else if (verb == 253)
                    reply[1] = 252;
                if (reply[1])
                    queue(reply, 3);
                state = 0;
            } else if (state == 3) {
                if (ch == 255)
                    state = 4;
            } else
                state = ch == 240 ? 0 : 3;
        }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    pump();
    return ERR_OK;
}

static err_t connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    ready = true;
    status = "Connected";
    return ERR_OK;
}

void console_init(const ip_addr_t *hu)
{
    target = *hu;
}

bool console_active(void)
{
    return connection != NULL;
}

static bool decimal(const char *s, uint32_t *value)
{
    if (!*s)
        return false;
    unsigned long long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
        v = v * 10 + (*s - '0');
        if (v > 0xffffffffu)
            return false;
    }
    *value = (uint32_t)v;
    return true;
}

static int unhex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

bool console_action(const char *action, const char *id, const char *data)
{
    if (!strcmp(action, "open")) {
        if (connection || !netif_is_link_up(&usb_netif))
            return false;
        memset(ring, 0, sizeof(ring));
        end = queued = state = 0;
        session++;
        if (!session)
            session++;
        started = activity = now();
        connection = tcp_new();
        if (!connection)
            return false;
        tcp_arg(connection, NULL);
        tcp_err(connection, failed);
        tcp_recv(connection, receive);
        tcp_nagle_disable(connection);
        if (tcp_bind(connection, netif_ip_addr4(&usb_netif), 0) != ERR_OK ||
            tcp_connect(connection, &target, 23, connected) != ERR_OK) {
            close_console("Connection setup failed");
            return false;
        }
        status = "Connecting";
        return true;
    }
    uint32_t sid;
    if (!decimal(id, &sid) || sid != session || !connection)
        return false;
    if (!strcmp(action, "close")) {
        close_console("Disconnected");
        memset(ring, 0, sizeof(ring));
        end = 0;
        return true;
    }
    if (strcmp(action, "send") || !ready)
        return false;
    size_t n = strlen(data);
    if (!n || n > 512 || n % 2)
        return false;
    uint8_t decoded[512];
    unsigned used = 0;
    for (unsigned i = 0; i < n; i += 2) {
        int a = unhex(data[i]), b = unhex(data[i + 1]);
        if (a < 0 || b < 0)
            return false;
        uint8_t ch = (a << 4) | b;
        decoded[used++] = ch;
        if (ch == 255)
            decoded[used++] = 255;
    }
    bool ok = queue(decoded, used);
    memset(decoded, 0, sizeof(decoded));
    if (ok) {
        activity = now();
        pump();
    }
    return ok;
}

size_t console_json(char *out, size_t size, const char *cursor)
{
    uint32_t from = 0;
    if (*cursor && !decimal(cursor, &from))
        return 0;
    uint32_t first = end > RING ? end - RING : 0;
    bool lost = from < first || from > end;
    if (from < first || from > end)
        from = first;
    uint32_t count = end - from;
    if (count > 1024)
        count = 1024;
    int n =
        snprintf(out, size,
                 "{\"session\":%lu,\"connected\":%s,\"ready\":%s,\"status\":\"%s\",\"lost\":%s,"
                 "\"next\":%lu,\"data\":\"",
                 (unsigned long)session, connection ? "true" : "false", ready ? "true" : "false",
                 status, lost ? "true" : "false", (unsigned long)(from + count));
    if (n < 0 || (size_t)n + count * 2 + 3 >= size)
        return 0;
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < count; i++) {
        uint8_t ch = ring[(from + i) % RING];
        out[n++] = hex[ch >> 4];
        out[n++] = hex[ch & 15];
    }
    memcpy(out + n, "\"}", 3);
    return n + 2;
}

void console_poll(bool up)
{
    if (!up && connection)
        close_console("USB network disconnected");
    else if (connection &&
             ((!ready && now() - started > 15000) || now() - activity > 10 * 60 * 1000))
        close_console("Console timed out");
    pump();
}
