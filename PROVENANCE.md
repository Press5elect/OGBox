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

Each file names its authors in its header. OGBox's own files carry "OGBox by Press5elect".

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
