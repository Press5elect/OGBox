# OGBox: changelog

A short entry for each release, newest first. The full notes of a release (what was tested,
installation, the build's versions and checksum) are its page in this folder: see
[`README.md`](README.md).

## 1.0.2

Added
- HD texture packs and texture dumps (Settings > Picture > Textures, per game): each texture the
  game uses can be saved once (compressed ones as DDS, the rest as PNG) and replaced by a PNG or
  DDS file at 1 to 8 times its size, named after the texture's code. A replacement goes to the GPU
  once and is shared by every texture with that code.
- Sound mixing (Settings > Advanced): Default is the game's own sound program's mix, as on the
  console; Fast is OGBox's own mix of the voices.
- OGBox's own 60 FPS patches for `4B4E0004` (versions 1 and 3, in place of the imported ones),
  `4541009E` (v1), `4541007B` (v7), `45410047` (both version 1 releases), `45410071` (v1) and
  `4541005A` (v1); Nemesis2000's widescreen patch for `45410047`, ported to version 1.
- A patch can be for several releases of a game, and is then listed once.
- A game fix for `45410049` (v1): smooth frame pacing.

Fixed
- Particles and billboards that came and went (`4D530039`: waterfalls, wheel dust, roadside
  trees): a game that sends `BEGIN` again before each chunk of a draw and `END` once had only its
  last chunk drawn.
- A game's first start could stop with a disc error or a black screen while it copied files to a
  cache drive (`4D530039`): a rename refused while the file was still being written or closed is
  tried again.
- The balance of voices, music and effects in games whose sound program does the mixing.

## 1.0.1

Added
- Hard disk size (Settings > Xbox): the size the emulated Xbox hard disk reports to games.
  Automatic (the default) makes E: (saves and downloads) as large as what it holds plus the free
  space of an empty Xbox hard disk (about 4.9 GB), never more than your storage has free; 8 GB is
  the Xbox's layout, as before; 16, 32, 64 or 128 GB give E: everything beyond the other
  partitions.

Fixed
- E: was reported as a full drive once it held more than an Xbox's 4.9 GB, so games found no room
  for saves (hakuX's kernel reported the retail size whatever the folder held).
- Missing stencil shadows (`45410012`): shadow volumes that count in the stencil buffer without
  writing depth use the GPU's own depth, not exact depth's.
- White streaks across the picture (`4D5300D1`), there since the first public builds: exact depth
  keeps the game's own depth only for primitives at one depth (the flickering sky it was made for),
  the GPU's for everything else.
- The kernel has all 337 of the Xbox kernel's functions: the driver model (I/O requests, drivers,
  device queues), kernel calls that run several of the game's routines in turn, thread notify
  routines, `PsCreateSystemThread`, `PsQueryStatistics`, interlocked lists, HAL interrupt masks,
  software interrupts and the runtime library's string, number and `printf` functions.
- Merged from hakuX-lr (PortareOS): priority boosts after waits, `KeQuerySystemTime` in the game's
  code, the MTRR capabilities of a Pentium III, unbuffered overlapped disk transfers, the top 128
  KiB of RAM reserved at boot, RAM through large pages, EEPROM over SMBus; the NV2A's logic ops,
  register-combiner precision, point sizes, specular colour and alpha, GL-style clamps and texture
  LOD limits, context DMA bases, P8 palettes, the NV039 notifier, `R16B16` textures, `X1R5G5B5`
  clears, methods skipped while no graphics class is bound, wireframe and point fill modes and
  smoothing, the PVIDEO overlay and cylindrical texture wrap (off by default); a sound no longer
  ends a frame early, and save states keep each voice's modulation and filter state.

## 1.0.0

Added
- 60 FPS patches of OGBox's own (the game's Patches, off until turned on), for title IDs `45410012`,
  `4D5300D1`, `55530041`, `4D53000D`, `54540008`, `54540079` (versions 1, 2, 5, 6), `41560020`
  (versions 3, 5) and `4156004E`. Each keeps the game's speed (its clock is the real time, or its
  step follows the frame rate) and its videos at their own rate; patches that place code check the
  XBE's CRC-32.
- 120 fps mods for a 120 Hz screen (Settings > Graphics > Display refresh rate: 120 Hz; the 60 fps
  patch or mod off): `4E4D0009` (version 3; menus at 60), `55530041` (version 2) and `45410083`
  (version 2), with `54540082` and `545400A4`'s from before; `45410083` also gets its full 60 fps
  mod (physics, ragdolls, recoil, sway, zoom and glass at the speed they have at 30). A mod shows its
  author on the game page.
- An exit, back to the game list, a restart or a state load first waits for what is still on its way
  to the disk, with a bar at the bottom saying what.
- Internal resolution up to 12x; above 6x needs Settings > Advanced > Internal resolutions above 6x
  (performance may vary, and it may crash).
- Network shares are woken when OGBox starts, so a NAS's disks are turning by the time a game
  starts.
- A game's save states, "where you left off" too, can be deleted (TRIANGLE); the quick menu's apply
  says "Applying Settings".
- Choosing where to keep data lists a USB drive first, recommended: the console slows a title's
  writes to its own storage after a few GB, which slows the game; on a USB drive it doesn't.
- DualSense in the menus (Settings > Interface > DualSense): Vibration, Circle confirms and the
  light bar.
- A cache drive's writes are held in memory and written behind the game.
- A startup video: the OGBox X glows behind black glass, three blows crack it and it shatters, with
  its own sound (8 seconds; CROSS or CIRCLE skips it, Settings > Interface > Startup video turns it
  off). Its sources and how to make it again are in `ps5/art/intro`.
- Quincunx anti-aliasing, the original Xbox's own: each pixel is its middle and its four corners,
  very smooth edges and a softer picture, as on the console. It raises the internal resolution to
  the screen's at least.
- Debanding (Settings > Picture and Sound): smooths the colour bands in skies, fog and dark gradients that
  show at high internal resolutions, keeping edges and detail. Low for 32-bit colour's fine steps,
  High for 16-bit colour's wider ones.
- SSAA + SMAA (Anti-aliasing): SSAA, then SMAA over its result for the edges it leaves.
- SSAA down-sampling (Settings > Picture and Sound): Sharp (Lanczos) shrinks SSAA's larger picture with
  crisper edges and less shimmer on fine patterns than Soft, the even average (the default).
- Texture sharpness (Settings > Picture and Sound): textures stay sharper into the distance, more so at a
  higher internal resolution; some games' fine patterns may shimmer. Off by default.
- CRT look (Settings > Picture and Sound): the scanlines between the game's own lines, as a TV of the time
  showed them, and with Strong the screen's red, green and blue stripes; the picture keeps its
  brightness. Best on a 4K screen.
- HDR (Settings > Picture and Sound): the picture goes to an HDR TV as HDR10 (BT.2020 colours, the PQ
  curve), with the game's brightest parts raised toward HDR highlights (Off, or 400 to 4000 nits;
  1000 by default) and white at the HDR white level (203 nits, HDR's reference, by default). The
  menus stay at the white level. It needs the console's own HDR setting on and a TV that takes HDR;
  otherwise the picture stays SDR. OGBox now declares HDR to the console, so an HDR TV goes into HDR
  mode while OGBox runs, with this on or off. The framebuffers are switched as VLC for PS5 does
  (`ps5/patches/mesa-27/0019`), and the declaration is the one other PS5 homebrew shows working.
- Adjust HDR brightness (Settings > Picture and Sound, under HDR): a test screen for the TV. First the
  highlights: an X in a square at the brightest the TV shows, raised until the X can no longer be
  seen (the TV's peak); then the white level, the menus and a grey scale showing each step as it is
  chosen.
- Brightness, Contrast and Colour (Settings > Picture and Sound, a game's own, changed at once from the
  quick menu): for a game that is too dark, too bright or too strongly coloured. Brightness moves
  the middle tones (black and white stay where they are, nothing is lost); Contrast spreads the
  darks and lights about the middle grey; Colour from black and white (-50) to twice the colour
  (+50). Only the game's picture changes, not the menus, in SDR and in HDR.

Fixed
- Games that wait for one of their threads to end no longer wait for ever: a thread's end now sets
  what XAPI's `GetExitCodeThread` reads.
- The AV pack is the one the console reports (`HalBootSMCVideoMode`): games that check the cable
  keep their HD modes (`4D530064` lost 480p).
- A game that rewrites its own code and then protects it runs the new code; a game that never
  initializes a critical section no longer hangs on it; MMX instructions with a `0x66` prefix run
  as the console's CPU runs them (a crash at boot).
- Vertex alignment on the PS5's GPU, and three-component vertex attributes whose fourth component
  was taken from the next attribute.
- Up to 1,024 XBE sections, 64 nested exceptions per thread, a contiguous memory block per page of
  RAM, directory listings with FindFirstFile's dot rules, named kernel objects (`OpenEvent` and the
  like), GPU work queued into memory a game had freed.
- The kernel checked with Cxbx-Reloaded's kernel test suite (its expected results come from a
  console): 80 of its 87 tests with checks pass, where it stopped at its second before. About 30 kernel functions that stopped a game are now there: thread
  creation, string and number routines, stack walks, kernel queues, I/O completion ports, directory
  objects, raised statuses, `XcPKDecPrivate`; and the console's results where ours differed: Latin-1
  case, string conversion limits, DES and RSA edge cases, section page counts, suspend counts.
- Stencil shadows extruded to infinity (`41560020`) drew wedges, spikes and flickering patches:
  exact depth now takes a primitive clipped at the eye from clip space.
- A hang a few hundred frames in at 720p (`4E4D0003`): the scanline register counts the video mode's
  lines.
- Save states that didn't load ("too many open files"), and the quick menu's apply with them.
- The 120 fps mods' movies of `54540082` and `545400A4` played twice as fast.
- From hakuX-lr (PortareOS): kernel calls' faults go to the game's exception handlers, NV097's
  inline attribute methods, the pipeline table recycled when it fills, 2D blits clipped as the NV2A
  does, render targets across adjacent blocks of memory, and the DSP's loops that end in a call.
- Textures drawn from render targets of other sizes and layouts.
- Frame generation turning itself off after a change from the quick menu.
- A game that jumps to an address with nothing there stops with a report instead of closing OGBox.

## 0.0.8

Added
- Compressed `.cso` images: the original Xbox's CSO (Stellar's, as MakeMHz's `ciso.py` and
  Repackinator make it), one file or split in two (`game.1.cso`, `game.2.cso`). With `.cci`
  (Cerbios's), one copy of your games now works on an original Xbox and in OGBox.
- Extracted games: a folder with its `default.xbe` is a game in the list, under the folder's name,
  and any other `.xbe` starts with its folder as the disc; on the console, a USB drive or a network
  share. Mods, title updates and a game's switch to another of its XBEs work as with an image.
- Saves kept in sync between consoles: the hard disk manager's **Keep saves in sync with...** picks
  a network share (or a USB drive); from then on the saves are kept the same both ways with
  `OGBox/Saves/UDATA` there (laid out as the Xbox's `E:\UDATA`), when OGBox starts, after each game
  and with **Sync now**, as Crux2 does with its memory cards. A save is copied whole; one changed on
  both sides keeps the newer and puts the other in `OGBox/Saves/.older`. A save deleted on one side
  comes back from the other: delete it on both. Settings > Network turns it on or off.
- A 60 fps mod for the game with title ID `4E4D0009` (version 3).
- A game fix for `4D530051` (version 2): its crash and restart a few minutes into a level.

Fixed
- Sound that cut in and out, in menus and races, in a game that filters its sounds (`4541009E`):
  the voice filter's damping, as 0.0.6 took it from xemu's measured APU, made the filter unstable
  at its open end; it overflowed within 30 ms and silenced the whole mix until the sound ended.
  Capped as xemu had it before; a filter that still runs away restarts from rest.
- Frames shown twice at 60 fps: a GPU interrupt lost when the game enabled interrupts just as one
  was raised (in hakuX's interrupt line).
- Frame drops while a game creates files, and late interrupts while it waits on DirectSound.
- A game whose rooms went black and whose character fell through the floor (`4E4D0009`): a cached
  overlapped write now completes in its call.
- 100 ms GPU stalls in `4D530051`; the boot log is now written when a game restarts itself after a
  crash.

## 0.0.7

Added
- Settings > Picture and sound > Frame generation: makes extra frames between the game's own, so
  that a game at 30 fps looks like 60 (and, at 120 Hz, a game at 60 like 120). Off by default. It
  adds about one refresh of delay. Four methods, all from Crux2: Crux2's own motion search,
  AMD FidelityFX FSR 3 optical flow, MihawkGen's neural network (by Mihawk) and its Blend. Nothing is
  made for a game that already draws a frame every refresh. MihawkGen works up to 4x internal
  resolution, the other two up to 6x. With it on, the FPS counter also shows the pictures a second
  the screen is given.
- Settings > Picture and sound > Display refresh rate (60 Hz, or 120 Hz for a TV that takes it) and
  Variable refresh rate (at 120 Hz). They take effect at the next start of OGBox; the boot log's
  `display:` lines say what was asked. The game itself keeps its speed at 120 Hz.
- Settings change during a game: the picture's (aspect, anti-aliasing, upscaling, FPS counter, frame
  generation) at once; the others (internal resolution, dithering, ...) put Apply Changes Now at the
  top of the quick menu, which restarts the game from where it is with them.
- The quick menu shows what you choose, in your order: TRIANGLE in it shows or hides each entry
  (any game setting too, changed with left and right) and moves it.
- Settings > Advanced > Boot log: kept in memory and written to `ogbox-boot.log` only when something
  goes wrong (a crash, a failed game, 15 s without a new frame, or the quick menu's Save the Log),
  the default; or always written, as before.
- Mods for the game with title ID 54540082 (version 2) and 545400A4 (version 1): 60 fps, 120 fps,
  no heat haze, no colour filter, no speed blur; draw distance x2 (54540082 only).

Changed
- The start screen: the logo fades in and a light crosses it, "by Press5elect" under it; the start
  notice is gone.

## 0.0.6

Added
- Settings > Picture and sound > Upscaling filter: Bilinear (Sharp), and FSR1 (AMD's FidelityFX
  Super Resolution 1.0) in place of Snapdragon GSR.
- Settings > Picture and sound > FPS counter: which corner it is in.
- Settings > Controller: a drawn Xbox controller with each button's DualSense counterpart, and
  Rumble strength (0 to 100%).
- Settings > Interface > Quick menu button: L1+L2+R1+R2+OPTIONS, Touch pad held or L3+R3, which
  leave OPTIONS free to be the Xbox's Start.
- Settings > Updates > What's New?: this version's notes.
- A game on a network share waits for the share when it doesn't answer at the start, with the
  seconds counted on the screen ("Waiting for the network share..."; CIRCLE goes back to the game
  list). A share that stops answering during a game is shown the same way while the game waits.

Changed
- Sound as the Xbox's sound chip makes it, from tests on a real console (xemu pull request #3105
  by raddad772): its own 8-point resampling with pitch changes smoothed across each frame, the
  volume curve, envelope and LFO timings, 3D sound's head filters and their fades, the effects'
  filters, and the mix's rounding. Settings > Advanced > Resampler: Normal (the Xbox's own, the
  new default), Sinc or Linear.
- The game list's first tab is called Internal. The Network tab shows only the shares set in
  Settings > Network and shares, with no Up or "Add a network share" card.
- The game page is Game Details. Load State is always there; the mods screen is Game Patches,
  with every patch in one group; a patch made for another version of the game says Incompatible
  and which version it is for; Saves reads Import Saves while the game has none.
- The quick menu: Resume, Save State, Load State (LEFT and RIGHT choose the slot), Game Settings,
  Patches, Log What the GPU Draws (for debugging), Restart the Game and Quit the Game.
- Settings: shorter descriptions, without the "takes effect at the next game start" and "default"
  lines (TRIANGLE sets the default). Resolution scale is Internal resolution; Force 32-bit colour
  is one of 16-bit dithering's choices; Drive timing is Fast disc drive (turned over); The Xbox is
  XBOX; Network is Network and shares; the data folder and the Xbox hard disk's location are
  under About, with the credits; the saves are under XBOX. A game's setting that follows Settings
  reads Default. Your old settings carry over.
- Controller: the sticks are Normal or Swapped, with Invert left stick and Invert right stick.
- Long descriptions scroll with the right stick, in every menu.
- Xbox Live through Insignia is greyed out in Settings > Network and shares: work in progress.
- The start notice is shown once per version (a fresh install or after an update).

Fixed
- OGBox's logo is back at the top left of the menus (it was missing after the start screen).
- A game that checks where its files are on the disc against its own default.xbe crippled itself
  and crashed when one of its patches was on: the patched default.xbe reported a made-up position.

## 0.0.5

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

## 0.0.4

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

## 0.0.3

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

## 0.0.2

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

## 0.0.1 (initial release)

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

