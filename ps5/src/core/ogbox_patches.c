/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * XBE patches by title ID (ogbox_patches.h). The record kinds are those of Jay's Magic Patcher's
 * .JMP files (search and replace, offset, append; github.com/JayYardley/Xbox-Magic-Patches-by-Jay's
 * README), IPS and BPS (byuu's format description); the code is OGBox's own.
 */
#include "core/ogbox_patches.h"
#include "core/ogbox_mods.h"

#include "platform/ps5_bootlog.h"

#include "native/core/xdvdfs.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_PATCHES 256

/* ---- small helpers ---- */

static void say(char *why, size_t len, const char *fmt, const char *a, unsigned long long b)
{
    if (why && len) {
        snprintf(why, len, fmt, a, b);
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex digits into bytes (malloc'd); NULL when not an even number of hex digits */
static uint8_t *unhex(const char *s, size_t *n)
{
    const size_t len = strlen(s);
    if (len == 0 || len % 2) {
        return NULL;
    }
    uint8_t *out = malloc(len / 2);
    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < len / 2; i++) {
        const int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            free(out);
            return NULL;
        }
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    *n = len / 2;
    return out;
}

static bool buf_reserve(OgboxBuf *b, size_t size)
{
    if (size <= b->cap) {
        return true;
    }
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < size) {
        cap *= 2;
    }
    uint8_t *d = realloc(b->data, cap);
    if (!d) {
        return false;
    }
    b->data = d;
    b->cap = cap;
    return true;
}

static bool buf_resize(OgboxBuf *b, size_t size)
{
    if (!buf_reserve(b, size)) {
        return false;
    }
    if (size > b->size) {
        memset(b->data + b->size, 0, size - b->size);
    }
    b->size = size;
    return true;
}

static bool buf_copy(OgboxBuf *dst, const OgboxBuf *src)
{
    if (!buf_reserve(dst, src->size ? src->size : 1)) {
        return false;
    }
    if (src->size) {
        memcpy(dst->data, src->data, src->size);
    }
    dst->size = src->size;
    return true;
}

static uint8_t *read_host(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = len >= 0 && len < (64 << 20) ? malloc((size_t)len + 1) : NULL;
    if (p && fread(p, 1, (size_t)len, f) != (size_t)len) {
        free(p);
        p = NULL;
    }
    fclose(f);
    if (p) {
        *n = (size_t)len;
    }
    return p;
}

uint32_t ogbox_crc32(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static bool made;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* ---- IPS ---- */

bool ogbox_apply_ips(OgboxBuf *buf, const uint8_t *p, size_t n, char *why, size_t why_len)
{
    if (n < 8 || memcmp(p, "PATCH", 5) != 0) {
        say(why, why_len, "%s (%llu bytes)", "not an IPS file", (unsigned long long)n);
        return false;
    }
    OgboxBuf out = {0};
    if (!buf_copy(&out, buf)) {
        return false;
    }
    size_t at = 5;
    for (;;) {
        if (at + 3 > n) {
            free(out.data);
            say(why, why_len, "%s at %llu", "the IPS file ends early", (unsigned long long)at);
            return false;
        }
        if (!memcmp(p + at, "EOF", 3)) {
            at += 3;
            break;
        }
        if (at + 5 > n) {
            free(out.data);
            say(why, why_len, "%s at %llu", "the IPS file ends early", (unsigned long long)at);
            return false;
        }
        const size_t off = (size_t)p[at] << 16 | (size_t)p[at + 1] << 8 | p[at + 2];
        size_t len = (size_t)p[at + 3] << 8 | p[at + 4];
        at += 5;
        if (len) {
            if (at + len > n || !buf_reserve(&out, off + len)) {
                free(out.data);
                say(why, why_len, "%s at %llu", "a record past the IPS file's end", (unsigned long long)at);
                return false;
            }
            if (off + len > out.size && !buf_resize(&out, off + len)) {
                free(out.data);
                return false;
            }
            memcpy(out.data + off, p + at, len);
            at += len;
        } else {
            if (at + 3 > n) {
                free(out.data);
                say(why, why_len, "%s at %llu", "the IPS file ends early", (unsigned long long)at);
                return false;
            }
            len = (size_t)p[at] << 8 | p[at + 1];
            const uint8_t v = p[at + 2];
            at += 3;
            if (!buf_reserve(&out, off + len)) {
                free(out.data);
                return false;
            }
            if (off + len > out.size && !buf_resize(&out, off + len)) {
                free(out.data);
                return false;
            }
            memset(out.data + off, v, len);
        }
    }
    if (at + 3 <= n) {
        /* the truncation extension */
        const size_t size = (size_t)p[at] << 16 | (size_t)p[at + 1] << 8 | p[at + 2];
        if (size < out.size) {
            out.size = size;
        }
    }
    free(buf->data);
    *buf = out;
    return true;
}

/* ---- BPS ---- */

static bool bps_num(const uint8_t *p, size_t n, size_t *at, uint64_t *v)
{
    uint64_t data = 0, shift = 1;
    while (*at < n) {
        if (shift >> 56) {
            return false;   /* more than 64 bits: no patch has a number that large */
        }
        const uint8_t x = p[(*at)++];
        data += (x & 0x7F) * shift;
        if (x & 0x80) {
            *v = data;
            return true;
        }
        shift <<= 7;
        data += shift;
    }
    return false;
}

bool ogbox_apply_bps(OgboxBuf *buf, const uint8_t *p, size_t n, char *why, size_t why_len)
{
    if (n < 4 + 12 || memcmp(p, "BPS1", 4) != 0) {
        say(why, why_len, "%s (%llu bytes)", "not a BPS file", (unsigned long long)n);
        return false;
    }
    const uint32_t src_crc = (uint32_t)p[n - 12] | (uint32_t)p[n - 11] << 8 | (uint32_t)p[n - 10] << 16 |
                             (uint32_t)p[n - 9] << 24;
    const uint32_t dst_crc = (uint32_t)p[n - 8] | (uint32_t)p[n - 7] << 8 | (uint32_t)p[n - 6] << 16 |
                             (uint32_t)p[n - 5] << 24;
    const uint32_t have = ogbox_crc32(buf->data, buf->size);
    if (have != src_crc) {
        say(why, why_len, "%s %08llx", "the file is not the one the BPS patch was made for: its CRC-32 is",
            (unsigned long long)have);
        return false;
    }
    size_t at = 4;
    uint64_t src_size, dst_size, meta;
    if (!bps_num(p, n, &at, &src_size) || !bps_num(p, n, &at, &dst_size) || !bps_num(p, n, &at, &meta) ||
        src_size != buf->size || at > n - 12 || meta > n - 12 - at || dst_size > (256u << 20)) {
        say(why, why_len, "%s (%llu)", "a BPS header that doesn't fit the file", (unsigned long long)src_size);
        return false;
    }
    at += meta;
    OgboxBuf out = {0};
    if (!buf_resize(&out, (size_t)dst_size)) {
        return false;
    }
    size_t o = 0;
    int64_t src_rel = 0, dst_rel = 0;
    while (at < n - 12) {
        uint64_t d;
        if (!bps_num(p, n - 12, &at, &d)) {
            break;
        }
        const unsigned cmd = (unsigned)(d & 3);
        const uint64_t len64 = (d >> 2) + 1;
        if (len64 > out.size - o) {
            free(out.data);
            say(why, why_len, "%s at %llu", "a BPS action past the end", (unsigned long long)o);
            return false;
        }
        const size_t len = (size_t)len64;
        if (cmd == 0) {             /* SourceRead */
            if (o > buf->size || len > buf->size - o) {
                free(out.data);
                return false;
            }
            memcpy(out.data + o, buf->data + o, len);
        } else if (cmd == 1) {      /* TargetRead */
            if (len > n - 12 - at) {
                free(out.data);
                return false;
            }
            memcpy(out.data + o, p + at, len);
            at += len;
        } else {
            uint64_t raw;
            if (!bps_num(p, n - 12, &at, &raw)) {
                free(out.data);
                return false;
            }
            /* the offsets stay within the 256 MB output, so a larger step is broken, and turning
             * it down keeps the sums below from overflowing */
            if ((raw >> 1) > (256u << 20)) {
                free(out.data);
                return false;
            }
            const int64_t delta = (raw & 1 ? -1 : 1) * (int64_t)(raw >> 1);
            if (cmd == 2) {         /* SourceCopy */
                src_rel += delta;
                if (src_rel < 0 || (uint64_t)src_rel > buf->size || len > buf->size - (size_t)src_rel) {
                    free(out.data);
                    return false;
                }
                memcpy(out.data + o, buf->data + src_rel, len);
                src_rel += (int64_t)len;
            } else {                /* TargetCopy: byte by byte (it may overlap) */
                dst_rel += delta;
                /* from bytes already written: each byte read is then behind the one written */
                if (dst_rel < 0 || (uint64_t)dst_rel >= o) {
                    free(out.data);
                    return false;
                }
                for (size_t i = 0; i < len; i++) {
                    out.data[o + i] = out.data[dst_rel + (int64_t)i];
                }
                dst_rel += (int64_t)len;
            }
        }
        o += len;
    }
    if (o != out.size || ogbox_crc32(out.data, out.size) != dst_crc) {
        free(out.data);
        say(why, why_len, "%s (%llu)", "the BPS result's CRC-32 is wrong", (unsigned long long)o);
        return false;
    }
    free(buf->data);
    *buf = out;
    return true;
}

/* ---- the records ---- */

/* 0.1.47: the CRC-32 of the target as the disc has it, while the patches for a game are applied in
 * turn: a patch's CRC check is of the release it was made for, not of the file another patch already
 * changed (Manhunt's widescreen patch after its 720p one). 0: the buffer's own (the tests). */
static uint32_t g_original_crc;

bool ogbox_apply_records(OgboxBuf *buf, char *const *records, size_t n, const char *files_dir, char *why,
                         size_t why_len)
{
    OgboxBuf work = {0};
    if (!buf_copy(&work, buf)) {
        return false;
    }
    bool ok = true;
    for (size_t r = 0; r < n && ok; r++) {
        char line[8192];
        snprintf(line, sizeof(line), "%s", records[r]);
        char *f[4] = {0};
        unsigned nf = 0;
        for (char *save, *t = strtok_r(line, "\t", &save); t && nf < 4; t = strtok_r(NULL, "\t", &save)) {
            f[nf++] = t;
        }
        if (!nf) {
            continue;
        }
        const char kind = f[0][0];
        if (kind == 'C' && nf >= 2) {
            const uint32_t want = (uint32_t)strtoul(f[1], NULL, 16),
                           have = g_original_crc ? g_original_crc : ogbox_crc32(work.data, work.size);
            if (want != have) {
                if (why && why_len) {
                    snprintf(why, why_len, "it was made for the file of CRC-32 %08x, this disc's is %08x", want, have);
                }
                ok = false;
            }
        } else if (kind == 'F' && nf >= 3) {
            size_t fn = 0, rn = 0;
            uint8_t *find = unhex(f[1], &fn), *repl = unhex(f[2], &rn);
            ok = find && repl && fn == rn && fn <= work.size;
            if (ok) {
                ok = false;
                for (size_t i = 0; i + fn <= work.size; i++) {
                    if (work.data[i] == find[0] && !memcmp(work.data + i, find, fn)) {
                        memcpy(work.data + i, repl, rn);
                        ok = true;
                        break;
                    }
                }
                if (!ok && why && why_len) {
                    snprintf(why, why_len, "bytes it looks for are not in the file (%.24s%s)", f[1],
                             strlen(f[1]) > 24 ? "..." : "");
                }
            }
            free(find);
            free(repl);
        } else if (kind == 'O' && nf >= 3) {
            size_t dn = 0;
            const unsigned long long off = strtoull(f[1], NULL, 16);
            uint8_t *data = unhex(f[2], &dn);
            ok = data && off <= work.size && dn <= work.size - off;
            if (ok) {
                memcpy(work.data + off, data, dn);
            } else if (why && why_len) {
                snprintf(why, why_len, "an offset past the file's end (0x%llx, the file %zu bytes)", off, work.size);
            }
            free(data);
        } else if (kind == 'A' && nf >= 2) {
            size_t dn = 0;
            uint8_t *data = unhex(f[1], &dn);
            const size_t at = work.size;
            ok = data && buf_resize(&work, at + dn);
            if (ok) {
                memcpy(work.data + at, data, dn);
            }
            free(data);
        } else if ((kind == 'I' || kind == 'B') && nf >= 2) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", files_dir, f[1]);
            size_t pn = 0;
            uint8_t *p = read_host(path, &pn);
            if (!p) {
                if (why && why_len) {
                    snprintf(why, why_len, "%s cannot be read", path);
                }
                ok = false;
            } else {
                ok = kind == 'I' ? ogbox_apply_ips(&work, p, pn, why, why_len)
                                 : ogbox_apply_bps(&work, p, pn, why, why_len);
                free(p);
            }
        }
    }
    if (ok) {
        free(buf->data);
        *buf = work;
    } else {
        free(work.data);
    }
    return ok;
}

/* ---- the database ---- */

typedef struct Entry {
    OgboxPatch p;
    char **lines;   /* its records, T lines among them */
    size_t nlines;
} Entry;

static Entry g_db[MAX_PATCHES];
static unsigned g_ndb;

static void db_reset(void)
{
    for (unsigned i = 0; i < g_ndb; i++) {
        for (size_t k = 0; k < g_db[i].nlines; k++) {
            free(g_db[i].lines[k]);
        }
        free(g_db[i].lines);
    }
    memset(g_db, 0, sizeof(g_db));
    g_ndb = 0;
}

static void copyf(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

static void db_dir(char *out, size_t n)
{
    snprintf(out, n, "%s/gamedata", pshakux_title_dir());
}

/* The entries for key's title ID and version (or any version). */
static void db_load(const char *key)
{
    db_reset();
    char tid[9] = "";
    unsigned version = 0;
    if (!key || sscanf(key, "%8[0-9A-Fa-f].%u", tid, &version) < 1) {
        return;
    }
    char path[1024];
    db_dir(path, sizeof(path));
    strncat(path, "/patches.db", sizeof(path) - strlen(path) - 1);
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char vtext[16];
    snprintf(vtext, sizeof(vtext), "%u", version);
    static char line[65536];
    Entry *cur = NULL;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = 0;
        }
        if (!len || line[0] == '#') {
            continue;
        }
        if (line[0] == 'P' && line[1] == '\t') {
            cur = NULL;
            char *fl[11] = {0};
            unsigned nf = 0;
            char *s = line;
            while (nf < 11) {
                fl[nf++] = s;
                char *tab = strchr(s, '\t');
                if (!tab) {
                    break;
                }
                *tab = 0;
                s = tab + 1;
            }
            if (nf < 10 || strcasecmp(fl[1], tid) != 0 || (strcmp(fl[2], "*") != 0 && strcmp(fl[2], vtext) != 0) ||
                g_ndb >= MAX_PATCHES) {
                continue;
            }
            cur = &g_db[g_ndb++];
            copyf(cur->p.id, sizeof(cur->p.id), fl[3]);
            copyf(cur->p.category, sizeof(cur->p.category), fl[4]);
            copyf(cur->p.name, sizeof(cur->p.name), fl[5]);
            copyf(cur->p.author, sizeof(cur->p.author), fl[6]);
            copyf(cur->p.note, sizeof(cur->p.note), fl[7]);
            copyf(cur->p.source, sizeof(cur->p.source), fl[8]);
            copyf(cur->p.region, sizeof(cur->p.region), nf > 9 ? fl[9] : "");
            continue;
        }
        if (!cur) {
            continue;
        }
        char **grown = realloc(cur->lines, (cur->nlines + 1) * sizeof(char *));
        if (!grown) {
            continue;
        }
        cur->lines = grown;
        cur->lines[cur->nlines++] = strdup(line);
    }
    fclose(f);
}

/* mods.txt's "<key> patch <id>" lines: the entries on */
static void read_on(const char *key)
{
    const char *paths[2] = {"/data/ogbox/mods.txt", NULL};
    char second[1024];
    snprintf(second, sizeof(second), "%s/mods.txt", pshakux_title_dir());
    paths[1] = second;
    for (int p = 0; p < 2; p++) {
        FILE *f = fopen(paths[p], "r");
        if (!f) {
            continue;
        }
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            char k[64], kind[16], id[64];
            if (line[0] == '#' || sscanf(line, "%63s %15s %63s", k, kind, id) != 3) {
                continue;
            }
            if (strcasecmp(k, key) || strcasecmp(kind, "patch")) {
                continue;
            }
            for (unsigned i = 0; i < g_ndb; i++) {
                if (!strcasecmp(g_db[i].p.id, id)) {
                    g_db[i].p.on = true;
                }
            }
        }
        fclose(f);
        return;     /* the first file there is the one (as ogbox_mods.c reads it) */
    }
}

static bool has_word(const char *s, const char *w)
{
    const size_t n = strlen(w);
    for (; *s; s++) {
        if (!strncasecmp(s, w, n)) {
            return true;
        }
    }
    return false;
}

bool ogbox_patches_need_128(const char *key)
{
    static OgboxPatch list[64];
    const int n = ogbox_patches_catalog(key, list, 64);
    for (int i = 0; i < n; i++) {
        const OgboxPatch *p = &list[i];
        if (!p->on) {
            continue;
        }
        /* 0.1.51: only a patch whose notes say so (0.1.47 took every 720p patch for one: Manhunt's
         * says only "720p", and with 128 MB its D3D's push buffer ran past its own limit and the game
         * hung at its menu). 0.1.52: the words that say it needs it, not any "128" ("probably not
         * much better on 128MB") */
        static const char *const k_needs[] = {"needs 128 MB", "128MB required", "128 MB required", "128MB only",
                                              "128 MB only", "128MB mandatory", "128 MB mandatory"};
        bool needs = false;
        for (size_t w = 0; w < sizeof(k_needs) / sizeof(k_needs[0]); w++) {
            needs |= has_word(p->note, k_needs[w]) || has_word(p->name, k_needs[w]);
        }
        if (needs) {
            pshakux_log("patches: %s: %s (%s) needs the 128 MB RAM upgrade, its notes say: the game gets 128 MB "
                        "(Settings > Console > Memory: Auto)", key, p->name, p->id);
            return true;
        }
    }
    return false;
}

static char g_crc_key[32];
static uint32_t g_crc;

void ogbox_patches_set_disc_crc(const char *key, uint32_t crc)
{
    snprintf(g_crc_key, sizeof(g_crc_key), "%s", key ? key : "");
    g_crc = crc;
}

/* An entry's release check (its C line, before its first target), 0 when it has none. */
static uint32_t entry_crc(const Entry *e)
{
    for (unsigned k = 0; k < e->nlines; k++) {
        if (e->lines[k][0] == 'T' && e->lines[k][1] == '\t') {
            break;
        }
        if (e->lines[k][0] == 'C' && e->lines[k][1] == '\t') {
            return (uint32_t)strtoul(e->lines[k] + 2, NULL, 16);
        }
    }
    return 0;
}

/* ---- 0.0.2: the release check (ogbox_patches_check) ---- */

static struct {
    char key[32];
    unsigned n;
    struct {
        char id[16];
        int fit;
        char why[192];
    } e[MAX_PATCHES];
} g_fit;

static const char *fit_of(const char *key, const char *id, int *fit)
{
    *fit = OGBOX_FIT_UNKNOWN;
    if (!key || strcasecmp(key, g_fit.key) != 0) {
        return "";
    }
    for (unsigned i = 0; i < g_fit.n; i++) {
        if (!strcasecmp(g_fit.e[i].id, id)) {
            *fit = g_fit.e[i].fit;
            return g_fit.e[i].why;
        }
    }
    return "";
}

int ogbox_patches_catalog(const char *key, OgboxPatch *out, int max)
{
    db_load(key);
    read_on(key);
    const uint32_t disc = key && g_crc && !strcasecmp(key, g_crc_key) ? g_crc : 0;
    int n = 0;
    for (unsigned i = 0; i < g_ndb && n < max; i++) {
        /* 0.0.2: one made for another release of the game (its CRC-32 another: 0.1.52 left it out of
         * the list) is listed, greyed out, with the others the release check found (the screen
         * doesn't let it be turned on) */
        const uint32_t crc = disc ? entry_crc(&g_db[i]) : 0;
        out[n] = g_db[i].p;
        copyf(out[n].fit_why, sizeof(out[n].fit_why), fit_of(key, g_db[i].p.id, &out[n].fit));
        if (crc && crc != disc && out[n].fit != OGBOX_FIT_NO) {
            out[n].fit = OGBOX_FIT_NO;
            snprintf(out[n].fit_why, sizeof(out[n].fit_why), "it was made for the default.xbe of CRC-32 %08x, this "
                     "disc's is %08x", crc, disc);
        }
        n++;
    }
    db_reset();
    return n;
}

int ogbox_patches_count(const char *key)
{
    db_load(key);
    const int n = (int)g_ndb;
    db_reset();
    return n;
}

/* ---- the disc's files ---- */

typedef struct Target {
    char name[256];     /* as the database names it */
    char path[512];     /* on the disc */
    OgboxBuf buf;
    bool changed;
    uint32_t crc;       /* 0.1.47: the disc's file's, before any patch */
} Target;

/* A file anywhere on the disc by its name (case aside), depth first: its path. */
static bool find_named(const NcDisc *disc, const NcDiscFile *dir, const char *dir_path, const char *name, int depth,
                       char *out, size_t n)
{
    if (depth > 6 || dir->size == 0 || dir->size > (4u << 20)) {
        return false;
    }
    uint8_t *d = malloc(dir->size);
    if (!d || !nc_disc_read(disc, dir, 0, d, dir->size)) {
        free(d);
        return false;
    }
    bool found = false;
    /* the entries, 4-byte aligned; 0xFF pads a sector's rest */
    for (uint32_t at = 0; at + 14 <= dir->size && !found;) {
        if (d[at] == 0xFF && d[at + 1] == 0xFF) {
            at = (at / 2048 + 1) * 2048;
            continue;
        }
        const uint32_t sector = (uint32_t)d[at + 4] | (uint32_t)d[at + 5] << 8 | (uint32_t)d[at + 6] << 16 |
                                (uint32_t)d[at + 7] << 24;
        const uint32_t size = (uint32_t)d[at + 8] | (uint32_t)d[at + 9] << 8 | (uint32_t)d[at + 10] << 16 |
                              (uint32_t)d[at + 11] << 24;
        const uint8_t attr = d[at + 12], nl = d[at + 13];
        if (!nl || at + 14 + nl > dir->size) {
            break;
        }
        char ename[256];
        memcpy(ename, d + at + 14, nl);
        ename[nl] = 0;
        char epath[512];
        snprintf(epath, sizeof(epath), "%s\\%s", dir_path, ename);
        if (attr & 0x10) {
            const NcDiscFile sub = {.sector = sector, .size = size, .attr = attr};
            found = find_named(disc, &sub, epath, name, depth + 1, out, n);
        } else if (!strcasecmp(ename, name)) {
            snprintf(out, n, "%s", epath);
            found = true;
        }
        at = (at + 14 + nl + 3) & ~3u;
    }
    free(d);
    return found;
}

static bool target_load(const NcDisc *disc, Target *t)
{
    NcDiscFile f;
    char path[512];
    if (strchr(t->name, '\\') || strchr(t->name, '/')) {
        snprintf(path, sizeof(path), "%s%s", (t->name[0] == '\\' || t->name[0] == '/') ? "" : "\\", t->name);
        for (char *p = path; *p; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
    } else {
        snprintf(path, sizeof(path), "\\%s", t->name);
    }
    if (!nc_disc_find_orig(disc, path, &f)) {
        const NcDiscFile root = {.sector = disc->root_sector, .size = disc->root_size, .attr = 0x10};
        if (!find_named(disc, &root, "", t->name, 0, path, sizeof(path)) || !nc_disc_find_orig(disc, path, &f)) {
            return false;
        }
    }
    if (f.attr & 0x10 || f.size > (64u << 20) || !buf_resize(&t->buf, f.size)) {
        return false;
    }
    for (uint32_t at = 0; at < f.size;) {
        const uint32_t chunk = f.size - at < (1u << 20) ? f.size - at : (1u << 20);
        if (!nc_disc_read_orig(disc, &f, at, t->buf.data + at, chunk)) {
            return false;
        }
        at += chunk;
    }
    snprintf(t->path, sizeof(t->path), "%s", path);
    return true;
}

/* default.xbe's certificate's game regions (bit 0 North America), 0 if unread */
static uint32_t xbe_regions(const OgboxBuf *x)
{
    if (!x || !x->data || x->size < 0x1000) {
        return 0;
    }
    const uint8_t *h = x->data;
    const uint32_t base = (uint32_t)h[0x104] | (uint32_t)h[0x105] << 8 | (uint32_t)h[0x106] << 16 |
                          (uint32_t)h[0x107] << 24;
    const uint32_t cert = ((uint32_t)h[0x118] | (uint32_t)h[0x119] << 8 | (uint32_t)h[0x11A] << 16 |
                           (uint32_t)h[0x11B] << 24) - base;
    return cert + 0xA4 <= x->size ? ((uint32_t)h[cert + 0xA0] | (uint32_t)h[cert + 0xA1] << 8) : 0;
}

static bool is_default_xbe(const char *name)
{
    while (*name == '\\' || *name == '/') {
        name++;
    }
    return !strcasecmp(name, "default.xbe");
}

/* A file the patches change other than default.xbe, as the disc has it (t->name): 1 read (its
 * CRC-32 in t->crc), 0 left unchecked (over 16 MiB: reading a level file of a hundred would hold the
 * screen up; or unread), -1 not on the disc */
static int other_load(const NcDisc *disc, Target *t)
{
    char path[512];
    NcDiscFile f;
    snprintf(path, sizeof(path), "%s", t->name);
    if (!nc_disc_find_orig(disc, path, &f)) {
        const NcDiscFile root = {.sector = disc->root_sector, .size = disc->root_size, .attr = 0x10};
        if (!find_named(disc, &root, "", t->name, 0, path, sizeof(path)) || !nc_disc_find_orig(disc, path, &f)) {
            return -1;
        }
    }
    if ((f.attr & 0x10) || f.size > (16u << 20) || !target_load(disc, t)) {
        free(t->buf.data);
        memset(&t->buf, 0, sizeof(t->buf));
        return 0;
    }
    t->crc = ogbox_crc32(t->buf.data, t->buf.size);
    return 1;
}

int ogbox_patches_check(const NcDisc *disc, const char *key, const OgboxBuf *xbe)
{
    db_load(key);
    memset(&g_fit, 0, sizeof(g_fit));
    copyf(g_fit.key, sizeof(g_fit.key), key);
    char files_dir[1024];
    db_dir(files_dir, sizeof(files_dir));
    strncat(files_dir, "/files", sizeof(files_dir) - strlen(files_dir) - 1);
    const uint32_t regions = xbe_regions(xbe);
    const uint32_t xbe_crc = xbe && xbe->data ? ogbox_crc32(xbe->data, xbe->size) : 0;
    /* the other files the patches change, as the disc has them (other_load) */
    Target others[4];
    int other_state[4] = {0};
    unsigned nothers = 0;
    memset(others, 0, sizeof(others));
    unsigned bad = 0;
    for (unsigned i = 0; i < g_ndb && g_fit.n < MAX_PATCHES; i++) {
        const Entry *e = &g_db[i];
        int fit = OGBOX_FIT_YES;
        char why[192] = "";
        if (!strcasecmp(e->p.region, "USA") && regions && !(regions & 1)) {
            fit = OGBOX_FIT_NO;
            snprintf(why, sizeof(why), "made for the USA release, and this disc is another (its regions %x)", regions);
        }
        const char *tname = "default.xbe";
        size_t start = 0;
        for (size_t k = 0; k <= e->nlines && fit == OGBOX_FIT_YES; k++) {
            const bool end = k == e->nlines || (e->lines[k][0] == 'T' && e->lines[k][1] == '\t');
            if (!end) {
                continue;
            }
            if (k > start) {
                const OgboxBuf *src = NULL;
                uint32_t crc = 0;
                if (is_default_xbe(tname)) {
                    src = xbe && xbe->data ? xbe : NULL;
                    crc = xbe_crc;
                    if (!src) {
                        fit = OGBOX_FIT_UNKNOWN;
                    }
                } else {
                    int j = 0;
                    while (j < (int)nothers && strcasecmp(others[j].name, tname) != 0) {
                        j++;
                    }
                    if (j == (int)nothers && nothers < 4 && disc) {
                        snprintf(others[j].name, sizeof(others[j].name), "%s", tname);
                        other_state[j] = other_load(disc, &others[j]);
                        nothers++;
                    }
                    if (j < (int)nothers && other_state[j] > 0) {
                        src = &others[j].buf;
                        crc = others[j].crc;
                    } else if (j < (int)nothers && other_state[j] < 0) {
                        fit = OGBOX_FIT_NO;
                        snprintf(why, sizeof(why), "%s is not on this disc", tname);
                    } else {
                        fit = OGBOX_FIT_UNKNOWN;
                    }
                }
                if (src) {
                    OgboxBuf work = {0};
                    if (!buf_copy(&work, src)) {
                        fit = OGBOX_FIT_UNKNOWN;
                    } else {
                        g_original_crc = crc;
                        if (!ogbox_apply_records(&work, e->lines + start, k - start, files_dir, why, sizeof(why))) {
                            fit = OGBOX_FIT_NO;
                        }
                        g_original_crc = 0;
                        free(work.data);
                    }
                }
            }
            if (k < e->nlines) {
                tname = e->lines[k] + 2;
                start = k + 1;
            }
        }
        copyf(g_fit.e[g_fit.n].id, sizeof(g_fit.e[g_fit.n].id), e->p.id);
        g_fit.e[g_fit.n].fit = fit;
        copyf(g_fit.e[g_fit.n].why, sizeof(g_fit.e[g_fit.n].why), fit == OGBOX_FIT_NO ? why : "");
        g_fit.n++;
        bad += fit == OGBOX_FIT_NO;
    }
    for (unsigned j = 0; j < nothers; j++) {
        free(others[j].buf.data);
    }
    const int n = (int)g_ndb;
    if (n) {
        pshakux_log("patches: %s: %d in gamedata/patches.db, %u of them for another release of the game", key, n, bad);
    }
    db_reset();
    return n;
}

int ogbox_patches_prepare(const NcDisc *disc, const char *key, const char *work_dir, FILE *manifest)
{
    db_load(key);
    read_on(key);
    unsigned on = 0;
    for (unsigned i = 0; i < g_ndb; i++) {
        on += g_db[i].p.on;
    }
    pshakux_log("patches: %s: %u in gamedata/patches.db, %u on", key, g_ndb, on);
    if (!on || !disc) {
        db_reset();
        return 0;
    }
    char files_dir[1024];
    db_dir(files_dir, sizeof(files_dir));
    strncat(files_dir, "/files", sizeof(files_dir) - strlen(files_dir) - 1);

    /* the disc's region (default.xbe's certificate: bit 0 North America) for a patch made for one */
    uint32_t regions = 0;
    {
        Target x = {0};
        snprintf(x.name, sizeof(x.name), "default.xbe");
        if (target_load(disc, &x) && x.buf.size >= 0x1000) {
            const uint8_t *h = x.buf.data;
            const uint32_t base = (uint32_t)h[0x104] | (uint32_t)h[0x105] << 8 | (uint32_t)h[0x106] << 16 |
                                  (uint32_t)h[0x107] << 24;
            const uint32_t cert = ((uint32_t)h[0x118] | (uint32_t)h[0x119] << 8 | (uint32_t)h[0x11A] << 16 |
                                   (uint32_t)h[0x11B] << 24) - base;
            if (cert + 0xA4 <= x.buf.size) {
                regions = (uint32_t)h[cert + 0xA0] | (uint32_t)h[cert + 0xA1] << 8;
            }
        }
        free(x.buf.data);
    }

    Target targets[8];
    unsigned ntargets = 0;
    memset(targets, 0, sizeof(targets));
    unsigned applied = 0;
    for (unsigned i = 0; i < g_ndb; i++) {
        Entry *e = &g_db[i];
        if (!e->p.on) {
            continue;
        }
        if (!strcasecmp(e->p.region, "USA") && regions && !(regions & 1)) {
            pshakux_log("patches:   %s (%s): left out: made for the USA release, this disc is not one (regions %x)",
                        e->p.name, e->p.id, regions);
            continue;
        }
        /* its records by target, each applied whole or not at all */
        bool ok = true;
        char why[256] = "";
        const char *tname = "default.xbe";
        size_t start = 0;
        Target *undo[8];
        OgboxBuf saved[8];
        unsigned nundo = 0;
        for (size_t k = 0; k <= e->nlines && ok; k++) {
            const bool end = k == e->nlines || (e->lines[k][0] == 'T' && e->lines[k][1] == '\t');
            if (!end) {
                continue;
            }
            if (k > start) {
                Target *t = NULL;
                for (unsigned j = 0; j < ntargets; j++) {
                    if (!strcasecmp(targets[j].name, tname)) {
                        t = &targets[j];
                    }
                }
                if (!t && ntargets < 8) {
                    t = &targets[ntargets];
                    snprintf(t->name, sizeof(t->name), "%s", tname);
                    if (target_load(disc, t)) {
                        t->crc = ogbox_crc32(t->buf.data, t->buf.size);
                        ntargets++;
                    } else {
                        free(t->buf.data);
                        memset(t, 0, sizeof(*t));
                        snprintf(why, sizeof(why), "%s is not on the disc", tname);
                        t = NULL;
                    }
                }
                if (!t) {
                    ok = false;
                    break;
                }
                OgboxBuf before = {0};
                buf_copy(&before, &t->buf);
                g_original_crc = t->crc;
                const bool done = ogbox_apply_records(&t->buf, e->lines + start, k - start, files_dir, why, sizeof(why));
                g_original_crc = 0;
                if (done) {
                    undo[nundo] = t;
                    saved[nundo++] = before;
                } else {
                    free(before.data);
                    ok = false;
                }
            }
            if (k < e->nlines) {
                tname = e->lines[k] + 2;
                start = k + 1;
            }
        }
        if (ok) {
            for (unsigned u = 0; u < nundo; u++) {
                undo[u]->changed = true;
                free(saved[u].data);
            }
            applied++;
            pshakux_log("patches:   %s (%s, %s by %s): applied", e->p.name, e->p.id, e->p.category, e->p.author);
        } else {
            /* the targets as they were before this patch */
            for (unsigned u = nundo; u-- > 0;) {
                free(undo[u]->buf.data);
                undo[u]->buf = saved[u];
            }
            pshakux_log("patches:   %s (%s): left out: %s", e->p.name, e->p.id, why[0] ? why : "?");
        }
    }
    int files = 0;
    for (unsigned j = 0; j < ntargets; j++) {
        Target *t = &targets[j];
        if (t->changed) {
            char host[1024];
            snprintf(host, sizeof(host), "%s/patched-%u-", work_dir, j);
            size_t hl = strlen(host);
            for (const char *p = t->name; *p && hl + 1 < sizeof(host); p++) {
                host[hl++] = (isalnum((unsigned char)*p) || *p == '.' || *p == '-') ? *p : '_';
            }
            host[hl] = 0;
            FILE *out = fopen(host, "wb");
            bool written = out && fwrite(t->buf.data, 1, t->buf.size, out) == t->buf.size;
            if (out && fclose(out) != 0) {
                written = false;
            }
            if (written) {
                fprintf(manifest, "file\t%s\t%s\n", t->path, host);
                files++;
                pshakux_log("patches: %s patched (%zu bytes) -> %s", t->path, t->buf.size, host);
            } else {
                pshakux_log("patches: %s cannot be written (%s): the game starts unpatched", host, strerror(errno));
            }
        }
        free(t->buf.data);
    }
    pshakux_log("patches: %u of %u applied", applied, on);
    db_reset();
    return files;
}
