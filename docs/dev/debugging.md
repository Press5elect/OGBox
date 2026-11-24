# Debugging: the boot log and the diagnostic switches

## Getting a log

OGBox writes `ogbox-boot.log` in the title folder (`/data/homebrew/PPSA99251/`); the session before
is kept as `ogbox-boot.previous.log`. A session's log runs on across relaunches (starting a game
restarts the title), so one file can hold several games.

- After a crash (a fault, abort, `std::terminate`, a resource limit; not SIGTERM or SIGHUP, which a
  close may send) the handler leaves `ogbox-crashed.txt` (one line: what it was). The next start
  copies the report, with up to 256 KiB of the lines before it, out of the log that holds it
  (`ogbox-boot.previous.log`) into `ogbox-crash.log`, which stays until the next crash, and the game
  list says so once (`ps5_crash.cpp`: `pshakux_crash_previous`). A game's start (autoboot) copies it
  and leaves the notice for the next game list.
- **Settings > Advanced > Boot log** is "Only when something goes wrong" by default: the log is
  kept in memory and written after a crash, a failed game, 15 s without a new frame, or the quick
  menu's **Save the Log**. A file with only `[log: this session's log is kept in memory ...]` and a
  few `stall:` lines is that mode with nothing saved. For a full log, ask for **Save the Log**
  before quitting, or set the option to "Always written".
- **Log What the GPU Draws** (quick menu; "debugging only") records 5000 draws into the log,
  starting 5 s after the game goes on: about ten frames. The problem has to be on screen then; it
  cannot catch something that only shows while moving unless the player keeps moving.

## Reading a log

Logs are large (2 to 10 MB). Don't read them top to bottom; process every line with scripts, read
the distinct messages, then look closely around the problem:

1. Split it by run: `OGBox (PPSA99251) starts`, `library: starting <image>`, `core: disc image`,
   `core: booted`, `core: paused for the exit`.
2. For the run in question, collapse numbers (`sed -E 's/0x[0-9a-f]+/X/g; s/[0-9]+/N/g' | sort |
   uniq -c | sort -rn`) and read the rare messages: errors and one-offs are at the bottom.
3. Check what was actually running:
   - `build identity:` (which build),
   - `ogbox: settings for <key>:` (scale, the `NC_*` switches),
   - `boot.txt: environment` lines,
   - `mods:` and `patches:` lines (what was applied; a mod listed that the source no longer has is
     a leftover folder on the console).
4. The lines that usually matter:

| Line | Meaning |
| --- | --- |
| `hakuX-hle-fps:` | The game's frame rate, every 5 s |
| `hakuX-hle-hitch: N s: ... draws a frame, V vblanks a frame` | Per-frame costs; `stutter #`, `HANG` for stalls |
| `presenter: N passes in 10 s` | The display side's pacing. Its `the frame's copy N times, x ms on average` is the time a pass spent getting the HLE GPU's frame (with two devices and the copy not waited for, only the fence read and the next copy's submit: well under 1 ms) |
| `presenter: the frame's copy in 10 s, not waited for: ...` | (1.0.5, two devices, `NC_PRESENT_WAIT` not set) The handoff of `ps5/src/ogbox_take.h`: copies started; picked up in the same pass (done at once), the next or later; passes that kept the previous picture because the copy wasn't done (the game's frame shows a refresh later instead of the presenter missing the refresh); passes that found no staging buffer free (from FG none should: three staging buffers, one more than the frame slots; with two, FF's log had 230-280 such passes in 10 s, each a game frame never shown, the counter jumping 60-120); copies lost (the GPU lost); the oldest copy picked up, ms and passes. Mostly `the next` and few `kept` is the expected picture; many `kept` or `later` with ages of several refreshes means the HLE GPU's queue is the bottleneck (then `NC_PRESENT_WAIT=1` would have skipped refreshes instead) |
| `hakuX-native-io: ... failed: c00000xx`, `rename ...` | File calls that failed, with the NT status |
| `hakuX-native: seh:` / `frontier: stop:` | The game faulted: address, registers, the code around |
| `hakuX-native-frontier: threads` | Thread dump when nothing moves: who waits on what |
| `stall:` | No new frame; where the emulated CPU was |
| `hakuX-hle-pb: draw N:` | The draw log: one line a draw (program, counts, state, textures) |
| `hakuX-hle-tex:` | Texture packs: found, used, refused |
| `hakuX-native-mods:` | Each mod patch line applied |
| `writetest:` | The storage write test (Settings > Advanced; `ps5/src/platform/ps5_broker.cpp`): each phase's start, its 256 MiB steps (`step N a-bMiB s MiB/s`), its end (bytes, time, MiB/s, the fsync, each write's p50/p95/p99, `direct=`, `error=`), the read-back check and a summary. The broker's own lines are in klog as `[write-broker]` |
| `ogbox: fast writes on the console's storage (helper): in use ...` / `not in use: <why>` | Settings > Advanced > **Fast writes on the console's storage (helper)** (on by default; `ps5/src/platform/ps5_wbremote.cpp`), said once at a game's start: whether the cache drives' (X/Y/Z) write-back goes through the write broker, and why not (the setting off; the hard disk not under `/data/`; the broker not started: no payload loader, no answer in 8 s; a core without the hook) |
| `storage: moving through the helper (...)` / `storage: not moving through the helper: <why>` | Settings > Storage, a move whose new folder is under `/data/` (`ps5/src/ogbox_storage.cpp`), said at the first file to copy: the write broker copies the files (COPY: it reads the source and writes the copy itself), or why not (the new folder elsewhere; the broker not started: no payload loader, no answer in 8 s) |
| `storage: the helper stops: <why>; OGBox copies this file again and the rest itself` | A request to the broker failed (an errno, no answer in 120 s, the connection gone, a copy of another size): the broker is let end first, then that file is copied again from its start by OGBox, and every one after. `<file>: the helper doesn't take its path; OGBox copies it itself`: that one file (a path over 1024 bytes) |
| `storage: copied N MB in s (MiB/s): the helper N files, ... ; OGBox itself N files, ...; N files fell back from the helper` | A move's end of copying: the whole, the broker's part (opening to closing its files, the fsync included), OGBox's own, and how many files the broker began and OGBox copied after all |
| `hakuX-native-wb: the held bytes written by the helper ...` / `... by OGBox itself (no helper)` | The core's start (`native/kernel/wback.c`): which path the held bytes take, with the requests in flight (`NC_WB_PIPE`, 4) |
| `hakuX-native-wb: helper in use: last 10 s: ...` | Every 10 s while it writes: MiB and requests through the broker, the most in flight, the round trip's p50/p99, MiB OGBox wrote itself, the totals, the bytes held, the fallbacks |
| `hakuX-native-wb: helper ...: <why>: OGBox writes the cache drives itself from here on; N held write(s) ...` | The broker failed (no answer in 120 s, a write refused, the connection gone): what it hadn't answered was written again by OGBox in order; no byte lost. `<path>: the helper can't open it (...)`: that one file written by OGBox |

## Switches

Set in `boot.txt` as `env NAME=VALUE` (every game), unless a setting covers it. Remove them after
the test: some cost speed. Only the ones that have earned their keep are listed; the code has more
(`grep getenv` in hakux-native).

| Switch | What it does |
| --- | --- |
| `HAKUX_HLE_VISALL=1` | Every occlusion query reports "fully visible": rules visibility tests in or out |
| `HAKUX_HLE_VISLOG=<n>` | Logs the next n query results |
| `HAKUX_HLE_VSHLOG=1` | Logs each vertex program once: its tokens and the GLSL it runs as. The draw log names each draw's program by the same hash |
| `HAKUX_HLE_PSLOG=1` | Logs each register-combiner (pixel) program |
| `HAKUX_HLE_SKIPVSH=<hash>[/<count>[/<alphatest>]]` | Drops the draws of a vertex program: find which draws make something on screen |
| `HAKUX_HLE_CENSUS=1` | One line a frame for 2 minutes: alpha-tested draws by vertex program, objects that vanish and return. Note its object key includes the vertex count, so batched geometry whose count changes every frame (particles, billboards) always looks like it "returns" |
| `HAKUX_HLE_TEXDUMP=<id>` | One texture's raw texels to a file (the pack feature's dumps are the general tool) |
| `NC_DEVTIME=disc_us,disc_kBps,hdd_us,hdd_kBps` | The drives' modelled speed, with Fast disc drive off (e.g. `125,27600,8000,20000` for a console-like hard disk) |
| `NC_APU_DSP=0/1`, `NC_APU_MON=gp` | The sound path (the Sound mixing setting sets these; in boot.txt they override it) |
| `NC_APU_HRTF=0` | The 3D voices' HRTF filters off (the HRTF setting sets it; in boot.txt it overrides it) |
| `NC_APU_VOICE_LOG=1` | Each sound's sends, for mixing questions |
| `OGBOX_LOG=file` | The log always written, whatever the setting |
| `presenter: a refresh skipped, N ms between two acquires` | (1.0.4, always on in a game, at most 300 a run) When the presenter's loop takes over 1.5 refreshes between two swapchain acquires, the CPU time of each step of the pass before and of this one up to its acquire: input, settle (a staging buffer's fence), acquire (prepareFrame), vsync, frame (the copy from the HLE GPU: 1.0.5, with two devices only its fence read and the next submit, unless `NC_PRESENT_WAIT=1`), plan (frame generation), record, submit+present, the rest; and the time outside render() (the template's loop, ImGui). The step that grew is where the refresh went |
| `NC_PRESENT_WAIT=1` | (1.0.5) Two devices (One GPU device off): each frame's copy from the HLE GPU waited for on the presenter's thread, as up to 1.0.4 (`presenter: two devices: each frame's copy waited for` at the first frame), for an A/B against the default, where the copy is submitted and picked up by a later pass once its fence reads done (`... the frame's copy picked up by a later pass, not waited for`). The default adds up to a refresh of latency to a frame and keeps a busy HLE GPU from making the presenter miss refreshes; compare the 10 s skip summary's `frame` entries and the `not waited for` line. A copy not done after 2 s is logged each 2 s either way (`present (host image): the frame's copy not done after N s: waiting on`) and never given up |
| `NC_WB_PIPE=<1..16>` | With Fast writes (helper) in use: the requests the cache drives' write-back keeps in flight to the broker (4) |
| `NC_HLE_ONE_DEVICE=1` | One Vulkan device: the HLE GPU renders on the title's device and the title copies its display image on the GPU (no read back, no fence waited on the render thread). Read when the title makes its device, so a boot.txt `env` line or the core switch from the settings works; a game started after waiting for its share gets two devices. The log's `GPU: one device (shared with the title)` or `GPU: own device (why)` (the title's and the core's lines), and `presenter: the first frame ... (one device: ...)`. Off: two devices, as before |
| `NC_HLE_GUEST_RAM_IMPORT=1` | Guest RAM imported into the GPU (VK_EXT_external_memory_host on its host view): vertex arrays read in place, guarded by whole 16 KiB blocks; uploads and readbacks still copy. The log's `guest RAM imported: memtype N flags 0xe ...` or `guest RAM import ...: <why>` then `copy fallback`; every 10 s `vb in place N copied M`. `HAKUX_HLE_VB_COPY=1` keeps the copies for an A/B. Off: every vertex array copied, as before |

The core's texture, lighting and shadow options (1.0.5) each have a setting on the Picture page
(global or a game's, read once at the game's start); a `boot.txt` `env` line sets the same word
for every game. All of them are untested on the console (`open-work.md`).

| Switch (setting) | What it does, and its log lines |
| --- | --- |
| `NC_HLE_TEXROLE=0` (**Sharpness for colour textures only**, on; shown with Texture sharpness or Anisotropic filtering above Default) | On (the core's default): `NC_HLE_LODBIAS` and `NC_HLE_ANISO` apply to colour stages only (`shader/texrole.h`); lookups, normal maps, shadow buffers, render targets and alpha-only stages keep the game's sampler. `0`: every smooth, mipmapped stage, as before. `texture sharpness: by stage role (...)` or `every smooth, mipmapped stage (NC_HLE_TEXROLE=0)` once, then every 10 s (60 times at most) `texture sharpness: by stage role (N stages kept as the game set them: lookups, render targets, shadow buffers, alpha only, unread; N colour stages sharpened ...)` |
| `NC_HLE_SMALLRT_FILTER=0` (**Smooth small effects**, on) | On (the core's default): a 1x target (glow, blur chain: `surf_scale`) magnified into a scaled target is read through a cubic B-spline (4 taps) instead of one bilinear tap. Once: `1x targets read smoothly: B-spline on N stages (first draw: stages x, pixel shader / fixed function, surface id WxH)`; off: `1x targets read smoothly: off (NC_HLE_SMALLRT_FILTER=0)` |
| `NC_HLE_SHADOW_PCF=3\|5` (**Shadow softness**: Game's own (2x2) / Soft (3x3) / Softer (5x5)) | Shadow buffers' compare averaged over N x N of the NV2A's 2 x 2 compares. `shadow buffers: NxN filtering (game's own: 2x2)` when set; for any game that compares, once: `hakuX-hle-pipe: shadow compare: N draws in the first 10 s (from the first)` (no such line: the game has no shadow buffers, the setting does nothing) |
| `NC_HLE_ALPHA_AA=1\|2` (**Foliage coverage**: Off / Alpha-tested / Alpha-tested and blended) | Alpha-tested textures' mip levels keep level 0's coverage under the game's test (`tex/alpha_cov.h`); 2 also for blended draws. `NC_HLE_ALPHA_AA=N: alpha-tested textures' mip levels keep level 0's coverage` at the start, then the first 16 textures changed: `alpha coverage: texture id, WxH format ff as ff, N levels, test >= ref: levels 1-3 alpha x a b c, the smallest x d` |
| `NC_HLE_FF_PIXEL_LIGHT=1` (**Per-pixel lighting**, off) | Fixed-function (push-buffer mode, no vertex program) lights evaluated per pixel. `fixed-function lights: per pixel (NC_HLE_FF_PIXEL_LIGHT)` / `per vertex, as the console`; once: `fixed-function lighting per pixel: N draws in the first 10 s (of M): on / off` (N = 0: the game lights with vertex programs, the setting does nothing), or later `... the first lit draw at s s`; `vertex shader <hash>: lights per vertex (the pixel shader can't take them)` when a pixel shader has no place for them |
| `NC_HLE_AO=1` (**Ambient occlusion**, off), `NC_HLE_AO_STRENGTH=<%>` (50/100/150, 100), `NC_HLE_AO_RADIUS=<% of the height>` (2/4/6, 4) | Ambient occlusion at half resolution from the frame's paired depth buffer, multiplied in the display pass; what was drawn after the scene's last depth write (HUD) is left as it is. Always (AO on or off), once after 600 frames: `depth pairing, N frames: P% drawn with a depth buffer (...), transform: fixed-function perspective / vertex programs / no perspective; W-buffer, float depth; written again before the swap` and `depth pairing, a frame: N draws into it, N writing depth, N after the last of those (HUD, effects) ... AO marks`. On: `ambient occlusion: on (radius, strength; vertex programs: near far, degrees)`, `ambient occlusion: pass ready (half resolution, ...)`, and on each change `ambient occlusion: on the frames from here on` / `none from here on: <why>` (no depth buffer, float depth, written again after the frame's draws, no perspective, `vertex programs: their projection needs NC_HLE_AO_NEAR and NC_HLE_AO_FAR`). With `HAKUX_HLE_PROF` its cost is its own pass (#47) |
| `NC_HLE_AO_NEAR=<n>`, `NC_HLE_AO_FAR=<f>`, `NC_HLE_AO_FOV=<degrees>` (**Ambient occlusion near / far / field of view**, a game's own; Not set, Not set, 60) | For games drawn with vertex programs: the D3D projection the depth is read with (the game's camera planes, in its own units). The settings offer presets (near 0.1 to 10, far 100 to 50000, 45 to 90 degrees); any other value goes in `boot.txt` (`env NC_HLE_AO_NEAR=0.25`) |
| `NC_HLE_TEXUP=2\|4` (**Texture upscaling**: Off / 2x / 4x), `NC_HLE_TEXUP_BC=0` (**Upscaled texture format**: Uncompressed; default Compressed), `NC_HLE_TEXUP_MIB=64..4096` (no setting; 1024) | A texture unchanged for 60 frames that a colour stage (`texrole.h`) with linear magnification and no dependent reads sampled gets an image 2 or 4 times its size, made on the GPU at the frame's end (`tex/texup.h`); its levels s.. are the texture's own (with Foliage coverage, the rescaled ones). A DXT texture's image is in its DXT format (`tex/texbc.h`: the new levels encoded by a block pass, the game's levels copied block for block); other formats', and every one with `NC_HLE_TEXUP_BC=0`, RGBA8. `NC_HLE_TEXUP_MIB` is the images' memory budget, apart from the texture packs'. At the start `texture upscaling: Nx, textures the same for 60 frames, N MiB of images at most` (or `... not available (why)`), then every 10 s `texture upscaling Nx: N images, M of B MiB; the last 10 s: N made (Mpx; N of them in DXT blocks), N reused, N evicted, N textures back to their own, ms recording` |
| `NC_HLE_GPU_TIME=0` (no setting; on) | Every 10 s `hakuX-hle-gpu: the game's GPU work: X ms a second (P% of the time) in the last 10 s, N submissions of A ms on average, the longest L ms`: the emulator's own command buffers timed on the GPU (begin and end timestamps), apart from the presenter's. Near 1000 ms a second the game is held up by the GPU. With `HAKUX_HLE_PROF` the profile's 1 s `gpu=` line instead |

Settings > Advanced > Diagnostics has two toggles (global or a game's, both off by default, taking
effect at the next start): **One GPU device** (`NC_HLE_ONE_DEVICE=1`) and **Guest RAM imported**
(`NC_HLE_GUEST_RAM_IMPORT=1`). The log says at the start `ogbox: one GPU device: on|off, guest RAM
imported: on|off`. A settings file from before them with `core_experiment=one_device` or `ram_import`
carries over to the matching toggle once; the other old choices are ignored. The core's other A/B
switches (`HAKUX_HLE_NO_FBFETCH=1`, `HAKUX_HLE_BATCH=0`, `HLE_KICK_SPIN_MS=2`, `NC_IDLE_SPIN_MS=2`) have no
setting any more but stay in the core: a `boot.txt` `env` line or a core argument that starts `NC_`,
`HAKUX_HLE_` or `HLE_` sets them as environment switches (`ps5/src/core/hakux_core.c`).

## Method notes

- **Rule things out one at a time, cheaply.** Before a code change, look for a setting or switch
  that tests the theory with the build the owner already has (resolution scale 1x, `VISALL`, Fast
  disc drive off).
- **Compare with the console's behaviour, not with expectation.** Many "game" problems are the
  emulator doing something the NV2A or the kernel doesn't: check how the game drives the hardware
  (the push buffer's methods, kernel calls) against what hakuX does with it.
- **Caps in the data are clues.** A value that never exceeds a round limit (no draw above 31
  quads) points at a batch boundary being mishandled.
- **PC reproduction** when the logic doesn't need the console: the patch loader, the texture pack
  module, the image readers all have host tests or can be linked into a small harness. Render
  targets sharing memory (a texture taken from memory instead of a target's image, an image
  landing over the guest's data): hakux-native's `tests/cohalias`, a scenario of the title's
  passes run against a model of the console's memory, with `-vv` for each landing, upload and
  draw in order ([`hakux-fork.md`](hakux-fork.md)).
