/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tests/ui: every menu screen drawn on a computer, to look at before a build goes to the console.
 * ImGui's draw lists are rasterized here in software (textured, coloured triangles, clip rectangles,
 * alpha blending) into raw RGBA files; run-ui-tests.sh turns them into PNGs.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "ogbox_ui.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

struct Image {
	int w{ 0 }, h{ 0 };
	std::vector<uint8_t> px;   // RGBA
};
std::vector<std::unique_ptr<Image>> g_images;

ui::Tex make(const uint8_t *rgba, int w, int h)
{
	auto im = std::make_unique<Image>();
	im->w = w;
	im->h = h;
	im->px.assign(rgba, rgba + static_cast<size_t>(w * h * 4));
	ui::Tex t{ im.get(), static_cast<float>(w), static_cast<float>(h) };
	g_images.push_back(std::move(im));
	return t;
}

ui::Tex loadPng(const std::string &path)
{
	int w, h, n;
	uint8_t *d = stbi_load(path.c_str(), &w, &h, &n, 4);
	if (!d) {
		return {};
	}
	ui::Tex t = make(d, w, h);
	stbi_image_free(d);
	return t;
}

struct Canvas {
	int w, h;
	std::vector<float> c;   // RGB, linear 0..1 (sRGB values, blended as ImGui's backend does)
	Canvas(int w_, int h_) : w(w_), h(h_), c(static_cast<size_t>(w_ * h_ * 3), 0.0f) {}
};

void sample(const Image *im, float u, float v, float out[4])
{
	if (!im) {
		out[0] = out[1] = out[2] = out[3] = 1.0f;
		return;
	}
	// bilinear, clamped
	const float x = u * im->w - 0.5f, y = v * im->h - 0.5f;
	const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
	const float fx = x - x0, fy = y - y0;
	float acc[4]{};
	for (int j = 0; j < 2; j++) {
		for (int i = 0; i < 2; i++) {
			const int xx = std::max(0, std::min(im->w - 1, x0 + i)), yy = std::max(0, std::min(im->h - 1, y0 + j));
			const float wgt = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
			const uint8_t *p = &im->px[static_cast<size_t>((yy * im->w + xx) * 4)];
			for (int k = 0; k < 4; k++) {
				acc[k] += p[k] / 255.0f * wgt;
			}
		}
	}
	std::memcpy(out, acc, sizeof acc);
}

void triangle(Canvas &cv, const ImDrawVert &a, const ImDrawVert &b, const ImDrawVert &c, const Image *tex, const ImVec4 &clip)
{
	const float minx = std::max(clip.x, std::min({ a.pos.x, b.pos.x, c.pos.x }));
	const float maxx = std::min(clip.z, std::max({ a.pos.x, b.pos.x, c.pos.x }));
	const float miny = std::max(clip.y, std::min({ a.pos.y, b.pos.y, c.pos.y }));
	const float maxy = std::min(clip.w, std::max({ a.pos.y, b.pos.y, c.pos.y }));
	const int x0 = std::max(0, static_cast<int>(std::floor(minx))), x1 = std::min(cv.w - 1, static_cast<int>(std::ceil(maxx)));
	const int y0 = std::max(0, static_cast<int>(std::floor(miny))), y1 = std::min(cv.h - 1, static_cast<int>(std::ceil(maxy)));
	const float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
	if (std::fabs(area) < 1e-6f) {
		return;
	}
	const auto col = [](ImU32 v, int s) { return static_cast<float>((v >> s) & 0xFF) / 255.0f; };
	for (int y = y0; y <= y1; y++) {
		const float py = y + 0.5f;
		if (py < clip.y || py >= clip.w) {
			continue;
		}
		for (int x = x0; x <= x1; x++) {
			const float pxx = x + 0.5f;
			if (pxx < clip.x || pxx >= clip.z) {
				continue;
			}
			float w0 = ((b.pos.x - pxx) * (c.pos.y - py) - (b.pos.y - py) * (c.pos.x - pxx)) / area;
			float w1 = ((c.pos.x - pxx) * (a.pos.y - py) - (c.pos.y - py) * (a.pos.x - pxx)) / area;
			float w2 = 1.0f - w0 - w1;
			if (w0 < 0 || w1 < 0 || w2 < 0) {
				continue;
			}
			const float u = a.uv.x * w0 + b.uv.x * w1 + c.uv.x * w2;
			const float v = a.uv.y * w0 + b.uv.y * w1 + c.uv.y * w2;
			float t[4];
			sample(tex, u, v, t);
			float s[4];
			for (int k = 0; k < 4; k++) {
				const int sh = k * 8;
				s[k] = (col(a.col, sh) * w0 + col(b.col, sh) * w1 + col(c.col, sh) * w2) * t[k];
			}
			float *d = &cv.c[static_cast<size_t>((y * cv.w + x) * 3)];
			for (int k = 0; k < 3; k++) {
				d[k] = s[k] * s[3] + d[k] * (1.0f - s[3]);
			}
		}
	}
}

void render(Canvas &cv, ImDrawData *dd)
{
	for (int n = 0; n < dd->CmdListsCount; n++) {
		const ImDrawList *l = dd->CmdLists[n];
		const ImDrawIdx *idx = l->IdxBuffer.Data;
		for (const ImDrawCmd &cmd : l->CmdBuffer) {
			if (cmd.UserCallback) {
				cmd.UserCallback(l, &cmd);
			} else {
				const Image *tex = static_cast<const Image *>(cmd.TextureId);
				for (unsigned i = 0; i + 2 < cmd.ElemCount + 0u; i += 3) {
					triangle(cv, l->VtxBuffer[idx[i]], l->VtxBuffer[idx[i + 1]], l->VtxBuffer[idx[i + 2]], tex, cmd.ClipRect);
				}
			}
			idx += cmd.ElemCount;
		}
	}
}

void save(const Canvas &cv, const std::string &path)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f) {
		return;
	}
	std::fprintf(f, "P6\n%d %d\n255\n", cv.w, cv.h);
	std::vector<uint8_t> row(static_cast<size_t>(cv.w * 3));
	for (int y = 0; y < cv.h; y++) {
		for (int x = 0; x < cv.w * 3; x++) {
			row[static_cast<size_t>(x)] = static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(1.0f, cv.c[static_cast<size_t>(y * cv.w * 3 + x)])) * 255.0f));
		}
		std::fwrite(row.data(), 1, row.size(), f);
	}
	std::fclose(f);
}

// a made-up cover: a gradient with shapes (no game art in the repository)
ui::Tex fakeCover(uint32_t seed, int w = 240, int h = 336)
{
	std::vector<uint8_t> px(static_cast<size_t>(w * h * 4));
	const float hue = static_cast<float>(seed % 360);
	const auto hsv = [](float hdeg, float s, float v, float out[3]) {
		const float c = v * s, x = c * (1 - std::fabs(std::fmod(hdeg / 60.0f, 2.0f) - 1)), m = v - c;
		float r = 0, g = 0, b = 0;
		const int i = static_cast<int>(hdeg / 60.0f) % 6;
		if (i == 0) { r = c; g = x; } else if (i == 1) { r = x; g = c; } else if (i == 2) { g = c; b = x; }
		else if (i == 3) { g = x; b = c; } else if (i == 4) { r = x; b = c; } else { r = c; b = x; }
		out[0] = r + m; out[1] = g + m; out[2] = b + m;
	};
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float c[3];
			const float t = static_cast<float>(y) / h;
			hsv(std::fmod(hue + t * 40.0f, 360.0f), 0.7f, 0.25f + 0.55f * (1.0f - t), c);
			const float dx = x - w * 0.6f, dy = y - h * 0.38f;
			if (dx * dx + dy * dy < (w * 0.28f) * (w * 0.28f)) {
				hsv(std::fmod(hue + 180.0f, 360.0f), 0.5f, 0.9f, c);
			}
			uint8_t *p = &px[static_cast<size_t>((y * w + x) * 4)];
			p[0] = static_cast<uint8_t>(c[0] * 255); p[1] = static_cast<uint8_t>(c[1] * 255); p[2] = static_cast<uint8_t>(c[2] * 255); p[3] = 255;
		}
	}
	return make(px.data(), w, h);
}

struct Frame {
	std::string name;
	std::function<void(ImDrawList *, ImVec2)> draw;
};

} // namespace

int main(int argc, char **argv)
{
	const std::string root = argc > 1 ? argv[1] : ".";
	const std::string out = argc > 2 ? argv[2] : ".";
	const int W = 3840, H = 2160;
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
	io.DisplaySize = ImVec2(static_cast<float>(W), static_cast<float>(H));
	ui::setScale(H / 1080.0f);
	{
		const std::string inter = root + "/ps5/assets/fonts/Inter-Medium.ttf";
		io.Fonts->AddFontFromFileTTF(inter.c_str(), 17.0f * 2.5f);   // the overlay's Fonts[0], as on the console
	}
	if (!ui::loadFonts(io.Fonts, root + "/ps5/assets/fonts")) {
		std::fprintf(stderr, "fonts missing\n");
		return 1;
	}
	unsigned char *fp;
	int fw, fh;
	io.Fonts->GetTexDataAsRGBA32(&fp, &fw, &fh);
	std::printf("font atlas %d x %d\n", fw, fh);
	io.Fonts->TexID = make(fp, fw, fh).id;
	ui::setTexMaker(make);
	const ui::Tex logo = loadPng(root + "/ps5/art/ogbox-logo.png");

	// the games: made-up names, made-up covers (two without one, as a library has)
	const char *names[] = { "Hollow Harbor", "Neon Drift Racing", "Starfall Tactics", "Brick & Mortar", "Ironclad Legion",
		"The Lantern Keeper", "Polar Rally 2", "Skyline Couriers", "Quiet Machines", "Deep Blue Salvage" };
	std::vector<ui::HomeGame> games;
	std::vector<ui::Tex> covers, blurs;
	for (int i = 0; i < 10; i++) {
		ui::HomeGame g;
		g.id = std::string("/games/") + names[i];
		g.title = names[i];
		g.genre = i % 3 == 0 ? "Action" : i % 3 == 1 ? "Racing" : "Strategy";
		const bool has = i != 1 && i != 4;
		covers.push_back(has ? fakeCover(static_cast<uint32_t>(i * 47 + 20)) : ui::Tex{});
		if (has) {
			const Image *im = static_cast<const Image *>(covers.back().id);
			g.tint = ui::tintOf(im->px.data(), im->w, im->h);
			const auto tiny = ui::shrink(im->px.data(), im->w, im->h, 12, 16);
			blurs.push_back(make(tiny.data(), 12, 16));
		} else {
			g.tint = ui::tintFor(g.title);
			blurs.push_back({});
		}
		games.push_back(g);
	}

	ui::HomeView home;
	home.logo = logo;
	home.tabs = { "Games", "USB", "Network" };
	home.status = { { "Insignia", ui::P().ok }, { "21:42", 0 } };
	home.coverProgress = 0.37f;   // 0.0.3: the covers downloading
	home.coverText = "Covers 52 of 140";
	home.games = games;
	home.cover = [&](size_t i) { return covers[i]; };
	home.blur = [&](size_t i) { return blurs[i]; };
	home.gameSel = 2;
	home.kicker = "Continue";
	home.title = names[2];
	home.chips = { { "Plays well", 1 }, { "NTSC", 0 }, { "Played 2 h ago", 0 } };
	home.meta = "Left off 2 hours ago, in the middle of chapter 3. Save states: 4.";
	home.about = "Starfall Tactics is a made-up turn-based strategy game for the tests. A fleet of salvaged ships crosses a broken "
		"star system, trading fuel for passage with the stations that survived, and every battle is fought on a grid that "
		"drifts with the system's two suns. Its campaign branches on which stations the player spares.";
	home.aboutSource = "From Wikipedia, \"Starfall Tactics\" \xC2\xB7 CC BY-SA 4.0";
	home.primary = "Continue";
	home.secondary = "Start from the beginning";
	home.cardsTitle = "Games";
	home.cardsSub = "/data/ogbox/games";
	home.cards = { { "Up", "ogbox", ui::Icon::Up }, { "Classics", "", ui::Icon::Folder }, { "Racing", "", ui::Icon::Folder },
		{ "Xbox dashboard", "", ui::Icon::Dash, false, true } };
	home.footer = { { "X", "Continue" }, { "S", "From the start" }, { "T", "Game Details" }, { "L2 R2", "A to Z" }, { "OPT", "Settings" },
		{ "O", "Back" } };
	home.version = "OGBox 0.0.3";

	ui::SettingsView set;
	set.logo = logo;
	set.title = "Settings";
	set.nav = { { "Picture and sound", "2x \xC2\xB7 Auto" }, { "XBOX", "English \xC2\xB7 HDTV" },
		{ "Advanced", "experimental", 0, true }, { "Network and shares", "LAN \xC2\xB7 2 shares", ui::P().ok, false, true },
		{ "Game list", "with covers" }, { "Interface", "Dark" }, { "Updates", "0.0.6 \xC2\xB7 the newest" }, { "About", "0.0.6" } };
	set.navSel = 0;
	set.focusNav = false;
	set.pageTitle = "Picture and sound";
	set.pageAction = "";
	{
		ui::SetRow r;
		r.key = "render_scale"; r.name = "Internal resolution"; r.choices = { "1x (Xbox default)", "2x", "3x", "4x" }; r.choice = 2;
		r.desc = "1x: the Xbox's default. 2x for a 1080p screen, 3x for a 1440p screen, 4x for a 4K screen.";
		set.rows.push_back(r);
		r = {}; r.key = "aspect"; r.name = "Shape"; r.choices = { "As the game", "4:3", "16:9", "Stretch" }; r.choice = 0;
		set.rows.push_back(r);
		r = {}; r.key = "upscale"; r.name = "Upscaling filter"; r.choices = { "Bilinear", "Bilinear (Sharp)", "FSR1" }; r.choice = 2; r.changed = true;
		set.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Toggle; r.key = "fxaa"; r.name = "Smooth edges (FXAA)"; r.on = true;
		set.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Toggle; r.key = "crop"; r.name = "Crop the overscan"; r.on = false;
		set.rows.push_back(r);
		r = {}; r.key = "dsp"; r.name = "Sound processing"; r.choices = { "Full", "Fast" }; r.choice = 0;
		set.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Toggle; r.key = "fps"; r.name = "Show the frame rate"; r.on = false;
		set.rows.push_back(r);
	}
	set.rowSel = 0;
	set.footer = { { "UD", "Choose" }, { "LR", "Change" }, { "T", "Default" }, { "O", "Back" } };

	ui::SettingsView net = set;
	net.navSel = 3;
	net.pageTitle = "Network and shares";
	net.pageAction = "";
	net.rows.clear();
	{
		ui::SetRow r;
		r.kind = ui::SetRow::Kind::Group; r.key = "g1"; r.name = "Online play"; net.rows.push_back(r);
		r = {}; r.key = "conn"; r.name = "Connection"; r.choices = { "Off (no cable)", "LAN: consoles on this network", "XLink Kai: over the Internet",
			"Xbox Live: Insignia (work in progress)" }; r.choice = 3; r.offChoices = { 3 };
		net.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Text; r.key = "dns"; r.name = "Insignia DNS"; r.value = "46.101.64.175"; r.placeholder = "DNS";
		r.desc = "Accounts are made on insignia.live and tied to the console's identity; sign in from the Xbox dashboard.";
		net.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Text; r.key = "kai"; r.name = "XLink Kai engine"; r.placeholder = "address";
		net.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Group; r.key = "g2"; r.name = "Network shares"; net.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Share; r.key = "s1"; r.name = "Synology"; r.desc = "smb://192.168.1.10/games/xbox \xC2\xB7 as kuba";
		r.value = "Connected \xC2\xB7 212 games"; r.dot = ui::P().ok; r.icon = ui::Icon::Server; net.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Share; r.key = "s2"; r.name = "Desk PC"; r.desc = "nfs://192.168.1.22/export/xbox";
		r.value = "Not reachable"; r.dot = ui::P().warn; r.icon = ui::Icon::Server; net.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Action; r.key = "add"; r.name = "Add a network share"; r.value = "SMB or NFS"; r.icon = ui::Icon::Plus;
		net.rows.push_back(r);
	}
	net.rowSel = 2;
	net.footer = { { "X", "Edit" }, { "S", "Test" }, { "T", "Remove" }, { "O", "Back" } };

	ui::FormView form;
	form.title = "Add a network share";
	{
		ui::SetRow r;
		r.kind = ui::SetRow::Kind::Text; r.key = "name"; r.name = "Name"; r.value = "Synology"; form.rows.push_back(r);
		r = {}; r.key = "proto"; r.name = "Protocol"; r.choices = { "SMB", "NFS" }; form.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Text; r.key = "host"; r.name = "Server"; r.value = "192.168.1.1"; form.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Text; r.key = "share"; r.name = "Share and folder"; r.placeholder = "games/xbox"; form.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Text; r.key = "user"; r.name = "User"; r.placeholder = "empty for a guest"; form.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Text; r.key = "pass"; r.name = "Password"; r.password = true; r.value = "secret"; form.rows.push_back(r);
		r = {}; r.kind = ui::SetRow::Kind::Action; r.key = "go"; r.name = "Connect and add"; form.rows.push_back(r);
	}
	form.sel = 2;
	form.note = "Saved to /data/ogbox/network.cfg; the password stays on the console.";
	ui::Keyboard kb;

	ui::GameView gp;
	gp.title = names[2];
	gp.cover = covers[2];
	gp.chips = { { "Plays well", 1 }, { "NTSC-U", 0 } };
	gp.facts = { { "Played", "12 h 40 min" }, { "Last", "2 hours ago" }, { "Title ID", "4D530064" }, { "Folder", "Games" } };
	gp.about = "A made-up game for the tests: a lighthouse keeper's daughter crosses a harbour of drowned streets, "
		"trading lamps for passage with the creatures that live under the bridges, in a world that changes with the tide. "
		"Its levels are built from the same twelve rooms, rearranged each night by the sea.";
	gp.aboutSource = "From Wikipedia, \"Hollow Harbor\" \xC2\xB7 CC BY-SA 4.0";
	for (int i = 0; i < 4; i++) {
		ui::GameState s;
		s.label = i == 0 ? "Where I left off" : "Slot " + std::to_string(i);
		s.picture = fakeCover(static_cast<uint32_t>(100 + i * 70), 320, 240);
		gp.states.push_back(s);
	}
	gp.states.push_back({ "Empty slot", {}, true });
	gp.facts = { { "Last played", "2 hours ago" }, { "Title ID", "4D530064.01" }, { "Serial", "MS-064" }, { "File", "Starfall Tactics.iso" } };
	gp.actions = { { "Continue", "Where you left off, 2 hours ago", ui::Icon::Play }, { "From the start", "", ui::Icon::Restart },
		{ "Load State", "4 saved", ui::Icon::Load }, { "Settings", "3 of its own", ui::Icon::Gear },
		{ "Game Patches", "12 for it, 2 on", ui::Icon::Puzzle }, { "Import Saves", "", ui::Icon::Save } };
	gp.footer = { { "LR", "Choose" }, { "X", "Open" }, { "O", "Back" } };

	ui::PopupView pop;
	pop.title = "Where you left off";
	pop.text = "This game has a save state from when you last left it, 2 hours ago.";
	pop.items = { "Continue", "Start from the beginning", "Cancel" };
	pop.sel = 0;

	const ui::Head setHead{ logo, "Game Patches", "Starfall Tactics \xC2\xB7 4D530064.01", {} };
	ui::ListScreenView mods;
	mods.head = setHead;
	{
		using R = ui::ListRow;
		R r;
		r.kind = R::Kind::Section; r.key = "s1"; r.title = "Patches"; mods.rows.push_back(r);
		r = {}; r.key = "p1"; r.title = "16:9 widescreen"; r.sub = "by Mobcat \xC2\xB7 from patches.db"; r.toggle = 1; mods.rows.push_back(r);
		r = {}; r.key = "p2"; r.title = "60 FPS"; r.sub = "by Jay"; r.toggle = 0; r.dim = true;
		r.right = "Incompatible"; mods.rows.push_back(r);
		r = {}; r.kind = R::Kind::Section; r.key = "s2"; r.title = "Cheats"; mods.rows.push_back(r);
		r = {}; r.key = "c1"; r.title = "Infinite health"; r.toggle = 0; mods.rows.push_back(r);
		r = {}; r.key = "c2"; r.title = "All levels open"; r.toggle = 1; r.right = "once"; mods.rows.push_back(r);
		r = {}; r.kind = R::Kind::Section; r.key = "s3"; r.title = "Trainers \xC2\xB7 one at a time"; mods.rows.push_back(r);
		r = {}; r.key = "t0"; r.title = "No trainer"; r.toggle = 0; mods.rows.push_back(r);
		r = {}; r.key = "t1"; r.title = "Starfall Tactics +6"; r.right = "6 options"; r.toggle = 1; mods.rows.push_back(r);
		r = {}; r.key = "o1"; r.title = "Infinite ammo"; r.toggle = 1; r.indent = 1; mods.rows.push_back(r);
		r = {}; r.key = "o2"; r.title = "Slow motion"; r.right = "in-game keys"; r.toggle = 0; r.indent = 1; mods.rows.push_back(r);
	}
	mods.sel = 2;
	mods.aside.title = "16:9 widescreen";
	mods.aside.text = "Draws the game in 16:9 with the right shape. By Mobcat, from patches.db. Applied at the game's next start; "
		"a patch whose bytes are not in this disc's XBE is left out (the boot log says so).";
	mods.aside.note = "/data/ogbox/mods.txt \xC2\xB7 default.xbe CRC-32 1A2B3C4D";
	mods.footer = { { "UD", "Choose" }, { "X", "On / off" }, { "O", "Back" } };

	ui::ListScreenView storage;
	storage.head = { logo, "Data folder", "where OGBox keeps the Xbox hard disk, caches, covers", {} };
	storage.intro = "Now: /data/ogbox (412 GB free). Choose another place and OGBox moves your Xbox hard disk, caches, covers and "
		"screenshots there.";
	{
		using R = ui::ListRow;
		R r;
		r.key = "a"; r.title = "This console"; r.sub = "/data/ogbox \xC2\xB7 412 GB free"; r.icon = ui::Icon::Console; r.current = true;
		storage.rows.push_back(r);
		r = {}; r.key = "b"; r.title = "USB drive 0"; r.sub = "/mnt/usb0/OGBox \xC2\xB7 1.8 TB free"; r.icon = ui::Icon::Usb; r.right = "recommended";
		r.accent = true; storage.rows.push_back(r);
		r = {}; r.key = "c"; r.title = "Another folder\xE2\x80\xA6"; r.sub = "internal storage, USB, extended storage"; r.icon = ui::Icon::Folder;
		storage.rows.push_back(r);
	}
	storage.sel = 1;
	storage.aside.title = "What goes there";
	storage.aside.facts = { { "hdd", "The Xbox's hard disk: saves, the dashboard if you add one" },
		{ "cache", "Shaders built for your games, patched XBEs" }, { "covers, shots", "The game list's covers, your screenshots" } };
	storage.aside.note = "A USB drive must stay plugged in while you play. exFAT is best; FAT32 can't hold files over 4 GB.";
	storage.footer = { { "X", "Choose" }, { "O", "Back" } };

	ui::ListScreenView hdd;
	hdd.head = { logo, "Xbox hard disk", "E: 1.2 GB used", {} };
	hdd.twoLists = true;
	{
		using R = ui::ListRow;
		R r;
		r.key = "i"; r.title = "Bring saves in\xE2\x80\xA6"; r.icon = ui::Icon::Load; hdd.rows.push_back(r);
		r = {}; r.key = "e"; r.title = "Copy every save out\xE2\x80\xA6"; r.icon = ui::Icon::Save; hdd.rows.push_back(r);
		r = {}; r.key = "c"; r.title = "Empty the cache drives"; r.right = "312 MB"; r.icon = ui::Icon::Trash; hdd.rows.push_back(r);
		r = {}; r.kind = R::Kind::Section; r.key = "g"; r.title = "Games"; hdd.rows.push_back(r);
		for (int i = 0; i < 4; i++) {
			r = {}; r.key = "t" + std::to_string(i); r.title = names[i]; r.sub = std::to_string(i + 1) + " saves \xC2\xB7 " + std::to_string(120 * (i + 1)) + " KB";
			r.thumb = covers[i]; r.thumbSlot = true; hdd.rows.push_back(r);
		}
	}
	hdd.sel = 6;
	hdd.title2 = "Saves of Starfall Tactics";
	{
		using R = ui::ListRow;
		R r;
		for (int i = 0; i < 3; i++) {
			r = {}; r.key = "s" + std::to_string(i); r.title = i == 0 ? "Chapter 3 \xE2\x80\x94 The Tide" : i == 1 ? "Chapter 2" : "Autosave";
			r.sub = std::to_string(i + 1) + " days ago \xC2\xB7 64 KB"; hdd.rows2.push_back(r);
		}
	}
	hdd.sel2 = 0;
	hdd.footer = { { "X", "Choose" }, { "T", "Copy, delete" }, { "O", "Back" } };

	ui::QuickView qv;
	qv.cover = covers[2];
	qv.title = names[2];
	qv.sub = "4D530064.01";
	{
		using R = ui::ListRow;
		R r;
		r.key = "resume"; r.title = "Resume"; r.icon = ui::Icon::Play; qv.rows.push_back(r);
		r = {}; r.key = "save"; r.title = "Save State"; r.right = "\xE2\x86\x90 slot 3 \xE2\x86\x92"; r.icon = ui::Icon::Save; qv.rows.push_back(r);
		r = {}; r.key = "load"; r.title = "Load State"; r.right = "\xE2\x86\x90 slot 3 \xE2\x86\x92"; r.icon = ui::Icon::Load; qv.rows.push_back(r);
		r = {}; r.key = "set"; r.title = "Game Settings"; r.right = "next start"; r.icon = ui::Icon::Gear; qv.rows.push_back(r);
		r = {}; r.key = "mods"; r.title = "Patches"; r.right = "next start"; r.icon = ui::Icon::Puzzle; qv.rows.push_back(r);
		r = {}; r.key = "log"; r.title = "Log What the GPU Draws"; r.right = "debugging only"; r.icon = ui::Icon::Info; qv.rows.push_back(r);
		r = {}; r.key = "restart"; r.title = "Restart the Game"; r.icon = ui::Icon::Restart; qv.rows.push_back(r);
		r = {}; r.key = "quit"; r.title = "Quit the Game"; r.icon = ui::Icon::Power; r.danger = true; qv.rows.push_back(r);
	}
	qv.sel = 1;
	qv.showSlot = true;
	qv.slot.picture = fakeCover(300, 320, 180);
	qv.slot.title = "Slot 3";
	qv.slot.text = "Saved yesterday, 21:14";
	qv.footer = { { "X", "Choose" }, { "LR", "Slot" }, { "O", "Resume" } };

	ui::NoticeView notice;
	notice.kicker = "Experimental build";
	notice.title = "Expect crashes and freezes";
	notice.subtitle = "OGBox 0.0.4 for PS5";
	notice.lead = "This is a highly experimental build of an original Xbox emulator.";
	notice.paragraphs = { "Many games don't start yet, and the ones that do can stop, slow down, or show missing graphics or sound "
		"problems at any point. Save in game often, and keep a copy of anything you care about.",
		"If something goes wrong, ogbox-boot.log in OGBox's title folder says what happened: please include it when you report a problem." };
	notice.fine = "OGBox comes with no games, BIOS or keys. Use it only with backups of games you own.";
	notice.canContinue = true;

	ui::HudView hud;
	hud.showFps = true;
	hud.fps = 59.8f;
	hud.showLed = true;
	hud.ledColour = IM_COL32(77, 255, 64, 255);
	hud.ledText = "green";
	hud.showStatus = true;
	hud.statusTitle = "OGBox 0.0.4";
	hud.statusLines = { { "Disc", "Starfall Tactics.iso" }, { "Core", "booting" } };
	hud.note = "Loading the kernel's patches for this release; the first start of a game builds its shaders and takes longer.";
	hud.waiting = true;
	hud.statusHints = { { "OPT", "Quick menu: save, load, quit" } };

	const std::vector<Frame> frames = {
		{ "home", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { games[2].tint, blurs[2], games[2].id });
			ui::drawHome(dl, S, home);
		} },
		{ "home-nocover", [&](ImDrawList *dl, ImVec2 S) {
			ui::HomeView v = home;
			v.gameSel = 1;
			v.title = names[1];
			v.kicker = "Play";
			v.primary = "Play";
			v.secondary.clear();
			v.chips = { { "Untested", 2 } };
			v.meta = "";
			ui::backdrop(dl, S, { games[1].tint, {}, games[1].id });
			ui::drawHome(dl, S, v);
		} },
		{ "home-strip", [&](ImDrawList *dl, ImVec2 S) {
			ui::HomeView v = home;
			v.focusRow = 1;
			v.cardSel = 1;
			ui::backdrop(dl, S, { games[2].tint, blurs[2], games[2].id });
			ui::drawHome(dl, S, v);
		} },
		{ "home-list", [&](ImDrawList *dl, ImVec2 S) {
			ui::HomeView v = home;
			v.list = true;
			for (auto &g : v.games) {
				g.sub = "USA";
				g.size = "4.2 GB";
			}
			ui::backdrop(dl, S, { games[2].tint, blurs[2], games[2].id });
			ui::drawHome(dl, S, v);
		} },
		{ "game", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { games[2].tint, blurs[2], games[2].id });
			ui::drawGamePage(dl, S, gp, 1.0f);
		} },
		{ "settings", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { 0, {}, "settings", { 0.2f, 0.3f }, 0.6f });
			ui::drawSettings(dl, S, set);
		} },
		{ "network", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { 0, {}, "settings", { 0.2f, 0.3f }, 0.6f });
			ui::drawSettings(dl, S, net);
		} },
		{ "controller", [&](ImDrawList *dl, ImVec2 S) {
			ui::SettingsView v = set;
			v.navSel = 1;
			v.nav.insert(v.nav.begin() + 1, ui::SetNav{ "Controller", "2 changed" });
			v.pageTitle = "Controller";
			v.rows.clear();
			ui::SetRow r;
			const char *names[][3] = { { "pad_lt", "Left trigger", "L2" }, { "pad_lthumb", "Left stick click", "L3" },
				{ "pad_up", "D-pad up", "D-pad up" }, { "pad_down", "D-pad down", "D-pad down" }, { "pad_left", "D-pad left", "D-pad left" },
				{ "pad_right", "D-pad right", "D-pad right" }, { "pad_back", "Back", "Touch pad" }, { "pad_rt", "Right trigger", "R2" },
				{ "pad_y", "Y", "Triangle" }, { "pad_x", "X", "Square" }, { "pad_b", "B", "Circle" }, { "pad_a", "A", "Square" },
				{ "pad_white", "White", "L1" }, { "pad_black", "Black", "R1" }, { "pad_start", "Start", "Touch pad" },
				{ "pad_rthumb", "Right stick click", "R3" } };
			for (const auto &n : names) {
				r = {}; r.key = n[0]; r.name = n[1]; r.choices = { n[2] }; r.choice = 0; r.hidden = true;
				r.changed = std::string(n[0]) == "pad_a";
				v.rows.push_back(r);
				v.pad.push_back({ n[0], n[1], n[2], r.changed });
			}
			r = {}; r.kind = ui::SetRow::Kind::Group; r.key = "gs"; r.name = "Sticks and triggers"; v.rows.push_back(r);
			r = {}; r.key = "sticks"; r.name = "Sticks"; r.choices = { "Normal", "Swapped" }; v.rows.push_back(r);
			r = {}; r.kind = ui::SetRow::Kind::Toggle; r.key = "inv"; r.name = "Invert left stick"; v.rows.push_back(r);
			r = {}; r.key = "dl"; r.name = "Left stick deadzone"; r.choices = { "15%" }; r.changed = true; v.rows.push_back(r);
			r = {}; r.kind = ui::SetRow::Kind::Group; r.key = "gr"; r.name = "Rumble"; v.rows.push_back(r);
			r = {}; r.key = "rumble"; r.name = "Rumble strength"; r.choices = { "80%" }; r.changed = true; v.rows.push_back(r);
			v.rowSel = 11;
			ui::backdrop(dl, S, { 0, {}, "settings", { 0.2f, 0.3f }, 0.6f });
			ui::drawSettings(dl, S, v);
		} },
		{ "keyboard", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { 0, {}, "settings", { 0.2f, 0.3f }, 0.6f });
			ui::drawSettings(dl, S, net);
			ui::drawForm(dl, S, form, 1.0f);
			if (!kb.isOpen()) {
				kb.open("Server", "192.168.1.1", "address or name", ui::Keyboard::Kind::Address, false, nullptr);
			}
			kb.draw(dl, S, true);
		} },
		{ "game-states", [&](ImDrawList *dl, ImVec2 S) {
			ui::GameView v = gp;
			v.stateSel = 1;
			v.about.clear();
			v.footer = { { "LR", "Choose" }, { "X", "Load" }, { "O", "Back" } };
			ui::backdrop(dl, S, { games[2].tint, blurs[2], games[2].id });
			ui::drawGamePage(dl, S, v, 1.0f);
		} },
		{ "mods", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { 0, {}, "mods", { 0.2f, 0.3f }, 0.6f });
			ui::drawListScreen(dl, S, mods);
		} },
		{ "storage", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { 0, {}, "storage", { 0.2f, 0.3f }, 0.6f });
			ui::drawListScreen(dl, S, storage);
		} },
		{ "storage-moving", [&](ImDrawList *dl, ImVec2 S) {
			ui::ListScreenView v = storage;
			v.intro = "Moving your data to /mnt/usb0/OGBox. Leave OGBox open until it's done.";
			v.rows.clear();
			v.progress = 0.42f;
			v.progressText = "1.4 GB of 3.3 GB \xC2\xB7 hdd/UDATA/4d530064/";
			v.footer = {};
			v.footerRight = "Moving\xE2\x80\xA6";
			ui::backdrop(dl, S, { 0, {}, "storage", { 0.2f, 0.3f }, 0.6f });
			ui::drawListScreen(dl, S, v);
		} },
		{ "hdd", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { 0, {}, "hdd", { 0.2f, 0.3f }, 0.6f });
			ui::drawListScreen(dl, S, hdd);
		} },
		{ "quick", [&](ImDrawList *dl, ImVec2 S) {
			ui::Tex shot = fakeCover(31, 640, 360);
			dl->AddImage(shot.id, ImVec2(0, 0), S);
			ui::drawHud(dl, S, ui::HudView{});
			ui::drawQuickMenu(dl, S, qv, 1.0f);
		} },
		{ "notice", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { 0, {}, "notice", { 0.5f, 0.3f }, 0.5f });
			ui::drawNotice(dl, S, notice);
		} },
		{ "boot", [&](ImDrawList *dl, ImVec2 S) {
			dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, 255));
			const float lh = S.y * 0.46f, lw = lh * logo.w / logo.h;
			const ImVec4 box((S.x - lw) * 0.5f, S.y * 0.12f, lw, lh);
			dl->AddImage(logo.id, ImVec2(box.x, box.y), ImVec2(box.x + box.z, box.y + box.w));
			ui::BootView b;
			b.logoBox = box;
			b.progress = 0.6f;
			b.left = "Starting the Xbox";
			b.right = "OGBox 0.0.4";
			b.lines = { "hakuX core: ready", "disc: Starfall Tactics.iso (XISO, 4.2 GB)", "kernel: 5838, EEPROM: NTSC-U", "video: 640 x 480, 3x" };
			b.elapsed = 1.6f;
			ui::drawBoot(dl, S, b);
		} },
		{ "hud", [&](ImDrawList *dl, ImVec2 S) {
			dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(12, 16, 22, 255));
			ui::drawHud(dl, S, hud);
		} },
		{ "popup", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { games[2].tint, blurs[2], games[2].id });
			ui::drawHome(dl, S, home);
			ui::drawPopup(dl, S, pop, 1.0f);
			ui::drawToast(dl, S, "Share Synology added: 212 games", 1.0f);
		} },
	};

	for (const Frame &f : frames) {
		// a second of frames, so the motion settles
		for (int i = 0; i < 90; i++) {
			io.DeltaTime = 1.0f / 60.0f;
			ImGui::NewFrame();
			ui::beginFrame(static_cast<double>(i) / 60.0 + 10.0);
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			ImGui::Begin("screen", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
			f.draw(ImGui::GetWindowDrawList(), io.DisplaySize);
			ImGui::End();
			ImGui::PopStyleVar();
			ImGui::Render();
			if (i == 89) {
				Canvas cv(W, H);
				render(cv, ImGui::GetDrawData());
				save(cv, out + "/" + f.name + ".ppm");
				std::printf("%s\n", f.name.c_str());
			}
		}
	}
	ImGui::DestroyContext();
	return 0;
}
