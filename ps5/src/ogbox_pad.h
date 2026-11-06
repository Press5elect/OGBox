/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The DualSense as an Xbox controller: which button drives each of the Xbox's (Settings >
 * Controller, a game's own too), the sticks' and triggers' deadzones, and a tap latch.
 *
 * The tap latch: the console keeps every reading between two frames, and a press shorter than a
 * frame (a quick tap while a game runs at 30 frames a second, or the presenter is slow) was seen by
 * nobody: only the newest reading went to the game. A press is now held for the game for at least
 * kMinHold seconds, from any reading it shows in.
 *
 * Nothing here knows the console or the emulator's core beyond PshxPad, so tests/pad checks it on
 * a computer.
 */
#ifndef OGBOX_PAD_H
#define OGBOX_PAD_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/ogbox_core.h"

namespace ogbox {

/* The DualSense's controls a mapping can name: platform.h's PAD_* bits, and these of OGBox's. */
namespace src {
constexpr uint32_t none = 0;
constexpr uint32_t l2 = 0x00000100;          // the triggers pressed past half (scePad's own bits)
constexpr uint32_t r2 = 0x00000200;
constexpr uint32_t touchLeft = 0x01000000;   // a click on the touch pad's left or right half
constexpr uint32_t touchRight = 0x02000000;
constexpr uint32_t automatic = 0x40000000;   // Start and Back: as OGBox does by default (the
                                             // touch pad's halves, OPTIONS for players 2 to 4)
} // namespace src

/* The Xbox's controls, in Settings' order. */
enum class XboxControl { A, B, X, Y, Black, White, LT, RT, Up, Down, Left, Right, Start, Back, LThumb, RThumb, Count };

struct PadSource {
	const char *value;    // settings.txt's word
	const char *label;
	uint32_t bits;
};
/* The choices for a control: none, every button, the touch pad's halves (Start and Back: automatic too). */
const std::vector<PadSource> &padSources();
uint32_t padSourceBits(const std::string &value);

struct PadMap {
	uint32_t source[static_cast<int>(XboxControl::Count)];
	bool swapSticks{ false };
	bool invertLeftY{ false }, invertRightY{ false };
	float deadLeft{ 0.0f }, deadRight{ 0.0f };   // 0..1 of the stick's reach
	float deadTriggers{ 0.0f };                  // 0..1 of the trigger's travel
	PadMap();             // the default: OGBox's own layout
};
/* The settings' keys: "pad_a" ... "pad_rthumb", "pad_sticks", "pad_invert_ly", "pad_invert_ry",
 * "pad_dead_l", "pad_dead_r", "pad_dead_t" (percent). value: what the run uses for a key. */
PadMap padMapFrom(const std::function<std::string(const char *)> &value);
const char *padKey(XboxControl c);
const char *padLabel(XboxControl c);

/* One controller's state for a frame: its buttons (platform.h's bits; src::touchLeft/Right for the
 * touch pad's halves), its sticks as read (0..255, 128 at rest, y down) and triggers (0..255). */
struct PadFrame {
	uint32_t buttons{ 0 };
	uint8_t lx{ 128 }, ly{ 128 }, rx{ 128 }, ry{ 128 };
	uint8_t l2{ 0 }, r2{ 0 };
};
/* The Xbox controller's state. Start and Back mapped "automatic" are left out: the caller adds them. */
void mapPad(const PadMap &m, const PadFrame &in, PshxPad &out);
/* A stick's two axes with a radial deadzone (the rest rescaled to the full reach), as the Xbox's. */
void stickWithDeadzone(uint8_t x, uint8_t y, bool invertY, float dead, int16_t &ox, int16_t &oy);

/* A press shorter than a frame held long enough for the game to see it. */
class TapLatch {
public:
	static constexpr double kMinHold = 0.05;   // seconds: three frames of a game at 60, one and a half at 30
	/* any: every bit pressed in any of the frame's readings; newest: the newest reading's. The
	 * buttons the game gets. */
	uint32_t update(uint32_t any, uint32_t newest, double now);

private:
	uint32_t held{ 0 };         // what was given last
	double since[32]{};         // when each bit went down
};

} // namespace ogbox

#endif
