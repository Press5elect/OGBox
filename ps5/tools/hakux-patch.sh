#!/usr/bin/env bash
# OGBox - hakuX for PS5
# Copyright (C) 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#
# ps5/patches/hakux/ps5.patch made again from a hakux-native work tree: the patch's header as
# committed, the commits since the revision it was last made from added to its list, and the
# difference between the pinned upstream hakuX (vendor/hakuX) and <ref> as its body.
#
#   ps5/tools/hakux-patch.sh <hakux-native> <ref> [<since>]
#
#   <hakux-native>  a clone of github.com/Press5elect/hakux-native
#   <ref>           the branch or commit the patch is made from (tiers6...)
#   <since>         the revision the committed patch was made from (its list's last commit when
#                   left out); the commits <since>..<ref> are added to the list
#
# The patch is written over ps5/patches/hakux/ps5.patch: commit it for a release, or put it back
# (git checkout ps5/patches/hakux/ps5.patch) after a test build (ps5/tools/test-build.sh does both).
# docs/dev/hakux-fork.md says how the fork and the patch go together.
set -euo pipefail
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
[[ $# -ge 2 ]] || { sed -n '7,20p' "${BASH_SOURCE[0]}" >&2; exit 2; }
native=$(cd -- "$1" && pwd); ref=$2
patch=$ps5/patches/hakux/ps5.patch
paths=(native hw include tests licenses LICENSE)
"$ps5/tools/fetch-hakux.sh"
pin=$("$ps5/tools/fetch-hakux.sh" --revision)
# the pinned upstream's objects, for the difference (hakux-native's history does not hold them)
git -C "$native" cat-file -e "$pin^{commit}" 2>/dev/null ||
    git -C "$native" fetch --quiet "$root/vendor/hakuX" "$pin" 2>/dev/null || true
git -C "$native" cat-file -e "$pin^{commit}" || { echo "hakux-patch: the pin $pin is not in $native" >&2; exit 1; }
first=$(grep -m1 -n '^diff --git' "$patch" | cut -d: -f1)
since=${3:-$(sed -n "1,$((first - 2))p" "$patch" | grep -E '^  [0-9a-f]{10} ' | tail -1 | awk '{print $1}')}
git -C "$native" merge-base --is-ancestor "$since" "$ref" ||
    { echo "hakux-patch: $since (the committed patch's last commit) is not an ancestor of $ref" >&2; exit 1; }
tmp=$(mktemp)
{
    sed -n "1,$((first - 2))p" "$patch"
    # (an author recorded as "0" is Möhle Jonas: hakuX-lr's pull requests, as merged)
    git -C "$native" log --reverse --format='  %h %an: %s' --abbrev=10 "$since..$ref" |
        sed 's/^  \([0-9a-f]*\) 0:/  \1 Möhle Jonas:/' |
        python3 -c 'import sys, textwrap
for l in sys.stdin:
    print(textwrap.fill(l.rstrip("\n"), 100, subsequent_indent=" " * 13, break_long_words=False, break_on_hyphens=False))'
    echo
    git -C "$native" diff --full-index "$pin" "$ref" -- "${paths[@]}"
} > "$tmp"
mv "$tmp" "$patch"
echo "hakux-patch: $patch from $(git -C "$native" rev-parse --short=10 "$ref") ($(git -C "$native" rev-list --count "$since..$ref") commits since $since)" >&2
