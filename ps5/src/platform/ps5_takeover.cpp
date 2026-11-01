// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.4 - the takeover of 0x80000000-0x80200000 from sce_module/libc.prx (ps5_takeover.h).
// Ported from PSHakuX System Check 0.0.5's src/cpu/hxtakeover.inc (Press5elect), where it was proven
// on the console: the scan, the shadow of libc.prx's data, libkernel's registrations made again, the
// rewrite, then (here: in hakuX-native's vmhost, when the core starts) the map over 0x80000000. The
// check's own tests after the map (code at 0x80001000, counted stray calls) are not here; the core
// writes plain stubs at the old entries instead (src/core/hakux_core.c).
//
// What libc.prx is (PS5_Vulkan's tooling/native/libc_builder.cpp, generated, nothing copied from
// the console). Its mapped image is 0x118000 bytes:
//
//   +0x000000-0x0cc000  text, execute-only. Code only at:
//     +0x10     DT_INIT: calls the preinit array's entry (+0x100)
//     +0x30     the export +F+9hhi6k9Q: xor eax,eax; ret
//     +0x40     the export sjpkrhugvVI: xor eax,eax; ret
//     +0x50     every other exported function: pxor xmm0,xmm0; xor eax,eax; ret
//     +0x100    the initializer: registers +0x200/+0x210/+0x220 with libkernel
//               (_sceKernelSetThreadDtors, ...AtexitCount, ...AtexitReport) and its heap API table
//               (+0x110100) with _sceKernelRtldSetApplicationHeapAPI
//     +0x200    thread destructors: ret          (libkernel calls it at every thread exit)
//     +0x210    thread atexit count: xor eax,eax; ret
//     +0x220    thread atexit report: ret
//     +0x230    relative anchor: ret
//     +0xc8010  DT_FINI: ret
//   +0x0cc000-0x10c000  read-only data
//   +0x10c000-0x110000  RELRO (anchor slots, GLOB_DAT/TLS slots, the GOT, the preinit array,
//                       module parameters, the TLS initialisation image)
//   +0x110000-0x118000  data: +0x110100 the heap API table, then the exported data objects
//
// On the console (system check 0.0.5): the four registration functions were not found by name
// (fine: the rewrite covers what the scan sees), and after the map the only callers into the old
// module were thread exits calling +0x200 (4 after a few threads, 101 by the end of the run).

#include "ps5_takeover.h"

#include "ps5_bootlog.h"
#include "ps5_modules.h"

#include <cerrno>
#include <cstdarg>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

extern "C" {
int sceKernelGetModuleList(int32_t* handles, size_t capacity, size_t* count);
int sceKernelGetModuleInfo(int32_t handle, void* info);
int sceKernelVirtualQuery(const void* address, int flags, void* info, size_t size);
}

const unsigned pshakux_libc_entries[] = {0x10, 0x30, 0x40, 0x50, 0x100, 0x200, 0x210, 0x220, 0x230};
const unsigned pshakux_libc_entry_count = sizeof(pshakux_libc_entries) / sizeof(pshakux_libc_entries[0]);

namespace
{

constexpr uint64_t kHoleLo = 0x80000000ull;
constexpr uint64_t kTakeHi = 0x80200000ull;
constexpr uint64_t kLibcImage = 0x118000ull;
constexpr uint64_t kLibcDataLo = 0xcc000ull;
constexpr uint64_t kShadowBytes = kLibcImage - kLibcDataLo; // 0x4c000

int g_done;
char g_reason[160] = "not tried yet";
void (*g_exit_hook)(const char*);

void SetReason(const char* format, ...) __attribute__((format(printf, 1, 2)));
void SetReason(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(g_reason, sizeof(g_reason), format, args);
	va_end(args);
}

// The eboot's equivalent of every code entry of libc.prx
extern "C" __attribute__((naked, used)) void pshakux_libc_stub(void)
{
	__asm__ volatile("pxor %xmm0, %xmm0\n\txor %eax, %eax\n\tret");
}

alignas(0x4000) uint8_t g_shadow[kShadowBytes];

struct LibcEntry
{
	uint32_t offset;
	const char* what;
};
const LibcEntry kEntries[] = {
	{0x10, "DT_INIT"},
	{0x30, "export +F+9hhi6k9Q"},
	{0x40, "export sjpkrhugvVI"},
	{0x50, "an exported function (all but two)"},
	{0x100, "initializer (preinit array)"},
	{0x200, "thread destructors"},
	{0x210, "thread atexit count"},
	{0x220, "thread atexit report"},
	{0x230, "relative anchor"},
	{0xc8010, "DT_FINI"},
};

// What a value in the hole is, and where it can point instead (0: left)
enum
{
	kCode,
	kData,
	kBook,
	kUnknown,
	kPast
};
const char* const kClassNames[] = {"code entry", "data", "bookkeeping", "UNKNOWN CODE", "past libc.prx"};

int Classify(uint64_t v, uint64_t* to, const char** what)
{
	const uint64_t off = v - kHoleLo;
	*to = 0;
	*what = "";
	for (const LibcEntry& e : kEntries)
		if (off == e.offset)
		{
			*what = e.what;
			*to = reinterpret_cast<uint64_t>(&pshakux_libc_stub);
			return kCode;
		}
	if (off >= kLibcDataLo && off < kLibcImage)
	{
		*what = off == 0xcc000 ? "Need_sceLibc marker" : off < 0x104164 ? "read-only data" :
			off < 0x10c000 ? "unwind header" : off < 0x10f800 ? "relative anchor slots" :
			off < 0x10f870 ? "GLOB_DAT/TLS slots" : off < 0x10fce0 ? "GOT" : off < 0x10fce8 ? "preinit array" :
			off < 0x10fd20 ? "module parameters" : off < 0x110000 ? "TLS image" :
			off < 0x110148 && off >= 0x110100 ? "heap API table" : "data";
		*to = reinterpret_cast<uint64_t>(g_shadow) + (off - kLibcDataLo);
		return kData;
	}
	if (off == 0 || off == kLibcImage)
	{
		*what = off == 0 ? "the module's base" : "the module's end";
		return kBook;
	}
	if (off < kLibcDataLo)
		return kUnknown;
	return kPast;
}

// ---- the modules ----
struct ModSeg
{
	uint64_t address;
	uint32_t size;
	int32_t prot;
	char name[40];
};
ModSeg g_modsegs[256];
int g_nmodsegs;
ModSeg g_libc_segs[4];
int g_libc_nsegs;
uint64_t g_libc_at;
bool g_foreign; // a module other than the eboot and libc.prx in 0x80000000-0x80200000
char g_foreign_name[64];

bool ReadModules()
{
	int32_t handles[256];
	size_t count = 0;
	const int rc = sceKernelGetModuleList(handles, 256, &count);
	pshakux_log("takeover: module list: 0x%x, %zu modules", static_cast<unsigned>(rc), count);
	if (rc != 0 || count == 0)
		return false;
	g_nmodsegs = g_libc_nsegs = 0;
	g_libc_at = 0;
	g_foreign = false;
	for (size_t i = 0; i < count && i < 256; i++)
	{
		alignas(8) uint8_t info[0x160];
		std::memset(info, 0, sizeof(info));
		const uint64_t size = sizeof(info);
		std::memcpy(info, &size, 8);
		if (sceKernelGetModuleInfo(handles[i], info) != 0)
			continue;
		char name[64];
		std::memcpy(name, info + 8, 63);
		name[63] = 0;
		uint32_t nsegs;
		std::memcpy(&nsegs, info + 0x148, 4);
		const bool libc = std::strcmp(name, "libc.prx") == 0;
		for (uint32_t s = 0; s < nsegs && s < 4; s++)
		{
			uint64_t addr;
			uint32_t sz;
			int32_t prot;
			std::memcpy(&addr, info + 0x108 + s * 16, 8);
			std::memcpy(&sz, info + 0x110 + s * 16, 4);
			std::memcpy(&prot, info + 0x114 + s * 16, 4);
			pshakux_log("takeover:   \"%s\" segment 0x%llx+0x%x p%x", name, static_cast<unsigned long long>(addr), sz,
				prot);
			if (libc)
			{
				if (s == 0)
					g_libc_at = addr;
				if (g_libc_nsegs < 4)
					g_libc_segs[g_libc_nsegs++] = ModSeg{addr, sz, prot, "libc.prx"};
			}
			else if (addr < kTakeHi && addr + sz > kHoleLo && !std::strstr(name, "eboot") && !g_foreign)
			{
				g_foreign = true;
				std::snprintf(g_foreign_name, sizeof(g_foreign_name), "%s", name);
			}
			if (g_nmodsegs < 256)
			{
				ModSeg& m = g_modsegs[g_nmodsegs++];
				m.address = addr;
				m.size = sz;
				m.prot = prot;
				std::snprintf(m.name, sizeof(m.name), "%s", name);
			}
		}
	}
	return true;
}

// ---- the mappings ----
struct Region
{
	uint64_t start, end;
	int prot;  // 1 read, 2 write, 4 execute
	int flags; // 1 flexible, 2 direct, 4 stack
	char name[40];
};
Region g_regions[2048];

int ReadRegions()
{
	int n = 0;
	uint64_t q = 0;
	while (n < static_cast<int>(sizeof(g_regions) / sizeof(g_regions[0])))
	{
		struct
		{
			uint64_t start, end, offset;
			int32_t protection, memory_type;
			uint32_t bits;
			char name[32];
			uint32_t pad;
		} info;
		std::memset(&info, 0, sizeof(info));
		if (sceKernelVirtualQuery(reinterpret_cast<void*>(q), 1, &info, sizeof(info)) != 0 || info.end <= q)
			break;
		Region& g = g_regions[n++];
		g.start = info.start;
		g.end = info.end;
		g.prot = info.protection & 7;
		g.flags = (info.bits & 1 ? 1 : 0) | (info.bits & 2 ? 2 : 0) | (info.bits & 4 ? 4 : 0);
		info.name[31] = 0;
		std::snprintf(g.name, sizeof(g.name), "%s", info.name);
		q = info.end;
	}
	return n;
}

const char* Place(uint64_t a, const Region* g)
{
	for (int k = 0; k < g_nmodsegs; k++)
		if (a >= g_modsegs[k].address && a < g_modsegs[k].address + g_modsegs[k].size)
			return g_modsegs[k].name;
	if (g && (g->flags & 4))
		return "a stack";
	return g && g->name[0] ? g->name : "anonymous";
}

// ---- the eboot's imports (ogbox-imports.txt, written by tools/link-title.sh) ----
struct Import
{
	uint64_t slot;
	char type[20];
	char name[64];
};
Import* g_imports;
int g_nimports;
uint64_t g_got_lo, g_got_hi;

void ReadImports()
{
	char path[512], line[256];
	std::snprintf(path, sizeof(path), "%s/ogbox-imports.txt", pshakux_title_dir());
	FILE* f = std::fopen(path, "r");
	if (!f)
	{
		pshakux_log("takeover: eboot imports: no %s (tools/link-title.sh writes it): its GOT is left to the scan", path);
		return;
	}
	g_imports = static_cast<Import*>(std::calloc(4096, sizeof(Import)));
	g_nimports = 0;
	uint64_t text = 0;
	while (g_imports && std::fgets(line, sizeof(line), f) && g_nimports < 4096)
	{
		unsigned long long a;
		if (std::sscanf(line, "text %llx", &a) == 1)
		{
			text = a;
			continue;
		}
		Import& im = g_imports[g_nimports];
		if (std::sscanf(line, "%llx %19s %63s", &a, im.type, im.name) == 3)
		{
			im.slot = a;
			g_nimports++;
		}
	}
	std::fclose(f);
	// The eboot is loaded at its link address when linked high, else where the loader put it
	const uint64_t start = pshakux_eboot_start();
	const uint64_t bias = text >= 0x100000000ull ? 0 : start - text;
	g_got_lo = ~0ull;
	g_got_hi = 0;
	for (int i = 0; i < g_nimports; i++)
	{
		g_imports[i].slot += bias;
		if (g_imports[i].slot < g_got_lo)
			g_got_lo = g_imports[i].slot;
		if (g_imports[i].slot + 8 > g_got_hi)
			g_got_hi = g_imports[i].slot + 8;
	}
	pshakux_log("takeover: eboot imports: %d slots (link text 0x%llx, load bias 0x%llx), 0x%llx-0x%llx", g_nimports,
		static_cast<unsigned long long>(text), static_cast<unsigned long long>(bias),
		static_cast<unsigned long long>(g_got_lo), static_cast<unsigned long long>(g_got_hi));
}

const Import* ImportAt(uint64_t slot)
{
	for (int i = 0; i < g_nimports; i++)
		if (g_imports[i].slot == slot)
			return &g_imports[i];
	return nullptr;
}

// ---- the scan, under a guard: a mapping whose read faults ends only its own scan ----
struct Hit
{
	uint64_t where, value;
	int region, cls, rewritable;
};
Hit g_hits[1024];
int g_nhits;
uint64_t g_my_stack;

sigjmp_buf g_escape;
volatile sig_atomic_t g_guarding;
pthread_t g_guard_thread;
struct sigaction g_old_segv, g_old_bus;

void OnFault(int sig, siginfo_t* info, void* context)
{
	if (g_guarding && pthread_equal(pthread_self(), g_guard_thread))
		siglongjmp(g_escape, 1);
	// not the scan's: on to the handler before (the crash report)
	const struct sigaction& old = sig == SIGBUS ? g_old_bus : g_old_segv;
	if (old.sa_flags & SA_SIGINFO)
	{
		if (old.sa_sigaction)
			old.sa_sigaction(sig, info, context);
	}
	else if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN && old.sa_handler)
	{
		old.sa_handler(sig);
	}
	else
	{
		signal(sig, SIG_DFL);
		raise(sig);
	}
}

void GuardOn()
{
	struct sigaction sa;
	std::memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sa.sa_sigaction = &OnFault;
	g_guard_thread = pthread_self();
	sigaction(SIGSEGV, &sa, &g_old_segv);
	sigaction(SIGBUS, &sa, &g_old_bus);
}

void GuardOff()
{
	g_guarding = 0;
	sigaction(SIGSEGV, &g_old_segv, nullptr);
	sigaction(SIGBUS, &g_old_bus, nullptr);
}

bool MayRewrite(uint64_t where, const Region* g)
{
	if (g->flags & 4)
		return false; // a stack: a thread's own locals
	bool in_eboot = false;
	for (int k = 0; k < g_nmodsegs; k++)
		if (std::strstr(g_modsegs[k].name, "eboot") && where >= g_modsegs[k].address &&
			where < g_modsegs[k].address + g_modsegs[k].size)
			in_eboot = true;
	if (where >= reinterpret_cast<uint64_t>(g_shadow) && where < reinterpret_cast<uint64_t>(g_shadow) + kShadowBytes)
		return true;
	if (in_eboot)
		return where >= g_got_lo && where < g_got_hi; // our GOT, not our own variables
	return true;
}

volatile uint64_t g_scan_done; // bytes of the current mapping read so far

void ScanRegion(int index)
{
	const Region* g = &g_regions[index];
	const volatile uint64_t* w = reinterpret_cast<const volatile uint64_t*>(g->start);
	const uint64_t words = (g->end - g->start) / 8;
	for (uint64_t k = 0; k < words; k++)
	{
		g_scan_done = k * 8;
		const uint64_t v = w[k];
		if (v < kHoleLo || v >= kTakeHi)
			continue;
		const uint64_t where = g->start + k * 8;
		if (where >= reinterpret_cast<uint64_t>(g_hits) && where < reinterpret_cast<uint64_t>(g_hits + 1024))
			continue; // the scan's own records
		if (g_nhits < 1024)
		{
			Hit& h = g_hits[g_nhits++];
			uint64_t to;
			const char* what;
			h.where = where;
			h.value = v;
			h.region = index;
			h.cls = Classify(v, &to, &what);
			h.rewritable = MayRewrite(where, g);
		}
	}
	g_scan_done = words * 8;
}

void Scan(const char* when)
{
	const int n = ReadRegions();
	uint64_t scanned = 0, skipped = 0;
	int regions = 0, skipped_regions = 0, faulted = 0;
	g_nhits = 0;
	GuardOn();
	for (int i = 0; i < n; i++)
	{
		const Region* g = &g_regions[i];
		const uint64_t bytes = g->end - g->start;
		if (!(g->prot & 1) || (g->prot & 4) || (g->start < kTakeHi && g->end > kHoleLo))
			continue;
		// direct memory with no name (the GPU's buffers, arenas), and very large mappings
		if (((g->flags & 2) && !(g->flags & 1) && !g->name[0]) || bytes > 0x20000000ull)
		{
			skipped += bytes;
			skipped_regions++;
			continue;
		}
		regions++;
		g_scan_done = 0;
		if (sigsetjmp(g_escape, 1) == 0)
		{
			g_guarding = 1;
			ScanRegion(i);
			g_guarding = 0;
		}
		else
		{
			g_guarding = 0;
			faulted++;
			pshakux_log("takeover:   mapping 0x%llx-0x%llx p%x f%x \"%s\": the read faulted at +0x%llx; the rest skipped",
				static_cast<unsigned long long>(g->start), static_cast<unsigned long long>(g->end), g->prot, g->flags,
				g->name, static_cast<unsigned long long>(g_scan_done));
		}
		scanned += g_scan_done;
	}
	GuardOff();
	pshakux_log("takeover: scan %s: %d mappings, %llu KiB (%d faulted); skipped %d (%llu KiB: unnamed direct memory or "
				"over 512 MiB); %d values in 0x80000000-0x80200000",
		when, regions, static_cast<unsigned long long>(scanned >> 10), faulted, skipped_regions,
		static_cast<unsigned long long>(skipped >> 10), g_nhits);
}

void ListHits(int* counts)
{
	std::memset(counts, 0, 5 * sizeof(*counts));
	for (int i = 0; i < g_nhits; i++)
	{
		const Hit& h = g_hits[i];
		const Region* g = &g_regions[h.region];
		uint64_t to;
		const char* what;
		Classify(h.value, &to, &what);
		counts[h.cls]++;
		const Import* im = ImportAt(h.where);
		if (i < 160)
			pshakux_log("takeover:   0x%llx = 0x%llx: libc.prx +0x%llx, %s%s%s; in %s [0x%llx-0x%llx p%x f%x]%s%s%s",
				static_cast<unsigned long long>(h.where), static_cast<unsigned long long>(h.value),
				static_cast<unsigned long long>(h.value - kHoleLo), kClassNames[h.cls], what[0] ? ": " : "", what,
				Place(h.where, g), static_cast<unsigned long long>(g->start), static_cast<unsigned long long>(g->end),
				g->prot, g->flags, im ? " import " : "", im ? im->name : "", h.rewritable ? "" : " (not ours to rewrite)");
	}
	pshakux_log("takeover:   by class: %d code entries, %d data, %d bookkeeping, %d UNKNOWN CODE, %d past libc.prx",
		counts[0], counts[1], counts[2], counts[3], counts[4]);
}

int Rewrite()
{
	int done = 0, failed = 0;
	for (int i = 0; i < g_nhits; i++)
	{
		Hit& h = g_hits[i];
		const Region* g = &g_regions[h.region];
		uint64_t to;
		const char* what;
		if (!h.rewritable || Classify(h.value, &to, &what) > kData || !to)
			continue;
		volatile uint64_t* p = reinterpret_cast<volatile uint64_t*>(h.where);
		const uint64_t page = h.where & ~0x3fffull;
		bool made_writable = false;
		if (!(g->prot & 2))
		{
			pshakux_log("takeover:   0x%llx is read-only (p%x): its page made writable for it",
				static_cast<unsigned long long>(h.where), g->prot);
			errno = 0;
			if (mprotect(reinterpret_cast<void*>(page), 0x4000, PROT_READ | PROT_WRITE) != 0)
			{
				pshakux_log("takeover:   mprotect: errno %d: 0x%llx left", errno, static_cast<unsigned long long>(h.where));
				failed++;
				continue;
			}
			made_writable = true;
		}
		if (*p == h.value)
			*p = to;
		const bool ok = *p == to;
		if (made_writable)
			mprotect(reinterpret_cast<void*>(page), 0x4000, PROT_READ);
		if (i < 160 || !ok)
			pshakux_log("takeover:   0x%llx: 0x%llx -> 0x%llx (%s)%s", static_cast<unsigned long long>(h.where),
				static_cast<unsigned long long>(h.value), static_cast<unsigned long long>(to), what, ok ? "" : " FAILED");
		ok ? done++ : failed++;
	}
	pshakux_log("takeover: rewritten %d, failed %d", done, failed);
	return failed;
}

bool FillShadow()
{
	uint64_t lo = ~0ull, hi = 0;
	for (int s = 0; s < g_libc_nsegs; s++)
	{
		const ModSeg& m = g_libc_segs[s];
		if (!(m.prot & 1) || m.address < kHoleLo + kLibcDataLo)
			continue;
		lo = m.address < lo ? m.address : lo;
		hi = m.address + m.size > hi ? m.address + m.size : hi;
	}
	if (lo != kHoleLo + kLibcDataLo || hi != kHoleLo + kLibcImage)
	{
		pshakux_log("takeover: libc.prx's readable segments are 0x%llx-0x%llx, not 0x800cc000-0x80118000: no shadow",
			static_cast<unsigned long long>(lo), static_cast<unsigned long long>(hi));
		return false;
	}
	pshakux_log("takeover: copying libc.prx +0xcc000-0x118000 (its readable data) to the shadow at %p",
		static_cast<void*>(g_shadow));
	std::memcpy(g_shadow, reinterpret_cast<const void*>(lo), kShadowBytes);
	int inner = 0, unknown = 0;
	for (uint64_t k = 0; k < kShadowBytes / 8; k++)
	{
		uint64_t* w = reinterpret_cast<uint64_t*>(g_shadow + k * 8);
		if (*w < kHoleLo || *w >= kTakeHi)
			continue;
		uint64_t to;
		const char* what;
		const int c = Classify(*w, &to, &what);
		if (c <= kData && to)
		{
			*w = to;
			inner++;
		}
		else if (c == kUnknown)
		{
			unknown++;
		}
	}
	pshakux_log("takeover: the shadow's own pointers into libc.prx pointed into the shadow or the stub: %d "
				"(unknown code %d)",
		inner, unknown);
	return true;
}

void RegisterAgain()
{
	static const char* const setters[] = {"_sceKernelSetThreadDtors", "_sceKernelSetThreadAtexitCount",
		"_sceKernelSetThreadAtexitReport"};
	for (const char* name : setters)
	{
		auto set = reinterpret_cast<int (*)(void*)>(PS5SystemModules::FindFunction("libkernel", name, nullptr));
		if (!set)
		{
			pshakux_log("takeover: %s: not found (the rewrite covers it if the scan sees the pointer; the stubs at "
						"the old entries cover the rest)",
				name);
			continue;
		}
		pshakux_log("takeover: %s(%p) next", name, reinterpret_cast<void*>(&pshakux_libc_stub));
		const int rc = set(reinterpret_cast<void*>(&pshakux_libc_stub));
		pshakux_log("takeover: %s = 0x%x", name, static_cast<unsigned>(rc));
	}
	auto heap = reinterpret_cast<int (*)(void*)>(
		PS5SystemModules::FindFunction("libkernel", "_sceKernelRtldSetApplicationHeapAPI", nullptr));
	void* table = g_shadow + (0x110100 - kLibcDataLo);
	if (heap)
	{
		pshakux_log("takeover: _sceKernelRtldSetApplicationHeapAPI(%p, the shadow's copy of the table) next", table);
		const int rc = heap(table);
		pshakux_log("takeover: _sceKernelRtldSetApplicationHeapAPI = 0x%x", static_cast<unsigned>(rc));
	}
	else
	{
		pshakux_log("takeover: _sceKernelRtldSetApplicationHeapAPI: not found");
	}
}

bool Flag(const char* name)
{
	char path[512];
	struct stat st;
	std::snprintf(path, sizeof(path), "/data/ogbox/%s", name);
	if (stat(path, &st) == 0)
		return true;
	std::snprintf(path, sizeof(path), "%s/%s", pshakux_title_dir(), name);
	return stat(path, &st) == 0;
}

int Prepare()
{
	volatile int here = 0;
	g_my_stack = reinterpret_cast<uint64_t>(&here);
	pshakux_log("takeover: 0x80000000-0x80200000 from sce_module/libc.prx, so the guest's kernel image can be at "
				"0x80010000 (nothing is mapped here: the core maps the guest's window over it when it starts)");
	if (Flag("takeover-off"))
	{
		SetReason("the flag file takeover-off is there");
		return 0;
	}
	if (!ReadModules())
	{
		SetReason("the module list could not be read");
		return 0;
	}
	if (g_libc_at != kHoleLo)
	{
		if (g_libc_at)
			SetReason("libc.prx is not at 0x80000000 (0x%llx)", static_cast<unsigned long long>(g_libc_at));
		else
			SetReason("libc.prx is not in the module list");
		return 0;
	}
	if (g_foreign)
	{
		SetReason("another module (\"%s\") has a segment in 0x80000000-0x80200000", g_foreign_name);
		return 0;
	}
	ReadImports();
	int into = 0;
	for (int i = 0; i < g_nimports; i++)
	{
		const uint64_t v = *reinterpret_cast<volatile uint64_t*>(g_imports[i].slot);
		if (v >= kHoleLo && v < kTakeHi)
		{
			uint64_t to;
			const char* what;
			Classify(v, &to, &what);
			if (into < 32)
				pshakux_log("takeover:   import %s (%s, slot 0x%llx) = libc.prx +0x%llx, %s", g_imports[i].name,
					g_imports[i].type, static_cast<unsigned long long>(g_imports[i].slot),
					static_cast<unsigned long long>(v - kHoleLo), what);
			into++;
		}
	}
	pshakux_log("takeover: %d of the eboot's %d import slots are bound into libc.prx", into, g_nimports);

	pshakux_log("takeover: scan 1: every readable, non-executable mapping outside 0x80000000-0x80200000");
	const double t0 = pshakux_uptime();
	Scan("1");
	pshakux_log("takeover: scan 1 took %.1f ms", (pshakux_uptime() - t0) * 1000.0);
	int counts[5];
	ListHits(counts);

	// The decision (as the system check's). libc.prx has code only at its ten entries, so a value
	// elsewhere in its text is not a pointer to it (the console's error codes are 0x8002xxxx), nor
	// are its base and end (the loader's bookkeeping) or values past it. In the way: a pointer to an
	// entry or its data on another thread's stack (it may be live), or in a read-only segment of
	// another module (a GOT bound to it, or a constant that only looks like one).
	int on_stacks = 0, read_only = 0;
	for (int i = 0; i < g_nhits; i++)
	{
		const Hit& h = g_hits[i];
		const Region* g = &g_regions[h.region];
		if (h.cls > kData)
			continue;
		if ((g->flags & 4) && !(g->start <= g_my_stack && g->end > g_my_stack))
			on_stacks++;
		else if (h.rewritable && !(g->prot & 2) && !(h.where >= g_got_lo && h.where < g_got_hi))
			read_only++;
	}
	const bool forced = Flag("takeover-force");
	const bool go = forced || (on_stacks == 0 && read_only == 0);
	pshakux_log("takeover: %s (pointers to libc.prx on other threads' stacks: %d, in read-only segments of other "
				"modules: %d; flag takeover-force %s)",
		go ? "going on" : "NOT done", on_stacks, read_only, forced ? "present" : "absent");
	if (!go)
	{
		SetReason("%d pointers to libc.prx on other threads' stacks, %d in read-only segments of other modules "
				  "(the flag file takeover-force overrides)",
			on_stacks, read_only);
		return 0;
	}

	if (!FillShadow())
	{
		SetReason("libc.prx's data is not laid out as expected: stopped before changing anything");
		return 0;
	}
	RegisterAgain();
	pshakux_log("takeover: scan 2: after the registrations");
	Scan("2");
	ListHits(counts);
	pshakux_log("takeover: rewriting what is left next");
	const int failed = Rewrite();
	pshakux_log("takeover: scan 3: what still points into 0x80000000-0x80200000");
	Scan("3");
	ListHits(counts);
	int left = 0;
	for (int i = 0; i < g_nhits; i++)
		left += g_hits[i].rewritable && g_hits[i].cls <= kData;
	pshakux_log("takeover: pointers left that the rewrite should have taken: %d", left);
	if (failed || left)
	{
		// What was rewritten points at the eboot's copies, which stay right while libc.prx stays mapped.
		SetReason("%d rewrites failed, %d pointers left: libc.prx stays mapped", failed, left);
		return 0;
	}
	return 1;
}

} // namespace

extern "C" int pshakux_takeover_prepare(void)
{
	g_done = Prepare();
	if (g_done)
	{
		g_reason[0] = 0;
		pshakux_log("takeover: done: libc.prx's users point into the eboot; the core maps the guest's window over "
					"0x80000000 when it starts (the kernel image at 0x80010000), with stubs at libc.prx's old entries");
	}
	else
	{
		pshakux_log("takeover: NOT done: %s. The 0.1.3 layout stays: the guest's kernel area past libc.prx "
					"(0x80120000), and games that read the kernel's header at 0x80010000 are ended by the console",
			g_reason);
	}
	return g_done;
}

extern "C" int pshakux_takeover_done(void)
{
	return g_done;
}

extern "C" const char* pshakux_takeover_reason(void)
{
	return g_reason;
}

extern "C" void pshakux_takeover_set_exit_hook(void (*hook)(const char* why))
{
	g_exit_hook = hook;
}

extern "C" void pshakux_takeover_before_exit(const char* why)
{
	if (g_done && g_exit_hook)
		g_exit_hook(why);
}
