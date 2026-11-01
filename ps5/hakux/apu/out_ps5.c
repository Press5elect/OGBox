/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The APU's output on the console, in place of hakuX's native/dev/apu/out.c (AAudio), with the
 * same interface (native/dev/apu/apu_int.h: apu_out_open, apu_out_push, apu_out_queued,
 * apu_out_stats) and the same ring: the APU's frame thread pushes each EP frame (256 stereo
 * samples, 1024 bytes); one producer and one consumer, so the ring is two counters, the producer
 * publishing its writes with the head (release), the consumer its reads with the tail.
 *
 * The consumer is a thread of its own on sceAudioOut: the system user's main port, 48 kHz 16-bit
 * stereo, 256 samples a grain (the APU's frame), each sceAudioOutOutput blocking while the
 * console plays the grain before: the device paces the thread, as AAudio's callback paced
 * hakuX's. A grain is what the ring has, the rest silence (an underrun, counted as a short
 * callback); paused (nc_pause_set, or nc_audio_set_paused), silence and the ring kept: the frame
 * thread stops at its high watermark. With no port (sceAudioOutOpen refused), the thread drains
 * the ring at the device's rate instead, so the APU keeps its timing, and says so once.
 *
 * The functions are found through the SDK's stubs (libSceAudioOut), as the template's
 * ps5/src/platform.c does.
 */
#include "native/dev/apu/apu_int.h"

#include "platform/ps5_priority.h"

#include <time.h>

#define OUT_RATE 48000
#define OUT_GRAIN 256        /* samples per sceAudioOutOutput: the APU's EP frame */
#define OUT_FRAME_BYTES 4    /* a stereo sample: two 16-bit channels */
#define AUDIO_ALREADY_INITIALISED ((int)0x8026000e)

int sceAudioOutInit(void);
int sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t grain, uint32_t rate, uint32_t format);
int sceAudioOutOutput(int32_t port, const void *samples);
int sceAudioOutClose(int32_t port);

static struct {
    uint8_t *buf;
    uint32_t cap;
    uint64_t head, tail;     /* bytes written, bytes read */
    pthread_cond_t *wake;
    int32_t port;            /* -1: none (the thread drains the ring at the device's rate) */
    bool paused;             /* nc_audio_set_paused: the title is not in front */
    uint32_t callbacks, short_callbacks;
} g_out = {.port = -1};

int apu_out_queued(void)
{
    return (int)(apu_ld_acq(&g_out.head) - apu_ld_acq(&g_out.tail));
}

void apu_out_push(const void *frame, int bytes)
{
    const uint64_t head = g_out.head;
    assert(head + (uint64_t)bytes - apu_ld_acq(&g_out.tail) <= g_out.cap);
    const uint32_t at = (uint32_t)(head % g_out.cap);
    const uint32_t first = MIN((uint32_t)bytes, g_out.cap - at);
    memcpy(g_out.buf + at, frame, first);
    memcpy(g_out.buf, (const uint8_t *)frame + first, (uint32_t)bytes - first);
    apu_st_rel(&g_out.head, head + (uint64_t)bytes);
}

static int pop(uint8_t *out, int bytes)
{
    const uint64_t tail = g_out.tail;
    const int n = (int)MIN((uint64_t)bytes, apu_ld_acq(&g_out.head) - tail);
    const uint32_t at = (uint32_t)(tail % g_out.cap);
    const uint32_t first = MIN((uint32_t)n, g_out.cap - at);
    memcpy(out, g_out.buf + at, first);
    memcpy(out + first, g_out.buf, (uint32_t)n - first);
    apu_st_rel(&g_out.tail, tail + (uint64_t)n);
    return n;
}

void apu_out_stats(uint32_t *callbacks, uint32_t *short_callbacks)
{
    *callbacks = apu_xchg(&g_out.callbacks, 0);
    *short_callbacks = apu_xchg(&g_out.short_callbacks, 0);
}

/* One grain: what the ring has, the rest silence. Paused, silence, the ring kept. */
static void fill(uint8_t *grain, int bytes)
{
    if (nc_paused() || __atomic_load_n(&g_out.paused, __ATOMIC_ACQUIRE)) {
        memset(grain, 0, (size_t)bytes);
        return;
    }
    const int n = pop(grain, bytes);
    apu_inc(&g_out.callbacks);
    if (n < bytes) {
        apu_inc(&g_out.short_callbacks);
        memset(grain + n, 0, (size_t)(bytes - n));
    }
    pthread_cond_broadcast(g_out.wake);
}

static void *out_thread(void *arg)
{
    (void)arg;
    pshakux_priority_set(PSHAKUX_ROLE_SOUND);
    static uint8_t grain[OUT_GRAIN * OUT_FRAME_BYTES] __attribute__((aligned(64)));
    const long period_ns = (long)((int64_t)OUT_GRAIN * 1000000000LL / OUT_RATE);
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    bool said = false;
    for (;;) {
        fill(grain, (int)sizeof(grain));
        if (g_out.port >= 0 && sceAudioOutOutput(g_out.port, grain) >= 0) {
            continue;               /* the output blocked while the grain before played */
        }
        if (!said) {
            said = true;
            nc_log("hakuX-apu", "no sound output (sceAudioOut): draining without sound at 48 kHz");
        }
        next.tv_nsec += period_ns;
        while (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return NULL;
}

int apu_out_open(int capacity_bytes, int samples, pthread_cond_t *wake)
{
    g_out.buf = calloc(1, (size_t)capacity_bytes);
    g_out.cap = (uint32_t)capacity_bytes;
    g_out.wake = wake;
    const int init = sceAudioOutInit();
    if (init < 0 && init != AUDIO_ALREADY_INITIALISED) {
        nc_log("hakuX-apu", "sceAudioOutInit: 0x%08x", (unsigned)init);
    } else {
        /* the system user's main port: 256 samples, 48 kHz, 16-bit stereo (format 1) */
        g_out.port = sceAudioOutOpen(0xff, 0, 0, OUT_GRAIN, OUT_RATE, 1);
        if (g_out.port < 0) {
            nc_log("hakuX-apu", "sceAudioOutOpen: 0x%08x", (unsigned)g_out.port);
            g_out.port = -1;
        }
    }
    nc_log("hakuX-apu", "output: sceAudioOut port %d, 48 kHz stereo, %d-sample grains (the APU asked for %d), "
           "ring %d bytes", (int)g_out.port, OUT_GRAIN, samples, capacity_bytes);
    pthread_t t;
    apu_thread_start(&t, "apu.out", out_thread, NULL, false);
    return OUT_GRAIN * OUT_FRAME_BYTES;
}

/* The title in front or not: silence meanwhile (the ring kept). */
void nc_audio_set_paused(bool on)
{
    __atomic_store_n(&g_out.paused, on, __ATOMIC_RELEASE);
}
