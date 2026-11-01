/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * The Android log, for hakuX files that write to it directly: one line on standard error, which
 * the boot log (pshakux-boot.log) takes and copies on to klog (ps5/src/platform/ps5_bootlog.cpp).
 */
#ifndef PSHAKUX_ANDROID_LOG_H
#define PSHAKUX_ANDROID_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0, ANDROID_LOG_DEFAULT, ANDROID_LOG_VERBOSE, ANDROID_LOG_DEBUG,
    ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR, ANDROID_LOG_FATAL, ANDROID_LOG_SILENT,
} android_LogPriority;

int __android_log_write(int prio, const char *tag, const char *text);
int __android_log_print(int prio, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif
