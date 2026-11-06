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
	/* OGBox 0.0.1: the settings in eight sections, each about one thing (the picture, how games
	 * run, the sound, the Xbox, its video modes, the network, the game list, the menus), with
	 * shorter words; the keys and values are as before, so settings.txt and a game's own carry
	 * over. */
	// ---- Graphics: how the picture looks
	o.push_back({ "render_scale", Section::Graphics, "Resolution scale",
		"Draws the game at a multiple of its own resolution: sharper, and heavier. 2x suits a 1080p screen, 4x a 4K "
		"one.",
		{ { "1", "1x (original)" }, { "2", "2x" }, { "3", "3x" }, { "4", "4x" }, { "5", "5x (very heavy)" },
			{ "6", "6x (heaviest)" } },
		"1", true });
	/* 0.1.58: the anti-aliasing (the key stays "fxaa": its "1" is FXAA, as before) */
	o.push_back({ "fxaa", Section::Graphics, "Anti-aliasing",
		"Smooths jagged edges. FXAA: light, a little soft. SMAA: sharper, a little heavier. SSAA: the game drawn above "
		"the screen's resolution and scaled down (the resolution scale goes up to 4x on a 1080p screen, 6x on 4K): "
		"the best, and by far the heaviest.",
		{ { "0", "Off" }, { "1", "FXAA" }, { "smaa", "SMAA" }, { "ssaa", "SSAA (heaviest)" } }, "0", true });
	o.push_back({ "aspect", Section::Graphics, "Aspect ratio",
		"The picture's shape. Auto: the game's own (16:9 when the Xbox's screen format is widescreen).",
		{ { "auto", "Auto" }, { "4:3", "4:3" }, { "16:9", "16:9" }, { "stretch", "Stretch to the screen" } },
		"auto", true });
	o.push_back({ "upscale", Section::Graphics, "Upscaling filter",
		"How a picture smaller than the screen is scaled up. Snapdragon GSR keeps edges sharper. (With FXAA on, the "
		"picture is scaled bilinearly.)",
		{ { "bilinear", "Bilinear (smooth)" }, { "sgsr", "Snapdragon GSR (sharper)" } }, "bilinear", true });
	/* 0.0.1: the depth vertex programs store, exactly their screen-space z (hakuX-native's
	 * shader/ff.h HLE_ZEXACT_IN_GLSL); off: the GPU's own, with its early depth test */
	o.push_back({ "exact_depth", Section::Graphics, "Exact depth",
		"Surfaces the game draws at one depth (a sky's layers) get exactly that depth, as on the console, "
		"instead of flickering where they overlap. Costs some GPU time: turn it off for a game that "
		"needs the speed and shows no flicker.",
		kOnOff, "1", true });
	/* 0.0.3: dithering into 16-bit targets (hakuX's vk/draw.c NC_HLE_DITHER) */
	o.push_back({ "dither", Section::Graphics, "16-bit dithering",
		"Games that draw in 16-bit colour are dithered by the Xbox in a fine pattern of dots, to hide colour bands. "
		"As the Xbox: its pattern, by the game's own pixels (with the resolution scale up, it shows as a grain of "
		"dots). Fine: the same pattern by screen pixel, too fine to see. Off: no dots, but visible colour bands.",
		{ { "xbox", "As the Xbox" }, { "fine", "Fine" }, { "off", "Off" } }, "xbox", true });
	/* 0.0.4: anisotropic filtering, 32-bit colour and sharpening (hakuX's NC_HLE_ANISO, NC_HLE_FORCE32 and
	 * NC_HLE_SHARPEN): all off by default, the Xbox's own picture */
	o.push_back({ "aniso", Section::Graphics, "Anisotropic filtering",
		"Keeps floors, roads and walls seen at a slant sharp into the distance, where the game's own filtering "
		"blurs them. A higher resolution scale doesn't do this. Only textures the game filters smoothly are "
		"changed (not pixel art or menus). Light on the console's GPU. As the game sets it: the Xbox's own.",
		{ { "0", "As the game sets it" }, { "2", "2x" }, { "4", "4x" }, { "8", "8x" }, { "16", "16x" } },
		"0", true });
	o.push_back({ "force32", Section::Graphics, "Force 32-bit colour",
		"Games that draw in 16-bit colour (skies, fog, smoke and dark areas show bands of colour) are drawn in 32-bit "
		"instead, so the bands and the dots that hide them go. The game still sees 16-bit pixels where it reads "
		"them back, so a few games that depend on that can look wrong with it: turn it off for those. Experimental.",
		kOnOff, "0", true });
	o.push_back({ "sharpen", Section::Graphics, "Sharpening",
		"Sharpens the picture's edges and fine detail (contrast-adaptive: flat areas and already strong edges "
		"are left alone, so it adds no halos). Light on the GPU.",
		{ { "0", "Off" }, { "30", "Low" }, { "60", "Medium" }, { "90", "High" } }, "0", true });
	/* 0.1.59: the overscan a TV hides, cropped (hakuX-lr's 3/80 a side: the picture keeps its shape) */
	o.push_back({ "crop_overscan", Section::Graphics, "Crop overscan",
		"Hides the picture's edges (3.75% a side) as a TV of the time did, where some games leave garbage or black "
		"borders.", kOnOff, "0", true });
	// ---- Performance: how games run
	/* 0.0.1: the HLE GPU's two threads (the push-buffer decoder, the renderer) on cores of their own */
	o.push_back({ "gpu_cores", Section::Performance, "GPU on separate cores",
		"Runs the Xbox GPU's emulation (its command reader and its renderer) on processor cores of its own, apart "
		"from the Xbox CPU and everything else. Faster; turn it off for a game that freezes or stutters with it.",
		kOnOff, "1", true });
	// OGBox 0.1.29: hakuX PR #14 (pjperez), the app's "Frame pacing" (on there by default; off
	// here, as an enhancement is the user's choice).
	o.push_back({ "frame_pacing", Section::Performance, "Frame pacing",
		"Keeps the game at most two frames ahead of the picture, so the controller is read close to what is on "
		"screen. Helps the input lag of a game whose drawing can't keep up; no change in the others.",
		kOnOff, "0", true });
	o.push_back({ "uncapped", Section::Performance, "Uncapped frame rate",
		"Shows each frame as soon as it is ready instead of waiting for the display.", kOnOff, "0", true });
	// 0.0.2: off by default (the console draws everything). On, a draw whose shader isn't built yet is
	// skipped when its target was drawn in the last frames; a loading screen's background is drawn once
	// under a spinner that redraws every frame, so it never came back (a game's first run, its cache
	// empty). Once a game's shaders are cached, "Build cached shaders at start" readies them anyway.
	o.push_back({ "async_shaders", Section::Performance, "Compile shaders in the background",
		"Off: the game waits for each new shader (a short stutter the first time), and everything is drawn. On: "
		"new shaders are compiled without stopping the game, and what needs them is missing until they are ready: "
		"something drawn once, such as a loading screen's background, can stay missing.",
		kOnOff, "0", true });
	o.push_back({ "prebuild_shaders", Section::Performance, "Build cached shaders at start",
		"Builds the shaders a game used before while it starts, so they are ready when it needs them.", kOnOff, "1",
		true });
	// ---- Audio (0.1.59's APU, hakuX-lr's: its DSP programs run, or the fast mix; the voices' resampler)
	o.push_back({ "audio_dsp", Section::Audio, "Audio processing",
		"Fast: the game's voices mixed directly, as hakuX does. Full DSP: the Xbox's own audio programs run (effects, "
		"reverb, the mix as the console makes it): accurate, much heavier. Where the console can't keep it in real time, a game "
		"switches to Fast by itself after a few seconds (the boot log says so).",
		{ { "fast", "Fast" }, { "full", "Full DSP (accurate, heavy)" } }, "fast", true });
	o.push_back({ "audio_resampler", Section::Audio, "Resampler",
		"How each voice is brought to the output rate. Sinc: clean highs. Linear: lighter, a little dull.",
		{ { "sinc", "Sinc" }, { "linear", "Linear" } }, "sinc", true });
	// ---- Console: the Xbox itself
	o.push_back({ "language", Section::Console, "Language",
		"Games show their text and speech in it where they have it; the others keep their own.",
		{ { "auto", "Auto (English)" }, { "en", "English" }, { "ja", "Japanese" }, { "de", "Deutsch" },
			{ "fr", "Fran\xC3\xA7" "ais" }, { "es", "Espa\xC3\xB1ol" }, { "it", "Italiano" }, { "ko", "Korean" },
			{ "zh", "Chinese" }, { "pt", "Portugu\xC3\xAAs" } },
		"auto", true });
	o.push_back({ "console_ram", Section::Console, "Memory",
		"The Xbox's RAM. Auto: 64 MB as the console has, 128 MB (the RAM upgrade) when a patch that needs it is on. "
		"128 MB: as a console with the upgrade, so a game that asks for 64 MB only (most do) gets 64 MB.",
		{ { "auto", "Auto" }, { "64", "64 MB" }, { "128", "128 MB" } }, "auto", true });
	o.push_back({ "console_timing", Section::Console, "Drive timing",
		"Disc and hard disk reads take time as on the console (a quarter of it at its fastest), so a game's other "
		"threads run while it loads, as games expect. Off: faster loads, but some games freeze.",
		kOnOff, "1", true });
	{
		Option days{ "clock_days", Section::Console, "Clock offset: days",
			"Moves the Xbox's clock from the PS5's time. Games date their saves by it, and some hold events on given "
			"days. 0 days and 0 hours: the PS5's time.", {},
			"0", false };
		days.minimum = -3650;
		days.maximum = 3650;
		days.unit = " days";
		o.push_back(days);
		Option hours{ "clock_hours", Section::Console, "Clock offset: hours",
			"Added to the days above.", {}, "0", false };
		hours.minimum = -23;
		hours.maximum = 23;
		hours.unit = " hours";
		o.push_back(hours);
	}
	// ---- Video modes: the Xbox's own video settings
	o.push_back({ "screen_format", Section::Video, "Screen format",
		"The TV the Xbox thinks it has. Games that support widescreen or letterbox use it.",
		{ { "normal", "Normal (4:3)" }, { "letterbox", "Letterbox" }, { "widescreen", "Widescreen (16:9)" } },
		"normal", true });
	o.push_back({ "480p", Section::Video, "480p", "Progressive 640x480 instead of interlaced.", kOnOff, "1", true });
	o.push_back({ "720p", Section::Video, "720p",
		"1280x720 in games that offer it. Heavier: consider a lower resolution scale.", kOnOff, "0", true });
	o.push_back({ "1080i", Section::Video, "1080i",
		"1920x1080 in the few games that offer it. Much heavier: use resolution scale 1x.", kOnOff, "0", true });
	o.push_back({ "pal60", Section::Video, "PAL 60 Hz",
		"European games run at 60 Hz instead of 50 Hz where they offer it.", kOnOff, "0", true });
	o.push_back({ "av_pack", Section::Video, "Video cable", "The cable the Xbox thinks it has.",
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
		place("list_covers", "Covers and descriptions",
			"Each disc's box art, downloaded the first time it is shown and kept in the data folder's covers: "
			"libretro's thumbnails by its Redump name, else MobCat's scans of the boxes by the disc's serial, else "
			"the picture in the disc's own default.xbe. A game's page shows its description from Wikipedia.",
			kOnOff, "1");
		place("home_layout", "Layout", "The games as a row of covers with the chosen one's details beside them, or as a "
			"list with the chosen one's cover and details at the right (for big folders).",
			{ { "covers", "Covers" }, { "list", "List" } }, "covers");
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
	// ---- Interface: OGBox's menus
	{
		/* 0.1.52 */
		Option theme{ "theme", Section::Interface, "Theme",
			"How the menus look. Dark: a dark ground lit by the chosen game's colours. Code: black and green, with the "
			"falling code (0.0.3: the Emerald theme is gone; it shows as Dark).",
			{ { "dark", "Dark" }, { "code", "Code" } }, "dark", false };
		theme.now = true;
		o.push_back(theme);
	}
	{
		Option rain{ "rain", Section::Interface, "Code rain",
			"The falling code behind the menus (the Code theme).", kOnOff, "1", false };
		rain.now = true;
		o.push_back(rain);
	}
	{
		/* 0.0.1: the updater (Crux2's) */
		Option up{ "update_check", Section::Interface, "Look for updates at start",
			"Asks GitHub whether a newer OGBox is out each time OGBox starts, and offers it with what is new. "
			"Nothing is downloaded without asking. Settings > Updates asks at any time.",
			kOnOff, "1", false };
		up.now = true;
		o.push_back(up);
	}
	o.push_back({ "show_fps", Section::Interface, "FPS counter",
		"The frame rate and frame time in a corner while playing.", kOnOff, "1", false });
	{
		/* 0.1.53 */
		Option qm{ "quick_menu", Section::Interface, "Quick menu button",
			"The button that opens the quick menu in a game (save states, the game's settings, quit). OPTIONS: the "
			"Xbox's Start and Back are the touch pad's right and left halves. OPTIONS held: a short press is Start, "
			"held half a second it opens the menu; the touch pad is Back.",
			{ { "options", "OPTIONS" }, { "options_hold", "OPTIONS held" } }, "options", false };
		qm.now = true;
		o.push_back(qm);
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
	case Section::Performance: return "Performance";
	case Section::Audio: return "Audio";
	case Section::Console: return "Console";
	case Section::Video: return "Video modes";
	case Section::Network: return "Network";
	case Section::Library: return "Game list";
	case Section::Interface: return "Interface";
	default: return "";
	}
}

const char *sectionHelp(Section s)
{
	switch (s) {
	case Section::Graphics: return "How the picture looks: its resolution, anti-aliasing, shape and scaling.";
	case Section::Performance: return "How games run: the processor cores, frame pacing and shaders.";
	case Section::Audio: return "How the Xbox's sound is made.";
	case Section::Console: return "The Xbox itself: its language, memory, drive timing and clock.";
	case Section::Video: return "The Xbox's own video settings, as its dashboard sets them. Games that support a mode use it.";
	case Section::Network: return "Xbox Live through Insignia, or system link with consoles on your network or over XLink Kai.";
	case Section::Library: return "Which places the game list shows, and how it looks. Changes apply at once.";
	case Section::Interface: return "OGBox's menus: the theme, updates, the FPS counter and the quick menu button.";
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
	const bool patch128 = !gameKey.empty() && ogbox_patches_need_128(gameKey.c_str());
	if (ram == "128" || (ram == "auto" && patch128)) {
		a.push_back("NC_RAM_MB=128");
		/* 0.0.3: a game whose XBE limits it to 64 MB gets 64 MB, as on a 128 MB console; a patch that
		 * needs 128 MB has it anyway (Cerbios's DisableLimitMem) */
		if (patch128) {
			a.push_back("NC_RAM_NOLIMIT=1");
		}
	}
	// 0.1.59: the APU (native/dev/apu/apu.c config_init)
	if (value("audio_dsp") == "full") {
		a.push_back("NC_APU_DSP=1");
	}
	if (value("audio_resampler") == "linear") {
		a.push_back("NC_APU_RESAMPLER=linear");
	}
	if (value("exact_depth") == "0") {   // 0.0.1
		a.push_back("NC_HLE_EXACT_DEPTH=0");
	}
	if (value("dither") == "fine" || value("dither") == "off") {   // 0.0.3
		a.push_back("NC_HLE_DITHER=" + value("dither"));
	}
	// 0.0.4: anisotropic filtering, 32-bit colour, sharpening
	if (value("aniso") == "2" || value("aniso") == "4" || value("aniso") == "8" || value("aniso") == "16") {
		a.push_back("NC_HLE_ANISO=" + value("aniso"));
	}
	if (value("force32") == "1") {
		a.push_back("NC_HLE_FORCE32=1");
	}
	if (value("sharpen") == "30" || value("sharpen") == "60" || value("sharpen") == "90") {
		a.push_back("NC_HLE_SHARPEN=" + value("sharpen"));
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
