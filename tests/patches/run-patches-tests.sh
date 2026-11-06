#!/usr/bin/env bash
# OGBox by Press5elect, GPL-3.0-or-later. The PC test of the IPS and BPS readers
# (ps5/src/core/ogbox_patches.c, tests/patches/patches_test.c), with the address and
# undefined-behaviour sanitizers. Only the readers are linked: the disc side is hakuX's
# (tests/patches/stub stands in for its header) and is dropped with --gc-sections.
#   tests/patches/run-patches-tests.sh [work folder]
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${1:-$root/build/patches-tests}
mkdir -p "$work"
cc=${CC:-cc}
flags=(-std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined
	-fno-omit-frame-pointer -ffunction-sections -fdata-sections -I "$root/ps5/src" -I "$here/stub")
"$cc" "${flags[@]}" "$here/patches_test.c" "$root/ps5/src/core/ogbox_patches.c" -Wl,--gc-sections \
	-o "$work/patches_test"
"$work/patches_test"
