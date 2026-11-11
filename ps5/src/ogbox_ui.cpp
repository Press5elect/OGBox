/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The menus' look (ogbox_ui.h): the tokens, type, motion, and the pieces every screen is made of.
 */
#include "ogbox_ui.h"
#include "ogbox_ui_internal.h"

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
	// a cool near-black, glass surfaces, the Xbox's green as the one accent
	p.bg = rgba(8, 10, 13);
	p.surface = rgba(20, 24, 30);
	p.surface2 = rgba(30, 36, 44);
	p.surfaceHi = rgba(255, 255, 255, 28);
	p.border = rgba(255, 255, 255, 22);
	p.text = rgba(244, 246, 248);
	p.textDim = rgba(200, 206, 214);
	p.textFaint = rgba(128, 139, 152);
	p.accent = rgba(107, 214, 79);
	p.accentInk = rgba(8, 20, 6);
	p.accentSoft = rgba(107, 214, 79, 40);
	p.accentText = rgba(168, 236, 146);
	p.warn = rgba(232, 168, 72);
	p.warnSoft = rgba(232, 168, 72, 32);
	p.warnText = rgba(240, 202, 140);
	p.fail = rgba(255, 112, 98);
	p.ok = rgba(107, 214, 79);
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
		p.ok = p.accent;
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

} // namespace

// ---- shared with the screens' files (ogbox_ui_internal.h) ----------------------------------------

namespace detail {

void ring(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float r, float off, ImU32 col, float t)
{
	dl->AddRect(ImVec2(p0.x - off, p0.y - off), ImVec2(p1.x + off, p1.y + off), col, r + off, ImDrawCornerFlags_All, t);
}

void imageFill(ImDrawList *dl, const Tex &t, ImVec2 p0, ImVec2 p1, float r, ImU32 col)
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

Tex grad()
{
	ensureTextures();
	return g_grad;
}

void jumpLetter(ImDrawList *dl, ImVec2 c, char letter, float a)
{
	if (!letter || a <= 0.0f) {
		return;
	}
	const char t[2] = { letter, 0 };
	const float box = px(150.0f);
	shadow(dl, ImVec2(c.x - box * 0.5f, c.y - box * 0.5f), ImVec2(c.x + box * 0.5f, c.y + box * 0.5f), px(24.0f), px(30.0f), 0.5f * a);
	dl->AddRectFilled(ImVec2(c.x - box * 0.5f, c.y - box * 0.5f), ImVec2(c.x + box * 0.5f, c.y + box * 0.5f), alpha(P().surface, 0.94f * a),
		px(24.0f));
	const ImVec2 ts = measure(F::Display, px(90.0f), t);
	text(dl, F::Display, px(90.0f), ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), alpha(P().accent, a), t);
}

float fitSize(F f, const std::string &s, float w, float max, float min, int lines, float lineGap)
{
	float fs = max;
	for (; fs > min; fs -= px(2.0f)) {
		if (paragraph(nullptr, f, fs, ImVec2(0, 0), 0, s, w, 99, lineGap) <= fs * (static_cast<float>(lines) * lineGap + 0.2f)) {
			break;
		}
	}
	return std::max(fs, min);
}

} // namespace detail

using detail::ring;
using detail::imageFill;

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

namespace {
float g_scrollIn = 0.0f;
}

void setScrollInput(float y)
{
	g_scrollIn = std::fabs(y) < 0.2f ? 0.0f : y;
}

float scrollParagraph(ImDrawList *dl, F f, float size, ImVec2 p, ImU32 col, const std::string &s, float w, float maxH,
	const std::string &key, float lineGap)
{
	const float full = paragraph(nullptr, f, size, ImVec2(0, 0), 0, s, w, 9999, lineGap);
	if (full <= maxH + 0.5f) {
		if (dl) {
			paragraph(dl, f, size, p, col, s, w, 9999, lineGap);
		}
		return full;
	}
	if (!dl) {
		return maxH;
	}
	// the scroll, kept by key and the text's length (another text starts at its top)
	float &at = value("scroll." + key + "." + std::to_string(s.size()), 0.0f);
	at = std::max(0.0f, std::min(full - maxH, at + g_scrollIn * dt() * px(700.0f)));
	dl->PushClipRect(p, ImVec2(p.x + w + px(4.0f), p.y + maxH), true);
	paragraph(dl, f, size, ImVec2(p.x, p.y - at), col, s, w, 9999, lineGap);
	dl->PopClipRect();
	// a thin bar at the right: where in the text this is
	const float barX = p.x + w + px(10.0f);
	const float th = std::max(px(24.0f), maxH * maxH / full);
	const float ty = p.y + (maxH - th) * (at / (full - maxH));
	dl->AddRectFilled(ImVec2(barX, p.y), ImVec2(barX + px(3.0f), p.y + maxH), IM_COL32(255, 255, 255, 18), px(2.0f));
	dl->AddRectFilled(ImVec2(barX, ty), ImVec2(barX + px(3.0f), ty + th), col, px(2.0f));
	return maxH;
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
	} else if (name == "U") {
		word = "\xE2\x86\x91";
	} else if (name == "D") {
		word = "\xE2\x86\x93";
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

static bool g_swapConfirm = false;
static bool g_listEdge = false;

void setSwapConfirm(bool on)
{
	g_swapConfirm = on;
}

void listEdge()
{
	g_listEdge = true;
}

bool takeListEdge()
{
	const bool was = g_listEdge;
	g_listEdge = false;
	return was;
}

/* A hint's or a button's key as the menus' buttons are (Circle confirms: X and O swapped). */
static std::string menuKey(const std::string &k)
{
	if (!g_swapConfirm) {
		return k;
	}
	return k == "X" ? std::string("O") : k == "O" ? std::string("X") : k;
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
		const std::string k = menuKey(keys.substr(at, sp - at));
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
	case Icon::Play:
		dl->AddTriangleFilled(ImVec2(c.x - h * 0.45f, c.y - h * 0.7f), ImVec2(c.x + h * 0.75f, c.y), ImVec2(c.x - h * 0.45f, c.y + h * 0.7f), col);
		break;
	case Icon::Restart:
		dl->PathArcTo(c, h * 0.68f, -2.4f, 2.0f, 24);
		dl->PathStroke(col, false, t);
		dl->AddTriangleFilled(ImVec2(c.x - h * 0.78f, c.y - h * 0.86f), ImVec2(c.x - h * 0.18f, c.y - h * 0.5f),
			ImVec2(c.x - h * 0.86f, c.y - h * 0.2f), col);
		break;
	case Icon::Save:
		dl->AddLine(ImVec2(c.x, c.y - h * 0.8f), ImVec2(c.x, c.y + h * 0.25f), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.42f, c.y - h * 0.15f), ImVec2(c.x, c.y + h * 0.27f), col, t);
		dl->AddLine(ImVec2(c.x + h * 0.42f, c.y - h * 0.15f), ImVec2(c.x, c.y + h * 0.27f), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.8f, c.y + h * 0.75f), ImVec2(c.x + h * 0.8f, c.y + h * 0.75f), col, t);
		break;
	case Icon::Load:
		dl->AddLine(ImVec2(c.x, c.y + h * 0.3f), ImVec2(c.x, c.y - h * 0.75f), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.42f, c.y - h * 0.33f), ImVec2(c.x, c.y - h * 0.77f), col, t);
		dl->AddLine(ImVec2(c.x + h * 0.42f, c.y - h * 0.33f), ImVec2(c.x, c.y - h * 0.77f), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.8f, c.y + h * 0.75f), ImVec2(c.x + h * 0.8f, c.y + h * 0.75f), col, t);
		break;
	case Icon::Puzzle:
		dl->AddRect(ImVec2(c.x - h * 0.7f, c.y - h * 0.5f), ImVec2(c.x + h * 0.5f, c.y + h * 0.7f), col, h * 0.12f, ImDrawCornerFlags_All, t);
		dl->AddCircle(ImVec2(c.x - h * 0.1f, c.y - h * 0.62f), h * 0.2f, col, 12, t);
		dl->AddCircle(ImVec2(c.x + h * 0.62f, c.y + h * 0.1f), h * 0.2f, col, 12, t);
		break;
	case Icon::Trash:
		dl->AddLine(ImVec2(c.x - h * 0.8f, c.y - h * 0.55f), ImVec2(c.x + h * 0.8f, c.y - h * 0.55f), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.25f, c.y - h * 0.8f), ImVec2(c.x + h * 0.25f, c.y - h * 0.8f), col, t);
		dl->AddRect(ImVec2(c.x - h * 0.58f, c.y - h * 0.55f), ImVec2(c.x + h * 0.58f, c.y + h * 0.8f), col, h * 0.12f,
			ImDrawCornerFlags_Bot, t);
		break;
	case Icon::Back:
		dl->AddLine(ImVec2(c.x + h * 0.7f, c.y), ImVec2(c.x - h * 0.6f, c.y), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.1f, c.y - h * 0.5f), ImVec2(c.x - h * 0.62f, c.y), col, t);
		dl->AddLine(ImVec2(c.x - h * 0.1f, c.y + h * 0.5f), ImVec2(c.x - h * 0.62f, c.y), col, t);
		break;
	case Icon::Info:
		dl->AddCircle(c, h * 0.85f, col, 32, t);
		dl->AddLine(ImVec2(c.x, c.y - h * 0.1f), ImVec2(c.x, c.y + h * 0.45f), col, t * 1.2f);
		dl->AddCircleFilled(ImVec2(c.x, c.y - h * 0.4f), t * 0.9f, col, 8);
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

float chipKind(ImDrawList *dl, ImVec2 p, const std::string &s, int kind, float a, bool measureOnly)
{
	const Palette &pl = P();
	const ImU32 fill = kind == 1 ? pl.accentSoft : kind == 2 ? pl.warnSoft : IM_COL32(255, 255, 255, 24);
	const ImU32 col = kind == 1 ? pl.accentText : kind == 2 ? pl.warnText : pl.textDim;
	return chip(dl, p, s, alpha(fill, a), alpha(col, a), measureOnly);
}

float chipRow(ImDrawList *dl, ImVec2 p, const std::vector<std::pair<std::string, int>> &chips, float maxW, float a)
{
	if (chips.empty()) {
		return 0.0f;
	}
	const float h = px(34.0f), gap = px(10.0f);
	float x = p.x, y = p.y;
	for (const auto &c : chips) {
		const float w = chipKind(nullptr, ImVec2(0, 0), c.first, c.second, a, true);
		if (x + w > p.x + maxW && x > p.x) {
			x = p.x;
			y += h + gap;
		}
		chipKind(dl, ImVec2(x, y), c.first, c.second, a);
		x += w + gap;
	}
	return y + h - p.y;
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

void focusBox(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float r, float amount, bool warn)
{
	if (amount <= 0.01f) {
		return;
	}
	dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, static_cast<int>(26 * amount)), r);
	dl->AddRect(p0, p1, alpha(warn ? P().warn : P().accent, 0.9f * amount), r, ImDrawCornerFlags_All, std::max(1.5f, px(2.0f)));
}

void progress(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float f, ImU32 col)
{
	const float h = p1.y - p0.y;
	dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 28), h * 0.5f);
	f = std::max(0.0f, std::min(1.0f, f));
	if (f > 0.0f) {
		dl->AddRectFilled(p0, ImVec2(p0.x + std::max(h, (p1.x - p0.x) * f), p1.y), col ? col : P().accent, h * 0.5f);
	}
}

float button(ImDrawList *dl, ImVec2 p, const Button &b, float focus, float a, bool measureOnly)
{
	const Palette &pl = P();
	const float h = px(kButtonH), fs = px(19.0f), kw = px(30.0f), padX = px(24.0f);
	const bool lead = !b.key.empty() || b.icon != Icon::None;
	const float tw = measure(F::Heading, fs, b.text.c_str()).x;
	const float w = padX + (lead ? kw + px(12.0f) : 0.0f) + tw + padX + px(4.0f);
	if (measureOnly || !dl) {
		return w;
	}
	const ImVec2 p1(p.x + w, p.y + h);
	const float r = px(grid::rMid);
	ImU32 ink;
	if (b.primary) {
		dl->AddRectFilled(p, p1, alpha(pl.accent, a), r);
		ink = pl.accentInk;
	} else {
		dl->AddRectFilled(p, p1, IM_COL32(255, 255, 255, static_cast<int>((14 + 16 * focus) * a)), r);
		dl->AddRect(p, p1, IM_COL32(255, 255, 255, static_cast<int>(48 * a)), r, ImDrawCornerFlags_All, px(1.5f));
		ink = pl.text;
	}
	if (focus > 0.01f) {
		ring(dl, p, p1, r, px(4.0f), alpha(b.primary ? pl.text : pl.accent, focus * a), px(2.5f));
	}
	float x = p.x + padX;
	if (!b.key.empty()) {
		keycap(dl, ImVec2(x, p.y + (h - kw) * 0.5f), kw, menuKey(b.key), alpha(ink, a), b.primary ? alpha(IM_COL32(255, 255, 255, 90), a) : 0);
		x += kw + px(12.0f);
	} else if (b.icon != Icon::None) {
		icon(dl, b.icon, ImVec2(x + kw * 0.5f, p.y + h * 0.5f), px(24.0f), alpha(b.primary ? ink : pl.textDim, a));
		x += kw + px(12.0f);
	}
	text(dl, F::Heading, fs, ImVec2(x, p.y + (h - measure(F::Heading, fs, "Ag").y) * 0.5f), alpha(ink, a), b.text.c_str());
	return w;
}

void head(ImDrawList *dl, ImVec2 S, const Head &h)
{
	const Palette &pl = P();
	const float L = px(grid::margin), y = px(grid::headY), hh = px(grid::headH);
	float x = L;
	if (h.logo) {
		const float lw = hh * h.logo.w / h.logo.h;
		dl->AddImageRounded(h.logo.id, ImVec2(x, y), ImVec2(x + lw, y + hh), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, px(grid::rSmall));
		x += lw + px(28.0f);
	}
	float right = S.x - L;
	{
		const float fs = px(17.0f);
		for (auto it = h.status.rbegin(); it != h.status.rend(); ++it) {
			const ImVec2 ts = measure(F::Small, fs, it->first.c_str());
			right -= ts.x;
			text(dl, F::Small, fs, ImVec2(right, y + (hh - ts.y) * 0.5f), pl.textFaint, it->first.c_str());
			if (it->second) {
				right -= px(18.0f);
				dl->AddCircleFilled(ImVec2(right + px(5.0f), y + hh * 0.5f), px(4.5f), it->second, 12);
			}
			right -= px(30.0f);
		}
	}
	if (!h.title.empty()) {
		const float tfs = px(34.0f);
		const float tw = textFit(dl, F::Display, tfs, ImVec2(x, y + (hh - measure(F::Display, tfs, "Ag").y) * 0.5f), pl.text, h.title,
			right - x);
		x += tw + px(20.0f);
		if (!h.subtitle.empty() && x < right) {
			const float sfs = px(19.0f);
			textFit(dl, F::Body, sfs, ImVec2(x, y + (hh - measure(F::Body, sfs, "Ag").y) * 0.5f + px(2.0f)), pl.textFaint, h.subtitle,
				right - x);
		}
	}
}

void busy(ImDrawList *dl, ImVec2 S, const std::string &s)
{
	if (s.empty()) {
		return;
	}
	const Palette &pl = P();
	dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, 150));
	const float fs = px(24.0f);
	const ImVec2 ts = measure(F::Title, fs, s.c_str());
	const float w = ts.x + px(132.0f), h = px(104.0f);
	const ImVec2 p0((S.x - w) * 0.5f, (S.y - h) * 0.5f), p1(p0.x + w, p0.y + h);
	shadow(dl, p0, p1, px(grid::rLarge), px(40.0f), 0.6f);
	panel(dl, p0, p1, px(grid::rLarge), pl.surface, pl.border);
	spinner(dl, ImVec2(p0.x + px(52.0f), p0.y + h * 0.5f), px(18.0f), pl.accent);
	text(dl, F::Title, fs, ImVec2(p0.x + px(92.0f), p0.y + (h - ts.y) * 0.5f), pl.text, s.c_str());
}

// ---- lists ------------------------------------------------------------------------------------

namespace {

float listRowH(const ListRow &r)
{
	switch (r.kind) {
	case ListRow::Kind::Section:
		return px(50.0f);
	case ListRow::Kind::Note:
		return px(40.0f);
	default:
		break;
	}
	if (r.thumb || r.thumbSlot) {
		return px(80.0f);
	}
	return r.sub.empty() ? px(60.0f) : px(72.0f);
}

void listRow(ImDrawList *dl, const ListRow &r, ImVec2 p0, float w, float h, float hl, bool focused, float a)
{
	const Palette &p = P();
	if (r.kind == ListRow::Kind::Section) {
		label(dl, px(13.0f), ImVec2(p0.x + px(22.0f) + px(28.0f) * static_cast<float>(r.indent), p0.y + px(24.0f)), alpha(p.textFaint, a), r.title);
		return;
	}
	if (r.kind == ListRow::Kind::Note) {
		textFit(dl, F::Small, px(16.0f), ImVec2(p0.x + px(22.0f) + px(28.0f) * static_cast<float>(r.indent), p0.y + px(10.0f)),
			alpha(p.textFaint, a), r.title, w - px(44.0f));
		return;
	}
	const ImVec2 p1(p0.x + w, p0.y + h);
	if (hl > 0.01f) {
		if (focused) {
			focusBox(dl, p0, p1, px(grid::rMid), hl * a, r.danger);
		} else {
			dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, static_cast<int>(16 * hl * a)), px(grid::rMid));
		}
	}
	const float cy = p0.y + h * 0.5f;
	float x = p0.x + px(22.0f) + px(32.0f) * static_cast<float>(r.indent);
	if (r.indent) {
		dl->AddLine(ImVec2(x - px(18.0f), p0.y + px(6.0f)), ImVec2(x - px(18.0f), p1.y - px(6.0f)), alpha(p.border, a), px(2.0f));
	}
	const ImU32 nameCol = alpha(r.danger ? p.fail : r.dim ? p.textFaint : r.accent ? p.accentText : p.text, a);
	if (r.thumb) {
		const float th = h - px(16.0f), tw = th * std::min(1.6f, std::max(0.6f, r.thumb.w / r.thumb.h));
		imageFill(dl, r.thumb, ImVec2(x, p0.y + px(8.0f)), ImVec2(x + tw, p0.y + px(8.0f) + th), px(6.0f),
			IM_COL32(255, 255, 255, static_cast<int>(255 * a * (r.dim ? 0.5f : 1.0f))));
		x += tw + px(18.0f);
	} else if (r.thumbSlot) {
		// a cover's room, a disc in it (as the game list draws a game without one)
		const float th = h - px(16.0f), tw = th / 1.4f;
		const ImVec2 t0(x, p0.y + px(8.0f)), t1(x + tw, p0.y + px(8.0f) + th);
		dl->AddRectFilled(t0, t1, alpha(tintFor(r.title), a), px(6.0f));
		icon(dl, Icon::Disc, ImVec2((t0.x + t1.x) * 0.5f, (t0.y + t1.y) * 0.5f), tw * 0.5f, alpha(IM_COL32(255, 255, 255, 70), a));
		x += tw + px(18.0f);
	} else if (r.icon != Icon::None) {
		icon(dl, r.icon, ImVec2(x + px(15.0f), cy), px(28.0f), alpha(r.danger ? p.fail : r.accent ? p.accent : r.dim ? p.textFaint : p.textDim, a));
		x += px(48.0f);
	}
	// the right: a switch, words, a check
	float right = p1.x - px(22.0f);
	if (r.toggle >= 0) {
		float &k = value("lst.tog." + r.key, r.toggle ? 1.0f : 0.0f);
		k = approach(k, r.toggle ? 1.0f : 0.0f, 16.0f);
		right -= px(56.0f);
		toggle(dl, ImVec2(right, cy - px(16.0f)), k, focused && hl > 0.5f);
		if (r.dim) {
			dl->AddRectFilled(ImVec2(right, cy - px(16.0f)), ImVec2(right + px(56.0f), cy + px(16.0f)), alpha(p.bg, 0.5f * a), px(16.0f));
		}
		right -= px(20.0f);
	} else if (r.current) {
		icon(dl, Icon::Check, ImVec2(right - px(12.0f), cy), px(24.0f), alpha(p.accent, a));
		right -= px(44.0f);
	}
	if (!r.right.empty()) {
		const float fs = px(16.0f);
		const float maxW = std::max(px(80.0f), (right - x) * 0.45f);
		const float tw = std::min(maxW, measure(F::Small, fs, r.right.c_str()).x);
		textFit(dl, F::Small, fs, ImVec2(right - tw, cy - measure(F::Small, fs, "Ag").y * 0.5f),
			alpha(r.dot == p.fail ? p.fail : r.dot == p.warn ? p.warnText : p.textFaint, a), r.right, maxW);
		if (r.dot) {
			dl->AddCircleFilled(ImVec2(right - tw - px(14.0f), cy), px(4.5f), alpha(r.dot, a), 12);
			right -= px(20.0f);
		}
		right -= tw + px(24.0f);
	}
	const float nw = right - x;
	const float nfs = px(19.5f);
	if (r.sub.empty()) {
		textFit(dl, F::Heading, nfs, ImVec2(x, cy - measure(F::Heading, nfs, "Ag").y * 0.5f), nameCol, r.title, nw);
	} else {
		textFit(dl, F::Heading, nfs, ImVec2(x, cy - px(23.0f)), nameCol, r.title, nw);
		textFit(dl, F::Small, px(15.0f), ImVec2(x, cy + px(4.0f)), alpha(p.textFaint, a), r.sub, nw);
	}
}

} // namespace

int listStep(const std::vector<ListRow> &rows, int sel, int dir)
{
	const int n = static_cast<int>(rows.size());
	if (sel < 0 || sel >= n) {
		for (int i = 0; i < n; i++) {
			if (rows[static_cast<size_t>(i)].kind == ListRow::Kind::Item) {
				return i;
			}
		}
		return -1;
	}
	for (int i = sel + dir; i >= 0 && i < n; i += dir) {
		if (rows[static_cast<size_t>(i)].kind == ListRow::Kind::Item) {
			return i;
		}
	}
	g_listEdge = true;   // (nothing further that way: the end of the list)
	return sel;
}

float drawList(ImDrawList *dl, ImVec2 p0, ImVec2 p1, const std::vector<ListRow> &rows, int sel, bool focused,
	const std::string &key, float appear)
{
	const float w = p1.x - p0.x, gap = px(4.0f);
	std::vector<float> ys(rows.size()), hs(rows.size());
	float total = 0.0f;
	for (size_t i = 0; i < rows.size(); i++) {
		hs[i] = listRowH(rows[i]);
		ys[i] = total;
		total += hs[i] + gap;
	}
	const float viewH = p1.y - p0.y;
	float &scroll = value(key + ".scroll", 0.0f);
	float target = scroll;
	if (sel >= 0 && sel < static_cast<int>(rows.size())) {
		// the section's heading above the first item shows too
		const size_t s = static_cast<size_t>(sel);
		float top = ys[s] - px(70.0f);
		if (s > 0 && rows[s - 1].kind == ListRow::Kind::Section) {
			top = ys[s - 1] - px(10.0f);
		}
		const float bottom = ys[s] + hs[s] + px(70.0f);
		if (top < target) {
			target = top;
		} else if (bottom > target + viewH) {
			target = bottom - viewH;
		}
	}
	target = std::max(0.0f, std::min(std::max(0.0f, total - viewH), target));
	if (std::fabs(target - scroll) > viewH * 3.0f) {
		scroll = target;   // (a long jump: no slide through hundreds of rows)
	}
	scroll = approach(scroll, target, 15.0f);
	const float a = easeOut(appear);
	dl->PushClipRect(ImVec2(p0.x - px(8.0f), p0.y - px(6.0f)), ImVec2(p1.x + px(8.0f), p1.y), true);
	for (size_t i = 0; i < rows.size(); i++) {
		const float y = p0.y + ys[i] - scroll;
		if (y > p1.y || y + hs[i] < p0.y) {
			continue;
		}
		float &hl = value(key + ".h." + rows[i].key, 0.0f);
		const bool on = static_cast<int>(i) == sel;
		hl = approach(hl, on ? 1.0f : 0.0f, 18.0f);
		listRow(dl, rows[i], ImVec2(p0.x, y), w, hs[i], hl, focused, a);
	}
	dl->PopClipRect();
	if (scroll + viewH < total - px(4.0f)) {
		dl->AddRectFilledMultiColor(ImVec2(p0.x - px(8.0f), p1.y - px(48.0f)), ImVec2(p1.x + px(8.0f), p1.y), alpha(P().bg, 0.0f),
			alpha(P().bg, 0.0f), alpha(P().bg, 0.85f), alpha(P().bg, 0.85f));
	}
	if (total > viewH * 1.5f) {
		// where in the list: a thin bar at its right
		const float bh = std::max(px(40.0f), viewH * viewH / total);
		const float by = p0.y + (viewH - bh) * (scroll / std::max(1.0f, total - viewH));
		dl->AddRectFilled(ImVec2(p1.x + px(14.0f), by), ImVec2(p1.x + px(18.0f), by + bh), IM_COL32(255, 255, 255, 46), px(2.0f));
	}
	return total;
}

void drawAside(ImDrawList *dl, ImVec2 p0, ImVec2 p1, const Aside &as, float appear)
{
	const Palette &p = P();
	const float a = easeOut(appear);
	panel(dl, p0, p1, px(grid::rLarge), IM_COL32(255, 255, 255, static_cast<int>(10 * a)), alpha(p.border, a));
	const float pad = px(32.0f), w = p1.x - p0.x - pad * 2.0f;
	float y = p0.y + pad;
	if (as.picture || !as.pictureEmpty.empty()) {
		const float ih = w * 9.0f / 16.0f;
		const ImVec2 i0(p0.x + pad, y), i1(p0.x + pad + w, y + ih);
		if (as.picture) {
			imageFill(dl, as.picture, i0, i1, px(grid::rMid), IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
		} else {
			dl->AddRect(i0, i1, IM_COL32(255, 255, 255, static_cast<int>(40 * a)), px(grid::rMid), ImDrawCornerFlags_All, px(1.5f));
			const ImVec2 ts = measure(F::Small, px(16.0f), as.pictureEmpty.c_str());
			text(dl, F::Small, px(16.0f), ImVec2(i0.x + (w - ts.x) * 0.5f, i0.y + (ih - ts.y) * 0.5f), alpha(p.textFaint, a),
				as.pictureEmpty.c_str());
		}
		y += ih + px(24.0f);
	}
	if (!as.title.empty()) {
		y += paragraph(dl, F::Title, px(24.0f), ImVec2(p0.x + pad, y), alpha(p.text, a), as.title, w, 2, 1.15f) + px(14.0f);
	}
	for (const auto &f : as.facts) {
		label(dl, px(12.5f), ImVec2(p0.x + pad, y), alpha(p.textFaint, a), f.first);
		y += px(22.0f);
		y += paragraph(dl, F::Body, px(18.0f), ImVec2(p0.x + pad, y), alpha(p.textDim, a), f.second, w, 2) + px(16.0f);
	}
	const float noteH = as.note.empty() ? 0.0f : paragraph(nullptr, F::Small, px(14.5f), ImVec2(0, 0), 0, as.note, w, 4) + px(16.0f);
	if (!as.text.empty()) {
		const float room = p1.y - pad - noteH - y;
		const int lines = std::max(1, static_cast<int>(room / (px(17.5f) * 1.4f)));
		paragraph(dl, F::Body, px(17.5f), ImVec2(p0.x + pad, y), alpha(as.warn ? p.warnText : p.textDim, a), as.text, w, lines, 1.4f);
	}
	if (!as.note.empty()) {
		paragraph(dl, F::Small, px(14.5f), ImVec2(p0.x + pad, p1.y - pad - noteH + px(16.0f)), alpha(p.textFaint, a), as.note, w, 4);
	}
}


// ---- the backdrop -----------------------------------------------------------------------------

static const ImU32 *g_backdropLight = nullptr;

ImU32 backdropLight()
{
	return g_backdropLight && *g_backdropLight ? *g_backdropLight : P().accent;
}

void backdrop(ImDrawList *dl, ImVec2 size, const Backdrop &b)
{
	static std::string curKey;
	static Tex cur, prev;
	static ImU32 curTint = 0, prevTint = 0;
	static float fade = 1.0f;
	g_backdropLight = &curTint;
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
