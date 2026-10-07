// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>

// Walk only the received bytes, including PAD and END. Never read a length or
// value past the datagram boundary, even for a truncated or unknown option.
static inline const uint8_t *dhcp_option(const uint8_t *data, size_t length, uint8_t wanted,
                                         size_t exact_length)
{
    size_t i = 0;
    while (i < length) {
        uint8_t code = data[i++];
        if (code == 255)
            return NULL;
        if (code == 0)
            continue;
        if (i == length)
            return NULL;
        size_t n = data[i++];
        if (n > length - i)
            return NULL;
        if (code == wanted)
            return n == exact_length ? data + i : NULL;
        i += n;
    }
    return NULL;
}
