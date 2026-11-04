/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The Core (ogbox_core.h) on hakuX's native core: the sequence hakuX's own front ends use
 * (native/app/app_main.c, native/run/xboxrun.c), for the console.
 *
 *   configure  HAKUX_HLE_MODE=replace (the HLE GPU as the app runs it), the shader and pipeline
 *              caches in data_root/hle (HAKUX_NATIVE_BASE), hakuX's options (nc_options_init,
 *              nc_option, nc_options_apply), the hard disk (nc_io_set_hdd_root, Partition0-5
 *              made), the disc image opened, and the fault handler: SIGSEGV and SIGBUS go to the
 *              core first (nc_core_fault: device registers, dirty tracking, the CPU backend's
 *              own), and what it does not take to the handler before it (the title's crash
 *              report, ps5/src/platform/ps5_crash.cpp).
 *   start      the vCPU thread, 16 MiB of stack as app_main.c gives it, at the emulation's
 *              priority (700): nc_core_boot, then nc_core_run until the guest stops.
 *
 * The crash report gets the core's line (the state and the guest's EIP when the fault was in its
 * code), and the boot log's heartbeat the status line.
 */
#include "core/ogbox_core.h"
#include "core/ogbox_mods.h"
#include "hle/present_host.h"
#include "platform/ps5_bootlog.h"
#include "platform/ps5_priority.h"
#include "platform/ps5_takeover.h"

#include "native/state/save.h"
#include "native/core/core.h"
#include "native/core/irq.h"
#include "native/core/options.h"
#include "native/core/trainer.h"
#include "native/dev/usb.h"
#include "native/hle/hle_native.h"
#include "hw/xbox/hle/pb/pbdraw.h"
#include "native/kernel/kernel.h"
#if defined(NC_CPU_NATIVE)
#include "native/cpu/vmhost.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static NcCore g_core;
int ps5vfs_is_virtual(const char *path);
int ps5vfs_disc_open(const char *path);

static int g_disc_fd = -1;
static int g_state = PSHX_CORE_IDLE;
static bool g_configured, g_widescreen;
static char g_message[384];
static char g_image[1024];
static struct sigaction g_old_segv, g_old_bus;

static void set_state(int state)
{
    __atomic_store_n(&g_state, state, __ATOMIC_RELEASE);
}

int pshx_core_state(void)
{
    return __atomic_load_n(&g_state, __ATOMIC_ACQUIRE);
}

bool pshx_core_relaunch_wanted(void)
{
    return pshx_core_state() == PSHX_CORE_STOPPED && g_core.kernel.relaunch;
}

const char *pshx_core_message(void)
{
    const char *fatal = nc_hle_fatal();
    if (fatal && fatal[0]) {
        return fatal;
    }
    return g_message;
}

const char *pshx_core_name(void)
{
    return "hakuX (native core, HLE GPU)";
}

/* ---- faults: the core's first ---------------------------------------------------------------- */

static void on_fault(int sig, siginfo_t *info, void *ctx)
{
    if (nc_core_fault(&g_core, sig, info, ctx)) {
        return;
    }
    const struct sigaction *old = sig == SIGBUS ? &g_old_bus : &g_old_segv;
    if (old->sa_flags & SA_SIGINFO) {
        if (old->sa_sigaction) {
            old->sa_sigaction(sig, info, ctx);
            return;
        }
    } else if (old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN && old->sa_handler) {
        old->sa_handler(sig);
        return;
    }
    /* none before: the default action, and the fault comes again */
    signal(sig, SIG_DFL);
}

static void install_fault_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_old_segv);
    sigaction(SIGBUS, &sa, &g_old_bus);
}

/* The crash report's line (a signal handler: no locks, no allocation). */
static const char *state_name(int state)
{
    switch (state) {
    case PSHX_CORE_IDLE: return "idle";
    case PSHX_CORE_BOOTING: return "booting";
    case PSHX_CORE_RUNNING: return "running";
    case PSHX_CORE_STOPPED: return "stopped";
    case PSHX_CORE_FAILED: return "failed";
    }
    return "?";
}

static void crash_note(char *line, unsigned size)
{
    snprintf(line, size, "hakuX %s, CPU backend %s", state_name(pshx_core_state()),
             g_core.cpu ? "up" : "none");
}

void pshx_core_status(char *line, unsigned size)
{
    uint64_t presented = 0, taken = 0;
    pshx_present_counts(&presented, &taken);
    uint32_t w = 0, h = 0;
    pshx_present_size(&w, &h);
    snprintf(line, size, "core=%s frames=%llu shown=%llu display=%ux%u%s%s", state_name(pshx_core_state()),
             (unsigned long long)presented, (unsigned long long)taken, w, h,
             nc_paused() ? " paused" : "", pshx_core_state() == PSHX_CORE_FAILED ? " (see the lines above)" : "");
}

/* 0.1.4: libc.prx's old entries in guest RAM (physical page 0, under the takeover): put back when
 * the guest or a device wrote over one, counted for the heartbeat. */
static unsigned g_stubs_put_back;
static uint32_t g_stub_first;
/* 0.1.44: a heartbeat every 0.25 s until then (the run's first 25 s), then every 2 s again */
static double g_fast_until = -1.0;

static void heartbeat_fn(char *line, unsigned size);
static void heartbeat(char *line, unsigned size)
{
    heartbeat_fn(line, size);
    if (g_fast_until > 0.0 && pshakux_uptime() > g_fast_until) {
        g_fast_until = 0.0;
        pshakux_diag_set_status(heartbeat, 0);
    }
}

static void heartbeat_fn(char *line, unsigned size)
{
    pshx_core_status(line, size);
    {
        char mem[160];
        pshakux_memory_note(mem, sizeof(mem));
        const size_t used = strlen(line);
        snprintf(line + used, size > used ? size - used : 0, "; memory: %s", mem);
    }
#if defined(NC_CPU_NATIVE)
    /* 0.1.5: the core watches the page and puts the entries back within milliseconds (vmhost.c);
     * the heartbeat says how often, and how many writes the watch caught */
    if (nc_vmhost_window_low_active()) {
        nc_vmhost_check_window_low(NULL);
        uint32_t first = 0;
        unsigned host = 0, guest = 0;
        g_stubs_put_back = nc_vmhost_window_low_put_back(&first, &host, &guest);
        g_stub_first = first;
        if (g_stubs_put_back || host || guest) {
            const size_t used = strlen(line);
            snprintf(line + used, size > used ? size - used : 0,
                     " libc.prx-stubs-put-back=%u (first +0x%x; writes caught: host %u, guest %u; page %s)",
                     g_stubs_put_back, (unsigned)g_stub_first, host, guest,
                     nc_vmhost_window_low_watched() ? "watched" : "not watched now");
        }
        /* 0.1.44: which of the old entries the host still calls, and how often */
        uint32_t offs[NC_VMHOST_LOW_MAX], calls[NC_VMHOST_LOW_MAX];
        const unsigned n = nc_vmhost_window_low_calls(offs, calls, NC_VMHOST_LOW_MAX);
        if (n) {
            size_t used = strlen(line);
            snprintf(line + used, size > used ? size - used : 0, "; libc.prx's old entries called:");
            unsigned any = 0;
            for (unsigned i = 0; i < n; i++) {
                if (calls[i]) {
                    used = strlen(line);
                    snprintf(line + used, size > used ? size - used : 0, " +0x%x %u", (unsigned)offs[i],
                             (unsigned)calls[i]);
                    any++;
                }
            }
            if (!any) {
                used = strlen(line);
                snprintf(line + used, size > used ? size - used : 0, " none");
            }
        }
    }
#endif
}

#if defined(NC_CPU_NATIVE)
/* The code at libc.prx's old entries under the takeover: xor eax,eax; ret (the same bytes in
 * 64-bit and 32-bit code: libkernel calls them on host threads, in 64-bit mode). */
static const uint8_t k_libc_stub[] = {0x31, 0xC0, 0xC3};

/* The title is ending (the shell exit, a relaunch): the old entries once more, and DT_FINI's
 * (+0xc8010, inside guest RAM: whatever the game put there), through the host's view of RAM, in
 * case anything on the way out runs the module's finalizer. */
static void before_exit(const char *why)
{
    uint8_t *phys = __atomic_load_n(&g_core.vm.phys, __ATOMIC_ACQUIRE);
    if (!phys) {
        pshakux_log("takeover: %s: the guest's window was never mapped: libc.prx is still there", why);
        return;
    }
    memcpy(phys + PSHAKUX_LIBC_DT_FINI, k_libc_stub, sizeof(k_libc_stub));
    uint32_t first = 0;
    const unsigned n = nc_vmhost_check_window_low(&first);
    nc_vmhost_low_restore();   /* 0.1.49: also when the guest had the page (Manhunt) */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    pshakux_log("takeover: %s: libc.prx's DT_FINI (0x%08x) and %u entries made stubs again (%u had changed)", why,
                0x80000000u + PSHAKUX_LIBC_DT_FINI, pshakux_libc_entry_count, n);
}
#endif

/* 0.1.4: the guest's window over 0x80000000 when the takeover was done (the kernel image at
 * 0x80010000, as on the Xbox), else the 0.1.3 layout. Before the core's start (nc_vm_init). */
static void choose_window(void)
{
#if defined(NC_CPU_NATIVE)
    if (!pshakux_takeover_done()) {
        if (!nc_window_hole_end) {
            /* built with no hole (PSHAKUX_WINDOW_HOLE_END=0): without the takeover the window's map
             * would replace libc.prx with its users still pointing there */
            nc_vmhost_set_window_hole(0x80120000u);
        }
        pshakux_log("core: the window keeps its hole: 0x80000000-0x%08x stays libc.prx's (the takeover was not done: "
                    "%s); the guest's kernel area at 0x%08x, and a game that reads the kernel's header at 0x80010000 "
                    "(titles built with the XDK) is ended by the console there (SYSTEM_XO_VIOLATION in klog, no crash "
                    "report)",
                    (unsigned)nc_window_hole_end, pshakux_takeover_reason(), (unsigned)NC_KERNEL_VA);
        return;
    }
    uint32_t offsets[NC_VMHOST_LOW_MAX];
    unsigned n = 0;
    for (unsigned i = 0; i < pshakux_libc_entry_count && n < NC_VMHOST_LOW_MAX; i++) {
        offsets[n++] = pshakux_libc_entries[i];
    }
    if (!nc_vmhost_window_low(offsets, n, k_libc_stub, sizeof(k_libc_stub))) {
        pshakux_log("core: the stubs for libc.prx's old entries could not be set: the window keeps its hole "
                    "(0x80000000-0x%08x)", (unsigned)nc_window_hole_end);
        return;
    }
    nc_vmhost_set_window_hole(0);
    pshakux_takeover_set_exit_hook(before_exit);
    pshakux_log("core: no hole: the guest's window will be mapped over 0x80000000 (libc.prx's range, taken over), "
                "the kernel's area at 0x%08x and its KPCR at 0x%08x as on the Xbox; stubs (xor eax,eax; ret) at "
                "libc.prx's %u old entries in guest RAM's page 0 (the thread destructors' 0x80000200 among them)",
                (unsigned)NC_KERNEL_VA, (unsigned)NC_KERNEL_VA + 0x10000u, n);
#else
    pshakux_log("core: no native CPU: the window's layout is the stub's");
#endif
}

/* ---- configure and start --------------------------------------------------------------------- */

static void make_folder(const char *path)
{
    if (mkdir(path, 0777) == 0) {
        chmod(path, 0777);
    }
}

static void make_hdd(const char *root)
{
    char p[512];
    make_folder(root);
    for (int i = 0; i <= 5; i++) {
        snprintf(p, sizeof(p), "%s/Partition%d", root, i);
        make_folder(p);
    }
}

bool pshx_core_configure(const PshxCoreConfig *config)
{
    pshakux_crash_set_note(crash_note);
    g_fast_until = pshakux_uptime() + 25.0;
    pshakux_diag_set_status(heartbeat, 1);
    if (g_configured) {
        snprintf(g_message, sizeof(g_message), "The core is configured once a run.");
        return false;
    }
    g_configured = true;

    setenv("HAKUX_HLE_MODE", "replace", 0);
    if (config->data_root && config->data_root[0]) {
        char base[512];
        make_folder(config->data_root);
        snprintf(base, sizeof(base), "%s/hle", config->data_root);
        make_folder(base);
        setenv("HAKUX_NATIVE_BASE", base, 1);
    }

    NcOptions opt;
    nc_options_init(&opt);
    for (int i = 0; i < config->argc; i++) {
        const char *arg = config->argv[i];
        if (!arg || !arg[0]) {
            continue;
        }
        const char *eq = strchr(arg, '=');
        if (eq && !strncmp(arg, "NC_", 3)) {
            /* 0.1.11: a switch like NC_SWEEP_RDTSC=0 written without "env " is
             * one too (0.1.10 left it out: the A/B run tested nothing) */
            char name[64];
            const size_t n = (size_t)(eq - arg) < sizeof(name) - 1 ? (size_t)(eq - arg) : sizeof(name) - 1;
            memcpy(name, arg, n);
            name[n] = 0;
            setenv(name, eq + 1, 1);
            pshakux_log("core: switch %s=%s", name, eq + 1);
            continue;
        }
        if (nc_option(&opt, arg)) {
            pshakux_log("core: option %s", arg);
        } else {
            pshakux_log("core: option %s is not one of hakuX's (native/core/options.h): left out", arg);
        }
    }
    nc_options_apply(&opt);
    g_widescreen = opt.video >= 0 && (opt.video & NC_VIDEO_WIDESCREEN);

    if (config->hdd_root && config->hdd_root[0]) {
        make_hdd(config->hdd_root);
        nc_io_set_hdd_root(config->hdd_root);
    }
    pshakux_log("core: hard disk %s, caches %s", nc_io_hdd_root(), getenv("HAKUX_NATIVE_BASE"));
    choose_window();

    snprintf(g_image, sizeof(g_image), "%s", config->image ? config->image : "");
    if (!g_image[0]) {
        snprintf(g_message, sizeof(g_message), "No disc image was given (boot.txt's first line).");
        set_state(PSHX_CORE_FAILED);
        return false;
    }
    if (strcmp(g_image, "none") != 0) {
        const size_t n = strlen(g_image);
        if (n > 4 && !strcasecmp(g_image + n - 4, ".xbe")) {
            snprintf(g_message, sizeof(g_message),
                     "%s is an XBE: hakuX boots the default.xbe of an Xbox disc image (an XISO).", g_image);
            set_state(PSHX_CORE_FAILED);
            return false;
        }
        /* 0.1.14: an image on a network share (/smb/<share>/...) through the virtual file
         * layer: a descriptor whose pread and fstat it answers (ps5vfs_disc_open) */
        g_disc_fd = ps5vfs_is_virtual(g_image) ? ps5vfs_disc_open(g_image) : open(g_image, O_RDONLY | O_CLOEXEC);
        if (g_disc_fd < 0) {
            snprintf(g_message, sizeof(g_message), "The disc image %s cannot be opened (%s).", g_image,
                     strerror(errno));
            set_state(PSHX_CORE_FAILED);
            return false;
        }
        pshakux_log("core: disc image %s", g_image);
        /* 0.1.19: hakuX's mods and cheats for this game (its mods/ folder, ogbox_mods.h) */
        ogbox_mods_prepare(g_disc_fd, config->data_root && config->data_root[0] ? config->data_root : "/data/ogbox");
    } else {
        pshakux_log("core: no disc: the dashboard from the hard disk");
    }
    install_fault_handler();
    return true;
}

static void *vcpu_thread(void *arg)
{
    (void)arg;
    pshakux_thread_named("hakux-vcpu");
    pshakux_install_crash_stack();
    pthread_set_name_np(pthread_self(), "hakux-vcpu");
    pshakux_priority_set(PSHAKUX_ROLE_VCPU);
    /* the crash report shows the guest's registers when the fault is in
     * compat-mode code (the native CPU's 32-bit segment) */
    pshakux_crash_set_guest_cs(0x33);
    pshakux_stall_set_thread();   /* OGBox 0.1.28: where it is when a game stops giving frames */
    pshakux_log("core: vCPU thread: booting (guest memory, the kernel, the devices, the XBE, the CPU)");
    if (!nc_core_boot(&g_core, g_disc_fd)) {
        if (!nc_hle_fatal()) {
            nc_hle_set_fatal(NC_FATAL_BOOT, "The game stopped while starting.");
        }
        pshakux_log("core: boot failed: %s", pshx_core_message());
        set_state(PSHX_CORE_FAILED);
        pshakux_thread_gone();
        return NULL;
    }
    pshakux_log("core: booted; entering the guest (native CPU, cs 0x33)");
    set_state(PSHX_CORE_RUNNING);
    nc_core_run(&g_core);
    pshakux_log("core: back from the guest");
    const NcKernel *k = &g_core.kernel;
    snprintf(g_message, sizeof(g_message), "%s",
             k->quit ? "The game left (to the dashboard, a reboot or a shutdown)."
             : k->relaunch ? "The game started another XBE: the title starts again with it."
             : k->stop ? "The game stopped (see ogbox-boot.log)." : "The game ended.");
    pshakux_log("core: the run ended: %s", g_message);
    set_state(k->stop && !k->quit && !k->relaunch ? PSHX_CORE_FAILED : PSHX_CORE_STOPPED);
    pshakux_thread_gone();
    return NULL;
}

/* 0.1.5: where the guest's time goes, every 10 s in the boot log: 400 samples of the vCPU's EIP
 * over a second (hakuX's sampler: the native CPU's kick signal reads it), the share halted (every
 * guest thread waiting), in kernel calls or host code, and the busiest guest EIPs with the code
 * at the first. A game that runs on without progressing (Burnout 3 at its loading screen with
 * 0.1.4: the vCPU never idle) shows the loop it spins in. PSHAKUX_GUEST_SAMPLER=0 (an env line in
 * boot.txt) turns it off. */
extern bool (*g_hle_pc_sampler)(uint32_t *pc);
/* hakuX (native/hle/glue.c): the registers of the sample just taken, guest code only */
extern bool nc_hle_sample_regs(uint32_t regs[9]);
/* hakuX (hw/xbox/hle/pb/pbmode.c): what the push-buffer decoder knows of a physical address */
extern int hle_pb_describe_pa(uint32_t pa, char *buf, size_t len);

/* 0.1.7: a register's value as a guest address, for the sampler's line: the dword there, its
 * physical address and what the GPU's decoder knows of it ("" when it isn't a mapped address). */
static void describe_reg(const char *name, uint32_t v, char *out, size_t len)
{
    out[0] = 0;
    if (v < 0x10000u || !g_core.vm.pte || nc_vm_phys(&g_core.vm, v & ~3u) == UINT32_MAX ||
        nc_vm_phys(&g_core.vm, (v & ~3u) + 3) == UINT32_MAX) {
        return;
    }
    const uint32_t pa = nc_vm_phys(&g_core.vm, v);
    char what[200];
    hle_pb_describe_pa(pa, what, sizeof(what));
    snprintf(out, len, " [%s]=%08x (pa %08x%s%s)", name, nc_vm_rd32(&g_core.vm, v & ~3u), pa,
             v >= 0x80000000u ? ", the contiguous window" : "", what);
}
#define SAMPLER_EIP_HOST 0xF0000000u
#define SAMPLER_EIP_SYSOP 0x08000000u
#define SAMPLER_EIP_IDLE 0xFFFFFFFFu

/* 0.0.2: PSHAKUX_PEEK (an env line in boot.txt): guest memory the sampler logs with its line, for a
 * loop whose state lives a few pointers away. Chains separated by ';', each an address and offsets,
 * ':' between: "0x01dbf89c:0:0:4:0:0x1c" reads [0x01dbf89c+0], then [that+0], [that+4], [that+0],
 * [that+0x1c], each step logged, and 8 dwords at the last address reached. */
static bool vm_read32(uint32_t va, uint32_t *v)
{
    if (!g_core.vm.pte || nc_vm_phys(&g_core.vm, va & ~3u) == UINT32_MAX ||
        nc_vm_phys(&g_core.vm, (va & ~3u) + 3) == UINT32_MAX) {
        return false;
    }
    *v = nc_vm_rd32(&g_core.vm, va);
    return true;
}

static void log_peeks(void)
{
    static char spec[512];
    static int have = -1;
    if (have < 0) {
        const char *e = getenv("PSHAKUX_PEEK");
        have = e && e[0];
        if (have) {
            snprintf(spec, sizeof(spec), "%s", e);
        }
    }
    if (!have) {
        return;
    }
    char work[512];
    snprintf(work, sizeof(work), "%s", spec);
    for (char *save1, *chain = strtok_r(work, ";", &save1); chain; chain = strtok_r(NULL, ";", &save1)) {
        char line[700];
        int n = snprintf(line, sizeof(line), "guest: peek %s:", chain);
        char *save2;
        char *tok = strtok_r(chain, ":", &save2);
        uint32_t at = tok ? (uint32_t)strtoul(tok, NULL, 0) : 0;
        bool ok = tok != NULL;
        for (tok = strtok_r(NULL, ":", &save2); ok && tok && n < (int)sizeof(line) - 40;
             tok = strtok_r(NULL, ":", &save2)) {
            const uint32_t off = (uint32_t)strtoul(tok, NULL, 0);
            uint32_t v;
            ok = vm_read32(at + off, &v);
            n += ok ? snprintf(line + n, sizeof(line) - (size_t)n, " [%08x]=%08x", at + off, v)
                    : snprintf(line + n, sizeof(line) - (size_t)n, " [%08x] unmapped", at + off);
            at = v;
        }
        if (ok) {
            n += snprintf(line + n, sizeof(line) - (size_t)n, " | at %08x:", at);
            for (unsigned i = 0; i < 8 && n < (int)sizeof(line) - 12; i++) {
                uint32_t v;
                n += vm_read32(at + 4 * i, &v) ? snprintf(line + n, sizeof(line) - (size_t)n, " %08x", v)
                                               : snprintf(line + n, sizeof(line) - (size_t)n, " --------");
            }
        }
        pshakux_log("%s", line);
    }
}

static void *sampler_thread(void *arg)
{
    (void)arg;
    pshakux_thread_named("guest-sampler");
    pshakux_priority_set(PSHAKUX_ROLE_BACKGROUND);
    enum { SAMPLES = 400, SLOTS = 64 };
    for (;;) {
        sleep(9);
        const int state = pshx_core_state();
        if (state == PSHX_CORE_STOPPED || state == PSHX_CORE_FAILED) {
            break;
        }
        if (state != PSHX_CORE_RUNNING || !g_hle_pc_sampler || nc_paused()) {
            continue;
        }
        uint32_t eip[SLOTS] = {0}, cnt[SLOTS] = {0};
        uint32_t regs[SLOTS][9];
        bool have_regs[SLOTS] = {false};
        unsigned taken = 0, idle = 0, kcall = 0, host = 0;
        /* 0.0.2: the guest code's samples by the guest thread running (hakuX's g_nc_current_tid), each
         * with its busiest EIP: a busy thread that waits for another shows what the other does */
        enum { TIDS = 16, TEIPS = 8 };
        struct {
            uint32_t tid, n, eip[TEIPS], cnt[TEIPS];
        } th[TIDS];
        unsigned nth = 0;
        for (unsigned i = 0; i < SAMPLES; i++) {
            uint32_t pc = 0;
            const struct timespec ts = {0, 2500000};
            nanosleep(&ts, NULL);
            if (!g_hle_pc_sampler(&pc)) {
                continue;
            }
            taken++;
            if (pc == SAMPLER_EIP_IDLE) {
                idle++;
                continue;
            }
            if ((pc & SAMPLER_EIP_HOST) == SAMPLER_EIP_HOST) {
                if (pc & SAMPLER_EIP_SYSOP) {
                    kcall++;
                } else {
                    host++;
                }
                continue;
            }
            {
                const uint32_t tid = __atomic_load_n(&g_nc_current_tid, __ATOMIC_RELAXED);
                unsigned t = 0;
                while (t < nth && th[t].tid != tid) {
                    t++;
                }
                if (t == nth && nth < TIDS) {
                    memset(&th[nth], 0, sizeof(th[nth]));
                    th[nth++].tid = tid;
                }
                if (t < nth) {
                    th[t].n++;
                    for (unsigned e = 0; e < TEIPS; e++) {
                        if (!th[t].cnt[e] || th[t].eip[e] == pc) {
                            th[t].eip[e] = pc;
                            th[t].cnt[e]++;
                            break;
                        }
                    }
                }
            }
            unsigned k = (pc * 2654435761u) >> 26 & (SLOTS - 1);
            for (unsigned j = 0; j < SLOTS; j++, k = (k + 1) & (SLOTS - 1)) {
                if (!cnt[k] || eip[k] == pc) {
                    eip[k] = pc;
                    cnt[k]++;
                    have_regs[k] = nc_hle_sample_regs(regs[k]) || have_regs[k];
                    break;
                }
            }
        }
        if (!taken) {
            continue;
        }
        char top[400];
        int n = 0;
        top[0] = 0;
        uint32_t first = 0;
        int first_slot = -1;
        for (unsigned r = 0; r < 8; r++) {
            unsigned best = SLOTS;
            for (unsigned k = 0; k < SLOTS; k++) {
                if (cnt[k] && (best == SLOTS || cnt[k] > cnt[best])) {
                    best = k;
                }
            }
            if (best == SLOTS) {
                break;
            }
            if (!r) {
                first = eip[best];
                first_slot = have_regs[best] ? (int)best : -1;
            }
            n += snprintf(top + n, sizeof(top) - (size_t)n, " %08x %u%%", eip[best], cnt[best] * 100u / taken);
            cnt[best] = 0;
        }
        char code[3 * 24 + 1];
        code[0] = 0;
        if (first && g_core.vm.pte && nc_vm_phys(&g_core.vm, first - 8) != UINT32_MAX &&
            nc_vm_phys(&g_core.vm, first + 15) != UINT32_MAX) {
            int c = 0;
            for (unsigned i = 0; i < 24; i++) {
                c += snprintf(code + c, sizeof(code) - (size_t)c, "%02x ", *(const uint8_t *)nc_vm_host(&g_core.vm, first - 8 + i));
            }
        }
        pshakux_log("guest: %u samples in 1 s: halted %u%%, kernel calls %u%%, host %u%%; busiest guest EIPs:%s%s%s",
                    taken, idle * 100u / taken, kcall * 100u / taken, host * 100u / taken, top[0] ? top : " none",
                    code[0] ? " | code at the first -8: " : "", code);
        if (nth > 1) {
            char by[400];
            int b = 0;
            by[0] = 0;
            for (unsigned r = 0; r < 6 && b < (int)sizeof(by) - 48; r++) {
                unsigned best = nth;
                for (unsigned t = 0; t < nth; t++) {
                    if (th[t].n && (best == nth || th[t].n > th[best].n)) {
                        best = t;
                    }
                }
                if (best == nth) {
                    break;
                }
                unsigned e1 = 0;
                for (unsigned e = 1; e < TEIPS; e++) {
                    e1 = th[best].cnt[e] > th[best].cnt[e1] ? e : e1;
                }
                b += snprintf(by + b, sizeof(by) - (size_t)b, "%s t%u %u%% (most at %08x)", r ? "," : "", th[best].tid,
                              th[best].n * 100u / taken, th[best].eip[e1]);
                th[best].n = 0;
            }
            pshakux_log("guest: the guest code's samples by thread:%s", by);
        }
        log_peeks();
        if (first_slot >= 0 && cnt[first_slot] == 0) {
            /* the busiest EIP's registers (its last sample) and what they point at: a loop that
             * waits for a value shows the value, where it lives, and (10 s later) whether it moved */
            const uint32_t *r = regs[first_slot];
            static const char *const nm[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
            char derefs[1200];
            int d = 0;
            derefs[0] = 0;
            for (unsigned i = 0; i < 8 && d < (int)sizeof(derefs) - 260; i++) {
                if (i == 4) {
                    continue;
                }
                char one[256];
                describe_reg(nm[i], r[i], one, sizeof(one));
                d += snprintf(derefs + d, sizeof(derefs) - (size_t)d, "%s", one);
            }
            pshakux_log("guest: at %08x: eax %08x ecx %08x edx %08x ebx %08x esp %08x ebp %08x esi %08x edi %08x "
                        "eflags %08x |%s", first, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8],
                        derefs[0] ? derefs : " no register points at guest memory");
        }
    }
    pshakux_thread_gone();
    return NULL;
}

static void start_sampler(void)
{
    const char *e = getenv("PSHAKUX_GUEST_SAMPLER");
    if (e && e[0] == '0') {
        pshakux_log("guest: sampler off (PSHAKUX_GUEST_SAMPLER=0)");
        return;
    }
    pthread_t t;
    if (pthread_create(&t, NULL, sampler_thread, NULL) == 0) {
        pthread_detach(t);
    }
}

bool pshx_core_start(void)
{
    if (!g_configured || pshx_core_state() != PSHX_CORE_IDLE) {
        return false;
    }
    set_state(PSHX_CORE_BOOTING);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);   /* as app_main.c's core thread */
    pthread_t t;
    const int r = pthread_create(&t, &attr, vcpu_thread, NULL);
    pthread_attr_destroy(&attr);
    if (r != 0) {
        snprintf(g_message, sizeof(g_message), "The emulation thread could not be started (%d).", r);
        set_state(PSHX_CORE_FAILED);
        return false;
    }
    pthread_detach(t);
    start_sampler();
    return true;
}

/* ---- input, frames, pause -------------------------------------------------------------------- */

void pshx_core_set_pad(unsigned player, const PshxPad *pad)
{
    if (player >= 4) {
        return;
    }
    NcPadState s;
    memset(&s, 0, sizeof(s));
    s.buttons = pad->buttons;
    memcpy(s.analog, pad->analog, sizeof(s.analog));
    s.lx = pad->lx;
    s.ly = pad->ly;
    s.rx = pad->rx;
    s.ry = pad->ry;
    nc_pad_set(player, &s);
}

void pshx_core_set_players(unsigned mask)
{
    nc_dev_ohci_set_pads(mask & 0xF);
}

void pshx_core_rumble(unsigned player, float *large, float *small)
{
    uint16_t l = 0, r = 0;
    if (player < 4) {
        nc_pad_rumble(player, &l, &r);
    }
    *large = l / 65535.0f;
    *small = r / 65535.0f;
}

bool pshx_core_take_frame(uint64_t *seen, void *dst, uint32_t pitch, uint32_t max_w, uint32_t max_h, uint32_t *w,
                          uint32_t *h)
{
    return pshx_present_take(seen, dst, pitch, max_w, max_h, w, h);
}

void pshx_core_frame_size(uint32_t *w, uint32_t *h)
{
    pshx_present_size(w, h);
}

bool pshx_core_widescreen(void)
{
    return g_widescreen;
}

void pshx_core_pause(bool on)
{
    nc_pause_set(on);
}

void pshx_core_drawlog(int n, unsigned delay_ms)
{
    hle_pbd_drawlog(n, delay_ms);
}

bool pshx_core_save_state(const char *path, char *why, unsigned why_size)
{
    if (pshx_core_state() != PSHX_CORE_RUNNING) {
        snprintf(why, why_size, "the game isn't running");
        return false;
    }
    const double t0 = pshakux_uptime();
    const char *err = nc_state_save(path);
    /* a game writing a save of its own: again in a moment (hakuX's own answer) */
    for (int tries = 0; err && strstr(err, "try again") && tries < 30; tries++) {
        usleep(100000);
        err = nc_state_save(path);
    }
    pshakux_log("states: %s %s in %.0f ms%s%s", path, err ? "NOT saved" : "saved", (pshakux_uptime() - t0) * 1000.0,
                err ? ": " : "", err ? err : "");
    if (err) {
        snprintf(why, why_size, "%s", err);
        return false;
    }
    return true;
}

int hle_vk_exit_idle(void); /* hw/xbox/hle/vk/display.c */

void pshx_core_quiesce(void)
{
    if (pshx_core_state() == PSHX_CORE_IDLE) {
        return;
    }
    nc_pause_set(true);
    pshakux_log("core: paused for the exit");
    /* the guest's CPU parks at its next kernel entry; the decoder and the render thread finish
     * what they have */
    usleep(150000);
    const int r = hle_vk_exit_idle();
    pshakux_log("core: the emulated GPU's work done (%d)", r);
}

bool pshx_core_trainer_message(int *kind, unsigned *led, char *text, unsigned text_size)
{
    NcTrainerMessage m;
    if (!nc_trainer_take_message(&m)) {
        return false;
    }
    *kind = m.kind;
    *led = m.led;
    if (text && text_size) {
        snprintf(text, text_size, "%s", m.text);
    }
    return true;
}
