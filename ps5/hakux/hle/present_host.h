/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * The HLE GPU's presenter on the console: a host-owned image. On Android hakuX draws its front
 * frame into the activity's window through a swapchain of its own (hw/xbox/hle/vk/swapchain.c);
 * on the console the title owns the display (the template's VK_KHR_display swapchain), so the
 * HLE GPU only hands the front frame over and the title draws it.
 *
 * This first form copies: the front image is read back into memory the CPU sees and given to the
 * title as RGBA8 rows, which it uploads into its own image (ps5/src/pshakux_main.cpp). It is the
 * bring-up path, simple and independent of how the two Vulkan devices relate. The zero-copy form
 * shares the image (one device, or memory exported between the two) behind the same calls.
 */
#ifndef PSHAKUX_PRESENT_HOST_H
#define PSHAKUX_PRESENT_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The HLE GPU is up and its device not lost: frames can come. */
bool pshx_present_ready(void);

/* The front frame, when it is newer than *seen (a frame counter the caller keeps, 0 at first):
 * its size in *w, *h, and its pixels, RGBA8, top row first, copied into dst (pitch bytes a row;
 * at most max_w x max_h: a larger frame is refused). *seen becomes the frame's. false: no new
 * frame (dst untouched), or none fits. On the title's render thread; the HLE GPU's render thread
 * goes on meanwhile (the slot read is held until the copy is done). */
bool pshx_present_take(uint64_t *seen, void *dst, uint32_t pitch, uint32_t max_w, uint32_t max_h, uint32_t *w,
                       uint32_t *h);

/* The front frame's size without taking it (0 x 0: none yet). */
void pshx_present_size(uint32_t *w, uint32_t *h);

/* Frames presented, and those taken by the title. */
void pshx_present_counts(uint64_t *presented, uint64_t *taken);

/* Releases what the presenter made (before the HLE GPU's device goes). */
void pshx_present_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
