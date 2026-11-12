#!/usr/bin/env bash
# OGBox 0.0.9 - HDR output's last steps (hdr.frag, the 10-bit image copied into the framebuffer's
# format) on the PC, on lavapipe (Mesa's software Vulkan driver).
#   ps5/tests/hdr-ogbox/run.sh <work folder>
# Needs g++, libvulkan-dev and mesa-vulkan-drivers; without a driver it says so and checks nothing.
# The validation layer (vulkan-validationlayers) is used when installed.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ps5=$(cd "$here/../.." && pwd)
work=${1:?usage: run.sh <work folder>}
mkdir -p "$work"
icd=/usr/share/vulkan/icd.d/lvp_icd.json
[[ -f $icd ]] || { echo "hdr on the PC: NOT RUN (needs Mesa's lavapipe, $icd)"; exit 0; }
g++ -std=gnu++20 -O1 -g -Wall "$here/hdr_host_test.cpp" -o "$work/hdr_host_test" -lvulkan
VK_ICD_FILENAMES=$icd VK_DRIVER_FILES=$icd "$work/hdr_host_test" "$ps5/../shaders/glsl/"
