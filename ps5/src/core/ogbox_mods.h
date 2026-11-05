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
#include <stddef.h>
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
    uint32_t xbe_time; /* 0.1.54: default.xbe's date (its header's 0x114: the trainers' release check) */
} OgboxGameInfo;

/* A disc image's default.xbe certificate (on a share, through ps5vfs_disc_open). */
bool ogbox_game_info(const char *image, OgboxGameInfo *info);
/* 0.1.52: the CRC-32 of a disc image's default.xbe (the patches' release check); false if unread. */
bool ogbox_game_xbe_crc(const char *image, uint32_t *crc);

/* 0.0.2: whether a mod, patch or cheat was made for the disc's release (a title ID and version can
 * stand for more than one: a USA and a European disc) */
enum {
    OGBOX_FIT_UNKNOWN = 0,     /* not checked (the disc not read) */
    OGBOX_FIT_YES = 1,         /* its bytes are the disc's */
    OGBOX_FIT_NO = 2,          /* made for another release: can't be turned on, left out at the start */
    OGBOX_FIT_UNCHECKED = 3,   /* nothing in it to check against the disc (a mod without "old ->" lines,
                                  cheats.txt without a crc: line) */
};

typedef struct OgboxModEntry {
    char id[128];          /* a mod's folder name; a cheat's name; a patch's id */
    char name[160];
    char description[384];
    int refresh;           /* a mod's display refresh (120: a 120 fps mod) */
    bool cheat, once, on;
    /* 0.0.3: a game fix (mod.txt "gamefix: true"): always on, shown greyed, can't be turned off */
    bool gamefix;
    /* 0.1.45: an XBE patch of gamedata/patches.db (ogbox_patches.h) */
    bool patch;
    char category[16];
    char author[64];
    char source[64];
    /* 0.0.2: the release check (OGBOX_FIT_*), and why it doesn't fit */
    int fit;
    char fit_why[192];
} OgboxModEntry;

/* The game's mods, its patches (0.1.45), then its cheats, with which are on (/data/ogbox/mods.txt):
 * how many. 0.0.2: with the release check of the last ogbox_mods_check_disc or ogbox_mods_prepare
 * for key (OGBOX_FIT_UNKNOWN without one). */
int ogbox_mods_catalog(const char *key, OgboxModEntry *out, int max);
/* 0.0.2: the release check of key's mods, patches and cheats against a disc image's files (the
 * game list's Mods screen, before ogbox_mods_catalog; ogbox_mods_prepare does it at the start):
 * default.xbe's CRC-32 into *crc (0: unread; the patches' release check too,
 * ogbox_patches_set_disc_crc). False if the disc can't be read. */
bool ogbox_mods_check_disc(const char *image, const char *key, uint32_t *crc);
/* 0.0.2: default.xbe's CRC-32 of the last check for key, 0 if none. */
uint32_t ogbox_mods_disc_crc(const char *key);
/* One mod or cheat on or off in /data/ogbox/mods.txt. */
bool ogbox_mods_choose(const char *key, bool cheat, const char *name, bool on);
/* 0.1.45: the same of any kind ("mod", "cheat", "patch"). */
bool ogbox_mods_choose_kind(const char *key, const char *kind, const char *name, bool on);
/* 0.1.54: a game's one line of a kind ("trainer", "trainer-options"): its value (the rest of the
 * line), or false; and set (every line of that kind for the game replaced by one; "" none). */
bool ogbox_mods_get(const char *key, const char *kind, char *value, size_t n);
bool ogbox_mods_set(const char *key, const char *kind, const char *value);

#ifdef __cplusplus
}
#endif

#endif
