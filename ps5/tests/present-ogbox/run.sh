#!/usr/bin/env bash
# OGBox 1.0.4 - the two devices' frame handoff on the PC: ps5/hakux/hle/present_host.c compiled against
# stand-ins for the HLE back end (stubs/) and run with a fake Vulkan device and display, driven by the
# title's side (src/ogbox_take.h) as ogbox_main.cpp's fetchPicture does (present_host_test.cpp says
# what is checked). Needs gcc, g++ and the Vulkan headers (libvulkan-dev); no driver.
#   ps5/tests/present-ogbox/run.sh <work folder>
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ps5=$(cd "$here/../.." && pwd)
work=${1:?usage: run.sh <work folder>}
mkdir -p "$work"
[[ -f /usr/include/vulkan/vulkan.h ]] || { echo "present on the PC: NOT RUN (needs the Vulkan headers)"; exit 0; }
cc=${CC:-gcc}
cxx=${CXX:-g++}
san=-fsanitize=address,undefined
"$cc" -std=gnu11 -O1 -g -Wall -Wextra -Werror $san -I"$here/stubs" -I"$ps5/hakux/hle" -I"$ps5/src" \
	-c "$ps5/hakux/hle/present_host.c" -o "$work/present_host.o"
"$cxx" -std=gnu++20 -O1 -g -Wall -Wextra -Werror $san -I"$here/stubs" -I"$ps5/hakux/hle" -I"$ps5/src" \
	"$here/present_host_test.cpp" "$work/present_host.o" -o "$work/present_host_test" -lpthread
"$work/present_host_test"
