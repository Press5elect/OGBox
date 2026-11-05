/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.3: the menus' look. A neutral dark ground, one accent, Manrope for titles and Inter for
 * text, everything sized for a 1080-line screen and scaled (K) to the real one, and motion that
 * answers the controller: the selection slides, the chosen cover grows, the background takes the
 * chosen game's colours and drifts.
 *
 * The screens here draw from a view (what to show) into an ImGui draw list and keep their own
 * motion state; the title fills the views and reads the controller (ogbox_main.cpp). Nothing here
 * knows the console, Vulkan or the emulator, so tests/ui renders every screen on a computer
 * (a software rasterizer, PNG files) to look at it before a build goes to the console.
 */
#ifndef OGBOX_UI_H
#define OGBOX_UI_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

namespace ui {

// ---- scale, palette, type -------------------------------------------------------------------

/* The screen's height over 1080: every size here is a 1080-line one times it. */
void setScale(float k);
float K();
inline float px(float v) { return v * K(); }

enum class Theme { Dark, Code };
struct Palette {
	ImU32 bg, surface, surface2, surfaceHi, border, text, textDim, textFaint, accent, accentInk, accentSoft, accentText,
		warn, warnSoft, warnText, fail, ok;
};
void setTheme(Theme t);
Theme theme();
const Palette &P();
ImU32 alpha(ImU32 c, float a);          // c with its alpha times a
ImU32 mix(ImU32 a, ImU32 b, float t);

enum class F { Display, Title, Heading, Body, Small, Label, Count };
/* The typefaces, baked at their sizes into the atlas (before it is built). dir: the folder with
 * Manrope-ExtraBold.ttf, Manrope-Bold.ttf, Inter-Medium.ttf, Inter-SemiBold.ttf. */
bool loadFonts(ImFontAtlas *atlas, const std::string &dir);
bool fontsLoaded();
ImFont *font(F f);

ImVec2 measure(F f, float size, const char *s, const char *end = nullptr, float wrap = 0.0f);
void text(ImDrawList *dl, F f, float size, ImVec2 p, ImU32 col, const char *s, const char *end = nullptr, float wrap = 0.0f);
/* One line cut with an ellipsis to maxW; its width. */
float textFit(ImDrawList *dl, F f, float size, ImVec2 p, ImU32 col, const std::string &s, float maxW);
/* Upper-case letters spaced out (a label); its width. */
float label(ImDrawList *dl, float size, ImVec2 p, ImU32 col, const std::string &s, bool measureOnly = false);
/* Lines of s wrapped to w, at most maxLines (the last cut with an ellipsis); the height. */
float paragraph(ImDrawList *dl, F f, float size, ImVec2 p, ImU32 col, const std::string &s, float w, int maxLines = 99,
	float lineGap = 1.35f);

// ---- time and motion --------------------------------------------------------------------------

/* Once a frame, before any screen draws. */
void beginFrame(double seconds);
double now();
float dt();
/* A value that follows its target, quickly at first then gently (critically damped). */
float approach(float current, float target, float rate = 14.0f);
/* A persistent value by name (a row's highlight, a list's scroll); created at init. */
float &value(const std::string &key, float init = 0.0f);
float easeOut(float t);
float smooth(float t);

// ---- images -----------------------------------------------------------------------------------

struct Tex {
	ImTextureID id{ nullptr };
	float w{ 0.0f }, h{ 0.0f };
	explicit operator bool() const { return id != nullptr && w > 0.0f && h > 0.0f; }
};
/* How an image is made from RGBA pixels (the title's: a Vulkan texture; the tests': theirs). */
using TexMaker = std::function<Tex(const uint8_t *rgba, int w, int h)>;
void setTexMaker(TexMaker maker);
/* A soft round light, white in the middle, gone at the edge (drawn tinted). */
Tex glow();

// ---- pieces -----------------------------------------------------------------------------------

/* The DualSense's buttons: X O S T (cross, circle, square, triangle), L1 R1 L2 R2 OPT TP, LR (left
 * and right), UD. Drawn as an outlined key; its width. */
float keycap(ImDrawList *dl, ImVec2 p, float h, const std::string &name, ImU32 col, ImU32 fill = 0, bool measureOnly = false);
struct Hint {
	std::string key, text;
};
/* A key and its words; its width. */
float hint(ImDrawList *dl, ImVec2 p, const Hint &h, ImU32 col, bool measureOnly = false);
/* The hints along the foot of the screen, and words at its right. */
void footer(ImDrawList *dl, ImVec2 screen, const std::vector<Hint> &hints, const std::string &right = std::string());

enum class Icon { None, Folder, Disc, Up, Console, Usb, Network, Server, Gear, Power, Dash, Check, Plus, Lock, Warn, Pad, Keyboard };
void icon(ImDrawList *dl, Icon i, ImVec2 centre, float size, ImU32 col);

/* A rounded label (a game's status, its region); its width. */
float chip(ImDrawList *dl, ImVec2 p, const std::string &s, ImU32 fill, ImU32 col, bool measureOnly = false);
/* Choices side by side, the chosen one filled; its width. */
float segmented(ImDrawList *dl, ImVec2 p, const std::vector<std::string> &items, int on, bool focused, bool measureOnly = false);
/* An on/off switch (its knob slides: anim 0..1). */
void toggle(ImDrawList *dl, ImVec2 p, float anim, bool focused);
/* A text field's box with its value (or the hint, faint), a cursor when editing. */
void field(ImDrawList *dl, ImVec2 p0, ImVec2 p1, const std::string &value, const std::string &placeholder, bool focused,
	bool password = false, int cursor = -1);
/* A filled rounded rectangle with a faint border. */
void panel(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float r, ImU32 fill, ImU32 border = 0);
/* A soft shadow under a box. */
void shadow(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float r, float spread, float strength = 0.55f);
/* A spinner. */
void spinner(ImDrawList *dl, ImVec2 c, float r, ImU32 col);

// ---- the backdrop -----------------------------------------------------------------------------

/* The ground behind every screen: the colour, a tint's soft light drifting, the chosen game's cover
 * (blurred: a tiny image drawn large) cross-faded as the choice changes, darkened at the edges. */
struct Backdrop {
	ImU32 tint{ 0 };              // the light's colour (a game's), 0 the accent's
	Tex blur;                     // the chosen game's cover, tiny
	std::string key;              // what it is of (a change cross-fades)
	ImVec2 focus{ 0.42f, 0.55f }; // where the light sits (0..1)
	float strength{ 1.0f };
};
void backdrop(ImDrawList *dl, ImVec2 size, const Backdrop &b);

// ---- the game list ----------------------------------------------------------------------------

struct HomeGame {
	std::string id;               // its path (the motion's key)
	std::string title;            // shown under/over the cover
	std::string genre;            // a word over the placeholder ("" none)
	std::string sub, size;        // the list's second line: its region, its size
	ImU32 tint{ 0 };              // its colour (the placeholder's; the light's)
};
struct HomeCard {
	std::string title, sub;
	Icon icon{ Icon::Folder };
	bool dashed{ false };         // an action ("Add a network share")
	bool accent{ false };         // its icon in the accent (the dashboard)
};
struct HomeView {
	bool list{ false };           // 0.0.3: the list layout (the cards first, then the games, one column)
	// the head
	Tex logo;
	std::vector<std::string> tabs;
	int tab{ 0 };
	std::vector<std::pair<std::string, ImU32>> status;   // at the right: words and a dot's colour (0 none)
	float coverProgress{ -1.0f };   // 0.0.3: the covers downloading, 0..1 (under the status; < 0 none)
	std::string coverText;          // ... "Covers 12 of 140 · 8%"
	// the games along the row
	std::vector<HomeGame> games;
	std::function<Tex(size_t)> cover;   // a game's cover (only those in sight are asked)
	std::function<Tex(size_t)> blur;    // ... its tiny one
	int gameSel{ -1 };
	// the chosen game
	std::string kicker;           // "CONTINUE", "PLAY"
	std::string title;
	std::vector<std::pair<std::string, int>> chips;   // words and a kind (0 plain, 1 good, 2 warn)
	std::string meta;             // a line or two
	std::string primary, secondary;   // the buttons ("Continue where I left off", "Start from the beginning")
	std::string primaryKey{ "X" }, secondaryKey{ "S" };
	// the row of cards below (folders, the dashboard, an action)
	std::string cardsTitle, cardsSub;
	std::vector<HomeCard> cards;
	int cardSel{ -1 };
	int focusRow{ 0 };            // 0 the games, 1 the cards
	// when there is nothing
	std::string empty, error;
	bool loading{ false };
	char jumpLetter{ 0 };
	float jumpAlpha{ 0.0f };
	std::vector<Hint> footer;
	std::string version;
	std::string busy;             // "Starting Hollow Harbor..." over everything
};
void drawHome(ImDrawList *dl, ImVec2 screen, const HomeView &v);

// ---- settings -----------------------------------------------------------------------------------

struct SetRow {
	enum class Kind { Choice, Toggle, Number, Text, Action, Info, Group, Share };
	Kind kind{ Kind::Choice };
	std::string key;              // the motion's key
	std::string name, desc;
	std::vector<std::string> choices;   // a few short ones: drawn side by side; else the value
	int choice{ 0 };
	std::string value;            // the value's words (Choice with many, Number, Text, Info, Action's right)
	std::string placeholder;
	bool on{ false };
	bool changed{ false };        // not its default (marked)
	bool inherited{ false };      // a game's setting that follows the app's
	bool password{ false };
	Icon icon{ Icon::None };
	ImU32 dot{ 0 };               // a status dot before the value (a share's)
	bool warn{ false };
};
struct SetNav {
	std::string name, sub;
	ImU32 dot{ 0 };
	bool warn{ false };           // its sub in the warning colour
	bool gapBefore{ false };
};
struct SettingsView {
	Tex logo;
	std::string title, subtitle, right;
	std::vector<SetNav> nav;
	int navSel{ 0 };
	bool focusNav{ true };
	std::string pageTitle, pageAction;   // the page's head, and an action at its right ("reset this section")
	std::string banner;
	bool bannerWarn{ false };
	std::vector<SetRow> rows;
	int rowSel{ 0 };
	ImU32 glowTint{ 0 };
	std::vector<Hint> footer;
	std::string footerRight;
};
void drawSettings(ImDrawList *dl, ImVec2 screen, const SettingsView &v);
/* A list of rows in a box (a page's; a form's): its rows from scroll, the chosen one framed and its
 * words shown; returns the height it took. */
float drawRows(ImDrawList *dl, ImVec2 p0, ImVec2 p1, const std::vector<SetRow> &rows, int sel, bool focused,
	const std::string &scrollKey);

// ---- a form over a screen (a network share) ----------------------------------------------------

struct FormView {
	std::string title, note;
	std::vector<SetRow> rows;     // Text fields, a Choice (the protocol), Actions (the buttons)
	int sel{ 0 };
	std::string status;           // under the rows ("Connecting...", the error)
	bool statusBad{ false };
	bool busy{ false };
};
void drawForm(ImDrawList *dl, ImVec2 screen, const FormView &v, float appear);

// ---- the on-screen keyboard -------------------------------------------------------------------

struct Pad {
	bool up{ false }, down{ false }, left{ false }, right{ false };
	bool cross{ false }, circle{ false }, square{ false }, triangle{ false };
	bool l1{ false }, r1{ false }, l2{ false }, r2{ false }, options{ false };
};

class Keyboard {
public:
	enum class Kind { Text, Address, Number };
	using Done = std::function<void(bool ok, const std::string &value)>;
	void open(const std::string &title, const std::string &value, const std::string &placeholder, Kind kind, bool password,
		Done done);
	bool isOpen() const { return openFlag; }
	void close(bool ok);
	/* The controller's presses this frame (with repeats); typed: what a USB keyboard typed (text,
	 * and \b backspace, \n enter, \x1b escape, \x02 left, \x03 right). */
	void input(const Pad &pad, const std::string &typed = std::string());
	void draw(ImDrawList *dl, ImVec2 screen, bool usbKeyboard);
	const std::string &value() const { return text; }

private:
	bool openFlag{ false };
	std::string title, placeholder, text;
	Kind kind{ Kind::Text };
	bool password{ false };
	Done done;
	int cursor{ 0 };
	int row{ 1 }, col{ 0 };
	int layer{ 0 };               // 0 lower, 1 upper, 2 symbols
	bool shiftOnce{ false };
	double openedAt{ 0.0 };
	void press(const std::string &key);
	std::vector<std::vector<std::string>> layout() const;
};

// ---- the game page -----------------------------------------------------------------------------

struct GameFact {
	std::string label, value;
};
struct GameState {
	std::string label;
	Tex picture;
	bool empty{ false };
	bool left{ false };           // "where I left off"
};
struct GameAction {
	std::string name, sub;
};
struct GameView {
	std::string title, coverTitle;
	Tex cover;
	ImU32 tint{ 0 };
	std::vector<std::pair<std::string, int>> chips;
	std::vector<GameFact> facts;
	std::string about, aboutSource;   // 0.0.3: its description and where it is from ("" none)
	std::vector<GameState> states;
	int stateSel{ -1 };           // the states have the controller
	std::vector<GameAction> actions;
	int actionSel{ 0 };
	std::vector<Hint> footer;
};
void drawGamePage(ImDrawList *dl, ImVec2 screen, const GameView &v, float appear);

// ---- a choice over a screen, and a message -----------------------------------------------------

struct PopupView {
	std::string title, text;
	std::vector<std::string> items;
	int sel{ 0 };
};
void drawPopup(ImDrawList *dl, ImVec2 screen, const PopupView &v, float appear);
void drawToast(ImDrawList *dl, ImVec2 screen, const std::string &text, float alpha);

/* A colour for a name (a game without a cover): one of a few dark, saturated ones. */
ImU32 tintFor(const std::string &name);
/* A picture's average colour, deepened (a cover's light). */
ImU32 tintOf(const uint8_t *rgba, int w, int h);
/* A picture made tiny (w x h), for a blurred backdrop. */
std::vector<uint8_t> shrink(const uint8_t *rgba, int w, int h, int tw, int th);

} // namespace ui

#endif
