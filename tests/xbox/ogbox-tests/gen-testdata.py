#!/usr/bin/env python3
# OGBox tests - writes testdata.bin: word i (at byte 4*i, little-endian) is gen_word(i) of
# common.h, cut to DATA_SIZE bytes (t_disc.c). OGBox by Press5elect, GPL-3.0-or-later.
import sys

DATA_SIZE = 24 * 1024 * 1024 + 1234
M = 0xFFFFFFFF


def main(path):
    try:
        import numpy as np
    except ImportError:
        np = None
    words = (DATA_SIZE + 3) // 4
    if np is not None:
        i = np.arange(words, dtype=np.uint64)
        x = ((i * 0x9E3779B1) & M) ^ 0x7F4A7C15
        x ^= x >> 15
        x = (x * 0x2C1B3C6D) & M
        x ^= x >> 12
        x = (x * 0x297A2D39) & M
        x ^= x >> 15
        data = x.astype("<u4").tobytes()
    else:
        out = bytearray(words * 4)
        for i in range(words):
            x = ((i * 0x9E3779B1) & M) ^ 0x7F4A7C15
            x ^= x >> 15
            x = (x * 0x2C1B3C6D) & M
            x ^= x >> 12
            x = (x * 0x297A2D39) & M
            x ^= x >> 15
            out[4 * i:4 * i + 4] = x.to_bytes(4, "little")
        data = bytes(out)
    with open(path, "wb") as f:
        f.write(data[:DATA_SIZE])


if __name__ == "__main__":
    main(sys.argv[1])
