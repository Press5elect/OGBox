/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * The title's arguments, and the title starting itself again with new ones.
 *
 * Measured on the console (PPSA99251, 2026-11-01): sceSystemServiceLoadExec("/app0/eboot.bin",
 * argv), called through the title's own import (a run-time lookup of it by name fails), ends the
 * title and starts it again, and the new instance's main() gets exactly the strings passed:
 * argc 3 for three, argv[0] being the FIRST passed argument (there is no program name), with
 * spaces kept. So the arguments are read from argv[0] on, and nothing is skipped.
 *
 * hakuX's core has no teardown yet (a run cannot be stopped and another booted in the same
 * process), so a new boot is a relaunch: quitting a game back to the library, loading a
 * savestate (--boot <image> --loadstate <file>) and the guest's own reboot into another XBE
 * (--boot <image> --relaunch) all start the title again with the arguments that say what to do.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The first argument of every relaunch, so the new instance knows it is one. */
#define PSHAKUX_RELAUNCHED "--pshakux-relaunched"

/* From main(), as it got them (argv[0] is the first argument: see above); logged. */
void pshakux_args_set(int argc, char **argv);
int pshakux_argc(void);
/* The i-th argument (0 is the first), NULL past the end. */
const char *pshakux_argv(int i);
/* Whether this instance was started by pshakux_relaunch. */
int pshakux_relaunched(void);

/* Ends the title and starts it again with PSHAKUX_RELAUNCHED and args (a NULL-terminated list,
 * at most 30). The boot log is drained first. Returns only when the console refused (its error
 * code); the caller says so and goes on. */
int pshakux_relaunch(const char *const *args);

#ifdef __cplusplus
}
#endif
