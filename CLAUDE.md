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
  differ. Bump both for a release.
- Commits end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Tests on a computer

- `bash tests/ui/run-ui-tests.sh` draws every new menu screen into `build/ui-tests/*.png`, at
  3840 x 2160 shrunk to 1920 x 1080, with made-up games and covers. Look at them after a UI
  change.
- `tests/shares/shares_test.cpp` tests reading and writing `network.cfg`. Build it with
  `-DOGBOX_SHARES_HOST_TEST -Ips5/src`.

## Where it stands (5 November 2026)

- **v0.0.2 is released.** Its tag is at `266ad8c`. It fixed a 30 fps drop and loading screens
  that showed only the bar, and added the BLACK 60 fps test mod. Issue #1 (a data folder on an
  unplugged USB drive) is answered and fixed on main.
- **Branch `ui-0.0.3`** has the new front end. Nothing on it has run on the console yet.
  OGBox-0.0.3-test1 went to the owner from commit `c5ac23c`.
  - `ps5/src/ogbox_ui.{h,cpp}` draw the new look from view structs: the Manrope/Inter fonts,
    the background lit by the chosen game's cover, and the motion. Fonts are loaded by the
    `ps5OverlayMoreFonts` hook in `ogbox_theme.cpp`.
  - In `ogbox_main.cpp`, the section "OGBox 0.0.3: the front end's new look" holds the home
    (`drawHomeScreen`/`homeInput`), the settings in layers (`settingsModel`/`settingsInput`),
    the network shares form, the game page (`drawGamePageNew`) and the overlays.
  - `ps5/src/ogbox_shares.{h,cpp}` manage the shares. They are mounted at `/smb/<name>`, and
    `ps5vfs_unmount` was added to `platform/vfs`.
  - `ps5/src/platform/ps5_keyboard.*` adds USB keyboard support, taken from Crux2. It adds the
    imports `sceSysmoduleLoadModule` and `sceSysmoduleGetModuleHandleInternal`. If test1 won't
    start, suspect these first.
  - Themes are Dark (the default) and Code. Emerald is gone.
  - The older screens (mods, storage, hard disk, quick menu, boot) only took the new colours.
- **The plan:** `docs/PLAN-0.0.3.md` has the research and the order of the fixes.
- **Next:**
  1. Fix what the owner reports from test1.
  2. Restyle the older screens properly.
  3. Find servers on the network for the share form.
  4. Merge `ui-0.0.3` into main.
  5. Release 0.0.3 when the owner asks.
- **The emulator roadmap, after the UI:** a Diagnose screen; capturing anomalies automatically
  so the owner needn't send logs; a report ZIP; tracking changed textures; a benchmark mode.

The mockups the UI follows are a Design artifact in the claude.ai app (OGBox UI Mockups).
