// OGBox - the PC test of the game fixes fetched at start (src/ogbox_fixes_fetch.cpp).
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
	system(("rm -rf '" + dir + "' && mkdir -p '" + title + "/mods/4D530039.1/old' '" + title + "/gamedata'").c_str());
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

	fprintf(stderr, g_fail ? "fixes: %d failed\n" : "fixes: all passed\n", g_fail);
	printf(g_fail ? "fixes: FAILED\n" : "fixes: passed\n");
	return g_fail ? 1 : 0;
}
