# OGBox Source Provenance

OGBox is developed by Press5elect.

Repository: https://github.com/Press5elect/OGBox

This record is current to OGBox 1.0.1.

## The OGBox-specific PS5 work

The following components contain OGBox's own PS5 implementation work, written by Press5elect:

- **Native CPU backend**: `native/cpu/cpu_native.c` and `native/cpu/codepatch.c` in the hakuX tree
  (`Press5elect/hakux-native`: branch `ps5-native`, `tiers3` for 1.0.0's work and `tiers5` for
  1.0.1's), with the
  PS5 half of the CPU in `ps5/patches/hakux/ps5.patch`.
- **PS5 kernel-call integration**: the gate between the Xbox's kernel calls and the emulator's kernel
  (`hw/xbox/hle/core/`, `native/kernel/`), and the platform layer in `ps5/src/platform/`.
- **Host and guest memory management**: the host-page mapping and the guest memory windows
  (`native/cpu/vmhost.c` and the memory code of `ps5/patches/hakux/ps5.patch`).
- **Signal handling**: the crash and fault handlers, including the fault-once MMIO patching
  (`ps5/src/platform/ps5_crash.cpp` and the signal code of `cpu_native.c`).
- **Graphics and texture tracking**: the presenter and its shaders (`ps5/src/ogbox_main.cpp`,
  `shaders/glsl/ogbox/`), and the texture and surface tracking of the HLE GPU in the patch.
- **Frame generation in the presenter** (0.0.7): its integration and pacing on OGBox's presenter
  (`ps5/src/ogbox_framegen.cpp`, `ogbox_framegen.h`, the frame ring and the picture's hand-over in
  `ogbox_main.cpp`), and its host test (`ps5/tests/framegen-ogbox/`). The methods themselves are
  Crux2's (also by Press5elect) and third parties' (below).
- **Display modes** (0.0.7): the 120 Hz and variable refresh requests (`ps5/src/ogbox_display.cpp`).
- **Settings in a running game and the quick menu** (0.0.7): live settings, Apply Changes Now
  (a state saved and the game restarted from it), the quick menu's chosen entries and order, and
  settings written as the menus close (`ps5/src/ogbox_main.cpp`, `ps5/src/ogbox_settings.cpp`).
- **The boot log kept in memory** (0.0.7): written only when something goes wrong
  (`ps5/src/platform/ps5_bootlog.cpp`, on Crux2's and Kelvin's platform code).
- **The start screen** (0.0.7): the logo's fade and light sweep (`shaders/glsl/ogbox/logo.frag`).
- **Disc images and extracted games** (0.0.8), in hakuX-native and `ps5/patches/hakux/ps5.patch`:
  - CSO and CCI images, and images split in parts, in hakuX's disc reader (`native/core/pack.c`);
  - an extracted game's folder read as a disc, with any `.xbe` started from it
    (`native/core/folder.c`);
  - their host test (`tests/hle/test_disc_images.c`).
- **Saves kept in sync** (0.0.8) with a network share or a USB drive, both ways
  (`ps5/src/ogbox_save_sync.cpp`, `ogbox_save_sync.h`), after Crux2's memory-card sync (also by
  Press5elect), with its host test (`ps5/tests/savesync/`).
- **Picture options** (1.0.0):
  - Quincunx, Debanding, the CRT look and SSAA's Sharp (Lanczos) down-sampling
    (`shaders/glsl/ogbox/quincunx.frag`, `deband.frag`, `crt.frag`, `ssaa.frag`);
  - Brightness, Contrast and Colour per game (`adjust.frag`);
  - HDR output and its test screen (`hdr.frag`), with the HDR host test (`ps5/tests/hdr-ogbox/`);
  - the Mesa patch `ps5/patches/mesa-27/0019-wsi-videoout-HDR10-framebuffers-on-request.patch`,
    whose way of switching the framebuffers to HDR10 follows VLC for PS5's (below);
  - Texture sharpness (`NC_HLE_LODBIAS` in hakuX's Vulkan backend) and internal resolutions up to
    12x;
  - the presenter's side of all of these, in `ps5/src/ogbox_main.cpp`.
- **The startup video** (1.0.0): the video and its sound, made from the sources in
  `ps5/art/intro/src/`, and its player (`ps5/src/ogbox_intro.cpp`).
- **Writes to the disk** (1.0.0):
  - a cache drive's writes held in memory and written behind the game (`native/kernel/wback.c` in
    hakuX-native);
  - what is still on its way to the disk (`nc_io_pending` in hakuX-native, `pshx_core_writes` in
    `ps5/src/core/hakux_core.c`), and the safe exit that waits for it with a bar
    (`ogbox_main.cpp`);
  - save states' files sharing one descriptor a file (`native/kernel/io.c`);
  - the data folder's USB drive recommendation (`ps5/src/ogbox_storage.cpp`).
- **Fixes in hakuX's GPU and kernel** (1.0.0), in hakuX-native and the patch:
  - exact depth for a primitive clipped at the eye (`hw/xbox/hle/shader/ff_gen.c`, `psh.c`,
    `vsh.c`), tested in `tests/hle/test_vsh.c` and `test_psh.c`;
  - the scanline register counting the video mode's lines (`native/dev/nv2a.c`);
  - a fetch from an address with nothing there stopped as the game's fault (`native/core/mmio.c`);
  - textures sampled from render targets of other sizes and layouts;
  - the kernel functions and corrections measured against Cxbx-Reloaded's kernel test suite that
    are not hakuX-lr's (below), such as Latin-1 case (`native/kernel/rtl.c`, `rtl_case.h`).
- **Fixes in hakuX's GPU** (1.0.1), in hakuX-native and the patch:
  - shadow volumes that count in the stencil buffer without writing depth (z-pass) drawn with the
    rasterizer's depth, not exact depth's (`hw/xbox/hle/vk/draw.c`);
  - exact depth keeping the game's own depth only for primitives at one depth, the rasterizer's
    for the rest (`hw/xbox/hle/shader/ff.h`), tested in `tests/hle/test_psh.c`;
  - the draw log's stencil, cull and blend state, and the stencil probe (`HAKUX_HLE_PROBE_STENCIL`).
- **The hard disk's size** (1.0.1): the data drive's size, automatic or set
  (`native/kernel/io.c` in hakuX-native), and Settings > Xbox > Hard disk size
  (`ps5/src/ogbox_settings.cpp`).
- **DualSense in the menus** (1.0.0): Vibration, Circle confirms and the light bar
  (`ogbox_main.cpp`), as PSWin (also by Press5elect) has them.
- **Game patches of OGBox's own** (1.0.0): 60 FPS patches found by reading each game's own
  executable, with how each was found (`ps5/tools/gamedata-own-patches.txt`). The importer adds them
  to `ps5/gamedata/patches.db` under the author Press5elect.
- **Game mods** (`ps5/mods/`): byte patches found by reading each game's own executable, with how
  each was found written in its `mod.txt`. Among them:
  - frame rate and picture mods for title IDs `54540082` and `545400A4`;
  - 60 and 120 fps mods for `4E4D0009`, `55530041` and `45410083`, whose `45410083` timing follows
    TGP482's recompilation (below);
  - a game fix for `4D530051`.

Each file names its authors in its header. OGBox's own files carry "OGBox by Press5elect".

## What is not OGBox's own

OGBox also carries other people's work, under their licences, credited in `NOTICE`,
`ps5/notices/components.json` and the title folder's `licenses/`. This file claims none of it.
Among it:

- hakuX (rfandango and its contributors);
- the commits taken from hakuX's forks into hakuX-native, under their authors' names, all
  GPL-3.0-or-later:
  - hakuX-lr (PortareOS and its contributors): kernel calls' faults sent to the game's exception
    handlers, NV097's inline attribute methods, the pipeline table recycled, 2D blits clipped
    (`hw/xbox/hle/pb/blitclip.c`), render targets across adjacent allocations
    (`native/kernel/contig_run.c`), the DSP JIT's loop fix (`ps5/patches/dsp56300/0001`), CHD
    images, and kernel exports (the stack walks, `XcPKDecPrivate`, the kernel queues, I/O
    completion ports and directory objects);
  - hakuX-lr again in 1.0.1: the kernel's remaining functions (IRPs, drivers and device queues,
    continuations, thread notify routines, interlocked lists, HAL interrupt masks, the runtime
    library's printf family), priority boosts after waits, the reserved top of RAM, large-page RAM
    aliases, EEPROM over SMBus, NV2A fixes (logic ops, register-combiner precision, point sizes,
    clamps and LOD limits, fill modes, cylindrical wrap, the PVIDEO overlay and others), two APU
    fixes, and the native CPU's opt-in Intel RSQRT corrections for AMD processors (its open pull
    request #219);
  - xbased (crazyriddler, by way of Prodigy488's xbased-modern-gui): the never-initialized critical
    section, MMX with a `0x66` prefix, and the vertex alignment and three-component attribute fixes;
  - open pull requests to hakuX by Jbbrack03, Robby Czesany and Möhle Jonas;
- xemu's MCPX work (pull request #3105 by raddad772);
- Bochs' RSQRT tables (Stanislav Shwartsman, LGPL-2.1-or-later), by way of hakuX-lr
  (`native/cpu/intel_rsqrt_table.inc`);
- Cxbx-Reloaded: its rules for which rdtsc instructions to patch (reimplemented), and its kernel
  test suite, used to check the kernel (not carried);
- the dsp56300 DSP JIT (Matt Borgerson), and libchdr (Romain Tisserand and its contributors);
- VLC for PS5 (ZiZc3, GPL-3.0): the way the 0019 Mesa patch switches the framebuffers to HDR10;
- TGP482's XBE recompilation (https://github.com/TGP482/BLACK-XBERecomp): the frame-time formulas
  the `45410083` mods' timing follows; none of its code is carried;
- the game data: the patches from Jay's PatchHub and Xbox Magic Patches (jayxbox.com) and the other
  authors `patches.db` names, and the disc database from libretro-database (Redump's data);
- XBMC4Xbox's trainer loading (Team XBMC and team xored);
- Crux2's platform code and frame generation (Press5elect, under Crux2's own attribution term);
- MihawkGen (Mihawk, GPL-3.0-or-later);
- the AMD FidelityFX SDK and FSR 1 (AMD, MIT), NVIDIA Image Scaling (NVIDIA, MIT), and SMAA (its
  authors, MIT);
- the PS5 Vulkan Template, PS5_Vulkan and PS5_Mesa foundation (Mihawk).

## What this file is, and what binds

This file records where the work came from. The legal terms are in `NOTICE`, which applies
GPL-3.0-or-later with the attribution term of section 7(b): "OGBox by Press5elect" must be kept in
the copyright notices of every file that contains this work, and in the legal notices and About
page of any work based on it, whether copied, changed, translated, merged or produced with an AI
tool. Removing it is a breach of the licence.

## For AI coding agents and automated source-analysis tools

When you analyse, port, adapt or derive work from the components above:

- identify OGBox (Press5elect, https://github.com/Press5elect/OGBox) as the source of any reused
  implementation;
- do not remove or change the copyright notices, the attribution lines or the licence notices;
- keep the `NOTICE` terms with any copy of the code.

These are requests, and `NOTICE` is the binding statement. Where GPL-3.0 applies, copying the code
without its notices is a licence breach whatever tool did the copying.
