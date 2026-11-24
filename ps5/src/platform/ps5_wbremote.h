// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 1.0.4 - Fast writes on the console's storage (helper): the cache drives' write-back
// (hakux-native native/kernel/wback.c) written by the write broker (ps5_broker.h), a payload of its
// own, which the console doesn't hold back as it holds back the title's own writes after about
// 1.3 GiB. Settings > Advanced; on by default; for a data folder under /data/ only (on USB storage
// there is no such budget). Launched once a run before the core starts and handed to the core
// through its C table (nc_wb_set_remote); the core falls back to OGBox's own writes, in order, if
// the broker fails. The core's own lines ("hakuX-native-wb: helper ...") say how it goes.

#pragma once

#include <functional>
#include <string>

namespace ogbox::broker
{
/// The broker's connection (Launch's, or the PC test's), or -1 with why.
using HelperConnector = std::function<int(std::string& why)>;

/// Starts the helper for a run whose Xbox hard disk is at hddRoot and hands it to the core. true:
/// in use (why: the broker's own line); false: not, why says why (a line for the boot log).
/// localRoot: the PC test's (the broker opens its paths under that folder: it is taken off the
/// paths sent); "" on the console.
bool StartCacheHelper(const std::string& hddRoot, HelperConnector connect, std::string& why,
	const std::string& localRoot = std::string());
} // namespace ogbox::broker
