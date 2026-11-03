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

#include "platform/ps5_bootlog.h"

#include "native/core/mods.h"
#include "native/core/xdvdfs.h"
#include "native/core/chd.h"

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

/* 0.1.59: a disc opened here on a descriptor someone else closes: its CHD reader (hunk cache and
 * all) freed, the descriptor left open (nc_disc_close would close it too) */
static void disc_release(NcDisc *d)
{
    if (d->chd) {
        nc_chd_close(d->chd);
        d->chd = NULL;
    }
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

static bool write_manifest(const char *path, const char *game_dir, const NcDisc *disc, const char *key,
                           const char *work_dir)
{
    FILE *out = fopen(path, "w");
    if (!out) {
        pshakux_log("mods: the manifest %s cannot be written (%s)", path, strerror(errno));
        return false;
    }
    int refresh = 0;
    for (unsigned i = 0; i < g_nmods && game_dir; i++) {
        if (g_mods[i].on) {
            char files[1024];
            snprintf(files, sizeof(files), "%s/%s/files", game_dir, g_mods[i].id);
            write_files(out, files, "");
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
    char game_dir[1024] = "";
    if (folder) {
        snprintf(game_dir, sizeof(game_dir), "%s/%s", mods_dir, folder);
    } else {
        pshakux_log("mods: %s: none in %s", key, mods_dir);
    }
    free_names(folders, nfolders);

    if (game_dir[0]) {
        size_t nmods = 0;
        char **ids = list_dir(game_dir, &nmods, true);
        for (size_t i = 0; i < nmods; i++) {
            load_mod(game_dir, ids[i]);
        }
        free_names(ids, nmods);
        char cheats[1100];
        snprintf(cheats, sizeof(cheats), "%s/cheats.txt", game_dir);
        load_cheats(cheats);
        pshakux_log("mods: %s: %u mods and %u cheats in %s", key, g_nmods, g_ncheats, game_dir);
    }
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
    NcDisc disc;
    memset(&disc, 0, sizeof(disc));
    const bool have_disc = nc_disc_open(&disc, disc_fd); /* (not closed: the descriptor is the core's) */
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
}

/* The title folder's mods/<key> (any case), or false. */
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
    const bool virt = ps5vfs_is_virtual(image) != 0;
    const int fd = virt ? ps5vfs_disc_open(image) : open(image, O_RDONLY | O_CLOEXEC);
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
    if (virt) {
        ps5vfs_disc_close(fd);
    } else {
        close(fd);
    }
    info->ok = ok;
    return ok;
}

bool ogbox_game_xbe_crc(const char *image, uint32_t *crc)
{
    *crc = 0;
    const bool virt = ps5vfs_is_virtual(image) != 0;
    const int fd = virt ? ps5vfs_disc_open(image) : open(image, O_RDONLY | O_CLOEXEC);
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
    if (virt) {
        ps5vfs_disc_close(fd);
    } else {
        close(fd);
    }
    return ok;
}

int ogbox_mods_catalog(const char *key, OgboxModEntry *out, int max)
{
    catalog_reset();
    char game_dir[1024];
    if (!key || !key[0]) {
        return 0;
    }
    if (find_game_dir(key, game_dir, sizeof(game_dir))) {
        size_t nmods = 0;
        char **ids = list_dir(game_dir, &nmods, true);
        for (size_t i = 0; i < nmods; i++) {
            load_mod(game_dir, ids[i]);
        }
        free_names(ids, nmods);
        char cheats[1100];
        snprintf(cheats, sizeof(cheats), "%s/cheats.txt", game_dir);
        load_cheats(cheats);
        read_choices(key, pshakux_title_dir());
    }
    int n = 0;
    for (unsigned i = 0; i < g_nmods && n < max; i++, n++) {
        OgboxModEntry *e = &out[n];
        memset(e, 0, sizeof(*e));
        copy(e->id, sizeof(e->id), g_mods[i].id);
        copy(e->name, sizeof(e->name), g_mods[i].name);
        copy(e->description, sizeof(e->description), g_mods[i].description);
        e->refresh = g_mods[i].refresh;
        e->on = g_mods[i].on;
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
