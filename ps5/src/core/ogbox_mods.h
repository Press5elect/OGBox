/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * hakuX's mods and cheats (its mods/ folder, shipped in the title folder as mods/): what hakuX's
 * Android app does with them (data/Mods.kt, ModCatalog and ModRun), for the console. The game
 * is known by its disc's title ID and certificate version ("4541005B.1", Burnout 3), the
 * folder mods/<that>/ holds its cheats.txt and a folder a mod; which are on is in
 * /data/ogbox/mods.txt (or the title folder's mods.txt), one per line:
 *
 *     4541005B.1 mod 120fps
 *     4D530064.3 mod 60fps
 *
 * The manifest of the run (native/core/mods.h) is written and handed to the core.
 */
#ifndef OGBOX_CORE_MODS_H
#define OGBOX_CORE_MODS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* After the disc is open (disc_fd), before the core boots: the game's key from its default.xbe,
 * the mods and cheats it has, those chosen, the manifest in work_dir. Said in the boot log. */
void ogbox_mods_prepare(int disc_fd, const char *work_dir);

/* OGBox 0.1.26: what the game list asks, without the core. */

typedef struct OgboxGameInfo {
    bool ok;
    uint32_t title_id, version;
    char key[32];     /* "%08X.%u": the mods' and the game's settings' key */
    char title[48];   /* the certificate's title name (ASCII; '?' for other characters) */
    uint32_t regions; /* 0.1.45: the certificate's game regions (1 North America, 2 Japan, 4 the rest) */
} OgboxGameInfo;

/* A disc image's default.xbe certificate (on a share, through ps5vfs_disc_open). */
bool ogbox_game_info(const char *image, OgboxGameInfo *info);

typedef struct OgboxModEntry {
    char id[128];          /* a mod's folder name; a cheat's name; a patch's id */
    char name[160];
    char description[384];
    int refresh;           /* a mod's display refresh (120: a 120 fps mod) */
    bool cheat, once, on;
    /* 0.1.45: an XBE patch of gamedata/patches.db (ogbox_patches.h) */
    bool patch;
    char category[16];
    char author[64];
    char source[64];
} OgboxModEntry;

/* The game's mods, its patches (0.1.45), then its cheats, with which are on (/data/ogbox/mods.txt):
 * how many. */
int ogbox_mods_catalog(const char *key, OgboxModEntry *out, int max);
/* One mod or cheat on or off in /data/ogbox/mods.txt. */
bool ogbox_mods_choose(const char *key, bool cheat, const char *name, bool on);
/* 0.1.45: the same of any kind ("mod", "cheat", "patch"). */
bool ogbox_mods_choose_kind(const char *key, const char *kind, const char *name, bool on);

#ifdef __cplusplus
}
#endif

#endif
