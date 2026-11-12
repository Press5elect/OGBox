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
#include <deque>
#include <string>

#include <sys/stat.h>

#include "platform/ps5_bootlog.h"
#include "core/ogbox_patches.h"
#include "ogbox_pad.h"

namespace ogbox {

namespace {

constexpr const char *kSettingsFile = "/data/ogbox/settings.txt";
constexpr const char *kGamesDir = "/data/ogbox/games";

const std::vector<Choice> kOnOff = { { "0", "Off" }, { "1", "On" } };

/* 0.0.9: a choice for each step of a range, its words made here and kept as long as the options;
 * at 0 the words zero says, at the low end the words low says (nullptr: the number). */
std::vector<Choice> rangeChoices(int from, int to, int step, const char *zero, const char *low = nullptr,
	const char *unit = "", const std::vector<int> &extra = {})
{
	static std::deque<std::string> words;
	std::vector<int> values;
	for (int v = from; v <= to; v += step) {
		values.push_back(v);
	}
	values.insert(values.end(), extra.begin(), extra.end());
	std::sort(values.begin(), values.end());
	std::vector<Choice> out;
	for (int v : values) {
		words.push_back(std::to_string(v));
		const char *value = words.back().c_str();
		if (v == 0 && zero) {
			words.push_back(zero);
		} else if (v == from && low) {
			words.push_back(std::to_string(v) + " " + low);
		} else {
			words.push_back((from < 0 && v > 0 ? "+" : "") + std::to_string(v) + unit);
		}
		out.push_back({ value, words.back().c_str() });
	}
	return out;
}

} // namespace

/* 0.0.9: HDR's white levels and peaks (the settings' choices, and Adjust HDR brightness's steps) */
std::vector<Choice> hdrWhiteChoices()
{
	static const std::vector<Choice> c = [] {
		std::vector<Choice> v = rangeChoices(80, 400, 10, nullptr, nullptr, " nits", { 203 });
		for (Choice &x : v) {
			if (std::strcmp(x.value, "203") == 0) {
				x.label = "203 nits (reference)";
			}
		}
		return v;
	}();
	return c;
}

std::vector<Choice> hdrPeakChoices()
{
	static const std::vector<Choice> c = [] {
		std::vector<Choice> v = rangeChoices(400, 1000, 100, nullptr, nullptr, " nits", { 1250, 1500, 1750, 2000, 2500, 3000, 4000 });
		v.insert(v.begin(), Choice{ "0", "Off" });
		return v;
	}();
	return c;
}

namespace {

std::vector<Option> makeOptions()
{
	std::vector<Option> o;
	/* OGBox 0.0.1: the settings in eight sections, each about one thing (the picture, how games
	 * run, the sound, the Xbox, its video modes, the network, the game list, the menus), with
	 * shorter words; the keys and values are as before, so settings.txt and a game's own carry
	 * over. */
	// ---- Graphics: how the picture looks
	o.push_back({ "render_scale", Section::Graphics, "Internal resolution",
		"1x: the Xbox's default. 2x for a 1080p screen, 3x for a 1440p screen, 4x for a 4K screen.",
		{ { "1", "1x (Xbox default)" }, { "2", "2x" }, { "3", "3x" }, { "4", "4x" }, { "5", "5x" }, { "6", "6x" } },
		"1", true });
	/* 0.1.58: the anti-aliasing (the key stays "fxaa": its "1" is FXAA, as before) */
	o.push_back({ "fxaa", Section::Graphics, "Anti-aliasing",
		"Smooths jagged edges. FXAA: the lightest and softest. SMAA: sharper. SSAA: the best picture quality, but it "
		"may tank performance. SSAA + SMAA: SSAA, then SMAA over the result for the edges it leaves. Quincunx: the "
		"original Xbox's default, very smooth and soft (it raises the internal resolution to the screen's at least; "
		"Sharpening brings back detail).",
		{ { "0", "Off" }, { "1", "FXAA" }, { "smaa", "SMAA" }, { "ssaa", "SSAA" }, { "ssaa_smaa", "SSAA + SMAA" },
			{ "quincunx", "Quincunx" } }, "0", true });
	/* 0.0.9: SSAA's down-sampling filter (ssaa.frag: the box average, or Lanczos-2) */
	o.push_back({ "ssaa_filter", Section::Graphics, "SSAA down-sampling",
		"For SSAA: Soft: default. Sharp: a Lanczos filter, crisper edges and less shimmer on fine patterns, with a "
		"faint halo along hard edges.",
		{ { "soft", "Soft" }, { "sharp", "Sharp (Lanczos)" } }, "soft", true });
	/* 0.0.9: the CRT look (crt.frag), the picture's last pass */
	o.push_back({ "crt", Section::Graphics, "CRT look",
		"The picture as a TV of the time showed it: the scanlines between the game's own lines (and, Strong, the "
		"screen's red, green and blue stripes). Replaces the upscaling filter. Best on a 4K screen.",
		{ { "off", "Off" }, { "light", "Light" }, { "strong", "Strong" } }, "off", true });
	/* 0.0.9: HDR output (ogbox_main.cpp hdrUpdate, hdr.frag; the driver's ps5/patches/mesa-27/0019).
	 * The app's, for the menus too: not a game's own. */
	{
		Option hdr{ "hdr", Section::Graphics, "HDR",
			"Sends the picture to the TV as HDR10, with the game's brightest parts brighter (HDR highlights). Needs "
			"the console's own HDR on (Settings > Screen and Video > Video Output) and a TV that takes HDR; without "
			"them the picture stays SDR. The TV goes into HDR mode while OGBox runs whether this is on or off.",
			kOnOff, "0", false };
		hdr.now = true;
		o.push_back(hdr);
		Option white{ "hdr_white", Section::Graphics, "HDR white level",
			"203 nits is HDR's reference level; raise it if the picture looks dim.",
			hdrWhiteChoices(), "203", false };
		white.now = true;
		o.push_back(white);
		Option peak{ "hdr_highlights", Section::Graphics, "HDR highlights",
			"Set it to the TV's peak brightness. Off: the SDR picture.",
			hdrPeakChoices(), "1000", false };
		peak.now = true;
		o.push_back(peak);
	}
	/* 0.0.9: the game's picture lighter or darker, its contrast and its colour (adjust.frag, a pass over
	 * the picture before it is scaled: ogbox_main.cpp recordPost); a game's own, at once in a game */
	o.push_back({ "pic_brightness", Section::Graphics, "Brightness",
		"For a game that is too dark or too bright.",
		rangeChoices(-50, 50, 5, "0 (default)"), "0", true });
	o.push_back({ "pic_contrast", Section::Graphics, "Contrast",
		"Spreads the game's darks and lights apart (+) or brings them together (-), about the middle grey. Much more "
		"than +20 loses detail in the darkest and brightest parts.",
		rangeChoices(-50, 50, 5, "0 (default)"), "0", true });
	o.push_back({ "pic_colour", Section::Graphics, "Colour",
		"More (+) or less (-) colour in the game's picture; -50 is black and white.",
		rangeChoices(-50, 50, 5, "0 (default)", "(monochrome)"), "0", true });
	/* 0.0.9: a pass over the picture before it is scaled (ogbox_main.cpp recordPost) */
	o.push_back({ "deband", Section::Graphics, "Debanding",
		"Smooths the colour bands in skies, fog and dark gradients, more visible at high internal resolutions; edges "
		"and detail are kept. Low: the fine steps of 32-bit colour. High: the wider ones of 16-bit colour.",
		{ { "0", "Off" }, { "low", "Low" }, { "high", "High" } }, "0", true });
	o.push_back({ "aspect", Section::Graphics, "Aspect ratio", "",
		{ { "auto", "Auto" }, { "4:3", "4:3" }, { "16:9", "16:9" }, { "stretch", "Stretch to the screen" } },
		"auto", true });
	/* 0.0.6: Bilinear (Sharp) and FSR1 (AMD's EASU) in place of Snapdragon GSR ("sgsr" reads as FSR1) */
	o.push_back({ "upscale", Section::Graphics, "Upscaling filter",
		"Bilinear: the default. FSR1 scales the picture up to 4K; it does nothing at an internal resolution above 4x.",
		{ { "bilinear", "Bilinear" }, { "sharp", "Bilinear (Sharp)" }, { "fsr", "FSR1" } }, "bilinear", true });
	/* 0.0.1: the depth vertex programs store, exactly their screen-space z (hakuX-native's
	 * shader/ff.h HLE_ZEXACT_IN_GLSL); off: the GPU's own, with its early depth test */
	/* 0.0.7: the display's mode, asked of the driver at the title's start (Crux2's switches) */
	o.push_back({ "output_refresh", Section::Graphics, "Display refresh rate",
		"60 Hz: the default. 120 Hz: the console's display is put into its 119.88 Hz mode; each frame of a 60 fps game is "
		"shown twice, the game itself keeps its speed. Needs a TV that accepts 120 Hz. Takes effect at the next start of OGBox.",
		{ { "60", "60 Hz" }, { "120", "120 Hz" } }, "60", false });
	o.push_back({ "vrr", Section::Graphics, "Variable refresh rate (VRR)",
		"At 120 Hz only: lets the TV follow the frames the console sends. Needs a TV with VRR. Takes effect at the next "
		"start of OGBox.", kOnOff, "0", false });
	/* 0.0.7: frame generation (Crux2's three methods): a picture made between two of the game's, shown on the
	 * refresh a new one arrives; a game that draws 30 a second looks like 60 */
	o.push_back({ "framegen", Section::Graphics, "Frame generation",
		"Makes extra frames between the game's own, so a game at 30 fps looks like 60 (and at 120 Hz, 60 like 120). "
		"Adds about one refresh of delay. Crux2's own: the lightest. FSR 3 optical flow: AMD's motion finder. MihawkGen: "
		"a small neural network, the heaviest; its Blend is a plain mix of the two frames. Best up to 4x internal "
		"resolution. Nothing is made for a game that already draws a frame every refresh.",
		{ { "off", "Off" }, { "own", "Crux2 (own motion search)" }, { "flow", "FSR 3 optical flow" },
			{ "neural", "MihawkGen (neural network)" }, { "blend", "MihawkGen (blend)" } }, "off", true });
	o.push_back({ "framegen_quality", Section::Graphics, "Optical flow quality",
		"For FSR 3 optical flow: the picture the motion is found on (at most 540, 720 or 1080 lines).",
		{ { "0", "Performance" }, { "1", "Balanced" }, { "2", "Best" } }, "1", true });
	o.push_back({ "exact_depth", Section::Graphics, "Exact depth",
		"Helps games that struggle with flickering textures.", kOnOff, "1", true });
	/* 0.0.3: dithering into 16-bit targets (hakuX's vk/draw.c NC_HLE_DITHER); 0.0.6: with Force 32-bit
	 * colour (NC_HLE_FORCE32) as one of its choices */
	o.push_back({ "dither", Section::Graphics, "16-bit dithering",
		"Games that draw in 16-bit colour hide its colour bands with a fine pattern of dots. Default: the Xbox's "
		"pattern, by the game's own pixels (at a higher internal resolution it shows as a grain). Scaled dithering: "
		"the same pattern by screen pixel, too fine to see. Force 32-bit: drawn in 32-bit colour, with no bands and "
		"no dots; a few games look wrong with it. Off: no dots, but visible colour bands.",
		{ { "off", "Off" }, { "xbox", "Default" }, { "fine", "Scaled dithering" }, { "force32", "Force 32-bit" } },
		"xbox", true });
	/* 0.0.4: anisotropic filtering and sharpening (hakuX's NC_HLE_ANISO and NC_HLE_SHARPEN) */
	o.push_back({ "aniso", Section::Graphics, "Anisotropic filtering", "",
		{ { "0", "Default" }, { "2", "2x" }, { "4", "4x" }, { "8", "8x" }, { "16", "16x" } }, "0", true });
	/* 0.0.9: the mip levels' bias (hakuX's NC_HLE_LODBIAS) */
	o.push_back({ "lod_bias", Section::Graphics, "Texture sharpness",
		"Textures stay sharper into the distance, the more so at a higher internal resolution. Some games' fine "
		"patterns may shimmer.",
		{ { "0", "Default" }, { "-0.5", "Sharper" }, { "-1", "Sharpest" } }, "0", true });
	o.push_back({ "sharpen", Section::Graphics, "Sharpening", "CAS: sharpens the picture's edges and details.",
		{ { "0", "Off" }, { "30", "Low" }, { "60", "Medium" }, { "90", "High" } }, "0", true });
	/* 0.1.59: the overscan a TV hides, cropped (hakuX-lr's 3/80 a side: the picture keeps its shape) */
	o.push_back({ "crop_overscan", Section::Graphics, "Crop overscan",
		"Hides the picture's edges (3.75% a side) as a TV of the time did, where some games leave garbage or black "
		"borders.", kOnOff, "0", true });
	// ---- Performance: how games run
	/* 0.0.1: the HLE GPU's two threads (the push-buffer decoder, the renderer) on cores of their own */
	o.push_back({ "gpu_cores", Section::Performance, "GPU on separate cores",
		"Runs the Xbox GPU on processor cores of its own. Turn it off when a game stutters or freezes.", kOnOff, "1",
		true });
	// OGBox 0.1.29: hakuX PR #14 (pjperez), the app's "Frame pacing" (off by default)
	o.push_back({ "frame_pacing", Section::Performance, "Frame pacing",
		"Helps the input lag of a game whose drawing can't keep up.", kOnOff, "0", true });
	o.push_back({ "uncapped", Section::Performance, "Uncapped frame rate", "For debugging only.", kOnOff, "0", true });
	// 0.0.2: off by default (the console draws everything); on, a draw whose shader isn't built yet is skipped
	o.push_back({ "async_shaders", Section::Performance, "Compile shaders in the background",
		"The first time a game runs, expect hitches while this is off. On, the hitches are gone, but some textures or "
		"loading screens might be missing.",
		kOnOff, "0", true });
	o.push_back({ "prebuild_shaders", Section::Performance, "Build cached shaders at start",
		"Builds the shaders a game used before, before it starts.", kOnOff, "1", true });
	// ---- Audio (0.1.59's APU, hakuX-lr's: its DSP programs run, or the fast mix; the voices' resampler)
	o.push_back({ "audio_dsp", Section::Audio, "Audio processing",
		"Fast: the game's voices mixed directly. Full DSP: the Xbox's own audio programs run; experimental.",
		{ { "fast", "Fast" }, { "full", "Full DSP" } }, "fast", true });
	/* the MCPX's own resampler (xemu PR #3105, measured on a console) is the default */
	o.push_back({ "audio_resampler", Section::Audio, "Resampler",
		"Normal: the Xbox's own 8-point interpolation. Sinc: cleaner highs than the Xbox. Linear: a little dull.",
		{ { "mcpx", "Normal" }, { "sinc", "Sinc" }, { "linear", "Linear" } }, "mcpx", true });
	// ---- Console: the Xbox itself
	o.push_back({ "language", Section::Console, "Language",
		"Affects the in-game language.",
		{ { "auto", "Auto (English)" }, { "en", "English" }, { "ja", "Japanese" }, { "de", "Deutsch" },
			{ "fr", "Fran\xC3\xA7" "ais" }, { "es", "Espa\xC3\xB1ol" }, { "it", "Italiano" }, { "ko", "Korean" },
			{ "zh", "Chinese" }, { "pt", "Portugu\xC3\xAAs" } },
		"auto", true });
	/* 0.0.7: the boot log kept in memory, written only when something goes wrong (ps5_bootlog.cpp) */
	o.push_back({ "log_mode", Section::Performance, "Boot log",
		"Only when something goes wrong: the log is kept in memory and written to ogbox-boot.log only after a crash, a "
		"failed game, 15 s without a new frame, or the quick menu's Save the Log (no writing to the drive otherwise). "
		"Always written: every line goes to the file as it happens. Takes effect at the next start of OGBox.",
		{ { "problems", "Only when something goes wrong" }, { "always", "Always written" } }, "problems", false });
	o.push_back({ "console_ram", Section::Console, "Memory",
		"The Xbox's RAM. Auto: 128 MB only when a patch needs it to run. 64 MB: the Xbox's default.",
		{ { "auto", "Auto" }, { "64", "64 MB" }, { "128", "128 MB" } }, "auto", true });
	/* 0.0.6: "Drive timing" turned over ("console_timing" 0 reads as on) */
	o.push_back({ "fast_drive", Section::Console, "Fast disc drive", "Faster game loads; might freeze some games.",
		kOnOff, "0", true });
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
	o.push_back({ "av_pack", Section::Video, "Video cable", "",
		{ { "hdtv", "HDTV (component): all modes" }, { "standard", "Standard AV (composite): 480i only" },
			{ "svideo", "S-Video: 480i only" } },
		"hdtv", true });
	// ---- Network
	o.push_back({ "network", Section::Network, "Connection",
		"System link: play with the consoles on your network, or over the Internet through XLink Kai. Xbox Live "
		"through Insignia: work in progress.",
		{ { "off", "Off (no cable)" }, { "lan", "LAN: consoles on this network" },
			{ "kai", "XLink Kai: over the Internet" }, { "insignia", "Xbox Live: Insignia (work in progress)", true } },
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
	// 0.0.8: the saves kept in sync with a share or a USB drive (ogbox_save_sync.h)
	o.push_back({ "save_sync", Section::Network, "Keep saves in sync",
		"The Xbox hard disk's saves (E:\\UDATA) kept the same both ways with OGBox/Saves on a network share or a USB "
		"drive, as Crux2 keeps its memory cards: every console that syncs with the same share has the same saves. When "
		"OGBox starts, after each game, and with Sync now in the hard disk manager, where the place is chosen. A save "
		"changed on both sides: the newer is kept, the other put in OGBox/Saves/.older (on the console: backups/older saves).",
		kOnOff, "0", false });
	{
		Option place{ "save_sync_place", Section::Network, "Saves kept in sync with",
			"Chosen in the hard disk manager (Keep saves in sync with...), or save_sync_place=/smb/<share> in "
			"/data/ogbox/settings.txt.",
			{}, "", false };
		place.text = true;
		o.push_back(place);
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
		place("list_ext", "Internal NVMe", "The console's M.2 storage (/mnt/ext0, ext1), when there.", kOnOff, "1");
		place("list_network", "Network shares", "The shares of /data/ogbox/network.cfg.", kOnOff, "1");
		// 0.1.45: how the list looks
		place("list_covers", "Covers and descriptions",
			"Each disc's box art, downloaded the first time it is shown and kept in the data folder's covers: "
			"libretro's thumbnails by its Redump name, else MobCat's scans of the boxes by the disc's serial, else "
			"the picture in the disc's own default.xbe. A game's page shows its description from Wikipedia.",
			kOnOff, "1");
		place("home_layout", "View", "The games as a row of covers with the chosen one's details beside them, or as a "
			"list with the chosen one's cover and details at the right (for big folders).",
			{ { "covers", "Covers" }, { "list", "List" } }, "covers");
		place("list_layout", "View", "The folder's discs as a list, or as a grid of their covers.",
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
		/* 0.0.9: the startup video (ogbox_intro.h): the X behind black glass, cracked and shattered */
		Option intro{ "startup_video", Section::Interface, "Startup video",
			"The cracked-glass intro with sound when OGBox starts. CROSS or CIRCLE skips it.", kOnOff, "1", false };
		intro.now = true;
		o.push_back(intro);
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
	/* 0.0.6: which corner ("1", as before: the top right) */
	o.push_back({ "show_fps", Section::Interface, "FPS counter",
		"The frame rate and frame time in a corner while playing.",
		{ { "0", "Off" }, { "tl", "Top left" }, { "1", "Top right" }, { "bl", "Bottom left" }, { "br", "Bottom right" } }, "1", false });
	{
		/* 0.1.53 */
		Option qm{ "quick_menu", Section::Interface, "Quick menu button",
			"The button that opens the quick menu in a game. OPTIONS: the Xbox's Start and Back are the touch pad's right "
			"and left halves. OPTIONS held: a short press is Start, held half a second it opens the menu. With a "
			"combination, OPTIONS is the Xbox's Start and the touch pad its Back (Settings > Controller can change both). "
			"TRIANGLE in the quick menu chooses what it shows and in what order.",
			{ { "options", "OPTIONS" }, { "options_hold", "OPTIONS held" }, { "combo", "L1+L2+R1+R2+OPTIONS" },
				{ "touch_hold", "Touch pad held" }, { "l3r3", "L3+R3" } }, "options", false };
		qm.now = true;
		o.push_back(qm);
		/* 0.0.7: what the quick menu shows, in its order (TRIANGLE in the menu edits it): "resume", "save",
		 * "load", "settings", "mods", "drawlog", "restart", "quit" and "opt:<setting>", by commas; "": the default */
		Option layout{ "quick_layout", Section::Interface, "Quick menu layout",
			"What the quick menu shows and in what order. Press TRIANGLE in the quick menu to change it.", {}, "", false };
		layout.text = true;
		layout.now = true;
		o.push_back(layout);
	}
	{
		/* 0.0.9: the DualSense in the menus, as PSWin has it */
		Option vib{ "menu_vibration", Section::Interface, "Vibration",
			"A short buzz at the end of a list and when a game starts. The games' own rumble is Settings > Controller's.",
			kOnOff, "1", false };
		vib.now = true;
		o.push_back(vib);
		Option swap{ "circle_confirms", Section::Interface, "Circle confirms",
			"Circle confirms and Cross goes back, in OGBox's menus only (the games' buttons stay as they are).",
			kOnOff, "0", false };
		swap.now = true;
		o.push_back(swap);
		Option bar{ "light_bar", Section::Interface, "Light bar",
			"The controller's light: in the menus, the chosen game's colour (or the theme's); in a game, each player's "
			"colour, as the PS5 gives them: blue, red, green, pink. Left as the console has it: from the next start.",
			{ { "colours", "Menu and player colours" }, { "console", "Left as the console has it" } }, "colours", false };
		bar.now = true;
		o.push_back(bar);
	}
	// ---- Controller: which DualSense button is each of the Xbox's, the sticks, the deadzones
	{
		for (int i = 0; i < static_cast<int>(XboxControl::Count); i++) {
			const XboxControl c = static_cast<XboxControl>(i);
			const bool startBack = c == XboxControl::Start || c == XboxControl::Back;
			std::vector<Choice> choices;
			for (const PadSource &s : padSources()) {
				if (s.bits != src::automatic || startBack) {
					choices.push_back({ s.value, s.label });
				}
			}
			const PadMap def;
			const char *defValue = "none";
			for (const PadSource &s : padSources()) {
				if (s.bits == def.source[i]) {
					defValue = s.value;
					break;
				}
			}
			Option p{ padKey(c), Section::Controller, padLabel(c), "",
				std::move(choices), defValue, true };
			p.now = true;
			o.push_back(p);
		}
		const std::vector<Choice> dead = { { "0", "Off" }, { "5", "5%" }, { "10", "10%" }, { "15", "15%" }, { "20", "20%" },
			{ "25", "25%" }, { "30", "30%" }, { "35", "35%" }, { "40", "40%" } };
		const auto add = [&](const char *key, const char *label, const char *description, std::vector<Choice> choices,
			const char *def) {
			Option p{ key, Section::Controller, label, description, std::move(choices), def, true };
			p.now = true;
			o.push_back(p);
		};
		add("pad_sticks", "Sticks", "", { { "normal", "Normal" }, { "swapped", "Swapped" } }, "normal");
		add("pad_invert_ly", "Invert left stick", "", kOnOff, "0");
		add("pad_invert_ry", "Invert right stick", "", kOnOff, "0");
		add("pad_dead_l", "Left stick deadzone", "", dead, "0");
		add("pad_dead_r", "Right stick deadzone", "", dead, "0");
		add("pad_dead_t", "Trigger deadzone", "", dead, "0");
		/* 0.0.6: the rumble's strength, as PSWin's game profiles have it (a percent of what the game asks for) */
		add("pad_rumble", "Rumble strength", "", { { "0", "Off" }, { "10", "10%" }, { "20", "20%" }, { "30", "30%" },
			{ "40", "40%" }, { "50", "50%" }, { "60", "60%" }, { "70", "70%" }, { "80", "80%" }, { "90", "90%" }, { "100", "100%" } },
			"100");
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
		const std::string v = trim(s.substr(eq + 1));
		if (option(key)) {
			out[key] = v;
		} else if (key == "force32" && v == "1" && !out.count("dither")) {
			out["dither"] = "force32";     // 0.0.6: Force 32-bit colour is one of the dithering's choices
		} else if (key == "console_timing" && v == "0" && !out.count("fast_drive")) {
			out["fast_drive"] = "1";       // 0.0.6: Drive timing off is Fast disc drive on
		}
	}
	std::fclose(f);
	if (out.count("upscale") && out["upscale"] == "sgsr") {
		out["upscale"] = "fsr";            // 0.0.6: Snapdragon GSR gave way to FSR1
	}
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
	case Section::Network: return "Network and shares";
	case Section::Library: return "Game list";
	case Section::Interface: return "Interface";
	case Section::Controller: return "Controller";
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
	case Section::Controller: return "Which DualSense button is each of the Xbox's, the sticks and their deadzones. A game can have its own.";
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
	flush();   // 0.0.7: what was changed, kept before reading again
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
	dirtyGlobal = true;
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
	dirtyGame = true;
	pshakux_log("settings: %s: %s = %s", gameKey.c_str(), key.c_str(), v.empty() ? "(the app's)" : v.c_str());
}

void Settings::clearOverrides()
{
	gameValues.clear();
	dirtyGame = true;
}

void Settings::flush()
{
	if (dirtyGlobal) {
		saveGlobal();
		dirtyGlobal = false;
	}
	if (dirtyGame) {
		saveGame();
		dirtyGame = false;
	}
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
	a.push_back(std::string("devtime=") + (on("fast_drive") ? "0" : "1"));
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
	// 0.0.4: said either way: hakuX's own default is full DSP wherever its DSP JIT is built in (it is
	// now), so leaving it out for Fast ran full DSP all the same
	// 0.0.4: Fast only. Full DSP's option is hidden (experimental, as in xemu: some games' EP programs
	// never finish a frame, xemu issue #2901) and a setting saved as "full" before is not applied;
	// the code stays (env NC_APU_DSP=1 in boot.txt still turns it on, for testing: then this says
	// nothing)
	if (!std::getenv("NC_APU_DSP")) {
		a.push_back("NC_APU_DSP=0");
	}
	if (value("audio_resampler") == "linear" || value("audio_resampler") == "sinc") {
		a.push_back("NC_APU_RESAMPLER=" + value("audio_resampler"));   // (the core's own default: mcpx)
	}
	if (value("exact_depth") == "0") {   // 0.0.1
		a.push_back("NC_HLE_EXACT_DEPTH=0");
	}
	if (value("dither") == "fine" || value("dither") == "off") {   // 0.0.3
		a.push_back("NC_HLE_DITHER=" + value("dither"));
	}
	if (value("dither") == "force32") {   // 0.0.6: Force 32-bit colour, one of the dithering's choices
		a.push_back("NC_HLE_FORCE32=1");
	}
	// 0.0.4: anisotropic filtering, 32-bit colour, sharpening
	if (value("aniso") == "2" || value("aniso") == "4" || value("aniso") == "8" || value("aniso") == "16") {
		a.push_back("NC_HLE_ANISO=" + value("aniso"));
	}
	if (value("sharpen") == "30" || value("sharpen") == "60" || value("sharpen") == "90") {
		a.push_back("NC_HLE_SHARPEN=" + value("sharpen"));
	}
	if (value("lod_bias") == "-0.5" || value("lod_bias") == "-1") {   // 0.0.9: Texture sharpness
		a.push_back("NC_HLE_LODBIAS=" + value("lod_bias"));
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
	} else if (net == "insignia" && false) {   // 0.0.6: Insignia is greyed out (work in progress)
		const std::string dns = trim(value("insignia_dns"));
		a.push_back("net=nat");
		a.push_back("dns=" + (dns.empty() ? std::string("46.101.64.175") : dns));
	}
	return a;
}

} // namespace ogbox
