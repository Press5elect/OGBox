#!/usr/bin/env bash
# OGBox 1.0.3 - the data folder's move (src/ogbox_storage.cpp) on the PC, in a folder of its own and
# one on another file system (/dev/shm: a copy, as to a USB drive, not a rename).
#   ps5/tests/storage/run.sh <work folder>
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ps5=$(cd "$here/../.." && pwd)
work=${1:?usage: run.sh <work folder>}
mkdir -p "$work"
work=$(cd "$work" && pwd)
rm -rf "$work/tree"
mkdir -p "$work/tree"
g++ -std=gnu++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$ps5/src" \
    "-DOGBOX_STORAGE_FILE=\"$work/tree/conf/storage.txt\"" \
    "$here/storage_test.cpp" "$ps5/src/ogbox_storage.cpp" -o "$work/storage_test" -lpthread
"$work/storage_test" "$work/tree" "${OGBOX_STORAGE_OTHER_FS:-/dev/shm}"
