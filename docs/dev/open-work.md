# Open work

What is known to be unfinished, untested or deliberately left. Update it when something here is
done or something new is learned; delete what no longer applies.

## Untested on the console (as of 1.0.2)

| Item | State |
| --- | --- |
| The saves manager on the sync place (hard disk manager: **Saves on <place>...**, and **Delete here and on <place>**; `savesync::remove`, `listThere`, `startPlaceScan`, `startSaveDeletion`) | Untested on the console. The engine passed its PC test (`ps5/tests/savesync`: deleted on both stays deleted after a sync, a whole game, a save only there, each side's own spelling, names outside a save's folder refused, the place not answering deletes nothing, the listing). Not seen: deleting on an SMB/NFS share and a USB drive through ps5vfs (`ps5vfs_remove` of the files, then of the empty folders), the listing's time over a slow share, the screen itself. By design a deleted save is deleted, not put in `.older`; another console syncing with the same place sends its own copy back until it is deleted there too |
| Game fixes fetched from GitHub at start (`ps5/src/ogbox_fixes_fetch.cpp`) | Passed its PC test (fetch, unchanged, tampered file, unsafe paths, withdrawn mod, no network); nothing can be fetched until `ps5/game-fixes.txt` is on main |
| `4B4E0034 v2`: render targets over one memory landed and taken in the order drawn (hakux-native `coh-seq2`, on `apu-diag` 0bb77e6f98, test build EA) | 0bb77e6f98's fixes (the flashes, the blurred patches) were confirmed on the console; on the PC it still failed every random scenario where parts of targets are drawn or blended over (`tests/cohalias chaos`), which `coh-seq2` passes. Whether the white lawn that comes back while attacking goes with it: not yet run on the console. The shadow spikes: see below |
| HD texture **replacements** on the PS5 | Dumps were made and the scan was seen working; the owner's first pack failed because of the `dump` folder bug (fixed). A replacement drawn in a game has not been confirmed since. The module's logic passed its PC tests |
| The rename retry for write-behind files (`native/kernel/io.c`) | The failure was random; the run after the fix copied 25 tracks without it. The retry's own log line (`rename ... refused ...; tried N times more`) has not been seen, so the cause (the closer thread still syncing the file) is likely, not confirmed |
| `4541005A` and `45410071` 60 FPS patches in online/LAN modes and in-engine cut scenes | The steps-per-frame fix was modelled, and races measured at 59.9 fps; the online speed change is carried over from analysis |
| `45410049` game fix in modes that depend on lockstep timing | Fights and cut scenes confirmed smooth; online/LAN play not tried |
| Texture packs with palette (P8) textures whose palette animates | Each palette is a different code: a pack needs one file per palette state |

## Known problems, cause not found

| Problem | What is known |
| --- | --- |
| `4D530039`: xemu issue 1379 ("vertex explosions" on some tracks) | Not looked at. The `BEGIN`-without-`END` fix may or may not cover it |
| `4D530039`: a group of about 37 draws missing on a quarter of frames, every other frame | Seen in the per-frame counts; never identified. No visible symptom tied to it after 1.0.2 |
| `4B4E0034 v2`: meshes explode into black spikes, scenes wash out white or go one flat colour (the first garden tutorial) | In the published 1.0.1 too (the owner's log of 2026-11-14), so not from 1.0.2's changes. Its draws all have ordinary vertex formats and no skinning; the 160x120/320x240 target fix ("doesn't fit it") is working. The vertex values themselves are not in the draw log: the next step is logging them for the exploding draws |
| `4B4E0034 v2`: long dark shadow spikes from a swing's trail, about 3 frames an attack | Not reproduced on the PC: no vertex path the PC test models (the guest's stores, file reads, the 2D engine's blits into the heap's holes) read other bytes than the console's. A lead: an effect drawn at a frame's end and composited at the next frame's start was composited from memory once older blur images landed over it (`tests/cohalias` carry), fixed on `apu-diag` since 0bb77e6f98 |
| A target's own bytes on an edge page made hot (`mem/barrier.c` `NBR_HOT`: no guard closes it from then on) written by the guest | Its image doesn't land before the store, and at its next first write the whole image is replaced by the memory, older than the image everywhere the guest didn't write. `tests/cohalias chaos gpu=jobs seed=22` with 4 KiB pages; seeds 31 and 34 with `gpu=rand lag=1`. Needs a landing or upload of the changed bytes only. Not tied to a symptom seen |
| A device's write (a file read) into memory a target's image is newer than | The image doesn't land first (the guest's stores land it; a device's don't), so the rest of its bytes stay older than the image. Seen only in a PC scenario; no game known to read into a live target |
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
