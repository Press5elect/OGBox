/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The DualSense as an Xbox controller (ogbox_pad.h).
 */
#include "ogbox_pad.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "platform.h"

namespace ogbox {

namespace {

constexpr int kCount = static_cast<int>(XboxControl::Count);

struct ControlInfo {
	const char *key;
	const char *label;
	uint32_t def;
};

const ControlInfo kControls[kCount] = {
	{ "pad_a", "A", PAD_CROSS },
	{ "pad_b", "B", PAD_CIRCLE },
	{ "pad_x", "X", PAD_SQUARE },
	{ "pad_y", "Y", PAD_TRIANGLE },
	{ "pad_black", "Black", PAD_R1 },
	{ "pad_white", "White", PAD_L1 },
	{ "pad_lt", "Left trigger", src::l2 },
	{ "pad_rt", "Right trigger", src::r2 },
	{ "pad_up", "D-pad up", PAD_UP },
	{ "pad_down", "D-pad down", PAD_DOWN },
	{ "pad_left", "D-pad left", PAD_LEFT },
	{ "pad_right", "D-pad right", PAD_RIGHT },
	{ "pad_start", "Start", src::automatic },
	{ "pad_back", "Back", src::automatic },
	{ "pad_lthumb", "Left stick click", PAD_L3 },
	{ "pad_rthumb", "Right stick click", PAD_R3 },
};

int16_t axis(uint8_t value, bool invert)
{
	int v = (static_cast<int>(value) - 128) * 256;
	if (invert) {
		v = -v;
	}
	return static_cast<int16_t>(std::max(-32768, std::min(32767, v)));
}

/* A trigger's travel past its deadzone, rescaled to 0..255. */
uint8_t trigger(uint8_t v, float dead)
{
	const float f = static_cast<float>(v) / 255.0f;
	if (f <= dead) {
		return 0;
	}
	return static_cast<uint8_t>(std::lround(std::min(1.0f, (f - dead) / (1.0f - dead)) * 255.0f));
}

int percent(const std::string &s)
{
	return std::max(0, std::min(90, std::atoi(s.c_str())));
}

} // namespace

const std::vector<PadSource> &padSources()
{
	static const std::vector<PadSource> s = {
		{ "auto", "Touch pad", src::automatic },   // 0.0.6: Start and Back on the touch pad's halves
		{ "cross", "Cross", PAD_CROSS },
		{ "circle", "Circle", PAD_CIRCLE },
		{ "square", "Square", PAD_SQUARE },
		{ "triangle", "Triangle", PAD_TRIANGLE },
		{ "l1", "L1", PAD_L1 },
		{ "r1", "R1", PAD_R1 },
		{ "l2", "L2", src::l2 },
		{ "r2", "R2", src::r2 },
		{ "l3", "L3", PAD_L3 },
		{ "r3", "R3", PAD_R3 },
		{ "up", "D-pad up", PAD_UP },
		{ "down", "D-pad down", PAD_DOWN },
		{ "left", "D-pad left", PAD_LEFT },
		{ "right", "D-pad right", PAD_RIGHT },
		{ "options", "OPTIONS", PAD_OPTIONS },
		{ "touch", "Touch pad (whole)", PAD_TOUCH_PAD },
		{ "touch_left", "Touch pad, left half", src::touchLeft },
		{ "touch_right", "Touch pad, right half", src::touchRight },
		{ "none", "Nothing", src::none },
	};
	return s;
}

uint32_t padSourceBits(const std::string &value)
{
	for (const PadSource &p : padSources()) {
		if (value == p.value) {
			return p.bits;
		}
	}
	return src::none;
}

const char *padKey(XboxControl c)
{
	return kControls[static_cast<int>(c)].key;
}

const char *padLabel(XboxControl c)
{
	return kControls[static_cast<int>(c)].label;
}

PadMap::PadMap()
{
	for (int i = 0; i < kCount; i++) {
		source[i] = kControls[i].def;
	}
}

PadMap padMapFrom(const std::function<std::string(const char *)> &value)
{
	PadMap m;
	for (int i = 0; i < kCount; i++) {
		const std::string v = value(kControls[i].key);
		if (v.empty()) {
			continue;
		}
		const uint32_t bits = padSourceBits(v);
		const bool startBack = i == static_cast<int>(XboxControl::Start) || i == static_cast<int>(XboxControl::Back);
		/* "automatic" is Start's and Back's only (the others: their default) */
		m.source[i] = bits == src::automatic && !startBack ? kControls[i].def : bits;
	}
	m.swapSticks = value("pad_sticks") == "swapped";
	m.invertLeftY = value("pad_invert_ly") == "1";
	m.invertRightY = value("pad_invert_ry") == "1";
	m.deadLeft = static_cast<float>(percent(value("pad_dead_l"))) / 100.0f;
	m.deadRight = static_cast<float>(percent(value("pad_dead_r"))) / 100.0f;
	m.deadTriggers = static_cast<float>(percent(value("pad_dead_t"))) / 100.0f;
	return m;
}

void stickWithDeadzone(uint8_t x, uint8_t y, bool invertY, float dead, int16_t &ox, int16_t &oy)
{
	/* the Xbox's y is up, the DualSense's down: inverted unless asked to be (then as read) */
	const int16_t ax = axis(x, false), ay = axis(y, !invertY);
	if (dead <= 0.0f) {
		ox = ax;
		oy = ay;
		return;
	}
	const float fx = static_cast<float>(ax) / 32767.0f, fy = static_cast<float>(ay) / 32767.0f;
	const float m = std::sqrt(fx * fx + fy * fy);
	if (m <= dead) {
		ox = oy = 0;
		return;
	}
	const float k = std::min(1.0f, (m - dead) / (1.0f - dead)) / m;
	const auto clamp = [](float v) { return static_cast<int16_t>(std::lround(std::max(-32768.0f, std::min(32767.0f, v * 32767.0f)))); };
	ox = clamp(fx * k);
	oy = clamp(fy * k);
}

void mapPad(const PadMap &m, const PadFrame &in, PshxPad &out)
{
	out = PshxPad{};
	uint32_t b = in.buttons;
	// the triggers as buttons: past half their travel
	if (in.l2 > 127) {
		b |= src::l2;
	}
	if (in.r2 > 127) {
		b |= src::r2;
	}
	const auto src = [&](XboxControl c) { return m.source[static_cast<int>(c)]; };
	const auto down = [&](XboxControl c) {
		const uint32_t s = src(c);
		return s != src::none && s != src::automatic && (b & s) != 0;
	};
	/* the analog buttons: a trigger mapped to one gives its travel, a button full or nothing */
	const auto level = [&](XboxControl c) -> uint8_t {
		const uint32_t s = src(c);
		if (s == src::l2) {
			return trigger(in.l2, m.deadTriggers);
		}
		if (s == src::r2) {
			return trigger(in.r2, m.deadTriggers);
		}
		return down(c) ? 0xFF : 0;
	};
	out.analog[PSHX_A] = level(XboxControl::A);
	out.analog[PSHX_B] = level(XboxControl::B);
	out.analog[PSHX_X] = level(XboxControl::X);
	out.analog[PSHX_Y] = level(XboxControl::Y);
	out.analog[PSHX_BLACK] = level(XboxControl::Black);
	out.analog[PSHX_WHITE] = level(XboxControl::White);
	out.analog[PSHX_LT] = level(XboxControl::LT);
	out.analog[PSHX_RT] = level(XboxControl::RT);
	const struct {
		XboxControl c;
		uint16_t bit;
	} digital[] = {
		{ XboxControl::Up, PSHX_PAD_UP }, { XboxControl::Down, PSHX_PAD_DOWN }, { XboxControl::Left, PSHX_PAD_LEFT },
		{ XboxControl::Right, PSHX_PAD_RIGHT }, { XboxControl::Start, PSHX_PAD_START }, { XboxControl::Back, PSHX_PAD_BACK },
		{ XboxControl::LThumb, PSHX_PAD_LTHUMB }, { XboxControl::RThumb, PSHX_PAD_RTHUMB },
	};
	for (const auto &d : digital) {
		if (down(d.c)) {
			out.buttons |= d.bit;
		}
	}
	// the sticks, swapped if asked
	const uint8_t lx = m.swapSticks ? in.rx : in.lx, ly = m.swapSticks ? in.ry : in.ly;
	const uint8_t rx = m.swapSticks ? in.lx : in.rx, ry = m.swapSticks ? in.ly : in.ry;
	stickWithDeadzone(lx, ly, m.invertLeftY, m.deadLeft, out.lx, out.ly);
	stickWithDeadzone(rx, ry, m.invertRightY, m.deadRight, out.rx, out.ry);
}

uint32_t TapLatch::update(uint32_t any, uint32_t newest, double now)
{
	any |= newest;
	uint32_t out = newest;
	for (int i = 0; i < 32; i++) {
		const uint32_t bit = 1u << i;
		if ((any & bit) && !(held & bit)) {
			since[i] = now;   // went down in this frame's readings
		}
		/* down in a reading but up in the newest (a tap), or let go: held until kMinHold has passed */
		if (!(newest & bit) && ((any | held) & bit) && now - since[i] < kMinHold) {
			out |= bit;
		}
	}
	held = out;
	return out;
}

} // namespace ogbox
