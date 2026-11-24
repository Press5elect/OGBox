/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 1.0.3: the data folder's move (ogbox_storage.h) on the PC: a move copied to another file
 * system to its last file and the old copy's deletion, a target inside the data folder refused, a
 * note that can't be written deleting nothing, a link left where it was, a move begun again going
 * on with what it copied (and deleting what the data folder no longer has), a rename on one file
 * system, and a note that doesn't read back put back as it was. 1.0.4: a move onto the console's
 * storage (/data/) copied by the write broker (its real request handler on a socketpair, every pwrite
 * cut short), with files of 0 bytes to more than one COPY's; one whose path the broker doesn't take
 * copied by OGBox; the broker dying part-way (the rest copied by OGBox, the file it was on again);
 * no broker; and a move elsewhere not using it. The copies are the files' bytes, each time.
 *   storage_test <work folder> <a folder on another file system>
 */
#include "ogbox_storage.h"
#include "../../broker/protocol.hpp"
#include "../../broker/server.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>

std::mutex g_logMutex;
std::vector<std::string> g_log;   // 1.0.4: the lines, for the helper's

extern "C" void pshakux_log(const char *format, ...)
{
	char line[2048];
	va_list a;
	va_start(a, format);
	std::vsnprintf(line, sizeof(line), format, a);
	va_end(a);
	std::fprintf(stdout, "  log: %s\n", line);
	std::lock_guard<std::mutex> lock(g_logMutex);
	g_log.push_back(line);
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
ogbox::StorageMove::Progress move(const std::string &from, const std::string &to, bool *sawRemoving = nullptr,
	ogbox::StorageMove::HelperConnect helper = nullptr, const std::string &helperRoot = "")
{
	ogbox::StorageMove &m = ogbox::StorageMove::get();
	expect(m.start(from, to, helper, helperRoot), "a move starts");
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

/* 1.0.4: the write broker for the move: its real request handler on a socketpair, in a thread */
struct Brokers {
	std::string root;
	::broker::ServerOptions options;
	std::vector<std::thread> threads;
	int launched{ 0 };
	ogbox::StorageMove::HelperConnect connect()
	{
		return [this](std::string &why) {
			launched++;
			int fds[2];
			if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
				why = "no socketpair";
				return -1;
			}
			const int server = fds[1];
			::broker::ServerOptions o = options;
			o.root = root.c_str();
			threads.emplace_back([server, o]() {
				::broker::serve(server, server, o);
				shutdown(server, SHUT_RDWR);   // (the payload's end: its process gone, the connection with it)
				close(server);
			});
			why = "the test's broker";
			return fds[0];
		};
	}
	void join()
	{
		for (std::thread &t : threads) {
			t.join();
		}
		threads.clear();
	}
};

/* a file's bytes from its name: checked once its source is gone */
std::string bytesOf(const std::string &name, size_t size)
{
	std::string d(size, '\0');
	uint64_t salt = 1469598103934665603ull;
	for (char ch : name) {
		salt = (salt ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
	}
	::broker::fill_pattern(d.data(), d.size(), 0, salt);
	return d;
}

bool logged(const std::string &part, size_t from)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	for (size_t i = from; i < g_log.size(); i++) {
		if (g_log[i].find(part) != std::string::npos) {
			return true;
		}
	}
	return false;
}

size_t logAt()
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	return g_log.size();
}

void helperMoves(const std::string &w, const std::string &other)
{
	/* the console's paths under a root of the test's: /data/ there on the work folder's file system,
	 * /mnt/usb0 a link to a folder on the other one (a copy, not a rename) */
	const std::string r = w + "/console";
	makeFolders(r + "/data");
	makeFolders(r + "/mnt");
	makeFolders(other + "/usb0");
	makeFolders(other + "/usb1");
	expect(symlink((other + "/usb0").c_str(), (r + "/mnt/usb0").c_str()) == 0, "helper: the USB drive's link made");
	expect(symlink((other + "/usb1").c_str(), (r + "/mnt/usb1").c_str()) == 0, "helper: the other USB drive's link made");
	std::string deep = "/hdd/deep";
	for (int i = 0; i < 5; i++) {
		deep += "/" + std::string(220, 'd');   // (past the broker's 1024 bytes)
	}
	struct File {
		std::string name;
		size_t size;
	};
	std::vector<File> files = { { "/hdd/Partition1/empty.bin", 0 }, { "/hdd/Partition1/one.bin", 1 },
		{ "/hdd/Partition1/big.bin", (70u << 20) + 4097 }, { "/cache/a.bin", 4096 }, { "/cache/b.bin", (1u << 20) + 3 },
		{ "/covers/4D530039.png", 12345 }, { deep + "/x.bin", 3000 } };
	uint64_t seed = 12345;
	for (int i = 0; i < 12; i++) {   // and some of random sizes
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
		files.push_back({ "/shots/s" + std::to_string(i) + ".bin", static_cast<size_t>((seed >> 33) % (5u << 20)) });
	}
	const auto fill = [&](const std::string &root) {
		for (const File &f : files) {
			put(root + f.name, bytesOf(f.name, f.size), 1550000000);
		}
	};
	const auto same = [&](const std::string &root, const char *what) {
		bool ok = true;
		for (const File &f : files) {
			if (get(root + f.name) != bytesOf(f.name, f.size) || mtimeOf(root + f.name) != 1550000000) {
				std::fprintf(stderr, "  %s: %s differs\n", what, f.name.c_str());
				ok = false;
			}
		}
		expect(ok, what);
	};
	Brokers b;
	b.root = r;
	b.options.io_cap = (1u << 20) + 3;   // every pwrite cut short (the broker's loop finishes each)
	b.options.message_ms = 10000;

	/* USB -> console storage: through the broker, every file but the one whose path it doesn't take */
	std::string from = r + "/mnt/usb0/OGBox", to = r + "/data/OGBox";
	fill(from);
	size_t at = logAt();
	ogbox::StorageMove::Progress p = move(from, to, nullptr, b.connect(), r);
	b.join();
	expect(p.done && p.ok && p.error.empty(), "helper: the move ends well");
	expect(ogbox::storageRoot() == to, "helper: the new folder is the data folder");
	same(to, "helper: every file's bytes and date there");
	expect(!there(from + "/hdd") && !there(from + "/cache") && !there(from + "/shots"), "helper: the old copy deleted");
	expect(b.launched == 1, "helper: one broker for the move");
	expect(logged("storage: moving through the helper", at), "helper: said");
	expect(logged("the helper " + std::to_string(files.size() - 1) + " files", at) && logged("; 1 files fell back", at),
		"helper: every file but one copied by it");
	expect(logged("the helper doesn't take its path; OGBox copies it itself", at), "helper: the long path copied by OGBox");

	/* console storage -> USB: not through the broker */
	from = to;
	to = r + "/mnt/usb1/OGBox";
	at = logAt();
	p = move(from, to, nullptr, b.connect(), r);
	b.join();
	expect(p.done && p.ok && b.launched == 1, "elsewhere: moved, no broker started");
	expect(logged("not moving through the helper: the new folder isn't on the console's storage", at), "elsewhere: said");
	same(to, "elsewhere: every file there");

	/* the broker dies part-way (after carrying out a request it doesn't answer): the file it was on
	 * copied again by OGBox, and the rest */
	for (const unsigned die : { 2u, 9u, 40u }) {
		from = to;
		to = r + "/data/OGBox" + std::to_string(die);
		b.options.die_after = die;
		at = logAt();
		p = move(from, to, nullptr, b.connect(), r);
		b.join();
		const std::string what = "dying after " + std::to_string(die) + " requests: ";
		expect(p.done && p.ok, (what + "the move ends well").c_str());
		same(to, (what + "every file's bytes and date there").c_str());
		expect(logged("storage: the helper stops:", at), (what + "said").c_str());
		expect(!there(from + "/hdd"), (what + "the old copy deleted").c_str());
		to = r + "/mnt/usb" + std::to_string(die % 2) + "/OGBox";   // (back to a USB drive, without it)
		from = r + "/data/OGBox" + std::to_string(die);
		p = move(from, to);
		expect(p.done && p.ok, (what + "moved back").c_str());
	}
	b.options.die_after = 0;

	/* no broker: OGBox copies */
	from = to;
	to = r + "/data/OGBox-none";
	at = logAt();
	p = move(from, to, nullptr, [](std::string &why) {
		why = "no payload loader (the test's)";
		return -1;
	}, r);
	expect(p.done && p.ok, "no broker: moved");
	same(to, "no broker: every file there");
	expect(logged("not moving through the helper: it could not be started: no payload loader", at), "no broker: said");
	removeAll(r);
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
		put(e + "/hdd/gone.dat", "deleted here since");                               // (deleted from the data folder since)
		put(e + "/hdd/gone/save.dat", "deleted here since");
	}
	p = move(d, e);
	expect(p.done && p.ok, "begun again: the move ends well");
	expect(ogbox::storageRoot() == e && !there(d + "/hdd") && !there(d + "/cache"), "begun again: moved");
	if (crossDevice) {
		expect(get(e + "/hdd/p.dat") == std::string(5000, 'P'), "begun again: the file copied whole not copied again");
		expect(get(e + "/cache/q.dat") == std::string(7000, 'q'), "begun again: the file cut short copied again");
		expect(!anyNamed(e, "hdd.old-") && !anyNamed(e, "cache.old-"), "begun again: its copy not set aside");
		expect(!there(e + "/hdd.copying") && !there(e + "/cache.copying"), "begun again: the marks taken away");
		expect(!there(e + "/hdd/gone.dat") && !there(e + "/hdd/gone"), "begun again: what the data folder no longer has deleted");
	}

	/* on one file system: renamed */
	const std::string f = w + "/f", g = w + "/g";
	put(f + "/states/4D530039/slot1.state", "state");
	p = move(f, g);
	expect(p.done && p.ok && get(g + "/states/4D530039/slot1.state") == "state" && !there(f + "/states"), "renamed: moved");

	/* a note that doesn't read back (a path longer than its line): the note as it was put back; a path
	 * with a space around it refused before anything is written */
	const std::string noteBefore = get(note);
	std::string why, longPath = w + "/long";
	for (int i = 0; i < 6; i++) {
		longPath += "/" + std::string(200, 'l');
	}
	expect(!ogbox::setStorageRoot(longPath, why) && why.find("read back") != std::string::npos, "not read back: refused");
	expect(get(note) == noteBefore && ogbox::storageRoot() == g, "not read back: the note as it was");
	why.clear();
	expect(!ogbox::setStorageRoot(w + "/spaced ", why) && !why.empty() && !there(w + "/spaced "), "a space around it: refused");
	expect(get(note) == noteBefore && ogbox::storageRoot() == g, "a space around it: the note as it was");
	removeAll(w + "/long");

	removeAll(other);
	helperMoves(w, other);

	std::printf("storage: %d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
