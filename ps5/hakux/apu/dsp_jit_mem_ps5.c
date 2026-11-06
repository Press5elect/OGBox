/*
 * OGBox - the DSP56300 JIT's code memory on the PS5, and its panics' messages (the dsp56300 crate's
 * host-memory feature: dsp56300_host_map_rwx, dsp56300_host_unmap_rwx, dsp56300_host_log).
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * On the console an anonymous map asked for read, write and execute succeeds and isn't
 * executable (test5: the first call into the JIT's code faulted), and a shared-memory view mapped
 * with no address lands in the Vulkan driver's GPU window, where execute isn't granted (test7: the
 * view's protection read back as read-write). The platform layer's executable memory is what the
 * console runs a JIT from (ps5platform/exec.h, measured in its docs/PROBE.md): direct memory
 * mapped read-write, given execute with sceKernelMprotect, placed outside the GPU window. Each of
 * the JIT's chunks (1 MiB and up) is one such region, read, write and execute at once (the PS5
 * makes read+execute pages execute-only, and Cranelift's code reads constants placed after it).
 * The protection is still read back: without execute the region goes back and the JIT isn't used
 * (dsp56300_create returns NULL: the interpreter), instead of the first call faulting.
 */
#include "native/dev/apu/apu_base.h"

#include <ps5platform/exec.h>
#include <ps5platform/kernel.h>

#include <pthread.h>
#include <stddef.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_said;

void *dsp56300_host_map_rwx(size_t len)
{
    void *p = ps5_exec_allocate(len, 0);
    int why = p ? 0 : -1;
    uint32_t prot = 0;
    if (p) {
        void *start = NULL, *end = NULL;
        const int32_t q = sceKernelQueryMemoryProtection(p, &start, &end, &prot);
        /* the query can fail on memory it doesn't track; only a definite answer without execute
         * refuses the region */
        if (q == 0 && (prot & 0x4) == 0) {
            ps5_exec_release(p);
            p = NULL;
            why = -2;
        }
    }
    pthread_mutex_lock(&g_lock);
    const bool say = !g_said || !p;
    g_said = true;
    pthread_mutex_unlock(&g_lock);
    if (say) {
        if (p) {
            nc_log("hakuX-apu", "dsp jit: code memory: %zu KiB of direct memory at %p (read, write, execute)",
                   len >> 10, p);
        } else if (why == -2) {
            nc_log("hakuX-apu", "dsp jit: the region at protection %x isn't executable: the DSP interpreter runs",
                   prot);
        } else {
            nc_log("hakuX-apu", "dsp jit: no code memory (%zu KiB): the DSP interpreter runs", len >> 10);
        }
    }
    return p;
}

void dsp56300_host_unmap_rwx(void *p, size_t len)
{
    (void)len;
    if (p) {
        ps5_exec_release(p);
    }
}

/* A panic in the JIT (caught by the crate: native/dev/apu/dsp/jit.c goes on with the interpreter),
 * its message and where */
void dsp56300_host_log(const char *line)
{
    nc_log("hakuX-apu", "%s", line ? line : "dsp56300: panicked");
    nc_log_flush();
}
