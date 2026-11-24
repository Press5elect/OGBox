// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 1.0.4 - the write broker's client, and the storage write test built on it (a diagnostic:
// Settings > Advanced > Storage write test).
//
// The console holds a title's writes to its storage back after a burst of about 1.3 GiB (242 MiB/s,
// then about 2 MiB/s; the budget is the process's: PS5_PayloadSDK platform/docs/PROBE.md). The
// broker (ps5/broker, write-broker.elf in the title folder) is a payload, a process of its own: the
// title sends it to the payload loader on 127.0.0.1:9021, as it sends the elevation helper, and then
// speaks ps5/broker/protocol.hpp with it over the same connection. Whether a payload's writes are
// held back the same way decides whether OGBox's writes should go through it; the write test
// measures that and logs every number to the boot log as "writetest:" lines. 1.0.4: the cache
// drives' write-back goes through it too (Settings > Advanced > Fast writes on the console's
// storage: ps5_wbremote.h).

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <sys/uio.h>
#include <vector>

#include "../../broker/protocol.hpp"

namespace ogbox::broker
{
using ::broker::BenchArgs;
using ::broker::BenchResult;
using ::broker::BenchStep;
using ::broker::Op;
using ::broker::Reply;

/// One connection to the broker. Requests may be sent ahead of their replies (pipelined); the
/// replies are taken in the order the requests went, and one out of that order is a protocol error.
/// The calls return 0, the broker's errno, or -1 when the connection failed (`error` says how).
class Client
{
public:
	explicit Client(int fd = -1) : m_fd(fd) {}
	~Client();
	Client(const Client&) = delete;
	Client& operator=(const Client&) = delete;

	int Fd() const { return m_fd; }
	const std::string& Error() const { return m_error; }
	size_t InFlight() const { return m_pending.size(); }

	bool Send(Op op, uint32_t handle, uint64_t offset, uint64_t length, uint32_t flags, const void* body, size_t size,
		uint32_t* seq = nullptr);
	/// 1.0.4: a request whose body is the n pieces one after the other (a WRITE from the cache
	/// drives' held items).
	bool SendV(Op op, uint32_t handle, uint64_t offset, const struct iovec* iov, int n);
	/// 1.0.4: no more requests: the connection's sending side shut, the broker's answers read and
	/// let go until it ends (it carries out every request it got whole first), for at most wait_ms;
	/// then closed. false: it hadn't ended by then.
	bool Finish(int wait_ms);
	/// The next reply (and its body, when it has one and body is given).
	bool Receive(Reply& reply, std::vector<uint8_t>* body, int timeout_ms);
	/// Send, then its reply; -1 or the reply's error.
	int Call(Op op, uint32_t handle, uint64_t offset, uint64_t length, uint32_t flags, const void* body, size_t size,
		Reply& reply, std::vector<uint8_t>* out, int timeout_ms);

	int Hello(std::string& text, uint32_t& version, int timeout_ms);
	int Open(const std::string& path, uint32_t flags, uint32_t& handle, bool& direct);
	int Write(uint32_t handle, uint64_t offset, const void* data, size_t size, uint64_t& written);
	int Read(uint32_t handle, uint64_t offset, size_t size, std::vector<uint8_t>& out);
	int Truncate(uint32_t handle, uint64_t size);
	int Sync(uint32_t handle, int timeout_ms);
	int Close(uint32_t handle);
	int Unlink(const std::string& path);
	/// The broker's own writes (protocol.hpp BENCH); steps as many as the result says.
	int Bench(const std::string& path, const BenchArgs& args, BenchResult& result, std::vector<BenchStep>& steps);
	int Quit();

	int timeout_ms = 30000;   // a reply's wait, for the calls without their own

private:
	int m_fd;
	uint32_t m_seq = 1;
	std::deque<std::pair<uint32_t, uint16_t>> m_pending;   // seq, op
	std::string m_error;
	bool Fail(const char* what);
};

/// Connects to the payload loader on 127.0.0.1:port and sends it the broker's ELF: the connection
/// (the broker's once the loader has started it), or -1 with why.
int Launch(const std::string& elf, std::string& why, unsigned port = 9021);

/// The write test's sizes and limits (the PC test runs a small one).
struct Plan
{
	uint64_t chunk = 16ull << 20;
	uint64_t aTotal = 2ull << 30;   unsigned aSeconds = 120;   // A: the title's own writes
	uint64_t bTotal = 4ull << 30;   unsigned bSeconds = 180;   // B: the broker's own, 16 MiB at a time
	uint64_t cTotal = 1ull << 30;   unsigned cSeconds = 60;    // C: the broker's own, 64 KiB at a time
	uint32_t cChunk = 64u << 10;
	uint64_t dSpace = 1ull << 30;   unsigned dSeconds = 30;    // D: random 4 KiB and 16 KiB, each
	uint64_t eTotal = 1ull << 30;   unsigned eSeconds = 120;   // E: the title's 16 MiB through the broker
	uint64_t eSpace = 1ull << 30;   unsigned eRandSeconds = 20;// ... and 4 KiB at random, then 64 in flight
	unsigned ePipeline = 64;
	uint64_t fTotal = 256ull << 20; unsigned fSeconds = 60;    // F: the title's own again
	unsigned verifyRanges = 64;     uint64_t verifyRange = 1ull << 20;
	unsigned verifyWrites = 256;
	uint64_t minFree = 10ull << 30;
	int helloMs = 20000;
	/// The PC test's: OGBox's own file calls go to this folder + the path, as the test's broker's do
	/// (::broker::ServerOptions::root). "" on the console: both see the same paths.
	std::string localRoot;
};

using Log = std::function<void(const std::string& line)>;
/// The broker's connection (Launch's), or -1 with why.
using Connector = std::function<int(std::string& why)>;

/// The storage write test, on a thread of its own (ps5_broker.cpp says what each phase is).
class WriteTest
{
public:
	struct Progress
	{
		bool running{ false }, done{ false }, ok{ false };
		std::string phase;     // "B: the broker writes 16 MiB at a time"
		std::string detail;    // "45 s of at most 180 s, 1234 MiB"
		std::string summary;   // at the end: what it found, a line a phase
		double seconds{ 0.0 }; // since it began
	};
	static WriteTest& Get();
	/// false: one is under way already. In dir (made; refused unless the broker accepts it).
	bool Start(const std::string& dir, Connector connect, const Plan& plan, Log log);
	Progress State();
	/// Runs the test on the calling thread (Start's thread does this; the PC test calls it).
	bool Run(const std::string& dir, Connector connect, const Plan& plan, Log log);

private:
	std::mutex m_mutex;
	Progress m_state;
	uint64_t m_began = 0;
	void Phase(const std::string& phase, const std::string& detail);
	void Detail(const std::string& detail);
};

/// The free bytes of the file system path is on; false when unknown.
bool FreeBytes(const std::string& path, uint64_t& bytes);
} // namespace ogbox::broker
