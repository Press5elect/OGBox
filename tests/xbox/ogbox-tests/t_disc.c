/*
 * OGBox tests - reading the disc.
 *
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * testdata.bin next to default.xbe holds a known sequence (gen_word), so every byte a read gives
 * can be checked. OGBox reads a disc image on a share through a block cache (256 KB blocks,
 * ps5/src/platform/vfs/ps5_vfs.cpp) and hakuX's I/O queue; Crash Bandicoot crashes in code fed
 * by its file loads. Reads of every shape: sequential, random, unaligned buffers, across block
 * boundaries, at the end of the file, at the file position (no offset), several at once
 * (overlapped), from two threads, and through the C library.
 */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_SIZE (24u * 1024u * 1024u + 1234u) /* tools/gen-testdata.py */
#define BLOCK (256u * 1024u)
#define MAX_READ (512u * 1024u)

static char g_path[200];
static uint8_t *g_buf_raw;
static unsigned g_details;

static void data_path(void)
{
    const ANSI_STRING *img = &XeImageFileName[0];
    unsigned n = img->Length < sizeof(g_path) - 20 ? img->Length : sizeof(g_path) - 20;
    memcpy(g_path, img->Buffer, n);
    g_path[n] = 0;
    char *slash = strrchr(g_path, '\\');
    strcpy(slash ? slash + 1 : g_path, "testdata.bin");
}

static NTSTATUS open_data(HANDLE *h, bool sync)
{
    ANSI_STRING name;
    RtlInitAnsiString(&name, g_path);
    OBJECT_ATTRIBUTES oa = {NULL, &name, OBJ_CASE_INSENSITIVE};
    IO_STATUS_BLOCK iosb;
    return NtOpenFile(h, GENERIC_READ | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ,
                      FILE_NON_DIRECTORY_FILE | (sync ? FILE_SYNCHRONOUS_IO_NONALERT : 0));
}

/* one read at off of len into a buffer misaligned by mis; 0 when every byte is right */
static int read_check(HANDLE h, uint64_t off, uint32_t len, unsigned mis, const char *what)
{
    uint8_t *buf = g_buf_raw + mis;
    memset(buf, 0xEE, len);
    IO_STATUS_BLOCK iosb = {0};
    LARGE_INTEGER at = {.QuadPart = (LONGLONG)off};
    const NTSTATUS st = NtReadFile(h, NULL, NULL, NULL, &iosb, buf, len, &at);
    const uint32_t want = off >= DATA_SIZE ? 0 : (off + len > DATA_SIZE ? (uint32_t)(DATA_SIZE - off) : len);
    int bad = 0;
    int64_t at_bad = -1;
    if (!NT_SUCCESS(st) || iosb.Information != want) {
        bad = 1;
    } else if ((at_bad = first_mismatch(buf, off, want)) >= 0) {
        bad = 2;
    }
    if (bad && g_details++ < 6) {
        if (bad == 1) {
            report("INFO   %s: read +%llx %lu (buffer +%u): status %08lx, %lu bytes (want %lu)", what,
                   (unsigned long long)off, (unsigned long)len, mis, (unsigned long)st,
                   (unsigned long)iosb.Information, (unsigned long)want);
        } else {
            const uint64_t a = off + (uint64_t)at_bad;
            report("INFO   %s: read +%llx %lu (buffer +%u): byte +%llx is %02x, want %02x", what,
                   (unsigned long long)off, (unsigned long)len, mis, (unsigned long long)a, buf[at_bad],
                   (uint8_t)(gen_word((uint32_t)(a >> 2)) >> (8 * (a & 3))));
        }
    }
    return bad;
}

static uint32_t rnd_len(void)
{
    const uint32_t r = rnd() % 100;
    if (r < 50) {
        return 1 + rnd() % 4096;
    }
    if (r < 85) {
        return 1 + rnd() % 65536;
    }
    return 1 + rnd() % MAX_READ;
}

/* ------------------------------------------------------------ two threads */

struct reader
{
    unsigned id, reads, bad;
    uint8_t *buf;
    uint64_t end_qpc;
};

static DWORD WINAPI reader_main(LPVOID p)
{
    struct reader *r = p;
    HANDLE h;
    if (!NT_SUCCESS(open_data(&h, true))) {
        r->bad = 1000000;
        return 0;
    }
    uint32_t s = 0xC0FFEEu + r->id * 7919u;
    while (qpc() < r->end_qpc) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        const uint64_t off = s % DATA_SIZE;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        uint32_t len = 1 + s % (128u * 1024u);
        if (off + len > DATA_SIZE) {
            len = (uint32_t)(DATA_SIZE - off);
        }
        IO_STATUS_BLOCK iosb = {0};
        LARGE_INTEGER at = {.QuadPart = (LONGLONG)off};
        const NTSTATUS st = NtReadFile(h, NULL, NULL, NULL, &iosb, r->buf, len, &at);
        r->reads++;
        if (!NT_SUCCESS(st) || iosb.Information != len || first_mismatch(r->buf, off, len) >= 0) {
            r->bad++;
        }
    }
    NtClose(h);
    return 0;
}

void test_disc(void)
{
    section("disc: reads of testdata.bin");
    data_path();
    report("INFO %s", g_path);

    g_buf_raw = malloc(MAX_READ + 64);
    HANDLE h;
    NTSTATUS st = open_data(&h, true);
    check(NT_SUCCESS(st), "open %s: status %08lx", g_path, (unsigned long)st);
    if (!NT_SUCCESS(st)) {
        return;
    }

    {
        IO_STATUS_BLOCK iosb;
        FILE_STANDARD_INFORMATION fsi;
        memset(&fsi, 0, sizeof(fsi));
        NtQueryInformationFile(h, &iosb, &fsi, sizeof(fsi), FileStandardInformation);
        check(fsi.EndOfFile.QuadPart == DATA_SIZE, "its size %llu (%u)", (unsigned long long)fsi.EndOfFile.QuadPart,
              DATA_SIZE);
    }

    /* the whole file, 64 KB at a time */
    {
        unsigned bad = 0;
        const uint64_t q0 = qpc();
        for (uint64_t off = 0; off < DATA_SIZE; off += 65536) {
            bad += read_check(h, off, 65536, 0, "sequential") != 0;
        }
        const double s = qpc_us(qpc() - q0) / 1e6;
        check(bad == 0, "sequential 64 KB reads of the whole file: %u bad", bad);
        report("INFO sequential: %.1f MB/s", DATA_SIZE / 1048576.0 / s);
    }

    /* the same again, now from the cache */
    {
        const uint64_t q0 = qpc();
        unsigned bad = 0;
        for (uint64_t off = 0; off < 4u * 1024u * 1024u; off += 65536) {
            bad += read_check(h, off, 65536, 0, "again") != 0;
        }
        report("INFO the first 4 MB again: %.1f MB/s, %u bad", 4.0 / (qpc_us(qpc() - q0) / 1e6), bad);
    }

    /* random reads */
    {
        rnd_seed(0x5EEDu);
        unsigned bad = 0;
        const unsigned n = 3000;
        const uint64_t q0 = qpc();
        for (unsigned i = 0; i < n; i++) {
            const uint64_t off = rnd() % DATA_SIZE;
            bad += read_check(h, off, rnd_len(), rnd() % 16, "random") != 0;
        }
        check(bad == 0, "%u random reads (1 byte to 512 KB, buffers misaligned 0-15): %u bad", n, bad);
        report("INFO random reads: %.0f a second", n / (qpc_us(qpc() - q0) / 1e6));
    }

    /* across OGBox's 256 KB block boundaries */
    {
        unsigned bad = 0, n = 0;
        static const struct
        {
            int32_t back;
            uint32_t len;
        } shapes[] = {{1, 2}, {3, 7}, {4096, 8192}, {1, BLOCK + 2}, {0, 1}, {2048, 2048}, {2047, 2050}, {17, BLOCK * 2}};
        for (uint32_t b = BLOCK; b < DATA_SIZE; b += BLOCK * 7) {
            for (unsigned k = 0; k < sizeof(shapes) / sizeof(shapes[0]); k++) {
                bad += read_check(h, b - shapes[k].back, shapes[k].len, k % 4, "boundary") != 0;
                n++;
            }
        }
        check(bad == 0, "%u reads across 256 KB block boundaries: %u bad", n, bad);
    }

    /* the end of the file */
    {
        IO_STATUS_BLOCK iosb = {0};
        LARGE_INTEGER at = {.QuadPart = DATA_SIZE - 100};
        st = NtReadFile(h, NULL, NULL, NULL, &iosb, g_buf_raw, 1000, &at);
        check(NT_SUCCESS(st) && iosb.Information == 100 && first_mismatch(g_buf_raw, DATA_SIZE - 100, 100) < 0,
              "a read of 1000 bytes 100 before the end: status %08lx, %lu bytes (success, 100)", (unsigned long)st,
              (unsigned long)iosb.Information);
        memset(&iosb, 0, sizeof(iosb));
        at.QuadPart = DATA_SIZE;
        st = NtReadFile(h, NULL, NULL, NULL, &iosb, g_buf_raw, 1000, &at);
        check(st == (NTSTATUS)STATUS_END_OF_FILE && iosb.Information == 0,
              "a read at the end: status %08lx, %lu bytes (c0000011 end of file, 0)", (unsigned long)st,
              (unsigned long)iosb.Information);
    }

    /* at the file position (no offset): what fread does */
    {
        FILE_POSITION_INFORMATION fpi = {.CurrentByteOffset = {.QuadPart = 12345}};
        IO_STATUS_BLOCK iosb;
        NtSetInformationFile(h, &iosb, &fpi, sizeof(fpi), FilePositionInformation);
        static const uint32_t sizes[] = {1000, 7, 65537, 8192, 1, 300000, 4095, 2};
        uint64_t pos = 12345;
        unsigned bad = 0;
        for (unsigned r = 0; r < 40; r++) {
            const uint32_t len = sizes[r % 8];
            memset(&iosb, 0, sizeof(iosb));
            st = NtReadFile(h, NULL, NULL, NULL, &iosb, g_buf_raw, len, NULL);
            if (!NT_SUCCESS(st) || iosb.Information != len || first_mismatch(g_buf_raw, pos, len) >= 0) {
                bad++;
            }
            pos += len;
        }
        memset(&fpi, 0, sizeof(fpi));
        NtQueryInformationFile(h, &iosb, &fpi, sizeof(fpi), FilePositionInformation);
        check(bad == 0 && (uint64_t)fpi.CurrentByteOffset.QuadPart == pos,
              "40 reads at the file position: %u bad, position %llu (%llu)", bad,
              (unsigned long long)fpi.CurrentByteOffset.QuadPart, (unsigned long long)pos);
    }
    NtClose(h);

    /* sixteen overlapped reads at once */
    {
        HANDLE ha;
        st = open_data(&ha, false);
        check(NT_SUCCESS(st), "open for overlapped reads: status %08lx", (unsigned long)st);
        if (NT_SUCCESS(st)) {
            enum { N = 16, LEN = 96 * 1024 };
            static uint8_t bufs[N][LEN];
            HANDLE ev[N];
            IO_STATUS_BLOCK iosb[N];
            uint64_t off[N];
            NTSTATUS issued[N];
            rnd_seed(0xA5A5u);
            for (unsigned i = 0; i < N; i++) {
                NtCreateEvent(&ev[i], NULL, NotificationEvent, FALSE);
                off[i] = rnd() % (DATA_SIZE - LEN);
                memset(bufs[i], 0xEE, LEN);
                memset(&iosb[i], 0, sizeof(iosb[i]));
                LARGE_INTEGER at = {.QuadPart = (LONGLONG)off[i]};
                issued[i] = NtReadFile(ha, ev[i], NULL, NULL, &iosb[i], bufs[i], LEN, &at);
            }
            unsigned pending = 0, bad = 0;
            for (unsigned i = 0; i < N; i++) {
                if (issued[i] == STATUS_PENDING) {
                    pending++;
                    NtWaitForSingleObjectEx(ev[i], UserMode, FALSE, NULL);
                }
                if (!NT_SUCCESS(iosb[i].Status) || iosb[i].Information != LEN || first_mismatch(bufs[i], off[i], LEN) >= 0) {
                    bad++;
                }
                NtClose(ev[i]);
            }
            check(bad == 0, "16 overlapped 96 KB reads at once: %u bad (%u came back pending)", bad, pending);
            NtClose(ha);
        }
    }

    /* two threads reading at once, 2 s */
    {
        struct reader r[2];
        HANDLE th[2];
        const uint64_t end = qpc() + qpc_freq() * 2;
        for (unsigned i = 0; i < 2; i++) {
            memset(&r[i], 0, sizeof(r[i]));
            r[i].id = i;
            r[i].buf = malloc(128 * 1024);
            r[i].end_qpc = end;
            th[i] = CreateThread(NULL, 0, reader_main, &r[i], 0, NULL);
        }
        WaitForMultipleObjects(2, th, TRUE, 30000);
        for (unsigned i = 0; i < 2; i++) {
            CloseHandle(th[i]);
            free(r[i].buf);
        }
        check(r[0].bad == 0 && r[1].bad == 0, "two threads reading at once: %u and %u reads, %u and %u bad",
              r[0].reads, r[1].reads, r[0].bad, r[1].bad);
    }

    /* through the C library (D: is the XBE's folder) */
    {
        FILE *f = fopen("D:\\testdata.bin", "rb");
        check(f != NULL, "fopen D:\\testdata.bin");
        if (f) {
            rnd_seed(0xF00Du);
            unsigned bad = 0;
            for (unsigned i = 0; i < 300; i++) {
                const uint32_t off = rnd() % DATA_SIZE;
                uint32_t len = 1 + rnd() % 20000;
                if (off + len > DATA_SIZE) {
                    len = DATA_SIZE - off;
                }
                fseek(f, (long)off, SEEK_SET);
                const size_t got = fread(g_buf_raw, 1, len, f);
                if (got != len || first_mismatch(g_buf_raw, off, len) >= 0) {
                    bad++;
                }
            }
            fclose(f);
            check(bad == 0, "300 fseek + fread through the C library: %u bad", bad);
        }
    }
    free(g_buf_raw);
}
