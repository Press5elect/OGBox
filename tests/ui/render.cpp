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
	home.games = games;
	home.cover = [&](size_t i) { return covers[i]; };
	home.blur = [&](size_t i) { return blurs[i]; };
	home.gameSel = 2;
	home.kicker = "Continue";
	home.title = names[2];
	home.chips = { { "Plays well", 1 }, { "NTSC", 0 }, { "Played 2 h ago", 0 } };
	home.meta = "Left off 2 hours ago, in the middle of chapter 3. Save states: 4.";
	home.primary = "Continue";
	home.secondary = "Start from the beginning";
	home.cardsTitle = "Folders";
	home.cardsSub = "/data/ogbox/games";
	home.cards = { { "Classics", "14 games", ui::Icon::Folder }, { "Racing", "6 games", ui::Icon::Folder },
		{ "Xbox dashboard", "the console's own menu", ui::Icon::Dash, false, true }, { "Up", "/data/ogbox", ui::Icon::Up },
		{ "Add a network share", "", ui::Icon::Plus, true } };
	home.footer = { { "X", "Play" }, { "T", "Game page" }, { "OPT", "Settings" }, { "L2 R2", "A-Z" } };
	home.version = "OGBox 0.0.3";

	ui::SettingsView set;
	set.logo = logo;
	set.title = "Settings";
	set.subtitle = "for every game";
	set.nav = { { "Picture and sound", "what you'd change" }, { "The Xbox", "as its dashboard sets it" },
		{ "Advanced", "can break games", 0, true }, { "Network", "Insignia connected", ui::P().ok, false, true },
		{ "Data folder", "/data/ogbox \xC2\xB7 412 GB free" }, { "Xbox hard disk", "38 saves" }, { "Updates", "0.0.3 \xC2\xB7 the newest" },
		{ "Interface", "Dark" }, { "About", "build 6d5366ca" } };
	set.navSel = 0;
	set.focusNav = false;
	set.pageTitle = "Picture and sound";
	set.pageAction = "";
	{
		ui::SetRow r;
		r.key = "render_scale"; r.name = "Resolution"; r.choices = { "1x", "2x", "3x", "4x" }; r.choice = 2;
		r.desc = "How sharp the picture is drawn: 3x is 1920 x 1440 for a 640 x 480 game. Higher costs speed in heavy scenes.";
		set.rows.push_back(r);
		r = {}; r.key = "aspect"; r.name = "Shape"; r.choices = { "As the game", "4:3", "16:9", "Stretch" }; r.choice = 0;
		set.rows.push_back(r);
		r = {}; r.key = "upscale"; r.name = "Scaling"; r.choices = { "Smooth", "Sharp" }; r.choice = 0; r.changed = true;
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
	net.pageTitle = "Network";
	net.pageAction = "";
	net.rows.clear();
	{
		ui::SetRow r;
		r.kind = ui::SetRow::Kind::Group; r.key = "g1"; r.name = "Online play"; net.rows.push_back(r);
		r = {}; r.key = "conn"; r.name = "Connection"; r.choices = { "Off", "LAN", "XLink Kai", "Insignia" }; r.choice = 3;
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
	for (int i = 0; i < 4; i++) {
		ui::GameState s;
		s.label = i == 0 ? "Where I left off" : "Slot " + std::to_string(i);
		s.picture = fakeCover(static_cast<uint32_t>(100 + i * 70), 320, 240);
		gp.states.push_back(s);
	}
	gp.states.push_back({ "Empty slot", {}, true });
	gp.actions = { { "Continue", "where I left off" }, { "Start from the beginning", "" }, { "Settings for this game", "3 changed" },
		{ "Remove from recently played", "" } };
	gp.footer = { { "X", "Choose" }, { "O", "Back" } };

	ui::PopupView pop;
	pop.title = "Where you left off";
	pop.text = "This game has a save state from when you last left it, 2 hours ago.";
	pop.items = { "Continue", "Start from the beginning", "Cancel" };
	pop.sel = 0;

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
		{ "home-cards", [&](ImDrawList *dl, ImVec2 S) {
			ui::HomeView v = home;
			v.focusRow = 1;
			v.cardSel = 2;
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
		{ "keyboard", [&](ImDrawList *dl, ImVec2 S) {
			ui::backdrop(dl, S, { 0, {}, "settings", { 0.2f, 0.3f }, 0.6f });
			ui::drawSettings(dl, S, net);
			ui::drawForm(dl, S, form, 1.0f);
			if (!kb.isOpen()) {
				kb.open("Server", "192.168.1.1", "address or name", ui::Keyboard::Kind::Address, false, nullptr);
			}
			kb.draw(dl, S, true);
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
