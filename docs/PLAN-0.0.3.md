# OGBox: the fixes planned after 0.0.2 (5 November 2026)

Research done for this plan:
- the repository's issues;
- upstream hakuX: still at the pinned `a7cdb5a975`, with nothing newer;
- xemu's commits since July 2026: xemu is at v0.8.139, master `6095054b7d`;
- XPSemu: another original Xbox emulator for the PS5, ported from xemu, Alpha 1 released on
  1 October.

The work is listed in order. Each step ends with a build that fixes something; none adds
logging only.

## 1. Reported problems

### 1.1 Issue #2: Fable: The Lost Chapters shows "CORE Failed"

The disc format isn't the cause. hakuX reads both an XISO and a full redump ISO: it looks for
the game partition at 0, 0x18300000, 0xFD90000 and 0x2080000 (`native/core/xdvdfs.c`). The
reporter saw the same failure with both.

"CORE Failed" comes from one of three places in `ps5/src/core/hakux_core.c`:
- the image cannot be opened;
- `nc_core_boot` fails (the XBE's load, the kernel or the memory);
- the guest stops (`k->stop`).

The boot log's line `core: boot failed:` or `core: the run ended:` says which.

This session cannot download the reporter's two logs, because GitHub refuses the attachment
links. The owner downloads them from the issue and attaches them. They were already posted,
so this is not a request for a new log.

XPSemu (xemu) reaches gameplay in this game at 10 to 30 fps, so it is a hakuX problem, and
worth fixing. Fable is 64 MB, and loads its levels by streaming from the disc. The suspects,
once the log is read:
- a kernel call hakuX doesn't implement, or implements in part;
- an XBE section flag;
- the 128 MB check in `console_ram`'s Auto mode.

### 1.2 The 0.0.3 front end (test1)

Fix whatever the owner reports. If the title doesn't start at all, suspect the USB keyboard's
`libSceSysmodule` imports first: build without `ps5_keyboard.cpp` to confirm.

## 2. Graphics: xemu's NV2A fixes since our last port

We ported xemu's fixes up to mid-October in 0.0.2 (`897db38`: fog carryover, clear alpha,
nearest readback, `PFIFO DMA_CTL`). These came after it, or weren't checked then.

hakuX draws with its own HLE renderer (`hw/xbox/hle/`), not with xemu's pgraph. So each fix is
checked against hakuX's code first. The checks use abaire's nxdk_pgraph_tests and their
results from real hardware.

| xemu | Fix | Where in hakuX |
|---|---|---|
| `aa54c34` | Vertex shader register carryover (fog, through nv2a_vsh_cpu; seen in Blade 2) | `hle/shader/vsh*`, `pb/pbdraw.c` |
| `0103939` | R12 read by paired MAC+ILU operations that write oPos | `hle/shader/vsh*` |
| `03b988c`, `4c236c7` | R6G5B5 textures are not signed; uploaded as 4-component SNORM | `pb/pbres.c`, `vk/draw.c` |
| `8f12d6a`, `49bfab0` | A4R4G4B4 as B4G4R4A4 with a swizzle; RGBA swizzles for colours | `vk/vkdev.c`, `vk/draw.c` |
| `ac72edf` | Y16 texture formats | `vk/draw.c` |
| `578094b` | Overlapped dirty draws resolved before a 2D blit | `pb/pbdraw.c` (blits) |
| `f7ec4e6` | A swizzled texture used as a linear render target for a clear (Tron 2.0) | `pb/pbdraw.c` `texture()` |
| `25e1ee2` | Memory marked dirty after a blit and a surface download | `pb/pbdraw.c` |
| `136fa4f` | Spotlight falloff | `shader/ffhw.c`, `ff_gen.c` |
| `5c59ae2` | Texgen's "infinite viewer" model | `shader/ff_gen.c` |
| `5c516ff`, `04f12b5` | Specular parameters; ambient and emissive from the material colour source | `shader/ffhw.c` |
| `e7a681c` | Pixel shader passthru values clamped to 0..1 | `shader/psh.c` |
| `41fa73d` | Point sprites only when drawing points | `pb/kelvin.c`, `shader/psh.c` |
| `5c6c550` | Clears of X1A7 formats | `pb/pbdraw.c` |
| `f78d85e` | Colour and depth offsets given as RAM addresses | `pb/pbdraw.c` |
| `d73326b`, `75650bd` | PTIMER alarm interrupts; rounding in the timer's scheduling | `native/dev/nv2a.c` |
| `bff2145` | The APU's `XGSCNT` returns the sample count | `native/dev/apu/apu.c` |

## 3. Fewer logs: OGBox writes the report itself

The owner shouldn't have to send logs for each guess.
- **Automatic captures.** When a game stops, the core fails, a game hangs (no frames for 3 s),
  or a stretch runs below 50 fps, OGBox saves a report to `/data/ogbox/reports/<game>-<time>/`.
  It holds:
  - the boot log;
  - the last 5 s of `hitch.c`'s frame stages;
  - the draw log, armed by the anomaly itself, as `loading_watch()` does now;
  - the last frame as a PNG;
  - the settings and mods in use.
  A toast says "A report was saved".
- **Settings > About > Reports.** Lists the reports, packs one into a ZIP on a USB drive, and
  shows the text to paste into a GitHub issue.
- **Fix the build that has the data:** before each fix build, run the test game's report
  through a host-side checker, so a build is never sent just to collect data.

## 4. Speed

- **Texture dirty tracking.** Write-protect a texture's guest pages, or use the existing
  `obj_page_maybe` bitmap and a write generation, instead of hashing textures again each frame.
  This targets the 30 fps drops above roughly 8,800 draws a frame.
- **A benchmark mode** (the quick menu, or `boot.txt`). It plays 60 s of a game from a save
  state and writes the minimum, median and 1% frame times to a report, to compare builds
  without guessing.
- **Look again at the display writeback when the title stops flipping** (reverted in 0.0.2,
  because of a crash in the guest's sound interrupt handler). Write back only on the render
  thread, never while the APU's interrupt handler runs.

## 5. The front end, after test1

- Redraw the older screens (mods, data folder, hard disk manager, quick menu, boot) with
  `ogbox_ui`, so they match the home and the settings.
- **Find servers for the share form.** Send an SMB NetBIOS name query and an mDNS `_smb._tcp`
  query on the local network, and list what answers, so a server is chosen instead of typed.
- **A compatibility mark per game** on the home's chips (plays well, playable, starts, broken),
  from a list in the repository that the owner updates.

## 6. Later (on hold)

- **Compressed and split game images.** Read `.cci` (Cerbios Compressed Image: an ISO in
  fixed-size LZ4-compressed blocks with a block index; format as Team Resurgent's Repackinator
  writes it) and `.cso`, and images split in parts (`game.1.iso` + `game.2.iso`, the same for
  `.cci`). In hakuX's image reader next to ISO and CHD, and in the game library's scan. Less
  space on the drive and less to read over a network share; images people already made for
  their consoles play as they are. Idea from Cerbios (closed source: nothing of its code used).
- **The XBE's 64 MB limit at 128 MB.** hakuX never reads the XBE's init flags (header 0x124);
  `XINIT_LIMIT_DEVKIT_MEMORY` (0x4) keeps a title to 64 MB on a 128 MB console with a stock
  kernel (Cerbios's `DisableLimitMem`, off by default). Today Memory: 128 MB gives every game
  128 MB, likely why "a few games hang with it". Honour the flag at 128 MB (the kernel's highest
  page, MmQueryStatistics, contiguous allocations below 64 MB for such a title); a 128 MB patch
  (720p mods) clears the flag. Then 128 MB can be safe for all games.

## Order

1. Issue #2, as soon as its two logs are attached.
2. Fix the test1 reports.
3. The xemu table, one build for the vertex shader fixes and one for the texture and blit
   fixes.
4. The automatic reports.
5. Texture dirty tracking and the benchmark.
6. The front end's remaining screens.
7. Release 0.0.3 when the owner asks.
