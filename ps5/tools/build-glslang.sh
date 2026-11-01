#!/usr/bin/env bash
#
# Kelvin - original Xbox emulator for PS5
# Copyright (C) 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPLv3 section 7(b)): credit to Press5elect, see LICENSE.md
#
# OGBox: from Kelvin 3061bf1 (ps5/tools/build-glslang.sh), unchanged but for this line. Kelvin by
# Press5elect; see NOTICE.
#
# glslang (Khronos, BSD-3-Clause / MIT / Apache-2.0 parts: its LICENSE.txt) as static
# libraries, for the NV2A renderer's run-time GLSL -> SPIR-V compiles (src/hw/vk_renderer.c
# uses its C interface, glslang/Include/glslang_c_interface.h).
#
#   ps5/tools/build-glslang.sh ps5     the console's archives (the payload SDK's toolchain)
#   ps5/tools/build-glslang.sh host    the PC's, with the system's C++ library (tests/run_xbe)
#   ps5/tools/build-glslang.sh host-libcxx  the PC's with libc++ 18 (the host reference build,
#                                      which links libc++ as the console does)
#
# Prints the install prefix (include/, lib/) on its last line. Pinned to the newest release
# when it was added (16.6.0, checked 2026-10-26: git ls-remote --tags); built without
# SPIRV-Tools (ENABLE_OPT=OFF: the SPIR-V goes to the driver as glslang writes it), without
# HLSL and without its executables.
set -euo pipefail

kind=${1:?usage: build-glslang.sh ps5|host|host-libcxx}
version=16.6.0
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
deps="$root/.deps/glslang-$version"
src="$deps/src"
prefix="$deps/$kind"
build="$deps/build-$kind"

if [[ -f $prefix/.done ]]; then
    echo "$prefix"
    exit 0
fi
if [[ ! -f $src/CMakeLists.txt ]]; then
    rm -rf "$src"
    git clone -q --depth 1 --branch "$version" https://github.com/KhronosGroup/glslang "$src" >&2
fi
ninja=$(command -v ninja || echo "$HOME/.local/bin/ninja")
common=(-G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
        -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=OFF -DENABLE_OPT=OFF -DENABLE_HLSL=OFF
        -DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_TESTS=OFF -DGLSLANG_ENABLE_INSTALL=ON
        -DENABLE_SPVREMAPPER=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
case $kind in
ps5)
    sdk=$(bash "$ps5/tools/setup-sdk.sh")
    export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
    cmake -S "$src" -B "$build" "${common[@]}" -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
        "-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections -fno-omit-frame-pointer" \
        "-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections -fno-omit-frame-pointer" > "$deps/$kind.log" 2>&1
    ;;
host | host-libcxx)
    # host-libcxx: against the libc++ ps5/tools/host-reference.sh unpacks (it must be there)
    libcxx="$root/build/ps5-host/libcxx/root"
    cxxflags="-ffunction-sections -fdata-sections -fno-omit-frame-pointer"
    if [[ $kind == host-libcxx ]]; then
        [[ -d $libcxx/include/c++/v1 ]] || { echo "build-glslang.sh: no libc++ in $libcxx" >&2; exit 2; }
        cxxflags+=" -stdlib=libc++ -nostdinc++ -isystem $libcxx/include/c++/v1"
    fi
    cmake -S "$src" -B "$build" "${common[@]}" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        "-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections -fno-omit-frame-pointer" \
        "-DCMAKE_CXX_FLAGS=$cxxflags" > "$deps/$kind.log" 2>&1
    ;;
*)
    echo "build-glslang.sh: ps5, host or host-libcxx" >&2
    exit 2
    ;;
esac
cmake --build "$build" >> "$deps/$kind.log" 2>&1 || { tail -30 "$deps/$kind.log" >&2; exit 1; }
cmake --install "$build" >> "$deps/$kind.log" 2>&1
git -C "$src" rev-parse HEAD > "$prefix/REVISION"
touch "$prefix/.done"
echo "$prefix"
