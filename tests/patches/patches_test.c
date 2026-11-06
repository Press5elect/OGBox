/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The PC test of the IPS and BPS readers (ps5/src/core/ogbox_patches.c): good patches apply, and
 * broken or hostile ones are turned down without reading or writing past a buffer.
 *   tests/patches/run-patches-tests.sh
 */
#include "core/ogbox_patches.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t d[4096];
    size_t n;
} Bytes;

static void put(Bytes *b, const void *p, size_t n)
{
    assert(b->n + n <= sizeof(b->d));
    memcpy(b->d + b->n, p, n);
    b->n += n;
}

static void put8(Bytes *b, uint8_t v) { put(b, &v, 1); }

/* BPS's numbers, as byuu's encoder writes them */
static void put_num(Bytes *b, uint64_t v)
{
    for (;;) {
        const uint8_t x = v & 0x7F;
        v >>= 7;
        if (!v) {
            put8(b, 0x80 | x);
            return;
        }
        put8(b, x);
        v--;
    }
}

static void put32(Bytes *b, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        put8(b, (uint8_t)(v >> (8 * i)));
    }
}

static OgboxBuf buf_of(const char *s)
{
    OgboxBuf b = {0};
    b.size = strlen(s);
    b.cap = b.size ? b.size : 1;
    b.data = malloc(b.cap);
    memcpy(b.data, s, b.size);
    return b;
}

/* "BPS1", the sizes, no metadata; the actions are the caller's, then the footer */
static void bps_head(Bytes *b, size_t src, size_t dst)
{
    put(b, "BPS1", 4);
    put_num(b, src);
    put_num(b, dst);
    put_num(b, 0);
}

static void bps_foot(Bytes *b, const OgboxBuf *src, const char *dst)
{
    put32(b, ogbox_crc32(src->data, src->size));
    put32(b, ogbox_crc32((const uint8_t *)dst, strlen(dst)));
    put32(b, 0);
}

static bool apply_bps(const char *src, const Bytes *p)
{
    OgboxBuf b = buf_of(src);
    char why[160] = "";
    const bool ok = ogbox_apply_bps(&b, p->d, p->n, why, sizeof(why));
    free(b.data);
    return ok;
}

static void test_ips(void)
{
    Bytes p = {0};
    put(&p, "PATCH", 5);
    put(&p, "\x00\x00\x01\x00\x02XY", 7);       /* offset 1: "XY" */
    put(&p, "\x00\x00\x06\x00\x00\x00\x03Z", 8); /* offset 6: "ZZZ" (RLE, past the end) */
    put(&p, "EOF", 3);
    OgboxBuf b = buf_of("abcdef");
    char why[160] = "";
    assert(ogbox_apply_ips(&b, p.d, p.n, why, sizeof(why)));
    assert(b.size == 9 && !memcmp(b.data, "aXYdefZZZ", 9));
    free(b.data);

    /* the truncation extension */
    put(&p, "\x00\x00\x04", 3);
    b = buf_of("abcdef");
    assert(ogbox_apply_ips(&b, p.d, p.n, why, sizeof(why)));
    assert(b.size == 4 && !memcmp(b.data, "aXYd", 4));
    free(b.data);

    /* a record longer than the file */
    Bytes bad = {0};
    put(&bad, "PATCH", 5);
    put(&bad, "\x00\x00\x00\xFF\xFF" "ab", 7);
    put(&bad, "EOF", 3);
    b = buf_of("abc");
    assert(!ogbox_apply_ips(&b, bad.d, bad.n, why, sizeof(why)));
    assert(b.size == 3 && !memcmp(b.data, "abc", 3));
    free(b.data);
}

static void test_bps_good(void)
{
    /* "hello world" -> "hello hello world!": SourceRead 6, SourceCopy 0 (len 6), SourceCopy 6
     * (len 5), TargetRead "!", then the same with a TargetCopy that overlaps */
    const char *src = "hello world";
    const char *dst = "hello hello world!";
    OgboxBuf s = buf_of(src);
    Bytes p = {0};
    bps_head(&p, strlen(src), strlen(dst));
    put_num(&p, (6 - 1) << 2 | 0);
    put_num(&p, (6 - 1) << 2 | 2);
    put_num(&p, 0 << 1);
    put_num(&p, (5 - 1) << 2 | 2);
    put_num(&p, 0 << 1);              /* src_rel is 6 after the first copy */
    put_num(&p, (1 - 1) << 2 | 1);
    put8(&p, '!');
    bps_foot(&p, &s, dst);
    OgboxBuf b = buf_of(src);
    char why[160] = "";
    assert(ogbox_apply_bps(&b, p.d, p.n, why, sizeof(why)));
    assert(b.size == strlen(dst) && !memcmp(b.data, dst, b.size));
    free(b.data);

    /* "ab" -> "abababab": TargetRead "ab", TargetCopy from 0 (len 6) */
    free(s.data);
    s = buf_of("ab");
    Bytes q = {0};
    bps_head(&q, 2, 8);
    put_num(&q, (2 - 1) << 2 | 1);
    put(&q, "ab", 2);
    put_num(&q, (6 - 1) << 2 | 3);
    put_num(&q, 0 << 1);
    bps_foot(&q, &s, "abababab");
    assert(apply_bps("ab", &q));
    free(s.data);
}

static void test_bps_hostile(void)
{
    const char *src = "0123456789abcdef";
    OgboxBuf s = buf_of(src);

    /* a TargetCopy that starts in the bytes it is writing and reads past the output's end: an 8 KB
     * output (larger than the 4 KB the buffer starts at, so the sanitizer sees the end), filled by
     * one byte and a copy of it, then 8 bytes copied from its last byte */
    Bytes tc = {0};
    bps_head(&tc, strlen(src), 8192);
    put_num(&tc, (1 - 1) << 2 | 1);
    put8(&tc, 'x');
    put_num(&tc, (8183 - 1) << 2 | 3);
    put_num(&tc, 0 << 1);
    put_num(&tc, (8 - 1) << 2 | 3);
    put_num(&tc, 8 << 1);            /* dst_rel 8183 + 8 = 8191: reads 8191..8198 of 8192 */
    bps_foot(&tc, &s, src);
    assert(!apply_bps(src, &tc));

    /* lengths far beyond the output */
    Bytes p = {0};
    bps_head(&p, strlen(src), 16);
    put_num(&p, (1 - 1) << 2 | 1);
    put8(&p, 'x');
    put_num(&p, ((UINT64_MAX >> 2) - 1) << 2 | 1);
    bps_foot(&p, &s, "xxxxxxxxxxxxxxxx");
    assert(!apply_bps(src, &p));

    /* the same with a SourceRead and a SourceCopy */
    Bytes q = {0};
    bps_head(&q, strlen(src), 16);
    put_num(&q, (1 - 1) << 2 | 0);
    put_num(&q, ((UINT64_MAX >> 2) - 1) << 2 | 0);
    bps_foot(&q, &s, src);
    assert(!apply_bps(src, &q));
    Bytes r = {0};
    bps_head(&r, strlen(src), 16);
    put_num(&r, (1 - 1) << 2 | 0);
    put_num(&r, ((UINT64_MAX >> 2) - 1) << 2 | 2);
    put_num(&r, 0);
    bps_foot(&r, &s, src);
    assert(!apply_bps(src, &r));

    /* a TargetCopy from bytes not written yet, reaching past the output's end */
    Bytes t = {0};
    bps_head(&t, strlen(src), 16);
    put_num(&t, (8 - 1) << 2 | 0);
    put_num(&t, (8 - 1) << 2 | 3);
    put_num(&t, 15 << 1);            /* dst_rel 15, 8 bytes: reads 15..22 of 16 */
    bps_foot(&t, &s, src);
    assert(!apply_bps(src, &t));

    /* metadata longer than the file (its length wraps the offset) */
    Bytes m = {0};
    put(&m, "BPS1", 4);
    put_num(&m, strlen(src));
    put_num(&m, 16);
    put_num(&m, UINT64_MAX - 2);
    bps_foot(&m, &s, src);
    assert(!apply_bps(src, &m));

    /* a number with more than 64 bits in it */
    Bytes w = {0};
    bps_head(&w, strlen(src), 16);
    for (int i = 0; i < 12; i++) {
        put8(&w, 0x7F);
    }
    put8(&w, 0x80);
    bps_foot(&w, &s, src);
    assert(!apply_bps(src, &w));
    free(s.data);
}

int main(void)
{
    test_ips();
    test_bps_good();
    test_bps_hostile();
    puts("patches: ok");
    return 0;
}
