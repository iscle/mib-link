/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Bounded file hashing with no dependency on a firmware's crypto-library ABI. */
int sd_sha256(const char *path, char hex[65]);
int sd_hash_matches(const char *path, const char *expected);
