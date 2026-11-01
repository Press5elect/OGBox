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
stays there; 0.1.5 adds the boot-log diagnostics that say what it waits for, and 0.1.8 logs
the APU's voices and the guest threads' waits, to find what that loading screen waits for.

## Using it

1. Copy the `PPSA99251` folder to `/data/homebrew/` on the console.
2. Put your Xbox disc images (XISOs, `.iso` or `.xiso`) on the console (`/data/...`), a USB
   drive, extended storage or a network share. For a share, add a `path =` line for it to
   `/data/ogbox/network.cfg` (written with examples on the first start): `192.168.1.10/Games/Xbox`
   for SMB, with `user =` and `password =` lines if the share needs them, or
   `nfs://192.168.1.20/volume1/games/xbox` for NFS. With the payload loader (elfldr, port 9021)
   running, OGBox leaves its sandbox at start, so USB drives and extended storage are visible.
   A file named `elevation-off.txt` in the title folder keeps it inside the sandbox.
3. Start OGBox from the home screen and pick a game from the list: D-pad to move, CROSS to open
   a folder or start a game, CIRCLE to go back. In a game, hold **OPTIONS and the touch pad**
   for two seconds to go back to the list; in the list, the same quits.

`boot.txt` (copy `boot.example.txt`) holds hakuX's options (`scale=2`, `uncap`, ...) for every
game. Its first line may name a disc image; with an `autoboot` line that image boots straight
away, without the list.

hakuX's mods and cheats (60 and 120 fps mods, game fixes; the `mods` folder is hakuX's own) are
off until chosen: add `<key> mod <name>` or `<key> cheat <name>` lines to `/data/ogbox/mods.txt`.
The boot log lists what the running game has, with the key to use.

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

### 0.1.19 (bring-up)

Added
- hakuX's mods and cheats, as it ships them (its `mods/` folder, in the title folder: 120 and
  60 fps mods and the disc-specific patches of fourteen releases). Which are on is in
  `/data/ogbox/mods.txt` (or `mods.txt` in the title folder), one per line:
  `4541005B.1 mod 120fps` (Burnout 3) or `4D530064.3 mod 60fps` (Halo 2); a cheat is `<key> cheat <name>`. The boot log lists what the
  game has (`mods:` lines, the key is the disc's title ID and certificate version) and what is on.
  All are off until chosen.
- The boot log says how late the console's timed sleeps wake (`timing:` line, Crux2's probe).

Changed
- The 0.1.18 core layout for the GPU threads is off unless `env OGBOX_GPU_PIN=1` is in
  `boot.txt`; the emulated CPU still has a core of its own and the other threads the rest.
- While the emulated CPU waits (a game's `Sleep(1)`, a poll of its GPU) it waits awake for the
  last 20 ms of the wait; `env NC_IDLE_SPIN_MS=0` in `boot.txt` goes back to a plain timed wait.

Fixed
- Burnout 3 (and other GPU-heavy games) falling from 60 to 20 frames a second in a race, with the
  emulated CPU sitting idle: the graphics driver waits for the GPU by sleeping a millisecond
  between looks, and a one-millisecond sleep on the console lasts up to 18. Every wait for the
  GPU that took over 1.5 ms came back about 18 ms late, so the game, held at its read of an
  occlusion-test result, flipped its frames a vblank or two behind. The driver's sleeps now wait
  awake, and so do the emulated CPU's short sleeps (a game's `Sleep(1)` took 18.6 ms).

### 0.1.18 (bring-up)

Changed
- The HLE GPU's two threads (the push-buffer decoder and the render thread) each run on a
  processor core of their own, as Crux2's GS does; the boot log shows every thread's processors.
- `env NC_DUMP_CODE=<addr>:<len>,...` in `boot.txt` adds those ranges of the game's code to the
  thread dumps, for diagnosing a freeze.

Fixed
- Wrong, flickering and missing textures that grew worse the longer a game ran (Manhunt: the
  scene black with only the HUD left; Burnout 3's colour streaks). The emulator watches the
  game's memory for changes by write-protecting pages; the console refuses a protection change
  once its memory map is too fragmented, and up to 0.1.17 that failure was silent, so from then
  on the game's texture updates were never noticed. The failure is now detected, the map is
  made whole and re-armed, and a page that still cannot be watched counts as always changed.

### 0.1.17 (bring-up)

Changed
- Thread priorities as hakuX has them: every emulation thread at the same priority (only the sound
  output above, shader compiles below). The emulated CPU has a processor core of its own and
  every other thread runs on the remaining cores, as in Crux2. `env OGBOX_THREAD_LAYOUT=0` in
  `boot.txt` turns the core layout off.
- When a network share cannot be opened, the server's reason is shown under the game list.

### 0.1.16 (bring-up)

Added
- Hold L3 and R3 (both stick buttons) for a second in a game to save a screenshot to
  `/data/ogbox/shots/`; the boot log notes when.
- When a game freezes or crashes, the boot log now shows the game's code around each of its
  threads, so the cause can be found from the log.

Fixed
- Network shares: `network.cfg` is read from `/data/ogbox/`, the title folder or `/data/`,
  whichever has a `path =` line (0.1.15 stopped at OGBox's own example, all comments).

### 0.1.15 (bring-up)

Added
- The foot of the game list shows how many network shares were set up, or why
  `network.cfg` could not be read. A `network.cfg` in the title folder is read too.

Fixed
- A `network.cfg` copied to the console over FTP that the title could not open was taken as
  no shares, without saying why; OGBox now makes it readable and reads it.

### 0.1.14 (bring-up)

Added
- A game list: OGBox starts on a list of places (internal storage, USB drives, extended
  storage and network shares) to pick a game from, instead of booting what `boot.txt` names.
  D-pad to move, CROSS to open a folder or start a game, CIRCLE to go back. It remembers the
  last folder.
- Games on network shares (SMB or NFS): list them in `/data/ogbox/network.cfg` (an example is
  written the first time), one `path =` line each.
- `autoboot` in `boot.txt` boots its first line straight away, as before.

Changed
- Holding OPTIONS and the touch pad in a game goes back to the game list; in the list it quits.
- `boot.txt`'s options also apply to games started from the list.

Fixed
- Def Jam: Fight for NY: the freeze after the EA logo (11 timer reads were left at the PS5's
  clock rate).
- Burnout 3: the pink and black screens in a race (the emulator gave up on GPU results after
  30 ms; it now waits up to 250 ms).

### 0.1.13 (bring-up)

Changed
- The game's code is now patched the way Cxbx-Reloaded does it: only rdtsc, only in the
  game's own code, and only the two bytes of the instruction itself. cpuid and pushfd/popfd
  are no longer patched (`env NC_SWEEP_CPUID=1` and `env NC_SWEEP_FLAGS=1` bring them back
  for testing).

Fixed
- Burnout 3: the crash when a movie starts, and the freeze on the loading screen, both
  caused by patches written into data that looked like code.

### 0.1.12 (bring-up)

Added
- `NC_SWEEP_SKIP=<section>,...` in `boot.txt` leaves those sections of the game unpatched,
  for testing; the boot log shows each rdtsc patch's original bytes.

### 0.1.11 (bring-up)

Fixed
- A switch in `boot.txt` written without `env ` (for example `NC_SWEEP_RDTSC=0`) now takes
  effect; 0.1.10 left it out.

### 0.1.10 (bring-up)

Added
- One `boot.txt` switch per kind of code patch, for testing: `env NC_SWEEP_RDTSC=0`,
  `env NC_SWEEP_CPUID=0`, `env NC_SWEEP_FLAGS=0` (pushfd/popfd), `env NC_SWEEP_TABLES=1`.

Changed
- hakuX's own defaults are passed to the core on every run, as hakuX's app does: console
  timing (the drive's real read times, which pace streaming and movies), asynchronous and
  prebuilt shaders, 1x scale, and the console's video settings (HDTV AV pack, 480p allowed).
  Options in `boot.txt` still override them.

Fixed
- Burnout 3's intro movie no longer crashes the title: `sgdt`/`sidt` are no longer patched on
  the PS5 (the console traps the real ones itself), and one that was really movie data no longer
  gets overwritten.

### 0.1.9 (bring-up)

Fixed
- A thread the kernel switched to when a game's `popfd` (or its page-fault handler's `iretd`)
  turned interrupts back on no longer resumes at the wrong address. Burnout 3 stayed on its
  loading screen because of it: DirectSound's interrupt work went astray and its music stream
  was never resumed.

### 0.1.8 (bring-up)

Added
- The APU's state in the boot log every 5 s: the guest's DirectSound methods by name and count,
  the interrupt's rises by source (a voice's notification, the FE's trap), the notifications
  written (how many, the last one's voice and address), the idle traps, and each voice on the
  lists with its format, stream or buffer, position, envelope, volumes, a stream's SSLs, and the
  samples, segments and SSLs it played since the last line.
- An APU event log: the first 300 methods that start, stop, pause or place a voice's data, and
  the first 100 notifications and idle traps. `env NC_APU_TRACE=n` in `boot.txt` sets how many
  (0 turns it off).
- The thread dump says, for each object a thread waits on, how often and how it was last
  signalled, when, by which thread and from where (or that it never was), and how long the
  thread has waited and the call that waits.

Changed
- The thread dump comes every 10 s from 10 s in (was every 30 s from 20 s in, after the point
  where Burnout 3's runs were quit).

Fixed
- Quitting with OPTIONS and the touch pad ends the title cleanly: confirmed on the console with
  0.1.7 and Burnout 3 (exit status 0, the title closed by the shell).

### 0.1.7 (bring-up)

Added
- A guest memory history for crashes like Burnout 3's at its intro movie (0.1.6): the last 8,192
  allocations, commits, frees, contiguous blocks, kernel stacks, section loads and host unmaps,
  each with the guest thread and caller. A guest access to a page it doesn't have now logs the
  reservation around that address, what was done to the memory within 64 KiB of it, and the page
  table around it.
- A read log: each file handle's first six reads (thread, caller, offset, size, overlapped or
  waited for) and their completions (status, bytes, time taken), every failed read, and the disc
  reader's state every 10 s (what it is reading and for how long, the queue, reads finished but
  not yet completed for the game). `env NC_READ_LOG=n` in `boot.txt` sets the reads per handle
  (0 turns it off).
- The `guest:` sampler line is followed by the busiest EIP's registers, and for each register
  that points at guest memory, the value there, its physical address and what the GPU knows
  about it (the semaphore, the report area, the push buffer, the display).
- The 30-s thread dump also shows each thread's start routine, queued APCs and the type of each
  object it waits on, the interrupt state (enabled, asserted, IRQL, ISR, DPC thread) and the disc
  reader's state.

Changed
- The native CPU's code sweep also patches `rdtsc`, `cpuid`, `sidt` and `sgdt` where six
  instructions after them decode as code. Burnout 3's timer reads at 001fc961 and 001fc984 ran
  unscaled beside the scaled one at 001fc934.

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
