#!/usr/bin/env bash
# OGBox by Press5elect, GPL-3.0-or-later. The PC test of network.cfg (tests/shares/shares_test.cpp,
# ps5/src/ogbox_shares.cpp), with the address and undefined-behaviour sanitizers. The mounts are
# the title's VFS (ps5/src/platform/vfs); here they are stand-ins that succeed.
#   tests/shares/run-shares-tests.sh [work folder]
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${1:-$root/build/shares-tests}
mkdir -p "$work"
cat > "$work/vfs_stub.c" <<'C'
int ps5vfs_mount_smb(const char *name, const char *url) { (void)name; (void)url; return 0; }
int ps5vfs_mount_nfs(const char *name, const char *url) { (void)name; (void)url; return 0; }
int ps5vfs_unmount(const char *mount_point) { (void)mount_point; return 0; }
C
san=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
"${CC:-cc}" "${san[@]}" -c "$work/vfs_stub.c" -o "$work/vfs_stub.o"
"${CXX:-c++}" -std=c++20 "${san[@]}" -I "$root/ps5/src" "$here/shares_test.cpp" "$root/ps5/src/ogbox_shares.cpp" \
	"$work/vfs_stub.o" -o "$work/shares_test"
"$work/shares_test"
