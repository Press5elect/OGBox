// Kelvin - original Xbox emulator for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPLv3 section 7(b)): credit to Press5elect, see LICENSE.md
//
// The sleeps PS5Timing.h's runtime (ps5_timing.cpp) gives the rest of the title: what Crux2
// gives PCSX2 as Threading::Sleep and Threading::SleepUntil (Crux2 v1.0.14,
// ps5/common/PS5Misc.cpp), on the title's monotonic clock (ps5_clock.h, nanoseconds).

#pragma once

#include "PS5Timing.h"

namespace PS5Timing
{
	// Sleeps for ns with the call the start-up probe chose (nanosleep until it has measured);
	// a sleep a signal cuts short goes on for what is left.
	void SleepNs(uint64_t ns);
	// The frame limiter's wait until `end_ns` on ps5px_monotonic_ns(): sleeps for what is left
	// less SpinMarginNs(), then waits out the rest awake (WaitUntil), and counts it for
	// TakeLimiterLine.
	void SleepUntilNs(uint64_t end_ns);
} // namespace PS5Timing
