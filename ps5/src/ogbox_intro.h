/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.9: the startup video. The OGBox X behind black glass, cracked by three blows and
 * shattered (ps5/art/intro: its sources, rendered by tools there). It plays when OGBox starts,
 * before the game list: VP9 in an IVF file (assets/intro.ivf), decoded by libvpx on a thread of
 * its own into RGBA frames, and its sound (assets/intro.wav, 48 kHz 16-bit stereo) through the
 * platform's sound output. The caller shows the frame for the time it asks (frame()), and the
 * video goes on by the clock, not by frames shown: a slow frame is skipped, not stretched.
 */
#ifndef OGBOX_INTRO_H
#define OGBOX_INTRO_H

#include <cstdint>
#include <memory>
#include <string>

namespace ogbox {

class IntroPlayer {
public:
	IntroPlayer();
	~IntroPlayer();
	IntroPlayer(const IntroPlayer &) = delete;
	IntroPlayer &operator=(const IntroPlayer &) = delete;

	/* Reads the video (and the sound, when there is one) and starts decoding ahead; false (and
	 * a line in the boot log) when the video is missing or unreadable: no intro then. */
	bool open(const std::string &video, const std::string &sound);
	/* The clock and the sound start. */
	void start();
	/* The frame for the time since start(): its RGBA pixels (w x h x 4, valid until the next
	 * call), nullptr when none has been decoded yet. *serial changes when the frame does. */
	const uint8_t *frame(uint32_t *w, uint32_t *h, uint64_t *serial);
	/* Seconds since start() (0 before it). */
	double elapsed() const;
	double duration() const;
	/* Past its end, or cut short: true. */
	bool finished() const;
	/* CROSS or CIRCLE: the sound fades out over a fifth of a second and the video ends. */
	void skip();
	/* Stops the sound and the decoder (also the destructor's). */
	void close();

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};

} // namespace ogbox

#endif
