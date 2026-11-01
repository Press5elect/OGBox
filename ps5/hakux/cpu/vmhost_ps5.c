/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * The native CPU backend's host side of guest memory (hakuX-native's native/cpu/vmhost.h) on the
 * console, in place of its Linux native/cpu/vmhost.c (memfd and mmap of it): the platform layer's
 * shared memory on direct memory (ps5platform/shm.h), which one allocation can be mapped from any
 * number of times and is charged to the direct pool only, not to the title's small flexible
 * budget (a memfd's every view would be).
 *
 *   the guest's ranges     reserved at their own addresses (ps5_vrange_reserve_at): the user range
 *                          from 0x10000, and from the contiguous window up, from NC_WINDOW_HOLE_END
 *                          (0x80120000: a system module sits at 0x80000000 on the console)
 *   guest RAM              one shm object (ps5_shm_create), a handle to it; the host's view of all
 *                          of it anywhere (ps5_shm_map, no address)
 *   a mapping              ps5_shm_map at the guest's address, PS5_SHM_FIXED (replacing the
 *                          reservation there), read, write and execute: the guest's code runs from
 *                          it (the platform layer grants execute by a protection change after the
 *                          map, as the kernel wants)
 *   an unmapping           ps5_shm_unmap with PS5_SHM_KEEP_RESERVED: the range is a hole of the
 *                          reservation again, without access
 */
#include "native/cpu/vmhost.h"
#include "native/core/nc.h"

#include <ps5platform/shm.h>

#include <pthread.h>
#include <string.h>

#ifndef NC_WINDOW_HOLE_END
#define NC_WINDOW_HOLE_END 0x80120000u
#endif

static const struct {
    uint64_t lo, hi;
} k_ranges[] = {
    {0x00010000ull, 0x80000000ull},
    {NC_WINDOW_HOLE_END ? NC_WINDOW_HOLE_END : 0x80000000ull, 0x100000000ull},
};

#define RAM_MAX 4
static struct ps5_shm g_ram[RAM_MAX];
static int g_ram_count;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

bool nc_vmhost_reserve_guest(void)
{
    for (size_t i = 0; i < sizeof(k_ranges) / sizeof(k_ranges[0]); i++) {
        const size_t len = (size_t)(k_ranges[i].hi - k_ranges[i].lo);
        const int r = ps5_vrange_reserve_at((void *)(uintptr_t)k_ranges[i].lo, len);
        if (r != 0) {
            nc_log("hakuX-native", "vm: can't reserve guest %08llx-%08llx (0x%08x%s)",
                   (unsigned long long)k_ranges[i].lo, (unsigned long long)k_ranges[i].hi, (unsigned)r,
                   r == PS5_SHM_NO_PLACE ? ": something of the title's is there" : "");
            return false;
        }
    }
    return true;
}

bool nc_vmhost_reserved(uint32_t va)
{
    for (size_t i = 0; i < sizeof(k_ranges) / sizeof(k_ranges[0]); i++) {
        if (va >= k_ranges[i].lo && va < k_ranges[i].hi) {
            return true;
        }
    }
    return false;
}

int nc_vmhost_ram_create(uint32_t size)
{
    pthread_mutex_lock(&g_lock);
    int handle = -1;
    if (g_ram_count < RAM_MAX) {
        struct ps5_shm shm;
        memset(&shm, 0, sizeof(shm));
        const int r = ps5_shm_create(size, &shm);
        if (r == 0) {
            handle = g_ram_count;
            g_ram[g_ram_count++] = shm;
        } else {
            nc_log("hakuX-native", "vm: guest RAM (%u MiB of direct memory): ps5_shm_create 0x%08x", size >> 20,
                   (unsigned)r);
        }
    }
    pthread_mutex_unlock(&g_lock);
    return handle;
}

static const struct ps5_shm *ram_of(int ram)
{
    return ram >= 0 && ram < g_ram_count ? &g_ram[ram] : NULL;
}

void *nc_vmhost_ram_view(int ram, uint32_t size)
{
    const struct ps5_shm *shm = ram_of(ram);
    void *view = NULL;
    if (!shm || ps5_shm_map(shm, 0, size, NULL, PS5_SHM_READ | PS5_SHM_WRITE, 0, &view) != 0) {
        return NULL;
    }
    return view;
}

bool nc_vmhost_map(uint32_t va, int ram, uint32_t off, uint32_t len)
{
    const struct ps5_shm *shm = ram_of(ram);
    void *view = NULL;
    if (!shm) {
        return false;
    }
    const int r = ps5_shm_map(shm, off, len, (void *)(uintptr_t)va, PS5_SHM_READ | PS5_SHM_WRITE | PS5_SHM_EXEC,
                              PS5_SHM_FIXED, &view);
    return r == 0 && view == (void *)(uintptr_t)va;
}

bool nc_vmhost_unmap(uint32_t va, uint32_t len)
{
    return ps5_shm_unmap((void *)(uintptr_t)va, len, PS5_SHM_KEEP_RESERVED) == 0;
}
