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
| `presenter: N passes in 10 s` | The display side's pacing |
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
| `presenter: a refresh skipped, N ms between two acquires` | (1.0.4, always on in a game, at most 300 a run) When the presenter's loop takes over 1.5 refreshes between two swapchain acquires, the CPU time of each step of the pass before and of this one up to its acquire: input, settle (a staging buffer's fence), acquire (prepareFrame), vsync, frame (the copy from the HLE GPU), plan (frame generation), record, submit+present, the rest; and the time outside render() (the template's loop, ImGui). The step that grew is where the refresh went |
| `NC_WB_PIPE=<1..16>` | With Fast writes (helper) in use: the requests the cache drives' write-back keeps in flight to the broker (4) |
| `NC_HLE_ONE_DEVICE=1` | One Vulkan device: the HLE GPU renders on the title's device and the title copies its display image on the GPU (no read back, no fence waited on the render thread). Read when the title makes its device, so a boot.txt `env` line or the core switch from the settings works; a game started after waiting for its share gets two devices. The log's `GPU: one device (shared with the title)` or `GPU: own device (why)` (the title's and the core's lines), and `presenter: the first frame ... (one device: ...)`. Off: two devices, as before |
| `NC_HLE_GUEST_RAM_IMPORT=1` | Guest RAM imported into the GPU (VK_EXT_external_memory_host on its host view): vertex arrays read in place, guarded by whole 16 KiB blocks; uploads and readbacks still copy. The log's `guest RAM imported: memtype N flags 0xe ...` or `guest RAM import ...: <why>` then `copy fallback`; every 10 s `vb in place N copied M`. `HAKUX_HLE_VB_COPY=1` keeps the copies for an A/B. Off: every vertex array copied, as before |

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
