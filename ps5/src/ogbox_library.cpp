/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The game list's database, covers and history (ogbox_library.h). The covers are libretro's
 * thumbnails (github.com/libretro-thumbnails/Microsoft_-_Xbox, served at thumbnails.libretro.com),
 * fetched over plain HTTP with the title's own small client: one request at a time, each kept in
 * /data/ogbox/covers as <name>.png, or <name>.none when libretro has none (asked again after a week).
 */
#include "ogbox_library.h"
#include "ogbox_storage.h"

#include "platform/ps5_bootlog.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef OGBOX_PLAYED_FILE
#define OGBOX_PLAYED_FILE "/data/ogbox/played.txt"
#endif

namespace ogbox {

namespace {

std::string lower(std::string s)
{
	for (char &c : s) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

constexpr const char *kPlayedFile = OGBOX_PLAYED_FILE;
/* 0.1.48: in the data folder (ogbox_storage.h); OGBOX_COVERS_DIR for the tests */
std::string coversDir()
{
#ifdef OGBOX_COVERS_DIR
	return OGBOX_COVERS_DIR;
#else
	return storagePath("covers");
#endif
}
constexpr const char *kHost = "thumbnails.libretro.com";
constexpr const char *kSystem = "/Microsoft%20-%20Xbox/Named_Boxarts/";

/* libretro's file names: these characters become underscores. */
std::string libretroName(const std::string &name)
{
	std::string out = name;
	for (char &c : out) {
		if (std::strchr("&*/:`<>?\\|\"", c)) {
			c = '_';
		}
	}
	return out;
}

std::string urlEncode(const std::string &s)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
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

/* GET http://host/path: the status (0 when the network failed) and the body. */
int httpGet(const std::string &host, const std::string &path, std::string &body, std::string &location)
{
	body.clear();
	location.clear();
	addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo *res = nullptr;
	if (getaddrinfo(host.c_str(), "80", &hints, &res) != 0 || !res) {
		return 0;
	}
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		freeaddrinfo(res);
		return 0;
	}
	const int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	int rc = connect(fd, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	if (rc != 0 && errno == EINPROGRESS) {
		pollfd p{ fd, POLLOUT, 0 };
		rc = poll(&p, 1, 10000) == 1 ? 0 : -1;
		int err = 0;
		socklen_t len = sizeof(err);
		if (rc == 0 && (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0)) {
			rc = -1;
		}
	}
	if (rc != 0) {
		close(fd);
		return 0;
	}
	fcntl(fd, F_SETFL, flags);
	timeval tv{ 20, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
		"\r\nUser-Agent: OGBox (PlayStation 5 homebrew)\r\nAccept: image/png\r\nConnection: close\r\n\r\n";
	if (send(fd, request.data(), request.size(), 0) != static_cast<ssize_t>(request.size())) {
		close(fd);
		return 0;
	}
	std::string data;
	char chunk[16384];
	for (;;) {
		const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
		if (n <= 0) {
			break;
		}
		data.append(chunk, static_cast<size_t>(n));
		if (data.size() > (16u << 20)) {
			break;
		}
	}
	close(fd);
	const size_t end = data.find("\r\n\r\n");
	if (end == std::string::npos || data.compare(0, 5, "HTTP/") != 0) {
		return 0;
	}
	const size_t sp = data.find(' ');
	const int status = sp != std::string::npos ? std::atoi(data.c_str() + sp + 1) : 0;
	const std::string head = lower(data.substr(0, end));
	const size_t loc = head.find("\r\nlocation:");
	if (loc != std::string::npos) {
		size_t a = loc + 11;
		while (a < end && data[a] == ' ') {
			a++;
		}
		location = data.substr(a, data.find("\r\n", a) - a);
	}
	body = data.substr(end + 4);
	if (head.find("transfer-encoding: chunked") != std::string::npos) {
		std::string plain;
		size_t at = 0;
		while (at < body.size()) {
			const size_t le = body.find("\r\n", at);
			if (le == std::string::npos) {
				break;
			}
			const size_t len = std::strtoul(body.c_str() + at, nullptr, 16);
			if (!len) {
				break;
			}
			plain.append(body, le + 2, len);
			at = le + 2 + len + 2;
		}
		body.swap(plain);
	}
	return status;
}

bool writeFile(const std::string &path, const std::string &data)
{
	const std::string tmp = path + ".part";
	FILE *f = std::fopen(tmp.c_str(), "wb");
	if (!f) {
		return false;
	}
	const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
	if (std::fclose(f) != 0 || !ok) {
		std::remove(tmp.c_str());
		return false;
	}
	return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool exists(const std::string &path, double *mtime = nullptr)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		return false;
	}
	if (mtime) {
		*mtime = static_cast<double>(st.st_mtime);
	}
	return true;
}

} // namespace

// ---- the database ------------------------------------------------------------------------------

GameDb &GameDb::get()
{
	static GameDb db;
	return db;
}

GameDb::GameDb()
{
	const std::string path = std::string(pshakux_title_dir()) + "/gamedata/games.db";
	FILE *f = std::fopen(path.c_str(), "r");
	if (!f) {
		pshakux_log("library: %s cannot be read: no disc database", path.c_str());
		return;
	}
	char line[1024];
	while (std::fgets(line, sizeof(line), f)) {
		if (line[0] == '#') {
			continue;
		}
		std::string l(line);
		while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) {
			l.pop_back();
		}
		std::vector<std::string> f5;
		size_t a = 0;
		for (int i = 0; i < 5; i++) {
			const size_t b = i < 4 ? l.find('\t', a) : std::string::npos;
			f5.push_back(l.substr(a, b == std::string::npos ? std::string::npos : b - a));
			if (b == std::string::npos) {
				break;
			}
			a = b + 1;
		}
		if (f5.size() < 5) {
			continue;
		}
		DiscRecord r;
		r.titleId = static_cast<uint32_t>(std::strtoul(f5[0].c_str(), nullptr, 16));
		r.region = f5[1];
		r.languages = f5[2];
		r.serial = f5[3];
		r.name = f5[4];
		records.push_back(std::move(r));
	}
	std::fclose(f);
	pshakux_log("library: %zu discs in the Redump database (%s)", records.size(), path.c_str());
}

const DiscRecord *GameDb::byFileName(const std::string &fileName) const
{
	const std::string stem = lower(stemOf(fileName));
	for (const DiscRecord &r : records) {
		if (lower(r.name) == stem) {
			return &r;
		}
	}
	return nullptr;
}

const DiscRecord *GameDb::byTitle(uint32_t titleId, const std::string &fileName, uint32_t regions) const
{
	if (const DiscRecord *r = byFileName(fileName)) {
		if (r->titleId == titleId) {
			return r;
		}
	}
	const DiscRecord *first = nullptr, *region = nullptr;
	for (const DiscRecord &r : records) {
		if (r.titleId != titleId) {
			continue;
		}
		if (!first) {
			first = &r;
		}
		const bool na = r.region.find("USA") != std::string::npos || r.region == "World";
		const bool jp = r.region.find("Japan") != std::string::npos || r.region.find("Asia") != std::string::npos;
		const bool eu = !na && !jp;
		const bool extra = r.name.find("Demo") != std::string::npos || r.name.find("Beta") != std::string::npos ||
			r.name.find("Proto") != std::string::npos || r.name.find("Kiosk") != std::string::npos;
		if (!region && !extra && (((regions & 1) && na) || ((regions & 2) && jp) || ((regions & 4) && eu))) {
			region = &r;
		}
	}
	return region ? region : first;
}

std::string stemOf(const std::string &fileName)
{
	const size_t slash = fileName.find_last_of('/');
	std::string s = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
	const size_t dot = s.rfind('.');
	if (dot != std::string::npos) {
		const std::string ext = lower(s.substr(dot + 1));
		if (ext == "iso" || ext == "xiso" || ext == "png") {
			s.erase(dot);
		}
	}
	return s;
}

std::string shortName(const std::string &fileName)
{
	std::string s = stemOf(fileName), out;
	int depth = 0;
	for (char c : s) {
		if (c == '(' || c == '[') {
			depth++;
		} else if ((c == ')' || c == ']') && depth > 0) {
			depth--;
		} else if (!depth) {
			out += c;
		}
	}
	while (!out.empty() && out.back() == ' ') {
		out.pop_back();
	}
	return out.empty() ? s : out;
}

// ---- the covers --------------------------------------------------------------------------------

struct Covers::Impl {
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<std::string> queue;
	std::set<std::string> asked;
	std::atomic<bool> on{ false };
	bool started{ false };
	unsigned downloaded{ 0 }, missing{ 0 }, failed{ 0 };
	double offlineUntil{ 0.0 };

	static std::string pathOf(const std::string &name, const char *ext)
	{
		return coversDir() + "/" + libretroName(name) + ext;
	}

	void run()
	{
		pshakux_thread_named("ogbox-covers");
		for (;;) {
			std::string name;
			{
				std::unique_lock<std::mutex> lock(mutex);
				cv.wait(lock, [&] { return !queue.empty() && on.load(); });
				name = queue.front();
				queue.pop_front();
			}
			const double now = static_cast<double>(std::time(nullptr));
			if (now < offlineUntil) {
				std::lock_guard<std::mutex> lock(mutex);
				asked.erase(name);      // asked again later
				continue;
			}
			std::string body, location, host = kHost;
			std::string path = kSystem + urlEncode(libretroName(name)) + ".png";
			int status = 0;
			for (int hop = 0; hop < 3; hop++) {
				status = httpGet(host, path, body, location);
				if ((status == 301 || status == 302 || status == 307 || status == 308) &&
					location.compare(0, 7, "http://") == 0) {
					const size_t slash = location.find('/', 7);
					host = location.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
					path = slash == std::string::npos ? "/" : location.substr(slash);
					continue;
				}
				break;
			}
			std::lock_guard<std::mutex> lock(mutex);
			if (status == 200 && body.size() > 8 && !body.compare(0, 8, "\x89PNG\r\n\x1a\n", 8)) {
				if (writeFile(pathOf(name, ".png"), body)) {
					downloaded++;
					pshakux_log("covers: %s (%zu bytes)", name.c_str(), body.size());
				}
			} else if (status == 404) {
				writeFile(pathOf(name, ".none"), std::string());
				missing++;
				pshakux_log("covers: %s: libretro has none", name.c_str());
			} else {
				failed++;
				asked.erase(name);
				if (status == 0) {
					offlineUntil = now + 60.0;  // no network: a minute's rest
					pshakux_log("covers: %s: no answer from %s (the network?): asking again in a minute", name.c_str(), kHost);
				} else {
					pshakux_log("covers: %s: HTTP %d", name.c_str(), status);
				}
			}
		}
	}
};

Covers &Covers::get()
{
	static Covers covers;
	return covers;
}

Covers::Covers() : impl(new Impl) {}

void Covers::enable(bool on)
{
	std::lock_guard<std::mutex> lock(impl->mutex);
	impl->on = on;
	if (on && !impl->started) {
		impl->started = true;
		mkdir(coversDir().c_str(), 0777);
		std::thread([this] { impl->run(); }).detach();
	}
	impl->cv.notify_all();
}

Covers::State Covers::find(const std::string &name, std::string &path)
{
	if (name.empty()) {
		return State::None;
	}
	const std::string png = Impl::pathOf(name, ".png");
	if (exists(png)) {
		path = png;
		return State::Ready;
	}
	double when = 0.0;
	if (exists(Impl::pathOf(name, ".none"), &when) && static_cast<double>(std::time(nullptr)) - when < 7 * 86400.0) {
		return State::Missing;
	}
	std::lock_guard<std::mutex> lock(impl->mutex);
	if (!impl->on) {
		return State::None;
	}
	if (impl->asked.insert(name).second) {
		impl->queue.push_back(name);
		impl->cv.notify_all();
	}
	return State::Queued;
}

std::string Covers::status() const
{
	std::lock_guard<std::mutex> lock(impl->mutex);
	char text[160];
	std::snprintf(text, sizeof(text), "%u downloaded, %u not on libretro, %zu waiting%s", impl->downloaded,
		impl->missing, impl->queue.size(), impl->offlineUntil > static_cast<double>(std::time(nullptr)) ? " (no network)" : "");
	return text;
}

// ---- the history -------------------------------------------------------------------------------

namespace {
std::mutex g_playedMutex;
std::unordered_map<std::string, double> g_played;
bool g_playedRead = false;

void readPlayed()
{
	if (g_playedRead) {
		return;
	}
	g_playedRead = true;
	FILE *f = std::fopen(kPlayedFile, "r");
	if (!f) {
		return;
	}
	char line[1200];
	while (std::fgets(line, sizeof(line), f)) {
		char *tab = std::strchr(line, '\t');
		if (!tab) {
			continue;
		}
		*tab = 0;
		std::string path(tab + 1);
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
			path.pop_back();
		}
		g_played[path] = std::atof(line);
	}
	std::fclose(f);
}
} // namespace

void notePlayed(const std::string &path)
{
	std::lock_guard<std::mutex> lock(g_playedMutex);
	readPlayed();
	g_played[path] = static_cast<double>(std::time(nullptr));
	std::vector<std::pair<double, std::string>> all;
	for (const auto &kv : g_played) {
		all.emplace_back(kv.second, kv.first);
	}
	std::sort(all.rbegin(), all.rend());
	if (all.size() > 500) {
		all.resize(500);
	}
	std::string text;
	for (const auto &e : all) {
		char when[32];
		std::snprintf(when, sizeof(when), "%.0f\t", e.first);
		text += when + e.second + "\n";
	}
	writeFile(kPlayedFile, text);
}

double playedAt(const std::string &path)
{
	std::lock_guard<std::mutex> lock(g_playedMutex);
	readPlayed();
	const auto it = g_played.find(path);
	return it == g_played.end() ? 0.0 : it->second;
}

} // namespace ogbox
