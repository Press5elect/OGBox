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
 * 0.0.3: first MobCat's scans of the boxes (github.com/MobCat/MobCats-original-xbox-game-list, over
 * HTTPS with curl: the front cut from the scan, scaled and kept as mc-<stem>.jpg), and last the
 * disc's own title image (its default.xbe's, kept as xbe-<title ID>.png).
 */
#include "ogbox_library.h"
#include "ogbox_storage.h"

#include "platform/ps5_bootlog.h"
#include "platform/ps5_update_net.h"
#include "core/ogbox_xpr.h"

#include "json.hpp"      // (tinygltf's copy of nlohmann's JSON)
#include "stb_image.h"   // (its implementation is the base class's: base/VulkanglTFModel.cpp)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

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

std::mutex &discMutex()
{
	static std::mutex m;
	return m;
}

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
	byName.reserve(records.size());
	for (size_t i = 0; i < records.size(); i++) {
		byName.emplace(lower(records[i].name), i);   // (the first of a name, as the walk found)
	}
	pshakux_log("library: %zu discs in the Redump database (%s)", records.size(), path.c_str());
}

const DiscRecord *GameDb::byFileName(const std::string &fileName) const
{
	const auto it = byName.find(lower(stemOf(fileName)));
	return it == byName.end() ? nullptr : &records[it->second];
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

std::vector<const DiscRecord *> GameDb::allByTitle(uint32_t titleId, const std::string &fileName, uint32_t regions) const
{
	std::vector<const DiscRecord *> out;
	if (const DiscRecord *best = byTitle(titleId, fileName, regions)) {
		out.push_back(best);
	}
	for (const DiscRecord &r : records) {
		if (r.titleId == titleId && (out.empty() || &r != out.front())) {
			out.push_back(&r);
		}
	}
	return out;
}

std::vector<std::string> coverNames(const std::string &fileName, uint32_t titleId, uint32_t regions)
{
	std::vector<std::string> names;
	const auto add = [&](const std::string &n) {
		if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end()) {
			names.push_back(n);
		}
	};
	const GameDb &db = GameDb::get();
	if (!fileName.empty()) {
		if (const DiscRecord *r = db.byFileName(fileName)) {
			add(r->name);                   // the database's spelling (libretro's case)
		}
		add(stemOf(fileName));
	}
	if (titleId) {
		for (const DiscRecord *r : db.allByTitle(titleId, fileName, regions)) {
			add(r->name);
		}
	}
	return names;
}

namespace {
std::mutex g_mcMutex;
std::vector<std::string> g_mc;   // the stems, sorted
bool g_mcRead = false;

const std::vector<std::string> &mobcatIndex()
{
	std::lock_guard<std::mutex> lock(g_mcMutex);
	if (!g_mcRead) {
		g_mcRead = true;
		FILE *f = std::fopen((std::string(pshakux_title_dir()) + "/gamedata/covers-mobcat.txt").c_str(), "r");
		if (f) {
			char line[160];
			while (std::fgets(line, sizeof(line), f)) {
				std::string l(line);
				while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) {
					l.pop_back();
				}
				if (!l.empty() && l[0] != '#') {
					g_mc.push_back(l);
				}
			}
			std::fclose(f);
			std::sort(g_mc.begin(), g_mc.end());
		}
		pshakux_log("covers: MobCat's index: %zu scans", g_mc.size());
	}
	return g_mc;
}

/* a region letter of MobCat's (the certificate's bits: A 1, J 2, K 3, E 4, L 5, W 7) */
uint32_t regionBits(char c)
{
	switch (c) {
	case 'A': return 1;
	case 'J': return 2;
	case 'K': return 3;
	case 'E': return 4;
	case 'L': return 5;
	case 'W': return 7;
	default: return 0;
	}
}
} // namespace

std::string mobcatCover(uint32_t titleId, const std::string &serial, uint32_t version, uint32_t regions)
{
	/* the serial without its dash: the record's, else the title ID's (two letters, the number) */
	std::string sn;
	for (char c : serial) {
		if (c != '-' && c != ' ') {
			sn += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
	}
	if (sn.size() < 5 && titleId) {
		char b[16];
		std::snprintf(b, sizeof(b), "%c%c%03u", static_cast<char>(titleId >> 24), static_cast<char>(titleId >> 16),
			static_cast<unsigned>(titleId & 0xFFFF));
		sn = b;
	}
	if (sn.size() < 5) {
		return std::string();
	}
	const std::vector<std::string> &idx = mobcatIndex();
	std::string best;
	int bestScore = -1;
	for (auto it = std::lower_bound(idx.begin(), idx.end(), sn); it != idx.end() && !it->compare(0, sn.size(), sn); ++it) {
		const std::string &st = *it;
		if (st.size() < sn.size() + 3 || !std::isdigit(static_cast<unsigned char>(st[sn.size()])) ||
			!std::isdigit(static_cast<unsigned char>(st[sn.size() + 1]))) {
			continue;   // (another serial that starts the same: MS01 and MS013)
		}
		const unsigned ver = static_cast<unsigned>(std::atoi(st.substr(sn.size(), 2).c_str()));
		const uint32_t rb = regionBits(st[sn.size() + 2]);
		int score = static_cast<int>(ver);                              // the later release
		score += st.size() == sn.size() + 3 ? 40 : 0;                 // a plain scan, not a variant's
		/* a variant's scan named for the market wanted ("-NTSC-U-Platinum", "-PAL-GBR"): that box's */
		const std::string var = st.size() > sn.size() + 3 ? st.substr(sn.size() + 3) : std::string();
		if (!var.empty() && (((regions & 1) && var.find("NTSC-U") != std::string::npos) ||
				((regions & 2) && var.find("NTSC-J") != std::string::npos) ||
				((regions & 4) && var.find("PAL") != std::string::npos))) {
			score += 60;
		}
		score += regions && (rb & regions) ? 200 : 0;                 // the regions'
		score += regions && rb == regions ? 50 : 0;
		score += version && ver == (version & 0xFF) ? 400 : 0;        // this very release
		if (score > bestScore) {
			bestScore = score;
			best = st;
		}
	}
	return best;
}

std::vector<std::string> coverSources(const std::string &fileName, const std::string &path, uint32_t titleId,
	uint32_t regions, uint32_t version)
{
	const GameDb &db = GameDb::get();
	const DiscRecord *rec = titleId ? db.byTitle(titleId, fileName, regions) : db.byFileName(fileName);
	const uint32_t given = titleId, givenRegions = regions;   // libretro's names as before (the disc's once read)
	if (!titleId && rec) {
		titleId = rec->titleId;
	}
	if (!regions && rec) {
		const std::string r = lower(rec->region);
		regions = r.find("usa") != std::string::npos || r.find("canada") != std::string::npos ? 1
			: r.find("japan") != std::string::npos || r.find("asia") != std::string::npos || r.find("korea") != std::string::npos ? 2
			: r.empty() ? 0 : 4;
	}
	std::vector<std::string> out;
	const std::string mc = mobcatCover(titleId, rec ? rec->serial : std::string(), version, regions);
	if (!mc.empty()) {
		out.push_back("@mc:" + mc);
	}
	for (const std::string &n : coverNames(fileName, given, givenRegions)) {
		out.push_back(n);
	}
	if (titleId && !path.empty()) {
		char b[16];
		std::snprintf(b, sizeof(b), "%08X", titleId);
		out.push_back(std::string("@xbe:") + b + "|" + path);
	}
	return out;
}

std::string stemOf(const std::string &fileName)
{
	const size_t slash = fileName.find_last_of('/');
	std::string s = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
	const size_t dot = s.rfind('.');
	if (dot != std::string::npos) {
		const std::string ext = lower(s.substr(dot + 1));
		if (ext == "iso" || ext == "xiso" || ext == "chd" || ext == "cci" || ext == "png") {
			s.erase(dot);
			/* 0.0.3: a split image's first part: "Halo 2 (USA).1" is "Halo 2 (USA)" */
			if (s.size() > 2 && s[s.size() - 2] == '.' && s.back() == '1') {
				s.erase(s.size() - 2);
			}
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
	unsigned downloaded{ 0 }, missing{ 0 }, failed{ 0 }, scans{ 0 }, images{ 0 };
	unsigned batchDone{ 0 }, batchTotal{ 0 };
	bool busy{ false };
	double offlineUntil{ 0.0 };

	static bool mobcat(const std::string &name) { return !name.compare(0, 4, "@mc:"); }
	static bool xbe(const std::string &name) { return !name.compare(0, 5, "@xbe:"); }

	static std::string pathOf(const std::string &name, const char *ext)
	{
		if (mobcat(name)) {
			return coversDir() + "/mc-" + name.substr(4) + (std::strcmp(ext, ".png") ? ext : ".jpg");
		}
		if (xbe(name)) {
			return coversDir() + "/xbe-" + name.substr(5, 8) + ext;
		}
		return coversDir() + "/" + libretroName(name) + ext;
	}

	/* MobCat's scan: the box's front (the right of the scan: back, spine, front), at most 720 high, as
	 * a JPEG. The status (-1 the network). */
	int fetchMobcat(const std::string &name, size_t &bytes)
	{
		const std::string stem = name.substr(4);
		const std::string url = "https://raw.githubusercontent.com/MobCat/MobCats-original-xbox-game-list/main/cover/" +
			stem.substr(0, 2) + "/" + stem + ".jpg";
		std::vector<uint8_t> body;
		const int status = OGBoxNet::Get(url, body, 60);
		if (status != 200) {
			return status;
		}
		int w = 0, h = 0, n = 0;
		stbi_uc *px = stbi_load_from_memory(body.data(), static_cast<int>(body.size()), &w, &h, &n, 3);
		if (!px || w < 16 || h < 16) {
			if (px) {
				stbi_image_free(px);
			}
			return 415;   // (not a picture: kept as none)
		}
		/* a wrap-around scan is wider than high: its front is the right 47.4% (Xbox cases: two
		 * 129 mm covers and a 14 mm spine) */
		const int fx = w > h ? w - static_cast<int>(w * 0.474f + 0.5f) : 0, fw = w - fx;
		const int oh = std::min(h, 720), ow = std::max(1, static_cast<int>(static_cast<int64_t>(fw) * oh / h));
		std::vector<uint8_t> out(static_cast<size_t>(ow) * oh * 3);
		for (int y = 0; y < oh; y++) {
			const int y0 = static_cast<int>(static_cast<int64_t>(y) * h / oh), y1 = std::max(y0 + 1,
				static_cast<int>(static_cast<int64_t>(y + 1) * h / oh));
			for (int x = 0; x < ow; x++) {
				const int x0 = fx + static_cast<int>(static_cast<int64_t>(x) * fw / ow);
				const int x1 = std::max(x0 + 1, fx + static_cast<int>(static_cast<int64_t>(x + 1) * fw / ow));
				unsigned sum[3] = { 0, 0, 0 }, cnt = 0;
				for (int yy = y0; yy < y1; yy += (y1 - y0 > 4 ? 2 : 1)) {
					for (int xx = x0; xx < x1; xx += (x1 - x0 > 4 ? 2 : 1)) {
						const stbi_uc *p = px + (static_cast<size_t>(yy) * w + xx) * 3;
						sum[0] += p[0];
						sum[1] += p[1];
						sum[2] += p[2];
						cnt++;
					}
				}
				uint8_t *o = &out[(static_cast<size_t>(y) * ow + x) * 3];
				for (int k = 0; k < 3; k++) {
					o[k] = static_cast<uint8_t>(sum[k] / cnt);
				}
			}
		}
		stbi_image_free(px);
		const std::string dst = pathOf(name, ".jpg"), tmp = dst + ".part";
		if (!stbi_write_jpg(tmp.c_str(), ow, oh, 3, out.data(), 88) || std::rename(tmp.c_str(), dst.c_str()) != 0) {
			std::remove(tmp.c_str());
			return 500;
		}
		bytes = body.size();
		return 200;
	}

	/* the disc's default.xbe's title image, as a PNG: 200, or 404 when it has none */
	int fetchXbe(const std::string &name)
	{
		const size_t bar = name.find('|');
		if (bar == std::string::npos) {
			return 404;
		}
		uint8_t *rgba = nullptr;
		int w = 0, h = 0;
		bool ok;
		{
			std::lock_guard<std::mutex> disc(discMutex());   // (one reader of a share's discs at a time)
			ok = ogbox_game_title_image(name.substr(bar + 1).c_str(), &rgba, &w, &h);
		}
		if (!ok) {
			return 404;
		}
		const std::string dst = pathOf(name, ".png"), tmp = dst + ".part";
		const bool written = stbi_write_png(tmp.c_str(), w, h, 4, rgba, w * 4) && std::rename(tmp.c_str(), dst.c_str()) == 0;
		std::free(rgba);
		if (!written) {
			std::remove(tmp.c_str());
		}
		return written ? 200 : 500;
	}

	void run()
	{
		pshakux_thread_named("ogbox-covers");
		for (;;) {
			std::string name;
			{
				std::unique_lock<std::mutex> lock(mutex);
				if (queue.empty()) {
					busy = false;
					batchDone = batchTotal = 0;   // the bar's next batch from nothing
				}
				cv.wait(lock, [&] { return !queue.empty() && on.load(); });
				name = queue.front();
				queue.pop_front();
				busy = true;
			}
			const double now = static_cast<double>(std::time(nullptr));
			if (!xbe(name) && now < offlineUntil) {
				std::lock_guard<std::mutex> lock(mutex);
				asked.erase(name);      // asked again later
				batchDone++;
				continue;
			}
			int status = 0;
			size_t bytes = 0;
			std::string body;
			if (mobcat(name)) {
				status = fetchMobcat(name, bytes);
				if (status == -1) {
					status = 0;
				}
			} else if (xbe(name)) {
				status = fetchXbe(name);
			} else {
				std::string location, host = kHost;
				std::string path = kSystem + urlEncode(libretroName(name)) + ".png";
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
				if (status == 200 && !(body.size() > 8 && !body.compare(0, 8, "\x89PNG\r\n\x1a\n", 8))) {
					status = 415;
				}
				if (status == 200 && writeFile(pathOf(name, ".png"), body)) {
					bytes = body.size();
				} else if (status == 200) {
					status = 500;
				}
			}
			std::lock_guard<std::mutex> lock(mutex);
			batchDone++;
			const char *from = mobcat(name) ? "MobCat" : xbe(name) ? "the disc's title image" : "libretro";
			if (status == 200) {
				(mobcat(name) ? scans : xbe(name) ? images : downloaded)++;
				pshakux_log("covers: %s from %s (%zu bytes)", name.c_str(), from, bytes);
			} else if (status == 404 || status == 415) {
				writeFile(pathOf(name, ".none"), std::string());
				missing++;
				pshakux_log("covers: %s: %s has none", name.c_str(), from);
			} else {
				failed++;
				asked.erase(name);
				/* 0.0.3: a minute's rest after any failure (a server's error was asked for again each frame) */
				offlineUntil = now + (status == 429 ? 300.0 : 60.0);
				if (status == 0) {
					pshakux_log("covers: %s: no answer from %s (the network?): asking again in a minute", name.c_str(), from);
				} else {
					pshakux_log("covers: %s: %s: HTTP %d: asking again in a minute", name.c_str(), from, status);
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
	/* 0.0.3: no network: the network's sources passed over for now, so the disc's own picture shows */
	if (!Impl::xbe(name) && impl->offlineUntil > static_cast<double>(std::time(nullptr))) {
		return State::Missing;
	}
	if (impl->asked.insert(name).second) {
		impl->queue.push_back(name);
		impl->batchTotal++;
		impl->cv.notify_all();
	}
	return State::Queued;
}

Covers::State Covers::findAny(const std::vector<std::string> &names, std::string &path)
{
	State last = State::None;
	for (const std::string &n : names) {
		last = find(n, path);
		if (last != State::Missing) {
			return last;                    // Ready, Queued (its answer first), or None (covers off)
		}
	}
	return last;
}

std::string Covers::status() const
{
	std::lock_guard<std::mutex> lock(impl->mutex);
	char text[200];
	std::snprintf(text, sizeof(text), "%u scans from MobCat, %u from libretro, %u title images, %u without one, %zu waiting%s",
		impl->scans, impl->downloaded, impl->images, impl->missing, impl->queue.size(),
		impl->offlineUntil > static_cast<double>(std::time(nullptr)) ? " (no network)" : "");
	return text;
}

void Covers::progress(unsigned &done, unsigned &total) const
{
	std::lock_guard<std::mutex> lock(impl->mutex);
	done = impl->batchDone;
	total = impl->busy || !impl->queue.empty() ? impl->batchTotal : 0;
}

// ---- 0.0.3: the descriptions -------------------------------------------------------------------

struct About::Impl {
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<std::string> queue;
	std::set<std::string> asked;
	std::atomic<bool> on{ false };
	bool started{ false };
	double offlineUntil{ 0.0 };

	static std::string pathOf(const std::string &name, const char *ext)
	{
		return coversDir() + "/about-" + libretroName(name) + ext;
	}

	/* the article: "", or its key and title; status -1 the network */
	static int search(const std::string &name, std::string &key)
	{
		std::vector<uint8_t> body;
		const std::string q = urlEncode(name + " video game");
		const int status = OGBoxNet::Get("https://en.wikipedia.org/w/rest.php/v1/search/page?q=" + q + "&limit=5", body, 30);
		if (status != 200) {
			return status;
		}
		const nlohmann::json j = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
		if (j.is_discarded() || !j.count("pages") || !j["pages"].is_array()) {
			return 404;
		}
		for (const auto &pg : j["pages"]) {
			const std::string d = pg.count("description") && pg["description"].is_string()
				? lower(pg["description"].get<std::string>()) : std::string();
			if (d.find("video game") != std::string::npos && pg.count("key") && pg["key"].is_string()) {
				key = pg["key"].get<std::string>();
				return 200;
			}
		}
		return 404;
	}

	void run()
	{
		pshakux_thread_named("ogbox-about");
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
				asked.erase(name);
				continue;
			}
			std::string key, text, title;
			int status = search(name, key);
			if (status == 200) {
				std::vector<uint8_t> body;
				status = OGBoxNet::Get("https://en.wikipedia.org/api/rest_v1/page/summary/" + urlEncode(key), body, 30);
				if (status == 200) {
					const nlohmann::json j = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
					if (!j.is_discarded() && j.count("extract") && j["extract"].is_string()) {
						text = j["extract"].get<std::string>();
						title = j.count("title") && j["title"].is_string() ? j["title"].get<std::string>() : key;
					}
					status = text.empty() ? 404 : 200;
				}
			}
			std::lock_guard<std::mutex> lock(mutex);
			if (status == 200) {
				writeFile(pathOf(name, ".txt"), title + "\n" + text);
				pshakux_log("about: %s: Wikipedia's \"%s\" (%zu characters)", name.c_str(), title.c_str(), text.size());
			} else if (status == 404) {
				writeFile(pathOf(name, ".none"), std::string());
				pshakux_log("about: %s: no Wikipedia article about the game", name.c_str());
			} else {
				asked.erase(name);
				/* no network, or Wikipedia busy (429: its rate limit): a rest before the next */
				offlineUntil = now + (status == 429 ? 300.0 : 60.0);
				pshakux_log("about: %s: Wikipedia: %s", name.c_str(), status <= 0 ? "no answer (the network?)" :
					("HTTP " + std::to_string(status)).c_str());
			}
		}
	}
};

About &About::get()
{
	static About about;
	return about;
}

About::About() : impl(new Impl) {}

void About::enable(bool on)
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

bool About::find(const std::string &name, std::string &text, std::string &article)
{
	if (name.empty()) {
		return false;
	}
	const std::string path = Impl::pathOf(name, ".txt");
	if (FILE *f = std::fopen(path.c_str(), "r")) {
		std::string all;
		char buf[4096];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
			all.append(buf, n);
		}
		std::fclose(f);
		const size_t nl = all.find('\n');
		article = nl == std::string::npos ? std::string() : all.substr(0, nl);
		text = nl == std::string::npos ? all : all.substr(nl + 1);
		return !text.empty();
	}
	double when = 0.0;
	if (exists(Impl::pathOf(name, ".none"), &when) && static_cast<double>(std::time(nullptr)) - when < 30 * 86400.0) {
		return false;
	}
	std::lock_guard<std::mutex> lock(impl->mutex);
	if (impl->on && impl->asked.insert(name).second) {
		impl->queue.push_back(name);
		impl->cv.notify_all();
	}
	return false;
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

std::vector<std::pair<double, std::string>> recentlyPlayed(size_t n)
{
	std::lock_guard<std::mutex> lock(g_playedMutex);
	readPlayed();
	std::vector<std::pair<double, std::string>> all;
	for (const auto &kv : g_played) {
		all.emplace_back(kv.second, kv.first);
	}
	std::sort(all.rbegin(), all.rend());
	if (all.size() > n) {
		all.resize(n);
	}
	return all;
}

} // namespace ogbox
