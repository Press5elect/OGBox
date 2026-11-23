/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The PC test of the IPS and BPS readers (ps5/src/core/ogbox_patches.c): good patches apply, and
 * broken or hostile ones are turned down without reading or writing past a buffer. 1.0.4: the
 * player's own patches (a folder of .jmp, .ips and .bps files), on a disc stood in for here.
 *   tests/patches/run-patches-tests.sh
 */
#include "core/ogbox_patches.h"
#include "core/ogbox_mods.h"
#include "platform/ps5_bootlog.h"

#include "native/core/xdvdfs.h"

#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/* ---- 1.0.4: the player's own patches ---- */

/* what the loader calls of hakuX and the boot log, stood in for: the title folder, the log (kept, to
 * be looked at), and a disc that has only a default.xbe */
static char g_title[512];
static char g_log[1 << 17];
static size_t g_logn;
static OgboxBuf g_xbe;

void pshakux_log(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char line[2048];
    vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    g_logn += (size_t)snprintf(g_log + g_logn, sizeof(g_log) - g_logn, "%s\n", line);
    assert(g_logn < sizeof(g_log));
}

const char *pshakux_title_dir(void)
{
    return g_title;
}

bool nc_disc_read(const NcDisc *d, const NcDiscFile *f, uint64_t off, void *buf, size_t len)
{
    (void)d, (void)f, (void)off, (void)buf, (void)len;
    return false;
}

bool nc_disc_find_orig(const NcDisc *d, const char *path, NcDiscFile *out)
{
    (void)d;
    if (strcasecmp(path, "\\default.xbe") != 0) {
        return false;
    }
    out->sector = 0;
    out->size = (uint32_t)g_xbe.size;
    out->attr = 0;
    return true;
}

bool nc_disc_read_orig(const NcDisc *d, const NcDiscFile *f, uint64_t off, void *buf, size_t len)
{
    (void)d, (void)f;
    if (off > g_xbe.size || len > g_xbe.size - off) {
        return false;
    }
    memcpy(buf, g_xbe.data + off, len);
    return true;
}

static void write_file(const char *dir, const char *name, const void *p, size_t n)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(p, 1, n, f) == n);
    assert(fclose(f) == 0);
}

static void write_text(const char *dir, const char *name, const char *text)
{
    write_file(dir, name, text, strlen(text));
}

static void subdir(char *out, size_t n, const char *dir, const char *name)
{
    snprintf(out, n, "%s/%s", dir, name);
    assert(mkdir(out, 0777) == 0);
}

static const OgboxPatch *find_patch(const OgboxPatch *list, int n, const char *id)
{
    for (int i = 0; i < n; i++) {
        if (!strcmp(list[i].id, id)) {
            return &list[i];
        }
    }
    return NULL;
}

static bool logged(const char *text)
{
    if (!strstr(g_log, text)) {
        fprintf(stderr, "patches: the log has no \"%s\":\n%s", text, g_log);
        return false;
    }
    return true;
}

/* An IPS file writing two bytes at off */
static void ips_two(Bytes *p, unsigned off, const char *two)
{
    memset(p, 0, sizeof(*p));
    put(p, "PATCH", 5);
    const uint8_t rec[5] = {(uint8_t)(off >> 16), (uint8_t)(off >> 8), (uint8_t)off, 0, 2};
    put(p, rec, 5);
    put(p, two, 2);
    put(p, "EOF", 3);
}

/* the game's start: the patches on applied, the patched default.xbe (NULL: none written) */
static uint8_t *prepare(const char *base, const char *key, size_t *n)
{
    char work[600], mpath[700];
    snprintf(work, sizeof(work), "%s/work", base);
    mkdir(work, 0777);
    snprintf(mpath, sizeof(mpath), "%s/manifest.txt", base);
    FILE *m = fopen(mpath, "w");
    assert(m);
    NcDisc disc = {0};
    const int files = ogbox_patches_prepare(&disc, key, work, m);
    assert(fclose(m) == 0);
    if (!files) {
        return NULL;
    }
    m = fopen(mpath, "r");
    char line[2048];
    assert(m && fgets(line, sizeof(line), m));
    fclose(m);
    line[strcspn(line, "\n")] = 0;
    assert(!strncmp(line, "file\t\\default.xbe\t", 18));
    FILE *f = fopen(line + 18, "rb");
    assert(f);
    static uint8_t out[8192];
    *n = fread(out, 1, sizeof(out), f);
    fclose(f);
    uint8_t *copy = malloc(*n);
    memcpy(copy, out, *n);
    return copy;
}

static void test_user(void)
{
    char base[] = "/tmp/ogbox-patches-XXXXXX";
    assert(mkdtemp(base));
    char gamedata[600], patches[600], attach[600], sub[700];
    subdir(g_title, sizeof(g_title), base, "title");
    subdir(gamedata, sizeof(gamedata), g_title, "gamedata");
    snprintf(patches, sizeof(patches), "%s/patches", base);
    snprintf(attach, sizeof(attach), "%s/user-patches.txt", base);
    /* the database: one of the game's, and one with the id a file of the player's would have */
    write_text(gamedata, "patches.db",
               "P\t4F475801\t*\tpship\tWidescreen\tShipped\tOGBox\t\tOGBox\t\nT\tdefault.xbe\nO\t0\t00\n"
               "P\t4F475801\t*\tuser:4F475801 clash.ips\tMisc\tClash\tOGBox\t\tOGBox\t\nT\tdefault.xbe\nO\t0\t00\n");
    /* the disc's default.xbe: F00DFACE once, at 0x100 */
    g_xbe.size = g_xbe.cap = 0x1000;
    g_xbe.data = malloc(g_xbe.size);
    for (size_t i = 0; i < g_xbe.size; i++) {
        g_xbe.data[i] = (uint8_t)(i * 7 + 3);
    }
    memcpy(g_xbe.data + 0x100, "\xF0\x0D\xFA\xCE", 4);
    const uint32_t crc = ogbox_crc32(g_xbe.data, g_xbe.size);
    const char *key = "4F475801.1";
    static OgboxPatch list[64];
    static OgboxUserPatchFile files[64];

    /* no folder, then an empty one: the database's only */
    ogbox_patches_set_user_dir(patches, attach);
    ogbox_patches_set_game_name(key, "Test Game Deluxe");
    assert(ogbox_patches_catalog(key, list, 64) == 2);
    assert(ogbox_patches_user_files(key, files, 64) == 0);
    assert(mkdir(patches, 0777) == 0);
    assert(ogbox_patches_catalog(key, list, 64) == 2);
    assert(ogbox_patches_count(key) == 2);

    /* the files */
    char text[2048];
    snprintf(text, sizeof(text),
             "name=Widescreen\nauthor=Tester\nversion=1.0 {%08X}\nnotes=Wide.\n\n# default.xbe - widescreen\n"
             "0x10: AA BB\nF00DFACE\n0BADC0DE\n# [-] default.xbe - an alternative, off\n20: CCDD\nAPPEND: 0102\n"
             "# default.xbe - more\r\nAPPEND: 0304\r\n", crc);
    write_text(patches, "ws.jmp", text);
    write_text(patches, "other.jmp", "version=1.0 {DEADBEEF}\n0x10: 00\n");             /* another game's */
    subdir(sub, sizeof(sub), patches, "4F475801");
    write_text(sub, "mis.jmp", "version=4F475801 {DEADBEEF}\n0x10: 00\n");             /* another release */
    write_text(patches, "Test Game Deluxe broken.jmp", "author=X\n0x10: 00\ngarbage here\n");
    Bytes p;
    ips_two(&p, 0x200, "HU");
    write_file(patches, "4F475801 hud.ips", p.d, p.n);
    write_file(patches, "4F475801 clash.ips", p.d, p.n);
    write_file(patches, "4F475801..x.ips", p.d, p.n);
    write_file(patches, "Test Game Deluxe 2 ws.ips", p.d, p.n);   /* the sequel's: not by its name */
    ips_two(&p, 0x300, "LO");
    write_file(patches, "loose.ips", p.d, p.n);
    write_text(patches, "readme.txt", "not a patch");
    char deep[800];
    subdir(deep, sizeof(deep), sub, "deeper");
    write_file(deep, "4F475801 deep.ips", p.d, p.n);
    {
        uint8_t *big = calloc(1, (4u << 20) + 1);
        memcpy(big, "PATCH", 5);
        write_file(patches, "4F475801 big.ips", big, (4u << 20) + 1);
        free(big);
    }
    /* a BPS of this disc's default.xbe ("BP" at 0x400), and one of another's */
    Bytes bps = {0};
    bps_head(&bps, 0x1000, 0x1000);
    put_num(&bps, (0x400 - 1) << 2 | 0);
    put_num(&bps, (2 - 1) << 2 | 1);
    put(&bps, "BP", 2);
    put_num(&bps, (0x1000 - 0x402 - 1) << 2 | 0);
    OgboxBuf want_bps = {0};
    want_bps.data = malloc(0x1000);
    want_bps.size = want_bps.cap = 0x1000;
    memcpy(want_bps.data, g_xbe.data, 0x1000);
    memcpy(want_bps.data + 0x400, "BP", 2);
    put32(&bps, crc);
    put32(&bps, ogbox_crc32(want_bps.data, want_bps.size));
    put32(&bps, 0);
    write_file(patches, "good.bps", bps.d, bps.n);
    bps.d[bps.n - 12] ^= 1;
    write_file(patches, "wrong.bps", bps.d, bps.n);

    /* the release check, as the game page has it */
    NcDisc disc = {0};
    ogbox_patches_set_disc_crc(key, crc);
    ogbox_patches_check(&disc, key, &g_xbe);
    int n = ogbox_patches_catalog(key, list, 64);
    assert(n == 7);
    assert(!strcmp(list[0].id, "pship") && !strcmp(list[1].id, "user:4F475801 clash.ips"));
    const char *order[] = {"user:4F475801 hud.ips", "user:4F475801/mis.jmp", "user:good.bps",
                           "user:Test Game Deluxe broken.jmp", "user:ws.jmp"};
    for (int i = 0; i < 5; i++) {
        assert(!strcmp(list[2 + i].id, order[i]));
        assert(!list[2 + i].on);
    }
    const OgboxPatch *ws = find_patch(list, n, "user:ws.jmp");
    assert(ws->fit == OGBOX_FIT_YES && !strcmp(ws->name, "ws") && !strcmp(ws->author, "Tester") &&
           !strcmp(ws->category, "Your patches") && strstr(ws->note, "Wide.") && strstr(ws->note, "CRC-32"));
    const OgboxPatch *mis = find_patch(list, n, "user:4F475801/mis.jmp");
    assert(mis->fit == OGBOX_FIT_NO && strstr(mis->fit_why, "deadbeef") && strstr(mis->note, "title ID"));
    const OgboxPatch *broken = find_patch(list, n, "user:Test Game Deluxe broken.jmp");
    assert(broken->fit == OGBOX_FIT_NO && strstr(broken->fit_why, "line 3") && strstr(broken->note, "game's name"));
    const OgboxPatch *hud = find_patch(list, n, "user:4F475801 hud.ips");
    assert(hud->fit == OGBOX_FIT_UNCHECKED && strstr(hud->note, "release not checked"));
    assert(find_patch(list, n, "user:good.bps")->fit == OGBOX_FIT_YES);
    /* another version: no CRC-32 known for it; the title ID and name rules still hold */
    assert(ogbox_patches_catalog("4F475801.2", list, 64) == 5);

    /* those that say nothing of their game and no rule joins: offered to attach */
    assert(ogbox_patches_user_files(key, files, 64) == 2);
    assert(!strcmp(files[0].rel, "loose.ips") && !files[0].attached);
    assert(!strcmp(files[1].rel, "Test Game Deluxe 2 ws.ips"));
    assert(ogbox_patches_attach(key, "loose.ips", true));
    assert(ogbox_patches_user_files(key, files, 64) == 2 && files[0].attached);
    n = ogbox_patches_catalog(key, list, 64);
    assert(n == 8 && strstr(find_patch(list, n, "user:loose.ips")->note, "attached by hand"));

    /* on in mods.txt: applied at the start, the [-] records not, the one it can't read left out */
    write_text(g_title, "mods.txt", "4F475801.1 patch user:ws.jmp\n4F475801.1 patch user:4F475801 hud.ips\n"
                                    "4F475801.1 patch user:loose.ips\n4F475801.1 patch user:Test Game Deluxe broken.jmp\n");
    n = ogbox_patches_catalog(key, list, 64);
    assert(find_patch(list, n, "user:ws.jmp")->on && find_patch(list, n, "user:loose.ips")->on &&
           !find_patch(list, n, "user:good.bps")->on && !find_patch(list, n, "pship")->on);
    g_logn = 0;
    g_log[0] = 0;
    size_t got = 0;
    uint8_t *out = prepare(base, key, &got);
    assert(out && got == 0x1002);
    uint8_t want[0x1002];
    memcpy(want, g_xbe.data, 0x1000);
    memcpy(want + 0x10, "\xAA\xBB", 2);
    memcpy(want + 0x100, "\x0B\xAD\xC0\xDE", 4);
    memcpy(want + 0x200, "HU", 2);
    memcpy(want + 0x300, "LO", 2);
    memcpy(want + 0x1000, "\x03\x04", 2);
    assert(!memcmp(out, want, sizeof(want)));
    free(out);
    assert(logged("your ws.jmp: for this game: the CRC-32 of its version= line"));
    assert(logged("your loose.ips: for this game: attached by hand"));
    assert(logged("your 4F475801 hud.ips: for this game: the title ID in its name"));
    assert(logged("your good.bps: for this game: its source's CRC-32"));
    assert(logged("your 4F475801 clash.ips: for this game (the title ID in its name), left out: its id"));
    assert(logged("your 4F475801 big.ips: left out: 4194305 bytes"));
    assert(logged("your 4F475801..x.ips: left out: its name"));
    assert(logged("your 4F475801/deeper: not read"));
    assert(logged("Test Game Deluxe broken (user:Test Game Deluxe broken.jmp): left out: line 3"));
    assert(logged("ws (user:ws.jmp, Your patches by Tester): applied; yours, for this game: the CRC-32"));
    assert(logged("patches: 3 of 4 applied"));
    assert(!strstr(g_log, "other.jmp") && !strstr(g_log, "wrong.bps"));

    /* the BPS alone; default.xbe's CRC-32 read at the start (none known before) */
    write_text(g_title, "mods.txt", "4F475801.1 patch user:good.bps\n");
    ogbox_patches_set_disc_crc("00000000.0", 0);
    out = prepare(base, key, &got);
    assert(out && got == 0x1000 && !memcmp(out, want_bps.data, 0x1000));
    free(out);

    /* renamed: another file to OGBox (off, not attached), offered again */
    char from[800], to[800];
    snprintf(from, sizeof(from), "%s/loose.ips", patches);
    snprintf(to, sizeof(to), "%s/loose2.ips", patches);
    assert(rename(from, to) == 0);
    n = ogbox_patches_catalog(key, list, 64);
    assert(n == 7 && !find_patch(list, n, "user:loose.ips") && !find_patch(list, n, "user:loose2.ips"));
    assert(ogbox_patches_user_files(key, files, 64) == 2 && !strcmp(files[0].rel, "loose2.ips") && !files[0].attached);
    /* attached and detached */
    assert(ogbox_patches_attach(key, "loose2.ips", true));
    assert(ogbox_patches_catalog(key, list, 64) == 8);
    assert(ogbox_patches_attach(key, "loose2.ips", false));
    assert(ogbox_patches_catalog(key, list, 64) == 7);
    assert(ogbox_patches_user_files(key, files, 64) == 2 && !files[0].attached);

    /* all off: nothing written */
    write_text(g_title, "mods.txt", "# none\n");
    assert(!prepare(base, key, &got));

    /* no folder set: none of the player's */
    ogbox_patches_set_user_dir("", "");
    assert(ogbox_patches_catalog(key, list, 64) == 2);
    free(want_bps.data);
    free(g_xbe.data);
    char rm[700];
    snprintf(rm, sizeof(rm), "rm -rf '%s'", base);
    assert(system(rm) == 0);
    puts("patches: the player's own patches: joined, listed, checked, applied as they should be");
}

int main(int argc, char **argv)
{
    test_ips();
    test_bps_good();
    test_bps_hostile();
    test_records();
    test_user();
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
