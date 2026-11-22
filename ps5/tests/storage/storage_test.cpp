/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 1.0.3: the data folder's move (ogbox_storage.h) on the PC: a move copied to another file
 * system to its last file and the old copy's deletion, a target inside the data folder refused, a
 * note that can't be written deleting nothing, a link left where it was, a move begun again going
 * on with what it copied, and a rename on one file system.
 *   storage_test <work folder> <a folder on another file system>
 */
#include "ogbox_storage.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

extern "C" void pshakux_log(const char *format, ...)
{
	va_list a;
	va_start(a, format);
	std::fprintf(stdout, "  log: ");
	std::vfprintf(stdout, format, a);
	std::fprintf(stdout, "\n");
	va_end(a);
}

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

bool makeFolders(const std::string &path)
{
	for (size_t at = 1; at <= path.size(); at++) {
		if (at == path.size() || path[at] == '/') {
			mkdir(path.substr(0, at).c_str(), 0777);
		}
	}
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void put(const std::string &path, const std::string &data, time_t mtime = 1600000000)
{
	makeFolders(path.substr(0, path.find_last_of('/')));
	FILE *f = std::fopen(path.c_str(), "wb");
	if (f) {
		std::fwrite(data.data(), 1, data.size(), f);
		std::fclose(f);
	}
	struct timeval t[2] = { { mtime, 0 }, { mtime, 0 } };
	utimes(path.c_str(), t);
}

std::string get(const std::string &path)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) {
		return "<none>";
	}
	std::string d;
	char b[4096];
	size_t n;
	while ((n = std::fread(b, 1, sizeof(b), f)) > 0) {
		d.append(b, n);
	}
	std::fclose(f);
	return d;
}

bool there(const std::string &path)
{
	struct stat st;
	return lstat(path.c_str(), &st) == 0;
}

time_t mtimeOf(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}

bool anyNamed(const std::string &folder, const char *prefix)
{
	bool found = false;
	if (DIR *d = opendir(folder.c_str())) {
		while (struct dirent *e = readdir(d)) {
			found |= std::strncmp(e->d_name, prefix, std::strlen(prefix)) == 0;
		}
		closedir(d);
	}
	return found;
}

void removeAll(const std::string &path)
{
	const std::string rm = "rm -rf '" + path + "'";
	if (std::system(rm.c_str()) != 0) {
		std::printf("(%s stays)\n", path.c_str());
	}
}

/* The move to its end: it must end (the owner's fault: it stood at its last file). */
ogbox::StorageMove::Progress move(const std::string &from, const std::string &to, bool *sawRemoving = nullptr)
{
	ogbox::StorageMove &m = ogbox::StorageMove::get();
	expect(m.start(from, to), "a move starts");
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
	for (;;) {
		const ogbox::StorageMove::Progress p = m.progress();
		if (sawRemoving && p.removing) {
			*sawRemoving = true;
		}
		if (p.done) {
			return p;
		}
		if (std::chrono::steady_clock::now() > until) {
			expect(false, "the move ends (it didn't in 60 s)");
			return p;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: storage_test <work folder> <a folder on another file system>\n");
		return 2;
	}
	const std::string w = argv[1];
	const std::string other = std::string(argv[2]) + "/ogbox-storage-test-" + std::to_string(getpid());
	makeFolders(w);
	makeFolders(other);
	struct stat sw, so;
	const bool crossDevice = stat(w.c_str(), &sw) == 0 && stat(other.c_str(), &so) == 0 && sw.st_dev != so.st_dev;
	if (!crossDevice) {
		std::printf("storage: %s is on the work folder's file system: the copies are renames\n", other.c_str());
	}

	/* a move copied to the other file system: to its last file, then the old copy deleted, with its
	 * progress; a link made by hand is left where it was */
	const std::string a = w + "/a", b = other + "/b";
	const std::string save = "/hdd/Partition1/UDATA/4d530039/save1/save.dat";
	put(a + save, std::string(1 << 20, 's'));
	put(a + "/cache/shaders.bin", std::string(3000, 'c'));
	put(a + "/covers/4D530039.png", "png", 1500000000);
	expect(symlink("save1", (a + "/hdd/Partition1/UDATA/4d530039/link").c_str()) == 0, "a link made");
	bool sawRemoving = false;
	ogbox::StorageMove::Progress p = move(a, b, &sawRemoving);
	expect(p.done && p.ok && p.error.empty(), "copied: the move ends well");
	expect(ogbox::storageRoot() == b, "copied: the new folder is the data folder");
	expect(get(b + save) == std::string(1 << 20, 's') && get(b + "/cache/shaders.bin") == std::string(3000, 'c') &&
			get(b + "/covers/4D530039.png") == "png",
		"copied: every file there");
	expect(mtimeOf(b + "/covers/4D530039.png") == 1500000000, "copied: its date kept");
	expect(!there(a + "/cache") && !there(a + "/covers") && !there(a + save), "copied: the old copy deleted");
	if (crossDevice) {
		expect(p.removing && p.bytes == p.total && p.total == (1u << 20) + 3000 + 3, "copied: the deletion's progress to its end");
		expect(there(a + "/hdd/Partition1/UDATA/4d530039/link") && !there(b + "/hdd/Partition1/UDATA/4d530039/link"),
			"copied: the link left where it was");
		expect(p.note.find("link") != std::string::npos, "copied: the link left said");
		expect(!there(b + "/hdd.copying") && !there(b + "/cache.copying"), "copied: no mark left");
	}
	std::printf("storage: copied (%s), the deletion %s, note: %s\n", crossDevice ? "to another file system" : "renamed",
		sawRemoving ? "seen" : "too quick to see", p.note.c_str());

	/* into the data folder itself, or one of its parts (by their resolved paths): refused, nothing moved */
	const std::string c = w + "/c";
	put(c + "/hdd/x.dat", "x");
	p = move(c, c + "/hdd/new");
	expect(p.done && !p.ok && p.error.find("inside") != std::string::npos, "inside a part: refused");
	expect(get(c + "/hdd/x.dat") == "x" && !there(c + "/hdd/new/hdd"), "inside a part: nothing moved");
	expect(symlink((c + "/hdd").c_str(), (w + "/to-c-hdd").c_str()) == 0, "a link to the part made");
	p = move(c, w + "/to-c-hdd/deeper");
	expect(p.done && !p.ok && p.error.find("inside") != std::string::npos, "inside a part through a link: refused");
	p = move(c, c);
	expect(p.done && !p.ok, "the data folder itself: refused");
	expect(get(c + "/hdd/x.dat") == "x" && ogbox::storageRoot() == b, "refused: the data folder as it was");

	/* the note of the new folder can't be written: nothing deleted, the data folder as it was; the copy
	 * is marked there */
	const std::string d = w + "/d", e = other + "/e";
	put(d + "/hdd/p.dat", std::string(5000, 'p'));
	put(d + "/cache/q.dat", std::string(7000, 'q'));
	const std::string note = w + "/conf/storage.txt";
	expect(makeFolders(note + ".new"), "the note's temporary file blocked");
	p = move(d, e);
	expect(p.done && !p.ok && p.error.find("note") != std::string::npos, "the note not written: the move stops");
	expect(get(d + "/hdd/p.dat") == std::string(5000, 'p') && get(d + "/cache/q.dat") == std::string(7000, 'q'),
		"the note not written: nothing deleted");
	expect(ogbox::storageRoot() == b && get(note).find(b) != std::string::npos, "the note not written: the data folder as it was");
	if (crossDevice) {
		expect(there(e + "/hdd.copying") && there(e + "/cache.copying"), "the note not written: the copy marked unfinished");
	}
	rmdir((note + ".new").c_str());

	/* begun again: it goes on with what it copied (a file there whole, its size and date as here, not
	 * copied again; one cut short copied again), nothing set aside as data from before */
	if (crossDevice) {
		put(e + "/hdd/p.dat", std::string(5000, 'P'), mtimeOf(d + "/hdd/p.dat"));   // (whole: its bytes marked)
		put(e + "/cache/q.dat", std::string(10, 'q'));                                // (cut short)
	}
	p = move(d, e);
	expect(p.done && p.ok, "begun again: the move ends well");
	expect(ogbox::storageRoot() == e && !there(d + "/hdd") && !there(d + "/cache"), "begun again: moved");
	if (crossDevice) {
		expect(get(e + "/hdd/p.dat") == std::string(5000, 'P'), "begun again: the file copied whole not copied again");
		expect(get(e + "/cache/q.dat") == std::string(7000, 'q'), "begun again: the file cut short copied again");
		expect(!anyNamed(e, "hdd.old-") && !anyNamed(e, "cache.old-"), "begun again: its copy not set aside");
		expect(!there(e + "/hdd.copying") && !there(e + "/cache.copying"), "begun again: the marks taken away");
	}

	/* on one file system: renamed */
	const std::string f = w + "/f", g = w + "/g";
	put(f + "/states/4D530039/slot1.state", "state");
	p = move(f, g);
	expect(p.done && p.ok && get(g + "/states/4D530039/slot1.state") == "state" && !there(f + "/states"), "renamed: moved");

	removeAll(other);
	std::printf("storage: %d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
