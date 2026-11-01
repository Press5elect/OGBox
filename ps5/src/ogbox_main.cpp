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
 *    Xbox disc images (.iso, .xiso). D-pad to move, CROSS to open a folder or start a game,
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
#include "platform/vfs/ps5_vfs.h"
#include "core/ogbox_core.h"
#include "core/ogbox_mods.h"
#include "ogbox_settings.h"

#include "stb_image.h"   // (its implementation is the base class's: base/VulkanglTFModel.cpp)

extern "C" int pshakux_props_load(const char *path);
extern "C" int pshakux_prop_set(const char *name, const char *value);

namespace {

constexpr const char *kBootFile = "/data/homebrew/PPSA99251/boot.txt";
constexpr const char *kDataRoot = "/data/ogbox";
constexpr double kQuitHoldSeconds = 2.0;
constexpr const char *kLibraryFile = "/data/ogbox/library.txt";
constexpr const char *kNetworkFile = "/data/ogbox/network.cfg";

struct BootConfig {
	std::string image;
	std::string hdd = std::string(kDataRoot) + "/hdd";
	std::vector<std::string> options;
	std::string source;
	std::string bootTxtImage;   // boot.txt's first line (0.1.14: boots only with "autoboot")
	bool autoboot{ false };
	std::string gameKey;        // 0.1.26: --game <title ID>.<version> (the game list read it)
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
	enum Kind { Place, Folder, Image, Up, Quit, Settings, Dashboard } kind;
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
	return !strcasecmp(ext, "iso") || !strcasecmp(ext, "xiso");
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

/* The places the list starts from: internal storage, the drives that are there, the shares. */
std::vector<LibraryEntry> listPlaces()
{
	std::vector<LibraryEntry> places;
	const std::string titleGames = std::string(pshakux_title_dir()) + "/games";
	if (isFolder(titleGames)) {
		places.push_back({ LibraryEntry::Place, "OGBox's games folder", titleGames, 0, titleGames });
	}
	places.push_back({ LibraryEntry::Place, "Internal storage", "/data", 0, "/data" });
	for (int i = 0; i < 8; i++) {
		const std::string usb = "/mnt/usb" + std::to_string(i);
		if (isFolder(usb)) {
			places.push_back({ LibraryEntry::Place, "USB drive " + std::to_string(i), usb, 0, usb });
		}
	}
	for (int i = 0; i < 2; i++) {
		const std::string ext = "/mnt/ext" + std::to_string(i);
		if (isFolder(ext)) {
			places.push_back({ LibraryEntry::Place, "Extended storage " + std::to_string(i), ext, 0, ext });
		}
	}
	for (unsigned i = 0;; i++) {
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
		const int mounted = ps5vfs_load_network_config(chosen.c_str());
		g_networkStatus = std::to_string(mounted < 0 ? 0 : mounted) + " share(s) from " + chosen;
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

/* OGBox 0.1.26: one disc read at a time for the game list's details (the share's disc layer has
 * one disc descriptor; the mods' catalog is the core's one table). */
std::mutex g_discInfoMutex;

} // namespace

// ---- OGBox 0.1.26: the look (black and green, "the code"), in the overlay's hooks ------------

namespace mx {
const ImVec4 bright(0.0f, 1.0f, 0.255f, 1.0f);    // #00ff41: titles, values, the selection
const ImVec4 text(0.49f, 1.0f, 0.61f, 1.0f);      // #7dff9c: the body
const ImVec4 label(0.24f, 0.78f, 0.40f, 1.0f);    // #3dc766: labels, hints
const ImVec4 line(0.05f, 0.36f, 0.13f, 1.0f);     // #0d5c22: borders
const ImVec4 panel(0.0f, 0.03f, 0.01f, 0.86f);    // the panels over the rain
const ImVec4 head(0.71f, 1.0f, 0.78f, 1.0f);      // #b6ffc8: a drop's head
const ImVec4 fail(1.0f, 0.45f, 0.38f, 1.0f);
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
	double shotHeldSince{ -1.0 };             // L3 + R3 held since (a screenshot)
	bool shotWanted{ false };
	unsigned shotsTaken{ 0 };

	// ---- OGBox 0.1.26: the menus (the boot screen, the game list, the settings, the mods) ----
	enum class Screen { Boot, Library, Settings, Mods };
	Screen screen{ Screen::Library };
	double screenSince{ 0.0 };
	struct pad menuPad{};                     // player 0's, `pressed` against the last frame's
	ogbox::Settings prefs;                    // the app's; in a run, with the game's own on top
	std::vector<LibraryEntry> places;         // the left column
	double placesRead{ -100.0 };
	// the selected disc's details, read on a thread of their own once the selection rests
	struct Details {
		std::mutex mutex;
		bool done{ false };
		OgboxGameInfo info{};
		int mods{ 0 }, modsOn{ 0 }, cheats{ 0 }, cheatsOn{ 0 }, overrides{ 0 };
	};
	std::shared_ptr<Details> details;
	std::string detailsPath, restingPath;
	double restingSince{ 0.0 };
	// the settings screen: the app's, or one game's
	int section{ 0 };
	bool forGame{ false };
	std::string forGameKey, forGameTitle;
	std::string help;
	bool focusList{ false };
	bool wantFocus{ true };                   // the next screen's window takes the controller
	bool modsFocus{ false };
	bool swallowNav{ false };                 // this frame's CROSS and CIRCLE are not ImGui's
	// the mods and cheats screen
	std::vector<OgboxModEntry> modList;
	std::string modsKey, modsTitle;
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
	double fpsSince{ -1.0 };
	uint64_t fpsPictures{ 0 }, fpsDraws{ 0 };
	float fps{ 0.0f }, drawRate{ 0.0f };
	std::vector<std::string> bootLines;

	/* Opens a folder ("" for the places), listing it on a thread of its own. */
	void openFolder(const std::string &path, const std::string &select = std::string())
	{
		folder = path;
		selectName = select;
		entries.clear();
		listError.clear();
		selected = 0;
		focusSet = false;
		listing.reset();
		if (path.empty()) {
			entries = listPlaces();
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
		case LibraryEntry::Image:
			startPath = entry.path;
			startDelay = 2;   // a frame or two to say so before the relaunch
			pshakux_log("library: starting %s", entry.path.c_str());
			break;
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
			break;
		}
	}

	/* Back to the game list from a game: the title starts again with no game named. */
	/* OGBox 0.1.25: the boot log on the disk now: a console that goes down while a game is left
	 * keeps the lines that say how far the exit got. */
	static void syncLog()
	{
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
		const char *args[] = { nullptr };
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
		quiesceForExit();
		pshakux_relaunch(args.data());
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
		mkdir(kDataRoot, 0777);
		chmod(kDataRoot, 0777);
		const std::string props = std::string(kDataRoot) + "/props.txt";
		const int loaded = pshakux_props_load(props.c_str());
		if (loaded >= 0) {
			pshakux_log("ogbox: %d properties from %s", loaded, props.c_str());
		}
		ps5.ownOverlay = true;   // OGBox lays out its own windows (the game list, the status)
		startFileLayer();
		/* boot.txt's options every run (0.1.14: a relaunch's arguments no longer leave them out);
		 * then the arguments, which win */
		readBootConfig(boot);
		const bool named = readArguments(boot);
		if (!named && boot.autoboot && !boot.bootTxtImage.empty()) {
			boot.image = boot.bootTxtImage;
			boot.source = boot.source + " (autoboot)";
		}
		if (boot.image.empty()) {
			library = true;
			prefs.load();
			rainOn = prefs.on("rain");
			screen = Screen::Boot;
			screenSince = now_seconds();
			bootLines = {
				std::string("> OGBox ") + pshakux_build_version() + " - hakuX native, for PS5",
				"> settings: /data/ogbox/settings.txt",
				"> mods: the title folder's mods/, chosen in /data/ogbox/mods.txt",
				"> network shares: " + g_networkStatus,
				"> the game list: " + std::string(readLastFolder().empty() ? "the places" : readLastFolder()),
			};
			std::string last = readLastFolder();
			if (last.empty() && !boot.bootTxtImage.empty()) {
				last = parentOf(boot.bootTxtImage);
			}
			if (!last.empty() && isFolder(last)) {
				openFolder(last);
			} else {
				openFolder(std::string());
			}
			pshakux_log("ogbox: the game list (%s)", folder.empty() ? "the places" : folder.c_str());
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
		if (key.empty() && boot.image != "none") {
			OgboxGameInfo info{};
			if (ogbox_game_info(boot.image.c_str(), &info)) {
				key = info.key;
			}
		}
		prefs.load(key);
		aspectMode = prefs.value("aspect");
		fxaaOn = prefs.on("fxaa");
		sgsrOn = prefs.value("upscale") == "sgsr";
		showFps = prefs.on("show_fps");
		static std::vector<std::string> fromSettings;
		fromSettings = prefs.coreArgs();
		std::string said;
		for (const std::string &option : fromSettings) {
			argv.push_back(option.c_str());
			said += " " + option;
		}
		pshakux_log("ogbox: settings%s%s:%s; the picture: aspect %s, FXAA %s, upscaling %s, FPS counter %s",
			key.empty() ? "" : " for ", key.c_str(), said.c_str(), aspectMode.c_str(), fxaaOn ? "on" : "off",
			sgsrOn ? "SGSR" : "bilinear", showFps ? "on" : "off");
		if (prefs.overrides()) {
			pshakux_log("ogbox: %s has %d settings of its own (%s)", key.c_str(), prefs.overrides(),
				ogbox::gameSettingsPath(key).c_str());
		}
		for (const std::string &option : boot.options) {
			argv.push_back(option.c_str());
		}
		const std::string cache = std::string(kDataRoot) + "/cache";
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
		} variants[] = { { "ogbox/fxaa.frag.spv", &pipelineFxaa }, { "ogbox/sgsr.frag.spv", &pipelineSgsr } };
		for (const auto &v : variants) {
			stages[1] = loadShader(getShadersPath() + v.file, VK_SHADER_STAGE_FRAGMENT_BIT);
			pipelineCI.pStages = stages.data();
			if (vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, v.out) != VK_SUCCESS) {
				*v.out = VK_NULL_HANDLE;
				pshakux_log("presenter: %s cannot be made: that filter is off", v.file);
			}
		}
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
		if (!pshx_core_take_frame(&frameSeen, s.mapped, w * 4, w, h, &fw, &fh)) {
			return;
		}
		ensurePicture(fw, fh);
		upload = true;
		havePicture = true;
		showingLogo = false;
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
			return aspect;   // the logo at its own shape
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
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			VkRect2D scissor{ { static_cast<int32_t>(x), static_cast<int32_t>(y) }, { static_cast<uint32_t>(w), static_cast<uint32_t>(h) } };
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
			VkPipeline chosen = pipeline;
			if (havePicture && !showingLogo) {
				const bool smaller = static_cast<float>(picture.width) < w - 0.5f || static_cast<float>(picture.height) < h - 0.5f;
				if (fxaaOn && pipelineFxaa != VK_NULL_HANDLE) {
					chosen = pipelineFxaa;
				} else if (sgsrOn && smaller && pipelineSgsr != VK_NULL_HANDLE) {
					chosen = pipelineSgsr;
				}
			}
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, chosen);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
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
					pshx_core_set_pad(static_cast<unsigned>(player), &x);
					break;
				}
			}
			float large = 0.0f, small = 0.0f;
			pshx_core_rumble(static_cast<unsigned>(player), &large, &small);
			pad_player_vibrate(player, large, small);
		}
		struct pad first{};
		pad_player(0, &first);
		const uint32_t combo = PAD_OPTIONS | PAD_TOUCH_PAD;
		const double now = now_seconds();
		if ((first.held & combo) == combo) {
			if (quitHeldSince < 0.0) {
				quitHeldSince = now;
				pshakux_log("ogbox: OPTIONS and the touch pad down");
				syncLog();
			} else if (now - quitHeldSince >= kQuitHoldSeconds) {
				pshakux_log("ogbox: OPTIONS and the touch pad held for %.0f s: back to the game list", kQuitHoldSeconds);
				quitHeldSince = now + 1e9;   // once
				relaunchToLibrary();
			}
		} else {
			quitHeldSince = -1.0;
		}
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
		if (screen == Screen::Boot) {
			if ((pressed & (PAD_CROSS | PAD_CIRCLE)) || now_seconds() - screenSince > 3.2) {
				screen = Screen::Library;
				screenSince = now_seconds();
				focusSet = false;
				wantFocus = true;
				swallowNav = (pressed & (PAD_CROSS | PAD_CIRCLE)) != 0;   // the skip's press is not the list's
			}
		} else if (screen == Screen::Library) {
			if ((pressed & PAD_CIRCLE) && startPath.empty()) {
				goUp();
			}
			if ((pressed & PAD_TRIANGLE) && startPath.empty()) {
				openSettings(std::string(), std::string());
			}
		} else if (pressed & PAD_CIRCLE) {
			screen = Screen::Library;
			screenSince = now_seconds();
			focusSet = false;
			wantFocus = true;
		}
		const uint32_t combo = PAD_OPTIONS | PAD_TOUCH_PAD;
		const double now = now_seconds();
		if ((menuPad.held & combo) == combo) {
			if (quitHeldSince < 0.0) {
				quitHeldSince = now;
			} else if (now - quitHeldSince >= kQuitHoldSeconds) {
				pshakux_log("ogbox: OPTIONS and the touch pad held in the game list: quitting");
				quit = true;
			}
		} else {
			quitHeldSince = -1.0;
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
		screen = Screen::Settings;
		screenSince = now_seconds();
		wantFocus = true;
		pshakux_log("ogbox: settings%s%s", forGame ? " of " : "", forGame ? key.c_str() : "");
	}

	void openMods(const std::string &key, const std::string &title)
	{
		modsKey = key;
		modsTitle = title;
		std::lock_guard<std::mutex> disc(g_discInfoMutex);
		modList.assign(160, OgboxModEntry{});
		modList.resize(static_cast<size_t>(std::max(0, ogbox_mods_catalog(key.c_str(), modList.data(), 160))));
		screen = Screen::Mods;
		screenSince = now_seconds();
		help.clear();
		wantFocus = true;
		modsFocus = false;
	}

	void renderLibrary()
	{
		collectListing();
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
				args = { "--boot", path.c_str() };
				if (!key.empty()) {
					args.push_back("--game");
					args.push_back(key.c_str());
				}
			}
			args.push_back(nullptr);
			quiesceForExit();
			const int r = pshakux_relaunch(args.data());
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
			if (!library) {
				std::memset(io.NavInputs, 0, sizeof(io.NavInputs));   // the game's pad
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
		fetchPicture();
		if (shotWanted && upload && havePicture) {
			shotWanted = false;
			takeShot();
		}
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
		framesDrawn++;
	}

	/* The frame just taken from the core (this frame's staging buffer) to /data/ogbox/shots. */
	void takeShot()
	{
		const std::string folder = std::string(kDataRoot) + "/shots";
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
		ImGui::PushStyleColor(ImGuiCol_WindowBg, transparent ? ImVec4(0, 0, 0, 0) : ImVec4(0, 0, 0, 1));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad * 1.4f, pad));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pad * 0.4f, pad * 0.3f));
		if (wantFocus || (screen == Screen::Library && !focusSet)) {
			ImGui::SetNextWindowFocus();
			wantFocus = false;
		}
		const bool open = ImGui::Begin(name, nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoCollapse);
		drawRain(ImGui::GetWindowDrawList(), rainAlpha, hole);
		return open;
	}

	void endScreen()
	{
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);
	}

	/* A title in large letters with a glow (drawn three times, the outer ones faint). */
	void glowTitle(const char *text, float scale)
	{
		ImGui::SetWindowFontScale(scale);
		const ImVec2 at = ImGui::GetCursorScreenPos();
		ImDrawList *dl = ImGui::GetWindowDrawList();
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

	/* The hints at the foot of a screen. */
	void footer(const char *hints, const std::string &second = std::string())
	{
		const float lines = second.empty() ? 1.0f : 2.0f;
		ImGui::SetCursorPosY(static_cast<float>(height) - ImGui::GetStyle().WindowPadding.y -
			ImGui::GetTextLineHeightWithSpacing() * lines);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const ImVec2 at = ImGui::GetCursorScreenPos();
		dl->AddLine(ImVec2(at.x, at.y - 4.0f), ImVec2(static_cast<float>(width) - at.x, at.y - 4.0f), mx::u32(mx::line), 2.0f);
		ImGui::TextColored(mx::label, "%s", hints);
		ImGui::SameLine(static_cast<float>(width) - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize("OGBox by Press5elect").x);
		ImGui::TextColored(mx::line, "OGBox by Press5elect");
		if (!second.empty()) {
			ImGui::TextColored(mx::label, "%s", second.c_str());
		}
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
		const std::string right = std::string("OGBox ") + pshakux_build_version();
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
		ImGui::SetCursorScreenPos(ImVec2(W - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize("CROSS: skip").x,
			H - ImGui::GetStyle().WindowPadding.y - ImGui::GetTextLineHeightWithSpacing()));
		ImGui::TextColored(mx::line, "CROSS: skip");
		endScreen();
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
		/* (not while a folder is being listed: one request at a time to a share's server) */
		if (path.empty() || path == detailsPath || now - restingSince < 0.25 || listing) {
			return;
		}
		detailsPath = path;
		auto job = std::make_shared<Details>();
		details = job;
		std::thread([job, path]() {
			std::lock_guard<std::mutex> disc(g_discInfoMutex);
			OgboxGameInfo info{};
			ogbox_game_info(path.c_str(), &info);
			int mods = 0, modsOn = 0, cheats = 0, cheatsOn = 0, overrides = 0;
			if (info.ok) {
				std::vector<OgboxModEntry> list(160);
				const int n = ogbox_mods_catalog(info.key, list.data(), 160);
				for (int i = 0; i < n; i++) {
					const OgboxModEntry &m = list[static_cast<size_t>(i)];
					(m.cheat ? cheats : mods)++;
					if (m.on) {
						(m.cheat ? cheatsOn : modsOn)++;
					}
				}
				ogbox::Settings s;
				s.load(info.key);
				overrides = s.overrides();
			}
			std::lock_guard<std::mutex> lock(job->mutex);
			job->info = info;
			job->mods = mods;
			job->modsOn = modsOn;
			job->cheats = cheats;
			job->cheatsOn = cheatsOn;
			job->overrides = overrides;
			job->done = true;
		}).detach();
	}

	/* A row in a list: the text, a note on the right; true when chosen (CROSS). */
	bool row(const std::string &id, const std::string &text, const std::string &note, bool current, const ImVec4 &color)
	{
		ImGui::PushID(id.c_str());
		const float h = ImGui::GetTextLineHeight() * 1.5f;
		const ImVec2 at = ImGui::GetCursorScreenPos();
		const bool chosen = ImGui::Selectable("##row", current, 0, ImVec2(0.0f, h));
		const bool focused = ImGui::IsItemFocused();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const float ty = at.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
		const float pad = ImGui::GetStyle().FramePadding.x;
		const ImU32 c = focused ? mx::u32(ImVec4(0, 0, 0, 1)) : mx::u32(color);
		if (focused) {
			const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
			const float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(now_seconds() * 4.0));
			dl->AddRectFilled(at, ImVec2(right, at.y + h), mx::u32(mx::bright, pulse));
		}
		dl->AddText(ImVec2(at.x + pad, ty), c, text.c_str());
		if (!note.empty()) {
			const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
			const float nw = ImGui::CalcTextSize(note.c_str()).x;
			dl->AddText(ImVec2(right - nw - pad, ty), focused ? c : mx::u32(mx::label), note.c_str());
		}
		ImGui::PopID();
		return chosen;
	}

	void drawLibrary()
	{
		ensureDetails();
		if (placesRead < 0.0 || now_seconds() - placesRead > 5.0) {
			places = listPlaces();
			placesRead = now_seconds();
		}
		beginScreen("##library", 0.35f);
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const ImGuiStyle &style = ImGui::GetStyle();
		// the head
		glowTitle("OGBOX", 1.9f);
		ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.55f);
		ImGui::TextColored(mx::label, "%s", folder.empty() ? "/ the places" : folder.c_str());
		cursor();
		const std::string version = std::string(pshakux_build_version()) + "  hakuX native";
		ImGui::SameLine(W - style.WindowPadding.x - ImGui::CalcTextSize(version.c_str()).x);
		ImGui::TextColored(mx::line, "%s", version.c_str());

		const float footH = ImGui::GetTextLineHeightWithSpacing() * 2.6f;
		const float colH = H - ImGui::GetCursorPosY() - style.WindowPadding.y - footH;
		const float leftW = W * 0.19f, rightW = W * 0.29f;
		const float midW = W - style.WindowPadding.x * 2.0f - leftW - rightW - style.ItemSpacing.x * 2.0f;
		const ImGuiWindowFlags child = ImGuiWindowFlags_NavFlattened;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x * 0.5f, style.WindowPadding.y * 0.6f));

		// the places, the settings, the dashboard, quit
		int chosenPlace = -1;
		ImGui::BeginChild("places", ImVec2(leftW, colH), true, child);
		ImGui::TextColored(mx::label, "PLACES");
		for (size_t i = 0; i < places.size(); i++) {
			const bool here = !folder.empty() && folder.compare(0, places[i].path.size(), places[i].path) == 0;
			if (row("p" + std::to_string(i), places[i].name, std::string(), false, here ? mx::bright : mx::text)) {
				chosenPlace = static_cast<int>(i);
			}
		}
		ImGui::Spacing();
		ImGui::TextColored(mx::label, "OGBOX");
		const bool openSet = row("settings", "Settings", "TRIANGLE", false, mx::text);
		const bool dash = row("dash", "Xbox dashboard", std::string(), false, mx::text);
		const bool leave = row("quit", "Quit OGBox", std::string(), false, mx::text);
		ImGui::EndChild();

		// the folder
		ImGui::SameLine();
		int chosen = -1;
		ImGui::BeginChild("entries", ImVec2(midW, colH), true, child);
		if (listing) {
			ImGui::TextColored(mx::label, "Reading the folder");
			cursor();
		}
		if (folder.empty()) {
			ImGui::TextColored(mx::label, "Choose a place on the left: your discs (.iso, .xiso) are in its folders.");
		}
		for (size_t i = 0; i < entries.size(); i++) {
			const LibraryEntry &e = entries[i];
			std::string label, note;
			ImVec4 color = mx::text;
			switch (e.kind) {
			case LibraryEntry::Folder: label = "> " + e.name; color = mx::label; break;
			case LibraryEntry::Image: label = e.name; note = e.note; break;
			case LibraryEntry::Up: label = "< the folder above"; color = mx::label; break;
			default: label = e.name; break;
			}
			if (row("e" + std::to_string(i), label, note, false, color) && startPath.empty()) {
				chosen = static_cast<int>(i);
			}
			if (!focusSet && static_cast<int>(i) == selected && screen == Screen::Library) {
				ImGui::SetItemDefaultFocus();
				ImGui::SetNavIDWithRectRel(ImGui::GetCurrentWindow()->DC.LastItemId, 0,
					ImRect(ImGui::GetItemRectMin() - ImGui::GetWindowPos(), ImGui::GetItemRectMax() - ImGui::GetWindowPos()));
				ImGui::SetScrollHere(0.5f);
			}
			if (ImGui::IsItemFocused()) {
				selected = static_cast<int>(i);
				focusSet = true;
			}
		}
		if (!listing && entries.size() <= 1 && !folder.empty() && listError.empty()) {
			ImGui::TextColored(mx::label, "No folders or Xbox disc images (.iso, .xiso) here.");
		}
		if (!listError.empty()) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(mx::fail, "%s", listError.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();

		// the selected disc
		ImGui::SameLine();
		ImGui::BeginChild("details", ImVec2(rightW, colH), true, child);
		drawDetails();
		ImGui::EndChild();
		ImGui::PopStyleVar();

		if (!startPath.empty()) {
			footer(("Starting " + (startPath == "none" ? std::string("the Xbox dashboard") : baseName(startPath)) + " ...").c_str());
		} else {
			footer("CROSS open / start    CIRCLE back    TRIANGLE settings    hold OPTIONS + touch pad: quit",
				"Network shares: " + g_networkStatus);
		}
		endScreen();

		if (chosenPlace >= 0) {
			openFolder(places[static_cast<size_t>(chosenPlace)].path);
		} else if (chosen >= 0 && chosen < static_cast<int>(entries.size())) {
			const LibraryEntry entry = entries[static_cast<size_t>(chosen)];
			activate(entry);
		} else if (openSet) {
			openSettings(std::string(), std::string());
		} else if (dash && startPath.empty()) {
			startPath = "none";
			startDelay = 2;
			pshakux_log("library: starting the Xbox dashboard");
		} else if (leave) {
			pshakux_log("library: Quit OGBox");
			quit = true;
		}
	}

	/* The right column: what is selected, and what can be done with it. */
	void drawDetails()
	{
		const LibraryEntry *e = selectedEntry();
		ImGui::TextColored(mx::label, "SELECTED");
		if (!e || e->kind != LibraryEntry::Image) {
			ImGui::Spacing();
			ImGui::PushTextWrapPos(0.0f);
			if (!e) {
				ImGui::TextColored(mx::text, "Nothing yet.");
			} else if (e->kind == LibraryEntry::Folder) {
				ImGui::TextColored(mx::text, "Folder %s", e->name.c_str());
				ImGui::TextColored(mx::label, "CROSS opens it.");
			} else if (e->kind == LibraryEntry::Up) {
				ImGui::TextColored(mx::text, "The folder above.");
			} else {
				ImGui::TextColored(mx::text, "%s", e->name.c_str());
			}
			ImGui::Spacing();
			ImGui::TextColored(mx::label, "The Xbox dashboard (on the left) starts C:\\xboxdash.xbe from the hard disk; OGBox doesn't include one.");
			ImGui::PopTextWrapPos();
			return;
		}
		bool done = false;
		OgboxGameInfo info{};
		int mods = 0, modsOn = 0, cheats = 0, cheatsOn = 0, overrides = 0;
		if (details && detailsPath == e->path) {
			std::lock_guard<std::mutex> lock(details->mutex);
			done = details->done;
			info = details->info;
			mods = details->mods;
			modsOn = details->modsOn;
			cheats = details->cheats;
			cheatsOn = details->cheatsOn;
			overrides = details->overrides;
		}
		ImGui::PushTextWrapPos(0.0f);
		ImGui::SetWindowFontScale(1.25f);
		ImGui::TextColored(mx::bright, "%s", done && info.ok && info.title[0] ? info.title : e->name.c_str());
		ImGui::SetWindowFontScale(1.0f);
		ImGui::Spacing();
		const auto field = [&](const char *name, const std::string &value) {
			ImGui::TextColored(mx::label, "%-9s", name);
			ImGui::SameLine();
			ImGui::TextColored(mx::text, "%s", value.c_str());
		};
		field("FILE", e->name);
		field("SIZE", e->note);
		if (!done) {
			field("DISC", "reading...");
		} else if (!info.ok) {
			field("DISC", "no default.xbe certificate read");
		} else {
			field("TITLE ID", info.key);
			field("MODS", std::to_string(mods) + (mods ? " (" + std::to_string(modsOn) + " on)" : std::string()));
			field("CHEATS", std::to_string(cheats) + (cheats ? " (" + std::to_string(cheatsOn) + " on)" : std::string()));
			field("SETTINGS", overrides ? std::to_string(overrides) + " of its own" : std::string("the app's"));
		}
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		ImGui::Spacing();
		const float bw = ImGui::GetContentRegionAvail().x;
		const float bh = ImGui::GetTextLineHeight() * 1.8f;
		ImGui::PushStyleColor(ImGuiCol_Button, mx::bright);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
		const bool start = ImGui::Button("START", ImVec2(bw, bh));
		ImGui::PopStyleColor(2);
		const bool own = done && info.ok && ImGui::Button("GAME SETTINGS", ImVec2(bw, bh));
		const bool modsButton = done && info.ok && ImGui::Button("MODS AND CHEATS", ImVec2(bw, bh));
		if (start && startPath.empty()) {
			activate(*e);
		} else if (own) {
			openSettings(info.key, info.title[0] ? info.title : e->name);
		} else if (modsButton) {
			openMods(info.key, info.title[0] ? info.title : e->name);
		}
	}

	/* The settings: the sections on the left, their options on the right, the focused one's words
	 * below. CROSS the next choice, SQUARE the one before, TRIANGLE (a game's) the app's again. */
	void drawSettings()
	{
		beginScreen("##settings", 0.25f);
		const float W = static_cast<float>(width), H = static_cast<float>(height);
		const ImGuiStyle &style = ImGui::GetStyle();
		glowTitle(forGame ? "GAME SETTINGS" : "SETTINGS", 1.6f);
		if (forGame) {
			ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 0.4f);
			ImGui::TextColored(mx::label, "%s  %s", forGameTitle.c_str(), forGameKey.c_str());
		}
		std::vector<ogbox::Section> sections;
		for (int i = 0; i < static_cast<int>(ogbox::Section::Count); i++) {
			const ogbox::Section sec = static_cast<ogbox::Section>(i);
			for (const ogbox::Option &o : ogbox::options()) {
				if (o.section == sec && (!forGame || o.perGame)) {
					sections.push_back(sec);
					break;
				}
			}
		}
		section = std::min(section, static_cast<int>(sections.size()) - 1);
		const float footH = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
		const float helpH = ImGui::GetTextLineHeightWithSpacing() * 3.6f;
		const float colH = H - ImGui::GetCursorPosY() - style.WindowPadding.y - footH - helpH - style.ItemSpacing.y;
		const float leftW = W * 0.2f;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x * 0.5f, style.WindowPadding.y * 0.6f));
		ImGui::BeginChild("sections", ImVec2(leftW, colH), true, ImGuiWindowFlags_NavFlattened);
		bool reset = false;
		for (size_t i = 0; i < sections.size(); i++) {
			row("s" + std::to_string(i), ogbox::sectionName(sections[i]), std::string(), static_cast<int>(i) == section,
				static_cast<int>(i) == section ? mx::bright : mx::text);
			if (!focusList && static_cast<int>(i) == section) {
				ImGui::SetItemDefaultFocus();
				ImGui::SetNavIDWithRectRel(ImGui::GetCurrentWindow()->DC.LastItemId, 0,
					ImRect(ImGui::GetItemRectMin() - ImGui::GetWindowPos(), ImGui::GetItemRectMax() - ImGui::GetWindowPos()));
				focusList = true;
			}
			if (ImGui::IsItemFocused()) {
				section = static_cast<int>(i);
				help = forGame ? "These apply to this game only; the others follow the app's settings. Changes take effect at the game's next start."
					: "OGBox's settings, hakuX's every one. Changes take effect at the next game start.";
			}
		}
		if (forGame) {
			ImGui::Spacing();
			reset = row("reset", "Use the app's for all", std::string(), false, mx::label);
			if (ImGui::IsItemFocused()) {
				help = "Every setting of this game back to the app's.";
			}
		}
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("options", ImVec2(0.0f, colH), true, ImGuiWindowFlags_NavFlattened);
		const ogbox::Section current = sections.empty() ? ogbox::Section::Graphics : sections[static_cast<size_t>(section)];
		ImGui::TextColored(mx::label, "%s", ogbox::sectionName(current));
		const uint32_t pressed = menuPad.pressed;
		for (const ogbox::Option &o : ogbox::options()) {
			if (o.section != current || (forGame && !o.perGame)) {
				continue;
			}
			const std::string own = forGame ? prefs.override_(o.key) : std::string();
			const std::string v = forGame ? (own.empty() ? prefs.global(o.key) : own) : prefs.global(o.key);
			std::string shown = ogbox::Settings::label(o, v);
			if (forGame && own.empty()) {
				shown = "App setting: " + shown;
			}
			const bool chosen = row(o.key, o.label, shown, false, forGame && !own.empty() ? mx::bright : mx::text);
			if (ImGui::IsItemFocused()) {
				help = std::string(o.description) + (o.text ? "" : " Takes effect at the next game start.");
				int by = 0;
				if (pressed & PAD_SQUARE) {
					by = -1;
				}
				if (o.choices.empty() && !o.text) {
					if (pressed & PAD_R1) {
						by = 30;
					} else if (pressed & PAD_L1) {
						by = -30;
					}
				}
				if (chosen) {
					by = 1;
				}
				if (by != 0 && !o.text) {
					const std::string next = ogbox::Settings::step(o, v, by);
					if (forGame) {
						prefs.setOverride(o.key, next == prefs.global(o.key) && own.empty() ? std::string() : next);
					} else {
						prefs.setGlobal(o.key, next);
						if (std::string(o.key) == "rain") {
							rainOn = next == "1";
						}
					}
				}
				if (forGame && (pressed & PAD_TRIANGLE)) {
					prefs.setOverride(o.key, std::string());
				}
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		// the focused option's words
		ImGui::BeginChild("help", ImVec2(0.0f, helpH), true, ImGuiWindowFlags_NoNav);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextColored(mx::text, "%s", help.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
		footer(forGame ? "CROSS next    SQUARE previous    TRIANGLE the app's    CIRCLE back"
			: "CROSS next    SQUARE previous    L1 / R1 -30 / +30 (days)    CIRCLE back");
		endScreen();
		if (reset) {
			prefs.clearOverrides();
			pshakux_log("settings: %s: every setting back to the app's", forGameKey.c_str());
		}
	}

	/* The game's mods and cheats: CROSS turns one on or off. */
	void drawMods()
	{
		beginScreen("##mods", 0.25f);
		const float H = static_cast<float>(height);
		const ImGuiStyle &style = ImGui::GetStyle();
		glowTitle("MODS AND CHEATS", 1.6f);
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
			ImGui::TextColored(mx::text, "No mods or cheats for this game.");
			ImGui::TextColored(mx::label, "OGBox has hakuX's (the title folder's mods/); a game's are in mods/%s/: a folder a mod, cheats.txt the cheats.", modsKey.c_str());
			ImGui::PopTextWrapPos();
		}
		bool first = true;
		for (size_t i = 0; i < modList.size(); i++) {
			OgboxModEntry &m = modList[i];
			if (first || (i > 0 && modList[i - 1].cheat != m.cheat)) {
				ImGui::TextColored(mx::label, "%s", m.cheat ? "CHEATS" : "MODS");
				first = false;
			}
			const std::string text = std::string(m.on ? "[ON]  " : "[OFF] ") + m.name;
			std::string note = m.cheat ? (m.once ? "once" : std::string()) : (m.refresh ? std::to_string(m.refresh) + " Hz" : std::string());
			const bool toggled = row("m" + std::to_string(i), text, note, false, m.on ? mx::bright : mx::text);
			if (!modsFocus) {
				ImGui::SetItemDefaultFocus();
				ImGui::SetNavIDWithRectRel(ImGui::GetCurrentWindow()->DC.LastItemId, 0,
					ImRect(ImGui::GetItemRectMin() - ImGui::GetWindowPos(), ImGui::GetItemRectMax() - ImGui::GetWindowPos()));
				modsFocus = true;
			}
			if (toggled) {
				m.on = !m.on;
				std::lock_guard<std::mutex> disc(g_discInfoMutex);
				ogbox_mods_choose(modsKey.c_str(), m.cheat, m.id, m.on);
				detailsPath.clear();   // the details again, with the change
			}
			if (ImGui::IsItemFocused()) {
				help = m.description[0] ? std::string(m.description) : std::string(m.cheat ? "A cheat." : "A mod.");
				help += m.cheat ? " Cheats apply while the game runs, from its next start." : " Mods change the game's code and files, from its next start.";
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::BeginChild("help", ImVec2(0.0f, helpH), true, ImGuiWindowFlags_NoNav);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextColored(mx::text, "%s", help.c_str());
		ImGui::TextColored(mx::line, "/data/ogbox/mods.txt");
		ImGui::PopTextWrapPos();
		ImGui::EndChild();
		footer("CROSS on / off    CIRCLE back");
		endScreen();
	}

	/* In a game: the FPS panel, the quit ring, and what is happening until the first frame. */
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
		if (trouble || !havePicture) {
			ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.85f);
			ImGui::Begin("##status", nullptr, flags);
			ImGui::TextColored(mx::bright, "OGBox %s", pshakux_build_version());
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
			ImGui::TextColored(mx::line, "Hold OPTIONS + touch pad: back to the game list");
			ImGui::End();
		}
		// the quit ring, while OPTIONS and the touch pad are held
		if (quitHeldSince >= 0.0 && quitHeldSince < 1e8) {
			const float done = static_cast<float>(std::min(1.0, (now - quitHeldSince) / kQuitHoldSeconds));
			const float r = H * 0.035f;
			ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H - pad * 3.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
			ImGui::SetNextWindowBgAlpha(0.9f);
			ImGui::PushStyleColor(ImGuiCol_Border, mx::bright);
			ImGui::Begin("##quit", nullptr, flags);
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(r * 2.2f, r * 2.2f));
			ImDrawList *dl = ImGui::GetWindowDrawList();
			const ImVec2 c(at.x + r * 1.1f, at.y + r * 1.1f);
			dl->AddCircle(c, r, mx::u32(mx::line), 48, r * 0.16f);
			const float a0 = -3.14159265f * 0.5f;
			dl->PathArcTo(c, r, a0, a0 + done * 2.0f * 3.14159265f, 48);
			dl->PathStroke(mx::u32(mx::bright), false, r * 0.16f);
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::TextColored(mx::bright, "Hold OPTIONS + touch pad");
			ImGui::TextColored(mx::text, "back to the game list in %.1f s", std::max(0.0, kQuitHoldSeconds - (now - quitHeldSince)));
			ImGui::EndGroup();
			ImGui::End();
			ImGui::PopStyleColor();
		}
	}

	void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		(void)overlay;
		if (!library) {
			drawGame();
			return;
		}
		switch (screen) {
		case Screen::Boot: drawBoot(); break;
		case Screen::Library: drawLibrary(); break;
		case Screen::Settings: drawSettings(); break;
		case Screen::Mods: drawMods(); break;
		}
	}
};

VULKAN_EXAMPLE_MAIN()
