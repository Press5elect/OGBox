/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * hakuX's mods and cheats for the console (ogbox_mods.h). The file formats are those of
 * hakuX's mods/README.md; the reading is that of its Android app (data/Mods.kt, rfandango's
 * hakuX, GPL-2.0-or-later) in C, and the manifest it writes is the one the core reads
 * (native/core/mods.h).
 */
#include "core/ogbox_mods.h"
#include "core/ogbox_patches.h"
#include "core/ogbox_xpr.h"

#include "platform/ps5_bootlog.h"

#include "native/core/mods.h"
#include "native/core/xdvdfs.h"
#include "native/core/pack.h"
#include "native/core/chd.h"
#include "hw/xbox/hle/core/xbe.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_MODS 32
#define MAX_CHEATS 256

#define MOD_EDITS 4

typedef struct Line {
    char *xbe; /* the XBE section it belongs to */
    char *text;
} Line;

typedef struct Mod {
    char id[128];
    char dir[1024];    /* 0.0.3: the folder it's in (mods/<key> or mods/<title ID>) */
    char name[128];
    char description[384];
    int refresh;
    bool on;
    bool gamefix;      /* 0.0.3: "gamefix: true": always on, can't be turned off (the Mods screen greys it) */
    /* 0.0.3: "edit: <disc file> | <line start> | <new line>": the disc's file with the first line that
     * starts so replaced, over the disc (the rest as the disc has it: one line fits every release) */
    char edit[MOD_EDITS][320];
    unsigned nedits;
    Line *patches;
    size_t npatches;
} Mod;

typedef struct Cheat {
    char name[128];
    bool once, on;
    char **code;
    size_t ncode;
} Cheat;

static Mod g_mods[MAX_MODS];
static unsigned g_nmods;
static Cheat g_cheats[MAX_CHEATS];
static unsigned g_ncheats;
/* 0.0.2: cheats.txt's "crc:" line (before its first cheat): the CRC-32s of the default.xbe its
 * addresses are for (the release check; hakuX skips the line there) */
#define MAX_CHEAT_CRCS 8
static uint32_t g_cheat_crc[MAX_CHEAT_CRCS];
static unsigned g_ncheat_crc;

/* ---- small helpers ---- */

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) {
        e--;
    }
    *e = 0;
    return s;
}

/* A line's content: trimmed, NULL when blank or a comment (# first). */
static char *content(char *line)
{
    char *s = trim(line);
    return (!*s || *s == '#') ? NULL : s;
}

static char *read_all(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    const long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < 0 || n > (8 << 20)) {
        fclose(fp);
        return NULL;
    }
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    const size_t got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    buf[got] = 0;
    return buf;
}

/* "key: value" with a key of letters only (Mods.kt's header()). */
static bool header(char *line, char **key, char **value)
{
    char *colon = strchr(line, ':');
    if (!colon || colon == line) {
        return false;
    }
    *colon = 0;
    char *k = trim(line);
    if (!*k) {
        *colon = ':';
        return false;
    }
    for (char *p = k; *p; p++) {
        if (!isalpha((unsigned char)*p)) {
            *colon = ':';
            return false;
        }
        *p = (char)tolower((unsigned char)*p);
    }
    *key = k;
    *value = trim(colon + 1);
    return true;
}

static void copy(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The names in a folder, sorted (no dot files). */
static char **list_dir(const char *dir, size_t *count, bool dirs_only)
{
    *count = 0;
    DIR *d = opendir(dir);
    if (!d) {
        return NULL;
    }
    size_t cap = 16, n = 0;
    char **names = malloc(cap * sizeof(*names));
    struct dirent *e;
    while (names && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        if (dirs_only) {
            char p[1024];
            struct stat st;
            snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
            if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) {
                continue;
            }
        }
        if (n == cap) {
            cap *= 2;
            char **grown = realloc(names, cap * sizeof(*names));
            if (!grown) {
                break;
            }
            names = grown;
        }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    if (names && n > 1) {
        qsort(names, n, sizeof(*names), by_name);
    }
    *count = n;
    return names;
}

static void free_names(char **names, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        free(names[i]);
    }
    free(names);
}

/* ---- the game's key ---- */

/* 0.1.59: a disc opened here on a descriptor someone else closes: its CHD reader (hunk cache and
 * all) freed, the descriptor left open (nc_disc_close would close it too) */
static void disc_release(NcDisc *d)
{
    if (d->chd) {
        nc_chd_close(d->chd);
        d->chd = NULL;
    }
    nc_pack_close(d->pack);     /* (0.0.3: a split image's other parts closed with it) */
    d->pack = NULL;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* "%08X.%u": the certificate's title ID and version (GameSettings.gameKey). */
static bool game_key(int disc_fd, char *key, size_t n)
{
    NcDisc disc;
    memset(&disc, 0, sizeof(disc));
    if (!nc_disc_open(&disc, disc_fd)) { /* (not closed: the descriptor is the core's) */
        return false;
    }
    NcDiscFile f;
    uint8_t h[0x1000];
    const bool read = nc_disc_find(&disc, "\\default.xbe", &f) && f.size >= sizeof(h) &&
                      nc_disc_read(&disc, &f, 0, h, sizeof(h));
    disc_release(&disc);
    if (!read) {
        return false;
    }
    const uint32_t base = le32(h + 0x104), cert_va = le32(h + 0x118);
    const uint32_t off = cert_va - base;
    if (cert_va < base || off > sizeof(h) - 0xB0) {
        return false;
    }
    const uint32_t title = le32(h + off + 8), version = le32(h + off + 0xAC);
    if (!title) {
        return false;
    }
    snprintf(key, n, "%08X.%u", title, version);
    return true;
}

/* ---- mods ---- */

static void add_patch(Mod *m, const char *xbe, const char *text)
{
    Line *grown = realloc(m->patches, (m->npatches + 1) * sizeof(Line));
    if (!grown) {
        return;
    }
    m->patches = grown;
    m->patches[m->npatches].xbe = strdup(xbe);
    m->patches[m->npatches].text = strdup(text);
    m->npatches++;
}

static void load_mod(const char *dir, const char *id)
{
    if (g_nmods >= MAX_MODS) {
        return;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/mod.txt", dir, id);
    char *text = read_all(path);
    if (!text) {
        return;
    }
    Mod *m = &g_mods[g_nmods++];
    memset(m, 0, sizeof(*m));
    copy(m->id, sizeof(m->id), id);
    copy(m->dir, sizeof(m->dir), dir);
    copy(m->name, sizeof(m->name), id);
    char xbe[256] = "";
    bool in_section = false;
    for (char *save, *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *s = content(line);
        if (!s) {
            continue;
        }
        const size_t len = strlen(s);
        if (s[0] == '[' && s[len - 1] == ']') {
            s[len - 1] = 0;
            copy(xbe, sizeof(xbe), trim(s + 1));
            in_section = true;
            continue;
        }
        if (in_section) {
            add_patch(m, xbe, s);
            continue;
        }
        char *key, *value;
        if (header(s, &key, &value)) {
            if (!strcmp(key, "name") && *value) {
                copy(m->name, sizeof(m->name), value);
            } else if (!strcmp(key, "description")) {
                copy(m->description, sizeof(m->description), value);
            } else if (!strcmp(key, "refresh")) {
                m->refresh = atoi(value);
            } else if (!strcmp(key, "edit") && m->nedits < MOD_EDITS) {
                copy(m->edit[m->nedits++], sizeof(m->edit[0]), value);
            } else if (!strcmp(key, "gamefix")) {
                m->gamefix = !strcasecmp(value, "true") || !strcmp(value, "1") || !strcasecmp(value, "yes");
                m->on |= m->gamefix;
            }
        }
    }
    free(text);
}

/* ---- cheats ---- */

static void add_code(Cheat *c, const char *line)
{
    char **grown = realloc(c->code, (c->ncode + 1) * sizeof(char *));
    if (grown) {
        c->code = grown;
        c->code[c->ncode++] = strdup(line);
    }
}

static void load_cheats(const char *path)
{
    char *text = read_all(path);
    if (!text) {
        return;
    }
    Cheat *cur = NULL;
    for (char *save, *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *s = content(line);
        if (!s) {
            continue;
        }
        const size_t len = strlen(s);
        if (s[0] == '[' && s[len - 1] == ']') {
            if (g_ncheats >= MAX_CHEATS) {
                break;
            }
            s[len - 1] = 0;
            cur = &g_cheats[g_ncheats++];
            memset(cur, 0, sizeof(*cur));
            copy(cur->name, sizeof(cur->name), trim(s + 1));
            continue;
        }
        if (!cur) {
            /* 0.0.2: "crc: 1A2B3C4D [...]" before the first cheat */
            char head[512], *hk, *hv;
            copy(head, sizeof(head), s);
            if (header(head, &hk, &hv) && !strcmp(hk, "crc")) {
                for (char *save2, *t = strtok_r(hv, " ,\t", &save2); t && g_ncheat_crc < MAX_CHEAT_CRCS;
                     t = strtok_r(NULL, " ,\t", &save2)) {
                    char *endp;
                    const unsigned long v = strtoul(t, &endp, 16);
                    if (endp != t && !*endp) {
                        g_cheat_crc[g_ncheat_crc++] = (uint32_t)v;
                    }
                }
            }
            continue;
        }
        char *key, *value;
        char copyline[512];
        copy(copyline, sizeof(copyline), s);
        if (header(copyline, &key, &value) && !strcmp(key, "note")) {
            continue;
        }
        if (!strcasecmp(s, "once")) {
            cur->once = true;
        } else {
            add_code(cur, s);
        }
    }
    free(text);
}

/* ---- the choices ---- */

static void read_choices(const char *key, const char *title_dir)
{
    char path[1024];
    char *text = NULL;
    const char *where = NULL;
    snprintf(path, sizeof(path), "/data/ogbox/mods.txt");
    if ((text = read_all(path)) != NULL) {
        where = path;
    } else {
        snprintf(path, sizeof(path), "%s/mods.txt", title_dir);
        if ((text = read_all(path)) != NULL) {
            where = path;
        }
    }
    if (!text) {
        return;
    }
    unsigned on = 0;
    for (char *save, *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *s = content(line);
        if (!s) {
            continue;
        }
        char *sp = s;
        while (*sp && !isspace((unsigned char)*sp)) {
            sp++;
        }
        if (!*sp) {
            continue;
        }
        *sp++ = 0;
        if (strcasecmp(s, key) != 0) {
            continue;
        }
        char *kind = trim(sp);
        char *name = kind;
        while (*name && !isspace((unsigned char)*name)) {
            name++;
        }
        if (*name) {
            *name++ = 0;
        }
        name = trim(name);
        if (!strcasecmp(kind, "mod")) {
            for (unsigned i = 0; i < g_nmods; i++) {
                if (!strcasecmp(g_mods[i].id, name) && !g_mods[i].on) {
                    g_mods[i].on = true;
                    on++;
                }
            }
        } else if (!strcasecmp(kind, "cheat")) {
            for (unsigned i = 0; i < g_ncheats; i++) {
                if (!strcasecmp(g_cheats[i].name, name) && !g_cheats[i].on) {
                    g_cheats[i].on = true;
                    on++;
                }
            }
        }
    }
    free(text);
    pshakux_log("mods: %s: %u chosen for %s", where, on, key);
}

/* ---- 0.0.2: the release check ---- */

/* A title ID and version can stand for more than one release of a game (a USA and a European
 * disc), and a mod's addresses or a cheat's hold for one. The core skips a mod's line whose bytes
 * differ ("old -> new") and writes the rest: half a mod, made for another release's code, crashed or
 * corrupted the game. So the disc is checked first: a mod's "old" bytes against its XBEs (a line
 * after another of the same mod over the same bytes against that one's: the core's order), cheats.txt's
 * crc: line against default.xbe's CRC-32, the patches by trying each (ogbox_patches_check). One that
 * doesn't fit can't be turned on (the Mods screen) and is left out at the start, whole. */

#define FIT_FILES 4

typedef struct XbeFile {
    char path[256];         /* as the mod's section names it */
    OgboxBuf buf;           /* the disc's file, as on the disc */
    HleXbeInfo *x;          /* its headers, NULL if it isn't an XBE */
    bool there;
} XbeFile;

static struct {
    char key[32];
    bool valid;
    uint32_t crc;           /* default.xbe's CRC-32, 0 unread */
    unsigned nmods;
    struct {
        char id[128];
        int fit;
        char why[192];
    } mods[MAX_MODS];
    int cheats;             /* cheats.txt's (OGBOX_FIT_*) */
    char cheats_why[192];
} g_fit;

/* A file of the disc, whole, as the disc has it (up to 64 MiB) */
static bool disc_file(const NcDisc *disc, const char *path, OgboxBuf *b)
{
    NcDiscFile f;
    if (!nc_disc_find_orig(disc, path, &f) || (f.attr & 0x10) || !f.size || f.size > (64u << 20)) {
        return false;
    }
    uint8_t *p = malloc(f.size);
    if (!p) {
        return false;
    }
    for (uint32_t at = 0; at < f.size;) {
        const uint32_t n = f.size - at < (1u << 20) ? f.size - at : (1u << 20);
        if (!nc_disc_read_orig(disc, &f, at, p + at, n)) {
            free(p);
            return false;
        }
        at += n;
    }
    b->data = p;
    b->size = b->cap = f.size;
    return true;
}

/* The XBE a mod's section names ("default.xbe", "bin\game.xbe"), read once a check */
static XbeFile *xbe_file(const NcDisc *disc, const char *path, XbeFile *files, unsigned *n)
{
    for (unsigned i = 0; i < *n; i++) {
        if (!strcasecmp(files[i].path, path)) {
            return &files[i];
        }
    }
    if (*n >= FIT_FILES) {
        return NULL;
    }
    XbeFile *f = &files[(*n)++];
    memset(f, 0, sizeof(*f));
    copy(f->path, sizeof(f->path), path);
    f->there = disc && disc_file(disc, path, &f->buf);
    if (f->there && f->buf.size >= 0x180) {
        const uint32_t hs = le32(f->buf.data + 0x108);
        f->x = malloc(sizeof(*f->x));
        if (f->x && (hs < 0x180 || hs > f->buf.size ||
                     hle_xbe_parse(f->buf.data, hs, f->x) != HLE_XBE_OK)) {
            free(f->x);
            f->x = NULL;
        }
    }
    return f;
}

static void xbe_files_free(XbeFile *files, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        free(files[i].buf.data);
        free(files[i].x);
    }
}

/* VA -> the XBE file's offset, as the core's mods.c (xbe_offset): its headers (to the first
 * section's data, or past it as far as sizeof_headers reaches), or a section's raw data */
static bool xbe_offset(const HleXbeInfo *x, uint32_t va, uint32_t len, uint32_t *off)
{
    uint32_t hdr_end = 0x1000;
    for (unsigned i = 0; i < x->nsections; i++) {
        if (x->sections[i].raw_off < hdr_end) {
            hdr_end = x->sections[i].raw_off;
        }
    }
    if (x->sizeof_headers > hdr_end) {
        hdr_end = x->sizeof_headers;
    }
    if (va >= x->base && (uint64_t)(va - x->base) + len <= hdr_end) {
        *off = va - x->base;
        return true;
    }
    for (unsigned i = 0; i < x->nsections; i++) {
        const HleXbeSection *sec = &x->sections[i];
        if (va >= sec->va && (uint64_t)(va - sec->va) + len <= sec->raw_size) {
            *off = sec->raw_off + (va - sec->va);
            return true;
        }
    }
    return false;
}

static unsigned hex_run(const char *p, uint8_t *out, unsigned cap, const char **end)
{
    unsigned n = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1]) || n == cap) {
            break;
        }
        const char b[3] = {p[0], p[1], 0};
        out[n++] = (uint8_t)strtoul(b, NULL, 16);
        p += 2;
    }
    *end = p;
    return n;
}

/* "0x0011A2B4: 90 90" or "0x0011A2B4: 74 05 -> EB 05" (the core's mods.c patch()): false if the
 * line is not a patch (the core skips it too) */
static bool parse_line(const char *line, uint32_t *va, uint8_t *old, unsigned *nold, uint8_t *neu, unsigned *nnew)
{
    char *colon;
    *va = (uint32_t)strtoul(line, &colon, 0);
    while (*colon == ' ') {
        colon++;
    }
    *nold = *nnew = 0;
    if (*colon != ':') {
        return false;
    }
    const char *p;
    *nnew = hex_run(colon + 1, neu, 256, &p);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p[0] == '-' && p[1] == '>') {
        memcpy(old, neu, *nnew);
        *nold = *nnew;
        *nnew = hex_run(p + 2, neu, 256, &p);
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return *nnew && (!*nold || *nold == *nnew) && (!*p || *p == '#');
}

static bool is_update_section(const char *xbe)
{
    return (xbe[0] == '$' && (xbe[1] == 'u' || xbe[1] == 'U') && (xbe[2] == '\\' || xbe[2] == '/'));
}

static void hex_text(char *out, size_t n, const uint8_t *b, unsigned len)
{
    size_t o = 0;
    out[0] = 0;
    for (unsigned i = 0; i < len && i < 6 && o + 4 < n; i++) {
        o += (size_t)snprintf(out + o, n - o, "%s%02X", i ? " " : "", b[i]);
    }
    if (len > 6 && o + 4 < n) {
        snprintf(out + o, n - o, " ..");
    }
}

/* A mod against the disc: OGBOX_FIT_YES (every line with "old" bytes found them, and at least one
 * had them), _NO (one didn't, or its XBE or address isn't the disc's: why says the first), or
 * _UNCHECKED (no line to check: no "old" bytes, only the title update's, or files only). Each
 * line's bytes go into the buffer for the next (as the core's overlay), and back out after. */
static int mod_fit(const Mod *m, const NcDisc *disc, XbeFile *files, unsigned *nfiles, char *why, size_t why_n)
{
    typedef struct Undo {
        XbeFile *f;
        uint32_t off;
        unsigned n;
        uint8_t old[256];
    } Undo;
    Undo *undo = m->npatches ? malloc(m->npatches * sizeof(Undo)) : NULL;
    unsigned nundo = 0, checked = 0, bad = 0, lines = 0;
    why[0] = 0;
    for (size_t k = 0; k < m->npatches; k++) {
        const Line *l = &m->patches[k];
        uint32_t va, off;
        uint8_t old[256], neu[256];
        unsigned nold, nnew;
        if (is_update_section(l->xbe) || !parse_line(l->text, &va, old, &nold, neu, &nnew)) {
            continue;       /* the title update's (not on the disc), or no patch (the core skips it) */
        }
        lines++;
        XbeFile *f = xbe_file(disc, l->xbe, files, nfiles);
        if (!f || !f->there || !f->x) {
            if (!bad++) {
                snprintf(why, why_n, "[%s] is not an XBE on this disc", l->xbe);
            }
            continue;
        }
        if (!xbe_offset(f->x, va, nnew, &off) || (uint64_t)off + nnew > f->buf.size) {
            if (!bad++) {
                snprintf(why, why_n, "[%s] 0x%08X is not in this disc's XBE", l->xbe, va);
            }
            continue;
        }
        if (nold) {
            if (memcmp(f->buf.data + off, old, nold) != 0) {
                if (!bad++) {
                    char have[32], want[32];
                    hex_text(have, sizeof(have), f->buf.data + off, nold);
                    hex_text(want, sizeof(want), old, nold);
                    snprintf(why, why_n, "[%s] 0x%08X: this disc has %s there, the mod expects %s", l->xbe, va, have,
                             want);
                }
                continue;
            }
            checked++;
        }
        if (undo) {
            Undo *u = &undo[nundo++];
            u->f = f;
            u->off = off;
            u->n = nnew;
            memcpy(u->old, f->buf.data + off, nnew);
            memcpy(f->buf.data + off, neu, nnew);
        }
    }
    while (nundo) {
        const Undo *u = &undo[--nundo];
        memcpy(u->f->buf.data + u->off, u->old, u->n);
    }
    free(undo);
    if (bad) {
        if (lines > 1) {
            const size_t len = strlen(why);
            snprintf(why + len, why_n - len, " (%u of its %u lines)", bad, lines);
        }
        return OGBOX_FIT_NO;
    }
    return checked ? OGBOX_FIT_YES : OGBOX_FIT_UNCHECKED;
}

/* The check of the mods and cheats loaded (g_mods, g_cheats) and the patches for key, kept in g_fit
 * (the catalog's) */
static void fit_check(const NcDisc *disc, const char *key)
{
    memset(&g_fit, 0, sizeof(g_fit));
    copy(g_fit.key, sizeof(g_fit.key), key);
    XbeFile files[FIT_FILES];
    unsigned nfiles = 0;
    const XbeFile *def = xbe_file(disc, "default.xbe", files, &nfiles);
    g_fit.crc = def && def->there ? ogbox_crc32(def->buf.data, def->buf.size) : 0;
    unsigned bad = 0;
    for (unsigned i = 0; i < g_nmods; i++) {
        copy(g_fit.mods[i].id, sizeof(g_fit.mods[i].id), g_mods[i].id);
        g_fit.mods[i].fit = mod_fit(&g_mods[i], disc, files, &nfiles, g_fit.mods[i].why, sizeof(g_fit.mods[i].why));
        bad += g_fit.mods[i].fit == OGBOX_FIT_NO;
    }
    g_fit.nmods = g_nmods;
    if (g_ncheats) {
        bool match = false;
        for (unsigned i = 0; i < g_ncheat_crc; i++) {
            match |= g_cheat_crc[i] == g_fit.crc;
        }
        g_fit.cheats = !g_ncheat_crc ? OGBOX_FIT_UNCHECKED : !g_fit.crc ? OGBOX_FIT_UNKNOWN
                       : match ? OGBOX_FIT_YES : OGBOX_FIT_NO;
        if (g_fit.cheats == OGBOX_FIT_NO) {
            snprintf(g_fit.cheats_why, sizeof(g_fit.cheats_why),
                     "cheats.txt is for the default.xbe of CRC-32 %08X%s, this disc's is %08X", g_cheat_crc[0],
                     g_ncheat_crc > 1 ? " (and others)" : "", g_fit.crc);
        }
    }
    ogbox_patches_set_disc_crc(key, g_fit.crc);
    ogbox_patches_check(disc, key, def && def->there ? &def->buf : NULL);
    xbe_files_free(files, nfiles);
    g_fit.valid = true;
    pshakux_log("mods: %s: default.xbe's CRC-32 %08X; %u of %u mods for another release%s", key, g_fit.crc, bad,
                g_nmods, g_fit.cheats == OGBOX_FIT_NO ? ", and the cheats" : "");
    for (unsigned i = 0; i < g_nmods; i++) {
        if (g_fit.mods[i].fit == OGBOX_FIT_NO) {
            pshakux_log("mods:   mod %s: for another release: %s", g_mods[i].id, g_fit.mods[i].why);
        }
    }
}

/* key's check (fit_check), if there is one */
static bool fit_valid(const char *key)
{
    return g_fit.valid && key && !strcasecmp(key, g_fit.key);
}

static int fit_of_mod(const char *key, const char *id, const char **why)
{
    *why = "";
    if (!fit_valid(key)) {
        return OGBOX_FIT_UNKNOWN;
    }
    for (unsigned i = 0; i < g_fit.nmods; i++) {
        if (!strcasecmp(g_fit.mods[i].id, id)) {
            *why = g_fit.mods[i].why;
            return g_fit.mods[i].fit;
        }
    }
    return OGBOX_FIT_UNKNOWN;
}

uint32_t ogbox_mods_disc_crc(const char *key)
{
    return fit_valid(key) ? g_fit.crc : 0;
}

/* ---- the manifest ---- */

/* A mod's files/ (the disc's paths, either slash): file lines. */
static void write_files(FILE *out, const char *dir, const char *rel)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s%s%s", dir, rel[0] ? "/" : "", rel);
    size_t n = 0;
    char **names = list_dir(path, &n, false);
    for (size_t i = 0; i < n; i++) {
        char sub[1024], host[1024];
        snprintf(sub, sizeof(sub), "%s%s%s", rel, rel[0] ? "/" : "", names[i]);
        snprintf(host, sizeof(host), "%s/%s", dir, sub);
        struct stat st;
        if (stat(host, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            write_files(out, dir, sub);
        } else {
            for (char *p = sub; *p; p++) {
                if (*p == '/') {
                    *p = '\\';
                }
            }
            fprintf(out, "file\t%s\t%s\n", sub, host);
        }
    }
    free_names(names, n);
}

/* 0.0.3: a mod's edit line ("<disc file> | <line start> | <new line>") written over the disc: the disc's
 * file with the first line starting with <line start> (after its spaces, any case) replaced, its line
 * ending kept, in work_dir/mod-edits/<mod>/; its line in the manifest */
static void write_edit(FILE *out, const NcDisc *disc, const char *work_dir, const char *mod, const char *edit)
{
    char line[320];
    copy(line, sizeof(line), edit);
    char *bar1 = strstr(line, " | "), *bar2 = bar1 ? strstr(bar1 + 3, " | ") : NULL;
    if (!bar1 || !bar2) {
        pshakux_log("mods:   mod %s: an edit line isn't \"<file> | <line start> | <new line>\": %s", mod, edit);
        return;
    }
    *bar1 = 0;
    *bar2 = 0;
    char *file = trim(line), *old = bar1 + 3, *neu = bar2 + 3;
    const size_t nold = strlen(old), nneu = strlen(neu);
    OgboxBuf b;
    memset(&b, 0, sizeof(b));
    if (!disc || !nold || !disc_file(disc, file, &b)) {
        pshakux_log("mods:   mod %s: %s isn't on the disc: its edit left out", mod, file);
        return;
    }
    /* the line that starts with <old> (after its spaces), to its line ending */
    uint8_t *at = NULL;
    size_t span = 0;
    for (size_t i = 0; i < b.size;) {
        size_t j = i;
        while (j < b.size && (b.data[j] == ' ' || b.data[j] == '\t')) {
            j++;
        }
        size_t e = i;
        while (e < b.size && b.data[e] != '\r' && b.data[e] != '\n') {
            e++;
        }
        if (e - j >= nold && !strncasecmp((const char *)b.data + j, old, nold)) {
            at = b.data + i;
            span = e - i;
            break;
        }
        i = e;
        while (i < b.size && (b.data[i] == '\r' || b.data[i] == '\n')) {
            i++;
        }
    }
    if (!at) {
        pshakux_log("mods:   mod %s: no line of the disc's %s starts with \"%s\": its edit left out", mod, file, old);
        free(b.data);
        return;
    }
    char dir[1024], host[1200];
    snprintf(dir, sizeof(dir), "%s/mod-edits", work_dir);
    mkdir(dir, 0777);
    snprintf(dir, sizeof(dir), "%s/mod-edits/%s", work_dir, mod);
    mkdir(dir, 0777);
    char flat[256];
    copy(flat, sizeof(flat), file);
    for (char *p = flat; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '_';
        }
    }
    snprintf(host, sizeof(host), "%s/%s", dir, flat);
    FILE *f = fopen(host, "wb");
    const size_t head = (size_t)(at - b.data), tail = b.size - head - span;
    const bool ok = f && fwrite(b.data, 1, head, f) == head && fwrite(neu, 1, nneu, f) == nneu &&
                    fwrite(at + span, 1, tail, f) == tail;
    if (f) {
        fclose(f);
    }
    free(b.data);
    if (!ok) {
        pshakux_log("mods:   mod %s: %s cannot be written (%s)", mod, host, strerror(errno));
        return;
    }
    char sub[256];
    copy(sub, sizeof(sub), file);
    for (char *p = sub; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
    fprintf(out, "file\t%s\t%s\n", sub, host);
    pshakux_log("mods:   mod %s: %s edited (the line \"%s...\" now \"%s\")", mod, file, old, neu);
}

static bool load_game(const char *key, char *dir, size_t n);

static bool write_manifest(const char *path, const char *game_dir, const NcDisc *disc, const char *key,
                           const char *work_dir)
{
    FILE *out = fopen(path, "w");
    if (!out) {
        pshakux_log("mods: the manifest %s cannot be written (%s)", path, strerror(errno));
        return false;
    }
    int refresh = 0;
    (void)game_dir;
    for (unsigned i = 0; i < g_nmods; i++) {
        if (g_mods[i].on) {
            char files[1024];
            snprintf(files, sizeof(files), "%s/%s/files", g_mods[i].dir, g_mods[i].id);
            write_files(out, files, "");
            for (unsigned e = 0; e < g_mods[i].nedits; e++) {
                write_edit(out, disc, work_dir, g_mods[i].id, g_mods[i].edit[e]);
            }
            if (g_mods[i].refresh > refresh) {
                refresh = g_mods[i].refresh;
            }
        }
    }
    /* 0.1.45: the XBE patches on (gamedata/patches.db): the patched files, as files of the disc */
    ogbox_patches_prepare(disc, key, work_dir, out);
    for (unsigned i = 0; i < g_nmods; i++) {
        /* a mod that is off too: a savestate saved with it on gets the disc's bytes back */
        const char *kind = g_mods[i].on ? "patch" : "patchoff";
        for (size_t k = 0; k < g_mods[i].npatches; k++) {
            fprintf(out, "%s\t%s\t%s\t%s\n", kind, g_mods[i].patches[k].xbe, g_mods[i].id, g_mods[i].patches[k].text);
        }
    }
    if (refresh > 0) {
        fprintf(out, "refresh\t%d\n", refresh);
    }
    for (unsigned i = 0; i < g_ncheats; i++) {
        fprintf(out, "cheat\t%d\t%d\t%s\n", g_cheats[i].on ? 1 : 0, g_cheats[i].once ? 1 : 0, g_cheats[i].name);
        for (size_t k = 0; k < g_cheats[i].ncode; k++) {
            fprintf(out, "code\t%s\n", g_cheats[i].code[k]);
        }
    }
    const bool ok = fclose(out) == 0;
    return ok;
}

void ogbox_mods_prepare(int disc_fd, const char *work_dir)
{
    char key[32];
    if (disc_fd < 0 || !game_key(disc_fd, key, sizeof(key))) {
        pshakux_log("mods: the game's key (its default.xbe's certificate) cannot be read: no mods");
        return;
    }
    char game_dir[1024] = "";
    if (load_game(key, game_dir, sizeof(game_dir))) {
        pshakux_log("mods: %s: %u mods and %u cheats (mods/%s and mods/<title ID>)", key, g_nmods, g_ncheats, key);
    } else {
        pshakux_log("mods: %s: none in %s/mods", key, pshakux_title_dir());
    }
    for (unsigned i = 0; i < g_nmods; i++) {
        pshakux_log("mods:   mod %s: %s (%zu patch lines%s)", g_mods[i].id, g_mods[i].name, g_mods[i].npatches,
                    g_mods[i].refresh ? ", asks for a vblank at its refresh rate" : "");
    }
    for (unsigned i = 0; i < g_ncheats; i++) {
        pshakux_log("mods:   cheat %s (%zu lines)", g_cheats[i].name, g_cheats[i].ncode);
    }
    read_choices(key, pshakux_title_dir());
    NcDisc disc;
    memset(&disc, 0, sizeof(disc));
    const bool have_disc = nc_disc_open(&disc, disc_fd); /* (not closed: the descriptor is the core's) */
    /* 0.0.2: the release check (fit_check): what was made for another release of the game stays off,
     * whole (the core would write the lines whose bytes it finds and skip the rest) */
    if (have_disc && (g_nmods || g_ncheats || ogbox_patches_count(key) > 0)) {
        fit_check(&disc, key);
        for (unsigned i = 0; i < g_nmods; i++) {
            const char *why;
            if (g_mods[i].on && fit_of_mod(key, g_mods[i].id, &why) == OGBOX_FIT_NO) {
                g_mods[i].on = false;
                pshakux_log("mods:   mod %s is on but left out: made for another release (%s)", g_mods[i].id, why);
            }
        }
        if (g_fit.cheats == OGBOX_FIT_NO) {
            unsigned off = 0;
            for (unsigned i = 0; i < g_ncheats; i++) {
                off += g_cheats[i].on;
                g_cheats[i].on = false;
            }
            if (off) {
                pshakux_log("mods:   %u cheats on but left out: made for another release (%s)", off, g_fit.cheats_why);
            }
        }
    }
    unsigned on = 0;
    for (unsigned i = 0; i < g_nmods; i++) {
        on += g_mods[i].on;
    }
    for (unsigned i = 0; i < g_ncheats; i++) {
        on += g_cheats[i].on;
    }
    if (!on) {
        pshakux_log("mods: none are on; to turn one on, add a line like \"%s mod %s\" to /data/ogbox/mods.txt", key,
                    g_nmods ? g_mods[0].id : "<mod>");
        /* (the manifest is still written: the mods that are off give a savestate its bytes back) */
    }
    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s/mods-run.txt", work_dir);
    if (write_manifest(manifest, game_dir[0] ? game_dir : NULL, have_disc ? &disc : NULL, key, work_dir)) {
        nc_mods_set_manifest(manifest);
    }
    disc_release(&disc);
}

/* ---- OGBox 0.1.26: the game list's questions ---- */

int ps5vfs_is_virtual(const char *path);
int ps5vfs_disc_open(const char *path);
void ps5vfs_disc_close(int fd);

/* 0.0.3: a disc image opened, on a share or not; one split in parts (game.1.iso, game.2.iso, ...:
 * a FAT drive's 4 GiB limit, Repackinator's .1.cci) with its other parts, for hakuX's reader
 * (native/core/pack.h). */
static int part_open(const char *path)
{
    return ps5vfs_is_virtual(path) ? ps5vfs_disc_open(path) : open(path, O_RDONLY | O_CLOEXEC);
}

int ogbox_image_open(const char *image)
{
    static bool closer;
    if (!closer) {
        closer = true;
        nc_pack_set_closer(ps5vfs_disc_close);
    }
    const int fd = part_open(image);
    if (fd < 0) {
        return fd;
    }
    /* "<stem>.1.<ext>": its parts .2, .3, ... while they are there */
    const char *ext = strrchr(image, '.');
    const size_t n = strlen(image);
    if (ext && ext - image >= 3 && ext[-2] == '.' && ext[-1] == '1' && n < 1000) {
        int more[15];
        int k = 0;
        char part[1024];
        for (; k < 15; k++) {
            snprintf(part, sizeof(part), "%.*s%d%s", (int)(ext - 1 - image), image, k + 2, ext);
            if ((more[k] = part_open(part)) < 0) {
                break;
            }
        }
        if (k > 0) {
            nc_pack_register_slices(fd, more, k);
            pshakux_log("disc: %s: %d more part%s", image, k, k == 1 ? "" : "s");
        }
    }
    return fd;
}

void ogbox_image_close(int fd)
{
    if (fd >= 0) {
        nc_pack_unregister(fd);
        ps5vfs_disc_close(fd);  /* (a plain descriptor: close) */
    }
}

/* The catalog of the last ogbox_mods_catalog (or ogbox_mods_prepare), freed. */
static void catalog_reset(void)
{
    for (unsigned i = 0; i < g_nmods; i++) {
        for (size_t k = 0; k < g_mods[i].npatches; k++) {
            free(g_mods[i].patches[k].xbe);
            free(g_mods[i].patches[k].text);
        }
        free(g_mods[i].patches);
    }
    for (unsigned i = 0; i < g_ncheats; i++) {
        for (size_t k = 0; k < g_cheats[i].ncode; k++) {
            free(g_cheats[i].code[k]);
        }
        free(g_cheats[i].code);
    }
    memset(g_mods, 0, sizeof(g_mods));
    memset(g_cheats, 0, sizeof(g_cheats));
    g_nmods = 0;
    g_ncheats = 0;
    g_ncheat_crc = 0;
}

/* The title folder's mods/<key> (any case), or false. */
static bool find_game_dir(const char *key, char *out, size_t n);

/* 0.0.3: a game's mods and cheats: mods/<title ID>.<version> (a release's: its mods and cheats), then
 * mods/<title ID> (every release's: its mods, those the release's folder hasn't one of the same name).
 * A game fix that changes a disc file's line or adds files fits every release of a game; byte patches
 * and cheats are a release's. dir: the release's folder ("" none). Whether either was found. */
static bool load_game(const char *key, char *dir, size_t n)
{
    bool any = false;
    dir[0] = 0;
    if (find_game_dir(key, dir, n)) {
        any = true;
        size_t nmods = 0;
        char **ids = list_dir(dir, &nmods, true);
        for (size_t i = 0; i < nmods; i++) {
            load_mod(dir, ids[i]);
        }
        free_names(ids, nmods);
        char cheats[1100];
        snprintf(cheats, sizeof(cheats), "%s/cheats.txt", dir);
        load_cheats(cheats);
    } else {
        dir[0] = 0;
    }
    char title[32];
    copy(title, sizeof(title), key);
    char *dot = strchr(title, '.');
    if (dot) {
        *dot = 0;
        char all[1024];
        if (find_game_dir(title, all, sizeof(all))) {
            any = true;
            size_t nmods = 0;
            char **ids = list_dir(all, &nmods, true);
            for (size_t i = 0; i < nmods; i++) {
                bool have = false;
                for (unsigned k = 0; k < g_nmods; k++) {
                    have = have || !strcasecmp(g_mods[k].id, ids[i]);
                }
                if (!have) {
                    load_mod(all, ids[i]);
                }
            }
            free_names(ids, nmods);
        }
    }
    return any;
}

static bool find_game_dir(const char *key, char *out, size_t n)
{
    char mods_dir[1024];
    snprintf(mods_dir, sizeof(mods_dir), "%s/mods", pshakux_title_dir());
    size_t nfolders = 0;
    char **folders = list_dir(mods_dir, &nfolders, true);
    bool found = false;
    for (size_t i = 0; i < nfolders && !found; i++) {
        if (!strcasecmp(folders[i], key)) {
            snprintf(out, n, "%s/%s", mods_dir, folders[i]);
            found = true;
        }
    }
    free_names(folders, nfolders);
    return found;
}

bool ogbox_game_info(const char *image, OgboxGameInfo *info)
{
    memset(info, 0, sizeof(*info));
    const int fd = ogbox_image_open(image);
    if (fd < 0) {
        return false;
    }
    NcDisc disc;
    memset(&disc, 0, sizeof(disc));
    NcDiscFile f;
    uint8_t h[0x1000];
    bool ok = nc_disc_open(&disc, fd) && nc_disc_find(&disc, "\\default.xbe", &f) && f.size >= sizeof(h) &&
              nc_disc_read(&disc, &f, 0, h, sizeof(h));
    if (ok) {
        const uint32_t base = le32(h + 0x104), cert_va = le32(h + 0x118);
        const uint32_t off = cert_va - base;
        ok = cert_va >= base && off <= sizeof(h) - 0xB0;
        if (ok) {
            info->title_id = le32(h + off + 8);
            info->version = le32(h + off + 0xAC);
            info->regions = le32(h + off + 0xA0);
            info->xbe_time = le32(h + 0x114);
            /* the title's name: 40 UTF-16LE characters at +0x0C; what is not ASCII becomes '?' */
            size_t n = 0;
            for (unsigned i = 0; i < 40 && n + 1 < sizeof(info->title); i++) {
                const uint16_t c = (uint16_t)(h[off + 0x0C + 2 * i] | h[off + 0x0D + 2 * i] << 8);
                if (!c) {
                    break;
                }
                info->title[n++] = c >= 0x20 && c < 0x7F ? (char)c : '?';
            }
            info->title[n] = 0;
            ok = info->title_id != 0;
            if (ok) {
                snprintf(info->key, sizeof(info->key), "%08X.%u", info->title_id, info->version);
            }
        }
    }
    disc_release(&disc);
    ogbox_image_close(fd);
    info->ok = ok;
    return ok;
}

bool ogbox_game_xbe_crc(const char *image, uint32_t *crc)
{
    *crc = 0;
    const int fd = ogbox_image_open(image);
    if (fd < 0) {
        return false;
    }
    NcDisc disc;
    memset(&disc, 0, sizeof(disc));
    NcDiscFile f;
    bool ok = nc_disc_open(&disc, fd) && nc_disc_find(&disc, "\\default.xbe", &f) && f.size && f.size <= (64u << 20);
    uint8_t *buf = ok ? malloc(f.size) : NULL;
    ok = ok && buf;
    for (uint32_t at = 0; ok && at < f.size;) {
        const uint32_t n = f.size - at < (1u << 20) ? f.size - at : (1u << 20);
        ok = nc_disc_read(&disc, &f, at, buf + at, n);
        at += n;
    }
    if (ok) {
        *crc = ogbox_crc32(buf, f.size);
    }
    free(buf);
    disc_release(&disc);
    ogbox_image_close(fd);
    return ok;
}

bool ogbox_mods_check_disc(const char *image, const char *key, uint32_t *crc)
{
    if (crc) {
        *crc = 0;
    }
    if (!image || !key || !key[0]) {
        return false;
    }
    const int fd = ogbox_image_open(image);
    if (fd < 0) {
        return false;
    }
    NcDisc disc;
    memset(&disc, 0, sizeof(disc));
    const bool ok = nc_disc_open(&disc, fd);
    if (ok) {
        catalog_reset();
        char game_dir[1024];
        load_game(key, game_dir, sizeof(game_dir));
        fit_check(&disc, key);
        catalog_reset();
        if (crc) {
            *crc = g_fit.crc;
        }
    }
    disc_release(&disc);
    ogbox_image_close(fd);
    return ok;
}

int ogbox_mods_catalog(const char *key, OgboxModEntry *out, int max)
{
    catalog_reset();
    char game_dir[1024];
    if (!key || !key[0]) {
        return 0;
    }
    if (load_game(key, game_dir, sizeof(game_dir))) {
        read_choices(key, pshakux_title_dir());
    }
    int n = 0;
    /* 0.0.3: the game fixes first, then the other mods */
    for (unsigned pass = 0; pass < 2; pass++)
    for (unsigned i = 0; i < g_nmods && n < max; i++) {
        if (g_mods[i].gamefix != (pass == 0)) {
            continue;
        }
        OgboxModEntry *e = &out[n++];
        memset(e, 0, sizeof(*e));
        copy(e->id, sizeof(e->id), g_mods[i].id);
        copy(e->name, sizeof(e->name), g_mods[i].name);
        copy(e->description, sizeof(e->description), g_mods[i].description);
        e->refresh = g_mods[i].refresh;
        e->on = g_mods[i].on;
        e->gamefix = g_mods[i].gamefix;
        const char *why;
        e->fit = fit_of_mod(key, g_mods[i].id, &why);
        copy(e->fit_why, sizeof(e->fit_why), why);
    }
    /* 0.1.45: the XBE patches */
    {
        static OgboxPatch patches[256];
        const int np = ogbox_patches_catalog(key, patches, 256);
        for (int i = 0; i < np && n < max; i++, n++) {
            OgboxModEntry *e = &out[n];
            memset(e, 0, sizeof(*e));
            copy(e->id, sizeof(e->id), patches[i].id);
            copy(e->name, sizeof(e->name), patches[i].name);
            copy(e->description, sizeof(e->description), patches[i].note);
            copy(e->category, sizeof(e->category), patches[i].category);
            copy(e->author, sizeof(e->author), patches[i].author);
            copy(e->source, sizeof(e->source), patches[i].source);
            e->patch = true;
            e->on = patches[i].on;
            e->fit = patches[i].fit;
            copy(e->fit_why, sizeof(e->fit_why), patches[i].fit_why);
        }
    }
    for (unsigned i = 0; i < g_ncheats && n < max; i++, n++) {
        OgboxModEntry *e = &out[n];
        memset(e, 0, sizeof(*e));
        copy(e->id, sizeof(e->id), g_cheats[i].name);
        copy(e->name, sizeof(e->name), g_cheats[i].name);
        e->cheat = true;
        e->once = g_cheats[i].once;
        e->on = g_cheats[i].on;
        e->fit = fit_valid(key) ? g_fit.cheats : g_ncheat_crc ? OGBOX_FIT_UNKNOWN : OGBOX_FIT_UNCHECKED;
        copy(e->fit_why, sizeof(e->fit_why), fit_valid(key) ? g_fit.cheats_why : "");
    }
    catalog_reset();
    return n;
}

bool ogbox_mods_choose(const char *key, bool cheat, const char *name, bool on)
{
    return ogbox_mods_choose_kind(key, cheat ? "cheat" : "mod", name, on);
}

bool ogbox_mods_choose_kind(const char *key, const char *kind, const char *name, bool on)
{
    const char *path = "/data/ogbox/mods.txt";
    char *text = read_all(path);
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    FILE *out = fopen(tmp, "w");
    if (!out) {
        free(text);
        pshakux_log("mods: %s cannot be written (%s)", tmp, strerror(errno));
        return false;
    }
    if (text) {
        for (char *save, *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char copyline[1024];
            copy(copyline, sizeof(copyline), line);
            char *s = content(copyline);
            bool same = false;
            if (s) {
                char *w1 = s, *rest = s;
                while (*rest && !isspace((unsigned char)*rest)) {
                    rest++;
                }
                if (*rest) {
                    *rest++ = 0;
                    rest = trim(rest);
                    char *w3 = rest;
                    while (*w3 && !isspace((unsigned char)*w3)) {
                        w3++;
                    }
                    if (*w3) {
                        *w3++ = 0;
                    }
                    w3 = trim(w3);
                    same = !strcasecmp(w1, key) && !strcasecmp(rest, kind) && !strcasecmp(w3, name);
                }
            }
            if (!same) {
                fprintf(out, "%s\n", line);
            }
        }
        free(text);
    } else {
        fprintf(out, "# OGBox: the mods, patches and cheats that are on, one a line: <game> mod <id> / <game> patch <id> / "
                     "<game> cheat <name>\n");
    }
    if (on) {
        fprintf(out, "%s %s %s\n", key, kind, name);
    }
    if (fclose(out) != 0 || rename(tmp, path) != 0) {
        pshakux_log("mods: %s cannot be written (%s)", path, strerror(errno));
        return false;
    }
    pshakux_log("mods: %s %s %s: %s", key, kind, name, on ? "on" : "off");
    return true;
}

/* ---- OGBox 0.1.54: one value a game (the trainer chosen, its options) ---- */

/* A line "<key> <kind> <value>": its value, else NULL (in line, which is changed). */
static char *line_value(char *line, const char *key, const char *kind)
{
    char *s = content(line);
    if (!s) {
        return NULL;
    }
    char *w1 = s, *rest = s;
    while (*rest && !isspace((unsigned char)*rest)) {
        rest++;
    }
    if (!*rest) {
        return NULL;
    }
    *rest++ = 0;
    rest = trim(rest);
    char *value = rest;
    while (*value && !isspace((unsigned char)*value)) {
        value++;
    }
    if (*value) {
        *value++ = 0;
    }
    value = trim(value);
    return !strcasecmp(w1, key) && !strcasecmp(rest, kind) ? value : NULL;
}

bool ogbox_mods_get(const char *key, const char *kind, char *value, size_t n)
{
    char *text = read_all("/data/ogbox/mods.txt");
    bool found = false;
    if (text) {
        for (char *save, *line = strtok_r(text, "\n", &save); line && !found; line = strtok_r(NULL, "\n", &save)) {
            const char *v = line_value(line, key, kind);
            if (v) {
                copy(value, n, v);
                found = true;
            }
        }
        free(text);
    }
    if (!found && n) {
        value[0] = 0;
    }
    return found;
}

bool ogbox_mods_set(const char *key, const char *kind, const char *value)
{
    const char *path = "/data/ogbox/mods.txt";
    char *text = read_all(path);
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    FILE *out = fopen(tmp, "w");
    if (!out) {
        free(text);
        pshakux_log("mods: %s cannot be written (%s)", tmp, strerror(errno));
        return false;
    }
    if (text) {
        for (char *save, *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            char copyline[1024];
            copy(copyline, sizeof(copyline), line);
            if (!line_value(copyline, key, kind)) {
                fprintf(out, "%s\n", line);
            }
        }
        free(text);
    } else {
        fprintf(out, "# OGBox: the mods, patches and cheats that are on, one a line: <game> mod <id> / <game> patch <id> / "
                     "<game> cheat <name> / <game> trainer <file> / <game> trainer-options <0 and 1 an option>\n");
    }
    if (value && value[0]) {
        fprintf(out, "%s %s %s\n", key, kind, value);
    }
    if (fclose(out) != 0 || rename(tmp, path) != 0) {
        pshakux_log("mods: %s cannot be written (%s)", path, strerror(errno));
        return false;
    }
    pshakux_log("mods: %s %s: %s", key, kind, value && value[0] ? value : "none");
    return true;
}

/* ---- OGBox 0.0.3: the title image (core/ogbox_xpr.h) ---- */

bool ogbox_game_title_image(const char *image, uint8_t **rgba, int *w, int *h)
{
    *rgba = NULL;
    const int fd = ogbox_image_open(image);
    if (fd < 0) {
        return false;
    }
    NcDisc disc;
    memset(&disc, 0, sizeof(disc));
    NcDiscFile f;
    bool ok = false;
    uint8_t *hdr = NULL, *sec = NULL;
    if (nc_disc_open(&disc, fd) && nc_disc_find(&disc, "\\default.xbe", &f) && f.size >= 0x180) {
        uint8_t h0[0x180];
        if (nc_disc_read(&disc, &f, 0, h0, sizeof(h0)) && !memcmp(h0, "XBEH", 4)) {
            const uint32_t base = le32(h0 + 0x104), hsize = le32(h0 + 0x108);
            const uint32_t nsec = le32(h0 + 0x11C), sva = le32(h0 + 0x120);
            if (hsize >= 0x180 && hsize <= (1u << 20) && hsize <= f.size && nsec && nsec < 1024 && (hdr = malloc(hsize)) &&
                nc_disc_read(&disc, &f, 0, hdr, hsize)) {
                const uint32_t so = sva - base;
                for (uint32_t i = 0; i < nsec && so + (i + 1) * 56 <= hsize; i++) {
                    const uint8_t *s = hdr + so + i * 56;
                    const uint32_t raw = le32(s + 12), rsize = le32(s + 16), name = le32(s + 20) - base;
                    if (name + 10 > hsize || memcmp(hdr + name, "$$XTIMAGE", 10) != 0) {
                        continue;
                    }
                    if (rsize >= 32 && rsize <= (4u << 20) && (uint64_t)raw + rsize <= f.size && (sec = malloc(rsize)) &&
                        nc_disc_read(&disc, &f, raw, sec, rsize)) {
                        ok = ogbox_xpr_decode(sec, rsize, rgba, w, h);
                    }
                    break;
                }
            }
        }
    }
    free(hdr);
    free(sec);
    disc_release(&disc);
    ogbox_image_close(fd);
    return ok;
}
