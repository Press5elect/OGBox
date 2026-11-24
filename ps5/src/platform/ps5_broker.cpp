// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 1.0.4 - the write broker's client and the storage write test (ps5_broker.h).
//
// The write test, in <data folder>/writetest/, each phase logged with its numbers ("writetest:"):
//   A  OGBox's own writes, 16 MiB at a time, 2 GiB at most, 120 s at most: spends the title's
//      budget (expected: about 242 MiB/s for 1.3 GiB, then about 2 MiB/s)
//   B  right after, the broker's own writes, 16 MiB at a time, 4 GiB, 180 s at most: whether a
//      payload, a process of its own, is held back too (the decisive number: past 2 GiB)
//   C  the broker's own, 64 KiB at a time, 1 GiB, 60 s at most: what a small write costs it (the
//      console's FTP server, a payload too, took about 7 MiB/s a connection)
//   D  the broker's random writes in C's file (1 GiB): 4 KiB, then 16 KiB, 30 s each: IOPS and
//      each write's time (p50/p95/p99)
//   E  OGBox's writes through the broker: 16 MiB messages, 1 GiB (the loopback and the broker's
//      writes), then 4 KiB at random for 20 s one at a time (the round trip), then 64 in flight
//   F  OGBox's own writes again, 256 MiB: is the title's budget still spent (the broker didn't
//      spend it, or refill it)
// Then what E wrote is read back through the broker and compared, the broker is told to quit and
// the files are deleted. A and F use the same code as the broker's own (ps5/broker/posix.hpp's
// run_bench), with O_DIRECT where the file system takes it, as PROBE.md's measurements were.

#include "ps5_broker.h"

#include "../../broker/posix.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#if defined(__PROSPERO__)
#include <sys/mount.h>
#else
#include <sys/statvfs.h>
#endif

namespace ogbox::broker
{
namespace
{
using ::broker::kStep;
using ::broker::now_ns;

std::string Format(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Format(const char* format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	return line;
}

double MiB(uint64_t bytes) { return static_cast<double>(bytes) / 1048576.0; }
double Rate(uint64_t bytes, uint64_t ns) { return ns ? MiB(bytes) / (static_cast<double>(ns) / 1e9) : 0.0; }
double Sec(uint64_t ns) { return static_cast<double>(ns) / 1e9; }
double Ms(uint64_t ns) { return static_cast<double>(ns) / 1e6; }

void Percentiles(std::vector<uint64_t>& ns, BenchResult& r)
{
	if (ns.empty())
		return;
	std::sort(ns.begin(), ns.end());
	r.p50_ns = ns[ns.size() * 50 / 100];
	r.p95_ns = ns[ns.size() * 95 / 100];
	r.p99_ns = ns[ns.size() * 99 / 100];
	r.max_ns = ns.back();
}

struct SeqNumbers
{
	double first = 0, last = 0, past2g = 0, overall = 0;
	bool reached2g = false;
};

// A sequential phase's lines: one a 256 MiB step, then its end.
SeqNumbers LogSeq(const Log& log, const char* tag, const BenchResult& r, const std::vector<BenchStep>& steps)
{
	SeqNumbers n;
	uint64_t at = 0, pastBytes = 0, pastNs = 0;
	for (size_t i = 0; i < steps.size(); i++)
	{
		const BenchStep& s = steps[i];
		log(Format("writetest: %s step %zu %.0f-%.0fMiB %.2fs %.1fMiB/s", tag, i + 1, MiB(at), MiB(at + s.bytes), Sec(s.ns),
			Rate(s.bytes, s.ns)));
		if (at >= (2ull << 30))
		{
			pastBytes += s.bytes;
			pastNs += s.ns;
		}
		at += s.bytes;
	}
	if (!steps.empty())
	{
		n.first = Rate(steps.front().bytes, steps.front().ns);
		n.last = Rate(steps.back().bytes, steps.back().ns);
	}
	n.reached2g = pastBytes != 0;
	n.past2g = Rate(pastBytes, pastNs);
	n.overall = Rate(r.bytes, r.ns);
	log(Format("writetest: %s end bytes=%.0fMiB %.1fs %.1fMiB/s sync=%.2fs with-sync=%.1fMiB/s writes=%llu p50=%.1fms p95=%.1fms "
		"p99=%.1fms max=%.1fms prep=%.2fs direct=%u error=%d%s%s", tag, MiB(r.bytes), Sec(r.ns), n.overall, Sec(r.sync_ns),
		Rate(r.bytes, r.ns + r.sync_ns), static_cast<unsigned long long>(r.ops), Ms(r.p50_ns), Ms(r.p95_ns), Ms(r.p99_ns),
		Ms(r.max_ns), Sec(r.prep_ns), r.direct, r.error, r.error ? Format(" (%s)", std::strerror(r.error)).c_str() : "",
		r.capped ? " stopped=time-limit" : ""));
	if (n.reached2g)
		log(Format("writetest: %s past 2048MiB: %.0fMiB in %.1fs, %.1fMiB/s", tag, MiB(pastBytes), Sec(pastNs), n.past2g));
	return n;
}

double LogRand(const Log& log, const char* tag, const BenchResult& r)
{
	const double iops = r.ns ? static_cast<double>(r.ops) / Sec(r.ns) : 0.0;
	log(Format("writetest: %s end writes=%llu %.1fs %.0fIOPS %.2fMiB/s p50=%.2fms p95=%.2fms p99=%.2fms max=%.1fms sync=%.2fs "
		"prep=%.2fs direct=%u error=%d%s", tag, static_cast<unsigned long long>(r.ops), Sec(r.ns), iops, Rate(r.bytes, r.ns),
		Ms(r.p50_ns), Ms(r.p95_ns), Ms(r.p99_ns), Ms(r.max_ns), Sec(r.sync_ns), Sec(r.prep_ns), r.direct, r.error,
		r.error ? Format(" (%s)", std::strerror(r.error)).c_str() : ""));
	return iops;
}

struct LocalProgress
{
	WriteTest* test;
	unsigned seconds;
	uint64_t began;
	uint64_t shown;
	std::function<void(const std::string&)> detail;
};

void OnLocalProgress(void* ctx, uint64_t bytes, uint64_t)
{
	auto* p = static_cast<LocalProgress*>(ctx);
	const uint64_t now = now_ns();
	if (now - p->shown < 250000000ull)
		return;
	p->shown = now;
	p->detail(Format("%.0f MiB, %.0f s of at most %u s", MiB(bytes), Sec(now - p->began), p->seconds));
}

uint64_t Next(uint64_t& state)
{
	state ^= state >> 12;
	state ^= state << 25;
	state ^= state >> 27;
	return state * 0x2545f4914f6cdd1dull;
}
} // namespace

// ------------------------------------------------------------------------------------------ client

Client::~Client()
{
	if (m_fd >= 0)
		close(m_fd);
}

bool Client::Fail(const char* what)
{
	const int e = errno;
	m_error = Format("%s (errno %d: %s)", what, e, std::strerror(e));
	return false;
}

bool Client::Send(Op op, uint32_t handle, uint64_t offset, uint64_t length, uint32_t flags, const void* body, size_t size,
	uint32_t* seq)
{
	if (m_fd < 0)
	{
		errno = ENOTCONN;
		return Fail("no connection");
	}
	::broker::Request q;
	q.op = static_cast<uint16_t>(op);
	q.seq = m_seq++;
	q.handle = handle;
	q.offset = offset;
	q.length = length;
	q.size = static_cast<uint32_t>(size);
	q.flags = flags;
	if (!::broker::send_all(m_fd, &q, sizeof(q), timeout_ms) || (size != 0 && !::broker::send_all(m_fd, body, size, timeout_ms)))
		return Fail("a request could not be sent");
	m_pending.emplace_back(q.seq, q.op);
	if (seq)
		*seq = q.seq;
	return true;
}

bool Client::SendV(Op op, uint32_t handle, uint64_t offset, const struct iovec* iov, int n)
{
	if (m_fd < 0)
	{
		errno = ENOTCONN;
		return Fail("no connection");
	}
	size_t size = 0;
	for (int i = 0; i < n; i++)
		size += iov[i].iov_len;
	if (size > ::broker::kMaxBody)
	{
		errno = EMSGSIZE;
		return Fail("a request too large");
	}
	::broker::Request q;
	q.op = static_cast<uint16_t>(op);
	q.seq = m_seq++;
	q.handle = handle;
	q.offset = offset;
	q.size = static_cast<uint32_t>(size);
	bool ok = ::broker::send_all(m_fd, &q, sizeof(q), timeout_ms);
	for (int i = 0; ok && i < n; i++)
		ok = iov[i].iov_len == 0 || ::broker::send_all(m_fd, iov[i].iov_base, iov[i].iov_len, timeout_ms);
	if (!ok)
		return Fail("a request could not be sent");
	m_pending.emplace_back(q.seq, q.op);
	return true;
}

bool Client::Finish(int wait_ms)
{
	if (m_fd < 0)
		return true;
	shutdown(m_fd, SHUT_WR);
	const uint64_t until = now_ns() + static_cast<uint64_t>(wait_ms) * 1000000ull;
	bool ended = false;
	unsigned char sink[4096];
	while (!ended)
	{
		const uint64_t now = now_ns();
		if (now >= until)
			break;
		pollfd p{m_fd, POLLIN, 0};
		const int ready = poll(&p, 1, static_cast<int>(std::min<uint64_t>((until - now) / 1000000ull + 1, 1000)));
		if (ready < 0 && errno != EINTR)
			break;
		if (ready <= 0)
			continue;
		const ssize_t got = read(m_fd, sink, sizeof(sink));
		if (got < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		ended = got <= 0;   // EOF (or the connection reset: it is gone either way)
	}
	close(m_fd);
	m_fd = -1;
	m_pending.clear();
	return ended;
}

bool Client::Receive(Reply& reply, std::vector<uint8_t>* body, int wait_ms)
{
	if (m_pending.empty())
	{
		errno = EINVAL;
		return Fail("no request is waiting for its reply");
	}
	if (!::broker::recv_exact(m_fd, &reply, sizeof(reply), wait_ms))
		return Fail("no reply from the broker");
	const auto expected = m_pending.front();
	m_pending.pop_front();
	if (reply.magic != ::broker::kMagic || reply.version != ::broker::kVersion || reply.seq != expected.first ||
		reply.op != expected.second || reply.size > ::broker::kMaxBody + 4096)
	{
		m_error = Format("a reply out of order or not one (seq %u for %u, op %u for %u, size %u)", reply.seq, expected.first,
			reply.op, expected.second, reply.size);
		return false;
	}
	std::vector<uint8_t> sink;
	std::vector<uint8_t>& into = body ? *body : sink;
	into.resize(reply.size);
	if (reply.size != 0 && !::broker::recv_exact(m_fd, into.data(), reply.size, timeout_ms))
		return Fail("a reply's body broke off");
	return true;
}

int Client::Call(Op op, uint32_t handle, uint64_t offset, uint64_t length, uint32_t flags, const void* body, size_t size,
	Reply& reply, std::vector<uint8_t>* out, int wait_ms)
{
	if (!Send(op, handle, offset, length, flags, body, size) || !Receive(reply, out, wait_ms))
		return -1;
	return reply.error;
}

int Client::Hello(std::string& text, uint32_t& version, int wait_ms)
{
	Reply r;
	std::vector<uint8_t> body;
	const int e = Call(Op::hello, 0, 0, ::broker::kVersion, 0, nullptr, 0, r, &body, wait_ms);
	version = static_cast<uint32_t>(r.value);
	text.assign(body.begin(), body.end());
	return e;
}

int Client::Open(const std::string& path, uint32_t flags, uint32_t& handle, bool& direct)
{
	Reply r;
	const int e = Call(Op::open, 0, 0, 0, flags, path.data(), path.size(), r, nullptr, timeout_ms);
	handle = static_cast<uint32_t>(r.value);
	direct = r.flags & 1;
	return e;
}

int Client::Write(uint32_t handle, uint64_t offset, const void* data, size_t size, uint64_t& written)
{
	Reply r;
	const int e = Call(Op::write, handle, offset, 0, 0, data, size, r, nullptr, timeout_ms);
	written = r.value;
	return e;
}

int Client::Read(uint32_t handle, uint64_t offset, size_t size, std::vector<uint8_t>& out)
{
	Reply r;
	return Call(Op::read, handle, offset, size, 0, nullptr, 0, r, &out, timeout_ms);
}

int Client::Truncate(uint32_t handle, uint64_t size)
{
	Reply r;
	return Call(Op::truncate, handle, 0, size, 0, nullptr, 0, r, nullptr, timeout_ms);
}

int Client::Sync(uint32_t handle, int wait_ms)
{
	Reply r;
	return Call(Op::sync, handle, 0, 0, 0, nullptr, 0, r, nullptr, wait_ms);
}

int Client::Close(uint32_t handle)
{
	Reply r;
	return Call(Op::close, handle, 0, 0, 0, nullptr, 0, r, nullptr, timeout_ms);
}

int Client::Unlink(const std::string& path)
{
	Reply r;
	return Call(Op::unlink, 0, 0, 0, 0, path.data(), path.size(), r, nullptr, timeout_ms);
}

int Client::Bench(const std::string& path, const BenchArgs& args, BenchResult& result, std::vector<BenchStep>& steps)
{
	std::vector<uint8_t> body(sizeof(args) + path.size());
	std::memcpy(body.data(), &args, sizeof(args));
	std::memcpy(body.data() + sizeof(args), path.data(), path.size());
	Reply r;
	std::vector<uint8_t> out;
	// the broker answers when it is done: its time limit, its fsync, and room
	const int e = Call(Op::bench, 0, 0, 0, 0, body.data(), body.size(), r, &out, (static_cast<int>(args.seconds) + 300) * 1000);
	result = BenchResult{};
	steps.clear();
	if (e != 0)
		return e;
	if (out.size() < sizeof(result))
	{
		m_error = "a BENCH reply too short";
		return -1;
	}
	std::memcpy(&result, out.data(), sizeof(result));
	const size_t n = std::min<size_t>(result.steps, (out.size() - sizeof(result)) / sizeof(BenchStep));
	steps.resize(n);
	if (n)
		std::memcpy(steps.data(), out.data() + sizeof(result), n * sizeof(BenchStep));
	return 0;
}

int Client::Quit()
{
	Reply r;
	return Call(Op::quit, 0, 0, 0, 0, nullptr, 0, r, nullptr, timeout_ms);
}

int Client::Copy(uint32_t from, uint64_t fromOffset, uint32_t to, uint64_t toOffset, uint64_t length, uint64_t& copied,
	int wait_ms)
{
	::broker::CopyArgs a;
	a.from = from;
	a.from_offset = fromOffset;
	Reply r;
	const int e = Call(Op::copy, to, toOffset, length, 0, &a, sizeof(a), r, nullptr, wait_ms);
	copied = e < 0 ? 0 : r.value;
	return e;
}

int Launch(const std::string& elf, std::string& why, unsigned port)
{
	const int file = open(elf.c_str(), O_RDONLY);
	if (file < 0)
	{
		why = Format("%s can't be opened (errno %d: %s)", elf.c_str(), errno, std::strerror(errno));
		return -1;
	}
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		why = Format("no socket (errno %d: %s)", errno, std::strerror(errno));
		close(file);
		return -1;
	}
	const struct timeval timeout = {5, 0};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	struct sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<unsigned short>(port));
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address)) != 0)
	{
		why = Format("no payload loader at 127.0.0.1:%u (errno %d: %s)", port, errno, std::strerror(errno));
		close(fd);
		close(file);
		return -1;
	}
	const int one = 1, buffer = 4 << 20;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
	std::vector<uint8_t> chunk(64 << 10);
	size_t sent = 0;
	for (;;)
	{
		const ssize_t got = read(file, chunk.data(), chunk.size());
		if (got == 0)
			break;
		if (got < 0 || !::broker::send_all(fd, chunk.data(), static_cast<size_t>(got), 5000))
		{
			why = Format("sending %s to the payload loader broke off after %zu bytes (errno %d: %s)", elf.c_str(), sent, errno,
				std::strerror(errno));
			close(fd);
			close(file);
			return -1;
		}
		sent += static_cast<size_t>(got);
	}
	close(file);
	why = Format("%s (%zu bytes) sent to the payload loader at 127.0.0.1:%u", elf.c_str(), sent, port);
	return fd;
}

bool FreeBytes(const std::string& path, uint64_t& bytes)
{
#if defined(__PROSPERO__)
	struct statfs s;
	if (statfs(path.c_str(), &s) != 0)
		return false;
	bytes = static_cast<uint64_t>(s.f_bavail) * static_cast<uint64_t>(s.f_bsize);
#else
	struct statvfs s;
	if (statvfs(path.c_str(), &s) != 0)
		return false;
	bytes = static_cast<uint64_t>(s.f_bavail) * static_cast<uint64_t>(s.f_frsize);
#endif
	return true;
}

// ------------------------------------------------------------------------------------- write test

WriteTest& WriteTest::Get()
{
	static WriteTest test;
	return test;
}

WriteTest::Progress WriteTest::State()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Progress p = m_state;
	if (m_began)
		p.seconds = Sec(now_ns() - m_began);
	return p;
}

void WriteTest::Phase(const std::string& phase, const std::string& detail)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_state.phase = phase;
	m_state.detail = detail;
}

void WriteTest::Detail(const std::string& detail)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_state.detail = detail;
}

bool WriteTest::Start(const std::string& dir, Connector connect, const Plan& plan, Log log)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_state.running)
			return false;
		m_state = Progress{};
		m_state.running = true;
		m_state.phase = "Starting";
		m_began = now_ns();
	}
	std::thread([this, dir, connect, plan, log]() { Run(dir, connect, plan, log); }).detach();
	return true;
}

bool WriteTest::Run(const std::string& dir, Connector connect, const Plan& plan, Log log)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_state.running = true;
		m_state.done = false;
		if (!m_began)
			m_began = now_ns();
	}
	const uint64_t began = now_ns();
	std::string summary;
	const auto line = [&](const std::string& s) { summary += s + "\n"; };
	const auto path = [&](const char* name) { return dir + "/" + name; };
	const std::string localDir = plan.localRoot + dir;
	const auto own = [&](const char* name) { return localDir + "/" + name; };
	static const char* const names[] = { "marker.txt", "A.bin", "B.bin", "C.bin", "E1.bin", "E2.bin", "F.bin" };
	const auto removeAll = [&]() {
		unsigned n = 0;
		for (const char* name : names)
			n += unlink(own(name).c_str()) == 0;
		rmdir(localDir.c_str());
		return n;
	};
	std::unique_ptr<Client> client;
	uint32_t e1 = 0, e2 = 0;
	bool ok = false;
	const auto finish = [&](bool good, const std::string& why) {
		Phase("Deleting the test files", "");
		int quit = -2;
		unsigned brokerDeleted = 0;
		if (client && client->Fd() >= 0 && client->Error().empty())   // (not over a connection that broke)
		{
			client->timeout_ms = 15000;
			if (e1)
				client->Close(e1);
			if (e2)
				client->Close(e2);
			for (const char* name : names)
				brokerDeleted += client->Unlink(path(name)) == 0;
			quit = client->Quit();
		}
		const unsigned deleted = removeAll();
		log(Format("writetest: cleanup: the broker deleted %u files, OGBox %u; quit %s", brokerDeleted, deleted,
			quit == -2 ? (client && !client->Error().empty() ? "not sent (the connection broke)" : "not sent")
			: quit == 0 ? "answered" : client->Error().c_str()));
		client.reset();
		if (!good)
		{
			log("writetest: stopped: " + why);
			line("Stopped: " + why);
		}
		log(Format("writetest: %s in %.0f s", good ? "done" : "ended", Sec(now_ns() - began)));
		std::lock_guard<std::mutex> lock(m_mutex);
		m_state.running = false;
		m_state.done = true;
		m_state.ok = good;
		m_state.summary = summary;
		m_state.phase = good ? "Done" : "Stopped";
		m_state.detail = why;
		ok = good;
		return good;
	};

	// ---- where, and whether there is room
	Phase("Checking the data folder", dir);
	mkdir(localDir.c_str(), 0777);
	removeAll();   // (what an earlier run left: OGBox closed while it ran)
	mkdir(localDir.c_str(), 0777);
	uint64_t freeBytes = 0;
	const bool knowFree = FreeBytes(localDir, freeBytes);
	log(Format("writetest: start dir=%s free=%s plan: A %.0fMiB/%us B %.0fMiB/%us C %.0fMiB/%us D %.0fMiB/%us E %.0fMiB/%us "
		"+%us F %.0fMiB/%us", dir.c_str(), knowFree ? Format("%.1fGiB", freeBytes / 1073741824.0).c_str() : "unknown", MiB(plan.aTotal),
		plan.aSeconds, MiB(plan.bTotal), plan.bSeconds, MiB(plan.cTotal), plan.cSeconds, MiB(plan.dSpace), plan.dSeconds,
		MiB(plan.eTotal), plan.eSeconds, plan.eRandSeconds, MiB(plan.fTotal), plan.fSeconds));
	const std::string probe = path("A.bin");
	if (!::broker::path_allowed(probe.data(), probe.size()))
		return finish(false, "the data folder (" + dir + ") is not under /data/, /mnt/usb<n>/ or /mnt/ext<n>/, where the broker writes");
	if (!knowFree || freeBytes < plan.minFree)
		return finish(false, knowFree ? Format("%.1f GiB free there; the test needs %.0f GiB", freeBytes / 1073741824.0,
			plan.minFree / 1073741824.0) : "the free space there could not be read");
	{
		const std::string marker = Format("OGBox write test, pid %d, %llu\n", static_cast<int>(getpid()),
			static_cast<unsigned long long>(now_ns()));
		const int fd = open(own("marker.txt").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0 || write(fd, marker.data(), marker.size()) != static_cast<ssize_t>(marker.size()))
		{
			if (fd >= 0)
				close(fd);
			return finish(false, Format("OGBox can't write in %s (errno %d: %s)", dir.c_str(), errno, std::strerror(errno)));
		}
		close(fd);

		// ---- the broker
		Phase("Starting the write broker", "the payload loader, 127.0.0.1:9021");
		std::string why;
		const uint64_t t0 = now_ns();
		const int fd2 = connect(why);
		log("writetest: broker: " + why);
		if (fd2 < 0)
			return finish(false, "the broker could not be started: " + why + " (nothing was written)");
		client = std::make_unique<Client>(fd2);
		std::string hello;
		uint32_t version = 0;
		const int e = client->Hello(hello, version, plan.helloMs);
		if (e != 0)
			return finish(false, e < 0 ? "no answer from the broker: " + client->Error() + " (is the payload loader busy, or not elfldr?)"
				: Format("the broker refused HELLO (%d)", e));
		log(Format("writetest: broker: hello in %.2fs: version %u, \"%s\"", Sec(now_ns() - t0), version, hello.c_str()));
		if (version != ::broker::kVersion)
			return finish(false, Format("the broker speaks version %u, OGBox %u", version, ::broker::kVersion));
		uint32_t h = 0;
		bool direct = false;
		std::vector<uint8_t> back;
		int r = client->Open(path("marker.txt"), ::broker::open_read, h, direct);
		if (r == 0)
		{
			r = client->Read(h, 0, 4096, back);
			client->Close(h);
		}
		if (r < 0)
			return finish(false, "the broker's connection broke: " + client->Error());
		const bool same = r == 0 && std::string(back.begin(), back.end()) == marker;
		log(Format("writetest: view: OGBox's marker file read back by the broker: %s", r != 0 ? Format("error %d (%s)", r,
			std::strerror(r)).c_str() : same ? "the same" : "different (the broker sees another file there)"));
		line(Format("The broker sees OGBox's files: %s", same ? "yes" : "no"));
	}

	auto local = std::make_unique<::broker::BenchOut>();
	const auto runLocal = [&](const char* tag, const char* name, uint64_t total, unsigned seconds) {
		LocalProgress p{ this, seconds, now_ns(), 0, [this](const std::string& d) { Detail(d); } };
		::broker::BenchArgs a;
		a.total = total;
		a.chunk = static_cast<uint32_t>(plan.chunk);
		a.pattern = static_cast<uint32_t>(::broker::Pattern::seq);
		a.seconds = seconds;
		a.flags = ::broker::bench_direct | ::broker::bench_unlink;
		log(Format("writetest: %s start file=%s chunk=%gMiB total=%.0fMiB cap=%us", tag, path(name).c_str(), MiB(plan.chunk),
			MiB(total), seconds));
		::broker::run_bench(own(name).c_str(), a, *local, &OnLocalProgress, &p);
		return LogSeq(log, tag, local->r, std::vector<BenchStep>(local->steps, local->steps + local->r.steps));
	};
	BenchResult br;
	std::vector<BenchStep> bs;
	const auto runBroker = [&](const char* tag, const char* name, uint64_t total, uint64_t space, uint32_t chunk,
		::broker::Pattern pattern, unsigned seconds, uint32_t flags) {
		::broker::BenchArgs a;
		a.total = total;
		a.space = space;
		a.chunk = chunk;
		a.pattern = static_cast<uint32_t>(pattern);
		a.seconds = seconds;
		a.flags = flags;
		Detail(Format("at most %u s", seconds));
		log(Format("writetest: %s start file=%s chunk=%u total=%.0fMiB space=%.0fMiB cap=%us", tag, path(name).c_str(), chunk,
			MiB(total), MiB(space), seconds));
		return client->Bench(path(name), a, br, bs);
	};

	// ---- A: the title's own
	Phase("A: OGBox's own writes, 16 MiB at a time", "");
	const SeqNumbers A = runLocal("A title-seq", "A.bin", plan.aTotal, plan.aSeconds);
	line(Format("A, OGBox's own writes: first 256 MiB %.0f MiB/s, last %.1f MiB/s (%.0f MiB in all)", A.first, A.last,
		MiB(local->r.bytes)));

	// ---- B: the broker's own, right after
	Phase("B: the broker's own writes, 16 MiB at a time", "");
	int r = runBroker("B payload-seq", "B.bin", plan.bTotal, 0, static_cast<uint32_t>(plan.chunk), ::broker::Pattern::seq,
		plan.bSeconds, ::broker::bench_direct | ::broker::bench_unlink);
	if (r < 0)
		return finish(false, "the broker's connection broke in B: " + client->Error());
	if (r > 0)
		log(Format("writetest: B payload-seq refused: %d (%s)", r, std::strerror(r)));
	const SeqNumbers B = LogSeq(log, "B payload-seq", br, bs);
	line(Format("B, the broker's own writes: first 256 MiB %.0f MiB/s, last %.1f MiB/s, past 2 GiB %s (%.0f MiB in all)", B.first,
		B.last, B.reached2g ? Format("%.1f MiB/s", B.past2g).c_str() : "not reached", MiB(br.bytes)));

	// ---- C: small writes
	Phase("C: the broker's own writes, 64 KiB at a time", "");
	r = runBroker("C payload-seq64k", "C.bin", plan.cTotal, 0, plan.cChunk, ::broker::Pattern::seq, plan.cSeconds,
		::broker::bench_direct);
	if (r < 0)
		return finish(false, "the broker's connection broke in C: " + client->Error());
	const SeqNumbers C = LogSeq(log, "C payload-seq64k", br, bs);
	line(Format("C, the broker's 64 KiB writes: %.1f MiB/s, each %.2f ms (p50)", C.overall, Ms(br.p50_ns)));

	// ---- D: random, in C's file
	Phase("D: the broker's random writes, 4 KiB then 16 KiB", "");
	r = runBroker("D payload-rand4k", "C.bin", 0, plan.dSpace, 4096, ::broker::Pattern::rand4k, plan.dSeconds,
		::broker::bench_direct);
	if (r < 0)
		return finish(false, "the broker's connection broke in D: " + client->Error());
	const double d4 = LogRand(log, "D payload-rand4k", br);
	const double d4p99 = Ms(br.p99_ns);
	r = runBroker("D payload-rand16k", "C.bin", 0, plan.dSpace, 16384, ::broker::Pattern::rand16k, plan.dSeconds,
		::broker::bench_direct | ::broker::bench_unlink);
	if (r < 0)
		return finish(false, "the broker's connection broke in D: " + client->Error());
	const double d16 = LogRand(log, "D payload-rand16k", br);
	line(Format("D, the broker's random writes: 4 KiB %.0f IOPS (p99 %.1f ms), 16 KiB %.0f IOPS (p99 %.1f ms)", d4, d4p99, d16,
		Ms(br.p99_ns)));

	// ---- E: OGBox's writes through the broker
	Phase("E: OGBox's writes through the broker, 16 MiB messages", "");
	client->timeout_ms = 120000;
	bool direct = false;
	r = client->Open(path("E1.bin"), ::broker::open_read | ::broker::open_write | ::broker::open_create | ::broker::open_truncate |
		::broker::open_direct, e1, direct);
	if (r != 0)
		return finish(false, r < 0 ? "the broker's connection broke in E: " + client->Error() : Format("E1.bin: open %d", r));
	constexpr uint64_t saltE1 = 0xE1;
	uint64_t e1Bytes = 0;
	{
		log(Format("writetest: E ipc-seq start file=%s message=%gMiB total=%.0fMiB cap=%us direct=%d (the times leave out "
			"making the bytes)", path("E1.bin").c_str(), MiB(plan.chunk), MiB(plan.eTotal), plan.eSeconds, direct ? 1 : 0));
		std::vector<uint8_t> buffer(plan.chunk);
		BenchResult er;
		er.direct = direct;
		std::vector<BenchStep> steps;
		std::vector<uint64_t> each;
		uint64_t stepBytes = 0, stepNs = 0;
		const uint64_t t0 = now_ns();
		while (e1Bytes < plan.eTotal)
		{
			if (now_ns() - t0 >= uint64_t(plan.eSeconds) * 1000000000ull)
			{
				er.capped = 1;
				break;
			}
			const size_t n = static_cast<size_t>(std::min<uint64_t>(plan.chunk, plan.eTotal - e1Bytes));
			::broker::fill_pattern(buffer.data(), n, e1Bytes, saltE1);
			uint64_t written = 0;
			const uint64_t t1 = now_ns();
			r = client->Write(e1, e1Bytes, buffer.data(), n, written);
			const uint64_t t2 = now_ns();
			each.push_back(t2 - t1);
			er.ns += t2 - t1;
			stepNs += t2 - t1;
			er.ops++;
			e1Bytes += written;
			er.bytes += written;
			stepBytes += written;
			if (r != 0)
			{
				er.error = r < 0 ? EPIPE : r;
				break;
			}
			if (stepBytes >= kStep)
			{
				steps.push_back({ stepBytes, stepNs });
				stepBytes = stepNs = 0;
			}
			Detail(Format("%.0f MiB, %.0f s of at most %u s", MiB(e1Bytes), Sec(now_ns() - t0), plan.eSeconds));
		}
		if (stepBytes)
			steps.push_back({ stepBytes, stepNs });
		if (r < 0)
			return finish(false, "the broker's connection broke in E: " + client->Error());
		const uint64_t t3 = now_ns();
		const int s = client->Sync(e1, 600000);
		er.sync_ns = now_ns() - t3;
		if (s != 0 && er.error == 0)
			er.error = s < 0 ? EPIPE : s;
		Percentiles(each, er);
		const SeqNumbers E = LogSeq(log, "E ipc-seq", er, steps);
		line(Format("E, OGBox's 16 MiB writes through the broker: %.1f MiB/s (%.0f MiB)", E.overall, MiB(e1Bytes)));
	}

	Phase("E: OGBox's 4 KiB writes through the broker", "");
	r = client->Open(path("E2.bin"), ::broker::open_read | ::broker::open_write | ::broker::open_create | ::broker::open_truncate,
		e2, direct);
	if (r == 0)
		r = client->Truncate(e2, plan.eSpace);
	if (r != 0)
		return finish(false, r < 0 ? "the broker's connection broke in E: " + client->Error() : Format("E2.bin: %d", r));
	std::unordered_map<uint64_t, uint32_t> lastSalt;   // offset -> the write that put its bytes there
	std::vector<uint64_t> order;
	uint32_t salt = 1;
	uint64_t rng = now_ns() | 1;
	const uint64_t blocks = plan.eSpace / 4096;
	std::vector<uint8_t> small(4096);
	double rttP50 = 0, pipeIops = 0;
	for (int pass = 0; pass < 2; pass++)
	{
		const unsigned depth = pass == 0 ? 1 : std::max(1u, plan.ePipeline);
		const char* tag = pass == 0 ? "E ipc-rand4k-1" : "E ipc-rand4k-pipelined";
		log(Format("writetest: %s start file=%s in-flight=%u cap=%us buffered", tag, path("E2.bin").c_str(), depth, plan.eRandSeconds));
		std::deque<uint64_t> sentAt;
		std::vector<uint64_t> rtt;
		BenchResult er;
		const uint64_t t0 = now_ns(), limit = uint64_t(plan.eRandSeconds) * 1000000000ull;
		bool broken = false;
		for (;;)
		{
			const bool more = now_ns() - t0 < limit && er.error == 0;
			while (more && client->InFlight() < depth)
			{
				const uint64_t offset = Next(rng) % blocks * 4096;
				::broker::fill_pattern(small.data(), small.size(), offset, salt);
				sentAt.push_back(now_ns());
				if (!client->Send(Op::write, e2, offset, 0, 0, small.data(), small.size()))
				{
					broken = true;
					break;
				}
				lastSalt[offset] = salt++;
				order.push_back(offset);
			}
			if (broken || client->InFlight() == 0)
				break;
			Reply reply;
			if (!client->Receive(reply, nullptr, 60000))
			{
				broken = true;
				break;
			}
			rtt.push_back(now_ns() - sentAt.front());
			sentAt.pop_front();
			er.ops++;
			er.bytes += reply.value;
			if (reply.error != 0 && er.error == 0)
				er.error = reply.error;
			if ((er.ops & 255) == 0)
				Detail(Format("%llu writes, %.0f s of %u s", static_cast<unsigned long long>(er.ops), Sec(now_ns() - t0), plan.eRandSeconds));
		}
		er.ns = now_ns() - t0;
		if (broken)
			return finish(false, "the broker's connection broke in E: " + client->Error());
		Percentiles(rtt, er);
		const double iops = LogRand(log, tag, er);
		if (pass == 0)
			rttP50 = Ms(er.p50_ns);
		else
			pipeIops = iops;
		if (pass == 0)
			line(Format("E, OGBox's 4 KiB writes through the broker, one at a time: %.0f a second, round trip %.2f ms (p50), "
				"%.2f ms (p99)", iops, rttP50, Ms(er.p99_ns)));
		else
			line(Format("E, the same with %u in flight: %.0f a second, %.2f ms each (p50)", depth, pipeIops, Ms(er.p50_ns)));
	}
	{
		const uint64_t t3 = now_ns();
		const int s = client->Sync(e2, 600000);
		log(Format("writetest: E ipc-rand4k sync=%.2fs error=%d", Sec(now_ns() - t3), s));
		if (s < 0)
			return finish(false, "the broker's connection broke in E: " + client->Error());
	}

	// ---- F: the title's own again
	Phase("F: OGBox's own writes again", "");
	const SeqNumbers F = runLocal("F title-seq", "F.bin", plan.fTotal, plan.fSeconds);
	line(Format("F, OGBox's own writes again: %.1f MiB/s (first 256 MiB %.1f MiB/s)", F.overall, F.first));

	// ---- what E wrote, read back through the broker
	Phase("Checking what was written", "");
	{
		unsigned match = 0, differ = 0, errors = 0;
		uint64_t sentSum = 0xcbf29ce484222325ull, readSum = sentSum;
		std::vector<uint8_t> want, got;
		std::string first;
		const auto check = [&](uint32_t handle, uint64_t offset, uint64_t size, uint64_t saltOf) {
			want.resize(size);
			::broker::fill_pattern(want.data(), want.size(), offset, saltOf);
			const int e = client->Read(handle, offset, size, got);
			if (e != 0)
			{
				errors++;
				return e >= 0;
			}
			sentSum = ::broker::checksum(want.data(), want.size(), sentSum);
			readSum = ::broker::checksum(got.data(), got.size(), readSum);
			if (got == want)
				match++;
			else
			{
				differ++;
				if (first.empty())
					first = Format(" first difference: offset %llu, %zu bytes back of %llu", static_cast<unsigned long long>(offset),
						got.size(), static_cast<unsigned long long>(size));
			}
			return true;
		};
		bool alive = true;
		const uint64_t ranges = e1Bytes / plan.verifyRange;
		for (unsigned i = 0; alive && ranges && i < plan.verifyRanges; i++)
			alive = check(e1, Next(rng) % ranges * plan.verifyRange, plan.verifyRange, saltE1);
		log(Format("writetest: verify E1 (16 MiB messages): %u ranges of %.0fMiB: %u the same, %u different, %u errors; checksum "
			"sent=%016llx read=%016llx%s", match + differ + errors, MiB(plan.verifyRange), match, differ, errors,
			static_cast<unsigned long long>(sentSum), static_cast<unsigned long long>(readSum), first.c_str()));
		const unsigned m1 = match, d1 = differ + errors;
		match = differ = errors = 0;
		sentSum = readSum = 0xcbf29ce484222325ull;
		first.clear();
		std::unordered_map<uint64_t, bool> seen;
		for (size_t i = order.size(); alive && i-- > 0 && seen.size() < plan.verifyWrites;)
		{
			if (!seen.emplace(order[i], true).second)
				continue;
			alive = check(e2, order[i], 4096, lastSalt[order[i]]);
		}
		log(Format("writetest: verify E2 (4 KiB writes): the last %u places written: %u the same, %u different, %u errors; checksum "
			"sent=%016llx read=%016llx%s", match + differ + errors, match, differ, errors, static_cast<unsigned long long>(sentSum),
			static_cast<unsigned long long>(readSum), first.c_str()));
		if (!alive)
			return finish(false, "the broker's connection broke while reading back: " + client->Error());
		const bool good = d1 == 0 && differ + errors == 0;
		line(Format("Read back: %s (%u + %u ranges)", good ? "all the same as written" : "DIFFERENT from what was written", m1 + d1,
			match + differ + errors));
		log(Format("writetest: verify %s", good ? "ok" : "FAILED"));
	}
	log("writetest: summary:");
	for (size_t at = 0; at < summary.size();)
	{
		const size_t end = summary.find('\n', at);
		log("writetest:   " + summary.substr(at, end - at));
		at = end == std::string::npos ? summary.size() : end + 1;
	}
	finish(true, "");
	return ok;
}
} // namespace ogbox::broker
