// OGBox - the updater (ps5_update.h, Crux2's) set up for OGBox: its releases on GitHub
// (Press5elect/OGBox, "OGBox-v<version>.zip"), asked over HTTPS with curl.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.1. The steps are ps5_update.h's (Check, Download, Install, GetStatus); these two set
// them up.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace OGBoxNet
{
/// 0.0.3: GET an HTTPS (or HTTP) address with curl and the title's certificates (cacert.pem), redirects
/// followed: its status (-1 when the network failed) and its body (16 MiB at most).
int Get(const std::string& url, std::vector<uint8_t>& out, unsigned seconds);
} // namespace OGBoxNet

namespace OGBoxUpdate
{
/// Once at start-up, before the title's files are read: an install a power cut stopped is undone,
/// and what an earlier update left in <title folder>/.update/ is removed. True when one was undone.
bool Recover();
/// Gives the updater the title folder, this build's version and curl. Asks nothing yet; once is
/// enough (later calls do nothing).
void Start();
} // namespace OGBoxUpdate
