/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.45: what the game list knows of a disc besides its file: the Redump database the title
 * ships (gamedata/games.db, from libretro-database), its cover from libretro's thumbnails (downloaded
 * on a thread of its own into /data/ogbox/covers, then kept), and the games played.
 */
#ifndef OGBOX_LIBRARY_H
#define OGBOX_LIBRARY_H

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ogbox {

struct DiscRecord {
	uint32_t titleId{ 0 };
	std::string region, languages, serial, name;   // name: Redump's (and libretro's thumbnails')
};

/* gamedata/games.db, read once (from any thread). */
class GameDb {
public:
	static GameDb &get();
	/* The disc a file is (by its name without the extension, as Redump names it), else nullptr. */
	const DiscRecord *byFileName(const std::string &fileName) const;
	/* A disc of this title ID: the one the file name is, else one of the regions (Xbox certificate
	 * bits: 1 North America, 2 Japan, 4 the rest), else the first. */
	const DiscRecord *byTitle(uint32_t titleId, const std::string &fileName, uint32_t regions) const;
	/* 0.0.2: every disc of this title ID, byTitle's first (the file's, else the regions'). */
	std::vector<const DiscRecord *> allByTitle(uint32_t titleId, const std::string &fileName, uint32_t regions) const;
	size_t size() const { return records.size(); }

private:
	GameDb();
	std::vector<DiscRecord> records;
	/* 0.0.3: the records by their lower-case name. byFileName was a walk of all 2553 a call, and
	 * the home looked up every game of a folder each frame (980 on a share: 5 fps). */
	std::unordered_map<std::string, size_t> byName;
};

/* A file name without its extension, and without its bracketed parts ("Halo 2 (USA) (En,Fr)": "Halo 2"). */
std::string stemOf(const std::string &fileName);
std::string shortName(const std::string &fileName);

/* 0.0.2: the names a disc's cover may go by on libretro, best first: the database's record for the
 * file's name, the file's own name, then (title ID known: titleId nonzero) every record of its title
 * ID, the regions' first. Up to 0.0.1 the title ID's record replaced the file's name once the
 * details had read the disc, and where libretro names the cover after the file's release and not
 * that record's (a title ID several releases share), the cover went. */
std::vector<std::string> coverNames(const std::string &fileName, uint32_t titleId = 0, uint32_t regions = 0);
/* 0.0.3: with the cover's other sources, best first: libretro's by name (above), then MobCat's scan
 * of the disc's box ("@mc:<stem>": by the serial the title ID makes and the release's version and
 * regions when known, else the record's region), then the disc's own title image from its
 * default.xbe ("@xbe:<title ID>|<disc path>", path known: read only when no other has one). */
std::vector<std::string> coverSources(const std::string &fileName, const std::string &path, uint32_t titleId = 0,
	uint32_t regions = 0, uint32_t version = 0);
/* 0.0.3: MobCat's scan for a title ("" none): its stem in gamedata/covers-mobcat.txt. */
std::string mobcatCover(uint32_t titleId, const std::string &serial, uint32_t version, uint32_t regions);

/* 0.0.3: one reader of a share's discs at a time (the game list's details, the title images) */
std::mutex &discMutex();

/* The covers. */
class Covers {
public:
	enum class State { None, Queued, Ready, Missing };
	static Covers &get();
	/* Turn downloads on or off (the settings' Game list > Covers). */
	void enable(bool on);
	/* The cover of a Redump name: Ready with its PNG's path, else queued for download (when on). */
	State find(const std::string &name, std::string &path);
	/* 0.0.2: the first of names with a cover: Ready with its path, else the first one libretro may
	 * still have (queued: the next is asked only once that one is known missing), Missing when none
	 * has one. */
	State findAny(const std::vector<std::string> &names, std::string &path);
	/* What the downloads have done ("12 downloaded, 3 not on libretro"), for the details. */
	std::string status() const;
	/* 0.0.3: the downloads of now: how many of how many asked since the queue was last empty (the
	 * home's bar); total 0 when none is waiting. */
	void progress(unsigned &done, unsigned &total) const;

private:
	Covers();
	struct Impl;
	Impl *impl;
};

/* 0.0.3: a game's description, the first paragraph of its English Wikipedia article (found by a
 * search for its name, "video game" in the article's description), downloaded on a thread of its own
 * and kept in the covers' folder (about-<name>.txt; about-<name>.none when there is none). */
class About {
public:
	static About &get();
	/* The description of a game by its name: true with it (and its article's title) when kept; else
	 * asked for (when on) and false. */
	bool find(const std::string &name, std::string &text, std::string &article);
	void enable(bool on);

private:
	About();
	struct Impl;
	Impl *impl;
};

/* The games played, newest first (/data/ogbox/played.txt): a disc's path and when (seconds). */
void notePlayed(const std::string &path);
double playedAt(const std::string &path);   // 0: never
/* 0.0.3: the games played last, newest first (their paths and when), at most n */
std::vector<std::pair<double, std::string>> recentlyPlayed(size_t n);

} // namespace ogbox

#endif
