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
#include <set>
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

// OGBox 1.0.3: "Download cheats" (Settings > Updates), only when the user asks. The title's
// gamedata/cheats-xbased.txt (ps5/tools/import-xbased-cheats.py) lists the Xbox scene's trainers xbased
// keeps in its repository:
//
//     # OGBox cheats 1 crazyriddler/xbased <commit, 40 hex>
//     <sha256> <bytes> cheats/<title ID, 8 hex>/<file>      (the name may have spaces)
//
// Cheats() fetches each from raw.githubusercontent.com/crazyriddler/xbased/<commit>/<path> into
// <trainers>/<title ID>/<file>, checked against its size and SHA-256, written beside its place and
// renamed there. A file already there with the same contents is left; one of the same name with
// other contents (the user's own) is kept as it is and counted; nothing is deleted. A name the
// tool would not list is refused here too (SafeCheatPath). The files are not OGBox's: none ships.
struct CheatsReport
{
	bool ok = false;        // the index was read and every file it names is in place (or kept)
	std::string error;      // why not, a sentence
	unsigned listed = 0, fetched = 0, present = 0, kept = 0, failed = 0;
	unsigned games = 0;     // 1.0.4: the title IDs asked for (0: every game's), and how many of them
	unsigned gamesListed = 0; // the index has cheats for
};

/// cheats/<8 hex digits>/<name>, the name as import-xbased-cheats.py allows it.
bool SafeCheatPath(const std::string& path);

/// The work (any thread): index is cheats-xbased.txt's path, raw the address before the commit
/// ("https://raw.githubusercontent.com/crazyriddler/xbased/"), trainers the user's trainers folder.
/// progress (may be empty) is told each file done, of how many.
/// 1.0.4: connections (1 to 16) files fetched at once, get called from that many threads (it must allow
/// it; progress is called under a lock, from any of them).
/// 1.0.4: titles (8 hex digits, upper case, as the index's folders), when not empty: only the files of
/// those games are fetched and counted (listed is theirs); empty, every game's.
bool Cheats(const std::string& index, const std::string& raw, const std::string& trainers, const Getter& get,
	CheatsReport& report, const std::function<void(unsigned done, unsigned total)>& progress, unsigned connections = 1,
	const std::set<std::string>& titles = std::set<std::string>());

/// Cheats() on a thread of its own with OGBoxNet::Get: false when one is running already. 1.0.4: titles
/// as Cheats() takes them (empty: every game's).
bool StartCheats(const std::string& index, const std::string& trainers,
	const std::set<std::string>& titles = std::set<std::string>());
/// 1.0.4: one game's cheats (a game's page with none in its folder), quietly on a thread of its own, after
/// any other download of them; GameCheatsFetched() counts the ones that brought a file (the trainers
/// are read again then).
void StartGameCheats(const std::string& index, const std::string& trainers, const std::string& titleId);
unsigned GameCheatsFetched();
/// While one runs: true, with how far it is.
bool CheatsRunning(unsigned& done, unsigned& total);
/// Its report, once, when it is done: true then.
bool PollCheats(CheatsReport& report);
} // namespace ogbox::fixes
