// OGBox - game fixes from GitHub at start (ogbox_fixes_fetch.h).
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.

#include "ogbox_fixes_fetch.h"

#include "platform/ps5_update.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#if defined(OGBOX_FIXES_TEST)
static void fixes_log(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void fixes_log(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fputc('\n', stderr);
}
#else
#include "platform/ps5_bootlog.h"
#include "platform/ps5_update_net.h"
#include <thread>
#define fixes_log pshakux_log
#endif

namespace ogbox::fixes
{
namespace
{
constexpr size_t kMostFiles = 4000;
constexpr uint64_t kMostBytes = 8u << 20;      // a file
constexpr size_t kMostList = 2u << 20;         // the list

struct Entry
{
	std::string sha, path;
	uint64_t bytes = 0;
};

bool IsHex64(const std::string& s)
{
	if (s.size() != 64)
		return false;
	for (char c : s)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	return true;
}

/// Only mods/<key>/<mod>/... and gamedata/...: plain names (letters, digits, . _ -), none starting
/// with '.', 2 to 6 parts. Anything else in the list is refused, so a list can't reach outside them.
bool SafePath(const std::string& path)
{
	if (path.empty() || path.size() > 200)
		return false;
	std::vector<std::string> parts;
	size_t at = 0;
	while (at <= path.size())
	{
		const size_t slash = path.find('/', at);
		const std::string part = path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
		if (part.empty() || part[0] == '.')
			return false;
		for (char c : part)
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
				    c == '-'))
				return false;
		parts.push_back(part);
		if (slash == std::string::npos)
			break;
		at = slash + 1;
	}
	if (parts.size() < 2 || parts.size() > 6)
		return false;
	if (parts[0] == "mods")
		return parts.size() >= 4;     // mods/<key>/<mod>/<file>
	return parts[0] == "gamedata";
}

bool ReadAll(const std::string& path, std::string& out, uint64_t most)
{
	out.clear();
	const int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return false;
	char buf[65536];
	for (;;)
	{
		const ssize_t n = read(fd, buf, sizeof(buf));
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 || out.size() + static_cast<size_t>(n) > most)
		{
			close(fd);
			return false;
		}
		if (n == 0)
			break;
		out.append(buf, static_cast<size_t>(n));
	}
	close(fd);
	return true;
}

void MakeDirs(const std::string& dir)
{
	for (size_t at = 1; at <= dir.size(); at++)
		if (at == dir.size() || dir[at] == '/')
			mkdir(dir.substr(0, at).c_str(), 0777);
}

/// Written beside its place, synced and renamed over it.
bool WriteOver(const std::string& path, const void* data, size_t bytes)
{
	const size_t slash = path.rfind('/');
	if (slash != std::string::npos)
		MakeDirs(path.substr(0, slash));
	const std::string part = path + ".part";
	const int fd = open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return false;
	const char* p = static_cast<const char*>(data);
	size_t left = bytes;
	while (left > 0)
	{
		const ssize_t n = write(fd, p, left);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
		{
			close(fd);
			unlink(part.c_str());
			return false;
		}
		p += n;
		left -= static_cast<size_t>(n);
	}
	fsync(fd);
	close(fd);
	if (rename(part.c_str(), path.c_str()) != 0)
	{
		unlink(part.c_str());
		return false;
	}
	return true;
}

std::vector<std::string> Lines(const std::string& text)
{
	std::vector<std::string> out;
	size_t at = 0;
	while (at < text.size())
	{
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(at, end - at);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		out.push_back(line);
		at = end + 1;
	}
	return out;
}

/// "name:" of a mod.txt, else the folder's name.
std::string ModName(const std::string& modTxt, const std::string& folder)
{
	for (const std::string& line : Lines(modTxt))
		if (line.compare(0, 5, "name:") == 0)
		{
			std::string v = line.substr(5);
			v.erase(0, v.find_first_not_of(" \t"));
			if (!v.empty())
				return v;
		}
	return folder;
}

uint32_t TitleOf(const std::string& key)
{
	if (key.size() < 8)
		return 0;
	uint32_t v = 0;
	for (size_t i = 0; i < 8; i++)
	{
		const char c = key[i];
		const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
		if (d < 0)
			return 0;
		v = v << 4 | static_cast<uint32_t>(d);
	}
	return v;
}

/// patches.db's patches: id -> (title ID, name, the block's text)
struct Patch
{
	std::string title, name, text;
};
std::map<std::string, Patch> Patches(const std::string& db)
{
	std::map<std::string, Patch> out;
	std::string id;
	for (const std::string& line : Lines(db))
	{
		if (line.compare(0, 2, "P\t") == 0)
		{
			std::vector<std::string> f;
			size_t at = 0;
			for (;;)
			{
				const size_t tab = line.find('\t', at);
				f.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
				if (tab == std::string::npos)
					break;
				at = tab + 1;
			}
			id = f.size() > 3 ? f[1] + "/" + f[3] : std::string();
			if (!id.empty())
				out[id] = { f[1], f.size() > 4 ? f[4] : f[3], std::string() };
		}
		if (!id.empty())
			out[id].text += line + "\n";
	}
	return out;
}

Fixed& FixedFor(std::vector<Fixed>& list, const std::string& key)
{
	for (Fixed& f : list)
		if (f.key == key)
			return f;
	list.push_back({ TitleOf(key), key, {} });
	return list.back();
}

void AddWhat(Fixed& f, const std::string& what)
{
	if (std::find(f.what.begin(), f.what.end(), what) == f.what.end())
		f.what.push_back(what);
}

void RemoveEmptyParents(const std::string& titleDir, std::string path)
{
	for (;;)
	{
		const size_t slash = path.rfind('/');
		if (slash == std::string::npos)
			return;
		path.resize(slash);
		if (path.find('/') == std::string::npos)   // "mods" itself stays
			return;
		if (rmdir((titleDir + "/" + path).c_str()) != 0)
			return;
	}
}
} // namespace

bool Sync(const std::string& base, const std::string& titleDir, const std::string& record, const Getter& get,
	Report& report)
{
	report = Report();
	std::vector<uint8_t> body;
	const int status = get(base + "game-fixes.txt", body);
	if (status != 200)
	{
		report.error = status < 0 ? "The network could not be reached." : "GitHub did not give the list of game fixes.";
		fixes_log("fixes: the list: HTTP %d", status);
		return false;
	}
	if (body.size() > kMostList)
	{
		report.error = "The list of game fixes is too large.";
		return false;
	}
	const std::vector<std::string> lines = Lines(std::string(body.begin(), body.end()));
	if (lines.empty() || lines[0] != "# OGBox game fixes 1")
	{
		report.error = "The list of game fixes is of a kind this OGBox doesn't know.";
		fixes_log("fixes: the list's first line isn't \"# OGBox game fixes 1\": left alone");
		return false;
	}
	std::vector<Entry> entries;
	std::set<std::string> listed;
	for (size_t i = 1; i < lines.size(); i++)
	{
		const std::string& line = lines[i];
		if (line.empty() || line[0] == '#')
			continue;
		Entry e;
		const size_t a = line.find(' '), b = a == std::string::npos ? a : line.find(' ', a + 1);
		if (b == std::string::npos)
			continue;
		e.sha = line.substr(0, a);
		e.bytes = strtoull(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
		e.path = line.substr(b + 1);
		if (!IsHex64(e.sha) || e.bytes > kMostBytes || !SafePath(e.path) || !listed.insert(e.path).second)
		{
			fixes_log("fixes: line %zu of the list refused: %s", i + 1, line.c_str());
			continue;
		}
		entries.push_back(e);
		if (entries.size() > kMostFiles)
		{
			report.error = "The list of game fixes is too long.";
			return false;
		}
	}

	std::string recorded;
	std::set<std::string> fetchedBefore;
	if (ReadAll(record, recorded, kMostList))
		for (const std::string& l : Lines(recorded))
			if (SafePath(l))
				fetchedBefore.insert(l);
	std::set<std::string> fetchedNow = fetchedBefore;

	for (const Entry& e : entries)
	{
		const std::string place = titleDir + "/" + e.path;
		std::string have;
		const bool had = ReadAll(place, have, kMostBytes);
		if (had && have.size() == e.bytes && PS5Update::Sha256Hex(have.data(), have.size()) == e.sha)
			continue;
		body.clear();
		const int st = get(base + e.path, body);
		if (st != 200 || body.size() != e.bytes || PS5Update::Sha256Hex(body.data(), body.size()) != e.sha)
		{
			fixes_log("fixes: %s: not fetched (HTTP %d, %zu bytes, the list says %llu)", e.path.c_str(), st, body.size(),
				static_cast<unsigned long long>(e.bytes));
			report.failed++;
			continue;
		}
		if (!WriteOver(place, body.data(), body.size()))
		{
			fixes_log("fixes: %s: cannot be written (%s)", e.path.c_str(), strerror(errno));
			report.failed++;
			continue;
		}
		report.fetched++;
		fetchedNow.insert(e.path);
		fixes_log("fixes: %s %s", had ? "updated" : "added", e.path.c_str());

		/* the notice: a mod by its game and name; a patch database by the patches that changed */
		if (e.path.compare(0, 5, "mods/") == 0)
		{
			const size_t s1 = e.path.find('/', 5), s2 = e.path.find('/', s1 + 1);
			const std::string key = e.path.substr(5, s1 - 5), mod = e.path.substr(s1 + 1, s2 - s1 - 1);
			std::string modTxt;
			ReadAll(titleDir + "/mods/" + key + "/" + mod + "/mod.txt", modTxt, kMostBytes);
			AddWhat(FixedFor(report.fixed, key), ModName(modTxt, mod));
		}
		else if (e.path == "gamedata/patches.db")
		{
			const std::map<std::string, Patch> before = Patches(have), after = Patches(std::string(body.begin(), body.end()));
			for (const auto& [id, p] : after)
			{
				const auto old = before.find(id);
				if (old == before.end() || old->second.text != p.text)
					AddWhat(FixedFor(report.fixed, p.title), p.name + " patch");
			}
		}
	}

	/* a mod file this fetched before and the list no longer has: its fix was withdrawn */
	for (const std::string& path : fetchedBefore)
	{
		if (listed.count(path) || path.compare(0, 5, "mods/") != 0)
			continue;
		const std::string place = titleDir + "/" + path;
		fetchedNow.erase(path);
		if (unlink(place.c_str()) != 0)
			continue;
		report.removed++;
		fixes_log("fixes: removed %s (no longer on GitHub)", path.c_str());
		const size_t s1 = path.find('/', 5), s2 = path.find('/', s1 + 1);
		AddWhat(FixedFor(report.fixed, path.substr(5, s1 - 5)), "(removed) " + path.substr(s1 + 1, s2 - s1 - 1));
		RemoveEmptyParents(titleDir, path);
	}

	if (fetchedNow != fetchedBefore)
	{
		std::string text;
		for (const std::string& p : fetchedNow)
			text += p + "\n";
		if (!WriteOver(record, text.data(), text.size()))
			fixes_log("fixes: the record %s cannot be written", record.c_str());
	}
	report.ok = report.failed == 0;
	if (!report.ok)
		report.error = std::to_string(report.failed) + " game fix files could not be fetched.";
	fixes_log("fixes: %zu files listed; %u fetched, %u removed, %u failed", entries.size(), report.fetched,
		report.removed, report.failed);
	return report.ok;
}

#if !defined(OGBOX_FIXES_TEST)
namespace
{
std::mutex g_mutex;
bool g_started = false, g_done = false, g_taken = false;
Report g_report;
} // namespace

void Start(const std::string& titleDir)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_started)
			return;
		g_started = true;
	}
	std::thread([titleDir]() {
		Report r;
		Sync("https://raw.githubusercontent.com/Press5elect/OGBox/main/ps5/", titleDir, "/data/ogbox/fixes-fetched.txt",
			[](const std::string& url, std::vector<uint8_t>& out) { return OGBoxNet::Get(url, out, 60); }, r);
		std::lock_guard<std::mutex> lock(g_mutex);
		g_report = std::move(r);
		g_done = true;
	}).detach();
}

bool Poll(Report& report)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_done || g_taken)
		return false;
	g_taken = true;
	report = g_report;
	return true;
}
#endif
} // namespace ogbox::fixes
