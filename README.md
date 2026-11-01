# PSHakuX

**An unofficial port of [hakuX](https://github.com/rfandango/hakuX), an original Xbox (2001)
emulator, to the PlayStation 5, as a homebrew title (`PPSA99251`) for a jailbroken console.**
PSHakuX by Press5elect.

> **Unofficial.** This is not the hakuX project's build and it is not supported by hakuX.
> Please do not report PSHakuX's problems to hakuX: open them here.
>
> **Free, and on GitHub only.** PSHakuX costs nothing. A copy anyone asks money for, or one from
> somewhere else, is not this release.
>
> **No piracy.** No games, Xbox BIOS, boot ROM, EEPROM, firmware or keys are in this repository
> or in any release, and none will be. Use only backups of games you own, made yourself.

## Where it is

Bring-up. The title builds and links hakuX's native core with its **native CPU backend**: the
Xbox's x86 code runs on the console's own processor in compatibility mode (hakuX-native's
`native/cpu/cpu_native.c`, branch `ps5-native`), with kernel calls through a far-call gate, its
HLE GPU on the console's RADV, its APU on the console's sound output, a bring-up front end and
the console's platform code. On Linux the same backend runs nxdk's `hello` sample to its main
loop. **Nothing has run on the console yet**: this build is the first console test. The bring-up
target is nxdk's `hello` (no push-buffer library: nxdk's pbkit writes to 0x80000000, which on the
console is the title's own runtime module and can't be touched).

## Using it

1. Copy the `PPSA99251` folder to `/data/homebrew/` on the console.
2. Copy `boot.example.txt` in it to `boot.txt` and write the path of your Xbox disc image
   (an XISO) on its first line, with hakuX's options after it (`scale=2`, `uncap`, ...).
3. Start PSHakuX from the home screen. Hold **OPTIONS and the touch pad** for two seconds to quit.

The controller is an Xbox controller: cross A, circle B, square X, triangle Y, R1 black,
L1 white, L2/R2 the triggers, OPTIONS start, the touch pad back. Each signed-in user's controller
is a player (1-4). The emulated hard disk lives in `/data/pshakux/hdd`, the shader caches in
`/data/pshakux/cache`, outside the title folder, so an update is just replacing the folder.
hakuX's core cannot yet stop one game and boot another in the same process, so a new boot is a
relaunch of the title with arguments (`--boot <image>`, `--loadstate <file>`, `--relaunch`,
`--hdd <folder>`, `--option <opt>`); a game's own reboot into another XBE relaunches it that way.
`pshakux-boot.log` in the title folder says what happened (the run before is kept as
`pshakux-boot.previous.log`).

## Building

It builds on my PS5 stack, checked out beside this repository: `PS5_Vulkan` (the RADV archive,
the link recipe, the native tool, `libc.prx`), `PS5_PayloadSDK` (the SDK fork, pinned) and,
to make the RADV archive again, `PS5_Mesa`.

```bash
PS5_CLANG=/usr/lib/llvm-20/bin/clang bash ps5/tools/build.sh       # dist/PPSA99251/
PS5_CLANG=/usr/lib/llvm-20/bin/clang HAKUX_SRC=../hakuX-native bash ps5/tools/build.sh
                                                    # hakuX from a work tree (the native CPU backend)
python3 ps5/tools/stage-notices.py --check dist/PPSA99251   # the release check
ps5/tools/deploy.sh                                 # upload over the console's FTP server
```

LLVM 20's clang is needed: with clang 18, RADV's weak imports do not link.

hakuX's source is not kept here. `ps5/tools/fetch-hakux.sh` fetches it at the pinned revision into
`vendor/hakuX`, untouched; `ps5/tools/prepare-hakux.sh` copies it, or the tree `HAKUX_SRC` names,
into `build/hakux-src` and lays `ps5/patches/hakux/*.patch` over the copy (a patch a work tree
already has, or changed, is left out and said). What the console needs instead of hakuX's
Android parts is in `ps5/hakux/`, not in hakuX:

| hakuX on Android | PSHakuX on the PS5 |
| --- | --- |
| FEXCore (the guest CPU, `native/cpu/cpu.cpp`) | the native backend (`native/cpu/cpu_native.c`, `NC_CPU=native`), a weak stub until it is there (`ps5/hakux/cpu/cpu_stub.c`) |
| AAudio (`native/dev/apu/out.c`) | `sceAudioOut`, 48 kHz, 256-sample blocking writes (`ps5/hakux/apu/out_ps5.c`) |
| the activity's window and its swapchain (`hw/xbox/hle/vk/swapchain.c`) | a host-owned image: the front frame handed to the title, which draws it on its own display (`ps5/hakux/hle/present_host.c`) |
| AHardwareBuffer guest RAM (zero copy) | hakuX's own copy fallback |
| system properties, prctl, eventfd, gettid, memfd | `ps5/hakux/compat` (properties from `boot.txt` and `props.txt`; memfd through the platform layer) |
| SDL3, JNI, the app (`native/app`) | the bring-up front end, `ps5/src/pshakux_main.cpp`, on the Core seam (`ps5/src/core/pshakux_core.h`) |

The front end talks to the emulator only through the Core (`ps5/src/core/pshakux_core.h`):
the full front end comes later on the same calls.

## Credits

- **hakuX** by rfandango and its contributors (GPL-3.0-or-later): the emulator. Its MCPX APU is
  xemu's (LGPL-2.1-or-later), with Hatari's DSP56300 emulation (GPL-2.0-or-later); its kernel
  export table is nxdk's (CC0).
- **Crux2** and **Kelvin** by Press5elect: the console platform code (the boot log, the crash
  report, the clock, the thread priorities, the virtual file layer), the build and release tools,
  and the high-base link.
- **PS5 Vulkan Template, PS5_Vulkan, PS5_Mesa and the payload SDK fork** by Mihawk: the
  foundation, RADV on the console and the platform layer.
- libslirp, nv2a_vsh_cpu, xxHash, glslang, volk, Dear ImGui, libktx, tinygltf, GLM, libsmb2,
  libnfs, zlib, Mesa, LLVM's runtime: every part, its authors and its licence are in
  `ps5/notices/components.json`, and in `licenses/README.txt` in the title folder.

## Licence

PSHakuX is GPL-3.0-or-later (`LICENSE`). Press5elect's own code carries an attribution term
under the GPL's section 7(b): keep "PSHakuX by Press5elect" (and "Crux2 by Press5elect" or
"Kelvin by Press5elect" for the files made from those projects) in the source notices and the
legal notices of any work based on it, however it is made, with AI tools included. The term
covers Press5elect's code only, not hakuX's or xemu's. `NOTICE` has it in full.

"Xbox" is a trademark of Microsoft Corporation; "PlayStation", "PS5" and "DualSense" are
trademarks of Sony Interactive Entertainment. This project is not affiliated with either.

## Release notes

### 0.1.2 (bring-up)

Fixed
- Quitting (OPTIONS and the touch pad) no longer crashes: the picture's descriptor pool was
  destroyed twice.

### 0.1.1 (bring-up)

Fixed
- The game's code no longer ends the title at its first interrupt or device access: signal
  handlers now run on their own stack (sigaltstack is imported as usual, and the stack's memory
  is touched up front), not on the game's stack, where the console could not write the signal
  frame and closed the title with SIGILL.
- Thread priorities are applied (with pthread_setschedparam); 0.1.0 left every thread at the
  title's priority.

### 0.1.0 (bring-up)

**Added**
- The title (`PPSA99251`) and its build: hakuX's native core, HLE GPU and APU for the console,
  with RADV, the platform layer and Crux2's and Kelvin's console code.
- The native CPU: the guest's x86 code runs on the console's CPU in compatibility mode (guest
  CS 0x33, GS the KPCR), kernel calls through a far-call gate, rdtsc scaled to the Xbox's
  733 MHz, guest memory in 16 KiB pages on direct memory, kept clear of 0x80000000-0x80120000.
- A bring-up front end: `boot.txt`, the picture on the console's display, four controllers,
  a boot log with a crash report (the guest's registers too) and a heartbeat, and a quit
  combination.

**Changed**
- Nothing yet.

**Fixed**
- Nothing yet.
