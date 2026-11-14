# Open work

What is known to be unfinished, untested or deliberately left. Update it when something here is
done or something new is learned; delete what no longer applies.

## Untested on the console (as of 1.0.2)

| Item | State |
| --- | --- |
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
