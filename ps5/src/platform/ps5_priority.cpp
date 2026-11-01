// Crux2 - which of the title's threads the console runs first: the runtime of ps5_priority.h.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
//
// Kelvin: from Crux2 v1.0.14 (github.com/Press5elect/Crux2 710d573, ps5/frontend/ps5_priority.cpp).
// Changed for Kelvin: the switch is SetEnabled() instead of PCSX2's Host setting, and the two
// kernel functions are found with ps5_modules.h (the part of Crux2's PS5SystemModules this needs).
// Kelvin by Press5elect.
//
// OGBox: from Kelvin 3061bf1; adds the Helper role (690) and pshakux_priority_set for the C code
// of the core's glue. OGBox by Press5elect.

#include "ps5_priority.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sched.h>

#include <ps5platform/kernel.h>   // scePthreadSetaffinity

extern "C" void ps5px_log(const char* format, ...);

namespace PS5Priority
{
namespace
{
// OGBox 0.1.1: pthread_getschedparam / pthread_setschedparam, which the console honours (the
// boot log's watcher line shows 700 -> 690 taking effect). 0.1.0 looked scePthreadSetprio up at
// run time; the lookup cannot find libkernel in this title, so no role was ever applied.
int GetPriority(pthread_t thread, int* priority)
{
	int policy = 0;
	sched_param param{};
	const int r = pthread_getschedparam(thread, &policy, &param);
	*priority = r == 0 ? param.sched_priority : -1;
	return r;
}

int SetPriority(pthread_t thread, int priority)
{
	int policy = 0;
	sched_param param{};
	int r = pthread_getschedparam(thread, &policy, &param);
	if (r != 0)
		return r;
	param.sched_priority = priority;
	return pthread_setschedparam(thread, policy, &param);
}

int PriorityOf(Role role)
{
	switch (role)
	{
		/* OGBox 0.1.17: as hakuX has them (it sets no priority but its pipeline compiles', lowered):
		 * every thread at the title's own 700, the sound output first (it only sleeps and copies),
		 * background jobs after. Up to 0.1.16 fourteen helpers ran at 690, ahead of the guest's
		 * CPU at 700, and the console's scheduler keeps a processor for the higher priority
		 * strictly: Def Jam's boot log showed "vCPU starved: 4 ms CPU of 59 (host)" frame after
		 * frame from its EA logo on, its 60 Hz interrupt 338 ms late, and the game stopped. */
		case Role::Sound: return 690;
		case Role::Input: return 700;
		case Role::Interface: return 700;
		case Role::Emulation: return 700;
		case Role::Helper: return 700;
		case Role::Background: return 730;
	}
	return 700;
}

const char* NameOf(Role role)
{
	switch (role)
	{
		case Role::Sound: return "sound";
		case Role::Input: return "controllers";
		case Role::Interface: return "interface";
		case Role::Emulation: return "emulation";
		case Role::Helper: return "helper";
		case Role::Background: return "background job";
	}
	return "?";
}

std::atomic<unsigned> s_said{0};
std::atomic<bool> s_enabled{false};

/* OGBox 0.1.17: where the threads run, after Crux2's thread layout (ps5_thread_layout.h, its
 * EE/VU/GS cores): the guest's CPU on logical processor 0 alone (core 0; its second hardware
 * thread, processor 1, left empty), every other thread of the title on processors 2-12 (the title
 * has 0-12), so nothing shares the guest CPU's core whatever it does. env OGBOX_THREAD_LAYOUT=0 in
 * boot.txt leaves the console's own placement. */
/* 0.1.18: the HLE GPU's two threads on cores of their own too, as Crux2's GS: the push-buffer
 * decoder (hle-pb) on core 1 (processors 2-3), the render thread (hle.render) on core 2 (4-5),
 * everything else on cores 3-6 (6-12). */
constexpr uint64_t kVcpuMask = 0x1u;
constexpr uint64_t kDecoderMask = 0xCu;
constexpr uint64_t kRenderMask = 0x30u;
constexpr uint64_t kOthersMask = 0x1FC0u;

uint64_t MaskFor(bool vcpu, const char* name)
{
	if (vcpu)
		return kVcpuMask;
	if (name && std::strcmp(name, "hle-pb") == 0)
		return kDecoderMask;
	if (name && std::strcmp(name, "hle.render") == 0)
		return kRenderMask;
	return kOthersMask;
}

bool LayoutWanted()
{
	static const bool wanted = [] {
		const char* e = std::getenv("OGBOX_THREAD_LAYOUT");
		return !(e && e[0] == '0');
	}();
	return wanted;
}

int Pin(pthread_t thread, bool vcpu, const char* name = nullptr)
{
	if (!LayoutWanted())
		return 0;
	return scePthreadSetaffinity(thread, MaskFor(vcpu, name));
}
} // namespace

void SetEnabled(bool on)
{
	s_enabled.store(on, std::memory_order_relaxed);
}

void SetCurrentThread(Role role)
{
	if (!s_enabled.load(std::memory_order_relaxed))
		return;
	const int wanted = PriorityOf(role);
	int before = -1;
	GetPriority(pthread_self(), &before);
	const int result = SetPriority(pthread_self(), wanted);
	const int pinned = Pin(pthread_self(), role == Role::Emulation);
	const unsigned bit = 1u << static_cast<unsigned>(role);
	if ((s_said.fetch_or(bit, std::memory_order_relaxed) & bit) == 0)
	{
		int after = -1;
		GetPriority(pthread_self(), &after);
		ps5px_log("priority: a %s thread from %d to %d: %#x (now %d); processors %#llx: %#x", NameOf(role), before,
			wanted, static_cast<unsigned>(result), after,
			static_cast<unsigned long long>(!LayoutWanted() ? 0 : MaskFor(role == Role::Emulation, nullptr)),
			static_cast<unsigned>(pinned));
	}
}
} // namespace PS5Priority

extern "C" void pshakux_priority_set(int role)
{
	using PS5Priority::Role;
	static const Role roles[] = {Role::Sound, Role::Input, Role::Interface, Role::Emulation, Role::Helper,
		Role::Background};
	if (role >= 0 && role < static_cast<int>(sizeof(roles) / sizeof(roles[0])))
		PS5Priority::SetCurrentThread(roles[role]);
}

// OGBox 0.1.3: a thread of hakuX's by its name, set by itself (its entry function's role) or by
// its creator (the name hakuX gives it after pthread_create). Every thread is said in the boot log,
// once: "thread NAME: ROLE, priority BEFORE -> WANTED: RESULT (now AFTER)".
extern "C" void pshakux_priority_set_thread(pthread_t thread, int role, const char* name, const char* how)
{
  using PS5Priority::Role;
  static const Role roles[] = {Role::Sound, Role::Input, Role::Interface, Role::Emulation, Role::Helper,
    Role::Background};
  int before = -1, after = -1;
  PS5Priority::GetPriority(thread, &before);
  if (role < 0 || role >= static_cast<int>(sizeof(roles) / sizeof(roles[0])))
  {
    const int pinned = PS5Priority::Pin(thread, false, name);
    ps5px_log("thread %s (%s): no role, stays at priority %d; processors %#llx: %#x", name ? name : "?", how ? how : "",
      before, static_cast<unsigned long long>(PS5Priority::MaskFor(false, name)), static_cast<unsigned>(pinned));
    return;
  }
  const Role r = roles[role];
  const int wanted = PS5Priority::PriorityOf(r);
  int result = 0;
  const bool on = PS5Priority::s_enabled.load(std::memory_order_relaxed);
  if (on)
    result = PS5Priority::SetPriority(thread, wanted);
  const bool vcpu = r == Role::Emulation;
  const int pinned = PS5Priority::Pin(thread, vcpu, name);
  PS5Priority::GetPriority(thread, &after);
  ps5px_log("thread %s (%s): %s, priority %d -> %d: %#x (now %d)%s; processors %#llx: %#x", name ? name : "?",
    how ? how : "", PS5Priority::NameOf(r), before, wanted, static_cast<unsigned>(result), after,
    on ? "" : " [priorities off]", static_cast<unsigned long long>(PS5Priority::MaskFor(vcpu, name)),
    static_cast<unsigned>(pinned));
}

extern "C" void pshakux_priority_enable(int on)
{
	PS5Priority::SetEnabled(on != 0);
}
