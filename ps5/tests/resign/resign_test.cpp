/*
 * OGBox 1.0.2 - the save re-signer (src/ogbox_resign.cpp) on the PC: its crypto against known
 * answers, the keys read from dumps made here as a console's are (test EEPROM keys: no real key is
 * needed or kept), and a hard disk of saves in every layout it looks for, re-signed and checked.
 */
#include "ogbox_resign.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

using namespace ogbox::resign;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static std::vector<uint8_t> bytes(size_t n, uint32_t seed)
{
	std::vector<uint8_t> v(n);
	for (size_t i = 0; i < n; i++) {
		seed = seed * 1664525u + 1013904223u;
		v[i] = (uint8_t)(seed >> 24);
	}
	return v;
}

static std::vector<uint8_t> hmac(const uint8_t *key, size_t kl, const uint8_t *d, size_t n)
{
	std::vector<uint8_t> o(20);
	hmacSha1(key, kl, d, n, o.data());
	return o;
}

static void put(const std::string &path, const std::vector<uint8_t> &d, time_t mtime = 1600000000)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	std::fwrite(d.data(), 1, d.size(), f);
	std::fclose(f);
	struct timeval t[2] = { { mtime, 0 }, { mtime, 0 } };
	utimes(path.c_str(), t);
}

static std::vector<uint8_t> get(const std::string &path)
{
	std::vector<uint8_t> d;
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) {
		return d;
	}
	int c;
	while ((c = std::fgetc(f)) != EOF) {
		d.push_back((uint8_t)c);
	}
	std::fclose(f);
	return d;
}

static time_t mtimeOf(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}

static void mkdirs(const std::string &p)
{
	std::string at;
	for (size_t i = 0; i <= p.size(); i++) {
		if (i == p.size() || p[i] == '/') {
			if (!at.empty()) {
				mkdir(at.c_str(), 0755);
			}
		}
		if (i < p.size()) {
			at += p[i];
		}
	}
}

/* a test EEPROM key's midstates, as the table's entries are */
static Midstates midsOf(const uint8_t key[16])
{
	Midstates m{ "test", {}, {} };
	innerMidstate(key, 16, m.inner);
	outerMidstate(key, 16, m.outer);
	return m;
}

/* HMAC under midstates, as the kernel's sec_hmac */
static void hmacMid(const Midstates &k, const uint8_t *msg, size_t n, uint8_t out[20]);

/* an EEPROM image whose section is encrypted under k (ex.c's sec_encrypt) */
static std::vector<uint8_t> eepromImage(const Midstates &k, const uint8_t hd[16], uint32_t seed)
{
	std::vector<uint8_t> e = bytes(256, seed);
	uint8_t plain[0x1C];
	std::memcpy(plain, e.data() + 0x14, 8);   // confounder
	std::memcpy(plain + 8, hd, 16);
	std::memset(plain + 24, 0, 4);
	plain[24] = 1;                              // region
	uint8_t hash[20], seedKey[20];
	hmacMid(k, plain, sizeof(plain), hash);
	hmacMid(k, hash, 20, seedKey);
	std::memcpy(e.data(), hash, 20);
	std::memcpy(e.data() + 20, plain, sizeof(plain));
	rc4(seedKey, 20, e.data() + 20, sizeof(plain));
	return e;
}

/* (SHA-1 from a midstate: the test's own, from the HMAC definition, over the midstates' key) */
static const uint8_t *g_midKey;
static void hmacMid(const Midstates &k, const uint8_t *msg, size_t n, uint8_t out[20])
{
	(void)k;   // the test's midstates are of g_midKey: HMAC with it is the same thing
	hmacSha1(g_midKey, 16, msg, n, out);
}

int main()
{
	/* RFC 2202's HMAC-SHA1 cases 1 and 6 (a key longer than a block) */
	{
		uint8_t k[20];
		std::memset(k, 0x0b, 20);
		const std::vector<uint8_t> h = hmac(k, 20, (const uint8_t *)"Hi There", 8);
		static const uint8_t want[20] = { 0xb6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64, 0xe2, 0x8b, 0xc0, 0xb6, 0xfb, 0x37, 0x8c, 0x8e, 0xf1, 0x46, 0xbe, 0x00 };
		CHECK(std::memcmp(h.data(), want, 20) == 0);
		uint8_t big[80];
		std::memset(big, 0xaa, 80);
		const char *msg = "Test Using Larger Than Block-Size Key - Hash Key First";
		const std::vector<uint8_t> h6 = hmac(big, 80, (const uint8_t *)msg, std::strlen(msg));
		static const uint8_t want6[20] = { 0xaa, 0x4a, 0xe5, 0xe1, 0x52, 0x72, 0xd0, 0x0e, 0x95, 0x70, 0x56, 0x37, 0xce, 0x8a, 0x3b, 0x55, 0xed, 0x40, 0x21, 0x12 };
		CHECK(std::memcmp(h6.data(), want6, 20) == 0);
		/* a 1000-byte message, across blocks */
		const std::vector<uint8_t> m = bytes(1000, 7);
		CHECK(hmac(k, 16, m.data(), m.size()) != hmac(k, 16, m.data(), m.size() - 1));
	}
	/* RC4: its own inverse, and RFC 6229's first key ("\x01\x02\x03\x04\x05", the first bytes) */
	{
		uint8_t key[5] = { 1, 2, 3, 4, 5 }, z[8] = {};
		rc4(key, 5, z, 8);
		static const uint8_t want[8] = { 0xb2, 0x39, 0x63, 0x05, 0xf0, 0x3d, 0xc0, 0x27 };
		CHECK(std::memcmp(z, want, 8) == 0);
	}
	/* the table's 1.0 key: its midstates are a key's (the inner and outer of the same 16 bytes are
	 * not checkable here without it; their count and names are) */
	CHECK(eepromKeys().size() == 4);

	/* the certificate key from a BIOS made here: a 2BL with a test EEPROM key and the certificate
	 * key after it, RC4 under the MCPX ROM's key */
	const std::vector<uint8_t> eepromKey = bytes(16, 11), certKey = bytes(16, 12);
	g_midKey = eepromKey.data();
	const Midstates test = midsOf(eepromKey.data());
	{
		std::vector<uint8_t> mcpx = bytes(512, 13), flash = bytes(0x100000, 14);
		std::vector<uint8_t> bl = bytes(0x6000, 15);
		std::memcpy(bl.data() + 0x1234, eepromKey.data(), 16);
		std::memcpy(bl.data() + 0x1244, certKey.data(), 16);
		rc4(mcpx.data() + 0x1A5, 16, bl.data(), bl.size());
		std::memcpy(flash.data() + flash.size() - 0x200 - 0x6000, bl.data(), bl.size());
		uint8_t got[16] = {};
		CHECK(certKeyFrom(mcpx, flash, got, { test }));
		CHECK(std::memcmp(got, certKey.data(), 16) == 0);
		CHECK(!certKeyFrom(mcpx, flash, got));                 // under the real keys' midstates: none
		CHECK(!certKeyFrom(std::vector<uint8_t>(511), flash, got, { test }));
	}
	/* the HD key from an EEPROM image encrypted as the kernel does it */
	const std::vector<uint8_t> srcHd = bytes(16, 21);
	{
		const std::vector<uint8_t> e = eepromImage(test, srcHd.data(), 22);
		uint8_t got[16] = {};
		CHECK(hdKeyFrom(e, got, { test }));
		CHECK(std::memcmp(got, srcHd.data(), 16) == 0);
		CHECK(!hdKeyFrom(bytes(256, 23), got, { test }));
	}

	char tmpl[] = "/tmp/ogbox-resign-XXXXXX";
	const std::string root = mkdtemp(tmpl);
	/* the native HD key: none yet (every console's first), an image kept in the clear */
	{
		uint8_t hd[16];
		nativeHdKey(root, hd);
		CHECK(std::memcmp(hd, "0000000001\x00\x50\xF2\x00\x00\x01", 16) == 0);
		std::vector<uint8_t> clear = bytes(256, 31);
		put(root + "/eeprom.bin", clear);
		nativeHdKey(root, hd);
		CHECK(std::memcmp(hd, clear.data() + 0x36, 16) == 0);
		unlink((root + "/eeprom.bin").c_str());
	}
	/* the certificate's keys: its own, two alternates (one its own ID again, one 0) */
	{
		std::vector<uint8_t> cert = bytes(0x1D0, 41);
		const uint32_t own = 0x4D530039, alt = 0x4D53000F;
		std::memcpy(cert.data() + 8, &own, 4);
		std::memset(cert.data() + 0x5C, 0, 64);
		std::memcpy(cert.data() + 0x5C, &alt, 4);
		std::memcpy(cert.data() + 0x60, &own, 4);
		const std::vector<TitleKey> k = certificateKeys(cert.data(), cert.size());
		CHECK(k.size() == 2 && k[0].titleId == own && k[1].titleId == alt);
		CHECK(std::memcmp(k[0].key, cert.data() + 0xC0, 16) == 0 && std::memcmp(k[1].key, cert.data() + 0xD0, 16) == 0);
		CHECK(certificateKeys(cert.data(), 0x100).empty());
	}
	/* hdkey.txt */
	{
		const std::string d = root + "/dumps";
		mkdirs(d);
		put(d + "/hdkey.txt", std::vector<uint8_t>((const uint8_t *)"00112233 44556677-8899aabbccddeeff\n",
			(const uint8_t *)"00112233 44556677-8899aabbccddeeff\n" + 35));
		const SourceKeys s = readSourceKeys(d);
		CHECK(!s.cert && s.hd && s.hdKey[0] == 0x00 && s.hdKey[1] == 0x11 && s.hdKey[15] == 0xff);
		put(d + "/hdkey.txt", std::vector<uint8_t>(31, '1'));
		CHECK(!readSourceKeys(d).hd);
	}

	/* a hard disk of a console's saves */
	const uint32_t tid = 0x4D530039;
	const std::vector<uint8_t> titleKey = bytes(16, 51);
	TitleKey tk;
	tk.titleId = tid;
	std::memcpy(tk.key, titleKey.data(), 16);
	const std::vector<uint8_t> authFull = hmac(certKey.data(), 16, titleKey.data(), 16);
	const uint8_t *auth = authFull.data();   // the title's key on the source console (16 of its bytes)
	static const uint8_t zero[16] = {};
	const std::string hdd = root + "/hdd", save = hdd + "/Partition1/UDATA/4d530039/save1",
		data = hdd + "/Partition1/TDATA/4d530039", dlc = data + "/$c/4d530039deadbeef";
	mkdirs(save);
	mkdirs(dlc);
	const std::vector<uint8_t> nativeHd(srcHd.rbegin(), srcHd.rend());   // this console's, another
	put(hdd + "/eeprom.bin", eepromImage(test, nativeHd.data(), 61));     // (read with the test key below)

	/* a: the signature at the end, over the rest */
	std::vector<uint8_t> a = bytes(300, 71);
	{
		const std::vector<uint8_t> s = hmac(auth, 16, a.data(), a.size());
		a.insert(a.end(), s.begin(), s.end());
		put(save + "/a.dat", a);
	}
	/* b: locked to the console, at the start, over the rest */
	std::vector<uint8_t> b = bytes(500, 72);
	{
		const std::vector<uint8_t> r = hmac(auth, 16, b.data() + 20, b.size() - 20);
		const std::vector<uint8_t> s = hmac(srcHd.data(), 16, r.data(), 20);
		std::memcpy(b.data(), s.data(), 20);
		put(save + "/b.dat", b);
	}
	/* c: a 4 KiB file of a fixed size: data, its signature, zeros */
	std::vector<uint8_t> c = bytes(900, 73);
	{
		const std::vector<uint8_t> s = hmac(auth, 16, c.data(), c.size());
		c.insert(c.end(), s.begin(), s.end());
		c.resize(4096, 0);
		put(save + "/Profile.bin", c);
	}
	/* d: its signature in a .sig file of its own, which signs itself over it too */
	std::vector<uint8_t> d = bytes(5000, 74), sig;
	{
		const std::vector<uint8_t> s = hmac(auth, 16, d.data(), d.size());
		sig = s;
		sig.resize(40);
		const std::vector<uint8_t> s2 = hmac(auth, 16, sig.data(), 20);   // over the other's
		std::memcpy(sig.data() + 20, s2.data(), 20);
		put(save + "/d.dat", d);
		put(save + "/d.sig", sig);
	}
	/* e: unsigned; f: the save's metadata; g: already this console's */
	const std::vector<uint8_t> e = bytes(256, 75), f = bytes(256, 76);
	put(save + "/e.dat", e);
	put(save + "/SaveMeta.xbx", f);
	std::vector<uint8_t> g = bytes(200, 77);
	{
		const std::vector<uint8_t> s = hmac(zero, 16, g.data(), g.size());
		g.insert(g.end(), s.begin(), s.end());
		put(save + "/g.dat", g);
	}
	/* h: downloaded content's ContentMeta.xbx */
	std::vector<uint8_t> h = bytes(0x200, 78);
	{
		const uint32_t size = 0x100;
		std::memcpy(h.data() + 0x18, &size, 4);
		std::memcpy(h.data() + 0x24, &tid, 4);
		const std::vector<uint8_t> k = hmac(srcHd.data(), 16, h.data() + 0x24, 4);
		const std::vector<uint8_t> s = hmac(k.data(), 20, h.data() + 0x14, size - 0x14);
		std::memcpy(h.data(), s.data(), 20);
		put(dlc + "/ContentMeta.xbx", h);
	}

	SourceKeys src;
	src.cert = src.hd = true;
	std::memcpy(src.certKey, certKey.data(), 16);
	std::memcpy(src.hdKey, srcHd.data(), 16);
	uint8_t native[16];
	{
		/* nativeHdKey reads <hdd>/eeprom.bin under the real keys: the test's image is under the test
		 * key, so it is read as kept in the clear (bytes 0x36-0x45); hdKeyFrom reads it with it */
		uint8_t viaTest[16];
		CHECK(hdKeyFrom(get(hdd + "/eeprom.bin"), viaTest, { test }) && std::memcmp(viaTest, nativeHd.data(), 16) == 0);
		std::memcpy(native, nativeHd.data(), 16);
	}
	const std::string cache = root + "/checked.txt";
	Report r = resignTitles(hdd, { tk }, src, native, cache);
	std::printf("resign: %d re-signed, %d read, %d skipped, %zu left:", r.resigned, r.checked, r.skipped, r.unsigned_.size());
	for (const std::string &u : r.unsigned_) {
		std::printf(" %s", u.c_str());
	}
	std::printf("\n");
	CHECK(r.resigned == 6);   // a, b, c, d (in d.sig), d.sig itself, h
	CHECK(r.unsigned_.size() == 2);   // e, g
	/* each now as this console signs it */
	{
		std::vector<uint8_t> want = a;
		const std::vector<uint8_t> s = hmac(zero, 16, a.data(), 300);
		std::memcpy(want.data() + 300, s.data(), 20);
		CHECK(get(save + "/a.dat") == want);
	}
	{
		std::vector<uint8_t> want = b;
		const std::vector<uint8_t> r1 = hmac(zero, 16, b.data() + 20, b.size() - 20);
		const std::vector<uint8_t> s = hmac(native, 16, r1.data(), 20);
		std::memcpy(want.data(), s.data(), 20);
		CHECK(get(save + "/b.dat") == want);
	}
	{
		std::vector<uint8_t> want = c;
		const std::vector<uint8_t> s = hmac(zero, 16, c.data(), 900);
		std::memcpy(want.data() + 900, s.data(), 20);
		CHECK(get(save + "/Profile.bin") == want);
	}
	{
		CHECK(get(save + "/d.dat") == d);
		std::vector<uint8_t> want(40);
		const std::vector<uint8_t> s = hmac(zero, 16, d.data(), d.size());
		std::memcpy(want.data(), s.data(), 20);
		const std::vector<uint8_t> s2 = hmac(zero, 16, want.data(), 20);
		std::memcpy(want.data() + 20, s2.data(), 20);
		CHECK(get(save + "/d.sig") == want);
	}
	CHECK(get(save + "/e.dat") == e && get(save + "/SaveMeta.xbx") == f && get(save + "/g.dat") == g);
	{
		std::vector<uint8_t> want = h;
		const std::vector<uint8_t> k = hmac(native, 16, h.data() + 0x24, 4);
		const std::vector<uint8_t> s = hmac(k.data(), 20, h.data() + 0x14, 0x100 - 0x14);
		std::memcpy(want.data(), s.data(), 20);
		CHECK(get(dlc + "/ContentMeta.xbx") == want);
	}
	CHECK(mtimeOf(save + "/a.dat") == 1600000000 && mtimeOf(save + "/d.sig") == 1600000000);   // dates kept
	/* again: everything unchanged since is skipped; without the cache, nothing matches the console now */
	r = resignTitles(hdd, { tk }, src, native, cache);
	CHECK(r.resigned == 0 && r.checked == 0 && r.skipped == 8);
	r = resignTitles(hdd, { tk }, src, native, std::string());
	CHECK(r.resigned == 0 && r.checked == 8 && r.unsigned_.size() == 8);
	/* other keys: the cache doesn't count */
	src.hdKey[0] ^= 1;
	r = resignTitles(hdd, { tk }, src, native, cache);
	CHECK(r.checked == 8 && r.skipped == 0);
	/* no keys: nothing read */
	SourceKeys none;
	r = resignTitles(hdd, { tk }, none, native, cache);
	CHECK(r.checked == 0 && r.resigned == 0);

	std::string rm = "rm -rf '" + root + "'";
	if (std::system(rm.c_str()) != 0) {
		std::printf("(the test's folder %s stays)\n", root.c_str());
	}
	std::printf(g_fail ? "resign: %d failed\n" : "resign: ok\n", g_fail);
	return g_fail ? 1 : 0;
}
