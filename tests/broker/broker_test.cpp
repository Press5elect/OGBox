// OGBox - the PC test of the write broker: its request handler (ps5/broker/server.cpp, the payload's
// whole work) over a socketpair and a loopback socket, the title's client and the storage write test
// (ps5/src/platform/ps5_broker.cpp) on a small plan.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#include "platform/ps5_broker.h"
#include "../../ps5/broker/posix.hpp"
#include "../../ps5/broker/server.hpp"

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace ogbox::broker;
using ::broker::End;
using ::broker::ServerOptions;

static int g_fail = 0;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); g_fail++; } } while (0)

static std::string g_root;   // the test's folder: the broker's "/data/..." is g_root + "/data/..."

static bool Allowed(const char* p) { return ::broker::path_allowed(p, std::strlen(p)); }

static void TestPaths()
{
	CHECK(Allowed("/data/ogbox/writetest/A.bin"));
	CHECK(Allowed("/data/x"));
	CHECK(Allowed("/mnt/usb0/OGBox/hdd.img"));
	CHECK(Allowed("/mnt/usb12/a"));
	CHECK(Allowed("/mnt/ext1/homebrew/x"));
	CHECK(Allowed("/data/a..b/c.d"));
	CHECK(!Allowed("/data/"));
	CHECK(!Allowed("/data"));
	CHECK(!Allowed("/data/x/"));
	CHECK(!Allowed("/data//x"));
	CHECK(!Allowed("/data/../etc/passwd"));
	CHECK(!Allowed("/data/x/.."));
	CHECK(!Allowed("/data/./x"));
	CHECK(!Allowed("/mnt/usb/x"));
	CHECK(!Allowed("/mnt/usbx/x"));
	CHECK(!Allowed("/mnt/usb0"));
	CHECK(!Allowed("/mnt/usb0/"));
	CHECK(!Allowed("/mnt/sandbox/x"));
	CHECK(!Allowed("/app0/eboot.bin"));
	CHECK(!Allowed("/system/x"));
	CHECK(!Allowed("data/x"));
	CHECK(!Allowed(""));
	CHECK(!::broker::path_allowed("/data/x\0y", 9));
	std::string longPath = "/data/" + std::string(1100, 'a');
	CHECK(!Allowed(longPath.c_str()));
}

// A broker on one end of a socketpair, in a thread; the client on the other.
struct Pair
{
	int fds[2] = { -1, -1 };
	std::thread thread;
	End end = End::memory;
	ServerOptions options;
	std::unique_ptr<Client> client;
	explicit Pair(size_t ioCap = 0, int idleMs = 60000)
	{
		options.root = g_root.c_str();
		options.io_cap = ioCap;
		options.idle_ms = idleMs;
		options.message_ms = 5000;
		CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
		const int server = fds[1];
		thread = std::thread([this, server]() {
			end = ::broker::serve(server, server, options);
			shutdown(server, SHUT_RDWR);   // (the payload's end: its process gone, the connection with it)
		});
		client = std::make_unique<Client>(fds[0]);
		client->timeout_ms = 10000;
	}
	~Pair()
	{
		client.reset();   // (closes its end: the broker sees EOF if it is still serving)
		thread.join();
		close(fds[1]);
	}
};

static std::string Real(const std::string& path) { return g_root + path; }
static long long SizeOf(const std::string& path)
{
	struct stat st;
	return stat(Real(path).c_str(), &st) == 0 ? static_cast<long long>(st.st_size) : -1;
}

static void TestRequests()
{
	mkdir(Real("/data").c_str(), 0777);
	mkdir(Real("/data/t").c_str(), 0777);
	Pair p(1000);   // every pwrite at most 1000 bytes: the broker's loop must finish each write
	Client& c = *p.client;
	std::string text;
	uint32_t version = 0;
	CHECK(c.Hello(text, version, 5000) == 0);
	CHECK(version == ::broker::kVersion);
	CHECK(text.rfind("write-broker 1 pid=", 0) == 0);

	uint32_t h = 0;
	bool direct = false;
	const uint32_t rw = ::broker::open_read | ::broker::open_write | ::broker::open_create | ::broker::open_truncate;
	CHECK(c.Open("/data/t/a.bin", rw | ::broker::open_direct, h, direct) == 0);
	CHECK(h >= 1 && h <= ::broker::kMaxHandles);

	// writes at random offsets, out of order, one of them not aligned; read back
	const uint64_t offsets[] = { 5 * 4096, 0, 3 * 4096, 100001, 1 << 20, 7 * 4096 + 17 };
	std::vector<uint8_t> data(65536), back;
	for (const uint64_t o : offsets)
	{
		::broker::fill_pattern(data.data(), 4096, o, 7);
		uint64_t written = 0;
		CHECK(c.Write(h, o, data.data(), 4096, written) == 0);
		CHECK(written == 4096);
	}
	for (const uint64_t o : offsets)   // (none of them overlap)
	{
		CHECK(c.Read(h, o, 4096, back) == 0);
		::broker::fill_pattern(data.data(), 4096, o, 7);
		CHECK(back.size() == 4096 && std::memcmp(back.data(), data.data(), 4096) == 0);
	}
	CHECK(SizeOf("/data/t/a.bin") == (1 << 20) + 4096);

	// a large write, split by the cap into 66 pwrites
	::broker::fill_pattern(data.data(), data.size(), 2 << 20, 9);
	uint64_t written = 0;
	CHECK(c.Write(h, 2 << 20, data.data(), data.size(), written) == 0);
	CHECK(written == data.size());
	CHECK(c.Read(h, 2 << 20, data.size(), back) == 0 && back == data);

	// read past the end: short, then empty
	CHECK(c.Read(h, (2 << 20) + 65536 - 100, 4096, back) == 0 && back.size() == 100);
	CHECK(c.Read(h, 10 << 20, 4096, back) == 0 && back.empty());

	// pipelined: 64 writes to the same place in flight, carried out in order: the last one's bytes
	std::vector<uint8_t> block(4096);
	for (uint32_t i = 0; i < 64; i++)
	{
		::broker::fill_pattern(block.data(), block.size(), 1 << 16, 100 + i);
		CHECK(c.Send(Op::write, h, 1 << 16, 0, 0, block.data(), block.size()));
	}
	CHECK(c.InFlight() == 64);
	for (uint32_t i = 0; i < 64; i++)
	{
		Reply r;
		CHECK(c.Receive(r, nullptr, 5000));
		CHECK(r.error == 0 && r.value == 4096);
	}
	::broker::fill_pattern(block.data(), block.size(), 1 << 16, 163);
	CHECK(c.Read(h, 1 << 16, 4096, back) == 0 && back == block);

	// truncate, sync, close
	CHECK(c.Truncate(h, 12345) == 0);
	CHECK(SizeOf("/data/t/a.bin") == 12345);
	CHECK(c.Sync(h, 5000) == 0);
	CHECK(c.Close(h) == 0);
	CHECK(c.Close(h) == EBADF);
	CHECK(c.Read(h, 0, 10, back) == EBADF);
	CHECK(c.Write(99, 0, data.data(), 10, written) == EBADF);
	CHECK(c.Write(0, 0, data.data(), 10, written) == EBADF);

	// paths refused, files not there, a read-only file written
	CHECK(c.Open("/etc/passwd", ::broker::open_read, h, direct) == EACCES);
	CHECK(c.Open("/data/../etc/passwd", ::broker::open_read, h, direct) == EACCES);
	CHECK(c.Open("/data/t/../t/a.bin", ::broker::open_read, h, direct) == EACCES);
	CHECK(c.Open("/data/t/none/x", rw, h, direct) == ENOENT);
	CHECK(c.Open("/data/t/missing", ::broker::open_read, h, direct) == ENOENT);
	CHECK(c.Unlink("/tmp/x") == EACCES);
	CHECK(c.Open("/data/t/a.bin", ::broker::open_read, h, direct) == 0);
	CHECK(c.Write(h, 0, data.data(), 10, written) == EBADF);
	CHECK(c.Close(h) == 0);

	// sixteen handles, not seventeen
	std::vector<uint32_t> hs;
	for (unsigned i = 0; i < ::broker::kMaxHandles; i++)
	{
		uint32_t x = 0;
		CHECK(c.Open("/data/t/a.bin", ::broker::open_read, x, direct) == 0);
		hs.push_back(x);
	}
	CHECK(c.Open("/data/t/a.bin", ::broker::open_read, h, direct) == EMFILE);
	for (uint32_t x : hs)
		CHECK(c.Close(x) == 0);

	// a body larger than the broker takes: drained and refused, and the connection goes on
	{
		std::vector<uint8_t> huge(::broker::kMaxBody + 64 * 1024);
		Reply r;
		c.timeout_ms = 30000;
		CHECK(c.Call(Op::write, 1, 0, 0, 0, huge.data(), huge.size(), r, nullptr, 30000) == EMSGSIZE);
		CHECK(c.Hello(text, version, 5000) == 0);
	}
	// a READ larger than the broker takes
	CHECK(c.Open("/data/t/a.bin", ::broker::open_read, h, direct) == 0);
	CHECK(c.Read(h, 0, ::broker::kMaxBody + 1, back) == EMSGSIZE);
	CHECK(c.Close(h) == 0);
	// an unknown request
	{
		Reply r;
		CHECK(c.Call(static_cast<Op>(77), 0, 0, 0, 0, nullptr, 0, r, nullptr, 5000) == ENOSYS);
	}

	// BENCH: sequential (steps of 256 MiB: one short one here), random
	BenchArgs a;
	a.total = 8 << 20;
	a.chunk = 1 << 20;
	a.pattern = static_cast<uint32_t>(::broker::Pattern::seq);
	a.seconds = 30;
	a.flags = ::broker::bench_direct;
	BenchResult r;
	std::vector<BenchStep> steps;
	CHECK(c.Bench("/data/t/b.bin", a, r, steps) == 0);
	CHECK(r.error == 0 && r.bytes == (8u << 20) && r.ops == 8 && r.steps == 1 && steps.size() == 1);
	CHECK(steps.size() == 1 && steps[0].bytes == (8u << 20));
	CHECK(r.p50_ns > 0 && r.max_ns >= r.p99_ns && r.p99_ns >= r.p50_ns);
	CHECK(SizeOf("/data/t/b.bin") == (8 << 20));
	a = BenchArgs{};
	a.space = 1 << 20;
	a.pattern = static_cast<uint32_t>(::broker::Pattern::rand4k);
	a.seconds = 1;
	a.flags = ::broker::bench_unlink;
	CHECK(c.Bench("/data/t/b.bin", a, r, steps) == 0);
	CHECK(r.error == 0 && r.ops > 0 && r.bytes == r.ops * 4096 && r.capped == 1 && steps.empty());
	CHECK(SizeOf("/data/t/b.bin") == -1);   // (deleted: bench_unlink)
	a.pattern = static_cast<uint32_t>(::broker::Pattern::rand16k);
	a.space = 8192;   // smaller than one write
	CHECK(c.Bench("/data/t/c.bin", a, r, steps) == 0 && r.error == EINVAL);
	a.space = 1 << 20;
	a.seconds = 0;
	CHECK(c.Bench("/data/t/c.bin", a, r, steps) == EINVAL);
	a.seconds = 1;
	CHECK(c.Bench("/etc/c.bin", a, r, steps) == EACCES);
	{
		Reply rr;
		CHECK(c.Call(Op::bench, 0, 0, 0, 0, "short", 5, rr, nullptr, 5000) == EINVAL);
	}

	CHECK(c.Unlink("/data/t/a.bin") == 0);
	CHECK(c.Unlink("/data/t/a.bin") == ENOENT);
	CHECK(c.Quit() == 0);
	p.client.reset();
	p.thread.join();
	CHECK(p.end == End::quit);
	p.thread = std::thread([] {});
}

// 1.0.4: what the cache drives' write-back uses: OPEN's file (OpenInfo), a gathered WRITE (SendV),
// and the end (Finish: what was sent whole carried out first)
static void TestCacheCalls()
{
	mkdir(Real("/data").c_str(), 0777);
	mkdir(Real("/data/c").c_str(), 0777);
	const int made = open(Real("/data/c/x.bin").c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
	CHECK(made >= 0);
	struct stat st;
	CHECK(fstat(made, &st) == 0);
	close(made);
	Pair p(777);
	Client& c = *p.client;
	Reply r;
	std::vector<uint8_t> body;
	CHECK(c.Call(Op::open, 0, 0, 0, ::broker::open_read | ::broker::open_write, "/data/c/x.bin", 13, r, &body, 5000) == 0);
	CHECK(body.size() == sizeof(::broker::OpenInfo));
	::broker::OpenInfo info;
	std::memcpy(&info, body.data(), sizeof(info));
	CHECK(info.ino == static_cast<uint64_t>(st.st_ino));
	const uint32_t h = static_cast<uint32_t>(r.value);
	std::vector<uint8_t> a(5000), b(70000), c3(1), want;
	::broker::fill_pattern(a.data(), a.size(), 0, 1);
	::broker::fill_pattern(b.data(), b.size(), 5000, 2);
	c3[0] = 0x42;
	const struct iovec iov[3] = { { a.data(), a.size() }, { b.data(), b.size() }, { c3.data(), 1 } };
	CHECK(c.SendV(Op::write, h, 100, iov, 3));
	CHECK(c.Receive(r, nullptr, 5000) && r.error == 0 && r.value == 75001);
	want.insert(want.end(), a.begin(), a.end());
	want.insert(want.end(), b.begin(), b.end());
	want.push_back(0x42);
	std::vector<uint8_t> back;
	CHECK(c.Read(h, 100, want.size(), back) == 0 && back == want);
	// sent, not answered, then the end: every one carried out
	std::vector<uint8_t> block(4096);
	for (uint32_t i = 0; i < 32; i++)
	{
		::broker::fill_pattern(block.data(), block.size(), i * 4096ull, 50);
		const struct iovec one = { block.data(), block.size() };
		CHECK(c.SendV(Op::write, h, 1000000 + i * 4096ull, &one, 1));
	}
	CHECK(c.Finish(10000));
	CHECK(c.Fd() < 0 && c.InFlight() == 0);
	const int fd = open(Real("/data/c/x.bin").c_str(), O_RDONLY);
	bool same = fd >= 0;
	for (uint32_t i = 0; same && i < 32; i++)
	{
		::broker::fill_pattern(block.data(), block.size(), i * 4096ull, 50);
		std::vector<uint8_t> got(4096);
		same = pread(fd, got.data(), got.size(), 1000000 + i * 4096) == 4096 && got == block;
	}
	CHECK(same);
	if (fd >= 0)
		close(fd);
	CHECK(!c.SendV(Op::write, h, 0, iov, 1));   // (closed)
}

static void TestEnds()
{
	{   // not a request: answered EPROTO, then the broker ends
		Pair p;
		::broker::Request q;
		q.magic = 0x12345678;
		q.seq = 5;
		CHECK(::broker::send_all(p.fds[0], &q, sizeof(q), 1000));
		Reply r;
		CHECK(::broker::recv_exact(p.fds[0], &r, sizeof(r), 5000));
		CHECK(r.error == EPROTO && r.seq == 5);
		p.thread.join();
		CHECK(p.end == End::protocol);
		p.thread = std::thread([] {});
	}
	{   // the connection closed (the title ended)
		Pair p;
		p.client.reset();
		p.thread.join();
		CHECK(p.end == End::closed);
		p.thread = std::thread([] {});
	}
	{   // nothing asked for a while
		Pair p(0, 200);
		p.thread.join();
		CHECK(p.end == End::idle);
		p.thread = std::thread([] {});
	}
	{   // a reply out of order is the client's protocol error
		Pair p;
		Client& c = *p.client;
		CHECK(c.Send(Op::hello, 0, 0, 1, 0, nullptr, 0));
		Reply r;
		CHECK(c.Receive(r, nullptr, 5000) && r.error == 0);
		CHECK(!c.Receive(r, nullptr, 100));   // none waiting
	}
}

// The payload loader's part: a listening socket that reads the ELF, then serves on that connection.
static void TestLaunch()
{
	const std::string elf = g_root + "/fake.elf";
	{
		FILE* f = fopen(elf.c_str(), "wb");
		std::vector<char> bytes(200000, 'E');
		fwrite(bytes.data(), 1, bytes.size(), f);
		fclose(f);
	}
	const int listener = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0);
	socklen_t n = sizeof(a);
	getsockname(listener, reinterpret_cast<sockaddr*>(&a), &n);
	CHECK(listen(listener, 1) == 0);
	const unsigned port = ntohs(a.sin_port);
	End end = End::memory;
	size_t got = 0;
	std::thread loader([&]() {
		const int fd = accept(listener, nullptr, nullptr);
		std::vector<char> elfBytes(200000);
		got = ::broker::recv_exact(fd, elfBytes.data(), elfBytes.size(), 5000) ? elfBytes.size() : 0;
		ServerOptions o;
		o.root = g_root.c_str();
		end = ::broker::serve(fd, fd, o);
		close(fd);
	});
	std::string why;
	const int fd = Launch(elf, why, port);
	CHECK(fd >= 0);
	CHECK(why.find("200000 bytes") != std::string::npos);
	{
		Client c(fd);
		std::string text;
		uint32_t version = 0;
		CHECK(c.Hello(text, version, 5000) == 0 && version == 1);
		CHECK(c.Quit() == 0);
	}
	loader.join();
	close(listener);
	CHECK(got == 200000 && end == End::quit);
	// nobody there
	CHECK(Launch(elf, why, port) < 0 && why.find("no payload loader") != std::string::npos);
	CHECK(Launch(g_root + "/none.elf", why, port) < 0 && why.find("can't be opened") != std::string::npos);
}

static Plan SmallPlan()
{
	Plan p;
	p.chunk = 1 << 20;
	p.aTotal = 4 << 20;
	p.aSeconds = 10;
	p.bTotal = 6 << 20;
	p.bSeconds = 10;
	p.cTotal = 2 << 20;
	p.cSeconds = 10;
	p.dSpace = 2 << 20;
	p.dSeconds = 1;
	p.eTotal = 5 << 20;
	p.eSeconds = 10;
	p.eSpace = 1 << 20;
	p.eRandSeconds = 1;
	p.ePipeline = 8;
	p.fTotal = 2 << 20;
	p.fSeconds = 10;
	p.verifyRanges = 4;
	p.verifyWrites = 32;
	p.minFree = 0;
	p.helloMs = 5000;
	p.localRoot = g_root;
	return p;
}

static bool Has(const std::vector<std::string>& lines, const std::string& part)
{
	for (const std::string& l : lines)
		if (l.find(part) != std::string::npos)
			return true;
	return false;
}

static void TestWriteTest()
{
	mkdir(Real("/data").c_str(), 0777);
	std::vector<std::string> lines;
	std::mutex m;
	const Log log = [&](const std::string& l) {
		std::lock_guard<std::mutex> lock(m);
		lines.push_back(l);
		fprintf(stderr, "%s\n", l.c_str());
	};
	{   // the whole run, small
		std::thread server;
		End end = End::memory;
		int fds[2] = { -1, -1 };
		ServerOptions o;
		o.root = g_root.c_str();
		const Connector connect = [&](std::string& why) {
			if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
				return -1;
			server = std::thread([&]() { end = ::broker::serve(fds[1], fds[1], o); });
			why = "a socketpair";
			return fds[0];
		};
		WriteTest& t = WriteTest::Get();
		CHECK(t.Run("/data/wt", connect, SmallPlan(), log));
		server.join();
		close(fds[1]);
		CHECK(end == End::quit);
		const WriteTest::Progress s = t.State();
		CHECK(s.done && s.ok && !s.running);
		CHECK(s.summary.find("Read back: all the same as written") != std::string::npos);
		CHECK(Has(lines, "writetest: view: OGBox's marker file read back by the broker: the same"));
		CHECK(Has(lines, "writetest: A title-seq end bytes=4MiB"));
		CHECK(Has(lines, "writetest: B payload-seq step 1 0-6MiB"));
		CHECK(Has(lines, "writetest: B payload-seq end bytes=6MiB"));
		CHECK(Has(lines, "writetest: C payload-seq64k end bytes=2MiB"));
		CHECK(Has(lines, "writetest: D payload-rand4k end writes="));
		CHECK(Has(lines, "writetest: D payload-rand16k end writes="));
		CHECK(Has(lines, "writetest: E ipc-seq end bytes=5MiB"));
		CHECK(Has(lines, "writetest: E ipc-rand4k-1 end writes="));
		CHECK(Has(lines, "writetest: E ipc-rand4k-pipelined end writes="));
		CHECK(Has(lines, "writetest: F title-seq end bytes=2MiB"));
		CHECK(Has(lines, "writetest: verify E1 (16 MiB messages): 4 ranges of 1MiB: 4 the same, 0 different, 0 errors"));
		CHECK(Has(lines, "writetest: verify E2 (4 KiB writes): the last 32 places written: 32 the same"));
		CHECK(Has(lines, "writetest: verify ok"));
		CHECK(Has(lines, "writetest: done in"));
		CHECK(!Has(lines, "error=-") && !Has(lines, "stopped:"));
		struct stat st;
		CHECK(stat(Real("/data/wt").c_str(), &st) != 0);   // the test's folder deleted
	}
	lines.clear();
	{   // no payload loader: stops before writing anything
		const Connector none = [](std::string& why) {
			why = "no payload loader at 127.0.0.1:9021 (errno 61: Connection refused)";
			return -1;
		};
		CHECK(!WriteTest::Get().Run("/data/wt", none, SmallPlan(), log));
		CHECK(Has(lines, "writetest: stopped: the broker could not be started"));
		CHECK(!Has(lines, "A title-seq start"));
		struct stat st;
		CHECK(stat(Real("/data/wt").c_str(), &st) != 0);
	}
	lines.clear();
	{   // a data folder the broker doesn't write in
		const Connector never = [](std::string&) { return -1; };
		CHECK(!WriteTest::Get().Run("/tmp/ogbox-broker-test-wt", never, Plan{}, log));
		CHECK(Has(lines, "is not under /data/"));
	}
	lines.clear();
	{   // not enough room
		Plan p = SmallPlan();
		p.minFree = ~0ull;
		const Connector never = [](std::string&) { return -1; };
		CHECK(!WriteTest::Get().Run("/data/wt", never, p, log));
		CHECK(Has(lines, "GiB free there; the test needs"));
	}
	lines.clear();
	{   // a broker that answers HELLO and then goes away (as A starts): stopped cleanly
		std::thread server;
		int fds[2] = { -1, -1 };
		const Connector connect = [&](std::string& why) {
			if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
				return -1;
			server = std::thread([&]() {
				ServerOptions o;
				o.root = g_root.c_str();
				::broker::serve(fds[1], fds[1], o);
			});
			why = "a socketpair";
			return fds[0];
		};
		const Log cut = [&](const std::string& l) {
			log(l);
			if (l.find("A title-seq start") != std::string::npos)
				shutdown(fds[1], SHUT_RDWR);
		};
		CHECK(!WriteTest::Get().Run("/data/wt", connect, SmallPlan(), cut));
		server.join();
		close(fds[1]);
		CHECK(Has(lines, "writetest: stopped: the broker's connection broke in B"));
		CHECK(Has(lines, "the broker deleted 0 files, OGBox 1; quit not sent (the connection broke)"));
		struct stat st;
		CHECK(stat(Real("/data/wt").c_str(), &st) != 0);
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: broker_test <work folder>\n");
		return 2;
	}
	g_root = argv[1];
	mkdir(g_root.c_str(), 0777);
	TestPaths();
	TestRequests();
	TestCacheCalls();
	TestEnds();
	TestLaunch();
	TestWriteTest();
	if (g_fail)
	{
		fprintf(stderr, "%d checks failed\n", g_fail);
		return 1;
	}
	fprintf(stderr, "broker: all checks passed\n");
	return 0;
}
