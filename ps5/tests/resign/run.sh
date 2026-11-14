#!/usr/bin/env bash
# OGBox 1.0.2 - the save re-signer (src/ogbox_resign.cpp) on the PC.
#   ps5/tests/resign/run.sh <work folder>
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ps5=$(cd "$here/../.." && pwd)
work=${1:?usage: run.sh <work folder>}
mkdir -p "$work"
g++ -std=gnu++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$ps5/src" \
    "$here/resign_test.cpp" "$ps5/src/ogbox_resign.cpp" -o "$work/resign_test"
"$work/resign_test"
