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
// The eboot's first byte: the linker script makes __dso_handle the start of .text (ps5/ld/).
extern "C" char __dso_handle[] __attribute__((weak, visibility("hidden")));
#else
// The program's first byte (the host's linker defines it).
extern "C" const char __executable_start[] __attribute__((weak));
#endif

namespace {

// PSHakuX: sigaltstack is found while the title runs, not imported. Measured on the console
// (2026-11-01): it is not among the exports a title is given, so an import of it could be null
// (a call through address 0); found or not, the title starts, and without it the crash report
// still runs, on the faulting thread's own stack (a stack overflow then goes unreported).
int AltStack(const stack_t* ss, stack_t* old)
{
#if defined(__PROSPERO__)
  using Fn = int (*)(const stack_t*, stack_t*);
  static const Fn fn = reinterpret_cast<Fn>(PS5SystemModules::FindFunction("libkernel", "sigaltstack", "sHziAegVp74"));
  if (!fn)
  {
    errno = ENOSYS;
    return -1;
  }
  return fn(ss, old);
#else
  return sigaltstack(ss, old);
#endif
}

volatile sig_atomic_t s_in_crash = 0;
// PSHakuX: the core's part of a report (pshakux_crash_set_note) and its guest code segment.
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
// it leaves pshakux_log's lines: it goes to klog here), "[PSHakuX] text\n" to klog.
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
    const char* prefix = "[PSHakuX] ";
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

// "eboot+0x1234 (elf 0x3000001234)" when the address is in the eboot.
Out& Where(Out& o, uintptr_t address)
{
  const uintptr_t base = pshakux_eboot_start();
  o.hex(address, 16);
  if (base != 0 && address >= base && address - base < 0x40000000u)
  {
    o.s("  eboot+").hex(address - base).s(" (elf ").hex(pshakux_eboot_link_base() + (address - base)).c(')');
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
  Out o;
  SetStamp();
  const ucontext_t* const context = static_cast<const ucontext_t*>(ctx);
  o.s("==== PSHakuX stopped: ").s(SignalName(signal)).s(" ====").end();
  o.s("  signal ").dec(signal).s(", code ").dec(info ? info->si_code : 0);
  if (info && info->si_code <= 0)
    o.s(" (sent by a process: pid ").dec(info->si_pid).c(')');
  o.s(", fault address ").hex(info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0, 16).end();
  const char* const name = pshakux_thread_name();
  o.s("  thread: id ").dec(pshakux_thread_id()).s(", ").s(name ? name : "(not named: a thread PSHakuX did not start, or one "
                                                                       "that did not name itself)")
    .s(", pthread ").hex(reinterpret_cast<uintptr_t>(reinterpret_cast<void*>(pthread_self()))).end();
  o.s("  eboot at ").hex(pshakux_eboot_start()).s(", linked at ").hex(pshakux_eboot_link_base()).s("; ")
    .s(pshakux_build_identity()).end();

  // PSHakuX: the core's own lines (pshakux_crash_set_note): where the guest was, for one.
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
    pshakux_log("==== PSHakuX stopped: std::terminate: %s%s%s ====", what, type[0] ? " (type " : "", type[0] ? type : "");
    if (type[0])
      pshakux_log("  (type names are mangled: c++filt reads them)");
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
