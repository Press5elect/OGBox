/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.8: the saves kept the same on both sides: the engine (ogbox_save_sync.h).
 */
#include "ogbox_save_sync.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

namespace ogbox {
namespace savesync {

namespace {

std::string join(const std::string &folder, const std::string &name)
{
	if (folder.empty()) {
		return name;
	}
	return folder.back() == '/' ? folder + name : folder + "/" + name;
}

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

std::string parentOf(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string nameOf(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

/* (a name of the copies' own: a share's file being written beside its place, a folder put away) */
bool hidden(const std::string &name)
{
	return name.empty() || name[0] == '.';
}

/* A folder's files (their paths below it) and what they add up to; false when it isn't there. */
bool collect(const Io &io, const std::string &folder, const std::string &rel, std::vector<std::string> *files, Sig *sig,
	int depth)
{
	std::vector<Entry> entries;
	if (!io.list(folder, &entries)) {
		return false;
	}
	for (const Entry &e : entries) {
		if (hidden(e.name)) {
			continue;
		}
		const std::string r = rel.empty() ? e.name : rel + "/" + e.name;
		if (e.dir) {
			if (depth < 8) {
				collect(io, join(folder, e.name), r, files, sig, depth + 1);
			}
			continue;
		}
		files->push_back(r);
		sig->files++;
		sig->bytes += e.size;
		sig->mtime = std::max(sig->mtime, e.mtime);
	}
	return true;
}

/* What a save (a folder) or a game's file is now. */
Sig sigAt(const Io &io, const std::string &path, bool dir)
{
	Sig g;
	if (dir) {
		std::vector<std::string> files;
		g.exists = collect(io, path, std::string(), &files, &g, 0);
		return g;
	}
	std::vector<Entry> entries;
	if (!io.list(parentOf(path), &entries)) {
		return g;
	}
	const std::string want = lower(nameOf(path));
	for (const Entry &e : entries) {
		if (!e.dir && lower(e.name) == want) {
			g.exists = true;
			g.files = 1;
			g.bytes = e.size;
			g.mtime = e.mtime;
			break;
		}
	}
	return g;
}

/* "AB12CD34EF56" put away at a moment: "AB12CD34EF56 2026-11-10_19-42-07" (a file's before its
 * extension: "TitleMeta 2026-11-10_19-42-07.xbx") */
std::string awayName(const std::string &name, bool dir, time_t now)
{
	struct tm local = {};
	localtime_r(&now, &local);
	char when[32];
	std::strftime(when, sizeof(when), "%Y-%m-%d_%H-%M-%S", &local);
	const size_t dot = dir ? std::string::npos : name.rfind('.');
	const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
	const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
	return stem + " " + when + ext;
}

/* A save copied: a file, or a folder's files; mirror: then the destination's files the source
 * doesn't have taken away (the destination becomes the source). */
bool copyItem(const Io &io, const std::string &from, const std::string &to, bool dir, bool mirror)
{
	if (!dir) {
		std::string data;
		io.makeFolder(parentOf(to));
		return io.read(from, &data) && io.write(to, data);
	}
	std::vector<std::string> files;
	Sig g;
	if (!collect(io, from, std::string(), &files, &g, 0) || !io.makeFolder(to)) {
		return false;
	}
	std::set<std::string> kept;
	for (const std::string &rel : files) {
		const size_t slash = rel.rfind('/');
		if (slash != std::string::npos && !io.makeFolder(join(to, rel.substr(0, slash)))) {
			return false;
		}
		std::string data;
		if (!io.read(join(from, rel), &data) || !io.write(join(to, rel), data)) {
			return false;
		}
		kept.insert(lower(rel));
	}
	if (mirror) {
		std::vector<std::string> old;
		Sig og;
		if (collect(io, to, std::string(), &old, &og, 0)) {
			for (const std::string &rel : old) {
				if (!kept.count(lower(rel))) {
					io.remove(join(to, rel));
				}
			}
		}
	}
	return true;
}

/* The same files with the same bytes (names compared as the Xbox does, case aside). */
bool sameContent(const Io &io, const std::string &a, const std::string &b, bool dir)
{
	if (!dir) {
		std::string x, y;
		return io.read(a, &x) && io.read(b, &y) && x == y;
	}
	std::vector<std::string> fa, fb;
	Sig ga, gb;
	if (!collect(io, a, std::string(), &fa, &ga, 0) || !collect(io, b, std::string(), &fb, &gb, 0) || fa.size() != fb.size() ||
		ga.bytes != gb.bytes) {
		return false;
	}
	std::map<std::string, std::string> byName;
	for (const std::string &rel : fb) {
		byName[lower(rel)] = rel;
	}
	for (const std::string &rel : fa) {
		const auto other = byName.find(lower(rel));
		std::string x, y;
		if (other == byName.end() || !io.read(join(a, rel), &x) || !io.read(join(b, other->second), &y) || x != y) {
			return false;
		}
	}
	return true;
}

struct Side {
	std::string name;
	bool dir{ false };
	bool exists{ false };
};

struct Item {
	std::string key, titleId;
	std::string hereTitle, thereTitle;   // the game's folder on each side (made when a save goes there)
	Side here, there;
};

} // namespace

bool isTitleFolder(const std::string &name)
{
	if (name.size() != 8) {
		return false;
	}
	for (const char c : name) {
		if (!std::isxdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	return true;
}

std::string formatRecords(const Records &records)
{
	std::string out = "# OGBox: the saves as they were after the last sync (files, bytes and newest time here, then "
		"there; a copy under way)\n";
	for (const auto &[key, r] : records) {
		char line[192];
		std::snprintf(line, sizeof(line), "\t%u\t%" PRIu64 "\t%" PRId64 "\t%u\t%" PRIu64 "\t%" PRId64 "\t%c\n", r.here.files,
			r.here.bytes, r.here.mtime, r.there.files, r.there.bytes, r.there.mtime, r.pending ? r.pending : '-');
		out += key;
		out += line;
	}
	return out;
}

Records parseRecords(const std::string &text)
{
	Records records;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::vector<std::string> f;
		size_t start = 0;
		for (;;) {
			const size_t tab = line.find('\t', start);
			f.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
			if (tab == std::string::npos) {
				break;
			}
			start = tab + 1;
		}
		if (f.size() != 8 || f[0].empty()) {
			continue;
		}
		Record r;
		r.here.files = static_cast<uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10));
		r.here.bytes = std::strtoull(f[2].c_str(), nullptr, 10);
		r.here.mtime = std::strtoll(f[3].c_str(), nullptr, 10);
		r.there.files = static_cast<uint32_t>(std::strtoul(f[4].c_str(), nullptr, 10));
		r.there.bytes = std::strtoull(f[5].c_str(), nullptr, 10);
		r.there.mtime = std::strtoll(f[6].c_str(), nullptr, 10);
		r.here.exists = r.there.exists = true;
		r.pending = f[7] == "S" ? 'S' : f[7] == "B" ? 'B' : 0;
		records[f[0]] = r;
	}
	return records;
}

Result sync(const Io &io, const Folders &folders, Records *records, time_t now)
{
	Result result;
	std::vector<Entry> hereTitles, thereTitles;
	io.makeFolder(folders.there);
	if (!io.list(folders.there, &thereTitles)) {
		return result;
	}
	result.listed = true;
	io.list(folders.here, &hereTitles);   // (none: a hard disk no game has saved on yet)

	/* the games on either side, by their title ID (either case), then their saves and files */
	std::map<std::string, std::pair<std::string, std::string>> titles;
	for (const Entry &e : hereTitles) {
		if (e.dir && isTitleFolder(e.name)) {
			titles[lower(e.name)].first = e.name;
		}
	}
	for (const Entry &e : thereTitles) {
		if (e.dir && isTitleFolder(e.name)) {
			titles[lower(e.name)].second = e.name;
		}
	}
	std::vector<Item> items;
	for (const auto &[id, names] : titles) {
		const std::string &any = names.first.empty() ? names.second : names.first;
		const std::string hereTitle = join(folders.here, names.first.empty() ? any : names.first);
		const std::string thereTitle = join(folders.there, names.second.empty() ? any : names.second);
		std::map<std::string, Item> byKey;
		const auto add = [&](const std::string &folder, bool here) {
			std::vector<Entry> entries;
			if (!io.list(folder, &entries)) {
				return;
			}
			for (const Entry &e : entries) {
				if (hidden(e.name)) {
					continue;
				}
				Item &it = byKey[id + "/" + lower(e.name)];
				it.key = id + "/" + lower(e.name);
				it.titleId = any;
				it.hereTitle = hereTitle;
				it.thereTitle = thereTitle;
				(here ? it.here : it.there) = Side{ e.name, e.dir, true };
			}
		};
		if (!names.first.empty()) {
			add(hereTitle, true);
		}
		if (!names.second.empty()) {
			add(thereTitle, false);
		}
		for (auto &[key, it] : byKey) {
			items.push_back(std::move(it));
		}
	}

	const auto keep = [&] {
		if (io.persist) {
			io.persist(formatRecords(*records));
		}
	};
	for (const Item &it : items) {
		if (io.stop && io.stop()) {
			result.stopped = true;
			break;
		}
		const std::string &name = it.here.exists ? it.here.name : it.there.name;
		const std::string label = it.titleId + "/" + name;
		if (it.here.exists && it.there.exists && it.here.dir != it.there.dir) {
			result.failed++;
			result.lines.push_back(label + ": a folder on one side and a file on the other: left as they are");
			continue;
		}
		const bool dir = it.here.exists ? it.here.dir : it.there.dir;
		const std::string hereP = join(it.hereTitle, it.here.exists ? it.here.name : name);
		const std::string thereP = join(it.thereTitle, it.there.exists ? it.there.name : name);
		const Sig h = it.here.exists ? sigAt(io, hereP, dir) : Sig{}, t = it.there.exists ? sigAt(io, thereP, dir) : Sig{};
		const auto found = records->find(it.key);
		const Record *rec = found == records->end() ? nullptr : &found->second;

		const auto remember = [&] {
			Record r;
			r.here = sigAt(io, hereP, dir);
			r.there = sigAt(io, thereP, dir);
			(*records)[it.key] = r;
		};
		/* a copy, marked in the list while it is under way */
		const auto copy = [&](bool toThere, bool mirror) {
			Record &mark = (*records)[it.key];
			mark.pending = toThere ? 'S' : 'B';
			keep();
			if (!copyItem(io, toThere ? hereP : thereP, toThere ? thereP : hereP, dir, mirror)) {
				return false;
			}
			remember();
			keep();
			return true;
		};
		const auto count = [&](bool ok, bool toThere, const char *why) {
			if (ok) {
				(toThere ? result.sent : result.brought)++;
				result.lines.push_back(label + (toThere ? ": sent (" : ": brought here (") + why + ")");
			} else {
				result.failed++;
				result.lines.push_back(label + (toThere ? ": could not be sent" : ": could not be brought here"));
			}
		};

		/* a copy cut off at the last sync: made again, from the side it was made from */
		if (rec && rec->pending && (rec->pending == 'S' ? h.exists : t.exists)) {
			const bool toThere = rec->pending == 'S';
			count(copy(toThere, true), toThere, "again: cut off at the last sync");
			continue;
		}
		if (h.exists && !t.exists) {
			count(copy(true, false), true, "not there");
			continue;
		}
		if (!h.exists && t.exists) {
			count(copy(false, false), false, "not here");
			continue;
		}
		if (!h.exists && !t.exists) {
			continue;
		}
		const bool hereChanged = !rec || !h.same(rec->here);
		const bool thereChanged = !rec || !t.same(rec->there);
		if (!rec && sameContent(io, hereP, thereP, dir)) {
			/* never synced: the same files need nothing but remembering */
			result.unchanged++;
			remember();
			continue;
		}
		if (!hereChanged && !thereChanged) {
			result.unchanged++;
			continue;
		}
		if (hereChanged != thereChanged) {
			count(copy(hereChanged, true), hereChanged, hereChanged ? "changed here" : "changed there");
			continue;
		}
		/* changed on both sides: the newer is kept on both, the other put away on its side first */
		const bool hereWins = h.mtime >= t.mtime;
		const std::string &loser = hereWins ? thereP : hereP;
		const std::string older = join(join(hereWins ? folders.olderThere : folders.olderHere, it.titleId), awayName(name, dir, now));
		io.makeFolder(parentOf(older));
		if (!copyItem(io, loser, older, dir, false)) {
			result.failed++;
			result.lines.push_back(label + ": changed on both sides, and the older could not be put away: left as it is");
			continue;
		}
		if (copy(hereWins, true)) {
			result.bothChanged++;
			(hereWins ? result.sent : result.brought)++;
			result.lines.push_back(label + (hereWins ? ": changed on both sides; the console's is newer and was sent, the other put away"
													 : ": changed on both sides; the other side's is newer and was brought here, the console's put away"));
		} else {
			result.failed++;
			result.lines.push_back(label + ": changed on both sides, and the newer could not be copied");
		}
	}
	keep();
	return result;
}

std::string recordKey(const std::string &titleId, const std::string &name)
{
	return lower(titleId) + "/" + lower(name);
}

namespace {

/* A folder's entry by its name, case aside (false: the folder can't be listed). */
bool findEntry(const Io &io, const std::string &folder, const std::string &name, Entry *found, bool *exists)
{
	std::vector<Entry> entries;
	*exists = false;
	if (!io.list(folder, &entries)) {
		return false;
	}
	const std::string want = lower(name);
	for (const Entry &e : entries) {
		if (lower(e.name) == want) {
			*found = e;
			*exists = true;
			break;
		}
	}
	return true;
}

/* A folder and everything in it (its hidden files too: a copy's half-written one). */
bool removeTree(const Io &io, const std::string &folder, int depth)
{
	std::vector<Entry> entries;
	if (depth > 8 || !io.list(folder, &entries)) {
		return false;
	}
	bool ok = true;
	for (const Entry &e : entries) {
		if (e.name.empty() || e.name == "." || e.name == "..") {
			continue;
		}
		const std::string p = join(folder, e.name);
		ok = (e.dir ? removeTree(io, p, depth + 1) : io.remove(p)) && ok;
	}
	return ok && io.removeFolder && io.removeFolder(folder);
}

bool plainName(const std::string &name)
{
	return !name.empty() && name[0] != '.' && name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
		name.find('\0') == std::string::npos;
}

} // namespace

Removal remove(const Io &io, const Folders &folders, Records *records, const std::string &titleId, const std::string &name,
	bool here, bool there)
{
	Removal r;
	if (!isTitleFolder(titleId) || (!name.empty() && !plainName(name))) {
		r.why = "not a save's name: " + titleId + "/" + name;
		return r;
	}
	if (!here && !there) {
		r.why = "no side to delete on";
		return r;
	}
	/* each side's own spelling of the game's folder and the save, both looked up before anything
	 * is deleted (a side that doesn't answer: nothing is) */
	struct Target {
		bool asked{ false }, found{ false }, dir{ false };
		std::string path;
	};
	Target sides[2];   // there, here (deleted in that order)
	const std::string roots[2] = { folders.there, folders.here };
	const bool asked[2] = { there, here };
	for (int i = 0; i < 2; i++) {
		Target &t = sides[i];
		t.asked = asked[i];
		if (!t.asked) {
			continue;
		}
		if (roots[i].empty()) {
			r.why = i == 0 ? "no place to delete on" : "no hard disk to delete on";
			return r;
		}
		Entry title, item;
		bool hasTitle = false, hasItem = false;
		if (!findEntry(io, roots[i], titleId, &title, &hasTitle)) {
			if (i == 0) {
				r.why = "the place did not answer";
				return r;
			}
			continue;   // (the console's UDATA not made yet: nothing to delete there)
		}
		if (!hasTitle || !title.dir) {
			continue;
		}
		const std::string titlePath = join(roots[i], title.name);
		if (name.empty()) {
			t.found = true;
			t.dir = true;
			t.path = titlePath;
			continue;
		}
		if (!findEntry(io, titlePath, name, &item, &hasItem)) {
			if (i == 0) {
				r.why = "the place did not answer";
				return r;
			}
			continue;
		}
		if (hasItem) {
			t.found = true;
			t.dir = item.dir;
			t.path = join(titlePath, item.name);
		}
	}
	r.listed = true;
	r.foundThere = sides[0].found;
	r.foundHere = sides[1].found;
	r.ok = true;
	for (int i = 0; i < 2; i++) {
		const Target &t = sides[i];
		if (!t.asked || !t.found) {
			continue;
		}
		const bool done = t.dir ? removeTree(io, t.path, 0) : io.remove(t.path);
		if (!done) {
			r.ok = false;
			r.why = std::string("not everything could be deleted ") + (i == 0 ? "there: " : "here: ") + t.path;
			break;   // (the console's left whole: the next sync sends what the place lost back)
		}
	}
	/* forgotten in the list: neither side has it now, nothing to remember of it */
	if (records) {
		if (name.empty()) {
			const std::string prefix = lower(titleId) + "/";
			for (auto it = records->lower_bound(prefix); it != records->end() && it->first.rfind(prefix, 0) == 0;) {
				it = records->erase(it);
			}
		} else {
			records->erase(recordKey(titleId, name));
		}
		if (io.persist) {
			io.persist(formatRecords(*records));
		}
	}
	return r;
}

bool listThere(const Io &io, const Folders &folders, std::vector<ThereTitle> *titles)
{
	std::vector<Entry> thereTitles, hereTitles;
	if (!io.list(folders.there, &thereTitles)) {
		return false;
	}
	io.list(folders.here, &hereTitles);
	for (const Entry &e : thereTitles) {
		if (!e.dir || !isTitleFolder(e.name)) {
			continue;
		}
		ThereTitle t;
		t.folder = e.name;
		const std::string path = join(folders.there, e.name);
		std::string hereTitle;
		for (const Entry &h : hereTitles) {
			if (h.dir && lower(h.name) == lower(e.name)) {
				t.here = true;
				hereTitle = join(folders.here, h.name);
			}
		}
		std::set<std::string> hereSaves;
		if (t.here) {
			std::vector<Entry> hs;
			io.list(hereTitle, &hs);
			for (const Entry &h : hs) {
				if (h.dir && !hidden(h.name)) {
					hereSaves.insert(lower(h.name));
				}
			}
		}
		std::vector<Entry> entries;
		if (!io.list(path, &entries)) {
			continue;
		}
		for (const Entry &s : entries) {
			if (hidden(s.name)) {
				continue;
			}
			if (!s.dir) {
				if (lower(s.name) == "titlemeta.xbx") {
					t.meta = join(path, s.name);
				}
				continue;
			}
			ThereSave sv;
			sv.name = s.name;
			sv.here = hereSaves.count(lower(s.name)) != 0;
			const std::string savePath = join(path, s.name);
			std::vector<std::string> files;
			sv.sig.exists = collect(io, savePath, std::string(), &files, &sv.sig, 0);
			for (const std::string &f : files) {
				if (lower(f) == "savemeta.xbx") {
					sv.meta = join(savePath, f);
				}
			}
			t.saves.push_back(sv);
		}
		std::sort(t.saves.begin(), t.saves.end(), [](const ThereSave &a, const ThereSave &b) { return a.sig.mtime > b.sig.mtime; });
		titles->push_back(t);
	}
	return true;
}

std::string summary(const Result &result, const std::string &placeName)
{
	if (!result.listed) {
		return "The saves were not synced: " + placeName + " did not answer.";
	}
	std::string text = "Saves synced with " + placeName;
	if (result.sent == 0 && result.brought == 0 && result.failed == 0) {
		return text + (result.stopped ? ": stopped (a game started)." : ": nothing had changed.");
	}
	std::string parts;
	const auto add = [&](int n, const char *what) {
		if (n) {
			parts += (parts.empty() ? " " : ", ") + std::to_string(n) + " " + what;
		}
	};
	add(result.sent, "sent");
	add(result.brought, "brought here");
	add(result.bothChanged, "changed on both sides (the older one kept aside)");
	add(result.failed, "could not be copied");
	return text + ":" + parts + (result.stopped ? " (stopped: a game started)." : ".");
}

} // namespace savesync
} // namespace ogbox
