#!/usr/bin/env bash
# PSHakuX - link the title and package its folder.
#
#   link-title.sh BUILD_DIR PS5_VULKAN_DIR SDK OBJECT... -- ARCHIVE...
#
# Run by the build (ps5/CMakeLists.txt) once the objects are compiled. The link is PS5_Vulkan's:
# its RADV archive (RADV, ACO, NIR and Mesa's runtime, built from PS5_Mesa), its link recipe
# (tools/radv-link.sh: the platform layer of this title's SDK pin, the heap and thread wraps, the
# libc names bound to ps5_*), its CRT, its native tool (ELF -> the console's fake SELF) and its
# libc.prx, as its CTS title links them.
#
# On top of the recipe:
#   - Kelvin's high base: the eboot is linked at 0x30_0000_0000 (ps5/ld/), so nothing of the title
#     sits below 4 GiB, where the Xbox's address space belongs (the native CPU backend runs guest
#     code at its own addresses); PS5_Vulkan's converter takes it with Kelvin's patch
#     (tools/build-native-tool.sh). PSHAKUX_EBOOT_BASE=low links at 0, as the template does.
#   - clock_gettime is wrapped for the whole title, RADV and libc++ included: CLOCK_MONOTONIC from
#     the processor's counter (src/platform/ps5_clock.c, Crux2's), not a 20 us system call.
#   - Crux2's libc names (src/platform/ps5_libc.c): getaddrinfo through the console's resolver,
#     the functions no system module exports (pipe2, lockf, timegm...) and those a title may be
#     refused, lstat through stat, poll restarted after a signal;
#   - every other libc name the title references that the platform layer replaces (as Crux2's
#     link-title.sh binds them), except the ones whose replacement calls the console's own.
#
# The title folder, dist/<TITLE_ID>/, gets the signed eboot.bin, sce_sys/, sce_module/libc.prx,
# the presenter's shaders, sandbox-elevator.elf (tools/build-elevator.sh); tools/build.sh then lays
# in the assets (tools/build-assets.py) and the
# notices (tools/stage-notices.py: LEGAL.txt, NOTICE.txt and licenses/).
#
# Made from PS5 Vulkan Template's ps5/tools/link-title.sh (Copyright (C) 2026 Mihawk, MIT), with
# Kelvin's and Crux2's additions (Press5elect). SPDX-License-Identifier: MIT

set -euo pipefail
[[ -n ${LINK_TRACE:-} ]] && set -x

work=$1 vulkan=$2 sdk=$3
shift 3
objects=()
archives=()
while (( $# )); do
    [[ $1 == -- ]] && { shift; archives=("$@"); break; }
    objects+=("$1")
    shift
done
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
# RADV_ARCHIVE (tools/build.sh sets it), else radv-crux2 when PS5_Vulkan has it, else its release
# archive, as tools/build.sh chooses (for a link run by ninja alone).
if [[ -z ${RADV_ARCHIVE:-} && -f $vulkan/.deps/native/radv-crux2/lib/libvulkan_radeon.ps5.a ]]; then
    archive=$vulkan/.deps/native/radv-crux2/lib/libvulkan_radeon.ps5.a
else
    archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
fi
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
tool="$vulkan/build/host/ps5-native-tool"
native="$vulkan/tooling/native"
param="$ps5/sce_sys/param.json"
nm="$sdk/bin/llvm-nm"

for file in "$archive" "$tool" "$param" "$sdk/bin/prospero-lld" "$vulkan/tools/radv-link.sh" \
        "$vulkan/runtime/libc.prx" "$work/samples.txt" "${archives[@]}"; do
    [[ -e $file ]] || { echo "missing: $file" >&2; exit 2; }
done
mkdir -p "$work/link/obj" "$work/link/stubs"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }

# The CRT. The C++ runtime is libc++abi's, as in PS5_Vulkan's CTS title.
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$work/link/obj/app_crt.o"
# RADV calls AGC, which the SDK has no stubs for: these name its imports.
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/link/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/link/stubs/${library}.so" "$work/link/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2

# ---- the high base (Kelvin's) ------------------------------------------------------------------
if [[ ${PSHAKUX_EBOOT_BASE:-high} == high ]]; then
    radv_linker_script=(-T "$ps5/ld/ps5-pie-unwind.ld" -L "$ps5/ld" -L "$vulkan/tooling/native")
    tool=$(bash "$ps5/tools/build-native-tool.sh" "$vulkan" "$work/native-tool")
fi

# ---- the libc names this title binds (Crux2's scheme) ---------------------------------------------
declare -A bound=()
for flag in "${radv_link_flags[@]}"; do
    case $flag in
        --defsym=*) name=${flag#--defsym=}; bound[${name%%=*}]=1 ;;
        --wrap=*) bound[${flag#--wrap=}]=1 ;;
    esac
done
extra_flags=()
extra_names=()
bind() { # bind NAME TARGET: the recipe's flag kept when it names the same target, made the title's when another
    local index
    for index in "${!radv_link_flags[@]}"; do
        case ${radv_link_flags[$index]} in
            "--defsym=$1=$2") return 0 ;;
            "--defsym=$1="*) radv_link_flags[index]="--defsym=$1=$2"; return 0 ;;
        esac
    done
    extra_flags+=("--defsym=$1=$2")
    extra_names+=("$1")
    bound[$1]=1
}
# Name lookups through the console's resolver (the platform layer's refuses every lookup).
bind getaddrinfo ps5px_getaddrinfo
bind freeaddrinfo ps5px_freeaddrinfo
# No system module exports these; and those a title may be refused (src/platform/ps5_libc.c).
for name in pipe2 lockf gethostid timegm getprotobyname sendmmsg recvmmsg _setjmp _longjmp \
        getlogin_r gethostname random srandom fdatasync issetugid pathconf memrchr basename fnmatch; do
    bind "$name" "ps5px_$name"
done
bind recvmsg ps5px_recvmsg
# std::filesystem (libc++) names these; the platform layer's refuse as a file system without links.
for name in symlink link getcwd; do
    bind "$name" "ps5_$name"
done
# clock_gettime from the processor's counter (src/platform/ps5_clock.c); lstat through stat and poll
# restarted after a signal (src/platform/ps5_libc.c). The SDK's libScePosixForWebKit stub defines
# __wrap_clock_gettime too, which would export the title's (the converter refuses exports): the
# wrappers stay local.
extra_flags+=(--wrap=clock_gettime --wrap=lstat --wrap=poll)
extra_names+=(__wrap_clock_gettime __wrap_lstat __wrap_poll)

platform="$sdk/target/lib/libps5platform.a"
"$nm" --defined-only "$platform" 2>/dev/null | awk '$2 == "T" && $3 ~ /^ps5_/ { print substr($3, 5) }' \
    | sort -u > "$work/link/platform-names.txt"
"$nm" -u "${objects[@]}" "${archives[@]}" 2>/dev/null | awk '$1 == "U" { print $2 }' \
    | sort -u > "$work/link/referenced-names.txt"
if grep -qx localeconv "$work/link/platform-names.txt" && [[ -z ${bound[localeconv]:-} ]]; then
    bind localeconv ps5_localeconv
fi
# The platform layer's functions that adjust an answer and call the console's own (sysconf,
# pthread_exit): bound by name, their call would be a call of themselves. Not bound.
"$nm" "$platform" 2>/dev/null | awk '
    /:$/ { member = $1; next }
    $1 == "U" { undefined[member] = undefined[member] " " $2 " " }
    $2 == "T" && $3 ~ /^ps5_/ { defined[member] = defined[member] " " substr($3, 5) }
    END {
        for (member in defined) {
            count = split(defined[member], names, " ")
            for (i = 1; i <= count; i++)
                if (index(undefined[member], " " names[i] " "))
                    print names[i]
        }
    }' | sort -u > "$work/link/platform-pass-through.txt"
while IFS= read -r name; do
    [[ -n ${bound[$name]:-} ]] && continue
    grep -qx -- "$name" "$work/link/platform-pass-through.txt" && continue
    bind "$name" "ps5_$name"
done < <(comm -12 "$work/link/platform-names.txt" "$work/link/referenced-names.txt")
printf '%s\n' "${extra_names[@]}" > "$work/link/bound-by-title.txt"
{
    printf '{\n    local:\n'
    printf '        %s;\n' "${extra_names[@]}"
    printf '};\n'
} > "$work/link/pshakux-local.map"

# ---- the link -------------------------------------------------------------------------------------
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" "${extra_flags[@]}" \
    --version-script "$native/app-symbols.map" \
    --version-script "$work/link/pshakux-local.map" --exclude-libs=ALL -Map="$work/link/llvm-pie.map" \
    -e _start -o "$work/link/llvm-pie.elf" \
    "$work/link/obj/app_crt.o" --start-group "${objects[@]}" "${archives[@]}" --end-group \
    "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so
# A title whose localeconv is still the console's reads numbers wrong (no decimal point).
if grep -qx localeconv "$work/link/platform-names.txt" &&
        grep -q " U localeconv$" <<<"$("$nm" "$work/link/llvm-pie.elf" 2>/dev/null)"; then
    echo "link-title.sh: localeconv is not bound to the platform layer's ps5_localeconv" >&2
    exit 1
fi
# The fast clock must be the one linked.
if ! grep -q " [Tt] __wrap_clock_gettime$" <<<"$("$nm" "$work/link/llvm-pie.elf" 2>/dev/null)"; then
    echo "link-title.sh: clock_gettime is wrapped but __wrap_clock_gettime (src/platform/ps5_clock.c) is not linked" >&2
    exit 1
fi
# A title loads neither libkernel_sys's exports nor libScePosixForWebKit's: an import only their
# stubs define links, and is null at run time (its first call jumps to address 0). Refused here.
null_imports=$(comm -23 \
    <("$nm" -D --undefined-only "$work/link/llvm-pie.elf" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so"; do
        case ${library##*/} in libkernel_sys.so | libScePosixForWebKit.so) continue ;; esac
        "$nm" -D --defined-only "$library" 2>/dev/null | awk '{ print $NF }'
    done | sort -u))
if [[ -n $null_imports ]]; then
    echo "link-title.sh: imports that no module a title loads exports (null at run time): ${null_imports//$'\n'/ }" >&2
    echo "link-title.sh: bind them to the platform layer, or to a function of the title's own" >&2
    exit 1
fi
[[ ${LINK_ONLY:-0} == 1 ]] && { echo "linked: $work/link/llvm-pie.elf"; exit 0; }
"$tool" link --in "$work/link/llvm-pie.elf" --out "$work/eboot.elf.new" \
    --stub-dir "$sdk/target/lib" --stub "$work/link/stubs/libSceAgc.so" \
    --stub "$work/link/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf

# ---- the title folder -----------------------------------------------------------------------------
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
app="$root/dist/$title_id"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/shaders/glsl"
"$tool" self --sign --in "$work/eboot.elf.new" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
cp "$param" "$app/sce_sys/param.json"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $ps5/sce_sys/$asset ]] && cp "$ps5/sce_sys/$asset" "$app/sce_sys/$asset"
done
(cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
# The precompiled SPIR-V of the base class and of the program (CMake writes the list without a
# final newline: read then reports end of file).
IFS=';' read -r -a samples < "$work/samples.txt" || (( ${#samples[@]} ))
rm -rf "$app/shaders/glsl"
mkdir -p "$app/shaders/glsl"
for dir in base "${samples[@]}"; do
    compgen -G "$root/shaders/glsl/$dir/*.spv" > /dev/null || continue
    mkdir -p "$app/shaders/glsl/$dir"
    cp "$root/shaders/glsl/$dir/"*.spv "$app/shaders/glsl/$dir/"
done
# Leaving the sandbox: the helper the title sends to the payload loader (tools/build-elevator.sh;
# src/platform/ps5_sandbox.cpp).
cp "$(bash "$ps5/tools/build-elevator.sh")" "$app/sandbox-elevator.elf"
# What a user writes: an example boot.txt (the title reads /data/homebrew/<TITLE_ID>/boot.txt).
cp "$ps5/package/boot.example.txt" "$app/boot.example.txt"
[[ -f $root/README.md ]] && cp "$root/README.md" "$app/README.md"
mv "$work/eboot.elf.new" "$work/eboot.elf"
archive_revision=$(sed -n 's/^revision: //p' "$(dirname "$(dirname "$archive")")/PROVENANCE.txt" 2>/dev/null || true)
printf '==> %s: %s (eboot.bin %s bytes; RADV %s %s; %d libc names bound by the title)\n' "$title_id" "$app" \
    "$(stat -c %s "$app/eboot.bin")" "$(basename "$(dirname "$(dirname "$archive")")")" "${archive_revision:0:11}" \
    "${#extra_names[@]}"
printf '    build identity: %s\n' "$(cat "$work/build-identity.txt" 2>/dev/null || echo unknown)"
# (The notices are written by tools/build.sh once the assets are in: tools/stage-notices.py.)
