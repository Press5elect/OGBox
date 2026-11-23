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
#include <stdbool.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static bool apply_records(OgboxBuf *b, const char *a, const char *c)
{
    char *recs[2] = {(char *)a, (char *)c};
    char why[160] = "";
    return ogbox_apply_records(b, recs, c ? 2 : 1, ".", why, sizeof(why));
}

static void test_records(void)
{
    OgboxBuf b = buf_of("abcdef");
    /* search and replace, offset, append */
    assert(apply_records(&b, "F\t6364\t5859", "O\t0\t41"));
    assert(b.size == 6 && !memcmp(b.data, "AbXYef", 6));
    assert(apply_records(&b, "A\t2121", NULL));
    assert(b.size == 8 && !memcmp(b.data, "AbXYef!!", 8));
    /* the CRC-32 the patch was made for */
    char crc[32];
    snprintf(crc, sizeof(crc), "C\t%08x", ogbox_crc32(b.data, b.size));
    assert(apply_records(&b, crc, "O\t7\t3f"));
    assert(!apply_records(&b, "C\t00000000", "O\t0\t00"));
    assert(!memcmp(b.data, "AbXYef!?", 8));         /* a record turned down leaves the file as it was */
    /* bytes not there, odd hex, an offset at or past the end, one whose sum wraps around */
    assert(!apply_records(&b, "F\t7a7a\t7979", NULL));
    assert(!apply_records(&b, "O\t0\t414", NULL));
    assert(!apply_records(&b, "O\t8\t41", NULL));
    assert(!apply_records(&b, "O\t7\t4142", NULL));
    assert(!apply_records(&b, "O\tFFFFFFFFFFFFFFFF\t41", NULL));
    assert(!apply_records(&b, "I\tno-such-file.ips", NULL));
    assert(b.size == 8 && !memcmp(b.data, "AbXYef!?", 8));

    /* 1.0.3: a record too long for the reader is refused, not cut and applied in part */
    static char longrec[12000];
    snprintf(longrec, sizeof(longrec), "O\t00\t");     /* (cut at 8191: an even number of digits) */
    for (size_t i = strlen(longrec); i < sizeof(longrec) - 1; i++) {
        longrec[i] = '4';
    }
    OgboxBuf big = {0};
    big.size = big.cap = 8192;
    big.data = calloc(1, big.size);
    assert(!apply_records(&big, longrec, NULL));
    assert(big.data[0] == 0);
    free(big.data);
    /* a record it can't read fails the patch: an unknown kind, one short of its fields, a number
     * that isn't one */
    assert(!apply_records(&b, "O\t0\t41", "X\t00"));
    assert(!apply_records(&b, "O\t0\t41", "F\t6162"));
    assert(!apply_records(&b, "O\tzz\t41", NULL));
    assert(!apply_records(&b, "O\t0\t41", "C\t1a2b3c4dxyz"));
    assert(!memcmp(b.data, "AbXYef!?", 8));
    /* an IPS or BPS file outside the patches' folder: one that would apply, named through ".." */
    char back[1024], tmpdir[] = "/tmp/ogbox-patches-XXXXXX";
    assert(getcwd(back, sizeof(back)) && mkdtemp(tmpdir) && chdir(tmpdir) == 0);
    FILE *ips = fopen("ogbox-records-test.ips", "wb");
    assert(ips);
    fwrite("PATCH\x00\x00\x00\x00\x01ZEOF", 1, 14, ips);
    fclose(ips);
    char here[1024], *slash, outside[1200];
    assert(getcwd(here, sizeof(here)) && (slash = strrchr(here, '/')));
    snprintf(outside, sizeof(outside), "I\t../%s/ogbox-records-test.ips", slash[1] ? slash + 1 : ".");
    assert(!apply_records(&b, outside, NULL));
    assert(!apply_records(&b, "B\t/etc/hostname", NULL));
    assert(!memcmp(b.data, "AbXYef!?", 8));
    assert(apply_records(&b, "I\togbox-records-test.ips", NULL));
    assert(!memcmp(b.data, "ZbXYef!?", 8));
    remove("ogbox-records-test.ips");
    assert(chdir(back) == 0 && rmdir(tmpdir) == 0);
    free(b.data);

    /* 1.0.4: N, the bytes in the file exactly so many times (hex) */
    OgboxBuf n = buf_of("xyxyxyxyxyxyxyxyxyxyz");
    assert(apply_records(&n, "N	7879	A", "F	7a	5a"));
    assert(!memcmp(n.data + 20, "Z", 1));
    assert(!apply_records(&n, "N	7879	9", "F	5a	7a"));       /* more than it says */
    assert(!apply_records(&n, "N	7879	B", "F	5a	7a"));       /* fewer */
    assert(!apply_records(&n, "N	7a	1", NULL));                /* not there */
    assert(apply_records(&n, "N	7a	0", NULL));
    assert(!apply_records(&n, "N	7879", NULL));                 /* short of its count */
    assert(!apply_records(&n, "N	787	1", NULL));               /* odd hex */
    assert(!apply_records(&n, "N	7879	zz", NULL));
    assert(!memcmp(n.data, "xyxyxyxyxyxyxyxyxyxyZ", 21));
    free(n.data);
}

/* 1.0.4: every "Widescreen (any release)" entry of the shipped patches.db (OgXHD's database for any
 * release) on a file made of its own patterns, each line's find once in turn: it applies and writes
 * every line's replacement; with one more copy of any of its patterns it is refused, the file left
 * as it was */
static void test_any_release(const char *db)
{
    FILE *f = fopen(db, "r");
    assert(f);
    static char line[65536];
    static char *recs[512];
    unsigned nrec = 0, entries = 0;
    bool in = false;
    for (bool more = true; more;) {
        more = fgets(line, sizeof(line), f) != NULL;
        line[strcspn(line, "\n")] = 0;
        const bool next = !more || !strncmp(line, "P\t", 2);
        if (next && in) {
            /* the file: each F's find, in order, apart by four bytes of 0xF4 */
            OgboxBuf b = {0}, want = {0};
            Bytes sep = {{0xF4, 0xF4, 0xF4, 0xF4}, 4};
            for (unsigned k = 0; k < nrec; k++) {
                if (recs[k][0] != 'F') {
                    continue;
                }
                char *tab = strchr(recs[k] + 2, '\t');
                assert(tab);
                const size_t len = (size_t)(tab - (recs[k] + 2)) / 2;
                assert(len && !strcmp(tab + 1 + 2 * len, ""));
                for (int w = 0; w < 2; w++) {
                    OgboxBuf *t = w ? &want : &b;
                    const char *hex = w ? tab + 1 : recs[k] + 2;
                    t->data = realloc(t->data, t->size + len + sep.n);
                    assert(t->data);
                    for (size_t i = 0; i < len; i++) {
                        unsigned v;
                        assert(sscanf(hex + 2 * i, "%2x", &v) == 1);
                        t->data[t->size + i] = (uint8_t)v;
                    }
                    memcpy(t->data + t->size + len, sep.d, sep.n);
                    t->size += len + sep.n;
                    t->cap = t->size;
                }
            }
            OgboxBuf orig = {0};
            orig.data = malloc(b.size);
            assert(orig.data);
            memcpy(orig.data, b.data, b.size);
            orig.size = orig.cap = b.size;
            char why[192] = "";
            if (!ogbox_apply_records(&b, recs, nrec, ".", why, sizeof(why)) || b.size != want.size ||
                memcmp(b.data, want.data, b.size)) {
                fprintf(stderr, "patches: %s: %s\n", recs[0], why);
                abort();
            }
            /* one more copy of the first find (its record's bytes after the file's) */
            const char *first = strchr(recs[0], '\t') + 1;
            const size_t flen = (size_t)(strchr(first, '\t') - first) / 2;
            b.data = realloc(b.data, orig.size + flen);
            assert(b.data);
            memcpy(b.data, orig.data, orig.size);
            b.size = orig.size;
            for (size_t i = 0; i < flen; i++) {
                unsigned v;
                assert(sscanf(first + 2 * i, "%2x", &v) == 1);
                b.data[b.size + i] = (uint8_t)v;
            }
            b.size += flen;
            b.cap = b.size;
            assert(!ogbox_apply_records(&b, recs, nrec, ".", why, sizeof(why)));
            assert(!memcmp(b.data, orig.data, orig.size));
            free(b.data);
            free(orig.data);
            free(want.data);
            entries++;
        }
        if (next) {
            for (unsigned k = 0; k < nrec; k++) {
                free(recs[k]);
            }
            nrec = 0;
            in = more && strstr(line, "\tWidescreen (any release)\tOgXHD\t") != NULL;
            continue;
        }
        if (in && line[0] && strncmp(line, "T\t", 2)) {
            assert(nrec < 512 && (line[0] == 'N' || line[0] == 'F'));
            assert(nrec || line[0] == 'N');
            recs[nrec++] = strdup(line);
        }
    }
    fclose(f);
    printf("patches: %u any-release entries of %s apply on their own patterns and refuse one more\n", entries, db);
    assert(entries > 0);
}

/* every .ips that ships (ps5/gamedata/files) reads to its end; without the game's XBE, onto an
 * empty file */
static void test_shipped(const char *dir)
{
    DIR *d = opendir(dir);
    assert(d);
    unsigned n = 0;
    for (struct dirent *e; (e = readdir(d));) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcmp(dot, ".ips")) {
            continue;
        }
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        assert(f);
        static uint8_t p[1 << 20];
        const size_t pn = fread(p, 1, sizeof(p), f);
        fclose(f);
        OgboxBuf b = {0};
        char why[160] = "";
        if (!ogbox_apply_ips(&b, p, pn, why, sizeof(why))) {
            fprintf(stderr, "patches: %s: %s\n", e->d_name, why);
            abort();
        }
        free(b.data);
        n++;
    }
    closedir(d);
    assert(n > 0);
}

/* Broken patches of every kind: random bytes changed, cut short or grown, from good ones; none
 * may touch memory outside its buffers (the sanitizers watch), whether it applies or not. */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;

static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 16);
}

static void mutate(Bytes *m, const Bytes *seed)
{
    *m = *seed;
    const unsigned edits = 1 + rnd() % 8;
    for (unsigned k = 0; k < edits; k++) {
        switch (rnd() % 4) {
        case 0:
            if (m->n) {
                m->d[rnd() % m->n] = (uint8_t)rnd();
            }
            break;
        case 1:
            if (m->n) {
                m->d[rnd() % m->n] ^= (uint8_t)(1u << (rnd() % 8));
            }
            break;
        case 2:
            m->n = m->n ? rnd() % m->n : 0;
            break;
        default:
            if (m->n < sizeof(m->d)) {
                m->d[m->n++] = (uint8_t)(rnd() % 3 ? rnd() : 0xFF);
            }
        }
    }
}

static void test_fuzz(void)
{
    const char *src = "hello world, a little longer source";
    OgboxBuf s = buf_of(src);
    Bytes ips = {0}, bps = {0};
    put(&ips, "PATCH", 5);
    put(&ips, "\x00\x00\x01\x00\x02XY", 7);
    put(&ips, "\x00\x00\x06\x00\x00\x00\x03Z", 8);
    put(&ips, "EOF\x00\x00\x10", 6);
    bps_head(&bps, strlen(src), 40);
    put_num(&bps, (6 - 1) << 2 | 0);
    put_num(&bps, (2 - 1) << 2 | 1);
    put(&bps, "ab", 2);
    put_num(&bps, (10 - 1) << 2 | 3);
    put_num(&bps, 6 << 1);
    put_num(&bps, (22 - 1) << 2 | 2);
    put_num(&bps, 3 << 1);
    bps_foot(&bps, &s, "x");
    free(s.data);
    char why[160];
    for (int i = 0; i < 200000; i++) {
        Bytes m;
        mutate(&m, i & 1 ? &bps : &ips);
        OgboxBuf b = buf_of(src);
        if (i & 1) {
            /* keep the source CRC right half the time, so the actions get read */
            if (rnd() & 1 && m.n >= 16) {
                const uint32_t c = ogbox_crc32(b.data, b.size);
                for (int k = 0; k < 4; k++) {
                    m.d[m.n - 12 + k] = (uint8_t)(c >> (8 * k));
                }
            }
            ogbox_apply_bps(&b, m.d, m.n, why, sizeof(why));
        } else {
            ogbox_apply_ips(&b, m.d, m.n, why, sizeof(why));
        }
        free(b.data);
    }
}

int main(int argc, char **argv)
{
    test_ips();
    test_bps_good();
    test_bps_hostile();
    test_records();
    if (argc > 1) {
        test_shipped(argv[1]);
    }
    if (argc > 2) {
        test_any_release(argv[2]);
    }
    test_fuzz();
    puts("patches: ok");
    return 0;
}
