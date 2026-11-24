#!/usr/bin/env bash
# OGBox 0.0.8 - the save sync's engine (src/ogbox_save_sync.cpp) on the PC; 1.0.3: the hard disk's
# saves on their files (src/ogbox_saves.cpp: an import's and a sync write's renames).
#   ps5/tests/savesync/run.sh <work folder>
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ps5=$(cd "$here/../.." && pwd)
work=${1:?usage: run.sh <work folder>}
mkdir -p "$work"
work=$(cd "$work" && pwd)
g++ -std=gnu++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$ps5/src" \
    "$here/save_sync_test.cpp" "$ps5/src/ogbox_save_sync.cpp" -o "$work/save_sync_test"
"$work/save_sync_test"
g++ -std=gnu++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$ps5/src" -I"$ps5/src/platform" \
    "-DOGBOX_STORAGE_FILE=\"$work/storage.txt\"" \
    "$here/saves_files_test.cpp" "$ps5/src/ogbox_save_sync.cpp" "$ps5/src/ogbox_storage.cpp" \
    "$ps5/src/platform/ps5_broker.cpp" -o "$work/saves_files_test" -lpthread
"$work/saves_files_test" "$work/files"
