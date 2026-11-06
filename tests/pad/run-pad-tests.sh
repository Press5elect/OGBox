#!/usr/bin/env bash
# OGBox by Press5elect, GPL-3.0-or-later. The PC test of ps5/src/ogbox_pad.cpp (tests/pad/pad_test.cpp),
# with the address and undefined-behaviour sanitizers.
#   tests/pad/run-pad-tests.sh [work folder]
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${1:-$root/build/pad-tests}
mkdir -p "$work"
"${CXX:-c++}" -std=c++20 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-fno-omit-frame-pointer -I "$root/ps5/src" "$here/pad_test.cpp" "$root/ps5/src/ogbox_pad.cpp" -o "$work/pad_test"
"$work/pad_test"
