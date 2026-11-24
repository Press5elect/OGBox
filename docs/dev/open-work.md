# Open work

What is known to be unfinished, untested or deliberately left. Update it when something here is
done or something new is learned; delete what no longer applies.

## Untested on the console (as of 1.0.2)

| Item | State |
| --- | --- |
| The saves manager on the sync place (hard disk manager: **Saves on <place>...**, and **Delete here and on <place>**; `savesync::remove`, `listThere`, `startPlaceScan`, `startSaveDeletion`) | Untested on the console. The engine passed its PC test (`ps5/tests/savesync`: deleted on both stays deleted after a sync, a whole game, a save only there, each side's own spelling, names outside a save's folder refused, the place not answering deletes nothing, the listing). Not seen: deleting on an SMB/NFS share and a USB drive through ps5vfs (`ps5vfs_remove` of the files, then of the empty folders), the listing's time over a slow share, the screen itself. By design a deleted save is deleted, not put in `.older`; another console syncing with the same place sends its own copy back until it is deleted there too |
| The version kept after an update, **Go back to <version>**, **What's new** and **Skip this version** (1.0.4; `ps5/src/platform/ps5_update.cpp`: `KeepPrevious`, `RollBack`, `Skip`; the offer in `ogbox_main.cpp`) | Passed the PC test (`tests/update`: kept with its digests, gone back exactly, a going back cut at every step and in its undo on drives with and without links, failing and lying renames, a missing or changed kept file and a damaged manifest refused, not kept with too little room, removed for a download's room, a skipped version not offered while a newer one is). Untested on the console: the digests of the kept files at the end of an install (about 80 MB, on the interface thread: a moment's pause before the close popup), the directory renames of `.update/backup` to `.previous/files` on the console's drive and on exFAT, the popups and their scrolling |
| The crash notice at the next start (1.0.4; `ps5_crash.cpp`: `WriteCrashed`, `pshakux_crash_previous`; `crashTick` in `ogbox_main.cpp`) | Checked on the PC only, outside the test suite (the handler built for Linux with stubs: a SIGSEGV and an abort leave the marker and their report is copied, SIGTERM leaves none, the notice once). Untested on the console: whether a close from the PS5's menu, or a relaunch into a game and back, sends a signal that is reported (SIGTERM and SIGHUP are left out for that reason); the marker written from the handler on the console's drive; the popup |
| Game fixes fetched from GitHub at start (`ps5/src/ogbox_fixes_fetch.cpp`) | Passed its PC test (fetch, unchanged, tampered file, unsafe paths, withdrawn mod, no network); nothing can be fetched until `ps5/game-fixes.txt` is on main (it reaches it with 1.0.2's merge); the first real fetch not yet seen on the console |
| **Download cheats** (Settings > Updates; `ogbox::fixes::Cheats` in `ps5/src/ogbox_fixes_fetch.cpp`, the index `ps5/gamedata/cheats-xbased.txt`) | Passed its PC test (`tests/fixes`: fetched and checked by URL-encoded names, a bad hash refused, a different file of the same name kept, unsafe names refused, no network, the shipped index read whole). Untested on the console: the download itself over the console's curl (958 small requests to raw.githubusercontent.com), the progress toast and the result, the trainers rescanned after it, writing to a USB data folder (exFAT) with names that have spaces and brackets. Six of xbased's files are left out of the index because their names have ".." ("(v0..etm"): harmless names, refused by the brief's rule; relax `SafeCheatPath` and the tool together if they are wanted |
| HD texture **replacements** on the PS5 | Dumps were made and the scan was seen working; the owner's first pack failed because of the `dump` folder bug (fixed). A replacement drawn in a game has not been confirmed since. The module's logic passed its PC tests |
| The rename retry for write-behind files (`native/kernel/io.c`) | The failure was random; the run after the fix copied 25 tracks without it. The retry's own log line (`rename ... refused ...; tried N times more`) has not been seen, so the cause (the closer thread still syncing the file) is likely, not confirmed |
| `4541005A` and `45410071` 60 FPS patches in online/LAN modes and in-engine cut scenes | The steps-per-frame fix was modelled, and races measured at 59.9 fps; the online speed change is carried over from analysis |
| `45410049` game fix in modes that depend on lockstep timing | Fights and cut scenes confirmed smooth; online/LAN play not tried |
| Saves re-signed from a console's (`ps5/src/ogbox_resign.cpp`, at a game's start) | Passed its PC test (`ps5/tests/resign`: HMAC/RC4 known answers, keys from dumps made as a console's under test keys, every signature layout, `.sig` holders, locked saves, `ContentMeta.xbx`, dates, the cache). Not run with a real console's dumps or saves; whether an Xbox 360's backward compatibility uses the Xbox's certificate key is not known (its HD key isn't an Xbox EEPROM's: `hdkey.txt`) |
| `41560020` / `4156004E` pacing (the step rounded to the 120 Hz pickups) | Each release's rewritten bytes run on the PC at their own addresses against a model (21000 frames): steps off by more than 1 ms at 60 from 19-41 % to 0.6-1.8 %. The owner reported the test builds with it (EF, EG) working as intended; no log of it measured |
| **Widescreen (any release)** (1.0.4: OgXHD's database again for 30 games, `N` records; `docs/dev/game-fixes.md`) | Passed its PC test (`tests/patches`: each of the 30 applies on a file of its own patterns and is refused with one more copy of one; `N` itself). Not seen on a console: an entry offered (not greyed) on a PAL or other non-USA disc and 16:9 with it on. The patterns were never checked against a real XBE here (none is in the repository), so how many of the 30 fit a given release is not known; one that doesn't is shown greyed with the count it found |
| The loading screen (1.0.4; `drawLoadingScreen` in `ps5/src/ogbox_main.cpp`, `ui::drawLoading`, `pshx_core_start_progress`) | Drawn by `tests/ui` (both themes, a long step, a failure). Not seen on a console: the steps as the core reports them (the push-buffer mode's start, the HLE back end's `running`, the prebuild's count: plain reads from the menu's thread), the cover in the game's own run, how long each step lasts (the boot log's `loading screen: step N after S s` lines say), the share wait on it. A prebuild that ends between two frames shows no count, only its check |
| Settings > Interface > Accessibility (1.0.4: Larger text, High contrast, Reduce motion) | Every screen drawn at Larger with High contrast by `tests/ui` at 2160 and 1080 lines (`a11y-*`). Not seen on a console: the fonts baked at the larger size at the start (`ps5OverlayMoreFonts`), the text drawn scaled when it is changed in the menus until the next start, the Code theme's code standing still |
| **Speed limit** (1.0.4: Settings > Advanced and a game's own, 100% / 150% / 200% / 300% / Unlimited; the core's `NC_SPEED`, `native/kernel/speed.h` in hakux-native `speed-limit`) | The clock arithmetic passed its PC test (`tests/hle/test_speed.c`: the scaled clock and its inverse, RDTSC's ratio and offset, the periods, the output's resampler, Unlimited's steps); nothing run on the console. To see: intros and menus at 200% twice as fast, the sound faster and higher without crackle, back to 100% normal, a state saved at 200% loading at 100% without a jump. A game that counts frames and a 60 FPS patch both speed up; frame generation shows the faster frames; with full DSP sound at 300% the APU may not keep up |
| The player's own patches (1.0.4: `<data folder>/patches`, `docs/dev/game-fixes.md`) | Passed its PC test (`tests/patches` `test_user`: each joining rule, a BPS of another game and a JMP of another CRC not shown, an IPS attached, renamed, detached, a `.jmp` with offset, find/replace, `APPEND:`, a `[-]` block, CRC match and mismatch, a broken `.jmp` with its line, the limits, an id clash, mods.txt on/off, the start's patched file and log) and the front end type-checks. Not seen on a console: the folder read on the console's file systems (exFAT USB names, case), the screen's **Your patches** group and its attach list, the details' counts. The `.jmp` reading was ported from the importer without a real file to try (none is kept in the repository); a real file with a line kind the importer silently skipped would now be greyed with that line |
| One Vulkan device (`NC_HLE_ONE_DEVICE=1`; hakux-native `one-device`: `vk/vkdev.c` `hle_vk_needs`, `hle_vk_share`; `present_host.c` `pshx_present_slot_take`; `ogbox_main.cpp` `getEnabledExtensions`, `shareDevice`, `recordSlotCopy`) | The core's adoption passed its PC test on lavapipe (hakux-native `tests/vkshare/run.sh`: a device made from its needs taken, the host's queue lock used; one without a feature, one missing an extension, one on another queue family, and no offer each give an own device); the front end type-checks. Nothing run on the console: whether RADV makes the title's device with the HLE GPU's chain, the frame's wait on the core's timeline in the title's submit, the slot copy, the queue lock's cost (the title's present now holds it: a present that blocks holds the HLE GPU's submits up), screenshots and save-state pictures read after the frame's fence, the quit (the shared device is left, not destroyed) |
| Texture packs with palette (P8) textures whose palette animates | Each palette is a different code: a pack needs one file per palette state |

## The write broker: the storage write test (1.0.4, untested on the console)

The console holds a title's writes back after a burst of about 1.3 GiB at 242 MiB/s, then about
2 MiB/s, per process (PS5_PayloadSDK `platform/docs/PROBE.md`, "Sustained writes" to "Writing
through another process"). The console's FTP server, a payload, wasn't held by a title's budget but
wrote only about 7 MiB/s a connection (22-25 MiB/s in all), which may be its own cost (a `write()`
a small `recv()`) rather than a limit on payloads; its STOR also can't write at an offset. Whether
OGBox's writes (the Xbox hard disk) should go through a payload of its own depends on what a payload
sustains past 2 GiB, so this build only measures: **Settings > Advanced > Storage write test**
(`ps5/src/platform/ps5_broker.cpp`), with the broker `write-broker.elf` (`ps5/broker`, built by
`ps5/tools/build-broker.sh`, sent to the payload loader on 127.0.0.1:9021 like the elevation helper).
Nothing in OGBox writes through the broker yet. Passed its PC test (`tests/broker`: offsets out of
order, short `pwrite`s, errors, pipelined requests in order, the path check, the whole test on a
small plan); the payload builds with the SDK's `prospero-clang++`. Not run on a console.

What the `writetest:` lines will decide:

| Result | Then |
| --- | --- |
| B (the broker's own 16 MiB writes, right after A spent the title's budget) stays near 242 MiB/s past 2 GiB, and F (the title again) is still about 2 MiB/s | A payload has a budget of its own or none: route the hard disk's writes through the broker (E's numbers say what the loopback adds) |
| B falls to about 2 MiB/s after its own 1.3 GiB | Payloads are held the same way, each its own budget: a broker only adds one burst; not worth it |
| B starts near 2 MiB/s | The budget is shared beyond the process (or A's spending held B): the broker can't help |
| B sustains, C (64 KiB writes) much lower | A write's own cost dominates (ftpsrv's 7 MiB/s): the broker must write in large pieces (gather small writes) |
| F faster than A's tail | Something refilled the title's budget during B to E (the idle time, about 7 minutes): note it |
| The view line says "different", or the read-back check fails | The broker sees other files than the title: fix that before anything else |

Risks to watch on the first run: the loader on 9021 must be free (the elevation helper uses it at
start, briefly); a broker left running ends itself after 10 minutes without a request, or when the
title's connection closes; starting a game during the test ends it (the files are deleted at the
next run's start); `O_DIRECT` is dropped by itself where the file system refuses it (`direct=0`).

## Known problems, cause not found

| Problem | What is known |
| --- | --- |
| `mem/barrier.c` `hle_barrier_arm_keep` / `hle_barrier_arm_src` on 16 KiB host pages | They protect whole 16 KiB blocks, but a fault on a neighbouring 4 KiB page waits only for that page's value and then opens the block (found while writing `hle_barrier_arm_place` for the guest-RAM import). Also: `tests/cohalias chaos gpu=rand lag=1` seeds 49 (16 KiB) and 31, 34 (4 KiB) fail on SAMPLE/DEST, identically on the copy path. Not tied to a symptom seen |
| `5443000D v5`: hakuX's `120fps` mod stops the game at the switch from the chapter-1 real-time scene to play. Withdrawn: `ps5/tools/build.sh` no longer ships hakuX's file, and `ps5/mods/5443000D.5/120fps/mod.txt` (no patch lines) replaces it on consoles that have it, through the game-fixes download | Cause traced on the console (2026-11-23, probe run): with the mod a resource group is loaded twice, the next load gets no memory (0x1720B0 returns -1), ADXF_ReadNw fails (-3, ignored at 0x31D247) and the load queue (0x1649D0) polls forever. Which double-run task queues the second copy is not proven; keeping slots 1 and 14 (0x2DF590/0x2E21B0, 0x1CA790) to logic frames crashed the game in the cutscene (a null read at 0x307A6A). The mod's design (any task that drew runs every frame) doesn't fit this game's tasks, which draw and run logic in one; frame generation gives 120 Hz instead |
| 0.1.52's merge: a PatchHub offset patch that absorbed an OgXHD pattern copy may have lost its release check (`import-gamedata.py` `merge_duplicates`: the CRC is kept only when every copy has one, the region only when every copy has the same) | Unverified (the sources aren't kept): 13 widescreen entries of the 30 games above are offset (`O`) records with no `C` record and no region, so on a release they weren't made for they are offered and write at that build's offsets unchecked. Seen in `patches.db`, not on a console. A full import with the merge keeping a copy's CRC and region for `O` patches would settle it; the `Widescreen (any release)` entries are offered beside them |
| `4D530039`: xemu issue 1379 ("vertex explosions" on some tracks) | Not looked at. The `BEGIN`-without-`END` fix may or may not cover it |
| `4D530039`: a group of about 37 draws missing on a quarter of frames, every other frame | Seen in the per-frame counts; never identified. No visible symptom tied to it after 1.0.2 |
| `4B4E0034 v2`: meshes explode into black spikes, scenes wash out white or go one flat colour (the first garden tutorial) | In the published 1.0.1 too (the owner's log of 2026-11-14), so not from 1.0.2's changes. Its draws all have ordinary vertex formats and no skinning; the 160x120/320x240 target fix ("doesn't fit it") is working. Not seen since the render-target fixes of 1.0.2 (the owner's runs of test builds EA to EE); if it comes back, the next step is logging the exploding draws' vertex values (test build EC's `drawlog_vtx`, hakux-native `vbcopy-test`) |
| A target's own bytes on an edge page made hot (`mem/barrier.c` `NBR_HOT`: no guard closes it from then on) written by the guest | Its image doesn't land before the store, and at its next first write the whole image is replaced by the memory, older than the image everywhere the guest didn't write. `tests/cohalias chaos gpu=jobs seed=22` with 4 KiB pages; seeds 31 and 34 with `gpu=rand lag=1`. Needs a landing or upload of the changed bytes only. Not tied to a symptom seen |
| A device's write (a file read) into memory a target's image is newer than | The image doesn't land first (the guest's stores land it; a device's don't), so the rest of its bytes stay older than the image. Seen only in a PC scenario; no game known to read into a live target |
| `4D5300D1 v1`: characters too bright with Texture sharpness at Sharpest | The owner's finding (2026-11-14): the setting (`NC_HLE_LODBIAS=-1`) is the cause, the rendering itself is as with Default. Likely a texture whose smaller mip levels the game made darker or blurred (a lighting lookup), now read at a larger one; which texture is not found yet: a draw recording with Sharpest on would show it, and the bias could then leave that kind of texture alone. Advise Default for this game |
| Distant alpha-tested foliage shimmering | With Texture sharpness at Sharpest and a high resolution scale: expected from the negative LOD bias. Advise Default for such games |

## Limits by design

- Texture packs don't cover render targets, cube maps, volume textures, linear-layout textures,
  bump-map and depth formats ([`../texture-packs.md`](../texture-packs.md)).
- PNG replacements stop at 2048 x 2048 (the upload arena is 32 MiB); larger packs need DDS.
- hakuX can't stop a game and start another in one run: starting a game relaunches the title.
- Installing a build doesn't remove files the new build no longer has (old mod folders stay).
  A build that needs one gone must say so; an installer step that prunes `mods/` would fix it.

## Ideas not taken up

- **A GPU frame capture** (the push buffer of a few frames, replayable on a PC): would have
  found the `4D530039` cause in one step. Real work; worth it if another long hunt starts.
- **The census's object key without the vertex count** (so batched geometry can be tracked).
- **DDS output for uncompressed dumps**, and BC7 packs.
- **Pruning stale mod folders at install or first start.**
- **A slower emulated GPU fence** (semaphore release when the frame is really drawn) for games
  that pace their streaming on it: one game's eviction rule was found to depend on it, without a
  visible symptom after its other cause was fixed. It would cost every game that waits on fences.

## Decisions on record

- Diagnostics stay in the core, off unless a `boot.txt` switch turns them on.
- A game fix that shows no benefit on the console is taken out, even if its analysis was right.
- The game's own sound mix (its DSP program's output) is the default; Fast is the fallback the
  core switches to by itself when that program stays silent.
- Release assets are the ZIP and its checksum; no source archive.
- The FTP share backend (`ps5/src/platform/vfs/ps5_vfs_ftp.cpp`) is never mounted: shares are
  mounted through `ogbox::mountShare` (SMB, NFS). Its defects found after 1.0.2 (rows 6.2, 6.12 and
  3.12's FTP half of `docs/dev/frontend-fix-plan.md`) were left; fix them before it is mounted.
- Crux2's share stall work (non-blocking sockets with time limits, the SMB port check, the share lock
  released per block during a store) was not ported: nothing on OGBox's console showed a stall
  there, and Crux2 hadn't run it on a console either. The first thing to try if a switched-off
  share ever stalls OGBox.
- A data-folder move now has its own "deleting the old copy" phase, with progress; before, the
  screen stood at the last file while the old copy was deleted.
