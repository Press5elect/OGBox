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
	enum Kind { Place, Folder, Image, Up, Quit } kind;
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

} // namespace

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
			entries.push_back({ LibraryEntry::Quit, "Quit OGBox", std::string(), 0, std::string() });
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
		/* 0.1.10: hakuX's own defaults, as its release app passes them every run
		 * (android/.../NativeCoreActivity.kt and AppPrefs.kt): console timing on
		 * (the DVD drive's and hard disk's real read times: games' streaming and
		 * movies are paced by them), shaders built asynchronously and the
		 * recorded ones before the game, 1x scale, and the console's video
		 * settings: HDTV AV pack with 480p allowed. boot.txt's own options come
		 * after these and win. */
		static const char *const kHakuxDefaults[] = { "devtime=1", "asyncpipe=1", "prebuild=1", "scale=1",
			"video=00080000", "avpack=hdtv" };
		for (const char *option : kHakuxDefaults) {
			argv.push_back(option);
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
		if ((havePicture || showingLogo) && picture.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			const float aspect = displayAspect();
			float w = static_cast<float>(width), h = static_cast<float>(height);
			if (w / h > aspect) {
				w = std::floor(h * aspect + 0.5f);
			} else {
				h = std::floor(w / aspect + 0.5f);
			}
			const float x = std::floor((static_cast<float>(width) - w) * 0.5f);
			const float y = std::floor((static_cast<float>(height) - h) * 0.5f);
			VkViewport viewport{ x, y, w, h, 0.0f, 1.0f };
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			VkRect2D scissor{ { static_cast<int32_t>(x), static_cast<int32_t>(y) }, { static_cast<uint32_t>(w), static_cast<uint32_t>(h) } };
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
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

	/* The game list's own buttons (the D-pad and CROSS go to the list through ImGui's navigation):
	 * CIRCLE the folder above; OPTIONS and the touch pad held, quit. */
	void libraryControls()
	{
		struct pad first{};
		pad_player(0, &first);
		if ((first.pressed & PAD_CIRCLE) && startPath.empty()) {
			goUp();
		}
		const uint32_t combo = PAD_OPTIONS | PAD_TOUCH_PAD;
		const double now = now_seconds();
		if ((first.held & combo) == combo) {
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

	void renderLibrary()
	{
		collectListing();
		if (!startPath.empty() && --startDelay < 0) {
			const std::string path = startPath;
			startPath.clear();
			const char *args[] = { "--boot", path.c_str(), nullptr };
			const int r = pshakux_relaunch(args);
			listError = "The console refused to start the title again (" + std::to_string(r) + ").";
			pshakux_log("library: the relaunch for %s was refused (%#x)", path.c_str(), static_cast<unsigned>(r));
		}
		ui.visible = true;
		VulkanExampleBase::prepareFrame();
		upload = false;
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
		ui.visible = !problem.empty() || state == PSHX_CORE_FAILED || state == PSHX_CORE_STOPPED || !havePicture;
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

	void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		if (library) {
			drawLibrary(overlay->scale);
		} else {
			drawStatus(overlay->scale);
		}
	}

	void drawLibrary(float s)
	{
		const ImVec4 accent(0.42f, 0.78f, 0.25f, 1.0f);   // Xbox green
		const ImVec4 dim(0.62f, 0.65f, 0.70f, 1.0f);
		const ImVec4 failure(1.0f, 0.45f, 0.40f, 1.0f);
		ImGui::SetNextWindowPos(ImVec2(width * 0.5f, height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(std::min(1300.0f * s, width - 48.0f * s), height - 48.0f * s), ImGuiCond_Always);
		if (!focusSet) {
			ImGui::SetNextWindowFocus();
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f * s, 24.0f * s));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 6.0f * s));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.08f, 0.97f));
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(accent.x, accent.y, accent.z, 0.30f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(accent.x, accent.y, accent.z, 0.45f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(accent.x, accent.y, accent.z, 0.60f));
		ImGui::Begin("OGBox", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
		ImGui::SetWindowFontScale(1.6f);
		ImGui::TextColored(accent, "OGBox");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::TextColored(dim, "%s by Press5elect - built on hakuX, for PS5 (unofficial)", pshakux_build_version());
		ImGui::Separator();
		ImGui::Text("%s", folder.empty() ? "Choose where your games are" : folder.c_str());
		ImGui::Spacing();

		const float helpHeight = ImGui::GetTextLineHeightWithSpacing() * (listError.empty() ? 2.0f : 3.5f) + 30.0f * s;
		ImGui::BeginChild("entries", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - helpHeight), false,
			ImGuiWindowFlags_NavFlattened);
		if (listing) {
			ImGui::TextColored(dim, "Reading the folder...");
		}
		int chosen = -1;
		for (size_t i = 0; i < entries.size(); i++) {
			const LibraryEntry &e = entries[i];
			ImGui::PushID(static_cast<int>(i));
			std::string label;
			switch (e.kind) {
			case LibraryEntry::Place: label = e.name; break;
			case LibraryEntry::Folder: label = "[" + e.name + "]"; break;
			case LibraryEntry::Image: label = e.name; break;
			case LibraryEntry::Up: label = ".. (the folder above)"; break;
			case LibraryEntry::Quit: label = "Quit OGBox"; break;
			}
			if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == selected) && startPath.empty()) {
				chosen = static_cast<int>(i);
			}
			if (!focusSet && static_cast<int>(i) == selected) {
				ImGui::SetItemDefaultFocus();
				ImGui::SetNavIDWithRectRel(ImGui::GetID(label.c_str()), 0,
					ImRect(ImGui::GetItemRectMin() - ImGui::GetWindowPos(), ImGui::GetItemRectMax() - ImGui::GetWindowPos()));
				ImGui::SetScrollHere(0.5f);
			}
			if (ImGui::IsItemFocused()) {
				selected = static_cast<int>(i);
				focusSet = true;
			}
			if (!e.note.empty() && (e.kind == LibraryEntry::Image || e.kind == LibraryEntry::Place)) {
				ImGui::SameLine(ImGui::GetWindowWidth() - 220.0f * s);
				ImGui::TextColored(dim, "%s", e.kind == LibraryEntry::Image ? e.note.c_str() : "");
			}
			ImGui::PopID();
		}
		if (!listing && entries.size() <= 1 && !folder.empty() && listError.empty()) {
			ImGui::TextColored(dim, "No folders or Xbox disc images (.iso, .xiso) here.");
		}
		ImGui::EndChild();
		if (chosen >= 0 && chosen < static_cast<int>(entries.size())) {
			const LibraryEntry entry = entries[static_cast<size_t>(chosen)];
			activate(entry);
		}

		ImGui::Separator();
		if (!startPath.empty()) {
			ImGui::TextColored(accent, "Starting %s...", baseName(startPath).c_str());
		} else {
			ImGui::TextColored(dim, "CROSS open / start   CIRCLE back   Hold OPTIONS + touch pad: quit (in a game: back here)");
			ImGui::TextColored(dim, "Network shares: %s", g_networkStatus.c_str());
		}
		if (!listError.empty()) {
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextColored(failure, "%s", listError.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::End();
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar(3);
	}

	/* While a game starts, or when it could not: what is happening, in a corner. */
	void drawStatus(float s)
	{
		if (!ui.visible) {
			return;
		}
		const int state = pshx_core_state();
		ImGui::SetNextWindowPos(ImVec2(10.0f * s, 10.0f * s), ImGuiCond_Always);
		ImGui::Begin("OGBox status", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav);
		ImGui::Text("OGBox %s by Press5elect - built on hakuX, for PS5 (unofficial)", pshakux_build_version());
		if (!problem.empty()) {
			ImGui::Text("%s", problem.c_str());
		} else {
			ImGui::Text("Disc: %s", boot.image.c_str());
			ImGui::Text("Core: %s", stateName(state));
			if (pshx_core_message()[0]) {
				ImGui::Text("%s", pshx_core_message());
			}
			if (state == PSHX_CORE_BOOTING || (state == PSHX_CORE_RUNNING && !havePicture)) {
				ImGui::Text("Waiting for the game's first frame...");
			}
		}
		ImGui::Text("Hold OPTIONS and the touch pad for two seconds: back to the game list. Log: ogbox-boot.log");
		ImGui::End();
	}
};

VULKAN_EXAMPLE_MAIN()
