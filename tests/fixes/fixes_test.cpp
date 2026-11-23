// OGBox - the PC test of the game fixes fetched at start and the cheats fetched at the user's asking (src/ogbox_fixes_fetch.cpp).
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#include "ogbox_fixes_fetch.h"
#include "platform/ps5_update.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>

using namespace ogbox::fixes;

static int g_fail = 0;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); g_fail++; } } while (0)

static std::map<std::string, std::string> server;   // url -> body
static int requests = 0;
static int Get(const std::string& url, std::vector<uint8_t>& out)
{
	requests++;
	const auto it = server.find(url);
	if (it == server.end())
		return 404;
	out.assign(it->second.begin(), it->second.end());
	return 200;
}
static std::string Line(const std::string& path, const std::string& body)
{
	return PS5Update::Sha256Hex(body.data(), body.size()) + " " + std::to_string(body.size()) + " " + path + "\n";
}
static std::string Read(const std::string& path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return "<none>";
	std::stringstream s;
	s << f.rdbuf();
	return s.str();
}
static bool Exists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;
	const std::string dir = argv[1], title = dir + "/title", record = dir + "/fetched.txt";
	const std::string B = "https://example/ps5/";
	if (system(("rm -rf '" + dir + "' && mkdir -p '" + title + "/mods/4D530039.1/old' '" + title + "/gamedata'").c_str()) != 0)
		return 2;   // the test's folders could not be made
	{ std::ofstream(title + "/gamedata/patches.db") << "# db\nP\t4D530039\t1\tpa\tFix A\tMisc\n"; }
	{ std::ofstream(title + "/mods/4D530039.1/old/mod.txt") << "name: Shipped\n"; }

	const std::string modA = "name: Steady trees\ngamefix: true\n", modB = "name: 60 FPS\n";
	const std::string db = "# db\nP\t4D530039\t1\tpa\tFix A\tMisc\nP\t45410049\t1\tpb\tFix B\tMisc\n";
	std::string list = "# OGBox game fixes 1\n" + Line("mods/4D530039.1/trees/mod.txt", modA) +
		Line("mods/45410049.1/60fps/mod.txt", modB) + Line("gamedata/patches.db", db) +
		Line("mods/../../escape.txt", "x") + Line("eboot.bin", "x") + Line("mods/.hidden/a/b", "x");
	server[B + "game-fixes.txt"] = list;
	server[B + "mods/4D530039.1/trees/mod.txt"] = modA;
	server[B + "mods/45410049.1/60fps/mod.txt"] = modB;
	server[B + "gamedata/patches.db"] = db;
	server[B + "eboot.bin"] = "x";

	// 1: everything new is fetched, the unsafe lines are refused, the notice names the games
	Report r;
	CHECK(Sync(B, title, record, Get, r));
	CHECK(r.fetched == 3 && r.removed == 0 && r.failed == 0);
	CHECK(Read(title + "/mods/4D530039.1/trees/mod.txt") == modA);
	CHECK(Read(title + "/gamedata/patches.db") == db);
	CHECK(!Exists(title + "/eboot.bin") && !Exists(dir + "/escape.txt"));
	CHECK(Read(title + "/mods/4D530039.1/old/mod.txt") == "name: Shipped\n");
	bool trees = false, b = false;
	for (const Fixed& f : r.fixed)
	{
		for (const std::string& w : f.what)
			fprintf(stderr, "  notice: %s (%08X): %s\n", f.key.c_str(), f.titleId, w.c_str());
		if (f.key == "4D530039.1" && f.titleId == 0x4D530039 && f.what.size() == 1 && f.what[0] == "Steady trees")
			trees = true;
		if (f.key == "45410049" && f.what.size() == 1 && f.what[0] == "Fix B patch")
			b = true;   // only the patch that changed
	}
	CHECK(trees && b);

	// 2: nothing changed: only the list is asked for
	requests = 0;
	CHECK(Sync(B, title, record, Get, r));
	CHECK(r.fetched == 0 && r.fixed.empty() && requests == 1);

	// 3: a file whose bytes don't match the list is not written; the 60fps mod, fetched before and no
	//    longer listed (withdrawn), is removed with its empty folders; the shipped mod, never fetched, stays
	const std::string modA2 = "name: Steady trees\ngamefix: true\n# v2\n";
	server[B + "game-fixes.txt"] = "# OGBox game fixes 1\n" + Line("mods/4D530039.1/trees/mod.txt", modA2) +
		Line("gamedata/patches.db", db);
	server[B + "mods/4D530039.1/trees/mod.txt"] = modA2 + "tampered";
	CHECK(!Sync(B, title, record, Get, r));
	CHECK(r.failed == 1 && r.removed == 1 && Read(title + "/mods/4D530039.1/trees/mod.txt") == modA);
	CHECK(!Exists(title + "/mods/4D530039.1/trees/mod.txt.part"));
	CHECK(!Exists(title + "/mods/45410049.1"));

	// 4: corrected: fetched
	server[B + "mods/4D530039.1/trees/mod.txt"] = modA2;
	CHECK(Sync(B, title, record, Get, r));
	CHECK(r.fetched == 1 && r.removed == 0);
	CHECK(Read(title + "/mods/4D530039.1/trees/mod.txt") == modA2);
	CHECK(Exists(title + "/mods/4D530039.1/old/mod.txt"));
	CHECK(Read(record).find("45410049") == std::string::npos);

	// 5: a list of another kind, or no network, changes nothing
	server[B + "game-fixes.txt"] = "# OGBox game fixes 2\n";
	CHECK(!Sync(B, title, record, Get, r) && r.fetched == 0);
	server.erase(B + "game-fixes.txt");
	CHECK(!Sync(B, title, record, Get, r) && !r.error.empty());
	CHECK(Read(title + "/mods/4D530039.1/trees/mod.txt") == modA2);

	// 6 (1.0.3): the cheats, at the user's asking: the index's files fetched into the trainers folder
	//   by their URL-encoded names, each checked; a file already there kept as it is
	{
		const std::string R = "https://raw.example/xbased/", C = "0123456789abcdef0123456789abcdef01234567";
		const std::string tr = dir + "/trainers", index = dir + "/cheats-xbased.txt";
		const std::string a = "ETM a", b = "ETM b", c = "ETM c", d = "ETM d", mine = "the user's own";
		if (system(("mkdir -p '" + tr + "/4D530039'").c_str()) != 0)
			return 2;
		{ std::ofstream(tr + "/4D530039/Mine [plus3].etm") << mine; }
		{ std::ofstream(tr + "/4D530039/Same.ETM") << d; }
		{
			std::ofstream(index) << "# OGBox cheats 1 crazyriddler/xbased " << C << "\n# a comment\n" <<
				Line("cheats/4D530039/Name [plus5] (v1).etm", a) + Line("cheats/45410049/b.xbtf", b) +
				Line("cheats/45410049/tampered.etm", c) + Line("cheats/4D530039/Mine [plus3].etm", "xbased's") +
				Line("cheats/4D530039/Same.ETM", d) + Line("cheats/4D530039/../../escape.etm", "x") +
				Line("cheats/4D530039/v0..etm", "x") + Line("cheats/4d530039/lower.etm", "x") +
				Line("cheats/4D530039/a:b.etm", "x") + Line("cheats/4D530039/readme.txt", "x") +
				Line("cheats/4D530039/.hidden.etm", "x") + Line("cheats/4D530039/tab\t.etm", "x") +
				Line("mods/4D530039.1/x/mod.txt", "x");
		}
		server.clear();
		server[R + C + "/cheats/4D530039/Name%20%5Bplus5%5D%20%28v1%29.etm"] = a;
		server[R + C + "/cheats/45410049/b.xbtf"] = b;
		server[R + C + "/cheats/45410049/tampered.etm"] = c + "!";
		server[R + C + "/cheats/4D530039/Mine%20%5Bplus3%5D.etm"] = "xbased's";
		server[R + C + "/cheats/4D530039/v0..etm"] = "x";
		requests = 0;
		unsigned lastDone = 0, lastTotal = 0;
		CheatsReport cr;
		CHECK(!Cheats(index, R, tr, Get, cr, [&](unsigned dn, unsigned tot) { lastDone = dn; lastTotal = tot; }));
		fprintf(stderr, "  cheats: %u listed, %u fetched, %u there, %u kept, %u failed: %s\n", cr.listed, cr.fetched, cr.present,
			cr.kept, cr.failed, cr.error.c_str());
		CHECK(cr.listed == 5 && cr.fetched == 2 && cr.present == 1 && cr.kept == 1 && cr.failed == 1);
		CHECK(lastDone == 5 && lastTotal == 5 && requests == 3);
		CHECK(Read(tr + "/4D530039/Name [plus5] (v1).etm") == a && Read(tr + "/45410049/b.xbtf") == b);
		CHECK(!Exists(tr + "/45410049/tampered.etm") && !Exists(tr + "/45410049/tampered.etm.part"));
		CHECK(Read(tr + "/4D530039/Mine [plus3].etm") == mine && Read(tr + "/4D530039/Same.ETM") == d);
		CHECK(!Exists(dir + "/escape.etm") && !Exists(tr + "/4D530039/v0..etm") && !Exists(tr + "/mods"));

		// again, corrected: only the missing one is asked for
		server[R + C + "/cheats/45410049/tampered.etm"] = c;
		requests = 0;
		CHECK(Cheats(index, R, tr, Get, cr, nullptr));
		CHECK(cr.fetched == 1 && cr.present == 3 && cr.kept == 1 && cr.failed == 0 && requests == 1);

		// no network: each missing one failed, with a sentence why; an index of another kind or none: nothing asked
		if (system(("rm -rf '" + tr + "/45410049'").c_str()) != 0)
			return 2;
		requests = 0;
		const Getter offline = [](const std::string&, std::vector<uint8_t>&) { requests++; return -1; };
		CHECK(!Cheats(index, R, tr, offline, cr, nullptr) && cr.failed == 2 && !cr.error.empty() && requests == 2);
		{ std::ofstream(index) << "# OGBox cheats 2 crazyriddler/xbased " << C << "\n" << Line("cheats/45410049/b.xbtf", b); }
		requests = 0;
		CHECK(!Cheats(index, R, tr, Get, cr, nullptr) && !cr.error.empty() && requests == 0);
		CHECK(!Cheats(dir + "/none.txt", R, tr, Get, cr, nullptr) && !cr.error.empty() && requests == 0);

		// the names: as the import tool allows them
		CHECK(SafeCheatPath("cheats/4D530039/Name [plus4] (USA)(v1).etm") && SafeCheatPath("cheats/4D530039/X.XBTF"));
		CHECK(!SafeCheatPath("cheats/4D530039/") && !SafeCheatPath("cheats/4D530039/a/b.etm") && !SafeCheatPath("cheats/4D53003/a.etm"));
		CHECK(!SafeCheatPath("cheats/4D530039/a.etm ") && !SafeCheatPath("cheats/4D530039/ a.etm") && !SafeCheatPath("cheats/4D530039/a\\b.etm"));
		// the shipped index (the run script passes it): every line of it read
		if (argc > 2)
		{
			std::ifstream f(argv[2]);
			unsigned lines = 0;
			for (std::string l; std::getline(f, l);)
				lines += !l.empty() && l[0] != '#';
			const Getter none = [](const std::string&, std::vector<uint8_t>&) { return 404; };
			CHECK(!Cheats(argv[2], R, dir + "/shipped", none, cr, nullptr) && cr.listed == lines && lines > 0);
			fprintf(stderr, "  cheats: the shipped index lists %u files, %u read\n", lines, cr.listed);
		}
		CHECK(!SafeCheatPath("cheats/4D530039/a\x7f.etm") && !SafeCheatPath("cheats/4D530039/\xc3\xa9.etm") && !SafeCheatPath("cheats/4D530039/a.etm.part"));
	}

	fprintf(stderr, g_fail ? "fixes: %d failed\n" : "fixes: all passed\n", g_fail);
	printf(g_fail ? "fixes: FAILED\n" : "fixes: passed\n");
	return g_fail ? 1 : 0;
}
