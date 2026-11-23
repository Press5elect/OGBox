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
#include <dirent.h>
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

/* A record's hex number, the whole field (strtoull alone took "zz" for 0) */
static bool hexnum(const char *s, unsigned long long *v)
{
    char *end;
    errno = 0;
    *v = strtoull(s, &end, 16);
    return isxdigit((unsigned char)s[0]) && end != s && !*end && errno == 0;
}

static void malformed(char *why, size_t why_len, const char *record)
{
    if (why && why_len) {
        snprintf(why, why_len, "a record it cannot read (%.32s%s)", record, strlen(record) > 32 ? "..." : "");
    }
}

/* An IPS or BPS file's name in the database: relative, without a ".." part */
static bool inside_name(const char *name)
{
    if (!name[0] || name[0] == '/') {
        return false;
    }
    for (const char *p = name; *p;) {
        const size_t n = strcspn(p, "/");
        if (n == 2 && p[0] == '.' && p[1] == '.') {
            return false;
        }
        p += n;
        p += *p == '/';
    }
    return true;
}

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
        /* 1.0.3: a record that doesn't fit is refused (cut, an offset's or an append's bytes were
         * applied in part) */
        if ((size_t)snprintf(line, sizeof(line), "%s", records[r]) >= sizeof(line)) {
            if (why && why_len) {
                snprintf(why, why_len, "a record longer than %zu characters (%.24s...)", sizeof(line) - 1, records[r]);
            }
            ok = false;
            break;
        }
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
            /* 1.0.2: a C line may name several releases (up to three CRC-32s): any of them */
            const uint32_t have = g_original_crc ? g_original_crc : ogbox_crc32(work.data, work.size);
            uint32_t want = 0;
            bool any = false;
            for (unsigned c = 1; c < nf && ok; c++) {
                unsigned long long v;
                ok = hexnum(f[c], &v) && v <= 0xFFFFFFFFu;
                want = c == 1 ? (uint32_t)v : want;
                any = any || (uint32_t)v == have;
            }
            if (!ok) {
                malformed(why, why_len, records[r]);
            } else if (!any) {
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
        } else if (kind == 'N' && nf >= 3) {
            /* 1.0.4: the bytes are in the file exactly this many times (hex), as it is here: a
             * pattern patch for any release (OgXHD's database) is refused where its F records would
             * write somewhere else than the places it was made for */
            size_t fn = 0;
            unsigned long long want = 0;
            uint8_t *find = unhex(f[1], &fn);
            if (!find || !fn || !hexnum(f[2], &want)) {
                malformed(why, why_len, records[r]);
                ok = false;
            } else {
                unsigned long long have = 0;
                for (size_t i = 0; i + fn <= work.size; i++) {
                    have += work.data[i] == find[0] && !memcmp(work.data + i, find, fn);
                }
                ok = have == want;
                if (!ok && why && why_len) {
                    snprintf(why, why_len, "bytes it looks for are in the file %llu times, not %llu (%.24s%s)", have,
                             want, f[1], strlen(f[1]) > 24 ? "..." : "");
                }
            }
            free(find);
        } else if (kind == 'O' && nf >= 3) {
            size_t dn = 0;
            unsigned long long off = 0;
            if (!hexnum(f[1], &off)) {
                malformed(why, why_len, records[r]);
                ok = false;
                continue;
            }
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
            /* 1.0.3: a file of files_dir only (not "/..." or "../...") */
            if (!inside_name(f[1])) {
                if (why && why_len) {
                    snprintf(why, why_len, "%.64s is not a file of the patches' folder", f[1]);
                }
                ok = false;
                continue;
            }
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
        } else {
            /* 1.0.3: a record of a kind it doesn't know, or short of its fields: the patch is not
             * applied (it was skipped, and the patch counted as applied without it) */
            malformed(why, why_len, records[r]);
            ok = false;
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
    /* 1.0.4: one of the player's (ogbox_patches.h): the folder its IPS or BPS file is in (NULL: the
     * database's files/), how it was joined to the game, its kind ('J', 'I', 'B'), and why it can't
     * be applied (a .jmp it cannot read: the line at fault) */
    char *files_dir;
    const char *how;
    char kind;
    char bad[192];
} Entry;

static Entry g_db[MAX_PATCHES];
static unsigned g_ndb;
static unsigned g_nuser;    /* 1.0.4: of them the player's (the last) */

static void user_load(const char *key, const char *tid);

static void db_reset(void)
{
    for (unsigned i = 0; i < g_ndb; i++) {
        for (size_t k = 0; k < g_db[i].nlines; k++) {
            free(g_db[i].lines[k]);
        }
        free(g_db[i].lines);
        free(g_db[i].files_dir);
    }
    memset(g_db, 0, sizeof(g_db));
    g_ndb = 0;
    g_nuser = 0;
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

/* db_load and then the player's patches for the title ID (1.0.4) */
static void db_load_all(const char *key)
{
    db_load(key);
    char tid[9] = "";
    if (key && sscanf(key, "%8[0-9A-Fa-f]", tid) == 1 && strlen(tid) == 8) {
        user_load(key, tid);
    }
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
            if (errno == ENOENT) {
                continue;
            }
            return;     /* (1.0.3: there and unreadable: not the other's, as ogbox_mods.c reads it) */
        }
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            char k[64], kind[16];
            int at = 0;
            if (line[0] == '#' || sscanf(line, "%63s %15s %n", k, kind, &at) != 2 || !at) {
                continue;
            }
            /* 1.0.4: the rest of the line (the player's patches' ids may hold spaces), as
             * ogbox_mods.c reads it */
            char *id = line + at;
            size_t il = strlen(id);
            while (il && isspace((unsigned char)id[il - 1])) {
                id[--il] = 0;
            }
            if (!il) {
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
static uint32_t g_size;     /* 1.0.4: default.xbe's size for g_crc_key, 0 unknown (a BPS's source size) */

void ogbox_patches_set_disc_crc(const char *key, uint32_t crc)
{
    if (!key || strcasecmp(key, g_crc_key) != 0 || crc != g_crc) {
        g_size = 0;
    }
    snprintf(g_crc_key, sizeof(g_crc_key), "%s", key ? key : "");
    g_crc = crc;
}

/* 1.0.4: the disc's default.xbe as read (ogbox_patches_check, ogbox_patches_prepare): its CRC-32 and size */
static void set_disc_xbe(const char *key, const OgboxBuf *xbe)
{
    if (key && xbe && xbe->data && xbe->size) {
        ogbox_patches_set_disc_crc(key, ogbox_crc32(xbe->data, xbe->size));
        g_size = (uint32_t)xbe->size;
    }
}

/* ---- 1.0.4: the player's own patches (ogbox_patches.h) ---- */

#define USER_FILE_MAX (4u << 20)    /* a file's size at most */
#define USER_GAME_MAX 64            /* a game's at most */
#define USER_SCAN_MAX 512           /* the folder's files read at most */

static char g_user_dir[1024];
static char g_attach_file[1024];
static struct {
    char tid[9];
    char name[160];
} g_names[16];
static unsigned g_names_next;
static bool g_user_log;     /* at the start (ogbox_patches_prepare): the boot log says what was found */
static const char k_by_hand[] = "attached by hand";

typedef struct UserFile {
    char rel[128];      /* in the folder: "name" or "folder/name" */
    char dir[1024];     /* the folder it is in */
    char name[128];
    char kind;          /* 'J', 'I', 'B' */
} UserFile;

static UserFile g_files[USER_SCAN_MAX];
static unsigned g_nfiles;

void ogbox_patches_set_user_dir(const char *dir, const char *attach_file)
{
    copyf(g_user_dir, sizeof(g_user_dir), dir);
    copyf(g_attach_file, sizeof(g_attach_file), attach_file);
}

static bool tid_of(const char *key, char tid[9])
{
    tid[0] = 0;
    return key && sscanf(key, "%8[0-9A-Fa-f]", tid) == 1 && strlen(tid) == 8;
}

void ogbox_patches_set_game_name(const char *key, const char *name)
{
    char tid[9];
    if (!tid_of(key, tid)) {
        return;
    }
    unsigned i = 0;
    while (i < 16 && strcasecmp(g_names[i].tid, tid) != 0) {
        i++;
    }
    if (i == 16) {
        i = g_names_next++ % 16;
    }
    copyf(g_names[i].tid, sizeof(g_names[i].tid), tid);
    copyf(g_names[i].name, sizeof(g_names[i].name), name);
}

static const char *game_name(const char *tid)
{
    for (unsigned i = 0; i < 16; i++) {
        if (g_names[i].tid[0] && !strcasecmp(g_names[i].tid, tid)) {
            return g_names[i].name;
        }
    }
    return "";
}

/* A name OGBox reads in the folder: printable ASCII without / \ : * ? " < > | (and so no tab or line
 * break), no "..", not beginning with a dot or a space nor ending in a space; 100 characters at most
 * (its id and mods.txt's line keep it whole) */
static bool plain_name(const char *s)
{
    const size_t n = strlen(s);
    if (!n || n > 100 || s[0] == '.' || s[0] == ' ' || s[n - 1] == ' ' || strstr(s, "..")) {
        return false;
    }
    for (const char *p = s; *p; p++) {
        if (*p < 0x20 || *p > 0x7E || strchr("/\\:*?\"<>|", *p)) {
            return false;
        }
    }
    return true;
}

static char file_kind(const char *name)
{
    const char *dot = strrchr(name, '.');
    return !dot ? 0 : !strcasecmp(dot, ".jmp") ? 'J' : !strcasecmp(dot, ".ips") ? 'I' : !strcasecmp(dot, ".bps") ? 'B' : 0;
}

static int user_file_cmp(const void *a, const void *b)
{
    return strcasecmp(((const UserFile *)a)->rel, ((const UserFile *)b)->rel);
}

/* The folder's .jmp, .ips and .bps files, and those of its folders (one level) */
static void scan_dir(const char *dir, const char *sub)
{
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    for (struct dirent *de; (de = readdir(d)) != NULL;) {
        const char *name = de->d_name;
        if (name[0] == '.') {
            continue;   /* ".", "..", and the hidden ("._x.ips", a Mac's) */
        }
        char path[1024];
        if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, name) >= sizeof(path)) {
            continue;
        }
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!sub[0] && plain_name(name)) {
                scan_dir(path, name);
            } else if (g_user_log) {
                pshakux_log("patches:   your %s%s%s: not read (%s)", sub, sub[0] ? "/" : "", name,
                            sub[0] ? "a folder in a folder" : "its name is not of plain characters");
            }
            continue;
        }
        const char kind = S_ISREG(st.st_mode) ? file_kind(name) : 0;
        if (!kind) {
            continue;
        }
        if (!plain_name(name)) {
            if (g_user_log) {
                pshakux_log("patches:   your %s%s%s: left out: its name is not of plain characters (letters, digits, "
                            "spaces and punctuation but / \\ : * ? \" < > |, no \"..\", 100 at most)", sub,
                            sub[0] ? "/" : "", name);
            }
            continue;
        }
        if (st.st_size > (off_t)USER_FILE_MAX) {
            if (g_user_log) {
                pshakux_log("patches:   your %s%s%s: left out: %lld bytes, over 4 MiB", sub, sub[0] ? "/" : "", name,
                            (long long)st.st_size);
            }
            continue;
        }
        if (g_nfiles >= USER_SCAN_MAX) {
            if (g_user_log) {
                pshakux_log("patches:   your %s%s%s: left out: the folder has over %u patches", sub, sub[0] ? "/" : "",
                            name, USER_SCAN_MAX);
            }
            continue;
        }
        UserFile *u = &g_files[g_nfiles];
        if ((size_t)snprintf(u->rel, sizeof(u->rel), "%s%s%s", sub, sub[0] ? "/" : "", name) > 100) {
            if (g_user_log) {
                pshakux_log("patches:   your %s%s%s: left out: its path in the folder is over 100 characters", sub,
                            sub[0] ? "/" : "", name);
            }
            continue;
        }
        g_nfiles++;
        copyf(u->dir, sizeof(u->dir), dir);
        copyf(u->name, sizeof(u->name), name);
        u->kind = kind;
    }
    closedir(d);
}

static void user_scan(void)
{
    g_nfiles = 0;
    if (g_user_dir[0]) {
        scan_dir(g_user_dir, "");
        qsort(g_files, g_nfiles, sizeof(g_files[0]), user_file_cmp);
    }
}

/* -- .jmp (Jay's Magic Patcher), as ps5/tools/import-gamedata.py's parse_jmp reads one: key=value
 * lines before the first record (name, author, version with the release's {CRC32}, notes), "#"
 * comments naming the file the records under them are for ("# default.xbe - ...") and, with "[-]",
 * records off (an alternative: left out), "APPEND: hex", "offset: hex", and a find line with its
 * replacement under it. Unlike the importer, a line it can't read is not skipped: the patch is
 * refused, with the line's number. */

typedef struct Jmp {
    char **lines;       /* the records, as the database's */
    size_t n;
    unsigned records;
    char crc[9], tid[9];
    char author[64], note[320], category[16], region[16];
    char bad[192];
} Jmp;

static void jmp_free(Jmp *j)
{
    for (size_t k = 0; k < j->n; k++) {
        free(j->lines[k]);
    }
    free(j->lines);
    j->lines = NULL;
    j->n = 0;
}

static void jmp_add(Jmp *j, const char *a, const char *b, const char *c)
{
    const size_t len = strlen(a) + strlen(b) + strlen(c) + 3;
    char **grown = realloc(j->lines, (j->n + 1) * sizeof(char *));
    char *line = malloc(len);
    if (!grown || !line) {
        free(line);
        if (grown) {
            j->lines = grown;
        }
        snprintf(j->bad, sizeof(j->bad), "out of memory");
        return;
    }
    j->lines = grown;
    snprintf(line, len, "%s%s%s%s%s", a, b[0] ? "\t" : "", b, c[0] ? "\t" : "", c);
    j->lines[j->n++] = line;
}

static char *strip(char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) {
        s[--n] = 0;
    }
    return s;
}

static void squeeze(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (!isspace((unsigned char)*s)) {
            *o++ = *s;
        }
    }
    *o = 0;
}

static bool all_hex(const char *s)
{
    if (!*s) {
        return false;
    }
    for (; *s; s++) {
        if (!isxdigit((unsigned char)*s)) {
            return false;
        }
    }
    return true;
}

/* The importer's ^\[?-?\]?\s*([A-Za-z0-9_ .\\/-]+\.xbe)\s*- on a comment */
static bool comment_target(const char *c, char *out, size_t n)
{
    const char *p = c;
    p += *p == '[';
    p += *p == '-';
    p += *p == ']';
    while (isspace((unsigned char)*p)) {
        p++;
    }
    const char *q = p;
    while (*q && (isalnum((unsigned char)*q) || strchr("_ .\\/-", *q))) {
        q++;
    }
    for (const char *x = q; x >= p + 5; x--) {
        if (strncasecmp(x - 4, ".xbe", 4) != 0) {
            continue;
        }
        const char *after = x;
        while (isspace((unsigned char)*after)) {
            after++;
        }
        if (*after != '-') {
            continue;
        }
        char name[256];
        snprintf(name, sizeof(name), "%.*s", (int)(x - p), p);
        snprintf(out, n, "%s", strip(name));
        return true;
    }
    return false;
}

/* The records' file: a T line when it changes (the C line first, before any) */
static void jmp_target(Jmp *j, const char *target, char *emitted, size_t n)
{
    if (!j->n && j->crc[0]) {
        jmp_add(j, "C", j->crc, "");
    }
    if (!emitted[0] || strcasecmp(emitted, target) != 0) {
        jmp_add(j, "T", target, "");
        snprintf(emitted, n, "%s", target);
    }
    j->records++;
}

static void jmp_parse(char *text, Jmp *j)
{
    char target[256] = "default.xbe", emitted[256] = "";
    bool off = false;
    char *next = text;
    unsigned no = 0;
    while (next && !j->bad[0]) {
        char *raw = next;
        next = strchr(raw, '\n');
        if (next) {
            *next++ = 0;
        }
        no++;
        char *line = strip(raw);
        if (!*line) {
            continue;
        }
        if (line[0] == '#') {
            const char *comment = strip(line + 1);
            comment_target(comment, target, sizeof(target));
            off = strstr(comment, "[-]") != NULL;
            continue;
        }
        char *eq = strchr(line, '='), *colon = strchr(line, ':');
        if (eq && !j->records && !all_hex(line) && !(colon && colon < eq)) {
            *eq = 0;
            char *k = strip(line), *v = strip(eq + 1);
            if (!strcasecmp(k, "author")) {
                copyf(j->author, sizeof(j->author), v);
            } else if (!strcasecmp(k, "notes")) {
                copyf(j->note, sizeof(j->note), v);
            } else if (!strcasecmp(k, "category")) {
                copyf(j->category, sizeof(j->category), v);
            } else if (!strcasecmp(k, "region")) {
                copyf(j->region, sizeof(j->region), v);
            } else if (!strcasecmp(k, "version")) {
                /* the release's {CRC32}; a title ID first (Jay's GitHub files: "4D530039 {...}") */
                const char *b = strchr(v, '{');
                if (b && strlen(b) >= 10 && b[9] == '}') {
                    char c[9];
                    snprintf(c, sizeof(c), "%.8s", b + 1);
                    if (all_hex(c)) {
                        copyf(j->crc, sizeof(j->crc), c);
                    }
                }
                char t[9];
                snprintf(t, sizeof(t), "%.8s", v);
                if (strlen(t) == 8 && all_hex(t) && !isalnum((unsigned char)v[8]) && v[8] != '_') {
                    copyf(j->tid, sizeof(j->tid), t);
                }
            }
            continue;
        }
        if (off) {
            continue;   /* a record off ("[-]"): left out, as the importer does */
        }
        if (!strncasecmp(line, "APPEND:", 7)) {
            char *data = line + 7;
            squeeze(data);
            if (!all_hex(data) || strlen(data) % 2) {
                snprintf(j->bad, sizeof(j->bad), "line %u: APPEND: without whole bytes of hex", no);
                break;
            }
            jmp_target(j, target, emitted, sizeof(emitted));
            jmp_add(j, "A", data, "");
            continue;
        }
        if (colon) {
            *colon = 0;
            char *o = strip(line), *data = colon + 1;
            squeeze(data);
            if (!strncasecmp(o, "0x", 2)) {
                o += 2;
            }
            unsigned long long at = 0;
            if (!hexnum(o, &at) || !all_hex(data) || strlen(data) % 2) {
                snprintf(j->bad, sizeof(j->bad), "line %u: an offset line that isn't \"offset: hex bytes\"", no);
                break;
            }
            char at_hex[24];
            snprintf(at_hex, sizeof(at_hex), "%llX", at);
            jmp_target(j, target, emitted, sizeof(emitted));
            jmp_add(j, "O", at_hex, data);
            continue;
        }
        if (all_hex(line)) {
            char *repl = NULL;
            /* 1.0.4: blank lines between a find and its replacement are skipped (one of Jay's
             * collection, a 54510004 widescreen patch, has one) */
            while (next && (!repl || !*repl)) {
                char *r = next;
                next = strchr(r, '\n');
                if (next) {
                    *next++ = 0;
                }
                no++;
                repl = strip(r);
            }
            if (!repl || !all_hex(repl) || strlen(repl) != strlen(line) || strlen(line) % 2) {
                snprintf(j->bad, sizeof(j->bad), "line %u: a find line without its replacement (as many whole bytes "
                         "of hex) under it", repl ? no - 1 : no);
                break;
            }
            jmp_target(j, target, emitted, sizeof(emitted));
            jmp_add(j, "F", line, repl);
            continue;
        }
        snprintf(j->bad, sizeof(j->bad), "line %u: not a line of a .jmp patch (%.32s%s)", no, line,
                 strlen(line) > 32 ? "..." : "");
    }
    if (!j->bad[0] && !j->records) {
        snprintf(j->bad, sizeof(j->bad), "no patch in it: no find, offset or APPEND: line that is on");
    }
}

/* A run of exactly 8 hex digits in s that is tid */
static bool tid_in(const char *s, const char *tid)
{
    for (const char *p = s; *p;) {
        if (!isxdigit((unsigned char)*p)) {
            p++;
            continue;
        }
        const char *q = p;
        while (isxdigit((unsigned char)*q)) {
            q++;
        }
        if (q - p == 8 && !strncasecmp(p, tid, 8)) {
            return true;
        }
        p = q;
    }
    return false;
}

/* s as lower-case words of letters and digits, one space between */
static void words_of(const char *s, char *out, size_t n)
{
    size_t o = 0;
    bool gap = false;
    for (; *s && o + 2 < n; s++) {
        if (isalnum((unsigned char)*s)) {
            if (gap && o) {
                out[o++] = ' ';
            }
            out[o++] = (char)tolower((unsigned char)*s);
            gap = false;
        } else {
            gap = true;
        }
    }
    out[o] = 0;
}

/* A sequel's number after a name ("2", "ii"): not that game's */
static bool sequel_word(const char *w, size_t n)
{
    static const char *const roman[] = {"ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x"};
    bool digits = n > 0;
    for (size_t i = 0; i < n; i++) {
        digits = digits && isdigit((unsigned char)w[i]);
    }
    for (size_t r = 0; !digits && r < sizeof(roman) / sizeof(roman[0]); r++) {
        digits = strlen(roman[r]) == n && !strncmp(w, roman[r], n);
    }
    return digits;
}

/* The game's whole name in the file's path (case, punctuation aside): its words in a row, not part
 * of longer words, and not followed by a sequel's number; a name of 4 letters or digits at least */
static bool name_in(const char *rel, const char *game)
{
    char f[256], g[192];
    words_of(rel, f, sizeof(f));
    words_of(game, g, sizeof(g));
    size_t letters = 0;
    for (const char *p = g; *p; p++) {
        letters += *p != ' ';
    }
    if (letters < 4) {
        return false;
    }
    const size_t gl = strlen(g);
    for (const char *p = f; (p = strstr(p, g)) != NULL; p++) {
        const char *after = p + gl;
        if ((p > f && p[-1] != ' ') || (*after && *after != ' ')) {
            continue;
        }
        if (*after == ' ' && sequel_word(after + 1, strcspn(after + 1, " "))) {
            continue;
        }
        return true;
    }
    return false;
}

/* attach_file's "<title ID> <path>" lines: whether rel is attached to tid */
static bool attach_has(const char *text, const char *tid, const char *rel)
{
    for (const char *p = text; p && *p;) {
        const char *end = strchr(p, '\n');
        const size_t n = end ? (size_t)(end - p) : strlen(p);
        char line[256];
        if (n < sizeof(line)) {
            memcpy(line, p, n);
            line[n] = 0;
            char *s = strip(line);
            if (s[0] != '#' && strlen(s) > 9 && s[8] == ' ' && !strncasecmp(s, tid, 8) && !strcasecmp(strip(s + 9), rel)) {
                return true;
            }
        }
        p = end ? end + 1 : NULL;
    }
    return false;
}

static char *attach_text(void)
{
    size_t n = 0;
    char *t = g_attach_file[0] ? (char *)read_host(g_attach_file, &n) : NULL;
    if (t) {
        t[n] = 0;
    }
    return t;
}

/* What a file is for: how it is joined to the title ID's game (NULL: not), whether it says which game
 * it is for (ident: a BPS, a JMP with a CRC-32 or a title ID), its JMP read (j, when given) and why it
 * can't be applied (bad) */
static const char *user_how(const UserFile *u, const char *key, const char *tid, const char *attached, bool *ident,
                            Jmp *j, char *bad, size_t bad_len)
{
    const uint32_t disc = key && g_crc && !strcasecmp(key, g_crc_key) ? g_crc : 0;
    const uint32_t size = disc ? g_size : 0;
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", u->dir, u->name);
    const char *how = NULL;
    *ident = false;
    bad[0] = 0;
    if (u->kind == 'B') {
        /* its source's size (the header) and CRC-32 (the footer's first) */
        uint8_t head[32], foot[12];
        FILE *f = fopen(path, "rb");
        size_t hn = f ? fread(head, 1, sizeof(head), f) : 0, at = 4;
        uint64_t src_size = 0;
        const bool ok = f && hn >= 5 && !memcmp(head, "BPS1", 4) && bps_num(head, hn, &at, &src_size) &&
                        fseek(f, -12, SEEK_END) == 0 && fread(foot, 1, 12, f) == 12;
        if (f) {
            fclose(f);
        }
        if (!ok) {
            snprintf(bad, bad_len, "not a BPS file");
        } else {
            *ident = true;
            const uint32_t src_crc = (uint32_t)foot[0] | (uint32_t)foot[1] << 8 | (uint32_t)foot[2] << 16 |
                                     (uint32_t)foot[3] << 24;
            if (disc && src_crc == disc && (!size || src_size == size)) {
                how = "its source's CRC-32 and size are this disc's default.xbe's";
            }
        }
    } else if (u->kind == 'J') {
        Jmp mine = {0}, *jj = j ? j : &mine;
        size_t n = 0;
        char *text = (char *)read_host(path, &n);
        if (!text) {
            snprintf(bad, bad_len, "it cannot be read");
        } else {
            text[n] = 0;
            jmp_parse(text, jj);
            free(text);
            copyf(bad, bad_len, jj->bad);
            *ident = jj->crc[0] || jj->tid[0];
            if (jj->crc[0] && disc && strtoul(jj->crc, NULL, 16) == disc) {
                how = "the CRC-32 of its version= line is this disc's default.xbe's";
            } else if (jj->tid[0] && !strcasecmp(jj->tid, tid)) {
                how = "the title ID of its version= line";
            }
        }
        if (jj == &mine) {
            jmp_free(&mine);
        }
    } else {
        uint8_t head[5];
        FILE *f = fopen(path, "rb");
        if (!f || fread(head, 1, 5, f) != 5 || memcmp(head, "PATCH", 5) != 0) {
            snprintf(bad, bad_len, "not an IPS file");
        }
        if (f) {
            fclose(f);
        }
    }
    if (!*ident && !how) {
        char stem[128];
        copyf(stem, sizeof(stem), u->rel);
        char *dot = strrchr(stem, '.');
        if (dot) {
            *dot = 0;
        }
        how = tid_in(stem, tid) ? "the title ID in its name"
              : name_in(stem, game_name(tid)) ? "the game's name in its name"
              : attached && attach_has(attached, tid, u->rel) ? k_by_hand : NULL;
    }
    return how;
}

/* The player's patches joined to the game of key (tid) after the database's, as entries of their own */
static void user_load(const char *key, const char *tid)
{
    if (g_user_log && g_user_dir[0]) {
        pshakux_log("patches: your patches folder %s:", g_user_dir);
    }
    user_scan();
    if (!g_nfiles) {
        if (g_user_log && g_user_dir[0]) {
            pshakux_log("patches:   no .jmp, .ips or .bps files in it");
        }
        return;
    }
    char *attached = attach_text();
    unsigned mine = 0;
    for (unsigned i = 0; i < g_nfiles; i++) {
        const UserFile *u = &g_files[i];
        Jmp j = {0};
        bool ident;
        char bad[192];
        const char *how = user_how(u, key, tid, attached, &ident, &j, bad, sizeof(bad));
        if (!how) {
            jmp_free(&j);
            continue;
        }
        char id[128];
        const char *refused = (size_t)snprintf(id, sizeof(id), "user:%s", u->rel) >= sizeof(id) ? "its path is too long" : NULL;
        for (unsigned k = 0; k < g_ndb && !refused; k++) {
            if (!strcasecmp(g_db[k].p.id, id)) {
                refused = "its id is another patch's (one of the database's, or a file of the same name in another case)";
            }
        }
        if (!refused && mine >= USER_GAME_MAX) {
            refused = "the game has 64 of your patches already";
        }
        if (!refused && g_ndb >= MAX_PATCHES) {
            refused = "the game has too many patches";
        }
        if (refused) {
            if (g_user_log) {
                pshakux_log("patches:   your %s: for this game (%s), left out: %s", u->rel, how, refused);
            }
            jmp_free(&j);
            continue;
        }
        Entry *e = &g_db[g_ndb++];
        mine++;
        g_nuser++;
        copyf(e->p.id, sizeof(e->p.id), id);
        copyf(e->p.name, sizeof(e->p.name), u->name);
        char *dot = strrchr(e->p.name, '.');
        if (dot) {
            *dot = 0;
        }
        copyf(e->p.category, sizeof(e->p.category), j.category[0] ? j.category : "Your patches");
        copyf(e->p.author, sizeof(e->p.author), j.author[0] ? j.author : "Author unknown");
        copyf(e->p.source, sizeof(e->p.source), "your patches folder");
        copyf(e->p.region, sizeof(e->p.region), j.region);
        snprintf(e->p.note, sizeof(e->p.note), "%.140s%sYour %s patch patches/%s, for this game: %s.%s", j.note,
                 j.note[0] ? " " : "", u->kind == 'J' ? "JMP" : u->kind == 'I' ? "IPS" : "BPS", u->rel, how,
                 u->kind == 'I' ? " An IPS file doesn't say which release it is for: release not checked." : "");
        copyf(e->bad, sizeof(e->bad), bad);
        e->how = how;
        e->kind = u->kind;
        if (u->kind == 'J') {
            e->lines = j.lines;
            e->nlines = j.n;
            j.lines = NULL;
            j.n = 0;
        } else if (!bad[0]) {
            e->files_dir = strdup(u->dir);
            e->lines = calloc(2, sizeof(char *));
            if (e->lines && e->files_dir) {
                e->lines[0] = strdup("T\tdefault.xbe");
                const size_t len = strlen(u->name) + 3;
                e->lines[1] = malloc(len);
                if (e->lines[1]) {
                    snprintf(e->lines[1], len, "%c\t%s", u->kind, u->name);
                }
                e->nlines = e->lines[0] && e->lines[1] ? 2 : 0;
            }
            if (!e->nlines) {
                snprintf(e->bad, sizeof(e->bad), "out of memory");
            }
        }
        jmp_free(&j);
        if (g_user_log) {
            pshakux_log("patches:   your %s: for this game: %s%s%s", u->rel, how, e->bad[0] ? "; it can't be applied: " : "",
                        e->bad);
        }
    }
    free(attached);
    if (g_user_log) {
        pshakux_log("patches:   %u of the folder's %u for this game; the others say they are another game's, or no "
                    "rule joins them to it", mine, g_nfiles);
    }
}

int ogbox_patches_user_files(const char *key, OgboxUserPatchFile *out, int max)
{
    char tid[9];
    if (!tid_of(key, tid)) {
        return 0;
    }
    user_scan();
    char *attached = attach_text();
    int n = 0;
    for (unsigned i = 0; i < g_nfiles && n < max; i++) {
        bool ident;
        char bad[192];
        const char *how = user_how(&g_files[i], key, tid, attached, &ident, NULL, bad, sizeof(bad));
        /* one that says which game it is for, or that a rule joins to this one, isn't offered */
        if (ident || (how && how != k_by_hand)) {
            continue;
        }
        copyf(out[n].rel, sizeof(out[n].rel), g_files[i].rel);
        out[n].attached = how == k_by_hand;
        n++;
    }
    free(attached);
    return n;
}

bool ogbox_patches_attach(const char *key, const char *rel, bool on)
{
    char tid[9];
    if (!tid_of(key, tid) || !rel || !rel[0] || strlen(rel) > 120 || strpbrk(rel, "\r\n") || !g_attach_file[0]) {
        return false;
    }
    char *text = attach_text();
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.new", g_attach_file);
    FILE *out = fopen(tmp, "w");
    if (!out) {
        free(text);
        pshakux_log("patches: %s cannot be written (%s)", tmp, strerror(errno));
        return false;
    }
    if (text && text[0]) {
        for (char *p = text; p && *p;) {
            char *end = strchr(p, '\n');
            if (end) {
                *end = 0;
            }
            if (!attach_has(p, tid, rel)) {
                fprintf(out, "%s\n", p);
            }
            p = end ? end + 1 : NULL;
        }
    } else {
        fprintf(out, "# OGBox: your patches (the data folder's patches/) attached to a game by hand: <title ID> <file>\n");
    }
    if (on) {
        fprintf(out, "%s %s\n", tid, rel);
    }
    free(text);
    if (fclose(out) != 0 || rename(tmp, g_attach_file) != 0) {
        pshakux_log("patches: %s cannot be written (%s)", g_attach_file, strerror(errno));
        return false;
    }
    pshakux_log("patches: your %s %s %s", rel, on ? "attached to" : "detached from", tid);
    return true;
}

/* An entry's release check (its C line, before its first target), 0 when it has none. 1.0.2: a C
 * line may name several releases (CRC-32s split by tabs): disc when it is one of them, else the
 * first. */
static uint32_t entry_crc(const Entry *e, uint32_t disc)
{
    for (unsigned k = 0; k < e->nlines; k++) {
        if (e->lines[k][0] == 'T' && e->lines[k][1] == '\t') {
            break;
        }
        if (e->lines[k][0] == 'C' && e->lines[k][1] == '\t') {
            const char *p = e->lines[k] + 2;
            const uint32_t first = (uint32_t)strtoul(p, NULL, 16);
            for (char *end; *p; p = end) {
                const uint32_t c = (uint32_t)strtoul(p, &end, 16);
                if (end == p) {
                    break;
                }
                if (c == disc) {
                    return disc;
                }
                while (*end == '\t' || *end == ' ') {
                    end++;
                }
            }
            return first;
        }
    }
    return 0;
}

/* ---- 0.0.2: the release check (ogbox_patches_check) ---- */

static struct {
    char key[32];
    unsigned n;
    struct {
        char id[128];
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
    db_load_all(key);
    read_on(key);
    const uint32_t disc = key && g_crc && !strcasecmp(key, g_crc_key) ? g_crc : 0;
    int n = 0;
    for (unsigned i = 0; i < g_ndb && n < max; i++) {
        /* 0.0.2: one made for another release of the game (its CRC-32 another: 0.1.52 left it out of
         * the list) is listed, greyed out, with the others the release check found (the screen
         * doesn't let it be turned on) */
        const uint32_t crc = disc ? entry_crc(&g_db[i], disc) : 0;
        out[n] = g_db[i].p;
        copyf(out[n].fit_why, sizeof(out[n].fit_why), fit_of(key, g_db[i].p.id, &out[n].fit));
        if (crc && crc != disc && out[n].fit != OGBOX_FIT_NO) {
            out[n].fit = OGBOX_FIT_NO;
            snprintf(out[n].fit_why, sizeof(out[n].fit_why), "its default.xbe has CRC-32 %08X; this disc's has %08X",
                     crc, disc);
        }
        /* 1.0.4: one of the player's that can't be read: greyed, with the line at fault */
        if (g_db[i].bad[0]) {
            out[n].fit = OGBOX_FIT_NO;
            copyf(out[n].fit_why, sizeof(out[n].fit_why), g_db[i].bad);
        }
        n++;
    }
    db_reset();
    return n;
}

int ogbox_patches_count(const char *key)
{
    db_load_all(key);
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
    /* (in 64 bits: a certificate address below the base wrapped the sum) */
    return (uint64_t)cert + 0xA4 <= x->size ? ((uint32_t)h[cert + 0xA0] | (uint32_t)h[cert + 0xA1] << 8) : 0;
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
    set_disc_xbe(key, xbe);     /* 1.0.4: the player's BPS and JMP files are joined by it */
    db_load_all(key);
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
        if (e->bad[0]) {
            fit = OGBOX_FIT_NO;
            copyf(why, sizeof(why), e->bad);
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
                        if (!ogbox_apply_records(&work, e->lines + start, k - start, e->files_dir ? e->files_dir : files_dir,
                                                 why, sizeof(why))) {
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
        /* 1.0.4: an IPS of the player's fits any file: its release isn't checked */
        if (fit == OGBOX_FIT_YES && e->kind == 'I') {
            fit = OGBOX_FIT_UNCHECKED;
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
        pshakux_log("patches: %s: %d in gamedata/patches.db and %u of yours, %u of them for another release of the game "
                    "or not to be applied", key, n - (int)g_nuser, g_nuser, bad);
    }
    db_reset();
    return n;
}

int ogbox_patches_prepare(const NcDisc *disc, const char *key, const char *work_dir, FILE *manifest)
{
    /* 1.0.4: default.xbe's CRC-32 and size, if the release check didn't read them (the player's BPS
     * and JMP files are joined to the game by them) */
    if (disc && g_user_dir[0] && !(key && g_crc && g_size && !strcasecmp(key, g_crc_key))) {
        Target x = {0};
        snprintf(x.name, sizeof(x.name), "default.xbe");
        if (target_load(disc, &x)) {
            set_disc_xbe(key, &x.buf);
        }
        free(x.buf.data);
    }
    g_user_log = true;
    db_load_all(key);
    g_user_log = false;
    read_on(key);
    unsigned on = 0;
    for (unsigned i = 0; i < g_ndb; i++) {
        on += g_db[i].p.on;
    }
    pshakux_log("patches: %s: %u in gamedata/patches.db, %u of yours, %u on", key, g_ndb - g_nuser, g_nuser, on);
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
            if ((uint64_t)cert + 0xA4 <= x.buf.size) {
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
        if (e->bad[0]) {
            pshakux_log("patches:   %s (%s): left out: %s", e->p.name, e->p.id, e->bad);
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
                const bool done = ogbox_apply_records(&t->buf, e->lines + start, k - start,
                                                      e->files_dir ? e->files_dir : files_dir, why, sizeof(why));
                g_original_crc = 0;
                if (done) {
                    /* 1.0.3: a target once, as it was before this patch (a ninth part ran past
                     * undo[8]; there are at most 8 targets) */
                    unsigned u = 0;
                    while (u < nundo && undo[u] != t) {
                        u++;
                    }
                    if (u < nundo) {
                        free(before.data);
                    } else {
                        undo[nundo] = t;
                        saved[nundo++] = before;
                    }
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
            pshakux_log("patches:   %s (%s, %s by %s): applied%s%s", e->p.name, e->p.id, e->p.category, e->p.author,
                        e->how ? "; yours, for this game: " : "", e->how ? e->how : "");
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
