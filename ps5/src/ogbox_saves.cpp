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
#include "ogbox_storage.h"

#include <algorithm>
#include <cctype>
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

/* A value of an Xbox .xbx text file (UTF-16LE with its BOM, "Key=value" lines), as ASCII
 * (other characters '?'). */
std::string xbxValue(const std::string &file, const char *key)
{
	FILE *f = std::fopen(file.c_str(), "rb");
	if (!f) {
		return std::string();
	}
	std::vector<uint8_t> raw(4096);
	raw.resize(std::fread(raw.data(), 1, raw.size(), f));
	std::fclose(f);
	std::string text;
	size_t i = raw.size() >= 2 && raw[0] == 0xFF && raw[1] == 0xFE ? 2 : 0;
	for (; i + 1 < raw.size(); i += 2) {
		const unsigned c = raw[i] | raw[i + 1] << 8;
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
