/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * The guest CPU's interface (hakuX's native/cpu/cpu.h) with no CPU behind it: every function is
 * weak, so the native backend (native/cpu/cpu_native.c, compat-mode execution on the console's
 * own CPU, written in hakuX-native's ps5-native branch) replaces each one it defines, and the
 * eboot links while it is being written. On Android the interface is FEXCore's (native/cpu/
 * cpu.cpp), which the console does not build.
 *
 * With the stub alone nothing runs: nc_cpu_create says so in the boot log and gives NULL, and the
 * core's boot stops there (the title shows why).
 */
#include "native/cpu/cpu.h"

#include <string.h>
#include <time.h>
#include <ucontext.h>

#define WEAK __attribute__((weak))

struct NcCpu {
    int unused;
};

WEAK NcCpu *nc_cpu_create(const NcCpuConfig *cfg, uint32_t fs_base)
{
    (void)cfg;
    (void)fs_base;
    nc_log("hakuX-native", "cpu: no CPU backend in this build (the stub): the guest cannot run");
    return NULL;
}

WEAK void nc_context_export(const NcCpuContext *c, NcCpuState *s)
{
    (void)c;
    memset(s, 0, sizeof(*s));
}

WEAK void nc_context_import(NcCpuContext *c, const NcCpuState *s)
{
    (void)s;
    memset(c, 0, sizeof(*c));
}

WEAK void nc_frame_save(const NcFrame *f, NcCpuContext *c)
{
    (void)f;
    memset(c, 0, sizeof(*c));
}

WEAK void nc_frame_load(NcFrame *f, const NcCpuContext *c)
{
    (void)f;
    (void)c;
}

WEAK uint32_t nc_frame_gpr(const NcFrame *f, int reg)
{
    (void)f;
    (void)reg;
    return 0;
}

WEAK void nc_frame_set_gpr(NcFrame *f, int reg, uint32_t v)
{
    (void)f;
    (void)reg;
    (void)v;
}

WEAK uint32_t nc_frame_eip(const NcFrame *f)
{
    (void)f;
    return 0;
}

WEAK void nc_frame_set_eip(NcFrame *f, uint32_t eip)
{
    (void)f;
    (void)eip;
}

WEAK bool nc_frame_if(const NcFrame *f)
{
    (void)f;
    return false;
}

WEAK void nc_frame_set_if(NcFrame *f, bool on)
{
    (void)f;
    (void)on;
}

WEAK void nc_context_init(NcCpu *cpu, NcCpuContext *c, uint32_t eip, uint32_t esp)
{
    (void)cpu;
    (void)eip;
    (void)esp;
    memset(c, 0, sizeof(*c));
}

WEAK void nc_context_set_gpr(NcCpuContext *c, int reg, uint32_t v)
{
    (void)c;
    (void)reg;
    (void)v;
}

WEAK uint32_t nc_context_gpr(const NcCpuContext *c, int reg)
{
    (void)c;
    (void)reg;
    return 0;
}

WEAK uint32_t nc_context_eip(const NcCpuContext *c)
{
    (void)c;
    return 0;
}

WEAK void nc_context_set_eip(NcCpuContext *c, uint32_t eip)
{
    (void)c;
    (void)eip;
}

WEAK void nc_cpu_run(NcCpu *cpu, const NcCpuContext *c)
{
    (void)cpu;
    (void)c;
    nc_log("hakuX-native", "cpu: nc_cpu_run without a CPU backend: returns at once");
}

WEAK void nc_cpu_invalidate(NcCpu *cpu, uint32_t va, uint32_t len)
{
    (void)cpu;
    (void)va;
    (void)len;
}

WEAK bool nc_cpu_x87_single_check(NcCpu *cpu)
{
    (void)cpu;
    return false;
}

WEAK void nc_cpu_request_interrupt(NcCpu *cpu)
{
    (void)cpu;
}

WEAK void nc_cpu_cycle_offset_add(NcCpu *cpu, uint64_t ticks)
{
    (void)cpu;
    (void)ticks;
}

/* The host's cycle counter: the console's time-stamp counter. */
WEAK uint64_t nc_cpu_host_cycles(void)
{
    return __builtin_ia32_rdtsc();
}

WEAK uint64_t nc_cpu_guest_cycles(NcCpu *cpu)
{
    (void)cpu;
    return nc_cpu_host_cycles();
}

WEAK void nc_cpu_guest_cycles_set(NcCpu *cpu, uint64_t ticks)
{
    (void)cpu;
    (void)ticks;
}

WEAK bool nc_cpu_x87_single(NcCpu *cpu)
{
    (void)cpu;
    return false;
}

WEAK void nc_cpu_x87_single_restore(NcCpu *cpu)
{
    (void)cpu;
}

WEAK void nc_frame_clear_interrupt(NcFrame *f)
{
    (void)f;
}

WEAK void nc_frame_bypass_hook(NcFrame *f)
{
    (void)f;
}

WEAK uint16_t nc_frame_cs(const NcFrame *f)
{
    (void)f;
    return 0x08;
}

WEAK uint32_t nc_frame_eflags(NcFrame *f)
{
    (void)f;
    return 0x202;
}

WEAK void nc_frame_set_eflags(NcFrame *f, uint32_t eflags)
{
    (void)f;
    (void)eflags;
}

WEAK uint64_t *nc_frame_regs(NcFrame *f)
{
    static uint64_t regs[16];
    (void)f;
    return regs;
}

WEAK uint64_t *nc_frame_rip(NcFrame *f)
{
    static uint64_t rip;
    (void)f;
    return &rip;
}

WEAK uint32_t nc_cpu_guest_eip_at(NcCpu *cpu, uintptr_t host_pc)
{
    (void)cpu;
    (void)host_pc;
    return 0;
}

WEAK bool nc_cpu_sample_pc(NcCpu *cpu, uintptr_t host_pc, uint32_t *eip)
{
    (void)cpu;
    (void)host_pc;
    *eip = 0;
    return false;
}

WEAK NcFrame *nc_cpu_fault_frame(NcCpu *cpu, void *ucontext)
{
    (void)cpu;
    (void)ucontext;
    return NULL;
}

WEAK void nc_cpu_fault_resume(NcCpu *cpu, void *ucontext)
{
    (void)cpu;
    (void)ucontext;
}

/* (hakuX-native's cpu.h adds it: the host PC of a signal's context, FreeBSD's mcontext here.) */
uintptr_t nc_cpu_ucontext_pc(const void *ucontext);
WEAK uintptr_t nc_cpu_ucontext_pc(const void *ucontext)
{
    return (uintptr_t)((const ucontext_t *)ucontext)->uc_mcontext.mc_rip;
}

WEAK bool nc_cpu_fault_is_write(const void *ucontext)
{
    (void)ucontext;
    return false;
}

WEAK bool nc_cpu_unaligned_fault(NcCpu *cpu, int code, void *ucontext)
{
    (void)cpu;
    (void)code;
    (void)ucontext;
    return false;
}

WEAK bool nc_cpu_callret_fault(NcCpu *cpu, uintptr_t fault_addr, void *ucontext)
{
    (void)cpu;
    (void)fault_addr;
    (void)ucontext;
    return false;
}
