/*
 * ps5-native-app-boilerplate - Application-side elevation API.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-FileCopyrightText: 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
 * OGBox: from Crux2 08c3507 (ps5/elevation), with this title's ID (PPSA99251) and the client's
 * steps reported for the boot log. Keep the attribution "OGBox by Press5elect" too.
 */
#pragma once

#include "protocol.hpp"

namespace elevation
{
// Call during single-threaded startup, before creating workers or opening files
// outside the sandbox. Only Status::ok permits continuing with elevated work.
// Each call launches one bundled helper; no persistent service is installed.
// OGBox: step, when given, is told each step as it is taken (a line of text, with ctx), so the
// boot log can say how far the request got. It is called before any thread exists: it must only
// keep the line (the log is not open yet).
using StepFn = void (*)(void *ctx, const char *line);
[[nodiscard]] Status request(Capability capability,
                             const char *helper_path = "/app0/sandbox-elevator.elf", StepFn step = nullptr,
                             void *ctx = nullptr) noexcept;
} // namespace elevation
