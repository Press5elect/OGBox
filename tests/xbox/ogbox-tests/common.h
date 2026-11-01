/*
 * OGBox tests - small Xbox programs (nxdk) that check one emulator behaviour at a time.
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * Every result is one line, sent three ways: DbgPrint (OGBox's boot log, "hakuX-native-dbg:
 * OGBT ..."), the screen, and E:\ogbox-tests.txt (/data/ogbox/hdd/... on the console).
 * PASS/FAIL lines compare with what the Xbox does; INFO lines are measurements to compare.
 */
#ifndef OGBOX_TESTS_COMMON_H
#define OGBOX_TESTS_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#define OGBT_VERSION "1.1"

void ogbt_open_output(void);
void ogbt_close_output(void);
void report(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void check(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void section(const char *name);

extern int g_pass, g_fail;

/* time */
uint64_t qpc(void);
uint64_t qpc_freq(void);
double qpc_us(uint64_t ticks);
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
extern double g_tsc_per_us; /* measured by test_time (needed by test_cpu) */
void calibrate_tsc(void);

/* the disc's test data: word i (at byte 4*i) is gen_word(i), little-endian */
static inline uint32_t gen_word(uint32_t i)
{
    uint32_t x = i * 0x9E3779B1u ^ 0x7F4A7C15u;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    x ^= x >> 15;
    return x;
}
/* the first byte in [off, off+len) that differs from the test data, or -1 */
int64_t first_mismatch(const uint8_t *buf, uint64_t off, uint32_t len);

/* a small, fixed pseudo-random sequence */
uint32_t rnd(void);
void rnd_seed(uint32_t s);

void test_cpu(void);
void test_mem(void);
void test_time(void);
void test_disc(void);
void test_hdd(void);

#endif
