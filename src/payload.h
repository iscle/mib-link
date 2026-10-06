// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
struct payload_asset {const char *path,*type;const uint8_t *data;size_t size;};
extern const struct payload_asset payload_assets[];
extern const size_t payload_asset_count;
