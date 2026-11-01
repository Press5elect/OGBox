/*
 * Kelvin - original Xbox emulator for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPLv3 section 7(b)): credit to Press5elect, see LICENSE.md
 *
 * ps5px_log, the one-line boot-log call the code from Crux2 makes (ps5_modules.cpp,
 * ps5_priority.cpp, ps5_timing.cpp): one line to the boot log and klog (pshakux_log,
 * ps5_bootlog.cpp); and the two names radv-crux2's driver calls in the title
 * (ps5_title_thread_named, ps5_title_breadcrumb).
 */
#include "ps5_bootlog.h"

#include <stdatomic.h>

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

void ps5px_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

void
ps5px_log(const char *format, ...)
{
   char line[1024];
   va_list args;
   va_start(args, format);
   vsnprintf(line, sizeof(line), format, args);
   va_end(args);
   pshakux_log("%s", line);
}

/* The graphics driver's recording thread names itself as it starts (radv-crux2: Crux2's Mesa patch
 * 0002 calls this function, which it declares weak). A weak import is one the native tool cannot
 * convert, so the title defines it, as Crux2 does (ps5/frontend/ps5_profiler.cpp, which also gives
 * the thread a crash stack and joins it to the profiler; Kelvin has no profiler yet): one line to
 * the boot log, a crash stack, and the name for the crash report (copied: up to 8 driver threads). */
void ps5_title_thread_named(const char *name);

void
ps5_title_thread_named(const char *name)
{
   static char names[8][32];
   static atomic_int used;
   ps5px_log("driver thread started: %s", name ? name : "(unnamed)");
   pshakux_install_crash_stack();
   const int slot = atomic_fetch_add(&used, 1);
   if (slot < 8) {
      snprintf(names[slot], sizeof(names[slot]), "radv: %s", name ? name : "(unnamed)");
      pshakux_thread_named(names[slot]);
   }
}

/* The graphics driver's start-up steps (radv-crux2: Crux2's Mesa patch 0001, diagnostic
 * breadcrumbs, also a weak name): each to klog at once, so the last line names the step a silent
 * stop happened in (as Crux2's ps5/frontend/ps5_platform.cpp does). */
void ps5_title_breadcrumb(const char *text);

void
ps5_title_breadcrumb(const char *text)
{
   ps5px_log("radv: %s", text ? text : "");
}

/* PSHakuX: what Crux2's host gives its port layer (ps5/frontend/ps5_host.cpp): a stop with a
 * message, for a state the title cannot go on from (src/platform/ps5_libc.c's setjmp guard). The
 * message goes to the boot log, and abort() writes the crash report after it. */
void ps5_title_fatal(const char *message) __attribute__((noreturn));

void
ps5_title_fatal(const char *message)
{
   pshakux_log("FATAL: %s", message ? message : "(no message)");
   abort();
}

long
pshakux_sysconf(int name)
{
#if defined(__PROSPERO__)
   switch (name) {
   case _SC_NPROCESSORS_ONLN:
   case _SC_NPROCESSORS_CONF:
      return 13;
   case _SC_PAGESIZE:
      return 16384;
   default:
      errno = EINVAL;
      return -1;
   }
#else
   return sysconf(name);
#endif
}
