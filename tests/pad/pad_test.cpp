// OGBox by Press5elect, GPL-3.0-or-later. The PC test of the DualSense as an Xbox controller
// (ps5/src/ogbox_pad.cpp): the default layout, remapping, the deadzones and the tap latch.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "ogbox_pad.h"
#include "platform.h"

using namespace ogbox;

static PadMap mapOf(const std::map<std::string, std::string> &kv)
{
	return padMapFrom([&](const char *k) {
		const auto it = kv.find(k);
		return it == kv.end() ? std::string() : it->second;
	});
}

int main()
{
	PshxPad x;
	// the default layout: OGBox's as before
	{
		const PadMap m = mapOf({});
		PadFrame f;
		f.buttons = PAD_CROSS | PAD_R1 | PAD_UP | PAD_L3 | PAD_OPTIONS | PAD_TOUCH_PAD;
		f.l2 = 200;
		f.ly = 0;   // pushed up
		mapPad(m, f, x);
		assert(x.analog[PSHX_A] == 0xFF && x.analog[PSHX_B] == 0 && x.analog[PSHX_BLACK] == 0xFF && x.analog[PSHX_LT] == 200);
		assert((x.buttons & PSHX_PAD_UP) && (x.buttons & PSHX_PAD_LTHUMB));
		assert(!(x.buttons & (PSHX_PAD_START | PSHX_PAD_BACK)));   // automatic: the caller's
		assert(x.ly == 32767 && x.lx == 0 && x.rx == 0);
	}
	// remapped: A on Square, X on Cross, the left trigger on R1 (full or nothing), Start on OPTIONS
	{
		const PadMap m = mapOf({ { "pad_a", "square" }, { "pad_x", "cross" }, { "pad_lt", "r1" }, { "pad_start", "options" },
			{ "pad_back", "none" }, { "pad_black", "l2" } });
		PadFrame f;
		f.buttons = PAD_SQUARE | PAD_R1 | PAD_OPTIONS | PAD_TOUCH_PAD;
		f.l2 = 90;
		mapPad(m, f, x);
		assert(x.analog[PSHX_A] == 0xFF && x.analog[PSHX_X] == 0 && x.analog[PSHX_LT] == 0xFF);
		assert(x.analog[PSHX_BLACK] == 90);   // a trigger on an analog button: its travel
		assert((x.buttons & PSHX_PAD_START) && !(x.buttons & PSHX_PAD_BACK));
	}
	// "automatic" only for Start and Back; an unknown word is nothing
	{
		const PadMap m = mapOf({ { "pad_a", "auto" }, { "pad_b", "nonsense" } });
		assert(m.source[static_cast<int>(XboxControl::A)] == PAD_CROSS);
		assert(m.source[static_cast<int>(XboxControl::B)] == src::none);
	}
	// the touch pad's halves as sources
	{
		const PadMap m = mapOf({ { "pad_back", "touch_left" }, { "pad_start", "touch_right" } });
		PadFrame f;
		f.buttons = src::touchRight;
		mapPad(m, f, x);
		assert((x.buttons & PSHX_PAD_START) && !(x.buttons & PSHX_PAD_BACK));
	}
	// the sticks: a 20% deadzone takes a small push to nothing, a full one stays full; swapped; inverted
	{
		const PadMap m = mapOf({ { "pad_dead_l", "20" }, { "pad_sticks", "swapped" }, { "pad_invert_ry", "1" } });
		PadFrame f;
		f.rx = 128 + 20;            // 16%: inside the deadzone (on the left stick, swapped)
		f.lx = 255;                 // the right stick, full right
		f.ly = 0;                   // ... and up: inverted, down
		mapPad(m, f, x);
		assert(x.lx == 0 && x.ly == 0);
		assert(x.rx > 32000 && x.ry < -32000);
		f.rx = 255;
		f.ry = 128;
		mapPad(m, f, x);
		assert(x.lx > 32000 && x.ly == 0);
		f.rx = 128 + 64;            // 50%: rescaled past the deadzone, (0.5 - 0.2) / 0.8
		mapPad(m, f, x);
		assert(std::abs(x.lx - static_cast<int>(0.375f * 32767)) < 600);
	}
	// the triggers' deadzone
	{
		const PadMap m = mapOf({ { "pad_dead_t", "10" } });
		PadFrame f;
		f.l2 = 20;
		f.r2 = 255;
		mapPad(m, f, x);
		assert(x.analog[PSHX_LT] == 0 && x.analog[PSHX_RT] == 255);
	}
	// the tap latch: a tap inside one frame reaches the game, and is held kMinHold
	{
		TapLatch t;
		assert(t.update(PAD_CROSS, 0, 1.000) == PAD_CROSS);      // down and up between two frames
		assert(t.update(0, 0, 1.016) == PAD_CROSS);              // still held for the game
		assert(t.update(0, 0, 1.033) == PAD_CROSS);
		assert(t.update(0, 0, 1.060) == 0);                      // let go after 50 ms
		// held long: let go at once
		assert(t.update(PAD_SQUARE, PAD_SQUARE, 2.0) == PAD_SQUARE);
		assert(t.update(PAD_SQUARE, PAD_SQUARE, 2.5) == PAD_SQUARE);
		assert(t.update(0, 0, 2.516) == 0);
		// a press and its release in the same frame, from rest: seen
		assert(t.update(PAD_UP, 0, 3.0) == PAD_UP);
	}
	std::puts("pad: ok");
	return 0;
}
