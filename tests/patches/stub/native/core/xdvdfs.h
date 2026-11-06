/* OGBox by Press5elect, GPL-3.0-or-later. The PC test's stand-in for hakuX's disc reader
 * (native/core/xdvdfs.h): only what ps5/src/core/ogbox_patches.c names, none of it called. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct NcDiscFile { uint32_t sector; uint32_t size; uint8_t attr; } NcDiscFile;
typedef struct NcDisc { uint32_t root_sector; uint32_t root_size; } NcDisc;
bool nc_disc_read(const NcDisc *d, const NcDiscFile *f, uint64_t off, void *buf, size_t len);
bool nc_disc_find_orig(const NcDisc *d, const char *path, NcDiscFile *out);
bool nc_disc_read_orig(const NcDisc *d, const NcDiscFile *f, uint64_t off, void *buf, size_t len);
