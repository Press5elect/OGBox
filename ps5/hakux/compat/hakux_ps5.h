/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * Included first in every hakuX file the title compiles (ps5/cmake/hakux.cmake): the Linux and
 * Android calls hakuX makes that the console's FreeBSD-like libc names otherwise or lacks, mapped
 * to the console's or to ps5/hakux/compat/hakux_ps5.c. hakuX itself is not changed for them.
 *
 *   gettid(), syscall(SYS_gettid)       the thread's id (pthread_getthreadid_np)
 *   syscall(__NR_memfd_create, ...)     memfd_create, which the link binds to the platform
 *                                       layer's (an anonymous shared memory object)
 *   any other syscall()                 ENOSYS (a title may make no system call)
 *   pthread_create                      through pshakux_pthread_create: the thread gets a crash
 *                                       report stack, its name and priority role by its entry
 *                                       function (hakux_ps5.c's table), said in the boot log
 *   pthread_setname_np                  pthread_set_name_np; the name (a thread's own or its
 *                                       creator's) gives the role of threads made by hakuX's
 *                                       thread helpers (hle_thread_create, apu_thread_start)
 *   setpriority(PRIO_PROCESS, tid, n)   a thread lowering itself (n > 0) is a background job (730)
 *   sysconf                             the title's (13 processors, 16 KiB pages): the console
 *                                       gives a title none
 *   PR_SET_TIMERSLACK, PR_SET_NAME      prctl: the name; the timer slack has no equal (0)
 */
#ifndef PSHAKUX_HAKUX_PS5_H
#define PSHAKUX_HAKUX_PS5_H

#if defined(__PROSPERO__) || defined(PSHAKUX)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <pthread.h>
#include <pthread_np.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <unistd.h>

/* What bionic's headers bring in with others, hakuX's files take for granted: stdio.h,
 * signal.h and netinet/in.h are included above for them, and the address families (FreeBSD's
 * numbers) that bionic's netinet/in.h gives. Not sys/socket.h: a file of hakuX's has a static
 * accept() of its own. */
#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 28
#endif

/* Reservations are never charged up front on the console: no flag for it. */
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Numbers no FreeBSD system call has: pshakux_syscall tells them apart. */
#ifndef __NR_memfd_create
#define __NR_memfd_create (-0x5f01)
#endif
#ifndef SYS_memfd_create
#define SYS_memfd_create __NR_memfd_create
#endif
#ifndef __NR_gettid
#define __NR_gettid (-0x5f02)
#endif
#ifndef SYS_gettid
#define SYS_gettid __NR_gettid
#endif
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001u
#endif

long pshakux_syscall(long number, ...);
int pshakux_gettid(void);
int pshakux_setname(pthread_t thread, const char *name);
int pshakux_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg,
                           const char *entry);
int pshakux_setpriority(int which, id_t who, int prio);
int memfd_create(const char *name, unsigned int flags);

#define syscall(...) pshakux_syscall(__VA_ARGS__)
#define gettid() pshakux_gettid()
#define pthread_setname_np(thread, name) pshakux_setname((thread), (name))
#define pthread_create(thread, attr, fn, arg) pshakux_pthread_create((thread), (attr), (fn), (arg), #fn)
#define setpriority(which, who, prio) pshakux_setpriority((which), (who), (prio))
/* No sysconf for a title on the console (measured 2026-11-01): src/platform/ps5_log.c's. */
long pshakux_sysconf(int name);
#define sysconf(name) pshakux_sysconf(name)

/* The console's libc names the coarse monotonic clock CLOCK_MONOTONIC_FAST. */
#ifndef CLOCK_MONOTONIC_COARSE
#define CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC
#endif
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME CLOCK_MONOTONIC
#endif

#ifdef __cplusplus
}
#endif

/* The debug properties (Android's calls, on the environment). */
#include <sys/system_properties.h>

#endif
#endif
