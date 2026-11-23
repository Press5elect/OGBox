# Patches, mods and game fixes

Three ways a single game's behaviour is changed. All are checked against the game's exact release.

| Kind | Where | On by | Use for |
| --- | --- | --- | --- |
| **XBE patch** | `ps5/gamedata/patches.db`, built by `ps5/tools/import-gamedata.py` from imported collections and OGBox's own `ps5/tools/gamedata-own-patches.txt` | The player (game page > Game Patches) | Optional changes: 60 FPS, widescreen, video modes |
| **Mod** | `ps5/mods/<title ID>.<version>/<mod>/mod.txt` (`ps5/mods/README.md` has the format) | The player | Larger optional changes, with files or disc-file edits |
| **Game fix** | A mod with `gamefix: true` | Always on, shown greyed | Something the game needs to run properly on the PS5 |

Prefer fixing the emulator when the game relies on hardware behaviour hakuX gets wrong: it helps
every game that does the same. A game fix is for what is specific to one game's code under
emulation (a timing assumption, an unchecked buffer).

## Reaching consoles without a new build

OGBox fetches the game fixes main has each time it starts (Settings > Updates, "Download game
fixes at start", on by default; `ps5/src/ogbox_fixes_fetch.cpp`). `ps5/game-fixes.txt` lists
every file of `ps5/mods/` and of `ps5/gamedata/` (`patches.db`, `files/`) with its SHA-256 and
size; a console fetches the files that differ from its title folder from
`raw.githubusercontent.com/Press5elect/OGBox/main/ps5/<path>`, checks each against the list,
and tells the player which games got fixes. A mod file it fetched that the list no longer has is
removed (a withdrawn fix); files it didn't fetch are never removed.

- After any change under `ps5/mods/` or `ps5/gamedata/`, run `ps5/tools/game-fixes-index.py`
  and commit `ps5/game-fixes.txt` with it (the PC tests fail when it isn't current).
- A fix reaches consoles when it is merged to main: that is a publication, so it needs the
  owner's word like any push. Check it on the console with a test build first.
- Only data travels this way. A fix that needs an emulator change still needs a build.

## The patch database

- Records: `P` (title ID, version or `*`, id, category, name, author, note, source), `C` (the
  CRC-32 of the original file: one to three, a patch may be for several releases whose code is the
  same at its sites), `T` (target file), `F` (find bytes → replace bytes, same length), `O`
  (offset → bytes), and others the loader documents (`ps5/src/core/ogbox_patches.c`).
- OGBox's own patches are appended after the imported ones, comments stripped. After editing
  `gamedata-own-patches.txt`, rebuild the database with `import-gamedata.py` (or append the
  non-comment lines to `patches.db` and check its tail equals the own-patches file's records).
- An imported patch that one of OGBox's replaces is listed in `SUPERSEDED` in
  `import-gamedata.py`, so it doesn't show beside it.
- Each `F` find must occur exactly once in the original file. The loader doesn't check it for an
  `F` record (it writes the first place the bytes are); an `N` record (find, count in hex: the
  bytes are in the file exactly that many times as it is at that record) does, and fails the patch
  otherwise.
- OgXHD's widescreen database (`wdb.dat`; xbased's `mods/widescreen.dat` is the same file) is made
  of patterns for any release. 0.1.52's merge folded most of its entries into a PatchHub patch that
  writes the same bytes, which for 30 games is made for one release only (offsets, or longer
  patterns of that build). `import-gamedata.py` (1.0.4, `wdb_any_release`) adds those 30 again as
  **Widescreen (any release)** by OgXHD: version `*`, no CRC, no region, an `N` record for each
  find (a find listed k times must be there k times) and its `F` records. A game whose database
  entry is already in `patches.db` as is for any release gets none. Without the other sources,
  `import-gamedata.py --out ps5/gamedata --any-release-only --wdb-any <file> [--wdb-via xbased]`
  makes only these again and keeps the rest of `patches.db` line for line. A console on a build
  before 1.0.4 doesn't know `N`: it shows these greyed ("a record it cannot read") and never
  applies them.
- A record the loader can't read (an unknown kind, a missing field, a number that isn't whole hex,
  a file name with a `..` part or a leading `/`, over 8191 characters) fails its whole patch, which
  shows greyed ("a record it cannot read"). Every shipped record passed this check when it came in.

## The player's own patches (1.0.4)

Players drop `.jmp`, `.ips` and `.bps` files into `<data folder>/patches/` (`storagePath("patches")`,
default `/data/ogbox/patches`), and one level of folders in it. The front end tells the loader
the folder and the game's name (`ogbox_patches_set_user_dir`, `ogbox_patches_set_game_name` in
`ogbox_main.cpp`'s `setUserPatches`: on the Game Patches screen, the details' thread and at the
start); `ps5/src/core/ogbox_patches.c` (`user_load`) reads the folder each time it loads a game's
patches and adds the ones for that game after the database's, as entries of their own.

- **Which game.** A BPS: its source CRC-32 and size are the disc's `default.xbe`'s (known once the
  release check or the start has read it: `ogbox_patches_check`, `ogbox_patches_prepare`). A JMP:
  the `{CRC32}` of its `version=` line is the disc's, or the title ID that line starts with is the
  game's. A file that says neither (an IPS, such a JMP): an 8-hex-digit run in its path that is the
  title ID; the game's name (as its page shows it) as whole words in its path, not followed by a
  sequel's number (`2`, `ii`...), 4 letters at least; or attached by hand (the screen's **Add a
  patch from the patches folder**, `/data/ogbox/user-patches.txt`: `<title ID> <path>` lines,
  `ogbox_patches_attach`). A file with a CRC-32 or title ID of its own is joined only by it and is
  never offered to attach. The details' counts don't know the disc's CRC-32, so they leave out the
  BPS and CRC-only JMP files until the Game Patches screen has read the disc.
- **Records.** A JMP is read as `import-gamedata.py`'s `parse_jmp` reads one (header lines, `#`
  comments naming the target, `[-]` blocks left out, `APPEND:`, `offset: bytes`, find/replace
  pairs) into `C`/`T`/`F`/`O`/`A` records; unlike the importer, a line it can't read refuses the
  file with its line number. An IPS or a BPS is an `I`/`B` record on `default.xbe` with the file's
  own folder as the files folder. A BPS checks its source CRC-32 when applied; an IPS has no check
  (the screen says "release not checked").
- **Ids and limits.** `user:<path in the folder>`; one equal (case aside) to another's is left out,
  so a file can't stand in for a shipped patch. Files over 4 MiB, names that aren't plain
  (printable ASCII but `/ \ : * ? " < > |`, no `..`, no leading dot or space, 100 characters), more
  than 64 for a game or 512 in the folder are left out, and the boot log says so. A renamed or
  moved file is another file: off, and no longer attached.
- **Never written by OGBox.** The game-fixes download writes only the title folder's `mods/` and
  `gamedata/` (`ogbox_fixes_fetch.cpp`), and the updater only the title folder's managed paths
  (`ps5_update.cpp`'s `MANAGED_FOLDERS`/`MANAGED_FILES`, which have no `patches/`), with its
  rollback undoing only those. The data folder's `patches` is one of `storageParts()`, so a move of
  the data folder carries it along.
- **The boot log** at the start: `patches: your patches folder ...:`, then each file left out
  (and why), each file for this game and how it was joined, the count, and the usual
  `applied`/`left out: <why>` line for each one on.
- Tests: `tests/patches/patches_test.c` (`test_user`).

## Writing a patch from the game's code

Work from the XBE (the owner sends `default.xbe`; treat it as untrusted data, never commit it):

1. **Find the cause in the code**, not a value that happens to work. Disassemble (capstone),
   follow who reads and writes the values involved, and model the behaviour (e.g. the steps a
   frame gets at 60 Hz with 1 to 2 ms of timing jitter) to show the problem and the fix in numbers.
2. **Smallest rewrite, in place where it fits.** Reuse constants that already exist in read-only
   data, and list every other reader of a constant before pointing at it.
3. **Verify, every time:**
   - disassembly before and after; the rewrite ends on the original's instruction boundaries;
   - nothing jumps into the rewritten bytes (scan rel8, rel32, jcc and absolute pointers);
   - at every join: the registers, flags, stack and x87/SSE state the following code reads are
     what the original left (a skipped block must leave what it would have set);
   - any field or memory reused is proven unused elsewhere (search every instruction's
     displacement for its offset);
   - applying the patch lines to the original file reproduces the patched copy (compare CRCs).
4. **Stub space** (when code doesn't fit in place) only in a function proven unreachable **as a
   whole**: no byte offset anywhere in the file, decoded as an absolute pointer, a rel32 call or
   jump, a jcc or a rel8 jump, points at any of the function's bytes, its start included. A check
   of only the stub's own range is not enough: a table of function pointers may hold the function's
   start.
5. Keep each mod patch line under about 250 characters (the core reads its manifest in 1024-byte
   lines): split long rewrites into consecutive `0xVA:` lines.
6. Comments in the patch or mod say what the code did and what it does now, with addresses; title
   ID only, never the game's name.

## Kinds of timing fault seen so far

| Fault | Where seen | Fix pattern |
| --- | --- | --- |
| Steps per frame = floor(frame time / step): 0 or 2 steps at 60 with jitter | `4541007B`, `4541009E` | Carry the remainder; skip a step only while under half a step |
| A minimum frame time applied before the carry: the world runs fast | `45410047`, `45410071`, `4541005A` | Take the carry before the minimum, floored at half a step |
| Steps counted from a separate 60 Hz timer's queued samples, not from the frame | `45410049` | Count steps from the game's own clock, remainder carried, half-step tolerance |
| Presentation interval set to two vblanks in game | several 60 FPS patches | ONE or ONE_OR_IMMEDIATE at the interval's own site |

The comment blocks in `gamedata-own-patches.txt` and `ps5/mods/45410049.1/` describe each in
full; read them before writing a similar one.

## A case study: 4D530039's flickering scenery

Its trees, dust and waterfalls came and went. Three game fixes were written and withdrawn
(longer draw distance, list bounds checks, a GPU-fence change) before the cause was found in the
emulator: the game sends `SET_BEGIN_END(prim)` again before each chunk of its inline quads and
points and `END` once; hakuX reset the draw at each `BEGIN`, so only the last chunk was drawn.
The fix keeps the chunks in one draw only for that case (inline quads or points, the same kind
again); every other `BEGIN` resets as before. What would have found it sooner:

- the bug was open in xemu's tracker, and fixed in a later xemu version: look there first;
- it reproduced at 1x resolution with visibility tests forced: an early sign it was not a
  quality or culling setting;
- a video of the problem, stepped frame by frame, showed an on/off switch of a whole effect;
- no draw of the affected programs ever exceeded one chunk (31 quads, 62 billboards) in any log.

The withdrawn fixes found real properties of the game (five unchecked fixed lists in its mesh
manager; an eviction rule that depends on GPU fences), recorded in the commit history of
`ps5/mods/4D530039.1/` should they matter again.
