#!/usr/bin/env bash
# PSHakuX - hakuX for PS5
# Copyright (C) 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
#
# The hakuX source the build compiles: a copy of HAKUX_SRC with ps5/patches/hakux/*.patch laid
# over it, in build/hakux-src. Prints its path.
#
#   ps5/tools/prepare-hakux.sh [HAKUX_SRC]
#
#   HAKUX_SRC   the hakuX tree to build (default vendor/hakuX: upstream at the pin of
#               ps5/tools/fetch-hakux.sh, fetched when it is missing). Any other tree is taken as
#               it is, e.g. HAKUX_SRC=../hakuX-native for the native CPU backend's work tree: it
#               is read, never written.
#
# Every patch is applied to vendor/hakuX's copy, and must apply. Another tree may already have a
# patch's change, or a change of its own in the same place: a patch that does not apply there is
# left out and said (that tree's code wins), so a work tree's own changes are never overridden.
# The copy is made with checksums, so a file that did not change keeps its time and is not
# compiled again.
set -euo pipefail

ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
src=${1:-${HAKUX_SRC:-$root/vendor/hakuX}}
if [[ $src == "$root/vendor/hakuX" ]]; then
    bash "$ps5/tools/fetch-hakux.sh"
fi
src=$(cd -- "$src" && pwd)
[[ -f $src/native/CMakeLists.txt && -d $src/hw/xbox/hle ]] || { echo "prepare-hakux: $src is not a hakuX tree" >&2; exit 2; }
out="$root/build/hakux-src"
stage="$root/build/hakux-stage"
mkdir -p "$out"
rm -rf "$stage"
mkdir -p "$stage"
# What the build compiles or includes: native/ and hw/ (not the Android app, the tests, the tools)
# and the licence files.
rsync -a --delete --exclude .git "$src/native" "$src/hw" "$stage/"
for file in COPYING LICENSE README.md; do
    [[ -f $src/$file ]] && cp -p "$src/$file" "$stage/"
done
[[ -d $src/licenses ]] && rsync -a "$src/licenses" "$stage/"
(cd "$stage" && git init -q . && git add -A && git -c user.name=PSHakuX -c user.email=noreply@anthropic.com commit -qm base)

vendored=$([[ $src == "$root/vendor/hakuX" ]] && echo 1 || echo 0)
applied=0 skipped=0
shopt -s nullglob
for patch in "$ps5"/patches/hakux/*.patch; do
    name=$(basename "$patch")
    # (A tree that has the patch's change already: its reverse applies. Checked first, since a
    # patch whose context repeats in the file could otherwise apply a second time.)
    if [[ $vendored == 0 ]] && git -C "$stage" apply --reverse --check --whitespace=nowarn "$patch" 2>/dev/null; then
        echo "prepare-hakux: $name: $src has it already" >&2
        skipped=$((skipped + 1))
    elif git -C "$stage" apply --check --whitespace=nowarn "$patch" 2>/dev/null; then
        git -C "$stage" apply --whitespace=nowarn "$patch"
        applied=$((applied + 1))
    elif [[ $vendored == 1 ]]; then
        echo "prepare-hakux: $name does not apply to vendor/hakuX" >&2
        git -C "$stage" apply --check --whitespace=nowarn "$patch" >&2 || true
        exit 1
    else
        echo "prepare-hakux: $name left out: it does not apply to $src (that tree's code is kept)" >&2
        skipped=$((skipped + 1))
    fi
done
rm -rf "$stage/.git"
rsync -a --delete --checksum "$stage/" "$out/"
rm -rf "$stage"
{
    echo "source: $src"
    echo "revision: $(git -C "$src" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "patches: $applied applied, $skipped left out"
} > "$out/.pshakux-source.txt"
echo "prepare-hakux: hakuX from $src ($(git -C "$src" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)): $applied patches applied, $skipped left out" >&2
echo "$out"
