#!/usr/bin/env bash
# OGBox - hakuX for PS5
# Copyright (C) 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#
# The build's identity, for the first lines of the boot log (ogbox-boot.log): which eboot.bin
# wrote a log, and which ELF its crash addresses belong to. As Crux2's tools/build.sh makes
# PS5PX_BUILD_IDENTITY and Kelvin's tools/build-identity.sh after it (Crux2 by Press5elect): one
# digest over what decides the binary, then the revisions by name.
#
#   build-identity.sh HEADER TEXT PS5_VULKAN_DIR HAKUX_SRC CPU_BACKEND
#
# Run by the build at every ninja run (ps5/CMakeLists.txt, target pshakux_identity). HEADER
# (ps5_build_identity.h) is rewritten only when the identity changed, so an unchanged tree does
# not recompile or relink; TEXT (build-identity.txt beside the link) has the same line.
#
#   OGBox's revision, "+dirty <digest>" when the working tree differs from it (the digest is
#   over the difference and the untracked files, so two dirty builds are told apart);
#   hakuX's source (vendor/hakuX at the pin with ps5/patches/hakux, or HAKUX_SRC's work tree), its
#   revision and its own "+dirty <digest>"; the CPU backend linked (native, or the stub);
#   the RADV archive the link takes (RADV_ARCHIVE, else PS5_Vulkan's radv-crux2, else its
#   radv-release), its Mesa revision and its sha256; the payload SDK pin; PS5_Vulkan's revision.
set -euo pipefail
header=$1 text=$2 vulkan=$3 hakux=$4 cpu=$5
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")

dirty_of() { # the "+dirty <digest>" of a work tree, or nothing
    local tree=$1
    [[ -n $(git -C "$tree" status --porcelain 2>/dev/null) ]] || return 0
    local digest
    digest=$( {
        git -C "$tree" diff HEAD 2>/dev/null
        git -C "$tree" ls-files --others --exclude-standard -z 2>/dev/null | (cd "$tree" && xargs -0 -r sha256sum)
    } | sha256sum | cut -c 1-8)
    echo "+dirty $digest"
}

revision=$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)
dirty=$(dirty_of "$root")
version=$(tr -d ' \n' < "$ps5/version.txt" 2>/dev/null || true)
# OGBox 0.0.4: a release build is asked for (OGBOX_RELEASE=1, with the release's version in
# version.txt) and made from a clean tree; any other is a developer build, and says so where the
# version shows ("v0.0.4 - developer build"); the updater then takes this version's release as newer.
release=0
[[ ${OGBOX_RELEASE:-0} == 1 && -z $dirty ]] && release=1
if [[ ${OGBOX_RELEASE:-0} == 1 && -n $dirty ]]; then
    echo "build-identity.sh: OGBOX_RELEASE=1 but the tree has changes: a developer build" >&2
fi
# OGBox 0.0.3: param.json's contentVersion (what the console and the homebrew stores show) is
# version.txt's: X.Y.Z as XX.(Y*100+Z, three digits).000 (0.0.3: 00.003.000). A build where they
# differ stops, so the two can't drift apart again.
if [[ $version =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    content=$(printf '%02d.%03d.000' "${BASH_REMATCH[1]}" $(( BASH_REMATCH[2] * 100 + BASH_REMATCH[3] )))
    have=$(sed -n 's/.*"contentVersion": *"\([^"]*\)".*/\1/p' "$ps5/sce_sys/param.json")
    if [[ $have != "$content" ]]; then
        echo "build-identity.sh: ps5/version.txt says $version, so sce_sys/param.json's contentVersion must be $content (it is $have)" >&2
        exit 1
    fi
fi
hakux_revision=$(git -C "$hakux" rev-parse HEAD 2>/dev/null || echo unknown)
hakux_dirty=
# vendor/hakuX is the pin with this repository's patches applied: that difference is the patches
# (in OGBox's own digest already), not a dirty tree.
[[ $hakux == "$root/vendor/hakuX" ]] || hakux_dirty=$(dirty_of "$hakux")
hakux_name=$([[ $hakux == "$root/vendor/hakuX" ]] && echo "vendor/hakuX + ps5/patches/hakux" || echo "$hakux")
sdk_revision=$(cat "$root/.deps/native/ps5-payload-sdk/.ps5-sdk-revision" 2>/dev/null || echo unknown)
vulkan_revision=$(git -C "$vulkan" rev-parse HEAD 2>/dev/null || echo unknown)

crux2_radv="$vulkan/.deps/native/radv-crux2/lib/libvulkan_radeon.ps5.a"
if [[ -n ${RADV_ARCHIVE:-} ]]; then
    archive=$RADV_ARCHIVE
elif [[ -f $crux2_radv ]]; then
    archive=$crux2_radv
else
    archive="$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
fi
archive_name=$(basename "$(dirname "$(dirname "$archive")")")
mesa_revision=$(sed -n 's/^revision: //p' "$(dirname "$(dirname "$archive")")/PROVENANCE.txt" 2>/dev/null || true)
archive_digest=$(sha256sum "$archive" 2>/dev/null | cut -d ' ' -f 1 || true)
radv="$archive_name ${archive_digest:0:12} from Mesa ${mesa_revision:0:11}"

digest=$(printf '%s\n' "$revision" "$dirty" "$version" "release=$release" "$hakux_revision" "$hakux_dirty" "$cpu" "$sdk_revision" \
    "$vulkan_revision" "$archive_digest" | sha256sum | cut -c 1-16)
identity="$digest (OGBox ${version:-?}$([[ $release == 1 ]] || echo " developer build") ${revision:0:12}${dirty:+ $dirty}; hakuX ${hakux_revision:0:10}${hakux_dirty:+ $hakux_dirty} ($hakux_name); CPU $cpu; RADV $radv; sdk ${sdk_revision:0:7}; PS5_Vulkan ${vulkan_revision:0:7})"

new=$(
    printf '/* Generated by ps5/tools/build-identity.sh: the build identity for the boot log. */\n'
    printf '#define PSHAKUX_BUILD_IDENTITY "%s"\n' "$identity"
    printf '#define PSHAKUX_BUILD_VERSION "%s"\n' "${version:-}"
    printf '#define PSHAKUX_BUILD_RELEASE %d\n' "$release"
    printf '#define PSHAKUX_BUILD_REVISION "%s"\n' "$revision"
    printf '#define PSHAKUX_BUILD_DIRTY %d\n' "$([[ -n $dirty ]] && echo 1 || echo 0)"
    printf '#define PSHAKUX_BUILD_HAKUX "%s%s"\n' "$hakux_revision" "${hakux_dirty:+ $hakux_dirty}"
    printf '#define PSHAKUX_BUILD_CPU "%s"\n' "$cpu"
    printf '#define PSHAKUX_BUILD_RADV "%s"\n' "$radv"
    printf '#define PSHAKUX_BUILD_SDK "%s"\n' "$sdk_revision"
    printf '#define PSHAKUX_BUILD_VULKAN "%s"\n' "$vulkan_revision"
)
# The time goes in only when the identity changes: it is when this identity was first built.
if [[ ! -f $header ]] || ! grep -qF "#define PSHAKUX_BUILD_IDENTITY \"$identity\"" "$header"; then
    mkdir -p "$(dirname "$header")"
    { printf '%s\n' "$new"; printf '#define PSHAKUX_BUILD_STAMP "%s"\n' "$(date -u '+%Y-%m-%d %H:%M UTC')"; } > "$header.new"
    mv "$header.new" "$header"
fi
[[ $(cat "$text" 2>/dev/null) == "$identity" ]] || printf '%s\n' "$identity" > "$text"
