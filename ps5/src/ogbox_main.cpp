/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The front end: a game list to pick from, then the game. It talks to the emulator only through
 * the Core (src/core/ogbox_core.h).
 *
 *  - The game list (0.1.14): what shows when the title starts without a game named in its
 *    arguments. It browses the console's internal storage (/data), USB drives (/mnt/usb0-7),
 *    extended storage (/mnt/ext0-1) and network shares (SMB or NFS, from
 *    /data/ogbox/network.cfg, Crux2's format: src/platform/vfs/ps5_vfs.h), showing folders and
 *    Xbox disc images (.iso, .xiso, .chd, .cci). D-pad to move, CROSS to open a folder or start a game,
 *    CIRCLE for the folder above. The folder last opened is remembered (/data/ogbox/library.txt).
 *    Starting a game relaunches the title with --boot <image> (src/platform/ps5_relaunch.h): the
 *    game boots in a fresh instance, the one way hakuX's core is known to start.
 *    Images on a share are read through the virtual file layer (ps5vfs_disc_open).
 *
 *  - The launch: the template's main (src/main.cpp) has opened the boot log (ogbox-boot.log in
 *    the title folder, the previous run's kept as ogbox-boot.previous.log, a crash report, an
 *    "alive:" heartbeat with the Core's status line), dismissed the splash and set up Vulkan on
 *    RADV; this class is its one program.
 *  - boot.txt: /data/homebrew/PPSA99251/boot.txt (or the title folder's): its options are read
 *    every run; its first line (a disc image) boots straight away only with an "autoboot" line
 *    (0.1.14; the game list otherwise). A path that does not start with '/' is in the title folder (0.1.3), one that
 *    does may be anywhere the title sees: out of its sandbox (src/platform/ps5_sandbox.cpp),
 *    /data/..., USB drives (/mnt/usb0/...) and extended storage (/mnt/ext0/...):
 *
 *        # comments
 *        /data/xbox/My Game.iso          the disc image (an XISO), or "none" for the dashboard
 *        scale=2 uncap                   hakuX's options (native/core/options.h), any number a line
 *        hdd=/data/ogbox/hdd           the hard disk's folder (default /data/ogbox/hdd)
 *        prop debug.hakux.NAME=VALUE     one of hakuX's debug properties
 *        env NAME=VALUE                  one of hakuX's environment switches (0.1.5: diagnostics)
 *        autoboot                        boot the first line's image without the game list
 *
 *    with /data/ogbox/props.txt for more properties (NAME=VALUE lines). The title's arguments come
 *    before boot.txt (a relaunch gives them, src/platform/ps5_relaunch.h; on the console argv[0] is
 *    the first argument, so all are read):
 *
 *        --boot <image>        what boots (then boot.txt is not read)
 *        --hdd <folder>        the hard disk's folder
 *        --loadstate <file>    into that savestate (hakuX's loadstate=)
 *        --relaunch            the guest's reboot into another XBE (hakuX's relaunch)
 *        --option <opt>, or a word with '='   one of hakuX's options
 *
 *    The emulator's own files
 *    (shader and pipeline caches) go to /data/ogbox/cache: outside the title folder, so an update
 *    is replacing the title folder.
 *  - The picture: the HLE GPU's front frame, handed over by the Core as RGBA8 rows (the
 *    host-owned image presenter, ps5/hakux/hle/present_host.c), uploaded into one sampled image
 *    and drawn on the template's display (VK_KHR_display) as Kelvin's presenter does: letterboxed
 *    at the frame's shape (4:3 for the SD modes; 16:9 for a 4:3-sized frame when the console is set
 *    to widescreen, which games render anamorphic; HD frames as they are), bilinear, nothing else.
 *  - The controllers: each signed-in user's DualSense is an Xbox controller (players 1-4), its
 *    raw readings, with no dead zone added (the game has its own): cross A, circle B, square X,
 *    triangle Y, R1 black, L1 white, L2/R2 the triggers, OPTIONS start, the touch pad back, the
 *    sticks and their clicks. The rumble the game asks for goes to the controller's motors.
 *  - Quitting: hold OPTIONS and the touch pad together for two seconds: in a game, back to the
 *    game list (a relaunch, 0.1.14); in the list, the title ends through the shell, as every title
 *    does (the template's exit), as does its "Quit OGBox" row. hakuX's core has no teardown yet, so a
 *    new boot is a relaunch of the title: the guest's reboot into another XBE relaunches it with
 *    --boot <same disc> --relaunch, and the full front end will do the same for "back to the
 *    library" (no arguments) and "load a savestate" (--boot <disc> --loadstate <file>).
 *  - Thread priorities: the vCPU at 700, the core's helpers at 690, background jobs at 730
 *    (src/platform/ps5_priority.h), on from the start.
 *
 * Made from Kelvin's examples/kelvin/kelvin.cpp (Press5elect) on the PS5 Vulkan Template
 * foundation (Mihawk; Sascha Willems' Vulkan example base class, MIT).
 */

#include "vulkanexamplebase.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <random>
#include <atomic>
#include <cerrno>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <condition_variable>   // (0.0.4; prelude.h has them: this file is in a namespace)
#include <deque>
#include <set>
#include <map>
#include <string>
#include <vector>

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include "platform.h"
#include "platform/ps5_bootlog.h"
#include "platform/ps5_priority.h"
#include "platform/ps5_relaunch.h"
#include <unistd.h>
#include "platform/ps5_sandbox.h"
#include "platform/ps5_update.h"
#include "platform/ps5_update_net.h"
#include "platform/vfs/ps5_vfs.h"
#include "core/ogbox_core.h"
#include "core/ogbox_mods.h"
#include "core/ogbox_patches.h"
#include "core/ogbox_trainers.h"
#include "ogbox_settings.h"
#include "ogbox_framegen.h"
#include "framegen/ps5_framegen_pacer.h"
#include "ogbox_library.h"
#include "ogbox_storage.h"
#include "ogbox_saves.h"
#include "ogbox_ui.h"
#include "ogbox_pad.h"
#include "VulkanTexture.h"
#include "third_party/smaa/AreaTex.h"     // SMAA (MIT): its area and search textures
#include "third_party/smaa/SearchTex.h"

#include "stb_image.h"   // (its implementation is the base class's: base/VulkanglTFModel.cpp)

extern "C" int pshakux_props_load(const char *path);
extern "C" int pshakux_prop_set(const char *name, const char *value);

namespace {

constexpr const char *kBootFile = "/data/homebrew/PPSA99251/boot.txt";
constexpr const char *kNoticeSeenFile = "/data/ogbox/notice-seen.txt";   // 0.0.6: the version whose notice was read

/* The version whose start notice was read ("" none). */
std::string noticeSeenFor()
{
	std::string v;
	if (FILE *f = std::fopen(kNoticeSeenFile, "r")) {
		char line[128] = "";
		if (std::fgets(line, sizeof(line), f)) {
			v = line;
		}
		std::fclose(f);
	}
	while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) {
		v.pop_back();
	}
	return v;
}
constexpr const char *kDataRoot = "/data/ogbox";
constexpr double kQuitHoldSeconds = 2.0;
constexpr const char *kLibraryFile = "/data/ogbox/library.txt";
constexpr const char *kLastGameFile = "/data/ogbox/last-game.txt";   // 0.1.43: the disc last started
constexpr const char *kNetworkFile = "/data/ogbox/network.cfg";

struct BootConfig {
	std::string image;
	std::string hdd = std::string(kDataRoot) + "/hdd";
	std::vector<std::string> options;
	std::string source;
	std::string bootTxtImage;   // boot.txt's first line (0.1.14: boots only with "autoboot")
	bool autoboot{ false };
	std::string gameKey;        // 0.1.26: --game <title ID>.<version> (the game list read it)
	bool back{ false };         // 0.1.43: --back, from a game to the game list (no boot screen)
};

std::string trim(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t\r\n");
	const size_t b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

/* The title folder's boot.txt, under the name the folder has now (/app0 inside the sandbox, its
 * install path out of it: src/platform/ps5_sandbox.cpp). */
std::string titleBootFile()
{
	return std::string(pshakux_title_dir()) + "/boot.txt";
}

/* A path from boot.txt or the arguments: as it is when it starts with '/' (anywhere the title can
 * see: /data/..., /mnt/usb0/..., /mnt/ext0/... out of the sandbox), else relative to the title
 * folder ("games/My Game.iso" is <title folder>/games/My Game.iso); "none" stays "none". */
std::string resolvePath(const std::string &path)
{
	if (path.empty() || path[0] == '/' || path == "none") {
		return path;
	}
	std::string rest = path;
	while (rest.rfind("./", 0) == 0) {
		rest = rest.substr(2);
	}
	return std::string(pshakux_title_dir()) + "/" + rest;
}

/* boot.txt: the first line that is not a comment is the disc image (it may hold spaces); then
 * hakuX's options, separated by spaces, and the hdd= and prop lines. */
bool readBootConfig(BootConfig &config)
{
	const std::string titleFile = titleBootFile();
	for (const char *path : { kBootFile, titleFile.c_str() }) {
		FILE *file = std::fopen(path, "r");
		if (!file) {
			continue;
		}
		config.source = path;
		char buffer[1024];
		bool haveImage = false;
		while (std::fgets(buffer, sizeof(buffer), file)) {
			const std::string line = trim(buffer);
			if (line.empty() || line[0] == '#') {
				continue;
			}
			/* 0.0.4: an env, prop or hdd= line, or autoboot, is never the disc image, even first (a
			 * boot.txt of two env lines lost the first as "the image") */
			const bool keyword = line.rfind("env ", 0) == 0 || line.rfind("prop ", 0) == 0 ||
			                     line.rfind("hdd=", 0) == 0 || line == "autoboot";
			if (!haveImage && !keyword) {
				config.bootTxtImage = resolvePath(line);
				haveImage = true;
				continue;
			}
			if (line == "autoboot") {
				config.autoboot = true;
				continue;
			}
			if (line.rfind("env ", 0) == 0) {
				/* 0.1.5: one of hakuX's environment switches (diagnostics: NC_TRACE_APU=1,
				 * HAKUX_APU_STATS=1, NC_TRACE_MMIO=lo-hi, NC_THREAD_DUMPS=n...), before the core starts */
				const std::string assignment = trim(line.substr(4));
				const size_t eq = assignment.find('=');
				if (eq != std::string::npos && eq > 0) {
					setenv(assignment.substr(0, eq).c_str(), assignment.substr(eq + 1).c_str(), 1);
					pshakux_log("boot.txt: environment %s", assignment.c_str());
				}
				continue;
			}
			if (line.rfind("prop ", 0) == 0) {
				const std::string assignment = trim(line.substr(5));
				const size_t eq = assignment.find('=');
				if (eq != std::string::npos) {
					pshakux_prop_set(assignment.substr(0, eq).c_str(), assignment.substr(eq + 1).c_str());
					pshakux_log("boot.txt: property %s", assignment.c_str());
				}
				continue;
			}
			size_t at = 0;
			while (at < line.size()) {
				const size_t end = line.find_first_of(" \t", at);
				const std::string word = line.substr(at, end == std::string::npos ? std::string::npos : end - at);
				if (word.rfind("hdd=", 0) == 0) {
					config.hdd = resolvePath(word.substr(4));
				} else if (!word.empty()) {
					config.options.push_back(word);
				}
				if (end == std::string::npos) {
					break;
				}
				at = line.find_first_not_of(" \t", end);
				if (at == std::string::npos) {
					break;
				}
			}
		}
		std::fclose(file);
		return true;
	}
	return false;
}

/* The title's arguments (all of them: argv[0] is the first). true when they name what boots. */
bool readArguments(BootConfig &config)
{
	bool boot = false;
	for (int i = 0; i < pshakux_argc(); i++) {
		const std::string arg = pshakux_argv(i);
		const char *next = pshakux_argv(i + 1);
		if (arg == PSHAKUX_RELAUNCHED) {
			continue;
		} else if (arg == "--boot" && next) {
			config.image = resolvePath(next);
			boot = true;
			i++;
		} else if (arg == "--hdd" && next) {
			config.hdd = resolvePath(next);
			i++;
		} else if (arg == "--loadstate" && next) {
			config.options.push_back(std::string("loadstate=") + next);
			i++;
		} else if (arg == "--game" && next) {
			config.gameKey = next;
			i++;
		} else if (arg == "--relaunch") {
			config.options.push_back("relaunch");
		} else if (arg == "--back") {
			config.back = true;
		} else if (arg == "--option" && next) {
			config.options.push_back(next);
			i++;
		} else if (arg.rfind("--", 0) != 0 && !arg.empty()) {
			config.options.push_back(arg);
		} else {
			pshakux_log("arguments: %s is not one of OGBox's: left out", arg.c_str());
		}
	}
	if (boot) {
		config.source = "the title's arguments";
	}
	return boot;
}

const char *stateName(int state)
{
	switch (state) {
	case PSHX_CORE_IDLE: return "not started";
	case PSHX_CORE_BOOTING: return "booting";
	case PSHX_CORE_RUNNING: return "running";
	case PSHX_CORE_STOPPED: return "stopped";
	case PSHX_CORE_FAILED: return "failed";
	}
	return "?";
}

// ---- the game list (0.1.14) ------------------------------------------------------------------

struct LibraryEntry {
	enum Kind { Place, Folder, Image, Up, Quit, Settings, Dashboard, Hdd, AddShare } kind;   // 0.0.3: Hdd, AddShare (the home's cards)
	std::string name;   // as shown
	std::string path;   // what it opens
	int64_t size{ 0 };
	std::string note;   // a dim line under it
};

bool isDiscImage(const std::string &name)
{
	const size_t dot = name.rfind('.');
	if (dot == std::string::npos) {
		return false;
	}
	const char *ext = name.c_str() + dot + 1;
	if (!strcasecmp(ext, "iso") || !strcasecmp(ext, "xiso") || !strcasecmp(ext, "chd") || !strcasecmp(ext, "cci")) {
		/* 0.0.3: CCI (Cerbios's compressed images); an image split in parts shows as its first,
		 * game.1.iso (its .2, .3 ... are read with it) */
		return !(dot >= 2 && name[dot - 2] == '.' && name[dot - 1] >= '2' && name[dot - 1] <= '9');
	}
	return false;   // 0.1.59: CHD (chdman createdvd)
}

std::string sizeText(int64_t bytes)
{
	char text[32];
	if (bytes >= (int64_t(1) << 30)) {
		std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / double(int64_t(1) << 30));
	} else {
		std::snprintf(text, sizeof(text), "%.0f MB", double(bytes) / double(1 << 20));
	}
	return text;
}

std::string parentOf(const std::string &path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}

std::string baseName(const std::string &path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool isFolder(const std::string &path)
{
	struct stat st;
	const int r = ps5vfs_is_virtual(path.c_str()) ? ps5vfs_stat(path.c_str(), &st) : stat(path.c_str(), &st);
	return r == 0 && S_ISDIR(st.st_mode);
}

/* OGBox 0.1.43: which places the game list shows (the settings' Game list section). */
struct PlaceFilter {
	bool games{ true }, internal{ true }, ext{ true }, network{ true };
	int usb{ 2 };   // 0 hidden, 1 the connected ones, 2 all eight
};

/* A drive plugged in: its folder is a mount, on another device than the folder above. */
bool isMountPoint(const std::string &path)
{
	struct stat st, up;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && stat(parentOf(path).c_str(), &up) == 0 &&
		st.st_dev != up.st_dev;
}

/* The places the list starts from: internal storage, the drives that are there, the shares; the
 * filter's only (none: every one, as going up from a place's folder needs). */
std::vector<LibraryEntry> listPlaces(const PlaceFilter *filter = nullptr)
{
	const PlaceFilter all;
	const PlaceFilter &f = filter ? *filter : all;
	std::vector<LibraryEntry> places;
	const std::string titleGames = std::string(pshakux_title_dir()) + "/games";
	if (f.games && isFolder(titleGames)) {
		places.push_back({ LibraryEntry::Place, "OGBox's games folder", titleGames, 0, titleGames });
	}
	if (f.internal) {
		places.push_back({ LibraryEntry::Place, "Internal storage", "/data", 0, "/data" });
	}
	for (int i = 0; i < 8 && f.usb > 0; i++) {
		const std::string usb = "/mnt/usb" + std::to_string(i);
		if (f.usb == 1 ? isMountPoint(usb) : isFolder(usb)) {
			places.push_back({ LibraryEntry::Place, "USB drive " + std::to_string(i), usb, 0, usb });
		}
	}
	for (int i = 0; i < 2 && f.ext; i++) {
		const std::string ext = "/mnt/ext" + std::to_string(i);
		if (isFolder(ext)) {
			places.push_back({ LibraryEntry::Place, "Extended storage " + std::to_string(i), ext, 0, ext });
		}
	}
	for (unsigned i = 0; f.network; i++) {
		const char *mount = ps5vfs_mount_point(i);
		if (!mount) {
			break;
		}
		if (std::strncmp(mount, "/smb/", 5) == 0) {
			places.push_back({ LibraryEntry::Place, std::string("Network: ") + (mount + 5), mount, 0, mount });
		}
	}
	return places;
}

/* OGBox 0.0.2: the game list's tabs, as Crux2's: the console's own storage, USB drives, network
 * shares. Each place is under one. */
enum LibraryTab { TabInternal = 0, TabUsb = 1, TabNetwork = 2, TabCount = 3 };

int tabOfPath(const std::string &path)
{
	if (path.rfind("/mnt/usb", 0) == 0) {
		return TabUsb;
	}
	if (path.rfind("/smb", 0) == 0 || path.rfind("/nfs", 0) == 0) {
		return TabNetwork;
	}
	return TabInternal;
}

const char *tabName(int tab)
{
	return tab == TabUsb ? "USB" : tab == TabNetwork ? "Network" : "Internal";
}

/* OGBox 0.0.2: the Xbox dashboard is there when the hard disk's C: drive (its Partition2) holds
 * xboxdash.xbe, whatever the case of its name. */
bool dashboardPresent(const std::string &hdd)
{
	bool found = false;
	if (DIR *d = opendir((hdd + "/Partition2").c_str())) {
		while (struct dirent *e = readdir(d)) {
			if (strcasecmp(e->d_name, "xboxdash.xbe") == 0) {
				found = true;
				break;
			}
		}
		closedir(d);
	}
	return found;
}

/* OGBox 0.1.43: the letter a list entry files under, for L2 / R2 ('#' digits and the rest). */
char letterOf(const std::string &name)
{
	for (const char c : name) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (std::isalpha(u)) {
			return static_cast<char>(std::toupper(u));
		}
		if (std::isdigit(u)) {
			return '#';
		}
	}
	return '#';
}

/* A folder's subfolders and disc images, folders first, by name. false with the reason. */
bool listFolder(const std::string &folder, std::vector<LibraryEntry> &out, std::string &error)
{
	std::vector<LibraryEntry> folders, images;
	const auto add = [&](const std::string &name, bool dir, int64_t size) {
		if (name.empty() || name[0] == '.' || name == "$RECYCLE.BIN" || name == "System Volume Information") {
			return;
		}
		const std::string path = (folder == "/" ? std::string() : folder) + "/" + name;
		if (dir) {
			folders.push_back({ LibraryEntry::Folder, name, path, 0, std::string() });
		} else if (isDiscImage(name)) {
			images.push_back({ LibraryEntry::Image, name, path, size, sizeText(size) });
		}
	};
	if (ps5vfs_is_virtual(folder.c_str())) {
		ps5vfs_dir *dir = ps5vfs_opendir(folder.c_str());
		if (!dir) {
			error = std::strerror(errno ? errno : ENOENT);
			return false;
		}
		ps5vfs_dirent entry;
		while (ps5vfs_readdir(dir, &entry) == 1) {
			add(entry.name, entry.is_directory != 0, entry.size);
		}
		ps5vfs_closedir(dir);
	} else {
		DIR *dir = opendir(folder.c_str());
		if (!dir) {
			error = std::strerror(errno);
			return false;
		}
		while (struct dirent *entry = readdir(dir)) {
			const std::string name = entry->d_name;
			if (name == "." || name == "..") {
				continue;
			}
			const std::string path = (folder == "/" ? std::string() : folder) + "/" + name;
			struct stat st;
			if (stat(path.c_str(), &st) != 0) {
				continue;
			}
			add(name, S_ISDIR(st.st_mode), static_cast<int64_t>(st.st_size));
		}
		closedir(dir);
	}
	const auto byName = [](const LibraryEntry &a, const LibraryEntry &b) {
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	};
	std::sort(folders.begin(), folders.end(), byName);
	std::sort(images.begin(), images.end(), byName);
	out = std::move(folders);
	out.insert(out.end(), images.begin(), images.end());
	return true;
}

/* A listing on a thread of its own: a share's server may take seconds to answer, and the list
 * keeps drawing meanwhile. */
struct Listing {
	std::mutex mutex;
	bool done{ false }, ok{ false };
	std::vector<LibraryEntry> entries;
	std::string error;
};

std::string g_networkStatus;   // at the foot of the game list

/* 0.1.16: a screenshot (L3 + R3 held in a game) as a 24-bit BMP: RGBA rows in, no library. */
bool writeBmp(const std::string &path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t stride)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f) {
		return false;
	}
	const uint32_t row = (w * 3 + 3) & ~3u;
	const uint32_t size = 54 + row * h;
	uint8_t header[54] = { 'B', 'M' };
	const auto put32 = [&](int at, uint32_t v) {
		for (int i = 0; i < 4; i++) header[at + i] = static_cast<uint8_t>(v >> (8 * i));
	};
	put32(2, size);
	put32(10, 54);
	put32(14, 40);
	put32(18, w);
	put32(22, h);
	header[26] = 1;
	header[28] = 24;
	put32(34, row * h);
	std::fwrite(header, 1, sizeof(header), f);
	std::vector<uint8_t> line(row, 0);
	for (uint32_t y = 0; y < h; y++) {
		const uint8_t *src = rgba + size_t(h - 1 - y) * stride;
		for (uint32_t x = 0; x < w; x++) {
			line[x * 3 + 0] = src[x * 4 + 2];
			line[x * 3 + 1] = src[x * 4 + 1];
			line[x * 3 + 2] = src[x * 4 + 0];
		}
		std::fwrite(line.data(), 1, row, f);
	}
	std::fclose(f);
	return true;
}

/* 0.1.17: the share layer's last failure ("connecting to smb://... failed: <the server's reason>"),
 * shown under the list when a share cannot be listed (it was only in a log a game's start replaced) */
std::mutex g_vfsErrorMutex;
std::string g_vfsError;

void vfsLog(const char *line)
{
	if (std::strstr(line, "failed") || std::strstr(line, "cannot") || std::strstr(line, "refused")) {
		std::lock_guard<std::mutex> lock(g_vfsErrorMutex);
		g_vfsError = line;
	}
	pshakux_log("%s", line);   // the layer's lines begin with "vfs: "
}

/* The virtual file layer: its stand-in file, its log, the shares of network.cfg (a commented
 * example is written the first time). */
void startFileLayer()
{
	static std::string shadow;
	shadow = std::string(pshakux_title_dir()) + "/sce_sys/param.json";
	ps5vfs_set_shadow_path(shadow.c_str());
	ps5vfs_set_log(vfsLog);
	struct stat st;
	if (stat(kNetworkFile, &st) != 0) {
		if (FILE *f = std::fopen(kNetworkFile, "w")) {
			std::fputs("# OGBox's network shares: one \"path =\" line per share, shown in the game list.\n"
				"# SMB: server/share/folder (the server by its IP address); NFS: nfs://server/export/folder\n"
				"#\n"
				"# path = 192.168.1.10/Games/Xbox\n"
				"# path = nfs://192.168.1.20/volume1/games/xbox\n"
				"# user = guest\n"
				"# password =\n", f);
			std::fclose(f);
			pshakux_log("ogbox: wrote an example %s", kNetworkFile);
		}
	}
	chmod(kNetworkFile, 0666);
	/* 0.1.15: /data/ogbox/network.cfg, else the title folder's. A file that cannot be opened is
	 * said, with the reason, in the log and at the foot of the game list (0.1.14 counted it as
	 * no shares and said nothing: a network.cfg copied in over FTP was not read). One the title
	 * may not read is made readable when it can be (chmod 0666), then tried again. */
	/* 0.1.16: every place a network.cfg may be is read, and the shares come from the first that
	 * has path lines (0.1.15 stopped at /data/ogbox's, OGBox's own example with every line a
	 * comment, while the user's edited file was elsewhere). */
	const std::string titleDir = pshakux_title_dir();
	g_networkStatus = std::string("no shares set up (") + kNetworkFile + ")";
	std::string chosen;
	for (const std::string &path : { std::string(kNetworkFile), titleDir + "/network.cfg", std::string("/data/network.cfg"),
			titleDir + "/netfork.cfg" }) {
		if (stat(path.c_str(), &st) != 0) {
			continue;
		}
		FILE *probe = std::fopen(path.c_str(), "r");
		int error = probe ? 0 : errno;
		if (!probe) {
			const int changed = chmod(path.c_str(), 0666);
			pshakux_log("ogbox: %s cannot be opened (%s; mode %o, owner %d): chmod 0666 %s", path.c_str(),
				std::strerror(error), static_cast<unsigned>(st.st_mode & 07777), static_cast<int>(st.st_uid),
				changed == 0 ? "done" : std::strerror(errno));
			probe = std::fopen(path.c_str(), "r");
			error = probe ? 0 : errno;
		}
		if (!probe) {
			g_networkStatus = path + " cannot be read: " + std::strerror(error);
			pshakux_log("ogbox: %s", g_networkStatus.c_str());
			continue;
		}
		char line[512];
		unsigned lines = 0, paths = 0;
		while (std::fgets(line, sizeof(line), probe)) {
			lines++;
			std::string text = trim(line);
			if (text.compare(0, 3, "\xef\xbb\xbf") == 0) {
				text = trim(text.substr(3));
			}
			if (strncasecmp(text.c_str(), "path", 4) == 0 && text.find('=') != std::string::npos) {
				paths++;
			}
		}
		std::fclose(probe);
		pshakux_log("ogbox: %s: %u lines, %u path lines", path.c_str(), lines, paths);
		if (paths == 0) {
			g_networkStatus = path + " has no \"path =\" line (lines starting with # are notes)";
			continue;
		}
		chosen = path;
		break;
	}
	if (!chosen.empty()) {
		/* 0.0.3: mounted as the settings edit them (ogbox_shares): each at /smb/<its name> */
		ogbox::ShareFile file;
		int mounted = 0;
		if (ogbox::readShares(chosen, file)) {
			std::vector<std::string> taken;
			for (ogbox::Share &share : file.shares) {
				std::string why;
				if (ogbox::mountShare(share, file, taken, &why)) {
					taken.push_back(share.mount);
					mounted++;
				} else {
					pshakux_log("ogbox: share %s: %s", share.name.c_str(), why.c_str());
				}
			}
		}
		g_networkStatus = std::to_string(mounted) + " share(s) from " + chosen;
		pshakux_log("ogbox: %s", g_networkStatus.c_str());
	}
}

std::string readLastFolder()
{
	std::string folder;
	if (FILE *f = std::fopen(kLibraryFile, "r")) {
		char line[1024];
		if (std::fgets(line, sizeof(line), f)) {
			folder = trim(line);
		}
		std::fclose(f);
	}
	return folder;
}

void writeLastFolder(const std::string &folder)
{
	if (FILE *f = std::fopen(kLibraryFile, "w")) {
		std::fprintf(f, "%s\n", folder.c_str());
		std::fclose(f);
	}
}

/* OGBox 0.1.43: the disc last started from the game list, selected when the list opens again. */
std::string readLastGame()
{
	std::string path;
	if (FILE *f = std::fopen(kLastGameFile, "r")) {
		char line[1024];
		if (std::fgets(line, sizeof(line), f)) {
			path = trim(line);
		}
		std::fclose(f);
	}
	return path;
}

void writeLastGame(const std::string &path)
{
	if (FILE *f = std::fopen(kLastGameFile, "w")) {
		std::fprintf(f, "%s\n", path.c_str());
		std::fclose(f);
	}
}

/* OGBox 0.1.26: one disc read at a time for the game list's details (the share's disc layer has
 * one disc descriptor; the mods' catalog is the core's one table). */
std::mutex &g_discInfoMutex = ogbox::discMutex();   // 0.0.3: the covers' title images take it too

} // namespace

/* The overlay's colours and typeface: src/ogbox_theme.cpp (at file scope, where the base class
 * looks for them; this file is compiled inside a namespace, src/wrap.cpp.in). */

class VulkanExample : public VulkanExampleBase
{
public:
	BootConfig boot;
	bool configured{ false };
	/* 0.0.6: the disc image is on a share that didn't answer at the start: tried again each second
	 * (on a thread of its own: a share that doesn't answer can take a while to say so) */
	struct ShareWait {
		bool active{ false };
		double since{ 0.0 }, nextTry{ 0.0 };
		std::shared_ptr<std::atomic<int>> result;   // 0 trying, 1 there, 2 not yet
	} shareWait;
	std::string problem;

	// The picture: one sampled image, a staging buffer per frame in flight
	struct {
		VkImage image{ VK_NULL_HANDLE };
		VkDeviceMemory memory{ VK_NULL_HANDLE };
		VkImageView view{ VK_NULL_HANDLE };
		uint32_t width{ 0 }, height{ 0 };
		VkImageLayout layout{ VK_IMAGE_LAYOUT_UNDEFINED };
		VkFormat format{ VK_FORMAT_R8G8B8A8_UNORM };
	} picture;
	struct Staging {
		VkBuffer buffer{ VK_NULL_HANDLE };
		VkDeviceMemory memory{ VK_NULL_HANDLE };
		void *mapped{ nullptr };
		VkDeviceSize size{ 0 };
	};
	std::array<Staging, maxConcurrentFrames> staging{};
	VkSampler sampler{ VK_NULL_HANDLE };
	VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkPipeline pipeline{ VK_NULL_HANDLE };

	uint64_t frameSeen{ 0 };
	bool upload{ false };       // this frame copies a new picture in
	bool havePicture{ false };  // a picture has been shown: it stays until a newer one
	// OGBox 0.0.7: frame generation (ogbox_framegen.h): the game's frames go into its ring, and the
	// picture is the newest of them or one made between the last two, as the pacer says
	ogbox::FrameGen frameGen;
	ogbox::FgMethod fgWanted{ ogbox::FgMethod::Off };
	uint32_t fgQuality{ 1 };
	PS5FrameGen::Pacer fgPacer;
	bool fgFailed{ false };      // it could not be made (or failed): the frames go straight to the picture
	bool fgFresh{ false };       // a new game frame was taken this refresh
	bool fgPush{ false };        // ... and this frame's staging buffer is copied into the ring
	bool fgShow{ false };        // the picture is replaced by the ring's (generated, or the newest)
	bool fgMade{ false };        // the picture now is a generated one
	uint64_t fgShownCount{ 0 }, fgMadeCount{ 0 };   // pictures put on the screen / of them made, since the start
	uint64_t fgShownMark{ 0 }, fgShownMark2{ 0 }, fgMadeMark{ 0 }, fgPicMark{ 0 };
	double fgLogAt{ 0.0 }, fgRateAt{ 0.0 };
	float fgShownFps{ 0.0f };    // the pictures a second the screen was given (0: frame generation is not running)
	float fgTime{ 1.0f };
	bool pictureChanged{ false };   // upload or fgShow: the picture has new content this frame
	// OGBox 0.1.6: until the game's first frame the screen shows OGBox's logo (assets/ogbox-logo.png,
	// ps5/art/ogbox-logo.png as it is), letterboxed on black at its own shape.
	std::vector<uint8_t> logoPixels;
	uint32_t logoWidth{ 0 }, logoHeight{ 0 };
	bool showingLogo{ false };  // the picture is the logo, not a game's frame
	uint64_t framesDrawn{ 0 }, picturesTaken{ 0 };
	uint32_t players{ 0 };
	double quitHeldSince{ -1.0 };
	int lastState{ -1 };
	bool relaunchAsked{ false };

	// The game list (0.1.14): shown when no game was named
	bool library{ false };
	std::string folder;                       // "" for the places
	std::vector<LibraryEntry> entries;
	std::shared_ptr<Listing> listing;         // the listing under way, if any
	std::string listError;
	int selected{ 0 };
	std::string startPath;                    // chosen: started on the next frame
	int startDelay{ 0 };
	std::string selectName;                   // to select once the listing comes (going up)
	std::string startState;                   // 0.1.53: the game starts with this save state
	double shotHeldSince{ -1.0 };             // L3 + R3 held since (a screenshot)
	bool shotWanted{ false };
	unsigned shotsTaken{ 0 };

	// ---- OGBox 0.1.26: the menus (the boot screen, the game list, the settings, the mods) ----
	enum class Screen { Notice, Boot, Library, Settings, Mods, Storage, Hdd };
	Screen screen{ Screen::Library };
	Screen afterNotice{ Screen::Boot };       // 0.0.1: where the experimental build's notice goes on CROSS
	/* 0.0.1: the updater (Crux2's): the state last answered, whether the user asked (a check by
	 * hand says "the newest" too) and whether an install was chosen */
	PS5Update::State updateSeen{ PS5Update::State::Idle };
	bool updateByHand{ false }, updateWanted{ false };
	double screenSince{ 0.0 };
	struct pad menuPad{};                     // player 0's, `pressed` against the last frame's
	ogbox::Settings prefs;                    // the app's; in a run, with the game's own on top
	std::vector<LibraryEntry> places;         // the left column
	double placesRead{ -100.0 };
	// 0.0.2: the tabs (Internal, USB, Network: L1 / R1) and the folder each was left at; whether
	// the hard disk has the Xbox dashboard (its row shows only then)
	int tab{ TabInternal };
	std::string tabFolder[TabCount];
	bool haveDash{ false };
	// 0.0.2: a game that started another XBE: its last frame, shown until the new XBE's first
	std::vector<uint8_t> frozenPixels;
	uint32_t frozenWidth{ 0 }, frozenHeight{ 0 };
	float frozenAspect{ 0.0f };
	bool showingFrozen{ false };
	// 0.1.43: back from a game, the list fades in (no boot screen); L2 / R2 a letter on
	double fadeSince{ -1.0 };
	float trigL{ 0.0f }, trigR{ 0.0f };      // the triggers last frame
	double trigHeldSince{ -1.0 }, trigNext{ 0.0 };
	char jumpLetter{ 0 };
	double jumpSince{ -1.0 };
	int jumpTo{ -1 };
	// 0.1.45: the covers as images ImGui draws (the UI overlay's addTexture), the least used let go
	struct CoverTex {
		vks::Texture2D tex{};
		VkDescriptorSet set{ VK_NULL_HANDLE };
		uint32_t w{ 0 }, h{ 0 };
		uint64_t used{ 0 };
		bool failed{ false };
	};
	std::map<std::string, CoverTex> coverTex;
	std::vector<std::pair<uint64_t, CoverTex>> coverRetired;   // destroyed some frames later
	unsigned coverUploads{ 0 };                                 // this frame's
	/* 0.0.4: the covers decoded on a thread of their own (a JPEG scan took the frame 20-40 ms on the
	 * interface's thread, two a frame: back from a game, the home filled in cover by cover as if they
	 * were downloading again); the frame only uploads what is decoded */
	struct Decoded {
		stbi_uc *px{ nullptr };
		int w{ 0 }, h{ 0 };
		bool failed{ false };
	};
	struct Decoder {
		std::mutex mutex;
		std::condition_variable cv;
		std::deque<std::string> queue;          // paths
		std::set<std::string> pending;
		std::map<std::string, Decoded> done;    // by path
		bool started{ false };

		void request(const std::string &path)
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (done.count(path) || !pending.insert(path).second) {
				return;
			}
			queue.push_back(path);
			if (!started) {
				started = true;
				std::thread([this] { run(); }).detach();
			}
			cv.notify_one();
		}
		/* the decoded picture of path, taken (false: not yet) */
		bool take(const std::string &path, Decoded &out)
		{
			std::lock_guard<std::mutex> lock(mutex);
			auto it = done.find(path);
			if (it == done.end()) {
				return false;
			}
			out = it->second;
			done.erase(it);
			return true;
		}
		void run()
		{
			pshakux_thread_named("ogbox-coverdec");
			for (;;) {
				std::string path;
				{
					std::unique_lock<std::mutex> lock(mutex);
					cv.wait(lock, [&] { return !queue.empty(); });
					path = queue.back();          // the newest asked first: what is on screen now
					queue.pop_back();
				}
				Decoded d;
				int n = 0;
				const std::string file = path.substr(0, path.find('\x01'));   // (a blur's key: path \x01 blur)
				d.px = stbi_load(file.c_str(), &d.w, &d.h, &n, 4);
				d.failed = !d.px || d.w <= 0 || d.h <= 0 || d.w > 2048 || d.h > 2048;
				if (d.failed && d.px) {
					stbi_image_free(d.px);
					d.px = nullptr;
				}
				std::lock_guard<std::mutex> lock(mutex);
				pending.erase(path);
				done[path] = d;
				while (done.size() > 96) {        // decoded but never drawn (scrolled past)
					auto old = done.begin();
					if (old->second.px) {
						stbi_image_free(old->second.px);
					}
					done.erase(old);
				}
			}
		}
	};
	Decoder coverDecoder;
	// the selected disc's details, read on a thread of their own once the selection rests
	struct Details {
		std::mutex mutex;
		bool done{ false };
		OgboxGameInfo info{};
		int mods{ 0 }, modsOn{ 0 }, cheats{ 0 }, cheatsOn{ 0 }, overrides{ 0 };
		int patches{ 0 }, patchesOn{ 0 };   // 0.1.45: gamedata/patches.db's
		int trainers{ 0 };                  // 0.1.54: the trainers for it, and whether one is chosen
		bool trainerOn{ false };
	};
	std::shared_ptr<Details> details;
	std::string detailsPath, restingPath;
	double restingSince{ 0.0 }, detailsSince{ 0.0 };
	// the settings screen: the app's, or one game's
	int section{ 0 };
	bool forGame{ false };
	std::string forGameKey, forGameTitle;
	int modsSel{ -1 };                        // the Mods screen's chosen row (-1: its first)
	// the mods and cheats screen
	std::vector<OgboxModEntry> modList;
	uint32_t modsCrc{ 0 };                    // 0.0.2: the disc's default.xbe's CRC-32 (0: unread)
	std::string modsKey, modsTitle;
	// 0.1.54: the game's trainers on the Mods screen (core/ogbox_trainers.h)
	struct TrainerView {
		std::vector<OgboxTrainer> list;
		unsigned generation{ ~0u };
		uint32_t titleId{ 0 }, xbeTime{ 0 };
		std::string chosen, bits, scroller;   // the chosen one's file under its folder, its options ('1' on)
		std::vector<std::string> options;
	} tv;
	bool trainerScanStarted{ false };
	uint32_t runXbeTime{ 0 };                 // the running game's default.xbe date
	unsigned ledSeq{ 0x0F };                  // the console's LED as the trainer last set it
	double ledUntil{ 0.0 };
	std::string ledText;
	// ---- 0.1.53: the in-game quick menu, the game page, the hard disk manager ----
	std::string runKey;                       // the running game's title ID and version
	int pictureBuffer{ -1 };                  // the staging buffer the shown picture came in
	struct Quick {
		bool open{ false };
		int sel{ 0 };
		int slot{ 1 };
		enum class Sub { None, Settings, Mods } sub{ Sub::None };
		bool optionsDown{ false };            // OPTIONS went down while the menu was open
		double optionsSince{ -1.0 };          // OPTIONS held since (the "held" mode)
		double startPulseUntil{ 0.0 };        // a short OPTIONS press: the Xbox's Start, this long
		double restartArmed{ 0.0 };
		double openedAt{ 0.0 };
		std::vector<uint8_t> thumb;           // the frozen picture, 320 wide, for a state's card
		uint32_t thumbW{ 0 }, thumbH{ 0 };
		std::string busy;                     // "Saving..." while a save runs
		std::shared_ptr<std::atomic<int>> result;   // 0 running, 1 saved, 2 failed
		std::shared_ptr<std::string> why;
		enum class After { Resume, Library } after{ After::Resume };
		int busySlot{ 1 };
		std::string pendingLoad;              // a state to start the game with, next frame
		bool pendingRestart{ false };
	} qm;
	bool touchBack[PAD_PLAYERS]{};            // a touch pad click began on its left half (Back)
	bool touchWas[PAD_PLAYERS]{};
	ogbox::PadMap padMap;                     // Settings > Controller, the game's own on top
	int rumblePercent{ 100 };
	double buzzUntil{ 0.0 };                  // 0.0.6: the settings' test buzz stops then                 // 0.0.6: Settings > Controller > Rumble strength
	ogbox::TapLatch tapLatch[PAD_PLAYERS];    // a tap shorter than a frame still reaches the game
	std::string toast;
	double toastUntil{ 0.0 };
	// a small list of choices over a screen (a confirmation, a place, an action)
	struct Popup {
		bool open{ false };
		std::string title, text;
		std::vector<std::string> items;
		int sel{ 0 };
		std::function<void(int)> pick;        // -1: cancelled
	} pop;
	struct GamePage {
		bool open{ false };
		std::string path, title;
		OgboxGameInfo info{};
		int pill{ 0 };
		bool states{ false };                 // the load-state list is shown
		bool returnTo{ false };               // a screen opened from it comes back to it
		std::string savesKnown;               // 0.0.6: the path whose saves were looked for
		bool hasSaves{ false };
		int stateSel{ 0 };
		double since{ 0.0 };
		int readTries{ 0 };                   // 0.0.3: its details read again after a failed read
	} gp;
	struct Hdd {
		std::vector<ogbox::HddTitle> titles;
		int pane{ 0 };                        // 0 the games, 1 the saves (or the copies to bring in)
		int selT{ 0 }, selS{ 0 };
		uint32_t onlyTitle{ 0 };              // opened from a game's page: that game first
		bool importing{ false };              // the right pane lists copies to bring in
		std::string importPlace, importName;
		std::vector<ogbox::SaveCopy> copies;
		uint64_t cacheBytes{ 0 }, usedBytes{ 0 };
		Screen back{ Screen::Library };
	} hdd;
	// 0.1.48: the data folder's screen (the first start's, and Settings > Data folder)
	enum class StorageStep { Places, Browse, Confirm, Moving, Done };
	StorageStep storageStep{ StorageStep::Places };
	bool storageFirst{ false };
	std::string storageFolder;                // Browse: "" the drives
	std::vector<std::string> storageSubs;     // its folders
	std::string storageTarget, storageMessage;
	uint64_t storageHave{ 0 };                // what the data folder holds now
	std::vector<ogbox::StoragePlace> storageList;
	int storageSel{ -1 };                     // the data folder screen's chosen row (-1: its first)
	// 0.1.52: the theme (Settings > Interface > Theme), applied at the next frame's start (0.0.3: Dark or Code)
	bool themeCode{ false };
	int themeApplied{ -1 };
	vks::Texture2D logoTex{};                 // OGBox's logo, as an image the menus draw
	VkDescriptorSet logoSet{ VK_NULL_HANDLE };
	bool logoTried{ false };
	// the code rain
	struct Drop {
		float x, y, speed;
		int length;
		std::string glyphs;
		float alpha;
	};
	std::vector<Drop> rain;
	float rainWidth{ 0.0f }, rainGlyph{ 0.0f };
	double rainLast{ 0.0 };
	std::minstd_rand rng{ 1987u };
	// a run's picture and overlay (the settings' Graphics and Interface)
	VkPipeline pipelineFxaa{ VK_NULL_HANDLE }, pipelineSgsr{ VK_NULL_HANDLE };   // (0.0.6: FSR1's EASU in SGSR's place)
	VkPipeline pipelineSharp{ VK_NULL_HANDLE };                                  // 0.0.6: Bilinear (Sharp)
	std::string aspectMode{ "auto" };
	bool fxaaOn{ false }, sgsrOn{ false }, showFps{ true }, rainOn{ true };
	std::string fpsCorner{ "1" };   // 0.0.6: Settings' FPS counter: "tl", "1" (top right), "bl", "br"
	bool sharpOn{ false };          // 0.0.6: Bilinear (Sharp)
	/* 0.1.58: the settings' anti-aliasing beside FXAA: SMAA 1x (three passes at the picture's size
	 * into images of its own, then shown as the picture is) and SSAA (the resolution scale raised
	 * above the screen, the picture averaged down over every texel a pixel covers) */
	bool smaaOn{ false }, ssaaOn{ false };
	bool cropOn{ false };       // 0.1.59: the overscan cropped (3/80 a side, hakuX-lr's)
	struct AaImage {
		VkImage image{ VK_NULL_HANDLE };
		VkDeviceMemory memory{ VK_NULL_HANDLE };
		VkImageView view{ VK_NULL_HANDLE };
	};
	AaImage smaaEdges{}, smaaWeights{}, smaaOut{}, smaaArea{}, smaaSearch{};
	uint32_t smaaW{ 0 }, smaaH{ 0 };
	VkImageView smaaPictureView{ VK_NULL_HANDLE };   // the picture the sets were written for
	bool smaaValid{ false };                         // smaaOut holds the current picture
	VkSampler pointSampler{ VK_NULL_HANDLE };
	VkDescriptorPool smaaPool{ VK_NULL_HANDLE };
	VkDescriptorSetLayout smaaSetLayout{ VK_NULL_HANDLE };
	VkPipelineLayout smaaPipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet smaaEdgeSet{ VK_NULL_HANDLE }, smaaWeightSet{ VK_NULL_HANDLE }, smaaBlendSet{ VK_NULL_HANDLE },
		smaaPresentSet{ VK_NULL_HANDLE };
	VkPipeline smaaEdge{ VK_NULL_HANDLE }, smaaWeight{ VK_NULL_HANDLE }, smaaBlend{ VK_NULL_HANDLE },
		pipelineSsaa{ VK_NULL_HANDLE };
	double fpsSince{ -1.0 };
	uint64_t fpsPictures{ 0 }, fpsDraws{ 0 };
	float fps{ 0.0f }, drawRate{ 0.0f };
	std::vector<std::string> bootLines;
	// 0.1.28: the stall probe (a game without a new frame for 0.4 s: where its CPU's thread is)
	uint64_t probePictures{ 0 };
	double probeLastFrame{ -1.0 }, probeNext{ 0.0 };
	unsigned probeThisStall{ 0 }, probeTotal{ 0 };
	int probeWanted{ -1 };

	/* 0.0.2: the tabs shown: Internal always, USB while the settings show USB drives, Network while
	 * they show the shares. */
	std::vector<int> tabsShown() const
	{
		const PlaceFilter f = placeFilter();
		std::vector<int> t{ TabInternal };
		if (f.usb > 0) {
			t.push_back(TabUsb);
		}
		if (f.network) {
			t.push_back(TabNetwork);
		}
		return t;
	}

	/* 0.0.2: L1 (-1) or R1 (+1): the tab before or after, round and round, at the folder it was
	 * left at. */
	void switchTab(int dir)
	{
		const std::vector<int> t = tabsShown();
		const int n = static_cast<int>(t.size());
		int at = static_cast<int>(std::find(t.begin(), t.end(), tab) - t.begin());
		at = at >= n ? 0 : (at + dir + n) % n;
		if (t[static_cast<size_t>(at)] == tab) {
			return;
		}
		tab = t[static_cast<size_t>(at)];
		pshakux_log("library: the %s tab", tabName(tab));
		openFolder(tabFolder[tab]);
	}

	/* 0.1.43: the places the settings' Game list section shows. */
	PlaceFilter placeFilter() const
	{
		PlaceFilter f;
		f.games = prefs.global("list_games") != "0";
		f.internal = prefs.global("list_internal") != "0";
		f.ext = prefs.global("list_ext") != "0";
		f.network = prefs.global("list_network") != "0";
		f.usb = std::atoi(prefs.global("list_usb").c_str());
		return f;
	}

	/* 0.1.43: L2 (-1) or R2 (+1): the first entry of the previous or the next letter in the list
	 * (folders, then discs, each by name); L2 inside a letter goes to its first entry first. */
	void jumpByLetter(int dir)
	{
		if (entries.empty() || listing) {
			return;
		}
		const int n = static_cast<int>(entries.size());
		const auto key = [&](int i) -> int {
			const LibraryEntry &e = entries[static_cast<size_t>(i)];
			if (e.kind != LibraryEntry::Folder && e.kind != LibraryEntry::Image) {
				return -1;
			}
			return (e.kind == LibraryEntry::Image ? 256 : 0) + static_cast<unsigned char>(letterOf(e.name));
		};
		int i = selected >= 0 && selected < n ? selected : 0;
		int to = i;
		if (dir > 0) {
			const int k = key(i);
			int j = i + 1;
			while (j < n && key(j) == k) {
				j++;
			}
			to = j < n ? j : n - 1;
		} else {
			if (key(i) < 0) {
				return;
			}
			int s0 = i;
			while (s0 > 0 && key(s0 - 1) == key(i)) {
				s0--;
			}
			if (s0 < i) {
				to = s0;
			} else if (s0 > 0 && key(s0 - 1) >= 0) {
				to = s0 - 1;
				while (to > 0 && key(to - 1) == key(s0 - 1)) {
					to--;
				}
			}
		}
		if (to != i || selected != to) {
			selected = to;
			jumpTo = to;
		}
		if (key(to) >= 0) {
			jumpLetter = letterOf(entries[static_cast<size_t>(to)].name);
			jumpSince = now_seconds();
		}
	}

	/* 0.1.45: a cover's image for ImGui (null when not ready: asked for, downloading, or none), its size in
	 * *w, *h. A frame decodes and uploads at most two. */
	ImTextureID coverImage(const std::string &name, float *w, float *h)
	{
		return coverImage(std::vector<std::string>{ name }, w, h);
	}

	/* 0.0.2: the first of names with a cover (ogbox::coverNames) */
	ImTextureID coverImage(const std::vector<std::string> &names, float *w, float *h)
	{
		if (names.empty() || prefs.global("list_covers") == "0") {
			return nullptr;
		}
		std::string path;
		if (ogbox::Covers::get().findAny(names, path) != ogbox::Covers::State::Ready) {
			return nullptr;
		}
		return fileImage(path, path, w, h, true);
	}

	/* 0.1.53: an image file (a cover, a save state's picture) as an image ImGui draws, kept under key
	 * (a state's path and time: a new save is another key); cover: one unreadable is removed. */
	ImTextureID fileImage(const std::string &path, const std::string &key, float *w, float *h, bool cover = false)
	{
		auto it = coverTex.find(key);
		if (it == coverTex.end()) {
			if (coverUploads >= 4) {
				return nullptr;
			}
			/* 0.0.4: decoded on the decoder's thread; here only uploaded */
			Decoded d;
			if (!coverDecoder.take(path, d)) {
				coverDecoder.request(path);
				return nullptr;
			}
			coverUploads++;
			CoverTex t;
			stbi_uc *pixels = d.px;
			const int iw = d.w, ih = d.h;
			if (d.failed) {
				/* 0.0.4: kept (it may be a picture of your own: PSFlyCast never deletes one), not
				 * tried again this run */
				t.failed = true;
				if (cover) {
					pshakux_log("covers: %s cannot be read (a PNG or JPEG up to 2048 x 2048): not shown", path.c_str());
				}
				coverTex.emplace(key, std::move(t));
				return nullptr;
			}
			t.tex.fromBuffer(pixels, static_cast<VkDeviceSize>(iw) * ih * 4, VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(iw),
				static_cast<uint32_t>(ih), vulkanDevice, queue);
			stbi_image_free(pixels);
			t.set = ui.addTexture(t.tex.sampler, t.tex.view);
			t.w = static_cast<uint32_t>(iw);
			t.h = static_cast<uint32_t>(ih);
			/* 0.1.50: drawn now (0.1.45 left it 0: the new cover was always the least used one, never
			 * let go, and the cache grew until the overlay's 96 image slots ran out) */
			t.used = framesDrawn;
			if (t.set == VK_NULL_HANDLE) {
				t.tex.destroy();   // (no slot: drawn as a placeholder, tried again later)
				return nullptr;
			}
			it = coverTex.emplace(key, std::move(t)).first;
			while (coverTex.size() > 48) {
				// the least recently drawn one goes (its image destroyed once no frame in flight draws it)
				auto old = coverTex.begin();
				for (auto c = coverTex.begin(); c != coverTex.end(); ++c) {
					if (c->second.used < old->second.used) {
						old = c;
					}
				}
				if (old == it) {
					break;
				}
				coverRetired.emplace_back(framesDrawn + 4, std::move(old->second));
				coverTex.erase(old);
			}
		}
		CoverTex &t = it->second;
		if (t.failed || t.set == VK_NULL_HANDLE) {
			return nullptr;
		}
		t.used = framesDrawn;
		*w = static_cast<float>(t.w);
		*h = static_cast<float>(t.h);
		return reinterpret_cast<ImTextureID>(t.set);
	}

	/* Each frame: the covers no frame in flight draws any more destroyed. */
	void coverHousekeeping()
	{
		coverUploads = 0;
		for (size_t i = 0; i < coverRetired.size();) {
			if (framesDrawn >= coverRetired[i].first) {
				ui.freeTexture(coverRetired[i].second.set);
				coverRetired[i].second.tex.destroy();
				coverRetired.erase(coverRetired.begin() + static_cast<long>(i));
			} else {
				i++;
			}
		}
	}

	/* 0.0.2: the names a disc's cover may go by, best first (ogbox::coverNames): the file's, then its
	 * title ID's records once a disc's certificate was read (info, or one read before: coverTitles,
	 * so the list's tiles fall back as the details do). 0.1.45 to 0.0.1 took the title ID's record
	 * alone once it was known, and a cover libretro has under the file's release went. */
	struct CoverTitle {
		uint32_t titleId, regions, version;
	};
	std::unordered_map<std::string, CoverTitle> coverTitles;   // file name: its certificate's

	/* 0.0.3: with the other sources (ogbox::coverSources): MobCat's scan first, the disc's title
	 * image last (path: the disc's) */
	std::vector<std::string> coverNames(const std::string &fileName, const OgboxGameInfo *info = nullptr,
		const std::string &path = std::string())
	{
		if (info && info->ok && info->title_id) {
			coverTitles[fileName] = { info->title_id, info->regions, info->version };
			return ogbox::coverSources(fileName, path, info->title_id, info->regions, info->version);
		}
		const auto it = coverTitles.find(fileName);
		return it == coverTitles.end() ? ogbox::coverSources(fileName, path)
			: ogbox::coverSources(fileName, path, it->second.titleId, it->second.regions, it->second.version);
	}

	std::vector<std::string> coverNames(const LibraryEntry &e, const OgboxGameInfo *info = nullptr)
	{
		return coverNames(e.name, info, e.kind == LibraryEntry::Image ? e.path : std::string());
	}

	/* The name the list shows for an entry (the settings' Names). */
	std::string shownName(const LibraryEntry &e) const
	{
		return e.kind == LibraryEntry::Image && prefs.global("list_names") == "short" ? ogbox::shortName(e.name) : e.name;
	}

	/* Opens a folder ("" for the places), listing it on a thread of its own. */
	void openFolder(const std::string &path, const std::string &select = std::string())
	{
		folder = path;
		if (!path.empty()) {
			tab = tabOfPath(path);   // 0.0.2: a folder is in its place's tab
		}
		tabFolder[tab] = path;
		selectName = select;
		entries.clear();
		listError.clear();
		selected = 0;
		jumpTo = -1;
		listing.reset();
		preferGame = select.empty();   // 0.0.3: a new listing chooses its first game
		lastGame = lastCard = -1;
		if (path.empty()) {
			const PlaceFilter f = placeFilter();
			for (const LibraryEntry &place : listPlaces(&f)) {
				if (tabOfPath(place.path) == tab) {
					entries.push_back(place);
				}
			}
			/* 0.0.3: a tab's home: its places and what OGBox has as cards, the games played last
			 * from its places along the covers' row */
			if (tab == TabInternal) {
				if (dashboardPresent(boot.hdd.empty() ? ogbox::storagePath("hdd") : boot.hdd)) {
					entries.push_back({ LibraryEntry::Dashboard, "Xbox dashboard", "none", 0, std::string() });
				}
				entries.push_back({ LibraryEntry::Hdd, "Xbox hard disk", std::string(), 0, std::string() });
			}   // 0.0.6: no "Add a network share" card: the shares are Settings > Network and shares
			for (const auto &played : ogbox::recentlyPlayed(60)) {
				const std::string &game = played.second;
				if (tabOfPath(game) != tab || !isDiscImage(baseName(game))) {
					continue;
				}
				struct stat st;
				if (!ps5vfs_is_virtual(game.c_str()) && stat(game.c_str(), &st) != 0) {
					continue;   // (gone, or its drive not plugged in)
				}
				entries.push_back({ LibraryEntry::Image, baseName(game), game, 0, std::string() });
				if (entries.size() > 40) {
					break;
				}
			}
			pickSelection();
			return;
		}
		auto job = std::make_shared<Listing>();
		listing = job;
		std::thread([job, path]() {
			std::vector<LibraryEntry> found;
			std::string error;
			const bool ok = listFolder(path, found, error);
			std::lock_guard<std::mutex> lock(job->mutex);
			job->entries = std::move(found);
			job->error = error;
			job->ok = ok;
			job->done = true;
		}).detach();
	}

	void pickSelection()
	{
		if (selectName.empty()) {
			return;
		}
		for (size_t i = 0; i < entries.size(); i++) {
			if (entries[i].name == selectName || entries[i].path == selectName) {
				selected = static_cast<int>(i);
				break;
			}
		}
		selectName.clear();
	}

	/* The listing's result into the list, when it has come. */
	void collectListing()
	{
		if (!listing) {
			return;
		}
		std::lock_guard<std::mutex> lock(listing->mutex);
		if (!listing->done) {
			return;
		}
		/* 0.0.6: no Up card in a network share: its folder is the one Settings > Network and shares
		 * names (CIRCLE still goes back) */
		const bool upCard = tab != TabNetwork;
		if (listing->ok) {
			entries.clear();
			if (upCard) {
				entries.push_back({ LibraryEntry::Up, "..", parentOf(folder), 0, std::string() });
			}
			entries.insert(entries.end(), listing->entries.begin(), listing->entries.end());
			if (prefs.global("list_sort") == "played") {
				// 0.1.45: the discs played last first (folders stay first, by name)
				std::stable_sort(entries.begin() + (upCard ? 1 : 0), entries.end(), [](const LibraryEntry &a, const LibraryEntry &b) {
					if (a.kind != b.kind) {
						return a.kind == LibraryEntry::Folder;
					}
					return a.kind == LibraryEntry::Image && ogbox::playedAt(a.path) > ogbox::playedAt(b.path);
				});
			}
			writeLastFolder(folder);
			pshakux_log("library: %s: %zu entries", folder.c_str(), entries.size() - (upCard ? 1 : 0));
		} else {
			listError = listing->error;
			{
				std::lock_guard<std::mutex> lock(g_vfsErrorMutex);
				if (!g_vfsError.empty() && ps5vfs_is_virtual(folder.c_str())) {
					listError += " - " + g_vfsError;
				}
			}
			entries.clear();
			if (upCard) {
				entries.push_back({ LibraryEntry::Up, "..", parentOf(folder), 0, std::string() });
			}
			pshakux_log("library: %s cannot be listed: %s", folder.c_str(), listError.c_str());
		}
		listing.reset();
		pickSelection();
	}

	/* The folder above, or the places from a place's own folder. */
	void goUp()
	{
		if (folder.empty()) {
			/* 0.0.2: nothing above a tab's places: CIRCLE asks before closing OGBox (Crux2's way) */
			openPopup("Close OGBox?", "Are you sure?", { "No", "Yes, close it" }, [this](int i) {
				if (i == 1) {
					pshakux_log("library: Quit OGBox (CIRCLE, confirmed)");
					quit = true;
				}
			});
			return;
		}
		for (const LibraryEntry &place : listPlaces()) {
			if (place.path == folder) {
				openFolder(std::string(), place.path);
				return;
			}
		}
		const std::string up = parentOf(folder);
		openFolder(up == "/" || up == "/mnt" || up == "/smb" ? std::string() : up, baseName(folder));
	}

	void activate(const LibraryEntry &entry)
	{
		switch (entry.kind) {
		case LibraryEntry::Place:
		case LibraryEntry::Folder:
			openFolder(entry.path);
			break;
		case LibraryEntry::Up:
			goUp();
			break;
		case LibraryEntry::Image: {
			std::string key;
			if (details && detailsPath == entry.path) {
				std::lock_guard<std::mutex> lock(details->mutex);
				if (details->done && details->info.ok) {
					key = details->info.key;
				}
			}
			startGame(entry.path, key);
			break;
		}
		case LibraryEntry::Quit:
			pshakux_log("library: Quit OGBox");
			quit = true;
			break;
		case LibraryEntry::Settings:
			openSettings(std::string(), std::string());
			break;
		case LibraryEntry::Dashboard:
			startPath = "none";
			startDelay = 2;
			pshakux_log("library: starting the Xbox dashboard");
			break;
		case LibraryEntry::Hdd:
			openHdd(0);
			break;
		case LibraryEntry::AddShare:
			openSettings(std::string(), std::string());
			openSettingsPage(SP_NETWORK);
			openShareForm(-1);
			break;
		}
	}

	/* 0.0.2: a game started from the list or its page goes on where it was left when there is a
	 * state of that (Quit, and save where I am promises it: "to go on from there next time"); the
	 * player chooses. Started plainly before, it began again from the start, the state only
	 * reachable through Load state. */
	/* how (0.0.3): 0 asks where to start when there is a state of where it was left, 1 goes on from
	 * there (the home's CROSS: its button says Continue), 2 starts from the beginning. */
	void startGame(const std::string &path, const std::string &key, int how = 0)
	{
		if (!startPath.empty()) {
			return;
		}
		/* 0.0.3: the data folder on a drive that isn't plugged in (a USB stick chosen at the first
		 * start): the game started with no Xbox hard disk, left at once to the dashboard, and the
		 * screen only said "The game left". Not started now: OGBox says what's missing. */
		const std::string root = ogbox::storageRoot();
		if (boot.hdd == ogbox::storagePath("hdd") && !isFolder(root)) {
			pshakux_log("library: the data folder %s isn't there: %s not started", root.c_str(), path.c_str());
			openPopup("Data folder not found",
				"OGBox keeps the Xbox hard disk (your saves), the caches and the covers in " + root +
				", which isn't there: the drive it's on may be unplugged. Plug it in and start the game again, "
				"or choose another data folder (it starts with an empty hard disk; the one on the drive stays as it is).",
				{ "Go back", "Choose another data folder" }, [this](int i) {
					if (i == 1) {
						openStorage(false);
					}
				});
			return;
		}
		std::string gameKey = key;
		if (gameKey.empty() && path != "none") {
			OgboxGameInfo info{};   // (the list's details not read yet: its disc's title ID now)
			if (ogbox_game_info(path.c_str(), &info) && info.ok) {
				gameKey = info.key;
			}
		}
		const ogbox::StateSlot left = gameKey.empty() ? ogbox::StateSlot() : ogbox::stateSlot(gameKey, ogbox::kQuitSlot);
		const auto go = [this, path](const std::string &state) {
			startState = state;
			startPath = path;
			startDelay = 2;   // a frame or two to say so before the relaunch
			gp.open = false;
			if (state.empty()) {
				pshakux_log("library: starting %s", path.c_str());
			} else {
				pshakux_log("library: starting %s where it was left (%s)", path.c_str(), state.c_str());
			}
		};
		if (!left.used || how == 2) {
			go(std::string());
			return;
		}
		if (how == 1) {
			go(left.path);
			return;
		}
		const std::string state = left.path;
		openPopup("Where you left off", "Saved " + ogbox::dateText(left.mtime) + ". Go on from there?",
			{ "Go on from there", "Start from the beginning", "Go back" }, [go, state](int i) {
				if (i == 0) {
					go(state);
				} else if (i == 1) {
					go(std::string());
				}
			});
	}

	/* Back to the game list from a game: the title starts again with no game named. */
	/* OGBox 0.1.25: the boot log on the disk now: a console that goes down while a game is left
	 * keeps the lines that say how far the exit got. */
	static void syncLog()
	{
		pshakux_diag_drain();   // 0.0.3: the lines the log's writer still holds
		const int fd = pshakux_log_fd();
		if (fd >= 0) {
			fsync(fd);
		}
	}

	/* OGBox 0.1.25: before the title is replaced, nothing of the game left running: the guest
	 * paused, the emulated GPU's work and the presenter's finished. Leaving a game crashed the
	 * console most times (0.1.24, Burnout 3: the log stopped before the exit's own lines). */
	void quiesceForExit()
	{
		syncLog();
		pshx_core_quiesce();
		syncLog();
		const VkResult r = vkDeviceWaitIdle(device);
		pshakux_log("ogbox: the presenter's GPU work done (%d)", static_cast<int>(r));
		syncLog();
	}

	/* 0.0.6: the share's disc image looked for again, once a second; the game started when it answers */
	void pollShareWait()
	{
		const double now = now_seconds();
		if (shareWait.result && shareWait.result->load() == 1) {
			shareWait.active = false;
			shareWait.result.reset();
			pshakux_log("ogbox: the share answered after %.1f s: starting the game", now - shareWait.since);
			struct stat st;
			if (ps5vfs_stat(boot.image.c_str(), &st) == 0) {
				pshakux_log("ogbox: disc image %s, %lld bytes", boot.image.c_str(), static_cast<long long>(st.st_size));
			}
			configureRun();
			if (configured) {
				pshakux_log("ogbox: starting the core");
				pshx_core_start();
			}
			return;
		}
		if ((!shareWait.result || shareWait.result->load() == 2) && now >= shareWait.nextTry) {
			shareWait.nextTry = now + 1.0;
			auto result = std::make_shared<std::atomic<int>>(0);
			shareWait.result = result;
			const std::string image = boot.image;
			std::thread([result, image]() {
				struct stat st;
				result->store(ps5vfs_stat(image.c_str(), &st) == 0 ? 1 : 2);
			}).detach();
		}
	}

	void relaunchToLibrary()
	{
		relaunchAsked = true;
		const char *args[] = { "--back", nullptr };   // 0.1.43: straight to the list, fading in
		pshakux_log("ogbox: back to the game list");
		quiesceForExit();
		const int r = pshakux_relaunch(args);
		pshakux_log("ogbox: the relaunch was refused (%#x): quitting", static_cast<unsigned>(r));
		quit = true;
	}

	/* The guest rebooted into another XBE: the title starts again with the same disc, hard disk and
	 * options, and hakuX's "relaunch" (it boots the XBE the launch data names). */
	void relaunchForGuest()
	{
		relaunchAsked = true;
		std::vector<std::string> words = { "--boot", boot.image, "--hdd", boot.hdd, "--relaunch" };
		for (const std::string &option : boot.options) {
			if (option != "relaunch" && option.rfind("loadstate=", 0) != 0) {
				words.push_back("--option");
				words.push_back(option);
			}
		}
		std::vector<const char *> args;
		for (const std::string &word : words) {
			args.push_back(word.c_str());
		}
		args.push_back(nullptr);
		saveFrozenFrame();
		quiesceForExit();
		pshakux_relaunch(args.data());
	}

	/* 0.0.2: a game starting another XBE keeps its picture: the last frame, kept for the title as it
	 * starts again (the frozen frame, shown until the new XBE's first instead of the boot screen). */
	static std::string frozenPath()
	{
		return ogbox::storagePath("cache") + "/xbe-switch-frame.bin";
	}

	void saveFrozenFrame()
	{
		if (!havePicture || showingLogo || pictureBuffer < 0 || !picture.width || !picture.height) {
			return;
		}
		const Staging &s = staging[static_cast<size_t>(pictureBuffer)];
		const size_t bytes = size_t(picture.width) * picture.height * 4;
		if (!s.mapped || s.size < bytes) {
			return;
		}
		mkdir(ogbox::storagePath("cache").c_str(), 0777);
		const std::string path = frozenPath();
		FILE *f = std::fopen(path.c_str(), "wb");
		if (!f) {
			return;
		}
		const float aspect = displayAspect();
		const uint32_t head[4] = { 0x4652474fu /* "OGRF" */, picture.width, picture.height, 0 };
		bool ok = std::fwrite(head, sizeof(head), 1, f) == 1 && std::fwrite(&aspect, sizeof(aspect), 1, f) == 1 &&
			std::fwrite(s.mapped, 1, bytes, f) == bytes;
		ok = std::fclose(f) == 0 && ok;
		pshakux_log("presenter: the last frame (%ux%u) kept for the game's next XBE: %s", picture.width, picture.height,
			ok ? "saved" : std::strerror(errno));
		if (!ok) {
			std::remove(path.c_str());
		}
	}

	/* The kept frame, read once and removed (true: it is the picture until the first frame). */
	bool loadFrozenFrame()
	{
		const std::string path = frozenPath();
		FILE *f = std::fopen(path.c_str(), "rb");
		if (!f) {
			return false;
		}
		uint32_t head[4] = {};
		float aspect = 0.0f;
		bool ok = std::fread(head, sizeof(head), 1, f) == 1 && head[0] == 0x4652474fu && head[1] && head[2] &&
			head[1] <= 8192 && head[2] <= 8192 && std::fread(&aspect, sizeof(aspect), 1, f) == 1;
		if (ok) {
			frozenPixels.resize(size_t(head[1]) * head[2] * 4);
			ok = std::fread(frozenPixels.data(), 1, frozenPixels.size(), f) == frozenPixels.size();
		}
		std::fclose(f);
		std::remove(path.c_str());
		if (!ok) {
			std::vector<uint8_t>().swap(frozenPixels);
			pshakux_log("presenter: the kept frame at %s could not be read: the boot screen instead", path.c_str());
			return false;
		}
		frozenWidth = head[1];
		frozenHeight = head[2];
		frozenAspect = aspect > 0.1f && aspect < 10.0f ? aspect : 0.0f;
		pshakux_log("presenter: the game's last frame (%ux%u) shown until this XBE's first", frozenWidth, frozenHeight);
		return true;
	}

	VulkanExample() : VulkanExampleBase()
	{
		title = "OGBox";
		apiVersion = VK_API_VERSION_1_3;
		useDynamicRendering = true;
		requiresStencil = true;
		deviceCreatepNextChain = &baseDynamicRenderingFeatures;
		settings.overlay = true;

		pshakux_priority_enable(1);
		pshakux_priority_set(PSHAKUX_ROLE_INTERFACE);
		/* 0.0.1: an update a power cut stopped is undone before anything of the title is read */
		if (OGBoxUpdate::Recover()) {
			pshakux_log("update: an interrupted install was undone");
		}
		mkdir(kDataRoot, 0777);
		chmod(kDataRoot, 0777);
		const std::string props = std::string(kDataRoot) + "/props.txt";
		const int loaded = pshakux_props_load(props.c_str());
		if (loaded >= 0) {
			pshakux_log("ogbox: %d properties from %s", loaded, props.c_str());
		}
		ps5.ownOverlay = true;   // OGBox lays out its own windows (the game list, the status)
		startFileLayer();
		/* 0.1.48: the hard disk in the data folder the player chose (boot.txt's hdd= and --hdd still win) */
		boot.hdd = ogbox::storagePath("hdd");
		/* boot.txt's options every run (0.1.14: a relaunch's arguments no longer leave them out);
		 * then the arguments, which win */
		readBootConfig(boot);
		const bool named = readArguments(boot);
		if (!named && !boot.back && boot.autoboot && !boot.bootTxtImage.empty()) {   // (0.1.43: back is the list)
			boot.image = boot.bootTxtImage;
			boot.source = boot.source + " (autoboot)";
		}
		if (boot.image.empty()) {
			library = true;
			prefs.load();
			/* 0.0.1: a newer OGBox on GitHub, asked at each start (not back from a game) unless
			 * Settings > Interface says not to; nothing is downloaded without asking */
			OGBoxUpdate::Start();
			if (!boot.back && prefs.global("update_check") != "0") {
				PS5Update::Check();
			}
			rainOn = prefs.on("rain");
			themeCode = prefs.global("theme") == "code";   // 0.1.52
			ogbox::Covers::get().enable(prefs.global("list_covers") != "0");
			ogbox::About::get().enable(prefs.global("list_covers") != "0");   // 0.0.3
			screen = Screen::Boot;
			screenSince = now_seconds();
			bootLines = {
				std::string("> OGBox ") + pshakux_build_label() + " for PS5",
				"> settings: /data/ogbox/settings.txt; data: " + ogbox::storageRoot(),
				"> mods: the title folder's mods/, chosen in /data/ogbox/mods.txt",
				"> network shares: " + g_networkStatus,
				"> the game list: " + std::string(readLastFolder().empty() ? "the places" : readLastFolder()),
			};
			std::string last = readLastFolder();
			if (last.empty() && !boot.bootTxtImage.empty()) {
				last = parentOf(boot.bootTxtImage);
			}
			/* 0.1.43: the disc last started, selected; back from a game the list itself fades in */
			const std::string lastGame = readLastGame();
			if (!lastGame.empty() && lastGame != "none" && isFolder(parentOf(lastGame))) {
				last = parentOf(lastGame);
			}
			const std::string select = !lastGame.empty() && parentOf(lastGame) == last ? baseName(lastGame) : std::string();
			if (boot.back) {
				screen = Screen::Library;
				fadeSince = -2.0;   // from the list's first frame
			}
			if (!last.empty() && isFolder(last)) {
				openFolder(last, select);
			} else {
				openFolder(std::string());
			}
			pshakux_log("ogbox: the game list (%s); the data folder %s", folder.empty() ? "the places" : folder.c_str(),
				ogbox::storageRoot().c_str());
			if (!ogbox::storageChosen() && !boot.back) {
				openStorage(true);   // 0.1.48: the first start asks where the data goes
			}
			/* 0.0.1: each start (not back from a game) first says what this build is: CROSS, then the
			 * splash (or the first start's question) */
			/* 0.0.6: only once a version (a fresh install, or after an update): the version it was read
			 * for is kept in /data/ogbox/notice-seen.txt */
			if (!boot.back && noticeSeenFor() != pshakux_build_label()) {
				afterNotice = screen;
				screen = Screen::Notice;
				screenSince = now_seconds();
			}
			return;
		}
		if (boot.image != "none") {
			struct stat st;
			const bool virtualImage = ps5vfs_is_virtual(boot.image.c_str()) != 0;
			if ((virtualImage ? ps5vfs_stat(boot.image.c_str(), &st) : stat(boot.image.c_str(), &st)) != 0) {
				const int error = errno;
				if (virtualImage) {
					/* 0.0.6: a share that doesn't answer yet (the network still coming up, the server
					 * waking): the game waits for it, the seconds on the screen, instead of failing */
					pshakux_log("ogbox: the disc image %s isn't reachable yet (%s): waiting for the share", boot.image.c_str(),
						std::strerror(error));
					shareWait.active = true;
					shareWait.since = now_seconds();
					return;
				}
				pshakux_log("ogbox: the disc image %s cannot be opened: %s (%s)", boot.image.c_str(), std::strerror(error),
					pshakux_sandbox_elevated() ? "out of the sandbox: the console's own paths are visible"
						: "inside the sandbox: only part of /data is visible; the boot log's \"sandbox:\" lines say why");
				problem = "The disc image " + boot.image + " cannot be opened: " + std::strerror(error) + ".";
				if (!pshakux_sandbox_elevated()) {
					problem += " The title is inside its sandbox (see sandbox: in ogbox-boot.log): put the image in the title folder, or run the payload loader (elfldr) so the title can leave it.";
				}
				return;
			}
			pshakux_log("ogbox: disc image %s, %lld bytes", boot.image.c_str(), static_cast<long long>(st.st_size));
		}
		configureRun();
	}

	/* The core's arguments for this run (the settings, the game's own, its trainer, boot.txt's), and the
	 * core configured with them. 0.0.6: from the constructor, or once a share being waited for answers. */
	void configureRun()
	{
		pshakux_log("ogbox: %s: disc %s, hard disk %s, %zu options", boot.source.c_str(), boot.image.c_str(),
			boot.hdd.c_str(), boot.options.size());
		std::vector<const char *> argv;
		/* 0.1.26: the settings (src/ogbox_settings.cpp: hakuX's, its defaults as its release app's)
		 * with the game's own on top, as hakuX's NativeCoreActivity.kt passes them; boot.txt's
		 * options and the title's arguments after them, and they win. The game is its disc's
		 * title ID and version: the game list's (--game), else read here. */
		std::string key = boot.gameKey;
		if (boot.image != "none") {
			/* (0.1.54: its default.xbe's date too, the trainers' release check) */
			OgboxGameInfo info{};
			if (ogbox_game_info(boot.image.c_str(), &info)) {
				if (key.empty()) {
					key = info.key;
				}
				runXbeTime = info.xbe_time;
			}
		}
		prefs.load(key);
		runKey = key;   // 0.1.53: the quick menu's states and settings
		/* 0.0.1: the GPU's threads on cores of their own (on by default), before the core starts them */
		pshakux_priority_gpu_pin(prefs.on("gpu_cores") ? 1 : 0);
		themeCode = prefs.global("theme") == "code";   // 0.1.52: the in-game panels too
		aspectMode = prefs.value("aspect");
		cropOn = prefs.on("crop_overscan");   // 0.1.59
		/* 0.1.58: "fxaa" is the anti-aliasing: 0 off, 1 FXAA (as before), smaa, ssaa */
		fxaaOn = prefs.on("fxaa");
		smaaOn = prefs.value("fxaa") == "smaa";
		ssaaOn = prefs.value("fxaa") == "ssaa";
		sgsrOn = prefs.value("upscale") == "fsr" || prefs.value("upscale") == "sgsr";   // 0.0.6: FSR1 (EASU)
		sharpOn = prefs.value("upscale") == "sharp";
		fgWanted = ogbox::fgMethodFromSetting(prefs.value("framegen"));
		fgQuality = static_cast<uint32_t>(std::max(0, std::min(2, prefs.number("framegen_quality"))));
		showFps = prefs.value("show_fps") != "0";
		fpsCorner = prefs.value("show_fps");
		loadPadMap();
		static std::vector<std::string> fromSettings;
		fromSettings = prefs.coreArgs();
		if (ssaaOn) {
			/* SSAA: the scale that draws a 480-line picture at 1.5 times the screen's height or more
			 * (4x on a 1080p screen, 6x on a 4K one; hakuX keeps a wider picture within 3840 at
			 * scales above 4), or the setting's if higher */
			const int want = std::min(6, std::max(1, static_cast<int>(std::ceil(1.5 * height / 480.0))));
			for (std::string &option : fromSettings) {
				if (option.rfind("scale=", 0) == 0 && std::atoi(option.c_str() + 6) < want) {
					option = "scale=" + std::to_string(want);
				}
			}
		}
		std::string said;
		for (const std::string &option : fromSettings) {
			argv.push_back(option.c_str());
			said += " " + option;
		}
		pshakux_log("ogbox: settings%s%s:%s; the picture: aspect %s%s, anti-aliasing %s, upscaling %s, FPS counter %s",
			key.empty() ? "" : " for ", key.c_str(), said.c_str(), aspectMode.c_str(), cropOn ? " (overscan cropped)" : "",
			fxaaOn ? "FXAA" : smaaOn ? "SMAA" : ssaaOn ? "SSAA" : "off",
			sgsrOn ? "FSR1" : sharpOn ? "sharp bilinear" : "bilinear", showFps ? "on" : "off");
		/* 0.1.54: the game's trainer (core/ogbox_trainers.h), run by hakuX before the game */
		static std::vector<std::string> fromTrainer;
		fromTrainer.clear();
		if (!key.empty()) {
			char rel[384] = "", bits[128] = "", path[1024] = "";
			if (ogbox_trainers_chosen(key.c_str(), rel, sizeof(rel), bits, sizeof(bits))) {
				setTrainerRoots();
				uint32_t made = 0;
				if (ogbox_trainers_resolve(rel, path, sizeof(path)) && runXbeTime &&
					ogbox_trainers_xbe_time(rel, &made) && made && made != runXbeTime) {
					/* 0.0.2: the release check: made for another release, it doesn't run */
					pshakux_log("ogbox: the trainer chosen for %s (%s) was made for another release (its XBE dated %08X, "
						"this disc's %08X): the game starts without it", key.c_str(), rel, static_cast<unsigned>(made),
						static_cast<unsigned>(runXbeTime));
				} else if (path[0]) {
					fromTrainer.push_back(std::string("trainer=") + path);
					fromTrainer.push_back(std::string("trainer_options=") + bits);
					pshakux_log("ogbox: trainer %s, options %s", path, bits[0] ? bits : "none on");
				} else {
					pshakux_log("ogbox: the trainer chosen for %s (%s) isn't in the trainers folders: the game starts without it",
						key.c_str(), rel);
				}
			}
		}
		for (const std::string &option : fromTrainer) {
			argv.push_back(option.c_str());
		}
		if (prefs.overrides()) {
			pshakux_log("ogbox: %s has %d settings of its own (%s)", key.c_str(), prefs.overrides(),
				ogbox::gameSettingsPath(key).c_str());
		}
		for (const std::string &option : boot.options) {
			argv.push_back(option.c_str());
		}
		const std::string cache = ogbox::storagePath("cache");
		PshxCoreConfig config{};
		config.image = boot.image.c_str();
		config.hdd_root = boot.hdd.c_str();
		config.data_root = cache.c_str();
		config.argc = static_cast<int>(argv.size());
		config.argv = argv.data();
		configured = pshx_core_configure(&config);
		pshakux_log("ogbox: core %s: %s%s%s", pshx_core_name(), configured ? "configured" : "not configured",
			configured ? "" : ": ", configured ? "" : pshx_core_message());
	}

	~VulkanExample() override
	{
		if (device == VK_NULL_HANDLE) {
			return;
		}
		checkInstanceTable("quit");
		vkDeviceWaitIdle(device);
		frameGen.destroy();
		destroyPicture();
		for (auto &s : staging) {
			destroyStaging(s);
		}
		vkDestroySampler(device, sampler, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipeline(device, pipelineFxaa, nullptr);
		vkDestroyPipeline(device, pipelineSgsr, nullptr);
		vkDestroyPipeline(device, pipelineSharp, nullptr);
		destroySmaa();
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		// The base class owns descriptorPool and destroys it again if it is still set: 0.1.1
		// crashed at quit there (RADV freeing the pool twice).
		descriptorPool = VK_NULL_HANDLE;
	}

	/* The instance-level commands the template uses (the base class, the swapchain on
	 * VK_KHR_display, the debug messenger) are volk's globals, one table for the whole title.
	 * hakuX's HLE GPU makes an instance of its own; up to 0.1.4 it loaded volk's instance table
	 * from it, made without VK_KHR_surface or VK_KHR_display, and the quit's swapchain cleanup
	 * called vkDestroySurfaceKHR through 0. hakuX keeps the title's table now (vkdev.c); this
	 * says so in the log (when the core is up, and at quit) and puts the title's table back if
	 * anything changed it. */
	void checkInstanceTable(const char *when)
	{
		if (instance == VK_NULL_HANDLE) {
			return;
		}
		struct Entry {
			const char *name;
			const void *fn;
		};
		const Entry entries[] = {
			{ "vkDestroyInstance", (const void *)vkDestroyInstance },
			{ "vkEnumeratePhysicalDevices", (const void *)vkEnumeratePhysicalDevices },
			{ "vkGetPhysicalDeviceProperties", (const void *)vkGetPhysicalDeviceProperties },
			{ "vkGetPhysicalDeviceFeatures", (const void *)vkGetPhysicalDeviceFeatures },
			{ "vkGetPhysicalDeviceMemoryProperties", (const void *)vkGetPhysicalDeviceMemoryProperties },
			{ "vkGetPhysicalDeviceQueueFamilyProperties", (const void *)vkGetPhysicalDeviceQueueFamilyProperties },
			{ "vkGetPhysicalDeviceFormatProperties", (const void *)vkGetPhysicalDeviceFormatProperties },
			{ "vkCreateDevice", (const void *)vkCreateDevice },
			{ "vkDestroySurfaceKHR", (const void *)vkDestroySurfaceKHR },
			{ "vkGetPhysicalDeviceSurfaceSupportKHR", (const void *)vkGetPhysicalDeviceSurfaceSupportKHR },
			{ "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", (const void *)vkGetPhysicalDeviceSurfaceCapabilitiesKHR },
			{ "vkGetPhysicalDeviceSurfaceFormatsKHR", (const void *)vkGetPhysicalDeviceSurfaceFormatsKHR },
			{ "vkGetPhysicalDeviceSurfacePresentModesKHR", (const void *)vkGetPhysicalDeviceSurfacePresentModesKHR },
			{ "vkGetPhysicalDeviceDisplayPropertiesKHR", (const void *)vkGetPhysicalDeviceDisplayPropertiesKHR },
			{ "vkGetPhysicalDeviceDisplayPlanePropertiesKHR", (const void *)vkGetPhysicalDeviceDisplayPlanePropertiesKHR },
			{ "vkGetDisplayPlaneSupportedDisplaysKHR", (const void *)vkGetDisplayPlaneSupportedDisplaysKHR },
			{ "vkGetDisplayModePropertiesKHR", (const void *)vkGetDisplayModePropertiesKHR },
			{ "vkGetDisplayPlaneCapabilitiesKHR", (const void *)vkGetDisplayPlaneCapabilitiesKHR },
			{ "vkCreateDisplayPlaneSurfaceKHR", (const void *)vkCreateDisplayPlaneSurfaceKHR },
		};
		unsigned missing = 0;
		const char *first = nullptr;
		for (const Entry &e : entries) {
			if (!e.fn) {
				missing++;
				first = first ? first : e.name;
			}
		}
		const bool ours = volkGetLoadedInstance() == instance;
		if (ours && !missing) {
			pshakux_log("vulkan: instance table at %s: the title's, %zu commands checked", when,
				sizeof(entries) / sizeof(entries[0]));
			return;
		}
		pshakux_log("vulkan: instance table at %s: %s, %u of %zu commands null (first %s): loaded again "
			"from the title's instance", when, ours ? "the title's instance" : "ANOTHER instance's",
			missing, sizeof(entries) / sizeof(entries[0]), first ? first : "none");
		volkLoadInstanceOnly(instance);
	}

	// ---- Vulkan resources (as Kelvin's presenter) -----------------------------------------------
	void destroyStaging(Staging &s)
	{
		if (s.mapped) {
			pshx_core_frame_forget(s.mapped);   // 0.0.4: the HLE GPU's device lets go of it first
			vkUnmapMemory(device, s.memory);
		}
		vkDestroyBuffer(device, s.buffer, nullptr);
		vkFreeMemory(device, s.memory, nullptr);
		s = Staging{};
	}

	void destroyPicture()
	{
		vkDestroyImageView(device, picture.view, nullptr);
		vkDestroyImage(device, picture.image, nullptr);
		vkFreeMemory(device, picture.memory, nullptr);
		picture.view = VK_NULL_HANDLE;
		picture.image = VK_NULL_HANDLE;
		picture.memory = VK_NULL_HANDLE;
		picture.width = picture.height = 0;
		picture.layout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	void ensureStaging(Staging &s, VkDeviceSize size)
	{
		size = (size + 0xFFFF) & ~VkDeviceSize(0xFFFF);   // 0.0.4: whole 64 KiB pages (the HLE GPU writes them)
		if (s.size >= size) {
			return;
		}
		destroyStaging(s);
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, size, &s.buffer, &s.memory));
		VK_CHECK_RESULT(vkMapMemory(device, s.memory, 0, VK_WHOLE_SIZE, 0, &s.mapped));
		s.size = size;
	}

	void ensurePicture(uint32_t w, uint32_t h)
	{
		if (picture.image != VK_NULL_HANDLE && picture.width == w && picture.height == h) {
			return;
		}
		vkDeviceWaitIdle(device);
		destroyPicture();
		VkImageCreateInfo imageCI = vks::initializers::imageCreateInfo();
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = picture.format;
		imageCI.extent = { w, h, 1 };
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &picture.image));
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(device, picture.image, &req);
		VkMemoryAllocateInfo alloc = vks::initializers::memoryAllocateInfo();
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = vulkanDevice->getMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &alloc, nullptr, &picture.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, picture.image, picture.memory, 0));
		VkImageViewCreateInfo viewCI = vks::initializers::imageViewCreateInfo();
		viewCI.image = picture.image;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = picture.format;
		viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &picture.view));
		picture.width = w;
		picture.height = h;
		picture.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkDescriptorImageInfo info{ sampler, picture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(descriptorSet,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &info);
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		pshakux_log("presenter: picture %ux%u", w, h);
	}

	void preparePresenter()
	{
		// The frame's values are display values: shown as they are. With an sRGB swapchain the
		// view decodes them, so the swapchain's encoding gives them back.
		const VkFormat sc = swapChain.colorFormat;
		const bool srgb = sc == VK_FORMAT_B8G8R8A8_SRGB || sc == VK_FORMAT_R8G8B8A8_SRGB || sc == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
		picture.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

		VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
		samplerCI.magFilter = samplerCI.minFilter = VK_FILTER_LINEAR;
		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerCI.addressModeU = samplerCI.addressModeV = samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.maxLod = 0.0f;
		samplerCI.maxAnisotropy = 1.0f;
		samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &sampler));

		VkDescriptorPoolSize poolSize = vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
		VkDescriptorPoolCreateInfo poolCI = vks::initializers::descriptorPoolCreateInfo(1, &poolSize, 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolCI, nullptr, &descriptorPool));
		VkDescriptorSetLayoutBinding binding = vks::initializers::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		VkDescriptorSetLayoutCreateInfo layoutCI = vks::initializers::descriptorSetLayoutCreateInfo(&binding, 1);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &descriptorSetLayout));
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		/* 0.1.58: a vec4 for ssaa.frag (the viewport's size) */
		const VkPushConstantRange presentPush{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &presentPush;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		VkPipelineInputAssemblyStateCreateInfo inputAssembly =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterization =
			vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachment = vks::initializers::pipelineColorBlendAttachmentState(0xF, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlend = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachment);
		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisample = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStates);
		VkPipelineVertexInputStateCreateInfo vertexInput = vks::initializers::pipelineVertexInputStateCreateInfo();
		std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
			loadShader(getShadersPath() + "ogbox/present.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "ogbox/present.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};
		VkPipelineRenderingCreateInfoKHR renderingCI{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
		renderingCI.colorAttachmentCount = 1;
		renderingCI.pColorAttachmentFormats = &swapChain.colorFormat;
		renderingCI.depthAttachmentFormat = depthFormat;
		renderingCI.stencilAttachmentFormat = depthFormat;
		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.pNext = &renderingCI;
		pipelineCI.layout = pipelineLayout;
		pipelineCI.pVertexInputState = &vertexInput;
		pipelineCI.pInputAssemblyState = &inputAssembly;
		pipelineCI.pRasterizationState = &rasterization;
		pipelineCI.pColorBlendState = &colorBlend;
		pipelineCI.pMultisampleState = &multisample;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(stages.size());
		pipelineCI.pStages = stages.data();
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
		/* 0.1.26: the settings' FXAA (hakuX's presenter's shader, shaders/glsl/ogbox/fxaa.frag); 0.0.6: FSR1
		 * (AMD's EASU, fsr1.frag) where Snapdragon GSR was, and Bilinear (Sharp) (sharp.frag): the same
		 * pipeline, another fragment stage */
		const struct {
			const char *file;
			VkPipeline *out;
		} variants[] = { { "ogbox/fxaa.frag.spv", &pipelineFxaa }, { "ogbox/fsr1.frag.spv", &pipelineSgsr }, { "ogbox/sharp.frag.spv", &pipelineSharp },
			{ "ogbox/ssaa.frag.spv", &pipelineSsaa } };
		for (const auto &v : variants) {
			stages[1] = loadShader(getShadersPath() + v.file, VK_SHADER_STAGE_FRAGMENT_BIT);
			pipelineCI.pStages = stages.data();
			if (vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, v.out) != VK_SUCCESS) {
				*v.out = VK_NULL_HANDLE;
				pshakux_log("presenter: %s cannot be made: that filter is off", v.file);
			}
		}
		prepareSmaa(pipelineCI, rasterization);
	}

	// ---- 0.1.58: SMAA 1x (iryoku/smaa, MIT: shaders/glsl/ogbox/SMAA.hlsl, third_party/smaa) -------
	void destroyAaImage(AaImage &a)
	{
		vkDestroyImageView(device, a.view, nullptr);
		vkDestroyImage(device, a.image, nullptr);
		vkFreeMemory(device, a.memory, nullptr);
		a = AaImage{};
	}

	void createAaImage(AaImage &a, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage)
	{
		VkImageCreateInfo imageCI = vks::initializers::imageCreateInfo();
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = format;
		imageCI.extent = { w, h, 1 };
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = usage;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &a.image));
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(device, a.image, &req);
		VkMemoryAllocateInfo alloc = vks::initializers::memoryAllocateInfo();
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = vulkanDevice->getMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &alloc, nullptr, &a.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, a.image, a.memory, 0));
		VkImageViewCreateInfo viewCI = vks::initializers::imageViewCreateInfo();
		viewCI.image = a.image;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = format;
		viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VK_CHECK_RESULT(vkCreateImageView(device, &viewCI, nullptr, &a.view));
	}

	/* A constant texture (SMAA's area and search tables) up once, left shader-readable. */
	void uploadAaImage(AaImage &a, VkFormat format, uint32_t w, uint32_t h, const void *data, VkDeviceSize bytes)
	{
		createAaImage(a, format, w, h, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		Staging s{};
		ensureStaging(s, bytes);
		std::memcpy(s.mapped, data, static_cast<size_t>(bytes));
		VkCommandBuffer cmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vks::tools::insertImageMemoryBarrier(cmd, a.image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, range);
		VkBufferImageCopy copy{};
		copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy.imageExtent = { w, h, 1 };
		vkCmdCopyBufferToImage(cmd, s.buffer, a.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		vks::tools::insertImageMemoryBarrier(cmd, a.image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, range);
		vulkanDevice->flushCommandBuffer(cmd, queue, true);
		destroyStaging(s);
	}

	/* The three passes' pipelines, their sets and SMAA's two tables; none of it if a part can't be
	 * made (SMAA is then off: the picture as it is). */
	void prepareSmaa(VkGraphicsPipelineCreateInfo pipelineCI, const VkPipelineRasterizationStateCreateInfo &)
	{
		VkSamplerCreateInfo samplerCI = vks::initializers::samplerCreateInfo();
		samplerCI.magFilter = samplerCI.minFilter = VK_FILTER_NEAREST;
		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerCI.addressModeU = samplerCI.addressModeV = samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCI.maxLod = 0.0f;
		samplerCI.maxAnisotropy = 1.0f;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCI, nullptr, &pointSampler));

		const VkDescriptorSetLayoutBinding bindings[3] = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
		};
		VkDescriptorSetLayoutCreateInfo layoutCI = vks::initializers::descriptorSetLayoutCreateInfo(bindings, 3);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &smaaSetLayout));
		const VkPushConstantRange push{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
		VkPipelineLayoutCreateInfo plCI = vks::initializers::pipelineLayoutCreateInfo(&smaaSetLayout, 1);
		plCI.pushConstantRangeCount = 1;
		plCI.pPushConstantRanges = &push;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &plCI, nullptr, &smaaPipelineLayout));

		VkDescriptorPoolSize poolSize = vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10);
		VkDescriptorPoolCreateInfo poolCI = vks::initializers::descriptorPoolCreateInfo(1, &poolSize, 4);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolCI, nullptr, &smaaPool));
		const VkDescriptorSetLayout three[3] = { smaaSetLayout, smaaSetLayout, smaaSetLayout };
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(smaaPool, three, 3);
		VkDescriptorSet sets[3];
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, sets));
		smaaEdgeSet = sets[0];
		smaaWeightSet = sets[1];
		smaaBlendSet = sets[2];
		allocInfo = vks::initializers::descriptorSetAllocateInfo(smaaPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &smaaPresentSet));

		const struct {
			const char *vert, *frag;
			VkFormat format;
			VkPipeline *out;
		} passes[] = {
			{ "ogbox/smaa_edge.vert.spv", "ogbox/smaa_edge.frag.spv", VK_FORMAT_R8G8_UNORM, &smaaEdge },
			{ "ogbox/smaa_weight.vert.spv", "ogbox/smaa_weight.frag.spv", VK_FORMAT_R8G8B8A8_UNORM, &smaaWeight },
			{ "ogbox/smaa_blend.vert.spv", "ogbox/smaa_blend.frag.spv", picture.format, &smaaBlend },
		};
		bool ok = true;
		for (const auto &pass : passes) {
			std::array<VkPipelineShaderStageCreateInfo, 2> stages = {
				loadShader(getShadersPath() + pass.vert, VK_SHADER_STAGE_VERTEX_BIT),
				loadShader(getShadersPath() + pass.frag, VK_SHADER_STAGE_FRAGMENT_BIT),
			};
			VkPipelineRenderingCreateInfoKHR renderingCI{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
			renderingCI.colorAttachmentCount = 1;
			renderingCI.pColorAttachmentFormats = &pass.format;
			pipelineCI.pNext = &renderingCI;
			pipelineCI.layout = smaaPipelineLayout;
			pipelineCI.stageCount = 2;
			pipelineCI.pStages = stages.data();
			pipelineCI.pDepthStencilState = nullptr;
			if (vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, pass.out) != VK_SUCCESS) {
				*pass.out = VK_NULL_HANDLE;
				ok = false;
			}
		}
		uploadAaImage(smaaArea, VK_FORMAT_R8G8_UNORM, AREATEX_WIDTH, AREATEX_HEIGHT, areaTexBytes, sizeof(areaTexBytes));
		uploadAaImage(smaaSearch, VK_FORMAT_R8_UNORM, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, searchTexBytes,
			sizeof(searchTexBytes));
		pshakux_log("presenter: SMAA %s (preset high: luma edges, area %ux%u, search %ux%u)",
			ok ? "ready" : "cannot be made: it is off", AREATEX_WIDTH, AREATEX_HEIGHT, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
	}

	bool smaaReady() const
	{
		return smaaEdge != VK_NULL_HANDLE && smaaWeight != VK_NULL_HANDLE && smaaBlend != VK_NULL_HANDLE;
	}

	/* The passes' images at the picture's size, and the sets pointed at the picture of now. */
	void ensureSmaa()
	{
		if (picture.view == VK_NULL_HANDLE) {
			return;
		}
		if (smaaW != picture.width || smaaH != picture.height) {
			vkDeviceWaitIdle(device);
			destroyAaImage(smaaEdges);
			destroyAaImage(smaaWeights);
			destroyAaImage(smaaOut);
			const VkImageUsageFlags rt = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			createAaImage(smaaEdges, VK_FORMAT_R8G8_UNORM, picture.width, picture.height, rt);
			createAaImage(smaaWeights, VK_FORMAT_R8G8B8A8_UNORM, picture.width, picture.height, rt);
			createAaImage(smaaOut, picture.format, picture.width, picture.height, rt);
			smaaW = picture.width;
			smaaH = picture.height;
			smaaPictureView = VK_NULL_HANDLE;
			smaaValid = false;
			VkDescriptorImageInfo edges{ sampler, smaaEdges.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo area{ sampler, smaaArea.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo search{ pointSampler, smaaSearch.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo weights{ sampler, smaaWeights.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo out{ sampler, smaaOut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			const VkWriteDescriptorSet writes[] = {
				vks::initializers::writeDescriptorSet(smaaWeightSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &edges),
				vks::initializers::writeDescriptorSet(smaaWeightSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &area),
				vks::initializers::writeDescriptorSet(smaaWeightSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &search),
				vks::initializers::writeDescriptorSet(smaaBlendSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &weights),
				vks::initializers::writeDescriptorSet(smaaPresentSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &out),
			};
			vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
			pshakux_log("presenter: SMAA's images %ux%u", smaaW, smaaH);
		}
		if (smaaPictureView != picture.view) {
			VkDescriptorImageInfo color{ sampler, picture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			const VkWriteDescriptorSet writes[] = {
				vks::initializers::writeDescriptorSet(smaaEdgeSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &color),
				vks::initializers::writeDescriptorSet(smaaBlendSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &color),
			};
			vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
			smaaPictureView = picture.view;
			smaaValid = false;
		}
	}

	/* One pass: its target cleared and drawn at the picture's size, then left shader-readable. */
	void smaaPass(VkCommandBuffer cmd, AaImage &target, VkPipeline pipe, VkDescriptorSet set)
	{
		const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vks::tools::insertImageMemoryBarrier(cmd, target.image, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, range);
		VkRenderingAttachmentInfoKHR color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
		color.imageView = target.view;
		color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		VkRenderingInfoKHR info{ VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
		info.renderArea = { { 0, 0 }, { smaaW, smaaH } };
		info.layerCount = 1;
		info.colorAttachmentCount = 1;
		info.pColorAttachments = &color;
		vkCmdBeginRendering(cmd, &info);
		VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(smaaW), static_cast<float>(smaaH), 0.0f, 1.0f };
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor{ { 0, 0 }, { smaaW, smaaH } };
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, smaaPipelineLayout, 0, 1, &set, 0, nullptr);
		const float metrics[4] = { 1.0f / static_cast<float>(smaaW), 1.0f / static_cast<float>(smaaH),
			static_cast<float>(smaaW), static_cast<float>(smaaH) };
		vkCmdPushConstants(cmd, smaaPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
			sizeof(metrics), metrics);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRendering(cmd);
		vks::tools::insertImageMemoryBarrier(cmd, target.image, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, range);
	}

	/* The three passes over a new picture (an unchanged one keeps its result). */
	void recordSmaa(VkCommandBuffer cmd)
	{
		ensureSmaa();
		if (smaaValid && !pictureChanged) {
			return;
		}
		smaaPass(cmd, smaaEdges, smaaEdge, smaaEdgeSet);
		smaaPass(cmd, smaaWeights, smaaWeight, smaaWeightSet);
		smaaPass(cmd, smaaOut, smaaBlend, smaaBlendSet);
		smaaValid = true;
	}

	void destroySmaa()
	{
		destroyAaImage(smaaEdges);
		destroyAaImage(smaaWeights);
		destroyAaImage(smaaOut);
		destroyAaImage(smaaArea);
		destroyAaImage(smaaSearch);
		vkDestroyPipeline(device, smaaEdge, nullptr);
		vkDestroyPipeline(device, smaaWeight, nullptr);
		vkDestroyPipeline(device, smaaBlend, nullptr);
		vkDestroyPipeline(device, pipelineSsaa, nullptr);
		vkDestroyPipelineLayout(device, smaaPipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, smaaSetLayout, nullptr);
		vkDestroyDescriptorPool(device, smaaPool, nullptr);
		vkDestroySampler(device, pointSampler, nullptr);
	}

	void prepare() override
	{
		VulkanExampleBase::prepare();
		preparePresenter();
		loadLogo();
		prepared = true;
		if (configured) {
			pshakux_log("ogbox: starting the core");
			pshx_core_start();
		}
	}

	// ---- each frame ---------------------------------------------------------------------------
	/* OGBox's logo, the picture until the game's first frame (none when the file is missing). */
	void loadLogo()
	{
		/* 0.0.2: the game started another XBE: its last frame instead of the boot screen; a kept
		 * frame left from before (no relaunch now) is removed unread */
		const bool xbeSwitch = !library && std::find(boot.options.begin(), boot.options.end(), "relaunch") != boot.options.end();
		if (xbeSwitch ? loadFrozenFrame() : (std::remove(frozenPath().c_str()), false)) {
			return;
		}
		const std::string path = getAssetPath() + "ogbox-logo.png";
		int w = 0, h = 0, n = 0;
		stbi_uc *pixels = stbi_load(path.c_str(), &w, &h, &n, STBI_rgb_alpha);
		if (!pixels || w <= 0 || h <= 0) {
			pshakux_log("presenter: no logo (%s: %s)", path.c_str(), pixels ? "empty" : stbi_failure_reason());
			if (pixels) {
				stbi_image_free(pixels);
			}
			return;
		}
		logoWidth = static_cast<uint32_t>(w);
		logoHeight = static_cast<uint32_t>(h);
		logoPixels.assign(pixels, pixels + size_t(logoWidth) * logoHeight * 4);
		stbi_image_free(pixels);
		pshakux_log("presenter: logo %ux%u from %s, shown until the game's first frame", logoWidth, logoHeight, path.c_str());
	}

	/* The logo into this frame's staging buffer, once, while no game frame has come. */
	bool fetchLogo()
	{
		if (!havePicture && !showingLogo && !frozenPixels.empty()) {
			Staging &s = staging[currentBuffer];
			ensureStaging(s, VkDeviceSize(frozenPixels.size()));
			std::memcpy(s.mapped, frozenPixels.data(), frozenPixels.size());
			ensurePicture(frozenWidth, frozenHeight);
			showingLogo = showingFrozen = true;
			std::vector<uint8_t>().swap(frozenPixels);
			return true;
		}
		if (havePicture || showingLogo || logoPixels.empty()) {
			return false;
		}
		Staging &s = staging[currentBuffer];
		ensureStaging(s, VkDeviceSize(logoPixels.size()));
		std::memcpy(s.mapped, logoPixels.data(), logoPixels.size());
		ensurePicture(logoWidth, logoHeight);
		showingLogo = true;
		std::vector<uint8_t>().swap(logoPixels);   // uploaded this frame: not needed again
		return true;
	}

	/* The newest frame into this frame's staging buffer, when there is one. */
	void fetchPicture()
	{
		upload = false;
		fgFresh = fgPush = fgShow = false;
		if (fetchLogo()) {
			upload = true;
			return;
		}
		uint32_t w = 0, h = 0;
		pshx_core_frame_size(&w, &h);
		if (!w || !h) {
			return;
		}
		Staging &s = staging[currentBuffer];
		ensureStaging(s, VkDeviceSize(w) * h * 4);
		uint32_t fw = 0, fh = 0;
		pshx_core_display_pickup();   // 0.0.4: the guest's vblank keeps clear of it
		// 0.0.4: the HLE GPU copies the frame straight into the staging buffer where it can
		if (!pshx_core_take_frame_into(&frameSeen, s.mapped, static_cast<size_t>(s.size), w * 4, w, h, &fw, &fh)) {
			return;
		}
		ensurePicture(fw, fh);
		fgFresh = true;
		fgPush = frameGenFor(fw, fh);
		upload = !fgPush;
		havePicture = true;
		pictureBuffer = static_cast<int>(currentBuffer);   // 0.1.53: a save state's picture comes from it
		showingLogo = false;
		showingFrozen = false;
		if (!picturesTaken++) {
			pshakux_log("presenter: the first frame, %ux%u, after %llu display frames", fw, fh,
				static_cast<unsigned long long>(framesDrawn));
			checkInstanceTable("the first frame (the HLE GPU's instance made)");
		}
	}

	/* 0.0.7: frame generation for frames of w x h: made (again) when the size or the method asks; false
	 * when it is off or cannot be, and the frames then go straight to the picture. */
	bool frameGenFor(uint32_t w, uint32_t h)
	{
		if (fgWanted == ogbox::FgMethod::Off || fgFailed) {
			return false;
		}
		if (frameGen.ready() && frameGen.width() == w && frameGen.height() == h) {
			return true;
		}
		if (frameGen.width() && !frameGen.ready()) {
			fgFailed = true;   // it failed while running (frameGen said why)
			return false;
		}
		/* the own method and optical flow search on a reduced picture (only the last pass is full size); the
		 * neural network and the blend work at full size */
		auto mostFor = [](ogbox::FgMethod m) {
			return (m == ogbox::FgMethod::Own || m == ogbox::FgMethod::Flow) ? 12000000ull : 5000000ull;
		};
		if ((fgWanted == ogbox::FgMethod::Neural || fgWanted == ogbox::FgMethod::Blend) && uint64_t(w) * h > mostFor(fgWanted) &&
			uint64_t(w) * h <= mostFor(ogbox::FgMethod::Own)) {
			pshakux_log("framegen: frames of %ux%u are too large for MihawkGen (the most is 5 million pixels, 4x): Crux2's own "
				"method makes them instead", w, h);
			fgWanted = ogbox::FgMethod::Own;
		}
		const uint64_t most = mostFor(fgWanted);
		if (uint64_t(w) * h > most) {
			pshakux_log("framegen: frames of %ux%u are too large for this method (the most is %.1f million pixels): off", w, h,
				static_cast<double>(most) / 1e6);
			fgFailed = true;
			return false;
		}
		ogbox::FrameGen::Init in;
		in.physical = vulkanDevice->physicalDevice;
		in.device = device;
		in.cache = pipelineCache;
		in.linear = sampler;
		in.shaderDir = getShadersPath();
		in.slots = static_cast<uint32_t>(drawCmdBuffers.size());
		in.quality = fgQuality;
		if (!frameGen.create(in, fgWanted, w, h)) {
			pshakux_log("framegen: could not be made (%s): off", frameGen.error().c_str());
			fgFailed = true;
			return false;
		}
		fgPacer.Reset();
		fgMade = false;
		return true;
	}

	/* 0.0.7: the quick menu changes the method or the quality of a running game's frame generation at once */
	void refreshFrameGen(const std::string &key)
	{
		if (key != "framegen" && key != "framegen_quality") {
			return;
		}
		fgWanted = ogbox::fgMethodFromSetting(prefs.value("framegen"));
		fgQuality = static_cast<uint32_t>(std::max(0, std::min(2, prefs.number("framegen_quality"))));
		fgFailed = false;
		fgMade = false;
		frameGen.destroy();
		pshakux_log("framegen: %s", fgWanted == ogbox::FgMethod::Off ? "off" : "changed in the quick menu");
	}

	/* 0.0.7: what this refresh shows: the pacer counts the game's pictures per refresh (a game at 30 fps on
	 * a 60 Hz screen: a new one every second refresh); when one arrives the picture between it and the one
	 * before is shown, the refresh after it the new one itself (Crux2's ps5_framegen_pacer.h). */
	void frameGenPlan()
	{
		fgShow = false;
		if (library || !frameGen.ready() || !havePicture || showingLogo || !frameGen.width()) {
			return;
		}
		fgPacer.Vsync(fgFresh);
		float t = 1.0f;
		if (fgPacer.Makes(1)) {
			t = fgPacer.Time(0, 1);
		}
		const bool made = t < 1.0f && frameGen.havePair();
		if (fgFresh || made || fgMade) {
			fgShow = true;
			fgTime = made ? t : 1.0f;
		}
		fgMade = made;
		if (fgShow) {
			fgShownCount++;
			fgMadeCount += made ? 1 : 0;
		}
		/* each half second: the rate shown; each 10 s: a line in the boot log */
		const double now = now_seconds();
		if (fgRateAt == 0.0) {
			fgRateAt = fgLogAt = now;
		} else if (now - fgRateAt >= 0.5) {
			fgShownFps = static_cast<float>(static_cast<double>(fgShownCount - fgShownMark) / (now - fgRateAt));
			fgShownMark = fgShownCount;
			fgRateAt = now;
		}
		if (now - fgLogAt >= 10.0) {
			pshakux_log("framegen: in %.0f s the screen was given %llu pictures, %llu of them made; the game gave %llu "
				"(the pacer takes each to stay %u refreshes)", now - fgLogAt,
				static_cast<unsigned long long>(fgShownCount - fgShownMark2),
				static_cast<unsigned long long>(fgMadeCount - fgMadeMark),
				static_cast<unsigned long long>(picturesTaken - fgPicMark), fgPacer.period);
			fgShownMark2 = fgShownCount;
			fgMadeMark = fgMadeCount;
			fgPicMark = picturesTaken;
			fgLogAt = now;
		}
	}

	/* The shape the frame is shown at: its own, 16:9 for a 4:3-sized frame of a console set to
	 * widescreen (anamorphic, as hakuX's "auto" does). */
	float displayAspect() const
	{
		float aspect = static_cast<float>(picture.width) / static_cast<float>(picture.height);
		if (showingLogo) {
			return showingFrozen && frozenAspect > 0.0f ? frozenAspect : aspect;   // the logo at its own shape
		}
		/* 0.1.26: the settings' aspect ratio (hakuX's: auto, 4:3, 16:9, stretch) */
		if (aspectMode == "4:3") {
			return 4.0f / 3.0f;
		}
		if (aspectMode == "16:9") {
			return 16.0f / 9.0f;
		}
		if (aspectMode == "stretch") {
			return static_cast<float>(width) / static_cast<float>(height);
		}
		if (pshx_core_widescreen() && aspect < 1.6f) {
			aspect = 16.0f / 9.0f;
		}
		return aspect;
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
		const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		pictureChanged = upload || fgShow;
		if (fgPush) {
			frameGen.push(cmdBuffer, staging[currentBuffer].buffer);
		}
		if (fgShow) {
			frameGen.show(cmdBuffer, currentBuffer, fgTime, picture.image, picture.layout);
			picture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		if (upload) {
			vks::tools::insertImageMemoryBarrier(cmdBuffer, picture.image,
				picture.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				picture.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, range);
			VkBufferImageCopy copy{};
			copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			copy.imageExtent = { picture.width, picture.height, 1 };
			vkCmdCopyBufferToImage(cmdBuffer, staging[currentBuffer].buffer, picture.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
			vks::tools::insertImageMemoryBarrier(cmdBuffer, picture.image,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, range);
			picture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		const bool smaaNow = smaaOn && smaaReady() && havePicture && !showingLogo &&
			picture.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		if (smaaNow) {
			recordSmaa(cmdBuffer);
		}
		beginDynamicRendering(cmdBuffer); // clears to black: the letterbox, or the whole screen
		const bool splash = library && screen == Screen::Boot && showingLogo;
		if ((havePicture || (showingLogo && (!library || splash))) &&
			picture.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			const float aspect = displayAspect();
			/* 0.1.26: the boot screen's logo in a box above its middle */
			const float boxW = static_cast<float>(width), boxH = splash ? static_cast<float>(height) * 0.46f : static_cast<float>(height);
			float w = boxW, h = boxH;
			if (w / h > aspect) {
				w = std::floor(h * aspect + 0.5f);
			} else {
				h = std::floor(w / aspect + 0.5f);
			}
			const float x = std::floor((static_cast<float>(width) - w) * 0.5f);
			const float y = splash ? std::floor(static_cast<float>(height) * 0.12f) : std::floor((static_cast<float>(height) - h) * 0.5f);
			VkViewport viewport{ x, y, w, h, 0.0f, 1.0f };
			if (cropOn && ((havePicture && !showingLogo) || showingFrozen)) {
				/* the overscan cropped: the picture drawn 1 / (1 - 2 * 3/80) larger about its
				 * middle, the scissor keeping its place (its shape kept: both sides alike) */
				const float k = 1.0f / (1.0f - 2.0f * 3.0f / 80.0f);
				viewport = VkViewport{ x - (k - 1.0f) * w * 0.5f, y - (k - 1.0f) * h * 0.5f, w * k, h * k, 0.0f, 1.0f };
			}
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			VkRect2D scissor{ { static_cast<int32_t>(x), static_cast<int32_t>(y) }, { static_cast<uint32_t>(w), static_cast<uint32_t>(h) } };
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
			VkPipeline chosen = pipeline;
			VkDescriptorSet set = descriptorSet;
			if (havePicture && !showingLogo) {
				const bool smaller = static_cast<float>(picture.width) < viewport.width - 0.5f || static_cast<float>(picture.height) < viewport.height - 0.5f;
				const bool larger = static_cast<float>(picture.width) > viewport.width + 0.5f || static_cast<float>(picture.height) > viewport.height + 0.5f;
				if (smaaNow && smaaValid) {
					set = smaaPresentSet;   // SMAA's result, scaled as the picture would be
				}
				if (fxaaOn && pipelineFxaa != VK_NULL_HANDLE) {
					chosen = pipelineFxaa;
				} else if (sgsrOn && smaller && pipelineSgsr != VK_NULL_HANDLE) {
					chosen = pipelineSgsr;
				} else if (sharpOn && smaller && pipelineSharp != VK_NULL_HANDLE) {
					chosen = pipelineSharp;
				} else if (ssaaOn && larger && pipelineSsaa != VK_NULL_HANDLE) {
					chosen = pipelineSsaa;   // averaged down over every texel a pixel covers
				}
			}
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, chosen);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr);
			if (chosen == pipelineSsaa || chosen == pipelineSharp) {
				const float metrics[4] = { 0.0f, 0.0f, viewport.width, viewport.height };
				vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(metrics), metrics);
			}
			vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
		}
		drawUI(cmdBuffer);
		endDynamicRendering(cmdBuffer);
		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	/* Each signed-in user's controller to the Core; the rumble back; the quit combination. */
	void feedControllers()
	{
		if (library) {
			libraryControls();
			return;
		}
		const uint32_t connected = pad_players();
		if (connected != players) {
			players = connected;
			pshx_core_set_players(connected);
			pshakux_log("controllers: players 0x%x", connected);
		}
		/* 0.1.53: player 1's OPTIONS opens the quick menu (or, held, in the "OPTIONS held" mode); the
		 * menu and its screens have the controller, and the game gets none while it is open */
		pad_player(0, &menuPad);
		ui::setScrollInput(menuPad.right_y);
		if (shareWait.active) {
			if ((menuPad.pressed & PAD_CIRCLE) && !relaunchAsked) {
				pshakux_log("ogbox: waiting for the share given up (CIRCLE)");
				relaunchToLibrary();
			}
			return;
		}
		gameMenuControls();
		const bool menu = qm.open || qm.sub != Quick::Sub::None;
		const bool holdMode = prefs.global("quick_menu") == "options_hold";
		const std::string quickMode = prefs.global("quick_menu");
		for (int player = 0; player < PAD_PLAYERS; player++) {
			const struct pad_reading *readings = nullptr;
			const int count = pad_player_readings(player, &readings);
			if (count <= 0) {
				continue;
			}
			/* the newest reading the shell did not have, and every button any of the frame's readings
			 * had: a tap that went down and up between two frames is held for the game (TapLatch) */
			int newest = -1;
			uint32_t any = 0;
			for (int i = 0; i < count; i++) {
				if (readings[i].connected && !(readings[i].buttons & PAD_INTERCEPTED)) {
					any |= readings[i].buttons;
					newest = i;
				}
			}
			if (newest >= 0) {
				const struct pad_reading &r = readings[newest];
				const uint32_t buttons = tapLatch[player].update(any, r.buttons, now_seconds());
				/* the touch pad's click: which half is decided when it goes down (the first reading
				 * with it down this frame, or the newest) */
				const bool touch = (buttons & PAD_TOUCH_PAD) != 0;
				if (touch && !touchWas[player]) {
					int16_t tx = r.touch_x;
					for (int i = 0; i < count; i++) {
						if ((readings[i].buttons & PAD_TOUCH_PAD) && readings[i].touch_x >= 0) {
							tx = readings[i].touch_x;
							break;
						}
					}
					touchBack[player] = tx < 960;
				}
				touchWas[player] = touch;
				ogbox::PadFrame f;
				f.buttons = buttons;
				if (touch) {
					f.buttons |= touchBack[player] ? ogbox::src::touchLeft : ogbox::src::touchRight;
				}
				f.lx = r.left_x;
				f.ly = r.left_y;
				f.rx = r.right_x;
				f.ry = r.right_y;
				f.l2 = r.l2;
				f.r2 = r.r2;
				PshxPad x;
				ogbox::mapPad(padMap, f, x);
				/* Start and Back left automatic: OPTIONS mode: the touch pad's left half Back, its right
				 * half Start; OPTIONS held mode: the whole pad Back. OPTIONS: the quick menu's for
				 * player 1, the others' Start. */
				const bool autoStart = padMap.source[static_cast<int>(ogbox::XboxControl::Start)] == ogbox::src::automatic;
				const bool autoBack = padMap.source[static_cast<int>(ogbox::XboxControl::Back)] == ogbox::src::automatic;
				/* 0.0.6: with a combination opening the menu, OPTIONS is Start and the whole touch pad Back
				 * (held, in the touch pad mode, it opens the menu: Back while held a moment only) */
				const bool comboMode = quickMode == "combo" || quickMode == "touch_hold" || quickMode == "l3r3";
				const bool back = holdMode || comboMode || touchBack[player];
				const bool shouldersHeld = player == 0 && quickMode == "combo" && (buttons & (PAD_L1 | PAD_R1)) == (PAD_L1 | PAD_R1) &&
					r.l2 > 127 && r.r2 > 127;
				if (touch && back && autoBack && !(player == 0 && quickMode == "touch_hold" && qm.optionsSince >= 0.0 &&
						now_seconds() - qm.optionsSince > 0.25)) {
					x.buttons |= PSHX_PAD_BACK;
				}
				if (autoStart && ((touch && !back) || ((buttons & PAD_OPTIONS) && (player != 0 || comboMode) && !shouldersHeld) ||
						(player == 0 && now_seconds() < qm.startPulseUntil))) {
					x.buttons |= PSHX_PAD_START;   // (the last: a short OPTIONS press in the held mode)
				}
				if (menu) {
					x = PshxPad{};
				}
				pshx_core_set_pad(static_cast<unsigned>(player), &x);
			}
			float large = 0.0f, small = 0.0f;
			pshx_core_rumble(static_cast<unsigned>(player), &large, &small);
			/* 0.0.6: Settings > Controller's rumble strength (a percent, PSWin's way) */
			const float strength = menu ? 0.0f : static_cast<float>(rumblePercent) / 100.0f;
			pad_player_vibrate(player, large * strength, small * strength);
		}
		struct pad first{};
		pad_player(0, &first);
		const double now = now_seconds();
		const uint32_t sticks = PAD_L3 | PAD_R3;
		if ((first.held & sticks) == sticks) {
			if (shotHeldSince < 0.0) {
				shotHeldSince = now;
			} else if (now - shotHeldSince >= 1.0 && shotHeldSince < 1e17) {
				shotWanted = true;
				shotHeldSince = 1e18;   // once a hold
			}
		} else {
			shotHeldSince = -1.0;
		}
	}

	/* The menus' controller: one screen has it a frame, and reads it itself (with the D-pad's
	 * repeats, L2 / R2 as presses). */
	void libraryControls()
	{
		if (buzzUntil > 0.0 && now_seconds() >= buzzUntil) {
			buzzUntil = 0.0;
			pad_vibrate(0.0f, 0.0f);
		}
		pad_player(0, &menuPad);
		ui::setScrollInput(menuPad.right_y);   // 0.0.6: long text scrolls with the right stick
		const uint32_t pressed = menuPad.pressed;
		const uint32_t nav = navPressed();   // 0.0.3: with the D-pad's repeats, L2 / R2 as presses
		/* 0.1.53: one owner of the controller a frame: the keyboard (0.0.3), a list of choices, the
		 * game page, the hard disk */
		if (kb.isOpen()) {
			keyboardInput(nav);
			return;
		}
		if (pop.open) {
			popupInput(nav);
			return;
		}
		if (gp.open && screen == Screen::Library) {
			gamePageInputNew(nav);
			return;
		}
		if (screen == Screen::Hdd) {
			hddInput(pressed);
			return;
		}
		if (screen == Screen::Notice) {
			/* 0.0.1: CROSS, after a second (a press held from the launch is not an answer) */
			if ((pressed & PAD_CROSS) && now_seconds() - screenSince > 1.0) {
				screen = afterNotice;
				screenSince = now_seconds();
				pshakux_log("ogbox: the experimental build's notice read");
				if (FILE *f = std::fopen(kNoticeSeenFile, "w")) {
					std::fputs(pshakux_build_label(), f);
					std::fclose(f);
				}
			}
			return;
		}
		switch (screen) {
		case Screen::Boot:
			/* CROSS or CIRCLE skips the splash (0.0.1: no longer said on it) */
			if ((pressed & (PAD_CROSS | PAD_CIRCLE)) || now_seconds() - screenSince > 3.2) {
				screen = Screen::Library;
				screenSince = now_seconds();
			}
			break;
		case Screen::Library: homeInput(nav); break;
		case Screen::Settings: settingsInput(nav); break;
		case Screen::Storage: storageInput(nav); break;
		case Screen::Mods: modsInput(nav); break;
		default: break;
		}
	}

	/* The settings screen: the app's ("" key), or a game's. */
	void openSettings(const std::string &key, const std::string &title)
	{
		forGame = !key.empty();
		forGameKey = key;
		forGameTitle = title;
		prefs.load(key);
		section = 0;
		setNavSel = 0;   // 0.0.3
		setRowSel = 0;
		setFocusNav = true;
		form.open = false;
		screen = Screen::Settings;
		screenSince = now_seconds();
		pshakux_log("ogbox: settings%s%s", forGame ? " of " : "", forGame ? key.c_str() : "");
	}

	/* ---- 0.1.54: the trainers ------------------------------------------------------------------- */

	/* Where they are looked for: the data folder's trainers, /data/ogbox/trainers, the USB drives'. */
	void setTrainerRoots()
	{
		std::vector<std::string> roots = { ogbox::storagePath("trainers"), std::string(ogbox::kConfigRoot) + "/trainers" };
		for (int i = 0; i < 8; i++) {
			roots.push_back("/mnt/usb" + std::to_string(i) + "/OGBox/trainers");
		}
		std::vector<const char *> c;
		for (const std::string &r : roots) {
			c.push_back(r.c_str());
		}
		ogbox_trainers_set_roots(c.data(), static_cast<int>(c.size()));
	}

	/* The folders read on a thread of their own (the index makes it quick after the first time). */
	void scanTrainers()
	{
		setTrainerRoots();
		mkdir(ogbox::storagePath("trainers").c_str(), 0777);   // the folder to copy them into
		const std::string cache = ogbox::storagePath("cache");
		mkdir(cache.c_str(), 0777);
		const std::string index = cache + "/trainers.idx";
		std::thread([index]() { ogbox_trainers_scan(index.c_str()); }).detach();
	}

	/* The Mods screen's trainers for tv.titleId, and the one chosen with its options. */
	void loadTrainerView()
	{
		unsigned gen = 0;
		if (ogbox_trainers_state(&gen, nullptr) == 0) {
			scanTrainers();
		}
		tv.generation = gen;
		tv.list.assign(64, OgboxTrainer{});
		tv.list.resize(static_cast<size_t>(std::max(0, ogbox_trainers_for_title(tv.titleId, tv.list.data(), 64))));
		const uint32_t mine = tv.xbeTime;
		std::stable_sort(tv.list.begin(), tv.list.end(), [mine](const OgboxTrainer &a, const OgboxTrainer &b) {
			const bool ao = mine && a.xbe_time && a.xbe_time != mine, bo = mine && b.xbe_time && b.xbe_time != mine;
			return !ao && bo;
		});
		char rel[384] = "", bits[128] = "";
		tv.chosen.clear();
		tv.bits.clear();
		if (!modsKey.empty() && ogbox_trainers_chosen(modsKey.c_str(), rel, sizeof(rel), bits, sizeof(bits))) {
			tv.chosen = rel;
			tv.bits = bits;
		}
		loadTrainerOptions();
	}

	void loadTrainerOptions()
	{
		tv.options.clear();
		tv.scroller.clear();
		if (tv.chosen.empty()) {
			return;
		}
		static char names[OGBOX_TRAINER_MAX_OPTIONS][OGBOX_TRAINER_TEXT];
		char scroller[256] = "";
		const int n = ogbox_trainers_options(tv.chosen.c_str(), names, OGBOX_TRAINER_MAX_OPTIONS, scroller, sizeof(scroller));
		for (int i = 0; i < n; i++) {
			tv.options.push_back(names[i]);
		}
		tv.scroller = scroller;
		tv.bits.resize(tv.options.size(), '0');
	}

	void chooseTrainer(const std::string &rel)
	{
		ogbox_trainers_choose(modsKey.c_str(), rel.c_str());
		tv.chosen = rel;
		tv.bits.clear();
		loadTrainerOptions();
		detailsPath.clear();   // the details again, with the change
	}

	static bool hasIgk(const std::string &name)
	{
		std::string u = name;
		for (char &ch : u) {
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}
		return u.find("IGK") != std::string::npos || u.find("IN GAME") != std::string::npos ||
			u.find("IN-GAME") != std::string::npos;
	}

	/* The Mods screen's rows: its mods, patches and cheats by kind, then the trainers (none, each for
	 * the game, the chosen one's options under it). Each row's press and words beside the list. */
	struct ModsRow {
		std::function<void()> press;
		std::string help;
	};

	void modsModel(std::vector<ui::ListRow> &rows, std::vector<ModsRow> &acts)
	{
		using R = ui::ListRow;
		/* each row's key is its own (a mod's id, a trainer's file): the cursor follows it when the rows
		 * above it change (a trainer chosen: its options in, the last one's out) */
		const auto add = [&](R r, ModsRow a) {
			if (r.key.empty()) {
				r.key = "s" + std::to_string(rows.size());
			}
			rows.push_back(std::move(r));
			acts.push_back(std::move(a));
		};
		const auto section = [&](const std::string &title, int indent = 0) {
			R r;
			r.kind = R::Kind::Section;
			r.title = title;
			r.indent = indent;
			add(r, {});
		};
		if (modList.empty()) {
			R r;
			r.key = "mempty";
			r.title = "No mods, patches or cheats for this game";
			r.sub = "hakuX's, in the title folder's mods/" + modsKey + "/";
			r.icon = ui::Icon::Info;
			r.dim = true;
			add(r, { nullptr, "OGBox has hakuX's mods (the title folder's mods/); a game's are in mods/" + modsKey +
				"/: a folder a mod, cheats.txt the cheats. Patches come from gamedata/patches.db." });
		}
		const auto kindOf = [](const OgboxModEntry &e) { return e.cheat ? 2 : e.patch ? 1 : e.gamefix ? -1 : 0; };
		for (size_t i = 0; i < modList.size(); i++) {
			OgboxModEntry &m = modList[i];
			/* 0.0.6: the patches in one group, Patches (no headings by kind: "Misc" was most of them) */
			if (i == 0 || kindOf(modList[i - 1]) != kindOf(m)) {
				section(m.cheat ? "Cheats" : m.patch ? "Patches" : m.gamefix ? "Game fixes \xC2\xB7 always on" : "Mods");
			}
			/* 0.0.2: the release check (core/ogbox_mods.h): one made for another release of the game is
			 * greyed out, without its switch, and can't be turned on (one on from before can be turned
			 * off: it's left out at the start anyway). 0.0.3: a game fix is on whatever is chosen. */
			const bool other = m.fit == OGBOX_FIT_NO, unchecked = m.fit == OGBOX_FIT_UNCHECKED;
			R r;
			r.key = "m" + std::to_string(kindOf(m)) + ":" + m.id;
			r.title = m.name;
			r.sub = m.patch ? std::string("by ") + m.author : std::string();
			r.right = other ? std::string(m.on ? "Incompatible: left out" : "Incompatible")
				: m.gamefix ? std::string("always on")
				: m.cheat ? (m.once ? "once" : std::string())
				: (!m.patch && m.refresh ? std::to_string(m.refresh) + " Hz" : std::string());
			if (unchecked && !m.patch && !m.gamefix) {
				r.right = r.right.empty() ? std::string("release not checked") : r.right + " \xC2\xB7 release not checked";
			}
			r.toggle = other && !m.on ? -1 : m.on ? 1 : 0;
			r.dim = other || m.gamefix;
			if (other && m.on) {
				r.dot = ui::P().fail;
			}
			std::string help;
			if (other) {
				/* 0.0.6: which version of the game it is for */
				const std::string region = m.region[0] ? std::string(m.region) : std::string();
				help = (region.empty() ? std::string("Incompatible: made for another version of the game")
						: "Incompatible: made for the " + region + " version of the game") +
					(m.fit_why[0] ? std::string(" (") + m.fit_why + ")" : std::string()) + ". " +
					(m.on ? "It's left out when the game starts. CROSS turns it off."
						  : "It can't be turned on: it would change bytes this disc doesn't have where it expects them.");
			} else {
				help = m.description[0] ? std::string(m.description) + " " : std::string(m.cheat ? "A cheat. " : m.patch ? "" : "A mod. ");
				if (m.patch) {
					help += std::string("By ") + m.author + ", from " + m.source + ". Applied at the game's next start; a patch "
						"whose bytes are not in this disc's XBE is left out (the boot log says so).";
					const std::string all = std::string(m.name) + " " + m.description;
					if (all.find("128") != std::string::npos) {
						help += " Needs the 128 MB RAM upgrade: with Settings > Advanced > Memory on Auto, OGBox gives the game 128 MB while it's on.";
					}
				} else if (m.gamefix) {
					help += "A game fix: OGBox always applies it, because the game doesn't run well on the PS5 without it. It can't be turned off.";
				} else {
					help += m.cheat ? "Cheats apply while the game runs, from its next start." : "Mods change the game's code and files, from its next start.";
				}
				if (unchecked && !m.patch) {
					help += m.cheat ? " Their release isn't checked: cheats.txt has no crc: line (the CRC-32 of the default.xbe "
						"they're for). On another release with the same title ID and version they may write the wrong places."
						: " Its release isn't checked: none of its lines says which bytes it replaces (\"old -> new\"). On "
						"another release with the same title ID and version it may break the game.";
				}
			}
			std::function<void()> press;
			if (!m.gamefix && !(other && !m.on)) {
				press = [this, i]() {
					OgboxModEntry &e = modList[i];
					e.on = !e.on;
					std::lock_guard<std::mutex> disc(g_discInfoMutex);
					ogbox_mods_choose_kind(modsKey.c_str(), e.cheat ? "cheat" : e.patch ? "patch" : "mod", e.id, e.on);
					detailsPath.clear();   // the details again, with the change
				};
			}
			add(r, { press, help });
		}
		// ---- 0.1.54: the trainers
		unsigned gen = 0, found = 0;
		const int state = ogbox_trainers_state(&gen, &found);
		if (state == 2 && gen != tv.generation) {
			loadTrainerView();   // a scan finished meanwhile
		}
		section("Trainers \xC2\xB7 one at a time, run as the game starts");
		bool chosenListed = false;
		for (const OgboxTrainer &t : tv.list) {
			chosenListed |= tv.chosen == t.rel;
		}
		if (tv.list.empty() && tv.chosen.empty()) {
			R r;
			r.key = "tempty";
			r.icon = state != 2 ? ui::Icon::None : ui::Icon::Info;
			r.dim = true;
			std::string help;
			if (state != 2) {
				r.title = "Reading the trainers folder\xE2\x80\xA6";
			} else if (!found) {
				r.title = "No trainers yet";
				r.sub = "Copy .etm and .xbtf trainers into " + ogbox::storagePath("trainers");
				help = "Copy the Xbox's .etm and .xbtf trainers into " + ogbox::storagePath("trainers") + " (subfolders are fine) or a "
					"USB drive's OGBox/trainers: each game lists its own here.";
			} else {
				char t[64];
				std::snprintf(t, sizeof(t), "%08X", static_cast<unsigned>(tv.titleId));
				r.title = "None for this game";
				r.sub = std::to_string(found) + " trainers in the folders, none for title ID " + t;
				help = "None of the " + std::to_string(found) + " trainers in the folders is for this game (title ID " + t + ").";
			}
			add(r, { nullptr, help });
			return;
		}
		{
			R r;
			r.key = "tnone";
			r.title = "No trainer";
			r.toggle = tv.chosen.empty() ? 1 : 0;
			add(r, { [this]() { chooseTrainer(std::string()); }, "The game starts without a trainer." });
		}
		const auto options = [&]() {
			for (size_t k = 0; k < tv.options.size(); k++) {
				const bool o = k < tv.bits.size() && tv.bits[k] == '1';
				const std::string &name = tv.options[k];
				const bool igk = hasIgk(name);
				R r;
				r.key = "to" + std::to_string(k);
				r.title = name.empty() ? "(option " + std::to_string(k + 1) + ")" : name;
				r.right = igk ? "in-game keys" : "";
				r.toggle = o ? 1 : 0;
				r.indent = 1;
				std::string help = "One of the trainer's options: on, it's set as the game starts. ";
				if (igk) {
					help += "In-game keys: the trainer also turns it on and off in the game with a button combination "
						"(its readme says which); the console's LED shows it, top right. ";
				}
				if (!tv.scroller.empty()) {
					help += "The trainer says: " + tv.scroller;
				}
				add(r, { [this, k]() {
					if (tv.bits.size() < tv.options.size()) {
						tv.bits.resize(tv.options.size(), '0');
					}
					tv.bits[k] = tv.bits[k] == '1' ? '0' : '1';
					ogbox_trainers_set_options(modsKey.c_str(), tv.bits.c_str());
					detailsPath.clear();
				}, help });
			}
		};
		for (size_t i = 0; i < tv.list.size(); i++) {
			const OgboxTrainer &t = tv.list[i];
			const bool on = tv.chosen == t.rel;
			/* 0.0.2: one for another release is greyed out and can't be chosen (one chosen from before
			 * can be put back to none: it doesn't run at the start anyway) */
			const bool other = tv.xbeTime && t.xbe_time && t.xbe_time != tv.xbeTime;
			R r;
			r.key = "t" + std::string(t.rel);
			r.title = t.name[0] ? t.name : baseName(t.rel);
			r.sub = std::string(t.author[0] ? t.author : "a trainer") + " \xC2\xB7 " + (t.xbtf ? "XBTF" : "ETM");
			r.right = other ? std::string(on ? "Incompatible: doesn't run" : "Incompatible")
				: std::to_string(t.noptions) + (t.noptions == 1 ? " option" : " options");
			r.toggle = other && !on ? -1 : on ? 1 : 0;
			r.dim = other;
			if (other && on) {
				r.dot = ui::P().fail;
			}
			std::string help = std::string(t.author[0] ? t.author : "A trainer") + ". " + (t.xbtf ? "XBTF" : "ETM") + ", " +
				std::to_string(t.noptions) + (t.noptions == 1 ? " option" : " options") + ": " + t.rel + ". ";
			if (other) {
				char d[256];   // 0.0.1: 96 cut the sentence short
				std::snprintf(d, sizeof(d), "Incompatible: made for another version of the game (its default.xbe dated %08X, this "
					"disc's %08X): it would find nothing to change, so it can't be chosen%s. ", static_cast<unsigned>(t.xbe_time),
					static_cast<unsigned>(tv.xbeTime), on ? ", and it doesn't run at the start" : "");
				help += d;
			}
			help += on ? "Chosen: CROSS again for none. Its options are below it."
				: other ? std::string() : "CROSS chooses it (one trainer at a time).";
			std::function<void()> press;
			if (!(other && !on)) {
				const std::string rel = t.rel;
				press = [this, rel, on]() { chooseTrainer(on ? std::string() : rel); };
			}
			add(r, { press, help });
			if (on) {
				options();
			}
		}
		if (!tv.chosen.empty() && !chosenListed) {
			R r;
			r.key = "tgone";
			r.title = baseName(tv.chosen);
			r.right = "not in the folders now";
			r.dot = ui::P().fail;
			r.toggle = 1;
			add(r, { [this]() { chooseTrainer(std::string()); }, "The trainer chosen for this game (" + tv.chosen + ") isn't in the "
				"trainers folders any more: the game starts without it. CROSS: none." });
			options();
		}
	}

	/* The Mods screen's first row that does something (else its first). */
	static int modsFirst(const std::vector<ui::ListRow> &rows, const std::vector<ModsRow> &acts)
	{
		for (size_t i = 0; i < rows.size(); i++) {
			if (rows[i].kind == ui::ListRow::Kind::Item && acts[i].press) {
				return static_cast<int>(i);
			}
		}
		return ui::listStep(rows, -1, 1);
	}

	void modsInput(uint32_t p)
	{
		std::vector<ui::ListRow> rows;
		std::vector<ModsRow> acts;
		modsModel(rows, acts);
		if (modsSel < 0 || modsSel >= static_cast<int>(rows.size()) || rows[static_cast<size_t>(modsSel)].kind != ui::ListRow::Kind::Item) {
			modsSel = modsFirst(rows, acts);
		}
		if (p & PAD_UP) {
			modsSel = ui::listStep(rows, modsSel, -1);
		}
		if (p & PAD_DOWN) {
			modsSel = ui::listStep(rows, modsSel, 1);
		}
		if ((p & PAD_CROSS) && modsSel >= 0 && acts[static_cast<size_t>(modsSel)].press) {
			const std::string key = rows[static_cast<size_t>(modsSel)].key;
			acts[static_cast<size_t>(modsSel)].press();
			// the rows again (a trainer's options in or out): the cursor stays on the row pressed
			rows.clear();
			acts.clear();
			modsModel(rows, acts);
			for (size_t i = 0; i < rows.size(); i++) {
				if (rows[i].key == key) {
					modsSel = static_cast<int>(i);
				}
			}
		}
		if (p & PAD_CIRCLE) {
			if (!library) {
				pshakux_log("quick menu: back from the game's mods");
				qm.sub = Quick::Sub::None;   // back to the quick menu
				prefs.load(runKey);
			} else {
				screen = Screen::Library;
				screenSince = now_seconds();
				reopenGamePage();
			}
		}
	}

	/* The game's mods, patches, cheats and trainers: CROSS turns one on or off. */
	void drawMods()
	{
		ui::ListScreenView v;
		v.head.logo = uiLogo();
		v.head.title = "Game Patches";   // 0.0.6
		v.head.subtitle = modsTitle + " \xC2\xB7 " + modsKey;
		std::vector<ModsRow> acts;
		modsModel(v.rows, acts);
		if (modsSel < 0 || modsSel >= static_cast<int>(v.rows.size()) || v.rows[static_cast<size_t>(modsSel)].kind != ui::ListRow::Kind::Item) {
			modsSel = modsFirst(v.rows, acts);
		}
		v.sel = modsSel;
		if (modsSel >= 0) {
			v.aside.title = v.rows[static_cast<size_t>(modsSel)].title;
			v.aside.text = acts[static_cast<size_t>(modsSel)].help;
			v.aside.warn = v.rows[static_cast<size_t>(modsSel)].dot != 0;
		}
		char crc[64] = "";
		if (modsCrc) {
			std::snprintf(crc, sizeof(crc), " \xC2\xB7 default.xbe CRC-32 %08X", static_cast<unsigned>(modsCrc));
		}
		v.aside.note = std::string("/data/ogbox/mods.txt") + crc + " \xC2\xB7 the trainers: " + ogbox::storagePath("trainers");
		v.footer = { { "UD", "Choose" }, { "X", "On / off" }, { "O", library ? "Back" : "Back to the menu" } };
		ImDrawList *dl = beginUiScreen("##mods", library ? Ground::Full : Ground::Dim);
		ui::drawListScreen(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)), v);
		endUiScreen();
	}

	void openMods(const std::string &key, const std::string &title, const std::string &image = std::string())
	{
		modsKey = key;
		modsTitle = title;
		std::lock_guard<std::mutex> disc(g_discInfoMutex);
		/* 0.1.52: the disc's default.xbe's CRC-32, for the patches made for another release of the game.
		 * 0.0.2: the release check of its mods, patches and cheats against the disc (core/ogbox_mods.h:
		 * one made for another release is greyed out and can't be turned on); in a game, the start's
		 * (ogbox_mods_prepare) */
		uint32_t crc = 0;
		if (!image.empty() && ogbox_mods_check_disc(image.c_str(), key.c_str(), &crc)) {
			pshakux_log("ogbox: %s: default.xbe's CRC-32 %08X", key.c_str(), static_cast<unsigned>(crc));
		}
		modsCrc = ogbox_mods_disc_crc(key.c_str());
		modList.assign(480, OgboxModEntry{});
		modList.resize(static_cast<size_t>(std::max(0, ogbox_mods_catalog(key.c_str(), modList.data(), 480))));
		/* 0.1.54: the trainers for its title ID, those for this release (its default.xbe's date) first */
		tv.titleId = static_cast<uint32_t>(std::strtoul(key.substr(0, 8).c_str(), nullptr, 16));
		tv.xbeTime = runXbeTime;
		if (!image.empty()) {
			OgboxGameInfo info{};
			if (ogbox_game_info(image.c_str(), &info)) {
				tv.xbeTime = info.xbe_time;
			}
		}
		loadTrainerView();
		screen = Screen::Mods;
		screenSince = now_seconds();
		modsSel = -1;
	}

	void renderLibrary()
	{
		if (!trainerScanStarted && screen == Screen::Library) {
			trainerScanStarted = true;   // 0.1.54: the trainers folders read once, in the background
			scanTrainers();
		}
		collectListing();
		coverHousekeeping();
		if (!startPath.empty() && --startDelay < 0) {
			const std::string path = startPath;
			startPath.clear();
			std::string key;
			if (details && detailsPath == path) {
				std::lock_guard<std::mutex> lock(details->mutex);
				if (details->done && details->info.ok) {
					key = details->info.key;
				}
			}
			std::vector<const char *> args;
			if (path == "none") {
				args = { "--boot", "none", "--option", "dash" };
			} else {
				writeLastGame(path);
				ogbox::notePlayed(path);
				args = { "--boot", path.c_str() };
				if (!key.empty()) {
					args.push_back("--game");
					args.push_back(key.c_str());
				}
				if (!startState.empty()) {
					args.push_back("--loadstate");   // 0.1.53: from the game page's Load state
					args.push_back(startState.c_str());
				}
			}
			args.push_back(nullptr);
			quiesceForExit();
			const int r = pshakux_relaunch(args.data());
			startState.clear();
			listError = "The console refused to start the title again (" + std::to_string(r) + ").";
			pshakux_log("library: the relaunch for %s was refused (%#x)", path.c_str(), static_cast<unsigned>(r));
		}
		ui.visible = true;
		VulkanExampleBase::prepareFrame();
		upload = screen == Screen::Boot && fetchLogo();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
		framesDrawn++;
	}

	void render() override
	{
		if (!prepared) {
			return;
		}
		feedControllers();
		/* every screen reads the controller itself (ogbox_ui draws them): ImGui's navigation gets none of it */
		std::memset(ImGui::GetIO().NavInputs, 0, sizeof(ImGui::GetIO().NavInputs));
		if (library) {
			renderLibrary();
			return;
		}
		if (shareWait.active) {
			pollShareWait();
		}
		const int state = pshx_core_state();
		if (state != lastState) {
			lastState = state;
			pshakux_log("ogbox: core %s%s%s", stateName(state), pshx_core_message()[0] ? ": " : "",
				pshx_core_message());
		}
		if (!relaunchAsked && pshx_core_relaunch_wanted()) {
			relaunchForGuest();
		}
		// The overlay only says what went wrong; OPTIONS is the game's START here, not the
		// template's settings window.
		ui.visible = true;   // 0.1.26: the FPS counter and the quit ring over the game too
		VulkanExampleBase::prepareFrame();
		pshx_core_display_vsync();    // 0.0.4: the display let the loop go (FIFO: once a refresh)
		fetchPicture();
		frameGenPlan();
		probeStall(state);
		if (shotWanted && upload && havePicture) {
			shotWanted = false;
			takeShot();
		}
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
		framesDrawn++;
	}

	/* 0.1.28: a running game that has given no new frame for 0.4 s: where its emulated CPU's thread
	 * is, every 0.3 s, at most 8 times a stall and 400 a run (pshakux_stall_report, the boot log's
	 * "stall:" lines). env OGBOX_STALL_PROBE=0 in boot.txt: none. */
	void probeStall(int state)
	{
		if (probeWanted < 0) {
			const char *e = std::getenv("OGBOX_STALL_PROBE");
			probeWanted = !(e && e[0] == '0');
		}
		const double now = now_seconds();
		if (picturesTaken != probePictures) {
			probePictures = picturesTaken;
			probeLastFrame = now;
			if (probeThisStall) {
				pshakux_log("stall: over, a new frame after the probe's %u looks", probeThisStall);
			}
			probeThisStall = 0;
			return;
		}
		if (!probeWanted || !havePicture || state != PSHX_CORE_RUNNING || probeLastFrame < 0.0 ||
			now - probeLastFrame < 0.4 || now < probeNext || probeThisStall >= 8 || probeTotal >= 400) {
			return;
		}
		pshakux_stall_report(now - probeLastFrame);
		probeNext = now + 0.3;
		probeThisStall++;
		probeTotal++;
	}

	/* The frame just taken from the core (this frame's staging buffer) to /data/ogbox/shots. */
	void takeShot()
	{
		const std::string folder = ogbox::storagePath("shots");
		mkdir(folder.c_str(), 0777);
		char name[96];
		std::snprintf(name, sizeof(name), "/shot-%04u-%.0fs.bmp", ++shotsTaken, now_seconds());
		const std::string path = folder + name;
		const bool ok = writeBmp(path, static_cast<const uint8_t *>(staging[currentBuffer].mapped), picture.width,
			picture.height, picture.width * 4);
		pshakux_log("ogbox: screenshot %ux%u %s: %s", picture.width, picture.height, path.c_str(), ok ? "saved" : std::strerror(errno));
	}

	// ---- 0.1.26: drawing the menus ------------------------------------------------------------

	/* The falling code (the Code theme's ground), in a draw list; none inside `hole`. */
	void drawRain(ImDrawList *dl, float alpha, const ImVec4 &hole = ImVec4(0, 0, 0, 0))
	{
		if (!rainOn || alpha <= 0.0f) {
			return;
		}
		static const char kGlyphs[] = "0123456789ABCDEFGHJKLMNPRSTUVWXYZ:=+*<>|$#%&?";
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const float glyph = ImGui::GetFontSize() * 0.8f;
		const auto pick = [&]() { return kGlyphs[rng() % (sizeof(kGlyphs) - 1)]; };
		const auto reset = [&](Drop &d, bool anywhere) {
			d.length = 8 + static_cast<int>(rng() % 22);
			d.speed = glyph * (5.0f + static_cast<float>(rng() % 1000) / 1000.0f * 13.0f);
			d.alpha = 0.25f + static_cast<float>(rng() % 1000) / 1000.0f * 0.75f;
			d.y = anywhere ? static_cast<float>(rng() % static_cast<unsigned>(H + 1.0f))
				: -static_cast<float>(rng() % static_cast<unsigned>(H * 0.5f + 1.0f));
			d.glyphs.resize(static_cast<size_t>(d.length));
			for (char &g : d.glyphs) {
				g = pick();
			}
		};
		if (rainWidth != W || rainGlyph != glyph || rain.empty()) {
			rain.clear();
			rainWidth = W;
			rainGlyph = glyph;
			const int columns = static_cast<int>(W / (glyph * 1.15f));
			for (int i = 0; i < columns; i++) {
				Drop d{};
				d.x = static_cast<float>(i) * glyph * 1.15f;
				reset(d, true);
				rain.push_back(d);
			}
			rainLast = now_seconds();
		}
		const double now = now_seconds();
		const float dt = static_cast<float>(std::min(0.1, std::max(0.0, now - rainLast)));
		rainLast = now;
		ImFont *font = ImGui::GetFont();
		const ImU32 body = ui::P().accent, head = ui::mix(ui::P().accent, IM_COL32(255, 255, 255, 255), 0.65f);
		for (Drop &d : rain) {
			d.y += d.speed * dt;
			if (d.y - static_cast<float>(d.length) * glyph > H) {
				reset(d, false);
			}
			if (rng() % 24 == 0) {
				d.glyphs[rng() % d.glyphs.size()] = pick();
			}
			for (int k = 0; k < d.length; k++) {
				const float y = d.y - static_cast<float>(k) * glyph;
				if (y < -glyph || y > H) {
					continue;
				}
				if (hole.z > 0.0f && d.x + glyph > hole.x && d.x < hole.x + hole.z && y + glyph > hole.y && y < hole.y + hole.w) {
					continue;
				}
				const float fade = 1.0f - static_cast<float>(k) / static_cast<float>(d.length);
				const ImU32 c = k == 0 ? ui::alpha(head, alpha * d.alpha) : ui::alpha(body, alpha * d.alpha * fade * 0.85f);
				const char *g = &d.glyphs[static_cast<size_t>(k)];
				dl->AddText(font, glyph, ImVec2(d.x, y), c, g, g + 1);
			}
		}
	}

	/* The boot screen: the logo (the presenter's), a bar filling, the start's lines typed. */
	void drawBoot()
	{
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const float aspect = logoWidth && logoHeight ? static_cast<float>(logoWidth) / static_cast<float>(logoHeight) : 1.5f;
		const float logoH = H * 0.46f, logoW = std::min(W, logoH * aspect);
		ui::BootView v;
		v.logoBox = ImVec4((W - logoW) * 0.5f, H * 0.12f, logoW, logoH);   // as buildCommandBuffer places it
		v.elapsed = static_cast<float>(now_seconds() - screenSince);
		v.progress = std::min(1.0f, v.elapsed / 2.6f);
		v.left = "Starting the Xbox";
		v.right = std::string("OGBox ") + pshakux_build_label();
		v.lines = bootLines;
		ImDrawList *dl = beginUiScreen("##boot", Ground::None);
		if (themeCode) {
			drawRain(dl, 0.9f, v.logoBox);
		}
		ui::drawBoot(dl, ImVec2(W, H), v);
		endUiScreen();
	}

	/* 0.0.1: before the splash, what this build is: an experimental one, crashes and freezes
	 * expected; no games with it. CROSS goes on. */
	void drawNotice()
	{
		ui::NoticeView v;
		/* 0.0.6: Press5elect's words */
		v.kicker = "Not so much of an experimental build";
		v.title = "But it might still crash or show glitches in games";
		v.subtitle = std::string("OGBox ") + pshakux_build_label() + " for PS5";
		v.lead = "If anything goes wrong, please send me ogbox-boot.log from OGBox's title folder.";
		v.paragraphs = { "Thanks, and enjoy!", "Press5elect" };
		v.fine = "OGBox comes with no games, BIOS or keys. Use it only with backups of games you own.";
		v.canContinue = now_seconds() - screenSince > 1.0;
		bd.key = "notice";
		bd.blur = ui::Tex{};
		bd.tint = 0;
		bd.focus = ImVec2(0.5f, 0.3f);
		bd.strength = 0.5f;
		ImDrawList *dl = beginUiScreen("##notice");
		ui::drawNotice(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)), v);
		endUiScreen();
	}

	/* The selected entry of the list, or nothing. */
	const LibraryEntry *selectedEntry() const
	{
		return selected >= 0 && selected < static_cast<int>(entries.size()) ? &entries[static_cast<size_t>(selected)] : nullptr;
	}

	/* The selected disc's details, read once the selection has rested a moment. */
	void ensureDetails()
	{
		const LibraryEntry *e = selectedEntry();
		const std::string path = e && e->kind == LibraryEntry::Image ? e->path : std::string();
		const double now = now_seconds();
		if (path != restingPath) {
			restingPath = path;
			restingSince = now;
		}
		/* (not while a folder is being listed: one request at a time to a share's server; not while a
		 * game's page is open: the page reads its own disc, gamePageInfo) */
		if (path.empty() || path == detailsPath || now - restingSince < 0.25 || listing || gp.open) {
			return;
		}
		startDetails(path);
	}

	/* 0.0.3: a disc's details read on a thread (the selection's, or the game page's own) */
	void startDetails(const std::string &path)
	{
		detailsPath = path;
		detailsSince = now_seconds();
		auto job = std::make_shared<Details>();
		details = job;
		std::thread([job, path]() {
			std::lock_guard<std::mutex> disc(g_discInfoMutex);
			OgboxGameInfo info{};
			ogbox_game_info(path.c_str(), &info);
			int mods = 0, modsOn = 0, cheats = 0, cheatsOn = 0, overrides = 0, patches = 0, patchesOn = 0;
			if (info.ok) {
				std::vector<OgboxModEntry> list(480);
				const int n = ogbox_mods_catalog(info.key, list.data(), 480);
				for (int i = 0; i < n; i++) {
					const OgboxModEntry &m = list[static_cast<size_t>(i)];
					(m.cheat ? cheats : m.patch ? patches : mods)++;
					if (m.on) {
						(m.cheat ? cheatsOn : m.patch ? patchesOn : modsOn)++;
					}
				}
				ogbox::Settings s;
				s.load(info.key);
				overrides = s.overrides();
			}
			int trainers = 0;
			bool trainerOn = false;
			if (info.ok) {
				char rel[384], bits[128];
				trainers = ogbox_trainers_count(info.title_id);
				trainerOn = ogbox_trainers_chosen(info.key, rel, sizeof(rel), bits, sizeof(bits));
			}
			std::lock_guard<std::mutex> lock(job->mutex);
			job->trainers = trainers;
			job->trainerOn = trainerOn;
			job->info = info;
			job->mods = mods;
			job->modsOn = modsOn;
			job->cheats = cheats;
			job->cheatsOn = cheatsOn;
			job->overrides = overrides;
			job->patches = patches;
			job->patchesOn = patchesOn;
			job->done = true;
		}).detach();
	}

	/* 0.1.48: the small pictures before a row's text */
	/* 0.1.54: what the game's trainer shows: loaded and its LCD lines as a note at the foot, the
	 * console's LED (its options going on and off, as on the Xbox) top right for a few seconds */
	void pollTrainer()
	{
		int kind = 0;
		unsigned led = 0;
		char text[160];
		while (pshx_core_trainer_message(&kind, &led, text, sizeof(text))) {
			if (kind == 1) {
				showToast(std::string("Trainer on: ") + text);
			} else if (kind == 2) {
				showToast(std::string("Trainer: ") + text);
			} else if (kind == 3) {
				ledSeq = led;
				ledText = text;
				ledUntil = now_seconds() + 4.0;
			}
		}
	}

	/* Over a running game: the frame rate, the trainer's LED (like the console's power button: a
	 * sequence turns through four quarters of a second), and what is happening until the first frame. */
	void drawGame()
	{
		const int state = pshx_core_state();
		// the frame rate: the game's frames that reached the screen, each half second
		const double now = now_seconds();
		if (fpsSince < 0.0) {
			fpsSince = now;
			fpsPictures = picturesTaken;
		} else if (now - fpsSince >= 0.5) {
			fps = static_cast<float>(static_cast<double>(picturesTaken - fpsPictures) / (now - fpsSince));
			fpsSince = now;
			fpsPictures = picturesTaken;
		}
		pollTrainer();
		if (qm.open || qm.sub != Quick::Sub::None) {
			return;   // the quick menu and its screens cover the game
		}
		ui::HudView v;
		v.showFps = showFps && havePicture;
		v.fpsCorner = fpsCorner == "tl" ? 0 : fpsCorner == "bl" ? 2 : fpsCorner == "br" ? 3 : 1;
		if (v.fpsCorner == 0 && (!problem.empty() || !havePicture)) {
			v.fpsCorner = 1;   // (the status panel's corner)
		}
		v.fps = fps;
		v.shownFps = frameGen.ready() && fgShownFps > 0.5f ? fgShownFps : 0.0f;
		if (now <= ledUntil) {
			const int phase = static_cast<int>(now * 4.0) & 3;
			const bool g = (ledSeq >> phase) & 1, r = (ledSeq >> (4 + phase)) & 1;
			v.showLed = true;
			v.ledAlpha = static_cast<float>(std::min(1.0, (ledUntil - now) / 0.5));
			v.ledColour = r && g ? IM_COL32(255, 158, 26, 255) : r ? IM_COL32(255, 46, 31, 255) : g ? IM_COL32(77, 255, 64, 255) : 0;
			v.ledText = ledText;
		}
		if (shareWait.active) {
			/* 0.0.6: waiting for the share, the seconds counted */
			v.showStatus = true;
			v.statusTitle = "Waiting for the network share\xE2\x80\xA6 " + std::to_string(static_cast<int>(now - shareWait.since)) + " s";
			v.statusLines.push_back({ "Disc", baseName(boot.image) });
			v.statusLines.push_back({ "Share", parentOf(boot.image) });
			v.note = "The game starts as soon as the share answers.";
			v.waiting = true;
			v.statusHints = { { "O", "Back to the game list" } };
			ImDrawList *dl = beginUiScreen("##hud", Ground::None);
			ui::drawHud(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)), v);
			endUiScreen();
			return;
		}
		/* 0.0.6: a read of the share that hasn't finished for 2 s (the share stopped answering): said,
		 * with the seconds, until it answers (the game waits for it meanwhile) */
		const double stalled = havePicture ? ps5vfs_stalled_seconds() : 0.0;
		if (stalled >= 2.0 && problem.empty() && state == PSHX_CORE_RUNNING) {
			v.showStatus = true;
			v.statusTitle = "Waiting for the network share\xE2\x80\xA6 " + std::to_string(static_cast<int>(stalled)) + " s";
			v.statusLines.push_back({ "Disc", baseName(boot.image) });
			v.note = "The game goes on when the share answers.";
			v.waiting = true;
			ImDrawList *dl = beginUiScreen("##hud", Ground::None);
			ui::drawHud(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)), v);
			endUiScreen();
			return;
		}
		const bool trouble = !problem.empty() || state == PSHX_CORE_FAILED || state == PSHX_CORE_STOPPED;
		/* 0.0.2: no panel over a game's frozen last frame (its next XBE is starting), only trouble */
		if (trouble || (!havePicture && !showingFrozen)) {
			v.showStatus = true;
			v.statusTitle = std::string("OGBox ") + pshakux_build_label();
			v.problem = problem;
			if (problem.empty()) {
				v.statusLines.push_back({ "Disc", boot.image == "none" ? std::string("none: the Xbox dashboard") : baseName(boot.image) });
				v.statusLines.push_back({ "Core", stateName(state) });
				if (state == PSHX_CORE_FAILED) {
					v.badLine = 1;
				}
				v.note = pshx_core_message();
				v.waiting = state == PSHX_CORE_BOOTING || (state == PSHX_CORE_RUNNING && !havePicture);
			}
			{
				const std::string m = prefs.global("quick_menu");
				v.statusHints = { { m == "combo" ? "L1+L2+R1+R2+OPTIONS" : m == "touch_hold" ? "TP" : m == "l3r3" ? "L3+R3" : "OPT",
					"Quick menu: save, load, quit" } };
			}
		}
		if (!v.showFps && !v.showLed && !v.showStatus) {
			return;
		}
		ImDrawList *dl = beginUiScreen("##hud", Ground::None);
		ui::drawHud(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)), v);
		endUiScreen();
	}

	// ---- 0.1.48: the data folder ------------------------------------------------------------

	void openStorage(bool first)
	{
		storageFirst = first;
		storageStep = StorageStep::Places;
		storageList = ogbox::storagePlaces();
		storageMessage.clear();
		screen = Screen::Storage;
		screenSince = now_seconds();
		storageSel = -1;
		pshakux_log("storage: %s (now %s)", first ? "the first start asks where the data goes" : "choosing the data folder again",
			ogbox::storageRoot().c_str());
	}

	void storageBrowse(const std::string &path)
	{
		storageFolder = path;
		storageSubs.clear();
		if (!path.empty()) {
			if (DIR *dir = opendir(path.c_str())) {
				while (struct dirent *e = readdir(dir)) {
					const std::string n = e->d_name;
					if (n.empty() || n[0] == '.' || n == "$RECYCLE.BIN" || n == "System Volume Information") {
						continue;
					}
					if (isFolder(path + "/" + n)) {
						storageSubs.push_back(n);
					}
				}
				closedir(dir);
			}
			std::sort(storageSubs.begin(), storageSubs.end(),
				[](const std::string &a, const std::string &b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
		}
		storageStep = StorageStep::Browse;
		storageSel = -1;
	}

	/* A folder chosen: the same one, an empty one, or one to move the data to. */
	void storageChoose(const std::string &target)
	{
		const std::string now = ogbox::storageRoot();
		std::string why;
		if (!ogbox::writableFolder(target, why)) {
			storageMessage = target + ": " + why;
			return;
		}
		storageTarget = target;
		storageHave = target == now ? 0 : ogbox::storageBytes(now);
		if (target != now && storageHave > 0) {
			storageStep = StorageStep::Confirm;
			storageSel = -1;
			storageMessage.clear();
			return;
		}
		if (!ogbox::setStorageRoot(target, why)) {
			storageMessage = why;
			return;
		}
		storageDone("OGBox keeps its data in " + target + ".");
	}

	void storageDone(const std::string &message)
	{
		storageMessage = message;
		storageStep = StorageStep::Done;
		storageSel = -1;
	}

	void storageLeave()
	{
		if (storageFirst) {
			screen = Screen::Library;
		} else {
			screen = Screen::Settings;
		}
		screenSince = now_seconds();
	}

	/* CIRCLE on the data folder's screen. */
	void storageBack()
	{
		switch (storageStep) {
		case StorageStep::Browse:
			if (storageFolder.empty()) {
				storageStep = StorageStep::Places;
			} else {
				const bool root = storageFolder == "/data" ||
					(storageFolder.rfind("/mnt/", 0) == 0 && std::count(storageFolder.begin(), storageFolder.end(), '/') <= 2);
				storageBrowse(root ? std::string() : parentOf(storageFolder));
			}
			storageSel = -1;
			break;
		case StorageStep::Confirm:
			storageStep = StorageStep::Places;
			storageSel = -1;
			break;
		case StorageStep::Moving:
			break;
		case StorageStep::Done:
		case StorageStep::Places:
			if (!storageFirst || ogbox::storageChosen()) {
				storageLeave();
			}
			break;
		}
	}

	/* A move under way: its end. */
	void storageTick()
	{
		if (storageStep != StorageStep::Moving) {
			return;
		}
		const ogbox::StorageMove::Progress pr = ogbox::StorageMove::get().progress();
		if (!pr.done) {
			return;
		}
		if (pr.ok) {
			storageDone("Done: OGBox keeps its data in " + storageTarget + " now.");
		} else {
			storageMessage = "The move stopped: " + pr.error + ". Your data is still in " + ogbox::storageRoot() + ".";
			storageStep = StorageStep::Places;
			storageSel = -1;
		}
	}

	/* The data folder's screen for its step: its words, its rows and what each does. */
	void storageModel(ui::ListScreenView &v, std::vector<std::function<void()>> &acts)
	{
		using R = ui::ListRow;
		const std::string now = ogbox::storageRoot();
		const auto add = [&](R r, std::function<void()> f) {
			r.key = std::to_string(v.rows.size()) + r.key;
			v.rows.push_back(std::move(r));
			acts.push_back(std::move(f));
		};
		v.head.logo = uiLogo();
		v.head.title = storageFirst ? "Welcome to OGBox" : "Data folder";
		v.head.subtitle = "where OGBox keeps the Xbox hard disk, caches, covers";
		switch (storageStep) {
		case StorageStep::Places:
			if (storageFirst) {
				v.intro = "Where should OGBox keep its data? That is the Xbox's hard disk (your saves), the shader caches, the covers and "
					"your screenshots: a few GB as you play. OGBox's settings stay on the console either way, and you can change this "
					"later in Settings.";
			} else if (isFolder(now)) {
				v.intro = "Now: " + now + " (" + ogbox::freeSpaceText(now) + "). Choose another place and OGBox moves your Xbox hard "
					"disk, caches, covers and screenshots there.";
			} else {   // 0.0.3: its drive unplugged
				v.intro = "Now: " + now + ", which isn't there: is its drive plugged in? Your Xbox hard disk is on it.";
				v.introWarn = true;
			}
			for (const ogbox::StoragePlace &p : storageList) {
				R r;
				r.key = p.path;
				r.title = p.name;
				r.sub = p.path + (p.note.empty() ? std::string() : " \xC2\xB7 " + p.note);
				r.icon = p.path.rfind("/mnt/", 0) == 0 ? ui::Icon::Usb : ui::Icon::Console;
				r.current = p.path == now;
				r.accent = p.recommended;
				r.right = p.recommended ? "recommended" : "";
				const std::string path = p.path;
				add(r, [this, path]() { storageChoose(path); });
			}
			{
				R r;
				r.key = "browse";
				r.title = "Another folder\xE2\x80\xA6";
				r.sub = "internal storage, USB, extended storage";
				r.icon = ui::Icon::Folder;
				add(r, [this]() { storageBrowse(std::string()); });
			}
			break;
		case StorageStep::Browse:
			v.title1 = storageFolder.empty() ? std::string("Drives") : storageFolder;
			if (storageFolder.empty()) {
				std::vector<std::pair<std::string, std::string>> roots = { { "This console", "/data" } };
				for (int i = 0; i < 8; i++) {
					const std::string u = "/mnt/usb" + std::to_string(i);
					if (isMountPoint(u)) {
						roots.push_back({ "USB drive " + std::to_string(i), u });
					}
				}
				for (int i = 0; i < 2; i++) {
					const std::string e = "/mnt/ext" + std::to_string(i);
					if (isMountPoint(e)) {
						roots.push_back({ "Extended storage " + std::to_string(i), e });
					}
				}
				for (const auto &root : roots) {
					R r;
					r.key = root.second;
					r.title = root.first;
					r.sub = root.second + " \xC2\xB7 " + ogbox::freeSpaceText(root.second);
					r.icon = root.second == "/data" ? ui::Icon::Console : ui::Icon::Usb;
					const std::string path = root.second;
					add(r, [this, path]() { storageBrowse(path); });
				}
			} else {
				R r;
				r.key = "use";
				r.title = "Use this folder";
				r.right = ogbox::freeSpaceText(storageFolder);
				r.icon = ui::Icon::Check;
				r.accent = true;
				add(r, [this]() { storageChoose(storageFolder); });
				r = {};
				r.key = "make";
				r.title = "Make a folder named OGBox here and use it";
				r.icon = ui::Icon::Plus;
				add(r, [this]() { storageChoose(storageFolder + "/OGBox"); });
				r = {};
				r.key = "up";
				r.title = "Back";
				r.icon = ui::Icon::Up;
				add(r, [this]() { storageBack(); });
				for (const std::string &sub : storageSubs) {
					r = {};
					r.key = "f" + sub;
					r.title = sub;
					r.icon = ui::Icon::Folder;
					const std::string path = storageFolder + "/" + sub;
					add(r, [this, path]() { storageBrowse(path); });
				}
			}
			break;
		case StorageStep::Confirm: {
			v.intro = "OGBox has " + sizeText(static_cast<int64_t>(storageHave)) + " of data in " + now + ". The new folder: " +
				storageTarget + " (" + ogbox::freeSpaceText(storageTarget) + ").";
			R r;
			r.key = "move";
			r.title = "Move my data there";
			r.right = sizeText(static_cast<int64_t>(storageHave));
			r.icon = ui::Icon::Check;
			r.accent = true;
			add(r, [this, now]() {
				ogbox::StorageMove::get().start(now, storageTarget);
				storageStep = StorageStep::Moving;
			});
			r = {};
			r.key = "empty";
			r.title = "Start there empty";
			r.sub = "the data stays in " + now;
			r.icon = ui::Icon::Plus;
			add(r, [this, now]() {
				std::string why;
				if (ogbox::setStorageRoot(storageTarget, why)) {
					storageDone("OGBox keeps its data in " + storageTarget + " from now on. What was in " + now + " is still there.");
				} else {
					storageMessage = why;
				}
			});
			r = {};
			r.key = "cancel";
			r.title = "Cancel";
			r.icon = ui::Icon::Back;
			add(r, [this]() {
				storageStep = StorageStep::Places;
				storageSel = -1;
			});
			break;
		}
		case StorageStep::Moving: {
			const ogbox::StorageMove::Progress pr = ogbox::StorageMove::get().progress();
			v.intro = "Moving your data to " + storageTarget + ". Leave OGBox open until it's done.";
			v.progress = pr.total ? std::min(1.0f, static_cast<float>(static_cast<double>(pr.bytes) / static_cast<double>(pr.total))) : 0.0f;
			v.progressText = sizeText(static_cast<int64_t>(pr.bytes)) + " of " + sizeText(static_cast<int64_t>(pr.total)) + " \xC2\xB7 " +
				pr.current;
			break;
		}
		case StorageStep::Done: {
			v.intro = storageMessage;
			R r;
			r.key = "ok";
			r.title = storageFirst ? "Go to the game list" : "Back to the settings";
			r.icon = ui::Icon::Check;
			r.accent = true;
			add(r, [this]() { storageLeave(); });
			break;
		}
		}
		if (!storageMessage.empty() && storageStep != StorageStep::Done) {
			v.message = storageMessage;
			v.messageBad = true;
		}
		v.aside.title = "What goes there";
		v.aside.facts = { { "hdd", "The Xbox's hard disk: saves, the dashboard if you add one" },
			{ "cache", "Shaders built for your games, patched XBEs" }, { "covers, shots", "The game list's covers, your screenshots" } };
		v.aside.note = "A USB drive must stay plugged in while you play. exFAT is best; FAT32 can't hold files over 4 GB.";
		if (storageStep == StorageStep::Moving) {
			v.footerRight = "Moving\xE2\x80\xA6";
		} else if (storageFirst && storageStep == StorageStep::Places) {
			v.footer = { { "UD", "Choose" }, { "X", "Choose this" } };
		} else {
			v.footer = { { "UD", "Choose" }, { "X", "Choose this" }, { "O", "Back" } };
		}
	}

	void storageInput(uint32_t p)
	{
		storageTick();
		ui::ListScreenView v;
		std::vector<std::function<void()>> acts;
		storageModel(v, acts);
		if (storageSel < 0 || storageSel >= static_cast<int>(v.rows.size())) {
			storageSel = ui::listStep(v.rows, -1, 1);
		}
		if (p & PAD_UP) {
			storageSel = ui::listStep(v.rows, storageSel, -1);
		}
		if (p & PAD_DOWN) {
			storageSel = ui::listStep(v.rows, storageSel, 1);
		}
		if ((p & PAD_CROSS) && storageSel >= 0 && acts[static_cast<size_t>(storageSel)]) {
			acts[static_cast<size_t>(storageSel)]();
		} else if (p & PAD_CIRCLE) {
			storageBack();
		}
	}

	void drawStorage()
	{
		storageTick();
		ui::ListScreenView v;
		std::vector<std::function<void()>> acts;
		storageModel(v, acts);
		if (storageSel < 0 || storageSel >= static_cast<int>(v.rows.size())) {
			storageSel = ui::listStep(v.rows, -1, 1);
		}
		v.sel = storageSel;
		bd.key = "storage";
		bd.blur = ui::Tex{};
		bd.tint = 0;
		bd.focus = ImVec2(0.2f, 0.3f);
		bd.strength = 0.6f;
		ImDrawList *dl = beginUiScreen("##storage");
		ui::drawListScreen(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)), v);
		endUiScreen();
	}

	void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		(void)overlay;
		uiInit();
		ui::beginFrame(now_seconds());
		if (themeApplied != (themeCode ? 1 : 0)) {
			applyTheme(themeCode);
		}
		if (!library) {
			drawGame();
			if (qm.sub == Quick::Sub::Settings) {
				drawSettingsScreen();
			} else if (qm.sub == Quick::Sub::Mods) {
				drawMods();
			} else if (qm.open) {
				drawQuickMenu();
			}
		} else {
			switch (screen) {
			case Screen::Notice: drawNotice(); break;
			case Screen::Boot: drawBoot(); break;
			case Screen::Library:
				if (gp.open) {
					drawGamePageNew();
				} else {
					drawHomeScreen();
				}
				break;
			case Screen::Settings: drawSettingsScreen(); break;
			case Screen::Mods: drawMods(); break;
			case Screen::Storage: drawStorage(); break;
			case Screen::Hdd: drawHdd(); break;
			}
			updateTick();
		}
		drawOverlays();
	}

	/* ---- 0.1.53: the in-game quick menu ---------------------------------------------------------
	 * OPTIONS (or OPTIONS held, Settings > Interface) opens it over the paused game: Resume, save and
	 * load a state in ten slots (each with the picture of its moment), the game where it was left,
	 * the game's own settings and its mods, patches and cheats (the screens of the game list), restart
	 * and quit. PSFlyCast's and Crux2's: a panel from the right, the game dimmed behind. */

	/* 0.0.6: the rows shown, in order; the state slot (LEFT and RIGHT on Save State and Load State choose it)
	 * and "Where you left off" (the game's start offers it) aren't rows any more */
	enum QuickRow { QR_RESUME, QR_SAVE, QR_LOAD, QR_SETTINGS, QR_MODS, QR_DRAWLOG, QR_RESTART, QR_QUIT, QR_COUNT, QR_SLOT, QR_LEFT };
	/* 0.0.2: the quick menu's "Log what the GPU draws": this many draws (a few frames of a game),
	 * from this long after it closes (time to bring the problem on screen) */
	static constexpr int kDrawLogDraws = 5000;
	static constexpr unsigned kDrawLogDelayMs = 5000;

	void quickOpen()
	{
		if (qm.open || relaunchAsked) {
			return;   // (a game that failed has the menu too: its way back to the list)
		}
		qm.open = true;
		qm.sel = QR_RESUME;
		qm.openedAt = now_seconds();
		qm.optionsDown = false;
		qm.restartArmed = 0.0;
		pshx_core_pause(true);
		/* the frozen picture, 320 wide, for a state saved now */
		qm.thumb.clear();
		qm.thumbW = qm.thumbH = 0;
		if (pictureBuffer >= 0 && picture.width && picture.height && staging[static_cast<size_t>(pictureBuffer)].mapped) {
			const uint8_t *src = static_cast<const uint8_t *>(staging[static_cast<size_t>(pictureBuffer)].mapped);
			const uint32_t tw = 320, th = std::max(1u, static_cast<uint32_t>(320.0f / displayAspect() + 0.5f));
			qm.thumb.resize(static_cast<size_t>(tw) * th * 4);
			for (uint32_t y = 0; y < th; y++) {
				const uint32_t sy = std::min(picture.height - 1, y * picture.height / th);
				for (uint32_t x = 0; x < tw; x++) {
					const uint32_t sx = std::min(picture.width - 1, x * picture.width / tw);
					std::memcpy(&qm.thumb[(static_cast<size_t>(y) * tw + x) * 4], src + (static_cast<size_t>(sy) * picture.width + sx) * 4, 4);
				}
			}
			qm.thumbW = tw;
			qm.thumbH = th;
		}
		pshakux_log("quick menu: open (the game paused)");
	}

	void quickClose()
	{
		if (!qm.open) {
			return;
		}
		qm.open = false;
		qm.sub = Quick::Sub::None;
		pshx_core_pause(false);
		pshakux_log("quick menu: closed (the game goes on)");
	}

	/* A state saved on a thread of its own (a second or so), then the menu closes or the title goes
	 * back to the game list. */
	void quickSave(int slot, Quick::After after)
	{
		if (!qm.busy.empty() || runKey.empty()) {
			return;
		}
		const ogbox::StateSlot st = ogbox::stateSlot(runKey, slot);
		mkdir(ogbox::storagePath("states").c_str(), 0777);
		mkdir(ogbox::stateFolder(runKey).c_str(), 0777);
		qm.busy = slot == ogbox::kQuitSlot ? "Saving where you are" : "Saving to slot " + std::to_string(slot);
		qm.busySlot = slot;
		qm.after = after;
		qm.result = std::make_shared<std::atomic<int>>(0);
		qm.why = std::make_shared<std::string>();
		auto result = qm.result;
		auto why = qm.why;
		const std::string tmp = st.path + ".part";
		const std::string path = st.path, thumbPath = st.thumb;
		const std::vector<uint8_t> thumb = qm.thumb;
		const uint32_t tw = qm.thumbW, th = qm.thumbH;
		std::thread([result, why, tmp, path, thumbPath, thumb, tw, th]() {
			char err[256] = "";
			const bool ok = pshx_core_save_state(tmp.c_str(), err, sizeof(err));
			if (ok) {
				std::rename(tmp.c_str(), path.c_str());
				if (!thumb.empty()) {
					writeBmp(thumbPath, thumb.data(), tw, th, tw * 4);
				}
			} else {
				std::remove(tmp.c_str());
				*why = err;
			}
			result->store(ok ? 1 : 2);
		}).detach();
	}

	/* The title started again with the state (hakuX loads a state as the game boots). */
	void quickLoad(const std::string &statePath)
	{
		relaunchAsked = true;
		std::vector<std::string> words = { "--boot", boot.image, "--hdd", boot.hdd, "--loadstate", statePath };
		if (!runKey.empty()) {
			words.push_back("--game");
			words.push_back(runKey);
		}
		std::vector<const char *> args;
		for (const std::string &w : words) {
			args.push_back(w.c_str());
		}
		args.push_back(nullptr);
		pshakux_log("quick menu: loading the state %s", statePath.c_str());
		quiesceForExit();
		const int r = pshakux_relaunch(args.data());
		pshakux_log("quick menu: the relaunch was refused (%#x)", static_cast<unsigned>(r));
		relaunchAsked = false;
		showToast("The console refused to start the game again.");
	}

	void quickRestart()
	{
		relaunchAsked = true;
		std::vector<std::string> words = { "--boot", boot.image, "--hdd", boot.hdd };
		if (!runKey.empty()) {
			words.push_back("--game");
			words.push_back(runKey);
		}
		std::vector<const char *> args;
		for (const std::string &w : words) {
			args.push_back(w.c_str());
		}
		args.push_back(nullptr);
		pshakux_log("quick menu: restarting the game");
		quiesceForExit();
		pshakux_relaunch(args.data());
		relaunchAsked = false;
	}

	/* ---- 0.0.1: the updater (Crux2's, ps5_update.h) --------------------------------------------
	 * Asked at each start or from Settings > Updates; what it finds is answered here, a frame at a
	 * time, on the game list or in the settings (never over the notice, the splash or a game): a newer release
	 * is offered with its notes, its download shown, and once it is checked and unpacked it is put
	 * in place and OGBox closes. */

	void checkForUpdate()
	{
		OGBoxUpdate::Start();
		updateByHand = true;
		updateSeen = PS5Update::State::Idle;   // the same answer as before is answered again
		PS5Update::Check();
		showToast("Looking for a newer OGBox...");
	}

	static std::string updateLine()
	{
		const PS5Update::Status st = PS5Update::GetStatus();
		switch (st.state) {
		case PS5Update::State::Checking: return "asking GitHub";
		case PS5Update::State::UpToDate: return "this is the newest (" + std::string(pshakux_build_label()) + ")";
		case PS5Update::State::Available: return st.tag + " is out";
		case PS5Update::State::NoRelease: return "no release found";
		case PS5Update::State::Downloading:
			return st.total ? "downloading " + std::to_string(static_cast<int>(100.0 * static_cast<double>(st.done) /
				static_cast<double>(st.total))) + "%" : std::string("downloading");
		case PS5Update::State::Verifying: return "checking the download";
		case PS5Update::State::Ready: return "ready to install";
		case PS5Update::State::Installed: return "installed";
		case PS5Update::State::Failed: return "did not work";
		default: return std::string("OGBox ") + pshakux_build_label();
		}
	}

	/* The release notes for the offer: from "Added" on when they have it (the notice and the
	 * warnings at their top are on GitHub), at most about 700 characters. */
	static std::string updateNotes(const std::string &notes)
	{
		std::string text = notes;
		const size_t added = text.find("Added");
		if (added != std::string::npos && added < 4000) {
			text = text.substr(added);
		}
		if (text.size() > 700) {
			size_t cut = text.rfind('\n', 700);
			text = text.substr(0, cut == std::string::npos || cut < 300 ? 700 : cut) + "\n...";
		}
		return text;
	}

	void updateTick()
	{
		const PS5Update::Status st = PS5Update::GetStatus();
		if (st.state == PS5Update::State::Downloading || st.state == PS5Update::State::Verifying) {
			if (updateWanted) {
				toast = "Updating to OGBox " + st.tag + ": " + updateLine();   // kept up while it lasts
				toastUntil = now_seconds() + 1.0;
			}
		}
		if (st.state == updateSeen) {
			return;
		}
		/* answered on the game list or in the settings, with nothing else open */
		if ((screen != Screen::Library && screen != Screen::Settings) || pop.open || gp.open || !startPath.empty()) {
			return;
		}
		updateSeen = st.state;
		switch (st.state) {
		case PS5Update::State::Available:
			pshakux_log("update: %s is offered", st.tag.c_str());
			openPopup("OGBox " + st.tag + " is out", updateNotes(st.notes) +
				"\n\nDownload and install it? Your settings, the Xbox hard disk, states and games stay.",
				{ "Install", "Not now" }, [this](int choice) {
					if (choice == 0) {
						updateWanted = true;
						pshakux_log("update: the install was chosen");
						PS5Update::Download();
					}
				});
			break;
		case PS5Update::State::UpToDate:
			if (updateByHand) {
				showToast("This is the newest OGBox (" + std::string(pshakux_build_label()) + ").");
			}
			updateByHand = false;
			break;
		case PS5Update::State::NoRelease:
		case PS5Update::State::Failed:
			if (updateWanted) {
				openPopup("The update did not work", st.error + "\nOGBox is as it was.", { "OK" }, nullptr);
			} else if (updateByHand) {
				showToast(st.error.empty() ? std::string("No release of OGBox was found.") : st.error);
			}
			updateWanted = updateByHand = false;
			break;
		case PS5Update::State::Ready:
			if (updateWanted) {
				if (PS5Update::Install()) {
					pshakux_log("update: installed; OGBox closes");
					openPopup("OGBox was updated", "The update is installed. OGBox will now close: start it again "
						"from the home screen.", { "Close OGBox" }, [this](int) { quit = true; });
				} else {
					openPopup("The update was not installed", PS5Update::GetStatus().error + "\nOGBox is as it was.",
						{ "OK" }, nullptr);
				}
				updateWanted = false;
			}
			break;
		default:
			break;
		}
	}

	void showToast(const std::string &text)
	{
		toast = text;
		toastUntil = now_seconds() + 2.5;
	}

	/* Each frame in a game: the quick menu's button, and the menu's own when it is open. */
	void gameMenuControls()
	{
		const uint32_t pressed = menuPad.pressed, held = menuPad.held;
		const double now = now_seconds();
		const bool holdMode = prefs.global("quick_menu") == "options_hold";
		/* a save under way: its end */
		if (!qm.busy.empty() && qm.result && qm.result->load() != 0) {
			const bool ok = qm.result->load() == 1;
			const int slot = qm.busySlot;
			qm.busy.clear();
			if (qm.after == Quick::After::Library) {
				if (!ok) {
					pshakux_log("quick menu: the state wasn't saved (%s): back to the game list anyway", qm.why->c_str());
				}
				relaunchToLibrary();
				return;
			}
			showToast(ok ? (slot == ogbox::kQuitSlot ? std::string("Saved where you are") : "Saved to slot " + std::to_string(slot))
				: "Not saved: " + *qm.why);
			if (ok) {
				quickClose();
			}
		}
		if (!qm.pendingLoad.empty()) {
			const std::string path = qm.pendingLoad;
			qm.pendingLoad.clear();
			quickLoad(path);
			return;
		}
		if (qm.pendingRestart) {
			qm.pendingRestart = false;
			quickRestart();
			return;
		}
		/* the button that opens it */
		if (!qm.open) {
			const std::string mode = prefs.global("quick_menu");
			if (mode == "combo" || mode == "touch_hold" || mode == "l3r3") {
				/* 0.0.6: a combination (OPTIONS is the Xbox's Start then): L1+L2+R1+R2+OPTIONS at once;
				 * the touch pad held half a second (a tap is Back); L3+R3 let go within a second (held
				 * longer, the screenshot) */
				const uint32_t shoulders = PAD_L1 | PAD_R1 | ogbox::src::l2 | ogbox::src::r2;
				uint32_t h = held;
				if (menuPad.l2 > 0.5f) {
					h |= ogbox::src::l2;
				}
				if (menuPad.r2 > 0.5f) {
					h |= ogbox::src::r2;
				}
				if (mode == "combo") {
					if ((h & shoulders) == shoulders && (pressed & PAD_OPTIONS)) {
						quickOpen();
					}
				} else if (mode == "touch_hold") {
					if (held & PAD_TOUCH_PAD) {
						if (qm.optionsSince < 0.0) {
							qm.optionsSince = now;
						} else if (qm.optionsSince > 0.0 && now - qm.optionsSince >= 0.5) {
							qm.optionsSince = 0.0;
							quickOpen();
						}
					} else {
						qm.optionsSince = -1.0;
					}
				} else {
					const uint32_t sticks = PAD_L3 | PAD_R3;
					if ((held & sticks) == sticks) {
						if (qm.optionsSince < 0.0) {
							qm.optionsSince = now;
						}
					} else {
						if (qm.optionsSince > 0.0 && now - qm.optionsSince < 1.0) {
							quickOpen();
						}
						qm.optionsSince = -1.0;
					}
				}
				return;
			}
			if (holdMode) {
				if (held & PAD_OPTIONS) {
					if (qm.optionsSince < 0.0) {
						qm.optionsSince = now;
					} else if (qm.optionsSince > 0.0 && now - qm.optionsSince >= 0.5) {
						qm.optionsSince = 0.0;   // (used: not a Start on release)
						quickOpen();
					}
				} else {
					if (qm.optionsSince > 0.0) {
						qm.startPulseUntil = now + 0.12;   // a short press: the Xbox's Start
					}
					qm.optionsSince = -1.0;
				}
			} else {
				/* OPTIONS acts when let go, so the press doesn't reach the menu */
				if (pressed & PAD_OPTIONS) {
					qm.optionsSince = now;
				}
				if (!(held & PAD_OPTIONS) && qm.optionsSince > 0.0) {
					qm.optionsSince = -1.0;
					quickOpen();
				}
			}
			return;
		}
		if (!qm.busy.empty()) {
			return;   // saving: no input
		}
		if (kb.isOpen()) {
			keyboardInput(navPressed());
			return;
		}
		if (pop.open) {
			popupInput(navPressed());
			return;
		}
		if (qm.sub == Quick::Sub::Settings) {
			settingsInput(navPressed());   // 0.0.3: CIRCLE from the sections back to the quick menu
			return;
		}
		if (qm.sub == Quick::Sub::Mods) {
			modsInput(navPressed());   // CIRCLE back to the quick menu
			return;
		}
		/* OPTIONS closes it when pressed in it; CIRCLE too */
		if (pressed & PAD_OPTIONS) {
			qm.optionsDown = true;
		}
		if ((qm.optionsDown && !(held & PAD_OPTIONS) && !(pressed & PAD_OPTIONS)) || (pressed & PAD_CIRCLE)) {
			qm.optionsDown = false;
			quickClose();
			return;
		}
		if (pressed & PAD_UP) {
			qm.sel = (qm.sel + QR_COUNT - 1) % QR_COUNT;
		}
		if (pressed & PAD_DOWN) {
			qm.sel = (qm.sel + 1) % QR_COUNT;
		}
		if (qm.sel == QR_SLOT || qm.sel == QR_SAVE || qm.sel == QR_LOAD) {
			if (pressed & PAD_LEFT) {
				qm.slot = qm.slot <= 1 ? ogbox::kStateSlots : qm.slot - 1;
			}
			if (pressed & PAD_RIGHT) {
				qm.slot = qm.slot >= ogbox::kStateSlots ? 1 : qm.slot + 1;
			}
		}
		if (!(pressed & PAD_CROSS)) {
			return;
		}
		switch (qm.sel) {
		case QR_RESUME:
			quickClose();
			break;
		case QR_SAVE:
			quickSave(qm.slot, Quick::After::Resume);
			break;
		case QR_LOAD: {
			const ogbox::StateSlot st = ogbox::stateSlot(runKey, qm.slot);
			if (st.used) {
				qm.pendingLoad = st.path;
				qm.busy = "Loading slot " + std::to_string(qm.slot);
			}
			break;
		}
		case QR_SLOT:
			qm.slot = qm.slot >= ogbox::kStateSlots ? 1 : qm.slot + 1;
			break;
		case QR_LEFT: {
			const ogbox::StateSlot st = ogbox::stateSlot(runKey, ogbox::kQuitSlot);
			if (st.used) {
				qm.pendingLoad = st.path;
				qm.busy = "Loading where you left off";
			}
			break;
		}
		case QR_SETTINGS:
			if (!runKey.empty()) {
				openSettings(runKey, boot.image == "none" ? std::string("Xbox dashboard") : ogbox::shortName(baseName(boot.image)));
				qm.sub = Quick::Sub::Settings;
			}
			break;
		case QR_MODS:
			if (!runKey.empty()) {
				openMods(runKey, ogbox::shortName(baseName(boot.image)));   // (not the disc: the game reads it)
				qm.sub = Quick::Sub::Mods;
			}
			break;
		case QR_DRAWLOG:
			/* 0.0.2: for a problem report: what the GPU draws a few seconds after the game goes on
			 * (time to bring the problem on screen) goes into the boot log */
			pshx_core_drawlog(kDrawLogDraws, kDrawLogDelayMs);
			pshakux_log("quick menu: %d of the GPU's draws go into this log, from %u s after the game goes on "
			            "(a problem report)", kDrawLogDraws, kDrawLogDelayMs / 1000);
			quickClose();
			break;
		case QR_RESTART:
			if (now < qm.restartArmed) {
				qm.pendingRestart = true;
				qm.busy = "Restarting";
			} else {
				qm.restartArmed = now + 4.0;
			}
			break;
		case QR_QUIT:
			openPopup("Quit the game", "Save where you are, to go on from there next time?",
				{ "Quit, and save where I am", "Quit without saving", "Go back" }, [this](int i) {
					if (i == 0) {
						quickSave(ogbox::kQuitSlot, Quick::After::Library);
					} else if (i == 1) {
						qm.busy = "Back to the game list";
						relaunchToLibrary();
					}
				});
			break;
		}
	}

	/* The menu: a panel from the right over the dimmed game, the state slot's card beside it. */
	void drawQuickMenu()
	{
		ui::QuickView v;
		v.title = boot.image == "none" ? std::string("Xbox dashboard") : ogbox::shortName(baseName(boot.image));
		v.sub = runKey;
		{
			LibraryEntry fake{ LibraryEntry::Image, baseName(boot.image), boot.image, 0, std::string() };
			v.cover = uiCover(coverNames(fake));
		}
		const ogbox::StateSlot slot = ogbox::stateSlot(runKey, qm.slot);
		const ogbox::StateSlot left = ogbox::stateSlot(runKey, ogbox::kQuitSlot);
		const bool restartArmed = now_seconds() < qm.restartArmed;
		const std::string slotText = "\xE2\x86\x90 slot " + std::to_string(qm.slot) + " \xE2\x86\x92";
		struct RowText {
			const char *key;
			std::string text, note;
			ui::Icon icon;
			bool dim;
		};
		const RowText rows[QR_COUNT] = {
			{ "resume", "Resume", "", ui::Icon::Play, false },
			{ "save", "Save State", slotText, ui::Icon::Save, runKey.empty() },
			{ "load", "Load State", slot.used ? slotText : "\xE2\x86\x90 slot " + std::to_string(qm.slot) + " is empty \xE2\x86\x92",
				ui::Icon::Load, !slot.used },
			{ "settings", "Game Settings", "next start", ui::Icon::Gear, runKey.empty() },
			{ "mods", "Patches", "next start", ui::Icon::Puzzle, runKey.empty() },
			{ "drawlog", "Log What the GPU Draws", "debugging only", ui::Icon::Info, false },
			{ "restart", restartArmed ? "Restart: press again" : "Restart the Game", "", ui::Icon::Restart, false },
			{ "quit", "Quit the Game", "", ui::Icon::Power, false },
		};
		for (int i = 0; i < QR_COUNT; i++) {
			ui::ListRow r;
			r.key = rows[i].key;
			r.title = rows[i].text;
			r.right = rows[i].note;
			r.icon = rows[i].icon;
			r.dim = rows[i].dim;
			r.danger = i == QR_QUIT || (i == QR_RESTART && restartArmed);
			v.rows.push_back(r);
		}
		v.sel = qm.sel;
		// the slot's card, beside the panel, for the state rows
		if (qm.sel == QR_SAVE || qm.sel == QR_LOAD || qm.sel == QR_SLOT || qm.sel == QR_LEFT) {
			const ogbox::StateSlot &st = qm.sel == QR_LEFT ? left : slot;
			v.showSlot = true;
			if (st.used) {
				float tw = 0.0f, th = 0.0f;
				const ImTextureID img = fileImage(st.thumb, st.thumb + "#" + std::to_string(st.mtime), &tw, &th);
				if (img && tw > 0.0f && th > 0.0f) {
					v.slot.picture = ui::Tex{ img, tw, th };
				}
			}
			v.slot.pictureEmpty = st.used ? "(no picture)" : "Empty slot";
			v.slot.title = qm.sel == QR_LEFT ? std::string("Where you left off") : "Slot " + std::to_string(st.slot);
			v.slot.text = st.used ? "Saved " + ogbox::dateText(st.mtime) : std::string(qm.sel == QR_SAVE ? "Saving here keeps where you are now." : "");
		}
		v.busy = qm.busy;
		v.footer = { { "X", "Choose" } };
		if (qm.sel == QR_SAVE || qm.sel == QR_LOAD || qm.sel == QR_SLOT) {
			v.footer.push_back({ "LR", "Slot" });
		}
		v.footer.push_back({ "O", "Resume" });
		const float appear = static_cast<float>(std::min(1.0, (now_seconds() - qm.openedAt) / 0.18));
		ImDrawList *dl = beginUiScreen("##quick", Ground::None);
		ui::drawQuickMenu(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)), v, appear);
		endUiScreen();
	}

	/* ---- 0.1.53: a list of choices over the screen (confirmations, places, actions) ---------- */

	void openPopup(const std::string &title, const std::string &text, const std::vector<std::string> &items,
		std::function<void(int)> pick)
	{
		pop.open = true;
		pop.title = title;
		pop.text = text;
		pop.items = items;
		pop.sel = 0;
		pop.pick = std::move(pick);
	}

	void popupInput(uint32_t pressed)
	{
		const int n = static_cast<int>(pop.items.size());
		if (pressed & PAD_UP) {
			pop.sel = (pop.sel + n - 1) % std::max(1, n);
		}
		if (pressed & PAD_DOWN) {
			pop.sel = (pop.sel + 1) % std::max(1, n);
		}
		if (pressed & (PAD_CROSS | PAD_CIRCLE)) {
			const int chosen = (pressed & PAD_CROSS) ? pop.sel : -1;
			pop.open = false;
			auto pick = std::move(pop.pick);
			pop.pick = nullptr;
			if (pick) {
				pick(chosen);
			}
		}
	}

	/* ---- 0.1.53: the game page (TRIANGLE on a disc) ---------------------------------------------
	 * A fixed panel in the middle of the screen, as PSFlyCast's and Crux2's: the cover, what is known
	 * of the disc, and Play, Load state, Game settings, Mods/patches/cheats and Saves along the foot,
	 * LEFT and RIGHT between them. It has the controller alone while it is open (the list stays where
	 * it was), and it is the game's whose page it is, not the list's selection. */

	enum GamePill { GP_PLAY, GP_STATES, GP_SETTINGS, GP_MODS, GP_SAVES, GP_COUNT };

	void openGamePage(const LibraryEntry &e)
	{
		gp = GamePage{};
		gp.open = true;
		gp.path = e.path;
		gp.title = ogbox::shortName(e.name);
		gp.since = now_seconds();
		pshakux_log("library: the game page of %s", e.path.c_str());
	}

	/* Back from a screen the page opened: the page again. */
	void reopenGamePage()
	{
		if (gp.returnTo) {
			gp.returnTo = false;
			gp.open = true;
			gp.since = now_seconds();
		}
	}

	/* The page's disc details, from the selection's reading when it is this disc's. */
	void gamePageInfo()
	{
		if (gp.info.ok || gp.path.empty()) {
			return;
		}
		/* 0.0.3: the page's own reading when the list's isn't this disc's (the page opened before the
		 * selection had rested, or while a folder was being listed: "reading..." stayed until the page
		 * was opened again), and again when a read failed (a share's hiccup), three times */
		if (!details || detailsPath != gp.path) {
			if (!listing) {
				startDetails(gp.path);
			}
			return;
		}
		bool failed = false;
		{
			std::lock_guard<std::mutex> lock(details->mutex);
			if (details->done) {
				gp.info = details->info;
				failed = !details->info.ok;
			}
		}
		if (failed && gp.readTries < 3 && now_seconds() - detailsSince > 1.5) {
			gp.readTries++;
			pshakux_log("library: %s's details unread: reading again (%d)", gp.path.c_str(), gp.readTries);
			startDetails(gp.path);
		}
	}

	std::vector<int> usedStateSlots(const std::string &key) const
	{
		std::vector<int> out;
		for (int i = 0; i <= ogbox::kStateSlots; i++) {
			if (ogbox::stateSlot(key, i).used) {
				out.push_back(i);
			}
		}
		std::sort(out.begin(), out.end(), [&](int a, int b) {
			return ogbox::stateSlot(key, a).mtime > ogbox::stateSlot(key, b).mtime;
		});
		return out;
	}

	/* ---- 0.1.53: the hard disk manager ----------------------------------------------------------
	 * The games' saves on the emulated Xbox hard disk, by game (Crux2's card manager's layout): copy a
	 * save out to the console's backups or a USB drive, move it there, delete it, bring saves in;
	 * delete a game's own data; empty the cache drives. */

	enum HddAction { HA_IMPORT, HA_EXPORT_ALL, HA_CACHES, HA_COUNT };

	void openHdd(uint32_t onlyTitle)
	{
		hdd.back = screen == Screen::Hdd ? hdd.back : screen;
		hdd.onlyTitle = onlyTitle;
		hdd.pane = 0;
		hdd.selS = 0;
		hdd.importing = false;
		hddRescan();
		hdd.selT = HA_COUNT;
		for (size_t i = 0; i < hdd.titles.size(); i++) {
			if (hdd.titles[i].titleId == onlyTitle) {
				hdd.selT = HA_COUNT + static_cast<int>(i);
				hdd.pane = hdd.titles[i].saves.empty() ? 0 : 1;
			}
		}
		if (hdd.titles.empty()) {
			hdd.selT = HA_IMPORT;
		}
		screen = Screen::Hdd;
		screenSince = now_seconds();
		pshakux_log("hard disk: %zu games with saves or data (%s)", hdd.titles.size(), boot.hdd.c_str());
	}

	void hddRescan()
	{
		hdd.titles = ogbox::hddScan(boot.hdd);
		hdd.cacheBytes = ogbox::hddCacheBytes(boot.hdd);
		hdd.usedBytes = ogbox::hddUsedBytes(boot.hdd);
		const int rows = HA_COUNT + static_cast<int>(hdd.titles.size());
		hdd.selT = std::min(hdd.selT, rows - 1);
	}

	const ogbox::HddTitle *hddTitle() const
	{
		const int i = hdd.selT - HA_COUNT;
		return i >= 0 && i < static_cast<int>(hdd.titles.size()) ? &hdd.titles[static_cast<size_t>(i)] : nullptr;
	}

	/* A place to copy to (or bring in from), then what. */
	void hddPickPlace(const std::string &title, std::function<void(const ogbox::SavePlace &)> then)
	{
		const std::vector<ogbox::SavePlace> places = ogbox::savePlaces();
		std::vector<std::string> names;
		for (const ogbox::SavePlace &p : places) {
			names.push_back(p.name);
		}
		openPopup(title, "", names, [places, then](int i) {
			if (i >= 0 && i < static_cast<int>(places.size())) {
				then(places[static_cast<size_t>(i)]);
			}
		});
	}

	void hddDone(bool ok, const std::string &yes, const std::string &why)
	{
		showToast(ok ? yes : "Not done: " + why);
		pshakux_log("hard disk: %s%s%s", ok ? yes.c_str() : "failed", ok ? "" : ": ", ok ? "" : why.c_str());
		hddRescan();
	}

	void hddInput(uint32_t pressed)
	{
		if (pressed & PAD_CIRCLE) {
			if (hdd.importing) {
				hdd.importing = false;
				hdd.pane = 0;
			} else if (hdd.pane == 1) {
				hdd.pane = 0;
			} else {
				screen = hdd.back == Screen::Hdd ? Screen::Library : hdd.back;
				screenSince = now_seconds();
				if (screen == Screen::Library) {
					reopenGamePage();
				}
			}
			return;
		}
		const ogbox::HddTitle *t = hddTitle();
		if (hdd.pane == 0) {
			const int rows = HA_COUNT + static_cast<int>(hdd.titles.size());
			if (pressed & PAD_UP) {
				hdd.selT = (hdd.selT + rows - 1) % rows;
				hdd.selS = 0;
			}
			if (pressed & PAD_DOWN) {
				hdd.selT = (hdd.selT + 1) % rows;
				hdd.selS = 0;
			}
			if ((pressed & PAD_RIGHT) && t && !t->saves.empty()) {
				hdd.pane = 1;
			}
			if ((pressed & PAD_TRIANGLE) && t) {
				const ogbox::HddTitle title = *t;
				openPopup(title.name, std::to_string(title.saves.size()) + " saves, " + ogbox::bytesText(title.bytes) +
					(title.dataBytes ? "; its own data " + ogbox::bytesText(title.dataBytes) : std::string()),
					{ "Copy all its saves to...", "Delete all its saves", "Delete its own data (its settings and caches)", "Go back" },
					[this, title](int i) {
						if (i == 0) {
							hddPickPlace("Copy to", [this, title](const ogbox::SavePlace &p) {
								std::string why;
								bool ok = true;
								for (const ogbox::HddSave &s : title.saves) {
									ok = ogbox::hddExportSave(title, s, p.path, why) && ok;
								}
								hddDone(ok, std::to_string(title.saves.size()) + " saves copied to " + p.name, why);
							});
						} else if (i == 1 || i == 2) {
							openPopup(i == 1 ? "Delete all its saves?" : "Delete its own data?",
								title.name + (i == 1 ? ": every save on the hard disk goes." : ": its settings and caches go; the game makes them again."),
								{ "Delete", "Go back" }, [this, title, i](int j) {
									if (j == 0) {
										std::string why;
										const bool ok = ogbox::hddDeleteTitle(title, i == 1, i == 2, boot.hdd, why);
										hddDone(ok, i == 1 ? "Its saves are deleted" : "Its own data is deleted", why);
									}
								});
						}
					});
			}
			if (!(pressed & PAD_CROSS)) {
				return;
			}
			if (hdd.selT == HA_IMPORT) {
				hddPickPlace("Bring saves in from", [this](const ogbox::SavePlace &p) {
					hdd.copies = ogbox::saveCopiesIn(p.path, boot.hdd);
					hdd.importPlace = p.path;
					hdd.importName = p.name;
					hdd.importing = true;
					hdd.pane = 1;
					hdd.selS = 0;
				});
			} else if (hdd.selT == HA_EXPORT_ALL) {
				hddPickPlace("Copy every save to", [this](const ogbox::SavePlace &p) {
					std::string why;
					bool ok = true;
					size_t n = 0;
					for (const ogbox::HddTitle &title : hdd.titles) {
						for (const ogbox::HddSave &s : title.saves) {
							ok = ogbox::hddExportSave(title, s, p.path, why) && ok;
							n++;
						}
					}
					hddDone(ok, std::to_string(n) + " saves copied to " + p.name, why);
				});
			} else if (hdd.selT == HA_CACHES) {
				openPopup("Empty the cache drives?", "X:, Y: and Z: hold what games keep for later (" + ogbox::bytesText(hdd.cacheBytes) +
					"); they fill them again as they need.", { "Empty them", "Go back" }, [this](int i) {
						if (i == 0) {
							std::string why;
							hddDone(ogbox::hddClearCaches(boot.hdd, why), "The cache drives are empty", why);
						}
					});
			} else if (t && !t->saves.empty()) {
				hdd.pane = 1;
			}
			return;
		}
		// the right pane
		if (hdd.importing) {
			const int rows = 1 + static_cast<int>(hdd.copies.size());
			if (pressed & PAD_UP) {
				hdd.selS = (hdd.selS + rows - 1) % rows;
			}
			if (pressed & PAD_DOWN) {
				hdd.selS = (hdd.selS + 1) % rows;
			}
			if (pressed & PAD_LEFT) {
				hdd.importing = false;
				hdd.pane = 0;
			}
			if (!(pressed & PAD_CROSS) || hdd.copies.empty()) {
				return;
			}
			if (hdd.selS == 0) {
				std::string why;
				size_t n = 0, kept = 0;
				for (const ogbox::SaveCopy &c : hdd.copies) {
					if (c.onDisk) {
						kept++;
						continue;
					}
					if (ogbox::hddImportSave(c, boot.hdd, false, why)) {
						n++;
					}
				}
				hdd.copies = ogbox::saveCopiesIn(hdd.importPlace, boot.hdd);
				hddDone(true, std::to_string(n) + " saves brought in" + (kept ? ", " + std::to_string(kept) + " the hard disk had already" : std::string()), why);
				return;
			}
			const ogbox::SaveCopy c = hdd.copies[static_cast<size_t>(hdd.selS - 1)];
			const auto bring = [this, c](bool overwrite) {
				std::string why;
				const bool ok = ogbox::hddImportSave(c, boot.hdd, overwrite, why);
				hdd.copies = ogbox::saveCopiesIn(hdd.importPlace, boot.hdd);
				hddDone(ok, "Brought in: " + c.name, why);
			};
			if (c.onDisk) {
				openPopup("Replace the save?", "The hard disk has \"" + c.folder + "\" of this game already.", { "Replace it", "Go back" },
					[bring](int i) {
						if (i == 0) {
							bring(true);
						}
					});
			} else {
				bring(false);
			}
			return;
		}
		if (!t) {
			hdd.pane = 0;
			return;
		}
		const int n = static_cast<int>(t->saves.size());
		if (pressed & PAD_LEFT) {
			hdd.pane = 0;
		}
		if ((pressed & PAD_UP) && n) {
			hdd.selS = (hdd.selS + n - 1) % n;
		}
		if ((pressed & PAD_DOWN) && n) {
			hdd.selS = (hdd.selS + 1) % n;
		}
		if (!(pressed & PAD_CROSS) || !n) {
			return;
		}
		const ogbox::HddTitle title = *t;
		const ogbox::HddSave save = t->saves[static_cast<size_t>(std::min(hdd.selS, n - 1))];
		openPopup(save.name, title.name + "   " + ogbox::dateText(save.mtime) + "   " + ogbox::bytesText(save.bytes),
			{ "Copy to...", "Move to...", "Delete", "Go back" }, [this, title, save](int i) {
				if (i == 0 || i == 1) {
					hddPickPlace(i == 0 ? "Copy to" : "Move to", [this, title, save, i](const ogbox::SavePlace &p) {
						std::string why;
						bool ok = ogbox::hddExportSave(title, save, p.path, why);
						if (ok && i == 1) {
							ok = ogbox::hddDeleteSave(save, why);
						}
						hddDone(ok, (i == 0 ? "Copied to " : "Moved to ") + p.name, why);
					});
				} else if (i == 2) {
					openPopup("Delete the save?", save.name + " (" + title.name + ")", { "Delete", "Go back" }, [this, save](int j) {
						if (j == 0) {
							std::string why;
							hddDone(ogbox::hddDeleteSave(save, why), "The save is deleted", why);
						}
					});
				}
			});
	}

	void drawHdd()
	{
		using R = ui::ListRow;
		ui::ListScreenView v;
		v.head.logo = uiLogo();
		v.head.title = "Xbox hard disk";
		v.head.subtitle = "E: " + ogbox::bytesText(hdd.usedBytes) + " used \xC2\xB7 " + boot.hdd;
		v.twoLists = true;
		v.pane = hdd.pane;
		// the left: the actions, then the games
		{
			R r;
			r.key = "import";
			r.title = "Bring saves in\xE2\x80\xA6";
			r.icon = ui::Icon::Load;
			v.rows.push_back(r);
			r = {};
			r.key = "export";
			r.title = "Copy every save out\xE2\x80\xA6";
			r.icon = ui::Icon::Save;
			v.rows.push_back(r);
			r = {};
			r.key = "caches";
			r.title = "Empty the cache drives (X, Y, Z)";
			r.right = ogbox::bytesText(hdd.cacheBytes);
			r.icon = ui::Icon::Trash;
			v.rows.push_back(r);
			r = {};
			r.kind = R::Kind::Section;
			r.key = "games";
			r.title = hdd.titles.empty() ? "No game has saved yet" : "Games";
			v.rows.push_back(r);
			for (const ogbox::HddTitle &t : hdd.titles) {
				r = {};
				r.key = t.idText;
				r.title = t.name;
				r.sub = std::to_string(t.saves.size()) + (t.saves.size() == 1 ? " save \xC2\xB7 " : " saves \xC2\xB7 ") + ogbox::bytesText(t.bytes) +
					" \xC2\xB7 " + t.idText;
				const int at = static_cast<int>(v.rows.size()) - 4;   // its place among the games
				if (std::abs(at - (hdd.selT - HA_COUNT)) <= 12) {
					r.thumb = uiCover(ogbox::coverSources(std::string(), std::string(), t.titleId, 1));
				}
				r.thumbSlot = true;
				v.rows.push_back(r);
			}
		}
		// (the rows have the games' heading between the actions and the games)
		v.sel = hdd.selT < HA_COUNT ? hdd.selT : hdd.selT + 1;
		// the right: the saves of the game, or the copies to bring in, or what the action does
		const ogbox::HddTitle *t = hddTitle();
		if (hdd.importing) {
			v.title2 = "In " + hdd.importName + " \xC2\xB7 " + hdd.importPlace;
			if (hdd.copies.empty()) {
				v.aside.text = "No saves there. A save is a folder <title ID>/<save>, or UDATA/<title ID>/<save>.";
			} else {
				R r;
				r.key = "all";
				r.title = "Bring them all in";
				r.sub = "those the hard disk doesn't have";
				r.icon = ui::Icon::Load;
				r.accent = true;
				v.rows2.push_back(r);
				for (const ogbox::SaveCopy &c : hdd.copies) {
					r = {};
					r.key = c.folder + c.name;
					r.title = c.name;
					r.right = (c.onDisk ? std::string("on the hard disk \xC2\xB7 ") : std::string()) + ogbox::dateText(c.mtime);
					v.rows2.push_back(r);
				}
			}
		} else if (t) {
			v.title2 = "Saves of " + t->name;
			if (t->saves.empty()) {
				v.aside.text = "No saves: only its own data (TDATA: " + ogbox::bytesText(t->dataBytes) + "). TRIANGLE opens what can be "
					"done with it.";
			}
			for (const ogbox::HddSave &sv : t->saves) {
				R r;
				r.key = sv.folder;
				r.title = sv.name;
				r.sub = ogbox::dateText(sv.mtime) + " \xC2\xB7 " + ogbox::bytesText(sv.bytes) + " \xC2\xB7 " + sv.folder;
				v.rows2.push_back(r);
			}
			if (t->dataBytes && !t->saves.empty()) {
				R r;
				r.kind = R::Kind::Note;
				r.key = "tdata";
				r.title = "Its own data (TDATA): " + ogbox::bytesText(t->dataBytes);
				v.rows2.push_back(r);
			}
		} else {
			v.aside.text = hdd.selT == HA_IMPORT ? "Saves copied out of OGBox, or from another Xbox or emulator, as folders <title ID>/<save> "
				"(or UDATA/<title ID>/<save>) in OGBox/saves on a USB drive, or in the data folder's backups/saves."
				: hdd.selT == HA_EXPORT_ALL ? "Every game's saves copied to the data folder's backups/saves, or to OGBox/saves on a USB drive."
				: "The games' cache drives. Emptying them is safe: games make what they need again (the first load after it is slower).";
		}
		v.sel2 = hdd.pane == 1 ? hdd.selS : -1;
		v.footer = hdd.pane == 0 ? std::vector<ui::Hint>{ { "UD", "Choose" }, { "X", "Open" }, { "T", "Copy, delete" }, { "O", "Back" } }
			: std::vector<ui::Hint>{ { "UD", "Choose" }, { "X", "Choose this" }, { "O", "Back" } };
		bd.key = "hdd";
		bd.blur = ui::Tex{};
		bd.tint = 0;
		bd.focus = ImVec2(0.2f, 0.3f);
		bd.strength = 0.6f;
		ImDrawList *dl = beginUiScreen("##hdd");
		ui::drawListScreen(dl, ImVec2(static_cast<float>(width), static_cast<float>(height)), v);
		endUiScreen();
	}

	/* ---- 0.1.52: the themes -------------------------------------------------------------------- */

	/* The palette and ImGui's colours for Code (black and green, the rain) or Emerald (green light). */
	/* ==== OGBox 0.0.3: the front end's new look (src/ogbox_ui.cpp draws it; tests/ui shows it) ======
	 * The library is a home: the chosen game's details on the left with its buttons, the games as a
	 * row of covers that slides (the chosen one larger), the folders, the Xbox dashboard and the hard
	 * disk as cards below; the ground takes the chosen game's colours. The settings are in three
	 * layers (what you'd change, the Xbox, Advanced) and the rest beside them, with their words under
	 * the chosen row; Network edits the online play and the shares with an on-screen keyboard. The
	 * controller drives all of it directly (ImGui's navigation is not used here). */

	std::vector<std::unique_ptr<vks::Texture2D>> uiTextures;   // ogbox_ui's own (its light, its shade)
	bool uiReady{ false };
	struct BlurTex {
		CoverTex t;
		ImU32 tint{ 0 };
	};
	std::map<std::string, BlurTex> blurTex;   // the covers made tiny (the ground), by file
	ui::Backdrop bd;                           // the ground: the chosen game's
	ui::Keyboard kb;
	int lastGame{ -1 }, lastCard{ -1 };        // the rows' last choices (entry indices)
	bool preferGame{ true };                   // a new listing chooses its first game
	uint32_t padPrev{ 0 };                     // the directions held last frame (with the stick's)
	double repeatAt[4]{};                      // when each direction repeats next
	float trigPrevL{ 0.0f }, trigPrevR{ 0.0f };
	enum : uint32_t { VPAD_L2 = 0x40000000u, VPAD_R2 = 0x80000000u };
	// the settings
	int setNavSel{ 0 }, setRowSel{ 0 };
	bool setFocusNav{ true };
	double setAppear{ 0.0 };
	struct RowAct {
		std::function<void()> press;         // CROSS
		std::function<void(int)> step;       // LEFT / RIGHT (L1 / R1: by 30 for a number)
		std::function<void()> reset;         // TRIANGLE
		std::function<void()> square;        // SQUARE (a share: test it)
		bool number{ false };
	};
	// the network shares
	ogbox::ShareFile shares;
	bool sharesRead{ false };
	struct ShareTest {
		std::mutex mutex;
		bool done{ false }, ok{ false };
		int items{ 0 };
		std::string error;
	};
	std::map<std::string, std::shared_ptr<ShareTest>> shareTests;   // by mount
	struct ShareForm {
		bool open{ false };
		int index{ -1 };                       // the share edited (-1 a new one)
		ogbox::Share s;
		int sel{ 0 };
		std::string status;
		bool bad{ false };
		std::shared_ptr<ShareTest> test;
		double since{ 0.0 };
		/* 0.0.3: the SMB servers on the network, looked for as the form opens (ogbox::findServers) */
		struct Found {
			std::mutex mutex;
			bool done{ false };
			std::vector<ogbox::FoundServer> servers;
		};
		std::shared_ptr<Found> found;
	} form;

	/* Once: ogbox_ui's scale, theme and how it makes images. */
	void uiInit()
	{
		if (uiReady) {
			return;
		}
		uiReady = true;
		ui::setScale(static_cast<float>(height) / 1080.0f);
		ui::setTheme(themeCode ? ui::Theme::Code : ui::Theme::Dark);
		ui::setTexMaker([this](const uint8_t *px, int w, int h) -> ui::Tex {
			auto t = std::make_unique<vks::Texture2D>();
			t->fromBuffer(const_cast<uint8_t *>(px), static_cast<VkDeviceSize>(w) * h * 4, VK_FORMAT_R8G8B8A8_UNORM,
				static_cast<uint32_t>(w), static_cast<uint32_t>(h), vulkanDevice, queue);
			const VkDescriptorSet set = ui.addTexture(t->sampler, t->view);
			if (set == VK_NULL_HANDLE) {
				t->destroy();
				return ui::Tex{};
			}
			uiTextures.push_back(std::move(t));
			return ui::Tex{ reinterpret_cast<ImTextureID>(set), static_cast<float>(w), static_cast<float>(h) };
		});
		pshakux_log("ogbox: the 0.0.3 menus (%s fonts)", ui::fontsLoaded() ? "their" : "the default");
	}

	ui::Tex uiCover(const std::vector<std::string> &names)
	{
		float w = 0.0f, h = 0.0f;
		const ImTextureID id = coverImage(names, &w, &h);
		return id ? ui::Tex{ id, w, h } : ui::Tex{};
	}

	/* A cover made tiny (12 x 16, drawn large: blurred) and its colour, for the ground. */
	ui::Tex uiBlur(const std::vector<std::string> &names, ImU32 *tint)
	{
		if (names.empty() || prefs.global("list_covers") == "0") {
			return {};
		}
		std::string path;
		if (ogbox::Covers::get().findAny(names, path) != ogbox::Covers::State::Ready) {
			return {};
		}
		auto it = blurTex.find(path);
		if (it == blurTex.end()) {
			if (coverUploads >= 2) {
				return {};
			}
			/* 0.0.4: decoded on the decoder's thread (its own request: the cover's picture is taken
			 * by the one who asked first) */
			Decoded d;
			const std::string bkey = path + "\x01" "blur";
			if (!coverDecoder.take(bkey, d)) {
				coverDecoder.request(bkey);
				return {};
			}
			coverUploads++;
			BlurTex b;
			const int iw = d.w, ih = d.h;
			stbi_uc *px = d.px;
			if (d.failed) {
				b.t.failed = true;
			} else {
				std::vector<uint8_t> tiny = ui::shrink(px, iw, ih, 12, 16);
				b.tint = ui::tintOf(px, iw, ih);
				stbi_image_free(px);
				b.t.tex.fromBuffer(tiny.data(), tiny.size(), VK_FORMAT_R8G8B8A8_UNORM, 12, 16, vulkanDevice, queue);
				b.t.set = ui.addTexture(b.t.tex.sampler, b.t.tex.view);
				b.t.w = 12;
				b.t.h = 16;
				if (b.t.set == VK_NULL_HANDLE) {
					b.t.tex.destroy();
					b.t.failed = true;
				}
			}
			b.t.used = framesDrawn;
			it = blurTex.emplace(path, std::move(b)).first;
			while (blurTex.size() > 6) {
				auto old = blurTex.end();
				for (auto c = blurTex.begin(); c != blurTex.end(); ++c) {
					if (c != it && (old == blurTex.end() || c->second.t.used < old->second.t.used)) {
						old = c;
					}
				}
				if (old == blurTex.end()) {
					break;
				}
				if (!old->second.t.failed) {
					coverRetired.emplace_back(framesDrawn + 4, std::move(old->second.t));
				}
				blurTex.erase(old);
			}
		}
		it->second.t.used = framesDrawn;
		if (tint) {
			*tint = it->second.tint;
		}
		if (it->second.t.failed || it->second.t.set == VK_NULL_HANDLE) {
			return {};
		}
		return ui::Tex{ reinterpret_cast<ImTextureID>(it->second.t.set), 12.0f, 16.0f };
	}

	ui::Tex uiLogo()
	{
		const ImTextureID id = logoImage();
		return id && logoHeight ? ui::Tex{ id, static_cast<float>(logoWidth), static_cast<float>(logoHeight) } : ui::Tex{};
	}

	/* This frame's presses with the D-pad's and the left stick's repeats (held 0.35 s, then every
	 * 0.07 s), and L2 / R2 as presses (VPAD_L2, VPAD_R2). */
	uint32_t navPressed()
	{
		static const uint32_t dirs[4] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT };
		uint32_t held = menuPad.held & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
		if (menuPad.left_y < -0.6f) {
			held |= PAD_UP;
		} else if (menuPad.left_y > 0.6f) {
			held |= PAD_DOWN;
		}
		if (menuPad.left_x < -0.6f) {
			held |= PAD_LEFT;
		} else if (menuPad.left_x > 0.6f) {
			held |= PAD_RIGHT;
		}
		uint32_t out = menuPad.pressed & ~(PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
		const double now = now_seconds();
		for (int i = 0; i < 4; i++) {
			const uint32_t d = dirs[i];
			if ((held & d) && !(padPrev & d)) {
				out |= d;
				repeatAt[i] = now + 0.35;
			} else if ((held & d) && now >= repeatAt[i]) {
				out |= d;
				repeatAt[i] = now + 0.07;
			}
		}
		padPrev = held;
		if (menuPad.l2 > 0.55f && trigPrevL <= 0.55f) {
			out |= VPAD_L2;
		}
		if (menuPad.r2 > 0.55f && trigPrevR <= 0.55f) {
			out |= VPAD_R2;
		}
		trigPrevL = menuPad.l2;
		trigPrevR = menuPad.r2;
		return out;
	}

	/* The on-screen keyboard has the controller (and a USB keyboard's typing) while it is open. */
	void keyboardInput(uint32_t p)
	{
		ui::Pad pad;
		pad.up = p & PAD_UP;
		pad.down = p & PAD_DOWN;
		pad.left = p & PAD_LEFT;
		pad.right = p & PAD_RIGHT;
		pad.cross = p & PAD_CROSS;
		pad.circle = p & PAD_CIRCLE;
		pad.square = p & PAD_SQUARE;
		pad.triangle = p & PAD_TRIANGLE;
		pad.l1 = p & PAD_L1;
		pad.r1 = p & PAD_R1;
		pad.l2 = p & VPAD_L2;
		pad.r2 = p & VPAD_R2;
		pad.options = p & PAD_OPTIONS;
		kb.input(pad, PS5Keyboard::Poll());
	}

	/* The screens' ground and a full-screen window to draw in; endUiScreen fades in from black.
	 * Full: the backdrop (the Code theme: the falling code); Dim: the paused game darkened; None:
	 * nothing (over a running game, the boot screen's logo). */
	enum class Ground { Full, Dim, None };
	ImDrawList *beginUiScreen(const char *name, Ground ground = Ground::Full)
	{
		const ImVec2 S(static_cast<float>(width), static_cast<float>(height));
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(S, ImGuiCond_Always);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		if (ground == Ground::Full) {
			drawGround(dl, 1.0f);
		} else if (ground == Ground::Dim) {
			dl->AddRectFilled(ImVec2(0, 0), S, themeCode ? IM_COL32(0, 0, 0, 236) : ui::alpha(ui::P().bg, 0.93f));
		}
		return dl;
	}

	void drawGround(ImDrawList *dl, float strength)
	{
		const ImVec2 S(static_cast<float>(width), static_cast<float>(height));
		if (themeCode) {
			dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, 255));
			drawRain(dl, 0.32f);
		} else {
			ui::Backdrop b = bd;
			b.strength *= strength;
			ui::backdrop(dl, S, b);
		}
	}

	void endUiScreen()
	{
		float fade = 1.0f;
		if (fadeSince == -2.0) {
			fadeSince = now_seconds();
		}
		if (fadeSince >= 0.0) {
			const double t = (now_seconds() - fadeSince) / 0.8;
			fade = t >= 1.0 ? 1.0f : static_cast<float>(std::max(0.0, t * t * (3.0 - 2.0 * t)));
			if (t >= 1.0) {
				fadeSince = -1.0;
			}
		}
		if (fade < 1.0f) {
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(static_cast<float>(width), static_cast<float>(height)),
				IM_COL32(0, 0, 0, static_cast<int>(255 * (1.0f - fade))));
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(2);
	}

	// ---- the library's home ---------------------------------------------------------------------

	/* The entries in the covers' row (the games) and in the cards' row (the rest), as indices, and
	 * the games as the home shows them: made again only when the listing changes (a share's folder
	 * of 980 games looked each one up every frame: 5 fps). */
	struct HomeCache {
		const LibraryEntry *data{ nullptr };
		size_t n{ SIZE_MAX };
		int tab{ -1 };
		std::string folder, first, last;
		std::vector<int> games, cards;
		std::vector<ui::HomeGame> list;
	} hc;

	void homeCacheCheck()
	{
		const std::string first = entries.empty() ? std::string() : entries.front().path;
		const std::string last = entries.empty() ? std::string() : entries.back().path;
		if (hc.n == entries.size() && hc.data == entries.data() && hc.tab == tab && hc.folder == folder && hc.first == first &&
			hc.last == last) {
			return;
		}
		hc.n = entries.size();
		hc.data = entries.data();
		hc.tab = tab;
		hc.folder = folder;
		hc.first = first;
		hc.last = last;
		hc.games.clear();
		hc.cards.clear();
		hc.list.clear();
		for (size_t i = 0; i < entries.size(); i++) {
			const LibraryEntry &e = entries[i];
			if (e.kind != LibraryEntry::Image) {
				hc.cards.push_back(static_cast<int>(i));
				continue;
			}
			hc.games.push_back(static_cast<int>(i));
			ui::HomeGame g;
			g.id = e.path;
			g.title = ogbox::shortName(e.name);
			if (const ogbox::DiscRecord *rec = ogbox::GameDb::get().byFileName(e.name)) {
				g.genre = rec->region;
				g.sub = rec->region;
			}
			g.size = e.note;
			g.tint = ui::tintFor(g.title);
			hc.list.push_back(std::move(g));
		}
		/* 0.0.3: every game's cover of the folder asked for now, in the list's order, not only those in
		 * sight (the home asked for a cover as it came on screen or was chosen) */
		if (prefs.global("list_covers") != "0" && !hc.games.empty()) {
			std::vector<std::vector<std::string>> all;
			all.reserve(hc.games.size());
			for (const int i : hc.games) {
				all.push_back(coverNames(entries[static_cast<size_t>(i)]));
			}
			/* 0.0.4: the first covers kept on the drive decoded now, before they come on screen */
			std::string path;
			for (size_t g = 0; g < all.size() && g < 24; g++) {
				if (ogbox::Covers::get().findAny(all[g], path) == ogbox::Covers::State::Ready) {
					coverDecoder.request(path);
				}
			}
			ogbox::Covers::get().want(std::move(all));
			/* 0.0.5: their descriptions too, after the covers' first round trips */
			std::vector<std::string> names;
			names.reserve(hc.games.size());
			for (const int i : hc.games) {
				names.push_back(ogbox::shortName(entries[static_cast<size_t>(i)].name));
			}
			ogbox::About::get().want(std::move(names));
		}
	}

	/* An entry's place in the covers' row (-1 not a game). */
	int homeGameAt(int entry) const
	{
		const auto it = std::lower_bound(hc.games.begin(), hc.games.end(), entry);
		return it != hc.games.end() && *it == entry ? static_cast<int>(it - hc.games.begin()) : -1;
	}

	/* The selection after a listing: the game last played here, else the first game, else the
	 * first card. */
	void homeSettle()
	{
		if (listing || entries.empty()) {
			return;   // (the listing to come chooses)
		}
		homeCacheCheck();
		const std::vector<int> &games = hc.games, &cards = hc.cards;
		if (selected >= 0 && selected < static_cast<int>(entries.size()) && !preferGame) {
			return;
		}
		if (selected >= 0 && selected < static_cast<int>(entries.size()) && entries[static_cast<size_t>(selected)].kind == LibraryEntry::Image) {
			preferGame = false;
			return;
		}
		if (!games.empty()) {
			selected = games.front();
		} else if (!cards.empty() && (selected < 0 || selected >= static_cast<int>(entries.size()))) {
			selected = cards.front();
		}
		preferGame = false;
	}

	/* What the buttons do on the home, in either layout (not the directions). */
	void homeActions(uint32_t p)
	{
		const LibraryEntry *e = selectedEntry();
		const bool onGame = e && e->kind == LibraryEntry::Image;
		if (p & PAD_CROSS) {
			if (e) {
				const LibraryEntry entry = *e;
				if (entry.kind == LibraryEntry::Image) {
					startGame(entry.path, homeKey(entry), 1);
				} else {
					activate(entry);
				}
			}
			return;
		}
		if ((p & PAD_SQUARE) && onGame) {
			startGame(e->path, homeKey(*e), 2);
			return;
		}
		if ((p & PAD_TRIANGLE) && onGame) {
			openGamePage(*e);
			return;
		}
		if (p & PAD_CIRCLE) {
			goUp();
			return;
		}
		if (p & PAD_OPTIONS) {
			openSettings(std::string(), std::string());
			return;
		}
		if (p & (PAD_L1 | PAD_R1)) {
			switchTab((p & PAD_R1) ? 1 : -1);
			return;
		}
		if ((p & (VPAD_L2 | VPAD_R2)) && onGame) {
			jumpByLetter((p & VPAD_R2) ? 1 : -1);
			if (selected >= 0 && selected < static_cast<int>(entries.size()) && entries[static_cast<size_t>(selected)].kind == LibraryEntry::Image) {
				lastGame = selected;
			}
		}
	}

	/* The home's directions. The games: LEFT / RIGHT along the shelf (the list: UP / DOWN one, LEFT /
	 * RIGHT ten); UP from them (the list: from its first) to the strip over them, its folders and
	 * actions, where LEFT / RIGHT move and DOWN goes back to the game last chosen. */
	void homeInput(uint32_t p)
	{
		if (!startPath.empty()) {
			return;
		}
		homeCacheCheck();
		const bool list = prefs.global("home_layout") == "list";
		const std::vector<int> &games = hc.games, &cards = hc.cards;
		const LibraryEntry *e = selectedEntry();
		const bool onGame = e && e->kind == LibraryEntry::Image;
		const auto choose = [&](int entry) {
			if (entry != selected) {
				selected = entry;
				(entries[static_cast<size_t>(entry)].kind == LibraryEntry::Image ? lastGame : lastCard) = entry;
			}
		};
		if (onGame) {
			const int n = static_cast<int>(games.size());
			const int at = static_cast<int>(std::find(games.begin(), games.end(), selected) - games.begin());
			int to = at;
			if (list) {
				to += (p & PAD_DOWN) ? 1 : 0;
				to -= (p & PAD_UP) && at > 0 ? 1 : 0;
				to += (p & PAD_RIGHT) ? 10 : 0;
				to -= (p & PAD_LEFT) ? 10 : 0;
			} else {
				to += (p & PAD_RIGHT) ? 1 : 0;
				to -= (p & PAD_LEFT) ? 1 : 0;
			}
			to = std::max(0, std::min(n - 1, to));
			if ((p & PAD_UP) && (!list || at == 0) && !cards.empty()) {
				lastGame = selected;
				choose(std::find(cards.begin(), cards.end(), lastCard) != cards.end() ? lastCard : cards.front());
			} else if (to != at) {
				choose(games[static_cast<size_t>(to)]);
			}
		} else if (!cards.empty()) {
			const int n = static_cast<int>(cards.size());
			int at = static_cast<int>(std::find(cards.begin(), cards.end(), selected) - cards.begin());
			if (at >= n) {
				at = 0;
				choose(cards.front());
			}
			if ((p & PAD_LEFT) && at > 0) {
				choose(cards[static_cast<size_t>(at - 1)]);
			}
			if ((p & PAD_RIGHT) && at + 1 < n) {
				choose(cards[static_cast<size_t>(at + 1)]);
			}
			if ((p & PAD_DOWN) && !games.empty()) {
				lastCard = selected;
				choose(std::find(games.begin(), games.end(), lastGame) != games.end() ? lastGame : games.front());
			}
		}
		homeActions(p);
	}

	/* A disc's title ID and version once its details are read ("" before). */
	std::string homeKey(const LibraryEntry &e)
	{
		if (details && detailsPath == e.path) {
			std::lock_guard<std::mutex> lock(details->mutex);
			if (details->done && details->info.ok) {
				return details->info.key;
			}
		}
		return std::string();
	}

	void drawHomeScreen()
	{
		ensureDetails();
		if (placesRead < 0.0 || now_seconds() - placesRead > 5.0) {
			const PlaceFilter f = placeFilter();
			places = listPlaces(&f);
			placesRead = now_seconds();
			const bool dash = dashboardPresent(boot.hdd.empty() ? ogbox::storagePath("hdd") : boot.hdd);
			const std::vector<int> t = tabsShown();
			if (std::find(t.begin(), t.end(), tab) == t.end()) {
				tab = TabInternal;
				openFolder(tabFolder[tab]);
			} else if (dash != haveDash && folder.empty() && !listing) {
				haveDash = dash;
				const LibraryEntry *cur = selectedEntry();
				openFolder(folder, cur ? cur->path : std::string());
			}
			haveDash = dash;
		}
		homeSettle();
		const ImVec2 S(static_cast<float>(width), static_cast<float>(height));
		ui::HomeView v;
		v.list = prefs.global("home_layout") == "list";
		v.logo = uiLogo();
		for (const int t : tabsShown()) {
			v.tabs.push_back(tabName(t));   // 0.0.6: Internal (was "Games"), USB, Network
			if (t == tab) {
				v.tab = static_cast<int>(v.tabs.size()) - 1;
			}
		}
		{
			int mounted = 0;
			for (unsigned i = 0; ps5vfs_mount_point(i); i++) {
				mounted++;
			}
			if (mounted) {
				v.status.push_back({ std::to_string(mounted) + (mounted == 1 ? " share" : " shares"), ui::P().ok });
			}
			const time_t now = std::time(nullptr);
			struct tm lt {};
			localtime_r(&now, &lt);
			char clock[16];
			std::strftime(clock, sizeof(clock), "%H:%M", &lt);
			v.status.push_back({ clock, 0 });
		}
		{   /* 0.0.3: the covers downloading: how many of how many, a bar at the foot */
			unsigned done = 0, total = 0;
			ogbox::Covers::get().progress(done, total);
			if (total > 0 && done < total && prefs.global("list_covers") != "0") {
				v.coverProgress = static_cast<float>(done) / static_cast<float>(total);
				v.coverText = "Covers " + std::to_string(done) + " of " + std::to_string(total);
			}
		}
		homeCacheCheck();
		const std::vector<int> &games = hc.games, &cards = hc.cards;
		v.gameSel = homeGameAt(selected);
		if (v.gameSel < 0 && !games.empty()) {
			const auto it = std::find(games.begin(), games.end(), lastGame);
			v.gameSel = it == games.end() ? 0 : static_cast<int>(it - games.begin());
		}
		v.cover = [this](size_t k) -> ui::Tex {
			const LibraryEntry &e = entries[static_cast<size_t>(hc.games[k])];
			OgboxGameInfo info{};
			const OgboxGameInfo *known = nullptr;
			if (details && detailsPath == e.path) {
				std::lock_guard<std::mutex> lock(details->mutex);
				if (details->done && details->info.ok) {
					info = details->info;
					known = &info;
				}
			}
			return uiCover(coverNames(e, known));
		};
		// the game shown: the selection's, or the row's last one while the cards have the controller
		const LibraryEntry *shown = v.gameSel >= 0 ? &entries[static_cast<size_t>(games[static_cast<size_t>(v.gameSel)])] : nullptr;
		const LibraryEntry *sel = selectedEntry();
		v.focusRow = sel && sel->kind == LibraryEntry::Image ? 0 : 1;
		if (shown) {
			bool done = false;
			OgboxGameInfo info{};
			int onCount = 0;
			if (details && detailsPath == shown->path) {
				std::lock_guard<std::mutex> lock(details->mutex);
				done = details->done;
				info = details->info;
				onCount = details->modsOn + details->patchesOn + details->cheatsOn + (details->trainerOn ? 1 : 0);
			}
			const ogbox::DiscRecord *rec = done && info.ok ? ogbox::GameDb::get().byTitle(info.title_id, shown->name, info.regions)
				: ogbox::GameDb::get().byFileName(shown->name);
			v.title = done && info.ok && info.title[0] ? std::string(info.title) : ogbox::shortName(shown->name);
			const ogbox::StateSlot left = done && info.ok ? ogbox::stateSlot(info.key, ogbox::kQuitSlot) : ogbox::StateSlot();
			const double played = ogbox::playedAt(shown->path);
			v.kicker = left.used ? "Continue" : played > 0.0 ? "Play again" : "Play";
			if (rec && !rec->region.empty()) {
				v.chips.push_back({ rec->region, 0 });
			}
			if (!shown->note.empty() && prefs.global("list_sizes") != "0") {
				v.chips.push_back({ shown->note, 0 });
			}
			if (onCount) {
				v.chips.push_back({ std::to_string(onCount) + (onCount == 1 ? " mod on" : " mods on"), 1 });
			}
			if (done && info.ok && v.chips.size() < 3) {
				v.chips.push_back({ info.key, 0 });
			}
			v.meta = left.used ? "Left off " + ogbox::dateText(left.mtime) + "." : played > 0.0 ?
				"Last played " + ogbox::dateText(static_cast<int64_t>(played)) + "." : "Not played yet.";
			if (folder.empty() && shown) {
				v.meta += " In " + parentOf(shown->path) + ".";
			}
			v.primary = left.used ? "Continue" : "Play";
			if (left.used) {
				v.secondary = "Start from the beginning";
			}
			{   /* 0.0.5: its description (Wikipedia's), as the game page shows it */
				std::string text, article;
				if (prefs.global("list_covers") != "0" && ogbox::About::get().find(ogbox::shortName(shown->name), text, article)) {
					v.about = text;
					v.aboutSource = "From Wikipedia" + (article.empty() ? std::string() : ", \"" + article + "\"") + " \xC2\xB7 CC BY-SA 4.0";
				}
			}
			ImU32 tint = 0;
			const ui::Tex blur = uiBlur(coverNames(*shown, done && info.ok ? &info : nullptr), &tint);
			bd.key = shown->path;
			bd.blur = blur;
			bd.tint = tint ? tint : ui::tintFor(ogbox::shortName(shown->name));
			bd.focus = ImVec2(0.42f, 0.55f);
			bd.strength = 1.0f;
		} else {
			bd.key = folder;
			bd.blur = ui::Tex{};
			bd.tint = 0;
			bd.strength = 0.7f;
		}
		// the cards
		v.cardsTitle = folder.empty() ? (tab == TabUsb ? "USB drives" : tab == TabNetwork ? "Network shares" : "Places") : baseName(folder);
		v.cardsSub = folder;
		for (const int i : cards) {
			const LibraryEntry &e = entries[static_cast<size_t>(i)];
			ui::HomeCard c;
			switch (e.kind) {
			case LibraryEntry::Place:
				c.title = e.name.rfind("Network: ", 0) == 0 ? e.name.substr(9) : e.name;
				c.icon = e.path.rfind("/mnt/usb", 0) == 0 || e.path.rfind("/mnt/ext", 0) == 0 ? ui::Icon::Usb
					: e.path.rfind("/smb", 0) == 0 ? ui::Icon::Server : e.path == "/data" ? ui::Icon::Console : ui::Icon::Folder;
				break;
			case LibraryEntry::Folder:
				c.title = e.name;
				c.icon = ui::Icon::Folder;
				break;
			case LibraryEntry::Up:
				c.title = "Up";
				c.sub = parentOf(folder) == "/" || folder == e.path ? std::string("the places") : baseName(parentOf(folder));
				c.icon = ui::Icon::Up;
				break;
			case LibraryEntry::Dashboard:
				c.title = "Xbox dashboard";
				c.icon = ui::Icon::Dash;
				c.accent = true;
				break;
			case LibraryEntry::Hdd:
				c.title = "Xbox hard disk";
				c.icon = ui::Icon::Console;
				break;
			case LibraryEntry::AddShare:
				c.title = "Add a network share";
				c.icon = ui::Icon::Plus;
				c.dashed = true;
				break;
			default:
				c.title = e.name;
				break;
			}
			v.cards.push_back(c);
			if (i == selected) {
				v.cardSel = static_cast<int>(v.cards.size()) - 1;
			}
		}
		if (v.cardSel < 0 && !cards.empty()) {
			const auto it = std::find(cards.begin(), cards.end(), lastCard);
			v.cardSel = it == cards.end() ? 0 : static_cast<int>(it - cards.begin());
		}
		v.loading = listing != nullptr;
		v.error = listError;
		if (!listing && games.empty()) {
			v.empty = folder.empty() ? (tab == TabInternal ? "No game played yet. Choose a place above: your games' folders are there."
				: tab == TabUsb ? "No game from a USB drive played yet. Choose a drive above."
				: "No game from a share played yet. Choose a share above, or add one.")
				: "No Xbox disc images (.iso, .xiso, .chd, .cci) in this folder.";
			if (folder.empty() && cards.empty()) {
				v.empty = tab == TabUsb ? "No USB drive is plugged in (or Settings > Game list hides them)."
					: tab == TabNetwork ? "No network share yet: add one in Settings > Network and shares."
					: "Nothing to show: Settings > Game list hides every place here.";
			}
		}
		if (jumpLetter && jumpSince >= 0.0 && now_seconds() - jumpSince < 0.7) {
			const float t = static_cast<float>(now_seconds() - jumpSince);
			v.jumpLetter = jumpLetter;
			v.jumpAlpha = t < 0.45f ? 1.0f : 1.0f - (t - 0.45f) / 0.25f;
		}
		if (v.focusRow == 0) {
			v.footer = { { "X", v.secondary.empty() ? "Play" : "Continue" } };
			if (!v.secondary.empty()) {
				v.footer.push_back({ "S", "From the start" });
			}
			v.footer.push_back({ "T", "Game Details" });
			v.footer.push_back({ "L2 R2", "A to Z" });
		} else {
			v.footer = { { "LR", "Choose" }, { "X", "Open" } };
			if (!games.empty()) {
				v.footer.push_back({ "D", "Games" });
			}
		}
		v.footer.push_back({ "OPT", "Settings" });
		v.footer.push_back({ "O", folder.empty() ? "Close OGBox" : "Back" });
		v.version = std::string("OGBox ") + pshakux_build_label();
		if (!startPath.empty()) {
			v.busy = "Starting " + (startPath == "none" ? std::string("the Xbox dashboard") : ogbox::shortName(baseName(startPath))) + "\xE2\x80\xA6";
		}
		ImDrawList *dl = beginUiScreen("##home");
		v.games.swap(hc.list);   // (the cache's, not copied each frame)
		ui::drawHome(dl, S, v);
		v.games.swap(hc.list);
		endUiScreen();
	}

	// ---- the settings -----------------------------------------------------------------------------

	enum SetPage { SP_PICTURE, SP_XBOX, SP_ADVANCED, SP_NETWORK, SP_LIST, SP_INTERFACE, SP_DATA, SP_HDD, SP_UPDATES, SP_ABOUT,
		SP_GAME_RESET, SP_CONTROLLER };

	std::vector<int> setPages() const
	{
		if (forGame) {
			return { SP_PICTURE, SP_CONTROLLER, SP_XBOX, SP_ADVANCED, SP_GAME_RESET };
		}
		/* 0.0.6: the data folder and the Xbox hard disk's place under About, its saves under XBOX */
		return { SP_PICTURE, SP_CONTROLLER, SP_XBOX, SP_ADVANCED, SP_NETWORK, SP_LIST, SP_INTERFACE, SP_UPDATES, SP_ABOUT };
	}

	static const char *setPageName(int page)
	{
		switch (page) {
		case SP_PICTURE: return "Picture and sound";
		case SP_CONTROLLER: return "Controller";
		case SP_XBOX: return "XBOX";
		case SP_ADVANCED: return "Advanced";
		case SP_NETWORK: return "Network and shares";
		case SP_LIST: return "Game list";
		case SP_INTERFACE: return "Interface";
		case SP_DATA: return "Data folder";
		case SP_HDD: return "Xbox hard disk";
		case SP_UPDATES: return "Updates";
		case SP_ABOUT: return "About";
		default: return "Use Default";
		}
	}

	/* The options each layer shows, in groups ("" none); what no layer names goes to Advanced. */
	static std::vector<std::pair<std::string, std::vector<std::string>>> setGroups(int page)
	{
		switch (page) {
		case SP_PICTURE:
			return { { "", { "render_scale", "aspect", "upscale", "fxaa", "framegen", "aniso", "sharpen", "dither", "crop_overscan", "show_fps" } } };
		case SP_XBOX:
			return { { "", { "language", "av_pack", "screen_format" } }, { "Video modes", { "480p", "720p", "1080i", "pal60" } },
				{ "Clock", { "clock_days", "clock_hours" } } };
		case SP_ADVANCED: {
			std::vector<std::pair<std::string, std::vector<std::string>>> g = {
				{ "", { "frame_pacing", "uncapped", "fast_drive", "gpu_cores", "async_shaders", "prebuild_shaders", "exact_depth",
					"audio_resampler", "console_ram" } },
			};
			std::vector<std::string> other;
			for (const ogbox::Option &o : ogbox::options()) {
				bool placed = false;
				for (int p2 : { SP_PICTURE, SP_CONTROLLER, SP_XBOX, SP_NETWORK, SP_LIST, SP_INTERFACE, SP_UPDATES }) {
					for (const auto &grp : setGroups(p2)) {
						placed = placed || std::find(grp.second.begin(), grp.second.end(), o.key) != grp.second.end();
					}
				}
				for (const auto &grp : g) {
					placed = placed || std::find(grp.second.begin(), grp.second.end(), o.key) != grp.second.end();
				}
				const std::string k = o.key;
				// 0.0.4: "audio_dsp" (full DSP) is hidden: experimental, as in xemu (some games' EP
				// programs never finish a frame); the code stays, Fast is what runs
				if (!placed && k != "list_layout" && k != "list_details" && k != "audio_dsp") {
					other.push_back(k);
				}
			}
			if (!other.empty()) {
				g.push_back({ "Other", other });
			}
			return g;
		}
		case SP_NETWORK:
			return { { "Online play", { "network", "insignia_dns", "kai_engine", "lan_peers" } } };
		case SP_LIST:
			return { { "Places", { "list_games", "list_internal", "list_usb", "list_ext", "list_network" } },
				{ "How it looks", { "home_layout", "list_covers", "list_names", "list_sort", "list_sizes" } } };
		case SP_INTERFACE:
			return { { "", { "theme", "rain", "quick_menu" } } };
		case SP_CONTROLLER:
			/* 0.0.6: the buttons on the controller's picture, in its order (left column, then right) */
			return { { "", { "pad_lt", "pad_lthumb", "pad_up", "pad_down", "pad_left", "pad_right", "pad_back", "pad_rt", "pad_y",
						"pad_x", "pad_b", "pad_a", "pad_white", "pad_black", "pad_start", "pad_rthumb" } },
				{ "Sticks and triggers", { "pad_sticks", "pad_invert_ly", "pad_invert_ry", "pad_dead_l", "pad_dead_r", "pad_dead_t" } },
				{ "Rumble", { "pad_rumble" } } };
		case SP_UPDATES:
			return { { "", { "update_check" } } };
		default:
			return {};
		}
	}

	/* Settings > Controller as the run uses it (the game's own on top), at once. */
	void loadPadMap()
	{
		padMap = ogbox::padMapFrom([this](const char *key) { return prefs.value(key); });
		const std::string r = prefs.value("pad_rumble");
		rumblePercent = r.empty() ? 100 : std::max(0, std::min(100, std::atoi(r.c_str())));
	}

	/* A value set from the settings, with what follows from it at once. */
	void setValue(const ogbox::Option &o, const std::string &next)
	{
		const std::string key = o.key;
		if (key == "pad_rumble") {   // 0.0.6: a short buzz at the new strength, as PSWin gives
			const float k = static_cast<float>(std::atoi(next.c_str())) / 100.0f;
			pad_vibrate(0.5f * k, 0.12f * k);
			buzzUntil = now_seconds() + 0.18;
		}
		if (forGame) {
			const std::string own = prefs.override_(key);
			prefs.setOverride(key, next == prefs.global(key) && own.empty() ? std::string() : next);
			if (o.section == ogbox::Section::Controller) {
				loadPadMap();
			}
			refreshFrameGen(key);
			return;
		}
		prefs.setGlobal(key, next);
		refreshFrameGen(key);
		if (key == "rain") {
			rainOn = next == "1";
		} else if (key == "theme") {
			themeCode = next == "code";
		} else if (key == "show_fps") {
			showFps = next != "0";
			fpsCorner = next;
		} else if (o.section == ogbox::Section::Controller) {
			loadPadMap();
		}
		if (o.section == ogbox::Section::Library) {
			placesRead = -100.0;
			ogbox::Covers::get().enable(prefs.global("list_covers") != "0");
			ogbox::About::get().enable(prefs.global("list_covers") != "0");   // 0.0.3
			if (!listing && (folder.empty() || key == "list_sort")) {
				const LibraryEntry *cur = selectedEntry();
				openFolder(folder, cur ? cur->path : std::string());
			}
		}
	}

	void optionRow(const ogbox::Option &o, std::vector<ui::SetRow> &rows, std::vector<RowAct> &acts)
	{
		const std::string own = forGame ? prefs.override_(o.key) : std::string();
		const std::string v = forGame ? (own.empty() ? prefs.global(o.key) : own) : prefs.global(o.key);
		ui::SetRow r;
		RowAct a;
		r.key = o.key;
		r.name = o.label;
		r.desc = o.description;   // 0.0.6: no "takes effect" or "default" lines (TRIANGLE gives the default)
		r.changed = forGame ? !own.empty() : (!o.text && v != o.def) || (o.text && !v.empty());
		r.inherited = forGame && own.empty();
		const ogbox::Option *op = &o;
		if (o.text) {
			r.kind = ui::SetRow::Kind::Text;
			r.value = v;
			r.placeholder = std::string(o.key) == "insignia_dns" ? "46.101.64.175 (Insignia's)" : std::string(o.key) == "lan_peers"
				? "addresses, by commas" : "an address";
			a.press = [this, op, v]() {
				kb.open(op->label, v, std::string(op->key) == "insignia_dns" ? "46.101.64.175" : "", ui::Keyboard::Kind::Address, false,
					[this, op](bool ok, const std::string &value) {
						if (ok) {
							setValue(*op, value);
							pshakux_log("settings: %s = %s", op->key, value.c_str());
						}
					});
			};
		} else if (o.choices.empty()) {
			r.kind = ui::SetRow::Kind::Number;
			r.value = ogbox::Settings::label(o, v) + (o.unit && *o.unit ? std::string(" ") + o.unit : std::string());
			a.number = true;
			a.step = [this, op, v](int by) { setValue(*op, ogbox::Settings::step(*op, v, by)); };
		} else if (o.choices.size() == 2 && std::string(o.choices[0].value) == "0" && std::string(o.choices[1].value) == "1") {
			r.kind = ui::SetRow::Kind::Toggle;
			r.on = v == "1";
			a.press = [this, op, v]() { setValue(*op, v == "1" ? "0" : "1"); };
			a.step = [this, op](int by) { setValue(*op, by > 0 ? "1" : "0"); };
		} else {
			r.kind = ui::SetRow::Kind::Choice;
			for (const ogbox::Choice &c : o.choices) {
				if (c.off) {
					r.offChoices.push_back(static_cast<int>(r.choices.size()));
				}
				r.choices.push_back(c.label);
				if (v == c.value) {
					r.choice = static_cast<int>(r.choices.size()) - 1;
				}
			}
			if (std::string(o.key) == "theme" && v == "emerald") {
				r.choice = 0;   // (0.0.2's Emerald: Dark now)
			}
			a.press = [this, op, v]() { setValue(*op, ogbox::Settings::step(*op, v, 1)); };
			a.step = [this, op, v](int by) {
				// no wrapping round at the ends: LEFT at the first stays
				int at = 0;
				for (size_t i = 0; i < op->choices.size(); i++) {
					if (v == op->choices[i].value) {
						at = static_cast<int>(i);
					}
				}
				const int to = std::max(0, std::min(static_cast<int>(op->choices.size()) - 1, at + by));
				setValue(*op, op->choices[static_cast<size_t>(to)].value);
			};
		}
		a.reset = [this, op]() {
			if (forGame) {
				prefs.setOverride(op->key, std::string());
			} else {
				setValue(*op, op->def);
			}
		};
		rows.push_back(r);
		acts.push_back(a);
	}

	void groupRow(const std::string &name, std::vector<ui::SetRow> &rows, std::vector<RowAct> &acts)
	{
		ui::SetRow r;
		r.kind = ui::SetRow::Kind::Group;
		r.key = "g." + name;
		r.name = name;
		rows.push_back(r);
		acts.push_back(RowAct{});
	}

	void actionRow(const std::string &key, const std::string &name, const std::string &value, const std::string &desc,
		std::function<void()> press, std::vector<ui::SetRow> &rows, std::vector<RowAct> &acts, ui::Icon icon = ui::Icon::None)
	{
		ui::SetRow r;
		r.kind = press ? ui::SetRow::Kind::Action : ui::SetRow::Kind::Info;
		r.key = key;
		r.name = name;
		r.value = value;
		r.desc = desc;
		r.icon = icon;
		rows.push_back(r);
		RowAct a;
		a.press = std::move(press);
		acts.push_back(a);
	}

	/* 0.0.6: the newest release notes in the README beside eboot.bin (its first "### " under "## Release
	 * notes"), as text: the Added / Changed / Fixed headings and their points. */
	std::string whatsNew() const
	{
		std::ifstream in(getAssetPath() + "../README.md");
		std::string line, out;
		bool inNotes = false, inVersion = false;
		while (std::getline(in, line)) {
			if (line.rfind("## Release notes", 0) == 0) {
				inNotes = true;
				continue;
			}
			if (!inNotes) {
				continue;
			}
			if (line.rfind("### ", 0) == 0) {
				if (inVersion) {
					break;
				}
				inVersion = true;
				continue;
			}
			if (line.rfind("## ", 0) == 0) {
				break;
			}
			if (!inVersion || line.empty()) {
				continue;
			}
			std::string t = line;
			for (const char *mark : { "**", "`" }) {
				for (size_t at = t.find(mark); at != std::string::npos; at = t.find(mark)) {
					t.erase(at, std::strlen(mark));
				}
			}
			if (t.rfind("- ", 0) == 0) {
				out += "\n\xE2\x80\xA2 " + t.substr(2);
			} else if (t.rfind("  ", 0) == 0) {
				out += " " + t.substr(t.find_first_not_of(' '));   // a point's next line
			} else {
				out += (out.empty() ? "" : "\n\n") + t;   // Added, Changed, Fixed
			}
		}
		return out.empty() ? std::string("This version's notes aren't in its folder (README.md).") : out;
	}

	/* 0.0.6: Settings > About's credits: who made what OGBox is built on. */
	static std::vector<std::pair<std::string, std::string>> credits()
	{
		return {
			{ "OGBox", "by Press5elect: the PS5 port, its menus, the logo and the native CPU backend." },
			{ "hakuX", "by rfandango and its contributors (GPL-3.0-or-later): the emulator. hakuX-lr's graphics, audio, disc and "
				"kernel work by PortareOS and its contributors." },
			{ "xemu", "The MCPX APU (LGPL-2.1-or-later), and raddad772's measurements of the Xbox's voice processor on a real "
				"console (xemu pull request #3105), by way of CallistoNTG's hakuX port." },
			{ "DSP56300", "Hatari's DSP56300 emulation (GPL-2.0-or-later), and its JIT." },
			{ "Cxbx-Reloaded", "The rules for which rdtsc instructions to patch (GPL-2.0-or-later)." },
			{ "nxdk", "The Xbox kernel's export table (CC0)." },
			{ "Mesa (RADV)", "The Vulkan driver (MIT and other permissive licences); its PS5 parts by Mihawk." },
			{ "PS5 Vulkan and the payload SDK", "Mihawk's PS5_Vulkan and platform layer; John Tornblom and ps5-payload-dev's SDK." },
			{ "FidelityFX FSR 1.0", "AMD's EASU upscaler (MIT)." },
			{ "SMAA", "Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez (MIT)." },
			{ "Dear ImGui", "Omar Cornut and contributors (MIT): the menus' drawing." },
			{ "Network shares", "libsmb2 and libnfs by Ronnie Sahlberg (LGPL-2.1-or-later)." },
			{ "Disc images", "libchdr (BSD-3-Clause), with the LZMA SDK, zstd and dr_flac." },
			{ "Covers and descriptions", "libretro's thumbnails, MobCat's scans of the boxes, and Wikipedia's articles (CC BY-SA 4.0)." },
			{ "Game data", "Redump's disc database (CC BY-SA 4.0); the patches' and trainers' authors are named beside each one." },
			{ "Everything else", "LEGAL.txt, NOTICE.txt and licenses/ in OGBox's folder name every part, its authors and its licence." },
		};
	}

	/* The page's rows and what each does. */
	void settingsModel(int page, std::vector<ui::SetRow> &rows, std::vector<RowAct> &acts)
	{
		for (const auto &grp : setGroups(page)) {
			bool any = false;
			for (const std::string &key : grp.second) {
				const ogbox::Option *o = ogbox::option(key);
				if (!o || (forGame && !o->perGame) || (key == "rain" && prefs.global("theme") != "code")) {
					continue;
				}
				if (!any && !grp.first.empty()) {
					groupRow(grp.first, rows, acts);
				}
				any = true;
				optionRow(*o, rows, acts);
			}
		}
		const std::string hdd = boot.hdd.empty() ? ogbox::storagePath("hdd") : boot.hdd;
		switch (page) {
		case SP_NETWORK:
			shareRows(rows, acts);
			break;
		case SP_LIST:
			if (!haveDash) {
				groupRow("The Xbox dashboard", rows, acts);
				actionRow("dash", "Not on the hard disk", "hidden", "OGBox doesn't include the Xbox dashboard. To use yours, put "
					"xboxdash.xbe (with its files) on the hard disk's C: drive, " + hdd + "/Partition2; its card shows then.",
					nullptr, rows, acts, ui::Icon::Dash);
			}
			break;
		case SP_DATA:
			actionRow("root", "Now", ogbox::storageRoot(), "Where OGBox keeps the Xbox hard disk, the caches, the covers and the "
				"screenshots.", nullptr, rows, acts, ui::Icon::Folder);
			actionRow("free", "Free there", ogbox::freeSpaceText(ogbox::storageRoot()), "", nullptr, rows, acts);
			actionRow("move", "Choose another place", "moves them there", "Choose another folder or drive; OGBox moves its "
				"data there and keeps using it.", [this]() { openStorage(false); }, rows, acts, ui::Icon::Usb);
			break;
		case SP_HDD:
			actionRow("hdd", "Saves and caches", "open", "The games' saves on the Xbox hard disk: copy them to a USB drive or the "
				"console's backups, bring saves in, delete them; empty the cache drives.", [this]() { openHdd(0); }, rows, acts,
				ui::Icon::Console);
			actionRow("hddpath", "Where it is", hdd, "", nullptr, rows, acts);
			break;
		case SP_XBOX:
			/* 0.0.6: the Xbox hard disk's saves here (its place: About) */
			groupRow("Xbox hard disk", rows, acts);
			actionRow("hdd", "Saves and caches", "open", "The games' saves on the Xbox hard disk: copy them to a USB drive or the "
				"console's backups, bring saves in, delete them; empty the cache drives.", [this]() { openHdd(0); }, rows, acts,
				ui::Icon::Console);
			break;
		case SP_UPDATES:
			actionRow("check", "Look for an update now", updateLine(), "Asks GitHub whether a newer OGBox is out and offers it with "
				"what is new. Nothing is downloaded without asking; your settings, the hard disk, states and games stay.",
				[this]() { checkForUpdate(); }, rows, acts, ui::Icon::Check);
			/* 0.0.6: this version's release notes (the README beside eboot.bin) */
			actionRow("news", "What's New?", "this version", "What was added, changed and fixed in this version.",
				[this]() { openPopup(std::string("What's new in OGBox ") + pshakux_build_label(), whatsNew(), { "Close" }, [](int) {}); },
				rows, acts, ui::Icon::Info);
			break;
		case SP_ABOUT:
			/* 0.0.6: the version, where OGBox keeps its data and the Xbox hard disk, and the credits */
			actionRow("ver", "OGBox", pshakux_build_label(), "", nullptr, rows, acts);
			groupRow("Data folder", rows, acts);
			actionRow("root", "Now", ogbox::storageRoot(), "Where OGBox keeps the Xbox hard disk, the caches, the covers and the "
				"screenshots.", nullptr, rows, acts, ui::Icon::Folder);
			actionRow("free", "Free there", ogbox::freeSpaceText(ogbox::storageRoot()), "", nullptr, rows, acts);
			actionRow("move", "Choose another place", "moves them there", "Choose another folder or drive; OGBox moves its "
				"data there and keeps using it.", [this]() { openStorage(false); }, rows, acts, ui::Icon::Usb);
			actionRow("hddpath", "Xbox hard disk", hdd, "", nullptr, rows, acts, ui::Icon::Console);
			groupRow("Credits", rows, acts);
			for (const auto &c : credits()) {
				actionRow("cr." + c.first, c.first, "", c.second, nullptr, rows, acts);
			}
			break;
		case SP_GAME_RESET:
			actionRow("reset", "Set everything to Default", std::to_string(prefs.overrides()) + " of its own",
				"Every setting of this game back to Default.", [this]() {
					prefs.clearOverrides();
					pshakux_log("settings: %s: every setting back to the app's", forGameKey.c_str());
					showToast("This game uses the Default settings again");
				}, rows, acts);
			break;
		default:
			break;
		}
	}

	std::string setPageSub(int page, ImU32 *dot, bool *warn)
	{
		const auto lab = [this](const char *key) {
			const ogbox::Option *o = ogbox::option(key);
			return o ? ogbox::Settings::label(*o, forGame ? prefs.value(key) : prefs.global(key)) : std::string();
		};
		switch (page) {
		case SP_PICTURE: return lab("render_scale") + " \xC2\xB7 " + lab("aspect");
		case SP_XBOX: return lab("language") + " \xC2\xB7 " + lab("av_pack");
		case SP_CONTROLLER: {
			int changed = 0;
			for (const ogbox::Option &o : ogbox::options()) {
				if (o.section == ogbox::Section::Controller) {
					const std::string v = forGame ? prefs.override_(o.key) : prefs.global(o.key);
					changed += forGame ? !v.empty() : v != o.def;
				}
			}
			return changed ? std::to_string(changed) + " changed" : std::string();
		}
		case SP_ADVANCED: *warn = true; return "experimental";
		case SP_NETWORK: {
			const std::string net = prefs.global("network");
			*dot = net.empty() || net == "0" || net == "off" ? 0 : ui::P().ok;
			const size_t n = shares.shares.size();
			return lab("network") + (n ? " \xC2\xB7 " + std::to_string(n) + (n == 1 ? " share" : " shares") : std::string());
		}
		case SP_LIST: return prefs.global("list_covers") == "0" ? "no covers" : "with covers";
		case SP_INTERFACE: return lab("theme");
		case SP_DATA: return ogbox::freeSpaceText(ogbox::storageRoot());
		case SP_HDD: return "saves and caches";
		case SP_UPDATES: return updateLine();
		case SP_ABOUT: return pshakux_build_label();   // 0.0.6: the version only
		case SP_GAME_RESET: return std::to_string(prefs.overrides()) + " of its own";
		default: return std::string();
		}
	}

	void openSettingsPage(int page)
	{
		const std::vector<int> pages = setPages();
		const auto it = std::find(pages.begin(), pages.end(), page);
		setNavSel = it == pages.end() ? 0 : static_cast<int>(it - pages.begin());
		setRowSel = 0;
		setFocusNav = false;
	}

	void leaveSettings()
	{
		kb.close(false);
		form.open = false;
		if (!library) {
			pshakux_log("quick menu: back from the game's settings");
			qm.sub = Quick::Sub::None;
			prefs.load(runKey);
			return;
		}
		screen = Screen::Library;
		screenSince = now_seconds();
		reopenGamePage();
	}

	void settingsInput(uint32_t p)
	{
		if (form.open) {
			formInput(p);
			return;
		}
		const std::vector<int> pages = setPages();
		setNavSel = std::max(0, std::min(static_cast<int>(pages.size()) - 1, setNavSel));
		const int page = pages[static_cast<size_t>(setNavSel)];
		if (page == SP_NETWORK) {
			readSharesOnce();
		}
		std::vector<ui::SetRow> rows;
		std::vector<RowAct> acts;
		settingsModel(page, rows, acts);
		const auto selectable = [&](int i) {
			return i >= 0 && i < static_cast<int>(rows.size()) && rows[static_cast<size_t>(i)].kind != ui::SetRow::Kind::Group;
		};
		if (setFocusNav) {
			const int n = static_cast<int>(pages.size());
			if (p & PAD_UP) {
				setNavSel = (setNavSel + n - 1) % n;
				setRowSel = 0;
			}
			if (p & PAD_DOWN) {
				setNavSel = (setNavSel + 1) % n;
				setRowSel = 0;
			}
			if (p & (PAD_CROSS | PAD_RIGHT)) {
				int first = 0;
				while (first < static_cast<int>(rows.size()) && !selectable(first)) {
					first++;
				}
				if (first < static_cast<int>(rows.size())) {
					setRowSel = first;
					setFocusNav = false;
				}
			}
			if (p & PAD_CIRCLE) {
				leaveSettings();
			}
			return;
		}
		if (!selectable(setRowSel)) {
			int first = 0;
			while (first < static_cast<int>(rows.size()) && !selectable(first)) {
				first++;
			}
			setRowSel = first;
		}
		if (p & PAD_UP) {
			int i = setRowSel - 1;
			while (i >= 0 && !selectable(i)) {
				i--;
			}
			if (i >= 0) {
				setRowSel = i;
			}
		}
		if (p & PAD_DOWN) {
			int i = setRowSel + 1;
			while (i < static_cast<int>(rows.size()) && !selectable(i)) {
				i++;
			}
			if (i < static_cast<int>(rows.size())) {
				setRowSel = i;
			}
		}
		if (p & PAD_CIRCLE) {
			setFocusNav = true;
			return;
		}
		if (p & PAD_SQUARE) {
			if (setRowSel < static_cast<int>(acts.size()) && acts[static_cast<size_t>(setRowSel)].square) {
				acts[static_cast<size_t>(setRowSel)].square();
			} else if (page == SP_PICTURE || page == SP_XBOX || page == SP_ADVANCED || page == SP_LIST || page == SP_INTERFACE ||
				page == SP_NETWORK) {
				openPopup("Reset " + std::string(setPageName(page)) + "?", forGame ? "This game's own settings here are removed, and it uses "
					"Default again." : "Every setting here goes back to how OGBox comes.", { "No", "Yes, reset them" }, [this, acts](int i) {
						if (i == 1) {
							for (const RowAct &a : acts) {
								if (a.reset) {
									a.reset();
								}
							}
							showToast("Reset");
						}
					});
			}
			return;
		}
		if (setRowSel >= static_cast<int>(acts.size())) {
			return;
		}
		const RowAct &a = acts[static_cast<size_t>(setRowSel)];
		if ((p & PAD_LEFT) && a.step) {
			a.step(-1);
		} else if ((p & PAD_LEFT) && !a.step) {
			setFocusNav = true;
		}
		if ((p & PAD_RIGHT) && a.step) {
			a.step(1);
		}
		if (a.number && (p & (PAD_L1 | PAD_R1)) && a.step) {
			a.step((p & PAD_R1) ? 30 : -30);
		}
		if ((p & PAD_CROSS) && (a.press || a.step)) {
			if (a.press) {
				a.press();
			} else {
				a.step(1);
			}
		}
		if ((p & PAD_TRIANGLE) && a.reset) {
			a.reset();
		}
	}

	void drawSettingsScreen()
	{
		const std::vector<int> pages = setPages();
		setNavSel = std::max(0, std::min(static_cast<int>(pages.size()) - 1, setNavSel));
		const int page = pages[static_cast<size_t>(setNavSel)];
		if (!forGame) {
			readSharesOnce();
		}
		const ImVec2 S(static_cast<float>(width), static_cast<float>(height));
		ui::SettingsView v;
		v.logo = uiLogo();
		v.title = "Settings";
		v.subtitle = forGame ? forGameTitle + " \xC2\xB7 this game's own" : std::string();
		v.right = forGame ? forGameKey : std::string();
		for (size_t i = 0; i < pages.size(); i++) {
			ui::SetNav n;
			n.name = setPageName(pages[i]);
			n.sub = setPageSub(pages[i], &n.dot, &n.warn);
			n.gapBefore = !forGame ? pages[i] == SP_NETWORK : pages[i] == SP_GAME_RESET;
			v.nav.push_back(n);
		}
		v.navSel = setNavSel;
		v.focusNav = setFocusNav;
		v.pageTitle = setPageName(page);
		if (page == SP_ADVANCED) {
			v.banner = "Experimental settings: they might crash the console.";
			v.bannerWarn = true;
		} else if (forGame && page != SP_GAME_RESET) {
			v.banner = "Only this game's: a setting marked \"Default\" follows Settings; TRIANGLE puts one back.";
		}
		std::vector<RowAct> acts;
		settingsModel(page, v.rows, acts);
		v.rowSel = setRowSel;
		if (page == SP_CONTROLLER) {
			/* 0.0.6: the buttons on the controller's picture instead of rows (still chosen with UP and DOWN) */
			for (int i = 0; i < static_cast<int>(ogbox::XboxControl::Count); i++) {
				const char *key = ogbox::padKey(static_cast<ogbox::XboxControl>(i));
				for (ui::SetRow &r : v.rows) {
					if (r.key == key) {
						r.hidden = true;
						ui::PadCallout c;
						c.key = key;
						c.name = ogbox::padLabel(static_cast<ogbox::XboxControl>(i));
						c.value = r.choice >= 0 && r.choice < static_cast<int>(r.choices.size()) ? r.choices[static_cast<size_t>(r.choice)] : r.value;
						c.changed = r.changed;
						v.pad.push_back(c);
					}
				}
			}
		}
		if (setFocusNav) {
			v.footer = { { "UD", "Choose" }, { "X", "Open" }, { "O", forGame && !library ? "Back to the game" : "Back" } };
		} else {
			const ui::SetRow *r = setRowSel >= 0 && setRowSel < static_cast<int>(v.rows.size()) ? &v.rows[static_cast<size_t>(setRowSel)] : nullptr;
			v.footer = { { "UD", "Choose" } };
			if (r && (r->kind == ui::SetRow::Kind::Choice || r->kind == ui::SetRow::Kind::Number || r->kind == ui::SetRow::Kind::Toggle)) {
				v.footer.push_back({ "LR", "Change" });
				v.footer.push_back({ "T", "Default" });
			} else if (r && r->kind == ui::SetRow::Kind::Text) {
				v.footer.push_back({ "X", "Type" });
				v.footer.push_back({ "T", "Clear" });
			} else if (r && r->kind == ui::SetRow::Kind::Share) {
				v.footer.push_back({ "X", "Edit" });
				v.footer.push_back({ "S", "Test" });
				v.footer.push_back({ "T", "Remove" });
			} else if (r && r->kind == ui::SetRow::Kind::Action) {
				v.footer.push_back({ "X", "Open" });
			}
			if (!(r && r->kind == ui::SetRow::Kind::Share) && page != SP_DATA && page != SP_HDD && page != SP_UPDATES &&
				page != SP_ABOUT && page != SP_GAME_RESET) {
				v.footer.push_back({ "S", "Reset section" });
			}
			v.footer.push_back({ "O", "Sections" });
		}
		v.footerRight = std::string("OGBox ") + pshakux_build_label();
		bd.strength = std::min(bd.strength, 0.6f);
		// in a game: over the paused picture, darkened
		ImDrawList *dl = beginUiScreen(library ? "##settings" : "##gamesettings", library ? Ground::Full : Ground::Dim);
		ui::drawSettings(dl, S, v);
		if (form.open) {
			drawShareForm(dl, S);
		}
		endUiScreen();
	}

	// ---- the network shares (Settings > Network) --------------------------------------------------

	void readSharesOnce()
	{
		if (sharesRead) {
			return;
		}
		sharesRead = true;
		std::string why;
		if (!ogbox::readShares(kNetworkFile, shares, &why)) {
			pshakux_log("settings: %s cannot be read (%s): no shares", kNetworkFile, why.c_str());
		}
		// each share's mount, as startFileLayer made it
		std::vector<std::string> mounts;
		for (unsigned i = 0; ps5vfs_mount_point(i); i++) {
			mounts.push_back(ps5vfs_mount_point(i));
		}
		for (ogbox::Share &s : shares.shares) {
			for (const std::string &m : mounts) {
				if (m == "/smb/" + ogbox::shareMountName(s.name, {})) {
					s.mount = m;
				}
			}
			if (!s.mount.empty()) {
				testShare(s.mount);
			}
		}
	}

	std::vector<std::string> mountsTaken() const
	{
		std::vector<std::string> m;
		for (unsigned i = 0; ps5vfs_mount_point(i); i++) {
			m.push_back(ps5vfs_mount_point(i));
		}
		return m;
	}

	/* Lists a share's folder on a thread of its own: whether it answers, and how much is there. */
	std::shared_ptr<ShareTest> testShare(const std::string &mount)
	{
		auto t = std::make_shared<ShareTest>();
		shareTests[mount] = t;
		std::thread([t, mount]() {
			int items = 0;
			bool ok = false;
			std::string error;
			if (ps5vfs_dir *d = ps5vfs_opendir(mount.c_str())) {
				ps5vfs_dirent e;
				while (ps5vfs_readdir(d, &e) == 1) {
					items++;
				}
				ps5vfs_closedir(d);
				ok = true;
			} else {
				std::lock_guard<std::mutex> lock(g_vfsErrorMutex);
				error = g_vfsError.empty() ? std::string("it doesn't answer") : g_vfsError;
			}
			std::lock_guard<std::mutex> lock(t->mutex);
			t->ok = ok;
			t->items = items;
			t->error = error;
			t->done = true;
		}).detach();
		return t;
	}

	void shareRows(std::vector<ui::SetRow> &rows, std::vector<RowAct> &acts)
	{
		groupRow("Network shares", rows, acts);
		for (size_t i = 0; i < shares.shares.size(); i++) {
			const ogbox::Share &s = shares.shares[i];
			ui::SetRow r;
			r.kind = ui::SetRow::Kind::Share;
			r.key = "share." + s.name;
			r.name = s.name;
			r.desc = ogbox::shareAddress(s) + (!s.nfs && !s.user.empty() ? " \xC2\xB7 as " + s.user : std::string());
			r.icon = ui::Icon::Server;
			const auto it = shareTests.find(s.mount);
			if (s.mount.empty()) {
				r.value = "not connected";
				r.dot = ui::P().warn;
			} else if (it == shareTests.end()) {
				r.value = "connected";
				r.dot = ui::P().ok;
			} else {
				std::lock_guard<std::mutex> lock(it->second->mutex);
				if (!it->second->done) {
					r.value = "connecting\xE2\x80\xA6";
				} else if (it->second->ok) {
					r.value = "connected \xC2\xB7 " + std::to_string(it->second->items) + " items";
					r.dot = ui::P().ok;
				} else {
					r.value = "not reachable";
					r.dot = ui::P().warn;
				}
			}
			rows.push_back(r);
			RowAct a;
			a.press = [this, i]() { openShareForm(static_cast<int>(i)); };
			a.square = [this, i]() {
				ogbox::Share &sh = shares.shares[i];
				if (sh.mount.empty()) {
					std::string why;
					if (!ogbox::mountShare(sh, shares, mountsTaken(), &why)) {
						showToast(sh.name + ": " + why);
						return;
					}
				}
				testShare(sh.mount);
			};
			a.reset = [this, i]() {
				const std::string name = shares.shares[i].name;
				openPopup("Remove " + name + "?", "It goes from the list and from " + std::string(kNetworkFile) + "; the files on the "
					"server stay.", { "No", "Yes, remove it" }, [this, i](int k) {
						if (k == 1 && i < shares.shares.size()) {
							ogbox::unmountShare(shares.shares[i]);
							const std::string name2 = shares.shares[i].name;
							shares.shares.erase(shares.shares.begin() + static_cast<long>(i));
							saveShares();
							showToast(name2 + " removed");
							setRowSel = std::max(0, setRowSel - 1);
						}
					});
			};
			acts.push_back(a);
		}
		actionRow("addshare", "Add a network share", "SMB or NFS", "Games on a NAS or a PC: they show in the library's Network tab. "
			"Saved to " + std::string(kNetworkFile) + ".", [this]() { openShareForm(-1); }, rows, acts, ui::Icon::Plus);
	}

	void saveShares()
	{
		std::string why;
		if (!ogbox::writeShares(kNetworkFile, shares, &why)) {
			showToast(std::string(kNetworkFile) + " cannot be written: " + why);
			pshakux_log("settings: %s cannot be written: %s", kNetworkFile, why.c_str());
		} else {
			pshakux_log("settings: %s: %zu shares", kNetworkFile, shares.shares.size());
		}
		placesRead = -100.0;
		if (library && folder.empty() && !listing) {
			openFolder(folder);
		}
	}

	void openShareForm(int index)
	{
		readSharesOnce();
		form = ShareForm{};
		form.open = true;
		form.index = index;
		form.since = now_seconds();
		{
			auto found = std::make_shared<ShareForm::Found>();
			form.found = found;
			std::thread([found]() {
				pshakux_thread_named("ogbox-servers");
				std::vector<ogbox::FoundServer> list = ogbox::findServers();
				pshakux_log("settings: %zu SMB servers on the network", list.size());
				for (const auto &f : list) {
					pshakux_log("settings:   %s %s", f.ip.c_str(), f.name.c_str());
				}
				std::lock_guard<std::mutex> lock(found->mutex);
				found->servers = std::move(list);
				found->done = true;
			}).detach();
		}
		if (index >= 0 && index < static_cast<int>(shares.shares.size())) {
			form.s = shares.shares[static_cast<size_t>(index)];
		}
		pshakux_log("settings: %s a network share", index < 0 ? "adding" : "editing");
	}

	enum FormRow { FR_NAME, FR_PROTOCOL, FR_HOST, FR_PATH, FR_USER, FR_PASSWORD, FR_SAVE, FR_SAVE_ANYWAY };

	std::vector<int> formRows() const
	{
		std::vector<int> r = { FR_NAME, FR_PROTOCOL, FR_HOST, FR_PATH };
		if (!form.s.nfs) {
			r.push_back(FR_USER);
			r.push_back(FR_PASSWORD);
		}
		r.push_back(FR_SAVE);
		if (form.bad) {
			r.push_back(FR_SAVE_ANYWAY);
		}
		return r;
	}

	void formInput(uint32_t p)
	{
		if (form.test) {
			return;   // connecting: wait
		}
		const std::vector<int> rows = formRows();
		const int n = static_cast<int>(rows.size());
		form.sel = std::max(0, std::min(n - 1, form.sel));
		if (p & PAD_UP) {
			form.sel = std::max(0, form.sel - 1);
		}
		if (p & PAD_DOWN) {
			form.sel = std::min(n - 1, form.sel + 1);
		}
		if (p & PAD_CIRCLE) {
			form.open = false;
			return;
		}
		const int row = rows[static_cast<size_t>(form.sel)];
		if (row == FR_PROTOCOL && (p & (PAD_LEFT | PAD_RIGHT | PAD_CROSS))) {
			form.s.nfs = (p & PAD_LEFT) ? false : (p & PAD_RIGHT) ? true : !form.s.nfs;
			return;
		}
		if (!(p & PAD_CROSS)) {
			if ((p & PAD_TRIANGLE) && (row == FR_NAME || row == FR_HOST || row == FR_PATH || row == FR_USER || row == FR_PASSWORD)) {
				formField(row) = std::string();
			}
			return;
		}
		switch (row) {
		case FR_NAME:
			kb.open("Name", form.s.name, ogbox::shareDefaultName(form.s), ui::Keyboard::Kind::Text, false,
				[this](bool ok, const std::string &v) { if (ok) { form.s.name = v; } });
			break;
		case FR_HOST: {
			/* 0.0.3: the servers found on the network to choose from, else typed */
			const auto typed = [this]() {
				kb.open("Server", form.s.host, "192.168.1.10 or a name", ui::Keyboard::Kind::Address, false,
					[this](bool ok, const std::string &v) { if (ok) { form.s.host = v; } });
			};
			std::vector<ogbox::FoundServer> list;
			bool done = false;
			if (form.found) {
				std::lock_guard<std::mutex> lock(form.found->mutex);
				done = form.found->done;
				list = form.found->servers;
			}
			if (form.s.nfs || list.empty()) {
				if (!form.s.nfs && !done) {
					form.status = "Still looking for servers on the network\xE2\x80\xA6 type one, or try again in a moment.";
				}
				typed();
				break;
			}
			std::vector<std::string> items;
			for (const auto &f : list) {
				items.push_back(f.name.empty() ? f.ip : f.name + "   " + f.ip);
			}
			items.push_back("Type an address");
			openPopup("Servers on the network", "These answer on the SMB port. Choose one, or type an address.", items,
				[this, list, typed](int i) {
					if (i >= 0 && i < static_cast<int>(list.size())) {
						form.s.host = list[static_cast<size_t>(i)].ip;
					} else if (i == static_cast<int>(list.size())) {
						typed();
					}
				});
			break;
		}
		case FR_PATH:
			kb.open(form.s.nfs ? "Export and folder" : "Share and folder", form.s.path, form.s.nfs ? "/volume1/games/xbox" : "games/xbox",
				ui::Keyboard::Kind::Address, false, [this](bool ok, const std::string &v) { if (ok) { form.s.path = v; } });
			break;
		case FR_USER:
			kb.open("User", form.s.user, "empty: a guest share", ui::Keyboard::Kind::Text, false,
				[this](bool ok, const std::string &v) { if (ok) { form.s.user = v; } });
			break;
		case FR_PASSWORD:
			kb.open("Password", form.s.password, "", ui::Keyboard::Kind::Text, true,
				[this](bool ok, const std::string &v) { if (ok) { form.s.password = v; } });
			break;
		case FR_SAVE:
			formSave(true);
			break;
		case FR_SAVE_ANYWAY:
			formSave(false);
			break;
		default:
			break;
		}
	}

	std::string &formField(int row)
	{
		switch (row) {
		case FR_NAME: return form.s.name;
		case FR_HOST: return form.s.host;
		case FR_PATH: return form.s.path;
		case FR_USER: return form.s.user;
		default: return form.s.password;
		}
	}

	/* Mounts the form's share (its old mount let go) and lists it; saved once it answers, or at
	 * once without connecting. */
	void formSave(bool connect)
	{
		ogbox::Share &s = form.s;
		const auto strip = [](std::string &t) {
			while (!t.empty() && (t.back() == ' ' || t.back() == '/')) {
				t.pop_back();
			}
			while (!t.empty() && t[0] == ' ') {
				t.erase(0, 1);
			}
		};
		strip(s.host);
		if (s.host.rfind("smb://", 0) == 0 || s.host.rfind("nfs://", 0) == 0) {
			s.host.erase(0, 6);
		}
		if (!s.nfs) {
			while (!s.path.empty() && s.path[0] == '/') {
				s.path.erase(0, 1);
			}
		} else if (!s.path.empty() && s.path[0] != '/') {
			s.path = "/" + s.path;
		}
		if (s.host.empty() || s.path.empty()) {
			form.status = s.host.empty() ? "The server is missing." : s.nfs ? "The export and folder are missing." : "The share and folder are missing.";
			form.bad = false;
			return;
		}
		if (s.name.empty()) {
			s.name = ogbox::shareDefaultName(s);
		}
		if (form.index >= 0 && form.index < static_cast<int>(shares.shares.size())) {
			ogbox::unmountShare(shares.shares[static_cast<size_t>(form.index)]);
		}
		s.mount.clear();
		std::string why;
		if (!ogbox::mountShare(s, shares, mountsTaken(), &why)) {
			form.status = "Not added: " + why + ".";
			form.bad = true;
			return;
		}
		if (!connect) {
			formCommit();
			return;
		}
		form.status = "Connecting to " + s.host + "\xE2\x80\xA6";
		form.bad = false;
		form.test = testShare(s.mount);
	}

	void formCommit()
	{
		const std::string name = form.s.name;
		if (form.index >= 0 && form.index < static_cast<int>(shares.shares.size())) {
			shares.shares[static_cast<size_t>(form.index)] = form.s;
		} else {
			shares.shares.push_back(form.s);
		}
		form.open = false;
		saveShares();
		g_networkStatus = std::to_string(shares.shares.size()) + " share(s) from " + kNetworkFile;
		showToast(name + " saved: it's in the library's Network tab");
	}

	/* Each frame while connecting: the test's answer. */
	void formTick()
	{
		if (!form.test) {
			return;
		}
		bool done, ok;
		int items;
		std::string error;
		{
			std::lock_guard<std::mutex> lock(form.test->mutex);
			done = form.test->done;
			ok = form.test->ok;
			items = form.test->items;
			error = form.test->error;
		}
		if (!done) {
			return;
		}
		form.test.reset();
		if (ok) {
			pshakux_log("settings: share %s answers (%d items)", form.s.mount.c_str(), items);
			formCommit();
		} else {
			form.status = "No answer: " + error + ". Check the server, the share and the account, or save it as it is.";
			form.bad = true;
			ogbox::unmountShare(form.s);
		}
	}

	void drawShareForm(ImDrawList *dl, ImVec2 S)
	{
		formTick();
		if (!form.open) {
			return;
		}
		ui::FormView f;
		f.title = form.index < 0 ? "Add a network share" : "Edit " + form.s.name;
		const std::vector<int> rows = formRows();
		for (const int r : rows) {
			ui::SetRow row;
			row.key = "f" + std::to_string(r);
			switch (r) {
			case FR_NAME:
				row.kind = ui::SetRow::Kind::Text;
				row.name = "Name";
				row.value = form.s.name;
				row.placeholder = ogbox::shareDefaultName(form.s).empty() ? "how the library shows it" : ogbox::shareDefaultName(form.s);
				break;
			case FR_PROTOCOL:
				row.name = "Protocol";
				row.choices = { "SMB", "NFS" };
				row.choice = form.s.nfs ? 1 : 0;
				break;
			case FR_HOST:
				row.kind = ui::SetRow::Kind::Text;
				row.name = "Server";
				row.value = form.s.host;
				row.placeholder = "192.168.1.10";
				break;
			case FR_PATH:
				row.kind = ui::SetRow::Kind::Text;
				row.name = form.s.nfs ? "Export and folder" : "Share and folder";
				row.value = form.s.path;
				row.placeholder = form.s.nfs ? "/volume1/games/xbox" : "games/xbox";
				break;
			case FR_USER:
				row.kind = ui::SetRow::Kind::Text;
				row.name = "User";
				row.value = form.s.user;
				row.placeholder = "empty: a guest share";
				break;
			case FR_PASSWORD:
				row.kind = ui::SetRow::Kind::Text;
				row.name = "Password";
				row.value = form.s.password;
				row.password = true;
				row.placeholder = "stays on the console";
				break;
			case FR_SAVE:
				row.kind = ui::SetRow::Kind::Action;
				row.name = form.bad ? "Try again" : "Connect and save";
				row.icon = ui::Icon::Check;
				break;
			default:
				row.kind = ui::SetRow::Kind::Action;
				row.name = "Save without connecting";
				row.warn = true;
				break;
			}
			f.rows.push_back(row);
		}
		f.sel = form.sel;
		f.status = form.status;
		f.statusBad = form.bad;
		f.busy = form.test != nullptr;
		f.note = "Saved to " + std::string(kNetworkFile) + "; the password stays on the console. X types, TRIANGLE clears, CIRCLE cancels.";
		const float appear = static_cast<float>(std::min(1.0, (now_seconds() - form.since) / 0.25));
		ui::drawForm(dl, S, f, appear);
	}

	// ---- the game page ----------------------------------------------------------------------------

	enum GameAct { GA_PLAY, GA_FRESH, GA_STATES, GA_SETTINGS, GA_MODS, GA_SAVES };

	/* 0.0.6: whether the game has saves on the Xbox hard disk (E:\UDATA\<title ID>), looked at once a page */
	bool gameHasSaves()
	{
		if (!gp.info.ok) {
			return false;
		}
		if (gp.savesKnown != gp.path) {
			gp.savesKnown = gp.path;
			char id[16];
			std::snprintf(id, sizeof(id), "%08x", static_cast<unsigned>(gp.info.title_id));
			const std::string hddRoot = boot.hdd.empty() ? ogbox::storagePath("hdd") : boot.hdd;
			gp.hasSaves = false;
			if (DIR *d = opendir((hddRoot + "/Partition1/UDATA/" + id).c_str())) {
				while (struct dirent *e = readdir(d)) {
					if (e->d_name[0] != '.' && std::strcmp(e->d_name, "TitleMeta.xbx") != 0 && std::strcmp(e->d_name, "TitleImage.xbx") != 0) {
						gp.hasSaves = true;
						break;
					}
				}
				closedir(d);
			}
		}
		return gp.hasSaves;
	}

	std::vector<int> gameActs() const
	{
		std::vector<int> a = { GA_PLAY };
		if (gp.info.ok && ogbox::stateSlot(gp.info.key, ogbox::kQuitSlot).used) {
			a.push_back(GA_FRESH);
		}
		if (gp.info.ok) {
			a.push_back(GA_STATES);   // 0.0.6: Load State always (it says when there is none)
			a.push_back(GA_SETTINGS);
			a.push_back(GA_MODS);
			a.push_back(GA_SAVES);
		}
		return a;
	}

	void gamePageInputNew(uint32_t p)
	{
		gamePageInfo();
		if (gp.states) {
			const std::vector<int> slots = usedStateSlots(gp.info.key);
			const int n = static_cast<int>(slots.size());
			if ((p & PAD_LEFT) && n) {
				gp.stateSel = std::max(0, gp.stateSel - 1);
			}
			if ((p & PAD_RIGHT) && n) {
				gp.stateSel = std::min(n - 1, gp.stateSel + 1);
			}
			if (p & (PAD_CIRCLE | PAD_UP)) {
				gp.states = false;
			} else if ((p & PAD_CROSS) && n && startPath.empty()) {
				const ogbox::StateSlot st = ogbox::stateSlot(gp.info.key, slots[static_cast<size_t>(std::min(gp.stateSel, n - 1))]);
				startState = st.path;
				startPath = gp.path;
				startDelay = 2;
				gp.open = false;
				pshakux_log("library: starting %s with the state %s", gp.path.c_str(), st.path.c_str());
			}
			return;
		}
		const std::vector<int> acts = gameActs();
		const int n = static_cast<int>(acts.size());
		gp.pill = std::max(0, std::min(n - 1, gp.pill));
		if (p & PAD_LEFT) {
			gp.pill = std::max(0, gp.pill - 1);
		}
		if (p & PAD_RIGHT) {
			gp.pill = std::min(n - 1, gp.pill + 1);
		}
		/* DOWN: the save states under the buttons, when there are */
		if ((p & PAD_DOWN) && gp.info.ok && !usedStateSlots(gp.info.key).empty()) {
			gp.states = true;
			gp.stateSel = 0;
			return;
		}
		if (p & (PAD_CIRCLE | PAD_TRIANGLE)) {
			gp.open = false;
			return;
		}
		if (!(p & PAD_CROSS)) {
			return;
		}
		const std::string title = gp.info.ok && gp.info.title[0] ? std::string(gp.info.title) : gp.title;
		switch (acts[static_cast<size_t>(gp.pill)]) {
		case GA_PLAY:
			startGame(gp.path, gp.info.ok ? std::string(gp.info.key) : std::string(), 1);
			break;
		case GA_FRESH:
			startGame(gp.path, gp.info.key, 2);
			break;
		case GA_STATES:
			if (usedStateSlots(gp.info.key).empty()) {
				showToast("No save state for this game yet: the quick menu's Save State makes one");
				break;
			}
			gp.states = true;
			gp.stateSel = 0;
			break;
		case GA_SETTINGS:
			gp.open = false;
			gp.returnTo = true;
			openSettings(gp.info.key, title);
			break;
		case GA_MODS:
			gp.open = false;
			gp.returnTo = true;
			openMods(gp.info.key, title, gp.path);
			break;
		case GA_SAVES:
			gp.open = false;
			gp.returnTo = true;
			openHdd(gp.info.title_id);
			break;
		}
	}

	void drawGamePageNew()
	{
		gamePageInfo();
		const ImVec2 S(static_cast<float>(width), static_cast<float>(height));
		ui::GameView v;
		const std::string base = baseName(gp.path);
		v.title = gp.info.ok && gp.info.title[0] ? std::string(gp.info.title) : gp.title;
		v.coverTitle = v.title;
		v.tint = ui::tintFor(gp.title);
		LibraryEntry fake{ LibraryEntry::Image, base, gp.path, 0, std::string() };
		v.cover = uiCover(coverNames(fake, gp.info.ok ? &gp.info : nullptr));
		const ogbox::DiscRecord *rec = gp.info.ok ? ogbox::GameDb::get().byTitle(gp.info.title_id, base, gp.info.regions)
			: ogbox::GameDb::get().byFileName(base);
		if (rec && !rec->region.empty()) {
			v.chips.push_back({ rec->region, 0 });
		}
		if (rec && !rec->languages.empty()) {
			v.chips.push_back({ rec->languages, 0 });
		}
		int onCount = 0, own = 0, extras = 0;
		if (details && detailsPath == gp.path) {
			std::lock_guard<std::mutex> lock(details->mutex);
			onCount = details->modsOn + details->patchesOn + details->cheatsOn + (details->trainerOn ? 1 : 0);
			own = details->overrides;
			extras = details->mods + details->patches + details->cheats + details->trainers;
		}
		const double played = ogbox::playedAt(gp.path);
		v.facts.push_back({ "Last played", played > 0.0 ? ogbox::dateText(static_cast<int64_t>(played)) : std::string("never") });
		v.facts.push_back({ "Title ID", gp.info.ok ? std::string(gp.info.key)
			: gp.readTries >= 3 ? std::string("unreadable") : std::string("reading\xE2\x80\xA6") });
		v.facts.push_back({ "Serial", rec ? rec->serial : std::string("-") });
		v.facts.push_back({ "File", base });
		{   /* 0.0.3: the game's description (Wikipedia's), when kept; asked for the first time here */
			std::string text, article;
			if (prefs.global("list_covers") != "0" && ogbox::About::get().find(gp.title, text, article)) {
				v.about = text;
				v.aboutSource = "From Wikipedia" + (article.empty() ? std::string() : ", \"" + article + "\"") + " \xC2\xB7 CC BY-SA 4.0";
			}
		}
		const std::vector<int> slots = gp.info.ok ? usedStateSlots(gp.info.key) : std::vector<int>();
		for (size_t i = 0; i < slots.size(); i++) {
			const ogbox::StateSlot st = ogbox::stateSlot(gp.info.key, slots[i]);
			ui::GameState s;
			s.label = (st.slot == ogbox::kQuitSlot ? std::string("Where I left off") : "Slot " + std::to_string(st.slot)) + " \xC2\xB7 " +
				ogbox::dateText(st.mtime);
			float tw = 0.0f, th = 0.0f;
			const ImTextureID img = fileImage(st.thumb, st.thumb + "#" + std::to_string(st.mtime), &tw, &th);
			if (img) {
				s.picture = ui::Tex{ img, tw, th };
			}
			s.left = st.slot == ogbox::kQuitSlot;
			v.states.push_back(s);
		}
		v.stateSel = gp.states ? gp.stateSel : -1;
		const std::vector<int> acts = gameActs();
		const ogbox::StateSlot left = gp.info.ok ? ogbox::stateSlot(gp.info.key, ogbox::kQuitSlot) : ogbox::StateSlot();
		for (const int a : acts) {
			switch (a) {
			case GA_PLAY:
				v.actions.push_back({ left.used ? "Continue" : "Play", left.used ? "Where you left off, " + ogbox::dateText(left.mtime) : "",
					ui::Icon::Play });
				break;
			case GA_FRESH: v.actions.push_back({ "From the start", "Start the game afresh; where you left off is kept", ui::Icon::Restart }); break;
			case GA_STATES:
				v.actions.push_back({ "Load State", slots.empty() ? std::string("None saved yet")
					: std::to_string(slots.size()) + " saved: choose one to start from it", ui::Icon::Load });
				break;
			case GA_SETTINGS:
				v.actions.push_back({ "Settings", own ? std::to_string(own) + " of its own" : "Default", ui::Icon::Gear });
				break;
			case GA_MODS:
				v.actions.push_back({ "Game Patches", extras ? std::to_string(extras) + " patches, mods, cheats and trainers for it" +
					(onCount ? ", " + std::to_string(onCount) + " on" : std::string()) : std::string("None known for it"), ui::Icon::Puzzle });
				break;
			case GA_SAVES:
				/* 0.0.6: Import Saves while it has none on the Xbox hard disk */
				v.actions.push_back({ gameHasSaves() ? "Saves" : "Import Saves", "", ui::Icon::Save });
				break;
			}
		}
		v.actionSel = gp.pill;
		v.footer = gp.states ? std::vector<ui::Hint>{ { "LR", "Choose" }, { "X", "Load" }, { "O", "Back" } }
			: std::vector<ui::Hint>{ { "LR", "Choose" }, { "X", "Open" }, { "O", "Back" } };
		if (!gp.states && !slots.empty()) {
			v.footer.insert(v.footer.begin() + 2, { "D", "Load State" });
		}
		const float appear = static_cast<float>(std::min(1.0, (now_seconds() - gp.since) / 0.25));
		ImDrawList *dl = beginUiScreen("##gamepage");
		ui::drawGamePage(dl, S, v, appear);
		endUiScreen();
	}

	// ---- popups, the toast and the keyboard, over everything ----------------------------------------

	void drawOverlays()
	{
		ImDrawList *dl = ImGui::GetOverlayDrawList();
		const ImVec2 S(static_cast<float>(width), static_cast<float>(height));
		static double popSince = 0.0;
		static bool popWas = false;
		if (pop.open && !popWas) {
			popSince = now_seconds();
		}
		popWas = pop.open;
		if (pop.open) {
			ui::PopupView v;
			v.title = pop.title;
			v.text = pop.text;
			v.items = pop.items;
			v.sel = pop.sel;
			ui::drawPopup(dl, S, v, static_cast<float>(std::min(1.0, (now_seconds() - popSince) / 0.2)));
		}
		if (kb.isOpen()) {
			kb.draw(dl, S, PS5Keyboard::IsConnected());
		}
		if (!toast.empty() && now_seconds() < toastUntil) {
			ui::drawToast(dl, S, toast, static_cast<float>(std::min(1.0, (toastUntil - now_seconds()) / 0.4)));
		}
	}

	/* 0.0.3: the look (Settings > Interface > Theme): Dark, or Code (black and green, the falling code
	 * behind every screen). 0.1.52's Emerald is gone (shown as Dark). */
	void applyTheme(bool code)
	{
		themeApplied = code ? 1 : 0;
		ui::setTheme(code ? ui::Theme::Code : ui::Theme::Dark);
		pshakux_log("ogbox: theme %s", code ? "Code" : "Dark");
	}

	/* OGBox's logo (Press5elect's, assets/ogbox-logo.png) as an image ImGui draws, once. */
	ImTextureID logoImage()
	{
		if (!logoTried) {
			logoTried = true;
			/* 0.0.6: read again from its file: the boot screen's copy is let go once it is shown (the
			 * menus that came after it had no logo) */
			const std::string path = getAssetPath() + "ogbox-logo.png";
			int w = 0, h = 0, n = 0;
			stbi_uc *pixels = stbi_load(path.c_str(), &w, &h, &n, STBI_rgb_alpha);
			if (pixels && w > 0 && h > 0) {
				logoWidth = static_cast<uint32_t>(w);
				logoHeight = static_cast<uint32_t>(h);
				logoTex.fromBuffer(pixels, static_cast<VkDeviceSize>(size_t(w) * size_t(h) * 4), VK_FORMAT_R8G8B8A8_UNORM,
					logoWidth, logoHeight, vulkanDevice, queue);
				logoSet = ui.addTexture(logoTex.sampler, logoTex.view);
			} else {
				pshakux_log("ui: no logo for the menus (%s: %s)", path.c_str(), pixels ? "empty" : stbi_failure_reason());
			}
			if (pixels) {
				stbi_image_free(pixels);
			}
		}
		return logoSet ? reinterpret_cast<ImTextureID>(logoSet) : nullptr;
	}
};

VULKAN_EXAMPLE_MAIN()
