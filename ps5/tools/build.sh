#!/usr/bin/env bash
# PSHakuX - build the title into dist/<TITLE_ID>/.
#
#   ps5/tools/build.sh             everything: the dependencies, hakuX's source, compile, link, package
#   ps5/tools/build.sh --compile   stop after compiling (no link)
#
# Configures ps5/ with the payload SDK's CMake toolchain (when its settings change), compiles what
# changed, links with RADV and packages the title folder (tools/link-title.sh), then lays the
# assets in (tools/build-assets.py).
#
#   PS5_CLANG        the host clang the SDK's compiler wrappers drive. LLVM 20's
#                    (/usr/lib/llvm-20/bin/clang): with clang 18, RADV's weak imports do not link.
#   HAKUX_SRC        the hakuX tree to build (default vendor/hakuX: upstream at the pin of
#                    tools/fetch-hakux.sh). HAKUX_SRC=../hakuX-native builds the native CPU
#                    backend's work tree as it is (read, never written: tools/prepare-hakux.sh)
#   NC_CPU           native (default: native/cpu/cpu_native.c when the tree has it) or stub
#   PS5_VULKAN_DIR   the PS5_Vulkan checkout (default ../PS5_Vulkan): the RADV archive, the link
#                    recipe, the native tool and libc.prx
#   RADV_ARCHIVE     the RADV archive to link (default: PS5_Vulkan's .deps/native/radv-crux2,
#                    made by tools/build-radv.sh, when it is there; else its radv-release)
#
# The payload SDK is this title's own pin of my fork (tools/setup-sdk.sh).
#
# Made from PS5 Vulkan Template's ps5/tools/build.sh (Copyright (C) 2026 Mihawk, MIT) and Kelvin's.
# The changes for PSHakuX: Copyright (C) 2026 Press5elect, GPL-3.0-or-later; keep the attribution
# "PSHakuX by Press5elect" (NOTICE).

set -euo pipefail

ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
vulkan=$(cd -- "${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}" && pwd)
sdk=$(bash "$ps5/tools/setup-sdk.sh")
build="$root/build/ps5"
ninja=$(command -v ninja || echo "$HOME/.local/bin/ninja")
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
case $("$PS5_CLANG" --version 2>/dev/null | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1) in
    1[0-9]) echo "build.sh: $PS5_CLANG is older than LLVM 20: RADV's weak imports will not link (PS5_CLANG=/usr/lib/llvm-20/bin/clang)" >&2 ;;
esac

# The dependencies, then hakuX's source as the build compiles it.
bash "$ps5/tools/build-deps.sh"
hakux_tree=$(cd -- "${HAKUX_SRC:-$root/vendor/hakuX}" 2>/dev/null && pwd || echo "$root/vendor/hakuX")
hakux_dir=$(bash "$ps5/tools/prepare-hakux.sh" "$hakux_tree")
cpu=${NC_CPU:-native}
# The guest's kernel area: past the title's libc.prx (0x80120000, the default), or 0 for a layout
# that leaves 0x80000000-0x80120000 free (ps5/cmake/hakux.cmake).
hole=${PSHAKUX_WINDOW_HOLE_END:-0x80120000u}

# (Configured again when what it is configured with changes.)
configure="$hakux_tree|$cpu|$hole|$vulkan|$sdk"
if [[ -f $build/build.ninja && $(cat "$build/configure-flags.txt" 2>/dev/null) != "$configure" ]]; then
    rm -f "$build/CMakeCache.txt" "$build/build.ninja"
fi
if [[ ! -f $build/build.ninja ]]; then
    mkdir -p "$build"
    printf '%s\n' "$configure" > "$build/configure-flags.txt"
    cmake -S "$ps5" -B "$build" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" \
        -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
        -DPS5_VULKAN_DIR="$vulkan" -DHAKUX_DIR="$hakux_dir" -DHAKUX_SOURCE_TREE="$hakux_tree" -DNC_CPU="$cpu" \
        -DPSHAKUX_WINDOW_HOLE_END="$hole" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF > "$build.configure.log" 2>&1 \
        || { cat "$build.configure.log" >&2; exit 1; }
    grep 'PSHakuX:' "$build.configure.log" | sed 's/^-- /    /' >&2 || true
fi

# The RADV archive: RADV_ARCHIVE when set; else radv-crux2 when PS5_Vulkan has it (PS5 Mesa at
# PS5_Vulkan's pin with the mesa-27 patches: tools/build-radv.sh); else PS5_Vulkan's own release
# archive. link-title.sh links it; radv-archive.txt names it, so a change of archive relinks.
crux2_radv="$vulkan/.deps/native/radv-crux2/lib/libvulkan_radeon.ps5.a"
if [[ -n ${RADV_ARCHIVE:-} ]]; then
    radv_choice="RADV_ARCHIVE"
elif [[ -f $crux2_radv ]]; then
    export RADV_ARCHIVE="$crux2_radv"
    radv_choice="radv-crux2"
else
    export RADV_ARCHIVE="$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
    radv_choice="radv-release (no radv-crux2 in PS5_Vulkan's .deps/native: tools/build-radv.sh makes it)"
fi
echo "==> RADV: $radv_choice: $RADV_ARCHIVE"
radv_stamp="$RADV_ARCHIVE $(sha256sum "$RADV_ARCHIVE" 2>/dev/null | cut -d' ' -f1)"
[[ $(cat "$build/radv-archive.txt" 2>/dev/null) == "$radv_stamp" ]] || printf '%s\n' "$radv_stamp" > "$build/radv-archive.txt"

if [[ ${1:-} == --compile ]]; then
    "$ninja" -C "$build" samples pshakux_platform pshakux_core hakux_glib hakux_slirp hakux_core hakux_hle hakux_apu hakux_ps5
    exit 0
fi
"$ninja" -C "$build" title
[[ ${LINK_ONLY:-0} == 1 ]] && exit 0
app="$root/dist/$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$ps5/sce_sys/param.json")"
python3 "$ps5/tools/build-assets.py" --install "$app"
# The notices beside eboot.bin, once everything else is in: LEGAL.txt, NOTICE.txt and licenses/,
# each part with its revision and the files it is in.
PSHAKUX_BUILD_DIR="$build" python3 "$ps5/tools/stage-notices.py" "$app"
