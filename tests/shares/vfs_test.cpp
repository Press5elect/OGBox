// OGBox by Press5elect, GPL-3.0-or-later. The virtual file layer above its backends
// (ps5/src/platform/vfs/ps5_vfs.cpp), with a backend in memory: a disc read that fails partway, seeks
// and reads whose sizes overflow, a share taken away while one of its files is open, and a disc
// image closed while another is opened.
#include "platform/vfs/ps5_vfs.h"
#include "platform/vfs/ps5_vfs_internal.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unistd.h>

static int s_failures = 0;
#define CHECK(x)                                                                    \
	do {                                                                            \
		if (!(x)) {                                                                 \
			std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); \
			s_failures++;                                                           \
		}                                                                           \
	} while (0)

namespace ps5vfs {
// The network backends are not part of this test.
std::unique_ptr<Backend> CreateSMBBackend(const char *, std::string *error) { *error = "not in this test"; return nullptr; }
std::unique_ptr<Backend> CreateNFSBackend(const char *, std::string *error) { *error = "not in this test"; return nullptr; }
std::unique_ptr<Backend> CreateFTPBackend(unsigned, const char *, std::string *error) { *error = "not in this test"; return nullptr; }
} // namespace ps5vfs

// The title links with --wrap=pread --wrap=fstat; here the real ones are libc's.
extern "C" ssize_t __real_pread(int fd, void *buffer, size_t length, off_t offset) { return pread(fd, buffer, length, offset); }
extern "C" int __real_fstat(int fd, struct stat *st) { return fstat(fd, st); }
extern "C" ssize_t __wrap_pread(int fd, void *buffer, size_t length, off_t offset);

namespace {

constexpr int64_t BLOCK = 256 * 1024;   // the layer's block
constexpr int64_t SIZE = 3 * BLOCK;

struct State {
	std::atomic<bool> destroyed{ false };
	std::atomic<int64_t> fail_at{ -1 };   // a read that covers this offset fails
	std::mutex mutex;
	std::condition_variable cv;
	bool hold_close = false;              // a file's close waits (a share that stopped answering)
	bool closing = false;
};

class TestFile final : public ps5vfs::RemoteFile {
public:
	explicit TestFile(State *s) : m_s(s) {}
	~TestFile() override
	{
		std::unique_lock lock(m_s->mutex);
		m_s->closing = true;
		m_s->cv.notify_all();
		m_s->cv.wait(lock, [this] { return !m_s->hold_close; });
	}
	int64_t Size() const override { return SIZE; }
	int64_t Read(void *buffer, uint32_t length, int64_t offset) override
	{
		const int64_t fail = m_s->fail_at.load();
		if (fail >= offset && fail < offset + length) {
			errno = EIO;
			return -1;
		}
		const int64_t n = std::min<int64_t>(length, SIZE - offset);
		for (int64_t i = 0; i < n; i++) {
			static_cast<uint8_t *>(buffer)[i] = static_cast<uint8_t>((offset + i) & 0xff);
		}
		return n < 0 ? 0 : n;
	}

private:
	State *m_s;
};

class TestBackend final : public ps5vfs::Backend {
public:
	explicit TestBackend(State *s) : m_s(s) {}
	~TestBackend() override { m_s->destroyed = true; }
	bool Stat(const std::string &path, ps5vfs::Entry *entry) override
	{
		if (path.empty()) {
			*entry = ps5vfs::Entry{ std::string(), true, 0, 0 };
			return true;
		}
		if (path == "a.bin" || path == "b.bin") {
			*entry = ps5vfs::Entry{ path, false, SIZE, 1000 };
			return true;
		}
		errno = ENOENT;
		return false;
	}
	bool List(const std::string &, std::vector<ps5vfs::Entry> *entries) override
	{
		entries->clear();
		entries->push_back(ps5vfs::Entry{ "a.bin", false, SIZE, 1000 });
		entries->push_back(ps5vfs::Entry{ "b.bin", false, SIZE, 1000 });
		return true;
	}
	std::unique_ptr<ps5vfs::RemoteFile> Open(const std::string &path) override
	{
		ps5vfs::Entry e;
		if (!Stat(path, &e) || e.is_directory) {
			errno = ENOENT;
			return nullptr;
		}
		return std::make_unique<TestFile>(m_s);
	}
	std::string Describe() const override { return "test"; }

private:
	State *m_s;
};

// 6.13: a seek past what 64 bits hold, and an fread whose size times count does not fit in size_t
void testOverflow(State &)
{
	FILE *fp = ps5vfs_fopen("/t/a.bin", "rb");
	CHECK(fp != nullptr);
	if (!fp) {
		return;
	}
	CHECK(ps5vfs_fseeko(fp, INT64_MAX, SEEK_SET) == 0);
	errno = 0;
	CHECK(ps5vfs_fseeko(fp, 10, SEEK_CUR) == -1 && errno == EINVAL);
	CHECK(ps5vfs_fseeko(fp, INT64_MAX, SEEK_END) == -1 && errno == EINVAL);
	CHECK(ps5vfs_fseeko(fp, 0, SEEK_SET) == 0);
	unsigned char small[16];
	// (SIZE_MAX / 2 + 1) * 3 wraps to SIZE_MAX / 2 + 1: the whole file into 16 bytes
	CHECK(ps5vfs_fread(small, SIZE_MAX / 2 + 1, 3, fp) == 0);
	CHECK(ps5vfs_fread(small, 1, sizeof(small), fp) == sizeof(small) && small[5] == 5);
	ps5vfs_fclose(fp);
}

// 6.4: a disc read that fails partway gives what it read; the failure is not kept for later reads
void testDiscReadFailure(State &s)
{
	const int fd = ps5vfs_disc_open("/t/a.bin");
	CHECK(fd >= 0);
	if (fd < 0) {
		return;
	}
	std::vector<uint8_t> buffer(static_cast<size_t>(2 * BLOCK));
	s.fail_at = BLOCK + 5;
	const ssize_t got = __wrap_pread(fd, buffer.data(), buffer.size(), 0);
	CHECK(got == BLOCK);
	errno = 0;
	CHECK(__wrap_pread(fd, buffer.data(), 16, BLOCK) == -1 && errno == EIO);
	// past the end: the end (0), not the failure of a read before
	s.fail_at = BLOCK + 5;
	(void)__wrap_pread(fd, buffer.data(), 2 * BLOCK, 0);   // short again
	CHECK(__wrap_pread(fd, buffer.data(), 16, SIZE) == 0);
	s.fail_at = -1;
	CHECK(__wrap_pread(fd, buffer.data(), 16, BLOCK + 3) == 16 && buffer[0] == ((BLOCK + 3) & 0xff));
	ps5vfs_disc_close(fd);
}

// 6.6: a share taken away is let go when its last file closes, at once when none is open
void testUnmount()
{
	static State kept;
	ps5vfs::Mount("/u1", std::make_unique<TestBackend>(&kept));
	FILE *fp = ps5vfs_fopen("/u1/a.bin", "rb");
	CHECK(fp != nullptr);
	CHECK(ps5vfs_unmount("/u1") == 0);
	CHECK(!kept.destroyed);
	unsigned char b[4];
	CHECK(fp && ps5vfs_fread(b, 1, 4, fp) == 4 && b[3] == 3);
	if (fp) {
		ps5vfs_fclose(fp);
	}
	CHECK(kept.destroyed);

	static State idle;
	ps5vfs::Mount("/u2", std::make_unique<TestBackend>(&idle));
	CHECK(ps5vfs_unmount("/u2") == 0);
	CHECK(idle.destroyed);
	CHECK(ps5vfs_fopen("/u2/a.bin", "rb") == nullptr);
}

// 6.10: a disc image whose close waits on its share does not hold up another being opened
void testDiscCloseOutsideLock(State &s)
{
	const int first = ps5vfs_disc_open("/t/a.bin");
	CHECK(first >= 0);
	{
		std::lock_guard lock(s.mutex);
		s.hold_close = true;
		s.closing = false;
	}
	std::thread closer([first] { ps5vfs_disc_close(first); });
	{
		std::unique_lock lock(s.mutex);
		s.cv.wait(lock, [&] { return s.closing; });
	}
	std::atomic<int> second{ -2 };
	std::thread opener([&] { second = ps5vfs_disc_open("/t/b.bin"); });
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (second == -2 && std::chrono::steady_clock::now() < until) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	CHECK(second >= 0);
	{
		std::lock_guard lock(s.mutex);
		s.hold_close = false;
		s.cv.notify_all();
	}
	closer.join();
	opener.join();
	if (second >= 0) {
		ps5vfs_disc_close(second);
	}
}

} // namespace

int main()
{
	static State state;
	ps5vfs::Mount("/t", std::make_unique<TestBackend>(&state));
	testOverflow(state);
	testDiscReadFailure(state);
	testUnmount();
	testDiscCloseOutsideLock(state);
	ps5vfs_unmount("/t");
	if (s_failures) {
		std::fprintf(stderr, "vfs: %d failed\n", s_failures);
		return 1;
	}
	std::puts("vfs: ok");
	return 0;
}
