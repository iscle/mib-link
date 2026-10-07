/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#define SD_MAX_FILES 32
#define SD_MAX_BYTES (8u * 1024u * 1024u)

struct sd_file {
    char path[96], sha256[65];
    unsigned size;
};

struct sd_manifest {
    char name[49], firmware[65];
    unsigned count, bytes;
    struct sd_file files[SD_MAX_FILES];
};

int sd_manifest_parse(const char *, size_t, struct sd_manifest *);
int sd_hex(const char *, unsigned);
int sd_path(const char *);
