#!/bin/bash
# Draws every menu screen on this computer into build/ui-tests/*.png (tests/ui/render.cpp).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:-$root/build/ui-tests}"
mkdir -p "$out"
rm -f "$out"/*.png "$out"/*.ppm   # (a screen taken out leaves no old picture)
im="$root/external/imgui"
clang++ -std=c++17 -O2 -w -I"$im" -I"$root/ps5/src" -I"$root/external/tinygltf" \
	"$root/tests/ui/render.cpp" "$root/ps5/src/ogbox_ui.cpp" "$root/ps5/src/ogbox_ui_home.cpp" "$root/ps5/src/ogbox_ui_settings.cpp" "$root/ps5/src/ogbox_ui_screens.cpp" "$im/imgui.cpp" "$im/imgui_draw.cpp" "$im/imgui_widgets.cpp" \
	-o "$out/render"
"$out/render" "$root" "$out"
python3 - "$out" <<'PY'
import sys, glob, os
from PIL import Image
for p in glob.glob(sys.argv[1] + "/*.ppm"):
    Image.open(p).resize((1920, 1080), Image.LANCZOS).save(p[:-4] + ".png")
    os.remove(p)
PY
ls "$out"/*.png
