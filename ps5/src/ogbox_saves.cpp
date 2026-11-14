/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.53: the hard disk's saves and the save states (ogbox_saves.h).
 */
#include "ogbox_saves.h"

#include "ogbox_library.h"
#include "ogbox_save_sync.h"
#include "ogbox_storage.h"
#include "platform/ps5_bootlog.h"
#include "platform/vfs/ps5_vfs.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ogbox {

namespace {

bool isDir(const std::string &p)
{
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/* A child of dir by its name, case aside (the hard disk's names are the games' own spelling). */
std::string child(const std::string &dir, const char *name)
{
	if (DIR *d = opendir(dir.c_str())) {
		while (struct dirent *e = readdir(d)) {
			if (!strcasecmp(e->d_name, name)) {
				const std::string found = dir + "/" + e->d_name;
				closedir(d);
				return found;
			}
		}
		closedir(d);
	}
	return dir + "/" + name;
}

std::vector<std::string> listDir(const std::string &dir, bool dirs)
{
	std::vector<std::string> out;
	if (DIR *d = opendir(dir.c_str())) {
		while (struct dirent *e = readdir(d)) {
			if (e->d_name[0] == '.') {
				continue;
			}
			if (isDir(dir + "/" + e->d_name) == dirs) {
				out.push_back(e->d_name);
			}
		}
		closedir(d);
	}
	return out;
}

void treeInfo(const std::string &path, uint64_t &bytes, int64_t &mtime)
{
	struct stat st;
	if (lstat(path.c_str(), &st) != 0) {
		return;
	}
	mtime = std::max<int64_t>(mtime, static_cast<int64_t>(st.st_mtime));
	if (!S_ISDIR(st.st_mode)) {
		bytes += static_cast<uint64_t>(st.st_size);
		return;
	}
	if (DIR *d = opendir(path.c_str())) {
		while (struct dirent *e = readdir(d)) {
			if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) {
				continue;
			}
			treeInfo(path + "/" + e->d_name, bytes, mtime);
		}
		closedir(d);
	}
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
	if (DIR *d = opendir(path.c_str())) {
		while (struct dirent *e = readdir(d)) {
			if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) {
				continue;
			}
			ok = removeTree(path + "/" + e->d_name) && ok;
		}
		closedir(d);
	}
	return rmdir(path.c_str()) == 0 && ok;
}

bool makeFolders(const std::string &path)
{
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

bool copyFile(const std::string &from, const std::string &to, std::string &why)
{
	const int in = open(from.c_str(), O_RDONLY);
	if (in < 0) {
		why = from + ": " + std::strerror(errno);
		return false;
	}
	const int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (out < 0) {
		why = to + ": " + std::strerror(errno);
		close(in);
		return false;
	}
	char buf[1 << 16];
	bool ok = true;
	for (;;) {
		const ssize_t n = read(in, buf, sizeof(buf));
		if (n <= 0) {
			ok = n == 0;
			if (!ok) {
				why = from + ": " + std::strerror(errno);
			}
			break;
		}
		if (write(out, buf, static_cast<size_t>(n)) != n) {
			why = to + ": " + std::strerror(errno ? errno : ENOSPC);
			ok = false;
			break;
		}
	}
	close(in);
	if (close(out) != 0 && ok) {
		why = to + ": " + std::strerror(errno);
		ok = false;
	}
	return ok;
}

bool copyTree(const std::string &from, const std::string &to, std::string &why)
{
	struct stat st;
	if (lstat(from.c_str(), &st) != 0) {
		why = from + ": " + std::strerror(errno);
		return false;
	}
	if (!S_ISDIR(st.st_mode)) {
		return copyFile(from, to, why);
	}
	if (!isDir(to) && mkdir(to.c_str(), 0777) != 0) {
		why = to + ": " + std::strerror(errno);
		return false;
	}
	bool ok = true;
	if (DIR *d = opendir(from.c_str())) {
		while (struct dirent *e = readdir(d)) {
			if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) {
				continue;
			}
			if (!copyTree(from + "/" + e->d_name, to + "/" + e->d_name, why)) {
				ok = false;
				break;
			}
		}
		closedir(d);
	}
	return ok;
}

/* A value of an Xbox .xbx text file's bytes (UTF-16LE with its BOM, "Key=value" lines), as ASCII
 * (other characters '?'). */
std::string xbxValueOf(const std::string &raw, const char *key)
{
	std::string text;
	size_t i = raw.size() >= 2 && uint8_t(raw[0]) == 0xFF && uint8_t(raw[1]) == 0xFE ? 2 : 0;
	for (; i + 1 < raw.size(); i += 2) {
		const unsigned c = uint8_t(raw[i]) | uint8_t(raw[i + 1]) << 8;
		if (!c) {
			break;
		}
		text += c == '\r' || c == '\n' ? '\n' : c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
	}
	const size_t klen = std::strlen(key);
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos) {
			end = text.size();
		}
		if (end - at > klen && !strncasecmp(text.c_str() + at, key, klen) && text[at + klen] == '=') {
			std::string v = text.substr(at + klen + 1, end - at - klen - 1);
			while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
				v.pop_back();
			}
			return v;
		}
		at = end + 1;
	}
	return std::string();
}

/* The same of a file on the console (its first 4 KB). */
std::string xbxValue(const std::string &file, const char *key)
{
	FILE *f = std::fopen(file.c_str(), "rb");
	if (!f) {
		return std::string();
	}
	std::string raw(4096, '\0');
	raw.resize(std::fread(&raw[0], 1, raw.size(), f));
	std::fclose(f);
	return xbxValueOf(raw, key);
}

bool hexId(const std::string &s, uint32_t &id)
{
	if (s.size() != 8) {
		return false;
	}
	for (char c : s) {
		if (!std::isxdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	id = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
	return true;
}

std::string gameName(uint32_t id, const std::string &meta)
{
	if (!meta.empty()) {
		return meta;
	}
	if (const DiscRecord *r = GameDb::get().byTitle(id, std::string(), 1)) {
		return shortName(r->name);
	}
	char t[16];
	std::snprintf(t, sizeof(t), "%08X", id);
	return t;
}

HddSave readSave(const std::string &dir, const std::string &folder)
{
	HddSave s;
	s.folder = folder;
	s.path = dir + "/" + folder;
	s.name = xbxValue(child(s.path, "SaveMeta.xbx"), "Name");
	if (s.name.empty()) {
		s.name = folder;
	}
	treeInfo(s.path, s.bytes, s.mtime);
	return s;
}

std::string usersPartition(const std::string &hddRoot)
{
	return child(hddRoot, "Partition1");
}

} // namespace

std::vector<HddTitle> hddScan(const std::string &hddRoot)
{
	std::vector<HddTitle> titles;
	const std::string e = usersPartition(hddRoot);
	const std::string udata = child(e, "UDATA"), tdata = child(e, "TDATA");
	for (const std::string &idText : listDir(udata, true)) {
		HddTitle t;
		if (!hexId(idText, t.titleId)) {
			continue;
		}
		t.idText = idText;
		t.path = udata + "/" + idText;
		for (const std::string &folder : listDir(t.path, true)) {
			t.saves.push_back(readSave(t.path, folder));
			t.bytes += t.saves.back().bytes;
			t.mtime = std::max(t.mtime, t.saves.back().mtime);
		}
		std::sort(t.saves.begin(), t.saves.end(), [](const HddSave &a, const HddSave &b) { return a.mtime > b.mtime; });
		t.name = gameName(t.titleId, xbxValue(child(t.path, "TitleMeta.xbx"), "TitleName"));
		titles.push_back(t);
	}
	/* the games with data of their own but no save */
	for (const std::string &idText : listDir(tdata, true)) {
		uint32_t id = 0;
		if (!hexId(idText, id)) {
			continue;
		}
		auto it = std::find_if(titles.begin(), titles.end(), [&](const HddTitle &t) { return t.titleId == id; });
		uint64_t bytes = 0;
		int64_t mtime = 0;
		treeInfo(tdata + "/" + idText, bytes, mtime);
		if (it == titles.end()) {
			HddTitle t;
			t.titleId = id;
			t.idText = idText;
			t.path = udata + "/" + idText;
			t.name = gameName(id, std::string());
			t.mtime = mtime;
			t.dataBytes = bytes;
			titles.push_back(t);
		} else {
			it->dataBytes = bytes;
		}
	}
	std::sort(titles.begin(), titles.end(), [](const HddTitle &a, const HddTitle &b) {
		return a.mtime != b.mtime ? a.mtime > b.mtime : strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	return titles;
}

uint64_t hddCacheBytes(const std::string &hddRoot)
{
	uint64_t bytes = 0;
	int64_t mtime = 0;
	for (int p = 3; p <= 5; p++) {
		treeInfo(child(hddRoot, ("Partition" + std::to_string(p)).c_str()), bytes, mtime);
	}
	return bytes;
}

uint64_t hddUsedBytes(const std::string &hddRoot)
{
	uint64_t bytes = 0;
	int64_t mtime = 0;
	treeInfo(usersPartition(hddRoot), bytes, mtime);
	return bytes;
}

std::vector<SavePlace> savePlaces()
{
	std::vector<SavePlace> out;
	out.push_back({ "This console (" + storageRoot() + "/backups)", storagePath("backups") + "/saves" });
	for (int i = 0; i < 8; i++) {
		const std::string usb = "/mnt/usb" + std::to_string(i);
		struct stat st, up;
		if (stat(usb.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && stat("/mnt", &up) == 0 && st.st_dev != up.st_dev) {
			out.push_back({ "USB drive " + std::to_string(i), usb + "/OGBox/saves" });
		}
	}
	return out;
}

std::vector<SaveCopy> saveCopiesIn(const std::string &placePath, const std::string &hddRoot)
{
	std::vector<SaveCopy> out;
	std::vector<std::string> roots = { placePath };
	const std::string udata = child(placePath, "UDATA");
	if (isDir(udata)) {
		roots.push_back(udata);   // UDATA/<title ID>/<save>, as other tools copy saves out
	}
	const std::string hddUdata = child(usersPartition(hddRoot), "UDATA");
	for (const std::string &root : roots) {
		for (const std::string &idText : listDir(root, true)) {
			uint32_t id = 0;
			if (!hexId(idText, id)) {
				continue;
			}
			const std::string dir = root + "/" + idText;
			for (const std::string &folder : listDir(dir, true)) {
				const HddSave s = readSave(dir, folder);
				SaveCopy c;
				c.titleId = id;
				c.idText = idText;
				c.folder = folder;
				c.name = gameName(id, xbxValue(child(dir, "TitleMeta.xbx"), "TitleName")) + ": " + s.name;
				c.path = s.path;
				c.bytes = s.bytes;
				c.mtime = s.mtime;
				c.onDisk = isDir(child(child(hddUdata, idText.c_str()), folder.c_str()));
				out.push_back(c);
			}
		}
	}
	std::sort(out.begin(), out.end(), [](const SaveCopy &a, const SaveCopy &b) { return a.mtime > b.mtime; });
	return out;
}

bool hddDeleteSave(const HddSave &save, std::string &why)
{
	if (!removeTree(save.path)) {
		why = save.path + ": " + std::strerror(errno);
		return false;
	}
	return true;
}

bool hddDeleteTitle(const HddTitle &title, bool saves, bool data, const std::string &hddRoot, std::string &why)
{
	bool ok = true;
	if (saves && !removeTree(title.path)) {
		why = title.path + ": " + std::strerror(errno);
		ok = false;
	}
	if (data) {
		const std::string tdata = child(child(usersPartition(hddRoot), "TDATA"), title.idText.c_str());
		if (!removeTree(tdata)) {
			why = tdata + ": " + std::strerror(errno);
			ok = false;
		}
	}
	return ok;
}

bool hddClearCaches(const std::string &hddRoot, std::string &why)
{
	bool ok = true;
	for (int p = 3; p <= 5; p++) {
		const std::string part = child(hddRoot, ("Partition" + std::to_string(p)).c_str());
		for (const std::string &name : listDir(part, true)) {
			ok = removeTree(part + "/" + name) && ok;
		}
		for (const std::string &name : listDir(part, false)) {
			ok = removeTree(part + "/" + name) && ok;
		}
	}
	if (!ok) {
		why = std::string("not everything could be deleted: ") + std::strerror(errno);
	}
	return ok;
}

bool hddExportSave(const HddTitle &title, const HddSave &save, const std::string &placePath, std::string &why)
{
	const std::string dir = placePath + "/" + title.idText;
	if (!makeFolders(dir)) {
		why = dir + ": " + std::strerror(errno);
		return false;
	}
	/* the game's name with it (TitleMeta.xbx), for whoever brings it in */
	const std::string meta = child(title.path, "TitleMeta.xbx");
	struct stat st;
	if (stat(meta.c_str(), &st) == 0) {
		std::string ignored;
		copyFile(meta, dir + "/TitleMeta.xbx", ignored);
	}
	const std::string to = dir + "/" + save.folder;
	removeTree(to);
	return copyTree(save.path, to, why);
}

bool hddImportSave(const SaveCopy &copy, const std::string &hddRoot, bool overwrite, std::string &why)
{
	const std::string udata = child(usersPartition(hddRoot), "UDATA");
	const std::string dir = child(udata, copy.idText.c_str());
	if (!makeFolders(dir)) {
		why = dir + ": " + std::strerror(errno);
		return false;
	}
	const std::string to = child(dir, copy.folder.c_str());
	if (isDir(to)) {
		if (!overwrite) {
			why = "the hard disk has that save already";
			return false;
		}
		removeTree(to);
	}
	/* the game's name file, when the hard disk has none for the game yet */
	const std::string meta = child(dir, "TitleMeta.xbx");
	struct stat st;
	const std::string from = copy.path.substr(0, copy.path.find_last_of('/'));
	if (stat(meta.c_str(), &st) != 0 && stat((from + "/TitleMeta.xbx").c_str(), &st) == 0) {
		std::string ignored;
		copyFile(from + "/TitleMeta.xbx", meta, ignored);
	}
	return copyTree(copy.path, to, why);
}

/* ---- 0.0.8: the save sync's file access and its job ---- */

namespace {

bool remotePath(const std::string &p)
{
	return ps5vfs_is_virtual(p.c_str()) != 0;
}

bool syncList(const std::string &folder, std::vector<savesync::Entry> *out)
{
	if (remotePath(folder)) {
		ps5vfs_dir *d = ps5vfs_opendir(folder.c_str());
		if (!d) {
			return false;
		}
		ps5vfs_dirent e;
		while (ps5vfs_readdir(d, &e) == 1) {
			if (std::strcmp(e.name, ".") && std::strcmp(e.name, "..")) {
				out->push_back({ e.name, e.is_directory != 0, e.size > 0 ? static_cast<uint64_t>(e.size) : 0, e.mtime });
			}
		}
		ps5vfs_closedir(d);
		return true;
	}
	DIR *d = opendir(folder.c_str());
	if (!d) {
		return false;
	}
	while (struct dirent *e = readdir(d)) {
		if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) {
			continue;
		}
		struct stat st;
		if (stat((folder + "/" + e->d_name).c_str(), &st) == 0) {
			out->push_back({ e->d_name, S_ISDIR(st.st_mode), S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0,
				static_cast<int64_t>(st.st_mtime) });
		}
	}
	closedir(d);
	return true;
}

bool syncRead(const std::string &path, std::string *data)
{
	const bool remote = remotePath(path);
	FILE *f = remote ? ps5vfs_fopen(path.c_str(), "rb") : std::fopen(path.c_str(), "rb");
	if (!f) {
		return false;
	}
	data->clear();
	char buf[1 << 16];
	for (;;) {
		const size_t n = remote ? ps5vfs_fread(buf, 1, sizeof(buf), f) : std::fread(buf, 1, sizeof(buf), f);
		if (!n) {
			break;
		}
		data->append(buf, n);
	}
	const bool ok = remote ? !ps5vfs_ferror(f) : !std::ferror(f);
	remote ? ps5vfs_fclose(f) : std::fclose(f);
	return ok;
}

/* whole: a share's beside its place first (ps5vfs_store); the console's in a hidden file beside it,
 * then moved there (a save is never half written on the hard disk) */
bool syncWrite(const std::string &path, const std::string &data)
{
	if (remotePath(path)) {
		return ps5vfs_store(path.c_str(), data.data(), data.size()) == 0;
	}
	const size_t slash = path.find_last_of('/');
	const std::string tmp = slash == std::string::npos ? "." + path + ".part"
		: path.substr(0, slash + 1) + "." + path.substr(slash + 1) + ".part";
	FILE *f = std::fopen(tmp.c_str(), "wb");
	if (!f) {
		return false;
	}
	bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = std::fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = std::fclose(f) == 0 && ok;
	if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
		unlink(tmp.c_str());
		return false;
	}
	return true;
}

bool syncMakeFolder(const std::string &folder)
{
	return remotePath(folder) ? ps5vfs_make_folders(folder.c_str()) == 0 : makeFolders(folder);
}

bool syncRemove(const std::string &path)
{
	return remotePath(path) ? ps5vfs_remove(path.c_str(), 0) == 0 : unlink(path.c_str()) == 0;
}

std::atomic<bool> g_syncStop{ false };
std::mutex g_syncMutex;
std::condition_variable g_syncEnded;
bool g_syncRunning = false;   // (g_syncMutex)

bool usbPlugged(const std::string &usb)
{
	struct stat st, up;
	return stat(usb.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && stat("/mnt", &up) == 0 && st.st_dev != up.st_dev;
}

} // namespace

std::vector<SyncPlace> syncPlaces()
{
	std::vector<SyncPlace> out;
	for (unsigned i = 0; const char *mp = ps5vfs_mount_point(i); i++) {
		const std::string m = mp;
		const size_t slash = m.find_last_of('/');
		if (m.rfind("/smb/", 0) == 0 || m.rfind("/nfs/", 0) == 0) {
			out.push_back({ m.substr(slash + 1), m });
		} else if (m.rfind("/mnt/usb", 0) == 0) {
			out.push_back({ "USB drive " + m.substr(8), m });   // (a drive served through the console's FTP)
		}
	}
	for (int i = 0; i < 8; i++) {
		const std::string usb = "/mnt/usb" + std::to_string(i);
		const bool listed = std::any_of(out.begin(), out.end(), [&](const SyncPlace &p) { return p.path == usb; });
		if (!listed && usbPlugged(usb)) {
			out.push_back({ "USB drive " + std::to_string(i), usb });
		}
	}
	return out;
}

std::string syncPlaceName(const std::string &path)
{
	for (const SyncPlace &p : syncPlaces()) {
		if (p.path == path) {
			return p.name;
		}
	}
	return std::string();
}

bool saveSyncRunning()
{
	std::lock_guard<std::mutex> lock(g_syncMutex);
	return g_syncRunning;
}

void saveSyncAskStop()
{
	std::lock_guard<std::mutex> lock(g_syncMutex);
	if (g_syncRunning && !g_syncStop) {
		g_syncStop = true;
		pshakux_log("save sync: OGBox closes: the sync stops after the save it is copying");
	}
}

void saveSyncFinish()
{
	std::unique_lock<std::mutex> lock(g_syncMutex);
	if (!g_syncRunning) {
		return;
	}
	g_syncStop = true;
	pshakux_log("save sync: a game starts: the sync stops after the save it is copying");
	g_syncEnded.wait(lock, [] { return !g_syncRunning; });
}

namespace {

/* The two sides of a sync with a place, and the list of what each had at its last sync. */
savesync::Folders syncFolders(const std::string &hddRoot, const SyncPlace &place)
{
	savesync::Folders folders;
	folders.here = child(usersPartition(hddRoot), "UDATA");
	folders.there = place.path + "/OGBox/Saves/UDATA";
	folders.olderHere = storagePath("backups") + "/older saves";
	folders.olderThere = place.path + "/OGBox/Saves/.older";
	return folders;
}

/* what each side was after the last sync with this place, in a list of the console's own */
std::string recordsPath(const SyncPlace &place)
{
	uint32_t hash = 2166136261u;
	for (const unsigned char c : place.path) {
		hash = (hash ^ c) * 16777619u;
	}
	char name[48];
	std::snprintf(name, sizeof(name), "/save-sync-%08x.txt", hash);
	const std::string cache = storagePath("cache");
	makeFolders(cache);
	return cache + name;
}

bool syncRemoveFolder(const std::string &folder)
{
	return remotePath(folder) ? ps5vfs_remove(folder.c_str(), 1) == 0 : rmdir(folder.c_str()) == 0;
}

savesync::Io syncIo(const std::string &list)
{
	savesync::Io io;
	io.list = syncList;
	io.read = syncRead;
	io.write = syncWrite;
	io.makeFolder = syncMakeFolder;
	io.remove = syncRemove;
	io.removeFolder = syncRemoveFolder;
	io.stop = [] { return g_syncStop.load(); };
	io.persist = [list](const std::string &records) { syncWrite(list, records); };
	return io;
}

/* A job with the place (a sync, a listing, a deletion) on a thread of its own, one at a time; what
 * it returns is called once the next one can start (its done: the menu may start another then). */
bool startSyncJob(std::function<std::function<void()>()> job)
{
	{
		std::lock_guard<std::mutex> lock(g_syncMutex);
		if (g_syncRunning) {
			return false;
		}
		g_syncRunning = true;
		g_syncStop = false;
	}
	std::thread([job] {
		const std::function<void()> then = job();
		{
			std::lock_guard<std::mutex> lock(g_syncMutex);
			g_syncRunning = false;
		}
		g_syncEnded.notify_all();
		if (then) {
			then();
		}
	}).detach();
	return true;
}

} // namespace

bool startSaveSync(const std::string &hddRoot, const SyncPlace &place, std::function<void(const SyncOutcome &)> done)
{
	return startSyncJob([hddRoot, place, done] {
		const savesync::Folders folders = syncFolders(hddRoot, place);
		const std::string list = recordsPath(place);
		std::string text;
		syncRead(list, &text);
		savesync::Records records = savesync::parseRecords(text);
		const savesync::Io io = syncIo(list);
		pshakux_log("save sync: %s with %s", folders.here.c_str(), folders.there.c_str());
		const savesync::Result r = savesync::sync(io, folders, &records, std::time(nullptr));
		for (const std::string &line : r.lines) {
			pshakux_log("save sync: %s", line.c_str());
		}
		const std::string summary = savesync::summary(r, place.name);
		pshakux_log("save sync: %s (%d unchanged)", summary.c_str(), r.unchanged);
		const SyncOutcome o{ summary, r.listed, r.sent, r.brought, r.failed };
		return std::function<void()>([done, o] {
			if (done) {
				done(o);
			}
		});
	});
}

bool startPlaceScan(const std::string &hddRoot, const SyncPlace &place, std::function<void(const PlaceSaves &)> done)
{
	return startSyncJob([hddRoot, place, done] {
		const savesync::Folders folders = syncFolders(hddRoot, place);
		const savesync::Io io = syncIo(recordsPath(place));
		PlaceSaves out;
		std::vector<savesync::ThereTitle> there;
		out.listed = savesync::listThere(io, folders, &there);
		const auto metaValue = [](const std::string &path, const char *key) {
			std::string raw;
			if (path.empty() || !syncRead(path, &raw)) {
				return std::string();
			}
			raw.resize(std::min<size_t>(raw.size(), 4096));
			return xbxValueOf(raw, key);
		};
		for (const savesync::ThereTitle &tt : there) {
			HddTitle t;
			if (!hexId(tt.folder, t.titleId)) {
				continue;
			}
			t.idText = tt.folder;
			t.path = folders.there + "/" + tt.folder;
			t.alsoHere = tt.here;
			t.name = gameName(t.titleId, metaValue(tt.meta, "TitleName"));
			for (const savesync::ThereSave &ts : tt.saves) {
				HddSave s;
				s.folder = ts.name;
				s.path = t.path + "/" + ts.name;
				s.name = metaValue(ts.meta, "Name");
				if (s.name.empty()) {
					s.name = ts.name;
				}
				s.bytes = ts.sig.bytes;
				s.mtime = ts.sig.mtime;
				s.alsoHere = ts.here;
				t.bytes += s.bytes;
				t.mtime = std::max(t.mtime, s.mtime);
				t.saves.push_back(s);
			}
			out.titles.push_back(t);
		}
		std::sort(out.titles.begin(), out.titles.end(), [](const HddTitle &a, const HddTitle &b) {
			return a.mtime != b.mtime ? a.mtime > b.mtime : strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
		});
		pshakux_log("save sync: %s's saves: %s, %zu games", place.name.c_str(), out.listed ? "listed" : "it did not answer",
			out.titles.size());
		return std::function<void()>([done, out] {
			if (done) {
				done(out);
			}
		});
	});
}

bool startSaveDeletion(const std::string &hddRoot, const SyncPlace &place, const SaveDeletion &what,
	std::function<void(const DeletionOutcome &)> done)
{
	return startSyncJob([hddRoot, place, what, done] {
		const savesync::Folders folders = syncFolders(hddRoot, place);
		const std::string list = recordsPath(place);
		std::string text;
		syncRead(list, &text);
		savesync::Records records = savesync::parseRecords(text);
		const savesync::Io io = syncIo(list);
		const savesync::Removal r = savesync::remove(io, folders, &records, what.idText, what.folder, what.here, what.there);
		const std::string where = what.here && what.there ? "here and on " + place.name : what.there ? "on " + place.name : "here";
		DeletionOutcome o;
		o.ok = r.listed && r.ok;
		if (!r.listed) {
			o.message = "Not deleted: " + (r.why == "the place did not answer" ? place.name + " did not answer" : r.why) + ".";
		} else if (!r.ok) {
			o.message = "Not all deleted: " + r.why + ".";
		} else {
			o.message = (what.folder.empty() ? "Its saves are deleted " : "The save is deleted ") + where + ".";
		}
		pshakux_log("save sync: delete %s/%s (%s) %s: %s%s%s", what.idText.c_str(), what.folder.empty() ? "*" : what.folder.c_str(),
			what.label.c_str(), where.c_str(), o.ok ? "done" : "failed", r.why.empty() ? "" : ": ", r.why.c_str());
		return std::function<void()>([done, o] {
			if (done) {
				done(o);
			}
		});
	});
}

std::string bytesText(uint64_t bytes)
{
	char t[32];
	if (bytes >= (uint64_t(1) << 30)) {
		std::snprintf(t, sizeof(t), "%.2f GB", double(bytes) / double(uint64_t(1) << 30));
	} else if (bytes >= (uint64_t(1) << 20)) {
		std::snprintf(t, sizeof(t), "%.1f MB", double(bytes) / double(1 << 20));
	} else if (bytes >= 10240) {
		std::snprintf(t, sizeof(t), "%.0f KB", double(bytes) / 1024.0);
	} else {
		std::snprintf(t, sizeof(t), "%.1f KB", double(bytes) / 1024.0);
	}
	return t;
}

std::string dateText(int64_t mtime)
{
	if (mtime <= 0) {
		return std::string();
	}
	const time_t t = static_cast<time_t>(mtime);
	struct tm lt {};
	localtime_r(&t, &lt);
	char text[32];
	std::strftime(text, sizeof(text), "%d %b %Y %H:%M", &lt);
	return text;
}

std::string stateFolder(const std::string &gameKey)
{
	return storagePath("states") + "/" + gameKey;
}

StateSlot stateSlot(const std::string &gameKey, int slot)
{
	StateSlot s;
	s.slot = slot;
	const std::string base = stateFolder(gameKey) + (slot == kQuitSlot ? std::string("/quit") : "/slot" + std::to_string(slot));
	s.path = base + ".state";
	s.thumb = base + ".bmp";
	struct stat st;
	if (stat(s.path.c_str(), &st) == 0 && st.st_size > 0) {
		s.used = true;
		s.mtime = static_cast<int64_t>(st.st_mtime);
	}
	return s;
}

int newestStateSlot(const std::string &gameKey)
{
	int best = -1;
	int64_t when = 0;
	for (int i = 0; i <= kStateSlots; i++) {
		const StateSlot s = stateSlot(gameKey, i);
		if (s.used && s.mtime >= when) {
			best = i;
			when = s.mtime;
		}
	}
	return best;
}

} // namespace ogbox
