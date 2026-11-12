/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.48: the data folder (ogbox_storage.h).
 */
#include "ogbox_storage.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#if defined(__PROSPERO__)
#include <sys/param.h>
#include <sys/mount.h>
#else
#include <sys/statvfs.h>
#endif

#include "platform/ps5_bootlog.h"

namespace ogbox {

namespace {

#ifndef OGBOX_STORAGE_FILE
#define OGBOX_STORAGE_FILE "/data/ogbox/storage.txt"
#endif
constexpr const char *kStorageFile = OGBOX_STORAGE_FILE;

std::mutex g_rootMutex;
std::string g_root;          // read once, then as set
bool g_rootRead{ false };

std::string trim(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t\r\n");
	const size_t b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool isDir(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/* A drive plugged in at a mount folder: another device than the folder above it. */
bool mounted(const std::string &path)
{
	struct stat st, up;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && stat((path + "/..").c_str(), &up) == 0 &&
		st.st_dev != up.st_dev;
}

bool makeFolders(const std::string &path)
{
	if (path.empty()) {
		return false;
	}
	for (size_t at = 1; at <= path.size(); at++) {
		if (at == path.size() || path[at] == '/') {
			const std::string part = path.substr(0, at);
			if (!isDir(part) && mkdir(part.c_str(), 0777) != 0 && errno != EEXIST) {
				return false;
			}
		}
	}
	return isDir(path);
}

uint64_t treeBytes(const std::string &path)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0) {
		return 0;
	}
	if (!S_ISDIR(st.st_mode)) {
		return static_cast<uint64_t>(st.st_size);
	}
	uint64_t total = 0;
	if (DIR *dir = opendir(path.c_str())) {
		while (struct dirent *e = readdir(dir)) {
			if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) {
				continue;
			}
			total += treeBytes(path + "/" + e->d_name);
		}
		closedir(dir);
	}
	return total;
}

bool removeTree(const std::string &path)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0) {
		return errno == ENOENT;
	}
	if (!S_ISDIR(st.st_mode)) {
		return unlink(path.c_str()) == 0;
	}
	bool ok = true;
	if (DIR *dir = opendir(path.c_str())) {
		while (struct dirent *e = readdir(dir)) {
			if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) {
				continue;
			}
			ok = removeTree(path + "/" + e->d_name) && ok;
		}
		closedir(dir);
	}
	return rmdir(path.c_str()) == 0 && ok;
}

} // namespace

std::string storageRoot()
{
	std::lock_guard<std::mutex> lock(g_rootMutex);
	if (!g_rootRead) {
		g_rootRead = true;
		g_root = kConfigRoot;
		if (FILE *f = std::fopen(kStorageFile, "r")) {
			char line[1024];
			while (std::fgets(line, sizeof(line), f)) {
				const std::string t = trim(line);
				if (!t.empty() && t[0] == '/') {
					g_root = t;
					break;
				}
			}
			std::fclose(f);
		}
		while (g_root.size() > 1 && g_root.back() == '/') {
			g_root.pop_back();
		}
	}
	return g_root;
}

bool storageChosen()
{
	struct stat st;
	return stat(kStorageFile, &st) == 0;
}

bool setStorageRoot(const std::string &path, std::string &why)
{
	if (!writableFolder(path, why)) {
		return false;
	}
	mkdir(kConfigRoot, 0777);
	FILE *f = std::fopen(kStorageFile, "w");
	if (!f) {
		why = std::string("its note can't be written: ") + std::strerror(errno);
		return false;
	}
	std::fprintf(f, "# OGBox's data folder: the Xbox hard disk, the caches, the covers, the screenshots.\n"
		"# Chosen at OGBox's first start; change it in Settings > Storage, which moves the data too.\n%s\n",
		path.c_str());
	std::fclose(f);
	chmod(kStorageFile, 0666);
	{
		std::lock_guard<std::mutex> lock(g_rootMutex);
		g_root = path;
		g_rootRead = true;
	}
	pshakux_log("storage: the data folder is %s", path.c_str());
	return true;
}

std::string storagePath(const char *part)
{
	return storageRoot() + "/" + part;
}

const std::vector<const char *> &storageParts()
{
	/* 0.1.54: the save states, the saves copied out (0.1.53) and the trainers move with it too */
	static const std::vector<const char *> parts = { "hdd", "cache", "covers", "shots", "states", "backups", "trainers" };
	return parts;
}

std::string freeSpaceText(const std::string &path)
{
	uint64_t free = 0;
#if defined(__PROSPERO__)
	struct statfs s;
	if (statfs(path.c_str(), &s) != 0) {
		return std::string();
	}
	free = static_cast<uint64_t>(s.f_bavail) * static_cast<uint64_t>(s.f_bsize);
#else
	struct statvfs s;
	if (statvfs(path.c_str(), &s) != 0) {
		return std::string();
	}
	free = static_cast<uint64_t>(s.f_bavail) * static_cast<uint64_t>(s.f_frsize);
#endif
	char text[32];
	if (free >= (uint64_t(1) << 30)) {
		std::snprintf(text, sizeof(text), "%.0f GB free", double(free) / double(uint64_t(1) << 30));
	} else {
		std::snprintf(text, sizeof(text), "%.0f MB free", double(free) / double(1 << 20));
	}
	return text;
}

uint64_t storageBytes(const std::string &root)
{
	uint64_t total = 0;
	for (const char *part : storageParts()) {
		total += treeBytes(root + "/" + part);
	}
	return total;
}

bool writableFolder(const std::string &path, std::string &why)
{
	if (path.empty() || path[0] != '/') {
		why = "not a folder on the console";
		return false;
	}
	if (!makeFolders(path)) {
		why = std::string("it can't be made: ") + std::strerror(errno);
		return false;
	}
	const std::string probe = path + "/.ogbox-write-test";
	const int fd = open(probe.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		why = std::string("OGBox can't write there: ") + std::strerror(errno);
		return false;
	}
	const bool wrote = write(fd, "ok", 2) == 2;
	close(fd);
	unlink(probe.c_str());
	if (!wrote) {
		why = "OGBox can't write there";
		return false;
	}
	return true;
}

std::vector<StoragePlace> storagePlaces()
{
	std::vector<StoragePlace> out;
	const auto add = [&](const std::string &name, const std::string &where, const std::string &probe, bool rec) {
		StoragePlace p;
		p.name = name;
		p.path = where;
		p.recommended = rec;
		p.note = freeSpaceText(probe);
		out.push_back(p);
	};
	/* 0.0.8: a USB drive first and recommended when one is plugged in: the Xbox hard disk and the caches
	 * are written to as games play, and the console holds a title's writes to its own storage to about
	 * 2 MiB/s after a burst of a few GiB (PS5_PayloadSDK platform/docs/PROBE.md, "Sustained writes"),
	 * which stalls the game; on a USB drive it doesn't (tested against the internal drive) */
	bool usbThere = false;
	for (int i = 0; i < 8; i++) {
		const std::string usb = "/mnt/usb" + std::to_string(i);
		if (mounted(usb)) {
			add("USB drive " + std::to_string(i), usb + "/OGBox", usb, !usbThere);
			usbThere = true;
		}
	}
	add("This console", std::string(kConfigRoot), "/data", !usbThere);
	add("This console, a folder of its own", "/data/OGBox", "/data", false);
	for (int i = 0; i < 2; i++) {
		const std::string ext = "/mnt/ext" + std::to_string(i);
		if (mounted(ext)) {
			add("Extended storage " + std::to_string(i), ext + "/OGBox", ext, false);
		}
	}
	return out;
}

// ---- the move ----------------------------------------------------------------------------------

namespace {

struct Copier {
	StorageMove::Progress *state;
	std::mutex *mutex;
	std::string error;

	void say(const std::string &file, uint64_t add)
	{
		std::lock_guard<std::mutex> lock(*mutex);
		state->bytes += add;
		if (!file.empty()) {
			state->current = file;
		}
	}

	bool copyFile(const std::string &from, const std::string &to, const std::string &shown)
	{
		const int in = open(from.c_str(), O_RDONLY);
		if (in < 0) {
			error = from + ": " + std::strerror(errno);
			return false;
		}
		const int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (out < 0) {
			error = to + ": " + std::strerror(errno);
			close(in);
			return false;
		}
		say(shown, 0);
		static thread_local std::vector<char> buffer(4 << 20);
		bool ok = true;
		for (;;) {
			const ssize_t n = read(in, buffer.data(), buffer.size());
			if (n < 0) {
				error = from + ": " + std::strerror(errno);
				ok = false;
				break;
			}
			if (n == 0) {
				break;
			}
			ssize_t done = 0;
			while (done < n) {
				const ssize_t w = write(out, buffer.data() + done, static_cast<size_t>(n - done));
				if (w <= 0) {
					error = to + ": " + std::strerror(w < 0 ? errno : ENOSPC);
					ok = false;
					break;
				}
				done += w;
			}
			if (!ok) {
				break;
			}
			say(std::string(), static_cast<uint64_t>(n));
		}
		close(in);
		if (close(out) != 0 && ok) {
			error = to + ": " + std::strerror(errno);
			ok = false;
		}
		return ok;
	}

	bool copyTree(const std::string &from, const std::string &to, const std::string &shown)
	{
		struct stat st;
		if (lstat(from.c_str(), &st) != 0) {
			return errno == ENOENT;
		}
		if (!S_ISDIR(st.st_mode)) {
			return S_ISREG(st.st_mode) ? copyFile(from, to, shown) : true;
		}
		if (!isDir(to) && mkdir(to.c_str(), 0777) != 0) {
			error = to + ": " + std::strerror(errno);
			return false;
		}
		DIR *dir = opendir(from.c_str());
		if (!dir) {
			error = from + ": " + std::strerror(errno);
			return false;
		}
		bool ok = true;
		while (struct dirent *e = readdir(dir)) {
			if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) {
				continue;
			}
			if (!copyTree(from + "/" + e->d_name, to + "/" + e->d_name, shown + "/" + e->d_name)) {
				ok = false;
				break;
			}
		}
		closedir(dir);
		return ok;
	}
};

} // namespace

StorageMove &StorageMove::get()
{
	static StorageMove *move = new StorageMove();   // (never destroyed: its thread may outlive main)
	return *move;
}

StorageMove::Progress StorageMove::progress()
{
	std::lock_guard<std::mutex> lock(mutex);
	return state;
}

bool StorageMove::start(const std::string &from, const std::string &to)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (state.running) {
			return false;
		}
		state = Progress{};
		state.running = true;
	}
	std::thread([this, from, to]() {
		std::string why;
		bool ok = writableFolder(to, why);
		uint64_t total = 0;
		if (ok) {
			total = storageBytes(from);
			std::lock_guard<std::mutex> lock(mutex);
			state.total = total;
		}
		pshakux_log("storage: moving the data folder's %llu MB from %s to %s", static_cast<unsigned long long>(total >> 20),
			from.c_str(), to.c_str());
		std::vector<std::string> copied;   // deleted from `from` once everything is there
		std::vector<std::pair<std::string, std::string>> renamed;   // (to, from): put back if the move stops
		Copier c{ &state, &mutex, std::string() };
		for (const char *part : storageParts()) {
			if (!ok) {
				break;
			}
			const std::string src = from + "/" + part, dst = to + "/" + part;
			struct stat st;
			if (lstat(src.c_str(), &st) != 0) {
				continue;   // nothing of that part yet
			}
			if (lstat(dst.c_str(), &st) == 0) {
				/* the new folder had one already (OGBox's data from before): the moved one wins, the
				 * old one kept beside it */
				const std::string aside = dst + ".old-" + std::to_string(static_cast<long long>(time(nullptr)));
				rename(dst.c_str(), aside.c_str());
			}
			if (rename(src.c_str(), dst.c_str()) == 0) {
				const uint64_t b = treeBytes(dst);
				c.say(part, b);
				renamed.emplace_back(dst, src);
				pshakux_log("storage: %s renamed into place", part);
				continue;
			}
			if (errno != EXDEV) {
				pshakux_log("storage: %s can't be renamed (%s): copied", part, std::strerror(errno));
			}
			ok = c.copyTree(src, dst, part);
			if (ok) {
				copied.push_back(src);
			} else {
				why = c.error;
			}
		}
		if (ok) {
			ok = setStorageRoot(to, why);
		}
		if (ok) {
			for (const std::string &src : copied) {
				if (!removeTree(src)) {
					pshakux_log("storage: %s was copied but not all of it could be deleted", src.c_str());
				}
			}
			pshakux_log("storage: the data folder is now %s", to.c_str());
		} else {
			for (const auto &r : renamed) {
				rename(r.first.c_str(), r.second.c_str());   // what was renamed, back where it was
			}
			pshakux_log("storage: the move stopped: %s; the data folder stays %s (what was copied is left in %s)",
				why.c_str(), from.c_str(), to.c_str());
		}
		std::lock_guard<std::mutex> lock(mutex);
		state.running = false;
		state.done = true;
		state.ok = ok;
		state.error = why;
	}).detach();
	return true;
}

} // namespace ogbox
