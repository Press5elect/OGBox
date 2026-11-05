/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.3: the menus' look (ogbox_ui.h).
 */
#include "ogbox_ui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <map>

#include "imgui_internal.h"

namespace ui {

namespace {

float g_k = 1.0f;
Theme g_theme = Theme::Dark;
Palette g_pal{};
ImFont *g_fonts[static_cast<int>(F::Count)]{};
float g_baked[static_cast<int>(F::Count)]{};
double g_now = 0.0, g_last = 0.0;
float g_dt = 1.0f / 60.0f;
std::map<std::string, float> g_values;
TexMaker g_maker;
Tex g_glow, g_grad;
bool g_glowTried = false;

constexpr ImU32 rgba(int r, int g, int b, int a = 255)
{
	return IM_COL32(r, g, b, a);
}

void makePalette()
{
	Palette &p = g_pal;
	p.bg = rgba(11, 13, 16);
	p.surface = rgba(18, 22, 27);
	p.surface2 = rgba(26, 32, 39);
	p.surfaceHi = rgba(255, 255, 255, 30);
	p.border = rgba(255, 255, 255, 26);
	p.text = rgba(255, 255, 255);
	p.textDim = rgba(201, 207, 214);
	p.textFaint = rgba(139, 149, 161);
	p.accent = rgba(61, 220, 132);
	p.accentInk = rgba(6, 16, 10);
	p.accentSoft = rgba(61, 220, 132, 40);
	p.accentText = rgba(127, 240, 174);
	p.warn = rgba(224, 162, 74);
	p.warnSoft = rgba(224, 162, 74, 30);
	p.warnText = rgba(232, 195, 137);
	p.fail = rgba(255, 120, 106);
	p.ok = rgba(61, 220, 132);
	if (g_theme == Theme::Code) {
		// the Code theme: black, and the falling code's green as the accent
		p.bg = rgba(0, 0, 0);
		p.surface = rgba(4, 14, 7);
		p.surface2 = rgba(6, 22, 10);
		p.accent = rgba(0, 255, 65);
		p.accentSoft = rgba(0, 255, 65, 40);
		p.accentText = rgba(125, 255, 156);
		p.textDim = rgba(190, 230, 200);
		p.textFaint = rgba(110, 170, 125);
	}
}

struct PaletteInit {
	PaletteInit() { makePalette(); }
} g_paletteInit;

uint32_t hashOf(const std::string &s)
{
	uint32_t h = 2166136261u;
	for (const unsigned char c : s) {
		h = (h ^ c) * 16777619u;
	}
	return h;
}

// UTF-8: the byte after the character at s
const char *nextChar(const char *s, const char *end)
{
	if (s >= end) {
		return end;
	}
	unsigned c = static_cast<unsigned char>(*s);
	int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
	return std::min(s + n, end);
}

void ensureTextures()
{
	if (g_glowTried || !g_maker) {
		return;
	}
	g_glowTried = true;
	// the light: 64 x 64, alpha falling off as a smooth bell
	{
		const int n = 64;
		std::vector<uint8_t> px(static_cast<size_t>(n * n * 4));
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				const float dx = (static_cast<float>(x) + 0.5f) / n * 2.0f - 1.0f;
				const float dy = (static_cast<float>(y) + 0.5f) / n * 2.0f - 1.0f;
				const float d = std::min(1.0f, std::sqrt(dx * dx + dy * dy));
				const float a = std::pow(1.0f - d * d * (3.0f - 2.0f * d), 1.6f);
				uint8_t *p = &px[static_cast<size_t>((y * n + x) * 4)];
				p[0] = p[1] = p[2] = 255;
				p[3] = static_cast<uint8_t>(std::lround(a * 255.0f));
			}
		}
		g_glow = g_maker(px.data(), n, n);
	}
	// a diagonal fall from white to a third (a placeholder's face, drawn tinted)
	{
		const int n = 32;
		std::vector<uint8_t> px(static_cast<size_t>(n * n * 4));
		for (int y = 0; y < n; y++) {
			for (int x = 0; x < n; x++) {
				const float t = (static_cast<float>(x) + static_cast<float>(y)) / (2.0f * (n - 1));
				const uint8_t v = static_cast<uint8_t>(std::lround((1.0f - 0.68f * t) * 255.0f));
				uint8_t *p = &px[static_cast<size_t>((y * n + x) * 4)];
				p[0] = p[1] = p[2] = v;
				p[3] = 255;
			}
		}
		g_grad = g_maker(px.data(), n, n);
	}
}

/* A rounded rectangle's outline drawn inside out by off (a focus ring). */
void ring(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float r, float off, ImU32 col, float t)
{
	dl->AddRect(ImVec2(p0.x - off, p0.y - off), ImVec2(p1.x + off, p1.y + off), col, r + off, ImDrawCornerFlags_All, t);
}

/* An image drawn to fill a box, cut to keep its shape, its corners rounded. */
void imageFill(ImDrawList *dl, const Tex &t, ImVec2 p0, ImVec2 p1, float r, ImU32 col = IM_COL32_WHITE)
{
	const float bw = p1.x - p0.x, bh = p1.y - p0.y;
	if (bw <= 0.0f || bh <= 0.0f) {
		return;
	}
	const float ia = t.w / t.h, ba = bw / bh;
	ImVec2 uv0(0, 0), uv1(1, 1);
	if (ia > ba) {
		const float keep = ba / ia;
		uv0.x = (1.0f - keep) * 0.5f;
		uv1.x = 1.0f - uv0.x;
	} else {
		const float keep = ia / ba;
		uv0.y = (1.0f - keep) * 0.5f;
		uv1.y = 1.0f - uv0.y;
	}
	dl->AddImageRounded(t.id, p0, p1, uv0, uv1, col, r);
}

} // namespace

// ---- scale, palette, type -------------------------------------------------------------------

void setScale(float k)
{
	g_k = k > 0.1f ? k : 1.0f;
}

float K()
{
	return g_k;
}

void setTheme(Theme t)
{
	g_theme = t;
	makePalette();
}

Theme theme()
{
	return g_theme;
}

const Palette &P()
{
	return g_pal;
}

ImU32 alpha(ImU32 c, float a)
{
	const float ca = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF) * std::max(0.0f, std::min(1.0f, a));
	return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::lround(ca)) << IM_COL32_A_SHIFT);
}

ImU32 mix(ImU32 a, ImU32 b, float t)
{
	t = std::max(0.0f, std::min(1.0f, t));
	ImU32 out = 0;
	for (int s = 0; s < 32; s += 8) {
		const float x = static_cast<float>((a >> s) & 0xFF), y = static_cast<float>((b >> s) & 0xFF);
		out |= static_cast<ImU32>(std::lround(x + (y - x) * t)) << s;
	}
	return out;
}

bool loadFonts(ImFontAtlas *atlas, const std::string &dir)
{
	static const ImWchar ranges[] = {
		0x0020, 0x00FF,   // Latin and Latin-1 (accents, the middle dot, the multiplication sign)
		0x0152, 0x0153,   // OE oe
		0x2013, 0x2014,   // dashes
		0x2018, 0x201E,   // quotes
		0x2022, 0x2022,   // bullet
		0x2026, 0x2026,   // ellipsis
		0x2190, 0x2193,   // arrows
		0,
	};
	struct Face {
		F f;
		const char *file;
		float size;   // at 1080 lines
		int over;
	};
	const Face faces[] = {
		{ F::Display, "Manrope-ExtraBold.ttf", 60.0f, 1 },
		{ F::Title, "Manrope-Bold.ttf", 32.0f, 1 },
		{ F::Heading, "Manrope-Bold.ttf", 21.0f, 2 },
		{ F::Body, "Inter-Medium.ttf", 20.0f, 2 },
		{ F::Small, "Inter-Medium.ttf", 15.5f, 2 },
		{ F::Label, "Inter-SemiBold.ttf", 14.0f, 2 },
	};
	bool ok = true;
	for (const Face &face : faces) {
		ImFontConfig cfg;
		cfg.OversampleH = face.over;
		cfg.OversampleV = 1;
		cfg.PixelSnapH = false;
		const float size = std::round(face.size * g_k);
		const std::string path = dir + "/" + face.file;
		ImFont *f = atlas->AddFontFromFileTTF(path.c_str(), size, &cfg, ranges);
		g_fonts[static_cast<int>(face.f)] = f;
		g_baked[static_cast<int>(face.f)] = size;
		ok = ok && f != nullptr;
	}
	return ok;
}

bool fontsLoaded()
{
	return g_fonts[0] != nullptr;
}

ImFont *font(F f)
{
	ImFont *p = g_fonts[static_cast<int>(f)];
	return p ? p : ImGui::GetFont();
}

ImVec2 measure(F f, float size, const char *s, const char *end, float wrap)
{
	return font(f)->CalcTextSizeA(size, FLT_MAX, wrap, s, end);
}

void text(ImDrawList *dl, F f, float size, ImVec2 p, ImU32 col, const char *s, const char *end, float wrap)
{
	dl->AddText(font(f), size, ImVec2(std::round(p.x), std::round(p.y)), col, s, end, wrap);
}

float textFit(ImDrawList *dl, F f, float size, ImVec2 p, ImU32 col, const std::string &s, float maxW)
{
	const char *b = s.c_str(), *e = b + s.size();
	const float full = measure(f, size, b, e).x;
	if (full <= maxW) {
		if (dl) {
			text(dl, f, size, p, col, b, e);
		}
		return full;
	}
	const char *dots = "\xE2\x80\xA6";
	const float dw = measure(f, size, dots).x;
	const char *cut = b;
	float w = 0.0f;
	while (cut < e) {
		const char *n = nextChar(cut, e);
		const float cw = measure(f, size, cut, n).x;
		if (w + cw + dw > maxW) {
			break;
		}
		w += cw;
		cut = n;
	}
	while (cut > b && cut[-1] == ' ') {
		cut--;
	}
	const std::string shown = std::string(b, cut) + dots;
	if (dl) {
		text(dl, f, size, p, col, shown.c_str());
	}
	return measure(f, size, shown.c_str()).x;
}

float label(ImDrawList *dl, float size, ImVec2 p, ImU32 col, const std::string &s, bool measureOnly)
{
	const float track = size * 0.14f;
	float x = p.x;
	const char *b = s.c_str(), *e = b + s.size();
	for (const char *c = b; c < e;) {
		const char *n = nextChar(c, e);
		char up[8]{};
		const size_t len = static_cast<size_t>(n - c);
		std::memcpy(up, c, std::min<size_t>(len, 7));
		if (len == 1 && up[0] >= 'a' && up[0] <= 'z') {
			up[0] = static_cast<char>(up[0] - 32);
		}
		if (dl && !measureOnly) {
			text(dl, F::Label, size, ImVec2(x, p.y), col, up);
		}
		x += measure(F::Label, size, up).x + track;
		c = n;
	}
	return x - p.x - (s.empty() ? 0.0f : track);
}

float paragraph(ImDrawList *dl, F f, float size, ImVec2 p, ImU32 col, const std::string &s, float w, int maxLines,
	float lineGap)
{
	ImFont *fnt = font(f);
	const float scale = size / fnt->FontSize;
	const float lh = size * lineGap;
	const char *b = s.c_str(), *e = b + s.size();
	int line = 0;
	float y = p.y;
	while (b < e && line < maxLines) {
		const char *stop = fnt->CalcWordWrapPositionA(scale, b, e, w);
		if (stop == b) {
			stop = nextChar(b, e);
		}
		const char *nl = static_cast<const char *>(std::memchr(b, '\n', static_cast<size_t>(stop - b)));
		if (nl) {
			stop = nl;
		}
		const bool last = line == maxLines - 1;
		const char *rest = stop;
		while (rest < e && (*rest == ' ' || *rest == '\n')) {
			rest++;
		}
		if (dl) {
			if (last && rest < e) {
				textFit(dl, f, size, ImVec2(p.x, y), col, std::string(b, e), w);
			} else {
				text(dl, f, size, ImVec2(p.x, y), col, b, stop);
			}
		}
		line++;
		y += lh;
		b = rest;
	}
	return y - p.y - (line ? lh - size * 1.15f : 0.0f);
}

// ---- time and motion --------------------------------------------------------------------------

void beginFrame(double seconds)
{
	g_dt = g_last > 0.0 ? static_cast<float>(std::min(0.1, std::max(0.0, seconds - g_last))) : 1.0f / 60.0f;
	g_last = seconds;
	g_now = seconds;
	ensureTextures();
}

double now()
{
	return g_now;
}

float dt()
{
	return g_dt;
}

float approach(float current, float target, float rate)
{
	const float t = 1.0f - std::exp(-rate * g_dt);
	const float v = current + (target - current) * t;
	return std::fabs(target - v) < 0.0005f ? target : v;
}

float &value(const std::string &key, float init)
{
	auto it = g_values.find(key);
	if (it == g_values.end()) {
		it = g_values.emplace(key, init).first;
	}
	return it->second;
}

float easeOut(float t)
{
	t = std::max(0.0f, std::min(1.0f, t));
	const float u = 1.0f - t;
	return 1.0f - u * u * u;
}

float smooth(float t)
{
	t = std::max(0.0f, std::min(1.0f, t));
	return t * t * (3.0f - 2.0f * t);
}

// ---- images -----------------------------------------------------------------------------------

void setTexMaker(TexMaker maker)
{
	g_maker = std::move(maker);
	g_glowTried = false;
}

Tex glow()
{
	ensureTextures();
	return g_glow;
}

// ---- pieces -----------------------------------------------------------------------------------

float keycap(ImDrawList *dl, ImVec2 p, float h, const std::string &name, ImU32 col, ImU32 fill, bool measureOnly)
{
	const float r = h * 0.27f;
	const bool face = name == "X" || name == "O" || name == "S" || name == "T";
	std::string word = name;
	if (name == "OPT") {
		word = "OPTIONS";
	} else if (name == "TP") {
		word = "TOUCH PAD";
	} else if (name == "LR") {
		word = "\xE2\x86\x90 \xE2\x86\x92";
	} else if (name == "UD") {
		word = "\xE2\x86\x91 \xE2\x86\x93";
	}
	const float fs = h * 0.44f;
	const float w = face ? h : std::max(h, measure(F::Label, fs, word.c_str()).x + h * 0.56f);
	if (measureOnly || !dl) {
		return w;
	}
	const ImVec2 p1(p.x + w, p.y + h);
	if (fill) {
		dl->AddRectFilled(p, p1, fill, r);
	}
	dl->AddRect(p, p1, alpha(col, 0.42f), r, ImDrawCornerFlags_All, std::max(1.0f, h * 0.05f));
	if (face) {
		const ImVec2 c(p.x + h * 0.5f, p.y + h * 0.5f);
		const float k = h * 0.21f, t = std::max(1.5f, h * 0.075f);
		if (name == "X") {
			dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col, t);
			dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), col, t);
		} else if (name == "O") {
			dl->AddCircle(c, k * 1.12f, col, 24, t);
		} else if (name == "S") {
			dl->AddRect(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col, 0.0f, ImDrawCornerFlags_All, t);
		} else {
			dl->AddTriangle(ImVec2(c.x, c.y - k * 1.15f), ImVec2(c.x + k * 1.2f, c.y + k * 0.9f),
				ImVec2(c.x - k * 1.2f, c.y + k * 0.9f), col, t);
		}
		return w;
	}
	const ImVec2 ts = measure(F::Label, fs, word.c_str());
	text(dl, F::Label, fs, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), col, word.c_str());
	return w;
}

float hint(ImDrawList *dl, ImVec2 p, const Hint &h, ImU32 col, bool measureOnly)
{
	const float kh = px(30.0f), fs = px(17.0f);
	float x = p.x;
	std::string keys = h.key;
	size_t at = 0;
	while (at < keys.size()) {
		size_t sp = keys.find(' ', at);
		if (sp == std::string::npos) {
			sp = keys.size();
		}
		const std::string k = keys.substr(at, sp - at);
		x += keycap(measureOnly ? nullptr : dl, ImVec2(x, p.y), kh, k, P().textDim, 0, measureOnly) + px(5.0f);
		at = sp + 1;
	}
	x += px(5.0f);
	const ImVec2 ts = measure(F::Small, fs, h.text.c_str());
	if (!measureOnly && dl) {
		text(dl, F::Small, fs, ImVec2(x, p.y + (kh - ts.y) * 0.5f), col, h.text.c_str());
	}
	return x + ts.x - p.x;
}

void footer(ImDrawList *dl, ImVec2 screen, const std::vector<Hint> &hints, const std::string &right)
{
	const float y = screen.y - px(44.0f) - px(30.0f);
	float x = px(96.0f);
	for (const Hint &h : hints) {
		x += hint(dl, ImVec2(x, y), h, P().textFaint) + px(34.0f);
	}
	if (!right.empty()) {
		const float fs = px(17.0f);
		const ImVec2 ts = measure(F::Small, fs, right.c_str());
		text(dl, F::Small, fs, ImVec2(screen.x - px(96.0f) - ts.x, y + (px(30.0f) - ts.y) * 0.5f), P().textFaint, right.c_str());
	}
}

void icon(ImDrawList *dl, Icon i, ImVec2 c, float s, ImU32 col)
{
	const float t = std::max(1.5f, s * 0.075f);
	const float h = s * 0.5f;
	switch (i) {
	case Icon::None:
		break;
	case Icon::Folder:
		dl->PathLineTo(ImVec2(c.x - h * 0.85f, c.y - h * 0.55f));
		dl->PathLineTo(ImVec2(c.x - h * 0.25f, c.y - h * 0.55f));
		dl->PathLineTo(ImVec2(c.x - h * 0.08f, c.y - h * 0.38f));
		dl->PathLineTo(ImVec2(c.x + h * 0.85f, c.y - h * 0.38f));
		dl->PathLineTo(ImVec2(c.x + h * 0.85f, c.y + h * 0.62f));
		dl->PathLineTo(ImVec2(c.x - h * 0.85f, c.y + h * 0.62f));
		dl->PathStroke(col, true, t);
		break;
	case Icon::Disc:
		dl->AddCircle(c, h * 0.85f, col, 32, t);
		dl->AddCircle(c, h * 0.22f, col, 16, t);
		break;
	case Icon::Up:
		dl->AddLine(ImVec2(c.x, c.y + h * 0.7f), ImVec2(c.x, c.y - h * 0.6f), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.5f, c.y - h * 0.1f), ImVec2(c.x, c.y - h * 0.62f), col, t);
		dl->AddLine(ImVec2(c.x + h * 0.5f, c.y - h * 0.1f), ImVec2(c.x, c.y - h * 0.62f), col, t);
		break;
	case Icon::Console:
		dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.65f), ImVec2(c.x + h * 0.85f, c.y + h * 0.65f), col, h * 0.22f,
			ImDrawCornerFlags_All, t);
		dl->AddLine(ImVec2(c.x - h * 0.85f, c.y - h * 0.15f), ImVec2(c.x + h * 0.85f, c.y - h * 0.15f), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.35f, c.y + h * 0.25f), ImVec2(c.x + h * 0.35f, c.y + h * 0.25f), col, t);
		break;
	case Icon::Usb:
		dl->AddRect(ImVec2(c.x - h * 0.4f, c.y - h * 0.2f), ImVec2(c.x + h * 0.4f, c.y + h * 0.85f), col, h * 0.12f,
			ImDrawCornerFlags_All, t);
		dl->AddRect(ImVec2(c.x - h * 0.26f, c.y - h * 0.8f), ImVec2(c.x + h * 0.26f, c.y - h * 0.2f), col, 0.0f,
			ImDrawCornerFlags_All, t);
		break;
	case Icon::Network:
		dl->AddCircle(c, h * 0.85f, col, 32, t);
		dl->AddLine(ImVec2(c.x - h * 0.85f, c.y), ImVec2(c.x + h * 0.85f, c.y), col, t);
		dl->PathArcTo(ImVec2(c.x + h * 0.95f, c.y), h * 1.15f, 2.5f, 3.78f, 16);
		dl->PathStroke(col, false, t);
		dl->PathArcTo(ImVec2(c.x - h * 0.95f, c.y), h * 1.15f, -0.64f, 0.64f, 16);
		dl->PathStroke(col, false, t);
		break;
	case Icon::Server:
		dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.75f), ImVec2(c.x + h * 0.85f, c.y - h * 0.08f), col, h * 0.12f,
			ImDrawCornerFlags_All, t);
		dl->AddRect(ImVec2(c.x - h * 0.85f, c.y + h * 0.08f), ImVec2(c.x + h * 0.85f, c.y + h * 0.75f), col, h * 0.12f,
			ImDrawCornerFlags_All, t);
		dl->AddCircleFilled(ImVec2(c.x - h * 0.5f, c.y - h * 0.41f), t * 0.9f, col, 8);
		dl->AddCircleFilled(ImVec2(c.x - h * 0.5f, c.y + h * 0.41f), t * 0.9f, col, 8);
		break;
	case Icon::Gear: {
		dl->AddCircle(c, h * 0.32f, col, 16, t);
		for (int k = 0; k < 8; k++) {
			const float a = static_cast<float>(k) * 0.785398f;
			dl->AddLine(ImVec2(c.x + std::cos(a) * h * 0.55f, c.y + std::sin(a) * h * 0.55f),
				ImVec2(c.x + std::cos(a) * h * 0.85f, c.y + std::sin(a) * h * 0.85f), col, t * 1.6f);
		}
		dl->AddCircle(c, h * 0.62f, col, 24, t);
		break;
	}
	case Icon::Power:
		dl->PathArcTo(c, h * 0.75f, -1.1f + 3.14159f * 0.5f - 3.14159f, 3.14159f * 1.5f - 0.45f - 3.14159f * 0.5f + 3.14159f * 0.5f, 24);
		dl->PathStroke(col, false, t);
		dl->AddLine(ImVec2(c.x, c.y - h * 0.9f), ImVec2(c.x, c.y - h * 0.1f), col, t);
		break;
	case Icon::Dash:
		dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.7f), ImVec2(c.x + h * 0.85f, c.y + h * 0.7f), col, h * 0.2f,
			ImDrawCornerFlags_All, t);
		dl->AddLine(ImVec2(c.x - h * 0.85f, c.y - h * 0.2f), ImVec2(c.x + h * 0.85f, c.y - h * 0.2f), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.4f, c.y + h * 0.25f), ImVec2(c.x + h * 0.4f, c.y + h * 0.25f), col, t);
		break;
	case Icon::Check:
		dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x - h * 0.15f, c.y + h * 0.55f), col, t * 1.3f);
		dl->AddLine(ImVec2(c.x - h * 0.15f, c.y + h * 0.55f), ImVec2(c.x + h * 0.75f, c.y - h * 0.5f), col, t * 1.3f);
		break;
	case Icon::Plus:
		dl->AddLine(ImVec2(c.x - h * 0.7f, c.y), ImVec2(c.x + h * 0.7f, c.y), col, t * 1.2f);
		dl->AddLine(ImVec2(c.x, c.y - h * 0.7f), ImVec2(c.x, c.y + h * 0.7f), col, t * 1.2f);
		break;
	case Icon::Lock:
		dl->AddRect(ImVec2(c.x - h * 0.6f, c.y - h * 0.1f), ImVec2(c.x + h * 0.6f, c.y + h * 0.75f), col, h * 0.12f,
			ImDrawCornerFlags_All, t);
		dl->PathArcTo(ImVec2(c.x, c.y - h * 0.1f), h * 0.38f, 3.14159f, 6.28318f, 16);
		dl->PathStroke(col, false, t);
		break;
	case Icon::Warn:
		dl->AddTriangle(ImVec2(c.x, c.y - h * 0.8f), ImVec2(c.x + h * 0.85f, c.y + h * 0.7f), ImVec2(c.x - h * 0.85f, c.y + h * 0.7f),
			col, t);
		dl->AddLine(ImVec2(c.x, c.y - h * 0.25f), ImVec2(c.x, c.y + h * 0.2f), col, t);
		dl->AddCircleFilled(ImVec2(c.x, c.y + h * 0.45f), t * 0.8f, col, 8);
		break;
	case Icon::Pad:
		dl->AddRect(ImVec2(c.x - h * 0.9f, c.y - h * 0.45f), ImVec2(c.x + h * 0.9f, c.y + h * 0.5f), col, h * 0.4f,
			ImDrawCornerFlags_All, t);
		dl->AddLine(ImVec2(c.x - h * 0.55f, c.y), ImVec2(c.x - h * 0.25f, c.y), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.4f, c.y - h * 0.15f), ImVec2(c.x - h * 0.4f, c.y + h * 0.15f), col, t);
		dl->AddCircleFilled(ImVec2(c.x + h * 0.4f, c.y - h * 0.08f), t, col, 8);
		dl->AddCircleFilled(ImVec2(c.x + h * 0.55f, c.y + h * 0.1f), t, col, 8);
		break;
	case Icon::Keyboard:
		dl->AddRect(ImVec2(c.x - h * 0.9f, c.y - h * 0.5f), ImVec2(c.x + h * 0.9f, c.y + h * 0.5f), col, h * 0.15f,
			ImDrawCornerFlags_All, t);
		for (int k = 0; k < 4; k++) {
			dl->AddCircleFilled(ImVec2(c.x - h * 0.55f + h * 0.37f * k, c.y - h * 0.15f), t * 0.8f, col, 6);
		}
		dl->AddLine(ImVec2(c.x - h * 0.45f, c.y + h * 0.22f), ImVec2(c.x + h * 0.45f, c.y + h * 0.22f), col, t);
		break;
	}
}

float chip(ImDrawList *dl, ImVec2 p, const std::string &s, ImU32 fill, ImU32 col, bool measureOnly)
{
	const float h = px(34.0f), fs = px(15.0f);
	const ImVec2 ts = measure(F::Small, fs, s.c_str());
	const float w = ts.x + px(28.0f);
	if (!measureOnly && dl) {
		dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), fill, h * 0.5f);
		text(dl, F::Small, fs, ImVec2(p.x + px(14.0f), p.y + (h - ts.y) * 0.5f), col, s.c_str());
	}
	return w;
}

float segmented(ImDrawList *dl, ImVec2 p, const std::vector<std::string> &items, int on, bool focused, bool measureOnly)
{
	const float h = px(40.0f), fs = px(15.5f), pad = px(16.0f), in = px(3.0f);
	std::vector<float> ws;
	float w = in * 2.0f;
	for (const std::string &s : items) {
		ws.push_back(measure(F::Small, fs, s.c_str()).x + pad * 2.0f);
		w += ws.back() + px(2.0f);
	}
	w -= px(2.0f);
	if (measureOnly || !dl) {
		return w;
	}
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, focused ? 26 : 18), h * 0.5f);
	float x = p.x + in;
	for (size_t i = 0; i < items.size(); i++) {
		const bool sel = static_cast<int>(i) == on;
		if (sel) {
			dl->AddRectFilled(ImVec2(x, p.y + in), ImVec2(x + ws[i], p.y + h - in), focused ? P().accent : alpha(P().accent, 0.8f),
				(h - in * 2.0f) * 0.5f);
		}
		const ImVec2 ts = measure(F::Small, fs, items[i].c_str());
		text(dl, F::Small, fs, ImVec2(x + (ws[i] - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f),
			sel ? P().accentInk : (focused ? P().textDim : P().textFaint), items[i].c_str());
		x += ws[i] + px(2.0f);
	}
	return w;
}

void toggle(ImDrawList *dl, ImVec2 p, float a, bool focused)
{
	const float w = px(56.0f), h = px(32.0f), k = px(26.0f), in = px(3.0f);
	const ImU32 off = IM_COL32(255, 255, 255, focused ? 44 : 34);
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), mix(off, P().accent, a), h * 0.5f);
	const float x = p.x + in + (w - in * 2.0f - k) * a;
	dl->AddCircleFilled(ImVec2(x + k * 0.5f, p.y + h * 0.5f), k * 0.5f, mix(P().textDim, P().accentInk, a), 24);
}

void field(ImDrawList *dl, ImVec2 p0, ImVec2 p1, const std::string &value, const std::string &placeholder, bool focused,
	bool password, int cursor)
{
	const float r = px(9.0f), fs = px(17.0f);
	dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 18), r);
	dl->AddRect(p0, p1, focused ? P().accent : IM_COL32(255, 255, 255, 36), r, ImDrawCornerFlags_All, focused ? px(2.0f) : px(1.0f));
	const float h = p1.y - p0.y;
	std::string shown = value;
	if (password) {
		shown.clear();
		for (size_t i = 0; i < value.size(); i++) {
			shown += "\xE2\x80\xA2";
		}
	}
	const float ty = p0.y + (h - measure(F::Small, fs, "Ag").y) * 0.5f;
	const float maxW = p1.x - p0.x - px(28.0f);
	dl->PushClipRect(p0, p1, true);
	if (shown.empty()) {
		textFit(dl, F::Small, fs, ImVec2(p0.x + px(14.0f), ty), P().textFaint, placeholder, maxW);
	} else {
		// the end shown when it is longer than the box
		float tw = measure(F::Small, fs, shown.c_str()).x;
		const float shift = std::max(0.0f, tw - maxW);
		text(dl, F::Small, fs, ImVec2(p0.x + px(14.0f) - shift, ty), P().text, shown.c_str());
		if (cursor >= 0) {
			std::string before = password ? std::string() : value.substr(0, std::min<size_t>(static_cast<size_t>(cursor), value.size()));
			if (password) {
				for (int i = 0; i < cursor; i++) {
					before += "\xE2\x80\xA2";
				}
			}
			const float cx = p0.x + px(14.0f) - shift + measure(F::Small, fs, before.c_str()).x;
			if (std::fmod(now(), 1.0) < 0.6) {
				dl->AddRectFilled(ImVec2(cx + px(1.0f), p0.y + h * 0.22f), ImVec2(cx + px(3.0f), p1.y - h * 0.22f), P().accent);
			}
		}
	}
	if (shown.empty() && cursor >= 0 && std::fmod(now(), 1.0) < 0.6) {
		dl->AddRectFilled(ImVec2(p0.x + px(14.0f), p0.y + h * 0.22f), ImVec2(p0.x + px(16.0f), p1.y - h * 0.22f), P().accent);
	}
	dl->PopClipRect();
}

void panel(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float r, ImU32 fill, ImU32 border)
{
	dl->AddRectFilled(p0, p1, fill, r);
	if (border) {
		dl->AddRect(p0, p1, border, r, ImDrawCornerFlags_All, std::max(1.0f, px(1.0f)));
	}
}

void shadow(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float r, float spread, float strength)
{
	const Tex g = glow();
	if (g) {
		const float s = spread * 1.6f;
		dl->AddImage(g.id, ImVec2(p0.x - s, p0.y - s * 0.6f), ImVec2(p1.x + s, p1.y + s * 1.4f), ImVec2(0, 0), ImVec2(1, 1),
			IM_COL32(0, 0, 0, static_cast<int>(255 * strength)));
		return;
	}
	for (int i = 3; i >= 1; i--) {
		const float o = spread * static_cast<float>(i) / 3.0f;
		dl->AddRectFilled(ImVec2(p0.x - o, p0.y - o * 0.5f), ImVec2(p1.x + o, p1.y + o * 1.5f), IM_COL32(0, 0, 0, static_cast<int>(40 * strength)),
			r + o);
	}
}

void spinner(ImDrawList *dl, ImVec2 c, float r, ImU32 col)
{
	const float a = static_cast<float>(std::fmod(now() * 5.0, 6.28318));
	dl->PathArcTo(c, r, a, a + 4.2f, 28);
	dl->PathStroke(col, false, std::max(2.0f, r * 0.16f));
}

// ---- the backdrop -----------------------------------------------------------------------------

void backdrop(ImDrawList *dl, ImVec2 size, const Backdrop &b)
{
	static std::string curKey;
	static Tex cur, prev;
	static ImU32 curTint = 0, prevTint = 0;
	static float fade = 1.0f;
	if (b.key != curKey) {
		prev = cur;
		prevTint = curTint;
		cur = b.blur;
		curTint = b.tint;
		curKey = b.key;
		fade = 0.0f;
	} else if (!cur && b.blur) {
		cur = b.blur;   // (its image came after it was chosen)
	}
	curTint = b.tint ? b.tint : curTint;
	fade = approach(fade, 1.0f, 5.0f);
	dl->AddRectFilled(ImVec2(0, 0), size, P().bg);
	const float t = static_cast<float>(now());
	const float drift = std::sin(t * 0.21f) * 0.035f, drift2 = std::cos(t * 0.17f) * 0.03f;
	// the cover, blurred, over a bigger box than the screen (its edges never show)
	const auto cover = [&](const Tex &tex, float a) {
		if (!tex || a <= 0.001f) {
			return;
		}
		const float over = 0.12f;
		const ImVec2 p0(-size.x * over + size.x * drift, -size.y * over + size.y * drift2);
		const ImVec2 p1(size.x * (1.0f + over) + size.x * drift, size.y * (1.0f + over) + size.y * drift2);
		const float hx = 0.5f / tex.w, hy = 0.5f / tex.h;
		dl->AddImage(tex.id, p0, p1, ImVec2(hx, hy), ImVec2(1.0f - hx, 1.0f - hy), IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
	};
	const float strength = 0.42f * b.strength;
	cover(prev, (1.0f - fade) * strength);
	cover(cur, fade * strength);
	// the light: the game's colour, drifting slowly
	const Tex g = glow();
	const auto light = [&](ImU32 tint, float a) {
		if (!g || a <= 0.001f) {
			return;
		}
		const ImU32 c = tint ? tint : P().accent;
		const float cx = size.x * (b.focus.x + drift * 1.4f), cy = size.y * (b.focus.y + drift2 * 1.4f);
		const float rx = size.x * 0.62f, ry = size.y * 0.78f;
		dl->AddImage(g.id, ImVec2(cx - rx, cy - ry), ImVec2(cx + rx, cy + ry), ImVec2(0, 0), ImVec2(1, 1),
			alpha(c, a * (tint ? 0.75f : 0.22f)));
	};
	light(prevTint, (1.0f - fade) * b.strength);
	light(curTint, fade * b.strength);
	// darker at the top and the foot, so the words over it read
	const ImU32 bg = P().bg;
	dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, size.y * 0.3f), alpha(bg, 0.55f), alpha(bg, 0.55f), alpha(bg, 0.0f),
		alpha(bg, 0.0f));
	dl->AddRectFilledMultiColor(ImVec2(0, size.y * 0.55f), ImVec2(size.x, size.y), alpha(bg, 0.0f), alpha(bg, 0.0f), alpha(bg, 0.82f),
		alpha(bg, 0.82f));
}

// ---- the game list ----------------------------------------------------------------------------

namespace {

/* A game's face: its cover, or a tile of its colour with its name. */
void drawCover(ImDrawList *dl, const HomeGame &g, const Tex &cover, ImVec2 p0, ImVec2 p1, float focus, float a, bool big)
{
	const float r = px(10.0f);
	const float w = p1.x - p0.x;
	shadow(dl, p0, p1, r, px(26.0f + 24.0f * focus), 0.45f * a + 0.25f * focus * a);
	if (cover) {
		imageFill(dl, cover, p0, p1, r, IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
	} else {
		const ImU32 tint = g.tint ? g.tint : tintFor(g.title);
		if (g_grad) {
			dl->AddImageRounded(g_grad.id, p0, p1, ImVec2(0, 0), ImVec2(1, 1), alpha(mix(tint, IM_COL32(255, 255, 255, 255), 0.18f), a), r);
		} else {
			dl->AddRectFilled(p0, p1, alpha(tint, a), r);
		}
		const Tex gl = glow();
		if (gl) {
			// a soft light across it, as on a box's face
			const ImVec2 c(p0.x + w * 0.68f, p0.y + (p1.y - p0.y) * 0.36f);
			dl->AddImage(gl.id, ImVec2(c.x - w * 0.7f, c.y - w * 0.7f), ImVec2(c.x + w * 0.7f, c.y + w * 0.7f), ImVec2(0, 0), ImVec2(1, 1),
				IM_COL32(255, 255, 255, static_cast<int>(46 * a)));
		}
		icon(dl, Icon::Disc, ImVec2(p0.x + w * 0.5f, p0.y + (p1.y - p0.y) * 0.4f), w * 0.34f, IM_COL32(255, 255, 255, static_cast<int>(40 * a)));
		if (!g.genre.empty()) {
			label(dl, px(12.0f), ImVec2(p0.x + px(16.0f), p0.y + px(16.0f)), IM_COL32(255, 255, 255, static_cast<int>(190 * a)), g.genre);
		}
		// the name at its foot, over a shade
		dl->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - (p1.y - p0.y) * 0.45f), ImVec2(p1.x, p1.y - r), IM_COL32(0, 0, 0, 0),
			IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, static_cast<int>(150 * a)), IM_COL32(0, 0, 0, static_cast<int>(150 * a)));
		dl->AddRectFilled(ImVec2(p0.x, p1.y - r), p1, IM_COL32(0, 0, 0, static_cast<int>(150 * a)), r,
			ImDrawCornerFlags_BotLeft | ImDrawCornerFlags_BotRight);
		const float fs = big ? px(30.0f) : px(25.0f);
		const float tw = w - px(36.0f);
		const float th = paragraph(nullptr, F::Title, fs, ImVec2(0, 0), 0, g.title, tw, 3, 1.08f);
		paragraph(dl, F::Title, fs, ImVec2(p0.x + px(18.0f), p1.y - px(20.0f) - th), IM_COL32(255, 255, 255, static_cast<int>(255 * a)),
			g.title, tw, 3, 1.08f);
	}
}

} // namespace

namespace {

void drawHomeHead(ImDrawList *dl, ImVec2 S, const HomeView &v)
{
	const Palette &p = P();
	const float L = px(96.0f);
	// ---- the head: the logo, the tabs, the status
	const float headY = px(56.0f), headH = px(56.0f);
	float x = L;
	if (v.logo) {
		const float lw = headH * v.logo.w / v.logo.h;
		dl->AddImageRounded(v.logo.id, ImVec2(x, headY), ImVec2(x + lw, headY + headH), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, px(8.0f));
		x += lw + px(56.0f);
	} else {
		text(dl, F::Title, px(34.0f), ImVec2(x, headY + px(8.0f)), p.text, "OGBox");
		x += measure(F::Title, px(34.0f), "OGBox").x + px(56.0f);
	}
	if (!v.tabs.empty()) {
		const float kh = px(30.0f), ty = headY + (headH - kh) * 0.5f;
		x += keycap(dl, ImVec2(x, ty), kh, "L1", p.textFaint) + px(10.0f);
		const float ph = px(44.0f), py = headY + (headH - ph) * 0.5f, fs = px(19.0f);
		std::vector<float> xs, ws;
		float tx = x;
		for (const std::string &t : v.tabs) {
			const float w = measure(F::Heading, fs, t.c_str()).x + px(44.0f);
			xs.push_back(tx);
			ws.push_back(w);
			tx += w + px(6.0f);
		}
		// the chosen tab's pill slides to it
		const int on = std::max(0, std::min(static_cast<int>(v.tabs.size()) - 1, v.tab));
		float &pillX = value("home.tabx", xs[static_cast<size_t>(on)]);
		float &pillW = value("home.tabw", ws[static_cast<size_t>(on)]);
		pillX = approach(pillX, xs[static_cast<size_t>(on)], 18.0f);
		pillW = approach(pillW, ws[static_cast<size_t>(on)], 18.0f);
		dl->AddRectFilled(ImVec2(pillX, py), ImVec2(pillX + pillW, py + ph), IM_COL32(255, 255, 255, 36), ph * 0.5f);
		for (size_t i = 0; i < v.tabs.size(); i++) {
			const ImVec2 ts = measure(F::Heading, fs, v.tabs[i].c_str());
			text(dl, F::Heading, fs, ImVec2(xs[i] + (ws[i] - ts.x) * 0.5f, py + (ph - ts.y) * 0.5f),
				static_cast<int>(i) == on ? p.text : p.textFaint, v.tabs[i].c_str());
		}
		keycap(dl, ImVec2(tx + px(4.0f), ty), kh, "R1", p.textFaint);
	}
	{
		float rx = S.x - L;
		const float fs = px(17.0f);
		for (auto it = v.status.rbegin(); it != v.status.rend(); ++it) {
			const ImVec2 ts = measure(F::Small, fs, it->first.c_str());
			rx -= ts.x;
			const float ty = headY + (headH - ts.y) * 0.5f;
			text(dl, F::Small, fs, ImVec2(rx, ty), p.textFaint, it->first.c_str());
			if (it->second) {
				rx -= px(18.0f);
				dl->AddCircleFilled(ImVec2(rx + px(5.0f), headY + headH * 0.5f), px(4.5f), it->second, 12);
			}
			rx -= px(30.0f);
		}
	}
	if (v.coverProgress >= 0.0f) {
		/* 0.0.3: the covers downloading, under the status at the right */
		const float bw = px(300.0f), bh = px(5.0f), fs = px(15.0f);
		const float bx = S.x - L - bw, by = headY + headH + px(16.0f);
		const ImVec2 ts = measure(F::Small, fs, v.coverText.c_str());
		text(dl, F::Small, fs, ImVec2(S.x - L - ts.x, by + bh + px(6.0f)), p.textFaint, v.coverText.c_str());
		dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 255, 255, 30), bh * 0.5f);
		const float f = std::max(0.0f, std::min(1.0f, v.coverProgress));
		float &shown = value("home.coverbar", f);
		shown = f < shown ? f : approach(shown, f, 10.0f);
		if (shown > 0.0f) {
			dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * shown, by + bh), p.accent, bh * 0.5f);
		}
	}
}

void drawHomeList(ImDrawList *dl, ImVec2 S, const HomeView &v);

} // namespace

void drawHome(ImDrawList *dl, ImVec2 S, const HomeView &v)
{
	if (v.list) {
		drawHomeList(dl, S, v);
		return;
	}
	const Palette &p = P();
	const float L = px(96.0f);
	drawHomeHead(dl, S, v);

	// ---- the chosen game: its details on the left
	const std::string selId = v.gameSel >= 0 && v.gameSel < static_cast<int>(v.games.size()) ? v.games[static_cast<size_t>(v.gameSel)].id
		: std::string();
	{
		static std::string shownId;
		float &appear = value("home.detail", 0.0f);
		if (selId != shownId) {
			shownId = selId;
			appear = 0.0f;
		}
		appear = approach(appear, 1.0f, 9.0f);
		const float a = smooth(appear);
		const float dy = px(14.0f) * (1.0f - a);
		const float dx = L, w = px(600.0f);
		float y = px(214.0f) + dy;
		if (!v.title.empty()) {
			if (!v.kicker.empty()) {
				label(dl, px(14.0f), ImVec2(dx, y), alpha(p.accent, a), v.kicker);
				y += px(30.0f);
			}
			// the title: as large as fits in two lines
			float fs = px(60.0f);
			for (; fs > px(34.0f); fs -= px(4.0f)) {
				if (paragraph(nullptr, F::Display, fs, ImVec2(0, 0), 0, v.title, w, 9, 1.05f) <= fs * 2.2f) {
					break;
				}
			}
			const float th = paragraph(dl, F::Display, fs, ImVec2(dx, y), alpha(p.text, a), v.title, w, 3, 1.05f);
			y += th + px(22.0f);
			float cx = dx;
			for (const auto &c : v.chips) {
				const ImU32 fill = c.second == 1 ? p.accentSoft : c.second == 2 ? p.warnSoft : IM_COL32(255, 255, 255, 26);
				const ImU32 col = c.second == 1 ? p.accentText : c.second == 2 ? p.warnText : p.textDim;
				const float cw = chip(nullptr, ImVec2(cx, y), c.first, 0, 0, true);
				if (cx + cw > dx + w && cx > dx) {
					break;
				}
				chip(dl, ImVec2(cx, y), c.first, alpha(fill, a), alpha(col, a));
				cx += cw + px(10.0f);
			}
			if (!v.chips.empty()) {
				y += px(34.0f) + px(22.0f);
			}
			if (!v.meta.empty()) {
				y += paragraph(dl, F::Body, px(19.0f), ImVec2(dx, y), alpha(p.textDim, a), v.meta, w - px(40.0f), 3) + px(28.0f);
			}
			float bx = dx;
			if (!v.primary.empty()) {
				const float bh = px(58.0f), fs2 = px(19.0f);
				const float kw = px(30.0f);
				const float tw = measure(F::Heading, fs2, v.primary.c_str()).x;
				const float bw = px(26.0f) + kw + px(14.0f) + tw + px(28.0f);
				dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw, y + bh), alpha(p.accent, a), px(12.0f));
				keycap(dl, ImVec2(bx + px(26.0f), y + (bh - kw) * 0.5f), kw, v.primaryKey, alpha(p.accentInk, a), alpha(IM_COL32(255, 255, 255, 90), a));
				text(dl, F::Heading, fs2, ImVec2(bx + px(26.0f) + kw + px(14.0f), y + (bh - measure(F::Heading, fs2, "Ag").y) * 0.5f),
					alpha(p.accentInk, a), v.primary.c_str());
				bx += bw + px(14.0f);
				if (!v.secondary.empty()) {
					const float tw2 = measure(F::Heading, fs2, v.secondary.c_str()).x;
					const float bw2 = px(22.0f) + kw + px(12.0f) + tw2 + px(24.0f);
					dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw2, y + bh), alpha(IM_COL32(255, 255, 255, 14), a), px(12.0f));
					dl->AddRect(ImVec2(bx, y), ImVec2(bx + bw2, y + bh), alpha(IM_COL32(255, 255, 255, 56), a), px(12.0f),
						ImDrawCornerFlags_All, px(1.5f));
					keycap(dl, ImVec2(bx + px(22.0f), y + (bh - kw) * 0.5f), kw, v.secondaryKey, alpha(p.textDim, a));
					text(dl, F::Heading, fs2, ImVec2(bx + px(22.0f) + kw + px(12.0f), y + (bh - measure(F::Heading, fs2, "Ag").y) * 0.5f),
						alpha(p.text, a), v.secondary.c_str());
				}
			}
		}
	}

	// ---- the games' row: the chosen one bigger, the row sliding to it
	const float rowX = px(760.0f), rowBottom = px(694.0f);
	const float baseW = px(300.0f), grow = px(60.0f), gap = px(28.0f);
	const int n = static_cast<int>(v.games.size());
	if (n > 0) {
		float &scroll = value("home.scroll", static_cast<float>(std::max(0, v.gameSel)));
		const float target = static_cast<float>(std::max(0, std::min(n - 1, v.gameSel)));
		if (std::fabs(scroll - target) > 30.0f) {
			scroll = target + (scroll < target ? -3.0f : 3.0f);   // (a long jump: the last stretch moves)
		}
		scroll = approach(scroll, target, 13.0f);
		// each one's growth (the chosen one's toward 1)
		const int first = std::max(0, static_cast<int>(std::floor(scroll)) - 3);
		const int last = std::min(n - 1, static_cast<int>(std::ceil(scroll)) + 8);
		std::vector<float> f(static_cast<size_t>(last - first + 1));
		for (int i = first; i <= last; i++) {
			float &g = value("home.g." + v.games[static_cast<size_t>(i)].id, 0.0f);
			g = approach(g, i == v.gameSel ? 1.0f : 0.0f, 12.0f);
			f[static_cast<size_t>(i - first)] = g;
		}
		const auto growth = [&](int i) { return i >= first && i <= last ? f[static_cast<size_t>(i - first)] : 0.0f; };
		// positions from the scroll's place: the one at the scroll sits at rowX
		const int s0 = static_cast<int>(std::floor(scroll));
		const float frac = scroll - static_cast<float>(s0);
		const float span0 = baseW + grow * growth(s0) + gap;
		float x0 = rowX - frac * span0;   // where item s0 starts
		std::vector<std::pair<int, float>> xs;
		{
			float xx = x0;
			for (int i = s0; i <= last; i++) {
				xs.push_back({ i, xx });
				xx += baseW + grow * growth(i) + gap;
			}
			xx = x0;
			for (int i = s0 - 1; i >= first; i--) {
				xx -= baseW + grow * growth(i) + gap;
				xs.push_back({ i, xx });
			}
		}
		// the ones before first, then the chosen one last (on top)
		std::sort(xs.begin(), xs.end(), [&](const std::pair<int, float> &a, const std::pair<int, float> &b) {
			return growth(a.first) < growth(b.first);
		});
		const float rowFocus = v.focusRow == 0 ? 1.0f : 0.0f;
		float &rf = value("home.rowfocus", rowFocus);
		rf = approach(rf, rowFocus, 10.0f);
		for (const auto &it : xs) {
			const int i = it.first;
			const float g = growth(i);
			const float w = baseW + grow * g, h = w * 1.4f;
			const float gx = it.second;
			if (gx > S.x || gx + w < 0.0f) {
				continue;
			}
			// fading out where the details are
			float a = std::max(0.0f, std::min(1.0f, (gx - px(640.0f)) / px(130.0f)));
			a *= std::max(0.0f, std::min(1.0f, (S.x - gx) / px(160.0f) + 0.15f));
			a = std::max(a, g);
			if (a <= 0.01f) {
				continue;
			}
			const float dim = 0.55f + 0.45f * std::max(g * rf, i == v.gameSel ? 0.0f : 0.0f);
			const ImVec2 q0(gx, rowBottom - h), q1(gx + w, rowBottom);
			const Tex cov = v.cover ? v.cover(static_cast<size_t>(i)) : Tex{};
			drawCover(dl, v.games[static_cast<size_t>(i)], cov, q0, q1, g, a * (i == v.gameSel ? 1.0f : dim), i == v.gameSel);
			if (i == v.gameSel && rf > 0.01f) {
				ring(dl, q0, q1, px(10.0f), px(6.0f), alpha(p.accent, rf * g), px(3.0f));
			}
		}
	} else {
		const std::string &msg = !v.error.empty() ? v.error : v.empty;
		if (v.loading) {
			spinner(dl, ImVec2(rowX + px(40.0f), px(440.0f)), px(22.0f), p.accent);
			text(dl, F::Body, px(20.0f), ImVec2(rowX + px(84.0f), px(428.0f)), p.textDim, "Reading the folder\xE2\x80\xA6");
		} else if (!msg.empty()) {
			paragraph(dl, F::Body, px(20.0f), ImVec2(rowX, px(410.0f)), v.error.empty() ? p.textDim : p.fail, msg, S.x - rowX - L, 4);
		}
	}

	// ---- the cards: folders, the dashboard, an action
	if (!v.cards.empty()) {
		const float cy = px(780.0f);
		text(dl, F::Heading, px(21.0f), ImVec2(L, cy), p.text, v.cardsTitle.c_str());
		if (!v.cardsSub.empty()) {
			text(dl, F::Small, px(16.0f), ImVec2(L + measure(F::Heading, px(21.0f), v.cardsTitle.c_str()).x + px(18.0f), cy + px(3.0f)),
				p.textFaint, v.cardsSub.c_str());
		}
		const float cw = px(300.0f), ch = px(104.0f), cg = px(20.0f), top = cy + px(40.0f);
		const int m = static_cast<int>(v.cards.size());
		const int sel = std::max(0, std::min(m - 1, v.cardSel));
		// the row slides so the chosen card stays in sight
		const float visible = std::floor((S.x - 2.0f * L + cg) / (cw + cg));
		const float wantFirst = std::max(0.0f, std::min(static_cast<float>(m) - visible, static_cast<float>(sel) - visible + 1.0f));
		float &cs = value("home.cardscroll", 0.0f);
		float target = cs;
		if (static_cast<float>(sel) < cs) {
			target = static_cast<float>(sel);
		} else if (static_cast<float>(sel) > cs + visible - 1.0f) {
			target = wantFirst;
		}
		target = std::max(0.0f, std::min(std::max(0.0f, static_cast<float>(m) - visible), target));
		cs = approach(cs, target, 14.0f);
		const bool rowOn = v.focusRow == 1;
		dl->PushClipRect(ImVec2(0, top - px(12.0f)), ImVec2(S.x, top + ch + px(12.0f)), true);
		for (int i = 0; i < m; i++) {
			const HomeCard &c = v.cards[static_cast<size_t>(i)];
			const float cx = L + (static_cast<float>(i) - cs) * (cw + cg);
			if (cx > S.x || cx + cw < 0.0f) {
				continue;
			}
			float &h = value("home.c." + c.title + c.sub, 0.0f);
			h = approach(h, rowOn && i == sel ? 1.0f : 0.0f, 14.0f);
			const ImVec2 c0(cx, top - px(4.0f) * h), c1(cx + cw, top + ch - px(4.0f) * h);
			if (c.dashed) {
				dl->AddRect(c0, c1, IM_COL32(255, 255, 255, static_cast<int>(46 + 60 * h)), px(12.0f), ImDrawCornerFlags_All, px(1.5f));
			} else {
				dl->AddRectFilled(c0, c1, IM_COL32(255, 255, 255, static_cast<int>(18 + 22 * h)), px(12.0f));
				dl->AddRect(c0, c1, IM_COL32(255, 255, 255, 20), px(12.0f), ImDrawCornerFlags_All, px(1.0f));
			}
			if (h > 0.01f) {
				ring(dl, c0, c1, px(12.0f), px(4.0f), alpha(p.accent, h), px(2.5f));
			}
			icon(dl, c.icon, ImVec2(c0.x + px(42.0f), (c0.y + c1.y) * 0.5f), px(34.0f), c.accent ? p.accent : (c.dashed ? p.textFaint : p.textDim));
			const float tx = c0.x + px(76.0f), tw = cw - px(92.0f);
			if (c.sub.empty()) {
				textFit(dl, F::Heading, px(19.0f), ImVec2(tx, (c0.y + c1.y) * 0.5f - px(11.0f)), c.dashed ? p.textDim : p.text, c.title, tw);
			} else {
				textFit(dl, F::Heading, px(19.0f), ImVec2(tx, (c0.y + c1.y) * 0.5f - px(24.0f)), p.text, c.title, tw);
				textFit(dl, F::Small, px(15.0f), ImVec2(tx, (c0.y + c1.y) * 0.5f + px(5.0f)), p.textFaint, c.sub, tw);
			}
		}
		dl->PopClipRect();
	}

	// ---- the letter L2 / R2 jumped to
	if (v.jumpLetter && v.jumpAlpha > 0.0f) {
		const char t[2] = { v.jumpLetter, 0 };
		const float box = px(150.0f);
		const ImVec2 c(rowX + px(180.0f), px(440.0f));
		dl->AddRectFilled(ImVec2(c.x - box * 0.5f, c.y - box * 0.5f), ImVec2(c.x + box * 0.5f, c.y + box * 0.5f),
			alpha(IM_COL32(18, 22, 27, 235), v.jumpAlpha), px(22.0f));
		const ImVec2 ts = measure(F::Display, px(90.0f), t);
		text(dl, F::Display, px(90.0f), ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), alpha(p.accent, v.jumpAlpha), t);
	}

	footer(dl, S, v.footer, v.version);

	if (!v.busy.empty()) {
		dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, 150));
		const ImVec2 ts = measure(F::Title, px(30.0f), v.busy.c_str());
		spinner(dl, ImVec2(S.x * 0.5f, S.y * 0.5f - px(50.0f)), px(26.0f), p.accent);
		text(dl, F::Title, px(30.0f), ImVec2((S.x - ts.x) * 0.5f, S.y * 0.5f), p.text, v.busy.c_str());
	}
}

namespace {

/* 0.0.3: the list layout: the cards (places, folders, the dashboard) then the games in one column
 * on the left, the chosen one's cover and details on the right. */
void drawHomeList(ImDrawList *dl, ImVec2 S, const HomeView &v)
{
	const Palette &p = P();
	const float L = px(96.0f);
	drawHomeHead(dl, S, v);
	const int nc = static_cast<int>(v.cards.size()), ng = static_cast<int>(v.games.size());
	const int n = nc + ng;
	int sel = v.focusRow == 1 ? v.cardSel : (v.gameSel >= 0 ? nc + v.gameSel : -1);
	if (sel < 0 && n) {
		sel = 0;
	}
	// ---- the list
	const float x0 = L, w = px(820.0f), top = px(150.0f), bottom = S.y - px(112.0f);
	const float rh = px(78.0f), gap = px(4.0f);
	const float viewH = bottom - top;
	float &scroll = value("homelist.scroll", 0.0f);
	{
		float target = scroll;
		if (sel >= 0) {
			const float y = static_cast<float>(sel) * (rh + gap);
			if (y - rh < target) {
				target = y - rh;
			} else if (y + 2.0f * rh > target + viewH) {
				target = y + 2.0f * rh - viewH;
			}
		}
		target = std::max(0.0f, std::min(std::max(0.0f, static_cast<float>(n) * (rh + gap) - viewH), target));
		if (std::fabs(target - scroll) > viewH * 3.0f) {
			scroll = target;   // (a long jump: no slide through hundreds of rows)
		}
		scroll = approach(scroll, target, 16.0f);
	}
	if (n == 0) {
		const std::string &msg = !v.error.empty() ? v.error : v.empty;
		if (v.loading) {
			spinner(dl, ImVec2(x0 + px(30.0f), top + px(40.0f)), px(20.0f), p.accent);
			text(dl, F::Body, px(20.0f), ImVec2(x0 + px(70.0f), top + px(28.0f)), p.textDim, "Reading the folder\xE2\x80\xA6");
		} else if (!msg.empty()) {
			paragraph(dl, F::Body, px(20.0f), ImVec2(x0, top + px(20.0f)), v.error.empty() ? p.textDim : p.fail, msg, w, 4);
		}
	}
	float &hy = value("homelist.hy", 0.0f);
	if (sel >= 0) {
		const float want = static_cast<float>(sel) * (rh + gap);
		hy = std::fabs(hy - want) > rh * 6.0f ? want : approach(hy, want, 22.0f);
	}
	dl->PushClipRect(ImVec2(x0 - px(8.0f), top - px(4.0f)), ImVec2(x0 + w + px(8.0f), bottom), true);
	if (sel >= 0) {
		const float y = top + hy - scroll;
		dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + rh), IM_COL32(255, 255, 255, 30), px(12.0f));
		dl->AddRect(ImVec2(x0, y), ImVec2(x0 + w, y + rh), alpha(p.accent, 0.8f), px(12.0f), ImDrawCornerFlags_All, px(2.0f));
	}
	const int first = std::max(0, static_cast<int>(scroll / (rh + gap)) - 1);
	const int last = std::min(n - 1, static_cast<int>((scroll + viewH) / (rh + gap)) + 1);
	for (int i = first; i <= last; i++) {
		const float y = top + static_cast<float>(i) * (rh + gap) - scroll;
		const bool on = i == sel;
		const float tx = x0 + px(84.0f), tw = w - px(84.0f) - px(24.0f);
		if (i < nc) {
			const HomeCard &c = v.cards[static_cast<size_t>(i)];
			icon(dl, c.icon, ImVec2(x0 + px(44.0f), y + rh * 0.5f), px(32.0f), c.accent ? p.accent : p.textDim);
			if (c.sub.empty()) {
				textFit(dl, F::Heading, px(20.0f), ImVec2(tx, y + rh * 0.5f - px(12.0f)), on ? p.text : p.textDim, c.title, tw);
			} else {
				textFit(dl, F::Heading, px(20.0f), ImVec2(tx, y + px(14.0f)), on ? p.text : p.textDim, c.title, tw);
				textFit(dl, F::Small, px(15.0f), ImVec2(tx, y + px(44.0f)), p.textFaint, c.sub, tw);
			}
			continue;
		}
		const int k = i - nc;
		const HomeGame &g = v.games[static_cast<size_t>(k)];
		const float cw = px(46.0f), ch = px(64.0f);
		const ImVec2 c0(x0 + px(16.0f), y + (rh - ch) * 0.5f), c1(c0.x + cw, c0.y + ch);
		const Tex cov = v.cover ? v.cover(static_cast<size_t>(k)) : Tex{};
		if (cov) {
			imageFill(dl, cov, c0, c1, px(5.0f));
		} else {
			dl->AddRectFilled(c0, c1, g.tint ? g.tint : tintFor(g.title), px(5.0f));
			icon(dl, Icon::Disc, ImVec2((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f), px(22.0f), IM_COL32(255, 255, 255, 60));
		}
		std::string sub = g.sub;
		if (!g.size.empty()) {
			sub += sub.empty() ? g.size : " \xC2\xB7 " + g.size;
		}
		if (sub.empty()) {
			textFit(dl, F::Heading, px(20.0f), ImVec2(tx, y + rh * 0.5f - px(12.0f)), on ? p.text : p.textDim, g.title, tw);
		} else {
			textFit(dl, F::Heading, px(20.0f), ImVec2(tx, y + px(14.0f)), on ? p.text : p.textDim, g.title, tw);
			textFit(dl, F::Small, px(15.0f), ImVec2(tx, y + px(44.0f)), p.textFaint, sub, tw);
		}
	}
	dl->PopClipRect();
	if (scroll + viewH < static_cast<float>(n) * (rh + gap) - px(4.0f)) {
		dl->AddRectFilledMultiColor(ImVec2(x0 - px(8.0f), bottom - px(50.0f)), ImVec2(x0 + w + px(8.0f), bottom), alpha(p.bg, 0.0f),
			alpha(p.bg, 0.0f), alpha(p.bg, 0.9f), alpha(p.bg, 0.9f));
	}
	if (n > 12) {
		// where in the list: a thin bar at its right
		const float bh = std::max(px(40.0f), viewH * viewH / (static_cast<float>(n) * (rh + gap)));
		const float by = top + (viewH - bh) * (scroll / std::max(1.0f, static_cast<float>(n) * (rh + gap) - viewH));
		dl->AddRectFilled(ImVec2(x0 + w + px(14.0f), by), ImVec2(x0 + w + px(18.0f), by + bh), IM_COL32(255, 255, 255, 50), px(2.0f));
	}

	// ---- the chosen one, at the right
	const float dx0 = x0 + w + px(64.0f), dx1 = S.x - L;
	static int shownSel = -2;
	float &appear = value("homelist.detail", 0.0f);
	if (sel != shownSel) {
		shownSel = sel;
		appear = 0.0f;
	}
	appear = approach(appear, 1.0f, 10.0f);
	const float a = smooth(appear);
	if (sel >= 0 && sel < nc) {
		const HomeCard &c = v.cards[static_cast<size_t>(sel)];
		const float y = top + px(40.0f);
		icon(dl, c.icon, ImVec2(dx0 + px(60.0f), y + px(60.0f)), px(96.0f), alpha(c.accent ? p.accent : p.textDim, a));
		paragraph(dl, F::Display, px(44.0f), ImVec2(dx0, y + px(150.0f)), alpha(p.text, a), c.title, dx1 - dx0, 2, 1.05f);
		if (!c.sub.empty()) {
			paragraph(dl, F::Body, px(19.0f), ImVec2(dx0, y + px(260.0f)), alpha(p.textDim, a), c.sub, dx1 - dx0, 3);
		}
	} else if (sel >= nc && v.gameSel >= 0) {
		const HomeGame &g = v.games[static_cast<size_t>(v.gameSel)];
		const float cw = px(380.0f), ch = cw * 1.4f;
		const ImVec2 c0(dx0, top + px(10.0f) + px(10.0f) * (1.0f - a)), c1(c0.x + cw, c0.y + ch);
		drawCover(dl, g, v.cover ? v.cover(static_cast<size_t>(v.gameSel)) : Tex{}, c0, c1, 1.0f, a, true);
		const float x = c1.x + px(44.0f), tw = dx1 - x;
		float y = top + px(14.0f);
		if (!v.kicker.empty()) {
			label(dl, px(14.0f), ImVec2(x, y), alpha(p.accent, a), v.kicker);
			y += px(30.0f);
		}
		float fs = px(46.0f);
		for (; fs > px(28.0f); fs -= px(3.0f)) {
			if (paragraph(nullptr, F::Display, fs, ImVec2(0, 0), 0, v.title, tw, 9, 1.05f) <= fs * 3.3f) {
				break;
			}
		}
		y += paragraph(dl, F::Display, fs, ImVec2(x, y), alpha(p.text, a), v.title, tw, 4, 1.05f) + px(20.0f);
		float cx = x;
		for (const auto &c : v.chips) {
			const ImU32 fill = c.second == 1 ? p.accentSoft : c.second == 2 ? p.warnSoft : IM_COL32(255, 255, 255, 26);
			const ImU32 col = c.second == 1 ? p.accentText : c.second == 2 ? p.warnText : p.textDim;
			const float cwid = chip(nullptr, ImVec2(0, 0), c.first, 0, 0, true);
			if (cx + cwid > x + tw && cx > x) {
				cx = x;
				y += px(42.0f);
			}
			chip(dl, ImVec2(cx, y), c.first, alpha(fill, a), alpha(col, a));
			cx += cwid + px(10.0f);
		}
		if (!v.chips.empty()) {
			y += px(34.0f) + px(22.0f);
		}
		if (!v.meta.empty()) {
			y += paragraph(dl, F::Body, px(18.5f), ImVec2(x, y), alpha(p.textDim, a), v.meta, tw, 6) + px(28.0f);
		}
		const float bh = px(56.0f), kw = px(30.0f), fs2 = px(19.0f);
		for (int b = 0; b < 2; b++) {
			const std::string &label2 = b == 0 ? v.primary : v.secondary;
			if (label2.empty()) {
				continue;
			}
			const std::string &key = b == 0 ? v.primaryKey : v.secondaryKey;
			const float bw = std::min(tw, px(26.0f) + kw + px(14.0f) + measure(F::Heading, fs2, label2.c_str()).x + px(28.0f));
			if (b == 0) {
				dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), alpha(p.accent, a), px(12.0f));
			} else {
				dl->AddRect(ImVec2(x, y), ImVec2(x + bw, y + bh), alpha(IM_COL32(255, 255, 255, 56), a), px(12.0f), ImDrawCornerFlags_All, px(1.5f));
			}
			keycap(dl, ImVec2(x + px(26.0f), y + (bh - kw) * 0.5f), kw, key, alpha(b == 0 ? p.accentInk : p.textDim, a),
				b == 0 ? alpha(IM_COL32(255, 255, 255, 90), a) : 0);
			textFit(dl, F::Heading, fs2, ImVec2(x + px(26.0f) + kw + px(14.0f), y + (bh - measure(F::Heading, fs2, "Ag").y) * 0.5f),
				alpha(b == 0 ? p.accentInk : p.text, a), label2, bw - px(26.0f) - kw - px(14.0f) - px(12.0f));
			y += bh + px(12.0f);
		}
	}
	if (v.jumpLetter && v.jumpAlpha > 0.0f) {
		const char t[2] = { v.jumpLetter, 0 };
		const float box = px(150.0f);
		const ImVec2 c(x0 + w * 0.5f, top + viewH * 0.5f);
		dl->AddRectFilled(ImVec2(c.x - box * 0.5f, c.y - box * 0.5f), ImVec2(c.x + box * 0.5f, c.y + box * 0.5f),
			alpha(IM_COL32(18, 22, 27, 235), v.jumpAlpha), px(22.0f));
		const ImVec2 ts = measure(F::Display, px(90.0f), t);
		text(dl, F::Display, px(90.0f), ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), alpha(p.accent, v.jumpAlpha), t);
	}
	footer(dl, S, v.footer, v.version);
	if (!v.busy.empty()) {
		dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, 150));
		const ImVec2 ts = measure(F::Title, px(30.0f), v.busy.c_str());
		spinner(dl, ImVec2(S.x * 0.5f, S.y * 0.5f - px(50.0f)), px(26.0f), p.accent);
		text(dl, F::Title, px(30.0f), ImVec2((S.x - ts.x) * 0.5f, S.y * 0.5f), p.text, v.busy.c_str());
	}
}

} // namespace

// ---- settings -----------------------------------------------------------------------------------

namespace {

bool sideBySide(const SetRow &r)
{
	if (r.kind != SetRow::Kind::Choice || r.choices.size() < 2 || r.choices.size() > 5) {
		return false;
	}
	return segmented(nullptr, ImVec2(0, 0), r.choices, r.choice, false, true) <= px(440.0f);
}

float rowHeight(const SetRow &r, float w, bool open)
{
	switch (r.kind) {
	case SetRow::Kind::Group:
		return px(48.0f);
	case SetRow::Kind::Share:
		return px(80.0f);
	default:
		break;
	}
	if (!open || r.desc.empty()) {
		return px(72.0f);
	}
	const float dw = w - px(52.0f);
	const float dh = paragraph(nullptr, F::Small, px(15.5f), ImVec2(0, 0), 0, r.desc, dw, 6);
	return px(72.0f) + dh + px(14.0f);
}

void drawRow(ImDrawList *dl, const SetRow &r, ImVec2 p0, float w, float h, float hl, bool focused, float open)
{
	const Palette &p = P();
	if (r.kind == SetRow::Kind::Group) {
		label(dl, px(13.0f), ImVec2(p0.x + px(26.0f), p0.y + px(22.0f)), p.textFaint, r.name);
		return;
	}
	const ImVec2 p1(p0.x + w, p0.y + h);
	if (hl > 0.01f) {
		dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, static_cast<int>(28 * hl)), px(12.0f));
		if (focused) {
			dl->AddRect(p0, p1, alpha(r.warn ? p.warn : p.accent, 0.75f * hl), px(12.0f), ImDrawCornerFlags_All, px(2.0f));
		}
	}
	const float lineH = px(72.0f);
	const float cy = p0.y + lineH * 0.5f;
	float nx = p0.x + px(26.0f);
	if (r.changed) {
		dl->AddCircleFilled(ImVec2(p0.x + px(13.0f), cy), px(3.5f), r.warn ? p.warn : p.accent, 10);
	}
	if (r.icon != Icon::None) {
		icon(dl, r.icon, ImVec2(nx + px(15.0f), cy), px(28.0f), p.textDim);
		nx += px(46.0f);
	}
	// the value at the right: what it takes decides the name's room
	float valueW = 0.0f;
	const float right = p1.x - px(26.0f);
	const float vh = px(40.0f);
	const ImVec2 vp(0, cy - vh * 0.5f);
	switch (r.kind) {
	case SetRow::Kind::Choice:
		if (sideBySide(r)) {
			valueW = segmented(nullptr, vp, r.choices, r.choice, focused, true);
			segmented(dl, ImVec2(right - valueW, vp.y), r.choices, r.choice, focused);
		} else {
			const std::string s = r.choice >= 0 && r.choice < static_cast<int>(r.choices.size()) ? r.choices[static_cast<size_t>(r.choice)] : r.value;
			const float fs = px(18.0f);
			const float tw = measure(F::Body, fs, s.c_str()).x;
			const float arrow = px(26.0f);
			valueW = tw + (focused ? arrow * 2.0f : 0.0f);
			float vx = right - valueW;
			if (focused) {
				text(dl, F::Body, fs, ImVec2(vx, cy - px(12.0f)), p.accent, "\xE2\x86\x90");
				vx += arrow;
			}
			text(dl, F::Body, fs, ImVec2(vx, cy - px(12.0f)), r.inherited ? p.textFaint : p.text, s.c_str());
			if (focused) {
				text(dl, F::Body, fs, ImVec2(vx + tw + px(8.0f), cy - px(12.0f)), p.accent, "\xE2\x86\x92");
			}
		}
		break;
	case SetRow::Kind::Number: {
		const float fs = px(18.0f);
		const float tw = measure(F::Body, fs, r.value.c_str()).x;
		const float arrow = px(26.0f);
		valueW = tw + (focused ? arrow * 2.0f : 0.0f);
		float vx = right - valueW;
		if (focused) {
			text(dl, F::Body, fs, ImVec2(vx, cy - px(12.0f)), p.accent, "\xE2\x86\x90");
			vx += arrow;
		}
		text(dl, F::Body, fs, ImVec2(vx, cy - px(12.0f)), r.inherited ? p.textFaint : p.text, r.value.c_str());
		if (focused) {
			text(dl, F::Body, fs, ImVec2(vx + tw + px(8.0f), cy - px(12.0f)), p.accent, "\xE2\x86\x92");
		}
		break;
	}
	case SetRow::Kind::Toggle: {
		float &k = value("tog." + r.key, r.on ? 1.0f : 0.0f);
		k = approach(k, r.on ? 1.0f : 0.0f, 16.0f);
		valueW = px(56.0f);
		toggle(dl, ImVec2(right - valueW, cy - px(16.0f)), k, focused);
		break;
	}
	case SetRow::Kind::Text: {
		valueW = px(300.0f);
		field(dl, ImVec2(right - valueW, cy - px(20.0f)), ImVec2(right, cy + px(20.0f)), r.value, r.placeholder, focused, r.password);
		break;
	}
	case SetRow::Kind::Action:
	case SetRow::Kind::Info: {
		const float fs = px(16.0f);
		std::string s = r.value;
		const float tw = s.empty() ? 0.0f : measure(F::Small, fs, s.c_str()).x;
		valueW = std::min(tw, px(420.0f)) + (r.kind == SetRow::Kind::Action ? px(28.0f) : 0.0f);
		float vx = right - valueW;
		if (r.dot) {
			dl->AddCircleFilled(ImVec2(vx - px(12.0f), cy), px(4.5f), r.dot, 12);
		}
		textFit(dl, F::Small, fs, ImVec2(vx, cy - px(10.0f)), r.kind == SetRow::Kind::Info ? p.textDim : p.textFaint, s, px(420.0f));
		if (r.kind == SetRow::Kind::Action) {
			const float ax = right - px(10.0f);
			dl->AddLine(ImVec2(ax - px(6.0f), cy - px(7.0f)), ImVec2(ax, cy), focused ? p.accent : p.textFaint, px(2.0f));
			dl->AddLine(ImVec2(ax - px(6.0f), cy + px(7.0f)), ImVec2(ax, cy), focused ? p.accent : p.textFaint, px(2.0f));
		}
		break;
	}
	case SetRow::Kind::Share: {
		const float fs = px(15.5f);
		const float tw = measure(F::Small, fs, r.value.c_str()).x;
		valueW = std::min(tw, px(380.0f)) + px(22.0f);
		const float vx = right - valueW + px(22.0f);
		if (r.dot) {
			dl->AddCircleFilled(ImVec2(vx - px(13.0f), cy), px(4.5f), r.dot, 12);
		}
		textFit(dl, F::Small, fs, ImVec2(vx, cy - px(10.0f)), r.dot == p.ok ? p.accentText : r.dot ? p.warnText : p.textFaint, r.value,
			px(380.0f));
		break;
	}
	default:
		break;
	}
	const float nameW = right - valueW - px(24.0f) - nx;
	if (r.kind == SetRow::Kind::Share) {
		textFit(dl, F::Heading, px(19.5f), ImVec2(nx, cy - px(22.0f)), p.text, r.name, nameW);
		textFit(dl, F::Small, px(15.0f), ImVec2(nx, cy + px(5.0f)), p.textFaint, r.desc, nameW);
		return;
	}
	const float nfs = px(19.5f);
	textFit(dl, F::Heading, nfs, ImVec2(nx, cy - measure(F::Heading, nfs, "Ag").y * 0.5f - (r.inherited ? px(9.0f) : 0.0f)),
		p.text, r.name, nameW);
	if (r.inherited) {
		text(dl, F::Small, px(13.5f), ImVec2(nx, cy + px(6.0f)), p.textFaint, "as the app's");
	}
	if (open > 0.01f && !r.desc.empty()) {
		dl->PushClipRect(ImVec2(p0.x, p0.y + lineH - px(6.0f)), p1, true);
		paragraph(dl, F::Small, px(15.5f), ImVec2(p0.x + px(26.0f), p0.y + lineH - px(6.0f)), alpha(p.textFaint, open), r.desc,
			w - px(52.0f), 6);
		dl->PopClipRect();
	}
}

} // namespace

float drawRows(ImDrawList *dl, ImVec2 p0, ImVec2 p1, const std::vector<SetRow> &rows, int sel, bool focused,
	const std::string &scrollKey)
{
	const float w = p1.x - p0.x;
	const float gap = px(4.0f);
	std::vector<float> hs(rows.size()), ys(rows.size());
	float total = 0.0f;
	for (size_t i = 0; i < rows.size(); i++) {
		const SetRow &r = rows[i];
		float &o = value(scrollKey + ".o." + r.key + r.name, 0.0f);
		o = approach(o, focused && static_cast<int>(i) == sel ? 1.0f : 0.0f, 12.0f);
		const float closed = rowHeight(r, w, false);
		const float full = rowHeight(r, w, true);
		hs[i] = closed + (full - closed) * smooth(o);
		ys[i] = total;
		total += hs[i] + gap;
	}
	// the scroll that keeps the chosen row in sight (a little room around it)
	const float viewH = p1.y - p0.y;
	float &scroll = value(scrollKey + ".scroll", 0.0f);
	float target = scroll;
	if (sel >= 0 && sel < static_cast<int>(rows.size())) {
		const float top = ys[static_cast<size_t>(sel)] - px(60.0f);
		const float bottom = ys[static_cast<size_t>(sel)] + hs[static_cast<size_t>(sel)] + px(60.0f);
		if (top < target) {
			target = top;
		} else if (bottom > target + viewH) {
			target = bottom - viewH;
		}
	}
	target = std::max(0.0f, std::min(std::max(0.0f, total - viewH), target));
	scroll = approach(scroll, target, 14.0f);
	dl->PushClipRect(p0, p1, true);
	for (size_t i = 0; i < rows.size(); i++) {
		const float y = p0.y + ys[i] - scroll;
		if (y > p1.y || y + hs[i] < p0.y) {
			continue;
		}
		const bool on = static_cast<int>(i) == sel;
		float &hl = value(scrollKey + ".h." + rows[i].key + rows[i].name, 0.0f);
		hl = approach(hl, on ? (focused ? 1.0f : 0.45f) : 0.0f, 16.0f);
		float &o = value(scrollKey + ".o." + rows[i].key + rows[i].name, 0.0f);
		drawRow(dl, rows[i], ImVec2(p0.x, y), w, hs[i], hl, on && focused, smooth(o));
	}
	dl->PopClipRect();
	// a hint that there is more below / above
	if (scroll + viewH < total - px(4.0f)) {
		dl->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - px(40.0f)), p1, alpha(P().bg, 0.0f), alpha(P().bg, 0.0f), alpha(P().bg, 0.9f),
			alpha(P().bg, 0.9f));
	}
	return total;
}

void drawSettings(ImDrawList *dl, ImVec2 S, const SettingsView &v)
{
	const Palette &p = P();
	const float L = px(96.0f);
	float &appear = value("set.appear", 0.0f);
	appear = approach(appear, 1.0f, 10.0f);
	// the head
	float x = L;
	const float headY = px(56.0f), headH = px(56.0f);
	if (v.logo) {
		const float lw = headH * v.logo.w / v.logo.h;
		dl->AddImageRounded(v.logo.id, ImVec2(x, headY), ImVec2(x + lw, headY + headH), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, px(8.0f));
		x += lw + px(28.0f);
	}
	const float tfs = px(36.0f);
	text(dl, F::Display, tfs, ImVec2(x, headY + (headH - measure(F::Display, tfs, "Ag").y) * 0.5f), p.text, v.title.c_str());
	x += measure(F::Display, tfs, v.title.c_str()).x + px(20.0f);
	if (!v.subtitle.empty()) {
		textFit(dl, F::Body, px(19.0f), ImVec2(x, headY + px(18.0f)), p.textDim, v.subtitle, px(700.0f));
	}
	if (!v.right.empty()) {
		const ImVec2 ts = measure(F::Small, px(16.5f), v.right.c_str());
		text(dl, F::Small, px(16.5f), ImVec2(S.x - L - ts.x, headY + (headH - ts.y) * 0.5f), p.textFaint, v.right.c_str());
	}
	// the sections
	const float navX = L, navW = px(500.0f), top = px(158.0f);
	{
		float y = top;
		const float rh = px(66.0f);
		std::vector<float> ys;
		for (const SetNav &n : v.nav) {
			if (n.gapBefore) {
				y += px(22.0f);
			}
			ys.push_back(y);
			y += rh + px(4.0f);
		}
		const int sel = std::max(0, std::min(static_cast<int>(v.nav.size()) - 1, v.navSel));
		if (!ys.empty()) {
			float &hy = value("set.navy", ys[static_cast<size_t>(sel)]);
			hy = approach(hy, ys[static_cast<size_t>(sel)], 18.0f);
			dl->AddRectFilled(ImVec2(navX, hy), ImVec2(navX + navW, hy + rh), IM_COL32(255, 255, 255, v.focusNav ? 34 : 18), px(12.0f));
			if (v.focusNav) {
				dl->AddRect(ImVec2(navX, hy), ImVec2(navX + navW, hy + rh), alpha(v.nav[static_cast<size_t>(sel)].warn ? p.warn : p.accent, 0.75f),
					px(12.0f), ImDrawCornerFlags_All, px(2.0f));
			}
		}
		for (size_t i = 0; i < v.nav.size(); i++) {
			const SetNav &n = v.nav[i];
			const float yy = ys[i];
			const bool on = static_cast<int>(i) == sel;
			const float nfs = px(20.0f);
			text(dl, F::Heading, nfs, ImVec2(navX + px(22.0f), yy + (rh - measure(F::Heading, nfs, "Ag").y) * 0.5f), on ? p.text : p.textDim,
				n.name.c_str());
			if (!n.sub.empty()) {
				const float sfs = px(14.5f);
				const float maxW = navW - px(44.0f) - measure(F::Heading, nfs, n.name.c_str()).x - px(20.0f);
				float sw = std::min(measure(F::Small, sfs, n.sub.c_str()).x, maxW);
				float sx = navX + navW - px(22.0f) - sw;
				const float sy = yy + (rh - measure(F::Small, sfs, "Ag").y) * 0.5f;
				textFit(dl, F::Small, sfs, ImVec2(sx, sy), n.warn ? p.warn : p.textFaint, n.sub, maxW);
				if (n.dot) {
					dl->AddCircleFilled(ImVec2(sx - px(11.0f), yy + rh * 0.5f), px(4.0f), n.dot, 10);
				}
			}
		}
	}
	// the page
	const float px0 = navX + navW + px(64.0f), px1 = S.x - L;
	const float slide = px(30.0f) * (1.0f - easeOut(appear));
	float y = top - px(6.0f);
	if (!v.pageTitle.empty()) {
		text(dl, F::Title, px(24.0f), ImVec2(px0 + px(26.0f) + slide, y + px(4.0f)), p.text, v.pageTitle.c_str());
		if (!v.pageAction.empty()) {
			const ImVec2 ts = measure(F::Small, px(15.5f), v.pageAction.c_str());
			text(dl, F::Small, px(15.5f), ImVec2(px1 - px(26.0f) - ts.x, y + px(10.0f)), p.textFaint, v.pageAction.c_str());
		}
		y += px(54.0f);
	}
	if (!v.banner.empty()) {
		const float bw = px1 - px0 - px(52.0f);
		const float th = paragraph(nullptr, F::Small, px(15.5f), ImVec2(0, 0), 0, v.banner, bw - px(44.0f), 4);
		const ImVec2 b0(px0 + px(26.0f), y), b1(px0 + px(26.0f) + bw, y + th + px(30.0f));
		panel(dl, b0, b1, px(12.0f), v.bannerWarn ? p.warnSoft : IM_COL32(255, 255, 255, 14),
			v.bannerWarn ? alpha(p.warn, 0.35f) : IM_COL32(255, 255, 255, 24));
		paragraph(dl, F::Small, px(15.5f), ImVec2(b0.x + px(22.0f), b0.y + px(15.0f)), v.bannerWarn ? p.warnText : p.textDim, v.banner,
			bw - px(44.0f), 4);
		y = b1.y + px(12.0f);
	}
	const float bottom = S.y - px(110.0f);
	drawRows(dl, ImVec2(px0 + slide, y), ImVec2(px1 + slide, bottom), v.rows, v.rowSel, !v.focusNav, "set." + v.pageTitle);
	footer(dl, S, v.footer, v.footerRight);
}

// ---- a form over a screen -----------------------------------------------------------------------

void drawForm(ImDrawList *dl, ImVec2 S, const FormView &v, float appear)
{
	const Palette &p = P();
	const float a = easeOut(appear);
	dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, static_cast<int>(140 * a)));
	const float w = px(780.0f);
	const float x0 = S.x - px(96.0f) - w + px(60.0f) * (1.0f - a), y0 = px(110.0f);
	float h = px(110.0f);
	for (const SetRow &r : v.rows) {
		h += rowHeight(r, w - px(40.0f), false) + px(4.0f);
	}
	if (!v.note.empty() || !v.status.empty()) {
		h += px(70.0f);
	}
	h = std::min(h, S.y - y0 - px(120.0f));
	const ImVec2 p0(x0, y0), p1(x0 + w, y0 + h);
	shadow(dl, p0, p1, px(18.0f), px(50.0f), 0.6f * a);
	panel(dl, p0, p1, px(18.0f), alpha(p.surface, a), IM_COL32(255, 255, 255, static_cast<int>(26 * a)));
	text(dl, F::Title, px(30.0f), ImVec2(p0.x + px(36.0f), p0.y + px(30.0f)), alpha(p.text, a), v.title.c_str());
	float y = p0.y + px(90.0f);
	const float listBottom = p1.y - ((!v.note.empty() || !v.status.empty()) ? px(78.0f) : px(20.0f));
	drawRows(dl, ImVec2(p0.x + px(14.0f), y), ImVec2(p1.x - px(14.0f), listBottom), v.rows, v.sel, true, "form." + v.title);
	y = listBottom + px(10.0f);
	if (!v.status.empty()) {
		if (v.busy) {
			spinner(dl, ImVec2(p0.x + px(52.0f), y + px(24.0f)), px(12.0f), p.accent);
		}
		paragraph(dl, F::Small, px(15.5f), ImVec2(p0.x + (v.busy ? px(76.0f) : px(40.0f)), y + px(10.0f)), v.statusBad ? p.fail : p.textDim,
			v.status, w - px(110.0f), 2);
	} else if (!v.note.empty()) {
		paragraph(dl, F::Small, px(15.5f), ImVec2(p0.x + px(40.0f), y + px(10.0f)), p.textFaint, v.note, w - px(80.0f), 2);
	}
}

// ---- the on-screen keyboard -------------------------------------------------------------------

void Keyboard::open(const std::string &t, const std::string &v, const std::string &ph, Kind k, bool pw, Done d)
{
	title = t;
	text = v;
	placeholder = ph;
	kind = k;
	password = pw;
	done = std::move(d);
	cursor = static_cast<int>(text.size());
	row = k == Kind::Number ? 0 : 1;
	col = 0;
	layer = 0;
	shiftOnce = false;
	openFlag = true;
	openedAt = now();
}

void Keyboard::close(bool ok)
{
	if (!openFlag) {
		return;
	}
	openFlag = false;
	Done d = std::move(done);
	done = nullptr;
	if (d) {
		d(ok, text);
	}
}

std::vector<std::vector<std::string>> Keyboard::layout() const
{
	// specials: BK backspace, SH shift, SYM the symbols, SP space, LT RT the cursor, OK done, NO cancel
	if (layer == 2) {
		return {
			{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "BK" },
			{ "!", "@", "#", "$", "%", "&", "*", "(", ")", "+", "=", "/" },
			{ "[", "]", "{", "}", "<", ">", "^", "~", "`", "|", "\\", ":" },
			{ "SH", "_", "'", "\"", ",", ".", ";", "?", "\xC2\xA3", "\xE2\x82\xAC", "\xC2\xB0", "SYM2" },
			{ "SYM", "SP", "LT", "RT", "NO", "OK" },
		};
	}
	const bool up = layer == 1 || shiftOnce;
	std::vector<std::vector<std::string>> rows = {
		{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "BK" },
		{ "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "/", ":" },
		{ "a", "s", "d", "f", "g", "h", "j", "k", "l", "@", ".", "_" },
		{ "SH", "z", "x", "c", "v", "b", "n", "m", ",", ";", "'", "?" },
		{ "SYM", "SP", "LT", "RT", "NO", "OK" },
	};
	if (up) {
		for (auto &r : rows) {
			for (std::string &k : r) {
				if (k.size() == 1 && k[0] >= 'a' && k[0] <= 'z') {
					k[0] = static_cast<char>(k[0] - 32);
				}
			}
		}
	}
	return rows;
}

namespace {

// a key's width in units (a letter is 1)
float keyUnits(const std::string &k, size_t rowSize)
{
	if (rowSize > 6) {
		return 1.0f;
	}
	if (k == "SP") {
		return 4.0f;
	}
	if (k == "SYM" || k == "OK") {
		return 2.0f;
	}
	if (k == "NO") {
		return 2.0f;
	}
	return 1.0f;
}

std::vector<float> keyCentres(const std::vector<std::string> &r)
{
	float total = 0.0f;
	for (const std::string &k : r) {
		total += keyUnits(k, r.size());
	}
	const float scale = 12.0f / total;
	std::vector<float> c;
	float x = 0.0f;
	for (const std::string &k : r) {
		const float w = keyUnits(k, r.size()) * scale;
		c.push_back(x + w * 0.5f);
		x += w;
	}
	return c;
}

} // namespace

void Keyboard::press(const std::string &k)
{
	if (k == "BK") {
		if (cursor > 0) {
			// a whole UTF-8 character
			int at = cursor - 1;
			while (at > 0 && (static_cast<unsigned char>(text[static_cast<size_t>(at)]) & 0xC0) == 0x80) {
				at--;
			}
			text.erase(static_cast<size_t>(at), static_cast<size_t>(cursor - at));
			cursor = at;
		}
	} else if (k == "SH") {
		if (layer == 2) {
			layer = 0;
		} else if (layer == 1) {
			layer = 0;
			shiftOnce = false;
		} else if (shiftOnce) {
			layer = 1;   // twice: caps lock
			shiftOnce = false;
		} else {
			shiftOnce = true;
		}
	} else if (k == "SYM" || k == "SYM2") {
		layer = layer == 2 ? 0 : 2;
		shiftOnce = false;
	} else if (k == "SP") {
		text.insert(static_cast<size_t>(cursor), " ");
		cursor++;
	} else if (k == "LT") {
		while (cursor > 0) {
			cursor--;
			if ((static_cast<unsigned char>(text[static_cast<size_t>(cursor)]) & 0xC0) != 0x80) {
				break;
			}
		}
	} else if (k == "RT") {
		while (cursor < static_cast<int>(text.size())) {
			cursor++;
			if (cursor >= static_cast<int>(text.size()) || (static_cast<unsigned char>(text[static_cast<size_t>(cursor)]) & 0xC0) != 0x80) {
				break;
			}
		}
	} else if (k == "OK") {
		close(true);
	} else if (k == "NO") {
		close(false);
	} else {
		text.insert(static_cast<size_t>(cursor), k);
		cursor += static_cast<int>(k.size());
		if (shiftOnce) {
			shiftOnce = false;
		}
	}
}

void Keyboard::input(const Pad &pad, const std::string &typed)
{
	if (!openFlag) {
		return;
	}
	// a USB keyboard's typing goes straight in
	for (size_t i = 0; i < typed.size(); i++) {
		const char c = typed[i];
		if (c == '\b') {
			press("BK");
		} else if (c == '\n') {
			close(true);
			return;
		} else if (c == '\x1b') {
			close(false);
			return;
		} else if (c == '\x02') {
			press("LT");
		} else if (c == '\x03') {
			press("RT");
		} else if (static_cast<unsigned char>(c) >= 0x20) {
			size_t n = 1;
			const unsigned char u = static_cast<unsigned char>(c);
			n = u < 0x80 ? 1 : u < 0xE0 ? 2 : u < 0xF0 ? 3 : 4;
			text.insert(static_cast<size_t>(cursor), typed.substr(i, n));
			cursor += static_cast<int>(n);
			i += n - 1;
		}
	}
	if (now() - openedAt < 0.15) {
		return;   // (the press that opened it is not a key)
	}
	const auto rows = layout();
	row = std::max(0, std::min(static_cast<int>(rows.size()) - 1, row));
	col = std::max(0, std::min(static_cast<int>(rows[static_cast<size_t>(row)].size()) - 1, col));
	if (pad.left) {
		col = (col + static_cast<int>(rows[static_cast<size_t>(row)].size()) - 1) % static_cast<int>(rows[static_cast<size_t>(row)].size());
	}
	if (pad.right) {
		col = (col + 1) % static_cast<int>(rows[static_cast<size_t>(row)].size());
	}
	if (pad.up || pad.down) {
		const int to = (row + (pad.down ? 1 : -1) + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
		const float cx = keyCentres(rows[static_cast<size_t>(row)])[static_cast<size_t>(col)];
		const std::vector<float> cs = keyCentres(rows[static_cast<size_t>(to)]);
		int best = 0;
		for (size_t i = 0; i < cs.size(); i++) {
			if (std::fabs(cs[i] - cx) < std::fabs(cs[static_cast<size_t>(best)] - cx)) {
				best = static_cast<int>(i);
			}
		}
		row = to;
		col = best;
	}
	if (pad.cross) {
		press(rows[static_cast<size_t>(row)][static_cast<size_t>(col)]);
	}
	if (!openFlag) {
		return;
	}
	if (pad.square) {
		press("SP");
	}
	if (pad.triangle) {
		press("BK");
	}
	if (pad.l2) {
		press("SH");
	}
	if (pad.r2) {
		press("SYM");
	}
	if (pad.l1) {
		press("LT");
	}
	if (pad.r1) {
		press("RT");
	}
	if (pad.options) {
		close(true);
	} else if (pad.circle) {
		close(true);
	}
}

void Keyboard::draw(ImDrawList *dl, ImVec2 S, bool usbKeyboard)
{
	if (!openFlag) {
		return;
	}
	const Palette &p = P();
	float &appear = ui::value("kb.appear", 0.0f);
	if (now() - openedAt < 0.02) {
		appear = 0.0f;
	}
	appear = approach(appear, 1.0f, 14.0f);
	const float a = easeOut(appear);
	const float w = px(940.0f);
	const float keyH = px(60.0f), g = px(8.0f);
	const float h = px(100.0f) + keyH * 5.0f + g * 4.0f + px(64.0f);
	const ImVec2 p0(px(96.0f), S.y - px(120.0f) - h + px(40.0f) * (1.0f - a)), p1(p0.x + w, p0.y + h);
	shadow(dl, p0, p1, px(18.0f), px(46.0f), 0.65f * a);
	panel(dl, p0, p1, px(18.0f), alpha(p.surface, a), IM_COL32(255, 255, 255, static_cast<int>(26 * a)));
	// what is being typed
	label(dl, px(13.0f), ImVec2(p0.x + px(28.0f), p0.y + px(24.0f)), alpha(p.textFaint, a), title);
	field(dl, ImVec2(p0.x + px(28.0f), p0.y + px(46.0f)), ImVec2(p1.x - px(28.0f), p0.y + px(92.0f)), text, placeholder, true, password, cursor);
	// the keys
	const auto rows = layout();
	const float kx0 = p0.x + px(28.0f), kw = w - px(56.0f);
	const float unit = (kw - g * 11.0f) / 12.0f;
	float y = p0.y + px(108.0f);
	float &selX = ui::value("kb.selx", 0.0f), &selY = ui::value("kb.sely", 0.0f), &selW = ui::value("kb.selw", 0.0f);
	bool selSet = false;
	for (size_t r = 0; r < rows.size(); r++) {
		float total = 0.0f;
		for (const std::string &k : rows[r]) {
			total += keyUnits(k, rows[r].size());
		}
		const float scale = 12.0f / total;
		float x = kx0;
		for (size_t c = 0; c < rows[r].size(); c++) {
			const std::string &k = rows[r][c];
			const float units = keyUnits(k, rows[r].size()) * scale;
			const float bw = unit * units + g * (units - 1.0f);
			const bool sel = static_cast<int>(r) == row && static_cast<int>(c) == col;
			const bool special = k.size() > 1 && (k == "BK" || k == "SH" || k == "SYM" || k == "SYM2" || k == "SP" || k == "LT" || k == "RT" ||
				k == "OK" || k == "NO");
			ImU32 fill = special ? IM_COL32(255, 255, 255, 16) : IM_COL32(255, 255, 255, 26);
			if (k == "OK") {
				fill = alpha(p.accent, 0.22f);
			}
			if (k == "SH" && (layer == 1 || shiftOnce)) {
				fill = alpha(p.accent, 0.3f);
			}
			dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + keyH), alpha(fill, a), px(10.0f));
			if (sel) {
				if (selW <= 0.0f) {
					selX = x;
					selY = y;
					selW = bw;
				}
				selSet = true;
				selX = approach(selX, x, 26.0f);
				selY = approach(selY, y, 26.0f);
				selW = approach(selW, bw, 26.0f);
			}
			std::string cap = k;
			F f = F::Body;
			float fs = px(23.0f);
			if (k == "BK") {
				cap = "\xE2\x86\x90";   // drawn as an arrow
			} else if (k == "SH") {
				cap = layer == 1 ? "CAPS" : "SHIFT";
			} else if (k == "SYM" || k == "SYM2") {
				cap = layer == 2 ? "ABC" : "#+=";
			} else if (k == "SP") {
				cap = "space";
			} else if (k == "LT") {
				cap = "\xE2\x86\x90";
			} else if (k == "RT") {
				cap = "\xE2\x86\x92";
			} else if (k == "OK") {
				cap = "done";
			} else if (k == "NO") {
				cap = "cancel";
			}
			if (special && cap.size() > 3) {
				f = F::Small;
				fs = px(15.5f);
			}
			const ImU32 col = k == "OK" ? p.accentText : k == "NO" ? p.warnText : special ? p.textDim : p.text;
			const ImVec2 ts = measure(f, fs, cap.c_str());
			ui::text(dl, f, fs, ImVec2(x + (bw - ts.x) * 0.5f, y + (keyH - ts.y) * 0.5f), alpha(col, a), cap.c_str());
			if (k == "BK") {
				// a backspace: a small box behind the arrow
				dl->AddRect(ImVec2(x + bw * 0.5f - px(14.0f), y + keyH * 0.5f - px(11.0f)), ImVec2(x + bw * 0.5f + px(18.0f), y + keyH * 0.5f + px(11.0f)),
					alpha(p.textFaint, a), px(4.0f), ImDrawCornerFlags_All, px(1.5f));
			}
			x += bw + g;
		}
		y += keyH + g;
	}
	if (selSet) {
		dl->AddRect(ImVec2(selX - px(3.0f), selY - px(3.0f)), ImVec2(selX + selW + px(3.0f), selY + keyH + px(3.0f)), alpha(p.accent, a), px(12.0f),
			ImDrawCornerFlags_All, px(3.0f));
	}
	// the buttons
	const std::vector<Hint> hints = { { "X", "type" }, { "S", "space" }, { "T", "delete" }, { "L2", "shift" }, { "R2", "symbols" },
		{ "L1 R1", "move" }, { "O", "done" } };
	float hx = p0.x + px(28.0f);
	const float hy = p1.y - px(48.0f);
	for (const Hint &hn : hints) {
		hx += hint(dl, ImVec2(hx, hy), hn, alpha(p.textFaint, a)) + px(22.0f);
	}
	if (usbKeyboard) {
		const char *s = "USB keyboard: type away";
		const ImVec2 ts = measure(F::Small, px(15.0f), s);
		ui::text(dl, F::Small, px(15.0f), ImVec2(p1.x - px(28.0f) - ts.x, hy + px(6.0f)), alpha(p.accentText, a), s);
	}
}

// ---- the game page -----------------------------------------------------------------------------

void drawGamePage(ImDrawList *dl, ImVec2 S, const GameView &v, float appear)
{
	const Palette &p = P();
	const float a = easeOut(appear);
	const float L = px(96.0f);
	// the cover, large
	const float cw = px(460.0f), ch = px(644.0f);
	const ImVec2 c0(L - px(40.0f) * (1.0f - a), px(130.0f)), c1(c0.x + cw, c0.y + ch);
	if (v.cover) {
		shadow(dl, c0, c1, px(14.0f), px(60.0f), 0.7f * a);
		imageFill(dl, v.cover, c0, c1, px(14.0f), IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
	} else {
		HomeGame g;
		g.title = v.coverTitle;
		g.tint = v.tint;
		drawCover(dl, g, Tex{}, c0, c1, 1.0f, a, true);
	}
	// the details
	const float x = c1.x + px(64.0f), w = S.x - L - x;
	float y = px(120.0f) + px(20.0f) * (1.0f - a);
	float fs = px(56.0f);
	for (; fs > px(34.0f); fs -= px(4.0f)) {
		if (paragraph(nullptr, F::Display, fs, ImVec2(0, 0), 0, v.title, w, 9, 1.05f) <= fs * 2.2f) {
			break;
		}
	}
	y += paragraph(dl, F::Display, fs, ImVec2(x, y), alpha(p.text, a), v.title, w, 2, 1.05f) + px(18.0f);
	float cx = x;
	for (const auto &c : v.chips) {
		const ImU32 fill = c.second == 1 ? p.accentSoft : c.second == 2 ? p.warnSoft : IM_COL32(255, 255, 255, 26);
		const ImU32 col = c.second == 1 ? p.accentText : c.second == 2 ? p.warnText : p.textDim;
		cx += chip(dl, ImVec2(cx, y), c.first, alpha(fill, a), alpha(col, a)) + px(10.0f);
	}
	y += px(34.0f) + px(34.0f);
	// the facts in a row of four
	if (!v.facts.empty()) {
		const float fw = w / static_cast<float>(std::max<size_t>(4, v.facts.size()));
		for (size_t i = 0; i < v.facts.size(); i++) {
			const float fx = x + fw * static_cast<float>(i);
			label(dl, px(13.0f), ImVec2(fx, y), alpha(p.textFaint, a), v.facts[i].label);
			paragraph(dl, F::Body, px(19.0f), ImVec2(fx, y + px(24.0f)), alpha(p.text, a), v.facts[i].value, fw - px(24.0f), 2);
		}
		y += px(96.0f);
	}
	// 0.0.3: its description, four lines at most, and where it is from
	if (!v.about.empty()) {
		const int lines = v.states.empty() ? 5 : 3;
		y += paragraph(dl, F::Body, px(17.5f), ImVec2(x, y - px(18.0f)), alpha(p.textDim, a), v.about, std::min(w, px(1100.0f)), lines, 1.35f);
		text(dl, F::Small, px(13.5f), ImVec2(x, y - px(10.0f)), alpha(p.textFaint, a), v.aboutSource.c_str());
		y += px(30.0f);
	}
	// the save states, as pictures
	if (!v.states.empty()) {
		text(dl, F::Heading, px(21.0f), ImVec2(x, y), alpha(p.text, a), "Save states");
		const char *howto = v.stateSel >= 0 ? "X load   T save here   O back" : "";
		if (*howto) {
			text(dl, F::Small, px(15.5f), ImVec2(x + px(150.0f), y + px(3.0f)), alpha(p.textFaint, a), howto);
		}
		y += px(38.0f);
		const float sw = px(220.0f), sh = px(140.0f), sg = px(16.0f);
		const int n = static_cast<int>(v.states.size());
		const float visible = std::floor((w + sg) / (sw + sg));
		float &ss = value("gp.statescroll", 0.0f);
		const int sel = std::max(0, v.stateSel);
		float target = ss;
		if (static_cast<float>(sel) < target) {
			target = static_cast<float>(sel);
		} else if (static_cast<float>(sel) > target + visible - 1.0f) {
			target = static_cast<float>(sel) - visible + 1.0f;
		}
		target = std::max(0.0f, std::min(std::max(0.0f, static_cast<float>(n) - visible), target));
		ss = approach(ss, target, 14.0f);
		dl->PushClipRect(ImVec2(x - px(10.0f), y - px(10.0f)), ImVec2(S.x - L + px(10.0f), y + sh + px(10.0f)), true);
		for (int i = 0; i < n; i++) {
			const GameState &st = v.states[static_cast<size_t>(i)];
			const float sx = x + (static_cast<float>(i) - ss) * (sw + sg);
			if (sx > S.x || sx + sw < x - sw) {
				continue;
			}
			const ImVec2 s0(sx, y), s1(sx + sw, y + sh);
			if (st.empty) {
				dl->AddRect(s0, s1, IM_COL32(255, 255, 255, static_cast<int>(46 * a)), px(10.0f), ImDrawCornerFlags_All, px(1.5f));
				const ImVec2 ts = measure(F::Small, px(15.0f), st.label.c_str());
				text(dl, F::Small, px(15.0f), ImVec2(sx + (sw - ts.x) * 0.5f, y + (sh - ts.y) * 0.5f), alpha(p.textFaint, a), st.label.c_str());
			} else {
				if (st.picture) {
					imageFill(dl, st.picture, s0, s1, px(10.0f), IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
				} else {
					dl->AddRectFilled(s0, s1, alpha(p.surface2, a), px(10.0f));
				}
				dl->AddRectFilledMultiColor(ImVec2(sx, y + sh * 0.5f), ImVec2(sx + sw, y + sh - px(10.0f)), 0, 0, IM_COL32(0, 0, 0, 170),
					IM_COL32(0, 0, 0, 170));
				dl->AddRectFilled(ImVec2(sx, y + sh - px(10.0f)), s1, IM_COL32(0, 0, 0, 170), px(10.0f),
					ImDrawCornerFlags_BotLeft | ImDrawCornerFlags_BotRight);
				textFit(dl, F::Small, px(14.0f), ImVec2(sx + px(12.0f), y + sh - px(28.0f)), alpha(p.text, a), st.label, sw - px(24.0f));
			}
			if (i == v.stateSel) {
				ring(dl, s0, s1, px(10.0f), px(4.0f), alpha(p.accent, a), px(3.0f));
			}
		}
		dl->PopClipRect();
		y += sh + px(36.0f);
	}
	// what to do
	const float rw = std::min(w, px(760.0f)), rh = px(62.0f);
	float &hy = value("gp.actiony", y);
	const float wantY = y + static_cast<float>(std::max(0, v.actionSel)) * (rh + px(4.0f));
	hy = approach(hy, wantY, 18.0f);
	if (v.stateSel < 0 && !v.actions.empty()) {
		dl->AddRectFilled(ImVec2(x - px(22.0f), hy), ImVec2(x - px(22.0f) + rw, hy + rh), IM_COL32(255, 255, 255, static_cast<int>(30 * a)), px(10.0f));
		dl->AddRect(ImVec2(x - px(22.0f), hy), ImVec2(x - px(22.0f) + rw, hy + rh), alpha(p.accent, 0.75f * a), px(10.0f), ImDrawCornerFlags_All, px(2.0f));
	}
	for (size_t i = 0; i < v.actions.size(); i++) {
		const float ry = y + static_cast<float>(i) * (rh + px(4.0f));
		const float nfs = px(20.0f);
		text(dl, F::Heading, nfs, ImVec2(x, ry + (rh - measure(F::Heading, nfs, "Ag").y) * 0.5f), alpha(p.text, a), v.actions[i].name.c_str());
		if (!v.actions[i].sub.empty()) {
			const float sfs = px(15.5f);
			const float maxW = rw - px(60.0f) - measure(F::Heading, nfs, v.actions[i].name.c_str()).x;
			const float sw = std::min(measure(F::Small, sfs, v.actions[i].sub.c_str()).x, maxW);
			textFit(dl, F::Small, sfs, ImVec2(x - px(22.0f) + rw - px(22.0f) - sw, ry + (rh - measure(F::Small, sfs, "Ag").y) * 0.5f),
				alpha(p.textFaint, a), v.actions[i].sub, maxW);
		}
	}
	footer(dl, S, v.footer);
}

// ---- a choice over a screen, and a message -----------------------------------------------------

void drawPopup(ImDrawList *dl, ImVec2 S, const PopupView &v, float appear)
{
	const Palette &p = P();
	const float a = easeOut(appear);
	dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, static_cast<int>(150 * a)));
	const float w = px(720.0f);
	const float tw = w - px(80.0f);
	const float textH = v.text.empty() ? 0.0f : paragraph(nullptr, F::Body, px(18.5f), ImVec2(0, 0), 0, v.text, tw, 8) + px(22.0f);
	const float rh = px(58.0f);
	const float h = px(96.0f) + textH + rh * static_cast<float>(v.items.size()) + px(76.0f);
	const ImVec2 p0((S.x - w) * 0.5f, (S.y - h) * 0.5f + px(24.0f) * (1.0f - a)), p1(p0.x + w, p0.y + h);
	shadow(dl, p0, p1, px(20.0f), px(50.0f), 0.7f * a);
	panel(dl, p0, p1, px(20.0f), alpha(p.surface, a), IM_COL32(255, 255, 255, static_cast<int>(26 * a)));
	text(dl, F::Title, px(28.0f), ImVec2(p0.x + px(40.0f), p0.y + px(34.0f)), alpha(p.text, a), v.title.c_str());
	float y = p0.y + px(92.0f);
	if (!v.text.empty()) {
		paragraph(dl, F::Body, px(18.5f), ImVec2(p0.x + px(40.0f), y), alpha(p.textDim, a), v.text, tw, 8);
		y += textH;
	}
	float &hy = value("popup.y", y);
	const float want = y + static_cast<float>(std::max(0, v.sel)) * rh;
	if (appear < 0.2f) {
		hy = want;
	}
	hy = approach(hy, want, 22.0f);
	dl->AddRectFilled(ImVec2(p0.x + px(24.0f), hy), ImVec2(p1.x - px(24.0f), hy + rh - px(6.0f)), alpha(p.accent, a), px(12.0f));
	for (size_t i = 0; i < v.items.size(); i++) {
		const bool on = static_cast<int>(i) == v.sel;
		const float iy = y + static_cast<float>(i) * rh;
		const float fs = px(20.0f);
		text(dl, F::Heading, fs, ImVec2(p0.x + px(44.0f), iy + (rh - px(6.0f) - measure(F::Heading, fs, "Ag").y) * 0.5f),
			alpha(on ? p.accentInk : p.text, a), v.items[i].c_str());
	}
	float hx = p0.x + px(40.0f);
	for (const Hint &hn : std::vector<Hint>{ { "X", "choose" }, { "O", "back" } }) {
		hx += hint(dl, ImVec2(hx, p1.y - px(54.0f)), hn, alpha(p.textFaint, a)) + px(28.0f);
	}
}

void drawToast(ImDrawList *dl, ImVec2 S, const std::string &s, float a)
{
	if (s.empty() || a <= 0.0f) {
		return;
	}
	const Palette &p = P();
	const float fs = px(18.0f);
	const float maxW = std::min(S.x - px(200.0f), px(1100.0f));
	const float th = paragraph(nullptr, F::Body, fs, ImVec2(0, 0), 0, s, maxW, 3);
	const float tw = std::min(maxW, measure(F::Body, fs, s.c_str()).x);
	const float w = tw + px(64.0f), h = th + px(36.0f);
	const float y = px(40.0f) - px(20.0f) * (1.0f - easeOut(std::min(1.0f, a * 2.0f)));
	const ImVec2 p0((S.x - w) * 0.5f, y), p1(p0.x + w, y + h);
	shadow(dl, p0, p1, h * 0.5f, px(30.0f), 0.6f * a);
	panel(dl, p0, p1, std::min(h * 0.5f, px(24.0f)), alpha(p.surface2, a), IM_COL32(255, 255, 255, static_cast<int>(30 * a)));
	dl->AddCircleFilled(ImVec2(p0.x + px(26.0f), p0.y + h * 0.5f), px(4.5f), alpha(p.accent, a), 10);
	paragraph(dl, F::Body, fs, ImVec2(p0.x + px(42.0f), p0.y + px(17.0f)), alpha(p.text, a), s, maxW, 3);
}

// ---- colours -----------------------------------------------------------------------------------

ImU32 tintFor(const std::string &name)
{
	static const ImU32 tints[] = {
		IM_COL32(31, 111, 74, 255), IM_COL32(43, 51, 64, 255), IM_COL32(58, 42, 82, 255), IM_COL32(74, 58, 30, 255),
		IM_COL32(35, 58, 74, 255), IM_COL32(86, 38, 44, 255), IM_COL32(28, 70, 92, 255), IM_COL32(70, 48, 26, 255),
	};
	return tints[hashOf(name) % (sizeof(tints) / sizeof(tints[0]))];
}

ImU32 tintOf(const uint8_t *rgba, int w, int h)
{
	if (!rgba || w <= 0 || h <= 0) {
		return 0;
	}
	// the average of the saturated pixels (a box's colour, not its black or white)
	double r = 0, g = 0, b = 0, n = 0;
	for (int y = 0; y < h; y += std::max(1, h / 48)) {
		for (int x = 0; x < w; x += std::max(1, w / 48)) {
			const uint8_t *q = rgba + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
			const int mx = std::max(q[0], std::max(q[1], q[2])), mn = std::min(q[0], std::min(q[1], q[2]));
			const double wgt = 0.15 + static_cast<double>(mx - mn) / 255.0;
			r += q[0] * wgt;
			g += q[1] * wgt;
			b += q[2] * wgt;
			n += wgt;
		}
	}
	if (n <= 0.0) {
		return 0;
	}
	r /= n;
	g /= n;
	b /= n;
	// deepened: as a light behind dark words
	const double mx = std::max(r, std::max(g, b));
	const double k = mx > 1.0 ? 150.0 / mx : 1.0;
	return IM_COL32(static_cast<int>(std::min(255.0, r * k)), static_cast<int>(std::min(255.0, g * k)), static_cast<int>(std::min(255.0, b * k)), 255);
}

std::vector<uint8_t> shrink(const uint8_t *rgba, int w, int h, int tw, int th)
{
	std::vector<uint8_t> out(static_cast<size_t>(tw * th * 4), 0);
	if (!rgba || w <= 0 || h <= 0) {
		return out;
	}
	for (int y = 0; y < th; y++) {
		const int y0 = y * h / th, y1 = std::max(y0 + 1, (y + 1) * h / th);
		for (int x = 0; x < tw; x++) {
			const int x0 = x * w / tw, x1 = std::max(x0 + 1, (x + 1) * w / tw);
			uint64_t s[4]{};
			uint64_t n = 0;
			for (int yy = y0; yy < y1; yy += std::max(1, (y1 - y0) / 8)) {
				for (int xx = x0; xx < x1; xx += std::max(1, (x1 - x0) / 8)) {
					const uint8_t *q = rgba + (static_cast<size_t>(yy) * static_cast<size_t>(w) + static_cast<size_t>(xx)) * 4;
					for (int c = 0; c < 4; c++) {
						s[c] += q[c];
					}
					n++;
				}
			}
			uint8_t *o = &out[static_cast<size_t>((y * tw + x) * 4)];
			for (int c = 0; c < 4; c++) {
				o[c] = static_cast<uint8_t>(n ? s[c] / n : 0);
			}
			o[3] = 255;
		}
	}
	return out;
}

} // namespace ui
