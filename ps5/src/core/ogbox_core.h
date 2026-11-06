/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The Core: the front end's one door into the emulator. Everything the title's front end does
 * with hakuX goes through these calls, and nothing else of hakuX is included by it: the bring-up
 * front end (ps5/src/ogbox_main.cpp) now, the full front end (Crux2's bigpicture interface,
 * with a game library, settings and the quick menu) later, on the same calls. Its implementation
 * for hakuX is src/core/hakux_core.c.
 *
 * The life of a run: pshx_core_configure (once: what boots and how), pshx_core_start (the vCPU
 * thread boots the title and runs it), then each display refresh pshx_core_take_frame and
 * pshx_core_set_pad, until the state is PSHX_CORE_STOPPED or PSHX_CORE_FAILED or the user quits.
 * hakuX's core has no way to stop and start again in the same process yet: a new boot is a relaunch
 * of the title with the arguments that say what to boot (src/platform/ps5_relaunch.h: quitting a
 * game back to the library, loading a savestate with "loadstate=<file>", the guest's own reboot
 * into another XBE with "relaunch"), and quitting ends the title through the shell.
 *
 * Threads: configure and start from the main thread; the rest from any thread unless said.
 */
#ifndef PSHAKUX_CORE_H
#define PSHAKUX_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum PshxCoreState {
    PSHX_CORE_IDLE,      /* configured or not, not started */
    PSHX_CORE_BOOTING,   /* the vCPU thread is booting the title */
    PSHX_CORE_RUNNING,   /* the title runs */
    PSHX_CORE_STOPPED,   /* the title ended (left to the dashboard, rebooted, halted) */
    PSHX_CORE_FAILED,    /* the boot or the run failed: pshx_core_message says why */
};

/* What runs: the disc image (an Xbox ISO, "XISO"; "none" boots the dashboard from the hard
 * disk), the hard disk's folder (Partition0-5 are made in it), the folder for the emulator's
 * own files (shader and pipeline caches), and hakuX's options as its front ends take them
 * (native/core/options.h: "scale=2", "uncap", "video=...", "lang=..."...). Options the core does
 * not know are said in the boot log and left out. false: nothing can boot (why in message). */
typedef struct PshxCoreConfig {
    const char *image;
    const char *hdd_root;
    const char *data_root;
    int argc;
    const char *const *argv;
} PshxCoreConfig;
bool pshx_core_configure(const PshxCoreConfig *config);

/* Starts the vCPU thread (16 MiB of stack, priority 700): it boots the title, then runs it. */
bool pshx_core_start(void);

int pshx_core_state(void);
/* The run stopped for the guest's reboot into another XBE (XLaunchNewImage): the front end
 * relaunches the title with the same disc and the option "relaunch" (hakuX boots the XBE its
 * launch data names). */
bool pshx_core_relaunch_wanted(void);
/* Why the run failed or stopped, for the screen ("" when there is nothing to say). */
const char *pshx_core_message(void);
/* The core's name and its revision, for the boot log and an about page. */
const char *pshx_core_name(void);

/* The Xbox controller of a player (0-3), as a modern pad gives it: the digital buttons, the
 * analog ones (A, B, X, Y, black, white, left and right trigger; 0-255) and the sticks
 * (-32768..32767, y up). */
enum {
    PSHX_PAD_UP = 0x0001, PSHX_PAD_DOWN = 0x0002, PSHX_PAD_LEFT = 0x0004, PSHX_PAD_RIGHT = 0x0008,
    PSHX_PAD_START = 0x0010, PSHX_PAD_BACK = 0x0020, PSHX_PAD_LTHUMB = 0x0040, PSHX_PAD_RTHUMB = 0x0080,
};
enum { PSHX_A, PSHX_B, PSHX_X, PSHX_Y, PSHX_BLACK, PSHX_WHITE, PSHX_LT, PSHX_RT };
typedef struct PshxPad {
    uint16_t buttons;
    uint8_t analog[8];
    int16_t lx, ly, rx, ry;
} PshxPad;
void pshx_core_set_pad(unsigned player, const PshxPad *pad);
/* The players with a controller plugged in (bit n: player n + 1's port). */
void pshx_core_set_players(unsigned mask);
/* The rumble the title asked of a player's controller: the large and the small motor, 0..1. */
void pshx_core_rumble(unsigned player, float *large, float *small);

/* The newest frame the title presented, when newer than *seen (0 at first): RGBA8 rows, top row
 * first, copied into dst (pitch bytes a row, at most max_w x max_h). false: none new. */
bool pshx_core_take_frame(uint64_t *seen, void *dst, uint32_t pitch, uint32_t max_w, uint32_t max_h, uint32_t *w,
                          uint32_t *h);
/* OGBox 0.0.4: the same into dst_bytes of the title's mapped upload buffer, which the HLE GPU then
 * fills itself (no CPU copy where its device can take that memory); pshx_core_frame_forget before
 * the buffer is freed. */
bool pshx_core_take_frame_into(uint64_t *seen, void *dst, size_t dst_bytes, uint32_t pitch, uint32_t max_w,
                               uint32_t max_h, uint32_t *w, uint32_t *h);
void pshx_core_frame_forget(void *dst);
/* OGBox 0.0.4: the display's timing, for the emulated vblank (hakuX's nv2a.c locks it to the panel,
 * as its Android front end's Choreographer does). display_vsync: the presenter's loop was let go by
 * the display (its swapchain image acquired, FIFO: once a refresh); display_pickup: it looks for a
 * new frame. The guest's vblank then comes half a refresh from the pickups, instead of running free
 * beside the panel and now and then landing on a pickup (a frame shown twice, the next never). */
void pshx_core_display_vsync(void);
void pshx_core_display_pickup(void);
/* The size the next frame has (the HLE GPU's display; 0 x 0 before the first). */
void pshx_core_frame_size(uint32_t *w, uint32_t *h);
/* The console's video setting is widescreen: a 4:3-sized frame is shown as 16:9 (anamorphic). */
bool pshx_core_widescreen(void);

/* Paused: the guest's clock and sound stop, the frame stays (the quick menu, the home screen). */
void pshx_core_pause(bool on);
/* OGBox 0.0.2: n of the GPU's draws into the boot log (hakuX's draw log: each draw's target and
 * textures, the targets' events among them), from delay_ms from now on, for a problem report: the
 * quick menu's (the delay: time to bring the problem on screen after it closes). */
void pshx_core_drawlog(int n, unsigned delay_ms);
/* OGBox 0.1.53: the running game saved to path (hakuX's savestate: the whole console, its hard disk's
 * open files included). Not on the emulated CPU's thread: it waits for the guest to reach its pause
 * point (paused or not) and for the write, a second or so; a game writing a file of its own at
 * that moment is waited for up to about 3 s. true, or false with the reason in why. A state is
 * loaded by starting the title again with it (--loadstate). */
bool pshx_core_save_state(const char *path, char *why, unsigned why_size);

/* OGBox 0.1.54: what the game's trainer shows, oldest first (native/core/trainer.h): kind 1 it
 * was loaded (text: its name), 2 its LCD's lines (text), 3 the console's LED changed (led: the
 * sequence, bits 7-4 red and 3-0 green a quarter each; text in words); false when nothing new. */
bool pshx_core_trainer_message(int *kind, unsigned *led, char *text, unsigned text_size);

/* OGBox 0.1.25: before the title is replaced (back to the game list): the guest paused, its GPU
 * work finished. Says each step in the boot log. */
void pshx_core_quiesce(void);

/* One line about the run, for the boot log's heartbeat and an overlay: the state, frames
 * presented and taken, the guest's position when the CPU backend knows it. */
void pshx_core_status(char *line, unsigned size);

#ifdef __cplusplus
}
#endif

#endif
