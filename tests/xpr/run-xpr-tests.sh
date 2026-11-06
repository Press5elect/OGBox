#!/usr/bin/env bash
# OGBox by Press5elect, GPL-3.0-or-later. The PC test of the title image decoder
# (ps5/src/core/ogbox_xpr.c, tests/xpr/xpr_test.c), with the address and undefined-behaviour sanitizers.
#   tests/xpr/run-xpr-tests.sh [work folder]
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${1:-$root/build/xpr-tests}
mkdir -p "$work"
"${CC:-cc}" -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-fno-omit-frame-pointer -I "$root/ps5/src" "$here/xpr_test.c" "$root/ps5/src/core/ogbox_xpr.c" -o "$work/xpr_test"
"$work/xpr_test"
