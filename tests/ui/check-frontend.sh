#!/usr/bin/env bash
# OGBox by Press5elect, GPL-3.0-or-later. The front end (ps5/src/ogbox_main.cpp) can only be built
# for the console, but it can be type-checked on a PC: the same flags, include paths and wrapping
# (ps5/src/wrap.cpp.in) as the samples target in ps5/CMakeLists.txt, with -fsyntax-only. The menus'
# drawing (ogbox_ui.cpp) is compiled and run by tests/ui/run-ui-tests.sh.
#   tests/ui/check-frontend.sh
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf '#include "prelude.h"\nnamespace sample_ogbox {\n#include "%s"\n}\n' "$root/ps5/src/ogbox_main.cpp" > "$work/wrap.cpp"
"${CXX_CHECK:-clang++}" -std=c++20 -fsyntax-only -Wall -Wno-unused-function -Wno-unused-private-field \
	-DVK_EXAMPLE_PS5 '-DPS5_TITLE_NAME="OGBox"' -D_DIRECT2DISPLAY -DVK_NO_PROTOTYPES -DNDEBUG \
	'-DVK_EXAMPLE_ASSETS_DIR=""' '-DVK_EXAMPLE_SHADERS_DIR=""' -include "$root/ps5/third_party/volk/volk.h" \
	-I"$root/external" -I"$root/external/glm" -I"$root/external/imgui" -I"$root/external/tinygltf" -I"$root/external/stb" \
	-I"$root/external/ktx/include" -I"$root/external/ktx/other_include" -I"$root/base" -I"$root/ps5/third_party/volk" \
	-I"$root/ps5/src" -I"$root/ps5/src/platform" "$work/wrap.cpp"
# the library (covers, descriptions), the settings and the controller's mapping, each on its own
for f in ogbox_library.cpp ogbox_settings.cpp ogbox_pad.cpp; do
	"${CXX_CHECK:-clang++}" -std=c++20 -fsyntax-only -Wall -Wno-unused-function -I"$root/external" -I"$root/external/stb" -I"$root/external/tinygltf" \
		-I"$root/ps5/src" -I"$root/ps5/src/platform" "$root/ps5/src/$f"
done
echo "frontend: type-checks"
