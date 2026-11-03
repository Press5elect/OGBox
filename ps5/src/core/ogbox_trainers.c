/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The trainers' index and choices (ogbox_trainers.h). The files are read with hakuX's reader
 * (native/core/trainer.c, XBMC4Xbox's format).
 */
#include "core/ogbox_trainers.h"
#include "core/ogbox_mods.h"

#include "platform/ps5_bootlog.h"

#include "native/core/trainer.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_ROOTS   8
#define MAX_FILES   20000
#define MAX_DEPTH   6
#define FLAG_XBTF   1u
#define FLAG_BROKEN 2u

typedef struct Entry {
    int root;
    char *rel;
    uint64_t size;
    int64_t mtime;
    uint32_t ids[3], xbe_time, hash;
    unsigned noptions, flags;
    char name[OGBOX_TRAINER_TEXT];
    char author[OGBOX_TRAINER_TEXT];
} Entry;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_roots[MAX_ROOTS][512];
static int g_nroots;
static Entry *g_list;                   /* the last scan's, sorted by name */
static size_t g_count;
static int g_state;                     /* 0 never read, 1 reading, 2 read */
static unsigned g_generation;

void ogbox_trainers_set_roots(const char *const *roots, int n)
{
    pthread_mutex_lock(&g_lock);
    g_nroots = 0;
    for (int i = 0; i < n && g_nroots < MAX_ROOTS; i++) {
        bool same = false;
        for (int k = 0; k < g_nroots; k++) {
            same |= !strcmp(g_roots[k], roots[i]);
        }
        if (roots[i] && roots[i][0] && !same) {
            snprintf(g_roots[g_nroots++], sizeof(g_roots[0]), "%s", roots[i]);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

int ogbox_trainers_state(unsigned *generation, unsigned *found)
{
    pthread_mutex_lock(&g_lock);
    const int s = g_state;
    if (generation) {
        *generation = g_generation;
    }
    if (found) {
        *found = (unsigned)g_count;
    }
    pthread_mutex_unlock(&g_lock);
    return s;
}

/* ---- the index file ---- */

static void free_list(Entry *list, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        free(list[i].rel);
    }
    free(list);
}

/* tabs and line ends out of a text kept in the index */
static void clean(char *s)
{
    for (; *s; s++) {
        if (*s == '\t' || *s == '\n' || *s == '\r') {
            *s = ' ';
        }
    }
}

typedef struct Old {
    char *path;                         /* root + "/" + rel */
    Entry e;
} Old;

static int by_path(const void *a, const void *b)
{
    return strcmp(((const Old *)a)->path, ((const Old *)b)->path);
}

/* The index read: its entries by path. */
static Old *read_index(const char *path, size_t *count)
{
    *count = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }
    size_t cap = 0;
    Old *old = NULL;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char *field[12];
        int nf = 0;
        char *s = line;
        s[strcspn(s, "\r\n")] = 0;
        while (nf < 12) {
            field[nf++] = s;
            char *t = strchr(s, '\t');
            if (!t) {
                break;
            }
            *t = 0;
            s = t + 1;
        }
        if (nf != 12 || line[0] == '#') {
            continue;
        }
        if (*count == cap) {
            cap = cap ? cap * 2 : 1024;
            Old *grown = realloc(old, cap * sizeof(*old));
            if (!grown) {
                break;
            }
            old = grown;
        }
        Old *o = &old[*count];
        memset(o, 0, sizeof(*o));
        o->e.size = strtoull(field[0], NULL, 10);
        o->e.mtime = strtoll(field[1], NULL, 10);
        for (int i = 0; i < 3; i++) {
            o->e.ids[i] = (uint32_t)strtoul(field[2 + i], NULL, 16);
        }
        o->e.xbe_time = (uint32_t)strtoul(field[5], NULL, 16);
        o->e.noptions = (unsigned)strtoul(field[6], NULL, 10);
        o->e.flags = (unsigned)strtoul(field[7], NULL, 10);
        o->e.hash = (uint32_t)strtoul(field[8], NULL, 16);
        snprintf(o->e.name, sizeof(o->e.name), "%s", field[9]);
        snprintf(o->e.author, sizeof(o->e.author), "%s", field[10]);
        o->path = strdup(field[11]);
        if (o->path) {
            (*count)++;
        }
    }
    fclose(f);
    qsort(old, *count, sizeof(*old), by_path);
    return old;
}

static bool write_index(const char *path, const Entry *list, size_t n)
{
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        return false;
    }
    fprintf(f, "# OGBox's trainers index: size, date, title IDs, XBE date, options, flags, hash, name, author, file\n");
    for (size_t i = 0; i < n; i++) {
        const Entry *e = &list[i];
        fprintf(f, "%llu\t%lld\t%08x\t%08x\t%08x\t%08x\t%u\t%u\t%08x\t%s\t%s\t%s/%s\n", (unsigned long long)e->size,
                (long long)e->mtime, e->ids[0], e->ids[1], e->ids[2], e->xbe_time, e->noptions, e->flags, e->hash,
                e->name, e->author, g_roots[e->root], e->rel);
    }
    const bool ok = fclose(f) == 0 && rename(tmp, path) == 0;
    return ok;
}

/* ---- the scan ---- */

typedef struct Scan {
    Entry *list;
    size_t n, cap;
    const Old *old;
    size_t nold;
    unsigned parsed, reused, broken;
} Scan;

static bool is_trainer(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".etm") || !strcasecmp(dot, ".xbtf"));
}

static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

/* A file read and parsed into e (broken: flagged). */
static void parse_file(const char *path, Entry *e)
{
    NcTrainerInfo info;
    memset(&info, 0, sizeof(info));
    FILE *f = fopen(path, "rb");
    uint8_t *buf = malloc(NC_TRAINER_MAX_FILE + 1);
    size_t n = 0;
    if (f && buf) {
        n = fread(buf, 1, NC_TRAINER_MAX_FILE + 1, f);
    }
    if (f) {
        fclose(f);
    }
    const char *dot = strrchr(path, '.');
    const bool xbtf = dot && !strcasecmp(dot, ".xbtf");
    size_t off = 0, size = 0;
    char why[160];
    if (buf && n && nc_trainer_parse(buf, n, xbtf, &info, &off, &size, why, sizeof(why))) {
        memcpy(e->ids, info.title_ids, sizeof(e->ids));
        e->xbe_time = info.xbe_time;
        e->noptions = info.noptions;
        e->flags = xbtf ? FLAG_XBTF : 0;
        /* the trainer's own bytes, its option bytes left out (the same trainer in an .etm and an
         * .xbtf, or with other defaults, is one) */
        if (info.options_off < size) {
            memset(buf + off + info.options_off, 0, size - info.options_off < info.noptions ? size - info.options_off : info.noptions);
        }
        e->hash = fnv(buf + off, size);
        snprintf(e->name, sizeof(e->name), "%s", info.name);
        snprintf(e->author, sizeof(e->author), "%s", info.author);
        clean(e->name);
        clean(e->author);
    } else {
        e->flags = FLAG_BROKEN | (xbtf ? FLAG_XBTF : 0);
    }
    free(buf);
}

static void add(Scan *s, int root, const char *rel, const char *path, const struct stat *st)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 1024;
        Entry *grown = realloc(s->list, s->cap * sizeof(Entry));
        if (!grown) {
            return;
        }
        s->list = grown;
    }
    Entry *e = &s->list[s->n];
    memset(e, 0, sizeof(*e));
    e->root = root;
    e->rel = strdup(rel);
    if (!e->rel) {
        return;
    }
    e->size = (uint64_t)st->st_size;
    e->mtime = (int64_t)st->st_mtime;
    /* the index's, if the file is the same */
    const Old key = { (char *)path, { 0 } };
    const Old *o = s->nold ? bsearch(&key, s->old, s->nold, sizeof(Old), by_path) : NULL;
    if (o && o->e.size == e->size && o->e.mtime == e->mtime) {
        memcpy(e->ids, o->e.ids, sizeof(e->ids));
        e->xbe_time = o->e.xbe_time;
        e->noptions = o->e.noptions;
        e->flags = o->e.flags;
        e->hash = o->e.hash;
        memcpy(e->name, o->e.name, sizeof(e->name));
        memcpy(e->author, o->e.author, sizeof(e->author));
        s->reused++;
    } else {
        parse_file(path, e);
        s->parsed++;
    }
    s->broken += (e->flags & FLAG_BROKEN) != 0;
    s->n++;
}

static void walk(Scan *s, int root, const char *dir, const char *rel, int depth)
{
    if (depth > MAX_DEPTH || s->n >= MAX_FILES) {
        return;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && s->n < MAX_FILES) {
        if (de->d_name[0] == '.') {
            continue;
        }
        char path[1024], sub[768];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        snprintf(sub, sizeof(sub), "%s%s%s", rel, rel[0] ? "/" : "", de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            walk(s, root, path, sub, depth + 1);
        } else if (S_ISREG(st.st_mode) && is_trainer(de->d_name) && st.st_size > 0) {
            add(s, root, sub, path, &st);
        }
    }
    closedir(d);
}

static int by_name(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    const int c = strcasecmp(x->name, y->name);
    return c ? c : strcasecmp(x->rel, y->rel);
}

int ogbox_trainers_scan(const char *index_path)
{
    pthread_mutex_lock(&g_lock);
    if (g_state == 1) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    g_state = 1;
    char roots[MAX_ROOTS][512];
    const int nroots = g_nroots;
    memcpy(roots, g_roots, sizeof(roots));
    pthread_mutex_unlock(&g_lock);

    Scan s;
    memset(&s, 0, sizeof(s));
    size_t nold = 0;
    Old *old = index_path ? read_index(index_path, &nold) : NULL;
    s.old = old;
    s.nold = nold;
    for (int r = 0; r < nroots; r++) {
        walk(&s, r, roots[r], "", 0);
    }
    for (size_t i = 0; i < nold; i++) {
        free(old[i].path);
    }
    free(old);
    qsort(s.list, s.n, sizeof(Entry), by_name);
    if (index_path && (s.parsed || s.n != nold)) {
        pthread_mutex_lock(&g_lock);   /* (write_index reads the roots) */
        if (!write_index(index_path, s.list, s.n)) {
            pshakux_log("trainers: the index %s can't be written (%s)", index_path, strerror(errno));
        }
        pthread_mutex_unlock(&g_lock);
    }
    pshakux_log("trainers: %zu in %d folders (%u read, %u from the index, %u not trainers)", s.n, nroots, s.parsed,
                s.reused, s.broken);
    pthread_mutex_lock(&g_lock);
    free_list(g_list, g_count);
    g_list = s.list;
    g_count = s.n;
    g_state = 2;
    g_generation++;
    pthread_mutex_unlock(&g_lock);
    return (int)s.n;
}

/* ---- the catalog ---- */

static bool for_title(const Entry *e, uint32_t id)
{
    return !(e->flags & FLAG_BROKEN) && id && (e->ids[0] == id || e->ids[1] == id || e->ids[2] == id);
}

int ogbox_trainers_for_title(uint32_t title_id, OgboxTrainer *out, int max)
{
    int n = 0;
    pthread_mutex_lock(&g_lock);
    for (size_t i = 0; i < g_count && n < max; i++) {
        const Entry *e = &g_list[i];
        if (!for_title(e, title_id)) {
            continue;
        }
        bool dup = false;
        for (int k = 0; k < n && !dup; k++) {
            dup = out[k].title_ids[0] == e->ids[0] && out[k].noptions == e->noptions &&
                  out[k].xbe_time == e->xbe_time && !strcmp(out[k].name, e->name);
        }
        /* (the same bytes: one; the hash is checked through the list) */
        for (size_t k = 0; k < i && !dup; k++) {
            dup = g_list[k].hash == e->hash && for_title(&g_list[k], title_id);
        }
        if (dup) {
            continue;
        }
        OgboxTrainer *t = &out[n++];
        memset(t, 0, sizeof(*t));
        snprintf(t->rel, sizeof(t->rel), "%s", e->rel);
        snprintf(t->path, sizeof(t->path), "%s/%s", g_roots[e->root], e->rel);
        snprintf(t->name, sizeof(t->name), "%s", e->name);
        snprintf(t->author, sizeof(t->author), "%s", e->author);
        memcpy(t->title_ids, e->ids, sizeof(t->title_ids));
        t->xbe_time = e->xbe_time;
        t->noptions = e->noptions;
        t->xbtf = (e->flags & FLAG_XBTF) != 0;
    }
    pthread_mutex_unlock(&g_lock);
    return n;
}

int ogbox_trainers_count(uint32_t title_id)
{
    static OgboxTrainer list[64];
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    const int n = ogbox_trainers_for_title(title_id, list, 64);
    pthread_mutex_unlock(&m);
    return n;
}

bool ogbox_trainers_resolve(const char *rel, char *path, size_t n)
{
    bool found = false;
    pthread_mutex_lock(&g_lock);
    for (int r = 0; r < g_nroots && !found; r++) {
        snprintf(path, n, "%s/%s", g_roots[r], rel);
        struct stat st;
        found = stat(path, &st) == 0 && S_ISREG(st.st_mode);
    }
    pthread_mutex_unlock(&g_lock);
    if (!found && n) {
        path[0] = 0;
    }
    return found;
}

int ogbox_trainers_options(const char *rel, char (*names)[OGBOX_TRAINER_TEXT], int max, char *scroller, size_t n)
{
    char path[1024];
    if (!ogbox_trainers_resolve(rel, path, sizeof(path))) {
        return -1;
    }
    static NcTrainerInfo info;          /* (big: 8.5 KB of option names) */
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    char why[160];
    int count = -1;
    if (nc_trainer_read(path, &info, why, sizeof(why))) {
        count = 0;
        for (unsigned i = 0; i < info.noptions && count < max; i++) {
            snprintf(names[count++], OGBOX_TRAINER_TEXT, "%s", info.options[i]);
        }
        if (scroller && n) {
            snprintf(scroller, n, "%s", info.scroller);
        }
    } else {
        pshakux_log("trainers: %s: %s", path, why);
    }
    pthread_mutex_unlock(&m);
    return count;
}

/* ---- the choices (mods.txt) ---- */

bool ogbox_trainers_chosen(const char *key, char *rel, size_t reln, char *bits, size_t bitsn)
{
    if (!ogbox_mods_get(key, "trainer", rel, reln) || !rel[0]) {
        if (bitsn) {
            bits[0] = 0;
        }
        return false;
    }
    ogbox_mods_get(key, "trainer-options", bits, bitsn);
    return true;
}

bool ogbox_trainers_choose(const char *key, const char *rel)
{
    const bool ok = ogbox_mods_set(key, "trainer", rel);
    ogbox_mods_set(key, "trainer-options", "");
    return ok;
}

bool ogbox_trainers_set_options(const char *key, const char *bits)
{
    /* none on: no line */
    const bool any = bits && strchr(bits, '1');
    return ogbox_mods_set(key, "trainer-options", any ? bits : "");
}
