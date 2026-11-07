# OGBox

**The original Xbox, on your PS5.** OGBox is an unofficial port of
[hakuX](https://github.com/rfandango/hakuX), an emulator for Microsoft's 2001 console, made into
a homebrew title (`PPSA99251`) for jailbroken PlayStation 5 consoles. It's made by Press5elect,
and it was called PSHakuX up to version 0.1.5.

**Latest release: [v0.0.5](https://github.com/Press5elect/OGBox/releases/tag/v0.0.5).** Download
`OGBox-v0.0.5.zip` and copy it to your console. If you already have 0.0.2 or newer, you don't need
to: OGBox offers the update itself the next time it starts.

> **It's free, and it lives here only.** OGBox costs nothing and is only published on this
> GitHub page. If someone is asking you to pay for it, or offering it from somewhere else, that
> isn't this project.
>
> **No piracy, ever.** There are no games, Xbox BIOS, boot ROM, EEPROM, firmware or keys in this
> repository or in any release, and there never will be. Play backups of games you own, made
> yourself.
>
> **Please report problems here, not to hakuX.** OGBox is built on hakuX (by rfandango and
> contributors) and on xemu's sound chip emulation, but neither project makes or supports it.

## What to expect

This is **highly experimental** software. 0.0.5 is the fifth public release, after a long run of
bring-up builds (0.1.1 to 0.1.59). Some games boot and play well. Many others won't start, stop
partway, slow down, or lose their graphics or sound. Crashes and freezes happen, and OGBox reminds
you of that every time it starts.

If you're curious how it works: the Xbox's own x86 code runs directly on the PS5's processor, in
its compatibility mode, instead of being translated. Calls into the Xbox kernel go through a gate
to the emulator's own kernel, the graphics are redrawn through the PS5's Vulkan driver (RADV), and the
sound comes out of the PS5's normal audio output.

When something goes wrong, the file `ogbox-boot.log` in the title folder says what happened (the
run before it is kept as `ogbox-boot.previous.log`). Please attach both when you open an issue.

## Getting started

1. **Install it.** Copy the `PPSA99251` folder from the ZIP to `/data/homebrew/` on the console.
2. **Bring your games.** OGBox reads disc images as `.iso` or `.xiso`, compressed `.cci`, images
   split in parts (`game.1.iso`, `game.2.iso`), and CHDs made with `chdman createdvd`. Put them
   wherever suits you:
   - on the console's storage, under `/data/`;
   - on a USB drive or extended storage;
   - on a computer or NAS on your network. In OGBox, open the **Network** tab and pick
     **Add a network share**. It can find SMB servers on your network for you, and NFS works too.

   With the payload loader (elfldr, port 9021) running, OGBox leaves the PS5's sandbox when it
   starts, so USB drives and extended storage show up. To keep it sandboxed, put an empty file
   named `elevation-off.txt` in the title folder.
3. **Start it** from the PS5 home screen. Press CROSS past the experimental notice. The first time,
   OGBox asks where to keep its data (the emulated Xbox hard disk, caches, covers and screenshots):
   the console's storage, a folder of your own, a USB drive or extended storage. You can change it
   later in Settings > Data folder, and OGBox moves everything for you.
4. **Pick a game.** Your games show as covers, or as a list if you prefer (Settings > Game list).
   - **L1 / R1** switch between the **Internal**, **USB** and **Network** tabs.
   - The **D-pad** moves along your games. **UP** goes to the strip above them: the folders, the
     Xbox dashboard and adding a network share. **DOWN** comes back to the games.
   - **CROSS** opens a folder or starts a game, **SQUARE** starts it from the beginning, **CIRCLE**
     goes back.
   - **L2 / R2** jump a letter back or forward.
   - **TRIANGLE** opens a game's page: its cover, description, save states, settings and mods.
   - **OPTIONS** opens Settings.
   - A USB keyboard works too.
5. **Play.** Press **OPTIONS** in a game for the **quick menu**. The game pauses and a panel slides
   in with Resume, Save state and Load state (10 slots, each with a picture of the moment), Where
   you left off, the game's settings and mods, Restart and Quit. Quit offers to save where you are,
   and the game offers to carry on from there next time.

### The controller

Your DualSense is an Xbox controller: CROSS is A, CIRCLE is B, SQUARE is X, TRIANGLE is Y, R1 is
Black and L1 is White, and L2 / R2 are the triggers. Click the left half of the touch pad for
Back and the right half for Start. OPTIONS is the quick menu for player 1 and Start for the other
players. If you'd rather have OPTIONS as Start, Settings > Interface > **Quick menu button** puts
the quick menu on a half-second hold instead. Each signed-in user's controller is a player (1 to 4),
and the menus show each face button in its Xbox colour (A green, B red, X blue, Y yellow).

**Settings > Controller** changes any of it: which DualSense button is each Xbox button, trigger,
D-pad direction, Start and Back, the sticks (swapped, or up and down turned over), and deadzones
for each stick and the triggers, for a worn stick that drifts. A game can have its own (its page >
Settings, or the quick menu), and a change applies at once, even in a game.

### Updates

OGBox checks GitHub for a newer release each time it starts (you can turn that off in
Settings > Interface), or when you choose **Updates** in Settings. It tells you what's new and
downloads nothing without asking. The download is checked against GitHub's SHA-256, then only
OGBox's own files are replaced. Your settings, the Xbox hard disk, save states, `boot.txt` and
games are left alone. OGBox then closes, ready to be started again, and if a power cut interrupts
an update, it's undone at the next start.

## Settings

Everything hakuX can change, and OGBox's own options, sorted into sections. The box under the list
explains whatever you've highlighted, and shows its default. CROSS picks the next choice, SQUARE the
one before.

**The defaults are the Xbox's own picture and sound.** Anything that changes the look (higher
resolution, anti-aliasing, sharpening and so on) is off until you turn it on.

- **Graphics:** resolution scale (up to 6x), anti-aliasing (FXAA, SMAA or SSAA), aspect ratio, the
  upscaling filter, anisotropic filtering, sharpening, forced 32-bit colour, 16-bit dithering,
  exact depth, and cropping the overscan.
- **Performance:** running the GPU on its own cores, frame pacing, an uncapped frame rate,
  compiling shaders in the background, and building cached shaders at start.
- **Audio:** the resampler (Normal, Sinc or Linear). Sound is mixed the fast way, as xemu does by default.
- **Console:** language, memory (64 MB, or the 128 MB upgrade some patches need; Auto picks it
  when such a patch is on), drive timing and the clock.
- **Video modes:** screen format, 480p, 720p, 1080i, PAL 60 and the video cable.
- **Network:** off, LAN, XLink Kai or Insignia.
- **Game list:** which places show, covers, layout (list or a grid of covers), names, sort order,
  and disc sizes.
- **Interface:** the theme (Dark, or Code with its falling green code), the FPS counter, checking
  for updates at start, and the quick menu button.

Any game can have its own Graphics, Performance, Audio, Console and Video settings: open its page
and choose **Game settings** (TRIANGLE puts one back to the app's). Settings are kept in
`/data/ogbox/settings.txt`, and each game's in `/data/ogbox/games/<title ID>.<version>.txt`. The
network addresses are typed there by hand (`lan_peers=`, `kai_engine=`, `insignia_dns=`).

## More to explore

**Covers and descriptions.** OGBox finds each game's cover on libretro's thumbnails first (by the
disc's Redump name, or its title ID), then MobCat's box scans, and finally the picture inside the
disc itself. A whole folder's covers download in one go, with a progress bar, and are kept in the
data folder's `covers`, named after the game's file (`Halo 2 (USA).iso` gets `Halo 2 (USA).png` or
`.jpg`). Want a different cover? Put your own picture there under the game's file name: it wins
over every download, and OGBox never deletes it. Game descriptions come from Wikipedia. You can
turn downloads off in Settings > Game list.

**Mods, patches and cheats.** A game's page lists what's available for it:
- hakuX's mods (60 and 120 fps mods, game fixes) and OGBox's own;
- XBE patches for widescreen, 60 fps and video modes, gathered from Jay's PatchHub and GitHub
  collection, icup321's XboxGraphicalPatches and OgXHD, all off by default;
- cheats.

Everything is checked against your exact release of the game first. One made for a different
release is greyed out, so it can't be turned on by mistake. **Game fixes** are mods a game needs
to run properly: they're always on and shown greyed out, so you can see what's applied.
`ps5/mods/README.md` explains how to write your own.

**Trainers.** OGBox runs the Xbox's `.etm` and `.xbtf` trainers, the way Evolution-X and
XBMC4Xbox did. It doesn't come with any: copy yours into the data folder's `trainers` folder,
`/data/ogbox/trainers`, or a USB drive's `OGBox/trainers`. Each game lists the trainers made for
it; ones made for another release are greyed out. Pick one, set its options, and they apply as the
game starts. Options marked IGK can also be switched in the game with the controller, just like on
the Xbox.

**The Xbox hard disk.** The hard disk manager (on the left, and in Settings) lists every game with
saves, newest first, and each save by name, date and size. You can copy saves out, move them,
delete them, or bring them back in from the data folder's `backups/saves` or a USB drive's
`OGBox/saves`. **Empty the cache drives** clears X:, Y: and Z:, which games simply fill again.

**The Xbox dashboard.** OGBox doesn't include it, but if your emulated hard disk has `xboxdash.xbe`
on its C: drive (`hdd/Partition2` in the data folder), an **Xbox dashboard** entry appears on the
left.

**`boot.txt`** (copy `boot.example.txt`) holds hakuX options (`scale=2`, `uncap`, ...) for every
game, on top of the settings. If its first line names a disc image and it has an `autoboot` line,
that game starts straight away, skipping the list.

**Where your data lives.** The Xbox hard disk is in the data folder's `hdd`, save states in
`states` and shader caches in `cache` (the data folder is `/data/ogbox` unless you chose another).
All of it sits outside the title folder, so updating is just replacing that folder. If you're
coming from 0.1.5 or older, the first start renames the old `/data/pshakux` for you.

One quirk: hakuX can't yet stop a game and start another in the same run, so starting a new game
or loading a state restarts the title with arguments (`--boot <image>`, `--loadstate <file>`,
`--relaunch`, `--hdd <folder>`, `--option <opt>`). A game that switches to another of its own XBEs
restarts the same way, keeping its last frame on screen meanwhile.

## Building it yourself

You'll need **LLVM 20's clang** (clang 18 can't link RADV's weak imports) and my PS5 stack checked
out next to this repository:
- `PS5_Vulkan`: the RADV archive, the link recipe, the native tool and `libc.prx`;
- `PS5_PayloadSDK`: the SDK fork, pinned;
- `PS5_Mesa`, only if you want to rebuild the RADV archive.

```bash
PS5_CLANG=/usr/lib/llvm-20/bin/clang bash ps5/tools/build.sh       # dist/PPSA99251/
PS5_CLANG=/usr/lib/llvm-20/bin/clang HAKUX_SRC=<a hakuX tree> bash ps5/tools/build.sh
                                                    # hakuX from a work tree instead
python3 ps5/tools/stage-notices.py --check dist/PPSA99251   # the release check
tests/run-pc-tests.sh --fetch                       # every PC test (patches, images, shares,
                                                    # updater, menus); CI runs it on each push
ps5/tools/deploy.sh                                 # upload over the console's FTP server
```

hakuX's source isn't kept here. `ps5/tools/fetch-hakux.sh` fetches it at the pinned revision into
`vendor/hakuX`, and `ps5/tools/prepare-hakux.sh` copies it (or the tree `HAKUX_SRC` names) into
`build/hakux-src` and applies `ps5/patches/hakux/ps5.patch` on top. That one patch is every change
OGBox makes to hakuX: the native CPU backend, save states, trainers, CHD images, the PS5
adaptations, and the work merged from hakuX-lr. Its header lists the commits it was made from,
with their authors. So this repository at a release's tag, plus hakuX at the pin, is that
release's complete source.

What the PS5 needs in place of hakuX's Android parts lives in `ps5/hakux/`:

| hakuX on Android | OGBox on the PS5 |
| --- | --- |
| FEXCore (the guest CPU, `native/cpu/cpu.cpp`) | the native backend (`native/cpu/cpu_native.c`, `NC_CPU=native`), a weak stub until it is there (`ps5/hakux/cpu/cpu_stub.c`) |
| AAudio (`native/dev/apu/out.c`) | `sceAudioOut`, 48 kHz, 256-sample blocking writes (`ps5/hakux/apu/out_ps5.c`) |
| the activity's window and its swapchain (`hw/xbox/hle/vk/swapchain.c`) | a host-owned image: the front frame handed to the title, which draws it on its own display (`ps5/hakux/hle/present_host.c`) |
| AHardwareBuffer guest RAM (zero copy) | hakuX's own copy fallback |
| system properties, prctl, eventfd, gettid, memfd | `ps5/hakux/compat` (properties from `boot.txt` and `props.txt`; memfd through the platform layer) |
| SDL3, JNI, the app (`native/app`) | OGBox's front end, `ps5/src/ogbox_main.cpp`, on the Core seam (`ps5/src/core/ogbox_core.h`) |

The front end talks to the emulator only through the Core (`ps5/src/core/ogbox_core.h`).

## Credits

- **hakuX** by rfandango and its contributors (GPL-3.0-or-later): the emulator. Its MCPX APU is
  xemu's (LGPL-2.1-or-later), with Hatari's DSP56300 emulation (GPL-2.0-or-later); its kernel
  export table is nxdk's (CC0). The voice processor's measured behaviour is from raddad772's
  tests on a retail console (xemu pull request #3105, LGPL-2.1-or-later), by way of
  CallistoNTG's hakuX port.
- **OGBox's logo** (the green X and the name) by Press5elect: `ps5/art/ogbox-logo.png`, the
  title's icon and launcher pictures (`ps5/tools/make-title-art.py`) and the start screen.
- **Crux2** and **Kelvin** by Press5elect: the console platform code (the boot log, the crash
  report, the clock, the thread priorities, the virtual file layer), the build and release tools,
  and the high-base link.
- **PS5 Vulkan Template, PS5_Vulkan, PS5_Mesa and the payload SDK fork** by Mihawk: the
  foundation, RADV on the console and the platform layer.
- **hakuX-lr** (PortareOS and its contributors; GPL-3.0-or-later): graphics, audio, disc and kernel
  improvements taken into the core.
- **libchdr** (Romain Tisserand; BSD-3-Clause), with the LZMA SDK, zstd and dr_flac: CHD images.
- **SMAA** (Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez; MIT):
  the SMAA anti-aliasing shader and its area and search textures.
- **XBMC4Xbox** (Team XBMC, from team xored; GPL-2.0-or-later): the trainers' formats and the
  loader's functions that trainers call (hakuX-native's `native/core/trainer.c`).
- curl, OpenSSL, libpsl, libzip, RapidJSON and Mozilla's certificate list (as certifi carries it):
  the updater, as Crux2 has it.
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

### Next

Changed
- Sound as the Xbox's sound chip makes it, from tests on a real console (xemu pull request #3105
  by raddad772): its own 8-point resampling with pitch changes smoothed across each frame, the
  volume curve, envelope and LFO timings, 3D sound's head filters and their fades, the effects'
  filters, and the mix's rounding. Settings > Advanced > Resampler: Normal (the Xbox's own, the
  new default), Sinc or Linear.
- The menus, tidied:
  - The game list's first tab is called Internal. The Network tab shows only the shares set in
    Settings > Network and shares, with no Up or "Add a network share" card.
  - The game page is Game Details: Load State is always there, the mods screen is Game Patches
    (its patches in one group, Patches; one made for another version of the game says
    Incompatible, and which version it is for), and Saves reads Import Saves while the game has
    none.
  - The quick menu: Resume, Save State, Load State (LEFT and RIGHT choose the slot), Game
    Settings, Patches, Log What the GPU Draws (for debugging), Restart the Game and Quit the
    Game.
  - Settings: shorter descriptions, without "takes effect at the next game start" and "default"
    lines (TRIANGLE gives the default). Resolution scale is Internal resolution; Force 32-bit
    colour is one of 16-bit dithering's choices; Drive timing is Fast disc drive (turned over);
    The Xbox is XBOX; Network is Network and shares; the data folder and the Xbox hard disk's
    place are under About, its saves under XBOX; About has the credits.
  - Settings > Controller: a drawn Xbox controller with each button's DualSense counterpart;
    Rumble strength (0 to 100%); the sticks Normal or Swapped, Invert left stick and Invert right
    stick.
  - Long descriptions scroll with the right stick, in every menu.
- Xbox Live through Insignia is greyed out in Settings > Network and shares: work in progress.

Added
- Settings > Picture and sound > Upscaling filter: Bilinear (Sharp), and FSR1 (AMD's FidelityFX
  Super Resolution 1.0) in place of Snapdragon GSR.
- Settings > Interface > FPS counter: which corner.
- Settings > Interface > Quick menu button: L1+L2+R1+R2+OPTIONS, Touch pad held or L3+R3, which
  leave OPTIONS to be the Xbox's Start.
- Settings > Updates > What's New?: this version's notes.

Fixed
- OGBox's logo is back at the top left of the menus (it was missing after the start screen).
### 0.0.5

Added
- Settings > Controller: which DualSense button is each Xbox button, trigger, D-pad direction,
  Start, Back and stick click; swapped sticks, inverted up and down, and deadzones for each stick
  and the triggers. A game can have its own, and changes apply at once, in a game too.
- Game descriptions on Home, in both layouts.

Changed
- One look for every menu: a cooler dark background, the Xbox green as the only accent and one
  highlight. Home has a strip for the folders, the dashboard and the actions above the games, the
  chosen game shown large and a row of covers below.
- The quick menu, the mods screen, the data folder, the hard disk, the notice, the boot screen
  and the in-game status are rebuilt in that look.
- Covers download three at a time; a folder's descriptions are fetched in the background.

Fixed
- Quick taps on the controller that never reached the game: a press is now held for at least
  50 ms.
- Reading damaged or malicious BPS and IPS patches, patch records and title images could read or
  write outside their buffers.
- The game page's actions running under the footer when a game had all six.

### 0.0.4

Added
- Settings > Anisotropic filtering (2x to 16x), Sharpening and Force 32-bit colour. All off by
  default: the picture stays the Xbox's own.

Changed
- The emulated console's refresh follows the PS5's display, so a finished frame no longer lands
  on the moment the screen takes one.
- Frames go from the emulated GPU straight into the picture, with no copy by the processor.
- The sound's thread has a processor core of its own.
- Sound is always mixed the fast way, as in xemu's default. Full DSP processing is experimental
  in xemu too (some games' sound programs never finish a frame), so its option is hidden.
- Covers are kept under the game's file name, and a picture of your own with that name wins.
- Test builds say "developer build" beside their version.

Fixed
- Frames shown twice or skipped in games running at 60 fps, most at high resolution scales.
- Movies stuttering while a game loads behind them (opening a file read the disc's folders from
  the network share each time).
- A black screen in the Xbox dashboard at a resolution scale of 5 or 6.
- OGBox not closing after the Xbox dashboard.
- Covers filling in one by one after coming back from a game.
- An `env` line first in `boot.txt` being taken for the disc image.

### 0.0.3

Added
- A new front end: a home of covers or a list (Settings > Game list > Layout), a game page with
  its save states, settings in layers, a USB keyboard, and Dark and Code themes.
- Network shares (SMB and NFS) added and edited on the console; the share form finds SMB servers
  on the network.
- Covers from libretro, MobCat's box scans and the disc's own title image, all of a folder's
  downloaded in one go with a progress bar; game descriptions from Wikipedia.
- Compressed CCI images and images split in parts (`game.1.iso`, `game.2.iso`).
- Game fixes: mods a game needs, always on. Mods can change one line of a disc file and can
  cover every release of a game.
- Settings > 16-bit dithering: as the Xbox, fine, or off.

Changed
- Memory 128 MB gives a game that asks for 64 MB only (most do) 64 MB, as a 128 MB console does.
- The boot log is written by a thread of its own.

Fixed
- Games freezing at random, the quick menu barely answering (each log line waited for the disk).
- Games demand-paging their memory loading for minutes, or crashing at the start.
- Regular quarter-second freezes in games that read visibility tests.
- A game hanging when a new block of its memory shared a 16 KB PS5 page with a closed one.
- A menu freezing behind a video (a polling thread kept its priority boost).
- Xbox Live menus restarting the console when no dashboard is installed.
- Slow first-time installs to the cache drive and slow disc reads into emulated memory.
- The game list at 5 fps on a network share, and a game page stuck at "reading".

### 0.0.2

Added
- Game list tabs, **Internal / USB / Network** (L1 / R1), as Crux2 has them; each remembers its
  folder. CIRCLE at a tab's places asks before closing OGBox instead of closing it.
- A game with a "Quit, and save where I am" state asks when it starts: go on from there, start from
  the beginning, or go back.
- Quick menu > **Log what the GPU draws**: the draws of a few seconds into the boot log, for a
  graphics problem's report.
- Mods, patches, cheats and trainers are checked against the disc's release: one made for another
  release of the game is greyed out and can't be turned on.
- A **60 fps test mod for BLACK** (Mods screen, off by default): its 60 fps limiter, frame time
  and constants without the full mod's injected code, for testing. Turn the 60 fps mod off to use
  it.

Changed
- When a game starts another of its XBEs, its last frame stays on screen instead of the "core is
  running" screen (which still shows when a game starts, and with an error).
- The dashboard row shows only when the Xbox hard disk has a dashboard; Settings > Game list says
  when it hasn't.
- Compile shaders in the background is off by default: on, a picture drawn once (a loading screen's
  background) could be skipped.
- Covers: the file's own release first, then the other records of its title ID.
- One boot log for the whole session (a game, back to the list, another game), written to the disk
  every 2 s.

Fixed
- OGBox, and sometimes the whole console, crashing when a game was quit, OGBox was closed or
  closed from the PS5's menu: each exit released two references to the console's root folder it
  never took.
- A game freezing while it loads a level: it ran out of open files (every handle had its own).
- A game running at 2 to 6 fps: its hard disk cache writes held the Xbox CPU (now written on a
  thread of their own), and its GPU interrupt handler waited for results a draw never writes.
- Games dropping to 30 fps in busy scenes: the emulated GPU spent about 40% of each draw waking its
  render thread (on the PS5's x86 processor that thread slept between draws); texture lookups are
  quicker as well.
- Loading screens showing only their bar: a copy from the frame on screen read empty memory instead
  of the picture.
- Games stopping at their menus: the processor now identifies itself as the Xbox's Pentium III.
- A game running out of memory at a level's load: alignment pages of its memory blocks are free, as
  on the console.
- A logo leaning or white (a linear texture's own pitch), trails behind a blur over the back buffer,
  file times on the Xbox hard disk, and xemu's fixes since hakuX's fork (fog, clear alpha, nearest
  readback, the push buffer's DMA control).

### 0.0.1 (initial release)

The first public release, after the bring-up builds 0.1.1 to 0.1.59 (their notes are under
Bring-up history, below).

Added
- **GPU on separate cores** (Settings > Performance, on by default): the emulated GPU's command
  reader and its renderer run on processor cores of their own, apart from the Xbox CPU and
  everything else. Faster; a game that freezes with it can have it off in its Game settings.
  `env OGBOX_GPU_PIN=0` or `1` in `boot.txt` still overrides the setting.
- A notice at every start, before the splash: this is a highly experimental build, expect
  crashes and freezes. CROSS goes on.
- **Performance** and **Audio** sections in Settings.
- **Exact depth** (Settings > Graphics, on by default): surfaces a game draws at one depth get
  exactly that depth, as on the console. Off, the GPU's own depth is used, a little faster.
- **Updates** (as Crux2 has them): a newer release on GitHub, looked for at each start or from
  Settings > Updates, offered with its notes, downloaded, checked and put in place.

Changed
- Settings are in eight sections (Graphics, Performance, Audio, Console, Video modes, Network,
  Game list, Interface), with shorter words; each section says what it holds and each setting its
  default. The audio settings moved from Console to Audio; frame pacing, the frame rate cap and
  the shader options from Graphics to Performance. Settings already saved carry over.
- The splash no longer shows "X to skip"; CROSS or CIRCLE still skips it.
- Version numbers start again at 0.0.1 with the first release.
- Every change OGBox makes to hakuX is now in this repository, as one patch over the pinned
  hakuX (`ps5/patches/hakux/ps5.patch`), and the release is built from it.

Fixed
- A game that sets a gamma ramp and then changes its display's size (one went from 640 x 960 to
  640 x 480 at its first frames) crashed a few seconds in, at any resolution scale: the gamma pass
  freed a picture's view twice. hakuX-lr has the same fix.
- Layers drawn at one depth over each other (a game's sky) flickered as triangles fighting each
  other: the GPU's depth for a triangle whose corners share one depth came out a step off here and
  there. Exact depth stores the depth the game gave (xemu found and fixed the same in its
  renderer).

## Bring-up history

The builds before the first release, 0.1.1 to 0.1.59.

### 0.1.59 (bring-up)

Added
- **CHD disc images** (`.chd`, made with `chdman createdvd`): listed and played as XISOs are, also
  from network shares.
- **Audio processing** setting (Settings > Console): Fast, as before, or Full DSP, which runs the
  Xbox's own audio programs (effects, reverb) and is much heavier.
- **Audio resampler** setting: Sinc (the new default, clean highs) or Linear.
- **Crop overscan** setting (Settings > Graphics): hides the picture's edges as a TV of the time
  did; off by default.

Changed
- The emulator core takes in hakuX-lr's improvements (PortareOS's hakuX fork):
  - Graphics: two-sided lighting, texture colour sign and colour key, the NV2A's quincunx and
    gaussian filters, line width, dithering into 16-bit targets, antialiasing control (sample
    mask, alpha-to-coverage), several and exclusive window clips, the DOT_ZW, BRDF and reflection
    shader modes, more texture formats, anisotropic filtering, defined border colours.
  - Audio: a band-limited resampler, the mix in SSE2/AVX2, 2D voices by their stereo downmix and
    3D voices by their ears as the Xbox's own audio program mixes them, ADPCM and stream fixes.
  - Discs with very deep folder trees, DVD security checks in late XDK titles, cheats compiled
    while the game runs, a game's own exception handlers for more faults.

Fixed
- A game that asks for small blocks of contiguous memory at its first level's load (as some do
  below 16 MB) no longer finds them taken by the emulator's own memory.

### 0.1.58 (bring-up)

Added
- **SMAA** and **SSAA** anti-aliasing (Settings > Graphics > Anti-aliasing, beside FXAA; off by
  default). SMAA smooths edges more sharply than FXAA. SSAA raises the resolution scale above the
  screen's (4x on a 1080p screen, 6x on a 4K one) and averages the picture down; it is by far the
  heaviest.
- Resolution scales 5x and 6x.

Fixed
- Games that use nearly all of the Xbox's 64 MB froze when they loaded more (a race's track): the
  PS5's larger memory pages wasted part of it. Games now get the full 64 MB, and no more, as on
  the console.

### 0.1.57 (bring-up)

Fixed
- Long loads in games that rebuild a cache file on the hard disk each time: the old file is now
  taken back at once instead of being copied back byte by byte.

### 0.1.56 (bring-up)

Fixed
- Games whose loading screen ran forever without loading: a timer function at the start of a
  game's code read the PS5's clock while the matching one read the Xbox's, so the game never
  thought it had time to load.

### 0.1.55 (bring-up)

Fixed
- Loading a save state crashed games that keep their own data in the Xbox's first page of memory:
  the load wrote OGBox's own entries back over it. That page is now restored as it was saved.

### 0.1.54 (bring-up)

Added
- **Trainers**: the Xbox's `.etm` and `.xbtf` trainers (Evolution-X's and XBMC4Xbox's), run before
  the game as XBMC4Xbox's loader runs them.
  - Copied into the data folder's `trainers` (subfolders too), `/data/ogbox/trainers` or a USB
    drive's `OGBox/trainers`; OGBox ships none. Read once into an index, so later starts are quick.
  - The Mods screen lists a game's under TRAINERS: one is chosen, then its options, set as the game
    starts. Trainers made for another release of the game are marked; the same trainer in several
    files is listed once.
  - In-game keys (options marked IGK) work with the pad; the console's LED, which trainers use to
    show an option going on or off, is drawn top right when it changes, and a trainer's LCD text
    shows at the top of the screen.
  - The details panel and the game page count a game's trainers; the boot log says which trainer
    ran, with which options, and whether it was made for this release.

Fixed
- Moving the data folder (Settings > Storage) left the save states and the saves copied out of the
  hard disk behind; they move with it now, and so do the trainers.
- The source beside a release lacked the changes to hakuX the build used (the native CPU,
  savestates, trainers): `hakux-native-<revision>.patch` now carries them over the pinned hakuX.

### 0.1.53 (bring-up)

Added
- **Quick menu** in a game (OPTIONS), after PSFlyCast's and Crux2's: the game pauses and dims, a
  panel comes in from the right with the game's cover.
  - **Save state** and **Load state** in 10 slots; a slot's card beside the panel shows the picture
    of the moment it was saved and when. Left / right changes the slot.
  - **Where you left off**: Quit offers "Quit, and save where I am", kept apart from the 10 slots.
    Starting that game again, from the list or its page, asks: go on from there, or start from the
    beginning.
  - The game's **settings** and **mods, patches, cheats** without leaving the game (they apply at
    its next start), **Restart** (press twice), **Quit** back to the list.
- **Game page** (TRIANGLE on a disc in the list): the cover and details in a fixed panel, with Play,
  Load state (the game's states, with pictures, newest first), Game settings, Mods and Saves.
- **Hard disk** manager (on the left and in Settings), after PSFlyCast's game card manager: the
  games with saves on the emulated hard disk; copy, move or delete a save; copy all of a game's;
  delete a game's saves or its own data; bring saves in from the data folder's `backups/saves` or a
  USB drive's `OGBox/saves`; empty the cache drives.
- The touch pad's halves: a click on the left half is the Xbox's back, on the right half start.
- Settings > Interface > **Quick menu button**: OPTIONS (default), or holding OPTIONS (a short press
  is then start).

Changed
- The details panel beside the list is drawn in fixed places (cover, name, then the same rows for
  every disc) and is no longer something the D-pad can move into: moving right from the list
  no longer jumped to another title. TRIANGLE opens the game page instead.
- The OPTIONS + touch pad hold that left a game (and quit OGBox in the list) is gone: the quick
  menu's Quit does it. In the list OPTIONS opens Settings.
- The Emerald theme moves: the light bands sway and breathe, a glint runs along each, and specks of
  light rise and twinkle.

Fixed
- Loading a save state with the native CPU: the state held the address of the CPU's gate from the
  run that saved it, so the loaded game crashed at its first kernel call (hakuX-native).

### 0.1.52 (bring-up)

Added
- Settings > Interface > **Theme**: **Emerald**, a green look with glowing light bands, glossy
  menu bars, rounded panels, OGBox's logo and the date and time at the top (all drawn by OGBox;
  Roboto for the type). **Code**, the black and green look with the falling code, stays the default.
- Patches from Jay's GitHub collection that PatchHub's site doesn't list (widescreen, 480i fixes, a
  few 60 FPS and 21:9 ones).

Changed
- The patch list is sorted out: 1169 entries down to 465.
  - Resolution patches (720p, 1080i, 128 MB D3D) are gone: Resolution scale renders the game
    sharper without them, and they changed the game's memory layout (Manhunt's hung).
  - The same patch from several places is one entry crediting the others: the same PatchHub file
    under a version and under "any", Nemesis2000's widescreen patches again in OgXHD's database,
    icup321's IPS files again as PatchHub files, one author's patch inside another's.
  - Patches that only work installed on a hard disk (FATX file name fixes), need files downloaded
    separately, or patch an XBE the disc doesn't have, are gone.
  - Kinds from the notes: most of PatchHub's "Misc" were 60 FPS patches; "480i fix" and "480p"
    are told apart. The Mods screen groups the patches under their kind.
  - The Mods screen leaves out patches made for another release of the disc (it reads the disc's
    XBE checksum when it opens).
- Memory on Auto reads PatchHub's own "needs 128 MB" flag; 0.1.45 missed it ("yes", not "Yes").

### 0.1.51 (bring-up)

Changed
- Memory on Auto gives a game 128 MB only when a patch that is on says in its notes that it
  needs 128 MB. 0.1.47 also gave it for every 720p patch: Manhunt's (its notes say only
  "720p") then hung at the game's menu, its graphics running past the memory limit it had set
  itself at the top of the 128 MB. Games and patches that need 128 MB may still hang the same
  way; that is being looked into.

### 0.1.50 (bring-up)

Fixed
- The game list drew scrambled names and covers that came and went. The menus' drawing used
  16-bit vertex numbers, at most 65536 to a window, and a big folder (975 discs) drew far more:
  every row's icon was drawn even out of sight, so the numbers wrapped and the shapes took parts
  of the font and the covers. Rows and tiles out of sight now draw nothing, and the menus use
  32-bit numbers.
- Covers in the grid: every tile of the folder asked for its cover each frame, out of sight
  too, so the covers kept were let go and loaded again; and a cover just loaded was never let go,
  so the cache grew until the menus' image slots ran out and covers stopped showing. Only tiles in
  sight ask now, and the least recently shown of 48 kept goes.

### 0.1.49 (bring-up)

Fixed
- Manhunt stopped at its first level load ("unhandled exception" in the boot log), with or
  without its patches' 128 MB. The game keeps data of its own in the Xbox's first page of memory,
  where the PS5 build had put a few bytes of the console's code (OGBox's takeover of the system's
  C library); since 0.1.44 those bytes were put back at every kernel call, over the game's data,
  and its next read of it took them (the address it crashed on held them). Once a game writes
  there, the page is now the game's: the console never calls those bytes (none of the runs since
  0.1.44 did), and they are written once more as the title exits. `env NC_LOW_KEEP=1` in boot.txt
  puts them back as before.

### 0.1.48 (bring-up)

Added
- The first start asks where OGBox keeps its data: the Xbox hard disk (your saves), the shader
  and patch caches, the covers and the screenshots. The console's storage (`/data/ogbox`, as
  before), a folder of its own on it (`/data/OGBox`), a USB drive, extended storage, or any folder
  you pick, each with its free space. The settings, mods choices and network shares stay in
  `/data/ogbox`. **Settings > Data folder** chooses again and moves the data there (renamed on
  the same drive, else copied with a progress bar, then deleted), or starts the new folder empty.
  boot.txt's `hdd=` is no longer needed (it still wins when set).
- The menus show the DualSense's buttons as pictures instead of their names, the face buttons in
  the colours of the Xbox buttons they are; the settings show the controller's layout.

Changed
- A cleaner front end: icons for the places, folders, discs and OGBox's own rows, rounded
  highlights, "Back to <folder>" instead of "< the folder above", switches for the mods, patches
  and cheats instead of [ON] / [OFF], and the screen's title says MODS, PATCHES, CHEATS.
- A patch that needs the 128 MB RAM upgrade says so in its description.

### 0.1.47 (bring-up)

Added
- Settings, Console, **Memory**: 64 MB as the console has it, or 128 MB, the RAM upgrade most
  720p patches and some others need (Manhunt's 720p patch stopped the game at its first level
  load with 64 MB). **Auto** (the default) gives a game 128 MB only while a patch that needs it
  is on: a 720p patch, unless its notes say it runs on the stock RAM, or any patch whose notes
  say 128 MB. The boot log says when. Per game too. The emulated GPU reports the size as well.

Fixed
- A patch checked against its release (a CRC-32) was left out when another patch was applied to
  the same file before it: the check was of the file already changed. It is now of the disc's
  file (Manhunt's widescreen patch with its 720p one).

### 0.1.46 (bring-up)

Fixed
- NFS Carbon closed a second after its loading screen. The console's crash report named it: the
  emulator's kernel, clearing a block of memory the game had just asked for, wrote through the
  Xbox's own view of that memory, where the graphics emulation still had the pages
  write-protected from the frame buffer that had been there before (freed a moment earlier).
  On the PS5 that fault inside a kernel call isn't one the title can catch, and the system ended
  it. The emulator now clears and copies new memory through its own view, which is never
  protected, and tells the graphics emulation the pages changed. Any game that frees and
  reallocates a frame buffer or texture memory could hit this.

Changed
- The boot log's heartbeat no longer stops while a burst of lines is copied on to klog (it
  went quiet for most of a second before Carbon's crash).

### 0.1.45 (bring-up)

Added
- **XBE patches**: Jay's PatchHub (jayxbox.com, its .JMP patches), icup321's
  XboxGraphicalPatches (IPS and BPS) and OgXHD's patch lists, matched to the games by title ID,
  in the disc's **Mods, patches, cheats** screen under PATCHES, each with its kind (widescreen,
  720p, 480p, 60 fps, fixes), author and notes. Off until turned on; applied to a copy of the
  XBE when the game starts, never to the disc. A patch made for another release of the game
  (its bytes not found, another CRC or region) is left out and the boot log says so.
- **Covers** from libretro's thumbnails, downloaded once into `/data/ogbox/covers`, in the
  details panel and in the new grid view.
- **Redump's Xbox database** (from libretro-database): the details panel shows the disc's Redump
  name, region, languages and serial, and the number of patches.
- Settings, Game list: **Covers** (download them or not), **Layout** (list or grid of covers),
  **Names** (file names or short names), **Order** (by name or last played first), **Disc sizes**
  and **Details panel**.

Changed
- "MODS AND CHEATS" is now "MODS, PATCHES, CHEATS", and it is there for every disc, also
  without a `mods` folder.

### 0.1.44 (bring-up)

Changed
- NFS Carbon closed about 0.3 s after its first picture with nothing in the boot log: the
  console ended the title without a signal, so no crash report could be written, and the last
  lines were still queued. For the cause, the boot log is now written at once (not queued) for
  a run's first 20 seconds (`env NC_LOG_SYNC_S=0` in boot.txt turns that off), the heartbeat
  comes every 0.25 s for a run's first 25 seconds, and each heartbeat says the memory left.
- A suspect: the PS5 build keeps a few bytes of the console's own code in the Xbox's first page
  of memory (OGBox's takeover of the system's C library), and Mortal Kombat and NFS Most Wanted
  write over them as their controllers start, at about the point where Carbon closed. They were
  put back within a millisecond; now they are also put back every time the game enters the emulated kernel, and
  the log says which game code wrote over them. The heartbeat counts how often the console still
  calls them.
- The log names any address a game asks the physical address of that isn't mapped (the
  emulator answers 0, and a device given it writes into that first page).

### 0.1.43 (bring-up)

Fixed
- The freezes in every game (0.1 s to 12 s). When a controller or GPU interrupt arrived just
  as a game entered the emulator's kernel through its gate, the emulator single-stepped the
  whole kernel call, one console signal per instruction: a thousand times slower. Games that
  poll a device or call the kernel in a tight loop (NFS Most Wanted, Mortal Kombat, Def Jam) hit
  it most. Now an interrupt that arrives on the way in waits for the call's end, which takes it
  at once. On the PC build of the same code, a 12-second test run stepped 40,000 to 107,000
  instructions this way before the fix and none after; the run is 15% faster.
- The boot log's CPU sampler counted the time in those kernel calls as "halted" (the guest
  idle), which hid the cause; it says "kernel calls" now.
- The kernel's last-events record in the boot log is printed newest first; 0.1.42 cut off the
  newest events.

Added
- Settings, **Game list**: which places the list's left column shows (OGBox's games folder,
  internal storage, USB drives, extended storage, network shares). The USB drives are hidden
  by default; "Connected ones" shows only drives that are plugged in.
- **L2 / R2** in the game list jump to the previous or next first letter, the letter shown
  large for a moment; held, they keep jumping.
- Back from a game, the list fades in straight away (no boot screen) with the game last played
  selected. It is selected at OGBox's start too. `autoboot` in boot.txt no longer starts its
  game again on the way back.

Changed
- The boot screen no longer names hakuX (hakuX stays credited below and in NOTICE).

### 0.1.42 (bring-up)

Changed
- NFS Most Wanted always stopped at the same spot: the game hits a debug breakpoint there, an
  internal check that something isn't as the game expects. A real Xbox without a debugger would
  stop too; on a development kit the developer presses "go". OGBox now does the latter: it
  goes on past the breakpoint and logs it. `env NC_INT3_CONTINUE=0` restores the stop.
- For the hitches: 0.1.41's logs show the cause's shape. After the USB or the GPU interrupt is
  delivered, its handler is ready to run but does not get to run for 150 ms to 1.4 s, and the
  GPU's next interrupt waits behind it. The boot log now records the emulated kernel's last
  48 steps whenever that happens, so the next log shows exactly which step holds it.

### 0.1.41 (bring-up)

Fixed
- Controllers did nothing in 0.1.40 (Mortal Kombat, NFS): its fix refused every USB write to
  the start of the Xbox's memory, but games keep USB buffers there (MK's at 0x364, NFS's at
  0x3c4, a pad's report at 0x25b). Now only the few bytes the PS5 build itself keeps there are
  protected; the game's buffers are written as before.

Changed
- The guard 0.1.37 put over that memory is off again by default: every USB write there became a
  fault. `env NC_LOW_WATCH=1` in boot.txt turns it on for diagnostics.
- The boot log now says what the USB interrupt handler is doing when it holds up the GPU's
  interrupt for more than 40 ms (the ~130-225 ms freezes during NFS's movies).

### 0.1.40 (bring-up)

Fixed
- Mortal Kombat Shaolin Monks crashed, and GTA San Andreas crashed the app and the console: the
  emulated USB controller wrote gamepad data to the very start of the Xbox's memory (read from a
  controller entry the game was in the middle of setting up), where the PS5 build keeps small
  pieces of system-library code. Nothing of the controller's is ever there; such writes are now
  refused. (Your MK log: 3 200 such writes, then the crash; NFS: 15 700.)
- The guard over that memory that 0.1.37 added could itself end the app when a write came in at
  the moment it was being switched on.
- NFS Most Wanted always stopped at the same spot ("system op 11 at 00054281"): the game runs a
  breakpoint instruction there. The Xbox kernel hands that to the game's own error handlers,
  which OGBox now does too; if the game has none, it stops as before, with more detail in the
  log.

Changed
- The emulated CPU's thread may now run on the PS5's first core's two hardware threads, not
  only one: pinned to one, it had to wait whenever the system used it (some trivial kernel
  calls "held the CPU" 8-22 ms in your logs). `env OGBOX_VCPU_CPUS=0x1` in boot.txt restores
  the old setting.

### 0.1.39 (bring-up)

Fixed
- Textures were rebuilt and uploaded again at nearly every draw that used them (about one per
  draw: 20 000 to 33 000 a second in NFS Most Wanted and Mortal Kombat, and many in Burnout 3),
  which kept the emulated GPU's command decoder busy 60-75% of the time while the game waited
  on it. The cause is the PS5's 16 KiB memory pages: the check for "has the game changed this
  texture" works on the Xbox's 4 KiB pages, and a texture sharing a 16 KiB page with other data
  was always taken as changed. The check now works in the console's page size, comparing the
  bytes at a texture's two ends instead. Android (4 KiB pages) never had this. The same applies
  to render targets the game reads back (Mortal Kombat's 184 000 render-target uploads).

### 0.1.38 (bring-up)

Changed
- Faster on every game: the emulated CPU no longer goes through the console's signal path for
  each access a game makes to the Xbox's hardware registers. That path cost thousands of trips
  a second (9 000 to 37 000 in your logs: Mortal Kombat, NFS Most Wanted, GTA San Andreas,
  Burnout 3), and every freeze in those logs stopped inside it. The Android version does not
  pay it: its CPU translator turns those accesses into direct calls. OGBox now does the
  equivalent: an instruction that keeps touching hardware is rewritten, after a few times, into
  a jump to a small stub that enters the emulator directly (no signal), and the hardware access
  is done there.
- The game's timestamp reads (`rdtsc`) that are made often are rewritten the same way, into a
  stub that computes the Xbox's time without leaving the game's code.
- The returns from every interrupt handler and deferred call of the emulated kernel no longer
  go through a signal either (one or two thousand a second).
- `NC_MMIO_GATE=0` in boot.txt turns the rewriting off, for comparison.

### 0.1.37 (bring-up)

Fixed
- Legacy of Kain: Defiance stopped as it started: it asks the kernel for 668 KiB in one block,
  and OGBox's kernel only gave such blocks from one unbroken run of memory, which there wasn't.
  The Xbox kernel builds them from pages anywhere; OGBox now does too.
- Grand Theft Auto: San Andreas crashed the app (and then the console): from its first movie on,
  something writes over the small code entries the console's system library runs whenever one
  of the app's threads ends, ten times a second. They were put back every 10 ms; now every
  millisecond, and again as each of the emulator's threads ends.

Changed
- The boot log names whatever writes over those entries: a write by the emulator's own code is
  caught with its address, and the first bytes found written over are shown.

### 0.1.36 (bring-up)

Changed
- The boot log's stall report (written when a game stops giving frames) now says what the
  emulated CPU's thread is stuck in: the system call it waits in and its arguments, and, when it
  is inside a signal's delivery, which signal it is and where it came in, with the emulator's own
  frames under it. The 1 to 2 second freezes (every game) all stop at the same place in the
  system library, inside a signal's delivery; this release is to name it.

Fixed
- The stall report looked for the emulator's own return addresses in only the first 32 MiB of
  the 60 MiB program, so it never listed them.

### 0.1.35 (bring-up)

Fixed
- Def Jam's main menu stayed black after the intro movies (at 30 frames a second since 0.1.34).
  Its default.xbe shows the cause: the game moves on only when its 60-a-second frame clock posts
  a frame, and that clock is a chain of Direct3D timer callbacks, each one asking for the next.
  Once, a host stall delayed one callback by 845 ms. Its next request was then for a time
  already past, which Direct3D refuses, and the game doesn't check, so the clock stopped for
  good: no more frames posted, no pad reads, no menu. On an Xbox a callback is never that late.
  Direct3D now arms a time already past at once, the game's own catch-up does the rest. Found by
  reading the game; DefJamFFNY-recomp's notes (by Vlad Yanhursky) describe the same chain and
  confirmed the reading, none of its code is used. `debug.hakux.d3dtimer=0` turns it off.

Changed
- The emulated kernel's scheduler follows the Xbox kernel (read from a 4627 kernel) more
  closely: a wait that is satisfied without waiting uses up part of the thread's turn, and a
  woken thread's priority boost depends on what woke it (1 for events, mutexes and semaphores,
  none for timers and timeouts). `NC_WAIT_QUANTUM=0` and `NC_WAIT_BOOST=0` turn them off.

### 0.1.34 (bring-up)

Fixed
- Def Jam ran at under half speed (12 frames a second where 30 were due) and its main menu
  stayed black. Its default.xbe shows why: the game's clock is a 120-a-second timer, served by
  a system thread of the same priority as the game's main loop, which never pauses. On the
  Xbox a thread woken from a wait gets a small priority boost and runs at once; OGBox's
  emulated kernel had none, so the timer thread waited each time for the main loop's 20 ms turn
  to end, and the game's clock ticked about 50 times a second instead of 120. Woken threads now
  get the Xbox kernel's boost. `NC_WAIT_BOOST=0` in boot.txt turns it off.

### 0.1.33 (bring-up)

Fixed
- The emulated Xbox's clock ran late. The Xbox interrupts every millisecond for its timers,
  sleeps and timeouts; on the PS5 the timed wait OGBox used for it woke up to 18 ms late, so
  while a game was busy its timers came about 55 times a second instead of 1000. Def Jam's
  default.xbe shows what that did: its menu video waits for a sound stream whose timer thread
  and streaming thread both run on those timers, so the menu stayed black at 12 frames a
  second. The clock now sleeps short of the time and waits out the rest awake.
  `NC_CLOCK_SPIN=0` in boot.txt puts the old wait back; the boot log says how late this
  console's sleeps run ("clock: the host's sleeps overshoot by").

### 0.1.32 (bring-up)

Fixed
- Def Jam froze for a third of a second and more at a time while the emulator wrote a line to
  its log: the system library's number formatting (for decimals) takes a lock and allocates
  memory, and the emulated CPU's thread waited on it (0.1.31's freeze probe found it there,
  inside the system's printf). The emulator's log lines are now formatted by OGBox's own code,
  without locks or allocation, and its writer thread runs at the helpers' priority.

### 0.1.31 (bring-up)

Changed
- The emulated CPU's thread back at the priority of the others (700, as up to 0.1.28): with
  0.1.29's higher one, Def Jam's freezes got longer, from about half a second to over a second.
  `OGBOX_VCPU_PRIORITY` in boot.txt still sets it.
- More in the boot log for the freezes: a device access (the GPU, sound, USB and so on) that
  takes over 5 ms is logged with the device and register ("slow device access"); the freeze
  probe names the system library and the function it stopped in, and looks further up the
  stack for OGBox's own calls. Crash reports name system-library addresses the same way.

### 0.1.30 (bring-up)

Changed
- The emulator core's log lines are written by a thread of their own. In Def Jam the emulated
  CPU's thread stopped for over 400 ms at a time inside the write of a log line (a late
  interrupt being noted, from the game's own GPU register writes), the game frozen meanwhile.
  The lines keep their order; `NC_LOG_SYNC=1` in boot.txt writes them at once again.
- The freeze probe also lists the game-side return addresses it finds on the emulated CPU's
  stack ("on the stack" lines), for the freezes where it stops inside the system library and
  the frame chain says nothing more.

### 0.1.29 (bring-up)

Added
- Frame pacing (Settings, Graphics; off by default): holds the game to the speed its frames are
  drawn at, for less input lag when drawing can't keep up. From hakuX pull request #14
  (pjperez).

Changed
- The emulated CPU's thread runs one step ahead of OGBox's other threads. Def Jam froze for about
  half a second every 7 to 10 seconds: the 0.1.28 freeze probe showed that thread not getting the
  processor, and the controller and video interrupts reaching the game a quarter to half a second
  late. `OGBOX_VCPU_PRIORITY=700` in boot.txt puts it back.
- Network shares: OGBox's connections ask for 4 MB send and receive buffers, so reads from an SMB
  or NFS share are no longer limited to about 18 MB/s by the console's small default window.
  `OGBOX_NET_BUFFER_KB` in boot.txt sets the size (0: the console's default).

Fixed
- The Graphics settings couldn't be entered: RIGHT from the first section (and from the last
  ones) went nowhere, as did RIGHT from a game in the list to its Start, Game settings and Mods
  buttons. The menus only moved sideways to an item level with the selected one; now a sideways
  press that finds nothing goes to the next column, and LEFT in the settings goes back to the
  current section. The settings screen was tested this time, headless, with OGBox's own ImGui.
- From hakuX's open pull requests (not merged there yet):
  - #19 (pjperez): two threads changing a memory page's protection at once could leave it
    read-only by mistake, and the game crashed on its next write there. One lock now covers
    them, and a page found in that state is opened instead of crashing.
  - #20 (pjperez): the table of textures waiting for the GPU to finish with them was written past
    its end when it filled up, overwriting what follows it.
  - #21 (pjperez): texture and surface lookups searched every slot once a game had made many
    textures; they are looked up directly now (faster drawing in texture-heavy games).
  - #23 (FadedAtlas): a game touching memory it can't use gets the error in its own handlers, as
    on the Xbox, instead of the emulator crashing, when the game has a handler for it.
- From xemu's open pull requests (abaire's and burningurine's, checked there against a real
  Xbox), rewritten for hakuX's renderer:
  - Specular highlights (#3076) and spotlight edges (#3078) use the Xbox GPU's own formula
    instead of an approximation, in games that drive the GPU's fixed-function lighting directly.
  - Reflection and sphere maps follow the game's "infinite viewer" setting (#3087).
  - Bump maps in the R6G5B5 format are drawn (#3081); they were left out before.
  - Y16 textures are drawn, with the channels the Xbox gives (#3070).

### 0.1.28 (bring-up)

Added
- A freeze probe, for the pauses of a second or more seen in most games. When no new picture has
  come for 0.4 s, the emulated CPU's thread is stopped for a moment and where it was is written to
  the log, every 0.3 s until pictures come again ("stall:" lines in `ogbox-boot.log` in the title folder: the
  place in OGBox or in the game's code, and the calls that led there). These lines say which
  part of the emulator the freeze is in. `OGBOX_STALL_PROBE=0` turns it off.

Changed
- PS5_Vulkan updated to 6224e4c (its newest; the Mesa revision it pins is unchanged).
- The graphics driver carries PS5_Mesa's newest RADV fix (53cc83ff82b, as patch 0018): a large
  buffer is backed by several pieces of the console's memory when no single piece that big is
  free, instead of failing.

Fixed
- OGBox answers the driver's variable refresh hook (Crux2's): the system function is looked up
  while running, so a firmware without it refuses variable refresh instead of failing to link.
- Building the driver again: the build scripts handle a newer Meson and a second LLVM installed
  on the build machine.

### 0.1.27 (bring-up)

Fixed
- The controller not moving right from a row the menus had selected for you: the first settings
  section (Graphics) couldn't be entered, and in a game list just opened the selected disc's
  buttons (Game settings, Mods and cheats) sometimes couldn't be reached. It worked again only
  after moving up or down once.

### 0.1.26 (bring-up)

Added
- A new look: black and green, falling code behind the menus, Share Tech Mono for the type, a
  boot screen with the logo and the start's lines typed out.
- The game list in three columns: the places with Settings, the Xbox dashboard and Quit; the
  folder; the selected disc (its name and title ID read from the disc, its mods, cheats and own
  settings, and Start, Game settings, Mods and cheats).
- Settings: every one of hakuX's, with its defaults, per game as well (see Using it).
- FXAA and Snapdragon GSR in the picture (hakuX's shaders), and the aspect ratio (auto, 4:3,
  16:9, stretch).
- Mods and cheats turned on and off from the menu.
- An FPS counter over the game (on, as in hakuX; off in the settings), and a ring that fills while
  OPTIONS and the touch pad are held.

### 0.1.25 (bring-up)

Fixed
- The console crashing, most of the time, when a game was left with OPTIONS and the touch
  pad. Before the title restarts into the game list, the emulated Xbox is now paused and the
  GPU work of the emulator and of the presenter is waited out. Each step of the exit is written
  to the boot log and forced to storage, so if the console still goes down, the log shows how
  far it got.
- Def Jam: Fight for NY freezing after the EA logo (0.1.20 to 0.1.24). 0.1.20's rule redirected
  a clock read that the decoder had seen only 2 instructions of code before, and it probably
  wasn't one. A read is now redirected against the byte check only after 8 or more decoded
  instructions in a row. Def Jam's other one, after 209 instructions, still is.

### 0.1.24 (bring-up)

Fixed
- 0.1.23 stopping games with SIGFPE. Its floating-point fix used the place the console SDK's
  header gives for the kernel's saved copy, and on the console that place holds something
  else. The emulator now looks for the copy by the marks every such image has (its MXCSR mask
  and control-word bits). If it finds one, it reads and writes the game's floating-point state
  there; if not, it keeps 0.1.20's behaviour. Either way, the boot log says what it found and
  shows the bytes once (`signal frame` lines).

### 0.1.23 (bring-up)

Fixed
- Game threads getting each other's floating-point state (the test disc's six thread failures,
  still there in 0.1.22). The emulator took the game's x87 and SSE registers to be live when
  its signal handler runs. On the console they are not: the handler starts with blank
  registers, and the game's values are only in the copy the kernel saved, which it loads back
  when the handler returns. So the emulator saved and switched blank state, and the kernel put
  the interrupted thread's values back into whichever thread resumed. The emulator now reads
  and writes the game's floating-point state in the kernel's copy, as it already did on Linux.
  The boot log's `signal frame FPU:` line says which copy it uses. (Corrected in 0.1.24: the
  copy isn't where the SDK's header says on the console; this version stopped games with
  SIGFPE.)

### 0.1.22 (bring-up)

Fixed
- 0.1.21 stopping every game within seconds (SIGFPE). Its floating-point fix wrote the game
  thread's state where the console SDK's header says the kernel keeps it, but the console's
  kernel keeps it elsewhere. The emulator now finds the kernel's copy by matching it against
  the registers, writes there, and says where in a `signal frame FPU:` boot-log line. Until it
  finds it, it writes nothing (0.1.20's behaviour). (Corrected in 0.1.23: it never found it,
  because the registers are blank when the handler starts, so this changed nothing.)

### 0.1.21 (bring-up)

Added
- The test disc (1.1) also writes files on hard-disk partitions 1 and 4 and reads them back
  after each change: written in pieces, overwritten at random, grown, cut short, deleted.
- A crash in game code puts more in the boot log: the game's stack frame and the code before
  the instruction that faulted.

Fixed
- Game threads getting each other's floating-point state. When the emulator switched from one
  game thread to another, the thread that resumed got the x87 and SSE registers, control word
  and MXCSR of the one it interrupted: the console's kernel loaded them back as it left the
  emulator's signal handler. The test disc found it (on 0.1.20, 6 of its checks failed, in
  every switch it caught). Any game with more than one thread that uses floating point could
  compute with another thread's numbers now and then. That fits Crash Bandicoot's crash (its
  animation code read a matrix of nonsense), but it isn't confirmed yet. (Corrected in 0.1.22:
  this version wrote the state to the wrong place and stopped every game with SIGFPE.)

### 0.1.20 (bring-up)

Added
- A test disc, `ogbox-tests.iso` (source in `tests/xbox/ogbox-tests`, built with nxdk): an Xbox
  program that checks the emulated CPU's floating-point state across thread switches and kernel
  calls, the clocks and timed waits, memory, and reads of every shape from the disc image. Put
  it on the share with the games and run it like one; each result is a PASS, FAIL or INFO line in
  the boot log (`OGBT` lines) and in `/data/ogbox/hdd/Partition1/ogbox-tests.txt`. It takes
  about 15 seconds.

Fixed
- A read of the CPU's time-stamp counter that the emulator left on the console's own fast
  clock, beside the emulated 733 MHz one the others read, when the instruction after it was not
  one Cxbx-Reloaded's list knows. Code that times a wait with both never stops waiting (the test
  disc did). Def Jam: Fight for NY has two of these reads; whether they cause its stop at the
  EA intro isn't known yet.

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
- The graphics driver's waits for the GPU look again every 0.1 ms instead of sleeping 1 ms.
  (Corrected in 0.1.20: these notes first called this the fix for Burnout 3's race slowdown,
  on the belief that a 1 ms sleep on the console lasts 18 ms. The `timing:` line measured
  0.1 ms late, so that wasn't the cause, and the slowdown remains.)

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
