# OGBox documentation

What exists, and where. The top-level [`README.md`](../README.md) is the user's guide; everything
else is indexed here.

## For users

| Document | What it holds |
| --- | --- |
| [`../README.md`](../README.md) | What OGBox is, getting started, the controller, settings, mods and patches, saves, building it, credits and the licence |
| [`texture-packs.md`](texture-packs.md) | Dumping a game's textures and replacing them with HD ones |
| [`releases/README.md`](releases/README.md) | Every release's notes, and the [changelog](releases/CHANGELOG.md) |
| [`../ps5/mods/README.md`](../ps5/mods/README.md) | Writing mods, game fixes and cheats |
| [`../PROVENANCE.md`](../PROVENANCE.md), [`../NOTICE`](../NOTICE) | Where the work came from; the licence terms that bind |

## For people and agents working on OGBox

Start with [`../CLAUDE.md`](../CLAUDE.md): the rules that always apply, and this same index.

| Document | Read it when |
| --- | --- |
| [`dev/workflow.md`](dev/workflow.md) | Before any change: the repositories, branches, test builds, commits, what needs the owner's word |
| [`dev/hakux-fork.md`](dev/hakux-fork.md) | Changing the emulator core: hakux-native, its branches, `ps5.patch` |
| [`dev/debugging.md`](dev/debugging.md) | Reading a boot log; the diagnostic switches; the problem report (draw log) |
| [`dev/game-fixes.md`](dev/game-fixes.md) | Writing an XBE patch or a game fix; the checks a patch must pass |
| [`dev/releasing.md`](dev/releasing.md) | Making a release |
| [`dev/open-work.md`](dev/open-work.md) | What is known to be unfinished or untested, and ideas not taken up |
| [`xbox-reference-notes.md`](xbox-reference-notes.md) | Facts about the Xbox itself: memory map, SMBus, boot, kernel, XBE, FATX |

## History

| Document | What it holds |
| --- | --- |
| [`history/bring-up.md`](history/bring-up.md) | The builds before the first release (0.1.0 to 0.1.59): why the port is built the way it is |
| [`history/plan-0.0.3.md`](history/plan-0.0.3.md) | The plan written after 0.0.2 (5 November 2026); done or overtaken, kept for its reasoning |

## Elsewhere in the tree

- `ps5/patches/mesa-27/README.md`: the patches to Mesa's RADV.
- `ps5/third_party/*/README.md`: the frame generation's sources (FidelityFX, MihawkGen, NIS).
- `ps5/art/intro/README.md`, `ps5/ASSETS.md`: the startup video and the generated art.
- `ps5/notices/components.json`: every third-party part, its licence and revision (the build
  stages it into the title folder's `licenses/`).
- Code comments carry the version a behaviour dates from ("0.1.52: ...", "OGBox 1.0.2: ..."):
  search for the version to find a change's reason.
