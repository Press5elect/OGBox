#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
# OGBox - the third-party sources and libraries the title builds with, for the console.
# (Crux2's ps5/tools/build-deps.sh, trimmed to what OGBox links; OGBox by Press5elect.)
#
#   ps5/tools/build-deps.sh          fetch and build what is missing into .deps/
#
# Where they come from:
#   - the ps5-payload-dev "pacbrew" ports release (prebuilt static libraries for the console):
#     zlib's headers (the library itself is the copy inside the RADV archive), libsmb2 for the
#     SMB shares of the virtual file layer (ps5/src/platform/vfs).
#   - built here with the SDK's compilers: libnfs, for its NFS shares (as Crux2 builds it: its
#     8.0.0 release and the fixes after it), and glslang (ps5/tools/build-glslang.sh), for the HLE
#     GPU's run-time GLSL -> SPIR-V compiles.
#   - (0.0.1) curl, OpenSSL and libpsl from the same ports release, and libzip (built here)
#     and RapidJSON (headers), for the updater.
#   - sources the build compiles itself (ps5/cmake/hakux.cmake), at the revisions hakuX's own
#     build names (its android/README.md): libslirp 4.7.0+ (26be815b), unmodified, the NAT's TCP/IP stack;
#     nv2a_vsh_cpu, the vertex shader interpreter, at 89315e8, the revision xemu pins
#     (subprojects/nv2a_vsh_cpu.wrap; hakuX names "561fe80", which neither xemu-project's nor
#     abaire's repository has). xxHash is its header, v0.8.4, its newest release (hakuX takes FEX's copy, which
#     OGBox does not have). libchdr (47d7941, hakuX-lr's pin) for CHD disc images.
#
# Everything lands in .deps/ (ignored): src/ the sources, ports/ the prefix the build reads,
# build/ the build trees and their logs. Prints nothing on success but the steps it took.
set -euo pipefail

ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
sdk=$(bash "$ps5/tools/setup-sdk.sh")
deps="$root/.deps"
src="$deps/src"
sys="$deps/ports"
bld="$deps/build"
mkdir -p "$src" "$sys/include" "$sys/lib" "$bld"

step() { echo "==> [deps] $1" >&2; }
have() { [[ -f $bld/$1.done ]]; }
mark() { touch "$bld/$1.done"; }

# ---------------------------------------------------------------- prebuilt ports
# v0.40.2: the release Crux2 builds with (v0.41.1 is newer, 2026-11-01: not taken yet, so the
# two titles link the same ports).
pacbrew_version=v0.40.2
pacbrew_hash=a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d
pacbrew="$deps/pacbrew/user/homebrew"
if [[ ! -d $pacbrew/lib ]]; then
    step "prebuilt ports $pacbrew_version (about 346 MB)"
    archive="$deps/pacbrew-$pacbrew_version.tar.gz"
    [[ -f $archive ]] || wget -q "https://github.com/ps5-payload-dev/pacbrew-repo/releases/download/$pacbrew_version/ps5-payload-dev.tar.gz" -O "$archive"
    printf '%s  %s\n' "$pacbrew_hash" "$archive" | sha256sum --check --strict >/dev/null
    mkdir -p "$deps/pacbrew"
    tar -xzf "$archive" -C "$deps/pacbrew" --strip-components=3 opt/ps5-payload-sdk/target/user/homebrew
fi
if ! have ports-copy; then
    step "copying the ports the title uses"
    for header in zlib.h zconf.h; do
        cp "$pacbrew/include/$header" "$sys/include/"
    done
    rm -rf "$sys/include/smb2"
    cp -r "$pacbrew/include/smb2" "$sys/include/"
    cp "$pacbrew/lib/libsmb2.a" "$sys/lib/"
    mark ports-copy
fi
# OGBox 0.0.1: HTTPS for the updater (ps5/src/platform/ps5_update.cpp), as Crux2 has it: curl with
# OpenSSL and libpsl, from the same ports release. zlib is still the RADV archive's; curl's zstd
# decoding is libchdr's copy of zstd's decoder, which the build compiles. (libpsl names iconv, but
# nothing of it is linked in.)
if ! have ports-https; then
    step "copying the HTTPS ports (curl, OpenSSL, libpsl)"
    cp "$pacbrew/include/libpsl.h" "$sys/include/"
    for folder in curl openssl; do
        rm -rf "$sys/include/$folder"
        cp -r "$pacbrew/include/$folder" "$sys/include/"
    done
    for library in libcurl.a libssl.a libcrypto.a libpsl.a libz.a; do
        cp "$pacbrew/lib/$library" "$sys/lib/"
    done
    mark ports-https
fi

# OGBox 0.0.9: the startup video's decoder (ps5/src/ogbox_intro.cpp): libvpx's VP9 (BSD-3-Clause), the
# same ports release's.
if ! have ports-vpx; then
    step "copying the video decoder's port (libvpx)"
    rm -rf "$sys/include/vpx"
    cp -r "$pacbrew/include/vpx" "$sys/include/"
    cp "$pacbrew/lib/libvpx.a" "$sys/lib/"
    mark ports-vpx
fi

# --------------------------------------------------------------------- sources
fetch() {
    local name=$1 url=$2 revision=$3
    [[ -d $src/$name/.git && $(cat "$src/$name/.pshakux-revision" 2>/dev/null) == "$revision" ]] && return 0
    step "fetching $name ($revision)"
    rm -rf "$src/$name"
    mkdir -p "$src/$name"
    git -C "$src/$name" init -q
    git -C "$src/$name" remote add origin "$url"
    git -C "$src/$name" fetch -q --depth 1 origin "$revision"
    git -C "$src/$name" -c advice.detachedHead=false checkout -q FETCH_HEAD
    echo "$revision" > "$src/$name/.pshakux-revision"
}
# hakuX's pins (android/README.md, "NC_DEPS"): libslirp "4.7.0 (26be815b)" is master after the
# v4.7.0 tag (it reports 4.7.0, and has slirp_ssize_t, which native/net/nat.c uses; v4.8.0 is the
# first release with it).
fetch libslirp https://gitlab.freedesktop.org/slirp/libslirp.git 26be815b86e8d49add8c9a8b320239b9594ff03d
fetch nv2a_vsh_cpu https://github.com/xemu-project/nv2a_vsh_cpu 89315e8eff62605d2a96fe14429ebeac1dacaa8c
fetch xxhash https://github.com/Cyan4973/xxHash refs/tags/v0.8.4
# libchdr: hakuX-lr's pin (tools/native/linux-deps.json), master's newest (2026-10-11): CHD disc
# images (hakuX-native's native/core/chd.c), with its LZMA, zstd and FLAC decoders.
fetch libchdr https://github.com/rtissera/libchdr 47d794151b2b64f6aba60304e6480f4f5a2bc24b
# libnfs: Crux2's pin (its 8.0.0 release and the four commits after it on master up to 7613236).
fetch libnfs https://github.com/sahlberg/libnfs 7613236464317bee2d9636f087f995148de4db27
# OGBox 0.0.1, the updater's: libzip's newest release (v1.12.1) for the release's ZIP, and
# RapidJSON's master (24b5e7a, 2025: its last release, v1.1.0, is of 2016) for GitHub's answers,
# as Crux2 has them (PCSX2's 3rdparty copies).
fetch libzip https://github.com/nih-at/libzip refs/tags/v1.12.1
fetch rapidjson https://github.com/Tencent/rapidjson 24b5e7a8b27f42fa16b96fc70aade9106cf7102f
# OGBox 0.0.4: the DSP56300 JIT for the APU's full DSP: Matt Borgerson's dsp56300 (MIT, the
# Cranelift JIT xemu takes; its master on 2026-11-06, after its 1.0.0 release), with
# ps5/patches/dsp56300.
fetch dsp56300 https://github.com/mborgerson/dsp56300 b36db2c23ee2dd6e643f48e4dc6288074030e1a0

# ---------------------------------------------------------------- cross builds
common=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="$sys" -DCMAKE_PREFIX_PATH="$sys" -DCMAKE_FIND_ROOT_PATH="$sys"
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_VERBOSE_MAKEFILE=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
build() {
    local name=$1
    shift
    cmake "${common[@]}" -B "$bld/$name" "$@" >"$bld/$name.log" 2>&1 &&
        cmake --build "$bld/$name" >>"$bld/$name.log" 2>&1 &&
        cmake --install "$bld/$name" >>"$bld/$name.log" 2>&1 ||
        { tail -30 "$bld/$name.log" >&2; exit 1; }
    mark "$name"
}

# libnfs: NFS versions 3 and 4 with AUTH_SYS, for ps5/src/platform/vfs/ps5_vfs_nfs.cpp, as Crux2
# builds it (its ps5/tools/build-deps.sh says why each flag).
if ! have libnfs; then
    step "libnfs 8.0.0+ (7613236)"
    rm -rf "$sys/include/nfsc" "$sys/lib/libnfs.a" "$sys/lib/cmake/libnfs"
    nfs_flags="-ffunction-sections -fdata-sections -fno-omit-frame-pointer -DCLOCK_MONOTONIC_COARSE=CLOCK_MONOTONIC"
    nfs_flags+=" -DNO_SRV_AUTOSCAN -Dgetservbyport=ps5vfs_nfs_getservbyport -Dgetuid=ps5vfs_nfs_getuid"
    nfs_flags+=" -Dgetgid=ps5vfs_nfs_getgid"
    build libnfs -S "$src/libnfs" -DENABLE_TLS=OFF -DENABLE_MULTITHREADING=OFF -DENABLE_UTILS=OFF \
        -DENABLE_EXAMPLES=OFF -DENABLE_TESTS=OFF -DENABLE_DOCUMENTATION=OFF "-DCMAKE_C_FLAGS=$nfs_flags"
    rm -rf "$sys/lib/cmake/libnfs" "$sys/lib/pkgconfig/libnfs.pc"
fi

# libzip: reading only, with zlib (deflate) and nothing else; its headers and libzip.a.
# (libz.a is there for its configure step: the title links the RADV archive's zlib.) The SDK's
# stubs have functions the console's headers do not declare (memcpy_s and the other _s ones,
# _close, arc4random, getprogname): libzip is told they are not there, as Crux2's build is.
if ! have libzip; then
    step "libzip v1.12.1"
    rm -rf "$sys/include/zip.h" "$sys/include/zipconf.h" "$sys/lib/libzip.a" "$sys/lib/cmake/libzip"
    build libzip -S "$src/libzip" -DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF \
        -DENABLE_OPENSSL=OFF -DENABLE_WINDOWS_CRYPTO=OFF -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF \
        -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_OSSFUZZ=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF \
        -DZLIB_INCLUDE_DIR="$sys/include" -DZLIB_LIBRARY="$sys/lib/libz.a" \
        -DHAVE_MEMCPY_S=OFF -DHAVE_STRNCPY_S=OFF -DHAVE_STRERROR_S=OFF -DHAVE_STRERRORLEN_S=OFF \
        -DHAVE__CLOSE=OFF -DHAVE_ARC4RANDOM=OFF -DHAVE_GETPROGNAME=OFF \
        "-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections -fno-omit-frame-pointer"
    rm -rf "$sys/lib/cmake/libzip" "$sys/lib/pkgconfig/libzip.pc"
fi
# RapidJSON: headers only.
if ! have rapidjson; then
    rm -rf "$sys/include/rapidjson"
    cp -r "$src/rapidjson/include/rapidjson" "$sys/include/"
    mark rapidjson
fi

# The DSP56300 JIT: dsp56300-emu-ffi as a static library for the console (hakux.cmake links it;
# without it the DSP interpreter runs). Rust's standard library is built again for the console's
# target (ps5/rust/ps5/x86_64-unknown-freebsd.json: FreeBSD's, with emulated thread-locals, as the
# console's clang makes them, and no dynamic linking), so the build takes a nightly toolchain,
# pinned, with rust-src. Its patch (ps5/patches/dsp56300): the JIT's code in read-write-execute
# memory, where the PS5 makes read+execute pages execute-only, which OGBox gives it
# (host-memory: ps5/hakux/apu/dsp_jit_mem_ps5.c). Six of std's imports that the
# console's libkernel stub defines (and lld would export a definition of) are renamed in the
# archive to ogbox_rs_<name>; ps5/hakux/compat/rust_std_stubs.c defines them and the calls the
# console lacks. Its crates' licences go to $bld/dsp56300-THIRD-PARTY.txt (the notices).
dsp_toolchain=nightly-2026-11-05
dsp_symbols="chown chroot lchown linkat setpgid pthread_setname_np"
dsp_stamp="$dsp_toolchain b36db2c $(sha256sum "$ps5/patches/dsp56300/ogbox-ps5.patch" "$ps5/rust/ps5/x86_64-unknown-freebsd.json" | cut -c1-16 | tr '\n' ' ')"
if [[ $(cat "$bld/dsp56300.done" 2>/dev/null) != "$dsp_stamp" ]]; then
    rm -f "$sys/lib/libdsp56300_emu_ffi.a" "$sys/include/dsp56300.h"
    if ! command -v rustup >/dev/null; then
        echo "==> [deps] no rustup: the DSP56300 JIT is not built (full DSP interprets; https://rustup.rs)" >&2
    else
        step "the DSP56300 JIT (dsp56300 b36db2c, Rust $dsp_toolchain)"
        rustup toolchain install "$dsp_toolchain" --profile minimal --component rust-src >/dev/null 2>&1
        git -C "$src/dsp56300" checkout -q -- . && git -C "$src/dsp56300" clean -qfd -e .pshakux-revision
        git -C "$src/dsp56300" apply "$ps5/patches/dsp56300/ogbox-ps5.patch"
        rm -f "$src/dsp56300/rust-toolchain.toml"   # (its channel, 1.99.1, can't build std for a target)
        target_dir="$bld/dsp56300"
        # (unwinding: the crate catches its panics at the C boundary, logs them and stops the JIT, the
        # interpreter going on, instead of the title aborting; the console's libunwind unwinds)
        ( cd "$src/dsp56300" && CARGO_TARGET_DIR="$target_dir" \
            cargo "+$dsp_toolchain" build --locked --release -Zjson-target-spec -Zbuild-std=std,panic_unwind \
            --target "$ps5/rust/ps5/x86_64-unknown-freebsd.json" \
            -p dsp56300-emu-ffi --no-default-features --features host-memory ) > "$bld/dsp56300.log" 2>&1 \
            || { tail -30 "$bld/dsp56300.log" >&2; exit 1; }
        objcopy=$(dirname "$("$sdk/toolchain/../bin/prospero-clang" -print-prog-name=clang 2>/dev/null || echo /usr/lib/llvm-20/bin/clang)")/llvm-objcopy
        [[ -x $objcopy ]] || objcopy=/usr/lib/llvm-20/bin/llvm-objcopy
        renames=()
        for name in $dsp_symbols; do renames+=(--redefine-sym "$name=ogbox_rs_$name"); done
        "$objcopy" "${renames[@]}" "$target_dir/x86_64-unknown-freebsd/release/libdsp56300_emu_ffi.a" \
            "$sys/lib/libdsp56300_emu_ffi.a"
        cp "$src/dsp56300/crates/emu-ffi/include/dsp56300.h" "$sys/include/"
        # every crate linked (the standard library's included) with its licence and its texts
        python3 "$ps5/tools/rust-licences.py" "$src/dsp56300" "$dsp_toolchain" "$target_dir" \
            > "$bld/dsp56300-THIRD-PARTY.txt"
        rustc "+$dsp_toolchain" --version > "$bld/dsp56300.toolchain.txt"
        echo "$dsp_stamp" > "$bld/dsp56300.done"
    fi
fi

# glslang (its own script: it is also built for the PC's tests).
bash "$ps5/tools/build-glslang.sh" ps5 > /dev/null
