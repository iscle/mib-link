// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"
#include <string.h>
#include <stddef.h>
struct mst_settings settings;

void settings_defaults(struct mst_settings *s)
{
    memset(s, 0, sizeof(*s));
    strcpy(s->ssid, "MST-Link");
    strcpy(s->password, "mstlink1");
    s->forwards[0] = (struct mst_forward){2323, 23};
    s->forwards[1] = (struct mst_forward){2222, 22};
    s->forwards[2] = (struct mst_forward){8080, 80};
}

static bool printable(const char *s, size_t cap, size_t min)
{
    size_t n = 0;
    for (; n < cap && s[n]; n++)
        if ((unsigned char)s[n] < 32 || (unsigned char)s[n] > 126)
            return false;
    return n >= min && n < cap;
}

bool settings_valid(const struct mst_settings *s)
{
    if (!printable(s->ssid, sizeof(s->ssid), 1) || !printable(s->password, sizeof(s->password), 8))
        return false;
    for (unsigned i = 0; i < MST_FORWARD_COUNT; i++) {
        unsigned p = s->forwards[i].local, r = s->forwards[i].remote;
        if ((!p) != (!r) || p == 80)
            return false;
        for (unsigned j = 0; j < i; j++)
            if (p && p == s->forwards[j].local)
                return false;
    }
    return true;
}

#ifdef MST_HOST_TEST
void settings_init(void)
{
    settings_defaults(&settings);
}

bool settings_save(const struct mst_settings *s)
{
    if (!settings_valid(s))
        return false;
    settings = *s;
    return true;
}

void settings_poll(void)
{
}
#endif
