#!/usr/bin/env bash
# OGBox - the PC test of the game fixes fetched at start (src/ogbox_fixes_fetch.cpp), with the address
# and undefined-behaviour sanitizers, and the check that ps5/game-fixes.txt is current.
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#
#   tests/fixes/run-fixes-tests.sh <work folder>
#
# ps5_update.cpp (its SHA-256) needs libzip and RapidJSON as the updater's test does: its libzip
# build is used (tests/update/run-update-tests.sh builds it in its work folder).
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${1:?usage: run-fixes-tests.sh <work folder>}
mkdir -p "$work"
work=$(cd "$work" && pwd)
python3 "$root/ps5/tools/game-fixes-index.py" --check
libzip_src="$root/.deps/src/libzip"
zipwork="$root/build/update-tests/libzip"
[[ -f $zipwork/lib/libzip.a && -d $root/.deps/src/rapidjson/include ]] ||
	{ echo "fixes: run tests/update/run-update-tests.sh build/update-tests first (libzip)"; exit 2; }
san=(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
echo 'int main(){}' | "${CXX:-clang++}" -x c++ "${san[@]}" - -o "$work/san-probe" 2> /dev/null || san=()
"${CXX:-clang++}" -std=c++20 -g -O1 -Wall -Wextra -Werror "${san[@]}" -fno-omit-frame-pointer -pthread \
	-DOGBOX_FIXES_TEST -I "$root/ps5/src" -I "$root/ps5/src/platform" -isystem "$root/.deps/src/rapidjson/include" \
	-isystem "$libzip_src/lib" -isystem "$zipwork" \
	"$here/fixes_test.cpp" "$root/ps5/src/ogbox_fixes_fetch.cpp" "$root/ps5/src/platform/ps5_update.cpp" \
	"$zipwork/lib/libzip.a" -lz -o "$work/fixes_test"
"$work/fixes_test" "$work/run" 2> "$work/fixes_test.log" || { cat "$work/fixes_test.log"; exit 1; }
