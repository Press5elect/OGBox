#!/usr/bin/env bash
# OGBox - the PC test of the write broker (ps5/broker: the payload's request handler; the title's
# client and storage write test, ps5/src/platform/ps5_broker.cpp), with the address and
# undefined-behaviour sanitizers; then the payload itself built for the console with the pinned SDK's
# prospero-clang++ (ps5/tools/build-broker.sh), when the SDK is there.
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#
#   tests/broker/run-broker-tests.sh <work folder>
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${1:?usage: run-broker-tests.sh <work folder>}
mkdir -p "$work"
work=$(cd "$work" && pwd)
san=(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
echo 'int main(){}' | "${CXX:-clang++}" -x c++ "${san[@]}" - -o "$work/san-probe" 2> /dev/null || san=()
"${CXX:-clang++}" -std=c++20 -g -O1 -Wall -Wextra -Werror "${san[@]}" -fno-omit-frame-pointer -pthread \
	-I "$root/ps5/src" -I "$root/ps5/src/platform" \
	"$here/broker_test.cpp" "$root/ps5/broker/server.cpp" "$root/ps5/src/platform/ps5_broker.cpp" -o "$work/broker_test"
rm -rf "$work/run"
"$work/broker_test" "$work/run" 2> "$work/broker_test.log" || { cat "$work/broker_test.log"; exit 1; }
tail -n 1 "$work/broker_test.log"
rm -rf "$work/run"
# the payload, for the console (skipped where the SDK isn't set up: CI without .deps)
if [[ -x $root/.deps/native/ps5-payload-sdk/bin/prospero-clang++ ]]; then
	bash "$root/ps5/tools/build-broker.sh" "$work/write-broker.elf" > /dev/null
	echo "broker: write-broker.elf built for the console ($(stat -c %s "$work/write-broker.elf") bytes)"
else
	echo "broker: the payload SDK is not set up (.deps/native/ps5-payload-sdk): write-broker.elf not built"
fi
