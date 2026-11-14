/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.53: the games' saves on the emulated Xbox hard disk (the hard disk manager) and the
 * save states (the quick menu, the game page).
 *
 * The hard disk is a folder (hakuX's native/kernel/io.c): Partition1 is E:, where a game keeps its
 * saves in UDATA\<title ID>\<one folder a save> (SaveMeta.xbx names it) beside TitleMeta.xbx (the
 * game's name), and its own data in TDATA\<title ID>; Partition3-5 are X:, Y: and Z:, the games'
 * cache drives. A copy of a save out of the hard disk is a folder <title ID>/<save folder>, in
 * <data folder>/backups/saves or <USB drive>/OGBox/saves; one brought in may also come as
 * UDATA/<title ID>/<save folder> (the layout other tools copy saves out in).
 */
#ifndef OGBOX_SAVES_H
#define OGBOX_SAVES_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ogbox {

struct HddSave {
	std::string folder;     // its folder's name in UDATA\<title ID>
	std::string name;       // SaveMeta.xbx's Name=, else the folder's
	std::string path;       // on the console
	uint64_t bytes{ 0 };
	int64_t mtime{ 0 };
	bool alsoHere{ false };  // (a save on the sync place) the hard disk has a save of that folder too
};

struct HddTitle {
	uint32_t titleId{ 0 };
	std::string idText;     // the folder's name ("4541009e")
	std::string name;       // TitleMeta.xbx's TitleName=, else the game list's database, else the ID
	std::string path;       // UDATA\<title ID> on the console
	std::vector<HddSave> saves;
	uint64_t bytes{ 0 };    // its saves'
	int64_t mtime{ 0 };     // its newest save's
	uint64_t dataBytes{ 0 };  // TDATA\<title ID>: the game's own data (settings, caches)
	bool alsoHere{ false };   // (a game on the sync place) the hard disk has its folder too
};

/* The games with saves (or data) on the hard disk at hddRoot, the newest played first. */
std::vector<HddTitle> hddScan(const std::string &hddRoot);
/* Bytes in the cache drives (X:, Y:, Z:) and in E: altogether. */
uint64_t hddCacheBytes(const std::string &hddRoot);
uint64_t hddUsedBytes(const std::string &hddRoot);

/* A place a save can be copied to or brought in from: the data folder's backups, a USB drive's. */
struct SavePlace {
	std::string name;       // "This console (backups)", "USB drive 0"
	std::string path;       // .../saves
};
std::vector<SavePlace> savePlaces();

/* A save found in a place, to bring in. */
struct SaveCopy {
	uint32_t titleId{ 0 };
	std::string idText, folder, name, path;
	uint64_t bytes{ 0 };
	int64_t mtime{ 0 };
	bool onDisk{ false };   // the hard disk has a save of that name already
};
std::vector<SaveCopy> saveCopiesIn(const std::string &placePath, const std::string &hddRoot);

/* The actions; false with the reason. */
bool hddDeleteSave(const HddSave &save, std::string &why);
bool hddDeleteTitle(const HddTitle &title, bool saves, bool data, const std::string &hddRoot, std::string &why);
bool hddClearCaches(const std::string &hddRoot, std::string &why);
/* A save copied out to place/<title ID>/<folder> (an older copy there replaced). */
bool hddExportSave(const HddTitle &title, const HddSave &save, const std::string &placePath, std::string &why);
/* A copy brought in to UDATA\<title ID>\<folder> (the one there replaced when overwrite). */
bool hddImportSave(const SaveCopy &copy, const std::string &hddRoot, bool overwrite, std::string &why);

/* ---- 0.0.8: the saves kept in sync with a share or a USB drive (ogbox_save_sync.h) ---- */

/* A place the saves can be kept in sync with: a network share's mount, a USB drive. */
struct SyncPlace {
	std::string name;       // "NAS" (a share), "USB drive 0"
	std::string path;       // its root ("/smb/NAS", "/mnt/usb0"): the saves in <path>/OGBox/Saves
};
std::vector<SyncPlace> syncPlaces();
/* The name of a place's path ("" when it is no place now). */
std::string syncPlaceName(const std::string &path);
/* What a sync did: its sentence (savesync::summary), whether the place answered, the saves copied. */
struct SyncOutcome {
	std::string summary;
	bool listed{ false };
	int sent{ 0 }, brought{ 0 }, failed{ 0 };
};
/* One sync of hddRoot's saves with the place, on a thread of its own (false: one is under way);
 * done is called on that thread at its end. */
bool startSaveSync(const std::string &hddRoot, const SyncPlace &place, std::function<void(const SyncOutcome &)> done);
bool saveSyncRunning();
/* A game is about to start (the hard disk becomes its): a sync under way stops before its next save,
 * and this waits for it (the save it was copying is whole). */
void saveSyncFinish();
/* OGBox 0.0.9: OGBox is closing: the same stop, without the wait (saveSyncRunning says when it ended). */
void saveSyncAskStop();

/* The saves on the sync place, deleted there too (savesync::remove, listThere). Each is a job on a
 * thread of its own, as a sync is, and never at the same time as one (false: a sync or another job
 * is under way); done is called on that thread at its end. */
struct PlaceSaves {
	bool listed{ false };   // the place answered
	std::vector<HddTitle> titles;   // laid out as hddScan's (paths there), the newest played first
};
bool startPlaceScan(const std::string &hddRoot, const SyncPlace &place, std::function<void(const PlaceSaves &)> done);
struct SaveDeletion {
	std::string idText;     // the game's folder (its title ID)
	std::string folder;     // the save's folder; "" the game's whole folder (all its saves)
	bool here{ true }, there{ true };
	std::string label;      // its name, for the log and the message
};
struct DeletionOutcome {
	bool ok{ false };
	std::string message;    // for a toast
};
bool startSaveDeletion(const std::string &hddRoot, const SyncPlace &place, const SaveDeletion &what,
	std::function<void(const DeletionOutcome &)> done);

std::string bytesText(uint64_t bytes);
std::string dateText(int64_t mtime);

/* ---- save states ---- */

constexpr int kStateSlots = 10;
constexpr int kQuitSlot = 0;   // slot 0: where the game was left (Quit, and save where I am)

struct StateSlot {
	int slot{ 0 };
	std::string path, thumb;   // the state, its picture (a BMP)
	bool used{ false };
	int64_t mtime{ 0 };
};
/* The folder of a game's states ("<data folder>/states/<title ID>.<version>"). */
std::string stateFolder(const std::string &gameKey);
StateSlot stateSlot(const std::string &gameKey, int slot);
/* The newest used slot (or -1). */
int newestStateSlot(const std::string &gameKey);

} // namespace ogbox

#endif
