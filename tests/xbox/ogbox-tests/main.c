/*
 * OGBox tests - the runner.
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 */
#include "common.h"

#include <hal/debug.h>
#include <hal/video.h>
#include <nxdk/mount.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_pass, g_fail;
static FILE *g_out;
static unsigned g_lines;

void ogbt_open_output(void)
{
    if (!nxIsDriveMounted('E')) {
        nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    }
    g_out = fopen("E:\\ogbox-tests.txt", "w");
}

void ogbt_close_output(void)
{
    if (g_out) {
        fclose(g_out);
        g_out = NULL;
    }
}

static void emit(const char *line)
{
    /* DbgPrint: OGBox's boot log keeps 255 characters of a %s argument */
    DbgPrint("OGBT %s", line);
    if (g_out) {
        fprintf(g_out, "%s\n", line);
        fflush(g_out);
    }
    /* the screen holds about 28 lines: start again at the top */
    if (++g_lines % 28 == 0) {
        debugClearScreen();
    }
    debugPrint("%s\n", line);
}

/* nxdk's C library (PDCLib) prints no floating point: %f is done here, the rest by snprintf one
 * conversion at a time */
static void fixed(char *out, size_t size, double v, int decimals)
{
    if (v != v) {
        snprintf(out, size, "nan");
        return;
    }
    const char *sign = "";
    if (v < 0) {
        sign = "-";
        v = -v;
    }
    if (v > 1e15) {
        snprintf(out, size, "%sinf", sign);
        return;
    }
    uint64_t scale = 1;
    for (int i = 0; i < decimals; i++) {
        scale *= 10;
    }
    const uint64_t all = (uint64_t)(v * (double)scale + 0.5);
    if (decimals > 0) {
        snprintf(out, size, "%s%llu.%0*llu", sign, (unsigned long long)(all / scale), decimals,
                 (unsigned long long)(all % scale));
    } else {
        snprintf(out, size, "%s%llu", sign, (unsigned long long)all);
    }
}

static void vformat(char *out, size_t size, const char *fmt, va_list ap)
{
    size_t n = 0;
    out[0] = 0;
    for (const char *p = fmt; *p && n + 1 < size;) {
        if (*p != '%') {
            out[n++] = *p++;
            out[n] = 0;
            continue;
        }
        char spec[24];
        size_t s = 0;
        spec[s++] = *p++;
        int longs = 0, decimals = 6;
        while (*p && strchr("-+ #0123456789.lhz", *p) && s < sizeof(spec) - 3) {
            if (*p == 'l') {
                longs++;
            }
            if (*p == '.') {
                decimals = atoi(p + 1);
            }
            spec[s++] = *p++;
        }
        if (!*p) {
            break;
        }
        const char conv = *p++;
        spec[s++] = conv;
        spec[s] = 0;
        char piece[128];
        switch (conv) {
        case 'f':
            fixed(piece, sizeof(piece), va_arg(ap, double), decimals);
            break;
        case 'd':
        case 'i':
            if (longs >= 2) {
                snprintf(piece, sizeof(piece), spec, va_arg(ap, long long));
            } else {
                snprintf(piece, sizeof(piece), spec, va_arg(ap, int));
            }
            break;
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            if (longs >= 2) {
                snprintf(piece, sizeof(piece), spec, va_arg(ap, unsigned long long));
            } else {
                snprintf(piece, sizeof(piece), spec, va_arg(ap, unsigned int));
            }
            break;
        case 'c':
            snprintf(piece, sizeof(piece), spec, va_arg(ap, int));
            break;
        case 's':
            snprintf(piece, sizeof(piece), spec, va_arg(ap, const char *));
            break;
        case 'p':
            snprintf(piece, sizeof(piece), "0x%08lx", (unsigned long)(uintptr_t)va_arg(ap, void *));
            break;
        default:
            snprintf(piece, sizeof(piece), "%s", conv == '%' ? "%" : spec);
            break;
        }
        const size_t len = strlen(piece);
        const size_t room = size - 1 - n;
        memcpy(out + n, piece, len < room ? len : room);
        n += len < room ? len : room;
        out[n] = 0;
    }
}

void report(const char *fmt, ...)
{
    char line[240];
    va_list ap;
    va_start(ap, fmt);
    vformat(line, sizeof(line), fmt, ap);
    va_end(ap);
    emit(line);
}

void check(bool ok, const char *fmt, ...)
{
    char text[220], line[240];
    va_list ap;
    va_start(ap, fmt);
    vformat(text, sizeof(text), fmt, ap);
    va_end(ap);
    snprintf(line, sizeof(line), "%s %s", ok ? "PASS" : "FAIL", text);
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
    }
    emit(line);
}

void section(const char *name)
{
    report("== %s", name);
}

/* time */
uint64_t qpc(void)
{
    return KeQueryPerformanceCounter();
}

uint64_t qpc_freq(void)
{
    static uint64_t f;
    if (!f) {
        f = KeQueryPerformanceFrequency();
    }
    return f;
}

double qpc_us(uint64_t ticks)
{
    return (double)ticks * 1e6 / (double)qpc_freq();
}

double g_tsc_per_us;

void calibrate_tsc(void)
{
    const uint64_t q0 = qpc(), t0 = rdtsc();
    while (qpc_us(qpc() - q0) < 100000.0) {
    }
    const uint64_t q1 = qpc(), t1 = rdtsc();
    g_tsc_per_us = (double)(t1 - t0) / qpc_us(q1 - q0);
}

int64_t first_mismatch(const uint8_t *buf, uint64_t off, uint32_t len)
{
    for (uint32_t k = 0; k < len;) {
        const uint64_t at = off + k;
        const uint32_t word = gen_word((uint32_t)(at >> 2));
        const uint8_t want = (uint8_t)(word >> (8 * (at & 3)));
        if (buf[k] != want) {
            return (int64_t)k;
        }
        k++;
    }
    return -1;
}

static uint32_t g_rnd = 0x12345678u;
void rnd_seed(uint32_t s)
{
    g_rnd = s ? s : 1;
}
uint32_t rnd(void)
{
    g_rnd ^= g_rnd << 13;
    g_rnd ^= g_rnd >> 17;
    g_rnd ^= g_rnd << 5;
    return g_rnd;
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    ogbt_open_output();

    report("OGBox tests %s (OGBox by Press5elect) - kernel %u.%u.%u.%u", OGBT_VERSION,
           XboxKrnlVersion.Major, XboxKrnlVersion.Minor, XboxKrnlVersion.Build, XboxKrnlVersion.Qfe);

    const uint64_t t0 = qpc();
    test_time();
    test_cpu();
    test_mem();
    test_disc();
    test_hdd();

    report("== done: %d passed, %d failed, in %.1f s", g_pass, g_fail, qpc_us(qpc() - t0) / 1e6);
    report("results: OGBox boot log (OGBT lines) and E:\\ogbox-tests.txt");
    ogbt_close_output();

    for (;;) {
        Sleep(1000);
    }
    return 0;
}
