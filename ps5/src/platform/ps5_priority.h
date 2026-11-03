// Crux2 - which of the title's threads the console runs first ([PS5] ThreadPriorities).
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
//
// Every thread of a title starts at the same priority (700 of 256 to 767, lower runs first), and
// the console's scheduler gives a processor to the higher priority strictly: a thread that never
// waits keeps its processor from every thread of its priority or below. (Measured by PS5_MihawkOS's
// scheduler probe, Copyright (C) 2026 Mihawk, GPL-3.0-or-later: with every processor busy, a
// periodic thread at the title's priority made no progress at all.) The emulator's EE, VU and GS
// threads spin while they wait for each other, so a sound thread, the controllers or the interface
// could wait behind them, and a background job (a download, an unpacking, card sync, the game
// list) could take a processor from them.
//
// A thread starts at the priority of the thread that started it, so the emulation thread sets the
// title's own (700) again: the GS, VU and software renderer threads it starts are then at 700 too.
//
// So: the sound threads and the microphones run first, then the controllers, then the interface;
// the emulator's threads stay where every thread starts; background jobs run after them all. Each
// thread sets its own, when it starts. scePthreadSetprio is found while the title runs (not
// imported: a title that imports what the console does not give it does not start).
//
// Kelvin: from Crux2 v1.0.14 (github.com/Press5elect/Crux2 710d573, ps5/frontend/ps5_priority.h).
// Kelvin has no settings store yet, so [PS5] ThreadPriorities is SetEnabled() here (off until
// called, as Crux2's setting is off by default). In Kelvin the roles map to: Sound - the APU/
// AC97 output; Input - the controllers; Interface - the main thread; Emulation - the CPU
// (x86 guest) thread and the NV2A thread it starts; Background - disc image scans, unpacking.

#pragma once

#include <pthread.h>

#ifdef __cplusplus

namespace PS5Priority
{
enum class Role
{
	Sound,      // 690: the sound output streams, the interface's sounds, the microphones
	Input,      // 692: the USB controllers' thread
	Interface,  // 695: the main thread (the interface, the host's loop)
	Emulation,  // 700: the emulation thread, and so the threads it starts (GS, VU, software renderer)
	Helper,     // 690: OGBox: the core's helpers (the HLE GPU's render and pipeline threads, the APU)
	Background, // 730: downloads, unpacking, copies, card sync, the game list, the web page
};

/// The calling thread runs at its role's priority, unless [PS5] ThreadPriorities is off.
/// Said once per role in the boot log ("priority: ...").
#ifdef __PROSPERO__
void SetCurrentThread(Role role);
/// Kelvin: [PS5] ThreadPriorities (off until set).
void SetEnabled(bool on);
#else
// The PC-side tests build the files that call this: nothing to set there.
inline void SetCurrentThread(Role) {}
inline void SetEnabled(bool) {}
#endif
} // namespace PS5Priority

// OGBox: the roles for C code (the core's glue, ps5/hakux): OGBox's thread layout is the vCPU
// at 700 (Emulation), its helpers at 690 (Helper) and background jobs at 730 (Background).
extern "C" {
#endif
enum { PSHAKUX_ROLE_SOUND, PSHAKUX_ROLE_INPUT, PSHAKUX_ROLE_INTERFACE, PSHAKUX_ROLE_VCPU,
       PSHAKUX_ROLE_HELPER, PSHAKUX_ROLE_BACKGROUND };
void pshakux_priority_set(int role);
/* OGBox 0.1.3: the role of a thread (itself or another), said in the boot log once with its
 * name and priority; role -1: none (said so). how: by what the role was known. */
void pshakux_priority_set_thread(pthread_t thread, int role, const char *name, const char *how);
/* [PS5] ThreadPriorities: off until enabled (as Crux2's setting). */
void pshakux_priority_enable(int on);
/* OGBox 0.0.1: the GPU's threads on cores of their own (the setting), and the caller placed again */
void pshakux_priority_gpu_pin(int on);
#ifdef __cplusplus
}
#endif
