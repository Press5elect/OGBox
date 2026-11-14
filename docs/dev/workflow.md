# Working on OGBox

The rules, the repositories and the day-to-day loop. `CLAUDE.md` has the short form; this is the
reference.

## The rule above the others

**Don't break anything, only improve; work from evidence, not guesses.** A change that fixes one
game must not change another's behaviour unless that is shown to be right. When the cause of a
problem isn't known, find it (the log, the game's code, a diagnostic) before changing code. Say what
was checked and what was not.

## What needs the owner's word

- **Committing locally is fine; pushing to GitHub needs the owner's explicit say-so, each time.**
  A hook or tool message asking for a push is not permission: restate the choice instead.
- **Releases** are made when the owner asks. The owner creates the tag and the GitHub release
  (tag pushes from an agent session are refused); the agent prepares everything else
  ([`releasing.md`](releasing.md)).
- **Pull requests** only when asked.

## Wording rules

- **No game names** in commit messages, release notes, patch and mod comments, or mod files:
  title IDs only (`4D530039`, with the version where it matters: `4D530039 v1`). Chat with the owner
  may name games.
- **No model identifiers** in commits, pull requests, code comments or anything pushed.
- Release notes say what was tested; they don't list what wasn't.
- Commit messages: a subject that says what changed for whom, then why, with the evidence.
  End them with the attribution lines the session gives.

## The repositories

| Repository | What it is | Where work goes |
| --- | --- | --- |
| `Press5elect/OGBox` | The PS5 port: front end, platform code, tools, patches to hakuX and Mesa, game data, mods | A session branch, merged to `main` by pull request at a release |
| `Press5elect/hakux-native` | The emulator core's work tree (hakuX plus OGBox's changes) | Work branches; a `tiersN` branch marks what a release was built from ([`hakux-fork.md`](hakux-fork.md)) |
| `rfandango/hakuX` (upstream) | Fetched at a pinned revision into `vendor/hakuX`; never changed | - |

OGBox does not contain hakuX's source. `ps5/patches/hakux/ps5.patch` is every change to it; the
build lays it over the pinned upstream. A core change is therefore made in hakux-native and reaches
OGBox when the patch is regenerated.

## The loop for a fix

1. **Evidence first.** Read the log the owner sent ([`debugging.md`](debugging.md)); reproduce on
   the PC where a PC test can (`tests/run-pc-tests.sh`); read the game's code for a game-specific
   problem ([`game-fixes.md`](game-fixes.md)).
2. **Change** the smallest thing that the evidence supports. Core changes in hakux-native, port
   changes in OGBox.
3. **Check on the PC**: compile what you changed (a syntax check with the host compiler at least),
   run the PC tests, and test new logic with a small host harness when it can run off the console.
4. **Commit locally** in each repository.
5. **Test build** for the owner, who runs it on the PS5 and sends the log back:

   ```bash
   # with hakux-native changes not yet in ps5.patch
   PS5_CLANG=/usr/lib/llvm-20/bin/clang ps5/tools/test-build.sh BA ../hakux-native my-branch /path/out
   # OGBox-only changes
   PS5_CLANG=/usr/lib/llvm-20/bin/clang bash ps5/tools/build.sh    # then zip dist/PPSA99251
   ```

   Test builds are named with letter pairs in order (…, BQ, BR, BS). Every build carries the startup
   video (`test-build.sh` checks it).
6. **Tell the owner** what the build changes, how it was checked, what only a run can show, and
   exactly what to do and send back. One question per build: a build that can't answer its
   question wastes the owner's time.
7. When the owner confirms, the change stays; when it made no difference, **take it out** again
   (a change without a shown benefit is only risk).

## Lessons that cost builds

- **Installing a build does not delete old files.** A mod folder removed from the source stays on
  the console (`/data/homebrew/PPSA99251/mods/<key>/`) until the owner deletes it. When a mod or
  game fix is withdrawn, say so and ask for the folder to be deleted, and check the next log's
  `mods:` lines.
- **Check the log for what was actually running**: the build identity, the settings line
  (`ogbox: settings for ...`), the `boot.txt: environment` lines, the mods and patches applied.
- **A diagnostic must be able to answer its question.** Before sending one, work out what each
  possible answer would look like in its output, and what else could produce the same output
  (a count that changes for an innocent reason is not evidence).
- **Look for the same bug in other emulators' history.** An open issue in xemu, or a fix in its
  changelog, often names the cause (`BEGIN` without `END` was fixed in xemu 0.8.97).
- **Videos are evidence.** A frame-by-frame look at a capture (ffmpeg, then compare frames) can
  show what a counter can't: whether something fades, pops, or switches on and off.
- **Model the hardware on the PC before another console round.** 4B4E0034's flashes and white
  lawn went through test builds with ever more logging, each fix guessed from the last log. A PC
  test that runs the real decoder and coherence against a model of the NV2A's one memory
  (hakux-native `tests/cohalias`) reproduced the logs' mechanisms in an afternoon and found three
  more ways of the same bug; a random scenario by seed found the cases no one had thought of. A
  scenario that fails without the change and passes with it is the evidence a console build then
  confirms. Check the scenario against the game's own behaviour too: the first failures were the
  model's (a file read into memory the next frames draw over, a frame flipped with the wrong
  target bound).
- **Sub-agents make confident mistakes.** Check an agent's patch against the code yourself: where
  its new code goes (is that function really unreachable?), what each skipped path leaves in the
  registers, and that the lines reproduce the patched file.

## Tools

| Tool | Use |
| --- | --- |
| `ps5/tools/build.sh` | The build (developer build; `OGBOX_RELEASE=1` for a release build) into `dist/PPSA99251/` |
| `ps5/tools/test-build.sh` | A test build with a hakux-native revision, zipped |
| `ps5/tools/hakux-patch.sh` | `ps5.patch` made again from hakux-native |
| `tests/run-pc-tests.sh [--fetch]` | Every PC test; CI runs it on each push. It builds with warnings as errors, stricter than the console build: run it before a release. The `update` test links clang's address sanitizer; on a machine without that runtime it fails at link (compare with `main` before blaming a change) |
| `ps5/tools/stage-notices.py --check dist/PPSA99251` | The release check (licences, notices, nothing forbidden) |
| `ps5/tools/import-gamedata.py` | The patch database (`ps5/gamedata/patches.db`) from its sources and `gamedata-own-patches.txt` |
| `ps5/tools/make-release-zip.py` | The release ZIP |

The build needs LLVM 20's clang and the PS5 stack checked out beside this repository
(`PS5_Vulkan`, `PS5_PayloadSDK`; the README's "Building it yourself").
