/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * Android's system properties, which hakuX reads for its debug switches (debug.hakux.*, through
 * hw/xbox/hle/prop.h and directly): on the console a table the title fills
 * (ps5/hakux/compat/hakux_ps5.c), from boot.txt's "prop NAME=VALUE" lines and props.txt in the
 * title folder; an unset one, as on a phone, reads as empty. hw/xbox/hle/prop.h's helpers are
 * Android-only (__ANDROID__), so they are given here as well, with the same behaviour.
 */
#ifndef PSHAKUX_SYS_SYSTEM_PROPERTIES_H
#define PSHAKUX_SYS_SYSTEM_PROPERTIES_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROP_NAME_MAX 32
#define PROP_VALUE_MAX 92

/* The value in value (empty when unset) and its length. */
int __system_property_get(const char *name, char *value);

/* The title's side: a property set (an empty value unsets it). */
int pshakux_prop_set(const char *name, const char *value);
/* NAME=VALUE lines of a file (# comments); the number set, -1 when it cannot be read. */
int pshakux_props_load(const char *path);

#ifdef __cplusplus
}
#endif

/* hw/xbox/hle/prop.h's helpers, which it defines for Android only. */
#ifndef __ANDROID__
#ifndef PSHAKUX_PROP_HELPERS
#define PSHAKUX_PROP_HELPERS
static inline int hle_prop_get(const char *name, char v[PROP_VALUE_MAX])
{
    static int off = -1;
    if (off < 0) {
        const char *e = getenv("HAKUX_NO_PROPS");
        off = e && e[0] == '1';
    }
    v[0] = 0;
    return off ? 0 : __system_property_get(name, v);
}

typedef struct HleProp {
    char last[PROP_VALUE_MAX];
    bool polled;
} HleProp;

static inline bool hle_prop_new(HleProp *p, const char *name, char v[PROP_VALUE_MAX])
{
    hle_prop_get(name, v);
    bool first = !p->polled;
    p->polled = true;
    if (!strcmp(v, p->last)) {
        return false;
    }
    strcpy(p->last, v);
    return !first && v[0];
}
#endif
#endif

#endif
