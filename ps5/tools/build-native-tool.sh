#!/usr/bin/env bash
# Kelvin - PS5_Vulkan's ps5-native-tool with the high-base patch.
#
#   build-native-tool.sh PS5_VULKAN_DIR OUT_DIR      prints the tool's path
#
# Kelvin links its eboot at 0x30_0000_0000 (ps5/ld/), so that nothing of the
# title sits below 4 GiB, where the Xbox's address space belongs. PS5_Vulkan's
# converter assumes text at address 0; ps5/patches/native-tool-high-base.patch
# lets it take a text segment at any page-aligned address (and, for tests, an
# e_type from PS5_ETYPE). For a link at 0 its output is byte for byte the same.
#
# Copyright (C) 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPLv3 section 7(b)): credit to Press5elect, see LICENSE.md
#
# OGBox: from Kelvin 3061bf1 (ps5/tools/build-native-tool.sh), unchanged but for this line. Kelvin by
# Press5elect; see NOTICE.
set -euo pipefail
vulkan=$1 out=$2
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
native="$vulkan/tooling/native"
patch_file="$ps5/patches/native-tool-high-base.patch"
tool="$out/ps5-native-tool"
zlib_root="$vulkan/.deps/native/zlib/root"
zlib_archive=$(find "$zlib_root" -type f -name libz.a -print -quit)
[[ -n $zlib_archive ]] || { echo "build-native-tool.sh: no zlib in $zlib_root (run PS5_Vulkan's setup first)" >&2; exit 2; }
stale=
if [[ ! -x $tool ]]; then
    stale=1
else
    for input in "$patch_file" "$native"/*.cpp "$native"/*.hpp "$native"/*.h; do
        [[ -e $input && $input -nt $tool ]] && { stale=1; break; }
    done
fi
if [[ -n $stale ]]; then
    rm -rf "$out/src"
    mkdir -p "$out/src"
    cp "$native"/*.cpp "$native"/*.hpp "$out/src/"
    cp "$native"/*.h "$out/src/" 2> /dev/null || true
    patch -s -d "$out/src" -p1 < "$patch_file" >&2
    "${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Werror -I "$zlib_root/usr/include" \
        "$out/src/native_app_builder.cpp" "$out/src/self_container.cpp" \
        "$out/src/elf_object.cpp" "$out/src/sce_module_writer.cpp" \
        "$zlib_archive" -o "$tool" >&2
fi
printf '%s\n' "$tool"
