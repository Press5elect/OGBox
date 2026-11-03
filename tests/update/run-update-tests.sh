#!/usr/bin/env bash
# Crux2 - the PC test of the updater, with the address and undefined-behaviour sanitizers.
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0+
# Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
#
# OGBox 0.0.1: Crux2's ps5/tests/update/run-update-tests.sh (08c3507) for OGBox's updater
# (ps5/src/platform/ps5_update.cpp): libzip is the one the title links (.deps/src/libzip,
# ps5/tools/build-deps.sh), built once for the PC into the work folder, deflate only; RapidJSON is
# the title's (.deps/src/rapidjson). OGBox by Press5elect.
#
#   tests/update/run-update-tests.sh <work folder>
#
# When a release ZIP was made in this tree (dist/release-<tag>/OGBox-<tag>.zip) the newest is
# installed as well.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${1:?usage: run-update-tests.sh <work folder>}
mkdir -p "$work"
work=$(cd "$work" && pwd)
libzip_src="$root/.deps/src/libzip"
[[ -f $libzip_src/CMakeLists.txt && -d $root/.deps/src/rapidjson/include ]] ||
	{ echo "update: run ps5/tools/build-deps.sh first (libzip, RapidJSON)"; exit 2; }

if [ ! -f "$work/libzip/lib/libzip.a" ]; then
	generator=()
	command -v ninja > /dev/null && generator=(-G Ninja)
	cmake -S "$libzip_src" -B "$work/libzip" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
		-DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF -DENABLE_OPENSSL=OFF \
		-DENABLE_WINDOWS_CRYPTO=OFF -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF -DBUILD_TOOLS=OFF \
		-DBUILD_REGRESS=OFF -DBUILD_OSSFUZZ=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF > "$work/libzip-build.log" 2>&1 &&
		cmake --build "$work/libzip" >> "$work/libzip-build.log" 2>&1 ||
		{ echo "update: libzip could not be built (see $work/libzip-build.log)"; exit 2; }
fi
zipflags=(-isystem "$libzip_src/lib" -isystem "$work/libzip" "$work/libzip/lib/libzip.a" -lz)

export ASAN_OPTIONS=detect_leaks=1
export UBSAN_OPTIONS=print_stacktrace=1
clang++ -std=c++20 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-fno-omit-frame-pointer -pthread -DOGBOX_UPDATE_TEST -I "$root/ps5/src/platform" \
	-isystem "$root/.deps/src/rapidjson/include" \
	"$here/update_test.cpp" "$root/ps5/src/platform/ps5_update.cpp" "${zipflags[@]}" -o "$work/update_test"
# A real release ZIP as well, when one was made here (dist/release-<tag>/OGBox-<tag>.zip, the one made
# last: 0.0.1 came after the bring-up builds 0.1.x).
real=$(ls -1t "$root"/dist/release-*/OGBox-v*.zip 2>/dev/null | grep -v -e '-source\.zip$' -e '-part[0-9]' | head -n 1 || true)
rm -rf "$work/run"
# The steps go to update_test.log; the summary line to the screen. On a failure, the log too.
"$work/update_test" "$work/run" ${real:+"$real"} 2> "$work/update_test.log" || { cat "$work/update_test.log"; exit 1; }
