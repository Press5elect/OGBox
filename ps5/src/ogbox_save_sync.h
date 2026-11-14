/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.8: the games' saves kept the same on the console's Xbox hard disk (E:\UDATA) and in a
 * folder on a network share or a USB drive, both ways, as Crux2 keeps its memory cards (Crux2's
 * ps5_card_sync): other consoles syncing with the same share have the same saves. The engine knows
 * nothing of the console (the file access is given to it), so its tests run on a PC
 * (ps5/tests/savesync).
 *
 * The folder there is "<place>/OGBox/Saves/UDATA", laid out as the Xbox's E:\UDATA: a folder a
 * game (its title ID, 8 hex digits), in it a folder a save and the game's own files (TitleMeta.xbx,
 * TitleImage.xbx). An Xbox, or another emulator, can take its saves from there as they are.
 *
 * What is synced: each save folder as one (its files never mixed from two sides), and each file at
 * a game's top. How it decides, for each, by what each side was after the last sync (its files'
 * count, bytes and newest time, kept in a list of the console's own):
 *   - on one side only: copied to the other (a sync never takes a save away: one deleted on one
 *     side comes back from the other; delete it on both, with remove below);
 *   - changed on one side since the last sync: that side's copy goes to the other, in place of what
 *     was there (its files the other side has and it hasn't taken away);
 *   - changed on both sides: the newer is kept on both, and the other is not lost: it is put in the
 *     "older" folder of its side first ("<data>/older saves" on the console, "<there>/../.older"
 *     there), named with the moment it was put away;
 *   - on both sides and never synced before: the same files are simply remembered; different ones
 *     are handled as changed on both.
 * A copy is marked in the list before it starts and the mark taken away after: one cut off (the
 * console turned off) is made again at the next sync, from the side it was made from, rather than
 * its half-copied folder taken for a newer save.
 *
 * Deleting (remove, the hard disk manager's "Delete here and on <place>" and its "Saves on <place>"):
 * a save, or a game's whole folder, taken away on the sides asked and forgotten in the list, so
 * the next sync has nothing to copy either way. The other side is listed first: when it doesn't
 * answer, nothing is deleted on either. Another console syncing with the same place still has its
 * own copy, and sends it back at its next sync (it has to be deleted there too).
 */
#ifndef OGBOX_SAVE_SYNC_H
#define OGBOX_SAVE_SYNC_H

#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ogbox {
namespace savesync {

struct Entry {
	std::string name;
	bool dir{ false };
	uint64_t size{ 0 };
	int64_t mtime{ 0 };
};

/* The file access, for both sides (a path says which: the console's own, or a share's). */
struct Io {
	/* A folder's entries; false when it can't be listed (not there). */
	std::function<bool(const std::string &folder, std::vector<Entry> *entries)> list;
	std::function<bool(const std::string &path, std::string *data)> read;
	/* The file holds exactly these bytes afterwards, or is as it was. */
	std::function<bool(const std::string &path, const std::string &data)> write;
	/* The folder and those above it. */
	std::function<bool(const std::string &folder)> makeFolder;
	/* A file taken away. */
	std::function<bool(const std::string &path)> remove;
	/* An empty folder taken away (only remove() uses it). */
	std::function<bool(const std::string &folder)> removeFolder;
	/* Asked before each save: true stops the sync there (a game is about to start). */
	std::function<bool()> stop;
	/* The list as it is now, kept (before and after each copy). */
	std::function<void(const std::string &records)> persist;
};

/* What a save (or a game's file) was: its files, their bytes, the newest time. */
struct Sig {
	bool exists{ false };
	uint32_t files{ 0 };
	uint64_t bytes{ 0 };
	int64_t mtime{ 0 };
	bool same(const Sig &o) const { return files == o.files && bytes == o.bytes && mtime == o.mtime; }
};

struct Record {
	Sig here, there;
	char pending{ 0 };   // 'S' a copy here -> there under way, 'B' there -> here
};
/* By "<title ID>/<name>", in lower case. */
using Records = std::map<std::string, Record>;

std::string formatRecords(const Records &records);
Records parseRecords(const std::string &text);

struct Folders {
	std::string here;        // the console's E:\UDATA
	std::string there;       // "<place>/OGBox/Saves/UDATA"
	std::string olderHere;   // "<data>/older saves"
	std::string olderThere;  // "<place>/OGBox/Saves/.older"
};

struct Result {
	bool listed{ false };    // the other side could be listed (false: nothing was done)
	bool stopped{ false };
	int sent{ 0 }, brought{ 0 }, bothChanged{ 0 }, failed{ 0 }, unchanged{ 0 };
	std::vector<std::string> lines;   // one for each save copied or not, for the log
};

/* A game's folder: its title ID, 8 hex digits. */
bool isTitleFolder(const std::string &name);

/* One sync. `records` is read and brought up to date. `now` names what is put away. */
Result sync(const Io &io, const Folders &folders, Records *records, time_t now);

/* ---- deleting, and what is on the other side ---- */

/* The key of a save (or a game's file) in the list: "<title ID>/<name>", in lower case. */
std::string recordKey(const std::string &titleId, const std::string &name);

struct Removal {
	bool listed{ false };    // each side asked could be listed (false: nothing was deleted)
	bool ok{ false };        // all of it deleted on each side asked
	bool foundHere{ false }, foundThere{ false };   // it was there to delete
	std::string why;
};
/* A save folder (or a game's file) `name` of the game titleId, or with name "" the game's whole
 * folder, deleted on the sides asked (there first), and forgotten in `records` (and kept: persist).
 * Nothing outside it is touched: titleId must be 8 hex digits, name one plain name. */
Removal remove(const Io &io, const Folders &folders, Records *records, const std::string &titleId, const std::string &name,
	bool here, bool there);

struct ThereSave {
	std::string name;        // its folder's name there
	Sig sig;
	std::string meta;        // its SaveMeta.xbx's path there ("" none)
	bool here{ false };      // the console has a save of that name for the game
};
struct ThereTitle {
	std::string folder;      // the game's folder's name there (its title ID)
	std::string meta;        // its TitleMeta.xbx's path there ("" none)
	bool here{ false };      // the console has the game's folder
	std::vector<ThereSave> saves;
};
/* The games and saves in folders.there (false: it can't be listed). */
bool listThere(const Io &io, const Folders &folders, std::vector<ThereTitle> *titles);

/* A sentence for a toast: "Saves synced with NAS: 2 sent, 1 brought here." */
std::string summary(const Result &result, const std::string &placeName);

} // namespace savesync
} // namespace ogbox

#endif
