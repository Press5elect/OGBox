/*
 * PSHakuX - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
 *
 * Linux's eventfd, which no system module of the console exports: a pipe stands in for it
 * (ps5/hakux/compat/hakux_ps5.c). eventfd gives the pipe's read end; what is written to the
 * counter goes to the write end, which pshakux_eventfd_writer gives (-1 for a descriptor that is
 * not one of these): a caller that writes to the eventfd's own descriptor must write there
 * instead (ps5/patches/hakux: the network's wake-up).
 */
#ifndef PSHAKUX_SYS_EVENTFD_H
#define PSHAKUX_SYS_EVENTFD_H

#include <stdint.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t eventfd_t;
#define EFD_SEMAPHORE 0x1
#define EFD_CLOEXEC O_CLOEXEC
#define EFD_NONBLOCK O_NONBLOCK

int eventfd(unsigned int initval, int flags);
int pshakux_eventfd_writer(int fd);
int eventfd_read(int fd, eventfd_t *value);
int eventfd_write(int fd, eventfd_t value);

#ifdef __cplusplus
}
#endif

#endif
