// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
// Crux2 - the console side of the port: the boot log.
//
// Kelvin: from Crux2 v1.0.15 (github.com/Press5elect/Crux2 228d0a5, ps5/frontend/ps5_platform.cpp:
// ps5px_log, the boot log's opening in EarlyInit with the previous run's kept and standard error
// moved into it, and the "cpu:" line). Changed for Kelvin: no sandbox elevation, USB drives or
// shares (Kelvin stays in /app0); standard error's lines are copied on to klog by a watcher
// thread, since the boot log has taken it from the SDK's klog capture; the header has the
// firmware, the clock, the memory and the eboot's address; and Kelvin's own: the probe's state
// in a heartbeat ("alive:"), the thread names for the crash report, and the "session:" line.
// The host reference build writes the same log in its title folder (PS5_APP_ROOT).
// Kelvin by Press5elect.
//
// PSHakuX: from Kelvin 3061bf1 (ps5/src/platform/ps5_bootlog.cpp), the probe's state replaced by a
// status line the core gives (pshakux_diag_set_status): pshakux-boot.log, pshakux-boot.previous.log.
// PSHakuX by Press5elect.

#if !defined(__PROSPERO__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "ps5_bootlog.h"


#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <new>
#include <pthread.h>
#include <sched.h>
#include <streambuf>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__PROSPERO__)
#include <ps5platform/kernel.h>
#include <ps5platform/klog.h>
#include <pthread_np.h> // pthread_set_name_np (already imported by the template's code)
#include "ps5_modules.h" // PS5SystemModules::FindFunction
extern "C" void ps5px_clock_line(char* line, unsigned size); // ps5_clock.c
#else
#include <sys/syscall.h>
#include <sys/utsname.h>
#endif

#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

namespace {

constexpr char TITLE_ID[] = "PPSA99251";

// Fixed storage: read by the crash report, in a signal handler.
char s_title_dir[160] = PS5_APP_ROOT;
char s_prefix[40] = "[PSHakuX] ";
int s_log_fd = -1;
int s_read_fd = -1; // the watcher's own descriptor of the log, to copy standard error on to klog
#if !defined(__PROSPERO__)
int s_term_fd = 2; // the host reference build's terminal: the standard error the process started with
#endif
std::atomic<bool> s_owns_stderr{false};
struct timespec s_start{};


// The watcher: klog's copy of standard error, and the heartbeat.
pthread_mutex_t s_forward_lock = PTHREAD_MUTEX_INITIALIZER;
off_t s_forward_offset = 0;
char s_forward_partial[1024];
size_t s_forward_partial_length = 0;
std::atomic<bool> s_watcher_running{false};

// Thread names for the crash report: a fixed table, claimed with a compare-and-swap.
constexpr int MAX_THREADS = 64;
std::atomic<int> s_thread_used[MAX_THREADS];
pthread_t s_thread_ids[MAX_THREADS];
const char* volatile s_thread_names[MAX_THREADS];

#if defined(__PROSPERO__)
using ThreadIdFn = int (*)(void);
ThreadIdFn s_getthreadid = nullptr; // pthread_getthreadid_np, found while the title runs
using PrioFn = int (*)(pthread_t, int);
using GetPrioFn = int (*)(pthread_t, int*);
#endif

double Since(const struct timespec& then)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<double>(now.tv_sec - then.tv_sec) + static_cast<double>(now.tv_nsec - then.tv_nsec) / 1e9;
}

// Whether a line of the log is one of pshakux_log's ("[   12.345] "), which went to klog already.
bool IsLogLine(const char* line, size_t length)
{
  if (length < 8 || line[0] != '[')
    return false;
  size_t i = 1;
  while (i < length && line[i] == ' ')
    i++;
  const size_t digits = i;
  while (i < length && line[i] >= '0' && line[i] <= '9')
    i++;
  if (i == digits || i + 5 >= length || line[i] != '.')
    return false;
  for (int k = 1; k <= 3; k++)
    if (line[i + k] < '0' || line[i + k] > '9')
      return false;
  return line[i + 4] == ']' && line[i + 5] == ' ';
}

void ForwardLine(const char* line, size_t length)
{
  if (IsLogLine(line, length))
    return;
#if defined(__PROSPERO__)
  char record[1100];
  const int n = std::snprintf(record, sizeof(record), "%s%.*s\n", s_prefix, static_cast<int>(length), line);
  if (n > 0)
    pshakux_klog_raw(record, static_cast<unsigned>(std::min<size_t>(static_cast<size_t>(n), sizeof(record) - 1)));
#else
  // The host's say() writes its own prefix; the driver's lines are given as they are.
  char record[1100];
  const size_t n = std::min(length, sizeof(record) - 2);
  std::memcpy(record, line, n);
  record[n] = '\n';
  record[n + 1] = '\0';
  pshakux_klog_raw(record, static_cast<unsigned>(n + 1));
#endif
}

// Copies the lines standard error has added to the log since the last look on to klog.
void Forward()
{
  if (s_read_fd < 0)
    return;
  pthread_mutex_lock(&s_forward_lock);
  char chunk[4096];
  for (;;)
  {
    const ssize_t got = pread(s_read_fd, chunk, sizeof(chunk), s_forward_offset);
    if (got <= 0)
      break;
    s_forward_offset += got;
    for (ssize_t i = 0; i < got; i++)
    {
      if (chunk[i] == '\n' || s_forward_partial_length == sizeof(s_forward_partial))
      {
        ForwardLine(s_forward_partial, s_forward_partial_length);
        s_forward_partial_length = 0;
        if (chunk[i] == '\n')
          continue;
      }
      s_forward_partial[s_forward_partial_length++] = chunk[i];
    }
  }
  pthread_mutex_unlock(&s_forward_lock);
}

// std::cerr writes through the stream it was made with at start-up, not through the stderr the
// boot log puts in place: this hands what it writes to the current stderr.
class StderrBuffer final : public std::streambuf
{
protected:
  int overflow(int c) override
  {
    if (c == EOF)
      return 0;
    const char ch = static_cast<char>(c);
    std::fwrite(&ch, 1, 1, stderr);
    return c;
  }
  std::streamsize xsputn(const char* s, std::streamsize n) override
  {
    return static_cast<std::streamsize>(std::fwrite(s, 1, static_cast<size_t>(n), stderr));
  }
  int sync() override { return std::fflush(stderr); }
};
// Never destroyed: std::cout and std::cerr are flushed after the statics are gone.
alignas(StderrBuffer) unsigned char s_stderr_storage[sizeof(StderrBuffer)];

// The processor's clock, measured: a chain of dependent additions takes one cycle each.
double MeasureCpuGHz()
{
  double best = 0;
  for (int round = 0; round < 3; round++)
  {
    constexpr uint64_t loops = 4000000; // 8 additions each: 32 million cycles, about 10 ms
    uint64_t x = 0;
    volatile uint64_t one = 1;
    const uint64_t step = one;
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);
    for (uint64_t i = 0; i < loops; i++)
    {
#if defined(__x86_64__)
      // Register operands (Crux2 adds immediates): a processor that folds immediate additions
      // in its renamer (Intel's since Golden Cove) would read twice its clock; Zen 2 reads the same.
      __asm__ volatile("add %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\t"
                       "add %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\tadd %1, %0"
                       : "+r"(x)
                       : "r"(step));
#else
      x += 8;
      __asm__ volatile("" : "+r"(x));
#endif
    }
    const double seconds = Since(started);
    if (seconds > 0)
      best = std::max(best, static_cast<double>(loops) * 8.0 / seconds / 1e9);
  }
  return best;
}

#if defined(__x86_64__) && !defined(__PROSPERO__) // the console asks the kernel for the rate
uint64_t ReadTsc()
{
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (static_cast<uint64_t>(hi) << 32) | lo;
}
#endif

void LogHeader()
{
  const std::time_t now = std::time(nullptr);
  char when[64] = "";
  if (const std::tm* const tm = std::gmtime(&now))
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", tm);
#if defined(__PROSPERO__)
  pshakux_log("PSHakuX (%s) starts, %s", TITLE_ID, when);
#else
  pshakux_log("PSHakuX (%s) starts, %s (host reference build)", TITLE_ID, when);
#endif
  pshakux_log("build identity: %s", pshakux_build_identity());
  pshakux_log("build: PSHakuX %s, revision %.12s%s, built %s; RADV %s; payload SDK %.12s", pshakux_build_version(),
             pshakux_build_revision(), pshakux_build_dirty() ? " with uncommitted changes (dirty)" : "", pshakux_build_stamp(),
             pshakux_build_radv(), pshakux_build_sdk());
  pshakux_log("build: hakuX %s; CPU backend %s", pshakux_build_hakux(), pshakux_build_cpu());

#if defined(__PROSPERO__)
  {
    struct ps5_kernel_sw_version version;
    std::memset(&version, 0, sizeof(version));
    version.size = sizeof(version);
    const int result = sceKernelGetSystemSwVersion(&version);
    char text[sizeof(version.text) + 1];
    std::memcpy(text, version.text, sizeof(version.text));
    text[sizeof(version.text)] = '\0';
    if (result == 0)
      pshakux_log("firmware: %s (0x%08x)", text, version.version);
    else
      pshakux_log("firmware: sceKernelGetSystemSwVersion answered 0x%08x", static_cast<unsigned>(result));
  }
#else
  {
    struct utsname name;
    if (uname(&name) == 0)
      pshakux_log("system: %s %s %s (host reference build)", name.sysname, name.release, name.machine);
  }
#endif
  pshakux_log("title folder: %s (this log: pshakux-boot.log, the run before: pshakux-boot.previous.log)", s_title_dir);
}

void LogMachine()
{
  // Where the eboot is: the crash report's addresses are given relative to it.
  pshakux_log("eboot: loaded at 0x%llx; a crash's \"eboot+0x...\" offsets are looked up "
             "with llvm-addr2line -f -C -e %s <0x%llx + offset>",
             static_cast<unsigned long long>(pshakux_eboot_start()),
#if defined(__PROSPERO__)
             "build/ps5/link/llvm-pie.elf",
#else
             "build/ps5-host/ps5-samples-host",
#endif
             static_cast<unsigned long long>(pshakux_eboot_link_base()));

  const double ghz = MeasureCpuGHz();
  const long cpus = pshakux_sysconf(_SC_NPROCESSORS_ONLN);
#if defined(__PROSPERO__)
  pshakux_log("cpu: about %.2f GHz (dependent additions, best of 3); %ld logical CPUs online; built with SDK version "
             "2.00, the kernel's high-frequency profile is expected (klog [BAPM] names it)",
             ghz, cpus);
  {
    const uint64_t hz = sceKernelGetTscFrequency();
    char line[256] = "";
    ps5px_clock_line(line, sizeof(line));
    pshakux_log("clock: TSC %llu Hz (%.4f GHz, sceKernelGetTscFrequency); %s", static_cast<unsigned long long>(hz),
               static_cast<double>(hz) / 1e9,
               line[0] ? line : "the title's monotonic clock (ps5_clock.c) has not started yet");
  }
  {
    const int64_t total = sceKernelGetDirectMemorySize();
    int64_t start = 0;
    size_t run = 0;
    const int direct = sceKernelAvailableDirectMemorySize(0, total, PS5_KERNEL_DIRECT_ALIGNMENT, &start, &run);
    size_t flexible = 0;
    const int flex = sceKernelAvailableFlexibleMemorySize(&flexible);
    pshakux_log("memory: direct memory %lld MiB in all, %llu MiB in its largest free run (result 0x%x); flexible memory "
               "%llu MiB free (result 0x%x; param.json gives the title 1024 MiB)",
               static_cast<long long>(total >> 20), static_cast<unsigned long long>(run >> 20),
               static_cast<unsigned>(direct), static_cast<unsigned long long>(flexible >> 20), static_cast<unsigned>(flex));
  }
#else
  pshakux_log("cpu: about %.2f GHz (dependent additions, best of 3); %ld logical CPUs online", ghz, cpus);
  {
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const uint64_t tsc0 = ReadTsc();
    while (Since(t0) < 0.02)
    {
    }
    const double seconds = Since(t0);
    const uint64_t tsc1 = ReadTsc();
    pshakux_log("clock: TSC about %.0f Hz (measured over %.0f ms against CLOCK_MONOTONIC)",
               static_cast<double>(tsc1 - tsc0) / seconds, seconds * 1e3);
  }
  {
    const long page = sysconf(_SC_PAGESIZE);
    pshakux_log("memory: %lld MiB free of %lld MiB (sysconf)",
               static_cast<long long>(sysconf(_SC_AVPHYS_PAGES)) * page >> 20,
               static_cast<long long>(sysconf(_SC_PHYS_PAGES)) * page >> 20);
  }
#endif
}

#if defined(__PROSPERO__)
// The watcher runs ahead of the title's threads (priority 690, as Crux2's sound role; the title's
// threads are at 700 and a thread that never waits keeps its processor from every thread of its
// priority or below), so the heartbeat goes on while the probe's busy threads take every core.
void RaiseWatcherPriority()
{
  const auto set = reinterpret_cast<PrioFn>(PS5SystemModules::FindFunction("libkernel", "scePthreadSetprio", "W0Hpm2X0uPE"));
  const auto get = reinterpret_cast<GetPrioFn>(PS5SystemModules::FindFunction("libkernel", "scePthreadGetprio", "1tKyG7RlMJo"));
  if (!set)
  {
    // The POSIX calls instead (libkernel's own, in every title's imports): log what the thread
    // has, then ask for 690, the number scePthreadSetprio would take (lower runs first).
    int policy = -1;
    struct sched_param param{};
    const int got = pthread_getschedparam(pthread_self(), &policy, &param);
    const int before = param.sched_priority;
    struct sched_param want = param;
    want.sched_priority = 690;
    const int result = got == 0 ? pthread_setschedparam(pthread_self(), policy, &want) : -1;
    struct sched_param now{};
    int policy_now = -1;
    pthread_getschedparam(pthread_self(), &policy_now, &now);
    pshakux_log("watcher: scePthreadSetprio not found; pthread_getschedparam %d (policy %d, priority %d), "
               "pthread_setschedparam to 690: %d (now policy %d, priority %d)",
               got, policy, before, result, policy_now, now.sched_priority);
    return;
  }
  int before = -1, after = -1;
  if (get)
    get(pthread_self(), &before);
  const int result = set(pthread_self(), 690);
  if (get)
    get(pthread_self(), &after);
  pshakux_log("watcher: priority %d -> 690: 0x%x (now %d); ahead of the title's threads", before,
             static_cast<unsigned>(result), after);
}
#endif

// PSHakuX: what the heartbeat says beside the time comes from the core (pshakux_diag_set_status):
// the guest's state, frames and the like; the provider writes one line into its buffer.
std::atomic<pshakux_status_fn> s_status{nullptr};
std::atomic<int> s_status_fast{0};

void Heartbeat()
{
  const unsigned long long ms = static_cast<unsigned long long>(Since(s_start) * 1e3);
  char line[512] = "";
  if (const pshakux_status_fn status = s_status.load(std::memory_order_acquire))
    status(line, sizeof(line));
  pshakux_log("alive: t=%llu ms%s%s", ms, line[0] ? " " : "", line);
}

void* WatcherMain(void*)
{
  pshakux_thread_named("pshakux-watch");
  pshakux_install_crash_stack();
#if defined(__PROSPERO__)
  pthread_set_name_np(pthread_self(), "pshakux-watch");
  RaiseWatcherPriority();
#endif
  double next_beat = 0;
  while (s_watcher_running.load(std::memory_order_relaxed))
  {
    usleep(50000);
    Forward();
    const double now = Since(s_start);
    if (now >= next_beat)
    {
      Heartbeat();
      next_beat = now + (s_status_fast.load(std::memory_order_relaxed) ? 0.25 : 2.0);
    }
    else if (s_status_fast.load(std::memory_order_relaxed) && next_beat - now > 0.25)
    {
      next_beat = now + 0.25; // a fast heartbeat was asked for within a quiet interval
    }
  }
  Forward();
  return nullptr;
}

} // namespace

extern "C" const char* pshakux_title_dir(void)
{
  return s_title_dir;
}

extern "C" int pshakux_log_fd(void)
{
  return s_log_fd;
}

extern "C" int pshakux_diag_owns_stderr(void)
{
  return s_owns_stderr.load(std::memory_order_relaxed) ? 1 : 0;
}

extern "C" double pshakux_uptime(void)
{
  return Since(s_start);
}

extern "C" void pshakux_klog_raw(const char* text, unsigned length)
{
#if defined(__PROSPERO__)
  (void)length;
  (void)sceKernelDebugOutText(0, text);
#else
  (void)!write(s_term_fd, text, length);
#endif
}

extern "C" void pshakux_log(const char* format, ...)
{
  char text[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);

  char line[1100];
  int length = std::snprintf(line, sizeof(line), "%s%s\n", s_prefix, text);
  if (length > 0)
    pshakux_klog_raw(line, static_cast<unsigned>(std::min<size_t>(static_cast<size_t>(length), sizeof(line) - 1)));

  if (s_log_fd >= 0)
  {
    length = std::snprintf(line, sizeof(line), "[%9.3f] %s\n", Since(s_start), text);
    if (length > 0)
      (void)!write(s_log_fd, line, std::min<size_t>(static_cast<size_t>(length), sizeof(line) - 1));
  }
}

extern "C" void pshakux_thread_named(const char* name)
{
  const pthread_t self = pthread_self();
  for (int i = 0; i < MAX_THREADS; i++)
  {
    if (s_thread_used[i].load(std::memory_order_acquire) == 2 && pthread_equal(s_thread_ids[i], self))
    {
      s_thread_names[i] = name;
      return;
    }
  }
  for (int i = 0; i < MAX_THREADS; i++)
  {
    int expected = 0;
    if (s_thread_used[i].compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
    {
      s_thread_ids[i] = self;
      s_thread_names[i] = name;
      s_thread_used[i].store(2, std::memory_order_release);
      return;
    }
  }
}

extern "C" void pshakux_thread_gone(void)
{
  const pthread_t self = pthread_self();
  for (int i = 0; i < MAX_THREADS; i++)
  {
    if (s_thread_used[i].load(std::memory_order_acquire) == 2 && pthread_equal(s_thread_ids[i], self))
    {
      s_thread_names[i] = nullptr;
      s_thread_used[i].store(0, std::memory_order_release);
      return;
    }
  }
}

extern "C" const char* pshakux_thread_name(void)
{
  const pthread_t self = pthread_self();
  for (int i = 0; i < MAX_THREADS; i++)
  {
    if (s_thread_used[i].load(std::memory_order_acquire) == 2 && pthread_equal(s_thread_ids[i], self))
      return s_thread_names[i];
  }
  return nullptr;
}

extern "C" long pshakux_thread_id(void)
{
#if defined(__PROSPERO__)
  return s_getthreadid ? static_cast<long>(s_getthreadid()) : -1;
#else
  return static_cast<long>(syscall(SYS_gettid));
#endif
}

extern "C" void pshakux_diag_start(const char* title_name)
{
  clock_gettime(CLOCK_MONOTONIC, &s_start);
  if (title_name && title_name[0])
    std::snprintf(s_prefix, sizeof(s_prefix), "[%s] ", title_name);
  pshakux_thread_named("main");

  // The boot log; the previous run's is kept, since a crash report is the last thing in it.
  // (The previous log goes first: a rename onto a file is not trusted on an exFAT drive.)
  char log_path[224], previous_path[224];
  std::snprintf(log_path, sizeof(log_path), "%s/pshakux-boot.log", s_title_dir);
  std::snprintf(previous_path, sizeof(previous_path), "%s/pshakux-boot.previous.log", s_title_dir);
  (void)unlink(previous_path);
  (void)rename(log_path, previous_path);
  s_log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
  (void)chmod(log_path, 0666);
  (void)chmod(previous_path, 0666);

#if !defined(__PROSPERO__)
  // The terminal keeps what the process started with as standard error.
  s_term_fd = dup(2);
  if (s_term_fd < 0)
    s_term_fd = 2;
#endif

  // Standard error goes into the boot log, each line written as it is printed: what the graphics
  // driver says before a stop is then in the file. (The console refuses dup2 onto descriptor 2 to a
  // title, so the stream is moved to a second descriptor of the file; both append.) The watcher
  // copies those lines on to klog.
  if (s_log_fd >= 0)
  {
    const int second = open(log_path, O_WRONLY | O_APPEND);
    FILE* const stream = (second >= 0) ? fdopen(second, "a") : nullptr;
    if (stream)
    {
      std::fflush(stderr);
      setvbuf(stream, nullptr, _IOLBF, 0);
      stderr = stream;
      std::cerr.rdbuf(new (s_stderr_storage) StderrBuffer);
      s_owns_stderr.store(true, std::memory_order_relaxed);
    }
    else if (second >= 0)
    {
      close(second);
    }
    s_read_fd = open(log_path, O_RDONLY);
  }

#if defined(__PROSPERO__)
  s_getthreadid = reinterpret_cast<ThreadIdFn>(PS5SystemModules::FindFunction("libkernel", "pthread_getthreadid_np", "3eqs37G74-s"));
#endif

  LogHeader();
  if (s_log_fd < 0)
    pshakux_log("boot log: %s cannot be written (%s): klog only", log_path, std::strerror(errno));

  pshakux_crash_install();
  LogMachine();
  pshakux_log("standard error: %s", pshakux_diag_owns_stderr() ? "into this log, a line at a time; the watcher copies it on to klog"
                                                            : "NOT moved into this log (the template's klog capture keeps it)");

  // The watcher: klog's copy of standard error and the heartbeat ("alive:" every 2 s, every
  // 250 ms while the probe runs a risky step), so a title the system ends without a signal
  // leaves the time and the step in the last line.
  s_watcher_running.store(true, std::memory_order_relaxed);
  pthread_t watcher;
  if (pthread_create(&watcher, nullptr, &WatcherMain, nullptr) == 0)
    pthread_detach(watcher);
  else
    pshakux_log("watcher: could not be started: no heartbeat, and standard error reaches klog only at the end");
}

extern "C" void pshakux_diag_set_status(pshakux_status_fn status, int fast)
{
  s_status_fast.store(fast, std::memory_order_relaxed);
  s_status.store(status, std::memory_order_release);
}

extern "C" void pshakux_diag_drain(void)
{
  Forward();
}

extern "C" void pshakux_diag_session_end(int status)
{
  const double seconds = Since(s_start);
  char line[512] = "";
  if (const pshakux_status_fn provider = s_status.load(std::memory_order_acquire))
    provider(line, sizeof(line));
  pshakux_log("session: %.1f s, status %d%s%s", seconds, status, line[0] ? "; " : "", line);
#if defined(__PROSPERO__)
  line[0] = '\0';
  ps5px_clock_line(line, sizeof(line));
  if (line[0])
    pshakux_log("session: %s", line);
#endif
  Forward();
  if (s_log_fd >= 0)
    fsync(s_log_fd);
}
