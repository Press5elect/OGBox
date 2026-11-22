/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 1.0.3: the hard disk's saves on their files (src/ogbox_saves.cpp, included whole for its own
 * helpers) on the PC: a save an import stopped between its renames put back by the next import, not
 * deleted; one a save sync's write stopped between its renames put back before it is listed.
 *   saves_files_test <work folder>
 */
#include "ogbox_saves.cpp"

#include <cstdarg>

/* what the saves' file code links with on the console, unused here (no shares, no game database) */
extern "C" {
void pshakux_log(const char *format, ...)
{
	va_list a;
	va_start(a, format);
	std::fprintf(stdout, "  log: ");
	std::vfprintf(stdout, format, a);
	std::fprintf(stdout, "\n");
	va_end(a);
}
int ps5vfs_is_virtual(const char *) { return 0; }
const char *ps5vfs_mount_point(unsigned) { return nullptr; }
ps5vfs_dir *ps5vfs_opendir(const char *) { return nullptr; }
int ps5vfs_readdir(ps5vfs_dir *, struct ps5vfs_dirent *) { return -1; }
void ps5vfs_closedir(ps5vfs_dir *) {}
FILE *ps5vfs_fopen(const char *, const char *) { return nullptr; }
int ps5vfs_fclose(FILE *) { return -1; }
size_t ps5vfs_fread(void *, size_t, size_t, FILE *) { return 0; }
int ps5vfs_ferror(FILE *) { return 1; }
int ps5vfs_store(const char *, const void *, size_t) { return -1; }
int ps5vfs_make_folders(const char *) { return -1; }
int ps5vfs_remove(const char *, int) { return -1; }
}
namespace ogbox {
GameDb &GameDb::get() { std::abort(); }
const DiscRecord *GameDb::byTitle(uint32_t, const std::string &, uint32_t) const { return nullptr; }
std::string shortName(const std::string &fileName) { return fileName; }
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

void put(const std::string &path, const std::string &data)
{
	ogbox::makeFolders(path.substr(0, path.find_last_of('/')));
	if (FILE *f = std::fopen(path.c_str(), "wb")) {
		std::fwrite(data.data(), 1, data.size(), f);
		std::fclose(f);
	}
}

std::string get(const std::string &path)
{
	std::string d;
	return ogbox::syncRead(path, &d) ? d : "<none>";
}

bool there(const std::string &path)
{
	struct stat st;
	return lstat(path.c_str(), &st) == 0;
}

bool listed(const std::string &folder, const char *name)
{
	std::vector<ogbox::savesync::Entry> entries;
	if (!ogbox::syncList(folder, &entries)) {
		return false;
	}
	for (const auto &e : entries) {
		if (e.name == name) {
			return true;
		}
	}
	return false;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: saves_files_test <work folder>\n");
		return 2;
	}
	const std::string w = argv[1];
	ogbox::removeTree(w);
	ogbox::makeFolders(w);

	/* an import over a save: in place, the old one gone */
	const std::string hdd = w + "/hdd", udata = hdd + "/Partition1/UDATA/4d530039";
	const std::string place = w + "/usb/4D530039";
	put(udata + "/save1/save.dat", "old");
	put(place + "/save1/save.dat", "new");
	ogbox::SaveCopy copy;
	copy.idText = "4d530039";
	copy.folder = "save1";
	copy.path = place + "/save1";
	std::string why;
	expect(ogbox::hddImportSave(copy, hdd, true, why), "import: done");
	expect(get(udata + "/save1/save.dat") == "new", "import: the new one in place");
	expect(!there(hdd + "/.ogbox-import-aside/4d530039/save1") && !there(hdd + "/.ogbox-import"), "import: nothing left aside");

	/* one stopped between its renames (the save aside, none in its place): the next import of another
	 * save puts it back and deletes nothing */
	ogbox::makeFolders(hdd + "/.ogbox-import-aside/4d530039");
	expect(rename((udata + "/save1").c_str(), (hdd + "/.ogbox-import-aside/4d530039/save1").c_str()) == 0, "stopped: its save aside");
	put(place + "/save2/save.dat", "two");
	copy.folder = "save2";
	copy.path = place + "/save2";
	expect(ogbox::hddImportSave(copy, hdd, true, why), "stopped, another save: imported");
	expect(get(hdd + "/.ogbox-import-aside/4d530039/save1/save.dat") == "new", "stopped, another save: the one aside not deleted");
	copy.folder = "save1";
	copy.path = place + "/save1";
	put(place + "/save1/save.dat", "newer");
	expect(ogbox::hddImportSave(copy, hdd, true, why), "stopped, the same save: imported");
	expect(get(udata + "/save1/save.dat") == "newer", "stopped, the same save: the new one in place");
	expect(!there(hdd + "/.ogbox-import-aside/4d530039/save1"), "stopped, the same save: nothing left aside");

	/* one left aside with a save in its place that the import didn't put there (no mark): kept */
	put(hdd + "/.ogbox-import-aside/4d530039/save1/save.dat", "kept");
	expect(ogbox::hddImportSave(copy, hdd, true, why), "unknown aside: imported");
	bool kept = false;
	for (const std::string &name : ogbox::listDir(hdd + "/.ogbox-import-aside/4d530039", true)) {
		kept |= name.rfind("save1.kept-", 0) == 0 && get(hdd + "/.ogbox-import-aside/4d530039/" + name + "/save.dat") == "kept";
	}
	expect(kept, "unknown aside: kept");

	/* a sync's write stopped between its renames: put back before it is listed, and written over */
	const std::string s = w + "/sync";
	put(s + "/.save.dat.old", "was");
	expect(listed(s, "save.dat") && get(s + "/save.dat") == "was" && !there(s + "/.save.dat.old"), "sync: put back when listed");
	put(s + "/.other.dat.old", "older");
	put(s + "/other.dat", "current");
	expect(listed(s, "other.dat") && get(s + "/other.dat") == "current", "sync: one in its place not overwritten");
	expect(ogbox::syncWrite(s + "/save.dat", "now") && get(s + "/save.dat") == "now" && !there(s + "/.save.dat.old") &&
			!there(s + "/.save.dat.part"),
		"sync: written");

	ogbox::removeTree(w);
	std::printf("saves files: %d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
