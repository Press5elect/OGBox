// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
// Crux2 - the crash report.
//
// A title that faults is closed by the console with an error code and nothing else. This handler
// first writes what a developer needs into the boot log in the title folder (the signal, the
// faulting address, the registers, and the return addresses up the stack), then hands the signal
// back to the console, which closes the title as it would have.
//
// Addresses are also given relative to the start of the program, so that they can be looked up in
// the link's output (which keeps its symbols) whatever address the console loaded the title at:
//
//     llvm-addr2line -f -C -e build/ps5/link/llvm-pie.elf 0x<0x3000000000 + offset>
//
// Kelvin: from Crux2 v1.0.15 (github.com/Press5elect/Crux2 228d0a5, ps5/frontend/ps5_crash.cpp:
// CrashSignalHandler, the crash stacks, RestoreDefault). Changed for Kelvin: no printf in the
// handler (a fixed buffer, hand-made numbers and write(2)), memory is read through a pipe so a
// bad pointer cannot fault the report (the bytes at rip, the frames, the guest stack), every
// register with the segment selectors and the fs/gs bases, the thread's id and name, and the
// CPU probe's state: the step it was in, whether the fault was in 32-bit guest code (and the
// guest's registers then), and the host stack of the guest run. The eboot is linked at
// 0x30_0000_0000 (ps5/ld/), so the ELF address of an offset is that plus the offset. The
// signals the console ends a title with (SIGTERM, SIGXCPU) are reported too, and std::terminate.
// SIGKILL cannot be caught: the boot log's last "alive:" line is then the report. No watchdog
// sampling (Crux2's SIGUSR1 is the probe's signal here). Kelvin by Press5elect.

#if !defined(__PROSPERO__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "ps5_bootlog.h"
#if defined(__PROSPERO__)
#include "ps5_modules.h" // PS5SystemModules
#endif


#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <typeinfo>
#include <ucontext.h>
#include <unistd.h>

#if defined(__PROSPERO__)
// Refuses to build against an SDK whose ucontext_t is not the console's layout.
#include <ps5platform/context.h>

extern "C" void nc_log_flush(void) __attribute__((weak));   // native/core/log.c
// The eboot's first byte: the linker script makes __dso_handle the start of .text (ps5/ld/).
extern "C" char __dso_handle[] __attribute__((weak, visibility("hidden")));
#else
// The program's first byte (the host's linker defines it).
extern "C" const char __executable_start[] __attribute__((weak));
#endif

namespace {

// sigaltstack is an ordinary libkernel import, as in Kelvin on the console. 0.1.0 looked it up
// at run time instead, which cannot find libkernel in this title: the lookup failed and signal
// handlers ran on the interrupted stack (a guest stack, where the kernel could not write the
// signal frame: the title got SIGILL).
int AltStack(const stack_t* ss, stack_t* old)
{
  return sigaltstack(ss, old);
}

volatile sig_atomic_t s_in_crash = 0;
// OGBox: the core's part of a report (pshakux_crash_set_note) and its guest code segment.
std::atomic<pshakux_crash_note_fn> s_note{nullptr};
std::atomic<uint16_t> s_guest_cs{0};
std::atomic<bool> s_terminating{false};
int s_pipe[2] = {-1, -1}; // memory is read through it: write() refuses a bad address with EFAULT

constexpr int FATAL_SIGNALS[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
// What the system may end a title with. (SIGKILL cannot be caught.)
constexpr int ENDING_SIGNALS[] = {SIGTERM, SIGXCPU, SIGXFSZ, SIGHUP};

const char* SignalName(int signal)
{
  switch (signal)
  {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGTRAP: return "SIGTRAP (trap)";
    case SIGSYS: return "SIGSYS (bad system call)";
    case SIGTERM: return "SIGTERM (asked to end)";
    case SIGXCPU: return "SIGXCPU (processor time limit)";
    case SIGXFSZ: return "SIGXFSZ (file size limit)";
    case SIGHUP: return "SIGHUP (hang-up)";
    default: return "signal";
  }
}

// ---- an async-signal-safe line writer -------------------------------------------------------
// Each line: "[   12.345] text\n" to the boot log (the timestamp makes the watcher leave it, as
// it leaves pshakux_log's lines: it goes to klog here), "[OGBox] text\n" to klog.
char s_stamp[16] = "[        ?] ";

struct Out
{
  char text[480];
  size_t n = 0;

  Out& s(const char* str)
  {
    while (str && *str && n < sizeof(text) - 1)
      text[n++] = *str++;
    return *this;
  }
  Out& c(char ch)
  {
    if (n < sizeof(text) - 1)
      text[n++] = ch;
    return *this;
  }
  Out& hex(uint64_t value, int digits = 0)
  {
    char tmp[16];
    int count = 0;
    do
    {
      tmp[count++] = "0123456789abcdef"[value & 15];
      value >>= 4;
    } while (value && count < 16);
    while (count < digits && count < 16)
      tmp[count++] = '0';
    s("0x");
    while (count)
      c(tmp[--count]);
    return *this;
  }
  Out& dec(int64_t value)
  {
    uint64_t magnitude = static_cast<uint64_t>(value);
    if (value < 0)
    {
      c('-');
      magnitude = 0 - magnitude;
    }
    return udec(magnitude);
  }
  Out& udec(uint64_t value)
  {
    char tmp[24];
    int count = 0;
    do
    {
      tmp[count++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value);
    while (count)
      c(tmp[--count]);
    return *this;
  }
  Out& pad(size_t column)
  {
    while (n < column && n < sizeof(text) - 1)
      text[n++] = ' ';
    return *this;
  }
  void end()
  {
    const int fd = pshakux_log_fd();
    if (fd >= 0)
    {
      (void)!write(fd, s_stamp, std::strlen(s_stamp));
      text[n] = '\n';
      (void)!write(fd, text, n + 1);
    }
    char record[520];
    size_t k = 0;
    const char* prefix = "[OGBox] ";
    while (*prefix)
      record[k++] = *prefix++;
    for (size_t i = 0; i < n && k < sizeof(record) - 2; i++)
      record[k++] = text[i];
    record[k++] = '\n';
    record[k] = '\0';
    pshakux_klog_raw(record, static_cast<unsigned>(k));
    n = 0;
  }
};

void SetStamp()
{
  // "[%9.3f] " by hand.
  const double t = pshakux_uptime();
  uint64_t ms = t > 0 ? static_cast<uint64_t>(t * 1000.0 + 0.5) : 0;
  char digits[24];
  int count = 0;
  uint64_t whole = ms / 1000;
  const unsigned frac = static_cast<unsigned>(ms % 1000);
  do
  {
    digits[count++] = static_cast<char>('0' + whole % 10);
    whole /= 10;
  } while (whole && count < 12);
  char out[16];
  int k = 0;
  out[k++] = '[';
  for (int i = count; i < 5; i++) // width 9 = up to 5 digits, '.', 3 digits
    out[k++] = ' ';
  while (count)
    out[k++] = digits[--count];
  out[k++] = '.';
  out[k++] = static_cast<char>('0' + frac / 100);
  out[k++] = static_cast<char>('0' + frac / 10 % 10);
  out[k++] = static_cast<char>('0' + frac % 10);
  out[k++] = ']';
  out[k++] = ' ';
  out[k] = '\0';
  std::memcpy(s_stamp, out, static_cast<size_t>(k + 1));
}

// Reads memory that may not be there: through the pipe, which the kernel fills with copyin and
// refuses with EFAULT, so the report never faults on a bad pointer.
bool SafeRead(uintptr_t address, void* out, size_t size)
{
  if (s_pipe[0] < 0 || size == 0 || size > 256 || address < 0x1000)
    return false;
  char drain[256];
  while (read(s_pipe[0], drain, sizeof(drain)) > 0)
  {
  }
  const ssize_t written = write(s_pipe[1], reinterpret_cast<const void*>(address), size);
  if (written != static_cast<ssize_t>(size))
  {
    if (written > 0)
      while (read(s_pipe[0], drain, sizeof(drain)) > 0)
      {
      }
    return false;
  }
  return read(s_pipe[0], out, size) == static_cast<ssize_t>(size);
}

// OGBox 0.1.31: the system's modules (libkernel, libSceLibcInternal, ...) and some of their
// functions, so an address in them is said by name: "libkernel.sprx+0x138c (after
// pthread_cond_wait+0x2c)". The process's module list shows a title only its own modules, but
// the console's loader answers sceKernelGetModuleInfo for the others' handles too; their exports
// are found with sceKernelDlsym. Made once, before the probes (MapSystemModules), read by Where.
struct ModuleRange
{
  uintptr_t start, end;
  char name[40];
};
struct KnownFunction
{
  uintptr_t address;
  const char* name;
};
ModuleRange s_modules[48];
int s_module_count = 0;
KnownFunction s_functions[160];
int s_function_count = 0;

extern "C" int sceKernelGetModuleInfo(int32_t handle, void* info);
extern "C" int sceKernelDlsym(int32_t handle, const char* symbol, void** address);

const char* const k_known_functions[] = {
  "pthread_mutex_lock", "pthread_mutex_trylock", "pthread_mutex_unlock", "pthread_mutex_timedlock",
  "pthread_cond_wait", "pthread_cond_timedwait", "pthread_cond_signal", "pthread_cond_broadcast",
  "pthread_rwlock_rdlock", "pthread_rwlock_wrlock", "pthread_rwlock_unlock", "pthread_once", "pthread_join",
  "pthread_kill", "pthread_sigmask", "pthread_yield", "pthread_self", "pthread_create", "pthread_getspecific",
  "pthread_setspecific", "pthread_getschedparam", "pthread_setschedparam", "sched_yield", "nanosleep", "usleep",
  "sleep", "clock_gettime", "clock_getres", "gettimeofday", "clock_nanosleep", "sigprocmask", "sigaction",
  "sigaltstack", "sigsuspend", "sigwait", "kill", "raise", "_umtx_op", "__error", "write", "read", "pread",
  "pwrite", "writev", "readv", "open", "close", "fsync", "lseek", "fstat", "stat", "ioctl", "fcntl", "poll",
  "select", "mmap", "munmap", "mprotect", "madvise", "getpid", "sysctl", "sysctlbyname", "socket", "connect",
  "send", "recv", "sendto", "recvfrom", "setsockopt", "getsockopt", "sceKernelUsleep", "sceKernelNanosleep",
  "sceKernelDebugOutText", "sceKernelGetProcessTime", "sceKernelReadTsc", "sceKernelGetTscFrequency",
  "sceKernelWaitEventFlag", "sceKernelWaitSema", "sceKernelSignalSema", "sceKernelWaitEqueue",
  "sceKernelCreateEqueue", "sceKernelMapDirectMemory", "sceKernelAllocateDirectMemory",
  "sceKernelMprotect", "sceKernelMunmap", "sceKernelDlsym", "sceKernelGetModuleInfo", "scePthreadMutexLock",
  "scePthreadMutexUnlock", "scePthreadCondWait", "scePthreadCondTimedwait", "scePthreadYield",
  "malloc", "free", "calloc", "realloc", "memalign", "posix_memalign", "aligned_alloc", "malloc_usable_size",
  "memcpy", "memmove", "memset", "strlen", "vsnprintf", "snprintf", "sprintf", "vfprintf", "fprintf", "printf",
  "fwrite", "fputs", "fflush", "fopen", "fclose", "fread", "localeconv", "setlocale", "strtod", "qsort",
  "abort", "exit", "atexit", "__cxa_atexit", "__cxa_guard_acquire", "__cxa_guard_release",
};

int CompareFunctions(const void* a, const void* b)
{
  const uintptr_t x = static_cast<const KnownFunction*>(a)->address, y = static_cast<const KnownFunction*>(b)->address;
  return x < y ? -1 : x > y ? 1 : 0;
}

void MapSystemModules()
{
  if (s_module_count)
    return;
  static const int32_t ranges[][2] = {{0, 0x80}, {0x2000, 0x2080}};
  for (const auto& range : ranges)
  {
    for (int32_t handle = range[0]; handle < range[1] && s_module_count < 48; handle++)
    {
      alignas(8) uint8_t info[0x160] = {};
      const uint64_t size = sizeof(info);
      std::memcpy(info, &size, sizeof(size));
      if (sceKernelGetModuleInfo(handle, info) != 0)
        continue;
      ModuleRange& m = s_modules[s_module_count];
      std::memcpy(m.name, info + 8, sizeof(m.name) - 1);
      m.name[sizeof(m.name) - 1] = 0;
      uint32_t segments = 0;
      std::memcpy(&segments, info + 0x148, sizeof(segments));
      m.start = UINTPTR_MAX;
      m.end = 0;
      for (uint32_t i = 0; i < segments && i < 4; i++)
      {
        uint64_t address = 0;
        uint32_t length = 0;
        std::memcpy(&address, info + 0x108 + 16 * i, sizeof(address));
        std::memcpy(&length, info + 0x108 + 16 * i + 8, sizeof(length));
        if (length && address < m.start)
          m.start = address;
        if (length && address + length > m.end)
          m.end = address + length;
      }
      bool seen = m.end <= m.start;
      for (int j = 0; j < s_module_count && !seen; j++)
        seen = s_modules[j].start == m.start;
      if (seen)
        continue;
      s_module_count++;
      for (const char* name : k_known_functions)
      {
        void* address = nullptr;
        if (s_function_count < 160 && sceKernelDlsym(handle, name, &address) == 0 && address &&
            reinterpret_cast<uintptr_t>(address) >= m.start && reinterpret_cast<uintptr_t>(address) < m.end)
        {
          bool dup = false;
          for (int j = 0; j < s_function_count && !dup; j++)
            dup = s_functions[j].address == reinterpret_cast<uintptr_t>(address);
          if (!dup)
            s_functions[s_function_count++] = {reinterpret_cast<uintptr_t>(address), name};
        }
      }
    }
  }
  std::qsort(s_functions, static_cast<size_t>(s_function_count), sizeof(s_functions[0]), CompareFunctions);
  char line[2048];
  int n = 0;
  for (int i = 0; i < s_module_count && n < static_cast<int>(sizeof(line)) - 96; i++)
    n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n), "%s%s %#lx-%#lx", i ? ", " : "",
      s_modules[i].name, static_cast<unsigned long>(s_modules[i].start), static_cast<unsigned long>(s_modules[i].end));
  pshakux_log("stall probe: %d system modules (%s); %d of their functions known by name", s_module_count,
    s_module_count ? line : "none answered", s_function_count);
}

// "eboot+0x1234 (elf 0x3000001234)" when the address is in the eboot; "libkernel.sprx+0x138c (after
// pthread_cond_wait+0x2c)" in a system module (MapSystemModules).
Out& Where(Out& o, uintptr_t address)
{
  const uintptr_t base = pshakux_eboot_start();
  o.hex(address, 16);
  if (base != 0 && address >= base && address - base < 0x40000000u)
  {
    o.s("  eboot+").hex(address - base).s(" (elf ").hex(pshakux_eboot_link_base() + (address - base)).c(')');
    return o;
  }
  for (int i = 0; i < s_module_count; i++)
  {
    const ModuleRange& m = s_modules[i];
    if (address < m.start || address >= m.end)
      continue;
    o.s("  ").s(m.name).c('+').hex(address - m.start);
    const KnownFunction* best = nullptr;
    for (int j = 0; j < s_function_count && s_functions[j].address <= address; j++)
    {
      if (s_functions[j].address >= m.start)
        best = &s_functions[j];
    }
    if (best)
      o.s(" (after ").s(best->name).c('+').hex(address - best->address).c(')');
    break;
  }
  return o;
}

void RestoreDefault(int signal)
{
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = SIG_DFL;
  (void)sigaction(signal, &sa, nullptr);
}

void FsyncLog()
{
  const int fd = pshakux_log_fd();
  if (fd >= 0)
    fsync(fd);
}

// The return addresses up a stack, by frame pointer (the title is built with them), each frame
// read through SafeRead and checked: above the last, aligned, within 16 MiB of the stack pointer.
void WalkFrames(Out& o, uintptr_t frame, uintptr_t stack, int first_depth)
{
  uintptr_t previous = stack;
  for (int depth = first_depth; depth < 40; depth++)
  {
    if (frame < previous || frame - stack > 0x1000000 || (frame & 7) != 0)
    {
      if (frame != 0)
        o.s("    (the walk ends: frame ").hex(frame).s(" is not on this stack)").end();
      break;
    }
    uintptr_t words[2];
    if (!SafeRead(frame, words, sizeof(words)))
    {
      o.s("    (the walk ends: frame ").hex(frame).s(" cannot be read)").end();
      break;
    }
    if (words[1] == 0)
      break;
    o.s("    #").udec(static_cast<uint64_t>(depth)).pad(8);
    Where(o, words[1]).end();
    previous = frame + 16;
    frame = words[0];
  }
}

void BytesAt(Out& o, const char* what, uintptr_t address)
{
  uint8_t bytes[16];
  o.s("  ").s(what).s(" ").hex(address).s(":");
  if (!SafeRead(address, bytes, sizeof(bytes)))
  {
    o.s(" (not readable)").end();
    return;
  }
  for (uint8_t b : bytes)
  {
    o.c(' ');
    o.c("0123456789abcdef"[b >> 4]).c("0123456789abcdef"[b & 15]);
  }
  o.end();
}

// OGBox 0.1.21: guest memory in lines of 32 bytes, for a crash in guest code: the stack frame
// (the locals a bad pointer came from) and the code before the faulting instruction (what made
// it). Lines that can't be read say so. Each line: "  <what> <address>: <32 bytes>".
void GuestLines(Out& o, const char* what, uint32_t from, uint32_t length)
{
  for (uint32_t at = from & ~31u; at < from + length; at += 32)
  {
    uint8_t bytes[32];
    o.s("  ").s(what).s(" ").hex(at, 8).s(":");
    if (!SafeRead(at, bytes, sizeof(bytes)))
    {
      o.s(" (not readable)").end();
      continue;
    }
    for (uint8_t b : bytes)
    {
      o.c(' ');
      o.c("0123456789abcdef"[b >> 4]).c("0123456789abcdef"[b & 15]);
    }
    o.end();
  }
}

struct Regs
{
  uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8, r9, r10, r11, r12, r13, r14, r15, rip, rflags;
  uint64_t cs, ss, ds, es, fs, gs, fsbase, gsbase, trapno, err, addr;
  bool have_bases, have_ds;
};

Regs ReadRegs(const ucontext_t* context)
{
  Regs r;
  std::memset(&r, 0, sizeof(r));
#if defined(__PROSPERO__)
  const mcontext_t& m = context->uc_mcontext;
  r.rax = m.mc_rax; r.rbx = m.mc_rbx; r.rcx = m.mc_rcx; r.rdx = m.mc_rdx;
  r.rsi = m.mc_rsi; r.rdi = m.mc_rdi; r.rbp = m.mc_rbp; r.rsp = m.mc_rsp;
  r.r8 = m.mc_r8; r.r9 = m.mc_r9; r.r10 = m.mc_r10; r.r11 = m.mc_r11;
  r.r12 = m.mc_r12; r.r13 = m.mc_r13; r.r14 = m.mc_r14; r.r15 = m.mc_r15;
  r.rip = m.mc_rip; r.rflags = m.mc_rflags;
  r.cs = m.mc_cs & 0xffff; r.ss = m.mc_ss & 0xffff; r.ds = m.mc_ds; r.es = m.mc_es; r.fs = m.mc_fs; r.gs = m.mc_gs;
  r.trapno = m.mc_trapno; r.err = m.mc_err; r.addr = m.mc_addr;
  r.have_ds = true;
  r.have_bases = (m.mc_flags & _MC_HASBASES) != 0;
  r.fsbase = m.mc_fsbase; r.gsbase = m.mc_gsbase;
#else
  const greg_t* g = context->uc_mcontext.gregs;
  r.rax = g[REG_RAX]; r.rbx = g[REG_RBX]; r.rcx = g[REG_RCX]; r.rdx = g[REG_RDX];
  r.rsi = g[REG_RSI]; r.rdi = g[REG_RDI]; r.rbp = g[REG_RBP]; r.rsp = g[REG_RSP];
  r.r8 = g[REG_R8]; r.r9 = g[REG_R9]; r.r10 = g[REG_R10]; r.r11 = g[REG_R11];
  r.r12 = g[REG_R12]; r.r13 = g[REG_R13]; r.r14 = g[REG_R14]; r.r15 = g[REG_R15];
  r.rip = g[REG_RIP]; r.rflags = g[REG_EFL];
  const uint64_t csgsfs = static_cast<uint64_t>(g[REG_CSGSFS]);
  r.cs = csgsfs & 0xffff; r.gs = (csgsfs >> 16) & 0xffff; r.fs = (csgsfs >> 32) & 0xffff; r.ss = (csgsfs >> 48) & 0xffff;
  r.trapno = g[REG_TRAPNO]; r.err = g[REG_ERR];
  r.have_ds = false; // Linux's context has no ds/es, nor the fs/gs bases
  r.have_bases = false;
#endif
  return r;
}

void Report(int signal, siginfo_t* info, void* ctx)
{
  if (nc_log_flush)
    nc_log_flush();               // 0.1.30: the core's queued lines first (native/core/log.c)
  pshakux_log_drain_crash();   // 0.0.3: the log's lines before the report's
  Out o;
  SetStamp();
  const ucontext_t* const context = static_cast<const ucontext_t*>(ctx);
  o.s("==== OGBox stopped: ").s(SignalName(signal)).s(" ====").end();
  o.s("  signal ").dec(signal).s(", code ").dec(info ? info->si_code : 0);
  if (info && info->si_code <= 0)
    o.s(" (sent by a process: pid ").dec(info->si_pid).c(')');
  o.s(", fault address ").hex(info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0, 16).end();
  const char* const name = pshakux_thread_name();
  o.s("  thread: id ").dec(pshakux_thread_id()).s(", ").s(name ? name : "(not named: a thread OGBox did not start, or one "
                                                                       "that did not name itself)")
    .s(", pthread ").hex(reinterpret_cast<uintptr_t>(reinterpret_cast<void*>(pthread_self()))).end();
  o.s("  eboot at ").hex(pshakux_eboot_start()).s(", linked at ").hex(pshakux_eboot_link_base()).s("; ")
    .s(pshakux_build_identity()).end();

  // OGBox: the core's own lines (pshakux_crash_set_note): where the guest was, for one.
  if (const pshakux_crash_note_fn note = s_note.load(std::memory_order_acquire))
  {
    char line[400];
    line[0] = '\0';
    note(line, sizeof(line));
    if (line[0])
      o.s("  core: ").s(line).end();
  }
  FsyncLog();

  if (!context)
  {
    o.s("  (no machine context)").end();
    return;
  }
  const Regs r = ReadRegs(context);
  const uint16_t guest_cs = s_guest_cs.load(std::memory_order_relaxed);
  const bool in_guest = guest_cs != 0 && r.cs == guest_cs;
  o.s("  rip ");
  Where(o, r.rip).end();
  o.s("  cs ").hex(r.cs, 2).s(" ss ").hex(r.ss, 2);
  if (r.have_ds)
    o.s(" ds ").hex(r.ds, 2).s(" es ").hex(r.es, 2);
  o.s(" fs ").hex(r.fs, 2).s(" gs ").hex(r.gs, 2).s("  rflags ").hex(r.rflags, 8).s("  trapno ").udec(r.trapno)
    .s(" err ").hex(r.err).end();
  if (r.have_bases)
    o.s("  mc_fsbase ").hex(r.fsbase, 16).s("  mc_gsbase ").hex(r.gsbase, 16).end();
  else
    o.s("  fs/gs bases: not in this context").end();
  o.s("  rax ").hex(r.rax, 16).s("  rbx ").hex(r.rbx, 16).s("  rcx ").hex(r.rcx, 16).s("  rdx ").hex(r.rdx, 16).end();
  o.s("  rsi ").hex(r.rsi, 16).s("  rdi ").hex(r.rdi, 16).s("  rbp ").hex(r.rbp, 16).s("  rsp ").hex(r.rsp, 16).end();
  o.s("  r8  ").hex(r.r8, 16).s("  r9  ").hex(r.r9, 16).s("  r10 ").hex(r.r10, 16).s("  r11 ").hex(r.r11, 16).end();
  o.s("  r12 ").hex(r.r12, 16).s("  r13 ").hex(r.r13, 16).s("  r14 ").hex(r.r14, 16).s("  r15 ").hex(r.r15, 16).end();
  FsyncLog();

  if (in_guest)
  {
    // 32-bit guest code: its registers are the low halves; its stack is the guest's.
    o.s("  in 32-bit guest code (cs ").hex(r.cs, 2).s(", the core's guest segment")
      .s("): eip ").hex(r.rip & 0xffffffffu, 8).s(" esp ").hex(r.rsp & 0xffffffffu, 8).s(" ebp ")
      .hex(r.rbp & 0xffffffffu, 8).s(" eax ").hex(r.rax & 0xffffffffu, 8).s(" ebx ").hex(r.rbx & 0xffffffffu, 8)
      .s(" ecx ").hex(r.rcx & 0xffffffffu, 8).end();
    BytesAt(o, "bytes at eip", r.rip & 0xffffffffu);
    uint32_t words[8];
    o.s("  guest stack at ").hex(r.rsp & 0xffffffffu, 8).s(":");
    if (SafeRead(r.rsp & 0xffffffffu, words, sizeof(words)))
      for (uint32_t w : words)
        o.c(' ').hex(w, 8);
    else
      o.s(" (not readable)");
    o.end();
    FsyncLog();
    // the rest of the frame: esp up to ebp + 0x100 when ebp is a frame above esp (at most 1 KB),
    // else 512 bytes from esp; then the code from 0x180 before eip to 0x40 after it
    const uint32_t esp = static_cast<uint32_t>(r.rsp), ebp = static_cast<uint32_t>(r.rbp);
    const uint32_t eip = static_cast<uint32_t>(r.rip);
    uint32_t stack_len = 512;
    if (ebp > esp && ebp - esp < 0x300)
      stack_len = ebp - esp + 0x100;
    GuestLines(o, "guest stack", esp, stack_len);
    FsyncLog();
    GuestLines(o, "guest code", eip - 0x180, 0x1c0);
    FsyncLog();
  }
  else
  {
    o.s("  in 64-bit host code").end();
    BytesAt(o, "bytes at rip", r.rip);
    o.s("  stack:").end();
    WalkFrames(o, r.rbp, r.rsp, 0);
  }
}

void CrashSignalHandler(int signal, siginfo_t* info, void* ctx)
{
  // A second fault (in another thread, or while this report walks a broken stack): the console
  // takes it from here.
  if (s_in_crash)
  {
    RestoreDefault(signal);
    return;
  }
  s_in_crash = 1;

  Report(signal, info, ctx);
  Out o;
  o.s("==== end of the report ====").end();
  FsyncLog();

  // Back to the console: returning re-executes the faulting instruction with the default action
  // in place, and the console closes the title with its own error screen. A signal that was sent
  // (abort, an int3, SIGTERM) is not re-raised by returning: it is sent again, and is delivered
  // with the default action once this handler returns.
  RestoreDefault(signal);
  const bool fault = info && info->si_code > 0 && signal != SIGABRT && signal != SIGTRAP &&
                     signal != SIGTERM && signal != SIGXCPU && signal != SIGXFSZ && signal != SIGHUP;
  if (!fault)
    (void)pthread_kill(pthread_self(), signal);
}

void InstallFor(int signal)
{
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sa.sa_sigaction = &CrashSignalHandler;
  (void)sigaction(signal, &sa, nullptr);
}

// std::terminate (an exception nothing caught, a noexcept broken): what and where, then abort,
// whose SIGABRT is not reported a second time.
[[noreturn]] void OnTerminate()
{
  bool expected = false;
  if (s_terminating.compare_exchange_strong(expected, true))
  {
    const char* what = "no exception: std::terminate was called directly";
    const char* type = "";
    if (const std::exception_ptr current = std::current_exception())
    {
      what = "an exception that is not a std::exception";
      try
      {
        std::rethrow_exception(current);
      }
      catch (const std::exception& e)
      {
        what = e.what();
        type = typeid(e).name();
      }
      catch (...)
      {
      }
    }
    pshakux_log("==== OGBox stopped: std::terminate: %s%s%s ====", what, type[0] ? " (type " : "", type[0] ? type : "");
    if (type[0])
      pshakux_log("  (type names are mangled: c++filt reads them)");
    pshakux_log_drain_crash();
    Out o;
    SetStamp();
    const char* const name = pshakux_thread_name();
    o.s("  thread: id ").dec(pshakux_thread_id()).s(", ").s(name ? name : "(not named)").end();
    o.s("  stack:").end();
    const uintptr_t frame = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    WalkFrames(o, frame, frame, 0);
    o.s("==== end of the report ====").end();
    FsyncLog();
  }
  RestoreDefault(SIGABRT);
  std::abort();
}

// A thread's own stack for the signal handler, freed with the thread.
struct CrashStack
{
  static constexpr size_t SIZE = 128 * 1024;
  void* memory = nullptr;
  ~CrashStack()
  {
    if (memory)
    {
      stack_t none;
      std::memset(&none, 0, sizeof(none));
      none.ss_flags = SS_DISABLE;
      (void)AltStack(&none, nullptr);
      std::free(memory);
    }
  }
};

} // namespace

extern "C" uintptr_t pshakux_eboot_start(void)
{
#if defined(__PROSPERO__)
  return reinterpret_cast<uintptr_t>(&__dso_handle[0]);
#else
  return reinterpret_cast<uintptr_t>(&__executable_start[0]);
#endif
}

extern "C" uintptr_t pshakux_eboot_link_base(void)
{
#if defined(__PROSPERO__)
  return 0x3000000000ull; // ps5/ld/ps5-pie.ld (a PSHAKUX_EBOOT_BASE=low build is linked at 0)
#else
  return 0; // the host's position-independent executable
#endif
}

extern "C" void pshakux_crash_set_note(pshakux_crash_note_fn note)
{
  s_note.store(note, std::memory_order_release);
}

// The native CPU's signal stack (hakuX-native's cpu_native.c, weak import): the sigaltstack found
// at run time, lent.
extern "C" int pshakux_sigaltstack(const stack_t* ss, stack_t* old)
{
  return AltStack(ss, old);
}

extern "C" void pshakux_crash_set_guest_cs(uint16_t cs)
{
  s_guest_cs.store(cs, std::memory_order_relaxed);
}

extern "C" void pshakux_install_crash_stack(void)
{
  static thread_local CrashStack stack;
  if (stack.memory)
    return;
  void* const memory = std::malloc(CrashStack::SIZE);
  if (!memory)
    return;
  std::memset(memory, 0, CrashStack::SIZE);  // touched now: the kernel cannot fault pages in while it writes a signal frame
  stack_t ss;
  std::memset(&ss, 0, sizeof(ss));
  ss.ss_sp = static_cast<char*>(memory);
  ss.ss_size = CrashStack::SIZE;
  if (AltStack(&ss, nullptr) != 0)
  {
    std::free(memory);
    return;
  }
  stack.memory = memory;
}

extern "C" void pshakux_crash_install(void)
{
  if (pipe(s_pipe) == 0)
  {
    for (const int fd : s_pipe)
      (void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  }
  pshakux_install_crash_stack();
  for (const int signal : FATAL_SIGNALS)
    InstallFor(signal);
  for (const int signal : ENDING_SIGNALS)
    InstallFor(signal);
  std::set_terminate(&OnTerminate);
  pshakux_log("crash report: installed for SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS, SIGTERM, SIGXCPU, "
             "SIGXFSZ, SIGHUP and std::terminate (the core's handlers pass on what they do not own); SIGKILL cannot "
             "be caught: a title the system kills ends with its last \"alive:\" line%s",
             s_pipe[0] >= 0 ? "" : "; no pipe: memory is not read in the report");
}

// ---- OGBox 0.1.28: the stall probe ------------------------------------------------------------
// When a game stops giving frames (the front end asks, pshakux_stall_report), where the emulated
// CPU's thread is: a SIGPROF to it (nothing else on the console uses it; hakuX's own sampler, the
// kick signal, is blocked inside its signal handlers), whose handler takes the interrupted
// context's PC and, in host code, the return addresses up its frames (the title is built with
// frame pointers), through SafeRead. A thread that does not answer within 100 ms is not running
// at all (not scheduled, or stopped in the kernel with SIGPROF blocked). The front end logs the
// answer; the addresses are looked up in build/ps5/link/llvm-pie.elf of the same build.

namespace {
struct StallSample
{
  uintptr_t rip, rsp, rbp;
  uint16_t cs;
  int frames;
  uintptr_t frame[12];
  int found;                // OGBox 0.1.30: the stack's return addresses into the title (StackScan)
  uintptr_t stack[16];
  // OGBox 0.1.36: the code before the PC and before the first return address (a system call's
  // number is in the bytes before it), and the signal the thread was inside of when its frames
  // end in the system's signal trampoline (FreeBSD's sigcode: "call *(%rsp)", the sigframe at
  // the return address's slot + 8: the handler, the interrupted context, its siginfo).
  uint64_t rax, rdi, rsi, rdx, r10;
  uint8_t code_pc[32], code_ret[32];
  bool have_code_pc, have_code_ret;
  int signo;                // 0: the frames do not end in the trampoline
  uintptr_t sig_handler;
  Regs sig;
  int sig_frames;
  uintptr_t sig_frame[12];
  int sig_found;
  uintptr_t sig_stack[16];
};
StallSample s_stall;
std::atomic<uint32_t> s_stall_seq{0};
std::atomic<pthread_t> s_stall_thread{};
std::atomic<bool> s_stall_ready{false};

/* Whether the bytes before address end with a call (E8 rel32, or FF /2 through a register or
 * memory): a return address, not a stray value of the title's range. */
bool AfterCall(uintptr_t address)
{
  uint8_t b[8];
  if (!SafeRead(address - 8, b, sizeof(b)))
    return false;
  return b[3] == 0xE8 ||                                          // call rel32
         (b[6] == 0xFF && ((b[7] & 0xF8) == 0xD0 || (b[7] & 0xF8) == 0x10)) ||   // call reg / [reg]
         (b[5] >= 0x40 && b[5] <= 0x4F && b[6] == 0xFF && (b[7] & 0xF8) == 0xD0) ||  // call r8-r15
         (b[5] == 0xFF && (b[6] & 0xF8) == 0x50) ||               // call [reg + disp8]
         (b[2] == 0xFF && (b[3] == 0x15 || (b[3] & 0xF8) == 0x90));  // call [rip + disp32], [reg + disp32]
}

/* OGBox 0.1.30: the title's return addresses on the stack from rsp up (4 KiB), whatever rbp
 * holds (0.1.31: 32 KiB): inside the system library, or code entered from a signal or the CPU gate, the rbp chain
 * is not the title's (0.1.29's Def Jam stalls: "at 0x80000138c from 0x80000c6fa" and nothing of
 * the title's). */
int StackScanInto(uintptr_t rsp, uintptr_t* out)
{
  const uintptr_t base = pshakux_eboot_start();
  int n = 0;
  for (uintptr_t at = rsp & ~uintptr_t(7); base != 0 && n < 16 && at < rsp + 32768; at += 256)
  {
    uintptr_t words[32];
    if (!SafeRead(at, words, sizeof(words)))
      break;
    for (int i = 0; i < 32 && n < 16; i++)
    {
      const uintptr_t v = words[i];
      // 0.1.36: the whole eboot (about 60 MiB; up to 0.1.35 only its first 32 MiB, which left out
      // the emulator's own code)
      if (v > base + 8 && v - base < 0x8000000u && AfterCall(v))
        out[n++] = v;
    }
  }
  return n;
}

void StackScan(uintptr_t rsp)
{
  s_stall.found = StackScanInto(rsp, s_stall.stack);
}

/* The return addresses up an rbp chain (host code). */
int WalkFrames(uintptr_t rbp, uintptr_t rsp, uintptr_t* out, uintptr_t* slots, int max)
{
  int n = 0;
  uintptr_t frame = rbp, previous = rsp;
  while (n < max && frame >= previous && frame - rsp < 0x1000000 && (frame & 7) == 0)
  {
    uintptr_t words[2];
    if (!SafeRead(frame, words, sizeof(words)) || words[1] == 0)
      break;
    out[n] = words[1];
    if (slots)
      slots[n] = frame + 8;
    n++;
    previous = frame + 16;
    frame = words[0];
  }
  return n;
}

bool InModule(uintptr_t address)
{
  const uintptr_t base = pshakux_eboot_start();
  if (base != 0 && address >= base && address - base < 0x40000000u)
    return true;
  for (int i = 0; i < s_module_count; i++)
    if (address >= s_modules[i].start && address < s_modules[i].end)
      return true;
  return false;
}

void StallHandler(int, siginfo_t*, void* context)
{
  const int saved = errno;
  const Regs r = ReadRegs(static_cast<const ucontext_t*>(context));
  s_stall.rip = r.rip;
  s_stall.rsp = r.rsp;
  s_stall.rbp = r.rbp;
  s_stall.cs = static_cast<uint16_t>(r.cs);
  int n = 0;
  uintptr_t slots[12] = {};
  s_stall.rax = r.rax, s_stall.rdi = r.rdi, s_stall.rsi = r.rsi, s_stall.rdx = r.rdx, s_stall.r10 = r.r10;
  s_stall.signo = 0;
  s_stall.have_code_pc = s_stall.have_code_ret = false;
  if (r.cs != s_guest_cs.load(std::memory_order_relaxed))
  {
    n = WalkFrames(r.rbp, r.rsp, s_stall.frame, slots, 12);
    s_stall.have_code_pc = SafeRead(r.rip - 24, s_stall.code_pc, sizeof(s_stall.code_pc));
    if (n > 0)
      s_stall.have_code_ret = SafeRead(s_stall.frame[0] - 24, s_stall.code_ret, sizeof(s_stall.code_ret));
    // the frames end in the signal trampoline: the signal, and where it came in
    for (int i = 0; i < n; i++)
    {
      uint8_t call[3];
      if (InModule(s_stall.frame[i]) || !SafeRead(s_stall.frame[i] - 3, call, 3) || call[0] != 0xFF ||
          call[1] != 0x14 || call[2] != 0x24)
        continue;
      const uintptr_t sigframe = slots[i] + 8;
      ucontext_t uc;
      siginfo_t si;
      uintptr_t handler = 0;
      if (SafeRead(sigframe, &handler, sizeof(handler)) && SafeRead(sigframe + 16, &uc, sizeof(uc)) &&
          SafeRead(sigframe + 16 + sizeof(ucontext_t), &si, sizeof(si)))
      {
        s_stall.signo = si.si_signo ? si.si_signo : -1;
        s_stall.sig_handler = handler;
        s_stall.sig = ReadRegs(&uc);
        s_stall.sig_frames = 0;
        s_stall.sig_found = 0;
        if (s_stall.sig.cs != s_guest_cs.load(std::memory_order_relaxed))
        {
          s_stall.sig_frames = WalkFrames(s_stall.sig.rbp, s_stall.sig.rsp, s_stall.sig_frame, nullptr, 12);
          s_stall.sig_found = StackScanInto(s_stall.sig.rsp, s_stall.sig_stack);
        }
      }
      break;
    }
  }
  s_stall.frames = n;
  s_stall.found = 0;
  if (r.cs != s_guest_cs.load(std::memory_order_relaxed))
    StackScan(r.rsp);
  s_stall_seq.fetch_add(1, std::memory_order_release);
  errno = saved;
}
} // namespace

extern "C" void pshakux_stall_set_thread(void)
{
  MapSystemModules();
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = StallHandler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGPROF, &sa, nullptr) != 0)
    return;
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGPROF);
  pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
  s_stall_thread.store(pthread_self(), std::memory_order_release);
  s_stall_ready.store(true, std::memory_order_release);
}

extern "C" void pshakux_stall_report(double seconds)
{
  Out o;
  SetStamp();
  o.s("stall: ").udec(static_cast<uint64_t>(seconds * 1000.0)).s(" ms without a new frame: ");
  if (!s_stall_ready.load(std::memory_order_acquire))
  {
    o.s("(no probe: the emulated CPU's thread has not started)").end();
    return;
  }
  const uint32_t seq = s_stall_seq.load(std::memory_order_acquire);
  if (pthread_kill(s_stall_thread.load(std::memory_order_acquire), SIGPROF) != 0)
  {
    o.s("(the signal could not be sent)").end();
    return;
  }
  bool answered = false;
  for (int i = 0; i < 1000 && !answered; i++)
  {
    answered = s_stall_seq.load(std::memory_order_acquire) != seq;
    if (!answered)
    {
      const struct timespec ts = {0, 100000};
      nanosleep(&ts, nullptr);
    }
  }
  if (!answered)
  {
    o.s("the emulated CPU's thread did not run within 100 ms (not scheduled, or in the kernel with "
        "SIGPROF blocked)").end();
    return;
  }
  if (s_stall.cs == s_guest_cs.load(std::memory_order_relaxed))
  {
    o.s("the emulated CPU in the game's code, eip ").hex(s_stall.rip & 0xffffffffu, 8).s(" esp ")
      .hex(s_stall.rsp & 0xffffffffu, 8).end();
    return;
  }
  o.s("the emulated CPU's thread in host code at ");
  Where(o, s_stall.rip).end();
  for (int i = 0; i < s_stall.frames; i++)
  {
    o.s("    from ");
    Where(o, s_stall.frame[i]).end();
  }
  for (int i = 0; i < s_stall.found; i++)
  {
    o.s("    on the stack ");
    Where(o, s_stall.stack[i]).end();
  }
  // 0.1.36: the registers (a system call's arguments) and the code before the PC and the first
  // return address
  o.s("    rax ").hex(s_stall.rax).s(" rdi ").hex(s_stall.rdi).s(" rsi ").hex(s_stall.rsi).s(" rdx ")
    .hex(s_stall.rdx).s(" r10 ").hex(s_stall.r10).end();
  if (s_stall.have_code_pc)
  {
    o.s("    code before the PC, from -0x18:");
    for (unsigned i = 0; i < sizeof(s_stall.code_pc); i++)
      o.c(' ').hex(s_stall.code_pc[i], 2);
    o.end();
  }
  if (s_stall.have_code_ret)
  {
    o.s("    code before the first return address, from -0x18:");
    for (unsigned i = 0; i < sizeof(s_stall.code_ret); i++)
      o.c(' ').hex(s_stall.code_ret[i], 2);
    o.end();
  }
  if (s_stall.signo)
  {
    const Regs& g = s_stall.sig;
    o.s("    inside the delivery of signal ").udec(static_cast<uint64_t>(s_stall.signo > 0 ? s_stall.signo : 0))
      .s(" (handler ");
    Where(o, s_stall.sig_handler).s("), which came in ");
    if (g.cs == s_guest_cs.load(std::memory_order_relaxed))
    {
      o.s("in the game's code, eip ").hex(g.rip & 0xffffffffu, 8).s(" esp ").hex(g.rsp & 0xffffffffu, 8).end();
    }
    else
    {
      o.s("in host code at ");
      Where(o, g.rip).end();
      for (int i = 0; i < s_stall.sig_frames; i++)
      {
        o.s("      from ");
        Where(o, s_stall.sig_frame[i]).end();
      }
      for (int i = 0; i < s_stall.sig_found; i++)
      {
        o.s("      on the stack ");
        Where(o, s_stall.sig_stack[i]).end();
      }
    }
  }
}
