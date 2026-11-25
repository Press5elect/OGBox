// OGBox - the PC test of Fast writes on the console's storage (ps5/src/platform/ps5_wbremote.cpp):
// the core's cache-drive write-back (hakux-native native/kernel/wback.c, the real one) handed the
// real broker (ps5/broker/server.cpp, on a socketpair in a thread, ending as the payload does)
// through the real client. Random workloads (writes at random offsets, runs of 4 KiB, truncates,
// renames over, reads and sizes checked while held) run in a child each: with OGBox's own writes,
// through the broker, and with the broker dying part-way (it ends after a number of requests, the
// last carried out and not answered). The files at the end must be the model's, byte for byte,
// and the same in the three.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#include "platform/ps5_broker.h"
#include "platform/ps5_wbremote.h"
#include "../../ps5/broker/server.hpp"

extern "C"
{
#include "native/kernel/wback.h"
}

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

static bool g_verbose = getenv("WBR_VERBOSE") != nullptr;
static std::vector<std::string> g_lines;

extern "C" void nc_log(const char* tag, const char* fmt, ...)
{
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	g_lines.push_back(line);
	if (g_verbose)
		fprintf(stderr, "  [%s] %s\n", tag, line);
}

static int g_fail = 0;
static void Check(bool ok, const std::string& what)
{
	fprintf(stderr, "%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
	g_fail += !ok;
}

static std::string g_root;   // the broker's "/data/..." is g_root + "/data/..."

static uint64_t g_x;
static uint32_t Rnd()
{
	g_x ^= g_x << 13;
	g_x ^= g_x >> 7;
	g_x ^= g_x << 17;
	return static_cast<uint32_t>(g_x >> 11);
}

static uint64_t InoOf(int fd)
{
	struct stat st;
	fstat(fd, &st);
	return static_cast<uint64_t>(st.st_ino);
}
static uint64_t DiskSize(int fd)
{
	struct stat st;
	fstat(fd, &st);
	return static_cast<uint64_t>(st.st_size);
}

static bool SameAs(const std::string& path, const std::vector<uint8_t>& m, uint64_t size)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<uint8_t> b(size + 1);
	const size_t n = fread(b.data(), 1, size + 1, f);
	fclose(f);
	return n == size && std::memcmp(b.data(), m.data(), size) == 0;
}

constexpr int kFiles = 3;
constexpr uint32_t kMax = 2u << 20;

struct Model
{
	std::string path;
	int fd = -1;
	uint64_t ino = 0, size = 0;
	std::vector<uint8_t> m = std::vector<uint8_t>(kMax);
};

// What a child reports.
struct Out
{
	unsigned bad, fallbacks, files_ok, used;
	uint64_t remote, local, held, requests;
	unsigned inflight;
};

// A seed's workload in dir; mode 0: OGBox's own writes, 1: the broker, 2: it dies part-way.
static Out Workload(const std::string& dir, uint64_t seed, int mode)
{
	Out out{};
	// (the broker serves until the child ends, as the payload until the title does)
	static int fds[2] = { -1, -1 };
	static ::broker::ServerOptions options;
	options.root = g_root.c_str();
	options.message_ms = 10000;
	if (mode)
	{
		g_x = seed * 0xD1B54A32D192ED03ull + 3;   // (the broker's knobs: a generator of their own)
		options.io_cap = Rnd() % 2 ? 1000 + Rnd() % 50000 : 0;
		options.die_after = mode == 2 ? 3 + Rnd() % 40 : 0;
		const ogbox::broker::HelperConnector connect = [&](std::string& why) {
			if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
				return -1;
			std::thread([]() {
				::broker::serve(fds[1], fds[1], options);
				shutdown(fds[1], SHUT_RDWR);   // (the payload's end)
			}).detach();
			why = "a socketpair";
			return fds[0];
		};
		std::string why;
		out.used = ogbox::broker::StartCacheHelper(dir + "/hdd", connect, why, g_root);
		if (!out.used)
			fprintf(stderr, "  not used: %s\n", why.c_str());
	}
	g_x = seed * 0x9E3779B97F4A7C15ull + 1;
	mkdir((dir + "/hdd").c_str(), 0777);
	nc_wb_start((dir + "/hdd/.ogbox-cache-journal").c_str(), (Rnd() % 2 ? 1u : 8u) << 20);
	Model f[kFiles];
	std::vector<uint8_t> buf(kMax), rb(kMax);
	for (int i = 0; i < kFiles; i++)
	{
		f[i].path = dir + "/hdd/f" + std::to_string(i);
		f[i].fd = open(f[i].path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
		f[i].ino = InoOf(f[i].fd);
	}
	const unsigned ops = 100 + Rnd() % 150;
	for (unsigned n = 0; n < ops; n++)
	{
		Model& x = f[Rnd() % kFiles];
		const unsigned r = Rnd() % 100;
		if (r < 50)
		{
			const uint32_t len = 1 + Rnd() % (Rnd() % 4 ? 8192 : 150000);
			const uint64_t off = Rnd() % (kMax - len);
			for (uint32_t k = 0; k < len; k++)
				buf[k] = static_cast<uint8_t>(Rnd());
			if (!nc_wb_write(x.fd, x.ino, x.path.c_str(), off, buf.data(), len) &&
				pwrite(x.fd, buf.data(), len, static_cast<off_t>(off)) != static_cast<ssize_t>(len))
				out.bad++;
			std::memcpy(x.m.data() + off, buf.data(), len);
			x.size = std::max<uint64_t>(x.size, off + len);
		}
		else if (r < 75)
		{
			const unsigned count = 1 + Rnd() % 64;
			for (unsigned c = 0; c < count && x.size + 4096 <= kMax; c++)
			{
				for (uint32_t k = 0; k < 4096; k++)
					buf[k] = static_cast<uint8_t>(Rnd());
				const uint64_t off = x.size;
				if (!nc_wb_write(x.fd, x.ino, x.path.c_str(), off, buf.data(), 4096) &&
					pwrite(x.fd, buf.data(), 4096, static_cast<off_t>(off)) != 4096)
					out.bad++;
				std::memcpy(x.m.data() + off, buf.data(), 4096);
				x.size = off + 4096;
			}
		}
		else if (r < 88)
		{
			// (io.c's way: the host's size, then wback's; asked again once: the last held write can
			// land in between, with or without the broker)
			uint64_t size = nc_wb_size(x.ino, DiskSize(x.fd));
			if (size != x.size && nc_wb_size(x.ino, DiskSize(x.fd)) != x.size)
				out.bad++;
			if (x.size)
			{
				const uint64_t off = Rnd() % x.size;
				uint32_t len = 1 + Rnd() % 200000;
				if (off + len > x.size)
					len = static_cast<uint32_t>(x.size - off);
				if (!nc_wb_read(x.fd, x.ino, off, rb.data(), len) || std::memcmp(rb.data(), x.m.data() + off, len))
					out.bad++;
			}
		}
		else if (r < 95)
		{
			const uint64_t size = Rnd() % 2 ? Rnd() % (x.size + 1) : Rnd() % kMax;
			nc_wb_truncate(x.ino, size);
			if (ftruncate(x.fd, static_cast<off_t>(size)) != 0)
				out.bad++;
			if (size < x.size)
				std::memset(x.m.data() + size, 0, x.size - size);
			x.size = size;
		}
		else
		{
			// renamed over another, which is then made again at the first's name
			Model& y = f[Rnd() % kFiles];
			if (&y == &x)
				continue;
			nc_wb_deleted(y.ino);
			close(y.fd);
			if (rename(x.path.c_str(), y.path.c_str()) != 0)
				out.bad++;
			nc_wb_renamed(x.path.c_str(), y.path.c_str());
			std::swap(x.path, y.path);
			std::swap(x, y);   // y: the moved file at its new name; x: the old name, made anew
			x.fd = open(x.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
			x.ino = InoOf(x.fd);
			x.size = 0;
			std::fill(x.m.begin(), x.m.end(), 0);
		}
	}
	nc_wb_drain_all();
	NcWbStats st;
	nc_wb_stats(&st);
	out.fallbacks = st.fallbacks;
	out.remote = st.remote_bytes;
	out.local = st.local_bytes;
	out.held = st.held;
	out.requests = st.remote_requests;
	out.inflight = st.inflight_max;
	for (int i = 0; i < kFiles; i++)
		out.files_ok += SameAs(f[i].path, f[i].m, f[i].size);
	return out;
}

static bool RunSeed(uint64_t seed, int mode, Out& out)
{
	const std::string dir = g_root + "/data/s" + std::to_string(seed) + "-" + std::to_string(mode);
	mkdir((g_root + "/data").c_str(), 0777);
	mkdir(dir.c_str(), 0777);
	int pfd[2];
	if (pipe(pfd) != 0)
		return false;
	const pid_t pid = fork();
	if (pid == 0)
	{
		close(pfd[0]);
		Out o = Workload(dir, seed, mode);
		if (write(pfd[1], &o, sizeof(o)) != sizeof(o))
			_exit(3);
		_exit(0);   // (the broker's thread with it: the payload's connection ends with the title)
	}
	close(pfd[1]);
	out = Out{};
	out.bad = 99;
	const bool got = read(pfd[0], &out, sizeof(out)) == sizeof(out);
	close(pfd[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	return got && WIFEXITED(status) && out.bad == 0 && out.files_ok == kFiles && out.held == 0;
}

static bool SameFiles(uint64_t seed, int a, int b)
{
	for (int i = 0; i < kFiles; i++)
	{
		const std::string pa = g_root + "/data/s" + std::to_string(seed) + "-" + std::to_string(a) + "/hdd/f" + std::to_string(i);
		const std::string pb = g_root + "/data/s" + std::to_string(seed) + "-" + std::to_string(b) + "/hdd/f" + std::to_string(i);
		FILE* x = fopen(pa.c_str(), "rb");
		FILE* y = fopen(pb.c_str(), "rb");
		bool same = x && y;
		while (same)
		{
			const int c = fgetc(x), d = fgetc(y);
			same = c == d;
			if (c == EOF)
				break;
		}
		if (x)
			fclose(x);
		if (y)
			fclose(y);
		if (!same)
			return false;
	}
	return true;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: wbremote_test <work folder> [seeds]\n");
		return 2;
	}
	g_root = argv[1];
	mkdir(g_root.c_str(), 0777);
	const unsigned seeds = argc > 2 ? static_cast<unsigned>(atoi(argv[2])) : 30;
	// not under /data/: not used
	{
		std::string why;
		const ogbox::broker::HelperConnector never = [](std::string& said) {
			said = "no loader";
			return -1;
		};
		bool used = ogbox::broker::StartCacheHelper("/mnt/usb0/ogbox/hdd", never, why);
		Check(!used && why.find("/data/") != std::string::npos, "a hard disk on USB storage: the broker not used (" + why + ")");
		used = ogbox::broker::StartCacheHelper("/data/ogbox/hdd", never, why);
		Check(!used && why.find("could not be started") != std::string::npos, "the broker not started: not used (" + why + ")");
	}
	unsigned ok[3] = { 0, 0, 0 }, same = 0, fallbacks = 0, used = 0, inflight = 0;
	uint64_t remote = 0, local2 = 0, requests = 0;
	for (unsigned s = 1; s <= seeds; s++)
	{
		Out o[3];
		bool all = true;
		for (int mode = 0; mode < 3; mode++)
		{
			const bool good = RunSeed(s, mode, o[mode]);
			ok[mode] += good;
			all &= good;
			if (!good)
				fprintf(stderr, "  seed %u mode %d: bad %u, files right %u of %d, held %llu\n", s, mode, o[mode].bad,
					o[mode].files_ok, kFiles, static_cast<unsigned long long>(o[mode].held));
		}
		used += o[1].used && o[2].used;
		remote += o[1].remote;
		requests += o[1].requests;
		inflight = std::max(inflight, o[1].inflight);
		fallbacks += o[2].fallbacks;
		local2 += o[2].local;
		if (o[1].fallbacks)
			all = false;
		if (all && SameFiles(s, 0, 1) && SameFiles(s, 0, 2))
		{
			same++;
			const std::string cmd = "rm -rf '" + g_root + "/data/s" + std::to_string(s) + "-'*";
			if (system(cmd.c_str()) != 0)
				fprintf(stderr, "  (not removed)\n");
		}
	}
	char line[300];
	snprintf(line, sizeof(line), "random (%u seeds): OGBox's own writes: the model's files (%u)", seeds, ok[0]);
	Check(ok[0] == seeds, line);
	snprintf(line, sizeof(line), "random: through the broker: the model's files, reads and sizes while held (%u; %.1f MiB in "
		"%llu requests, %u in flight at most)", ok[1], remote / 1048576.0, static_cast<unsigned long long>(requests), inflight);
	Check(ok[1] == seeds && used == seeds && remote > 0 && inflight <= 4, line);
	snprintf(line, sizeof(line), "random: the broker dying part-way: no byte lost (%u; %u fallbacks, %.1f MiB written by "
		"OGBox after)", ok[2], fallbacks, local2 / 1048576.0);
	Check(ok[2] == seeds && fallbacks > 0, line);
	snprintf(line, sizeof(line), "random: the three ways' files the same, byte for byte (%u of %u)", same, seeds);
	Check(same == seeds, line);
	fprintf(stderr, g_fail ? "wbremote: %d checks failed\n" : "wbremote: all checks passed\n", g_fail);
	return g_fail != 0;
}
