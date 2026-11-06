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
#include "ogbox_library.h"
#include "ogbox_storage.h"
#include "ogbox_saves.h"
#include "ogbox_ui.h"
#include "VulkanTexture.h"
#include "third_party/smaa/AreaTex.h"     // SMAA (MIT): its area and search textures
#include "third_party/smaa/SearchTex.h"

#include "stb_image.h"   // (its implementation is the base class's: base/VulkanglTFModel.cpp)

extern "C" int pshakux_props_load(const char *path);
extern "C" int pshakux_prop_set(const char *name, const char *value);

namespace {

constexpr const char *kBootFile = "/data/homebrew/PPSA99251/boot.txt";
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
			if (!haveImage) {
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

/* A stick's raw reading (0..255, 128 at rest, y down) as the Xbox controller's (y up). */
int16_t stickAxis(uint8_t value, bool invert)
{
	int v = (static_cast<int>(value) - 128) * 256;
	if (invert) {
		v = -v;
	}
	return static_cast<int16_t>(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

void toXboxPad(const struct pad_reading &r, PshxPad &x)
{
	x = PshxPad{};
	const uint32_t b = r.buttons;
	const auto analog = [&](int index, uint32_t button) { x.analog[index] = (b & button) ? 0xFF : 0; };
	analog(PSHX_A, PAD_CROSS);
	analog(PSHX_B, PAD_CIRCLE);
	analog(PSHX_X, PAD_SQUARE);
	analog(PSHX_Y, PAD_TRIANGLE);
	analog(PSHX_BLACK, PAD_R1);
	analog(PSHX_WHITE, PAD_L1);
	x.analog[PSHX_LT] = r.l2;
	x.analog[PSHX_RT] = r.r2;
	if (b & PAD_UP) x.buttons |= PSHX_PAD_UP;
	if (b & PAD_DOWN) x.buttons |= PSHX_PAD_DOWN;
	if (b & PAD_LEFT) x.buttons |= PSHX_PAD_LEFT;
	if (b & PAD_RIGHT) x.buttons |= PSHX_PAD_RIGHT;
	if (b & PAD_OPTIONS) x.buttons |= PSHX_PAD_START;
	if (b & PAD_TOUCH_PAD) x.buttons |= PSHX_PAD_BACK;
	if (b & PAD_L3) x.buttons |= PSHX_PAD_LTHUMB;
	if (b & PAD_R3) x.buttons |= PSHX_PAD_RTHUMB;
	x.lx = stickAxis(r.left_x, false);
	x.ly = stickAxis(r.left_y, true);
	x.rx = stickAxis(r.right_x, false);
	x.ry = stickAxis(r.right_y, true);
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

// ---- OGBox 0.1.26: the look (black and green, "the code"), in the overlay's hooks ------------

namespace mx {
/* 0.1.52: the theme's (applyTheme): Code's below, Emerald's in applyTheme */
ImVec4 bright(0.0f, 1.0f, 0.255f, 1.0f);    // #00ff41: titles, values, the selection
ImVec4 text(0.49f, 1.0f, 0.61f, 1.0f);      // #7dff9c: the body
ImVec4 label(0.24f, 0.78f, 0.40f, 1.0f);    // #3dc766: labels, hints
ImVec4 line(0.05f, 0.36f, 0.13f, 1.0f);     // #0d5c22: borders
ImVec4 panel(0.0f, 0.03f, 0.01f, 0.86f);    // the panels over the rain
ImVec4 head(0.71f, 1.0f, 0.78f, 1.0f);      // #b6ffc8: a drop's head
ImVec4 fail(1.0f, 0.45f, 0.38f, 1.0f);
bool emerald{ false };                       // the Emerald theme is on
inline ImU32 u32(const ImVec4 &c, float alpha = 1.0f)
{
	return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}
} // namespace mx

/* The overlay's colours and typeface: src/ogbox_theme.cpp (at file scope, where the base class
 * looks for them; this file is compiled inside a namespace, src/wrap.cpp.in). */

class VulkanExample : public VulkanExampleBase
{
public:
	BootConfig boot;
	bool configured{ false };
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
	bool focusSet{ false };
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
	std::string help;
	bool focusList{ false };
	/* 0.1.29: LEFT and RIGHT between the screens' columns when ImGui's own move finds nothing.
	 * ImGui 1.65 moves sideways only to an item level with the focused one, so the first settings
	 * section (above the options, which start under their heading), the last sections (below a
	 * short list) and a game-list row above the selected disc's buttons went nowhere. A press that
	 * left the focus where it was two frames later puts it on the next column's first item
	 * (columnItem); found with a headless ImGui test of the settings screen. In the settings LEFT
	 * always goes back to the current section. */
	struct ColumnNav {
		int focusCol{ -1 };       // the column of the item focused this frame
		int lastCol{ -1 };        // ... the frame before
		int pressCol{ -1 }, pressDir{ 0 }, wait{ 0 };
		ImGuiID pressId{ 0 };
		int jump{ -1 };           // the column whose first item takes the focus
	} colNav;
	bool wantFocus{ true };                   // the next screen's window takes the controller
	bool modsFocus{ false };
	bool swallowNav{ false };                 // this frame's CROSS and CIRCLE are not ImGui's
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
	bool storageFocus{ false };
	// 0.1.52: the theme (Settings > Interface > Theme), applied at the next frame's start (0.0.3: Dark or Code)
	bool themeCode{ false };
	int themeApplied{ -1 };
	vks::Texture2D logoTex{};                 // OGBox's logo for the Emerald theme's head
	VkDescriptorSet logoSet{ VK_NULL_HANDLE };
	bool logoTried{ false };
	struct WebPoint {
		float x, y;
	};
	std::vector<WebPoint> web;                // the Emerald backdrop's faint web
	float webW{ 0.0f }, webH{ 0.0f };
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
	VkPipeline pipelineFxaa{ VK_NULL_HANDLE }, pipelineSgsr{ VK_NULL_HANDLE };
	std::string aspectMode{ "auto" };
	bool fxaaOn{ false }, sgsrOn{ false }, showFps{ true }, rainOn{ true };
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
			focusSet = false;   // drawLibrary focuses and scrolls to it
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
		focusSet = false;
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
			} else if (tab == TabNetwork) {
				entries.push_back({ LibraryEntry::AddShare, "Add a network share", std::string(), 0, std::string() });
			}
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
		if (listing->ok) {
			entries = { { LibraryEntry::Up, "..", parentOf(folder), 0, std::string() } };
			entries.insert(entries.end(), listing->entries.begin(), listing->entries.end());
			if (prefs.global("list_sort") == "played") {
				// 0.1.45: the discs played last first (folders stay first, by name)
				std::stable_sort(entries.begin() + 1, entries.end(), [](const LibraryEntry &a, const LibraryEntry &b) {
					if (a.kind != b.kind) {
						return a.kind == LibraryEntry::Folder;
					}
					return a.kind == LibraryEntry::Image && ogbox::playedAt(a.path) > ogbox::playedAt(b.path);
				});
			}
			writeLastFolder(folder);
			pshakux_log("library: %s: %zu entries", folder.c_str(), entries.size() - 1);
		} else {
			listError = listing->error;
			{
				std::lock_guard<std::mutex> lock(g_vfsErrorMutex);
				if (!g_vfsError.empty() && ps5vfs_is_virtual(folder.c_str())) {
					listError += " - " + g_vfsError;
				}
			}
			entries = { { LibraryEntry::Up, "..", parentOf(folder), 0, std::string() } };
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
				wantFocus = true;
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
			if (!boot.back) {
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
		sgsrOn = prefs.value("upscale") == "sgsr";
		showFps = prefs.on("show_fps");
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
			sgsrOn ? "SGSR" : "bilinear", showFps ? "on" : "off");
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
		destroyPicture();
		for (auto &s : staging) {
			destroyStaging(s);
		}
		vkDestroySampler(device, sampler, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipeline(device, pipelineFxaa, nullptr);
		vkDestroyPipeline(device, pipelineSgsr, nullptr);
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
		/* 0.1.26: the settings' FXAA and Snapdragon GSR (hakuX's presenter's shaders,
		 * shaders/glsl/ogbox/fxaa.frag and sgsr.frag): the same pipeline, another fragment stage */
		const struct {
			const char *file;
			VkPipeline *out;
		} variants[] = { { "ogbox/fxaa.frag.spv", &pipelineFxaa }, { "ogbox/sgsr.frag.spv", &pipelineSgsr },
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
		if (smaaValid && !upload) {
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
		upload = true;
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
				} else if (ssaaOn && larger && pipelineSsaa != VK_NULL_HANDLE) {
					chosen = pipelineSsaa;   // averaged down over every texel a pixel covers
				}
			}
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, chosen);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr);
			if (chosen == pipelineSsaa) {
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
		gameMenuControls();
		const bool menu = qm.open || qm.sub != Quick::Sub::None;
		const bool holdMode = prefs.global("quick_menu") == "options_hold";
		for (int player = 0; player < PAD_PLAYERS; player++) {
			const struct pad_reading *readings = nullptr;
			const int count = pad_player_readings(player, &readings);
			if (count <= 0) {
				continue;
			}
			// the newest reading the shell did not have
			for (int i = count - 1; i >= 0; i--) {
				if (readings[i].connected && !(readings[i].buttons & PAD_INTERCEPTED)) {
					PshxPad x;
					toXboxPad(readings[i], x);
					x.buttons &= static_cast<uint16_t>(~(PSHX_PAD_START | PSHX_PAD_BACK));
					/* the touch pad's click: which half is decided when it goes down. OPTIONS mode:
					 * left half Back, right half Start; OPTIONS held mode: the whole pad Back */
					const bool touch = (readings[i].buttons & PAD_TOUCH_PAD) != 0;
					bool &isBack = touchBack[player];
					if (touch && !touchWas[player]) {
						isBack = holdMode || readings[i].touch_x < 0 || readings[i].touch_x < 960;
					}
					touchWas[player] = touch;
					if (touch) {
						x.buttons |= isBack ? PSHX_PAD_BACK : PSHX_PAD_START;
					}
					/* OPTIONS: the quick menu's for player 1; the others' Start */
					if ((readings[i].buttons & PAD_OPTIONS) && player != 0) {
						x.buttons |= PSHX_PAD_START;
					}
					if (player == 0 && now_seconds() < qm.startPulseUntil) {
						x.buttons |= PSHX_PAD_START;   // a short OPTIONS press in the held mode
					}
					if (menu) {
						x = PshxPad{};
					}
					pshx_core_set_pad(static_cast<unsigned>(player), &x);
					break;
				}
			}
			float large = 0.0f, small = 0.0f;
			pshx_core_rumble(static_cast<unsigned>(player), &large, &small);
			pad_player_vibrate(player, menu ? 0.0f : large, menu ? 0.0f : small);
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

	/* 0.1.26: the menus' own buttons (the D-pad and CROSS go to the widgets through ImGui's
	 * navigation): CIRCLE back (the folder above, or out of a screen), TRIANGLE the settings from
	 * the game list; OPTIONS and the touch pad held, quit. */
	void libraryControls()
	{
		pad_player(0, &menuPad);
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
				focusSet = false;
				wantFocus = true;
				swallowNav = true;
				pshakux_log("ogbox: the experimental build's notice read");
			}
			return;
		}
		if (screen == Screen::Boot) {
			/* CROSS or CIRCLE skips the splash (0.0.1: no longer said on it) */
			if ((pressed & (PAD_CROSS | PAD_CIRCLE)) || now_seconds() - screenSince > 3.2) {
				screen = Screen::Library;
				screenSince = now_seconds();
				focusSet = false;
				wantFocus = true;
				swallowNav = (pressed & (PAD_CROSS | PAD_CIRCLE)) != 0;   // the skip's press is not the list's
			}
		} else if (screen == Screen::Library) {
			homeInput(nav);   // 0.0.3
		} else if (screen == Screen::Settings) {
			settingsInput(nav);   // 0.0.3
		} else if (screen == Screen::Storage) {
			if (pressed & PAD_CIRCLE) {
				storageBack();
			}
		} else if (pressed & PAD_CIRCLE) {
			screen = Screen::Library;
			screenSince = now_seconds();
			focusSet = false;
			wantFocus = true;
			reopenGamePage();
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
		focusList = false;
		help.clear();
		setNavSel = 0;   // 0.0.3
		setRowSel = 0;
		setFocusNav = true;
		form.open = false;
		screen = Screen::Settings;
		screenSince = now_seconds();
		wantFocus = true;
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

	/* The Mods screen's TRAINERS: none, each for the game (one chosen), the chosen one's options. */
	void drawTrainerRows()
	{
		unsigned gen = 0, found = 0;
		const int state = ogbox_trainers_state(&gen, &found);
		if (state == 2 && gen != tv.generation) {
			loadTrainerView();   // a scan finished meanwhile
		}
		if (!modList.empty()) {
			ImGui::Spacing();
		}
		ImGui::TextColored(mx::label, "TRAINERS  (one at a time, run as the game starts)");
		bool chosenListed = false;
		for (const OgboxTrainer &t : tv.list) {
			chosenListed |= tv.chosen == t.rel;
		}
		if (tv.list.empty() && tv.chosen.empty()) {
			ImGui::PushTextWrapPos(0.0f);
			if (state != 2) {
				ImGui::TextColored(mx::text, "Reading the trainers folder...");
			} else if (!found) {
				ImGui::TextColored(mx::text, "No trainers yet. Copy the Xbox's .etm and .xbtf trainers into %s (subfolders are "
					"fine) or a USB drive's OGBox/trainers: each game lists its own here.", ogbox::storagePath("trainers").c_str());
			} else {
				ImGui::TextColored(mx::text, "None of the %u trainers in the folders is for this game (title ID %08X).", found,
					static_cast<unsigned>(tv.titleId));
			}
			ImGui::PopTextWrapPos();
			return;
		}
		if (row("tnone", "No trainer", "", false, tv.chosen.empty() ? mx::bright : mx::text, Icon::None, tv.chosen.empty() ? 2 : 1)) {
			chooseTrainer(std::string());
		}
		if (!modsFocus) {
			focusLastItem();
			modsFocus = true;
		}
		if (ImGui::IsItemFocused()) {
			help = "The game starts without a trainer.";
		}
		for (size_t i = 0; i < tv.list.size(); i++) {
			const OgboxTrainer &t = tv.list[i];
			const bool on = tv.chosen == t.rel;
			const bool other = tv.xbeTime && t.xbe_time && t.xbe_time != tv.xbeTime;
			/* 0.0.2: one for another release is greyed out and can't be chosen (one chosen from before
			 * can be put back to none: it doesn't run at the start anyway) */
			const std::string note = other ? std::string(on ? "another release: doesn't run" : "another release")
				: std::to_string(t.noptions) + (t.noptions == 1 ? " option" : " options");
			if (row("t" + std::to_string(i), t.name[0] ? t.name : baseName(t.rel), note, false,
					other ? (on ? mx::fail : mx::line) : on ? mx::bright : mx::text, Icon::None,
					other && !on ? 0 : on ? 2 : 1) && !(other && !on)) {
				chooseTrainer(on ? std::string() : std::string(t.rel));
			}
			if (ImGui::IsItemFocused()) {
				help = std::string(t.author[0] ? t.author : "A trainer") + ". " + (t.xbtf ? "XBTF" : "ETM") + ", " +
					std::to_string(t.noptions) + (t.noptions == 1 ? " option" : " options") + ": " + t.rel + ". ";
				if (other) {
					char d[256];   // 0.0.1: 96 cut the sentence short
					std::snprintf(d, sizeof(d), "It was made for another release of the game (its XBE dated %08X, this disc's "
						"%08X): it would find nothing to change, so it can't be chosen%s. ", static_cast<unsigned>(t.xbe_time),
						static_cast<unsigned>(tv.xbeTime), on ? ", and it doesn't run at the start" : "");
					help += d;
				}
				help += on ? "Chosen: CROSS again for none. Its options are below it."
					: other ? std::string() : "CROSS chooses it (one trainer at a time).";
			}
			if (on) {
				drawTrainerOptions();
			}
		}
		if (!tv.chosen.empty() && !chosenListed) {
			if (row("tgone", baseName(tv.chosen), "not in the folders now", false, mx::fail, Icon::None, 2)) {
				chooseTrainer(std::string());
			}
			if (ImGui::IsItemFocused()) {
				help = "The trainer chosen for this game (" + tv.chosen + ") isn't in the trainers folders any more: the game "
					"starts without it. CROSS: none.";
			}
			drawTrainerOptions();
		}
	}

	void drawTrainerOptions()
	{
		for (size_t k = 0; k < tv.options.size(); k++) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 1.6f);
			const bool o = k < tv.bits.size() && tv.bits[k] == '1';
			const std::string &name = tv.options[k];
			const bool igk = hasIgk(name);
			if (row("to" + std::to_string(k), name.empty() ? "(option " + std::to_string(k + 1) + ")" : name,
					igk ? "in-game keys" : "", false, o ? mx::bright : mx::text, Icon::None, o ? 2 : 1)) {
				if (tv.bits.size() < tv.options.size()) {
					tv.bits.resize(tv.options.size(), '0');
				}
				tv.bits[k] = o ? '0' : '1';
				ogbox_trainers_set_options(modsKey.c_str(), tv.bits.c_str());
				detailsPath.clear();
			}
			if (ImGui::IsItemFocused()) {
				help = "One of the trainer's options: on, it's set as the game starts. ";
				if (igk) {
					help += "In-game keys: the trainer also turns it on and off in the game with a button combination "
						"(its readme says which); the console's LED shows it, top right. ";
				}
				if (!tv.scroller.empty()) {
					help += "The trainer says: " + tv.scroller;
				}
			}
		}
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
		help.clear();
		wantFocus = true;
		modsFocus = false;
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
		/* 0.1.26: SQUARE, TRIANGLE, L1 and R1 are the menus' own (the previous choice, the
		 * settings, a bigger step), not ImGui's menu layer and window switching */
		{
			ImGuiIO &io = ImGui::GetIO();
			io.NavInputs[ImGuiNavInput_Menu] = 0.0f;
			io.NavInputs[ImGuiNavInput_Input] = 0.0f;
			io.NavInputs[ImGuiNavInput_FocusPrev] = 0.0f;
			io.NavInputs[ImGuiNavInput_FocusNext] = 0.0f;
			if ((!library && qm.sub == Quick::Sub::None) || pop.open || gp.open || screen == Screen::Hdd) {
				/* the game's pad; or a screen that reads the pad itself (0.1.53: the quick menu, a
				 * list of choices, the game page, the hard disk) */
				std::memset(io.NavInputs, 0, sizeof(io.NavInputs));
			}
			if (swallowNav) {
				io.NavInputs[ImGuiNavInput_Activate] = 0.0f;
				io.NavInputs[ImGuiNavInput_Cancel] = 0.0f;
				swallowNav = (menuPad.held & (PAD_CROSS | PAD_CIRCLE)) != 0;   // until let go
			}
		}
		if (library) {
			renderLibrary();
			return;
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

	/* The falling code, in a window's draw list (behind its widgets); none inside `hole`. */
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
				const ImU32 c = k == 0 ? mx::u32(mx::head, alpha * d.alpha) : mx::u32(mx::bright, alpha * d.alpha * fade * 0.85f);
				const char *g = &d.glyphs[static_cast<size_t>(k)];
				dl->AddText(font, glyph, ImVec2(d.x, y), c, g, g + 1);
			}
		}
	}

	/* A full-screen window for a menu screen: black, the rain behind. */
	bool beginScreen(const char *name, float rainAlpha, bool transparent = false, const ImVec4 &hole = ImVec4(0, 0, 0, 0))
	{
		const float pad = static_cast<float>(height) * 0.03f;
		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)), ImGuiCond_Always);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, transparent || !themeCode ? ImVec4(0, 0, 0, 0) : ImVec4(0, 0, 0, 1));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad * 1.4f, pad));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pad * 0.4f, pad * 0.3f));
		/* 0.1.43: back from a game, everything fades in from black over 0.8 s */
		float fade = 1.0f;
		if (fadeSince == -2.0) {
			fadeSince = now_seconds();
		}
		if (fadeSince >= 0.0) {
			const double t = (now_seconds() - fadeSince) / 0.8;
			fade = t >= 1.0 ? 1.0f : static_cast<float>(std::max(0.02, t * t * (3.0 - 2.0 * t)));
			if (t >= 1.0) {
				fadeSince = -1.0;
			}
		}
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
		if (wantFocus || (screen == Screen::Library && !focusSet)) {
			ImGui::SetNextWindowFocus();
			wantFocus = false;
		}
		const bool open = ImGui::Begin(name, nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoCollapse);
		if (!themeCode) {
			// 0.0.3: the screens' ground (ogbox_ui's), the boot screen's logo picture kept behind
			if (!transparent) {
				drawGround(ImGui::GetWindowDrawList(), 0.6f * fade);
			}
		} else {
			drawRain(ImGui::GetWindowDrawList(), rainAlpha, hole);
		}
		return open;
	}

	void endScreen()
	{
		ImGui::End();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
	}

	/* A title in large letters with a glow (drawn three times, the outer ones faint). */
	void glowTitle(const char *text, float scale)
	{
		ImGui::SetWindowFontScale(scale);
		const ImVec2 at = ImGui::GetCursorScreenPos();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		if (mx::emerald) {
			// 0.1.52: Emerald: a soft shadow under it
			const float d = ImGui::GetFontSize() * 0.05f;
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + d, at.y + d), mx::u32(ImVec4(0, 0, 0, 0.55f)), text);
			ImGui::TextColored(mx::bright, "%s", text);
			ImGui::SetWindowFontScale(1.0f);
			return;
		}
		const float spread = ImGui::GetFontSize() * 0.06f;
		for (int i = 1; i <= 2; i++) {
			const float d = spread * static_cast<float>(i);
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x - d, at.y), mx::u32(mx::bright, 0.18f), text);
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + d, at.y), mx::u32(mx::bright, 0.18f), text);
		}
		ImGui::TextColored(mx::bright, "%s", text);
		ImGui::SetWindowFontScale(1.0f);
	}

	/* A blinking cursor after the last item. */
	void cursor()
	{
		if (std::fmod(now_seconds(), 1.0) < 0.5) {
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::TextColored(mx::bright, "_");
		}
	}

	/* ---- 0.1.48: the DualSense's buttons, drawn (the typeface has no such symbols), each face
	 * button in the colour of the Xbox button it is: CROSS A green, CIRCLE B red, SQUARE X blue,
	 * TRIANGLE Y yellow. In text, {X} {O} {S} {T} {L1} {R1} {L2} {R2} {OPT} {TP} {DPAD} {L3} {R3}. */
	static ImVec4 xboxColor(char face)
	{
		switch (face) {
		case 'X': return ImVec4(0.36f, 0.76f, 0.12f, 1.0f);   // A
		case 'O': return ImVec4(0.92f, 0.20f, 0.18f, 1.0f);   // B
		case 'S': return ImVec4(0.18f, 0.49f, 0.92f, 1.0f);   // X
		case 'T': return ImVec4(0.96f, 0.77f, 0.10f, 1.0f);   // Y
		}
		return ImVec4(0.85f, 0.88f, 0.86f, 1.0f);
	}

	/* One button at x (its left), centred on y; its width. */
	float padGlyph(ImDrawList *dl, const std::string &name, float x, float cy, float size, float alpha = 1.0f, bool measure = false)
	{
		const float r = size * 0.5f;
		const ImU32 dark = mx::u32(ImVec4(0.05f, 0.06f, 0.06f, 1.0f), alpha);
		if (name == "X" || name == "O" || name == "S" || name == "T") {
			if (measure) {
				return size;
			}
			const ImVec2 c(x + r, cy);
			const ImU32 col = mx::u32(xboxColor(name[0]), alpha);
			dl->AddCircleFilled(c, r, dark, 32);
			dl->AddCircle(c, r - 1.0f, col, 32, std::max(1.5f, size * 0.07f));
			const float k = r * 0.46f, t = std::max(2.0f, size * 0.10f);
			if (name == "X") {
				dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col, t);
				dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), col, t);
			} else if (name == "O") {
				dl->AddCircle(c, k * 1.05f, col, 24, t);
			} else if (name == "S") {
				dl->AddRect(ImVec2(c.x - k * 0.9f, c.y - k * 0.9f), ImVec2(c.x + k * 0.9f, c.y + k * 0.9f), col, 0.0f, ImDrawCornerFlags_All, t);
			} else {
				dl->AddTriangle(ImVec2(c.x, c.y - k * 1.05f), ImVec2(c.x + k * 1.05f, c.y + k * 0.75f), ImVec2(c.x - k * 1.05f, c.y + k * 0.75f), col, t);
			}
			return size;
		}
		if (name == "DPAD") {
			if (measure) {
				return size;
			}
			const ImVec2 c(x + r, cy);
			const float a = r * 0.85f, b = r * 0.30f;
			const ImU32 col = mx::u32(ImVec4(0.85f, 0.88f, 0.86f, 1.0f), alpha);
			dl->AddRectFilled(ImVec2(c.x - b, c.y - a), ImVec2(c.x + b, c.y + a), col, b * 0.4f);
			dl->AddRectFilled(ImVec2(c.x - a, c.y - b), ImVec2(c.x + a, c.y + b), col, b * 0.4f);
			return size;
		}
		// the others: a pill with their name
		const char *text = name == "OPT" ? "OPTIONS" : name == "TP" ? "TOUCH PAD" : name.c_str();
		const float fs = size * 0.58f;
		const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
		const float w = ts.x + size * 0.6f;
		if (measure) {
			return w;
		}
		const ImVec2 p0(x, cy - r * 0.82f), p1(x + w, cy + r * 0.82f);
		dl->AddRectFilled(p0, p1, mx::u32(ImVec4(0.85f, 0.88f, 0.86f, 1.0f), alpha), r * 0.5f);
		dl->AddText(ImGui::GetFont(), fs, ImVec2(x + (w - ts.x) * 0.5f, cy - ts.y * 0.5f), dark, text);
		return w;
	}

	/* Text with {buttons} in it, from at (its top left); its width. */
	float richText(ImDrawList *dl, ImVec2 at, const std::string &text, ImU32 color, float alpha = 1.0f, bool measure = false)
	{
		const float size = ImGui::GetFontSize();
		const float lh = ImGui::GetTextLineHeight();
		float x = at.x;
		size_t i = 0;
		while (i < text.size()) {
			const size_t open = text.find('{', i);
			const size_t stop = open == std::string::npos ? text.size() : open;
			if (stop > i) {
				const char *b = text.c_str() + i, *e = text.c_str() + stop;
				if (!measure) {
					dl->AddText(ImGui::GetFont(), size, ImVec2(x, at.y), color, b, e);
				}
				x += ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, b, e).x;
			}
			if (open == std::string::npos) {
				break;
			}
			const size_t close = text.find('}', open);
			if (close == std::string::npos) {
				break;
			}
			x += padGlyph(dl, text.substr(open + 1, close - open - 1), x, at.y + lh * 0.5f, lh * 1.05f, alpha, measure) + size * 0.18f;
			i = close + 1;
		}
		return x - at.x;
	}

	/* richText at the cursor, as an item. */
	void richLine(const std::string &text, const ImVec4 &color)
	{
		const ImVec2 at = ImGui::GetCursorScreenPos();
		const float w = richText(ImGui::GetWindowDrawList(), at, text, mx::u32(color));
		ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
	}

	/* The hints at the foot of a screen, with the buttons drawn. */
	void footer(const std::string &hints, const std::string &second = std::string())
	{
		const float lines = second.empty() ? 1.0f : 2.0f;
		const float lh = ImGui::GetTextLineHeightWithSpacing() * 1.15f;
		ImGui::SetCursorPosY(static_cast<float>(height) - ImGui::GetStyle().WindowPadding.y - lh * lines);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const ImVec2 at = ImGui::GetCursorScreenPos();
		dl->AddLine(ImVec2(at.x, at.y - 6.0f), ImVec2(static_cast<float>(width) - at.x, at.y - 6.0f), mx::u32(mx::line), 2.0f);
		richText(dl, at, hints, mx::u32(mx::text));
		const char *sign = "OGBox by Press5elect";
		dl->AddText(ImVec2(static_cast<float>(width) - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(sign).x, at.y),
			mx::u32(mx::line), sign);
		if (!second.empty()) {
			dl->AddText(ImVec2(at.x, at.y + lh), mx::u32(mx::label), second.c_str());
		}
		ImGui::Dummy(ImVec2(1.0f, lh * lines));
	}

	/* The boot screen: the logo (the presenter's, above), a bar filling, the start's lines typed. */
	void drawBoot()
	{
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const float logoH = H * 0.46f, logoW = logoH * 1.5f;
		const ImVec4 hole((W - logoW) * 0.5f, H * 0.12f, logoW, logoH);
		beginScreen("##boot", 0.9f, true, hole);
		const double t = now_seconds() - screenSince;
		ImDrawList *dl = ImGui::GetWindowDrawList();
		// the bar
		const float barW = W * 0.34f, barH = H * 0.008f, barX = (W - barW) * 0.5f, barY = H * 0.63f;
		const float done = static_cast<float>(std::min(1.0, t / 2.6));
		dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), mx::u32(ImVec4(0.0f, 0.12f, 0.04f, 1.0f)));
		dl->AddRect(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), mx::u32(mx::line), 0.0f, 0, 2.0f);
		dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW * done, barY + barH), mx::u32(mx::bright));
		const char *left = "BOOTING THE XBOX";
		ImGui::SetCursorScreenPos(ImVec2(barX, barY - ImGui::GetTextLineHeightWithSpacing() * 1.2f));
		ImGui::TextColored(mx::label, "%s", left);
		const std::string right = std::string("OGBox ") + pshakux_build_label();
		ImGui::SetCursorScreenPos(ImVec2(barX + barW - ImGui::CalcTextSize(right.c_str()).x,
			barY - ImGui::GetTextLineHeightWithSpacing() * 1.2f));
		ImGui::TextColored(mx::label, "%s", right.c_str());
		// the lines, typed one after another
		float y = H - ImGui::GetStyle().WindowPadding.y - ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(bootLines.size()) + 0.5f);
		double start = 0.15;
		for (const std::string &line : bootLines) {
			const double shown = (t - start) * 110.0;   // characters a second
			start += static_cast<double>(line.size()) / 110.0 + 0.06;
			if (shown <= 0.0) {
				break;
			}
			const size_t n = std::min(line.size(), static_cast<size_t>(shown));
			ImGui::SetCursorScreenPos(ImVec2(ImGui::GetStyle().WindowPadding.x, y));
			ImGui::TextColored(mx::text, "%.*s", static_cast<int>(n), line.c_str());
			if (n < line.size()) {
				cursor();
				break;
			}
			y += ImGui::GetTextLineHeightWithSpacing();
		}
		endScreen();
	}

	/* 0.0.1: before the splash, what this build is: an experimental one, crashes and freezes
	 * expected; no games with it. CROSS goes on. */
	void drawNotice()
	{
		beginScreen("##notice", 0.25f);
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const ImGuiStyle &style = ImGui::GetStyle();
		const float colW = std::min(W - style.WindowPadding.x * 2.0f, ImGui::GetFontSize() * 46.0f);
		const float x = (W - colW) * 0.5f;
		ImGui::SetCursorPos(ImVec2(x, H * 0.2f));
		glowTitle("EXPERIMENTAL BUILD", 1.6f);
		ImGui::SetCursorPosX(x);
		ImGui::TextColored(mx::label, "OGBox %s for PS5", pshakux_build_label());
		ImGui::Spacing();
		ImGui::Spacing();
		ImGui::PushTextWrapPos(x + colW);
		const auto para = [&](const ImVec4 &color, const char *text) {
			ImGui::SetCursorPosX(x);
			ImGui::TextColored(color, "%s", text);
			ImGui::Spacing();
		};
		para(mx::bright, "This is a highly experimental build of an original Xbox emulator. Expect crashes and freezes.");
		para(mx::text, "Many games don't start yet, and the ones that do can stop, slow down, or show missing graphics "
			"or sound problems at any point. Save in game often, and keep a copy of anything you care about.");
		para(mx::text, "If something goes wrong, ogbox-boot.log in OGBox's title folder says what happened: please "
			"include it when you report a problem.");
		para(mx::label, "OGBox comes with no games, BIOS or keys. Use it only with backups of games you own.");
		ImGui::PopTextWrapPos();
		if (now_seconds() - screenSince > 1.0) {
			const std::string next = "{X} continue";
			ImDrawList *dl = ImGui::GetWindowDrawList();
			const float nw = richText(dl, ImVec2(0, 0), next, 0, 1.0f, true);
			richText(dl, ImVec2((W - nw) * 0.5f, H * 0.82f), next, mx::u32(mx::bright));
		}
		endScreen();
	}

	/* 0.1.27: the controller's focus on the item just drawn. The columns are NavFlattened child
	 * windows, so the focus belongs to the screen's window (RootWindowForNav), and ImGui 1.65 keeps
	 * the item's rectangle relative to that window (SetNavIDWithRectRel stores it in NavWindow).
	 * 0.1.26 gave it relative to the column: right from the restored row went nowhere (the first
	 * settings section, a game list just opened) until the selection moved up or down once. */
	void focusLastItem()
	{
		ImGuiContext &g = *GImGui;
		ImGuiWindow *window = ImGui::GetCurrentWindow();
		ImGuiWindow *root = window->RootWindowForNav ? window->RootWindowForNav : window;
		if (g.NavWindow != root) {
			ImGui::FocusWindow(root);
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SetNavIDWithRectRel(window->DC.LastItemId, 0,
			ImRect(ImGui::GetItemRectMin() - root->Pos, ImGui::GetItemRectMax() - root->Pos));
	}

	/* At the start of a screen with columns 0..count-1: a LEFT or RIGHT press ImGui didn't act on
	 * becomes a jump to the next column (ColumnNav). */
	void columnsBegin(int count, bool leftToFirst = false)
	{
		ColumnNav &n = colNav;
		n.lastCol = n.focusCol;
		n.focusCol = -1;
		if (n.jump >= 0) {
			n.jump = -1;                  // a column without items: nothing took it
		}
		if (n.wait > 0 && --n.wait == 0 && GImGui->NavId == n.pressId && n.pressCol >= 0) {
			const int to = n.pressCol + n.pressDir;
			if (to >= 0 && to < count) {
				n.jump = to;
			}
		}
		const uint32_t pressed = menuPad.pressed;
		if ((pressed & (PAD_LEFT | PAD_RIGHT)) && n.lastCol >= 0) {
			n.pressCol = n.lastCol;
			n.pressDir = (pressed & PAD_RIGHT) ? 1 : -1;
			n.pressId = GImGui->NavId;
			n.wait = 2;
			if (leftToFirst && n.pressDir < 0 && n.lastCol > 0) {
				/* LEFT goes to the column's own item (the settings' current section), not the
				 * level one ImGui would pick (from the options, another section) */
				ImGui::NavMoveRequestCancel();
				n.jump = n.lastCol - 1;
				n.wait = 0;
			}
		}
	}

	/* After an item of column col: focused, it is the column's; the first of a column the jump is
	 * for takes the focus (first: it is the column's first focusable item, or the one to land on). */
	void columnItem(int col, bool first)
	{
		if (ImGui::IsItemFocused()) {
			colNav.focusCol = col;
		}
		if (first && colNav.jump == col) {
			focusLastItem();
			colNav.jump = -1;
			colNav.wait = 0;
		}
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
	enum class Icon { None, Folder, Disc, Up, Console, Usb, Network, Gear, Power, Dash, Check, Plus };

	void drawIcon(ImDrawList *dl, Icon icon, ImVec2 c, float s, ImU32 col)
	{
		const float t = std::max(1.5f, s * 0.09f);
		switch (icon) {
		case Icon::Folder:
			dl->AddRectFilled(ImVec2(c.x - s * 0.45f, c.y - s * 0.30f), ImVec2(c.x - s * 0.05f, c.y - s * 0.18f), col, s * 0.05f);
			dl->AddRect(ImVec2(c.x - s * 0.45f, c.y - s * 0.20f), ImVec2(c.x + s * 0.45f, c.y + s * 0.32f), col, s * 0.06f, ImDrawCornerFlags_All, t);
			break;
		case Icon::Disc:
			dl->AddCircle(c, s * 0.40f, col, 24, t);
			dl->AddCircleFilled(c, s * 0.10f, col, 12);
			break;
		case Icon::Up:
			dl->AddLine(ImVec2(c.x + s * 0.15f, c.y - s * 0.30f), ImVec2(c.x - s * 0.15f, c.y), col, t * 1.3f);
			dl->AddLine(ImVec2(c.x - s * 0.15f, c.y), ImVec2(c.x + s * 0.15f, c.y + s * 0.30f), col, t * 1.3f);
			break;
		case Icon::Console:
			dl->AddRect(ImVec2(c.x - s * 0.42f, c.y - s * 0.22f), ImVec2(c.x + s * 0.42f, c.y + s * 0.22f), col, s * 0.05f, ImDrawCornerFlags_All, t);
			dl->AddLine(ImVec2(c.x - s * 0.25f, c.y), ImVec2(c.x + s * 0.10f, c.y), col, t);
			break;
		case Icon::Usb:
			dl->AddRect(ImVec2(c.x - s * 0.18f, c.y - s * 0.40f), ImVec2(c.x + s * 0.18f, c.y + s * 0.05f), col, 0.0f, ImDrawCornerFlags_All, t);
			dl->AddRectFilled(ImVec2(c.x - s * 0.28f, c.y + s * 0.05f), ImVec2(c.x + s * 0.28f, c.y + s * 0.42f), col, s * 0.06f);
			break;
		case Icon::Network:
			dl->AddCircle(ImVec2(c.x, c.y - s * 0.22f), s * 0.14f, col, 12, t);
			dl->AddCircle(ImVec2(c.x - s * 0.30f, c.y + s * 0.25f), s * 0.12f, col, 12, t);
			dl->AddCircle(ImVec2(c.x + s * 0.30f, c.y + s * 0.25f), s * 0.12f, col, 12, t);
			dl->AddLine(ImVec2(c.x, c.y - s * 0.08f), ImVec2(c.x - s * 0.22f, c.y + s * 0.16f), col, t);
			dl->AddLine(ImVec2(c.x, c.y - s * 0.08f), ImVec2(c.x + s * 0.22f, c.y + s * 0.16f), col, t);
			break;
		case Icon::Gear:
			dl->AddCircle(c, s * 0.26f, col, 16, t * 1.6f);
			for (int k = 0; k < 8; k++) {
				const float a = static_cast<float>(k) * 0.785398f;
				dl->AddLine(ImVec2(c.x + std::cos(a) * s * 0.30f, c.y + std::sin(a) * s * 0.30f),
					ImVec2(c.x + std::cos(a) * s * 0.44f, c.y + std::sin(a) * s * 0.44f), col, t * 1.6f);
			}
			break;
		case Icon::Power:
			dl->PathArcTo(c, s * 0.34f, -1.0f, 4.14f, 20);
			dl->PathStroke(col, false, t * 1.2f);
			dl->AddLine(ImVec2(c.x, c.y - s * 0.44f), ImVec2(c.x, c.y - s * 0.05f), col, t * 1.2f);
			break;
		case Icon::Dash:
			dl->AddCircle(c, s * 0.40f, col, 24, t);
			dl->AddLine(ImVec2(c.x - s * 0.22f, c.y - s * 0.22f), ImVec2(c.x + s * 0.22f, c.y + s * 0.22f), col, t * 1.4f);
			dl->AddLine(ImVec2(c.x - s * 0.22f, c.y + s * 0.22f), ImVec2(c.x + s * 0.22f, c.y - s * 0.22f), col, t * 1.4f);
			break;
		case Icon::Check:
			dl->AddLine(ImVec2(c.x - s * 0.32f, c.y), ImVec2(c.x - s * 0.08f, c.y + s * 0.25f), col, t * 1.4f);
			dl->AddLine(ImVec2(c.x - s * 0.08f, c.y + s * 0.25f), ImVec2(c.x + s * 0.34f, c.y - s * 0.28f), col, t * 1.4f);
			break;
		case Icon::Plus:
			dl->AddLine(ImVec2(c.x - s * 0.30f, c.y), ImVec2(c.x + s * 0.30f, c.y), col, t * 1.4f);
			dl->AddLine(ImVec2(c.x, c.y - s * 0.30f), ImVec2(c.x, c.y + s * 0.30f), col, t * 1.4f);
			break;
		case Icon::None:
			break;
		}
	}

	/* A row in a list: an icon, the text, a note on the right (it may hold {buttons}); true when
	 * chosen (CROSS). toggle: 0 none, 1 off, 2 on (a switch drawn at the right). */
	bool row(const std::string &id, const std::string &text, const std::string &note, bool current, const ImVec4 &color,
		Icon icon = Icon::None, int toggle = 0)
	{
		ImGui::PushID(id.c_str());
		const float h = ImGui::GetTextLineHeight() * 1.6f;
		const ImVec2 at = ImGui::GetCursorScreenPos();
		const bool chosen = ImGui::Selectable("##row", current, 0, ImVec2(0.0f, h));
		const bool focused = ImGui::IsItemFocused();
		/* 0.1.50: a row scrolled out of sight draws nothing (975 discs in a folder, each with its
		 * icon, overflowed the window's draw list) */
		if (!ImGui::IsItemVisible()) {
			ImGui::PopID();
			return chosen;
		}
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const float ty = at.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
		const float pad = ImGui::GetStyle().FramePadding.x;
		const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		ImU32 c = focused ? mx::u32(ImVec4(0, 0, 0, 1)) : mx::u32(color);
		if (mx::emerald) {
			/* 0.1.52: Emerald: each row a dark glass bar; the focused one a glossy green bar, white text */
			const float r = h * 0.22f;
			if (focused) {
				const float pulse = 0.88f + 0.12f * static_cast<float>(std::sin(now_seconds() * 3.0));
				dl->AddRectFilled(at, ImVec2(right, at.y + h), mx::u32(ImVec4(0.38f, 0.72f, 0.10f, pulse)), r);
				dl->AddRectFilledMultiColor(ImVec2(at.x + r * 0.5f, at.y + 1.5f), ImVec2(right - r * 0.5f, at.y + h * 0.5f),
					mx::u32(ImVec4(1, 1, 0.8f, 0.30f)), mx::u32(ImVec4(1, 1, 0.8f, 0.30f)), mx::u32(ImVec4(1, 1, 0.8f, 0.04f)),
					mx::u32(ImVec4(1, 1, 0.8f, 0.04f)));
				dl->AddRect(at, ImVec2(right, at.y + h), mx::u32(mx::bright), r, ImDrawCornerFlags_All, 2.0f);
				c = mx::u32(ImVec4(1, 1, 1, 1));
			} else {
				dl->AddRectFilled(at, ImVec2(right, at.y + h), mx::u32(ImVec4(0.02f, 0.13f, 0.03f, current ? 0.75f : 0.45f)), r);
				dl->AddRect(at, ImVec2(right, at.y + h), mx::u32(mx::line, current ? 0.9f : 0.35f), r, ImDrawCornerFlags_All,
					current ? 2.0f : 1.0f);
			}
		} else if (focused) {
			const float pulse = 0.80f + 0.20f * static_cast<float>(std::sin(now_seconds() * 4.0));
			dl->AddRectFilled(at, ImVec2(right, at.y + h), mx::u32(mx::bright, pulse), h * 0.18f);
		} else if (current) {
			dl->AddRect(at, ImVec2(right, at.y + h), mx::u32(mx::line), h * 0.18f, ImDrawCornerFlags_All, 2.0f);
		}
		float x = at.x + pad;
		if (icon != Icon::None) {
			const float s = ImGui::GetTextLineHeight();
			drawIcon(dl, icon, ImVec2(x + s * 0.5f, at.y + h * 0.5f), s, c);
			x += s * 1.4f;
		}
		float limit = right - pad;
		if (toggle) {
			const float th = ImGui::GetTextLineHeight() * 0.9f, tw = th * 1.9f;
			const ImVec2 t0(right - pad - tw, at.y + (h - th) * 0.5f), t1(right - pad, at.y + (h + th) * 0.5f);
			const bool on = toggle == 2;
			dl->AddRectFilled(t0, t1, focused ? mx::u32(ImVec4(0, 0, 0, 0.85f)) : mx::u32(on ? mx::bright : mx::line, on ? 0.9f : 1.0f), th * 0.5f);
			const float knob = th * 0.5f - 3.0f;
			dl->AddCircleFilled(ImVec2(on ? t1.x - th * 0.5f : t0.x + th * 0.5f, t0.y + th * 0.5f), knob,
				focused ? mx::u32(on ? mx::bright : mx::label) : mx::u32(on ? ImVec4(0, 0, 0, 1) : mx::label), 20);
			limit = t0.x - pad;
		}
		float nw = 0.0f;
		if (!note.empty()) {
			nw = richText(dl, ImVec2(0, 0), note, 0, 1.0f, true);
			richText(dl, ImVec2(limit - nw, ty), note, focused ? c : mx::u32(mx::label));
		}
		dl->PushClipRect(ImVec2(x, at.y), ImVec2(limit - nw - pad, at.y + h), true);
		if (mx::emerald && focused) {
			dl->AddText(ImVec2(x + 1.5f, ty + 1.5f), mx::u32(ImVec4(0, 0.15f, 0, 0.65f)), text.c_str());
		}
		dl->AddText(ImVec2(x, ty), c, text.c_str());
		dl->PopClipRect();
		ImGui::PopID();
		return chosen;
	}

	/* The game's mods and cheats: CROSS turns one on or off. */
	void drawMods()
	{
		beginScreen("##mods", 0.25f);
		const float H = static_cast<float>(height);
		const ImGuiStyle &style = ImGui::GetStyle();
		glowTitle("MODS, PATCHES, CHEATS", 1.6f);
		ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.4f);
		ImGui::TextColored(mx::label, "%s  %s", modsTitle.c_str(), modsKey.c_str());
		const float footH = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
		const float helpH = ImGui::GetTextLineHeightWithSpacing() * 3.6f;
		const float colH = H - ImGui::GetCursorPosY() - style.WindowPadding.y - footH - helpH - style.ItemSpacing.y;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x * 0.5f, style.WindowPadding.y * 0.6f));
		ImGui::BeginChild("list", ImVec2(0.0f, colH), true, ImGuiWindowFlags_NavFlattened);
		if (modList.empty()) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(mx::text, "No mods, patches or cheats of hakuX's for this game.");
			ImGui::TextColored(mx::label, "OGBox has hakuX's (the title folder's mods/); a game's are in mods/%s/: a folder a mod, cheats.txt the cheats.", modsKey.c_str());
			ImGui::PopTextWrapPos();
		}
		bool first = true;
		const auto kindOf = [](const OgboxModEntry &e) { return e.cheat ? 2 : e.patch ? 1 : e.gamefix ? -1 : 0; };
		for (size_t i = 0; i < modList.size(); i++) {
			OgboxModEntry &m = modList[i];
			if (first || (i > 0 && kindOf(modList[i - 1]) != kindOf(m))) {
				if (!first) {
					ImGui::Spacing();
				}
				ImGui::TextColored(mx::label, "%s", m.cheat ? "CHEATS" : m.patch ? "PATCHES  (applied to a copy of the disc's XBE at the start)"
					: m.gamefix ? "GAME FIXES  (always on)" : "MODS");
				first = false;
			}
			/* 0.1.52: the patches by kind, each under its own heading */
			if (m.patch && (i == 0 || !modList[i - 1].patch || std::strcmp(modList[i - 1].category, m.category) != 0)) {
				const std::string c = m.category;
				const char *heading = c == "Widescreen" ? "Widescreen (16:9)" : c == "60 FPS" ? "60 FPS"
					: c == "480i fix" ? "480i fix (for games that misbehave in interlaced video)"
					: c == "480p" ? "480p (progressive video for games without it)" : "Other fixes";
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFontSize());
				ImGui::TextColored(mx::text, "%s", heading);
			}
			const std::string text = m.name;
			/* 0.0.2: the release check (core/ogbox_mods.h): one made for another release of the game is
			 * greyed out, without its switch, and can't be turned on (one on from before can be turned
			 * off: it's left out at the start anyway) */
			const bool other = m.fit == OGBOX_FIT_NO, unchecked = m.fit == OGBOX_FIT_UNCHECKED;
			std::string note = other ? std::string(m.on ? "another release: left out" : "another release")
				: m.cheat ? (m.once ? "once" : std::string())
				: m.patch ? std::string(m.author)
				: (m.refresh ? std::to_string(m.refresh) + " Hz" : std::string());
			if (unchecked && !m.patch) {
				note = note.empty() ? std::string("release not checked") : note + "   release not checked";
			}
			/* 0.0.3: a game fix is on whatever is chosen: greyed, its switch on, CROSS does nothing */
			if (m.gamefix && !other) {
				note = "game fix: always on";
			}
			const ImVec4 &colour = m.gamefix && !other ? mx::line : other ? (m.on ? mx::fail : mx::line) : m.on ? mx::bright : mx::text;
			const bool toggled = row("m" + std::to_string(i), text, note, false, colour, Icon::None,
				other && !m.on ? 0 : m.on ? 2 : 1);
			if (!modsFocus) {
				focusLastItem();
				modsFocus = true;
			}
			if (toggled && !m.gamefix && !(other && !m.on)) {
				m.on = !m.on;
				std::lock_guard<std::mutex> disc(g_discInfoMutex);
				ogbox_mods_choose_kind(modsKey.c_str(), m.cheat ? "cheat" : m.patch ? "patch" : "mod", m.id, m.on);
				detailsPath.clear();   // the details again, with the change
			}
			if (ImGui::IsItemFocused() && other) {
				help = std::string("Made for another release of the game, not this disc: ") + m.fit_why + ". " +
					(m.on ? "It's left out when the game starts. CROSS turns it off."
						  : "It can't be turned on: it would change bytes this disc doesn't have where it expects them.");
			} else if (ImGui::IsItemFocused()) {
				help = m.description[0] ? std::string(m.description) + " " : std::string(m.cheat ? "A cheat. " : m.patch ? "" : "A mod. ");
				if (m.patch) {
					help += std::string("By ") + m.author + ", from " + m.source + ". Applied at the game's next start; a patch "
						"whose bytes are not in this disc's XBE is left out (the boot log says so).";
					const std::string all = std::string(m.name) + " " + m.description;
					if (all.find("128") != std::string::npos) {
						help += " Needs the 128 MB RAM upgrade: with Settings > Console > Memory on Auto, OGBox gives the game 128 MB while it's on.";
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
		}
		drawTrainerRows();   // 0.1.54
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::BeginChild("help", ImVec2(0.0f, helpH), true, ImGuiWindowFlags_NoNav);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextColored(mx::text, "%s", help.c_str());
		if (modsCrc) {
			ImGui::TextColored(mx::line, "/data/ogbox/mods.txt     default.xbe CRC-32 %08X     the trainers: %s",
				static_cast<unsigned>(modsCrc), ogbox::storagePath("trainers").c_str());
		} else {
			ImGui::TextColored(mx::line, "/data/ogbox/mods.txt     the trainers: %s", ogbox::storagePath("trainers").c_str());
		}
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
		footer("{X} on / off     {O} back");
		endScreen();
	}

	/* In a game: the FPS panel, the quit ring, and what is happening until the first frame. */
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

	/* The LED like the console's power button: its colour now (a sequence turns through four
	 * quarters of a second), "LED" and the colour in words beside it. */
	void drawLed(bool belowFps)
	{
		const double now = now_seconds();
		if (now > ledUntil) {
			return;
		}
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const float k = H / 1080.0f;
		const float alpha = static_cast<float>(std::min(1.0, (ledUntil - now) / 0.5));
		const int phase = static_cast<int>(now * 4.0) & 3;
		const bool g = (ledSeq >> phase) & 1, r = (ledSeq >> (4 + phase)) & 1;
		const ImVec4 col = r && g ? ImVec4(1.0f, 0.62f, 0.10f, 1.0f) : r ? ImVec4(1.0f, 0.18f, 0.12f, 1.0f)
			: g ? ImVec4(0.30f, 1.0f, 0.25f, 1.0f) : ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
		ImDrawList *dl = ImGui::GetOverlayDrawList();
		const float rad = 16.0f * k;
		const ImVec2 c(W - H * 0.02f - rad - 8.0f * k, H * 0.02f + rad + 8.0f * k + (belowFps ? 70.0f * k : 0.0f));
		const std::string label = "LED  " + ledText;
		const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
		const ImVec2 b0(c.x - rad - 24.0f * k - ts.x - 20.0f * k, c.y - rad - 10.0f * k), b1(c.x + rad + 10.0f * k, c.y + rad + 10.0f * k);
		dl->AddRectFilled(b0, b1, mx::u32(ImVec4(0, 0, 0, 0.72f), alpha), 12.0f * k);
		if (r || g) {
			dl->AddCircleFilled(c, rad * 1.6f, mx::u32(ImVec4(col.x, col.y, col.z, 0.25f), alpha), 32);   // the glow
		}
		dl->AddCircleFilled(c, rad, mx::u32(col, alpha), 32);
		dl->AddCircle(c, rad, mx::u32(ImVec4(1, 1, 1, 0.5f), alpha), 32, 2.0f);
		dl->AddText(ImVec2(b0.x + 20.0f * k, c.y - ts.y * 0.5f), mx::u32(mx::text, alpha), label.c_str());
	}

	void drawGame()
	{
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const float pad = H * 0.02f;
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
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
		drawLed(showFps && havePicture);
		if (showFps && havePicture) {
			ImGui::SetNextWindowPos(ImVec2(W - pad, pad), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
			ImGui::SetNextWindowBgAlpha(0.72f);
			ImGui::Begin("##fps", nullptr, flags);
			ImGui::SetWindowFontScale(1.4f);
			ImGui::TextColored(mx::bright, "%.0f", fps);
			ImGui::SetWindowFontScale(1.0f);
			ImGui::SameLine();
			ImGui::TextColored(mx::label, "FPS  %.1f ms", fps > 0.5f ? 1000.0f / fps : 0.0f);
			ImGui::End();
		}
		const bool trouble = !problem.empty() || state == PSHX_CORE_FAILED || state == PSHX_CORE_STOPPED;
		/* 0.0.2: no panel over a game's frozen last frame (its next XBE is starting), only trouble */
		if (trouble || (!havePicture && !showingFrozen)) {
			ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.85f);
			ImGui::Begin("##status", nullptr, flags);
			ImGui::TextColored(mx::bright, "OGBox %s", pshakux_build_label());
			if (!problem.empty()) {
				ImGui::PushTextWrapPos(W * 0.6f);
				ImGui::TextColored(mx::fail, "%s", problem.c_str());
				ImGui::PopTextWrapPos();
			} else {
				ImGui::TextColored(mx::label, "DISC  ");
				ImGui::SameLine();
				ImGui::TextColored(mx::text, "%s", boot.image == "none" ? "none: the Xbox dashboard" : baseName(boot.image).c_str());
				ImGui::TextColored(mx::label, "CORE  ");
				ImGui::SameLine();
				ImGui::TextColored(state == PSHX_CORE_FAILED ? mx::fail : mx::text, "%s", stateName(state));
				if (pshx_core_message()[0]) {
					ImGui::PushTextWrapPos(W * 0.6f);
					ImGui::TextColored(mx::text, "%s", pshx_core_message());
					ImGui::PopTextWrapPos();
				}
				if (state == PSHX_CORE_BOOTING || (state == PSHX_CORE_RUNNING && !havePicture)) {
					ImGui::TextColored(mx::label, "Waiting for the game's first frame");
					cursor();
				}
			}
			richLine("{OPT} quick menu: save, load, quit", mx::label);
			ImGui::End();
		}
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
		wantFocus = true;
		storageFocus = false;
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
		storageFocus = false;
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
			storageFocus = false;
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
		storageFocus = false;
	}

	void storageLeave()
	{
		if (storageFirst) {
			screen = Screen::Library;
			focusSet = false;
		} else {
			screen = Screen::Settings;
			focusList = false;
		}
		screenSince = now_seconds();
		wantFocus = true;
	}

	/* CIRCLE on the data folder's screen. */
	void storageBack()
	{
		switch (storageStep) {
		case StorageStep::Browse:
			if (storageFolder.empty()) {
				storageStep = StorageStep::Places;
			} else {
				const bool root = storageFolder == "/data" || storageFolder.rfind("/mnt/", 0) == 0 && std::count(storageFolder.begin(), storageFolder.end(), '/') <= 2;
				storageBrowse(root ? std::string() : parentOf(storageFolder));
			}
			storageFocus = false;
			break;
		case StorageStep::Confirm:
			storageStep = StorageStep::Places;
			storageFocus = false;
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

	void storageItemFocus()
	{
		if (!storageFocus) {
			focusLastItem();
			storageFocus = true;
		}
	}

	void drawStorage()
	{
		beginScreen("##storage", 0.25f);
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const ImGuiStyle &style = ImGui::GetStyle();
		glowTitle(storageFirst ? "WELCOME TO OGBOX" : "DATA FOLDER", 1.6f);
		const std::string now = ogbox::storageRoot();
		ImGui::PushTextWrapPos(W * 0.92f);
		if (storageFirst && storageStep == StorageStep::Places) {
			ImGui::TextColored(mx::text, "Where should OGBox keep its data? That is the Xbox's hard disk (your saves), the shader "
				"caches, the covers and your screenshots: a few GB as you play.");
			ImGui::TextColored(mx::label, "OGBox's settings stay on the console either way. You can change this later in Settings, "
				"and OGBox moves the data for you.");
		} else if (storageStep == StorageStep::Places) {
			if (isFolder(now)) {
				ImGui::TextColored(mx::text, "Now: %s  (%s)", now.c_str(), ogbox::freeSpaceText(now).c_str());
			} else {   // 0.0.3: its drive unplugged
				ImGui::TextColored(mx::fail, "Now: %s  (not there: is its drive plugged in? Your Xbox hard disk is on it)",
					now.c_str());
			}
			ImGui::TextColored(mx::label, "Choose another place and OGBox moves your Xbox hard disk, caches, covers and screenshots there.");
		}
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		const float footH = ImGui::GetTextLineHeightWithSpacing() * 1.8f;
		const float listH = H - ImGui::GetCursorPosY() - style.WindowPadding.y - footH;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x * 0.5f, style.WindowPadding.y * 0.6f));
		ImGui::BeginChild("storage", ImVec2(W * 0.62f, listH), true, ImGuiWindowFlags_NavFlattened);
		std::string pick;
		bool browse = false;
		switch (storageStep) {
		case StorageStep::Places: {
			ImGui::TextColored(mx::label, "PLACES");
			for (size_t i = 0; i < storageList.size(); i++) {
				const ogbox::StoragePlace &p = storageList[i];
				const bool here = p.path == now;
				const std::string text = p.name + (p.recommended ? "  (recommended)" : "") + (here ? "  - now" : "");
				const Icon icon = p.path.rfind("/mnt/", 0) == 0 ? Icon::Usb : Icon::Console;
				if (row("sp" + std::to_string(i), text, p.path + "    " + p.note, here, here ? mx::bright : mx::text, icon)) {
					pick = p.path;
				}
				storageItemFocus();
			}
			if (row("browse", "Another folder...", "internal storage, USB, extended storage", false, mx::text, Icon::Folder)) {
				browse = true;
			}
			break;
		}
		case StorageStep::Browse: {
			ImGui::TextColored(mx::label, "%s", storageFolder.empty() ? "DRIVES" : storageFolder.c_str());
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
				for (size_t i = 0; i < roots.size(); i++) {
					if (row("r" + std::to_string(i), roots[i].first, roots[i].second + "    " + ogbox::freeSpaceText(roots[i].second), false,
							mx::text, roots[i].second == "/data" ? Icon::Console : Icon::Usb)) {
						storageBrowse(roots[i].second);
					}
					storageItemFocus();
				}
			} else {
				if (row("use", "Use this folder", ogbox::freeSpaceText(storageFolder), false, mx::bright, Icon::Check)) {
					pick = storageFolder;
				}
				storageItemFocus();
				if (row("make", "Make a folder named OGBox here and use it", std::string(), false, mx::text, Icon::Plus)) {
					pick = storageFolder + "/OGBox";
				}
				if (row("up", "Back", std::string(), false, mx::label, Icon::Up)) {
					storageBack();
				}
				for (size_t i = 0; i < storageSubs.size(); i++) {
					if (row("f" + std::to_string(i), storageSubs[i], std::string(), false, mx::label, Icon::Folder)) {
						storageBrowse(storageFolder + "/" + storageSubs[i]);
						break;
					}
				}
			}
			break;
		}
		case StorageStep::Confirm: {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(mx::text, "OGBox has %s of data in %s.", sizeText(static_cast<int64_t>(storageHave)).c_str(), now.c_str());
			ImGui::TextColored(mx::label, "New folder: %s  (%s)", storageTarget.c_str(), ogbox::freeSpaceText(storageTarget).c_str());
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			if (row("move", "Move my data there", sizeText(static_cast<int64_t>(storageHave)), false, mx::bright, Icon::Check)) {
				ogbox::StorageMove::get().start(now, storageTarget);
				storageStep = StorageStep::Moving;
			}
			storageItemFocus();
			if (row("empty", "Start there empty", "the data stays in " + now, false, mx::text, Icon::Plus)) {
				std::string why;
				if (ogbox::setStorageRoot(storageTarget, why)) {
					storageDone("OGBox keeps its data in " + storageTarget + " from now on. What was in " + now + " is still there.");
				} else {
					storageMessage = why;
				}
			}
			if (row("cancel", "Cancel", std::string(), false, mx::label, Icon::Up)) {
				storageStep = StorageStep::Places;
				storageFocus = false;
			}
			break;
		}
		case StorageStep::Moving: {
			const ogbox::StorageMove::Progress pr = ogbox::StorageMove::get().progress();
			ImGui::TextColored(mx::text, "Moving your data to %s", storageTarget.c_str());
			cursor();
			ImGui::Spacing();
			const float f = pr.total ? std::min(1.0f, static_cast<float>(static_cast<double>(pr.bytes) / static_cast<double>(pr.total))) : 0.0f;
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float bw = ImGui::GetContentRegionAvail().x, bh = ImGui::GetTextLineHeight() * 0.8f;
			ImDrawList *dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(at, ImVec2(at.x + bw, at.y + bh), mx::u32(ImVec4(0.0f, 0.12f, 0.04f, 1.0f)), bh * 0.5f);
			dl->AddRectFilled(at, ImVec2(at.x + std::max(bh, bw * f), at.y + bh), mx::u32(mx::bright), bh * 0.5f);
			ImGui::Dummy(ImVec2(bw, bh));
			ImGui::TextColored(mx::label, "%s of %s   %s", sizeText(static_cast<int64_t>(pr.bytes)).c_str(),
				sizeText(static_cast<int64_t>(pr.total)).c_str(), pr.current.c_str());
			ImGui::TextColored(mx::label, "Leave OGBox open until it's done.");
			if (pr.done) {
				if (pr.ok) {
					storageDone("Done: OGBox keeps its data in " + storageTarget + " now.");
				} else {
					storageMessage = "The move stopped: " + pr.error + ". Your data is still in " + now + ".";
					storageStep = StorageStep::Places;
					storageFocus = false;
				}
			}
			break;
		}
		case StorageStep::Done: {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(mx::bright, "%s", storageMessage.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			if (row("ok", storageFirst ? "Go to the game list" : "Back to the settings", std::string(), false, mx::bright, Icon::Check)) {
				storageLeave();
			}
			storageItemFocus();
			break;
		}
		}
		if (!storageMessage.empty() && storageStep != StorageStep::Done) {
			ImGui::Spacing();
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(mx::fail, "%s", storageMessage.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("storage-about", ImVec2(0.0f, listH), true, ImGuiWindowFlags_NoNav);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextColored(mx::label, "WHAT GOES THERE");
		ImGui::TextColored(mx::text, "hdd");
		ImGui::TextColored(mx::label, "the Xbox's hard disk: saves, the dashboard if you add one");
		ImGui::TextColored(mx::text, "cache");
		ImGui::TextColored(mx::label, "shaders built for your games, patched XBEs");
		ImGui::TextColored(mx::text, "covers, shots");
		ImGui::TextColored(mx::label, "the game list's covers, your screenshots");
		ImGui::Spacing();
		ImGui::TextColored(mx::label, "A USB drive must stay plugged in while you play. exFAT is best; FAT32 can't hold files over 4 GB.");
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
		ImGui::PopStyleVar();
		footer(storageStep == StorageStep::Moving ? std::string("Moving...")
			: storageFirst && storageStep == StorageStep::Places ? std::string("{X} choose")
			: std::string("{X} choose     {O} back"));
		endScreen();
		if (!pick.empty()) {
			storageChoose(pick);
		} else if (browse) {
			storageBrowse(std::string());
		}
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

	enum QuickRow { QR_RESUME, QR_SAVE, QR_LOAD, QR_SLOT, QR_LEFT, QR_SETTINGS, QR_MODS, QR_DRAWLOG, QR_RESTART, QR_QUIT,
		QR_COUNT };
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
		if (qm.sub != Quick::Sub::None) {
			if (pressed & PAD_CIRCLE) {
				pshakux_log("quick menu: back from the game's %s", qm.sub == Quick::Sub::Settings ? "settings" : "mods");
				qm.sub = Quick::Sub::None;   // back to the quick menu
				prefs.load(runKey);
			}
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
				swallowNav = true;
			}
			break;
		case QR_MODS:
			if (!runKey.empty()) {
				openMods(runKey, ogbox::shortName(baseName(boot.image)));   // (not the disc: the game reads it)
				qm.sub = Quick::Sub::Mods;
				swallowNav = true;
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
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const float k = H / 1080.0f;
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(W, H));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
		ImGui::Begin("##quick", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const float t = static_cast<float>(std::min(1.0, (now_seconds() - qm.openedAt) / 0.18));
		const float ease = 1.0f - (1.0f - t) * (1.0f - t);
		dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), mx::u32(ImVec4(0, 0, 0, 0.55f * ease)));
		const float pw = 640.0f * k, px = W - pw * ease;
		const ImVec4 bg = mx::emerald ? ImVec4(0.02f, 0.14f, 0.04f, 0.97f) : ImVec4(0.0f, 0.04f, 0.01f, 0.97f);
		dl->AddRectFilledMultiColor(ImVec2(px - 40.0f * k, 0), ImVec2(px, H), 0, mx::u32(ImVec4(0, 0, 0, 0.5f)),
			mx::u32(ImVec4(0, 0, 0, 0.5f)), 0);
		dl->AddRectFilled(ImVec2(px, 0), ImVec2(W, H), mx::u32(bg));
		dl->AddLine(ImVec2(px, 0), ImVec2(px, H), mx::u32(mx::line), 2.0f);
		const float pad = 36.0f * k;
		float y = pad;
		// the head: the game's cover, name, "Paused"
		const std::string name = boot.image == "none" ? std::string("Xbox dashboard") : ogbox::shortName(baseName(boot.image));
		float cw = 0.0f, ch = 0.0f;
		LibraryEntry fake{ LibraryEntry::Image, baseName(boot.image), boot.image, 0, std::string() };
		const ImTextureID cover = coverImage(coverNames(fake), &cw, &ch);
		const float cs = 150.0f * k;
		if (cover && cw > 0.0f && ch > 0.0f) {
			const float s2 = std::min(cs / cw, cs / ch);
			dl->AddImageRounded(cover, ImVec2(px + pad, y), ImVec2(px + pad + cw * s2, y + ch * s2), ImVec2(0, 0), ImVec2(1, 1),
				IM_COL32_WHITE, 8.0f * k);
		} else {
			dl->AddRectFilled(ImVec2(px + pad, y), ImVec2(px + pad + cs * 0.72f, y + cs), mx::u32(mx::panel), 8.0f * k);
		}
		const float tx = px + pad + cs * 0.80f + 24.0f * k;
		const float fs = ImGui::GetFontSize();
		dl->PushClipRect(ImVec2(tx, y), ImVec2(W - pad, y + cs), true);
		dl->AddText(ImGui::GetFont(), fs * 1.3f, ImVec2(tx, y + 6.0f * k), mx::u32(mx::bright), name.c_str(), nullptr, W - pad - tx);
		dl->PopClipRect();
		dl->AddText(ImVec2(tx, y + cs - fs * 2.6f), mx::u32(mx::label), runKey.c_str());
		dl->AddText(ImVec2(tx, y + cs - fs * 1.3f), mx::u32(mx::text), "Paused");
		y += cs + 28.0f * k;
		// the rows
		const ogbox::StateSlot slot = ogbox::stateSlot(runKey, qm.slot);
		const ogbox::StateSlot left = ogbox::stateSlot(runKey, ogbox::kQuitSlot);
		struct RowText {
			std::string text, note;
			bool dim;
		};
		const bool restartArmed = now_seconds() < qm.restartArmed;
		const RowText rows[QR_COUNT] = {
			{ "Resume", "", false },
			{ "Save state", "slot " + std::to_string(qm.slot), runKey.empty() },
			{ "Load state", slot.used ? "slot " + std::to_string(qm.slot) : std::string("empty slot"), !slot.used },
			{ "State slot", "{L}  " + std::to_string(qm.slot) + "  {R}", false },
			{ "Where you left off", left.used ? ogbox::dateText(left.mtime) : std::string("none yet"), !left.used },
			{ "Game settings", "next start", runKey.empty() },
			{ "Mods, patches, cheats", "next start", runKey.empty() },
			{ "Log what the GPU draws", "starts 5 s after you resume", false },
			{ restartArmed ? "Restart: press again" : "Restart the game", "", false },
			{ "Quit the game", "", false },
		};
		const float footH = 70.0f * k;
		const float rh = std::min(70.0f * k, (H - y - footH) / static_cast<float>(QR_COUNT));
		for (int i = 0; i < QR_COUNT; i++) {
			const ImVec2 r0(px + pad * 0.6f, y), r1(W - pad * 0.6f, y + rh - 6.0f * k);
			const bool focus = i == qm.sel;
			if (focus) {
				if (mx::emerald) {
					dl->AddRectFilled(r0, r1, mx::u32(ImVec4(0.38f, 0.72f, 0.10f, 0.95f)), 10.0f * k);
					dl->AddRect(r0, r1, mx::u32(mx::bright), 10.0f * k, ImDrawCornerFlags_All, 2.0f);
				} else {
					dl->AddRectFilled(r0, r1, mx::u32(mx::bright), 6.0f * k);
				}
			}
			const ImU32 tc = focus ? (mx::emerald ? IM_COL32_WHITE : mx::u32(ImVec4(0, 0, 0, 1)))
				: mx::u32(rows[i].dim ? mx::line : (i == QR_QUIT ? mx::fail : mx::text));
			const float ty = y + (rh - 6.0f * k - ImGui::GetTextLineHeight()) * 0.5f;
			dl->AddText(ImVec2(r0.x + 18.0f * k, ty), tc, rows[i].text.c_str());
			if (!rows[i].note.empty()) {
				std::string note = rows[i].note;
				size_t at;
				while ((at = note.find("{L}")) != std::string::npos) note.replace(at, 3, "<");
				while ((at = note.find("{R}")) != std::string::npos) note.replace(at, 3, ">");
				const float nw = ImGui::CalcTextSize(note.c_str()).x;
				dl->AddText(ImVec2(r1.x - 18.0f * k - nw, ty), focus ? tc : mx::u32(mx::label), note.c_str());
			}
			y += rh;
		}
		// the hints
		richText(dl, ImVec2(px + pad, H - footH + 18.0f * k), "{X} choose   {O} resume   < > slot", mx::u32(mx::label));
		// the slot's card, beside the panel, for the state rows
		if (qm.sel == QR_SAVE || qm.sel == QR_LOAD || qm.sel == QR_SLOT || qm.sel == QR_LEFT) {
			const ogbox::StateSlot &st = qm.sel == QR_LEFT ? left : slot;
			const float cwid = 432.0f * k, chei = 320.0f * k;
			const float cx = px - cwid - 24.0f * k;
			const float cy = std::min(H - chei - pad, std::max(pad, pad + cs + 28.0f * k + rh * static_cast<float>(qm.sel) - chei * 0.4f));
			dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cwid, cy + chei), mx::u32(bg), 12.0f * k);
			dl->AddRect(ImVec2(cx, cy), ImVec2(cx + cwid, cy + chei), mx::u32(mx::line), 12.0f * k, ImDrawCornerFlags_All, 2.0f);
			const float iw = cwid - 32.0f * k, ih = iw * 9.0f / 16.0f;
			float tw = 0.0f, th = 0.0f;
			ImTextureID img = nullptr;
			if (st.used) {
				img = fileImage(st.thumb, st.thumb + "#" + std::to_string(st.mtime), &tw, &th);
			} else if (qm.sel == QR_SAVE && !qm.thumb.empty()) {
				// a new save: the frozen picture
			}
			const ImVec2 i0(cx + 16.0f * k, cy + 16.0f * k);
			if (img && tw > 0.0f && th > 0.0f) {
				const float s2 = std::min(iw / tw, ih / th);
				const ImVec2 o(i0.x + (iw - tw * s2) * 0.5f, i0.y + (ih - th * s2) * 0.5f);
				dl->AddImage(img, o, ImVec2(o.x + tw * s2, o.y + th * s2));
			} else {
				dl->AddRectFilled(i0, ImVec2(i0.x + iw, i0.y + ih), mx::u32(ImVec4(0, 0, 0, 0.5f)), 6.0f * k);
				const char *empty = st.used ? "(no picture)" : "Empty slot";
				const ImVec2 es = ImGui::CalcTextSize(empty);
				dl->AddText(ImVec2(i0.x + (iw - es.x) * 0.5f, i0.y + (ih - es.y) * 0.5f), mx::u32(mx::label), empty);
			}
			const std::string label = qm.sel == QR_LEFT ? std::string("Where you left off") : "Slot " + std::to_string(st.slot);
			dl->AddText(ImVec2(i0.x, i0.y + ih + 12.0f * k), mx::u32(mx::bright), label.c_str());
			const std::string when = st.used ? ogbox::dateText(st.mtime) : std::string();
			dl->AddText(ImVec2(i0.x + iw - ImGui::CalcTextSize(when.c_str()).x, i0.y + ih + 12.0f * k), mx::u32(mx::label), when.c_str());
		}
		// a save under way
		if (!qm.busy.empty()) {
			drawBusy(dl, qm.busy);
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}

	/* "Saving..." in the middle of the screen, a ring turning. */
	void drawBusy(ImDrawList *dl, const std::string &text)
	{
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const float k = H / 1080.0f;
		const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
		const float bw = ts.x + 140.0f * k, bh = 110.0f * k;
		const ImVec2 b0((W - bw) * 0.5f, (H - bh) * 0.5f), b1(b0.x + bw, b0.y + bh);
		dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), mx::u32(ImVec4(0, 0, 0, 0.35f)));
		dl->AddRectFilled(b0, b1, mx::u32(mx::emerald ? ImVec4(0.02f, 0.14f, 0.04f, 0.97f) : ImVec4(0, 0.05f, 0.01f, 0.97f)), 14.0f * k);
		dl->AddRect(b0, b1, mx::u32(mx::line), 14.0f * k, ImDrawCornerFlags_All, 2.0f);
		const ImVec2 c(b0.x + 50.0f * k, b0.y + bh * 0.5f);
		const float a0 = static_cast<float>(std::fmod(now_seconds() * 5.0, 6.2831853));
		dl->PathArcTo(c, 20.0f * k, a0, a0 + 4.2f, 24);
		dl->PathStroke(mx::u32(mx::bright), false, 4.0f * k);
		dl->AddText(ImVec2(b0.x + 90.0f * k, b0.y + (bh - ts.y) * 0.5f), mx::u32(mx::text), text.c_str());
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
		swallowNav = true;
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
		swallowNav = true;
		pshakux_log("library: the game page of %s", e.path.c_str());
	}

	/* Back from a screen the page opened: the page again. */
	void reopenGamePage()
	{
		if (gp.returnTo) {
			gp.returnTo = false;
			gp.open = true;
			gp.since = now_seconds();
			swallowNav = true;
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
		swallowNav = true;
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
				focusSet = false;
				focusList = false;
				wantFocus = true;
				swallowNav = true;
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
		beginScreen("##hdd", 0.25f);
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const float k = H / 1080.0f;
		const ImGuiStyle &style = ImGui::GetStyle();
		glowTitle("HARD DISK", 1.6f);
		ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.4f);
		ImGui::TextColored(mx::label, "E: %s used   %s", ogbox::bytesText(hdd.usedBytes).c_str(), boot.hdd.c_str());
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const float lh = ImGui::GetTextLineHeight();
		const float top = ImGui::GetCursorScreenPos().y + 10.0f * k;
		const float footH = lh * 2.6f;
		const float bottom = H - style.WindowPadding.y - footH;
		const float x0 = style.WindowPadding.x, xm = W * 0.42f, x1 = W - style.WindowPadding.x;
		const float r = mx::emerald ? 14.0f * k : 2.0f;
		dl->AddRectFilled(ImVec2(x0, top), ImVec2(xm - 12.0f * k, bottom), mx::u32(mx::panel), r);
		dl->AddRect(ImVec2(x0, top), ImVec2(xm - 12.0f * k, bottom), mx::u32(mx::line, hdd.pane == 0 ? 1.0f : 0.4f), r, ImDrawCornerFlags_All, 2.0f);
		dl->AddRectFilled(ImVec2(xm + 12.0f * k, top), ImVec2(x1, bottom), mx::u32(mx::panel), r);
		dl->AddRect(ImVec2(xm + 12.0f * k, top), ImVec2(x1, bottom), mx::u32(mx::line, hdd.pane == 1 ? 1.0f : 0.4f), r, ImDrawCornerFlags_All, 2.0f);
		const auto rowBox = [&](float rx0, float rx1, float y, float h, bool focus, bool active) {
			if (focus) {
				const ImU32 c = mx::emerald ? mx::u32(ImVec4(0.38f, 0.72f, 0.10f, active ? 0.95f : 0.45f)) : mx::u32(mx::bright, active ? 1.0f : 0.4f);
				dl->AddRectFilled(ImVec2(rx0, y), ImVec2(rx1, y + h - 4.0f * k), c, r * 0.6f);
			}
			return focus && active ? (mx::emerald ? IM_COL32_WHITE : mx::u32(ImVec4(0, 0, 0, 1))) : mx::u32(mx::text);
		};
		// the left pane: the actions, then the games
		{
			const float px0 = x0 + 14.0f * k, px1 = xm - 26.0f * k;
			float y = top + 14.0f * k;
			const char *actions[HA_COUNT] = { "Bring saves in...", "Copy every save out...", "Empty the cache drives (X, Y, Z)" };
			for (int i = 0; i < HA_COUNT; i++) {
				const ImU32 tc = rowBox(px0, px1, y, lh * 1.8f, hdd.selT == i, hdd.pane == 0);
				dl->AddText(ImVec2(px0 + 14.0f * k, y + lh * 0.35f), tc, actions[i]);
				if (i == HA_CACHES) {
					const std::string b = ogbox::bytesText(hdd.cacheBytes);
					dl->AddText(ImVec2(px1 - 14.0f * k - ImGui::CalcTextSize(b.c_str()).x, y + lh * 0.35f), tc, b.c_str());
				}
				y += lh * 1.8f;
			}
			y += lh * 0.5f;
			dl->AddText(ImVec2(px0 + 4.0f * k, y), mx::u32(mx::label), hdd.titles.empty() ? "NO GAME HAS SAVED YET" : "GAMES");
			y += lh * 1.4f;
			const float rowH = 64.0f * k;
			const int visible = std::max(1, static_cast<int>((bottom - y - 10.0f * k) / rowH));
			const int sel = hdd.selT - HA_COUNT;
			const int first = std::max(0, std::min(sel - visible / 2, static_cast<int>(hdd.titles.size()) - visible));
			dl->PushClipRect(ImVec2(px0, y), ImVec2(px1, bottom - 6.0f * k), true);
			for (int i = first; i < static_cast<int>(hdd.titles.size()) && i < first + visible; i++) {
				const ogbox::HddTitle &t = hdd.titles[static_cast<size_t>(i)];
				const ImU32 tc = rowBox(px0, px1, y, rowH, i == sel, hdd.pane == 0);
				float cw = 0.0f, ch = 0.0f;
				const ImTextureID img = coverImage(ogbox::coverSources(std::string(), std::string(), t.titleId, 1), &cw, &ch);
				const float ih = rowH - 14.0f * k;
				if (img && cw > 0.0f && ch > 0.0f) {
					const float s2 = ih / ch;
					dl->AddImage(img, ImVec2(px0 + 8.0f * k, y + 5.0f * k), ImVec2(px0 + 8.0f * k + cw * s2, y + 5.0f * k + ih));
				} else {
					drawIcon(dl, Icon::Disc, ImVec2(px0 + 8.0f * k + ih * 0.35f, y + rowH * 0.5f), ih * 0.7f, tc);
				}
				const float tx = px0 + 8.0f * k + ih * 0.75f + 14.0f * k;
				dl->PushClipRect(ImVec2(tx, y), ImVec2(px1 - 8.0f * k, y + rowH), true);
				dl->AddText(ImVec2(tx, y + 6.0f * k), tc, t.name.c_str());
				const std::string sub = std::to_string(t.saves.size()) + (t.saves.size() == 1 ? " save, " : " saves, ") + ogbox::bytesText(t.bytes) +
					"   " + t.idText;
				dl->AddText(ImVec2(tx, y + 6.0f * k + lh), i == sel && hdd.pane == 0 ? tc : mx::u32(mx::label), sub.c_str());
				dl->PopClipRect();
				y += rowH;
			}
			dl->PopClipRect();
		}
		// the right pane: the saves of the game, or the copies to bring in
		{
			const float px0 = xm + 26.0f * k, px1 = x1 - 14.0f * k;
			float y = top + 14.0f * k;
			const ogbox::HddTitle *t = hddTitle();
			if (hdd.importing) {
				dl->AddText(ImVec2(px0 + 4.0f * k, y), mx::u32(mx::label), ("IN " + hdd.importName + "   " + hdd.importPlace).c_str());
				y += lh * 1.4f;
				if (hdd.copies.empty()) {
					dl->AddText(ImVec2(px0 + 4.0f * k, y), mx::u32(mx::text), "No saves there. A save is a folder <title ID>/<save>, or UDATA/<title ID>/<save>.");
				} else {
					const ImU32 tc = rowBox(px0, px1, y, lh * 1.8f, hdd.selS == 0, hdd.pane == 1);
					dl->AddText(ImVec2(px0 + 14.0f * k, y + lh * 0.35f), tc, "Bring them all in (those the hard disk doesn't have)");
					y += lh * 2.0f;
				}
				const int visible = std::max(1, static_cast<int>((bottom - y) / (lh * 1.8f)));
				const int first = std::max(0, std::min(hdd.selS - 1 - visible / 2, static_cast<int>(hdd.copies.size()) - visible));
				for (int i = first; i < static_cast<int>(hdd.copies.size()) && i < first + visible; i++) {
					const ogbox::SaveCopy &c = hdd.copies[static_cast<size_t>(i)];
					const ImU32 tc = rowBox(px0, px1, y, lh * 1.8f, hdd.selS == i + 1, hdd.pane == 1);
					const std::string right = (c.onDisk ? std::string("on the hard disk   ") : std::string()) + ogbox::dateText(c.mtime);
					const float rw = ImGui::CalcTextSize(right.c_str()).x;
					dl->PushClipRect(ImVec2(px0, y), ImVec2(px1 - rw - 30.0f * k, y + lh * 1.8f), true);
					dl->AddText(ImVec2(px0 + 14.0f * k, y + lh * 0.35f), tc, c.name.c_str());
					dl->PopClipRect();
					dl->AddText(ImVec2(px1 - 14.0f * k - rw, y + lh * 0.35f), hdd.selS == i + 1 && hdd.pane == 1 ? tc : mx::u32(mx::label), right.c_str());
					y += lh * 1.8f;
				}
			} else if (t) {
				dl->AddText(ImVec2(px0 + 4.0f * k, y), mx::u32(mx::label), ("SAVES OF " + t->name).c_str());
				y += lh * 1.4f;
				if (t->saves.empty()) {
					dl->AddText(ImVec2(px0 + 4.0f * k, y), mx::u32(mx::text), "None: only its own data (TRIANGLE: delete it).");
				}
				for (size_t i = 0; i < t->saves.size(); i++) {
					const ogbox::HddSave &s = t->saves[i];
					const bool focus = hdd.selS == static_cast<int>(i);
					const ImU32 tc = rowBox(px0, px1, y, lh * 2.6f, focus, hdd.pane == 1);
					dl->AddText(ImVec2(px0 + 14.0f * k, y + lh * 0.2f), tc, s.name.c_str());
					const std::string sub = ogbox::dateText(s.mtime) + "   " + ogbox::bytesText(s.bytes) + "   " + s.folder;
					dl->AddText(ImVec2(px0 + 14.0f * k, y + lh * 1.25f), focus && hdd.pane == 1 ? tc : mx::u32(mx::label), sub.c_str());
					y += lh * 2.6f;
					if (y > bottom - lh * 2.6f) {
						break;
					}
				}
				if (t->dataBytes) {
					const std::string d = "Its own data (TDATA): " + ogbox::bytesText(t->dataBytes);
					dl->AddText(ImVec2(px0 + 4.0f * k, bottom - lh * 1.6f), mx::u32(mx::label), d.c_str());
				}
			} else {
				const char *help = hdd.selT == HA_IMPORT ? "Saves copied out of OGBox, or from another Xbox or emulator, as folders <title ID>/<save> (or UDATA/<title ID>/<save>) in OGBox/saves on a USB drive, or in the data folder's backups/saves."
					: hdd.selT == HA_EXPORT_ALL ? "Every game's saves copied to the data folder's backups/saves, or to OGBox/saves on a USB drive."
					: "The games' cache drives. Emptying them is safe: games make what they need again (the first load after it is slower).";
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(px0 + 4.0f * k, y), mx::u32(mx::text), help, nullptr, px1 - px0 - 8.0f * k);
			}
		}
		footer(hdd.pane == 0 ? "{X} choose     {T} the game's: copy, delete     {O} back" : "{X} choose     {O} back");
		endScreen();
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

	/* The screens' ground and a full-screen window to draw in; endUiScreen fades in from black. */
	ImDrawList *beginUiScreen(const char *name)
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
		drawGround(dl, 1.0f);
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

	/* The list layout: one column, the cards then the games; UP / DOWN one, LEFT / RIGHT ten. */
	void homeListInput(uint32_t p)
	{
		homeCacheCheck();
		std::vector<int> all(hc.cards);
		all.insert(all.end(), hc.games.begin(), hc.games.end());
		if (all.empty()) {
			if (p & PAD_CIRCLE) {
				goUp();
			} else if (p & PAD_OPTIONS) {
				openSettings(std::string(), std::string());
			} else if (p & (PAD_L1 | PAD_R1)) {
				switchTab((p & PAD_R1) ? 1 : -1);
			}
			return;
		}
		int at = static_cast<int>(std::find(all.begin(), all.end(), selected) - all.begin());
		if (at >= static_cast<int>(all.size())) {
			at = 0;
			selected = all[0];
		}
		const int n = static_cast<int>(all.size());
		int to = at;
		if (p & PAD_UP) {
			to = std::max(0, at - 1);
		}
		if (p & PAD_DOWN) {
			to = std::min(n - 1, at + 1);
		}
		if (p & PAD_LEFT) {
			to = std::max(0, at - 10);
		}
		if (p & PAD_RIGHT) {
			to = std::min(n - 1, at + 10);
		}
		if (to != at) {
			selected = all[static_cast<size_t>(to)];
			(entries[static_cast<size_t>(selected)].kind == LibraryEntry::Image ? lastGame : lastCard) = selected;
		}
		homeActions(p);
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

	void homeInput(uint32_t p)
	{
		if (!startPath.empty()) {
			return;
		}
		if (prefs.global("home_layout") == "list") {
			homeListInput(p);
			return;
		}
		homeCacheCheck();
		const std::vector<int> &games = hc.games, &cards = hc.cards;
		const LibraryEntry *e = selectedEntry();
		const bool onGame = e && e->kind == LibraryEntry::Image;
		const std::vector<int> &row = onGame ? games : cards;
		const int at = static_cast<int>(std::find(row.begin(), row.end(), selected) - row.begin());
		const auto choose = [&](int entry) {
			if (entry != selected) {
				selected = entry;
				(entries[static_cast<size_t>(entry)].kind == LibraryEntry::Image ? lastGame : lastCard) = entry;
			}
		};
		if ((p & PAD_LEFT) && at > 0 && at < static_cast<int>(row.size())) {
			choose(row[static_cast<size_t>(at - 1)]);
		}
		if ((p & PAD_RIGHT) && at + 1 < static_cast<int>(row.size())) {
			choose(row[static_cast<size_t>(at + 1)]);
		}
		if ((p & PAD_DOWN) && onGame && !cards.empty()) {
			lastGame = selected;
			choose(std::find(cards.begin(), cards.end(), lastCard) != cards.end() ? lastCard : cards.front());
		}
		if ((p & PAD_UP) && !onGame && !games.empty()) {
			lastCard = selected;
			choose(std::find(games.begin(), games.end(), lastGame) != games.end() ? lastGame : games.front());
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
			v.tabs.push_back(t == TabInternal ? "Games" : tabName(t));
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
		{   /* 0.0.3: the covers downloading: how many of how many, a bar under the head */
			unsigned done = 0, total = 0;
			ogbox::Covers::get().progress(done, total);
			if (total > 0 && prefs.global("list_covers") != "0") {
				v.coverProgress = static_cast<float>(done) / static_cast<float>(total);
				v.coverText = "Covers " + std::to_string(done) + " of " + std::to_string(total) + " \xC2\xB7 " +
					std::to_string(static_cast<int>(100.0f * v.coverProgress)) + "%";
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
		v.cardsSub = folder.empty() ? (games.empty() ? std::string() : std::string("the games here were played last")) : folder;
		for (const int i : cards) {
			const LibraryEntry &e = entries[static_cast<size_t>(i)];
			ui::HomeCard c;
			switch (e.kind) {
			case LibraryEntry::Place:
				c.title = e.name.rfind("Network: ", 0) == 0 ? e.name.substr(9) : e.name;
				c.sub = e.path;
				c.icon = e.path.rfind("/mnt/usb", 0) == 0 || e.path.rfind("/mnt/ext", 0) == 0 ? ui::Icon::Usb
					: e.path.rfind("/smb", 0) == 0 ? ui::Icon::Server : e.path == "/data" ? ui::Icon::Console : ui::Icon::Folder;
				break;
			case LibraryEntry::Folder:
				c.title = e.name;
				c.sub = "folder";
				c.icon = ui::Icon::Folder;
				break;
			case LibraryEntry::Up:
				c.title = "Up";
				c.sub = parentOf(folder) == "/" || folder == e.path ? std::string("the places") : baseName(parentOf(folder));
				c.icon = ui::Icon::Up;
				break;
			case LibraryEntry::Dashboard:
				c.title = "Xbox dashboard";
				c.sub = "the console's own menu";
				c.icon = ui::Icon::Dash;
				c.accent = true;
				break;
			case LibraryEntry::Hdd:
				c.title = "Xbox hard disk";
				c.sub = "saves and caches";
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
			v.empty = folder.empty() ? (tab == TabInternal ? "No game played yet. Choose a place below: your games' folders are there."
				: tab == TabUsb ? "No game from a USB drive played yet. Choose a drive below."
				: "No game from a share played yet. Choose a share below, or add one.")
				: "No Xbox disc images (.iso, .xiso, .chd, .cci) in this folder.";
			if (folder.empty() && cards.empty()) {
				v.empty = tab == TabUsb ? "No USB drive is plugged in (or Settings > Game list hides them)."
					: tab == TabNetwork ? "No network share yet: add one in Settings > Network."
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
			v.footer.push_back({ "T", "Game page" });
			v.footer.push_back({ "L2 R2", "A to Z" });
		} else {
			v.footer = { { "X", "Open" } };
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
		SP_GAME_RESET };

	std::vector<int> setPages() const
	{
		if (forGame) {
			return { SP_PICTURE, SP_XBOX, SP_ADVANCED, SP_GAME_RESET };
		}
		return { SP_PICTURE, SP_XBOX, SP_ADVANCED, SP_NETWORK, SP_LIST, SP_INTERFACE, SP_DATA, SP_HDD, SP_UPDATES, SP_ABOUT };
	}

	static const char *setPageName(int page)
	{
		switch (page) {
		case SP_PICTURE: return "Picture and sound";
		case SP_XBOX: return "The Xbox";
		case SP_ADVANCED: return "Advanced";
		case SP_NETWORK: return "Network";
		case SP_LIST: return "Game list";
		case SP_INTERFACE: return "Interface";
		case SP_DATA: return "Data folder";
		case SP_HDD: return "Xbox hard disk";
		case SP_UPDATES: return "Updates";
		case SP_ABOUT: return "About";
		default: return "Use the app's";
		}
	}

	/* The options each layer shows, in groups ("" none); what no layer names goes to Advanced. */
	static std::vector<std::pair<std::string, std::vector<std::string>>> setGroups(int page)
	{
		switch (page) {
		case SP_PICTURE:
			return { { "", { "render_scale", "aspect", "upscale", "fxaa", "aniso", "sharpen", "force32", "dither", "crop_overscan", "audio_dsp", "show_fps" } } };
		case SP_XBOX:
			return { { "", { "language", "av_pack", "screen_format" } }, { "Video modes", { "480p", "720p", "1080i", "pal60" } },
				{ "Clock", { "clock_days", "clock_hours" } } };
		case SP_ADVANCED: {
			std::vector<std::pair<std::string, std::vector<std::string>>> g = {
				{ "Timing", { "frame_pacing", "uncapped", "console_timing" } },
				{ "Graphics", { "gpu_cores", "async_shaders", "prebuild_shaders", "exact_depth" } },
				{ "Sound and memory", { "audio_resampler", "console_ram" } },
			};
			std::vector<std::string> other;
			for (const ogbox::Option &o : ogbox::options()) {
				bool placed = false;
				for (int p2 : { SP_PICTURE, SP_XBOX, SP_NETWORK, SP_LIST, SP_INTERFACE, SP_UPDATES }) {
					for (const auto &grp : setGroups(p2)) {
						placed = placed || std::find(grp.second.begin(), grp.second.end(), o.key) != grp.second.end();
					}
				}
				for (const auto &grp : g) {
					placed = placed || std::find(grp.second.begin(), grp.second.end(), o.key) != grp.second.end();
				}
				const std::string k = o.key;
				if (!placed && k != "list_layout" && k != "list_details") {
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
		case SP_UPDATES:
			return { { "", { "update_check" } } };
		default:
			return {};
		}
	}

	/* A value set from the settings, with what follows from it at once. */
	void setValue(const ogbox::Option &o, const std::string &next)
	{
		const std::string key = o.key;
		if (forGame) {
			const std::string own = prefs.override_(key);
			prefs.setOverride(key, next == prefs.global(key) && own.empty() ? std::string() : next);
			return;
		}
		prefs.setGlobal(key, next);
		if (key == "rain") {
			rainOn = next == "1";
		} else if (key == "theme") {
			themeCode = next == "code";
		} else if (key == "show_fps") {
			showFps = next == "1";
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
		r.desc = o.description;
		if (!o.text && !o.now) {
			r.desc += " Takes effect at the next game start.";
		}
		if (!o.text) {
			r.desc += " Default: " + ogbox::Settings::label(o, o.def) + ".";
		}
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
		case SP_UPDATES:
			actionRow("check", "Look for an update now", updateLine(), "Asks GitHub whether a newer OGBox is out and offers it with "
				"what is new. Nothing is downloaded without asking; your settings, the hard disk, states and games stay.",
				[this]() { checkForUpdate(); }, rows, acts, ui::Icon::Check);
			break;
		case SP_ABOUT:
			actionRow("ver", "OGBox", pshakux_build_label(), "OGBox by Press5elect: hakuX for the PS5.", nullptr, rows, acts);
			groupRow("The controller", rows, acts);
			actionRow("c1", "Cross, Circle, Square, Triangle", "A, B, X, Y", "", nullptr, rows, acts, ui::Icon::Pad);
			actionRow("c2", "L1, R1", "white, black", "", nullptr, rows, acts);
			actionRow("c3", "L2, R2", "the triggers", "", nullptr, rows, acts);
			actionRow("c4", prefs.global("quick_menu") == "options_hold" ? "OPTIONS (held), touch pad" : "OPTIONS, touch pad",
				prefs.global("quick_menu") == "options_hold" ? "start (quick menu), back" : "quick menu, back | start", "", nullptr, rows, acts);
			actionRow("c5", "L3 + R3 held", "a screenshot", "", nullptr, rows, acts);
			break;
		case SP_GAME_RESET:
			actionRow("reset", "Use the app's settings for everything", std::to_string(prefs.overrides()) + " of its own",
				"Every setting of this game back to the app's.", [this]() {
					prefs.clearOverrides();
					pshakux_log("settings: %s: every setting back to the app's", forGameKey.c_str());
					showToast("This game follows the app's settings again");
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
		case SP_ADVANCED: *warn = true; return "can break games";
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
		case SP_ABOUT: return pshakux_build_label();
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
				openPopup("Reset " + std::string(setPageName(page)) + "?", forGame ? "This game's own settings here go; it follows "
					"the app's again." : "Every setting here back to how OGBox comes.", { "No", "Yes, reset them" }, [this, acts](int i) {
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
		v.subtitle = forGame ? forGameTitle + " \xC2\xB7 this game's own" : std::string("for every game");
		v.right = forGame ? forGameKey : std::string("text fields open a keyboard; a USB keyboard types too");
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
			v.banner = "These change how games run. The defaults are the most accurate; change one when a game asks for it, "
				"and change it back if anything gets worse.";
			v.bannerWarn = true;
		} else if (forGame && page != SP_GAME_RESET) {
			v.banner = "Only this game's: a setting marked \"as the app's\" follows Settings; TRIANGLE puts one back.";
		}
		std::vector<RowAct> acts;
		settingsModel(page, v.rows, acts);
		v.rowSel = setRowSel;
		if (setFocusNav) {
			v.footer = { { "UD", "Choose" }, { "X", "Open" }, { "O", forGame && !library ? "Back to the game" : "Back" } };
		} else {
			const ui::SetRow *r = setRowSel >= 0 && setRowSel < static_cast<int>(v.rows.size()) ? &v.rows[static_cast<size_t>(setRowSel)] : nullptr;
			v.footer = { { "UD", "Choose" } };
			if (r && (r->kind == ui::SetRow::Kind::Choice || r->kind == ui::SetRow::Kind::Number || r->kind == ui::SetRow::Kind::Toggle)) {
				v.footer.push_back({ "LR", "Change" });
				v.footer.push_back({ "T", forGame ? "As the app's" : "Default" });
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
		ImDrawList *dl;
		if (library) {
			dl = beginUiScreen("##settings");
		} else {
			// in a game: over the paused picture, darkened
			ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
			ImGui::SetNextWindowSize(S, ImGuiCond_Always);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::Begin("##gamesettings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
			dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(8, 10, 13, 236));
		}
		ui::drawSettings(dl, S, v);
		if (form.open) {
			drawShareForm(dl, S);
		}
		if (library) {
			endUiScreen();
		} else {
			ImGui::End();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
		}
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

	std::vector<int> gameActs() const
	{
		std::vector<int> a = { GA_PLAY };
		if (gp.info.ok && ogbox::stateSlot(gp.info.key, ogbox::kQuitSlot).used) {
			a.push_back(GA_FRESH);
		}
		if (gp.info.ok && !usedStateSlots(gp.info.key).empty()) {
			a.push_back(GA_STATES);
		}
		if (gp.info.ok) {
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
			if (p & (PAD_CIRCLE | PAD_UP | PAD_DOWN)) {
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
		if (p & PAD_UP) {
			gp.pill = std::max(0, gp.pill - 1);
		}
		if (p & PAD_DOWN) {
			gp.pill = std::min(n - 1, gp.pill + 1);
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
			swallowNav = true;
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
			case GA_PLAY: v.actions.push_back({ left.used ? "Continue" : "Play", left.used ? "where I left off" : "" }); break;
			case GA_FRESH: v.actions.push_back({ "Start from the beginning", "" }); break;
			case GA_STATES: v.actions.push_back({ "Load a save state", std::to_string(slots.size()) + " saved" }); break;
			case GA_SETTINGS: v.actions.push_back({ "Settings for this game", own ? std::to_string(own) + " of its own" : "as the app's" }); break;
			case GA_MODS: v.actions.push_back({ "Mods, patches and cheats", extras ? std::to_string(extras) + " for it" +
				(onCount ? ", " + std::to_string(onCount) + " on" : std::string()) : std::string("none known") }); break;
			case GA_SAVES: v.actions.push_back({ "Saves on the Xbox hard disk", "copy, bring in, delete" }); break;
			}
		}
		v.actionSel = gp.pill;
		v.footer = gp.states ? std::vector<ui::Hint>{ { "LR", "Choose" }, { "X", "Load" }, { "O", "Back" } }
			: std::vector<ui::Hint>{ { "UD", "Choose" }, { "X", "Open" }, { "O", "Back" } };
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

	/* 0.0.3: the look (Settings > Interface > Theme): Dark, ogbox_ui's, for every screen (the older
	 * ImGui ones take its colours through mx and the style); Code, the black and green with the
	 * falling code. 0.1.52's Emerald is gone (shown as Dark). */
	void applyTheme(bool code)
	{
		themeApplied = code ? 1 : 0;
		mx::emerald = false;
		ui::setTheme(code ? ui::Theme::Code : ui::Theme::Dark);
		ImGuiStyle &st = ImGui::GetStyle();
		ImVec4 *c = st.Colors;
		const float k = static_cast<float>(height) / 1080.0f;
		if (code) {
			mx::bright = ImVec4(0.0f, 1.0f, 0.255f, 1.0f);
			mx::text = ImVec4(0.49f, 1.0f, 0.61f, 1.0f);
			mx::label = ImVec4(0.24f, 0.78f, 0.40f, 1.0f);
			mx::line = ImVec4(0.05f, 0.36f, 0.13f, 1.0f);
			mx::panel = ImVec4(0.0f, 0.03f, 0.01f, 0.86f);
			mx::head = ImVec4(0.71f, 1.0f, 0.78f, 1.0f);
			c[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.94f);
			c[ImGuiCol_Button] = ImVec4(0.0f, 0.20f, 0.07f, 0.9f);
			c[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.55f, 0.16f, 1.0f);
			c[ImGuiCol_Header] = ImVec4(0.0f, 0.35f, 0.11f, 0.85f);
			c[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.55f, 0.16f, 0.9f);
			c[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.75f, 0.20f, 1.0f);
			c[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.10f, 0.03f, 0.9f);
		} else {
			mx::bright = ImVec4(0.239f, 0.863f, 0.518f, 1.0f);   // #3ddc84, the accent
			mx::text = ImVec4(0.902f, 0.914f, 0.933f, 1.0f);     // #e6e9ee
			mx::label = ImVec4(0.545f, 0.584f, 0.631f, 1.0f);    // #8b95a1
			mx::line = ImVec4(1.0f, 1.0f, 1.0f, 0.16f);
			mx::panel = ImVec4(0.071f, 0.086f, 0.106f, 0.90f);   // #12161b
			mx::head = ImVec4(0.5f, 0.94f, 0.68f, 1.0f);
			c[ImGuiCol_WindowBg] = ImVec4(0.043f, 0.051f, 0.063f, 0.96f);
			c[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
			c[ImGuiCol_ButtonHovered] = ImVec4(0.239f, 0.863f, 0.518f, 0.55f);
			c[ImGuiCol_Header] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
			c[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.14f);
			c[ImGuiCol_HeaderActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.18f);
			c[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
		}
		const float r = code ? 0.0f : 10.0f * k;
		st.ChildRounding = st.FrameRounding = st.WindowRounding = st.ScrollbarRounding = st.GrabRounding = r;
		c[ImGuiCol_ButtonActive] = mx::bright;
		c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.3f);
		c[ImGuiCol_Text] = mx::text;
		c[ImGuiCol_TextDisabled] = mx::label;
		c[ImGuiCol_ChildBg] = mx::panel;
		c[ImGuiCol_Border] = mx::line;
		c[ImGuiCol_ScrollbarGrab] = mx::line;
		c[ImGuiCol_ScrollbarGrabHovered] = mx::label;
		c[ImGuiCol_ScrollbarGrabActive] = mx::bright;
		c[ImGuiCol_NavHighlight] = mx::bright;
		c[ImGuiCol_Separator] = mx::line;
		c[ImGuiCol_CheckMark] = mx::bright;
		pshakux_log("ogbox: theme %s", code ? "Code" : "Dark");
	}

	/* OGBox's logo (Press5elect's, assets/ogbox-logo.png) as an image ImGui draws, once. */
	ImTextureID logoImage()
	{
		if (!logoTried) {
			logoTried = true;
			if (!logoPixels.empty() && logoWidth && logoHeight) {
				logoTex.fromBuffer(logoPixels.data(), static_cast<VkDeviceSize>(logoPixels.size()), VK_FORMAT_R8G8B8A8_UNORM,
					logoWidth, logoHeight, vulkanDevice, queue);
				logoSet = ui.addTexture(logoTex.sampler, logoTex.view);
			}
		}
		return logoSet ? reinterpret_cast<ImTextureID>(logoSet) : nullptr;
	}
};

VULKAN_EXAMPLE_MAIN()
