/*
 * OGBox tests - writing and reading back the hard disk.
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * Games keep files on the hard disk and read them back: saves on partition 1 (E:), and caches on
 * the utility partitions (Crash Bandicoot: The Wrath of Cortex copies its fonts and levels to
 * partition 4 and loads them from there). Each partition here gets a 6 MB file written in
 * pieces, overwritten at random places, grown with a gap, cut short and written at the file
 * position; a copy of what the file should hold is kept in memory and the file is read back
 * against it after every step.
 */
#include "common.h"

#include <stdlib.h>
#include <string.h>

#define FILE_SIZE (6u * 1024u * 1024u)
#define SHADOW_SIZE (FILE_SIZE + 2u * 1024u * 1024u)

static uint8_t *g_shadow; /* what the file should hold */
static uint8_t *g_io;     /* a buffer for reads and writes */
static uint32_t g_size;   /* its size */

static NTSTATUS open_file(const char *path, HANDLE *h, ULONG disposition)
{
    ANSI_STRING name;
    RtlInitAnsiString(&name, path);
    OBJECT_ATTRIBUTES oa = {NULL, &name, OBJ_CASE_INSENSITIVE};
    IO_STATUS_BLOCK iosb;
    return NtCreateFile(h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, NULL, 0x80 /* normal */, 0,
                        disposition, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
}

static bool write_at(HANDLE h, uint32_t off, const uint8_t *data, uint32_t len)
{
    IO_STATUS_BLOCK iosb = {0};
    LARGE_INTEGER at = {.QuadPart = off};
    const NTSTATUS st = NtWriteFile(h, NULL, NULL, NULL, &iosb, (PVOID)data, len, &at);
    return NT_SUCCESS(st) && iosb.Information == len;
}

static uint64_t file_size(HANDLE h)
{
    IO_STATUS_BLOCK iosb;
    FILE_STANDARD_INFORMATION fsi;
    memset(&fsi, 0, sizeof(fsi));
    NtQueryInformationFile(h, &iosb, &fsi, sizeof(fsi), FileStandardInformation);
    return (uint64_t)fsi.EndOfFile.QuadPart;
}

/* the whole file read back against the copy: the first differing offset, or -1 */
static int64_t verify(HANDLE h)
{
    for (uint32_t off = 0; off < g_size; off += 256 * 1024) {
        const uint32_t len = g_size - off < 256 * 1024 ? g_size - off : 256 * 1024;
        IO_STATUS_BLOCK iosb = {0};
        LARGE_INTEGER at = {.QuadPart = off};
        memset(g_io, 0xEE, len);
        const NTSTATUS st = NtReadFile(h, NULL, NULL, NULL, &iosb, g_io, len, &at);
        if (!NT_SUCCESS(st) || iosb.Information != len) {
            return off;
        }
        for (uint32_t k = 0; k < len; k++) {
            if (g_io[k] != g_shadow[off + k]) {
                return (int64_t)off + k;
            }
        }
    }
    return -1;
}

static void fill(uint8_t *p, uint32_t len, uint32_t seed)
{
    for (uint32_t k = 0; k < len; k++) {
        p[k] = (uint8_t)(gen_word(seed + k / 4) >> (8 * (k & 3)));
    }
}

static void partition(const char *path, const char *name)
{
    report("INFO %s: %s", name, path);
    HANDLE h;
    NTSTATUS st = open_file(path, &h, FILE_OVERWRITE_IF);
    check(NT_SUCCESS(st), "%s: create a file: status %08lx", name, (unsigned long)st);
    if (!NT_SUCCESS(st)) {
        return;
    }

    /* 6 MB in pieces of 1 byte to 300 KB */
    rnd_seed(0xD15Cu);
    fill(g_shadow, FILE_SIZE, 1);
    unsigned failed = 0;
    const uint64_t q0 = qpc();
    for (uint32_t off = 0; off < FILE_SIZE;) {
        uint32_t len = 1 + rnd() % (300u * 1024u);
        if (off + len > FILE_SIZE) {
            len = FILE_SIZE - off;
        }
        failed += !write_at(h, off, g_shadow + off, len);
        off += len;
    }
    g_size = FILE_SIZE;
    IO_STATUS_BLOCK fiosb;
    NtFlushBuffersFile(h, &fiosb);
    report("INFO %s: written at %.1f MB/s", name, 6.0 / (qpc_us(qpc() - q0) / 1e6));
    int64_t bad = verify(h);
    check(failed == 0 && bad < 0 && file_size(h) == g_size,
          "%s: 6 MB written in pieces, read back: %u writes failed, first wrong byte %lld, size %llu", name, failed,
          (long long)bad, (unsigned long long)file_size(h));

    /* closed and opened again */
    NtClose(h);
    st = open_file(path, &h, FILE_OPEN);
    bad = NT_SUCCESS(st) ? verify(h) : 0;
    check(NT_SUCCESS(st) && bad < 0, "%s: the same after closing and opening it again: status %08lx, first wrong byte %lld",
          name, (unsigned long)st, (long long)bad);
    if (!NT_SUCCESS(st)) {
        return;
    }

    /* 400 overwrites at random places */
    failed = 0;
    for (unsigned i = 0; i < 400; i++) {
        const uint32_t off = rnd() % FILE_SIZE;
        uint32_t len = 1 + rnd() % 70000;
        if (off + len > FILE_SIZE) {
            len = FILE_SIZE - off;
        }
        fill(g_io, len, 0x1000000u + i * 977u);
        memcpy(g_shadow + off, g_io, len);
        failed += !write_at(h, off, g_io, len);
    }
    bad = verify(h);
    check(failed == 0 && bad < 0, "%s: 400 overwrites at random places: %u failed, first wrong byte %lld", name, failed,
          (long long)bad);

    /* grown: a write 100000 bytes past the end leaves zeros in the gap */
    {
        const uint32_t off = g_size + 100000;
        fill(g_io, 5000, 0x2000000u);
        memset(g_shadow + g_size, 0, off - g_size);
        memcpy(g_shadow + off, g_io, 5000);
        const bool ok = write_at(h, off, g_io, 5000);
        g_size = off + 5000;
        bad = verify(h);
        check(ok && bad < 0 && file_size(h) == g_size,
              "%s: a write past the end: the gap reads as zeros (first wrong byte %lld), size %llu (%u)", name,
              (long long)bad, (unsigned long long)file_size(h), g_size);
    }

    /* cut short */
    {
        FILE_END_OF_FILE_INFORMATION eof = {.EndOfFile = {.QuadPart = 4000000}};
        IO_STATUS_BLOCK iosb;
        st = NtSetInformationFile(h, &iosb, &eof, sizeof(eof), FileEndOfFileInformation);
        g_size = 4000000;
        bad = verify(h);
        check(NT_SUCCESS(st) && bad < 0 && file_size(h) == g_size,
              "%s: cut to 4000000 bytes: status %08lx, size %llu, first wrong byte %lld", name, (unsigned long)st,
              (unsigned long long)file_size(h), (long long)bad);
    }

    /* writes at the file position (no offset), as fwrite does */
    {
        FILE_POSITION_INFORMATION fpi = {.CurrentByteOffset = {.QuadPart = 3999000}};
        IO_STATUS_BLOCK iosb;
        NtSetInformationFile(h, &iosb, &fpi, sizeof(fpi), FilePositionInformation);
        uint32_t pos = 3999000;
        failed = 0;
        static const uint32_t sizes[] = {700, 3, 4096, 65537, 1, 12000};
        for (unsigned i = 0; i < 6; i++) {
            fill(g_io, sizes[i], 0x3000000u + i * 131u);
            memcpy(g_shadow + pos, g_io, sizes[i]);
            memset(&iosb, 0, sizeof(iosb));
            st = NtWriteFile(h, NULL, NULL, NULL, &iosb, g_io, sizes[i], NULL);
            failed += !NT_SUCCESS(st) || iosb.Information != sizes[i];
            pos += sizes[i];
        }
        if (pos > g_size) {
            g_size = pos;
        }
        bad = verify(h);
        check(failed == 0 && bad < 0 && file_size(h) == g_size,
              "%s: 6 writes at the file position: %u failed, size %llu (%u), first wrong byte %lld", name, failed,
              (unsigned long long)file_size(h), g_size, (long long)bad);
    }
    NtClose(h);

    /* opened again with an overwrite (empty), then grown back with SetEndOfFile: FATX's clusters
     * still hold the old bytes and the file reads them again (Forza's unpacked caches) */
    {
        st = open_file(path, &h, FILE_OVERWRITE);
        const uint64_t emptied = NT_SUCCESS(st) ? file_size(h) : ~0ull;
        NTSTATUS st2 = st;
        if (NT_SUCCESS(st)) {
            FILE_END_OF_FILE_INFORMATION eof = {.EndOfFile = {.QuadPart = g_size}};
            IO_STATUS_BLOCK iosb;
            st2 = NtSetInformationFile(h, &iosb, &eof, sizeof(eof), FileEndOfFileInformation);
            bad = verify(h);
            NtClose(h);
        }
        check(NT_SUCCESS(st2) && emptied == 0 && bad < 0,
              "%s: overwritten, then grown back: status %08lx, size after the overwrite %llu, first wrong byte %lld",
              name, (unsigned long)st2, (unsigned long long)emptied, (long long)bad);
    }

    /* gone */
    {
        ANSI_STRING an;
        RtlInitAnsiString(&an, path);
        OBJECT_ATTRIBUTES oa = {NULL, &an, OBJ_CASE_INSENSITIVE};
        st = NtDeleteFile(&oa);
        HANDLE h2;
        const NTSTATUS st2 = open_file(path, &h2, FILE_OPEN);
        if (NT_SUCCESS(st2)) {
            NtClose(h2);
        }
        check(NT_SUCCESS(st) && !NT_SUCCESS(st2), "%s: deleted: status %08lx, opening it again %08lx (fails)", name,
              (unsigned long)st, (unsigned long)st2);
    }
}

void test_hdd(void)
{
    section("hard disk: write, read back");
    g_shadow = malloc(SHADOW_SIZE);
    g_io = malloc(512 * 1024);
    if (!g_shadow || !g_io) {
        check(false, "memory for the hard disk test");
        return;
    }
    partition("\\Device\\Harddisk0\\Partition1\\ogbox-test.bin", "partition 1 (E:)");
    partition("\\Device\\Harddisk0\\Partition4\\ogbox-test.bin", "partition 4 (a cache)");
    free(g_shadow);
    free(g_io);
}
