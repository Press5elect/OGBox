# OGBox Source Provenance

OGBox is developed by Press5elect.

Repository: https://github.com/Press5elect/OGBox

## The OGBox-specific PS5 work

The following components contain OGBox's own PS5 implementation work, written by Press5elect:

- **Native CPU backend**: `native/cpu/cpu_native.c` and `native/cpu/codepatch.c` in the hakuX tree
  (branch `ps5-native` of `Press5elect/hakux-native`), with the PS5 half of the CPU in
  `ps5/patches/hakux/ps5.patch`.
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
- **Game mods** (`ps5/mods/`): byte patches found by reading each game's own executable, with how
  each was found written in its `mod.txt` (frame rate and picture mods for title IDs `54540082` and
  `545400A4`, among others).

Each file names its authors in its header. OGBox's own files carry "OGBox by Press5elect".

## What is not OGBox's own

OGBox also carries other people's work, under their licences, credited in `NOTICE`,
`ps5/notices/components.json` and the title folder's `licenses/`. Among it: hakuX (its authors),
xemu's MCPX work (pull request #3105 by raddad772), Crux2's platform code and frame generation
(Press5elect, under Crux2's own attribution term), MihawkGen (Mihawk, GPL-3.0-or-later), the AMD
FidelityFX SDK and FSR 1 (AMD, MIT), NVIDIA Image Scaling (NVIDIA, MIT), SMAA (its authors, MIT),
and the PS5 Vulkan Template, PS5_Vulkan and PS5_Mesa foundation (Mihawk). This file claims none of
it.

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
