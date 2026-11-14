# Debugging: the boot log and the diagnostic switches

## Getting a log

OGBox writes `ogbox-boot.log` in the title folder (`/data/homebrew/PPSA99251/`); the session before
is kept as `ogbox-boot.previous.log`. A session's log runs on across relaunches (starting a game
restarts the title), so one file can hold several games.

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
| `NC_APU_VOICE_LOG=1` | Each sound's sends, for mixing questions |
| `OGBOX_LOG=file` | The log always written, whatever the setting |

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
  module, the image readers all have host tests or can be linked into a small harness.
