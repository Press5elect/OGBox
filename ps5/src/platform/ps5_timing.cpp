// Crux2 - how late the console's sleeps wake, measured once at start, and the frame limiter's
// wait: the runtime of PS5Timing.h.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0+
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
//
// Kelvin: the PS5Timing part of Crux2 v1.0.14's ps5/common/PS5Misc.cpp
// (github.com/Press5elect/Crux2 710d573). Changed for Kelvin: PCSX2's GetCPUTicks() is the
// title's clock (ps5px_monotonic_ns, ps5_clock.c, in nanoseconds as on Crux2), Console.WriteLn
// is ps5px_log, Threading::SpinWait is a pause, and Threading::Sleep/SleepUntil are
// PS5Timing::SleepNs/SleepUntilNs (ps5_timing.h). Kelvin by Press5elect.

#include "ps5_timing.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sched.h>
#include <thread>

extern "C" int sceKernelUsleep(unsigned microseconds);
extern "C" uint64_t ps5px_monotonic_ns(void); // ps5_clock.c
extern "C" void ps5px_log(const char* format, ...);

namespace
{
uint64_t Now()
{
	return ps5px_monotonic_ns();
}

void SpinWait()
{
	__builtin_ia32_pause();
}
} // namespace

// What the start-up probe found (PS5Timing.h): the margin the frame limiter waits out awake, and
// which call sleeps.
static std::atomic<uint64_t> s_spin_margin_ns{PS5Timing::MARGIN_DEFAULT_NS};
static std::atomic<bool> s_sleep_with_usleep{false};

// One sleep with nanosleep. A sleep that a signal cut short goes on for what is left of it: a
// profiled test run interrupts its threads every two milliseconds (frontend/ps5_profiler.cpp), and
// a sleep that ended there would turn every wait into a busy one.
static bool NanosleepFor(uint64_t ns)
{
	const uint64_t until = Now() + ns;
	for (;;)
	{
		struct timespec ts;
		ts.tv_sec = static_cast<time_t>(ns / 1000000000ULL);
		ts.tv_nsec = static_cast<long>(ns % 1000000000ULL);
		if (nanosleep(&ts, nullptr) == 0)
			return true;
		if (errno != EINTR)
			return false;
		const uint64_t now = Now();
		if (now >= until)
			return true;
		ns = until - now;
	}
}

// The same with sceKernelUsleep, which takes microseconds and answers with an error code.
static bool UsleepFor(uint64_t ns)
{
	const uint64_t until = Now() + ns;
	for (;;)
	{
		const uint64_t us = (ns + 999) / 1000;
		const int r = sceKernelUsleep(static_cast<unsigned>(std::min<uint64_t>(us, 0xffffffffULL)));
		if (r == 0)
			return true;
		const uint64_t now = Now();
		if (now >= until)
			return true;
		// An interrupted sleep goes on; any other failure falls back to nanosleep for the rest.
		if (r != EINTR && r != static_cast<int>(0x80020004))
			return NanosleepFor(until - now);
		ns = until - now;
	}
}

static void SleepFor(uint64_t ns)
{
	if (s_sleep_with_usleep.load(std::memory_order_relaxed))
		UsleepFor(ns);
	else
		NanosleepFor(ns);
}

uint64_t PS5Timing::SpinMarginNs()
{
	return s_spin_margin_ns.load(std::memory_order_relaxed);
}

bool PS5Timing::SleepsWithUsleep()
{
	return s_sleep_with_usleep.load(std::memory_order_relaxed);
}

static void (*s_timing_report)(const char*) = nullptr;

void PS5Timing::StartProbe()
{
	static std::atomic<bool> started{false};
	if (started.exchange(true))
		return;
	std::thread([] {
		const auto now = [] { return Now(); };
		const Measured nano = Measure([](uint64_t ns) { return NanosleepFor(ns); }, now);
		const Measured usl = Measure([](uint64_t ns) { return UsleepFor(ns); }, now);
		const bool second = PreferSecond(nano, usl);
		const Measured& taken = second ? usl : nano;
		const uint64_t margin = taken.ok ? MarginFor(taken.lateness) : MARGIN_DEFAULT_NS;
		s_sleep_with_usleep.store(second, std::memory_order_relaxed);
		s_spin_margin_ns.store(margin, std::memory_order_relaxed);
		char line[256];
		std::snprintf(line, sizeof(line),
			"timing: sleeps wake late by %.2f ms typically, %.2f ms at worst (nanosleep %.2f/%.2f, "
			"sceKernelUsleep %.2f/%.2f ms at 90%%/worst); the frame limiter waits the last %.2f ms awake",
			taken.p50 / 1e6, taken.worst / 1e6, nano.p90 / 1e6, nano.worst / 1e6, usl.p90 / 1e6, usl.worst / 1e6,
			margin / 1e6);
		if (s_timing_report)
			s_timing_report(line);
		else
			ps5px_log("%s", line);
	}).detach();
}

void PS5Timing::SetReport(void (*report)(const char*))
{
	s_timing_report = report;
}

void PS5Timing::SleepNs(uint64_t ns)
{
	if (ns > 0)
		SleepFor(ns);
}

static std::mutex s_limiter_lock;
static PS5Timing::LimiterWaits s_limiter;

void PS5Timing::TakeLimiterLine(char* line, size_t size)
{
	if (!line || size == 0)
		return;
	line[0] = '\0';
	PS5Timing::LimiterWaits w;
	{
		std::lock_guard<std::mutex> lock(s_limiter_lock);
		w = s_limiter;
		s_limiter = PS5Timing::LimiterWaits();
	}
	if (w.waits == 0)
		return;
	const double waited = static_cast<double>(w.asleep_ns + w.awake_ns);
	std::snprintf(line, size,
		"frame limiter: %llu waits, %.2f ms asleep and %.2f ms awake each on average (%.0f%% awake, giving its turn "
		"away but for the last %.2f ms); %llu woke past their end, by %.2f ms at most",
		static_cast<unsigned long long>(w.waits), w.asleep_ns / 1e6 / w.waits, w.awake_ns / 1e6 / w.waits,
		waited > 0 ? 100.0 * w.awake_ns / waited : 0.0, PS5Timing::SPIN_LAST_NS / 1e6,
		static_cast<unsigned long long>(w.late), w.latest_ns / 1e6);
}

void PS5Timing::SleepUntilNs(uint64_t end_ns)
{
	// No clock_nanosleep() with an absolute time on the console. The frame limiter calls this once a
	// frame and wants to wake on time: sleep for what is left less the margin the start-up probe
	// measured for the kernel's wake-up, then wait out the margin awake, giving the core to the other
	// threads while more than SPIN_LAST_NS is left and spinning only the end (PS5Timing.h).
	PS5Timing::LimiterWaits waits;
	PS5Timing::WaitUntil(
		end_ns, PS5Timing::SpinMarginNs(), [] { return Now(); }, [](uint64_t ns) { SleepFor(ns); },
		[] { sched_yield(); }, [] { SpinWait(); }, waits);
	if (waits.waits == 0)
		return;
	std::lock_guard<std::mutex> lock(s_limiter_lock);
	s_limiter.Add(waits.asleep_ns, waits.awake_ns, waits.late ? waits.latest_ns : 0);
}
