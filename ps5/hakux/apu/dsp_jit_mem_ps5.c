/*
 * OGBox - the DSP56300 JIT's code memory on the PS5, and its panics' messages (the dsp56300 crate's
 * host-memory feature: dsp56300_host_map_rwx, dsp56300_host_unmap_rwx, dsp56300_host_log).
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * On the console an anonymous map asked for read, write and execute succeeds and isn't
 * executable: test5's first call into the JIT's code faulted (an instruction fetch, a protection
 * fault). The memory the Xbox's own code runs from is: the platform layer's shared memory on
 * direct memory, execute granted by a protection change after the map (ps5platform/shm.h). Each
 * of the JIT's chunks (1 MiB and up) is one such object with one view, read, write and execute
 * (the PS5 makes read+execute pages execute-only, and Cranelift's code reads constants placed
 * after it). The view's protection is read back (sceKernelQueryMemoryProtection): without
 * execute, the memory goes back and the JIT isn't used (dsp56300_create returns NULL: the
 * interpreter), instead of the first call faulting.
 */
#include "native/dev/apu/apu_base.h"

#include <ps5platform/kernel.h>
#include <ps5platform/shm.h>

#include <pthread.h>
#include <stddef.h>

#define SLOTS 64

static struct {
    void *view;
    size_t bytes;
    struct ps5_shm shm;
} g_chunk[SLOTS];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_said;

void *dsp56300_host_map_rwx(size_t len)
{
    struct ps5_shm shm;
    int r = ps5_shm_create(len, &shm);
    void *view = NULL;
    if (r == 0) {
        r = ps5_shm_map(&shm, 0, len, NULL, PS5_SHM_READ | PS5_SHM_WRITE | PS5_SHM_EXEC, 0, &view);
        if (r == 0) {
            void *start = NULL, *end = NULL;
            uint32_t prot = 0;
            const int32_t q = sceKernelQueryMemoryProtection(view, &start, &end, &prot);
            if (q != 0 || (prot & 0x7) != 0x7) {
                nc_log("hakuX-apu", "dsp jit: the view at %p has protection %x (0x%08x), not read, write and "
                       "execute", view, prot, (unsigned)q);
                ps5_shm_unmap(view, len, 0);
                r = -2;
            }
        }
        if (r != 0) {
            ps5_shm_destroy(&shm);
        }
    }
    pthread_mutex_lock(&g_lock);
    int slot = -1;
    for (int i = 0; r == 0 && i < SLOTS; i++) {
        if (!g_chunk[i].view) {
            slot = i;
            break;
        }
    }
    if (r == 0 && slot < 0) {
        ps5_shm_unmap(view, len, 0);
        ps5_shm_destroy(&shm);
        r = -1;
    }
    if (r == 0) {
        g_chunk[slot].view = view;
        g_chunk[slot].bytes = len;
        g_chunk[slot].shm = shm;
    }
    const bool say = !g_said || r != 0;
    g_said = true;
    pthread_mutex_unlock(&g_lock);
    if (say) {
        if (r == 0) {
            nc_log("hakuX-apu", "dsp jit: code memory: %zu KiB of direct memory at %p (read, write, execute)",
                   len >> 10, view);
        } else {
            nc_log("hakuX-apu", "dsp jit: no code memory (%zu KiB: 0x%08x): the DSP interpreter runs", len >> 10,
                   (unsigned)r);
        }
    }
    return r == 0 ? view : NULL;
}

void dsp56300_host_unmap_rwx(void *p, size_t len)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < SLOTS; i++) {
        if (g_chunk[i].view == p) {
            ps5_shm_unmap(p, len, 0);
            ps5_shm_destroy(&g_chunk[i].shm);
            g_chunk[i].view = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* A panic in the JIT (caught by the crate: native/dev/apu/dsp/jit.c goes on with the interpreter),
 * its message and where */
void dsp56300_host_log(const char *line)
{
    nc_log("hakuX-apu", "%s", line ? line : "dsp56300: panicked");
    nc_log_flush();
}
