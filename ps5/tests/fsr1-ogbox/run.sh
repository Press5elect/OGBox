#!/usr/bin/env bash
# OGBox - FSR1's upscale and sharpening (fsr1.frag's EASU into an image at the screen's size, then
# rcas.frag's RCAS into the screen, as ogbox_main.cpp records them) on the PC, on lavapipe (Mesa's
# software Vulkan driver), against AMD's RCAS done on the CPU.
#   ps5/tests/fsr1-ogbox/run.sh <work folder>
# Needs g++, libvulkan-dev and mesa-vulkan-drivers; without a driver it says so and checks nothing.
# The validation layer (vulkan-validationlayers) is used when installed: any message of it fails.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ps5=$(cd "$here/../.." && pwd)
work=${1:?usage: run.sh <work folder>}
mkdir -p "$work"
icd=/usr/share/vulkan/icd.d/lvp_icd.json
[[ -f $icd ]] || { echo "fsr1 on the PC: NOT RUN (needs Mesa's lavapipe, $icd)"; exit 0; }
g++ -std=gnu++20 -O1 -g -Wall -Werror "$here/fsr1_host_test.cpp" -o "$work/fsr1_host_test" -lvulkan
VK_ICD_FILENAMES=$icd VK_DRIVER_FILES=$icd "$work/fsr1_host_test" "$ps5/../shaders/glsl/"
