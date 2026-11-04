/*
 * OGBox tests - memory.
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * Retail Xbox: 64 MB at physical 0, the kernel's view of it at 0x80000000 (contiguous memory is
 * handed out there: physical address = virtual - 0x80000000). Committed virtual memory reads as
 * zeros (NT). What the hardware leaves in fresh contiguous memory is only measured.
 */
#include "common.h"

#include <string.h>

static unsigned nonzero_bytes(const uint8_t *p, unsigned len)
{
    unsigned n = 0;
    for (unsigned i = 0; i < len; i++) {
        n += p[i] != 0;
    }
    return n;
}

void test_mem(void)
{
    section("memory");

    MM_STATISTICS ms;
    memset(&ms, 0, sizeof(ms));
    ms.Length = sizeof(ms);
    MmQueryStatistics(&ms);
    report("INFO %lu physical pages (%lu MB), %lu available; hardware info flags %08lx GPU %02x MCP %02x",
           (unsigned long)ms.TotalPhysicalPages, (unsigned long)(ms.TotalPhysicalPages / 256),
           (unsigned long)ms.AvailablePages, (unsigned long)XboxHardwareInfo.Flags,
           XboxHardwareInfo.GpuRevision, XboxHardwareInfo.McpRevision);
    check(ms.TotalPhysicalPages == 16384 || ms.TotalPhysicalPages == 32768,
          "physical memory %lu pages (16384 retail, 32768 debug)", (unsigned long)ms.TotalPhysicalPages);

    /* committed virtual memory: zeros */
    {
        PVOID base = NULL;
        SIZE_T size = 4 * 1024 * 1024;
        const NTSTATUS st = NtAllocateVirtualMemory(&base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (NT_SUCCESS(st)) {
            const unsigned nz = nonzero_bytes(base, (unsigned)size);
            check(nz == 0, "NtAllocateVirtualMemory 4 MB at %p: %u nonzero bytes (0)", base, nz);
            memset(base, 0xA5, size);
            SIZE_T fs = 0;
            NtFreeVirtualMemory(&base, &fs, MEM_RELEASE);
            base = NULL;
            size = 4 * 1024 * 1024;
            if (NT_SUCCESS(NtAllocateVirtualMemory(&base, 0, &size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))) {
                const unsigned nz2 = nonzero_bytes(base, (unsigned)size);
                check(nz2 == 0, "the same 4 MB again after a 0xA5 fill and a free: %u nonzero bytes (0)", nz2);
                fs = 0;
                NtFreeVirtualMemory(&base, &fs, MEM_RELEASE);
            }
        } else {
            check(false, "NtAllocateVirtualMemory 4 MB: status %08lx", (unsigned long)st);
        }
    }

    /* contiguous memory and its physical address */
    {
        uint8_t *p = MmAllocateContiguousMemory(1024 * 1024);
        if (p) {
            const unsigned nz = nonzero_bytes(p, 1024 * 1024);
            const ULONG_PTR phys = MmGetPhysicalAddress(p);
            report("INFO MmAllocateContiguousMemory 1 MB at %p (physical %08lx): %u nonzero bytes", (void *)p,
                   (unsigned long)phys, nz);
            check(((uintptr_t)p & 0xF0000000u) == 0x80000000u && (uintptr_t)p - 0x80000000u == phys,
                  "contiguous memory at 0x80000000 + its physical address (%p, %08lx)", (void *)p,
                  (unsigned long)phys);
            check(phys + 1024 * 1024 <= ms.TotalPhysicalPages * 4096u, "its physical address is in RAM");
            MmFreeContiguousMemory(p);
        } else {
            check(false, "MmAllocateContiguousMemory 1 MB: none");
        }
    }

    /* write-combined contiguous memory (what D3D uses for vertex buffers): written, read back */
    {
        uint32_t *p = MmAllocateContiguousMemoryEx(256 * 1024, 0, 0x03FFFFFF, 4096, PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (p) {
            for (unsigned i = 0; i < 65536; i++) {
                p[i] = gen_word(i);
            }
            unsigned bad = 0;
            for (unsigned i = 0; i < 65536; i++) {
                bad += p[i] != gen_word(i);
            }
            check(bad == 0, "write-combined memory at %p: %u of 65536 words read back wrong (0)", (void *)p, bad);
            MmFreeContiguousMemory(p);
        } else {
            check(false, "MmAllocateContiguousMemoryEx write-combined 256 KB: none");
        }
    }

    /* 600 regions of one page each, then all that is left in one region: the Xbox hands memory out
     * in 4 KiB pages, so the large one fits whatever the host's pages (OGBox 0.1.58: Forza's map,
     * which on the PS5's 16 KiB pages ran out with 5 MiB still free); and one page more doesn't */
    {
        enum { SMALL = 600 };
        static PVOID small[SMALL];
        unsigned made = 0;
        for (unsigned i = 0; i < SMALL; i++) {
            PVOID b = NULL;
            SIZE_T sz = 4096;
            if (!NT_SUCCESS(NtAllocateVirtualMemory(&b, 0, &sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))) {
                break;
            }
            ((volatile uint8_t *)b)[0] = (uint8_t)i;
            small[made++] = b;
        }
        MM_STATISTICS m2;
        memset(&m2, 0, sizeof(m2));
        m2.Length = sizeof(m2);
        MmQueryStatistics(&m2);
        /* left for the large one: what is available less a margin for the page tables it needs */
        const unsigned long avail = (unsigned long)m2.AvailablePages;
        const unsigned long want = avail > 64 ? avail - 64 : 0;
        PVOID big = NULL;
        SIZE_T bsz = want * 4096;
        const NTSTATUS st = want ? NtAllocateVirtualMemory(&big, 0, &bsz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
                                 : 0xC0000017;
        unsigned bad = 0;
        for (unsigned i = 0; i < made; i++) {
            bad += ((volatile uint8_t *)small[i])[0] != (uint8_t)i;
        }
        check(made == SMALL && NT_SUCCESS(st) && bad == 0,
              "%u one-page regions (%u), then %lu KB in one (of %lu available): status %08lx, %u small ones changed",
              made, (unsigned)SMALL, want * 4, avail * 4, (unsigned long)st, bad);
        /* past the 64 MB: twice what is left can't be had */
        if (NT_SUCCESS(st)) {
            MmQueryStatistics(&m2);
            PVOID over = NULL;
            SIZE_T osz = ((SIZE_T)m2.AvailablePages + 256) * 4096;
            const NTSTATUS st2 = NtAllocateVirtualMemory(&over, 0, &osz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            check(!NT_SUCCESS(st2), "1 MB more than available: status %08lx (fails)", (unsigned long)st2);
            if (NT_SUCCESS(st2)) {
                SIZE_T fs = 0;
                NtFreeVirtualMemory(&over, &fs, MEM_RELEASE);
            }
            SIZE_T fs = 0;
            NtFreeVirtualMemory(&big, &fs, MEM_RELEASE);
        }
        for (unsigned i = 0; i < made; i++) {
            SIZE_T fs = 0;
            NtFreeVirtualMemory(&small[i], &fs, MEM_RELEASE);
        }
    }

    /* 300 contiguous blocks of 8 KiB at 16 KiB alignment, a game's pattern (OGBox 0.0.2: 277 of its
     * 44 MiB, most of them so, then a level's 8 KiB failed with 2.2 MiB missing): the kernel takes
     * each from the top down, and the pages its alignment skips up to the next block stay free, so
     * the free count goes down by the blocks alone (2 pages each). Up to 0.0.1 hakuX held those pages
     * with each block and counted them as used: 3 or 4 pages each. */
    {
        enum { BLOCKS = 300 };
        static PVOID blk[BLOCKS];
        MM_STATISTICS m0, m1;
        memset(&m0, 0, sizeof(m0));
        m0.Length = sizeof(m0);
        MmQueryStatistics(&m0);
        unsigned made = 0, misaligned = 0;
        for (unsigned i = 0; i < BLOCKS; i++) {
            PVOID b = MmAllocateContiguousMemoryEx(8192, 0, 0xFFFFFFFF, 16384, PAGE_READWRITE);
            if (!b) {
                break;
            }
            misaligned += ((uintptr_t)b & 0x3FFF) != 0;
            blk[made++] = b;
        }
        memset(&m1, 0, sizeof(m1));
        m1.Length = sizeof(m1);
        MmQueryStatistics(&m1);
        const unsigned long used = (unsigned long)m0.AvailablePages - (unsigned long)m1.AvailablePages;
        check(made == BLOCKS && misaligned == 0 && used <= BLOCKS * 2 + 16,
              "%u blocks of 8 KB at 16 KB alignment (%u, %u misaligned): %lu pages fewer free (%u for the blocks)",
              made, (unsigned)BLOCKS, misaligned, used, (unsigned)BLOCKS * 2);
        for (unsigned i = 0; i < made; i++) {
            MmFreeContiguousMemory(blk[i]);
        }
        MM_STATISTICS m2;
        memset(&m2, 0, sizeof(m2));
        m2.Length = sizeof(m2);
        MmQueryStatistics(&m2);
        check(m2.AvailablePages + 16 >= m0.AvailablePages, "all of them freed: %lu pages free (%lu before)",
              (unsigned long)m2.AvailablePages, (unsigned long)m0.AvailablePages);
    }

    /* the kernel's pool */
    {
        uint8_t *p = ExAllocatePool(64 * 1024);
        check(p != NULL, "ExAllocatePool 64 KB: %p", (void *)p);
        if (p) {
            memset(p, 0x5A, 64 * 1024);
            ExFreePool(p);
        }
    }
}
