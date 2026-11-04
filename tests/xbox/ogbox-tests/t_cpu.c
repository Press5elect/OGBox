/*
 * OGBox tests - the CPU's floating-point state.
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * Each Xbox thread has its own x87 and SSE state (control word, MXCSR, the eight x87 registers,
 * XMM0-7): the kernel saves it when a thread is switched out and gives it back when the thread
 * runs again; a new thread starts with control word 0x027F (double precision, everything
 * masked) and MXCSR 0x1F80. Under hakuX all the guest's threads share one host thread, and the
 * switch is hakuX's (cpu/cpu_native.c, through the signal frame's FXSAVE image). If any of it
 * goes missing, a game's float code reads another thread's numbers or a full x87 stack: a load
 * on a full stack gives the "real indefinite" NaN 0xFFC00000 (what Crash Bandicoot's stack is
 * full of when it crashes).
 */
#include "common.h"

#include <stddef.h>
#include <string.h>

static uint16_t get_fcw(void)
{
    uint16_t v;
    __asm__ volatile("fnstcw %0" : "=m"(v));
    return v;
}

static uint32_t get_mxcsr(void)
{
    uint32_t v;
    __asm__ volatile("stmxcsr %0" : "=m"(v));
    return v;
}

static void set_fcw(uint16_t v)
{
    __asm__ volatile("fldcw %0" : : "m"(v));
}

static void set_mxcsr(uint32_t v)
{
    __asm__ volatile("ldmxcsr %0" : : "m"(v));
}

/* x87 registers in use (the tag word's non-empty entries) */
static unsigned x87_used(void)
{
    uint32_t env[7];
    __asm__ volatile("fnstenv %0\n\tfldenv %0" : "=m"(env));
    const uint32_t tags = env[2] & 0xFFFF;
    unsigned used = 0;
    for (unsigned r = 0; r < 8; r++) {
        if (((tags >> (2 * r)) & 3) != 3) {
            used++;
        }
    }
    return used;
}

/* the x87 precision the control word gives arithmetic: 1 + 2^-60 is kept only with 64 bits,
 * 1 + 2^-40 with 53 or 64 */
static unsigned x87_precision(void)
{
    static const double t60 = 0x1p-60, t40 = 0x1p-40;
    uint8_t k60, k40;
    __asm__ volatile("fldl %2\n\t"
                     "fld1\n\t"
                     "faddp\n\t"
                     "fld1\n\t"
                     "fucomip %%st(1), %%st\n\t"
                     "fstp %%st(0)\n\t"
                     "setne %0\n\t"
                     "fldl %3\n\t"
                     "fld1\n\t"
                     "faddp\n\t"
                     "fld1\n\t"
                     "fucomip %%st(1), %%st\n\t"
                     "fstp %%st(0)\n\t"
                     "setne %1\n\t"
                     : "=&q"(k60), "=&q"(k40)
                     : "m"(t60), "m"(t40)
                     : "cc", "st", "st(1)");
    return k60 ? 64 : (k40 ? 53 : 24);
}

static int32_t x87_fist(double v)
{
    int32_t r;
    __asm__ volatile("fldl %1\n\tfistpl %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

static uint32_t x87_zero_by_zero(void)
{
    static const float z = 0.0f;
    uint32_t r;
    __asm__ volatile("flds %1\n\t"
                     "flds %1\n\t"
                     "fdivrp\n\t"
                     "fstps %0\n\t"
                     "fnclex\n\t"
                     : "=m"(r)
                     : "m"(z)
                     : "st", "st(1)");
    return r;
}

static uint32_t sse_zero_by_zero(void)
{
    static const float z = 0.0f;
    uint32_t r, m = get_mxcsr();
    __asm__ volatile("movss %1, %%xmm0\n\t"
                     "divss %1, %%xmm0\n\t"
                     "movss %%xmm0, %0\n\t"
                     : "=m"(r)
                     : "m"(z)
                     : "xmm0");
    set_mxcsr(m); /* the invalid flag off again */
    return r;
}

/* an x87 load onto a full stack: the stack fault's real indefinite */
static uint32_t x87_overflow(void)
{
    static const double one = 1.0;
    uint32_t r;
    __asm__ volatile("fldl %1\n\tfldl %1\n\tfldl %1\n\tfldl %1\n\t"
                     "fldl %1\n\tfldl %1\n\tfldl %1\n\tfldl %1\n\t"
                     "fldl %1\n\t" /* the ninth */
                     "fstps %0\n\t"
                     "fninit\n\t"
                     : "=m"(r)
                     : "m"(one)
                     : "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)");
    return r;
}

/* ---------------------------------------------------------------- holds */

/* seven doubles on the x87 stack and XMM0-7 loaded, a busy wait on the TSC until the deadline
 * (other threads run meanwhile), then everything stored back */
struct hold
{
    double in[7];
    double out[7];
    uint8_t xin[128];
    uint8_t xout[128];
    uint32_t deadline_lo, deadline_hi;
    void *fn;    /* hold_call: the kernel function called with the values held */
    uint32_t arg; /* its one argument, pushed (a function without arguments leaves it) */
};

static void hold_spin(struct hold *h)
{
    __asm__ volatile("fldl 0(%0)\n\tfldl 8(%0)\n\tfldl 16(%0)\n\tfldl 24(%0)\n\t"
                     "fldl 32(%0)\n\tfldl 40(%0)\n\tfldl 48(%0)\n\t"
                     "movups 112(%0), %%xmm0\n\tmovups 128(%0), %%xmm1\n\t"
                     "movups 144(%0), %%xmm2\n\tmovups 160(%0), %%xmm3\n\t"
                     "movups 176(%0), %%xmm4\n\tmovups 192(%0), %%xmm5\n\t"
                     "movups 208(%0), %%xmm6\n\tmovups 224(%0), %%xmm7\n\t"
                     "1:\n\t"
                     "pause\n\t"
                     "rdtsc\n\t"
                     "subl 368(%0), %%eax\n\t"
                     "sbbl 372(%0), %%edx\n\t"
                     "js 1b\n\t"
                     "movups %%xmm0, 240(%0)\n\tmovups %%xmm1, 256(%0)\n\t"
                     "movups %%xmm2, 272(%0)\n\tmovups %%xmm3, 288(%0)\n\t"
                     "movups %%xmm4, 304(%0)\n\tmovups %%xmm5, 320(%0)\n\t"
                     "movups %%xmm6, 336(%0)\n\tmovups %%xmm7, 352(%0)\n\t"
                     "fstpl 104(%0)\n\tfstpl 96(%0)\n\tfstpl 88(%0)\n\tfstpl 80(%0)\n\t"
                     "fstpl 72(%0)\n\tfstpl 64(%0)\n\tfstpl 56(%0)\n\t"
                     :
                     : "D"(h)
                     : "eax", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
                       "st(6)", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7");
}

/* three doubles on the x87 stack and XMM0-7 loaded across a call into the kernel */
static void hold_call(struct hold *h)
{
    __asm__ volatile("fldl 0(%%edi)\n\tfldl 8(%%edi)\n\tfldl 16(%%edi)\n\t"
                     "movups 112(%%edi), %%xmm0\n\tmovups 128(%%edi), %%xmm1\n\t"
                     "movups 144(%%edi), %%xmm2\n\tmovups 160(%%edi), %%xmm3\n\t"
                     "movups 176(%%edi), %%xmm4\n\tmovups 192(%%edi), %%xmm5\n\t"
                     "movups 208(%%edi), %%xmm6\n\tmovups 224(%%edi), %%xmm7\n\t"
                     "movl %%esp, %%esi\n\t"
                     "pushl 380(%%edi)\n\t"
                     "call *376(%%edi)\n\t"
                     "movl %%esi, %%esp\n\t"
                     "movups %%xmm0, 240(%%edi)\n\tmovups %%xmm1, 256(%%edi)\n\t"
                     "movups %%xmm2, 272(%%edi)\n\tmovups %%xmm3, 288(%%edi)\n\t"
                     "movups %%xmm4, 304(%%edi)\n\tmovups %%xmm5, 320(%%edi)\n\t"
                     "movups %%xmm6, 336(%%edi)\n\tmovups %%xmm7, 352(%%edi)\n\t"
                     "fstpl 72(%%edi)\n\tfstpl 64(%%edi)\n\tfstpl 56(%%edi)\n\t"
                     :
                     : "D"(h)
                     : "eax", "ecx", "edx", "esi", "cc", "memory", "st", "st(1)", "st(2)", "xmm0", "xmm1",
                       "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7");
}

_Static_assert(offsetof(struct hold, out) == 56, "hold layout");
_Static_assert(offsetof(struct hold, xin) == 112, "hold layout");
_Static_assert(offsetof(struct hold, xout) == 240, "hold layout");
_Static_assert(offsetof(struct hold, deadline_lo) == 368, "hold layout");
_Static_assert(offsetof(struct hold, fn) == 376, "hold layout");
_Static_assert(offsetof(struct hold, arg) == 380, "hold layout");

static void hold_fill(struct hold *h, unsigned id, unsigned n)
{
    memset(h, 0, sizeof(*h));
    for (unsigned k = 0; k < 7; k++) {
        h->in[k] = (double)(id * 1000 + n * 10 + k) + 0.123456789012345;
        h->out[k] = -1.0;
    }
    for (unsigned k = 0; k < 128; k++) {
        h->xin[k] = (uint8_t)(id * 37 + n * 11 + k * 3 + 1);
    }
}

/* 0 when everything came back; otherwise which part didn't */
static int hold_lost(const struct hold *h, unsigned doubles)
{
    for (unsigned k = 0; k < doubles; k++) {
        if (memcmp(&h->in[k], &h->out[k], sizeof(double)) != 0) {
            return 1 + (int)k; /* x87 register k */
        }
    }
    if (memcmp(h->xin, h->xout, sizeof(h->xin)) != 0) {
        return 100; /* XMM */
    }
    return 0;
}

/* ---------------------------------------------------------------- threads */

#define WORKERS 3

struct worker
{
    unsigned id;
    uint16_t fcw;      /* set at its start */
    uint32_t mxcsr;
    unsigned precision; /* what fcw gives */
    uint16_t start_fcw; /* what it started with */
    uint32_t start_mxcsr;
    unsigned start_x87;
    unsigned holds, lost_values, lost_fcw, lost_mxcsr, lost_precision, x87_left, switched;
    int first_lost;
    uint16_t bad_fcw;
    uint32_t bad_mxcsr;
};

static struct worker g_w[WORKERS];
static volatile uint32_t g_beat[WORKERS];
static volatile LONG g_go;
static uint64_t g_end_qpc;
static uint32_t g_hold_ticks;

static DWORD WINAPI worker_main(LPVOID param)
{
    struct worker *w = (struct worker *)param;
    w->start_fcw = get_fcw();
    w->start_mxcsr = get_mxcsr();
    w->start_x87 = x87_used();
    while (!g_go) {
        Sleep(0);
    }
    set_fcw(w->fcw);
    set_mxcsr(w->mxcsr);

    struct hold h;
    unsigned n = 0;
    while (qpc() < g_end_qpc) {
        hold_fill(&h, w->id, n++);
        uint32_t seen[WORKERS];
        for (unsigned i = 0; i < WORKERS; i++) {
            seen[i] = g_beat[i];
        }
        const uint64_t deadline = rdtsc() + g_hold_ticks;
        h.deadline_lo = (uint32_t)deadline;
        h.deadline_hi = (uint32_t)(deadline >> 32);
        hold_spin(&h);
        w->holds++;
        for (unsigned i = 0; i < WORKERS; i++) {
            if (i != w->id && g_beat[i] != seen[i]) {
                w->switched++; /* another thread ran during the hold */
                break;
            }
        }
        const int lost = hold_lost(&h, 7);
        if (lost) {
            if (!w->lost_values) {
                w->first_lost = lost;
            }
            w->lost_values++;
        }
        const uint16_t fcw = get_fcw();
        if (fcw != w->fcw) {
            w->lost_fcw++;
            w->bad_fcw = fcw;
            set_fcw(w->fcw);
        }
        const uint32_t mx = get_mxcsr() & ~0x3Fu; /* the exception flags aside */
        if (mx != (w->mxcsr & ~0x3Fu)) {
            w->lost_mxcsr++;
            w->bad_mxcsr = mx;
            set_mxcsr(w->mxcsr);
        }
        if (x87_precision() != w->precision) {
            w->lost_precision++;
        }
        if (x87_used()) {
            w->x87_left++;
            __asm__ volatile("fninit");
            set_fcw(w->fcw);
        }
        g_beat[w->id]++;
    }
    set_fcw(0x027F);
    set_mxcsr(0x1F80);
    return 0;
}

/* cpuid as the XDK's code has it: the leaf loaded right before it (OGBox 0.0.2: the voice library's
 * Voxware codecs are made only on family 6 with SSE, and on the PS5 cpuid answered with its own
 * AMD Zen 2, family 0xF) */
static void cpuid_leaf0(uint32_t r[4])
{
    __asm__ volatile("movl $0, %%eax\n\tcpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]));
}

static void cpuid_leaf1(uint32_t r[4])
{
    __asm__ volatile("movl $1, %%eax\n\tcpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]));
}

void test_cpu(void)
{
    section("cpu: the processor (cpuid)");
    {
        uint32_t v[4], f[4];
        cpuid_leaf0(v);
        cpuid_leaf1(f);
        char vendor[13];
        memcpy(vendor, &v[1], 4);
        memcpy(vendor + 4, &v[3], 4);
        memcpy(vendor + 8, &v[2], 4);
        vendor[12] = 0;
        const unsigned family = f[0] >> 8 & 0xF, model = f[0] >> 4 & 0xF;
        report("INFO cpuid: %s, leaf 1 %08lx (family %u model %u stepping %lu), features %08lx",
               vendor, (unsigned long)f[0], family, model, (unsigned long)(f[0] & 0xF), (unsigned long)f[3]);
        /* the Xbox's Pentium III (Coppermine): no SSE2, which every host since has */
        check(!strcmp(vendor, "GenuineIntel") && family == 6 && model == 8 && (f[3] >> 25 & 1) && !(f[3] >> 26 & 1),
              "cpuid: the Xbox's Pentium III (GenuineIntel, family 6 model 8, SSE, no SSE2): %s family %u model %u, "
              "SSE %lu SSE2 %lu", vendor, family, model, (unsigned long)(f[3] >> 25 & 1),
              (unsigned long)(f[3] >> 26 & 1));
    }

    section("cpu: x87 and SSE state");
    if (g_tsc_per_us <= 0.0) {
        calibrate_tsc();
    }

    /* the main thread, as the title starts (nxdk's start-up code leaves both alone) */
    const uint16_t fcw = get_fcw();
    const uint32_t mx = get_mxcsr();
    check(fcw == 0x027F, "main thread x87 control word %04x (Xbox kernel: 027f, double precision)", fcw);
    check((mx & ~0x3Fu) == 0x1F80, "main thread MXCSR %08x (Xbox kernel: 00001f80)", mx);
    check(x87_used() == 0, "main thread x87 registers in use: %u (0)", x87_used());
    check(x87_precision() == 53, "x87 arithmetic precision %u bits (53 with control word 027f)", x87_precision());

    check(x87_fist(2.5) == 2 && x87_fist(-2.5) == -2 && x87_fist(3.5) == 4,
          "x87 rounding to nearest even: 2.5 -> %d, -2.5 -> %d, 3.5 -> %d (2, -2, 4)", (int)x87_fist(2.5),
          (int)x87_fist(-2.5), (int)x87_fist(3.5));
    const uint32_t nan87 = x87_zero_by_zero(), nansse = sse_zero_by_zero();
    check(nan87 == 0xFFC00000u, "x87 0/0 = %08x (real indefinite ffc00000)", nan87);
    check(nansse == 0xFFC00000u, "SSE 0/0 = %08x (ffc00000)", nansse);
    const uint32_t ovf = x87_overflow();
    check(ovf == 0xFFC00000u, "x87 load onto a full stack = %08x (ffc00000)", ovf);
    set_fcw(0x027F);

    set_fcw(0x007F);
    const unsigned p24 = x87_precision();
    set_fcw(0x037F);
    const unsigned p64 = x87_precision();
    set_fcw(0x027F);
    check(p24 == 24 && p64 == 64, "precision control: 007f -> %u bits, 037f -> %u bits (24, 64)", p24, p64);

    /* values held across calls into the kernel */
    const struct
    {
        const char *name;
        void *fn;
        uint32_t arg;
    } calls[] = {
        {"KeQueryPerformanceCounter", (void *)KeQueryPerformanceCounter, 0},
        {"NtYieldExecution", (void *)NtYieldExecution, 0},
        {"KeQueryInterruptTime", (void *)KeQueryInterruptTime, 0},
        {"Sleep(1)", (void *)Sleep, 1},
        {"Sleep(20)", (void *)Sleep, 20},
    };
    for (unsigned c = 0; c < sizeof(calls) / sizeof(calls[0]); c++) {
        struct hold h;
        hold_fill(&h, 9, c);
        h.fn = calls[c].fn;
        h.arg = calls[c].arg;
        hold_call(&h);
        const int lost = hold_lost(&h, 3);
        check(lost == 0 && x87_used() == 0 && get_fcw() == 0x027F,
              "x87 and XMM held across %s: %s, x87 left in use %u, control word %04x", calls[c].name,
              lost == 0 ? "kept" : (lost == 100 ? "XMM changed" : "x87 value changed"), x87_used(), get_fcw());
        if (x87_used()) {
            __asm__ volatile("fninit");
            set_fcw(0x027F);
        }
    }

    /* three threads, each with its own control word and MXCSR, holding values while the others
     * run */
    static const uint16_t fcws[WORKERS] = {0x027F, 0x007F, 0x037F};
    static const unsigned precisions[WORKERS] = {53, 24, 64};
    static const uint32_t mxcsrs[WORKERS] = {0x1F80, 0x7F80, 0x9F80}; /* nearest; toward zero; FTZ */
    g_go = 0;
    g_hold_ticks = (uint32_t)(g_tsc_per_us * 15000.0); /* 15 ms of TSC */
    HANDLE th[WORKERS];
    for (unsigned i = 0; i < WORKERS; i++) {
        memset(&g_w[i], 0, sizeof(g_w[i]));
        g_w[i].id = i;
        g_w[i].fcw = fcws[i];
        g_w[i].precision = precisions[i];
        g_w[i].mxcsr = mxcsrs[i];
        g_beat[i] = 0;
        th[i] = CreateThread(NULL, 0, worker_main, &g_w[i], 0, NULL);
    }
    Sleep(50);
    g_end_qpc = qpc() + qpc_freq() * 3; /* 3 s */
    InterlockedExchange(&g_go, 1);
    const uint64_t qw = qpc();
    const DWORD wr = WaitForMultipleObjects(WORKERS, th, TRUE, 20000);
    const double waited = qpc_us(qpc() - qw) / 1000.0;
    check(wr == WAIT_OBJECT_0 && waited > 2500.0,
          "WaitForMultipleObjects(all 3 threads) returned %08lx after %.0f ms (0, when the last ends: 3000)",
          (unsigned long)wr, waited);
    for (unsigned i = 0; i < WORKERS; i++) {
        WaitForSingleObject(th[i], 20000); /* in case the wait for all came back early */
        CloseHandle(th[i]);
    }

    for (unsigned i = 0; i < WORKERS; i++) {
        const struct worker *w = &g_w[i];
        check(w->start_fcw == 0x027F && (w->start_mxcsr & ~0x3Fu) == 0x1F80 && w->start_x87 == 0,
              "new thread %u starts with control word %04x MXCSR %08x, %u x87 in use (027f, 1f80, 0)", i,
              w->start_fcw, w->start_mxcsr, w->start_x87);
        check(w->holds > 0 && w->lost_values == 0,
              "thread %u kept 7 x87 values and XMM0-7 through %u of %u 15-ms holds with other threads "
              "running (first loss: %s %d)",
              i, w->holds - w->lost_values, w->holds, w->first_lost == 100 ? "xmm" : "st", w->first_lost);
        check(w->lost_fcw == 0 && w->lost_mxcsr == 0 && w->lost_precision == 0 && w->x87_left == 0,
              "thread %u kept its control word %04x (lost %u, last %04x), MXCSR %08x (lost %u, last %08x), "
              "precision (lost %u), empty stack (%u)",
              i, w->fcw, w->lost_fcw, w->bad_fcw, w->mxcsr, w->lost_mxcsr, w->bad_mxcsr, w->lost_precision,
              w->x87_left);
        report("INFO thread %u: %u holds, %u with another thread run during them", i, w->holds, w->switched);
    }
    set_fcw(0x027F);
    set_mxcsr(0x1F80);
}
