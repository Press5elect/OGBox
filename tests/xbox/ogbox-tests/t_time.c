/*
 * OGBox tests - time, waits and the cost of kernel calls.
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The Xbox's clock interrupts every millisecond: KeTickCount counts milliseconds, the interrupt
 * and system times count 100-ns units. A guest Sleep(1) wakes at the next tick or two. Games that
 * spin on kernel calls (Def Jam: Fight for NY's main thread, NtWaitForSingleObjectEx with
 * NtReleaseMutant, 666 thousand pairs a second in OGBox 0.1.19) go only as fast as a kernel call
 * returns, so the cost of the common ones is measured too.
 */
#include "common.h"

#include <string.h>

static double ms_since(uint64_t q0)
{
    return qpc_us(qpc() - q0) / 1000.0;
}

static void sleep_stats(DWORD ms, unsigned n)
{
    double sum = 0.0, worst = 0.0, best = 1e9;
    for (unsigned i = 0; i < n; i++) {
        const uint64_t q0 = qpc();
        Sleep(ms);
        const double t = ms_since(q0);
        sum += t;
        worst = t > worst ? t : worst;
        best = t < best ? t : best;
    }
    const double avg = sum / n;
    /* the Xbox: the next tick or two after the time asked */
    const bool ok = ms == 0 ? worst < 2.0 : (avg >= (double)ms - 0.05 && avg < (double)ms + 2.5);
    check(ok, "Sleep(%lu) x%u: average %.2f ms, best %.2f, worst %.2f (Xbox: %lu to %lu ms)", (unsigned long)ms,
          n, avg, best, worst, (unsigned long)ms, (unsigned long)ms + 2);
}

/* calls of fn for about 300 ms: ns per call */
typedef void (*op_fn)(void *);
static void cost(const char *name, op_fn fn, void *ctx)
{
    unsigned n = 0;
    const uint64_t q0 = qpc();
    uint64_t q;
    do {
        for (unsigned i = 0; i < 256; i++) {
            fn(ctx);
        }
        n += 256;
        q = qpc();
    } while (qpc_us(q - q0) < 300000.0);
    const double ns = qpc_us(q - q0) * 1000.0 / n;
    report("INFO %s: %.0f ns a call (%.2f million a second)", name, ns, 1000.0 / ns);
}

static void op_tick(void *c)
{
    (void)c;
    (void)*(volatile DWORD *)&KeTickCount;
}
static void op_qpc(void *c)
{
    (void)c;
    (void)KeQueryPerformanceCounter();
}
static void op_yield(void *c)
{
    (void)c;
    NtYieldExecution();
}
static void op_mutant(void *c)
{
    LARGE_INTEGER zero = {.QuadPart = 0};
    NtWaitForSingleObjectEx(*(HANDLE *)c, UserMode, FALSE, &zero);
    NtReleaseMutant(*(HANDLE *)c, NULL);
}
static void op_event(void *c)
{
    NtSetEvent(*(HANDLE *)c, NULL);
    NtClearEvent(*(HANDLE *)c);
}
static void op_irql(void *c)
{
    (void)c;
    const KIRQL old = KeRaiseIrqlToDpcLevel();
    KfLowerIrql(old);
}

static HANDLE g_ping, g_pong;
static volatile LONG g_pp_stop;
static DWORD WINAPI pong_main(LPVOID p)
{
    (void)p;
    for (;;) {
        WaitForSingleObject(g_ping, INFINITE);
        if (g_pp_stop) {
            break;
        }
        SetEvent(g_pong);
    }
    return 0;
}

void test_time(void)
{
    section("time: clocks, sleeps, waits, kernel calls");

    calibrate_tsc();
    report("INFO KeQueryPerformanceFrequency %llu Hz; rdtsc runs at %.1f MHz (the Xbox's CPU: 733.3)",
           (unsigned long long)qpc_freq(), g_tsc_per_us);

    /* one second by the performance counter, against the others */
    LARGE_INTEGER st0, st1;
    const DWORD tick0 = KeTickCount;
    const ULONGLONG it0 = KeQueryInterruptTime();
    KeQuerySystemTime(&st0);
    const uint64_t q0 = qpc();
    while (ms_since(q0) < 1000.0) {
    }
    const double ms = ms_since(q0);
    const DWORD tick1 = KeTickCount;
    const ULONGLONG it1 = KeQueryInterruptTime();
    KeQuerySystemTime(&st1);
    const double ticks = (double)(tick1 - tick0) * 1000.0 / ms;
    const double itime = (double)(it1 - it0) / 10000.0 * 1000.0 / ms;
    const double stime = (double)(st1.QuadPart - st0.QuadPart) / 10000.0 * 1000.0 / ms;
    check(ticks > 990.0 && ticks < 1010.0, "KeTickCount: %.1f a second (1000)", ticks);
    check(itime > 990.0 && itime < 1010.0, "KeQueryInterruptTime: %.1f ms a second (1000)", itime);
    check(stime > 990.0 && stime < 1010.0, "KeQuerySystemTime: %.1f ms a second (1000)", stime);

    /* a busy loop without kernel calls still sees KeTickCount move */
    {
        const DWORD t0 = KeTickCount;
        const uint64_t qa = qpc();
        DWORD t;
        while ((t = *(volatile DWORD *)&KeTickCount) == t0 && ms_since(qa) < 100.0) {
        }
        const uint64_t qb = qpc();
        while (*(volatile DWORD *)&KeTickCount - t < 10 && ms_since(qb) < 1000.0) {
        }
        const double per10 = ms_since(qb);
        check(per10 > 8.0 && per10 < 13.0, "KeTickCount polled without kernel calls: 10 ticks in %.2f ms (10)",
              per10);
    }

    sleep_stats(0, 50);
    sleep_stats(1, 50);
    sleep_stats(2, 25);
    sleep_stats(10, 10);
    sleep_stats(16, 10);

    {
        double sum = 0.0, worst = 0.0;
        for (unsigned i = 0; i < 50; i++) {
            LARGE_INTEGER rel = {.QuadPart = -10000}; /* 1 ms */
            const uint64_t qa = qpc();
            KeDelayExecutionThread(UserMode, FALSE, &rel);
            const double t = ms_since(qa);
            sum += t;
            worst = t > worst ? t : worst;
        }
        check(sum / 50 < 3.0, "KeDelayExecutionThread(1 ms) x50: average %.2f ms, worst %.2f (1 to 2)", sum / 50,
              worst);
    }

    {
        HANDLE ev = CreateEvent(NULL, FALSE, FALSE, NULL);
        double sum = 0.0, worst = 0.0;
        for (unsigned i = 0; i < 20; i++) {
            const uint64_t qa = qpc();
            WaitForSingleObject(ev, 5);
            const double t = ms_since(qa);
            sum += t;
            worst = t > worst ? t : worst;
        }
        CloseHandle(ev);
        check(sum / 20 >= 4.9 && sum / 20 < 7.5,
              "WaitForSingleObject timeout 5 ms x20: average %.2f ms, worst %.2f (5 to 7)", sum / 20, worst);
    }

    {
        KTIMER timer;
        KeInitializeTimerEx(&timer, SynchronizationTimer);
        LARGE_INTEGER due = {.QuadPart = -160000};
        KeSetTimerEx(&timer, due, 16, NULL);
        KeWaitForSingleObject(&timer, Executive, KernelMode, FALSE, NULL);
        const uint64_t qa = qpc();
        double worst = 0.0;
        uint64_t last = qa;
        for (unsigned i = 0; i < 30; i++) {
            KeWaitForSingleObject(&timer, Executive, KernelMode, FALSE, NULL);
            const uint64_t q = qpc();
            const double gap = qpc_us(q - last) / 1000.0;
            worst = gap > worst ? gap : worst;
            last = q;
        }
        KeCancelTimer(&timer);
        const double avg = ms_since(qa) / 30.0;
        check(avg > 15.5 && avg < 17.0 && worst < 25.0,
              "periodic kernel timer 16 ms: average period %.2f ms, longest %.2f (16)", avg, worst);
    }

    /* the cost of kernel calls */
    cost("read KeTickCount (no call)", op_tick, NULL);
    cost("KeQueryPerformanceCounter", op_qpc, NULL);
    cost("NtYieldExecution, no other thread ready", op_yield, NULL);
    cost("KeRaiseIrqlToDpcLevel + KfLowerIrql", op_irql, NULL);
    {
        HANDLE m = NULL;
        NtCreateMutant(&m, NULL, FALSE);
        cost("NtWaitForSingleObjectEx(mutant, 0) + NtReleaseMutant (Def Jam's pair)", op_mutant, &m);
        NtClose(m);
        HANDLE e = NULL;
        NtCreateEvent(&e, NULL, NotificationEvent, FALSE);
        cost("NtSetEvent + NtClearEvent", op_event, &e);
        NtClose(e);
    }

    /* two threads handing over with events */
    {
        g_ping = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_pong = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_pp_stop = 0;
        HANDLE t = CreateThread(NULL, 0, pong_main, NULL, 0, NULL);
        unsigned n = 0;
        const uint64_t qa = qpc();
        while (ms_since(qa) < 500.0) {
            SetEvent(g_ping);
            WaitForSingleObject(g_pong, INFINITE);
            n++;
        }
        const double us = qpc_us(qpc() - qa) / n;
        g_pp_stop = 1;
        SetEvent(g_ping);
        WaitForSingleObject(t, 2000);
        CloseHandle(t);
        CloseHandle(g_ping);
        CloseHandle(g_pong);
        report("INFO event hand-over between two threads: %.1f us a round trip (%u in 0.5 s)", us, n);
    }
}
