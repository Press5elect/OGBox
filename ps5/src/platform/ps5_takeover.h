// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.4 - the takeover: 0x80000000-0x80200000 from the title's own sce_module/libc.prx.
//
// The console's loader puts the title's libc.prx (PS5_Vulkan's clean-room C runtime) at 0x80000000,
// where the Xbox's kernel image is (0x80010000; games built with the XDK read its PE header there).
// 0.1.3 left 0x80000000-0x80120000 to it and moved the guest's kernel past it, so those games were
// ended by the console at their first read (an execute-only page: SYSTEM_XO_VIOLATION, no crash
// report). This is the system check's step that took the range back (PSHakuX System Check 0.0.5,
// src/cpu/hxtakeover.inc, proven on the console): find every pointer into libc.prx that something
// outside it holds, point each at an equivalent in the eboot (a copy of the module's data, a stub
// for its code), and only then let the guest's memory be mapped over it (hakuX-native's vmhost, at
// the core's start). What still calls the old module afterwards (libkernel's thread destructors at
// +0x200, at every thread exit) finds stubs the core writes into guest RAM there
// (src/core/hakux_core.c).
//
// Every step is said in the boot log ("takeover:" lines) before it is done.
//
// Flag files (in /data/ogbox/ or the title folder): takeover-off (not tried: the 0.1.3 layout),
// takeover-force (done even when the scan finds pointers it would not take on its own).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Early in main(), after the boot log opens and before the title starts threads of its own:
/// the scan, the copy of libc.prx's data and the rewrite. Nothing is mapped. 1 when done (the core
/// may then map the guest's window over 0x80000000), 0 when not (the reason is logged).
int pshakux_takeover_prepare(void);

/// 1 when pshakux_takeover_prepare succeeded.
int pshakux_takeover_done(void);

/// Why it was not done (empty when it was).
const char* pshakux_takeover_reason(void);

/// libc.prx's code entries (offsets from 0x80000000) other code may still call after the takeover,
/// and DT_FINI's (inside what becomes guest RAM).
#define PSHAKUX_LIBC_DT_FINI 0xc8010u
extern const unsigned pshakux_libc_entries[];
extern const unsigned pshakux_libc_entry_count;

/// The title is ending (the shell exit, a relaunch): the core's hook (pshakux_takeover_set_exit_hook)
/// makes the old module's entries safe once more. From the thread that ends the title.
void pshakux_takeover_before_exit(const char* why);
void pshakux_takeover_set_exit_hook(void (*hook)(const char* why));

#ifdef __cplusplus
}
#endif
