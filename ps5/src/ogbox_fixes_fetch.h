// OGBox - game fixes from GitHub at start: the mods (mods/) and the XBE patches (gamedata/) that
// main has, fetched when they differ from the title folder's, so a game fix reaches the console
// without a new build of OGBox.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 1.0.2. The repository's ps5/game-fixes.txt (ps5/tools/game-fixes-index.py) lists every file:
//
//     # OGBox game fixes 1
//     <sha256> <bytes> <path below the title folder>      mods/<TID.version>/<mod>/mod.txt, gamedata/...
//
// Sync() reads it, hashes each file of the title folder it names, and fetches the ones that differ
// from ps5/<path> on the same branch: each checked against its size and SHA-256, written beside its
// place and renamed over it. A mod folder fetched before and no longer listed (a fix withdrawn) is
// removed; nothing else in the title folder is. What was fetched is kept in a record file (one path
// a line), so only what this fetched is ever removed. Nothing runs a fetched file: mods and patches
// are data the game's start reads (core/ogbox_mods.c, core/ogbox_patches.c).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ogbox::fixes
{
/// GET an address: the HTTP status (-1 when the network failed) and the body.
using Getter = std::function<int(const std::string& url, std::vector<uint8_t>& out)>;

/// One game whose fixes changed, for the notice.
struct Fixed
{
	uint32_t titleId = 0;   // 0: not known
	std::string key;        // "4D530039.1" (a mod's folder), or the title ID's 8 digits (a patch)
	std::vector<std::string> what; // the mods' and patches' names; "(removed) <name>" for a withdrawn one
};

struct Report
{
	bool ok = false;        // the list was read and every file it named is in place
	std::string error;      // why not, a sentence (the log has the rest)
	unsigned fetched = 0, removed = 0, failed = 0;
	std::vector<Fixed> fixed;
};

/// The work (any thread): base is the repository's ps5/ folder ("https://.../main/ps5/").
bool Sync(const std::string& base, const std::string& titleDir, const std::string& record, const Getter& get,
	Report& report);

/// At start: Sync() on a thread of its own, once a run, with OGBoxNet::Get and Press5elect/OGBox's main.
void Start(const std::string& titleDir);
/// Its report, once, when it is done: true then.
bool Poll(Report& report);
} // namespace ogbox::fixes
