# The emulator core: hakuX, hakux-native and ps5.patch

## The three pieces

- **Upstream hakuX** (`rfandango/hakuX`) at the pinned revision `a7cdb5a975e3`
  (`ps5/tools/fetch-hakux.sh` holds the pin and fetches it into `vendor/hakuX`, which stays as
  upstream has it).
- **hakux-native** (`Press5elect/hakux-native`): the work tree where the core is changed. Its
  history holds OGBox's changes commit by commit: the native CPU backend, save states, trainers,
  CHD and CSO images, the PS5 adaptations, the work merged from hakuX-lr and xbased, the texture
  packs.
- **`ps5/patches/hakux/ps5.patch`** in OGBox: the difference between the pin and a hakux-native
  revision, for `native/`, `hw/`, `include/`, `tests/`, `licenses/` and `LICENSE`. Its header
  lists the commits it was made from, with their authors. `ps5/tools/prepare-hakux.sh` lays it over
  a copy of the pinned source (`build/hakux-src`), and that is what the build compiles.

What the PS5 needs in place of hakuX's Android parts is in OGBox, `ps5/hakux/` (audio output, the
presenter, properties, the CPU stub), not in the patch.

## Branches of hakux-native

| Branch | What it is |
| --- | --- |
| `ps5-native` | The default branch: the long-running line |
| `tiers`, `tiers2`, … `tiers6` | What a release (or a batch of test builds) was built from. `tiers3` is 1.0.0, `tiers5` is 1.0.1 (`a125f74e54`), `tiers6` is 1.0.2 (`6360fddbbe`) |
| work branches (`apu-diag`, …) | A session's work; the next `tiersN` is cut from it at a release |

The repository's `HANDOFF.md`, `CHANGELOG.md` and `README.md` are upstream hakuX's own files
(about its Android app): leave them as they are. hakux-native's commit messages follow the same
wording rules as OGBox's ([`workflow.md`](workflow.md)).

## Changing the core

1. Commit the change in hakux-native on a work branch.
2. Check it compiles on the PC. A syntax check of one file:

   ```bash
   cc -std=gnu11 -fsyntax-only -Wall -DHLE_HOST_TEST -I. -Inative/compat/glib \
      -I<OGBox>/.deps/src/xxhash -I<dir holding subprojects/nv2a_vsh_cpu> \
      -I<volk.h's dir> -I<vulkan headers> hw/xbox/hle/pb/pbdraw.c
   ```

   (`hw/xbox/hle` uses xxHash with `XXH_INLINE_ALL`: no xxHash library is linked.) Logic that
   doesn't need the console can be tested with a small host harness linked against the one file.
   The render targets' coherence (images against memory) has two such tests in hakux-native,
   each running the real code on Linux x86-64 with 16 KiB host pages (the PS5's) and 4 KiB:
   `tests/coh16k/run.sh` (the write tracking and guards: does each guest store count) and
   `tests/cohalias/run.sh` (pbdraw.c's choice between a target's image and its memory, every
   draw checked against a model of the console's memory; exit 1 when a draw read other bytes
   than the console's would). Run both after a change to `pb/pbdraw.c`, `mem/barrier.c` or
   `native/hle/dirty.c`; `SRC=<a worktree of the commit before>` shows a new scenario failing
   without the change.
3. Test build: `ps5/tools/test-build.sh <tag> <hakux-native> <branch>` in OGBox. It regenerates
   the patch for the build and puts the committed one back.
4. A new source file is added to both build lists: OGBox's `ps5/cmake/hakux.cmake` and
   hakux-native's `native/cmake/hle.cmake`.

## Folding the core into OGBox (at a release)

```bash
git -C ../hakux-native branch tiersN <work branch>      # and push it, with the owner's word
ps5/tools/hakux-patch.sh ../hakux-native tiersN         # ps5.patch: header + new commits + body
git add ps5/patches/hakux/ps5.patch                     # commit: "hakuX: ps5.patch from hakux-native tiersN (<hash>)"
```

`hakux-patch.sh` adds the commits since the patch's last listed one to the header, and writes the
body as `git diff --full-index <pin> <ref>` over the paths above. Then update what names the
revision: `PROVENANCE.md`, the release notes, and `ps5/notices/components.json` when a new
third-party part came in with the core.

## Options and switches the core reads

OGBox passes settings to the core as arguments (`ps5/src/ogbox_settings.cpp`, `coreArgs`): hakuX's
own options (`scale=6`, `pace=0`, ...) and `NC_NAME=value` switches, which become environment
variables (`ps5/src/core/hakux_core.c`). `boot.txt` lines `env NAME=VALUE` set any other
(`HAKUX_HLE_*` diagnostics: [`debugging.md`](debugging.md)). A setting added to OGBox therefore
needs: the option in `ogbox_settings.cpp`, its place in a settings group in `ogbox_main.cpp`
(`setGroups`), and the switch the core reads.
