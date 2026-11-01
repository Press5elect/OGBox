#!/usr/bin/env bash
# PSHakuX - hakuX for PS5
# Copyright (C) 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "PSHakuX by Press5elect"; see NOTICE.
#
# hakuX's source at the pinned upstream revision, in vendor/hakuX, as it is upstream.
# (Made after Crux2's ps5/tools/fetch-pcsx2.sh, Crux2 by Press5elect.)
#
#   ps5/tools/fetch-hakux.sh            fetch it (does nothing when it is there at the pin)
#   HAKUX_MIRROR=../src-dl/hakuX ps5/tools/fetch-hakux.sh
#                                       fetch the pin from a local clone instead of GitHub
#
# hakuX's source is not kept in this repository, and vendor/hakuX stays as upstream has it: the
# port's changes to it are ps5/patches/hakux/*.patch, laid over a copy of the source the build
# compiles (ps5/tools/prepare-hakux.sh), and what the port replaces outright (the AAudio output,
# the Android window's presenter, the system properties...) is in ps5/hakux/, not in hakuX.
set -euo pipefail

ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
vendor="$root/vendor/hakuX"
# a7cdb5a975 (2026-11-01): hakuX's master when PSHakuX began; checked with git ls-remote that day.
revision=a7cdb5a975e31730d488c47f6f10440b16c1e6f4
url=https://github.com/rfandango/hakuX

if [[ ${1:-} == --revision ]]; then
    echo "$revision"
    exit 0
fi
if [[ -f $vendor/native/CMakeLists.txt && $(git -C "$vendor" rev-parse HEAD 2>/dev/null) == "$revision" ]]; then
    exit 0
fi
rm -rf "$vendor"
mkdir -p "$vendor"
git -C "$vendor" init --quiet
git -C "$vendor" remote add origin "$url"
if [[ -n ${HAKUX_MIRROR:-} ]]; then
    git -C "$vendor" fetch --quiet --depth 1 "$(cd "$HAKUX_MIRROR" && pwd)" "$revision"
else
    git -C "$vendor" fetch --quiet --depth 1 origin "$revision"
fi
git -C "$vendor" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
echo "vendor/hakuX: hakuX ${revision:0:10}" >&2
