/*
 * OGBox tests - a title's own page tables.
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * What a pager that keeps its own page tables does (one game streams its world so): it reads CR3,
 * reserves a window, writes a page directory entry for it pointing at a page table of its own, and
 * maps single 4 KiB pages anywhere in it; another entry points at the page directory itself, so
 * the page tables appear as a 4 MiB window that it reads before any flush. On the Xbox every 4 KiB
 * page maps anywhere. On a host whose pages are 16 KiB (the PS5) such a page can't share a host
 * page with its neighbours, and OGBox 0.0.3 reaches it by emulating each access: these checks are
 * the same on both.
 */
#include "common.h"

#include <string.h>

#define PG_FLAGS 0x63u   /* present, writable, accessed, dirty */

static uint32_t read_cr3(void)
{
    uint32_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void write_cr3(uint32_t v)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

void test_paging(void)
{
    section("a title's own page tables");

    const uint32_t cr3 = read_cr3() & ~0xFFFu;
    report("INFO CR3 %08lx", (unsigned long)cr3);
    volatile uint32_t *pd = (volatile uint32_t *)(0x80000000u | cr3);

    /* an 8 MiB window reserved; its 4 MiB-aligned half is the pager's */
    PVOID base = NULL;
    SIZE_T size = 8u << 20;
    NTSTATUS st = NtAllocateVirtualMemory(&base, 0, &size, MEM_RESERVE, PAGE_READWRITE);
    if (!NT_SUCCESS(st)) {
        check(false, "an 8 MiB window reserved: status %08lx", (unsigned long)st);
        return;
    }
    const uint32_t win = ((uint32_t)base + 0x3FFFFFu) & ~0x3FFFFFu;
    const unsigned pdi = win >> 22;
    report("INFO window %08lx (page directory entry %u)", (unsigned long)win, pdi);

    /* its page table, and two data pages, from contiguous memory */
    volatile uint32_t *pt = MmAllocateContiguousMemory(4096);
    volatile uint32_t *d1 = MmAllocateContiguousMemory(4096);
    volatile uint32_t *d2 = MmAllocateContiguousMemory(4096);
    if (!pt || !d1 || !d2) {
        check(false, "three pages of contiguous memory");
        return;
    }
    const uint32_t pt_pa = (uint32_t)MmGetPhysicalAddress((PVOID)pt);
    const uint32_t d1_pa = (uint32_t)MmGetPhysicalAddress((PVOID)d1);
    const uint32_t d2_pa = (uint32_t)MmGetPhysicalAddress((PVOID)d2);
    memset((void *)pt, 0, 4096);
    for (unsigned i = 0; i < 1024; i++) {
        d1[i] = 0xA5000000u | i;
        d2[i] = 0x5A000000u | i;
    }
    /* entries whose 4 KiB place differs from their page's within 16 KiB, and one where it is the same */
    unsigned j1 = 1;
    while (((j1 << 12) ^ d1_pa) & 0x3000u ? 0 : 1) {
        j1++;
    }
    unsigned j2 = j1 + 8;
    while ((((j2 << 12) ^ d2_pa) & 0x3000u) == 0) {
        j2++;
    }
    pt[j1] = d1_pa | PG_FLAGS;
    pt[j2] = d2_pa | PG_FLAGS;
    pd[pdi] = pt_pa | PG_FLAGS;
    write_cr3(read_cr3());

    volatile uint32_t *v1 = (volatile uint32_t *)(win + (j1 << 12));
    volatile uint32_t *v2 = (volatile uint32_t *)(win + (j2 << 12));
    check(v1[0] == (0xA5000000u | 0) && v1[1023] == (0xA5000000u | 1023),
          "a 4 KiB page mapped through the title's table reads its page: %08lx %08lx", (unsigned long)v1[0],
          (unsigned long)v1[1023]);
    check(v2[7] == (0x5A000000u | 7), "a second one: %08lx", (unsigned long)v2[7]);
    v1[3] = 0x12345678u;
    check(d1[3] == 0x12345678u, "a write through it lands in its page: %08lx", (unsigned long)d1[3]);
    d2[5] = 0xCAFEF00Du;
    check(v2[5] == 0xCAFEF00Du, "a write to its page is seen through it: %08lx", (unsigned long)v2[5]);

    /* the tables as a window: the directory's entry pdi + 1 points at the directory itself */
    const unsigned self = pdi + 1;
    pd[self] = cr3 | PG_FLAGS;
    volatile uint32_t *tables = (volatile uint32_t *)(self << 22);
    const uint32_t e = tables[pdi * 1024 + j1];   /* read before any flush, as the pager does */
    check((e & ~0xFFFu) == d1_pa, "the page table read through the directory's own entry, unflushed: %08lx (%08lx)",
          (unsigned long)e, (unsigned long)(d1_pa | PG_FLAGS));

    /* a page moved: the entry changed, flushed with INVLPG */
    pt[j1] = d2_pa | PG_FLAGS;
    __asm__ volatile("invlpg (%0)" : : "r"(v1) : "memory");
    check(v1[5] == 0xCAFEF00Du, "the entry pointed at another page, INVLPG: %08lx", (unsigned long)v1[5]);

    /* speed: a page reached by emulation costs a fault an access at first, then a gate call */
    {
        const uint64_t t0 = qpc();
        uint32_t sum = 0;
        for (unsigned r = 0; r < 200; r++) {
            for (unsigned i = 0; i < 1024; i += 16) {
                sum += v2[i];
            }
        }
        const double us = (double)(qpc() - t0) * 1e6 / (double)qpc_freq();
        report("INFO 12800 reads through the title's tables in %.0f us (sum %08lx)", us, (unsigned long)sum);
    }

    /* unmapped again */
    pt[j1] = 0;
    pt[j2] = 0;
    pd[self] = 0;
    pd[pdi] = 0;
    write_cr3(read_cr3());
    MmFreeContiguousMemory((PVOID)d2);
    MmFreeContiguousMemory((PVOID)d1);
    MmFreeContiguousMemory((PVOID)pt);
    size = 0;
    NtFreeVirtualMemory(&base, &size, MEM_RELEASE);
    check(true, "the window and its tables given back");
}
