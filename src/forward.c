// SPDX-License-Identifier: GPL-3.0-or-later
#include "forward.h"
#include "manager.h"
#include "console.h"
#include "settings.h"
#include "usb_asix.h"
#include "lwip/tcp.h"
#include "mib_platform.h"
#include <string.h>
#define CONNECTIONS 3
struct relay;

struct side {
    struct tcp_pcb *pcb;
    struct relay *relay;
    unsigned index;
    bool eof, fin;
};

struct relay {
    struct side sides[2];
    bool connected;
    uint16_t port;
    uint32_t started, activity;
};
static struct relay relays[CONNECTIONS];
static ip_addr_t target;
static unsigned listeners;

static uint32_t now(void)
{
    return mib_now_ms();
}

static void drop(struct relay *r)
{
    r->connected = false;
    for (unsigned i = 0; i < 2; i++)
        if (r->sides[i].pcb) {
            struct tcp_pcb *p = r->sides[i].pcb;
            r->sides[i].pcb = NULL;
            tcp_arg(p, NULL);
            tcp_recv(p, NULL);
            tcp_err(p, NULL);
            tcp_abort(p);
        }
}

static err_t receive(void *, struct tcp_pcb *, struct pbuf *, err_t);

static void failed(void *arg, err_t err)
{
    struct side *s = arg;
    s->pcb = NULL;
    drop(s->relay);
}

static void progress(struct relay *r)
{
    if (!r->connected)
        return;
    for (unsigned i = 0; i < 2; i++) {
        struct side *s = &r->sides[i], *other = &r->sides[i ^ 1];
        if (other->eof && s->pcb && !s->fin && tcp_shutdown(s->pcb, 0, 1) == ERR_OK)
            s->fin = true;
    }
    if (r->sides[0].eof && r->sides[1].eof && r->sides[0].fin && r->sides[1].fin) {
        for (unsigned i = 0; i < 2; i++)
            if (r->sides[i].pcb) {
                struct side *s = &r->sides[i];
                struct tcp_pcb *p = s->pcb;
                tcp_arg(p, NULL);
                tcp_recv(p, NULL);
                tcp_err(p, NULL);
                if (tcp_close(p) == ERR_OK)
                    s->pcb = NULL;
                else {
                    tcp_arg(p, s);
                    tcp_recv(p, receive);
                    tcp_err(p, failed);
                }
            }
    }
}

static err_t receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct side *s = arg;
    struct relay *r = s->relay;
    r->activity = now();
    if (!p) {
        s->eof = true;
        progress(r);
        return ERR_OK;
    }
    struct tcp_pcb *other = r->sides[s->index ^ 1].pcb;
    if (!other || !r->connected || p->tot_len > tcp_sndbuf(other))
        return ERR_MEM;
    static uint8_t buffer[TCP_WND];
    if (p->tot_len > sizeof(buffer)) {
        pbuf_free(p);
        drop(r);
        return ERR_ABRT;
    }
    unsigned n = p->tot_len;
    pbuf_copy_partial(p, buffer, n, 0);
    err_t result = tcp_write(other, buffer, n, TCP_WRITE_FLAG_COPY);
    if (result == ERR_MEM)
        return ERR_MEM;
    if (result != ERR_OK) {
        pbuf_free(p);
        drop(r);
        return ERR_ABRT;
    }
    tcp_output(other);
    tcp_recved(pcb, n);
    pbuf_free(p);
    return ERR_OK;
}

static err_t connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    struct side *s = arg;
    s->relay->connected = true;
    s->relay->activity = now();
    return ERR_OK;
}

static err_t accept_connection(void *arg, struct tcp_pcb *pcb, err_t err)
{
    const struct mib_forward *f = arg;
    if (!netif_is_link_up(&usb_netif) ||
        (f->remote == 23 && (manager_busy() || console_active() || forward_manual()))) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    struct relay *r = NULL;
    for (unsigned i = 0; i < CONNECTIONS; i++)
        if (!relays[i].sides[0].pcb && !relays[i].sides[1].pcb) {
            r = &relays[i];
            break;
        }
    if (!r) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    memset(r, 0, sizeof(*r));
    r->started = r->activity = now();
    r->port = f->remote;
    r->sides[0].pcb = pcb;
    r->sides[1].pcb = tcp_new();
    for (unsigned i = 0; i < 2; i++) {
        struct side *s = &r->sides[i];
        s->relay = r;
        s->index = i;
        if (s->pcb) {
            tcp_arg(s->pcb, s);
            tcp_recv(s->pcb, receive);
            tcp_err(s->pcb, failed);
            tcp_nagle_disable(s->pcb);
        }
    }
    if (!r->sides[1].pcb || tcp_bind(r->sides[1].pcb, netif_ip_addr4(&usb_netif), 0) != ERR_OK ||
        tcp_connect(r->sides[1].pcb, &target, f->remote, connected) != ERR_OK) {
        drop(r);
        return ERR_ABRT;
    }
    return ERR_OK;
}

void forward_init(const ip_addr_t *ap, const ip_addr_t *hu)
{
    target = *hu;
    for (unsigned i = 0; i < MIB_FORWARD_COUNT; i++) {
        if (!settings.forwards[i].local)
            continue;
        struct tcp_pcb *p = tcp_new();
        if (!p)
            continue;
        if (tcp_bind(p, ap, settings.forwards[i].local) != ERR_OK) {
            tcp_abort(p);
            continue;
        }
        struct tcp_pcb *l = tcp_listen(p);
        if (!l) {
            tcp_abort(p);
            continue;
        }
        tcp_arg(l, &settings.forwards[i]);
        tcp_accept(l, accept_connection);
        listeners++;
    }
}

void forward_poll(bool up)
{
    for (unsigned i = 0; i < CONNECTIONS; i++) {
        struct relay *r = &relays[i];
        if (!up || (!r->connected && now() - r->started > 15000) ||
            now() - r->activity > 30 * 60 * 1000)
            drop(r);
        else
            progress(r);
    }
}

bool forward_manual(void)
{
    for (unsigned i = 0; i < CONNECTIONS; i++)
        if (relays[i].port == 23 && (relays[i].sides[0].pcb || relays[i].sides[1].pcb))
            return true;
    return false;
}

unsigned forward_active(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < CONNECTIONS; i++)
        if (relays[i].sides[0].pcb || relays[i].sides[1].pcb)
            n++;
    return n;
}

unsigned forward_listeners(void)
{
    return listeners;
}
