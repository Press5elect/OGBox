/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The PC test of the title image decoder (ps5/src/core/ogbox_xpr.c): the formats it reads, and
 * broken images turned down without reading past them.
 *   tests/xpr/run-xpr-tests.sh
 */
#include "core/ogbox_xpr.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HDR 64

static void wr32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

/* an XPR0 of data_len bytes of texture data after a HDR-byte header */
static uint8_t *xpr(uint32_t fmt, uint32_t lw, uint32_t lh, uint32_t sizew, size_t data_len, size_t *n)
{
    *n = HDR + data_len;
    uint8_t *p = calloc(1, *n);
    memcpy(p, "XPR0", 4);
    wr32(p + 4, (uint32_t)*n);
    wr32(p + 8, HDR);
    wr32(p + 24, fmt << 8 | lw << 20 | lh << 24);
    wr32(p + 28, sizew);
    return p;
}

static bool decode(const uint8_t *p, size_t n, int *w, int *h, uint8_t **rgba)
{
    const bool ok = ogbox_xpr_decode(p, n, rgba, w, h);
    assert(ok == (*rgba != NULL));
    return ok;
}

static void test_formats(void)
{
    int w, h;
    uint8_t *rgba;
    size_t n;

    /* A8R8G8B8 swizzled, 2 x 2: BGRA in, RGBA out */
    uint8_t *p = xpr(0x06, 1, 1, 0, 16, &n);
    for (int i = 0; i < 4; i++) {
        uint8_t *px = p + HDR + 4 * i;
        px[0] = 1, px[1] = 2, px[2] = 3, px[3] = (uint8_t)(10 * i);
    }
    assert(decode(p, n, &w, &h, &rgba) && w == 2 && h == 2);
    assert(rgba[0] == 3 && rgba[1] == 2 && rgba[2] == 1 && rgba[3] == 0);
    free(rgba);
    free(p);

    /* X8R8G8B8 linear, 3 x 2 with a 64-byte pitch: alpha 255 */
    p = xpr(0x1E, 0, 0, 2 | 1 << 12 | 0u << 24, 128, &n);
    p[HDR + 64 + 8] = 7;
    assert(decode(p, n, &w, &h, &rgba) && w == 3 && h == 2);
    assert(rgba[(1 * 3 + 2) * 4 + 2] == 7 && rgba[3] == 255);
    free(rgba);
    free(p);

    /* DXT1, 8 x 4: two blocks; DXT5, 4 x 4: one */
    p = xpr(0x0C, 3, 2, 0, 16, &n);
    assert(decode(p, n, &w, &h, &rgba) && w == 8 && h == 4);
    free(rgba);
    free(p);
    p = xpr(0x0F, 2, 2, 0, 16, &n);
    assert(decode(p, n, &w, &h, &rgba) && w == 4 && h == 4);
    free(rgba);
    free(p);
}

static void test_broken(void)
{
    int w, h;
    uint8_t *rgba;
    size_t n;

    /* a linear texture's pitch shorter than its rows: 1024 x 1 with a 64-byte pitch and 64 bytes */
    uint8_t *p = xpr(0x12, 0, 0, 1023 | 0u << 12 | 0u << 24, 64, &n);
    assert(!decode(p, n, &w, &h, &rgba));
    free(p);

    /* data cut short: swizzled, DXT and linear */
    p = xpr(0x06, 2, 2, 0, 63, &n);
    assert(!decode(p, n, &w, &h, &rgba));
    free(p);
    p = xpr(0x0E, 3, 3, 0, 63, &n);
    assert(!decode(p, n, &w, &h, &rgba));
    free(p);
    p = xpr(0x1E, 0, 0, 15 | 3u << 12 | 0u << 24, 64 * 4 - 1, &n);
    assert(!decode(p, n, &w, &h, &rgba));
    free(p);

    /* too large, an unknown format, the header past the end, too short, not an XPR */
    p = xpr(0x06, 11, 0, 0, 16, &n);
    assert(!decode(p, n, &w, &h, &rgba));
    free(p);
    p = xpr(0x05, 0, 0, 0, 16, &n);
    assert(!decode(p, n, &w, &h, &rgba));
    free(p);
    p = xpr(0x06, 0, 0, 0, 4, &n);
    wr32(p + 8, (uint32_t)n);
    assert(!decode(p, n, &w, &h, &rgba));
    assert(!decode(p, 31, &w, &h, &rgba));
    p[0] = 'Y';
    assert(!decode(p, n, &w, &h, &rgba));
    free(p);
}

/* random headers over random data: whatever decodes stays in its buffers (the sanitizers watch) */
static void test_fuzz(void)
{
    uint64_t r = 0x2545F4914F6CDD1Dull;
    for (int i = 0; i < 20000; i++) {
        r ^= r << 13, r ^= r >> 7, r ^= r << 17;
        static const uint32_t fmts[] = {0x06, 0x07, 0x0C, 0x0E, 0x0F, 0x11, 0x12, 0x1E};
        const size_t data = (size_t)(r >> 40) % 9000;
        size_t n;
        uint8_t *p = xpr(fmts[r % 8], (uint32_t)(r >> 8) % 12, (uint32_t)(r >> 12) % 12, (uint32_t)(r >> 16), data, &n);
        if (r & 0x100000) {
            wr32(p + 8, (uint32_t)(r >> 24) % (uint32_t)(n + 8));
        }
        int w, h;
        uint8_t *rgba;
        if (decode(p, n, &w, &h, &rgba)) {
            assert(w > 0 && h > 0 && w <= 1024 && h <= 1024);
            rgba[(size_t)w * h * 4 - 1] ^= 1;
            free(rgba);
        }
        free(p);
    }
}

int main(void)
{
    test_formats();
    test_broken();
    test_fuzz();
    puts("xpr: ok");
    return 0;
}
