/*
 * OGBox - the DSP56300 JIT's code memory on the PS5, and its panics' messages (the dsp56300 crate's
 * host-memory feature: dsp56300_host_map_rwx, dsp56300_host_unmap_rwx, dsp56300_host_log).
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * On the console an anonymous map asked for read, write and execute succeeds and isn't
 * executable (test5: the first call into the JIT's code faulted). The platform layer's executable
 * memory is what the console runs a JIT from (ps5platform/exec.h, measured in its docs/PROBE.md,
 * and LRPS2's recompiler's code area): direct memory mapped read-write, given execute with
 * sceKernelMprotect, placed outside the GPU window. Each of the JIT's chunks (1 MiB and up) is one
 * such region, read, write and execute at once (the PS5 makes read+execute pages execute-only, and
 * Cranelift's code reads constants placed after it).
 *
 * test8: sceKernelQueryMemoryProtection reads such a region back as read-write (3) all the same,
 * so its answer doesn't decide (test7's and test8's JIT never started on it). Instead the first
 * region runs a few bytes of code before the JIT gets it, and the boot log says so (a fault there
 * stops at a line that names it, not somewhere in the sound).
 */
#include "native/dev/apu/apu_base.h"

#include <ps5platform/exec.h>

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_said;

/* mov eax, 0x0D5F5600; ret */
static const uint8_t k_probe[] = {0xB8, 0x00, 0x56, 0x5F, 0x0D, 0xC3};

void *dsp56300_host_map_rwx(size_t len)
{
    void *p = ps5_exec_allocate(len, 0);
    pthread_mutex_lock(&g_lock);
    const bool first = !g_said;
    g_said = true;
    pthread_mutex_unlock(&g_lock);
    if (!p) {
        nc_log("hakuX-apu", "dsp jit: no code memory (%zu KiB): the DSP interpreter runs", len >> 10);
        return NULL;
    }
    if (first) {
        nc_log("hakuX-apu", "dsp jit: code memory: %zu KiB of direct memory at %p (read, write, execute); "
               "a probe runs from it", len >> 10, p);
        nc_log_flush();
        memcpy(p, k_probe, sizeof(k_probe));
        const uint32_t r = ((uint32_t (*)(void))p)();
        nc_log("hakuX-apu", "dsp jit: the probe ran (%08x)", r);
        if (r != 0x0D5F5600u) {
            ps5_exec_release(p);
            return NULL;
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
