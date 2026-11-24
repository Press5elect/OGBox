/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 1.0.5: the two devices' frame handoff without the wait, the title's side. The HLE GPU's
 * copy of its display slot into one of the title's staging buffers is submitted
 * (pshx_core_take_start) and the pass goes on; a later pass reads its fence (pshx_core_take_poll,
 * no wait) and, once it is done, that staging buffer is the picture the upload, frame generation
 * and a screenshot take. Until then the screen keeps the picture it has (no upload).
 *
 * A staging buffer the HLE GPU writes is the copy's alone: not read, written or freed by the title.
 * One copy is in flight at a time, and none is started while it is (a newer frame's copy doesn't
 * queue behind the last one: at most one refresh more of latency). A new copy goes into a staging
 * buffer that is neither the shown picture's (the CPU reads that one: a screenshot, a save state's
 * picture, the frame kept for the next XBE) nor read by a title frame still on the GPU (its slot's
 * fence: readBy). The pass's own slot has had its fence waited (prepareFrame) before it asks.
 */
#ifndef OGBOX_TAKE_H
#define OGBOX_TAKE_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ogbox {

/* The answers of pshx_core_take_start / pshx_core_take_poll (core/ogbox_core.h's PSHX_CORE_TAKE_*). */
enum TakeAnswer { kTakeNone, kTakeStarted, kTakePending, kTakeDone, kTakeFailed };

template <size_t N>
class TakeAsync {
public:
	int target{ -1 };              // the staging buffer the HLE GPU is writing (a copy in flight); -1: none
	std::array<int, N> readBy;     // the frame slot whose submitted frame reads staging i; -1: none on the GPU
	/* the figures since the last line (clear()): copies started; picked up in the pass that started them,
	 * the next one, or later; passes that kept the previous picture because the copy wasn't done; passes
	 * no staging buffer was free to start one; copies lost (the GPU lost); the oldest copy picked up */
	uint64_t started{ 0 }, samePass{ 0 }, nextPass{ 0 }, laterPass{ 0 }, keptOld{ 0 }, noBuffer{ 0 }, failed{ 0 };
	double ageWorstMs{ 0.0 };
	uint64_t ageWorstPasses{ 0 };

	TakeAsync() { readBy.fill(-1); }

	void clear()
	{
		started = samePass = nextPass = laterPass = keptOld = noBuffer = failed = 0;
		ageWorstMs = 0.0;
		ageWorstPasses = 0;
	}

	/* staging i is read by the frame recorded now on frame slot `slot` (its upload, frame generation's push) */
	void reading(size_t i, int slot)
	{
		if (i < N) {
			readBy[i] = slot;
		}
	}

	/* A staging buffer a new copy may write, or -1: not `keep` (the shown picture's), not the one in flight,
	 * and no title frame on the GPU reading it. slotNow: this pass's slot (its fence waited this pass);
	 * done(slot): that slot's fence reads signalled (no wait). */
	template <class Done>
	int pick(int keep, int slotNow, Done done)
	{
		for (size_t i = 0; i < N; i++) {
			const int k = static_cast<int>(i);
			if (k == keep || k == target) {
				continue;
			}
			if (readBy[i] >= 0 && readBy[i] != slotNow && !done(readBy[i])) {
				continue;
			}
			readBy[i] = -1;
			return k;
		}
		return -1;
	}

	/* One pass (pass: its number, now: seconds). poll(): pshx_core_take_poll's answer; start(i): the copy into
	 * staging i (made large enough first) started, pshx_core_take_start's answer; done(slot) as pick's. keep:
	 * the shown picture's staging buffer (-1: none). The staging buffer that now holds a new picture, or -1. */
	template <class Poll, class Start, class Done>
	int pass(uint64_t passNo, double now, int keep, int slotNow, Poll poll, Start start, Done done)
	{
		int got = -1;
		if (target >= 0) {
			const int r = poll();
			if (r == kTakePending) {
				keptOld++;   // the picture stays; and no copy of a newer frame behind this one
				return -1;
			}
			got = settle(r, passNo, now);
		}
		const int t = pick(got >= 0 ? got : keep, slotNow, done);
		if (t < 0) {
			noBuffer++;
			return got;
		}
		if (start(static_cast<size_t>(t)) != kTakeStarted) {
			return got;
		}
		target = t;
		started++;
		startedPass = passNo;
		startedAt = now;
		if (got < 0) {
			const int r = poll();   // (done already: the HLE GPU's queue was idle)
			if (r != kTakePending) {
				got = settle(r, passNo, now);
			}
		}
		return got;
	}

private:
	uint64_t startedPass{ 0 };
	double startedAt{ 0.0 };

	/* the copy in flight over (r: the poll's answer): its staging buffer when it holds the frame */
	int settle(int r, uint64_t passNo, double now)
	{
		const int t = target;
		target = -1;
		if (r != kTakeDone) {
			failed += r == kTakeFailed ? 1 : 0;
			return -1;
		}
		const uint64_t passes = passNo - startedPass;
		(passes == 0 ? samePass : passes == 1 ? nextPass : laterPass)++;
		ageWorstMs = std::max(ageWorstMs, (now - startedAt) * 1000.0);
		ageWorstPasses = std::max(ageWorstPasses, passes);
		return t;
	}
};

}  // namespace ogbox

#endif
