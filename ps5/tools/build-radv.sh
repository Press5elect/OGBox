#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
# Crux2 - build the RADV archive Crux2 links: PS5 Mesa at the revision PS5_Vulkan pins, with every
# patch of ps5/patches/mesa applied in order, built by PS5_Vulkan's tools/build-radv.sh release.
#
# OGBox: Crux2's ps5/tools/build-radv-crux2.sh (08c3507), as ps5/tools/build-radv.sh, with Crux2's
# ps5/patches/mesa-27 (the only set OGBox carries). The archive is the same as Crux2's and Kelvin's
# and keeps its name, radv-crux2: one already built in PS5_Vulkan's .deps/native/radv-crux2 is
# linked as it is (ps5/tools/build.sh), and this script is only needed to make it again.
#
#   ps5/tools/build-radv.sh
#
# CRUX2_MESA_PATCHES names the folder of patches applied to the pin (ps5/patches/mesa-27 unless set;
# a relative path is taken from the repository's root): ps5/patches/mesa-27 carries the merge of
# Mesa main (27.0-devel) in place of the 26.2.x one.
#
# Makes branch crux2-radv in ../PS5_Mesa (the pin plus one commit a patch; nothing is pushed),
# builds it, and puts the archive in PS5_Vulkan's .deps/native/radv-crux2, which ps5/tools/build.sh
# links when it is there. PS5_Vulkan's own release archive (.deps/native/radv-release) is left as
# it was. Without radv-crux2, build.sh links that one, which has none of the patches.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
vulkan=${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}
mesa=${PS5_MESA_FORK:-$root/../PS5_Mesa}
patches=${CRUX2_MESA_PATCHES:-ps5/patches/mesa-27}
[[ $patches == /* ]] || patches="$root/$patches"
[[ -d $patches ]] && compgen -G "$patches/*.patch" > /dev/null ||
	{ echo "build-radv: no patches in $patches" >&2; exit 1; }
patches_name=${patches#"$root"/}

pin=$(sed -n 's/^mesa_revision=\${PS5_MESA_REVISION:-\([0-9a-f]*\)}$/\1/p' "$vulkan/tools/build-radv.sh")
[[ -n "$pin" ]] || { echo "build-radv: PS5_Vulkan's tools/build-radv.sh names no Mesa revision" >&2; exit 1; }
echo "==> PS5 Mesa ${pin:0:11} (PS5_Vulkan's pin) with $(ls "$patches"/*.patch | wc -l) patches"

git -C "$mesa" cat-file -e "$pin^{commit}" 2>/dev/null || git -C "$mesa" fetch --quiet origin
git -C "$mesa" checkout --quiet -B crux2-radv "$pin"
for patch in "$patches"/*.patch; do
	name=$(basename "$patch" .patch)
	if head -1 "$patch" | grep -q '^From [0-9a-f]\{40\}'; then
		# A git format-patch (author, date and message kept).
		git -C "$mesa" -c user.name=Crux2 -c user.email=noreply@anthropic.com am --quiet -3 "$patch"
	else
		git -C "$mesa" apply --index "$patch"
		git -C "$mesa" -c user.name=Crux2 -c user.email=noreply@anthropic.com commit --quiet -m "Crux2: $name"
	fi
	echo "    $name"
done
revision=$(git -C "$mesa" rev-parse HEAD)

native="$vulkan/.deps/native"
hold=""
if [[ -d "$native/radv-release" ]]; then
	hold=$(mktemp -d "$native/.radv-release-hold.XXXXXX")
	mv "$native/radv-release" "$hold/"
fi
restore() { [[ -n "$hold" && -d "$hold/radv-release" ]] && { rm -rf "$native/radv-release"; mv "$hold/radv-release" "$native/"; rmdir "$hold"; }; }
trap restore EXIT
# OGBox 0.1.28: PS5_Vulkan's script runs Meson through a wrapper made here, for two things:
#  - the host mesa_clc build takes the newest llvm-config it finds (llvm-config-20 when clang-20 is
#    installed), but the SPIR-V translator (LLVMSPIRVLib.pc) is built for one LLVM: the clc build is
#    given that LLVM's llvm-config in a native file (.deps/work/ogbox-clc-native.ini, which Meson reads again when it regenerates). Not through
#    LLVM_CONFIG: the SDK's compiler wrappers read it too, and RADV built by clang 18 leaves its weak
#    entry points visible, which the title's link refuses.
#  - it passes -Damd-use-llvm=false, which this Mesa deprecates in favour of the amd-options array;
#    Meson 1.12 hands the old boolean's "false" to the array and refuses it: -Damd-options=[]
#    instead (LLVM is disabled anyway).
if [[ -z ${MESON:-} ]]; then
	wrap=$(mktemp -d)
	real=$(command -v meson || echo "$HOME/.local/bin/meson")
	native_file=""
	if spirv=$(pkg-config --modversion LLVMSPIRVLib 2>/dev/null); then
		for config in "/usr/lib/llvm-${spirv%%.*}/bin/llvm-config" "$(command -v llvm-config-"${spirv%%.*}" || true)"; do
			[[ -x $config ]] && { native_file="$vulkan/.deps/work/ogbox-clc-native.ini"; mkdir -p "${native_file%/*}"; printf "[binaries]\nllvm-config = '%s'\n" "$config" > "$native_file"; break; }
		done
	fi
	cat > "$wrap/meson" <<WRAP
#!/usr/bin/env bash
args=()
clc=0
for a in "\$@"; do
	[[ \$a == -Damd-use-llvm=false ]] && grep -q "'amd-options'" "$mesa/meson.options" && a=-Damd-options=[]
	[[ \$a == */radv-clc-build ]] && clc=1
	args+=("\$a")
done
[[ \${args[0]:-} == setup && \$clc == 1 && -n "$native_file" ]] && args+=(--native-file "$native_file")
exec "$real" "\${args[@]}"
WRAP
	chmod +x "$wrap/meson"
	export MESON="$wrap/meson"
fi
(cd "$vulkan" && PS5_MESA_REVISION="$revision" bash tools/build-radv.sh release)
rm -rf "$native/radv-crux2"
mv "$native/radv-release" "$native/radv-crux2"
{
	echo
	echo "Crux2: PS5 Mesa $pin with $patches_name:"
	for patch in "$patches"/*.patch; do echo "  $(basename "$patch")"; done
} >> "$native/radv-crux2/PROVENANCE.txt"
echo "==> $native/radv-crux2 (Mesa crux2-radv ${revision:0:11}); ps5/tools/build.sh links it"
