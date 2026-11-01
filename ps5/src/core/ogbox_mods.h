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

#ifdef __cplusplus
extern "C" {
#endif

/* After the disc is open (disc_fd), before the core boots: the game's key from its default.xbe,
 * the mods and cheats it has, those chosen, the manifest in work_dir. Said in the boot log. */
void ogbox_mods_prepare(int disc_fd, const char *work_dir);

#ifdef __cplusplus
}
#endif

#endif
