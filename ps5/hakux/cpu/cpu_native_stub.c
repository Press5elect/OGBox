/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * The part of the native CPU backend's interface (hakuX-native's native/cpu/cpu_native.h) that
 * native/cpu/cpu_native.c defines, weak and empty, for a hakuX tree whose backend is not the
 * console's yet (its signal context is Linux's only): the rest of that tree (the VM, the kernel,
 * the MMIO decoder, the code sweep) is built for the native backend as it will run, and the eboot
 * links, with cpu_stub.c's CPU that boots nothing. ps5/cmake/hakux.cmake compiles this file only
 * then; once cpu_native.c builds for the console, it is left out.
 */
#include "native/cpu/cpu_native.h"

#include <string.h>

#define WEAK __attribute__((weak))

WEAK void ncn_frame_from_uc(NcFrame *f, const void *ucontext, bool fx)
{
    (void)f;
    (void)ucontext;
    (void)fx;
}

WEAK void ncn_frame_to_uc(const NcFrame *f, void *ucontext, bool fx)
{
    (void)f;
    (void)ucontext;
    (void)fx;
}

WEAK uintptr_t ncn_uc_pc(const void *ucontext)
{
    (void)ucontext;
    return 0;
}

WEAK bool ncn_uc_in_guest(const void *ucontext)
{
    (void)ucontext;
    return false;
}

WEAK void nc_cpu_native_attach(NcCpu *cpu, NcVm *vm, uint32_t noint_lo, uint32_t noint_hi)
{
    (void)cpu;
    (void)vm;
    (void)noint_lo;
    (void)noint_hi;
}

WEAK uint32_t nc_cpu_native_gate_install(NcCpu *cpu, uint8_t *host, uint32_t va)
{
    (void)cpu;
    (void)host;
    (void)va;
    return 0;
}

WEAK uint16_t nc_cpu_native_host_cs(void)
{
    return 0;
}

WEAK bool nc_cpu_native_gate_wanted(void)
{
    return false;
}

WEAK uint32_t ncn_seg_base(void)
{
    return 0;
}

WEAK uint64_t ncn_tsc_hz(void)
{
    return 0;
}

WEAK void nc_cpu_native_stats(NcCpu *cpu, NcNativeStats *out)
{
    (void)cpu;
    memset(out, 0, sizeof(*out));
}
