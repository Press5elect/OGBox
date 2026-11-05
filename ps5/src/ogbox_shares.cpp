/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.3: the network shares (ogbox_shares.h).
 */
#include "ogbox_shares.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>

#ifndef OGBOX_SHARES_HOST_TEST
#include "platform/vfs/ps5_vfs.h"
#endif

namespace ogbox {

namespace {

std::string trimmed(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
		a++;
	}
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
		b--;
	}
	return s.substr(a, b - a);
}

std::string lower(std::string s)
{
	for (char &c : s) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

} // namespace

std::string percentEncode(const std::string &s)
{
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for (const unsigned char c : s) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

std::string percentDecode(const std::string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
			std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
			out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
			i += 2;
		} else {
			out += s[i];
		}
	}
	return out;
}

std::string shareDefaultName(const Share &s)
{
	std::string p = s.path;
	const size_t q = p.find('?');
	if (q != std::string::npos) {
		p.resize(q);
	}
	while (!p.empty() && p.back() == '/') {
		p.pop_back();
	}
	const size_t slash = p.find_last_of('/');
	std::string last = slash == std::string::npos ? p : p.substr(slash + 1);
	return last.empty() ? s.host : last;
}

bool readShares(const std::string &file, ShareFile &out, std::string *error)
{
	out = ShareFile{};
	FILE *f = std::fopen(file.c_str(), "r");
	if (!f) {
		if (error) {
			*error = std::strerror(errno);
		}
		return false;
	}
	std::string pendingName;
	char line[2048];
	while (std::fgets(line, sizeof(line), f)) {
		std::string text = trimmed(line);
		if (text.compare(0, 3, "\xef\xbb\xbf") == 0) {
			text = trimmed(text.substr(3));
		}
		if (text.compare(0, 2, "#@") == 0) {
			const size_t eq = text.find('=');
			if (eq != std::string::npos && lower(trimmed(text.substr(2, eq - 2))) == "name") {
				pendingName = trimmed(text.substr(eq + 1));
			}
			continue;
		}
		if (text.empty() || text[0] == '#' || text[0] == ';' || text[0] == '[') {
			continue;
		}
		const size_t eq = text.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const std::string key = lower(trimmed(text.substr(0, eq)));
		std::string value = trimmed(text.substr(eq + 1));
		if (key == "user") {
			out.user = value.empty() ? "guest" : value;
		} else if (key == "password") {
			out.password = value;
		} else if (key == "uid") {
			out.uid = value;
		} else if (key == "gid") {
			out.gid = value;
		} else if (key == "path" && !value.empty()) {
			Share s;
			if (lower(value.substr(0, 6)) == "nfs://") {
				s.nfs = true;
				value.erase(0, 6);
			} else {
				for (char &c : value) {
					c = c == '\\' ? '/' : c;
				}
				if (lower(value.substr(0, 6)) == "smb://") {
					value.erase(0, 6);
				}
				while (!value.empty() && value[0] == '/') {
					value.erase(0, 1);
				}
			}
			const size_t slash = value.find('/');
			std::string head = value.substr(0, slash);
			s.path = slash == std::string::npos ? std::string() : value.substr(slash + 1);
			if (s.nfs) {
				s.path = "/" + s.path;
			}
			const size_t at = head.rfind('@');
			if (!s.nfs && at != std::string::npos) {
				const std::string account = head.substr(0, at);
				head.erase(0, at + 1);
				const size_t colon = account.find(':');
				std::string user = colon == std::string::npos ? account : account.substr(0, colon);
				const size_t semi = user.find(';');   // domain;user
				s.user = percentDecode(user);
				(void)semi;
				s.password = colon == std::string::npos ? std::string() : percentDecode(account.substr(colon + 1));
			}
			s.host = head;
			s.name = pendingName.empty() ? shareDefaultName(s) : pendingName;
			out.shares.push_back(s);
		}
		pendingName.clear();
	}
	std::fclose(f);
	return true;
}

bool writeShares(const std::string &file, const ShareFile &in, std::string *error)
{
	std::string text =
		"# OGBox's network shares (OGBox writes this file when the settings change them; editing it\n"
		"# by hand works too). One \"path =\" line per share, its name in the \"#@ name =\" line above it.\n"
		"# SMB: server/share/folder, an account of its own as user:password@server/... (percent-encoded);\n"
		"# NFS: nfs://server/export/folder. user and password: the account for SMB shares without one.\n\n";
	text += "user = " + (in.user.empty() ? std::string("guest") : in.user) + "\n";
	text += "password = " + in.password + "\n";
	if (!in.uid.empty()) {
		text += "uid = " + in.uid + "\n";
	}
	if (!in.gid.empty()) {
		text += "gid = " + in.gid + "\n";
	}
	for (const Share &s : in.shares) {
		text += "\n#@ name = " + s.name + "\n";
		if (s.nfs) {
			std::string p = s.path;
			while (!p.empty() && p[0] == '/') {
				p.erase(0, 1);
			}
			text += "path = nfs://" + s.host + "/" + p + "\n";
		} else {
			std::string account;
			if (!s.user.empty()) {
				account = percentEncode(s.user) + (s.password.empty() ? std::string() : ":" + percentEncode(s.password)) + "@";
			}
			text += "path = " + account + s.host + "/" + s.path + "\n";
		}
	}
	const std::string tmp = file + ".new";
	FILE *f = std::fopen(tmp.c_str(), "w");
	if (!f) {
		if (error) {
			*error = std::strerror(errno);
		}
		return false;
	}
	const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
	const bool closed = std::fclose(f) == 0;
	if (!ok || !closed || std::rename(tmp.c_str(), file.c_str()) != 0) {
		if (error) {
			*error = std::strerror(errno);
		}
		std::remove(tmp.c_str());
		return false;
	}
	return true;
}

std::string shareAddress(const Share &s)
{
	if (s.nfs) {
		std::string p = s.path;
		const size_t q = p.find('?');
		if (q != std::string::npos) {
			p.resize(q);
		}
		return "nfs://" + s.host + (p.empty() || p[0] != '/' ? "/" : "") + p;
	}
	return "smb://" + s.host + "/" + s.path;
}

std::string shareMountName(const std::string &name, const std::vector<std::string> &taken)
{
	std::string n;
	for (const char c : name) {
		const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
		n += ok ? c : '_';
	}
	if (n.size() > 28) {
		n.resize(28);
	}
	if (n.empty() || n == "." || n == "..") {
		n = "share";
	}
	std::string unique = n;
	for (unsigned k = 2;; k++) {
		bool used = false;
		for (const std::string &t : taken) {
			used = used || t == "/smb/" + unique;
		}
		if (!used) {
			return unique;
		}
		unique = n + "-" + std::to_string(k);
	}
}

bool mountShare(Share &s, const ShareFile &file, const std::vector<std::string> &taken, std::string *error)
{
	const std::string name = shareMountName(s.name, taken);
	std::string url;
	if (s.nfs) {
		url = shareAddress(s);
		const size_t q = s.path.find('?');
		if (q != std::string::npos) {
			url += s.path.substr(q);
		}
		for (const auto &kv : { std::make_pair(std::string("uid"), file.uid), std::make_pair(std::string("gid"), file.gid) }) {
			if (!kv.second.empty() && url.find(kv.first + "=") == std::string::npos) {
				url += (url.find('?') == std::string::npos ? "?" : "&") + kv.first + "=" + kv.second;
			}
		}
	} else {
		const std::string user = s.user.empty() ? file.user : s.user;
		const std::string password = s.user.empty() ? file.password : s.password;
		url = "smb://" + percentEncode(user.empty() ? std::string("guest") : user) +
			(password.empty() ? std::string() : ":" + percentEncode(password)) + "@" + s.host + "/" + s.path;
	}
#ifndef OGBOX_SHARES_HOST_TEST
	const int r = s.nfs ? ps5vfs_mount_nfs(name.c_str(), url.c_str()) : ps5vfs_mount_smb(name.c_str(), url.c_str());
	if (r != 0) {
		if (error) {
			*error = "the address isn't one OGBox can use";
		}
		return false;
	}
#endif
	s.mount = "/smb/" + name;
	return true;
}

void unmountShare(Share &s)
{
	if (!s.mount.empty()) {
#ifndef OGBOX_SHARES_HOST_TEST
		ps5vfs_unmount(s.mount.c_str());
#endif
		s.mount.clear();
	}
}

} // namespace ogbox
