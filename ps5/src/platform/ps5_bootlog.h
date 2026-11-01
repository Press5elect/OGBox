// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
// Crux2 - the console side of the port: where the title's folder is, the boot log, and how the
// title ends.
//
// Kelvin: the boot log and crash report part of Crux2 v1.0.15's ps5/frontend/ps5_platform.h
// (github.com/Press5elect/Crux2 228d0a5). Changed for Kelvin: C names with a pshakux_ prefix (the
// probe and the template's code are C), no sandbox elevation, USB drives or network shares, and
// what Kelvin adds: the heartbeat, the probe's lines and the session line. Built for the console
// and for the host reference build (ps5/tools/host-reference.sh), where the title folder is
// PS5_APP_ROOT. Kelvin by Press5elect.
//
// The boot log, ogbox-boot.log in the title folder (/data/homebrew/PPSA99251/ on the console):
//   - opened first thing in main(), before the probe, mode 0666; the previous run's is kept as
//     ogbox-boot.previous.log (Crux2's scheme: a crash report is the last thing in it);
//   - every line is written with write(2) as it happens, nothing is buffered in the title;
//   - standard error goes into it too, a line at a time (the template's say(), the driver's
//     messages, std::cerr), and a watcher thread copies those lines on to klog;
//   - a header: build identity, firmware, cpu and clock, memory, title folder, eboot address;
//   - "probe:" lines, a heartbeat ("alive:") and, at the end of a clean run, "session:".

#pragma once

#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// First thing in main(), while the process has one thread: opens the boot log (the previous
/// one kept), moves standard error into it, writes the header, installs the crash report
/// (ps5_crash.cpp) and starts the watcher thread (klog copy and heartbeat).
void pshakux_diag_start(const char* title_name);

/// One line to the boot log ("[   1.234] text") and to klog. From any thread; not from a
/// signal handler (the crash report has its own writer).
void pshakux_log(const char* format, ...) __attribute__((format(printf, 1, 2)));

/// The boot log's descriptor (-1: none), for the crash report.
int pshakux_log_fd(void);

/// Seconds since pshakux_diag_start, from the monotonic clock.
double pshakux_uptime(void);

/// The title's folder: /app0 on the console, PS5_APP_ROOT in the host reference build.
const char* pshakux_title_dir(void);

/// Whether the boot log has standard error (platform_init then leaves klog capture to it).
int pshakux_diag_owns_stderr(void);

/// A line of text to klog as it is (the console: sceKernelDebugOutText; the host reference
/// build: the terminal's standard error). Async-signal-safe.
void pshakux_klog_raw(const char* text, unsigned length);

/// The calling thread's name for the crash report ("main", "ogbox-watch", "probe-busy" ...).
/// Up to 64 threads; the name must stay valid (a literal).
void pshakux_thread_named(const char* name);
/// OGBox 0.1.3: the same for another thread (a thread its creator names; hakuX's helpers).
void pshakux_thread_named_for(pthread_t thread, const char* name);
/// The calling thread is ending: its name is let go (a later thread may get its pthread_t).
void pshakux_thread_gone(void);
/// The calling thread's name, or NULL. Async-signal-safe.
const char* pshakux_thread_name(void);

/// The calling thread's id as the kernel knows it (-1 when it cannot be had). Async-signal-safe.
long pshakux_thread_id(void);

/// A stack of the thread's own for the crash report's handler, so a stack overflow is reported
/// too (ps5_crash.cpp). pshakux_diag_start gives the main thread one, the watcher its own.
void pshakux_install_crash_stack(void);

/// Installs the crash report for the fatal signals and std::terminate (ps5_crash.cpp).
void pshakux_crash_install(void);

/// OGBox: the core's lines in a crash report (where the guest was, which device it touched...).
/// Called in the signal handler: it must be async-signal-safe (no locks, no allocation, no stdio
/// but snprintf into line).
typedef void (*pshakux_crash_note_fn)(char* line, unsigned size);
void pshakux_crash_set_note(pshakux_crash_note_fn note);
/// The code segment the guest's 32-bit code runs in (the native CPU backend's), so a fault in it
/// is reported as guest code (eip, esp, the guest stack); 0: none.
void pshakux_crash_set_guest_cs(uint16_t cs);

/// Where the eboot was loaded (its first byte; 0 when not known), and the address it was linked
/// at (0x30_0000_0000 on the console, 0 for the host's PIE): an address in a crash report is
/// looked up in the link's ELF at link base + (address - eboot start). (ps5_crash.cpp)
uintptr_t pshakux_eboot_start(void);
uintptr_t pshakux_eboot_link_base(void);

/// What the heartbeat ("alive:") and the session line say beside the time: the provider writes one
/// line (the guest's state, its frames...) into line. Called on the watcher thread, so it must only
/// read what other threads publish. fast: a heartbeat every 0.25 s rather than every 2 s (a risky
/// step, as bring-up is). NULL: the time only.
typedef void (*pshakux_status_fn)(char* line, unsigned size);
void pshakux_diag_set_status(pshakux_status_fn status, int fast);

/// At the end of a clean run: the "session:" line, the watcher stopped and drained.
void pshakux_diag_session_end(int status);

/// Copies what standard error has written since the watcher last looked on to klog, now.
void pshakux_diag_drain(void);

/// OGBox: sysconf without the console's, which a title is not given (measured 2026-11-01):
/// _SC_NPROCESSORS_ONLN and _CONF answer 13 (the processors a title's threads run on),
/// _SC_PAGESIZE and _SC_PAGE_SIZE 16384 (the console's page); anything else -1 with EINVAL.
long pshakux_sysconf(int name);

/// What tools/build-identity.sh recorded for this build (ps5_build_stamp.cpp).
const char* pshakux_build_stamp(void);
const char* pshakux_build_identity(void);
const char* pshakux_build_version(void);
const char* pshakux_build_revision(void);
int pshakux_build_dirty(void);
const char* pshakux_build_radv(void);
const char* pshakux_build_sdk(void);
/// hakuX's revision (and "+dirty <digest>" for a work tree with changes), and the CPU backend.
const char* pshakux_build_hakux(void);
const char* pshakux_build_cpu(void);

#ifdef __cplusplus
}
#endif
