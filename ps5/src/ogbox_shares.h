/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.3: the network shares as the settings edit them. /data/ogbox/network.cfg stays the
 * file ps5vfs reads (path, user, password, uid, gid lines); each share is one path line, with its
 * own account in it ("user:password@server/share/folder", percent-encoded) and its name in a
 * "#@ name = ..." line just above (a comment to anything else). OGBox mounts them itself, each at
 * /smb/<its name>, so a share added or changed in the settings is there at once.
 */
#ifndef OGBOX_SHARES_H
#define OGBOX_SHARES_H

#include <string>
#include <vector>

namespace ogbox {

struct Share {
	std::string name;       // as shown; its mount is /smb/<name, made safe>
	bool nfs{ false };
	std::string host;       // the server: an address or a name
	std::string path;       // SMB: share/folder; NFS: /export/folder (with ?options)
	std::string user, password;   // SMB; no user: the file's (guest)
	std::string mount;      // where it is mounted ("" not mounted)
};

struct ShareFile {
	std::vector<Share> shares;
	std::string user{ "guest" }, password;   // the file's account, for SMB shares without their own
	std::string uid, gid;                     // the file's, for NFS
};

/* Reads a network.cfg; false (and why) when it cannot be read. */
bool readShares(const std::string &file, ShareFile &out, std::string *error = nullptr);
/* Writes it whole (a short header, the file's account, each share); false and why. */
bool writeShares(const std::string &file, const ShareFile &in, std::string *error = nullptr);

/* "smb://192.168.1.10/games/xbox", "nfs://nas/volume1/xbox": the address, without the account. */
std::string shareAddress(const Share &s);
/* The mount's name for a share's name: letters, digits, '.', '_' and '-', at most 28, not taken. */
std::string shareMountName(const std::string &name, const std::vector<std::string> &taken);
/* Mounts it (at /smb/<name>), the file's account or uid/gid for what it lacks; false and why. */
bool mountShare(Share &s, const ShareFile &file, const std::vector<std::string> &taken, std::string *error = nullptr);
/* Its mount taken away (a read under way still ends). */
void unmountShare(Share &s);
/* The share's last folder (its name when it has none). */
std::string shareDefaultName(const Share &s);

std::string percentEncode(const std::string &s);
std::string percentDecode(const std::string &s);

} // namespace ogbox

#endif
