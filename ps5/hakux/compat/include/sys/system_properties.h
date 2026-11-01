/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * Android's system properties, which hakuX reads for its debug switches (debug.hakux.*, through
 * hw/xbox/hle/prop.h and directly). Where Android's are not, hakuX's prop.h reads a property from
 * the environment variable of its name upper-cased with dots as underscores
 * (debug.hakux.present: DEBUG_HAKUX_PRESENT; ps5/patches/hakux, as hakuX-native does), and so does
 * __system_property_get here (ps5/hakux/compat/hakux_ps5.c). The title sets them from boot.txt's
 * "prop NAME=VALUE" lines and /data/ogbox/props.txt; an unset one, as on a phone, reads as empty.
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

#endif
