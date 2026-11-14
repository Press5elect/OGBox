#!/usr/bin/env bash
# OGBox - hakuX for PS5
# Copyright (C) 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#
# A test build (a developer build) of this work tree with a hakux-native revision that is not in
# ps5/patches/hakux/ps5.patch yet: the patch made from it for the build (ps5/tools/hakux-patch.sh),
# the title built, zipped, and the committed patch put back.
#
#   ps5/tools/test-build.sh <tag> <hakux-native> <ref> [<out dir>]
#
#   <tag>           names the ZIP: OGBox-test-<tag>.zip (BA, BB...: one letter pair a build)
#   <hakux-native>  a clone of github.com/Press5elect/hakux-native
#   <ref>           its branch or commit to build with
#   <out dir>       where the ZIP and the build's log go (default: dist/)
#
# Without hakux-native changes, ps5/tools/build.sh alone is the test build. docs/dev/workflow.md.
set -euo pipefail
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
[[ $# -ge 3 ]] || { sed -n '7,18p' "${BASH_SOURCE[0]}" >&2; exit 2; }
tag=$1; native=$2; ref=$3; out=$(mkdir -p "${4:-$root/dist}" && cd "${4:-$root/dist}" && pwd)
cd "$root"
git diff --quiet -- ps5/patches/hakux/ps5.patch ||
    { echo "test-build: ps5/patches/hakux/ps5.patch has uncommitted changes" >&2; exit 1; }
trap 'git -C "$root" checkout -q ps5/patches/hakux/ps5.patch' EXIT
"$ps5/tools/hakux-patch.sh" "$native" "$ref"
touch ps5/cmake/hakux.cmake      # (it reads the CPU backend's file list when CMake configures)
rc=0
ps5/tools/build.sh > "$out/build-$tag.log" 2>&1 || rc=$?
echo "test-build: build exit $rc; $(grep -c ' error:' "$out/build-$tag.log" || true) errors; log $out/build-$tag.log"
grep 'build identity' "$out/build-$tag.log" | tail -1 | cut -c1-150 || true
[[ $rc == 0 ]] || exit "$rc"
# every build carries the startup video
[[ -s dist/PPSA99251/assets/intro.ivf && -s dist/PPSA99251/assets/intro.wav ]] ||
    { echo "test-build: the startup video (assets/intro.ivf, intro.wav) is missing" >&2; exit 1; }
rm -f "$out/OGBox-test-$tag.zip"
(cd dist && zip -qr "$out/OGBox-test-$tag.zip" PPSA99251)
echo "test-build: $out/OGBox-test-$tag.zip ($(stat -c %s "$out/OGBox-test-$tag.zip") bytes)"
