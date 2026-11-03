# OGBox - the title's build (ps5/tools/build.sh drives it): the top-level project for the
# prospero toolchain.
#
# Configured with the payload SDK's toolchain (toolchain/prospero.cmake), as PS5_Vulkan builds the
# CTS for the console. It compiles, into objects:
#   - the PS5 Vulkan Template foundation: the base class, its third-party code and the PS5 layer
#     (src/main.cpp, launcher, platform.c), with the one program listed in src/samples.cpp,
#     OGBox's front end (src/ogbox_main.cpp), in a namespace of its own;
#   - the console platform code from Crux2 and Kelvin (src/platform: the boot log, the crash
#     report, the clock, the thread priorities, libc's missing names, the virtual file layer);
#   - the Core seam (src/core: the front end's one door into the emulator);
#   - hakuX's native core, HLE GPU and APU, with the console's replacements (cmake/hakux.cmake);
# then tools/link-title.sh links them with RADV and packages the title folder.
#
# Made from PS5 Vulkan Template's ps5/CMakeLists.txt (Copyright (C) 2026 Mihawk, MIT) and Kelvin's
# (Press5elect). The changes for OGBox: Copyright (C) 2026 Press5elect, GPL-3.0-or-later, with the
# attribution term of NOTICE (GPL-3.0 section 7(b)): keep "OGBox by Press5elect".
# SPDX-License-Identifier: MIT AND GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(ogbox C CXX)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The title's name, from its identity: klog's prefix and the boot log's heading
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/param.json param_json)
string(JSON PS5_TITLE_NAME GET "${param_json}" localizedParameters en-US titleName)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS sce_sys/param.json)
set(PS5_VULKAN_DIR "${ROOT}/../PS5_Vulkan" CACHE PATH "the PS5_Vulkan checkout (RADV archive, link recipe, packaging)")
set(GLSLANG_DIR "${ROOT}/.deps/glslang-16.6.0/ps5" CACHE PATH "glslang's console build (tools/build-glslang.sh)")
set(PSHAKUX_PORTS "${ROOT}/.deps/ports" CACHE PATH "the prebuilt and cross-built libraries (tools/build-deps.sh)")
# PS5_PAYLOAD_SDK: the SDK the toolchain file came from (tools/setup-sdk.sh's pin)
if(PS5_HOST_REFERENCE)
	message(FATAL_ERROR "OGBox has no host reference build: the core is the console's (its CPU runs guest code natively)")
endif()

# The program: one SAMPLE(id, ...) line in src/samples.cpp. OGBox's is src/ogbox_main.cpp
# (src/<id>_main.cpp before examples/<id>/<id>.cpp).
file(READ src/samples.cpp samples_cpp)
string(REGEX MATCHALL "\n[ \t]*SAMPLE\\([a-z0-9_]+," sample_entries "${samples_cpp}")
set(PS5_SAMPLES)
foreach(entry IN LISTS sample_entries)
	string(REGEX REPLACE ".*SAMPLE\\(([a-z0-9_]+),$" "\\1" id "${entry}")
	list(APPEND PS5_SAMPLES ${id})
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/samples.cpp)

set(sample_sources)
foreach(id IN LISTS PS5_SAMPLES)
	set(ID ${id})
	set(SOURCE ${ROOT}/examples/${id}/${id}.cpp)
	if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${id}_main.cpp)
		set(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/${id}_main.cpp)
	endif()
	configure_file(src/wrap.cpp.in ${CMAKE_BINARY_DIR}/wrap/${id}.cpp @ONLY)
	list(APPEND sample_sources ${CMAKE_BINARY_DIR}/wrap/${id}.cpp)
endforeach()
file(WRITE ${CMAKE_BINARY_DIR}/samples.txt "${PS5_SAMPLES}")

file(GLOB base_sources ${ROOT}/base/*.cpp)
list(FILTER base_sources EXCLUDE REGEX "VulkanAndroid|VulkanOHOS")
set(third_party_sources
	${ROOT}/external/imgui/imgui.cpp
	${ROOT}/external/imgui/imgui_demo.cpp
	${ROOT}/external/imgui/imgui_draw.cpp
	${ROOT}/external/imgui/imgui_widgets.cpp
	# libktx: the base class's texture loader; its Vulkan calls (vkloader.c) go through volk
	${ROOT}/external/ktx/lib/texture.c
	${ROOT}/external/ktx/lib/hashlist.c
	${ROOT}/external/ktx/lib/checkheader.c
	${ROOT}/external/ktx/lib/swap.c
	${ROOT}/external/ktx/lib/memstream.c
	${ROOT}/external/ktx/lib/filestream.c
	${ROOT}/external/ktx/lib/vkloader.c
	# volk: the one copy for the whole title (hakuX's HLE GPU uses it too)
	third_party/volk/volk.c)
set_source_files_properties(${third_party_sources} PROPERTIES COMPILE_OPTIONS "-w")

set(app_root /app0)
set(common_options -O2 -g1 -ffunction-sections -fdata-sections -fno-omit-frame-pointer)

# ---- hakuX (cmake/hakux.cmake)
include(cmake/hakux.cmake)

# ---- the build's identity, for the boot log's first lines (tools/build-identity.sh)
add_custom_target(pshakux_identity
	COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/build-identity.sh
		${CMAKE_BINARY_DIR}/generated/ps5_build_identity.h ${CMAKE_BINARY_DIR}/build-identity.txt
		${PS5_VULKAN_DIR} ${HAKUX_SOURCE_TREE} ${PSHAKUX_CPU}
	BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/ps5_build_identity.h ${CMAKE_BINARY_DIR}/build-identity.txt
	COMMENT "OGBox: build identity"
	VERBATIM)

# ---- the console platform code (src/platform): from Crux2 (Press5elect) by way of Kelvin, and
# Crux2's libc additions and virtual file layer
add_library(pshakux_platform OBJECT
	src/platform/ps5_bootlog.cpp
	src/platform/ps5_crash.cpp
	src/platform/ps5_build_stamp.cpp
	src/platform/ps5_clock.c
	src/platform/ps5_gpuwait.c
	src/platform/ps5_log.c
	src/platform/ps5_modules.cpp
	src/platform/ps5_priority.cpp
	src/platform/ps5_relaunch.c
	src/platform/ps5_sandbox.cpp
	src/platform/ps5_takeover.cpp
	src/platform/ps5_timing.cpp
	src/platform/ps5_libc.c
	src/platform/vfs/ps5_vfs.cpp
	src/platform/vfs/ps5_vfs_ftp.cpp
	src/platform/vfs/ps5_vfs_nfs.cpp
	src/platform/vfs/ps5_vfs_smb.cpp)
add_dependencies(pshakux_platform pshakux_identity)
set_target_properties(pshakux_platform PROPERTIES C_STANDARD 11 CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(pshakux_platform PRIVATE src/platform src ${CMAKE_BINARY_DIR}/generated ${PSHAKUX_PORTS}/include)
target_compile_options(pshakux_platform PRIVATE ${common_options} -fno-stack-protector)

# ---- the Core seam (src/core): the front end's one door into hakuX
add_library(pshakux_core OBJECT src/core/hakux_core.c src/core/ogbox_mods.c src/core/ogbox_patches.c)
hakux_target(pshakux_core)
target_include_directories(pshakux_core PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/hakux ${HAKUX_DIR}/native/compat/glib)

# ---- the foundation and the program
add_library(samples OBJECT
	${base_sources}
	${third_party_sources}
	${sample_sources}
	src/main.cpp
	src/launcher.cpp
	src/samples.cpp
	src/example_ps5.cpp
	src/ogbox_settings.cpp
	src/ogbox_library.cpp
	src/ogbox_storage.cpp
	src/ogbox_saves.cpp
	src/ogbox_theme.cpp
	src/platform.c)
target_compile_definitions(samples PRIVATE
	VK_EXAMPLE_PS5
	"PS5_TITLE_NAME=\"${PS5_TITLE_NAME}\""
	_DIRECT2DISPLAY
	VK_NO_PROTOTYPES
	NDEBUG
	VK_EXAMPLE_ASSETS_DIR="${app_root}/assets/"
	VK_EXAMPLE_SHADERS_DIR="${app_root}/shaders/")
target_include_directories(samples PRIVATE
	${ROOT}/external
	${ROOT}/external/glm
	${ROOT}/external/imgui
	${ROOT}/external/tinygltf
	${ROOT}/external/ktx/include
	${ROOT}/external/ktx/other_include
	${ROOT}/base
	third_party/volk
	src)
set_target_properties(samples PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON C_STANDARD 11)
# Every unit sees volk's declarations first: the Vulkan commands are volk's pointers, filled from
# the RADV linked into the title. -fno-omit-frame-pointer: the crash backtraces are frame walks.
target_compile_options(samples PRIVATE ${common_options}
	"SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk/volk.h")

# ---- link, sign and package: the title folder in dist/<TITLE_ID>/
set(link_objects
	$<TARGET_OBJECTS:samples> $<TARGET_OBJECTS:pshakux_platform> $<TARGET_OBJECTS:pshakux_core>
	${PSHAKUX_HAKUX_OBJECTS})
set(link_archives
	${GLSLANG_DIR}/lib/libglslang.a ${GLSLANG_DIR}/lib/libMachineIndependent.a
	${GLSLANG_DIR}/lib/libGenericCodeGen.a ${GLSLANG_DIR}/lib/libOSDependent.a
	${GLSLANG_DIR}/lib/libSPIRV.a ${GLSLANG_DIR}/lib/libglslang-default-resource-limits.a
	${PSHAKUX_PORTS}/lib/libsmb2.a ${PSHAKUX_PORTS}/lib/libnfs.a)
if(NOT EXISTS ${CMAKE_BINARY_DIR}/radv-archive.txt)
	file(WRITE ${CMAKE_BINARY_DIR}/radv-archive.txt "")
endif()
add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/eboot.elf
	COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/link-title.sh ${CMAKE_BINARY_DIR} ${PS5_VULKAN_DIR} ${PS5_PAYLOAD_SDK}
		${link_objects} -- ${link_archives}
	# radv-archive.txt: the RADV archive tools/build.sh chose, so a change of archive relinks
	DEPENDS samples pshakux_platform pshakux_core ${PSHAKUX_HAKUX_TARGETS} ${link_objects} ${link_archives}
		${CMAKE_CURRENT_SOURCE_DIR}/tools/link-title.sh ${CMAKE_BINARY_DIR}/radv-archive.txt
	COMMAND_EXPAND_LISTS
	VERBATIM)
add_custom_target(title ALL DEPENDS ${CMAKE_BINARY_DIR}/eboot.elf)
