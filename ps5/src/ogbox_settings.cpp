/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The settings (ogbox_settings.h). The words and defaults are hakuX's (android/app/src/main/res/
 * values/strings.xml, data/AppPrefs.kt) where the setting is hakuX's; a few say what differs on
 * the console (no phone: "auto" is English, the network's addresses are written in the file).
 */
#include "ogbox_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <sys/stat.h>

#include "platform/ps5_bootlog.h"
#include "core/ogbox_patches.h"

namespace ogbox {

namespace {

constexpr const char *kSettingsFile = "/data/ogbox/settings.txt";
constexpr const char *kGamesDir = "/data/ogbox/games";

const std::vector<Choice> kOnOff = { { "0", "Off" }, { "1", "On" } };

std::vector<Option> makeOptions()
{
	std::vector<Option> o;
	// ---- Graphics (hakuX: Graphics, Display)
	o.push_back({ "render_scale", Section::Graphics, "Resolution scale",
		"The game's picture drawn at this many times its resolution. Higher is sharper and heavier.",
		{ { "1", "1x (native)" }, { "2", "2x the game's resolution" }, { "3", "3x the game's resolution" },
			{ "4", "4x the game's resolution" } },
		"1", true });
	o.push_back({ "uncapped", Section::Graphics, "Uncapped frame rate",
		"Present every frame at once instead of waiting for the display.", kOnOff, "0", true });
	// OGBox 0.1.29: hakuX PR #14 (pjperez), the app's "Frame pacing" (on there by default; off
	// here, as an enhancement is the user's choice).
	o.push_back({ "frame_pacing", Section::Graphics, "Frame pacing",
		"Hold the game to the speed frames are drawn at, two frames ahead at most, so the controller "
		"is read close to what is on screen. Helps a game whose drawing can't keep up (input lag); "
		"no change when it keeps up.",
		kOnOff, "0", true });
	o.push_back({ "async_shaders", Section::Graphics, "Asynchronous shader compilation",
		"Compile new shaders in the background instead of stopping the game; what needs them can be missing for a few frames.",
		kOnOff, "1", true });
	o.push_back({ "prebuild_shaders", Section::Graphics, "Build cached shaders before starting",
		"Build the shaders a game used before, before it starts, so they are ready when it needs them.", kOnOff, "1",
		true });
	o.push_back({ "aspect", Section::Graphics, "Aspect ratio", "The picture's shape on the screen.",
		{ { "auto", "Auto (the game's own; 16:9 when the console is widescreen)" }, { "4:3", "4:3" },
			{ "16:9", "16:9 (widescreen games)" }, { "stretch", "Stretch to the screen" } },
		"auto", true });
	o.push_back({ "fxaa", Section::Graphics, "FXAA anti-aliasing",
		"Smooths jagged edges of each frame. Most useful at low resolution scales: a scale above the screen's already smooths them.",
		kOnOff, "0", true });
	o.push_back({ "upscale", Section::Graphics, "Upscaling filter",
		"For a picture smaller than the screen. (With FXAA on, FXAA's picture is scaled bilinearly.)",
		{ { "bilinear", "Bilinear" }, { "sgsr", "Snapdragon GSR (sharper edges)" } }, "bilinear", true });
	// ---- Console
	o.push_back({ "language", Section::Console, "Language",
		"Games show their text and speech in it where they have it; the others keep their own.",
		{ { "auto", "Auto (English)" }, { "en", "English" }, { "ja", "Japanese" }, { "de", "Deutsch" },
			{ "fr", "Fran\xC3\xA7" "ais" }, { "es", "Espa\xC3\xB1ol" }, { "it", "Italiano" }, { "ko", "Korean" },
			{ "zh", "Chinese" }, { "pt", "Portugu\xC3\xAAs" } },
		"auto", true });
	o.push_back({ "console_timing", Section::Console, "Console drive timing",
		"Disc and hard disk reads take time, a quarter of the console's at its fastest, so a game's other threads run while it loads, as games expect (off, some can freeze). Loads are slower.",
		kOnOff, "1", true });
	o.push_back({ "console_ram", Section::Console, "Memory",
		"The Xbox's RAM. Auto: 64 MB as the console has it, 128 MB (the RAM upgrade) when a patch that needs it is on (720p and other 128 MB patches). 128 MB: always.",
		{ { "auto", "Auto" }, { "64", "64 MB (the console's)" }, { "128", "128 MB (the RAM upgrade)" } }, "auto", true });
	{
		Option days{ "clock_days", Section::Console, "Clock: days from the console's time",
			"Games date their saves by it, and some hold events on given days. 0 with 0 hours: the PS5's time.", {},
			"0", false };
		days.minimum = -3650;
		days.maximum = 3650;
		days.unit = " days";
		o.push_back(days);
		Option hours{ "clock_hours", Section::Console, "Clock: hours from the console's time",
			"Added to the days above.", {}, "0", false };
		hours.minimum = -23;
		hours.maximum = 23;
		hours.unit = " hours";
		o.push_back(hours);
	}
	// ---- Console video
	o.push_back({ "screen_format", Section::Video, "Screen format",
		"The Xbox's own video settings, as its dashboard sets them. Games that support a mode use it.",
		{ { "normal", "Normal (4:3)" }, { "letterbox", "Letterbox" }, { "widescreen", "Widescreen (16:9)" } },
		"normal", true });
	o.push_back({ "480p", Section::Video, "480p", "Progressive 640x480 instead of interlaced.", kOnOff, "1", true });
	o.push_back({ "720p", Section::Video, "720p",
		"1280x720 in games that offer it. Heavier: consider a lower resolution scale.", kOnOff, "0", true });
	o.push_back({ "1080i", Section::Video, "1080i",
		"1920x1080 in the few games that offer it. Much heavier: use resolution scale 1x.", kOnOff, "0", true });
	o.push_back({ "pal60", Section::Video, "PAL 60 Hz",
		"European games run at 60 Hz instead of 50 Hz where they offer it.", kOnOff, "0", true });
	o.push_back({ "av_pack", Section::Video, "Video cable", "The cable the console thinks it has.",
		{ { "hdtv", "HDTV (component): all modes" }, { "standard", "Standard AV (composite): 480i only" },
			{ "svideo", "S-Video: 480i only" } },
		"hdtv", true });
	// ---- Network
	o.push_back({ "network", Section::Network, "Connection",
		"Xbox Live through Insignia, or system link: play with the consoles on your network, or over the Internet through XLink Kai.",
		{ { "off", "Off (no cable)" }, { "lan", "LAN: consoles on this network" },
			{ "kai", "XLink Kai: over the Internet" }, { "insignia", "Xbox Live: Insignia" } },
		"off", false });
	{
		Option peers{ "lan_peers", Section::Network, "Other consoles",
			"Consoles the network's broadcasts don't reach, by address: lan_peers=10.147.17.5, 10.147.17.9:3075 in /data/ogbox/settings.txt.",
			{}, "", false };
		peers.text = true;
		o.push_back(peers);
		Option kai{ "kai_engine", Section::Network, "XLink Kai engine",
			"The address of the computer running the XLink Kai engine: kai_engine=192.168.1.20 in /data/ogbox/settings.txt.",
			{}, "", false };
		kai.text = true;
		o.push_back(kai);
		Option dns{ "insignia_dns", Section::Network, "DNS server",
			"Insignia's (46.101.64.175) when empty. Change it only if your provider blocks it: insignia_dns=<address> in /data/ogbox/settings.txt.",
			{}, "", false };
		dns.text = true;
		o.push_back(dns);
	}
	// ---- Game list (OGBox 0.1.43): which places its left column shows
	{
		const auto place = [&o](const char *key, const char *label, const char *description,
						 std::vector<Choice> choices, const char *def) {
			Option p{ key, Section::Library, label, description, std::move(choices), def, false };
			p.now = true;
			o.push_back(p);
		};
		place("list_games", "OGBox's games folder",
			"The games folder in OGBox's own folder, when there is one.", kOnOff, "1");
		place("list_internal", "Internal storage", "The console's own storage (/data).", kOnOff, "1");
		place("list_usb", "USB drives",
			"The console's USB drives (/mnt/usb0 to usb7). Connected ones: only a drive that is plugged in.",
			{ { "0", "Hidden" }, { "1", "Connected ones" }, { "2", "All eight" } }, "0");
		place("list_ext", "Extended storage", "The extended storage drives (/mnt/ext0, ext1), when there.", kOnOff, "1");
		place("list_network", "Network shares", "The shares of /data/ogbox/network.cfg.", kOnOff, "1");
		// 0.1.45: how the list looks
		place("list_covers", "Covers",
			"Each disc's box art from libretro's thumbnails (thumbnails.libretro.com), downloaded the first time it "
			"is shown and kept in /data/ogbox/covers. Found by the disc's Redump name: the file's, else the one the "
			"database has for its title ID.",
			kOnOff, "1");
		place("list_layout", "Layout", "The folder's discs as a list, or as a grid of their covers.",
			{ { "list", "List" }, { "grid", "Grid of covers" } }, "list");
		place("list_names", "Names", "The discs' names as their files have them, or without the extension and the "
			"bracketed parts (region, languages, revision).",
			{ { "file", "File names" }, { "short", "Short names" } }, "file");
		place("list_sort", "Order", "The folder's discs by name, or the ones played last first.",
			{ { "name", "By name" }, { "played", "Last played first" } }, "name");
		place("list_sizes", "Disc sizes", "Each disc's size beside its name.", kOnOff, "1");
		place("list_details", "Details panel", "The selected disc's details on the right: its cover, its database "
			"entry (Redump's name, region, languages, serial), its mods and patches.", kOnOff, "1");
	}
	// ---- Interface (OGBox's)
	o.push_back({ "show_fps", Section::Interface, "Show FPS counter",
		"Frame rate and frame time overlay while playing.", kOnOff, "1", false });
	{
		Option rain{ "rain", Section::Interface, "Code rain",
			"The falling code behind the game list and the menus.", kOnOff, "1", false };
		rain.now = true;
		o.push_back(rain);
	}
	return o;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) {
		a++;
	}
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) {
		b--;
	}
	return s.substr(a, b - a);
}

void readFile(const std::string &path, std::map<std::string, std::string> &out)
{
	out.clear();
	FILE *f = std::fopen(path.c_str(), "r");
	if (!f) {
		return;
	}
	char line[1024];
	while (std::fgets(line, sizeof(line), f)) {
		const std::string s = trim(line);
		if (s.empty() || s[0] == '#') {
			continue;
		}
		const size_t eq = s.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const std::string key = trim(s.substr(0, eq));
		if (option(key)) {
			out[key] = trim(s.substr(eq + 1));
		}
	}
	std::fclose(f);
}

void writeFile(const std::string &path, const std::map<std::string, std::string> &values, const char *head)
{
	const std::string tmp = path + ".new";
	FILE *f = std::fopen(tmp.c_str(), "w");
	if (!f) {
		pshakux_log("settings: %s cannot be written", tmp.c_str());
		return;
	}
	std::fprintf(f, "# %s\n", head);
	for (const Option &o : options()) {
		const auto it = values.find(o.key);
		if (it != values.end()) {
			std::fprintf(f, "%s=%s\n", o.key, it->second.c_str());
		}
	}
	std::fclose(f);
	std::rename(tmp.c_str(), path.c_str());
}

} // namespace

const char *sectionName(Section s)
{
	switch (s) {
	case Section::Graphics: return "Graphics";
	case Section::Console: return "Console";
	case Section::Video: return "Console video";
	case Section::Network: return "Network";
	case Section::Library: return "Game list";
	case Section::Interface: return "Interface";
	default: return "";
	}
}

const std::vector<Option> &options()
{
	static const std::vector<Option> all = makeOptions();
	return all;
}

const Option *option(const std::string &key)
{
	for (const Option &o : options()) {
		if (key == o.key) {
			return &o;
		}
	}
	return nullptr;
}

std::string gameSettingsPath(const std::string &key)
{
	return std::string(kGamesDir) + "/" + key + ".txt";
}

void Settings::load(const std::string &key)
{
	gameKey = key;
	readFile(kSettingsFile, globalValues);
	gameValues.clear();
	if (!gameKey.empty()) {
		readFile(gameSettingsPath(gameKey), gameValues);
		for (auto it = gameValues.begin(); it != gameValues.end();) {
			const Option *o = option(it->first);
			it = (o && o->perGame) ? std::next(it) : gameValues.erase(it);
		}
	}
}

std::string Settings::global(const std::string &key) const
{
	const auto it = globalValues.find(key);
	if (it != globalValues.end()) {
		return it->second;
	}
	const Option *o = option(key);
	return o ? o->def : "";
}

std::string Settings::override_(const std::string &key) const
{
	const auto it = gameValues.find(key);
	return it == gameValues.end() ? std::string() : it->second;
}

std::string Settings::value(const std::string &key) const
{
	const std::string v = override_(key);
	return v.empty() ? global(key) : v;
}

int Settings::number(const std::string &key) const
{
	return std::atoi(value(key).c_str());
}

void Settings::setGlobal(const std::string &key, const std::string &v)
{
	const Option *o = option(key);
	if (!o) {
		return;
	}
	if (v == o->def) {
		globalValues.erase(key);
	} else {
		globalValues[key] = v;
	}
	saveGlobal();
	pshakux_log("settings: %s = %s", key.c_str(), v.c_str());
}

void Settings::setOverride(const std::string &key, const std::string &v)
{
	const Option *o = option(key);
	if (!o || !o->perGame || gameKey.empty()) {
		return;
	}
	if (v.empty()) {
		gameValues.erase(key);
	} else {
		gameValues[key] = v;
	}
	saveGame();
	pshakux_log("settings: %s: %s = %s", gameKey.c_str(), key.c_str(), v.empty() ? "(the app's)" : v.c_str());
}

void Settings::clearOverrides()
{
	gameValues.clear();
	saveGame();
}

void Settings::saveGlobal() const
{
	mkdir("/data/ogbox", 0777);
	writeFile(kSettingsFile, globalValues, "OGBox's settings (the menu writes them; a line missing: the default)");
}

void Settings::saveGame() const
{
	if (gameKey.empty()) {
		return;
	}
	mkdir(kGamesDir, 0777);
	const std::string path = gameSettingsPath(gameKey);
	if (gameValues.empty()) {
		std::remove(path.c_str());
		return;
	}
	writeFile(path, gameValues, ("the settings of " + gameKey + " (the others: the app's)").c_str());
}

std::string Settings::label(const Option &o, const std::string &v)
{
	for (const Choice &c : o.choices) {
		if (v == c.value) {
			return c.label;
		}
	}
	if (o.text) {
		return v.empty() ? "Not set" : v;
	}
	if (o.choices.empty()) {
		return v + o.unit;
	}
	return v;
}

std::string Settings::step(const Option &o, const std::string &v, int by)
{
	if (o.text) {
		return v;
	}
	if (o.choices.empty()) {
		int n = std::atoi(v.c_str()) + by;
		if (n < o.minimum) {
			n = o.maximum;
		} else if (n > o.maximum) {
			n = o.minimum;
		}
		return std::to_string(n);
	}
	int at = 0;
	for (size_t i = 0; i < o.choices.size(); i++) {
		if (v == o.choices[i].value) {
			at = static_cast<int>(i);
		}
	}
	const int n = static_cast<int>(o.choices.size());
	at = ((at + by) % n + n) % n;
	return o.choices[static_cast<size_t>(at)].value;
}

std::vector<std::string> Settings::coreArgs() const
{
	std::vector<std::string> a;
	if (on("uncapped")) {
		a.push_back("uncap");
	}
	a.push_back("scale=" + value("render_scale"));
	// the console's video settings (AppPrefs.videoFlags) and its language (xboxLanguage)
	unsigned flags = 0;
	const std::string format = value("screen_format");
	if (format == "widescreen") {
		flags |= 0x00010000u;
	} else if (format == "letterbox") {
		flags |= 0x00100000u;
	}
	if (on("720p")) {
		flags |= 0x00020000u;
	}
	if (on("1080i")) {
		flags |= 0x00040000u;
	}
	if (on("480p")) {
		flags |= 0x00080000u;
	}
	if (on("pal60")) {
		flags |= 0x00400000u;
	}
	char hex[16];
	std::snprintf(hex, sizeof(hex), "%x", flags);
	a.push_back(std::string("video=") + hex);
	a.push_back("avpack=" + value("av_pack"));
	static const char *const kLanguages[] = { "en", "ja", "de", "fr", "es", "it", "ko", "zh", "pt" };
	int lang = 1;
	const std::string l = value("language");
	for (int i = 0; i < 9; i++) {
		if (l == kLanguages[i]) {
			lang = i + 1;
		}
	}
	a.push_back("lang=" + std::to_string(lang));
	// the console's clock: its distance from the PS5's, and the PS5's time zone
	const long long offset = static_cast<long long>(number("clock_days")) * 86400000LL +
		static_cast<long long>(number("clock_hours")) * 3600000LL;
	if (offset != 0) {
		a.push_back("clock=" + std::to_string(offset));
	}
	const time_t now = std::time(nullptr);
	struct tm local {};
	if (localtime_r(&now, &local)) {
		a.push_back("tz=" + std::to_string(local.tm_gmtoff / 60));
	}
	a.push_back(std::string("pace=") + (on("frame_pacing") ? "1" : "0"));
	a.push_back(std::string("asyncpipe=") + (on("async_shaders") ? "1" : "0"));
	a.push_back(std::string("prebuild=") + (on("prebuild_shaders") ? "1" : "0"));
	a.push_back(std::string("devtime=") + (on("console_timing") ? "1" : "0"));
	// 0.1.47: the RAM; Auto: 128 MB when a patch on for the game needs it (ogbox_patches.c)
	const std::string ram = value("console_ram");
	if (ram == "128" || (ram == "auto" && !gameKey.empty() && ogbox_patches_need_128(gameKey.c_str()))) {
		a.push_back("NC_RAM_MB=128");
	}
	// the network (NativeCoreActivity.kt's)
	const std::string net = value("network");
	if (net == "lan") {
		a.push_back("net=lan");
		std::string peers = value("lan_peers");
		peers.erase(std::remove(peers.begin(), peers.end(), ' '), peers.end());
		if (!peers.empty()) {
			a.push_back("lanpeers=" + peers);
		}
	} else if (net == "kai") {
		const std::string kai = trim(value("kai_engine"));
		if (!kai.empty()) {
			a.push_back("net=kai");
			a.push_back("kai=" + kai);
		}
	} else if (net == "insignia") {
		const std::string dns = trim(value("insignia_dns"));
		a.push_back("net=nat");
		a.push_back("dns=" + (dns.empty() ? std::string("46.101.64.175") : dns));
	}
	return a;
}

} // namespace ogbox
