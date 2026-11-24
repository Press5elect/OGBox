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
| **Download cheats** (Settings > Updates; `ogbox::fixes::Cheats` in `ps5/src/ogbox_fixes_fetch.cpp`, the index `ps5/gamedata/cheats-xbased.txt`) | Passed its PC test (`tests/fixes`: fetched and checked by URL-encoded names, a bad hash refused, a different file of the same name kept, unsafe names refused, no network, the shipped index read whole). Untested on the console: the download itself over the console's curl (958 small requests to raw.githubusercontent.com), the progress toast and the result, the trainers rescanned after it, writing to a USB data folder (exFAT) with names that have spaces and brackets. 1.0.4: "for your games" (the title IDs from the certificates read this run, Redump's record of each file name listed or played, the hard disk's UDATA/TDATA folders; logged as "cheats: N of your games known") and a game page's own fetch when the data folder's `trainers/<title ID>` is missing (once a game a run, quiet; logged "cheats: <ID>'s (its page)"): PC-tested filter only, the UI untested on the console. Six of xbased's files are left out of the index because their names have ".." ("(v0..etm"): harmless names, refused by the brief's rule; relax `SafeCheatPath` and the tool together if they are wanted |
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
| The two devices' frame handoff not waited for (1.0.5; `ps5/hakux/hle/present_host.c` `pshx_present_take_start`/`_poll`, `ps5/src/ogbox_take.h`, `fetchPicture` in `ogbox_main.cpp`; `NC_PRESENT_WAIT=1` waits as before) | Made for test build FB's evidence (`55530041 v2` at 4x, 120 Hz, heavy GPU options, One GPU device off: 704 of 36904 refreshes skipped, nearly all at "frame", the copy queued behind the HLE GPU's work and waited for). Passed its PC test (`ps5/tests/present-ogbox`: the real `present_host.c` on a fake device and display, driven as `fetchPicture` does with title frames finishing late: one copy at a time, the slot and the display's use held until the fence, no copy into a staging buffer the title's GPU or CPU reads, every picture whole and the frame its copy began with; a copy that never completes kept, the GPU lost, forget waiting for a copy) and the front end type-checks. Not seen on the console: fewer `frame` skips in the 10 s summary with the same options, the `not waited for` line's figures, the added latency (up to a refresh) with frame generation and at 60 Hz, a display resize (the core's render thread waits up to a refresh more for the copy), screenshots and save-state pictures. Taken out again if it shows no benefit |
| Texture packs with palette (P8) textures whose palette animates | Each palette is a different code: a pack needs one file per palette state |
| **FSR1 sharpening** (Settings > Picture, per game, shown with the FSR1 upscaling filter only: Off / Low / Medium (default) / High, AMD's RCAS at 2, 1 and 0.5 stops; `shaders/glsl/ogbox/rcas.frag`, ported from AMD's `ffx_fsr1.h` `FsrRcasF` (MIT, the same licence as `fsr1.frag`); `ogbox_main.cpp` `buildCommandBuffer`, `ensureRcas`) | With it on and the picture smaller than its place, EASU draws into `rcasIn` (the swapchain's format, the screen's size) and RCAS from it into the screen, or into HDR's picture layer before HDR's encoding. Passed its PC test on lavapipe (`ps5/tests/fsr1-ogbox/run.sh`, not in `run-pc-tests.sh`, as the HDR and frame generation host tests aren't: EASU into the image the same as into the screen, every RCAS pixel AMD's maths within half a code at all three strengths, the letterbox black, a black picture and pure primaries without NaN, no validation message). Not seen on a console: how it looks (a 480p game at 4K with FSR1, Off against Medium), its cost (the 10 s presenter line's new "FSR1's upscale before its sharpening" figure, and "scaled to the screen", which then holds RCAS), the 33 MB image at 4K. Off gives FSR1 exactly as before |
| **Sharpness for colour textures only** (1.0.5, Settings > Picture under Texture sharpness, on; hakux-native `nv-texrole`: `shader/texrole.h`, `NC_HLE_TEXROLE`) | Passed `tests/hle/test_texrole.c` (a table of keys and their roles). Not seen on a console: compare `4D5300D1 v1`'s characters with Texture sharpness Sharpest, this on and off (off: too bright); the 10 s `texture sharpness: by stage role` line's counts on a few games |
| **Smooth small effects** (1.0.5, Settings > Picture, Lighting and Effects, on; `nv-smallrt`: `ff.h` `HLE_BSPL_GLSL`, `psh.c` `sampb`, `NC_HLE_SMALLRT_FILTER`) | Passed `tests/hle/test_psh` and `test_ffgen` (the B-spline stages' GLSL; off, the shaders byte for byte as before). Not seen on a console: `5553001D`'s glow at 3x or 4x, on against off (blocks that shimmer when the camera moves), the `1x targets read smoothly` line, the cost (4 fetches for 1 on those pixels) |
| **Shadow softness** (1.0.5, per game: Game's own (2x2) / Soft (3x3) / Softer (5x5); `nv-shadow`: `psh.c` `shadow_sample`, `NC_HLE_SHADOW_PCF`) | Passed `tests/hle/test_psh` `test_shadow_pcf` (4 and 9 gathers; the key's size and record layout unchanged). Not seen on a console: a game whose log has `shadow compare: N draws` with N > 0, its shadow edges at 3x3 and 5x5 against the game's own, the frame rate |
| **Foliage coverage** (1.0.5, per game: Off / Alpha-tested / Alpha-tested and blended; `nv-alphaaa`: `tex/alpha_cov.h`, `pbres.c` `tex_define`, `NC_HLE_ALPHA_AA`) | Passed `tests/hle/test_alpha_cov.c`. Not seen on a console: distant trees, grass and fences in a game with alpha-tested foliage, on against off (thinning and flicker at distance), the `alpha coverage: texture ...` lines; whether 2 (blended too) thickens see-through foliage too much |
| **Per-pixel lighting** (1.0.5, per game, off; `nv-fflight`: `shader/ffhw.c`, `NC_HLE_FF_PIXEL_LIGHT`) | Passed `tests/hle/test_ffhw` (off: 200000 random keys' GLSL byte-identical; the per-pixel lights' text). Not seen on a console: a game whose log says `fixed-function lighting per pixel: N draws` with N well above 0, its specular highlights and spot lights on large surfaces on against off, any `lights per vertex (the pixel shader can't take them)` lines, the cost |
| **Ambient occlusion** (1.0.5, per game, off; Strength, Radius, and for vertex-program games Near / Far / field of view; `nv-depthao`: `vk/gamma.c`, `pbdraw.c` depth pairing, `NC_HLE_AO*`) | `gcc -fsyntax-only` and the GLSL through glslangValidator; no host test runs the pass. The depth pairing's census is always on (two `depth pairing` lines after 600 frames). Not seen on a console: the two census lines for a few games (which share pairs, fixed function or vertex programs), AO on in a fixed-function game (corners darkened, the HUD untouched), the `ambient occlusion: none from here on: <why>` reasons, Near / Far values for a vertex-program game (none known yet: found by trying), the half-resolution pass's cost (`HAKUX_HLE_PROF` #47) |
| **Texture upscaling** (1.0.5, per game: Off / 2x / 4x; `nv-texup`: `tex/texup.h`, `vk/draw.c` `up_*`, `NC_HLE_TEXUP`, budget `NC_HLE_TEXUP_MIB`, 1 GiB) | Passed `tests/hle/test_texup.c` (filter, sizes, LOD shift, cache); its GLSL matched the C reference on lavapipe. With Sharpness for colour textures only it upscales only colour stages (`up_stage_ok` takes texrole's role, whatever that setting says); with Foliage coverage the image's levels s.. are the rescaled ones and its key differs from the same texels without the test (levels 1..s-1 at 4x are halved from the upscaled level 0, without a coverage rescale). Not seen on a console: a game's textures at 2x and 4x against Off (the edges, text left alone), the 10 s `texture upscaling` line (images, MiB, ms recording), the GPU memory with a large game at 4x, hitches when many textures become due at once (16 images or 8 Mpx a frame at most). **Upscaled texture format** (`nv-texbc`, `tex/texbc.h`, `NC_HLE_TEXUP_BC`, default Compressed): a DXT texture's image in DXT, 4 to 8 times less memory a sample than RGBA8. The FE log (one spot in 55530041, 4x, ambient occlusion off) put 4x upscaling about 8 fps under 2x, with the presenter's copies waiting on the GPU, and 351 MiB of images against 80 at 2x: why it was tried. Passed `tests/hle/test_texbc.c` and `tests/texbc/run.sh` on lavapipe (the GPU's blocks equal the C reference's; mean colour error 3-5 of 255 on the filter's output). Not seen on a console: the speed at 4x against Uncompressed in the same spot (the new `hakuX-hle-gpu` 10 s line), the look of the new detail's blocks |

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
1.0.4: the cache drives' write-back can write through it (Fast writes, below). Passed its PC test (`tests/broker`: offsets out of
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

## Fast writes on the console's storage (helper) (1.0.4, untested on the console)

The owner's console (test build EV, data folder `/data/ogbox`): OGBox's own 16 MiB writes 192 MiB/s
for 1280 MiB, then 32, 27, 12 MiB/s; the broker right after, 4096 MiB at about 200 MiB/s (196.8
past 2 GiB); OGBox again 26.8 MiB/s. Random 4 KiB `pwrite` by the broker 565 IOPS; OGBox to the
broker over loopback 163.7 MiB/s in 16 MiB messages, 4 KiB one at a time 279/s (3.56 ms round
trip), 64 in flight 372/s. On a USB data folder there is no budget (82-90 MiB/s).

So the cache drives' write-back (hakux-native `native/kernel/wback.c`) can send the held bytes to
the broker: **Settings > Advanced > Fast writes on the console's storage (helper)**, global, **on
by default** (the owner's decision), at a game's next start, only with the Xbox hard disk under
`/data/`. OGBox launches the broker once a run before the core starts
(`ps5/src/platform/ps5_wbremote.cpp`) and hands the core a C table (`nc_wb_set_remote`). The
core keeps its order (the items in the order it would write them, up to 4 requests in flight,
adjacent items of one file in one request of up to 8 MiB; what it does itself waits for what is
in flight), counts an item written only when the broker answered (held bytes, reads, the journal's
"D", truncate/drain/format waits), opens a file at the broker only with nothing in flight and
checks the inode, pings it every 60 s idle (it ends after 10 minutes without a request) and, if
it fails, waits until it has carried out what it was sent and ended, then writes everything not
answered itself in order. No fsync either way (as before): a close's fsync of the title's
descriptor runs on the same file, and the bytes still held at a close were never covered by it.
Game files (E:, C:), save states and the state restore's copy still write as before.

Passed its PC tests: hakux-native `tests/native/linux/run-wback.sh` (a fake broker: slow, short
writes, dying part-way, refusing a write; 150 random seeds byte for byte with OGBox's own path;
"D" only after the last answer; a quit with bytes at the broker; ThreadSanitizer clean) and
`tests/broker` (the real broker and client with the real wback.c: `HAKUX_NATIVE=<hakux-native
work tree>`; the broker dying part-way). Not seen on a console: whether the broker's writes keep
their speed for a game's cache copy, the loader on 9021 at a game's start (the elevation helper
uses it too), the round trips under a game's load, the inode check on the console's `/data`.
To watch: a game writing many small files or small random writes may be slower through the
broker (each request costs it about 2.7 ms: 372/s in flight against its own 565 IOPS); what to
compare is a game that copies to its cache drive on first start, with the setting off and on.

## The data folder's move onto the console's storage through the helper (1.0.4, untested on the console)

Moving the data folder from a USB drive to `/data` copied at the title's held-back rate (the write
test above: 190 MiB/s for about 1.3 GiB, then 32, 27, 12 MiB/s and less). Now a move whose new
folder is under `/data/` (only then; renames and folders stay OGBox's) has the write broker copy each
file (`ps5/src/ogbox_storage.cpp` `MoveHelper`, the broker's COPY in `ps5/broker/server.cpp`: it
reads the source and writes the copy itself, 16 MiB at a time, so the bytes don't cross the
loopback): 32 MiB a request (the progress moves with each), then SYNC and CLOSE, the copy's size
checked, its date set as before. One broker for the move, started at the first file to copy, told to
quit at the end of copying. A request that fails stops the broker (it is let end first) and that file
and every one after are copied by OGBox from their start; the old copy is deleted, as before, only
once everything is there and the note names the new folder. No setting. Passed its PC tests
(`tests/broker`: COPY of random sizes from 0 to 70 MiB, pieces up to 64 MiB, every pwrite cut short
with O_DIRECT, short at the end, the errors; `ps5/tests/storage`: moves through the real broker on a
socketpair, byte for byte, with the broker dying part-way, not starting, a path it refuses, and a
move to USB not using it). Not seen on a console: the speed (the broker reads the USB drive and
writes `/data` one after the other, not overlapped), O_DIRECT on the copies, the loader on 9021 from
the game list. To test: a data folder of a few GiB from USB to the console's storage, timed, with the
log's `storage: copied ...` line; then a game from it.

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
