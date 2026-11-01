# OGBox

**An unofficial port of [hakuX](https://github.com/rfandango/hakuX), an original Xbox (2001)
emulator, to the PlayStation 5, as a homebrew title (`PPSA99251`) for a jailbroken console.**
OGBox by Press5elect. It was called PSHakuX up to 0.1.5.

OGBox is built on hakuX (rfandango and contributors, GPL-3.0-or-later) and on the parts of hakuX
that come from xemu (the MCPX APU, LGPL-2.1-or-later and GPL-2.0-or-later). It is not hakuX's or
xemu's work and neither project supports it.

> **Unofficial.** This is not the hakuX project's build and it is not supported by hakuX.
> Please do not report OGBox's problems to hakuX: open them here.
>
> **Free, and on GitHub only.** OGBox costs nothing. A copy anyone asks money for, or one from
> somewhere else, is not this release.
>
> **No piracy.** No games, Xbox BIOS, boot ROM, EEPROM, firmware or keys are in this repository
> or in any release, and none will be. Use only backups of games you own, made yourself.

## Where it is

Bring-up. The title builds and links hakuX's native core with its **native CPU backend**: the
Xbox's x86 code runs on the console's own processor in compatibility mode (hakuX-native's
`native/cpu/cpu_native.c`, branch `ps5-native`), with kernel calls through a far-call gate, its
HLE GPU on the console's RADV, its APU on the console's sound output, a bring-up front end and
the console's platform code. On the console (firmware 13.09) 0.1.1 and 0.1.2 boot nxdk's `hello`
from an XISO and show its picture. Games built with Microsoft's XDK read the Xbox kernel's header
at 0x80010000, where the console loads the title's own runtime module (`libc.prx`, execute-only),
and the console ended the title there (Burnout 3 with 0.1.2 and 0.1.3). 0.1.4 takes that range
back from `libc.prx` at start-up, as PSHakuX System Check 0.0.5 did on the console, and puts the
guest's kernel at 0x80010000; whether XDK games then boot is what 0.1.4's console runs will show.
`ogbox-boot.log` says whether the takeover was done (`takeover:` lines) and which layout the
core used (`core:` lines). With 0.1.4 Burnout 3 boots, runs its loading screen at 60 fps and
stays there; 0.1.5 adds the boot-log diagnostics that say what it waits for.

## Using it

1. Copy the `PPSA99251` folder to `/data/homebrew/` on the console.
2. Copy `boot.example.txt` in it to `boot.txt` and write the path of your Xbox disc image
   (an XISO) on its first line, with hakuX's options after it (`scale=2`, `uncap`, ...). A path
   without a leading `/` is in the title folder (`games/My Game.iso`). With the payload loader
   (elfldr, port 9021) running, OGBox leaves its sandbox at start, so the image can be anywhere:
   `/data/...`, a USB drive (`/mnt/usb0/...`) or extended storage (`/mnt/ext0/...`). A file
   named `elevation-off.txt` in the title folder keeps it inside the sandbox.
3. Start OGBox from the home screen. Hold **OPTIONS and the touch pad** for two seconds to quit.

The controller is an Xbox controller: cross A, circle B, square X, triangle Y, R1 black,
L1 white, L2/R2 the triggers, OPTIONS start, the touch pad back. Each signed-in user's controller
is a player (1-4). The emulated hard disk lives in `/data/ogbox/hdd`, the shader caches in
`/data/ogbox/cache`, outside the title folder, so an update is just replacing the folder.
(Up to 0.1.5 the folder was `/data/pshakux`: the first start of 0.1.6 renames it to
`/data/ogbox` when there is no `/data/ogbox` yet, and the boot log says so.)
hakuX's core cannot yet stop one game and boot another in the same process, so a new boot is a
relaunch of the title with arguments (`--boot <image>`, `--loadstate <file>`, `--relaunch`,
`--hdd <folder>`, `--option <opt>`); a game's own reboot into another XBE relaunches it that way.
`ogbox-boot.log` in the title folder says what happened (the run before is kept as
`ogbox-boot.previous.log`).

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

| hakuX on Android | OGBox on the PS5 |
| --- | --- |
| FEXCore (the guest CPU, `native/cpu/cpu.cpp`) | the native backend (`native/cpu/cpu_native.c`, `NC_CPU=native`), a weak stub until it is there (`ps5/hakux/cpu/cpu_stub.c`) |
| AAudio (`native/dev/apu/out.c`) | `sceAudioOut`, 48 kHz, 256-sample blocking writes (`ps5/hakux/apu/out_ps5.c`) |
| the activity's window and its swapchain (`hw/xbox/hle/vk/swapchain.c`) | a host-owned image: the front frame handed to the title, which draws it on its own display (`ps5/hakux/hle/present_host.c`) |
| AHardwareBuffer guest RAM (zero copy) | hakuX's own copy fallback |
| system properties, prctl, eventfd, gettid, memfd | `ps5/hakux/compat` (properties from `boot.txt` and `props.txt`; memfd through the platform layer) |
| SDL3, JNI, the app (`native/app`) | the bring-up front end, `ps5/src/ogbox_main.cpp`, on the Core seam (`ps5/src/core/ogbox_core.h`) |

The front end talks to the emulator only through the Core (`ps5/src/core/ogbox_core.h`):
the full front end comes later on the same calls.

## Credits

- **hakuX** by rfandango and its contributors (GPL-3.0-or-later): the emulator. Its MCPX APU is
  xemu's (LGPL-2.1-or-later), with Hatari's DSP56300 emulation (GPL-2.0-or-later); its kernel
  export table is nxdk's (CC0).
- **OGBox's logo** (the green X and the name) by Press5elect: `ps5/art/ogbox-logo.png`, the
  title's icon and launcher pictures (`ps5/tools/make-title-art.py`) and the start screen.
- **Crux2** and **Kelvin** by Press5elect: the console platform code (the boot log, the crash
  report, the clock, the thread priorities, the virtual file layer), the build and release tools,
  and the high-base link.
- **PS5 Vulkan Template, PS5_Vulkan, PS5_Mesa and the payload SDK fork** by Mihawk: the
  foundation, RADV on the console and the platform layer.
- libslirp, nv2a_vsh_cpu, xxHash, glslang, volk, Dear ImGui, libktx, tinygltf, GLM, libsmb2,
  libnfs, zlib, Mesa, LLVM's runtime: every part, its authors and its licence are in
  `ps5/notices/components.json`, and in `licenses/README.txt` in the title folder.

## Licence

OGBox is GPL-3.0-or-later (`LICENSE`). Press5elect's own code carries an attribution term
under the GPL's section 7(b): keep "OGBox by Press5elect" (and "Crux2 by Press5elect" or
"Kelvin by Press5elect" for the files made from those projects) in the source notices and the
legal notices of any work based on it, however it is made, with AI tools included. The term
covers Press5elect's code and logo only, not hakuX's or xemu's. `NOTICE` has it in full.

"Xbox" is a trademark of Microsoft Corporation; "PlayStation", "PS5" and "DualSense" are
trademarks of Sony Interactive Entertainment. This project is not affiliated with either.

## Release notes

### 0.1.6 (bring-up)

Added
- OGBox's logo on the screen from the start until the game's first frame.

Changed
- PSHakuX is now OGBox: the title's name, its boot log (`ogbox-boot.log`,
  `ogbox-boot.previous.log`), the crash report (`==== OGBox stopped`), the package
  (`OGBox-<version>.zip`), the notices and the source. The title ID stays `PPSA99251`.
- A new logo, home-screen icon and launcher pictures, made from Press5elect's logo; the
  temporary icon is gone.
- The data folder is `/data/ogbox`: at start-up an existing `/data/pshakux` is renamed to it
  once, when there is no `/data/ogbox` yet.
- The host's code entries at 0x80000000 are checked and put back every 10 ms (was 100 ms).

Fixed
- The console ended the title at a game's loading screen with `SYSTEM_XO_VIOLATION` (0.1.5): the
  page of the host's code entries at 0x80000000 was made read-only while staying executable,
  which the PS5 makes execute-only, so the game's read of it was fatal. That page is no longer
  protected on the PS5, and write tracking never leaves a guest page readable and executable
  without write there: a page it tracks is read-only without execute until it is written.

### 0.1.5 (bring-up)

Added
- Boot-log diagnostics for a game that runs on without progressing (Burnout 3 stays on its
  loading screen with 0.1.4): every 5 s the APU's state (`hakuX-apu: per s:` GP frames, why they
  stop, its interrupt raised and acknowledged, the voices playing, the output ring and grains,
  DirectSound's mailbox waits); every 10 s where the guest's time goes (`guest:` the busiest guest
  EIPs and the code at the first); every 30 s the guest's threads with what each waits on; the
  busiest device-register sites with the register they reach; the native CPU's signals by kind.
- `env NAME=VALUE` lines in `boot.txt`: hakuX's environment switches (diagnostics).
- The page of `libc.prx`'s old entries (guest physical page 0) is watched: a write there is let
  through and logged with the writer's address, and the entries are put back within milliseconds.

Changed
- The native CPU's code sweep also patches `rdtsc`, `cpuid` and `pushfd`/`popfd` at the start of
  short functions (a branch target): Burnout 3's own timer, D3D's and DirectSound's were left
  running unscaled or without the guest's interrupt flag.
- An `fs:` access that faults is emulated and made `gs:` (the KPCR), so it runs natively from then
  on: on the console it never traps as it does on Linux, and the kernel's own IRQL and
  critical-section paths took about 8,000 faults a second.
- hakuX's per-page "mapped over what was there" lines are gone (one for the taken-back range):
  each was a log line per 16 KiB of every allocation (a 5 MiB one held the CPU 235 ms).

Fixed
- The crash at quit (OPTIONS and the touch pad): hakuX's HLE GPU loaded volk's instance table from
  its own instance, made without the display extensions, and the title's swapchain cleanup called
  `vkDestroySurfaceKHR` through 0. hakuX keeps the title's table; the title checks it when the
  first frame comes and at quit and loads it again if anything changed it.

### 0.1.4 (bring-up)

Added
- The takeover of 0x80000000-0x80200000 from the title's `libc.prx` at start-up (the system
  check 0.0.5's step): every pointer into it outside it is pointed at a copy in the eboot, then
  the guest's memory is mapped over it when the core starts, with stubs at `libc.prx`'s old code
  entries (the thread destructors libkernel still calls at every thread exit). Every step is in
  the boot log (`takeover:` lines); when it cannot be done the 0.1.3 layout is kept and the boot
  log says why. Flag files in `/data/ogbox/`: `takeover-off` (not tried), `takeover-force`.
- The heartbeat says when a stub at the old entries was written over (by the game or a device)
  and put back.

Changed
- With the takeover done, the guest's kernel area is at 0x80010000 and its KPCR at 0x80020000,
  as on the Xbox (was past `libc.prx`, from 0x80120000); the hole is chosen at run time.
- The APU's voice workers and its output thread run at the sound priority (690); 0.1.3 left them
  at 700.
- Savestates made with the other layout of the 0x80000000 window are refused, with a reason.
- The payload SDK fork is pinned at e105cf6 (fdv tracks as many files as it has descriptor
  slots).

Fixed
- The read of the kernel's header at 0x8001003c that ended XDK games (Burnout 3) now reads the
  guest's own kernel image when the takeover is done (proven in the system check; not yet with a
  game in OGBox).

### 0.1.3 (bring-up)

Added
- Leaving the sandbox at start, as Crux2 does (`sandbox-elevator.elf`, BlackBearReloaded's
  ps5-native-app-boilerplate): disc images on `/data`, USB drives and extended storage; every
  step is in the boot log (`sandbox:` lines). `elevation-off.txt` keeps the title inside.
- Disc and hard-disk paths in `boot.txt` and the arguments may be relative to the title folder.
- The boot log names every thread hakuX starts with its priority role and priority, and says
  why a disc image cannot be opened.
- The native CPU writes its last words (a fatal signal no handler takes, an unhandled system op,
  a handler fault loop, a nearly full signal stack) into the boot log at once.

Changed
- Thread priorities for all of hakuX's threads: what the game's timing waits on (the GPU's
  vblank and timer, the decoder, render and timeline threads, the kernel clock, USB, sound, disc
  reads, the network, the hitch monitor) at 690 ahead of the vCPU (700); pipeline compiles, the
  pipeline cache, file closing and savestate packing at 730. Each hakuX thread also gets its own
  crash-report stack.
- The hitch monitor samples where the native CPU really is (its EIP in the game's code, or the
  kernel call it is in), and counts the kernel's idle wait as halted: a game that sleeps is
  reported as "guest idle", not as a hang.
- The native CPU's signal and gate stacks are 8 MiB (were 2 MiB).
- The guest's kernel area relocation is a build setting (`PSHAKUX_WINDOW_HOLE_END`, 0 puts it at
  0x80010000 for a layout that leaves that range free); the boot log says which is built.

Fixed
- The native CPU's code sweep no longer rewrites data: sections such as `.rdata` and `.data`
  are skipped even when the game marks them executable (Burnout 3 had ~400 bytes of its
  constants turned into traps).
- hakuX's threads named by their creator (the HLE GPU's, the APU's) now get their priority;
  0.1.2 applied it only to a thread naming itself.

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
