/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
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
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform/ps5_priority.h"
#include "platform/ps5_bootlog.h"

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

/* ---- hakuX's threads: names, priority roles, crash report stacks (0.1.3) --------------------
 *
 * The console's scheduler is strict (ps5_priority.h): the vCPU runs at 700; what the guest's
 * timing waits on runs ahead of it at 690 (it mostly sleeps, so it costs the vCPU nothing); work
 * that may take long and that nothing waits on at once runs behind at 730. Every thread hakuX
 * creates is in one of these tables; one that is not is said in the boot log as "no role". */
typedef struct ThreadRole {
    const char *key;    /* the entry function (pthread_create's argument) or the thread's name */
    int role;
    const char *name;   /* the name given (entry functions only) */
} ThreadRole;

/* pthread_create(..., entry, ...) in hakuX's own files */
static const ThreadRole k_entries[] = {
    {"vcpu_thread", PSHAKUX_ROLE_VCPU, "hakux-vcpu"},       /* src/core/hakux_core.c: the guest's CPU */
    /* 0.0.8: the device timers at 690 (ps5_priority.cpp, Role::Device): ahead of the helpers on their processors */
    {"vblank_thread", PSHAKUX_ROLE_DEVICE, "nv2a-vblank"},      /* the GPU's vertical blank */
    {"ptimer_thread", PSHAKUX_ROLE_DEVICE, "nv2a-ptimer"},      /* the GPU's timer interrupt */
    {"clock_thread", PSHAKUX_ROLE_DEVICE, "kernel-clock"},      /* the kernel's timer DPCs */
    {"timer_thread", PSHAKUX_ROLE_DEVICE, "hle-timers"},        /* the HLE GPU's timers */
    {"frame_thread", PSHAKUX_ROLE_DEVICE, "ohci-frame"},        /* USB frames: the controllers */
    {"pcm_thread", PSHAKUX_ROLE_HELPER, "aci-pcm"},             /* AC'97 sound */
    {"reader_thread", PSHAKUX_ROLE_HELPER, "disc-reader"},      /* disc reads the guest waits for */
    {"nvnet_thread", PSHAKUX_ROLE_HELPER, "nvnet"},             /* the NIC's interrupts */
    {"net_thread", PSHAKUX_ROLE_HELPER, "net"},                 /* the NIC's network (polls) */
    {"closer_thread", PSHAKUX_ROLE_BACKGROUND, "io-closer"},    /* closing written files */
    {"log_writer", PSHAKUX_ROLE_HELPER, "nc-log"},              /* 0.1.30: the log's lines written (native/core/log.c); 0.1.32: a helper (700), not behind the compiles: the vCPU shares its lock */
    {"ram_part", PSHAKUX_ROLE_BACKGROUND, "state-ram"},         /* savestates: RAM compressed */
    {"pz_piece", PSHAKUX_ROLE_BACKGROUND, "cache-pack"},        /* the caches compressed */
};

/* names given by hle_thread_create and apu_thread_start (their entry is a parameter, "fn") */
static const ThreadRole k_names[] = {
    {"hle-pb", PSHAKUX_ROLE_HELPER, NULL},        /* the push-buffer decoder */
    {"hle.render", PSHAKUX_ROLE_HELPER, NULL},    /* the render thread */
    {"hle.timeline", PSHAKUX_ROLE_HELPER, NULL},  /* GPU completion: the guest's fences */
    {"apu.frame", PSHAKUX_ROLE_HELPER, NULL},     /* the APU's frame */
    {"apu.voice", PSHAKUX_ROLE_HELPER, NULL},     /* 0.1.4: the APU's voice workers (the frame waits on them); 0.1.17: compute, not sound output */
    {"apu.out", PSHAKUX_ROLE_SOUND, NULL},        /* 0.1.4: the APU's output to the console (ps5/hakux/apu/out_ps5.c) */
    {"hle.hitch", PSHAKUX_ROLE_HELPER, NULL},     /* the hitch monitor: it must see a busy vCPU */
    {"hle-sampler", PSHAKUX_ROLE_HELPER, NULL},   /* the perf sampler: the same */
    {"hle.pipe", PSHAKUX_ROLE_BACKGROUND, NULL},  /* pipeline compiles */
    {"hle.prebuild", PSHAKUX_ROLE_BACKGROUND, NULL}, /* cached pipelines built ahead */
    {"hle.pcache", PSHAKUX_ROLE_BACKGROUND, NULL}, /* the pipeline cache saved */
};

static const ThreadRole *find_role(const ThreadRole *table, size_t n, const char *key)
{
    for (size_t i = 0; key && i < n; i++) {
        if (!strcmp(table[i].key, key)) {
            return &table[i];
        }
    }
    return NULL;
}

/* names given by a creator: kept here (the crash report keeps a pointer) */
#define NAME_SLOTS 64
static char g_names[NAME_SLOTS][16];
static atomic_int g_name_next;

static const char *keep_name(const char *name)
{
    const int i = atomic_fetch_add(&g_name_next, 1);
    if (i >= NAME_SLOTS) {
        return "hakux-thread";
    }
    snprintf(g_names[i], sizeof(g_names[i]), "%s", name);
    return g_names[i];
}

unsigned nc_vmhost_check_window_low(uint32_t *first) __attribute__((weak));

typedef struct Start {
    void *(*fn)(void *);
    void *arg;
    const char *entry;
} Start;

static void *thread_start(void *opaque)
{
    Start s = *(Start *)opaque;
    free(opaque);
    pshakux_install_crash_stack();  /* a fault or an overflow in a helper is reported too */
    const ThreadRole *r = find_role(k_entries, sizeof(k_entries) / sizeof(k_entries[0]), s.entry);
    if (r) {
        pthread_set_name_np(pthread_self(), r->name);
        pshakux_thread_named(r->name);
        pshakux_priority_set_thread(pthread_self(), r->role, r->name, s.entry);
    } else if (strcmp(s.entry, "fn") != 0) {
        /* not a thread helper's (those name it after pthread_create): no role known */
        pshakux_thread_named(s.entry);
        pshakux_priority_set_thread(pthread_self(), -1, s.entry, "not in hakux_ps5.c's tables");
    }
    void *const result = s.fn(s.arg);
    pshakux_thread_gone();
    /* OGBox 0.1.37: libkernel runs libc.prx's old entries at 0x80000000 as the
     * thread ends (its destructors): put back first if something wrote over
     * them since the last look (GTA San Andreas: ten times a second) */
    if (nc_vmhost_check_window_low) {
        nc_vmhost_check_window_low(NULL);
    }
    return result;
}

int pshakux_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg,
                           const char *entry)
{
    Start *s = malloc(sizeof(*s));
    if (!s) {
        return EAGAIN;
    }
    s->fn = fn;
    s->arg = arg;
    s->entry = entry ? entry : "?";
#undef pthread_create
    const int r = pthread_create(thread, attr, thread_start, s);
    if (r != 0) {
        free(s);
    }
    return r;
}

int pshakux_setname(pthread_t thread, const char *name)
{
    if (!name) {
        return EINVAL;
    }
    pthread_set_name_np(thread, name);
    const ThreadRole *r = find_role(k_names, sizeof(k_names) / sizeof(k_names[0]), name);
    bool named_by_entry = false;   /* its entry function gave it this name and role already */
    for (size_t i = 0; i < sizeof(k_entries) / sizeof(k_entries[0]); i++) {
        named_by_entry |= !strcmp(k_entries[i].name, name);
    }
    if (!named_by_entry) {
        const char *kept = keep_name(name);
        pshakux_thread_named_for(thread, kept);
        pshakux_priority_set_thread(thread, r ? r->role : -1, kept,
                                    pthread_equal(thread, pthread_self()) ? "its own name" : "its creator's name");
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

/* ---- the native CPU's last words (0.1.3) -----------------------------------------------------
 * native/cpu/cpu_native.c's line before a fatal signal goes on or an abort: straight into the boot
 * log, synced, and to klog, from the signal handler (no stdio, no locks). */
void nc_host_emergency_write(const char *text, size_t len)
{
    const int fd = pshakux_log_fd();
    if (fd >= 0) {
        char stamp[24];
        const int n = snprintf(stamp, sizeof(stamp), "[%8.3f] ", pshakux_uptime());
        if (n > 0) {
            (void)!write(fd, stamp, (size_t)n);
        }
        (void)!write(fd, text, len);
        (void)fsync(fd);
    }
    pshakux_klog_raw(text, (unsigned)len);
}
