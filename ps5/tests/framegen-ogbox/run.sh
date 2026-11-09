#!/usr/bin/env bash
# OGBox 0.0.7 - ogbox_framegen.cpp's four methods on the PC, on lavapipe (Mesa's software Vulkan driver).
#   ps5/tests/framegen-ogbox/run.sh <work folder>
# Needs g++, libvulkan-dev and mesa-vulkan-drivers; without a driver it says so and checks nothing.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ps5=$(cd "$here/../.." && pwd)
work=${1:?usage: run.sh <work folder>}
mkdir -p "$work"
icd=/usr/share/vulkan/icd.d/lvp_icd.json
[[ -f $icd ]] || { echo "framegen on the PC: NOT RUN (needs Mesa's lavapipe, $icd)"; exit 0; }
mg=$ps5/third_party/mihawkgen
g++ -std=gnu++20 -O1 -g -w -include vulkan/vulkan.h -I"$ps5" -I"$ps5/src" -I"$ps5/src/framegen" -I"$mg/include" -I"$mg/src" \
    -DMIHAWKGEN_MODEL_PATH="\"$mg/model/network.fgw\"" \
    "$here/fg_host_test.cpp" "$ps5/src/ogbox_framegen.cpp" "$ps5/src/framegen/ps5_compute_fx.cpp" \
    "$ps5/src/framegen/ps5_mihawkgen_model.cpp" "$mg/src/fg_common.cpp" "$mg/src/fg_vulkan.cpp" "$mg/src/fg_neural.cpp" \
    "$mg/src/fg_splat.cpp" -o "$work/fg_host_test" -lvulkan
VK_ICD_FILENAMES=$icd VK_DRIVER_FILES=$icd "$work/fg_host_test" "$ps5/../shaders/glsl/"
