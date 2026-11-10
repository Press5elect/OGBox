#!/usr/bin/env bash
# OGBox 0.0.8 - the save sync's engine (src/ogbox_save_sync.cpp) on the PC.
#   ps5/tests/savesync/run.sh <work folder>
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ps5=$(cd "$here/../.." && pwd)
work=${1:?usage: run.sh <work folder>}
mkdir -p "$work"
g++ -std=gnu++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$ps5/src" \
    "$here/save_sync_test.cpp" "$ps5/src/ogbox_save_sync.cpp" -o "$work/save_sync_test"
"$work/save_sync_test"
