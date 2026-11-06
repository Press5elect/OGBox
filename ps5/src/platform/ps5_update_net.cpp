// OGBox - the updater's network (ps5_update_net.h): HTTPS through curl, with Mozilla's certificate
// list from the title folder, as Crux2's updater and texture packs have it.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.1. Crux2 gives its updater PCSX2's HTTP downloader (ps5/frontend/ps5_services.cpp)
// and its texture packs curl directly (ps5_texture_packs.cpp); OGBox has neither PCSX2 nor its
// downloader, so both requests are curl's here, set up as Crux2's texture packs set it up: the
// certificates of cacert.pem in the title folder (certifi's copy of Mozilla's list, put there by
// ps5/tools/link-title.sh), redirects followed (a release's file is served from another host), a
// transfer that brings less than 1 KiB a second for a minute stopped. A download goes to its file
// as it comes, not through memory.

#include <cstdio>
#include "ps5_update_net.h"
#include "ps5_update.h"
#include "ps5_bootlog.h"

#include <curl/curl.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{

constexpr size_t MAX_ANSWER = 16u << 20; // GitHub's releases page; the updater takes 8 MiB at most

std::string CaBundle()
{
	return std::string(pshakux_title_dir()) + "/cacert.pem";
}

std::string UserAgent()
{
	const char* version = pshakux_build_version();
	return std::string("OGBox/") + (version && *version ? version : "dev") + " (PlayStation 5 homebrew; https://github.com/Press5elect/OGBox)";
}

void Prepare(CURL* curl, const std::string& url, const std::string& agent, const std::string& ca)
{
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
}

size_t ToVector(char* data, size_t size, size_t count, void* user)
{
	auto* out = static_cast<std::vector<uint8_t>*>(user);
	const size_t bytes = size * count;
	if (out->size() + bytes > MAX_ANSWER)
		return 0; // too long: curl stops with a write error
	out->insert(out->end(), data, data + bytes);
	return bytes;
}

struct ToFile
{
	int fd = -1;
	uint64_t limit = 0, written = 0;
	bool too_large = false, write_failed = false;
	const std::function<bool(uint64_t, uint64_t)>* progress = nullptr;
	bool stopped = false;
};

size_t WriteToFile(char* data, size_t size, size_t count, void* user)
{
	auto* to = static_cast<ToFile*>(user);
	const size_t bytes = size * count;
	if (to->limit != 0 && to->written + bytes > to->limit)
	{
		to->too_large = true;
		return 0;
	}
	size_t done = 0;
	while (done < bytes)
	{
		const ssize_t n = write(to->fd, data + done, bytes - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
		{
			to->write_failed = true;
			return 0;
		}
		done += static_cast<size_t>(n);
	}
	to->written += bytes;
	return bytes;
}

int Progress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
	auto* to = static_cast<ToFile*>(user);
	if (to->progress && *to->progress &&
		!(*to->progress)(static_cast<uint64_t>(now), total > 0 ? static_cast<uint64_t>(total) : 0))
	{
		to->stopped = true;
		return 1; // stops the transfer
	}
	return 0;
}

int HttpGet(const std::string& url, std::vector<uint8_t>& out, unsigned seconds)
{
	out.clear();
	CURL* curl = curl_easy_init();
	if (!curl)
		return -1;
	const std::string agent = UserAgent(), ca = CaBundle();
	Prepare(curl, url, agent, ca);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(seconds));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &ToVector);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
	curl_slist* headers = nullptr;
	if (url.find("api.github.com") != std::string::npos)
	{
		headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
		headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	}
	const CURLcode result = curl_easy_perform(curl);
	long code = -1;
	if (result == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	else
		pshakux_log("net: %s: %s", url.c_str(), curl_easy_strerror(result));
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return static_cast<int>(code);
}

int HttpDownload(const std::string& url, const std::string& path, uint64_t limit,
	const std::function<bool(uint64_t, uint64_t)>& progress)
{
	ToFile to;
	to.limit = limit;
	to.progress = &progress;
	to.fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
	if (to.fd < 0)
	{
		pshakux_log("update: %s cannot be written: %s", path.c_str(), std::strerror(errno));
		return -1;
	}
	CURL* curl = curl_easy_init();
	if (!curl)
	{
		close(to.fd);
		return -1;
	}
	const std::string agent = UserAgent(), ca = CaBundle();
	Prepare(curl, url, agent, ca);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteToFile);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &to);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &Progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &to);
	const CURLcode result = curl_easy_perform(curl);
	long code = -1;
	if (result == CURLE_OK)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	else if (!to.stopped)
		pshakux_log("update: %s: %s%s", url.c_str(), curl_easy_strerror(result),
			to.too_large ? " (larger than allowed)" : to.write_failed ? " (the file could not be written)" : "");
	curl_easy_cleanup(curl);
	const bool synced = fsync(to.fd) == 0 || errno == EINVAL;
	close(to.fd);
	if (result != CURLE_OK || !synced)
		return -1;
	if (code == 200 && progress)
		progress(to.written, to.written);
	return static_cast<int>(code);
}

std::once_flag s_started;

} // namespace

void OGBoxUpdate::Start()
{
	std::call_once(s_started, [] {
		curl_global_init(CURL_GLOBAL_DEFAULT);
		PS5Update::Setup setup;
		setup.app_dir = pshakux_title_dir();
		/* 0.0.4: a developer build of X.Y.Z runs before X.Y.Z's release: the release is offered */
		setup.version = pshakux_build_version();
		if (!pshakux_build_release()) {
			unsigned a = 0, b = 0, c = 0;
			if (std::sscanf(setup.version.c_str(), "%u.%u.%u", &a, &b, &c) == 3) {
				char before[48];
				if (c > 0) {
					std::snprintf(before, sizeof(before), "%u.%u.%u", a, b, c - 1);
				} else if (b > 0) {
					std::snprintf(before, sizeof(before), "%u.%u.999999", a, b - 1);
				} else {
					std::snprintf(before, sizeof(before), "%u.999999.999999", a > 0 ? a - 1 : 0);
				}
				pshakux_log("update: a developer build of %s: releases from v%s on are offered", setup.version.c_str(),
					setup.version.c_str());
				setup.version = before;
			}
		}
		setup.http_get = HttpGet;
		setup.http_download = HttpDownload;
		setup.log = [](const char* line) { pshakux_log("%s", line); };
		PS5Update::Init(setup);
		const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
		pshakux_log("update: the updater is ready (curl %s, %s; certificates %s)", v ? v->version : "?",
			v && v->ssl_version ? v->ssl_version : "no TLS", CaBundle().c_str());
	});
}

int OGBoxNet::Get(const std::string& url, std::vector<uint8_t>& out, unsigned seconds)
{
	return HttpGet(url, out, seconds);
}

bool OGBoxUpdate::Recover()
{
	return PS5Update::Recover(std::string(pshakux_title_dir()) + "/", [](const char* line) { pshakux_log("%s", line); });
}
