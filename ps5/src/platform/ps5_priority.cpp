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
// PSHakuX: from Kelvin 3061bf1; adds the Helper role (690) and pshakux_priority_set for the C code
// of the core's glue. PSHakuX by Press5elect.

#include "ps5_priority.h"

#include <atomic>
#include <pthread.h>
#include <sched.h>

extern "C" void ps5px_log(const char* format, ...);

namespace PS5Priority
{
namespace
{
// PSHakuX 0.1.1: pthread_getschedparam / pthread_setschedparam, which the console honours (the
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
		case Role::Sound: return 690;
		case Role::Input: return 692;
		case Role::Interface: return 695;
		case Role::Emulation: return 700;
		case Role::Helper: return 690;
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
	const unsigned bit = 1u << static_cast<unsigned>(role);
	if ((s_said.fetch_or(bit, std::memory_order_relaxed) & bit) == 0)
	{
		int after = -1;
		GetPriority(pthread_self(), &after);
		ps5px_log("priority: a %s thread from %d to %d: %#x (now %d)", NameOf(role), before, wanted,
			static_cast<unsigned>(result), after);
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

extern "C" void pshakux_priority_enable(int on)
{
	PS5Priority::SetEnabled(on != 0);
}
