/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.48: where OGBox keeps its data. The settings, the mods' choices, the network shares and
 * the game list's notes stay in /data/ogbox (small, always there); the big parts (the emulated Xbox
 * hard disk, the shader and patch caches, the covers, the screenshots) go in the data folder the
 * player chose at OGBox's first start: the console's storage, a folder of their own on it, a USB
 * drive or extended storage. Chosen again in Settings > Storage, with the data moved there.
 */
#ifndef OGBOX_STORAGE_H
#define OGBOX_STORAGE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ogbox {

constexpr const char *kConfigRoot = "/data/ogbox";   // the small files, always here

/* The data folder (default /data/ogbox), and whether the player chose one (storage.txt). */
std::string storageRoot();
bool storageChosen();
/* The data folder from now on (made if it isn't there; false with the reason). */
bool setStorageRoot(const std::string &path, std::string &why);
/* A part of the data folder: "hdd", "cache", "covers", "shots". */
std::string storagePath(const char *part);
/* The parts that move with the data folder. */
const std::vector<const char *> &storageParts();

/* A place the data folder can be: shown on the first start's screen and in Settings. */
struct StoragePlace {
	std::string name;    // "This console", "USB drive 0"
	std::string path;    // the folder the data goes in
	std::string note;    // free space, or why it can't be used
	bool usable{ true };
	bool recommended{ false };
};
std::vector<StoragePlace> storagePlaces();

/* The free space of the file system a path is on ("312 GB free"), "" when unknown. */
std::string freeSpaceText(const std::string &path);
/* How much the data folder's parts hold now (bytes). */
uint64_t storageBytes(const std::string &root);
/* A folder OGBox can write in (made with its parents); false with the reason. */
bool writableFolder(const std::string &path, std::string &why);

/* Moving the data folder's parts to another, on a thread of its own: renamed when both are on one
 * drive, else copied then deleted. The new folder is the data folder once everything is there. */
class StorageMove {
public:
	struct Progress {
		bool running{ false }, done{ false }, ok{ false };
		uint64_t bytes{ 0 }, total{ 0 };
		std::string current;   // the file being copied
		std::string error;
	};
	static StorageMove &get();
	/* Start moving from -> to. false: one is under way already. */
	bool start(const std::string &from, const std::string &to);
	Progress progress();

private:
	std::mutex mutex;
	Progress state;
};

} // namespace ogbox

#endif
