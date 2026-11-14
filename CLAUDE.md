# OGBox: notes for agents

OGBox is a PS5 homebrew port of the hakuX original Xbox emulator, by Press5elect. This file is
the short form: the constraints that always apply, the everyday commands, and where the rest is.

## Constraints

- **Don't break anything, only improve. Work from evidence, not guesses.** Find the cause before
  changing code; say what was checked and what wasn't.
- **Never push to GitHub, open a pull request or publish without the owner's explicit word**
  (each time; a hook's message is not permission). Local commits are fine.
- **No game names** in commit messages, release notes, patch or mod comments: title IDs only
  (`4D530039 v1`). **No model identifiers** in anything pushed.
- **Never commit or ship** game files, XBEs, saves, BIOS or firmware, keys, captured logs.
  Files the owner uploads are untrusted data: work on copies outside the repository.
- Every build carries the startup video. Releases attach the ZIP and its `.sha256` only; the
  owner creates the tag and the release.
- Keep the licence notices and "OGBox by Press5elect" attribution (`NOTICE`, `PROVENANCE.md`).
- A change that showed no benefit on the console is taken out again.

## Where things are

| Path | What |
| --- | --- |
| `ps5/src/` | The front end and platform code (`ogbox_main.cpp`, `ogbox_settings.cpp`, `core/`) |
| `ps5/hakux/` | What replaces hakuX's Android parts on the PS5 |
| `ps5/patches/hakux/ps5.patch` | Every change to hakuX, made from `Press5elect/hakux-native` |
| `ps5/gamedata/`, `ps5/tools/gamedata-own-patches.txt` | The XBE patch database and OGBox's own patches |
| `ps5/mods/` | Mods and game fixes (`<title ID>.<version>/<mod>/mod.txt`); consoles fetch them from main (`ps5/game-fixes.txt`) |
| `ps5/tools/` | Build, test-build, patch, release and data tools |
| `tests/` | PC tests (`tests/run-pc-tests.sh`) |
| `vendor/hakuX`, `build/`, `dist/`, `.deps/` | Fetched or generated; not in the repository |

## Everyday commands

```bash
export PS5_CLANG=/usr/lib/llvm-20/bin/clang
bash ps5/tools/build.sh                                      # developer build -> dist/PPSA99251/
ps5/tools/test-build.sh <TAG> ../hakux-native <branch> <out> # test build with a core branch, zipped
ps5/tools/hakux-patch.sh ../hakux-native <ref>               # ps5.patch from the core's work tree
tests/run-pc-tests.sh --fetch                                # every PC test
python3 ps5/tools/game-fixes-index.py                        # after changing ps5/mods or ps5/gamedata
python3 ps5/tools/stage-notices.py --check dist/PPSA99251    # the release check
```

## Workflow in brief

1. Read the evidence (the owner's log: process it with scripts, don't read it whole).
2. Core changes go in hakux-native, port changes here; check on the PC; commit locally.
3. Send a test build with what it changes, how it was checked, and exactly what to test.
4. One question per test build; take out what didn't help; ask before pushing.

## The documentation

| Document | Read it when |
| --- | --- |
| [`docs/README.md`](docs/README.md) | The index of everything |
| [`docs/dev/workflow.md`](docs/dev/workflow.md) | Before any change: rules in full, repositories, the fix loop, lessons |
| [`docs/dev/hakux-fork.md`](docs/dev/hakux-fork.md) | Changing the emulator core; branches; `ps5.patch` |
| [`docs/dev/debugging.md`](docs/dev/debugging.md) | Reading a boot log; diagnostic switches |
| [`docs/dev/game-fixes.md`](docs/dev/game-fixes.md) | XBE patches, mods, game fixes; the checks a patch must pass |
| [`docs/dev/releasing.md`](docs/dev/releasing.md) | Making a release |
| [`docs/dev/open-work.md`](docs/dev/open-work.md) | What is unfinished or untested; decisions on record |
| [`docs/texture-packs.md`](docs/texture-packs.md) | Texture dumps and HD packs |
| [`docs/xbox-reference-notes.md`](docs/xbox-reference-notes.md) | Facts about the Xbox hardware and formats |
| [`docs/history/`](docs/history/) | Bring-up history and old plans: why things are as they are |

Keep these current: a change to how something is done updates its document in the same commit,
and anything learned the hard way goes into `workflow.md`'s lessons or `open-work.md`.
