/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * The console's side of the Linux and Android calls hakuX makes (compat/hakux_ps5.h and
 * compat/include): the debug properties, syscall, gettid, thread names and priorities, eventfd and
 * the Android log.
 */
#include <sys/eventfd.h>
#include <android/log.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform/ps5_priority.h"

/* ---- the debug properties: environment variables, as hakuX's prop.h reads them off Android ------ */

/* debug.hakux.present -> DEBUG_HAKUX_PRESENT */
static void env_name(const char *name, char *env, size_t size)
{
    size_t i = 0;
    for (; name[i] && i + 1 < size; i++) {
        const char c = name[i];
        env[i] = c == '.' ? '_' : (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    env[i] = 0;
}

int __system_property_get(const char *name, char *value)
{
    char env[128];
    env_name(name, env, sizeof(env));
    const char *e = getenv(env);
    value[0] = 0;
    if (e) {
        snprintf(value, PROP_VALUE_MAX, "%s", e);
    }
    return (int)strlen(value);
}

int pshakux_prop_set(const char *name, const char *value)
{
    if (!name || !name[0]) {
        return -1;
    }
    char env[128];
    env_name(name, env, sizeof(env));
    return value && value[0] ? setenv(env, value, 1) : unsetenv(env);
}

int pshakux_props_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *s = line;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        char *eq = strchr(s, '=');
        if (*s == '#' || !eq || eq == s) {
            continue;
        }
        *eq = 0;
        if (pshakux_prop_set(s, eq + 1) == 0) {
            n++;
        }
    }
    fclose(f);
    return n;
}

/* ---- system calls, thread ids, names and priorities ----------------------------------------- */

long pshakux_syscall(long number, ...)
{
    va_list ap;
    va_start(ap, number);
    long result = -1;
    if (number == __NR_memfd_create) {
        const char *name = va_arg(ap, const char *);
        const unsigned flags = va_arg(ap, unsigned);
        result = memfd_create(name, flags);
    } else if (number == __NR_gettid) {
        result = pshakux_gettid();
    } else {
        errno = ENOSYS;
    }
    va_end(ap);
    return result;
}

int pshakux_gettid(void)
{
    return pthread_getthreadid_np();
}

/* The priority role of a hakuX thread, by the name it gives itself: the HLE GPU's render, timer
 * and timeline threads and the APU's are helpers (690, ahead of the vCPU at 700, which waits on
 * them); the pipeline compilers lower themselves (setpriority) and are background jobs (730). */
static int role_of(const char *name)
{
    static const char *const helpers[] = {"hle", "apu", "nv2a", "vblank", "timeline", "render", "dsp"};
    for (unsigned i = 0; i < sizeof(helpers) / sizeof(helpers[0]); i++) {
        if (!strncmp(name, helpers[i], strlen(helpers[i]))) {
            return PSHAKUX_ROLE_HELPER;
        }
    }
    if (!strncmp(name, "pipe", 4) || !strncmp(name, "shader", 6) || !strncmp(name, "net", 3)) {
        return PSHAKUX_ROLE_BACKGROUND;
    }
    return -1;
}

int pshakux_setname(pthread_t thread, const char *name)
{
    if (!name) {
        return EINVAL;
    }
    pthread_set_name_np(thread, name);
    if (pthread_equal(thread, pthread_self())) {
        const int role = role_of(name);
        if (role >= 0) {
            pshakux_priority_set(role);
        }
    }
    return 0;
}

int pshakux_setpriority(int which, id_t who, int prio)
{
    (void)which;
    if (prio > 0 && (who == 0 || (int)who == pshakux_gettid())) {
        pshakux_priority_set(PSHAKUX_ROLE_BACKGROUND);
    }
    return 0;
}

/* ---- eventfd: a pipe ------------------------------------------------------------------------ */

#define EVENTFDS_MAX 16
static struct {
    atomic_int read_fd, write_fd;
} g_eventfds[EVENTFDS_MAX];
static atomic_int g_eventfd_count;

int eventfd(unsigned int initval, int flags)
{
    int fds[2];
    if (pipe(fds) != 0) {
        return -1;
    }
    for (int i = 0; i < 2; i++) {
        if (flags & EFD_NONBLOCK) {
            fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
        }
        if (flags & EFD_CLOEXEC) {
            fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        }
    }
    const int slot = atomic_fetch_add(&g_eventfd_count, 1);
    if (slot >= EVENTFDS_MAX) {
        close(fds[0]);
        close(fds[1]);
        errno = EMFILE;
        return -1;
    }
    atomic_store(&g_eventfds[slot].write_fd, fds[1]);
    atomic_store(&g_eventfds[slot].read_fd, fds[0]);
    if (initval) {
        const eventfd_t v = initval;
        (void)!write(fds[1], &v, sizeof(v));
    }
    return fds[0];
}

int pshakux_eventfd_writer(int fd)
{
    const int n = atomic_load(&g_eventfd_count);
    for (int i = 0; i < n && i < EVENTFDS_MAX; i++) {
        if (atomic_load(&g_eventfds[i].read_fd) == fd) {
            return atomic_load(&g_eventfds[i].write_fd);
        }
    }
    return -1;
}

int eventfd_read(int fd, eventfd_t *value)
{
    /* Everything written so far, added up, as an eventfd's counter reads. */
    eventfd_t sum = 0, v;
    ssize_t got;
    int reads = 0;
    while ((got = read(fd, &v, sizeof(v))) == (ssize_t)sizeof(v)) {
        sum += v;
        reads++;
        const int flags = fcntl(fd, F_GETFL);
        if (!(flags & O_NONBLOCK)) {
            break;
        }
    }
    if (!reads) {
        return -1;
    }
    *value = sum;
    return 0;
}

int eventfd_write(int fd, eventfd_t value)
{
    const int writer = pshakux_eventfd_writer(fd);
    return write(writer >= 0 ? writer : fd, &value, sizeof(value)) == (ssize_t)sizeof(value) ? 0 : -1;
}

/* ---- the Android log: standard error (the boot log) ----------------------------------------- */

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap)
{
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    return __android_log_write(prio, tag, buf);
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = __android_log_vprint(prio, tag, fmt, ap);
    va_end(ap);
    return n;
}

int __android_log_write(int prio, const char *tag, const char *text)
{
    (void)prio;
    return fprintf(stderr, "%s: %s\n", tag ? tag : "hakuX", text ? text : "");
}

/* ---- memmem and nftw: no system module exports them ------------------------------------------ */

void *memmem(const void *haystack, size_t haystack_len, const void *needle, size_t needle_len)
{
    if (!needle_len) {
        return (void *)haystack;
    }
    const unsigned char *h = haystack, *n = needle;
    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        if (h[i] == n[0] && !memcmp(h + i, n, needle_len)) {
            return (void *)(h + i);
        }
    }
    return NULL;
}

/* nftw as the core uses it (a partition folder's size, FTW_PHYS): every entry below path, files
 * as FTW_F, folders as FTW_D before what is in them (FTW_DEPTH is not supported), with the depth
 * and the name's offset; a callback's non-zero answer ends the walk with it. The descriptor limit
 * is not needed: a folder is closed before the next level is opened. */
static int walk(char *path, size_t len, size_t cap, int level,
                int (*fn)(const char *, const struct stat *, int, struct FTW *), int flags)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        struct FTW f = {(int)(strrchr(path, '/') ? strrchr(path, '/') - path + 1 : 0), level};
        return fn(path, &st, FTW_NS, &f);
    }
    struct FTW f = {(int)(strrchr(path, '/') ? strrchr(path, '/') - path + 1 : 0), level};
    if (!S_ISDIR(st.st_mode)) {
        return fn(path, &st, FTW_F, &f);
    }
    int r = fn(path, &st, FTW_D, &f);
    if (r) {
        return r;
    }
    DIR *d = opendir(path);
    if (!d) {
        return 0;
    }
    /* the names first, so the folder is closed before going down */
    char **names = NULL;
    size_t n = 0, room = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        if (n == room) {
            room = room ? room * 2 : 32;
            char **grown = realloc(names, room * sizeof(*names));
            if (!grown) {
                break;
            }
            names = grown;
        }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    for (size_t i = 0; i < n; i++) {
        const size_t add = strlen(names[i]);
        if (!r && names[i] && len + 1 + add < cap) {
            path[len] = '/';
            memcpy(path + len + 1, names[i], add + 1);
            r = walk(path, len + 1 + add, cap, level + 1, fn, flags);
            path[len] = 0;
        }
        free(names[i]);
    }
    free(names);
    return r;
}

int nftw(const char *path, int (*fn)(const char *, const struct stat *, int, struct FTW *), int fd_limit,
         int flags)
{
    (void)fd_limit;
    char buf[1024];
    const size_t len = strlen(path);
    if (len >= sizeof(buf)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buf, path, len + 1);
    return walk(buf, len, sizeof(buf), 0, fn, flags);
}
