/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * The title's arguments and its relaunch (ps5_relaunch.h says what the console was measured to do).
 */
#include "ps5_relaunch.h"
#include "ps5_bootlog.h"
#include "ps5_takeover.h"

#include <string.h>

/* The SDK's libSceSystemService stub: imported, as the template's exit is. A lookup of it by name
 * at run time fails on the console; the import works. */
int sceSystemServiceLoadExec(const char *path, const char *const *argv);

#define ARGS_MAX 32

static int g_argc;
static char **g_argv;

void pshakux_args_set(int argc, char **argv)
{
    g_argc = argc > 0 && argv ? argc : 0;
    g_argv = argv;
    pshakux_log("arguments: %d%s", g_argc, pshakux_relaunched() ? " (a relaunch)" : "");
    for (int i = 0; i < g_argc; i++) {
        pshakux_log("  argv[%d] = \"%s\"", i, g_argv[i] ? g_argv[i] : "(null)");
    }
}

int pshakux_argc(void)
{
    return g_argc;
}

const char *pshakux_argv(int i)
{
    return i >= 0 && i < g_argc ? g_argv[i] : NULL;
}

int pshakux_relaunched(void)
{
    for (int i = 0; i < g_argc; i++) {
        if (g_argv[i] && !strcmp(g_argv[i], PSHAKUX_RELAUNCHED)) {
            return 1;
        }
    }
    return 0;
}

int pshakux_relaunch(const char *const *args)
{
    const char *argv[ARGS_MAX];
    int n = 0;
    argv[n++] = PSHAKUX_RELAUNCHED;
    for (int i = 0; args && args[i] && n < ARGS_MAX - 1; i++) {
        argv[n++] = args[i];
    }
    argv[n] = NULL;
    pshakux_log("relaunch: /app0/eboot.bin with %d arguments", n);
    for (int i = 0; i < n; i++) {
        pshakux_log("  argv[%d] = \"%s\"", i, argv[i]);
    }
    pshakux_takeover_before_exit("the relaunch");   /* 0.1.4: libc.prx's old entries made safe again */
    pshakux_diag_drain();
    const int result = sceSystemServiceLoadExec("/app0/eboot.bin", argv);
    pshakux_log("relaunch: sceSystemServiceLoadExec answered 0x%08x: the title goes on", (unsigned)result);
    return result;
}
