/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The game list and the game page.
 *
 * The home has one thing in focus at a time. Under the head, a strip of pills: where this is, its
 * folders, the Xbox dashboard, the actions. Below it the chosen game, large: its name, a few chips,
 * a line on where it was left, its buttons. At the foot, the shelf: every game's cover in a row, the
 * chosen one first and larger, the row sliding under it. UP goes to the strip, DOWN back to the
 * games. The list layout puts the games in a column instead, the chosen one beside it.
 *
 * The game page: its cover and facts at the left; its name, chips and description at the right,
 * what to do with it as a row of buttons, and its save states under them.
 */
#include "ogbox_ui.h"
#include "ogbox_ui_internal.h"

#include <algorithm>
#include <cmath>

namespace ui {

using detail::imageFill;
using detail::ring;

// ---- a game's face --------------------------------------------------------------------------------

void detail::drawCover(ImDrawList *dl, const HomeGame &g, const Tex &cover, ImVec2 p0, ImVec2 p1, float focus, float a, bool big)
{
	const float r = px(10.0f);
	const float w = p1.x - p0.x;
	shadow(dl, p0, p1, r, px(26.0f + 24.0f * focus), 0.45f * a + 0.25f * focus * a);
	if (cover) {
		imageFill(dl, cover, p0, p1, r, IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
		return;
	}
	const ImU32 tint = g.tint ? g.tint : tintFor(g.title);
	const Tex gr = grad();
	if (gr) {
		dl->AddImageRounded(gr.id, p0, p1, ImVec2(0, 0), ImVec2(1, 1), alpha(mix(tint, IM_COL32(255, 255, 255, 255), 0.18f), a), r);
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
	const float fs = big ? px(28.0f) : std::max(px(17.0f), w * 0.11f);
	const float tw = w - px(32.0f);
	const float th = paragraph(nullptr, F::Title, fs, ImVec2(0, 0), 0, g.title, tw, 3, 1.08f);
	paragraph(dl, F::Title, fs, ImVec2(p0.x + px(16.0f), p1.y - px(18.0f) - th), IM_COL32(255, 255, 255, static_cast<int>(255 * a)), g.title,
		tw, 3, 1.08f);
}

namespace {

// ---- the head: the logo, the tabs, the status --------------------------------------------------------

void drawHomeHead(ImDrawList *dl, ImVec2 S, const HomeView &v)
{
	const Palette &p = P();
	const float L = px(grid::margin);
	const float headY = px(grid::headY), headH = px(grid::headH);
	float x = L;
	if (v.logo) {
		const float lw = headH * v.logo.w / v.logo.h;
		dl->AddImageRounded(v.logo.id, ImVec2(x, headY), ImVec2(x + lw, headY + headH), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE,
			px(grid::rSmall));
		x += lw + px(48.0f);
	} else {
		text(dl, F::Title, px(34.0f), ImVec2(x, headY + px(8.0f)), p.text, "OGBox");
		x += measure(F::Title, px(34.0f), "OGBox").x + px(48.0f);
	}
	if (!v.tabs.empty()) {
		const float kh = px(30.0f), ty = headY + (headH - kh) * 0.5f;
		if (v.tabs.size() > 1) {
			x += keycap(dl, ImVec2(x, ty), kh, "L1", p.textFaint) + px(10.0f);
		}
		const float ph = px(44.0f), py = headY + (headH - ph) * 0.5f, fs = px(19.0f);
		std::vector<float> xs, ws;
		float tx = x;
		for (const std::string &t : v.tabs) {
			const float w = measure(F::Heading, fs, t.c_str()).x + px(40.0f);
			xs.push_back(tx);
			ws.push_back(w);
			tx += w + px(4.0f);
		}
		// the chosen tab's pill slides to it
		const int on = std::max(0, std::min(static_cast<int>(v.tabs.size()) - 1, v.tab));
		float &pillX = value("home.tabx", xs[static_cast<size_t>(on)]);
		float &pillW = value("home.tabw", ws[static_cast<size_t>(on)]);
		pillX = approach(pillX, xs[static_cast<size_t>(on)], 18.0f);
		pillW = approach(pillW, ws[static_cast<size_t>(on)], 18.0f);
		dl->AddRectFilled(ImVec2(pillX, py), ImVec2(pillX + pillW, py + ph), IM_COL32(255, 255, 255, 34), ph * 0.5f);
		for (size_t i = 0; i < v.tabs.size(); i++) {
			const ImVec2 ts = measure(F::Heading, fs, v.tabs[i].c_str());
			text(dl, F::Heading, fs, ImVec2(xs[i] + (ws[i] - ts.x) * 0.5f, py + (ph - ts.y) * 0.5f),
				static_cast<int>(i) == on ? p.text : p.textFaint, v.tabs[i].c_str());
		}
		if (v.tabs.size() > 1) {
			keycap(dl, ImVec2(tx + px(6.0f), ty), kh, "R1", p.textFaint);
		}
	}
	float rx = S.x - L;
	const float fs = px(17.0f);
	for (auto it = v.status.rbegin(); it != v.status.rend(); ++it) {
		const ImVec2 ts = measure(F::Small, fs, it->first.c_str());
		rx -= ts.x;
		text(dl, F::Small, fs, ImVec2(rx, headY + (headH - ts.y) * 0.5f), p.textFaint, it->first.c_str());
		if (it->second) {
			rx -= px(18.0f);
			dl->AddCircleFilled(ImVec2(rx + px(5.0f), headY + headH * 0.5f), px(4.5f), it->second, 12);
		}
		rx -= px(30.0f);
	}
}

/* The foot: the hints, and at the right the covers downloading and the version. */
void drawHomeFoot(ImDrawList *dl, ImVec2 S, const HomeView &v)
{
	footer(dl, S, v.footer, v.version);
	if (v.coverProgress < 0.0f || v.coverText.empty()) {
		return;
	}
	const Palette &p = P();
	const float fs = px(17.0f), y = px(grid::footY), kh = px(30.0f);
	const float vw = v.version.empty() ? 0.0f : measure(F::Small, fs, v.version.c_str()).x + px(36.0f);
	const ImVec2 ts = measure(F::Small, fs, v.coverText.c_str());
	const float bw = px(120.0f);
	const float x1 = S.x - px(grid::margin) - vw;
	const float tx = x1 - ts.x;
	text(dl, F::Small, fs, ImVec2(tx, y + (kh - ts.y) * 0.5f), p.textFaint, v.coverText.c_str());
	float &shown = value("home.coverbar", v.coverProgress);
	shown = v.coverProgress < shown ? v.coverProgress : approach(shown, v.coverProgress, 10.0f);
	progress(dl, ImVec2(tx - px(16.0f) - bw, y + kh * 0.5f - px(2.5f)), ImVec2(tx - px(16.0f), y + kh * 0.5f + px(2.5f)), shown);
}

/* The strip under the head: the folders, the dashboard, the actions, as pills; the chosen one
 * filled while the strip has the controller. Its height. */
float drawStrip(ImDrawList *dl, ImVec2 S, const HomeView &v, float y)
{
	const Palette &p = P();
	const float L = px(grid::margin);
	const int m = static_cast<int>(v.cards.size());
	if (m == 0) {
		return 0.0f;
	}
	const float h = px(48.0f), gap = px(10.0f), fs = px(17.0f), sfs = px(15.0f);
	float x = L;
	if (!v.cardsTitle.empty()) {
		x += label(dl, px(13.0f), ImVec2(x, y + (h - px(13.0f)) * 0.5f - px(1.0f)), p.textFaint, v.cardsTitle) + px(22.0f);
	}
	const bool on = v.focusRow == 1;
	const int sel = std::max(0, std::min(m - 1, v.cardSel));
	std::vector<float> ws(static_cast<size_t>(m));
	std::vector<float> xs(static_cast<size_t>(m));
	float tx = 0.0f;
	for (int i = 0; i < m; i++) {
		const HomeCard &c = v.cards[static_cast<size_t>(i)];
		float w = px(18.0f) + px(22.0f) + px(12.0f) + measure(F::Heading, fs, c.title.c_str()).x + px(20.0f);
		if (!c.sub.empty()) {
			w += px(10.0f) + std::min(px(220.0f), measure(F::Small, sfs, c.sub.c_str()).x);
		}
		ws[static_cast<size_t>(i)] = w;
		xs[static_cast<size_t>(i)] = tx;
		tx += w + gap;
	}
	// the strip slides so the chosen pill stays in sight
	const float room = S.x - L - x;
	float &scroll = value("home.strip", 0.0f);
	float target = scroll;
	const float sx0 = xs[static_cast<size_t>(sel)], sx1 = sx0 + ws[static_cast<size_t>(sel)];
	if (sx0 < target) {
		target = sx0;
	} else if (sx1 > target + room) {
		target = sx1 - room;
	}
	target = std::max(0.0f, std::min(std::max(0.0f, tx - gap - room), target));
	scroll = approach(scroll, target, 14.0f);
	dl->PushClipRect(ImVec2(x - px(6.0f), y - px(8.0f)), ImVec2(S.x - L + px(6.0f), y + h + px(8.0f)), true);
	for (int i = 0; i < m; i++) {
		const HomeCard &c = v.cards[static_cast<size_t>(i)];
		const float cx = x + xs[static_cast<size_t>(i)] - scroll;
		const float w = ws[static_cast<size_t>(i)];
		if (cx > S.x || cx + w < 0.0f) {
			continue;
		}
		float &hl = value("home.c." + c.title + c.sub, 0.0f);
		hl = approach(hl, on && i == sel ? 1.0f : 0.0f, 16.0f);
		const ImVec2 c0(cx, y), c1(cx + w, y + h);
		if (c.dashed) {
			dl->AddRect(c0, c1, IM_COL32(255, 255, 255, 60), h * 0.5f, ImDrawCornerFlags_All, px(1.5f));
		} else {
			dl->AddRectFilled(c0, c1, IM_COL32(255, 255, 255, 20), h * 0.5f);
		}
		if (hl > 0.01f) {
			dl->AddRectFilled(c0, c1, alpha(p.accent, hl), h * 0.5f);
		}
		const ImU32 ink = mix(c.accent ? p.accent : p.textDim, p.accentInk, hl);
		icon(dl, c.icon, ImVec2(cx + px(18.0f) + px(11.0f), y + h * 0.5f), px(22.0f), ink);
		const float nx = cx + px(18.0f) + px(22.0f) + px(12.0f);
		text(dl, F::Heading, fs, ImVec2(nx, y + (h - measure(F::Heading, fs, "Ag").y) * 0.5f), mix(p.text, p.accentInk, hl), c.title.c_str());
		if (!c.sub.empty()) {
			const float sx = nx + measure(F::Heading, fs, c.title.c_str()).x + px(10.0f);
			textFit(dl, F::Small, sfs, ImVec2(sx, y + (h - measure(F::Small, sfs, "Ag").y) * 0.5f), mix(p.textFaint, alpha(p.accentInk, 0.7f), hl),
				c.sub, px(220.0f));
		}
	}
	dl->PopClipRect();
	if (target + room < tx - gap - px(2.0f)) {
		// more to the right
		dl->AddRectFilledMultiColor(ImVec2(S.x - L - px(80.0f), y - px(8.0f)), ImVec2(S.x - L + px(6.0f), y + h + px(8.0f)), alpha(p.bg, 0.0f),
			alpha(p.bg, 0.8f), alpha(p.bg, 0.8f), alpha(p.bg, 0.0f));
	}
	return h;
}

/* The chosen game's name, chips, words and buttons from (x, y) in width w; the height it took. */
float drawHero(ImDrawList *dl, const HomeView &v, float x, float y, float w, float a, float maxTitle, int titleLines, bool focused)
{
	const Palette &p = P();
	const float y0 = y;
	if (!v.kicker.empty()) {
		label(dl, px(14.0f), ImVec2(x, y), alpha(p.accent, a), v.kicker);
		y += px(32.0f);
	}
	const float fs = detail::fitSize(F::Display, v.title, w, maxTitle, px(34.0f), titleLines);
	y += paragraph(dl, F::Display, fs, ImVec2(x, y), alpha(p.text, a), v.title, w, titleLines, 1.05f) + px(24.0f);
	const float ch = chipRow(dl, ImVec2(x, y), v.chips, w, a);
	if (ch > 0.0f) {
		y += ch + px(22.0f);
	}
	if (!v.meta.empty()) {
		y += paragraph(dl, F::Body, px(19.0f), ImVec2(x, y), alpha(p.textDim, a), v.meta, w - px(40.0f), 3) + px(30.0f);
	}
	float bx = x;
	if (!v.primary.empty()) {
		Button b{ v.primary, v.primaryKey, Icon::None, true };
		bx += button(dl, ImVec2(bx, y), b, 0.0f, a * (focused ? 1.0f : 0.75f)) + px(14.0f);
		if (!v.secondary.empty()) {
			Button b2{ v.secondary, v.secondaryKey, Icon::None, false };
			button(dl, ImVec2(bx, y), b2, 0.0f, a);
		}
		y += px(kButtonH);
	}
	return y - y0;
}

void drawEmpty(ImDrawList *dl, const HomeView &v, ImVec2 at, float w)
{
	const Palette &p = P();
	const std::string &msg = !v.error.empty() ? v.error : v.empty;
	if (v.loading) {
		spinner(dl, ImVec2(at.x + px(20.0f), at.y + px(14.0f)), px(18.0f), p.accent);
		text(dl, F::Body, px(20.0f), ImVec2(at.x + px(56.0f), at.y), p.textDim, "Reading the folder\xE2\x80\xA6");
	} else if (!msg.empty()) {
		icon(dl, v.error.empty() ? Icon::Disc : Icon::Warn, ImVec2(at.x + px(24.0f), at.y + px(20.0f)), px(40.0f),
			v.error.empty() ? p.textFaint : p.fail);
		paragraph(dl, F::Body, px(20.0f), ImVec2(at.x + px(70.0f), at.y + px(6.0f)), v.error.empty() ? p.textDim : p.fail, msg,
			w - px(70.0f), 4);
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
	const float L = px(grid::margin);
	drawHomeHead(dl, S, v);
	const float stripY = px(150.0f);
	drawStrip(dl, S, v, stripY);
	const bool gamesOn = v.focusRow == 0;

	// ---- the chosen game, large
	const int n = static_cast<int>(v.games.size());
	const std::string selId = v.gameSel >= 0 && v.gameSel < n ? v.games[static_cast<size_t>(v.gameSel)].id : std::string();
	{
		static std::string shownId;
		float &appear = value("home.detail", 0.0f);
		if (selId != shownId) {
			shownId = selId;
			appear = 0.0f;
		}
		appear = approach(appear, 1.0f, 9.0f);
		const float a = smooth(appear);
		if (!v.title.empty()) {
			drawHero(dl, v, L, px(262.0f) + px(14.0f) * (1.0f - a), px(900.0f), a, px(64.0f), 2, gamesOn);
		}
		/* its description in the room at the right, over the light */
		if (!v.about.empty()) {
			const float x = px(1090.0f), w = S.x - L - x, y = px(276.0f) + px(14.0f) * (1.0f - a);
			label(dl, px(13.0f), ImVec2(x, y), alpha(p.textFaint, a), "About");
			const float h = paragraph(dl, F::Body, px(18.0f), ImVec2(x, y + px(30.0f)), alpha(p.textDim, a), v.about, w, 9, 1.45f);
			if (!v.aboutSource.empty()) {
				textFit(dl, F::Small, px(13.5f), ImVec2(x, y + px(30.0f) + h + px(12.0f)), alpha(p.textFaint, a), v.aboutSource, w);
			}
		}
	}

	// ---- the shelf: the chosen game first and larger, the row sliding under it
	const float shelfBottom = px(grid::bottom - 6.0f);
	const float baseH = px(232.0f), bigH = px(296.0f), gap = px(22.0f);
	const float ratio = 1.0f / 1.4f;
	if (n > 0) {
		float &scroll = value("home.scroll", static_cast<float>(std::max(0, v.gameSel)));
		const float target = static_cast<float>(std::max(0, std::min(n - 1, v.gameSel)));
		if (std::fabs(scroll - target) > 30.0f) {
			scroll = target + (scroll < target ? -3.0f : 3.0f);   // (a long jump: the last stretch moves)
		}
		scroll = approach(scroll, target, 13.0f);
		const int first = std::max(0, static_cast<int>(std::floor(scroll)) - 2);
		const int last = std::min(n - 1, static_cast<int>(std::ceil(scroll)) + 12);
		std::vector<float> f(static_cast<size_t>(last - first + 1));
		for (int i = first; i <= last; i++) {
			float &g = value("home.g." + v.games[static_cast<size_t>(i)].id, 0.0f);
			g = approach(g, i == v.gameSel ? 1.0f : 0.0f, 12.0f);
			f[static_cast<size_t>(i - first)] = g;
		}
		const auto growth = [&](int i) { return i >= first && i <= last ? f[static_cast<size_t>(i - first)] : 0.0f; };
		const auto width = [&](int i) { return (baseH + (bigH - baseH) * growth(i)) * ratio; };
		// the one at the scroll's place sits at the margin
		const int s0 = static_cast<int>(std::floor(scroll));
		const float frac = scroll - static_cast<float>(s0);
		float x0 = L - frac * (width(s0) + gap);
		std::vector<std::pair<int, float>> xs;
		for (int i = s0; i <= last; i++) {
			xs.push_back({ i, x0 });
			x0 += width(i) + gap;
		}
		{
			float xx = L - frac * (width(s0) + gap);
			for (int i = s0 - 1; i >= first; i--) {
				xx -= width(i) + gap;
				xs.push_back({ i, xx });
			}
		}
		float &rf = value("home.rowfocus", gamesOn ? 1.0f : 0.0f);
		rf = approach(rf, gamesOn ? 1.0f : 0.0f, 10.0f);
		std::sort(xs.begin(), xs.end(), [&](const std::pair<int, float> &a, const std::pair<int, float> &b) {
			return growth(a.first) < growth(b.first);
		});
		for (const auto &it : xs) {
			const int i = it.first;
			const float g = growth(i);
			const float h = baseH + (bigH - baseH) * g, w = h * ratio;
			const float gx = it.second;
			if (gx > S.x || gx + w < 0.0f) {
				continue;
			}
			// the ones that slid past the margin fade out; the far right fades in
			float a = std::max(0.0f, std::min(1.0f, (gx + w * 0.5f - L * 0.2f) / (L * 0.8f)));
			a *= std::max(0.0f, std::min(1.0f, (S.x - gx) / px(180.0f)));
			if (a <= 0.01f) {
				continue;
			}
			const float dim = i == v.gameSel ? 1.0f : 0.62f + 0.2f * (1.0f - rf);
			const ImVec2 q0(gx, shelfBottom - h), q1(gx + w, shelfBottom);
			const Tex cov = v.cover ? v.cover(static_cast<size_t>(i)) : Tex{};
			detail::drawCover(dl, v.games[static_cast<size_t>(i)], cov, q0, q1, g, a * dim, false);
			if (i == v.gameSel && rf > 0.01f) {
				ring(dl, q0, q1, px(10.0f), px(6.0f), alpha(p.accent, rf * g), px(3.0f));
			}
		}
		// where in the row: "3 of 140" over its right end
		if (n > 1 && v.gameSel >= 0) {
			const std::string pos = std::to_string(v.gameSel + 1) + " of " + std::to_string(n);
			const ImVec2 ts = measure(F::Small, px(15.0f), pos.c_str());
			text(dl, F::Small, px(15.0f), ImVec2(S.x - L - ts.x, shelfBottom - bigH - px(34.0f)), p.textFaint, pos.c_str());
		}
	} else {
		drawEmpty(dl, v, ImVec2(L, px(v.title.empty() ? 300.0f : 720.0f)), px(1100.0f));
	}

	if (v.jumpLetter && v.jumpAlpha > 0.0f) {
		detail::jumpLetter(dl, ImVec2(S.x * 0.62f, px(440.0f)), v.jumpLetter, v.jumpAlpha);
	}
	drawHomeFoot(dl, S, v);
	busy(dl, S, v.busy);
}

namespace {

/* The list layout: the strip, then the games in one column at the left, the chosen one's cover and
 * details at the right. */
void drawHomeList(ImDrawList *dl, ImVec2 S, const HomeView &v)
{
	const Palette &p = P();
	const float L = px(grid::margin);
	drawHomeHead(dl, S, v);
	const float stripY = px(150.0f);
	const float sh = drawStrip(dl, S, v, stripY);
	const int n = static_cast<int>(v.games.size());
	const bool on = v.focusRow == 0;
	const int sel = n ? std::max(0, std::min(n - 1, v.gameSel)) : -1;
	const float x0 = L, w = px(760.0f), top = stripY + (sh > 0.0f ? sh + px(36.0f) : 0.0f), bottom = px(grid::bottom);
	const float rh = px(76.0f), gap = px(4.0f);
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
			scroll = target;
		}
		scroll = approach(scroll, target, 16.0f);
	}
	if (n == 0) {
		drawEmpty(dl, v, ImVec2(x0, top + px(20.0f)), w);
	}
	float &hy = value("homelist.hy", 0.0f);
	if (sel >= 0) {
		const float want = static_cast<float>(sel) * (rh + gap);
		hy = std::fabs(hy - want) > rh * 6.0f ? want : approach(hy, want, 22.0f);
	}
	dl->PushClipRect(ImVec2(x0 - px(8.0f), top - px(6.0f)), ImVec2(x0 + w + px(8.0f), bottom), true);
	if (sel >= 0) {
		const float y = top + hy - scroll;
		if (on) {
			focusBox(dl, ImVec2(x0, y), ImVec2(x0 + w, y + rh), px(grid::rMid), 1.0f);
		} else {
			dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + rh), IM_COL32(255, 255, 255, 16), px(grid::rMid));
		}
	}
	const int first = std::max(0, static_cast<int>(scroll / (rh + gap)) - 1);
	const int last = std::min(n - 1, static_cast<int>((scroll + viewH) / (rh + gap)) + 1);
	for (int k = first; k <= last; k++) {
		const float y = top + static_cast<float>(k) * (rh + gap) - scroll;
		const bool chosen = k == sel;
		const HomeGame &g = v.games[static_cast<size_t>(k)];
		const float cw = px(44.0f), ch = px(62.0f);
		const ImVec2 c0(x0 + px(16.0f), y + (rh - ch) * 0.5f), c1(c0.x + cw, c0.y + ch);
		const Tex cov = v.cover ? v.cover(static_cast<size_t>(k)) : Tex{};
		if (cov) {
			imageFill(dl, cov, c0, c1, px(5.0f));
		} else {
			dl->AddRectFilled(c0, c1, g.tint ? g.tint : tintFor(g.title), px(5.0f));
			icon(dl, Icon::Disc, ImVec2((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f), px(22.0f), IM_COL32(255, 255, 255, 60));
		}
		const float tx = c1.x + px(22.0f), tw = x0 + w - px(24.0f) - tx;
		std::string sub = g.sub;
		if (!g.size.empty()) {
			sub += sub.empty() ? g.size : " \xC2\xB7 " + g.size;
		}
		const ImU32 nc = chosen ? p.text : p.textDim;
		if (sub.empty()) {
			textFit(dl, F::Heading, px(20.0f), ImVec2(tx, y + rh * 0.5f - px(12.0f)), nc, g.title, tw);
		} else {
			textFit(dl, F::Heading, px(20.0f), ImVec2(tx, y + px(14.0f)), nc, g.title, tw);
			textFit(dl, F::Small, px(15.0f), ImVec2(tx, y + px(43.0f)), p.textFaint, sub, tw);
		}
	}
	dl->PopClipRect();
	if (scroll + viewH < static_cast<float>(n) * (rh + gap) - px(4.0f)) {
		dl->AddRectFilledMultiColor(ImVec2(x0 - px(8.0f), bottom - px(50.0f)), ImVec2(x0 + w + px(8.0f), bottom), alpha(p.bg, 0.0f),
			alpha(p.bg, 0.0f), alpha(p.bg, 0.85f), alpha(p.bg, 0.85f));
	}
	if (n > 9) {
		const float total = static_cast<float>(n) * (rh + gap);
		const float bh = std::max(px(40.0f), viewH * viewH / total);
		const float by = top + (viewH - bh) * (scroll / std::max(1.0f, total - viewH));
		dl->AddRectFilled(ImVec2(x0 + w + px(14.0f), by), ImVec2(x0 + w + px(18.0f), by + bh), IM_COL32(255, 255, 255, 46), px(2.0f));
	}

	// ---- the chosen one, at the right: its cover, then its details under it
	const float dx0 = x0 + w + px(72.0f), dx1 = S.x - L;
	static int shownSel = -2;
	float &appear = value("homelist.detail", 0.0f);
	if (sel != shownSel) {
		shownSel = sel;
		appear = 0.0f;
	}
	appear = approach(appear, 1.0f, 10.0f);
	const float a = smooth(appear);
	if (sel >= 0) {
		const HomeGame &g = v.games[static_cast<size_t>(sel)];
		const float cw = px(300.0f), chh = cw * 1.4f;
		const ImVec2 c0(dx0, top + px(8.0f) * (1.0f - a)), c1(c0.x + cw, c0.y + chh);
		detail::drawCover(dl, g, v.cover ? v.cover(static_cast<size_t>(sel)) : Tex{}, c0, c1, 1.0f, a, true);
		const float x = c1.x + px(40.0f);
		drawHero(dl, v, x, top, dx1 - x, a, px(44.0f), 4, on);
		/* its description under the cover, as wide as the column */
		if (!v.about.empty()) {
			const float y = c1.y + px(30.0f);
			const float lh = px(17.5f) * 1.45f;
			const int lines = std::max(0, static_cast<int>((bottom - y - px(60.0f)) / lh));
			if (lines > 0) {
				label(dl, px(13.0f), ImVec2(dx0, y), alpha(p.textFaint, a), "About");
				const float h = paragraph(dl, F::Body, px(17.5f), ImVec2(dx0, y + px(28.0f)), alpha(p.textDim, a), v.about, dx1 - dx0, lines, 1.45f);
				if (!v.aboutSource.empty()) {
					textFit(dl, F::Small, px(13.5f), ImVec2(dx0, y + px(28.0f) + h + px(10.0f)), alpha(p.textFaint, a), v.aboutSource, dx1 - dx0);
				}
			}
		}
	}
	if (v.jumpLetter && v.jumpAlpha > 0.0f) {
		detail::jumpLetter(dl, ImVec2(x0 + w * 0.5f, top + viewH * 0.5f), v.jumpLetter, v.jumpAlpha);
	}
	drawHomeFoot(dl, S, v);
	busy(dl, S, v.busy);
}

} // namespace

// ---- the game page -----------------------------------------------------------------------------

void drawGamePage(ImDrawList *dl, ImVec2 S, const GameView &v, float appear)
{
	const Palette &p = P();
	const float a = easeOut(appear);
	const float L = px(grid::margin), top = px(140.0f), bottom = px(grid::bottom);
	// ---- the left: the cover, its facts under it
	const float cw = px(400.0f), ch = cw * 1.4f;
	const ImVec2 c0(L - px(30.0f) * (1.0f - a), top), c1(c0.x + cw, c0.y + ch);
	{
		HomeGame g;
		g.title = v.coverTitle;
		g.tint = v.tint;
		if (v.cover) {
			shadow(dl, c0, c1, px(14.0f), px(60.0f), 0.7f * a);
		}
		detail::drawCover(dl, g, v.cover, c0, c1, 1.0f, a, true);
	}
	{
		float y = c1.y + px(34.0f);
		const float fw = cw * 0.5f;
		for (size_t i = 0; i < v.facts.size() && y < bottom - px(40.0f); i++) {
			const float fx = c0.x + fw * static_cast<float>(i % 2);
			label(dl, px(12.5f), ImVec2(fx, y), alpha(p.textFaint, a), v.facts[i].label);
			textFit(dl, F::Body, px(18.0f), ImVec2(fx, y + px(22.0f)), alpha(p.text, a), v.facts[i].value, fw - px(18.0f));
			if (i % 2 == 1) {
				y += px(66.0f);
			}
		}
	}
	// ---- the right: the name, chips and words; what to do; the save states
	const float x = c1.x + px(72.0f), w = S.x - L - x;
	float y = top - px(6.0f) + px(16.0f) * (1.0f - a);
	const float fs = detail::fitSize(F::Display, v.title, w, px(60.0f), px(36.0f), 2);
	y += paragraph(dl, F::Display, fs, ImVec2(x, y), alpha(p.text, a), v.title, w, 2, 1.05f) + px(22.0f);
	const float chh = chipRow(dl, ImVec2(x, y), v.chips, w, a);
	if (chh > 0.0f) {
		y += chh + px(28.0f);
	}
	// the room the actions and the states need: the description gets the rest
	const float actionsH = v.actions.empty() ? 0.0f : px(kButtonH) + px(44.0f);
	const float statesH = v.states.empty() ? 0.0f : px(44.0f) + px(124.0f);
	if (!v.about.empty()) {
		const float room = bottom - y - actionsH - statesH - px(64.0f);
		const int lines = std::max(2, std::min(5, static_cast<int>(room / (px(18.0f) * 1.4f))));
		y += paragraph(dl, F::Body, px(18.0f), ImVec2(x, y), alpha(p.textDim, a), v.about, std::min(w, px(1080.0f)), lines, 1.4f);
		if (!v.aboutSource.empty()) {
			text(dl, F::Small, px(13.5f), ImVec2(x, y + px(8.0f)), alpha(p.textFaint, a), v.aboutSource.c_str());
			y += px(26.0f);
		}
		y += px(34.0f);
	}
	if (!v.actions.empty()) {
		const bool on = v.stateSel < 0;
		float bx = x;
		const int sel = std::max(0, std::min(static_cast<int>(v.actions.size()) - 1, v.actionSel));
		float captionX = x;
		for (size_t i = 0; i < v.actions.size(); i++) {
			const GameAction &act = v.actions[i];
			Button b{ act.name, std::string(), act.icon, i == 0 };
			if (i == 0) {
				b.key = "X";
				b.icon = Icon::None;
			}
			const float bw = button(nullptr, ImVec2(0, 0), b, 0.0f, a, true);
			if (bx + bw > x + w && bx > x) {
				break;   // (no room: the rest is reached from the game list)
			}
			float &f = value("gp.b." + act.name, 0.0f);
			f = approach(f, on && static_cast<int>(i) == sel ? 1.0f : 0.0f, 18.0f);
			button(dl, ImVec2(bx, y), b, f, a);
			if (static_cast<int>(i) == sel) {
				captionX = bx;
			}
			bx += bw + px(12.0f);
		}
		const std::string &cap = v.actions[static_cast<size_t>(sel)].sub;
		if (on && !cap.empty()) {
			textFit(dl, F::Small, px(16.0f), ImVec2(std::min(captionX, x + w - px(300.0f)), y + px(kButtonH) + px(14.0f)),
				alpha(p.textFaint, a), cap, w);
		}
		y += actionsH;
	}
	if (!v.states.empty()) {
		text(dl, F::Heading, px(20.0f), ImVec2(x, y), alpha(p.text, a), "Save states");
		if (v.stateSel >= 0) {
			float hx = x + measure(F::Heading, px(20.0f), "Save states").x + px(24.0f);
			for (const Hint &h : std::vector<Hint>{ { "LR", "choose" }, { "X", "load" }, { "O", "back" } }) {
				hx += hint(dl, ImVec2(hx, y - px(4.0f)), h, alpha(p.textFaint, a)) + px(20.0f);
			}
		}
		y += px(40.0f);
		const float sh = std::min(px(124.0f), bottom - y), sw = sh * 16.0f / 9.0f, sg = px(16.0f);
		const int n = static_cast<int>(v.states.size());
		const float visible = std::max(1.0f, std::floor((w + sg) / (sw + sg)));
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
				dl->AddRectFilledMultiColor(ImVec2(sx, y + sh * 0.45f), ImVec2(sx + sw, y + sh - px(10.0f)), 0, 0, IM_COL32(0, 0, 0, 180),
					IM_COL32(0, 0, 0, 180));
				dl->AddRectFilled(ImVec2(sx, y + sh - px(10.0f)), s1, IM_COL32(0, 0, 0, 180), px(10.0f),
					ImDrawCornerFlags_BotLeft | ImDrawCornerFlags_BotRight);
				if (st.left) {
					dl->AddCircleFilled(ImVec2(sx + px(16.0f), y + px(16.0f)), px(5.0f), alpha(p.accent, a), 10);
				}
				textFit(dl, F::Small, px(14.0f), ImVec2(sx + px(12.0f), y + sh - px(28.0f)), alpha(p.text, a), st.label, sw - px(24.0f));
			}
			if (i == v.stateSel) {
				ring(dl, s0, s1, px(10.0f), px(4.0f), alpha(p.accent, a), px(3.0f));
			}
		}
		dl->PopClipRect();
	}
	footer(dl, S, v.footer);
}

} // namespace ui
