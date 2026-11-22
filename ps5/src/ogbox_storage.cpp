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
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <sys/time.h>
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
		return S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;   // (what the move copies)
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

/* The folder the note's path names, from its text (its first line that is a path). */
std::string noteRoot(FILE *f)
{
	std::string root;
	char line[1024];
	while (std::fgets(line, sizeof(line), f)) {
		const std::string t = trim(line);
		if (!t.empty() && t[0] == '/') {
			root = t;
			break;
		}
	}
	while (root.size() > 1 && root.back() == '/') {
		root.pop_back();
	}
	return root;
}

/* 1.0.3: a path the note can't give back as it is (spaces around it, a line break in it): refused
 * before anything is written or moved */
bool notedBadly(const std::string &path, std::string &why)
{
	if (trim(path) != path || path.find_first_of("\r\n") != std::string::npos) {
		why = "its name begins or ends with a space or holds a line break";
		return true;
	}
	return false;
}

} // namespace

std::string storageRoot()
{
	std::lock_guard<std::mutex> lock(g_rootMutex);
	if (!g_rootRead) {
		g_rootRead = true;
		g_root = kConfigRoot;
		if (FILE *f = std::fopen(kStorageFile, "r")) {
			const std::string noted = noteRoot(f);
			std::fclose(f);
			if (!noted.empty()) {
				g_root = noted;
			}
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
	if (notedBadly(path, why) || !writableFolder(path, why)) {
		return false;
	}
	const std::string note = kStorageFile;
	mkdir(note.substr(0, note.find_last_of('/')).c_str(), 0777);   // (kConfigRoot)
	/* the note as it was, put back if the new one doesn't read back */
	std::string before;
	bool hadNote = false;
	if (FILE *f = std::fopen(note.c_str(), "rb")) {
		char buf[4096];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
			before.append(buf, n);
		}
		hadNote = !std::ferror(f);
		std::fclose(f);
		if (!hadNote) {
			why = std::string("its note can't be read: ") + std::strerror(errno ? errno : EIO);
			return false;
		}
	} else if (errno != ENOENT) {
		why = std::string("its note can't be read: ") + std::strerror(errno);
		return false;
	}
	/* 1.0.3: written beside it, flushed, put in its place and read back: a move deletes the old copy
	 * only once the note names the new folder (a note left half written named no folder, and the
	 * next start used /data/ogbox with the data deleted from where it was) */
	const std::string text = "# OGBox's data folder: the Xbox hard disk, the caches, the covers, the screenshots.\n"
		"# Chosen at OGBox's first start; change it in Settings > Storage, which moves the data too.\n" + path + "\n";
	const std::string tmp = note + ".new";
	const auto place = [&](const std::string &body) {
		const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0) {
			return false;
		}
		size_t at = 0;
		while (at < body.size()) {
			const ssize_t w = write(fd, body.data() + at, body.size() - at);
			if (w <= 0) {
				break;
			}
			at += static_cast<size_t>(w);
		}
		bool ok = at == body.size();
		ok = fsync(fd) == 0 && ok;
		ok = close(fd) == 0 && ok;
		if (!ok || std::rename(tmp.c_str(), note.c_str()) != 0) {
			const int e = errno;
			unlink(tmp.c_str());
			errno = e;
			return false;
		}
		return true;
	};
	errno = 0;
	if (!place(text)) {
		why = std::string("its note can't be written: ") + std::strerror(errno ? errno : EIO);
		return false;
	}
	std::string noted;
	if (FILE *f = std::fopen(note.c_str(), "r")) {
		noted = noteRoot(f);
		std::fclose(f);
	}
	std::string want = path;
	while (want.size() > 1 && want.back() == '/') {
		want.pop_back();
	}
	if (noted != want) {
		/* the note as it was put back (the move puts the data back: the next start uses it where it was) */
		why = "its note can't be written: it doesn't read back";
		if (hadNote ? !place(before) : unlink(note.c_str()) != 0) {
			pshakux_log("storage: the note as it was can't be put back: %s", std::strerror(errno));
		}
		return false;
	}
	chmod(kStorageFile, 0666);
	{
		std::lock_guard<std::mutex> lock(g_rootMutex);
		g_root = want;
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
	/* 0.1.54: the save states, the saves copied out (0.1.53) and the trainers move with it too;
	 * 1.0.2: the texture packs and dumps, and the console dumps saves are re-signed with (ogbox_resign.h) */
	static const std::vector<const char *> parts = { "hdd", "cache", "covers", "shots", "states", "backups", "trainers", "textures",
		"console" };
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
	StorageMove::Progress *state{ nullptr };
	std::mutex *mutex{ nullptr };
	std::string error;
	uint64_t copied{ 0 };          // the bytes of the parts copied (what the old copy's deletion goes through)
	std::set<std::string> kept;    // 1.0.3: what isn't a file or a folder (a link made by hand): not copied, left where it was
	uint64_t left{ 0 };            // what of the old copy could not be deleted
	std::string leftFirst;

	void say(const std::string &file, uint64_t add)
	{
		std::lock_guard<std::mutex> lock(*mutex);
		state->bytes += add;
		if (!file.empty()) {
			state->current = file;
		}
	}

	bool copyFile(const std::string &from, const std::string &to, const std::string &shown, const struct stat &src)
	{
		/* 1.0.3: a move begun again (one stopped, or cut off by the console turned off) goes on: a file
		 * the earlier one copied whole (its size and date as here; the date is set once it is whole)
		 * is not copied again */
		struct stat there;
		if (lstat(to.c_str(), &there) == 0 && S_ISREG(there.st_mode) && there.st_size == src.st_size &&
			there.st_mtime == src.st_mtime) {
			say(shown, static_cast<uint64_t>(src.st_size));
			copied += static_cast<uint64_t>(src.st_size);
			return true;
		}
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
			copied += static_cast<uint64_t>(n);
		}
		close(in);
		if (close(out) != 0 && ok) {
			error = to + ": " + std::strerror(errno);
			ok = false;
		}
		if (ok) {
			struct timeval times[2] = { { src.st_atime, 0 }, { src.st_mtime, 0 } };
			utimes(to.c_str(), times);   // (a failure: the next move copies it again)
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
			if (S_ISREG(st.st_mode)) {
				return copyFile(from, to, shown, st);
			}
			kept.insert(from);   // (deleted with the old copy, it was lost)
			return true;
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

	/* 1.0.3: the old copy deleted, file by file, with its progress (it stood at the last file copied
	 * till it was done); what wasn't copied stays, and what can't be deleted is counted */
	void removeTree(const std::string &path, const std::string &shown)
	{
		struct stat st;
		if (kept.count(path) || lstat(path.c_str(), &st) != 0) {
			return;
		}
		if (!S_ISDIR(st.st_mode)) {
			if (unlink(path.c_str()) == 0) {
				say(shown, static_cast<uint64_t>(S_ISREG(st.st_mode) ? st.st_size : 0));
			} else {
				noteLeft(path);
			}
			return;
		}
		if (DIR *dir = opendir(path.c_str())) {
			while (struct dirent *e = readdir(dir)) {
				if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) {
					continue;
				}
				removeTree(path + "/" + e->d_name, shown + "/" + e->d_name);
			}
			closedir(dir);
		}
		if (rmdir(path.c_str()) != 0 && !keptBelow(path)) {
			noteLeft(path);
		}
	}

	bool keptBelow(const std::string &dir) const
	{
		const auto it = kept.lower_bound(dir + "/");
		return it != kept.end() && it->compare(0, dir.size() + 1, dir + "/") == 0;
	}

	void noteLeft(const std::string &path)
	{
		if (!left++) {
			leftFirst = path + " (" + std::strerror(errno) + ")";
		}
	}

	/* 1.0.3: a copy gone on with (begun again) made like the data folder's: what it holds that the data
	 * folder no longer has (a save deleted since the earlier move) is deleted from it (it came back) */
	bool prune(const std::string &from, const std::string &to)
	{
		struct stat st;
		if (lstat(to.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
			return true;
		}
		DIR *dir = opendir(to.c_str());
		if (!dir) {
			error = to + ": " + std::strerror(errno);
			return false;
		}
		std::vector<std::string> names;
		while (struct dirent *e = readdir(dir)) {
			if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..")) {
				names.push_back(e->d_name);
			}
		}
		closedir(dir);
		bool ok = true;
		for (const std::string &name : names) {
			struct stat src;
			const std::string f = from + "/" + name, t = to + "/" + name;
			if (lstat(f.c_str(), &src) == 0) {
				ok = (!S_ISDIR(src.st_mode) || prune(f, t)) && ok;
			} else if (errno != ENOENT) {
				error = f + ": " + std::strerror(errno);
				ok = false;
			} else if (!removeAll(t)) {
				error = t + ": " + std::strerror(errno);
				ok = false;
			}
		}
		return ok;
	}

	static bool removeAll(const std::string &path)
	{
		struct stat st;
		if (lstat(path.c_str(), &st) != 0) {
			return errno == ENOENT;
		}
		if (S_ISDIR(st.st_mode)) {
			if (DIR *dir = opendir(path.c_str())) {
				while (struct dirent *e = readdir(dir)) {
					if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..")) {
						removeAll(path + "/" + e->d_name);
					}
				}
				closedir(dir);
			}
			return rmdir(path.c_str()) == 0;
		}
		return unlink(path.c_str()) == 0;
	}
};

/* 1.0.3: a folder inside another (or the same), by their resolved paths */
std::string resolved(const std::string &path)
{
	char buf[PATH_MAX];
	return realpath(path.c_str(), buf) ? std::string(buf) : path;
}

bool within(const std::string &path, const std::string &folder)
{
	return path == folder || (path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/');
}

/* A move that can't be made: into the data folder itself, into one of its parts (the copy went on into
 * itself), or onto a folder whose parts hold the data folder. */
bool refusedMove(const std::string &from, const std::string &to, std::string &why)
{
	const std::string f = resolved(from), t = resolved(to);
	if (f == t) {
		why = "it is the data folder already";
		return true;
	}
	for (const char *part : storageParts()) {
		if (within(t, resolved(from + "/" + part))) {
			why = "it is inside the data folder's own \"" + std::string(part) + "\" folder";
			return true;
		}
		if (within(f, t + "/" + part)) {
			why = "the data folder is inside its \"" + std::string(part) + "\" folder";
			return true;
		}
	}
	return false;
}

/* 1.0.3: a part copied into the new folder is marked there (<part>.copying) until the move is done: a
 * move begun again goes on with it rather than setting it aside as data from before */
std::string copyingMark(const std::string &dst)
{
	return dst + ".copying";
}

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
		state.from = from;
	}
	try {
		std::thread([this, from, to]() {
			std::string why, note;
			bool ok = false;
			try {
				ok = run(from, to, why, note);
			} catch (...) {   // (1.0.3: it ends with a reason, never with the screen left at "Moving")
				ok = false;
				why = "the move stopped unexpectedly";
				pshakux_log("storage: the move stopped unexpectedly");
			}
			std::lock_guard<std::mutex> lock(mutex);
			state.running = false;
			state.done = true;
			state.ok = ok;
			state.error = why;
			state.note = note;
		}).detach();
	} catch (...) {
		std::lock_guard<std::mutex> lock(mutex);
		state.running = false;
		state.done = true;
		state.error = "the move could not be started";
		return true;   // (started, and ended: the screen says why)
	}
	return true;
}

bool StorageMove::run(const std::string &from, const std::string &to, std::string &why, std::string &note)
{
	bool ok = !notedBadly(to, why) && writableFolder(to, why);
	if (ok && refusedMove(from, to, why)) {
		ok = false;
	}
	uint64_t total = 0;
	if (ok) {
		total = storageBytes(from);
		std::lock_guard<std::mutex> lock(mutex);
		state.total = total;
	}
	pshakux_log("storage: moving the data folder's %llu MB from %s to %s", static_cast<unsigned long long>(total >> 20),
		from.c_str(), to.c_str());
	std::vector<std::pair<std::string, std::string>> copied;   // (from, the part): deleted from `from` once everything is there
	std::vector<std::pair<std::string, std::string>> renamed;   // (to, from): put back if the move stops
	Copier c;
	c.state = &state;
	c.mutex = &mutex;
	for (const char *part : storageParts()) {
		if (!ok) {
			break;
		}
		const std::string src = from + "/" + part, dst = to + "/" + part, mark = copyingMark(dst);
		struct stat st;
		if (lstat(src.c_str(), &st) != 0) {
			continue;   // nothing of that part yet
		}
		const bool resuming = lstat(mark.c_str(), &st) == 0;
		if (resuming) {
			pshakux_log("storage: %s: the copy an earlier move began goes on", part);
		} else if (lstat(dst.c_str(), &st) == 0) {
			/* the new folder had one already (OGBox's data from before): the moved one wins, the
			 * old one kept beside it */
			const std::string aside = dst + ".old-" + std::to_string(static_cast<long long>(time(nullptr)));
			rename(dst.c_str(), aside.c_str());
		}
		if (!resuming && rename(src.c_str(), dst.c_str()) == 0) {
			const uint64_t b = treeBytes(dst);
			c.say(part, b);
			renamed.emplace_back(dst, src);
			pshakux_log("storage: %s renamed into place", part);
			continue;
		}
		if (!resuming && errno != EXDEV) {
			pshakux_log("storage: %s can't be renamed (%s): copied", part, std::strerror(errno));
		}
		const int fd = open(mark.c_str(), O_WRONLY | O_CREAT, 0666);
		if (fd < 0) {
			why = mark + ": " + std::strerror(errno);
			ok = false;
			break;
		}
		close(fd);
		ok = c.copyTree(src, dst, part) && (!resuming || c.prune(src, dst));
		if (ok) {
			copied.emplace_back(src, part);
		} else {
			why = c.error;
		}
	}
	if (ok) {
		ok = setStorageRoot(to, why);
	}
	if (!ok) {
		for (const auto &r : renamed) {
			rename(r.first.c_str(), r.second.c_str());   // what was renamed, back where it was
		}
		pshakux_log("storage: the move stopped: %s; the data folder stays %s (what was copied is left in %s)", why.c_str(),
			from.c_str(), to.c_str());
		return false;
	}
	for (const auto &p : copied) {
		unlink(copyingMark(to + "/" + p.second).c_str());
	}
	if (!copied.empty()) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			state.removing = true;
			state.bytes = 0;
			state.total = c.copied;
			state.current.clear();
		}
		pshakux_log("storage: deleting the old copy in %s (%llu MB)", from.c_str(), static_cast<unsigned long long>(c.copied >> 20));
		for (const auto &p : copied) {
			c.removeTree(p.first, p.second);
		}
	}
	if (c.left) {
		note = "Not all of the old copy in " + from + " could be deleted (" + c.leftFirst + "); you can delete it yourself.";
		pshakux_log("storage: %llu things of the old copy could not be deleted, the first %s", static_cast<unsigned long long>(c.left),
			c.leftFirst.c_str());
	}
	if (!c.kept.empty()) {
		note += std::string(note.empty() ? "" : " ") + std::to_string(c.kept.size()) +
			(c.kept.size() == 1 ? " link (not a file) was" : " links (not files) were") + " left in " + from + ": " + *c.kept.begin() +
			(c.kept.size() > 1 ? " and others." : ".");
		pshakux_log("storage: %zu links or other entries that aren't files were not moved, the first %s", c.kept.size(),
			c.kept.begin()->c_str());
	}
	pshakux_log("storage: the data folder is now %s", to.c_str());
	return true;
}

} // namespace ogbox
