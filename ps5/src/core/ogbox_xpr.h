/*
 * OGBox - an XBE's title image (its $$XTIMAGE section, an XPR0 resource) as RGBA.
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.3: the game list's last cover, when no cover source has one: the picture every disc
 * carries in its default.xbe (the one the dashboard shows). An XPR0 holds one texture: DXT1, DXT3
 * or DXT5 (compressed, not swizzled), or 32-bit ARGB swizzled or linear.
 */
#ifndef OGBOX_XPR_H
#define OGBOX_XPR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An XPR0's first texture decoded: *rgba (malloc'd, w * h * 4 bytes, R G B A), its size. False if
 * it isn't an XPR0 or its format is one this doesn't take. */
bool ogbox_xpr_decode(const uint8_t *xpr, size_t n, uint8_t **rgba, int *w, int *h);

/* A disc image's (or a share's) default.xbe's title image, decoded (ogbox_xpr_decode). */
bool ogbox_game_title_image(const char *image, uint8_t **rgba, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
