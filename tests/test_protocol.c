// SPDX-License-Identifier: GPL-3.0-or-later
#include "asix_protocol.h"
#include "dhcp_options.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint8_t expected[ASIX_FRAME_MAX];
static unsigned expected_length, emitted;

static void receive(void *ctx, const uint8_t *p, unsigned n)
{
    assert(n == expected_length);
    assert(!memcmp(p, expected, n));
    emitted++;
}

static void receive_random(void *ctx, const uint8_t *p, unsigned n)
{
    assert(n >= 14 && n <= ASIX_FRAME_MAX);
}

static const uint8_t mac[6] = {2, 3, 4, 5, 6, 7};

static void framing(void)
{
    uint8_t wire[ASIX_FRAME_MAX + 12];
    for (unsigned n = 14; n <= ASIX_FRAME_MAX; n++) {
        expected_length = n;
        for (unsigned j = 0; j < n; j++)
            expected[j] = (uint8_t)(j + n);
        size_t count = asix_encode(wire, sizeof(wire), expected, n);
        assert(count >= n + 4 && count % 64 != 0);
        assert(!asix_encode(wire, count - 1, expected, n));
        // All possible splits include 64-byte USB packets, odd frames, frames
        // ending exactly at a USB boundary, and split four-byte headers.
        for (unsigned split = 1; split <= count; split++) {
            struct asix_state s;
            asix_init(&s, mac);
            emitted = 0;
            asix_receive(&s, wire, split, false, receive, NULL);
            asix_receive(&s, wire + split, count - split, true, receive, NULL);
            assert(emitted == 1 && s.malformed_frames == 0);
        }
        struct asix_state s;
        asix_init(&s, mac);
        emitted = 0;
        for (size_t at = 0; at < count; at += 64) {
            size_t take = count - at;
            if (take > 64)
                take = 64;
            asix_receive(&s, wire + at, take, take < 64, receive, NULL);
        }
        assert(emitted == 1 && s.malformed_frames == 0);
    }
    struct asix_state s;
    asix_init(&s, mac);
    uint8_t broken[] = {0xff, 0x7f, 0, 0x80};
    asix_receive(&s, broken, sizeof(broken), true, receive, NULL);
    assert(s.malformed_frames == 1);
    asix_receive(&s, wire, 3, true, receive, NULL);
    assert(s.malformed_frames == 2);
    expected_length = 61;
    memset(expected, 0x5a, 61);
    size_t n = asix_encode(wire, sizeof(wire), expected, 61);
    uint8_t aggregate[256];
    memcpy(aggregate, wire, n);
    memcpy(aggregate + n, wire, n);
    emitted = 0;
    asix_receive(&s, aggregate, 2 * n, true, receive, NULL);
    assert(emitted == 2);
    assert(!asix_encode(wire, sizeof(wire), expected, ASIX_FRAME_MAX + 1));
    assert(!asix_encode(wire, sizeof(wire), expected, 13));
}

static void controls(void)
{
    struct asix_state s;
    asix_init(&s, mac);
    uint8_t data[64] = {0};
    assert(asix_control(&s, true, 0x13, 0, 0, data, 6) == 6 && !memcmp(data, mac, 6));
    assert(asix_control(&s, false, 0x06, 0, 0, data, 0) == 0);
    assert(asix_control(&s, true, 0x09, 0, 0, data, 1) == 1 && data[0] == 1);
    assert(asix_control(&s, true, 0x19, 0, 0, data, 2) == 2 && data[1] == 0x10);
    assert(asix_control(&s, false, 0x10, 0x88, 0, data, 0) == 0 && s.rx_control == 0x88);
    assert(asix_control(&s, false, 0x12, 0x0c15, 0x12, data, 3) == 3);
    assert(s.ipg[0] == 0x15 && s.ipg[1] == 0x0c && s.ipg[2] == 0x12);
    data[0] = 0;
    data[1] = 0xb3;
    assert(asix_control(&s, false, 0x08, 0x10, 0, data, 2) == 2);
    assert((s.phy[0] & 0x8200) == 0);
    assert(asix_control(&s, true, 0x07, 0x10, 32, data, 2) == -1);
    assert(asix_control(&s, true, 0x13, 0, 0, data, 64) == -1);
    assert(asix_control(&s, false, 0xff, 0, 0, data, 0) == -1);
    // Sweep malformed control lengths. Sanitizers check every accepted buffer.
    for (unsigned r = 0; r < 256; r++)
        for (unsigned size = 0; size <= 64; size++) {
            uint8_t *p = malloc(size ? size : 1);
            memset(p, 0, size);
            asix_control(&s, true, r, 0, 0, p, size);
            asix_control(&s, false, r, 0, 0, p, size);
            free(p);
        }
}

static void dhcp(void)
{
    uint8_t data[] = {0, 0, 53, 1, 1, 0, 50, 4, 192, 168, 4, 17, 255};
    assert(*dhcp_option(data, sizeof(data), 53, 1) == 1);
    assert(!memcmp(dhcp_option(data, sizeof(data), 50, 4), data + 8, 4));
    assert(!dhcp_option(data, 11, 50, 4));
    assert(!dhcp_option(data, sizeof(data), 53, 4));
    for (unsigned n = 0; n < 512; n++) {
        uint8_t *p = malloc(n ? n : 1);
        for (unsigned pass = 0; pass < 100; pass++) {
            for (unsigned j = 0; j < n; j++)
                p[j] = rand();
            const uint8_t *v = dhcp_option(p, n, 53, 1);
            if (v)
                assert(v >= p && v < p + n);
            struct asix_state s;
            asix_init(&s, mac);
            asix_receive(&s, p, n, true, receive_random, NULL);
        }
        free(p);
    }
}

int main(void)
{
    framing();
    controls();
    dhcp();
    puts("PASS: frame boundaries, aggregation, invalid lengths, control requests, DHCP options and "
         "malformed-input sweeps");
}
