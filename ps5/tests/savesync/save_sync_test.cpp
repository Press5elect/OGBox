/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.8: the save sync's engine (ogbox_save_sync.h) on two folder trees kept in memory, as
 * Crux2's card sync is tested (ps5/tests/cardsync there).
 */
#include "ogbox_save_sync.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace ogbox::savesync;

namespace {

int g_checks = 0, g_failed = 0;

void expect(bool ok, const char *what)
{
	g_checks++;
	if (!ok) {
		g_failed++;
		std::fprintf(stderr, "FAILED: %s\n", what);
	}
}

/* Files in memory: a path to its bytes and its time (each write a new time); folders made. */
struct Files {
	std::map<std::string, std::pair<std::string, int64_t>> files;
	std::set<std::string> folders;
	int64_t clock = 1000;
	bool thereAnswers = true;
	bool stop = false;
	int writesThereLeft = -1;   // >= 0: that many more writes there succeed, then they fail
	std::string kept;           // the records as last persisted

	void put(const std::string &path, const std::string &data) { files[path] = { data, ++clock }; }
	std::string get(const std::string &path) const
	{
		const auto f = files.find(path);
		return f == files.end() ? std::string("<none>") : f->second.first;
	}
	size_t countUnder(const std::string &folder) const
	{
		size_t n = 0;
		for (const auto &[p, v] : files) {
			n += p.rfind(folder + "/", 0) == 0 ? 1 : 0;
		}
		return n;
	}
	bool isFolder(const std::string &f) const
	{
		if (folders.count(f)) {
			return true;
		}
		for (const auto &[p, v] : files) {
			if (p.rfind(f + "/", 0) == 0) {
				return true;
			}
		}
		return false;
	}

	Io io()
	{
		Io io;
		io.list = [this](const std::string &folder, std::vector<Entry> *entries) {
			if (folder.rfind("there", 0) == 0 && !thereAnswers) {
				return false;
			}
			if (!isFolder(folder)) {
				return false;
			}
			std::set<std::string> dirs;
			for (const auto &[p, v] : files) {
				if (p.rfind(folder + "/", 0) != 0) {
					continue;
				}
				const std::string rest = p.substr(folder.size() + 1);
				const size_t slash = rest.find('/');
				if (slash == std::string::npos) {
					entries->push_back({ rest, false, v.first.size(), v.second });
				} else {
					dirs.insert(rest.substr(0, slash));
				}
			}
			for (const std::string &f : folders) {
				if (f.rfind(folder + "/", 0) == 0) {
					const std::string rest = f.substr(folder.size() + 1);
					dirs.insert(rest.substr(0, rest.find('/')));
				}
			}
			for (const std::string &d : dirs) {
				entries->push_back({ d, true, 0, 0 });
			}
			return true;
		};
		io.read = [this](const std::string &path, std::string *data) {
			const auto f = files.find(path);
			if (f == files.end()) {
				return false;
			}
			*data = f->second.first;
			return true;
		};
		io.write = [this](const std::string &path, const std::string &data) {
			if (path.rfind("there", 0) == 0 && writesThereLeft >= 0 && writesThereLeft-- == 0) {
				writesThereLeft = 0;
				return false;
			}
			put(path, data);
			return true;
		};
		io.makeFolder = [this](const std::string &folder) {
			for (size_t at = 0; (at = folder.find('/', at + 1)) != std::string::npos;) {
				folders.insert(folder.substr(0, at));
			}
			folders.insert(folder);
			return true;
		};
		io.remove = [this](const std::string &path) { return files.erase(path) == 1; };
		io.stop = [this] { return stop; };
		io.persist = [this](const std::string &records) { kept = records; };
		return io;
	}
};

const Folders kFolders{ "here/UDATA", "there/OGBox/Saves/UDATA", "olderhere", "there/OGBox/Saves/.older" };
const std::string H = "here/UDATA/4541009e", T = "there/OGBox/Saves/UDATA/4541009e";

Result run(Files &fs, Records &records)
{
	return sync(fs.io(), kFolders, &records, 1767225600);   // (2026-01-01: the names put away)
}

void testFirstSyncSendsAndThenNothing()
{
	Files fs;
	Records records;
	fs.put(H + "/TitleMeta.xbx", "carbon");
	fs.put(H + "/AAAA0001/SaveMeta.xbx", "career");
	fs.put(H + "/AAAA0001/save.dat", "progress 1");
	fs.put(H + "/AAAA0002/save.dat", "options");
	Result r = run(fs, records);
	expect(r.listed && r.sent == 3 && r.brought == 0 && r.failed == 0, "first sync: the game's file and both saves sent");
	expect(fs.get(T + "/AAAA0001/save.dat") == "progress 1" && fs.get(T + "/TitleMeta.xbx") == "carbon", "first sync: there as here");
	r = run(fs, records);
	expect(r.sent == 0 && r.brought == 0 && r.unchanged == 3, "second sync: nothing changed");
	expect(parseRecords(formatRecords(records)).size() == records.size(), "the list read back");
}

void testChangedHereGoesThereWhole()
{
	Files fs;
	Records records;
	fs.put(H + "/AAAA0001/save.dat", "progress 1");
	fs.put(H + "/AAAA0001/extra.dat", "old part");
	run(fs, records);
	fs.put(H + "/AAAA0001/save.dat", "progress 2");
	fs.files.erase(H + "/AAAA0001/extra.dat");   // the game's newer save has one file fewer
	const Result r = run(fs, records);
	expect(r.sent == 1 && r.brought == 0, "changed here: sent");
	expect(fs.get(T + "/AAAA0001/save.dat") == "progress 2", "changed here: its new bytes there");
	expect(fs.get(T + "/AAAA0001/extra.dat") == "<none>", "changed here: the file it no longer has taken away there");
	expect(run(fs, records).unchanged == 1, "changed here: then nothing to do");
}

void testChangedThereComesHere()
{
	Files fs;
	Records records;
	fs.put(H + "/AAAA0001/save.dat", "progress 1");
	run(fs, records);
	fs.put(T + "/AAAA0001/save.dat", "progress from another console");
	const Result r = run(fs, records);
	expect(r.brought == 1 && r.sent == 0, "changed there: brought here");
	expect(fs.get(H + "/AAAA0001/save.dat") == "progress from another console", "changed there: its bytes here");
}

void testNewThereComesHereWithItsGame()
{
	Files fs;
	Records records;
	fs.put(T + "/AAAA0009/save.dat", "a save of a game this console never played");
	fs.put("there/OGBox/Saves/UDATA/4D530004/BBBB0001/save.dat", "halo");
	const Result r = run(fs, records);
	expect(r.brought == 2, "new there: both brought here");
	expect(fs.get("here/UDATA/4D530004/BBBB0001/save.dat") == "halo", "new there: its game's folder made here");
}

void testBothChangedNewerWinsOlderKept()
{
	Files fs;
	Records records;
	fs.put(H + "/AAAA0001/save.dat", "progress 1");
	run(fs, records);
	fs.put(T + "/AAAA0001/save.dat", "played on another console");
	fs.put(H + "/AAAA0001/save.dat", "played here, later");   // (the newer time)
	Result r = run(fs, records);
	expect(r.bothChanged == 1 && r.sent == 1, "both changed: the console's (newer) sent");
	expect(fs.get(T + "/AAAA0001/save.dat") == "played here, later", "both changed: the newer there");
	expect(fs.countUnder("there/OGBox/Saves/.older/4541009e") == 1, "both changed: the other's kept in .older there");
	// and the other way: the other side's newer
	fs.put(H + "/AAAA0001/save.dat", "here again");
	fs.put(T + "/AAAA0001/save.dat", "there, later");
	r = run(fs, records);
	expect(r.bothChanged == 1 && r.brought == 1, "both changed: the other side's (newer) brought");
	expect(fs.get(H + "/AAAA0001/save.dat") == "there, later", "both changed: the newer here");
	expect(fs.countUnder("olderhere/4541009e") == 1, "both changed: the console's kept in older saves");
}

void testNeverSyncedSameOrDifferent()
{
	Files fs;
	Records records;
	fs.put(H + "/AAAA0001/save.dat", "same");
	fs.put(T + "/AAAA0001/save.dat", "same");
	fs.put(H + "/AAAA0002/save.dat", "here's");
	fs.put(T + "/AAAA0002/save.dat", "there's, newer");
	const Result r = run(fs, records);
	expect(r.unchanged == 1, "never synced, the same: remembered");
	expect(r.bothChanged == 1 && fs.get(H + "/AAAA0002/save.dat") == "there's, newer", "never synced, different: the newer kept");
	expect(fs.countUnder("olderhere") == 1, "never synced, different: the other kept aside");
}

void testTitleCaseOnEitherSide()
{
	Files fs;
	Records records;
	fs.put(H + "/AAAA0001/save.dat", "same");
	fs.put("there/OGBox/Saves/UDATA/4541009E/AAAA0001/save.dat", "same");
	const Result r = run(fs, records);
	expect(r.unchanged == 1 && r.sent == 0 && r.brought == 0, "a title ID in capitals there: the same game");
}

void testThereDoesNotAnswer()
{
	Files fs;
	Records records;
	fs.put(H + "/AAAA0001/save.dat", "x");
	fs.thereAnswers = false;
	const Result r = run(fs, records);
	expect(!r.listed && r.sent == 0 && fs.countUnder("there") == 0, "there not answering: nothing done");
	expect(summary(r, "NAS").find("did not answer") != std::string::npos, "there not answering: said");
}

void testStopsWhenAGameStarts()
{
	Files fs;
	Records records;
	fs.put(H + "/AAAA0001/save.dat", "x");
	fs.stop = true;
	const Result r = run(fs, records);
	expect(r.stopped && r.sent == 0, "a game starting: stopped before a save");
}

void testCutOffCopyMadeAgain()
{
	Files fs;
	Records records;
	fs.put(H + "/AAAA0001/a.dat", "a1");
	fs.put(H + "/AAAA0001/b.dat", "b1");
	run(fs, records);
	fs.put(H + "/AAAA0001/a.dat", "a2");
	fs.put(H + "/AAAA0001/b.dat", "b2");
	fs.writesThereLeft = 1;   // the send writes a.dat, then the console is turned off
	Result r = run(fs, records);
	expect(r.failed == 1 && fs.get(T + "/AAAA0001/a.dat") == "a2" && fs.get(T + "/AAAA0001/b.dat") == "b1",
		"a send cut off: half there");
	expect(fs.kept.find("\tS\n") != std::string::npos, "a send cut off: marked in the list kept");
	Records reread = parseRecords(fs.kept);   // the next start reads the list as it was kept
	fs.writesThereLeft = -1;
	r = run(fs, reread);
	expect(r.sent == 1 && r.brought == 0, "the next sync: sent again, not the half-copied folder brought here");
	expect(fs.get(H + "/AAAA0001/a.dat") == "a2" && fs.get(T + "/AAAA0001/b.dat") == "b2", "the next sync: there whole");
	expect(fs.kept.find("\tS\n") == std::string::npos, "the next sync: the mark taken away");
}

} // namespace

int main()
{
	testFirstSyncSendsAndThenNothing();
	testChangedHereGoesThereWhole();
	testChangedThereComesHere();
	testNewThereComesHereWithItsGame();
	testBothChangedNewerWinsOlderKept();
	testNeverSyncedSameOrDifferent();
	testTitleCaseOnEitherSide();
	testThereDoesNotAnswer();
	testStopsWhenAGameStarts();
	testCutOffCopyMadeAgain();
	std::printf("save sync: %d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
