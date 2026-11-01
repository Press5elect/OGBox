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
