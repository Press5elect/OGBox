// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
// PSHakuX 0.1.3 - leaving the sandbox (ps5_sandbox.cpp says how and why).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// While the process has one thread: asks the bundled helper for the console's whole file system,
/// then finds the title folder (written to title_dir; 1 when it moved from /app0's view, 0 when
/// title_dir is left as it was). Every step is kept for pshakux_sandbox_log.
int pshakux_sandbox_leave(char* title_dir, unsigned size);
/// The kept steps, one line each, to log (the boot log, once it is open).
void pshakux_sandbox_log(void (*log)(const char* line));
/// Whether the process left its sandbox (paths such as /data/..., /mnt/usb0 are the console's).
int pshakux_sandbox_elevated(void);
int pshakux_sandbox_tried(void);

#ifdef __cplusplus
}
#endif
