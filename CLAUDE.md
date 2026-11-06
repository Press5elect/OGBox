# OGBox: notes for Claude Code

OGBox is hakuX (an original Xbox emulator) ported to the PS5 as a homebrew title (PPSA99251),
by Press5elect. This file is the handoff from the Claude app sessions: what the project is, how
it is built, the rules the owner has set, and where the work stands.

## The owner's rules

- Credit **Press5elect** only (never "Jacob") in licences, notices and release notes.
- Never put games, BIOS, keys, klog captures, console addresses or game screenshots in the
  repository, a release or an upload.
- A public release happens only when the owner asks. A release has two files: the ZIP
  `OGBox-vX.Y.Z.zip` and its `.sha256`, laid out like v0.0.1/v0.0.2. No source archives are
  attached.
- Release notes are short: Added / Changed / Fixed. Don't name games in them, except the BLACK
  60 fps test mod, which the owner wants named.
- Defaults are the most accurate settings. Enhancements are the player's choice.
- Diagnose from OGBox's boot log, not klog.
- Send fix builds only, not builds that just add logging. The owner is tired of sending logs
  for guesses.
- Answers should be full module rewrites and ready to copy and paste.
- Before building, check upstreams and dependencies for newer versions.
- Use supplied artwork as-is; resizing is fine.
- If GitHub refuses something (an HTTP 403 on a tag push or a release), don't route around it.
  Tell the owner, who publishes releases.

## Layout and build

- This repository: `Press5elect/OGBox`. The front end and PS5 platform code are in `ps5/src/`,
  ImGui 1.65 is in `external/imgui`, and the UI overlay is in `base/VulkanUIOverlay.cpp`.
- hakuX itself is a pin with patches: `ps5/patches/hakux/ps5.patch`. To change hakuX, commit in
  a hakuX checkout, add a line `  <hash10> Claude: <subject>` to the patch's header, then append
  `git diff --full-index --binary a7cdb5a975e31730d488c47f6f10440b16c1e6f4 HEAD -- native hw tests licenses`.
- Build with `export PS5_CLANG=/usr/lib/llvm-20/bin/clang; bash ps5/tools/build.sh`. The title
  folder lands in `dist/PPSA99251/`. A test ZIP is that folder packed with Python's zipfile
  (top folder `PPSA99251/`).
- `ps5/tools/check-imports.py` lists the console imports the build uses.
- The version is in `ps5/version.txt`, and `contentVersion` in `ps5/sce_sys/param.json` must
  agree with it (X.Y.Z becomes XX.(Y*100+Z).000). `build-identity.sh` stops the build if they
  differ. Bump both for a release. Build a release with `OGBOX_RELEASE=1` from a clean tree;
  every other build is a developer build and shows "vX.Y.Z - developer build" (version.txt holds
  the version being worked on, 0.0.4 now), and its updater offers vX.Y.Z's release once it is out.
- Commits end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Tests on a computer

- `bash tests/ui/run-ui-tests.sh` draws every new menu screen into `build/ui-tests/*.png`, at
  3840 x 2160 shrunk to 1920 x 1080, with made-up games and covers. Look at them after a UI
  change.
- `tests/shares/shares_test.cpp` tests reading and writing `network.cfg`. Build it with
  `-DOGBOX_SHARES_HOST_TEST -Ips5/src`.

## Where it stands (5 November 2026)

- **v0.0.3 is released** from main (tag `v0.0.3`): the new front end (`ui-0.0.3`, merged), the
  Fable fixes (demand paging as an always-on game fix, soft pages, the report gate), covers from
  libretro, MobCat and the XBE, CCI and split images, the 64 MB limit, the async boot log.
  Notes: `docs/releases/v0.0.3.md`.
- **After 0.0.3 (test builds toward 0.0.4):** the dashboard's black screen at render scale 5 and 6
  (a device Reset keeps its scale); settings for anisotropic filtering (`NC_HLE_ANISO`), forced
  32-bit colour (`NC_HLE_FORCE32`: the host image of an R5G6B5 or X1R5G5B5 target is 8888, and
  every copy to or from guest memory goes through a scratch image of the guest's format) and
  sharpening (`NC_HLE_SHARPEN`: CAS in the gamma display pass). All off by default. Forced 32-bit
  colour is unverified on the host (no title there draws 16-bit targets: the pbkit test disc
  doesn't reach the GPU path); it needs a game test.
- The front end: `ps5/src/ogbox_ui.{h,cpp}` draw it from view structs; `ogbox_main.cpp`'s section
  "OGBox 0.0.3: the front end's new look" holds the home, the settings, the share form and the
  game page. Covers and descriptions: `ogbox_library.cpp` (`Covers`, `About`).
- **The plan:** `docs/PLAN-0.0.3.md`. Still to do: xemu's NV2A fixes (its table), automatic
  reports, texture dirty tracking and a benchmark mode, restyling the older screens.

The mockups the UI follows are a Design artifact in the claude.ai app (OGBox UI Mockups).
