/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 1.0.2: saves from a console (an Xbox, xemu, or another emulator that signs as one) re-signed
 * for this one, as hakuX's app does it (android/.../data/hdd/SaveResigner.kt, ConsoleKeys.kt,
 * XCrypto.kt, ported).
 *
 * A game signs its saves with XAPI's XCalculateSignature: roamable = HMAC-SHA1(the title's key, the
 * data), the title's key being HMAC-SHA1(the console's certificate key, the certificate's signature
 * key)[0:16]; locked to the console = HMAC-SHA1(the HD key, the roamable signature). Downloaded
 * content's ContentMeta.xbx: HMAC-SHA1(HMAC-SHA1(HD key, title ID), its header). hakuX's kernel signs
 * as a console whose title keys are zero (OGBox has no Microsoft key, and needs none) and whose HD key
 * is its EEPROM's (native/kernel/ex.c nc_kernel_set_hd_key): a console's saves fail its checks.
 *
 * The source console's keys come from the user's own dumps, read when a game starts and never kept:
 * the certificate key from the BIOS (its 2BL holds it, RC4 under the MCPX 1.0 boot ROM's key), the
 * HD key from that console's EEPROM (its encrypted section) or as 32 hex digits. A file's signature
 * is looked for where games put it (XSavSig's resign.ini's places) and replaced only when it checks
 * with those keys: anything else is left as it is.
 */
#ifndef OGBOX_RESIGN_H
#define OGBOX_RESIGN_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ogbox {
namespace resign {

/* An EEPROM key's HMAC-SHA1 midstates (inner, outer): what tells it, without the key. */
struct Midstates {
	const char *name;
	uint32_t inner[5], outer[5];
};
/* The retail and debug kernels' (xemu's hw/xbox/eeprom_generation.c, hakuX's ConsoleKeys). */
const std::vector<Midstates> &eepromKeys();

/* The certificate key in a BIOS (256 KiB to 4 MiB) for the MCPX 1.0 boot ROM (512 bytes). */
bool certKeyFrom(const std::vector<uint8_t> &mcpx, const std::vector<uint8_t> &flash, uint8_t out[16],
	const std::vector<Midstates> &keys = eepromKeys());
/* The HD key in an EEPROM image (256 bytes): its encrypted section, checked by its hash. */
bool hdKeyFrom(const std::vector<uint8_t> &eeprom, uint8_t out[16], const std::vector<Midstates> &keys = eepromKeys());

/* The source console's keys, from a folder of dumps: mcpx.bin and bios.bin (the certificate key),
 * eeprom.bin (that console's, the HD key) or hdkey.txt (the HD key as 32 hex digits: an Xbox 360's
 * for its backward compatibility's saves). */
struct SourceKeys {
	bool cert{ false }, hd{ false };
	uint8_t certKey[16]{}, hdKey[16]{};
	std::string note;   // what was found and what wasn't, for the log
};
SourceKeys readSourceKeys(const std::string &dir);

/* The HD key hakuX's kernel signs with for the hard disk at hddRoot: <hddRoot>/eeprom.bin's (its
 * encrypted section's, or bytes 0x36-0x45 of an image kept in the clear), or before the first boot
 * the one every console starts with. */
void nativeHdKey(const std::string &hddRoot, uint8_t out[16]);

/* A title's key for its saves: from the certificate of a game's default.xbe (its own at +0xC0, and
 * per alternate title ID at +0x5C the key at +0xD0). */
struct TitleKey {
	uint32_t titleId{ 0 };
	uint8_t key[16]{};
};
std::vector<TitleKey> certificateKeys(const uint8_t *cert, size_t bytes);

struct Report {
	int resigned{ 0 };     // files whose signature was replaced (ContentMeta.xbx among them)
	int checked{ 0 };      // files read
	int skipped{ 0 };      // files seen before, unchanged (the cache)
	std::vector<std::string> unsigned_;   // read, no signature of the source console's found ("<title>/<path>")
};
/* The files of UDATA\<title ID> and TDATA\<title ID> on the hard disk at hddRoot, for each title of
 * keys, re-signed from the source console's keys to hakuX's. cachePath ("" none): a list of the files
 * read before (path, size, date) under these keys, which are not read again while unchanged. */
Report resignTitles(const std::string &hddRoot, const std::vector<TitleKey> &keys, const SourceKeys &source,
	const uint8_t nativeHd[16], const std::string &cachePath);

/* The crypto, for the test. */
void hmacSha1(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t len, uint8_t out[20]);
void innerMidstate(const uint8_t *key, size_t keyLen, uint32_t out[5]);
void outerMidstate(const uint8_t *key, size_t keyLen, uint32_t out[5]);
void rc4(const uint8_t *key, size_t keyLen, uint8_t *data, size_t len);

} // namespace resign
} // namespace ogbox

#endif
