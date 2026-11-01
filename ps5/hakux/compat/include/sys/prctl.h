/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * Linux's prctl, for the two options hakuX uses: PR_SET_NAME names the calling thread, and
 * PR_SET_TIMERSLACK (the core's 1 us slack for its timers) has no equal on the console, whose
 * sleeps the title's clock code measures instead (ps5/src/platform/PS5Timing.h): it succeeds.
 */
#ifndef PSHAKUX_SYS_PRCTL_H
#define PSHAKUX_SYS_PRCTL_H

#include <pthread.h>
#include <pthread_np.h>
#include <stdarg.h>

#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_TIMERSLACK 29

static inline int prctl(int option, ...)
{
    if (option == PR_SET_NAME) {
        va_list ap;
        va_start(ap, option);
        const char *name = (const char *)va_arg(ap, unsigned long);
        va_end(ap);
        pthread_set_name_np(pthread_self(), name);
    }
    return 0;
}

#endif
