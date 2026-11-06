/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The settings (sections at the left, the chosen one's rows at the right, each row's words under it
 * when it has the controller), a form over a screen (a network share), the on-screen keyboard.
 */
#include "ogbox_ui.h"
#include "ogbox_ui_internal.h"

#include <algorithm>
#include <cmath>

namespace ui {

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
		if (focused) {
			focusBox(dl, p0, p1, px(grid::rMid), hl, r.warn);
		} else {
			dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, static_cast<int>(16 * hl)), px(grid::rMid));
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
	Head hd;
	hd.logo = v.logo;
	hd.title = v.title;
	hd.subtitle = v.subtitle;
	if (!v.right.empty()) {
		hd.status.push_back({ v.right, 0 });
	}
	head(dl, S, hd);
	// the sections: a quiet column, the chosen one marked by a bar of the accent
	const float navX = L, navW = px(460.0f), top = px(grid::top);
	{
		float y = top;
		const float rh = px(62.0f);
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
			const bool warn = v.nav[static_cast<size_t>(sel)].warn;
			if (v.focusNav) {
				focusBox(dl, ImVec2(navX, hy), ImVec2(navX + navW, hy + rh), px(grid::rMid), 1.0f, warn);
			} else {
				dl->AddRectFilled(ImVec2(navX, hy), ImVec2(navX + navW, hy + rh), IM_COL32(255, 255, 255, 14), px(grid::rMid));
				dl->AddRectFilled(ImVec2(navX, hy + px(16.0f)), ImVec2(navX + px(4.0f), hy + rh - px(16.0f)), warn ? p.warn : p.accent,
					px(2.0f));
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
		text(dl, F::Title, px(26.0f), ImVec2(px0 + px(26.0f) + slide, y + px(2.0f)), p.text, v.pageTitle.c_str());
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
	const float bottom = px(grid::bottom);
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
	shadow(dl, p0, p1, px(grid::rLarge), px(50.0f), 0.6f * a);
	panel(dl, p0, p1, px(grid::rLarge), alpha(p.surface, a), alpha(p.border, a));
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

} // namespace ui
