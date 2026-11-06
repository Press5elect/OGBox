/*
 * OGBox - stand-ins for the C library calls Rust's standard library refers to that the PS5's
 * libc doesn't have (the DSP56300 JIT, ps5/cmake/hakux.cmake, is a Rust static library built
 * against FreeBSD's ABI).
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * Nothing the JIT does reaches them: they belong to std's process spawning (setgid, setpgid,
 * killpg, chroot, posix_spawn_file_actions_addchdir(_np), which std asks for weakly), its file
 * system calls the console lacks (chown, lchown, linkat, mkfifo), Unix sockets' peer credentials
 * (getpeereid), its backtraces (dl_iterate_phdr) and the region crate's memory queries
 * (kinfo_getvmmap). Each fails as the call does where it is not supported, so a caller that does
 * reach one gets an error, not a crash. pthread_setname_np goes to the platform's
 * pshakux_setname, as hakuX's own calls do.
 *
 * Why here: the console's module writer wants every import, weak or not, exported by a module
 * the title loads (link-title.sh refuses one that only libkernel_sys's stub has: null at run
 * time). Weak: a real definition, should the SDK gain one, wins. Six of them (chown, chroot,
 * lchown, linkat, setpgid, pthread_setname_np) libkernel_sys's stub does define, and lld exports
 * a title's definition of a name a linked library defines, which the module writer refuses (the
 * console's clang takes visibility from the DLL storage class: "hidden" doesn't stop it). The
 * Rust library's references to those six are renamed in the archive instead (ogbox_rs_<name>,
 * tools/build-deps.sh: llvm-objcopy --redefine-sym), and defined here under the new names.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#include <pthread.h>

struct dl_phdr_info;

#define OGBOX_STUB(ret, name, params) \
    __attribute__((weak)) ret ogbox_stub_##name params __asm__(#name); \
    ret ogbox_stub_##name params

#define OGBOX_ENOSYS(name, params, ...) \
    OGBOX_STUB(int, name, params) \
    { \
        __VA_ARGS__; \
        errno = ENOSYS; \
        return -1; \
    }

OGBOX_ENOSYS(setgid, (gid_t gid), (void)gid)
OGBOX_ENOSYS(ogbox_rs_setpgid, (pid_t pid, pid_t pgid), (void)pid, (void)pgid)
OGBOX_ENOSYS(killpg, (pid_t pgrp, int sig), (void)pgrp, (void)sig)
OGBOX_ENOSYS(ogbox_rs_chroot, (const char *path), (void)path)
OGBOX_ENOSYS(ogbox_rs_chown, (const char *path, uid_t owner, gid_t group), (void)path, (void)owner, (void)group)
OGBOX_ENOSYS(ogbox_rs_lchown, (const char *path, uid_t owner, gid_t group), (void)path, (void)owner, (void)group)
OGBOX_ENOSYS(ogbox_rs_linkat, (int fd1, const char *name1, int fd2, const char *name2, int flag), (void)fd1, (void)name1,
             (void)fd2, (void)name2, (void)flag)
OGBOX_ENOSYS(mkfifo, (const char *path, mode_t mode), (void)path, (void)mode)
OGBOX_ENOSYS(getpeereid, (int s, uid_t *euid, gid_t *egid), (void)s, (void)euid, (void)egid)
OGBOX_ENOSYS(posix_spawn_file_actions_addchdir, (void *actions, const char *path), (void)actions, (void)path)
OGBOX_ENOSYS(posix_spawn_file_actions_addchdir_np, (void *actions, const char *path), (void)actions, (void)path)

/* (pthread_setname_np, renamed) the platform's thread names, as hakuX's own calls get them
 * (hakux_ps5.h's macro) */
int pshakux_setname(pthread_t thread, const char *name);
OGBOX_STUB(int, ogbox_rs_pthread_setname_np, (pthread_t thread, const char *name))
{
    return pshakux_setname(thread, name);
}

/* no loaded objects to walk: a backtrace finds no symbols */
OGBOX_STUB(int, dl_iterate_phdr, (int (*callback)(struct dl_phdr_info *, size_t, void *), void *data))
{
    (void)callback;
    (void)data;
    return 0;
}

/* (libutil's) the process's memory map: none */
OGBOX_STUB(void *, kinfo_getvmmap, (pid_t pid, int *cntp))
{
    (void)pid;
    if (cntp) {
        *cntp = 0;
    }
    errno = ENOSYS;
    return NULL;
}
