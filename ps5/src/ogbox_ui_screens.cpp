/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The other screens: the list screens (a game's mods, the data folder, the Xbox hard disk), the quick
 * menu over a paused game, the notice before the first start, the boot screen, what shows over a
 * running game, and the popups and toasts over everything.
 */
#include "ogbox_ui.h"
#include "ogbox_ui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

using detail::imageFill;

// ---- a screen of rows --------------------------------------------------------------------------------

void drawListScreen(ImDrawList *dl, ImVec2 S, const ListScreenView &v)
{
	const Palette &p = P();
	const float L = px(grid::margin), R = S.x - L;
	head(dl, S, v.head);
	float y = px(grid::top);
	const float bottom = px(grid::bottom);
	const bool side = v.twoLists || !v.aside.title.empty() || !v.aside.text.empty() || v.aside.picture || !v.aside.facts.empty();
	const float split = v.twoLists ? L + (R - L) * 0.44f : R - px(560.0f);
	const float listR = side ? split - px(36.0f) : R;
	if (!v.intro.empty()) {
		y += paragraph(dl, F::Body, px(19.0f), ImVec2(L, y), v.introWarn ? p.warnText : p.textDim, v.intro, listR - L, 4) + px(22.0f);
	}
	if (v.progress >= 0.0f) {
		progress(dl, ImVec2(L, y), ImVec2(listR, y + px(8.0f)), v.progress);
		y += px(22.0f);
		if (!v.progressText.empty()) {
			textFit(dl, F::Small, px(16.0f), ImVec2(L, y), p.textFaint, v.progressText, listR - L);
			y += px(36.0f);
		}
	}
	const float msgH = v.message.empty() ? 0.0f : paragraph(nullptr, F::Body, px(17.5f), ImVec2(0, 0), 0, v.message, listR - L - px(40.0f), 3) +
		px(28.0f);
	if (!v.title1.empty()) {
		label(dl, px(13.0f), ImVec2(L + px(22.0f), y + px(4.0f)), p.textFaint, v.title1);
		y += px(34.0f);
	}
	drawList(dl, ImVec2(L, y), ImVec2(listR, bottom - msgH), v.rows, v.sel, v.pane == 0, "ls." + v.head.title + v.title1);
	if (!v.message.empty()) {
		const float my = bottom - msgH + px(12.0f);
		icon(dl, v.messageBad ? Icon::Warn : Icon::Check, ImVec2(L + px(14.0f), my + px(12.0f)), px(22.0f), v.messageBad ? p.fail : p.accent);
		paragraph(dl, F::Body, px(17.5f), ImVec2(L + px(40.0f), my), v.messageBad ? p.fail : p.textDim, v.message, listR - L - px(40.0f), 3);
	}
	if (v.twoLists) {
		float y2 = px(grid::top);
		if (!v.title2.empty()) {
			label(dl, px(13.0f), ImVec2(split + px(22.0f), y2 + px(4.0f)), p.textFaint, v.title2);
			y2 += px(34.0f);
		}
		if (v.rows2.empty() && !v.aside.text.empty()) {
			paragraph(dl, F::Body, px(18.0f), ImVec2(split + px(22.0f), y2 + px(8.0f)), p.textDim, v.aside.text, R - split - px(44.0f), 12, 1.4f);
		} else {
			drawList(dl, ImVec2(split, y2), ImVec2(R, bottom), v.rows2, v.sel2, v.pane == 1, "ls2." + v.head.title + v.title2);
		}
		if (v.pane == 0) {
			dl->AddRectFilled(ImVec2(split - px(18.0f), px(grid::top)), ImVec2(split - px(16.0f), bottom), IM_COL32(255, 255, 255, 18));
		}
	} else if (side) {
		static std::string shown;
		float &appear = value("ls.aside", 1.0f);
		const std::string key = v.aside.title + "\x01" + v.aside.text.substr(0, 40);
		if (key != shown) {
			shown = key;
			appear = 0.4f;
		}
		appear = approach(appear, 1.0f, 12.0f);
		drawAside(dl, ImVec2(split, px(grid::top)), ImVec2(R, bottom), v.aside, appear);
	}
	footer(dl, S, v.footer, v.footerRight);
}

// ---- the quick menu ----------------------------------------------------------------------------------

void drawQuickMenu(ImDrawList *dl, ImVec2 S, const QuickView &v, float appear)
{
	const Palette &p = P();
	const float a = easeOut(appear);
	dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, static_cast<int>(140 * a)));
	const float pw = px(620.0f);
	const float x0 = S.x - pw * a, x1 = x0 + pw;
	dl->AddRectFilledMultiColor(ImVec2(x0 - px(60.0f), 0), ImVec2(x0, S.y), 0, IM_COL32(0, 0, 0, static_cast<int>(110 * a)),
		IM_COL32(0, 0, 0, static_cast<int>(110 * a)), 0);
	dl->AddRectFilled(ImVec2(x0, 0), ImVec2(x1, S.y), alpha(p.surface, 0.97f));
	dl->AddLine(ImVec2(x0, 0), ImVec2(x0, S.y), p.border, px(1.5f));
	const float pad = px(44.0f);
	// the game: its cover, its name, paused
	float y = px(56.0f);
	const float ch = px(132.0f), cw = ch / 1.4f;
	{
		HomeGame g;
		g.title = v.title;
		detail::drawCover(dl, g, v.cover, ImVec2(x0 + pad, y), ImVec2(x0 + pad + cw, y + ch), 0.0f, a, false);
	}
	const float tx = x0 + pad + cw + px(24.0f), tw = x1 - pad - tx;
	label(dl, px(13.0f), ImVec2(tx, y + px(6.0f)), alpha(p.accent, a), v.status);
	paragraph(dl, F::Title, px(26.0f), ImVec2(tx, y + px(32.0f)), alpha(p.text, a), v.title, tw, 2, 1.1f);
	textFit(dl, F::Small, px(15.0f), ImVec2(tx, y + ch - px(20.0f)), alpha(p.textFaint, a), v.sub, tw);
	y += ch + px(32.0f);
	const float footY = S.y - px(92.0f);
	drawList(dl, ImVec2(x0 + px(20.0f), y), ImVec2(x1 - px(20.0f), footY - px(12.0f)), v.rows, v.sel, true, "qm", appear);
	float hx = x0 + pad;
	for (const Hint &h : v.footer) {
		hx += hint(dl, ImVec2(hx, footY + px(30.0f)), h, alpha(p.textFaint, a)) + px(24.0f);
	}
	// the slot's card beside the panel
	if (v.showSlot) {
		float &sa = value("qm.slot", 0.0f);
		sa = approach(sa, 1.0f, 14.0f);
		const float w = px(480.0f), h = w * 9.0f / 16.0f + px(150.0f);
		const ImVec2 s0(x0 - px(32.0f) - w + px(20.0f) * (1.0f - sa), std::max(px(60.0f), (S.y - h) * 0.5f)), s1(s0.x + w, s0.y + h);
		shadow(dl, s0, s1, px(grid::rLarge), px(40.0f), 0.6f * sa * a);
		panel(dl, s0, s1, px(grid::rLarge), alpha(p.surface, 0.98f * sa * a), alpha(p.border, sa * a));
		Aside slot = v.slot;
		drawAside(dl, s0, s1, slot, sa * a);
	} else {
		value("qm.slot", 0.0f) = 0.0f;
	}
	busy(dl, S, v.busy);
}

// ---- the notice --------------------------------------------------------------------------------------

void drawNotice(ImDrawList *dl, ImVec2 S, const NoticeView &v)
{
	const Palette &p = P();
	float &appear = value("notice.appear", 0.0f);
	appear = approach(appear, 1.0f, 4.0f);
	const float a = smooth(appear);
	const float w = std::min(S.x - px(2.0f * grid::margin), px(880.0f));
	const float x = (S.x - w) * 0.5f;
	float y = px(190.0f) + px(16.0f) * (1.0f - a);
	// a warning sign in a soft circle
	dl->AddCircleFilled(ImVec2(x + px(34.0f), y + px(34.0f)), px(34.0f), alpha(p.warnSoft, a), 40);
	icon(dl, Icon::Warn, ImVec2(x + px(34.0f), y + px(32.0f)), px(36.0f), alpha(p.warn, a));
	y += px(98.0f);
	if (!v.kicker.empty()) {
		label(dl, px(14.0f), ImVec2(x, y), alpha(p.warn, a), v.kicker);
		y += px(32.0f);
	}
	y += paragraph(dl, F::Display, px(52.0f), ImVec2(x, y), alpha(p.text, a), v.title, w, 2, 1.05f) + px(12.0f);
	if (!v.subtitle.empty()) {
		y += paragraph(dl, F::Body, px(18.0f), ImVec2(x, y), alpha(p.textFaint, a), v.subtitle, w, 1) + px(34.0f);
	}
	if (!v.lead.empty()) {
		y += paragraph(dl, F::Body, px(22.0f), ImVec2(x, y), alpha(p.text, a), v.lead, w, 4, 1.4f) + px(22.0f);
	}
	for (const std::string &s : v.paragraphs) {
		y += paragraph(dl, F::Body, px(19.0f), ImVec2(x, y), alpha(p.textDim, a), s, w, 5, 1.45f) + px(18.0f);
	}
	if (!v.fine.empty()) {
		y += px(6.0f);
		y += paragraph(dl, F::Small, px(16.5f), ImVec2(x, y), alpha(p.textFaint, a), v.fine, w, 3, 1.4f) + px(36.0f);
	}
	if (v.canContinue) {
		float &b = value("notice.button", 0.0f);
		b = approach(b, 1.0f, 8.0f);
		button(dl, ImVec2(x, y), Button{ "Continue", "X", Icon::None, true }, 0.0f, a * b);
	}
}

// ---- the boot screen ---------------------------------------------------------------------------------

void drawBoot(ImDrawList *dl, ImVec2 S, const BootView &v)
{
	const Palette &p = P();
	const float bw = S.x * 0.30f, bh = px(6.0f);
	const float bx = (S.x - bw) * 0.5f;
	const float by = v.logoBox.w > 0.0f ? v.logoBox.y + v.logoBox.w + px(56.0f) : S.y * 0.62f;
	const float fs = px(15.0f);
	label(dl, px(13.0f), ImVec2(bx, by - px(32.0f)), p.textFaint, v.left);
	if (!v.right.empty()) {
		const ImVec2 ts = measure(F::Small, fs, v.right.c_str());
		text(dl, F::Small, fs, ImVec2(bx + bw - ts.x, by - px(34.0f)), p.textFaint, v.right.c_str());
	}
	progress(dl, ImVec2(bx, by), ImVec2(bx + bw, by + bh), v.progress);
	// the start's lines, typed one after another, at the foot
	const float lfs = px(15.5f), lh = lfs * 1.55f;
	float y = S.y - px(64.0f) - lh * static_cast<float>(v.lines.size());
	double start = 0.15;
	for (const std::string &line : v.lines) {
		const double shown = (static_cast<double>(v.elapsed) - start) * 110.0;   // characters a second
		start += static_cast<double>(line.size()) / 110.0 + 0.06;
		if (shown <= 0.0) {
			break;
		}
		const size_t n = std::min(line.size(), static_cast<size_t>(shown));
		text(dl, F::Small, lfs, ImVec2(px(grid::margin), y), p.textFaint, line.c_str(), line.c_str() + n);
		if (n < line.size()) {
			const float cx = px(grid::margin) + measure(F::Small, lfs, line.c_str(), line.c_str() + n).x + px(2.0f);
			if (std::fmod(now(), 1.0) < 0.6) {
				dl->AddRectFilled(ImVec2(cx, y + px(2.0f)), ImVec2(cx + px(9.0f), y + lfs), p.accent);
			}
			break;
		}
		y += lh;
	}
}

// ---- over a running game -----------------------------------------------------------------------------

void drawHud(ImDrawList *dl, ImVec2 S, const HudView &v)
{
	const Palette &p = P();
	const float m = px(28.0f);
	float ry = m;
	if (v.showFps) {
		const std::string n = std::to_string(static_cast<int>(std::lround(v.fps)));
		char ms[32];
		std::snprintf(ms, sizeof(ms), "FPS  %.1f ms", v.fps > 0.5f ? 1000.0f / v.fps : 0.0f);
		const float nfs = px(30.0f), sfs = px(15.0f);
		const float nw = measure(F::Title, nfs, n.c_str()).x, sw = measure(F::Small, sfs, ms).x;
		const float w = px(20.0f) + nw + px(12.0f) + sw + px(20.0f), h = px(52.0f);
		const ImVec2 p0(S.x - m - w, ry), p1(S.x - m, ry + h);
		panel(dl, p0, p1, px(grid::rMid), IM_COL32(0, 0, 0, 180), IM_COL32(255, 255, 255, 24));
		text(dl, F::Title, nfs, ImVec2(p0.x + px(20.0f), p0.y + (h - measure(F::Title, nfs, "0").y) * 0.5f), p.accent, n.c_str());
		text(dl, F::Small, sfs, ImVec2(p0.x + px(20.0f) + nw + px(12.0f), p0.y + (h - measure(F::Small, sfs, "A").y) * 0.5f + px(2.0f)),
			p.textDim, ms);
		ry += h + px(12.0f);
	}
	if (v.showLed) {
		const float la = v.ledAlpha;
		const float rad = px(14.0f), fs = px(16.0f);
		const std::string s = "LED  " + v.ledText;
		const ImVec2 ts = measure(F::Small, fs, s.c_str());
		const float w = px(20.0f) + ts.x + px(18.0f) + rad * 2.0f + px(16.0f), h = px(52.0f);
		const ImVec2 p0(S.x - m - w, ry), p1(S.x - m, ry + h);
		panel(dl, p0, p1, px(grid::rMid), alpha(IM_COL32(0, 0, 0, 180), la), alpha(IM_COL32(255, 255, 255, 24), la));
		text(dl, F::Small, fs, ImVec2(p0.x + px(20.0f), p0.y + (h - ts.y) * 0.5f), alpha(p.textDim, la), s.c_str());
		const ImVec2 c(p1.x - px(16.0f) - rad, p0.y + h * 0.5f);
		if (v.ledColour) {
			dl->AddCircleFilled(c, rad * 1.7f, alpha(v.ledColour, 0.25f * la), 32);
		}
		dl->AddCircleFilled(c, rad, alpha(v.ledColour ? v.ledColour : IM_COL32(40, 40, 40, 255), la), 32);
		dl->AddCircle(c, rad, alpha(IM_COL32(255, 255, 255, 128), la), 32, px(1.5f));
	}
	if (v.showStatus) {
		const float pad = px(26.0f);
		// as wide as its words, within half the screen
		float inner = std::max(measure(F::Title, px(22.0f), v.statusTitle.c_str()).x, px(420.0f));
		for (const auto &l : v.statusLines) {
			inner = std::max(inner, px(90.0f) + measure(F::Body, px(18.0f), l.second.c_str()).x);
		}
		const float w = std::min(S.x * 0.5f, inner + pad * 2.0f);
		float h = pad * 2.0f + px(34.0f);
		const float lineH = px(30.0f);
		h += lineH * static_cast<float>(v.statusLines.size());
		const float probH = v.problem.empty() ? 0.0f : paragraph(nullptr, F::Body, px(18.0f), ImVec2(0, 0), 0, v.problem, w - pad * 2.0f, 6) + px(12.0f);
		h += probH + (v.waiting ? lineH : 0.0f) + (v.statusHints.empty() ? 0.0f : px(48.0f));
		const ImVec2 p0(m, m), p1(m + w, m + h);
		panel(dl, p0, p1, px(grid::rLarge), IM_COL32(0, 0, 0, 200), IM_COL32(255, 255, 255, 26));
		float y = p0.y + pad;
		text(dl, F::Title, px(22.0f), ImVec2(p0.x + pad, y), p.text, v.statusTitle.c_str());
		y += px(40.0f);
		if (!v.problem.empty()) {
			y += paragraph(dl, F::Body, px(18.0f), ImVec2(p0.x + pad, y), p.fail, v.problem, w - pad * 2.0f, 6) + px(12.0f);
		}
		for (const auto &l : v.statusLines) {
			label(dl, px(12.5f), ImVec2(p0.x + pad, y + px(5.0f)), p.textFaint, l.first);
			textFit(dl, F::Body, px(18.0f), ImVec2(p0.x + pad + px(90.0f), y), p.textDim, l.second, w - pad * 2.0f - px(90.0f));
			y += lineH;
		}
		if (v.waiting) {
			spinner(dl, ImVec2(p0.x + pad + px(10.0f), y + px(11.0f)), px(9.0f), p.accent);
			text(dl, F::Body, px(17.0f), ImVec2(p0.x + pad + px(32.0f), y), p.textFaint, "Waiting for the game's first frame");
			y += lineH;
		}
		float hx = p0.x + pad;
		for (const Hint &hn : v.statusHints) {
			hx += hint(dl, ImVec2(hx, y + px(12.0f)), hn, p.textFaint) + px(20.0f);
		}
	}
}

// ---- a choice over a screen, and a message -----------------------------------------------------------

void drawPopup(ImDrawList *dl, ImVec2 S, const PopupView &v, float appear)
{
	const Palette &p = P();
	const float a = easeOut(appear);
	dl->AddRectFilled(ImVec2(0, 0), S, IM_COL32(0, 0, 0, static_cast<int>(150 * a)));
	const float w = px(720.0f);
	const float tw = w - px(80.0f);
	const float textH = v.text.empty() ? 0.0f : paragraph(nullptr, F::Body, px(18.5f), ImVec2(0, 0), 0, v.text, tw, 8) + px(24.0f);
	const float rh = px(60.0f);
	const float h = px(100.0f) + textH + rh * static_cast<float>(v.items.size()) + px(80.0f);
	const ImVec2 p0((S.x - w) * 0.5f, (S.y - h) * 0.5f + px(24.0f) * (1.0f - a)), p1(p0.x + w, p0.y + h);
	shadow(dl, p0, p1, px(grid::rLarge), px(50.0f), 0.7f * a);
	panel(dl, p0, p1, px(grid::rLarge), alpha(p.surface, a), alpha(p.border, a));
	text(dl, F::Title, px(28.0f), ImVec2(p0.x + px(40.0f), p0.y + px(36.0f)), alpha(p.text, a), v.title.c_str());
	float y = p0.y + px(96.0f);
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
	focusBox(dl, ImVec2(p0.x + px(24.0f), hy), ImVec2(p1.x - px(24.0f), hy + rh - px(6.0f)), px(grid::rMid), a);
	for (size_t i = 0; i < v.items.size(); i++) {
		const float iy = y + static_cast<float>(i) * rh;
		const float fs = px(20.0f);
		text(dl, F::Heading, fs, ImVec2(p0.x + px(46.0f), iy + (rh - px(6.0f) - measure(F::Heading, fs, "Ag").y) * 0.5f),
			alpha(static_cast<int>(i) == v.sel ? p.text : p.textDim, a), v.items[i].c_str());
	}
	float hx = p0.x + px(40.0f);
	for (const Hint &hn : std::vector<Hint>{ { "X", "choose" }, { "O", "back" } }) {
		hx += hint(dl, ImVec2(hx, p1.y - px(56.0f)), hn, alpha(p.textFaint, a)) + px(28.0f);
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
	const float w = tw + px(68.0f), h = th + px(36.0f);
	const float y = px(40.0f) - px(20.0f) * (1.0f - easeOut(std::min(1.0f, a * 2.0f)));
	const ImVec2 p0((S.x - w) * 0.5f, y), p1(p0.x + w, y + h);
	shadow(dl, p0, p1, h * 0.5f, px(30.0f), 0.6f * a);
	panel(dl, p0, p1, std::min(h * 0.5f, px(24.0f)), alpha(p.surface2, a), alpha(p.border, a));
	dl->AddCircleFilled(ImVec2(p0.x + px(28.0f), p0.y + h * 0.5f), px(5.0f), alpha(p.accent, a), 10);
	paragraph(dl, F::Body, fs, ImVec2(p0.x + px(46.0f), p0.y + px(17.0f)), alpha(p.text, a), s, maxW, 3);
}

} // namespace ui
