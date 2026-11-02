/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.45: XBE patches from the title folder's gamedata/patches.db (ps5/tools/import-gamedata.py
 * writes it): Jay's PatchHub .JMP patches (jayxbox.com), icup321's XboxGraphicalPatches (IPS, BPS)
 * and OgXHD's databases, by title ID. Chosen per game in /data/ogbox/mods.txt ("<key> patch <id>",
 * the mods and cheats screen), applied when the game starts: the disc's file read, the patches'
 * records applied to it in the order of the database, the result written beside the run's manifest
 * and handed to the core as a file of the disc (native/core/mods.h "file"). A patch whose bytes are
 * not found, or whose file is not the release it was made for (a CRC-32, a region), is left out and
 * the boot log says why; the game starts without it.
 */
#ifndef OGBOX_CORE_PATCHES_H
#define OGBOX_CORE_PATCHES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OgboxPatch {
    char id[16];
    char category[16];     /* Widescreen, 720p, 480p, 60 FPS, Misc */
    char name[160];
    char author[64];
    char note[320];
    char source[64];
    char region[16];
    bool on;
} OgboxPatch;

/* The patches the database has for the game of key ("%08X.%u"): those for its title ID and its
 * version or any. With which are on (mods.txt). How many (at most max). */
int ogbox_patches_catalog(const char *key, OgboxPatch *out, int max);

/* 0.1.47: whether a patch on for key needs the 128 MB RAM upgrade: its notes say 128 MB (0.1.51: only
 * then; the boot log names it). */
bool ogbox_patches_need_128(const char *key);

struct NcDisc;
/* At the game's start: the patches on for key applied to the disc's files, each patched file
 * written into work_dir and a "file" line for it into the manifest. How many files. */
int ogbox_patches_prepare(const struct NcDisc *disc, const char *key, const char *work_dir, FILE *manifest);

/* ---- the engine (also for the tests) ---- */

typedef struct OgboxBuf {
    uint8_t *data;
    size_t size, cap;
} OgboxBuf;

/* An IPS or BPS patch over buf; false (buf unchanged) with the reason in why. */
bool ogbox_apply_ips(OgboxBuf *buf, const uint8_t *ips, size_t n, char *why, size_t why_len);
bool ogbox_apply_bps(OgboxBuf *buf, const uint8_t *bps, size_t n, char *why, size_t why_len);
uint32_t ogbox_crc32(const uint8_t *p, size_t n);

/* The database's patch records of one patch for one target, applied in order ("F find repl",
 * "O offset hex", "A hex", "C crc", "I file", "B file": files_dir holds the IPS and BPS files);
 * false (buf unchanged) with the reason. */
bool ogbox_apply_records(OgboxBuf *buf, char *const *records, size_t n, const char *files_dir, char *why,
                         size_t why_len);

#ifdef __cplusplus
}
#endif

#endif
