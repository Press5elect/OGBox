/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 1.0.2: saves re-signed for this console (ogbox_resign.h): hakuX's app's SaveResigner,
 * ConsoleKeys and XCrypto, ported.
 */
#include "ogbox_resign.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <set>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace ogbox {
namespace resign {

/* ---- SHA-1, from a midstate too ---- */

namespace {

constexpr uint32_t kIv[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };

inline uint32_t rol(uint32_t x, int n)
{
	return (x << n) | (x >> (32 - n));
}

void compress(uint32_t h[5], const uint8_t *block)
{
	uint32_t w[80];
	for (int t = 0; t < 16; t++) {
		w[t] = (uint32_t)block[4 * t] << 24 | (uint32_t)block[4 * t + 1] << 16 | (uint32_t)block[4 * t + 2] << 8 | block[4 * t + 3];
	}
	for (int t = 16; t < 80; t++) {
		w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
	}
	uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
	for (int t = 0; t < 80; t++) {
		uint32_t f, k;
		if (t < 20) {
			f = (b & c) | (~b & d);
			k = 0x5A827999u;
		} else if (t < 40) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1u;
		} else if (t < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDCu;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6u;
		}
		const uint32_t tmp = rol(a, 5) + f + e + k + w[t];
		e = d;
		d = c;
		c = rol(b, 30);
		b = a;
		a = tmp;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
}

struct Sha1 {
	uint32_t h[5];
	uint64_t len{ 0 };      // bytes hashed (a midstate's block counts)
	uint8_t buf[64];
	size_t n{ 0 };

	Sha1() { std::memcpy(h, kIv, sizeof(h)); }
	explicit Sha1(const uint32_t mid[5]) : len(64) { std::memcpy(h, mid, sizeof(h)); }

	void update(const uint8_t *p, size_t m)
	{
		len += m;
		while (m) {
			if (n == 0 && m >= 64) {
				compress(h, p);
				p += 64;
				m -= 64;
				continue;
			}
			const size_t take = std::min(m, 64 - n);
			std::memcpy(buf + n, p, take);
			n += take;
			p += take;
			m -= take;
			if (n == 64) {
				compress(h, buf);
				n = 0;
			}
		}
	}

	void final(uint8_t out[20])
	{
		const uint64_t bits = len * 8;
		const uint8_t one = 0x80, zero = 0;
		const uint64_t keep = len;
		update(&one, 1);
		while (n != 56) {
			update(&zero, 1);
		}
		uint8_t l[8];
		for (int i = 0; i < 8; i++) {
			l[i] = (uint8_t)(bits >> (56 - 8 * i));
		}
		update(l, 8);
		len = keep;
		for (int i = 0; i < 20; i++) {
			out[i] = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4)));
		}
	}
};

void padded(const uint8_t *key, size_t keyLen, uint8_t x, uint8_t out[64])
{
	uint8_t k[20];
	if (keyLen > 64) {
		Sha1 s;
		s.update(key, keyLen);
		s.final(k);
		key = k;
		keyLen = 20;
	}
	for (size_t i = 0; i < 64; i++) {
		out[i] = (uint8_t)((i < keyLen ? key[i] : 0) ^ x);
	}
}

/* HMAC-SHA1, incremental: a copy forks it, for messages that share a start */
struct Hmac {
	Sha1 inner;
	uint8_t outerPad[64];

	Hmac(const uint8_t *key, size_t keyLen)
	{
		uint8_t ip[64];
		padded(key, keyLen, 0x36, ip);
		padded(key, keyLen, 0x5C, outerPad);
		inner.update(ip, 64);
	}
	void update(const uint8_t *p, size_t m) { inner.update(p, m); }
	void finish(uint8_t out[20]) const
	{
		Sha1 i = inner;
		uint8_t d[20];
		i.final(d);
		Sha1 o;
		o.update(outerPad, 64);
		o.update(d, 20);
		o.final(out);
	}
};

/* HMAC-SHA1 from a key's inner and outer midstates */
void hmacMid(const Midstates &k, const uint8_t *msg, size_t len, uint8_t out[20])
{
	Sha1 i(k.inner);
	i.update(msg, len);
	uint8_t d[20];
	i.final(d);
	Sha1 o(k.outer);
	o.update(d, 20);
	o.final(out);
}

const std::vector<Midstates> kEepromKeys = {
	{ "debug", { 0x85F9E51Au, 0xE04613D2u, 0x6D86A50Cu, 0x77C32E3Cu, 0x4BD717A4u },
		{ 0x5D7A9C6Bu, 0xE1922BEBu, 0xB82CCDBCu, 0x3137AB34u, 0x486B52B3u } },
	{ "1.0", { 0x72127625u, 0x336472B9u, 0xBE609BEAu, 0xF55E226Bu, 0x99958DACu },
		{ 0x76441D41u, 0x4DE82659u, 0x2E8EF85Eu, 0xB256FACAu, 0xC4FE2DE8u } },
	{ "1.1-1.4", { 0x39B06E79u, 0xC9BD25E8u, 0xDBC6B498u, 0x40B4389Du, 0x86BBD7EDu },
		{ 0x9B49BED3u, 0x84B430FCu, 0x6B8749CDu, 0xEBFE5FE5u, 0xD96E7393u } },
	{ "1.6", { 0x8058763Au, 0xF97D4E0Eu, 0x865A9762u, 0x8A3D920Du, 0x08995B2Cu },
		{ 0x01075307u, 0xA2F1E037u, 0x1186EEEAu, 0x88DA9992u, 0x168A5609u } },
};

constexpr size_t kMcpxSize = 512;
constexpr size_t kMcpxRc4 = 0x1A5;
constexpr size_t kBlSize = 0x6000;   // the 2BL: the 24 KiB below the flash's last 512 bytes
constexpr size_t kBlEnd = 0x200;

/* The HD key every console starts with before it has an identity of its own (ex.c k_legacy_hd_key). */
constexpr uint8_t kLegacyHd[16] = { '0', '0', '0', '0', '0', '0', '0', '0', '0', '1', 0x00, 0x50, 0xF2, 0x00, 0x00, 0x01 };

bool readFile(const std::string &path, std::vector<uint8_t> &out, size_t maxBytes)
{
	out.clear();
	const int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0) {
		return false;
	}
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size > maxBytes) {
		close(fd);
		return false;
	}
	out.resize((size_t)st.st_size);
	size_t at = 0;
	while (at < out.size()) {
		const ssize_t r = read(fd, out.data() + at, out.size() - at);
		if (r <= 0) {
			close(fd);
			out.clear();
			return false;
		}
		at += (size_t)r;
	}
	close(fd);
	return true;
}

int hexValue(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

std::string lower(std::string s)
{
	for (char &c : s) {
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

/* a folder's entry by its name, whatever its case ("" none) */
std::string childAnyCase(const std::string &dir, const char *name, bool wantDir)
{
	DIR *d = opendir(dir.c_str());
	if (!d) {
		return std::string();
	}
	std::string found;
	while (dirent *e = readdir(d)) {
		if (strcasecmp(e->d_name, name) == 0) {
			const std::string p = dir + "/" + e->d_name;
			struct stat st;
			if (stat(p.c_str(), &st) == 0 && (wantDir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode))) {
				found = p;
				break;
			}
		}
	}
	closedir(d);
	return found;
}

void walkFiles(const std::string &dir, std::vector<std::string> &out, int depth = 0)
{
	if (depth > 16) {
		return;
	}
	DIR *d = opendir(dir.c_str());
	if (!d) {
		return;
	}
	std::vector<std::string> names;
	while (dirent *e = readdir(d)) {
		if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..")) {
			names.emplace_back(e->d_name);
		}
	}
	closedir(d);
	std::sort(names.begin(), names.end());
	for (const std::string &n : names) {
		const std::string p = dir + "/" + n;
		struct stat st;
		if (lstat(p.c_str(), &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			walkFiles(p, out, depth + 1);
		} else if (S_ISREG(st.st_mode)) {
			out.push_back(p);
		}
	}
}

constexpr int kRoam = 0, kNoRoam = 1;
constexpr uint64_t kMaxFile = 128ull << 20;

bool isMeta(const std::string &name)
{
	const std::string n = lower(name);
	return n == "titlemeta.xbx" || n == "titleimage.xbx" || n == "savemeta.xbx" || n == "saveimage.xbx";
}

/* A signature found: the data it covers [a, b), its kind, and where (file "": this one). */
struct Hit {
	size_t a{ 0 }, b{ 0 };
	int kind{ kRoam };
	std::string file;
	uint64_t off{ 0 };
};

size_t unpaddedLen(const std::vector<uint8_t> &d, uint8_t pad)
{
	size_t m = d.size();
	while (m > 0 && d[m - 1] == pad) {
		m--;
	}
	return m;
}

bool matchesAt(const std::vector<uint8_t> &d, size_t p, const uint8_t s[20])
{
	return p + 20 <= d.size() && std::memcmp(d.data() + p, s, 20) == 0;
}

long indexOf(const std::vector<uint8_t> &hay, const uint8_t s[20])
{
	if (hay.size() < 20) {
		return -1;
	}
	for (size_t i = 0; i + 20 <= hay.size(); i++) {
		if (hay[i] == s[0] && std::memcmp(hay.data() + i, s, 20) == 0) {
			return (long)i;
		}
	}
	return -1;
}

/* Where the file's signature is: its data range, kind and place. The places are tried from the
 * file's end, then from its content's end before padding (zeros or spaces): a game may write its
 * data and the signature after it into a file of a fixed size. */
bool findSignature(const std::vector<uint8_t> &d, const std::vector<std::pair<std::string, std::vector<uint8_t>>> &small,
	const uint8_t auth[16], const SourceKeys &src, Hit &hit)
{
	const size_t n = d.size();
	std::vector<size_t> ms;
	for (size_t m : { n, unpaddedLen(d, 0), unpaddedLen(d, 0x20) }) {
		if (m > 20 && std::find(ms.begin(), ms.end(), m) == ms.end()) {
			ms.push_back(m);
		}
	}
	for (size_t m : ms) {
		std::vector<size_t> ends;
		for (long e : { (long)m - 24, (long)m - 20, (long)m - 4, (long)m }) {
			if (e > 0 && std::find(ends.begin(), ends.end(), (size_t)e) == ends.end()) {
				ends.push_back((size_t)e);
			}
		}
		for (size_t a : { (size_t)0, (size_t)4, (size_t)20, (size_t)24 }) {
			Hmac h(auth, 16);
			size_t at = a;
			for (size_t b : ends) {
				if (b <= a) {
					continue;
				}
				h.update(d.data() + at, b - at);
				at = b;
				uint8_t sigs[2][20];
				h.finish(sigs[kRoam]);
				const int kinds = src.hd ? 2 : 1;
				if (src.hd) {
					hmacSha1(src.hdKey, 16, sigs[kRoam], 20, sigs[kNoRoam]);
				}
				for (long p : { 0L, 4L, (long)m - 24, (long)m - 20 }) {
					if (p < 0 || (size_t)p + 20 > m || ((size_t)p + 20 > a && (size_t)p < b)) {
						continue;
					}
					for (int k = 0; k < kinds; k++) {
						if (matchesAt(d, (size_t)p, sigs[k])) {
							hit = { a, b, k, std::string(), (uint64_t)p };
							return true;
						}
					}
				}
				if (a == 0 && b == n) {
					for (const auto &f : small) {
						for (int k = 0; k < kinds; k++) {
							const long q = indexOf(f.second, sigs[k]);
							if (q >= 0) {
								hit = { a, b, k, f.first, (uint64_t)q };
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

/* 20 bytes written at off of the file, its date kept */
bool writeSig(const std::string &path, uint64_t off, const uint8_t sig[20])
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		return false;
	}
	const int fd = open(path.c_str(), O_WRONLY);
	if (fd < 0) {
		return false;
	}
	const bool ok = pwrite(fd, sig, 20, (off_t)off) == 20;
	close(fd);
	struct timeval times[2] = { { st.st_atime, 0 }, { st.st_mtime, 0 } };   // (seconds: what the sync and the cache compare)
	utimes(path.c_str(), times);
	return ok;
}

uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* ContentMeta.xbx: its header [0x14, header size) signed at 0 with HMAC(HMAC(HD key, title ID at 0x24)). */
bool resignContent(const std::string &path, const std::vector<uint8_t> &d, const SourceKeys &src, const uint8_t nativeHd[16])
{
	if (!src.hd || d.size() < 0x28) {
		return false;
	}
	const uint32_t size = le32(d.data() + 0x18);
	if (size <= 0x14 || size > d.size()) {
		return false;
	}
	uint8_t k[20], sig[20];
	hmacSha1(src.hdKey, 16, d.data() + 0x24, 4, k);
	hmacSha1(k, 20, d.data() + 0x14, size - 0x14, sig);
	if (!matchesAt(d, 0, sig)) {
		return false;
	}
	hmacSha1(nativeHd, 16, d.data() + 0x24, 4, k);
	hmacSha1(k, 20, d.data() + 0x14, size - 0x14, sig);
	return writeSig(path, 0, sig);
}

std::string keysFingerprint(const SourceKeys &src, const uint8_t nativeHd[16])
{
	uint8_t b[49] = { (uint8_t)(src.cert | src.hd << 1) }, h[20];
	std::memcpy(b + 1, src.certKey, 16);
	std::memcpy(b + 17, src.hdKey, 16);
	std::memcpy(b + 33, nativeHd, 16);
	Sha1 s;
	s.update(b, sizeof(b));
	s.final(h);
	char t[17];
	for (int i = 0; i < 8; i++) {
		std::snprintf(t + 2 * i, 3, "%02x", h[i]);
	}
	return t;
}

} // namespace

void hmacSha1(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t len, uint8_t out[20])
{
	Hmac h(key, keyLen);
	h.update(data, len);
	h.finish(out);
}

void innerMidstate(const uint8_t *key, size_t keyLen, uint32_t out[5])
{
	uint8_t p[64];
	padded(key, keyLen, 0x36, p);
	std::memcpy(out, kIv, sizeof(kIv));
	compress(out, p);
}

void outerMidstate(const uint8_t *key, size_t keyLen, uint32_t out[5])
{
	uint8_t p[64];
	padded(key, keyLen, 0x5C, p);
	std::memcpy(out, kIv, sizeof(kIv));
	compress(out, p);
}

void rc4(const uint8_t *key, size_t keyLen, uint8_t *data, size_t len)
{
	uint8_t s[256];
	for (int i = 0; i < 256; i++) {
		s[i] = (uint8_t)i;
	}
	unsigned j = 0;
	for (int i = 0; i < 256; i++) {
		j = (j + s[i] + key[(size_t)i % keyLen]) & 0xFF;
		std::swap(s[i], s[j]);
	}
	unsigned i = 0;
	j = 0;
	for (size_t n = 0; n < len; n++) {
		i = (i + 1) & 0xFF;
		j = (j + s[i]) & 0xFF;
		std::swap(s[i], s[j]);
		data[n] ^= s[(s[i] + s[j]) & 0xFF];
	}
}

const std::vector<Midstates> &eepromKeys()
{
	return kEepromKeys;
}

bool certKeyFrom(const std::vector<uint8_t> &mcpx, const std::vector<uint8_t> &flash, uint8_t out[16],
	const std::vector<Midstates> &keys)
{
	if (mcpx.size() != kMcpxSize || flash.size() < 0x40000) {
		return false;
	}
	std::vector<uint8_t> bl(flash.end() - (long)(kBlEnd + kBlSize), flash.end() - (long)kBlEnd);
	rc4(mcpx.data() + kMcpxRc4, 16, bl.data(), bl.size());
	for (size_t off = 0; off + 48 <= bl.size(); off++) {
		uint32_t mid[5];
		innerMidstate(bl.data() + off, 16, mid);
		for (const Midstates &k : keys) {
			if (std::memcmp(mid, k.inner, sizeof(mid)) == 0) {
				std::memcpy(out, bl.data() + off + 16, 16);
				return true;
			}
		}
	}
	return false;
}

bool hdKeyFrom(const std::vector<uint8_t> &eeprom, uint8_t out[16], const std::vector<Midstates> &keys)
{
	if (eeprom.size() < 0x30) {
		return false;
	}
	for (const Midstates &k : keys) {
		uint8_t seed[20], plain[0x1C], hash[20];
		hmacMid(k, eeprom.data(), 20, seed);
		std::memcpy(plain, eeprom.data() + 20, sizeof(plain));
		rc4(seed, 20, plain, sizeof(plain));
		hmacMid(k, plain, sizeof(plain), hash);
		if (std::memcmp(hash, eeprom.data(), 20) == 0) {
			std::memcpy(out, plain + 8, 16);
			return true;
		}
	}
	return false;
}

SourceKeys readSourceKeys(const std::string &dir)
{
	SourceKeys k;
	std::vector<uint8_t> mcpx, bios, eeprom;
	const bool haveMcpx = readFile(dir + "/mcpx.bin", mcpx, kMcpxSize), haveBios = readFile(dir + "/bios.bin", bios, 0x400000);
	if (haveMcpx && haveBios) {
		k.cert = certKeyFrom(mcpx, bios, k.certKey);
		k.note = k.cert ? "the certificate key from mcpx.bin and bios.bin" : "mcpx.bin and bios.bin give no certificate key "
			"(the MCPX 1.0 boot ROM and a BIOS are needed)";
	} else {
		k.note = haveMcpx || haveBios ? std::string(haveMcpx ? "bios.bin" : "mcpx.bin") + " missing: no certificate key"
			: "no mcpx.bin and bios.bin: no certificate key";
	}
	std::vector<uint8_t> hex;
	if (readFile(dir + "/eeprom.bin", eeprom, 0x1000)) {
		k.hd = hdKeyFrom(eeprom, k.hdKey);
		k.note += k.hd ? "; the HD key from eeprom.bin" : "; eeprom.bin gives no HD key";
	} else if (readFile(dir + "/hdkey.txt", hex, 4096)) {
		uint8_t key[16];
		int got = 0, hi = -1;
		for (uint8_t c : hex) {
			const int v = hexValue((char)c);
			if (v < 0) {
				continue;
			}
			if (got == 16) {
				got = 17;   // more than 32 digits
				break;
			}
			if (hi < 0) {
				hi = v;
			} else {
				key[got++] = (uint8_t)(hi << 4 | v);
				hi = -1;
			}
		}
		k.hd = got == 16 && hi < 0;
		if (k.hd) {
			std::memcpy(k.hdKey, key, 16);
		}
		k.note += k.hd ? "; the HD key from hdkey.txt" : "; hdkey.txt isn't 32 hex digits";
	} else {
		k.note += "; no eeprom.bin or hdkey.txt: saves locked to the console stay as they are";
	}
	return k;
}

void nativeHdKey(const std::string &hddRoot, uint8_t out[16])
{
	std::vector<uint8_t> e;
	if (!readFile(hddRoot + "/eeprom.bin", e, 0x1000) || e.size() < 0x46) {
		std::memcpy(out, kLegacyHd, 16);
		return;
	}
	if (!hdKeyFrom(e, out)) {
		std::memcpy(out, e.data() + 0x36, 16);   // an image kept in the clear
	}
}

std::vector<TitleKey> certificateKeys(const uint8_t *cert, size_t bytes)
{
	std::vector<TitleKey> out;
	if (bytes < 0x1D0) {
		return out;
	}
	TitleKey own;
	own.titleId = le32(cert + 8);
	std::memcpy(own.key, cert + 0xC0, 16);
	if (own.titleId) {
		out.push_back(own);
	}
	for (int i = 0; i < 16; i++) {
		TitleKey alt;
		alt.titleId = le32(cert + 0x5C + 4 * i);
		if (!alt.titleId || std::any_of(out.begin(), out.end(), [&](const TitleKey &t) { return t.titleId == alt.titleId; })) {
			continue;
		}
		std::memcpy(alt.key, cert + 0xD0 + 16 * i, 16);
		out.push_back(alt);
	}
	return out;
}

Report resignTitles(const std::string &hddRoot, const std::vector<TitleKey> &keys, const SourceKeys &source,
	const uint8_t nativeHd[16], const std::string &cachePath)
{
	Report r;
	if (!source.cert && !source.hd) {
		return r;
	}
	/* the files read before under these keys, unchanged since: not read again */
	const std::string print = keysFingerprint(source, nativeHd);
	std::map<std::string, std::pair<uint64_t, int64_t>> seen;
	if (!cachePath.empty()) {
		FILE *f = std::fopen(cachePath.c_str(), "r");
		char line[2048];
		bool same = false;
		while (f && std::fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (!std::strncmp(line, "keys ", 5)) {
				same = print == line + 5;
				continue;
			}
			unsigned long long size = 0;
			long long mtime = 0;
			int at = 0;
			if (same && std::sscanf(line, "%llu %lld %n", &size, &mtime, &at) == 2 && line[at]) {
				seen[line + at] = { size, mtime };
			}
		}
		if (f) {
			std::fclose(f);
		}
	}
	const std::string e = childAnyCase(hddRoot, "Partition1", true);
	std::vector<std::pair<std::string, Hit>> found;
	std::set<std::string> visited;
	for (const TitleKey &tk : keys) {
		char id[9];
		std::snprintf(id, sizeof(id), "%08x", tk.titleId);
		uint8_t auth[20] = {};
		if (source.cert) {
			hmacSha1(source.certKey, 16, tk.key, 16, auth);   // the source console's key for this title
		}
		for (const char *area : { "UDATA", "TDATA" }) {
			const std::string a = e.empty() ? std::string() : childAnyCase(e, area, true);
			const std::string dir = a.empty() ? std::string() : childAnyCase(a, id, true);
			if (dir.empty()) {
				continue;
			}
			std::vector<std::string> files;
			walkFiles(dir, files);
			for (const std::string &path : files) {
				struct stat st;
				if (!visited.insert(path).second || stat(path.c_str(), &st) != 0) {
					continue;
				}
				const std::string name = path.substr(path.find_last_of('/') + 1);
				if ((uint64_t)st.st_size < 21 || (uint64_t)st.st_size > kMaxFile || isMeta(name)) {
					continue;
				}
				const auto was = seen.find(path);
				if (was != seen.end() && was->second.first == (uint64_t)st.st_size && was->second.second == (int64_t)st.st_mtime) {
					r.skipped++;
					continue;
				}
				std::vector<uint8_t> d;
				if (!readFile(path, d, kMaxFile)) {
					continue;
				}
				r.checked++;
				const std::string where = std::string(id) + path.substr(dir.size());
				if (strcasecmp(name.c_str(), "ContentMeta.xbx") == 0) {
					if (resignContent(path, d, source, nativeHd)) {
						r.resigned++;
					} else {
						r.unsigned_.push_back(where);
					}
					continue;
				}
				if (!source.cert) {
					r.unsigned_.push_back(where);
					continue;
				}
				/* a .sig file of its own: the small files beside it */
				std::vector<std::pair<std::string, std::vector<uint8_t>>> small;
				const std::string folder = path.substr(0, path.find_last_of('/'));
				std::vector<std::string> here;
				DIR *dd = opendir(folder.c_str());
				while (dd) {
					dirent *ent = readdir(dd);
					if (!ent) {
						break;
					}
					const std::string p = folder + "/" + ent->d_name;
					struct stat s2;
					if (p != path && stat(p.c_str(), &s2) == 0 && S_ISREG(s2.st_mode) && s2.st_size >= 20 && s2.st_size <= 4096) {
						here.push_back(p);
					}
				}
				if (dd) {
					closedir(dd);
				}
				std::sort(here.begin(), here.end());
				for (const std::string &p : here) {
					std::vector<uint8_t> b;
					if (readFile(p, b, 4096)) {
						small.emplace_back(p, std::move(b));
					}
				}
				Hit hit;
				if (findSignature(d, small, auth, source, hit)) {
					found.emplace_back(path, hit);
				} else {
					r.unsigned_.push_back(where);
				}
			}
		}
	}
	/* a file holding others' signatures signs itself last, over theirs */
	std::set<std::string> holders;
	for (const auto &f : found) {
		if (!f.second.file.empty()) {
			holders.insert(f.second.file);
		}
	}
	std::stable_sort(found.begin(), found.end(), [&](const auto &x, const auto &y) {
		const bool xl = x.second.file.empty() && holders.count(x.first), yl = y.second.file.empty() && holders.count(y.first);
		return !xl && yl;
	});
	static const uint8_t zero[16] = {};
	for (const auto &f : found) {
		std::vector<uint8_t> d;
		if (!readFile(f.first, d, kMaxFile) || f.second.b > d.size()) {
			continue;
		}
		uint8_t sig[20];
		hmacSha1(zero, 16, d.data() + f.second.a, f.second.b - f.second.a, sig);
		if (f.second.kind == kNoRoam) {
			uint8_t locked[20];
			hmacSha1(nativeHd, 16, sig, 20, locked);
			std::memcpy(sig, locked, 20);
		}
		if (writeSig(f.second.file.empty() ? f.first : f.second.file, f.second.off, sig)) {
			r.resigned++;
		}
	}
	/* the files read, as they are now */
	if (!cachePath.empty() && (r.checked || r.skipped)) {
		const std::string tmp = cachePath + ".tmp";
		FILE *f = std::fopen(tmp.c_str(), "w");
		if (f) {
			std::fprintf(f, "keys %s\n", print.c_str());
			for (const std::string &path : visited) {
				struct stat st;
				if (stat(path.c_str(), &st) == 0) {
					std::fprintf(f, "%llu %lld %s\n", (unsigned long long)st.st_size, (long long)st.st_mtime, path.c_str());
				}
			}
			for (const auto &s : seen) {
				if (!visited.count(s.first)) {
					std::fprintf(f, "%llu %lld %s\n", (unsigned long long)s.second.first, (long long)s.second.second, s.first.c_str());
				}
			}
			std::fclose(f);
			std::rename(tmp.c_str(), cachePath.c_str());
		}
	}
	return r;
}

} // namespace resign
} // namespace ogbox
