/*
 * OGBox - an XBE's title image as RGBA (ogbox_xpr.h).
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The XPR0 layout: "XPR0", its total size, the size of its headers (where the texture's data
 * starts), then the texture's resource header: its common word, data offset, lock, and the format
 * word (bits 8-15 the colour format, 20-23 log2 of the width, 24-27 of the height) and a size word
 * (linear formats: width - 1 in bits 0-11, height - 1 in 12-23, pitch / 64 - 1 in 24-31).
 */
#include "core/ogbox_xpr.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void rgb565(uint16_t c, uint8_t out[3])
{
    out[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
    out[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
    out[2] = (uint8_t)((c & 31) * 255 / 31);
}

/* a DXT colour block (8 bytes) into 4 x 4 RGBA; one_bit: DXT1's transparent fourth colour */
static void dxt_colour(const uint8_t *b, uint8_t px[16][4], bool one_bit)
{
    const uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    uint8_t c[4][4];
    rgb565(c0, c[0]);
    rgb565(c1, c[1]);
    c[0][3] = c[1][3] = c[2][3] = c[3][3] = 255;
    if (c0 > c1 || !one_bit) {
        for (int k = 0; k < 3; k++) {
            c[2][k] = (uint8_t)((2 * c[0][k] + c[1][k]) / 3);
            c[3][k] = (uint8_t)((c[0][k] + 2 * c[1][k]) / 3);
        }
    } else {
        for (int k = 0; k < 3; k++) {
            c[2][k] = (uint8_t)((c[0][k] + c[1][k]) / 2);
            c[3][k] = 0;
        }
        c[3][3] = 0;
    }
    const uint32_t idx = rd32(b + 4);
    for (int i = 0; i < 16; i++) {
        memcpy(px[i], c[(idx >> (2 * i)) & 3], 4);
    }
}

static void dxt_alpha3(const uint8_t *b, uint8_t px[16][4])
{
    for (int i = 0; i < 16; i++) {
        const uint8_t a = (uint8_t)((b[i / 2] >> (4 * (i & 1))) & 15);
        px[i][3] = (uint8_t)(a * 17);
    }
}

static void dxt_alpha5(const uint8_t *b, uint8_t px[16][4])
{
    uint8_t a[8];
    a[0] = b[0];
    a[1] = b[1];
    if (a[0] > a[1]) {
        for (int k = 1; k < 7; k++) {
            a[k + 1] = (uint8_t)(((7 - k) * a[0] + k * a[1]) / 7);
        }
    } else {
        for (int k = 1; k < 5; k++) {
            a[k + 1] = (uint8_t)(((5 - k) * a[0] + k * a[1]) / 5);
        }
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t bits = 0;
    for (int k = 0; k < 6; k++) {
        bits |= (uint64_t)b[2 + k] << (8 * k);
    }
    for (int i = 0; i < 16; i++) {
        px[i][3] = a[(bits >> (3 * i)) & 7];
    }
}

/* the swizzled (Morton order) index of x, y in a w x h texture */
static uint32_t swizzle(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t out = 0, bit = 1;
    for (uint32_t mx = 1, my = 1; mx < w || my < h; mx <<= 1, my <<= 1) {
        if (mx < w) {
            if (x & mx) {
                out |= bit;
            }
            bit <<= 1;
        }
        if (my < h) {
            if (y & my) {
                out |= bit;
            }
            bit <<= 1;
        }
    }
    return out;
}

bool ogbox_xpr_decode(const uint8_t *xpr, size_t n, uint8_t **rgba, int *w, int *h)
{
    *rgba = NULL;
    if (n < 32 || memcmp(xpr, "XPR0", 4) != 0) {
        return false;
    }
    const uint32_t hdr = rd32(xpr + 8);
    const uint32_t fmtw = rd32(xpr + 12 + 12), sizew = rd32(xpr + 12 + 16);
    const uint32_t fmt = (fmtw >> 8) & 0xFF;
    uint32_t tw = 1u << ((fmtw >> 20) & 15), th = 1u << ((fmtw >> 24) & 15);
    const bool linear = fmt == 0x12 || fmt == 0x1E || fmt == 0x11;
    uint32_t pitch = 0;
    if (linear) {
        tw = (sizew & 0xFFF) + 1;
        th = ((sizew >> 12) & 0xFFF) + 1;
        pitch = ((sizew >> 24) + 1) * 64;
    }
    if (hdr >= n || tw > 1024 || th > 1024 || !tw || !th) {
        return false;
    }
    const uint8_t *d = xpr + hdr;
    const size_t avail = n - hdr;
    uint8_t *out = calloc((size_t)tw * th, 4);
    if (!out) {
        return false;
    }
    bool ok = true;
    if (fmt == 0x0C || fmt == 0x0E || fmt == 0x0F) {
        /* DXT1, DXT2/3, DXT4/5 */
        const unsigned bs = fmt == 0x0C ? 8 : 16;
        const uint32_t bw = (tw + 3) / 4, bh = (th + 3) / 4;
        if ((size_t)bw * bh * bs > avail) {
            ok = false;
        }
        for (uint32_t by = 0; ok && by < bh; by++) {
            for (uint32_t bx = 0; bx < bw; bx++) {
                const uint8_t *b = d + ((size_t)by * bw + bx) * bs;
                uint8_t px[16][4];
                if (fmt == 0x0C) {
                    dxt_colour(b, px, true);
                } else {
                    dxt_colour(b + 8, px, false);
                    if (fmt == 0x0E) {
                        dxt_alpha3(b, px);
                    } else {
                        dxt_alpha5(b, px);
                    }
                }
                for (int i = 0; i < 16; i++) {
                    const uint32_t x = bx * 4 + (uint32_t)(i & 3), y = by * 4 + (uint32_t)(i >> 2);
                    if (x < tw && y < th) {
                        memcpy(out + ((size_t)y * tw + x) * 4, px[i], 4);
                    }
                }
            }
        }
    } else if (fmt == 0x06 || fmt == 0x07 || fmt == 0x12 || fmt == 0x1E) {
        /* A8R8G8B8 / X8R8G8B8, swizzled (06, 07) or linear (12, 1E) */
        const bool alpha = fmt == 0x06 || fmt == 0x12;
        if (linear ? (size_t)pitch * th > avail : (size_t)tw * th * 4 > avail) {
            ok = false;
        }
        for (uint32_t y = 0; ok && y < th; y++) {
            for (uint32_t x = 0; x < tw; x++) {
                const uint8_t *s = linear ? d + (size_t)y * pitch + x * 4 : d + (size_t)swizzle(x, y, tw, th) * 4;
                uint8_t *o = out + ((size_t)y * tw + x) * 4;
                o[0] = s[2];
                o[1] = s[1];
                o[2] = s[0];
                o[3] = alpha ? s[3] : 255;
            }
        }
    } else {
        ok = false;
    }
    if (!ok) {
        free(out);
        return false;
    }
    *rgba = out;
    *w = (int)tw;
    *h = (int)th;
    return true;
}
