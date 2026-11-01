/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.26: the settings, hakuX's every one (its Android app's AppPrefs.kt and GameSettings.kt:
 * the same keys, choices, defaults and words) and OGBox's own, kept in /data/ogbox/settings.txt
 * (key=value lines) with a game's overrides in /data/ogbox/games/<title ID>.<version>.txt, as
 * hakuX keeps a game's (GameOverrides: the disc's title ID and certificate version). What the core
 * takes becomes its arguments (coreArgs: hakuX's NativeCoreActivity.kt, the same words); the
 * picture's shape, FXAA, the upscaling and the FPS counter are OGBox's presenter's.
 */
#ifndef OGBOX_SETTINGS_H
#define OGBOX_SETTINGS_H

#include <map>
#include <string>
#include <vector>

namespace ogbox {

enum class Section { Graphics, Console, Video, Network, Interface, Count };
const char *sectionName(Section s);

struct Choice {
	const char *value;
	const char *label;
};

struct Option {
	const char *key;
	Section section;
	const char *label;
	const char *description;
	std::vector<Choice> choices;  // empty: a number (minimum..maximum) or text
	const char *def;
	bool perGame;                 // a game can have its own (hakuX's GameSetting)
	int minimum{ 0 }, maximum{ 0 }; // a number's range
	bool text{ false };           // free text (an address), set in settings.txt
	const char *unit{ "" };
};

const std::vector<Option> &options();
const Option *option(const std::string &key);

class Settings {
public:
	/* /data/ogbox/settings.txt; with a key, that game's overrides too ("" none). */
	void load(const std::string &gameKey = std::string());
	const std::string &game() const { return gameKey; }

	/* The app's value (its default when not set). */
	std::string global(const std::string &key) const;
	/* The game's own value, "" when it follows the app's. */
	std::string override_(const std::string &key) const;
	/* What a run uses. */
	std::string value(const std::string &key) const;
	bool on(const std::string &key) const { return value(key) == "1"; }
	int number(const std::string &key) const;

	void setGlobal(const std::string &key, const std::string &v);
	/* "" back to the app's */
	void setOverride(const std::string &key, const std::string &v);
	void clearOverrides();
	int overrides() const { return static_cast<int>(gameValues.size()); }

	/* The label of a value ("4:3", "On", "2x"). */
	static std::string label(const Option &o, const std::string &v);
	/* The next or the previous choice (a number: a step). */
	static std::string step(const Option &o, const std::string &v, int by);

	/* hakuX's core's arguments for this run (native/core/options.h). */
	std::vector<std::string> coreArgs() const;

private:
	std::string gameKey;
	std::map<std::string, std::string> globalValues, gameValues;
	void saveGlobal() const;
	void saveGame() const;
};

std::string gameSettingsPath(const std::string &key);

} // namespace ogbox

#endif
