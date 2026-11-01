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

#include "platform/ps5_bootlog.h"

#include "native/core/mods.h"
#include "native/core/xdvdfs.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_MODS 32
#define MAX_CHEATS 256

typedef struct Line {
    char *xbe; /* the XBE section it belongs to */
    char *text;
} Line;

typedef struct Mod {
    char id[128];
    char name[128];
    char description[256];
    int refresh;
    bool on;
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
    if (!nc_disc_find(&disc, "\\default.xbe", &f) || f.size < sizeof(h) ||
        !nc_disc_read(&disc, &f, 0, h, sizeof(h))) {
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

static bool write_manifest(const char *path, const char *game_dir)
{
    FILE *out = fopen(path, "w");
    if (!out) {
        pshakux_log("mods: the manifest %s cannot be written (%s)", path, strerror(errno));
        return false;
    }
    int refresh = 0;
    for (unsigned i = 0; i < g_nmods; i++) {
        if (g_mods[i].on) {
            char files[1024];
            snprintf(files, sizeof(files), "%s/%s/files", game_dir, g_mods[i].id);
            write_files(out, files, "");
            if (g_mods[i].refresh > refresh) {
                refresh = g_mods[i].refresh;
            }
        }
    }
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
    char mods_dir[1024];
    snprintf(mods_dir, sizeof(mods_dir), "%s/mods", pshakux_title_dir());
    size_t nfolders = 0;
    char **folders = list_dir(mods_dir, &nfolders, true);
    const char *folder = NULL;
    for (size_t i = 0; i < nfolders; i++) {
        if (!strcasecmp(folders[i], key)) {
            folder = folders[i];
        }
    }
    if (!folder) {
        pshakux_log("mods: %s: none in %s", key, mods_dir);
        free_names(folders, nfolders);
        return;
    }
    char game_dir[1024];
    snprintf(game_dir, sizeof(game_dir), "%s/%s", mods_dir, folder);
    free_names(folders, nfolders);

    size_t nmods = 0;
    char **ids = list_dir(game_dir, &nmods, true);
    for (size_t i = 0; i < nmods; i++) {
        load_mod(game_dir, ids[i]);
    }
    free_names(ids, nmods);
    char cheats[1024];
    snprintf(cheats, sizeof(cheats), "%s/cheats.txt", game_dir);
    load_cheats(cheats);

    pshakux_log("mods: %s: %u mods and %u cheats in %s", key, g_nmods, g_ncheats, game_dir);
    for (unsigned i = 0; i < g_nmods; i++) {
        pshakux_log("mods:   mod %s: %s (%zu patch lines%s)", g_mods[i].id, g_mods[i].name, g_mods[i].npatches,
                    g_mods[i].refresh ? ", asks for a vblank at its refresh rate" : "");
    }
    for (unsigned i = 0; i < g_ncheats; i++) {
        pshakux_log("mods:   cheat %s (%zu lines)", g_cheats[i].name, g_cheats[i].ncode);
    }
    read_choices(key, pshakux_title_dir());
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
    if (write_manifest(manifest, game_dir)) {
        nc_mods_set_manifest(manifest);
    }
}
