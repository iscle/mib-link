/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sha256.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t rr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

extern void sd_hash_progress(const char *, unsigned) __attribute__((weak));

static void block(uint32_t h[8], const unsigned char *p)
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (unsigned i = 16; i < 64; i++)
        w[i] = w[i - 16] + (rr(w[i - 15], 7) ^ rr(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
               (rr(w[i - 2], 17) ^ rr(w[i - 2], 19) ^ (w[i - 2] >> 10));
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], v = h[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t t1 = v + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        uint32_t t2 = (rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        v = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += v;
}

int sd_sha256(const char *path, char hex[65])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    unsigned char buf[8192];
    size_t n;
    uint64_t total = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) == sizeof(buf)) {
        total += n;
        if (total > 128u * 1024u * 1024u) {
            fclose(f);
            return 0;
        }
        for (unsigned i = 0; i < sizeof(buf); i += 64)
            block(h, buf + i);
        if (sd_hash_progress && !(total % (2u * 1024u * 1024u)))
            sd_hash_progress(path, (unsigned)total);
    }
    if (ferror(f) || !feof(f) || total + n > 128u * 1024u * 1024u) {
        fclose(f);
        return 0;
    }
    fclose(f);
    total += n;
    size_t at = 0;
    for (; at + 64 <= n; at += 64)
        block(h, buf + at);
    unsigned char tail[128] = {0};
    size_t left = n - at;
    memcpy(tail, buf + at, left);
    tail[left] = 0x80;
    unsigned bytes = left < 56 ? 64 : 128;
    uint64_t bits = total * 8;
    for (unsigned i = 0; i < 8; i++)
        tail[bytes - 1 - i] = (unsigned char)(bits >> (i * 8));
    block(h, tail);
    if (bytes == 128)
        block(h, tail + 64);
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; i++) {
        unsigned b = (h[i / 4] >> (24 - (i % 4) * 8)) & 255;
        hex[2 * i] = digits[b >> 4];
        hex[2 * i + 1] = digits[b & 15];
    }
    hex[64] = 0;
    return 1;
}

int sd_hash_matches(const char *path, const char *expected)
{
    char h[65];
    return sd_sha256(path, h) && !strcmp(h, expected);
}
