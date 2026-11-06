# OGBox - hakuX for PS5
# Copyright (C) 2026 Press5elect
# SPDX-License-Identifier: GPL-3.0-or-later
# Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#
# hakuX's native core for the console, included by ps5/CMakeLists.txt: what hakuX's own
# native/CMakeLists.txt and native/cmake/hle.cmake build, without FEX (cmake/fex.cmake), without
# the Android app (native/app, cmake/app.cmake: SDL3, adrenotools, JNI) and without xboxrun.
#
#   HAKUX_DIR     the source compiled: build/hakux-src (ps5/tools/prepare-hakux.sh: HAKUX_SRC, by
#                 default vendor/hakuX at the pin, with ps5/patches/hakux laid over it)
#   PSHAKUX_DEPS  .deps/src: libslirp 4.7.0, nv2a_vsh_cpu, xxHash (ps5/tools/build-deps.sh)
#   GLSLANG_DIR   glslang's console build (ps5/tools/build-glslang.sh)
#   NC_CPU        the guest CPU: "native" compiles native/cpu/cpu_native.c (the native backend,
#                 compat-mode execution on the console's own CPU) when the source has it; the
#                 stub (ps5/hakux/cpu/cpu_stub.c, weak) is always linked, so whatever the
#                 backend does not define yet still links, and a run says the CPU is missing.
#
# What hakuX does for Android, OGBox does for the console in ps5/hakux/, not in hakuX:
#   - compat/include: the Android and Linux headers hakuX includes (system properties, prctl,
#     eventfd, the Android log, AHardwareBuffer's types), mapped to the console's;
#     compat/hakux_ps5.h, included first in every hakuX file: gettid, the Linux system calls;
#   - apu/out_ps5.c for native/dev/apu/out.c (AAudio): sceAudioOut;
#   - hle/present_host.c for hw/xbox/hle/vk/swapchain.c (an Android window's swapchain): the
#     front frame handed to the title, which shows it on its own display (host-owned image);
#   - the UMA's AHardwareBuffer path is not compiled (hakuX's own fallback: a copy of guest RAM).

set(HAKUX_DIR "${CMAKE_BINARY_DIR}/../hakux-src" CACHE PATH "the hakuX source compiled (ps5/tools/prepare-hakux.sh)")
set(HAKUX_SOURCE_TREE "${ROOT}/vendor/hakuX" CACHE PATH "the hakuX tree HAKUX_DIR was prepared from (HAKUX_SRC)")
set(PSHAKUX_DEPS "${ROOT}/.deps/src" CACHE PATH "third-party sources (ps5/tools/build-deps.sh)")
set(NC_CPU "native" CACHE STRING "the guest CPU backend: native or stub")
set(PSHAKUX_HX ${CMAKE_CURRENT_SOURCE_DIR}/hakux)
get_filename_component(HAKUX_DIR "${HAKUX_DIR}" ABSOLUTE)
foreach(required native/CMakeLists.txt native/core/core.c hw/xbox/hle/hle.c)
	if(NOT EXISTS ${HAKUX_DIR}/${required})
		message(FATAL_ERROR "no hakuX source in ${HAKUX_DIR} (${required}): run ps5/tools/prepare-hakux.sh")
	endif()
endforeach()
foreach(required libslirp/src/slirp.c nv2a_vsh_cpu/src/nv2a_vsh_cpu.c xxhash/xxhash.h libchdr/src/libchdr_chd.c)
	if(NOT EXISTS ${PSHAKUX_DEPS}/${required})
		message(FATAL_ERROR "no ${required} in ${PSHAKUX_DEPS}: run ps5/tools/build-deps.sh")
	endif()
endforeach()

# Every hakuX file: hakuX's own include root, the PS5 shims ahead of the system's headers, volk
# (the template's: one volk for the whole title, initialised by the template's main from RADV),
# the Vulkan headers, and the PS5 prelude.
set(hakux_common_includes
	${PSHAKUX_HX}/compat/include
	${HAKUX_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${ROOT}/.deps/ports/include
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk
	${ROOT}/external)
set(hakux_common_options
	-O2 -g1 -ffunction-sections -fdata-sections -fno-omit-frame-pointer -fno-stack-protector
	-Wno-unused-parameter -Wno-unused-function -Wno-sign-compare
	# the console's math.h classifies with _Generic over qualified types: a warning at every isnan()
	-Wno-unreachable-code-generic-assoc
	"$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-include ${PSHAKUX_HX}/compat/hakux_ps5.h>")
set(hakux_common_definitions VK_NO_PROTOTYPES CONFIG_VULKAN=1 HAKUX_HLE_GPU=1 HAKUX_NATIVE_CORE=1
	_DIRECT2DISPLAY PSHAKUX=1)

function(hakux_target name)
	set_target_properties(${name} PROPERTIES C_STANDARD 11 CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
	target_compile_definitions(${name} PRIVATE ${hakux_cpu_definitions})
	target_compile_options(${name} PRIVATE ${hakux_cpu_options})
	target_include_directories(${name} BEFORE PRIVATE ${hakux_common_includes})
	target_compile_options(${name} PRIVATE ${hakux_common_options})
	target_compile_definitions(${name} PRIVATE ${hakux_common_definitions})
endfunction()

# ---- the GLib subset (hakuX's native/compat/glib), for the HLE GPU and the APU
add_library(hakux_glib OBJECT ${HAKUX_DIR}/native/compat/glib/glib.c)
hakux_target(hakux_glib)
target_include_directories(hakux_glib PRIVATE ${HAKUX_DIR}/native/compat/glib)

# ---- libslirp 4.7.0 (unmodified) on its own GLib subset (hakuX's native/net/slirp)
set(NC_SLIRP ${PSHAKUX_DEPS}/libslirp/src)
set(SLIRP_MAJOR_VERSION 4)
set(SLIRP_MINOR_VERSION 7)
set(SLIRP_MICRO_VERSION 0)
set(SLIRP_VERSION_STRING "\"4.7.0\"")
configure_file(${NC_SLIRP}/libslirp-version.h.in ${CMAKE_BINARY_DIR}/slirp/libslirp-version.h @ONLY)
configure_file(${NC_SLIRP}/libslirp.h ${CMAKE_BINARY_DIR}/slirp/libslirp.h COPYONLY)
add_library(hakux_slirp OBJECT
	${HAKUX_DIR}/native/net/slirp/glib.c
	${NC_SLIRP}/arp_table.c ${NC_SLIRP}/bootp.c ${NC_SLIRP}/cksum.c ${NC_SLIRP}/dhcpv6.c
	${NC_SLIRP}/dnssearch.c ${NC_SLIRP}/if.c ${NC_SLIRP}/ip6_icmp.c ${NC_SLIRP}/ip6_input.c
	${NC_SLIRP}/ip6_output.c ${NC_SLIRP}/ip_icmp.c ${NC_SLIRP}/ip_input.c ${NC_SLIRP}/ip_output.c
	${NC_SLIRP}/mbuf.c ${NC_SLIRP}/misc.c ${NC_SLIRP}/ncsi.c ${NC_SLIRP}/ndp_table.c
	${NC_SLIRP}/sbuf.c ${NC_SLIRP}/slirp.c ${NC_SLIRP}/socket.c ${NC_SLIRP}/state.c
	${NC_SLIRP}/stream.c ${NC_SLIRP}/tcp_input.c ${NC_SLIRP}/tcp_output.c ${NC_SLIRP}/tcp_subr.c
	${NC_SLIRP}/tcp_timer.c ${NC_SLIRP}/tftp.c ${NC_SLIRP}/udp.c ${NC_SLIRP}/udp6.c
	${NC_SLIRP}/util.c ${NC_SLIRP}/version.c ${NC_SLIRP}/vmstate.c)
hakux_target(hakux_slirp)
target_include_directories(hakux_slirp BEFORE PRIVATE ${HAKUX_DIR}/native/net/slirp ${CMAKE_BINARY_DIR}/slirp)
target_compile_options(hakux_slirp PRIVATE -w)

# ---- the native core (hakuX's hakux_native), without native/cpu/cpu.cpp (FEX)
set(hakux_core_sources
	core/log.c core/jmp.c core/vm.c core/xdvdfs.c core/pack.c core/chd.c core/xbe_load.c core/mods.c core/options.c
	core/cheats.c core/trainer.c core/memscan.c core/core.c core/mmio.c core/irq.c
	dev/nv2a.c dev/ports.c dev/usb.c dev/usb_hub.c dev/xid.c dev/ohci.c dev/aci.c dev/nvnet.c
	net/net.c net/lan.c net/kai.c net/nat.c
	kernel/kernel.c kernel/pool.c kernel/ob.c kernel/ps.c kernel/ke.c kernel/mm.c kernel/paging.c
	kernel/ex.c kernel/rtl.c kernel/seh.c kernel/fixups.c kernel/x86mmio.c kernel/mmiosite.c kernel/timer.c
	kernel/intr.c kernel/apc.c kernel/io.c kernel/xc.c kernel/xc_crypto.c kernel/kstate.c
	state/state.c state/vm_state.c state/save.c state/cache.c)
list(TRANSFORM hakux_core_sources PREPEND ${HAKUX_DIR}/native/)
list(APPEND hakux_core_sources ${HAKUX_DIR}/hw/xbox/hle/core/xbe.c)
# The native CPU backend, when the source has it and it is asked for (hakuX-native, branch
# ps5-native: native/cpu/cpu_native.c and the files its native/CMakeLists.txt lists in
# NC_CPU_SOURCES for NC_CPU=native, read from there so the list follows the backend). Its
# native/cpu/vmhost.c has the console's half itself (__PROSPERO__: the platform layer's shared
# memory); guest VA 0 is host address 0, and the start of
# the contiguous window is the console's (a system module sits at 0x80000000).
set(PSHAKUX_CPU "stub")
# (The console's window start, for a tree that reads it whatever the backend: hakuX-native's vm.c.)
# The start of the 0x80000000 window the guest cannot have when the title keeps it
# (NC_WINDOW_HOLE_END): the title's libc.prx sits at 0x80000000-0x80120000, so then the guest's
# kernel area is moved past it, and titles built with the XDK, which read the kernel's PE header at
# 0x80010000, are killed there (an execute-only page: SYSTEM_XO_VIOLATION, which no handler can
# catch). 0.1.4: this is only the fallback. At start-up the title takes 0x80000000-0x80200000 back
# from libc.prx (src/platform/ps5_takeover.cpp) and, when that is done, sets no hole at run time
# (hakuX-native's nc_vmhost_set_window_hole(0)): the kernel at 0x80010000 as on the Xbox. When it
# is not done, this layout is kept. A build with 0 here still falls back to 0x80120000 then.
set(PSHAKUX_WINDOW_HOLE_END "0x80120000u" CACHE STRING "NC_WINDOW_HOLE_END: end of the 0x80000000 window's start the guest cannot have (0: none)")
set(hakux_cpu_definitions NC_WINDOW_HOLE_END=${PSHAKUX_WINDOW_HOLE_END})
set(hakux_cpu_options)
set(PSHAKUX_CPU_NOTE "")
if(NC_CPU STREQUAL "native" AND EXISTS ${HAKUX_DIR}/native/cpu/cpu_native.c)
	# The backend is the console's when its signal context knows FreeBSD's mcontext (the PS5's);
	# until then it is Linux's only, and the stub links instead (the eboot still links).
	file(STRINGS ${HAKUX_DIR}/native/cpu/cpu_native.c cpu_native_ps5 REGEX "defined\\(__(FreeBSD|PROSPERO)__\\)")
	if(NOT cpu_native_ps5)
		set(PSHAKUX_CPU_NOTE " (native/cpu/cpu_native.c is there, but has no PS5 (FreeBSD) signal context yet: the stub links)")
	endif()
endif()
if(NC_CPU STREQUAL "native" AND EXISTS ${HAKUX_DIR}/native/cpu/cpu_native.c)
	enable_language(ASM)
	file(READ ${HAKUX_DIR}/native/CMakeLists.txt hakux_native_cmake)
	string(REGEX MATCH "elseif\\(NC_CPU STREQUAL \"native\"\\)[^\n]*\n[^\n]*\n[ \t]*set\\(NC_CPU_SOURCES ([^)]*)\\)" found "${hakux_native_cmake}")
	if(found)
		string(REGEX REPLACE "[ \t\n]+" ";" cpu_sources "${CMAKE_MATCH_1}")
	else()
		set(cpu_sources cpu/cpu_native.c)
		file(GLOB cpu_extra RELATIVE ${HAKUX_DIR}/native ${HAKUX_DIR}/native/cpu/*.c ${HAKUX_DIR}/native/cpu/*.S)
		list(APPEND cpu_sources ${cpu_extra})
		list(REMOVE_ITEM cpu_sources cpu/cpu.cpp)
	endif()
	if(PSHAKUX_CPU_NOTE)
		# Not the console's yet: everything but the CPU itself, which the stubs stand in for.
		list(REMOVE_ITEM cpu_sources cpu/cpu_native.c cpu/cpu_native_asm.S)
		list(APPEND hakux_core_sources ${PSHAKUX_HX}/cpu/cpu_native_stub.c)
	else()
		set(PSHAKUX_CPU "native")
	endif()
	foreach(source IN LISTS cpu_sources)
		if(source)
			list(APPEND hakux_core_sources ${HAKUX_DIR}/native/${source})
			if(source MATCHES "\\.S$")
				set_source_files_properties(${HAKUX_DIR}/native/${source} PROPERTIES LANGUAGE ASM)
			endif()
		endif()
	endforeach()
	set(hakux_cpu_definitions NC_CPU_NATIVE=1 NC_WINDOW_HOLE_END=${PSHAKUX_WINDOW_HOLE_END})
	set(hakux_cpu_options -fno-delete-null-pointer-checks)
	message(STATUS "OGBox: native CPU backend's sources: ${cpu_sources} (cpu/vmhost.c: its __PROSPERO__ half, the platform layer's shm)")
endif()
list(APPEND hakux_core_sources ${PSHAKUX_HX}/cpu/cpu_stub.c)
message(STATUS "OGBox: hakuX from ${HAKUX_DIR}; CPU backend ${PSHAKUX_CPU}${PSHAKUX_CPU_NOTE}")
# ---- libchdr (rtissera/libchdr, BSD-3-Clause): CHD disc images (native/core/chd.c), its decoders
# over the RADV archive's zlib, its own LZMA (the C decoder: the optimised one is arm64's), zstd
set(NC_CHDR ${PSHAKUX_DEPS}/libchdr)
file(GLOB NC_CHDR_LZMA LIST_DIRECTORIES true "${NC_CHDR}/deps/lzma-*")
file(GLOB NC_CHDR_ZSTD LIST_DIRECTORIES true "${NC_CHDR}/deps/zstd-*")
add_library(hakux_chdr OBJECT
	${NC_CHDR}/src/libchdr_bitstream.c ${NC_CHDR}/src/libchdr_cdrom.c ${NC_CHDR}/src/libchdr_chd.c
	${NC_CHDR}/src/libchdr_codec_avhuff.c ${NC_CHDR}/src/libchdr_codec_cdfl.c ${NC_CHDR}/src/libchdr_codec_cdlz.c
	${NC_CHDR}/src/libchdr_codec_cdzl.c ${NC_CHDR}/src/libchdr_codec_cdzs.c ${NC_CHDR}/src/libchdr_codec_flac.c
	${NC_CHDR}/src/libchdr_codec_huff.c ${NC_CHDR}/src/libchdr_codec_lzma.c ${NC_CHDR}/src/libchdr_codec_zlib.c
	${NC_CHDR}/src/libchdr_codec_zstd.c ${NC_CHDR}/src/libchdr_flac.c ${NC_CHDR}/src/libchdr_huffman.c
	${NC_CHDR_LZMA}/src/LzmaDec.c ${NC_CHDR_ZSTD}/zstddeclib.c)
set_target_properties(hakux_chdr PROPERTIES C_STANDARD 11)
target_include_directories(hakux_chdr PRIVATE ${NC_CHDR}/include ${NC_CHDR_LZMA}/include ${ROOT}/.deps/ports/include)
target_compile_definitions(hakux_chdr PRIVATE CHDR_SYSTEM_ZLIB)
target_compile_options(hakux_chdr PRIVATE -w)

add_library(hakux_core OBJECT ${hakux_core_sources})
hakux_target(hakux_core)
target_include_directories(hakux_core PRIVATE ${NC_CHDR}/include)
target_compile_definitions(hakux_core PRIVATE NC_HAVE_CHD)
target_include_directories(hakux_core PRIVATE ${PSHAKUX_DEPS}/xxhash ${CMAKE_BINARY_DIR}/slirp
	${HAKUX_DIR}/native/compat/glib)
target_compile_definitions(hakux_core PRIVATE HLE_HOST_TEST)
target_compile_options(hakux_core PRIVATE -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-sign-compare)

# ---- the HLE GPU (hw/xbox/hle) on its host interface (native/hle), without vk/swapchain.c
set(NC_HLE ${HAKUX_DIR}/hw/xbox/hle)
set(NC_VSH ${PSHAKUX_DEPS}/nv2a_vsh_cpu/src)
add_library(hakux_hle OBJECT
	${NC_HLE}/hle.c ${NC_HLE}/hitch.c
	${NC_HLE}/core/title.c ${NC_HLE}/core/probe.c ${NC_HLE}/core/xbe_guest.c
	${NC_HLE}/front/arena.c ${NC_HLE}/front/fe.c ${NC_HLE}/front/ring.c
	${NC_HLE}/mem/barrier.c ${NC_HLE}/mem/guestmem.c ${NC_HLE}/mem/pageuse.c
	${NC_HLE}/pb/kelvin.c ${NC_HLE}/pb/pbdraw.c ${NC_HLE}/pb/pbmode.c ${NC_HLE}/pb/pbres.c
	${NC_HLE}/pb/pbstate.c ${NC_HLE}/pb/pusher.c ${NC_HLE}/pb/objslot.c ${NC_HLE}/pb/wclip.c
	${NC_HLE}/shader/ff_gen.c ${NC_HLE}/shader/ffhw.c ${NC_HLE}/shader/glsl_compile.c
	${NC_HLE}/shader/psh.c ${NC_HLE}/shader/vsh.c
	${NC_HLE}/tex/xdk_res.c ${NC_HLE}/tex/xg_swizzle.c
	${NC_HLE}/uma/uma.c
	${NC_HLE}/util/thread.c
	${NC_HLE}/vk/back.c ${NC_HLE}/vk/display.c ${NC_HLE}/vk/batch.c ${NC_HLE}/vk/compat.c
	${NC_HLE}/vk/draw.c ${NC_HLE}/vk/quirks.c ${NC_HLE}/vk/gamma.c ${NC_HLE}/vk/guest_ram.c
	${NC_HLE}/vk/pcache.c ${NC_HLE}/vk/pipeasync.c ${NC_HLE}/vk/surf.c ${NC_HLE}/vk/timeline.c
	${NC_HLE}/vk/vkdev.c ${NC_HLE}/vk/zread.c
	${PSHAKUX_HX}/hle/present_host.c
	${NC_VSH}/nv2a_vsh_disassembler.c ${NC_VSH}/nv2a_vsh_cpu.c ${NC_VSH}/nv2a_vsh_emulator.c
	${NC_VSH}/nv2a_vsh_emulator_execution_state.c
	${HAKUX_DIR}/native/hle/glue.c ${HAKUX_DIR}/native/hle/host_native.c ${HAKUX_DIR}/native/hle/dirty.c)
hakux_target(hakux_hle)
# hakuX includes the interpreter as "subprojects/nv2a_vsh_cpu/src/...": its NC_DEPS/root.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/hakux-deps-root/subprojects)
file(CREATE_LINK ${PSHAKUX_DEPS}/nv2a_vsh_cpu ${CMAKE_BINARY_DIR}/hakux-deps-root/subprojects/nv2a_vsh_cpu SYMBOLIC)
target_include_directories(hakux_hle PRIVATE ${CMAKE_BINARY_DIR}/hakux-deps-root ${GLSLANG_DIR}/include
	${PSHAKUX_DEPS}/xxhash ${HAKUX_DIR}/native/compat/glib ${NC_VSH})
target_compile_definitions(hakux_hle PRIVATE HLE_HOST_TEST)

# ---- the MCPX APU (xemu's, on the core's services), with the console's sound output
set(NC_APU ${HAKUX_DIR}/native/dev/apu)
add_library(hakux_apu OBJECT
	${NC_APU}/apu.c ${NC_APU}/state.c
	${NC_APU}/dsp/debug.c ${NC_APU}/dsp/dsp.c ${NC_APU}/dsp/dsp_cpu.c ${NC_APU}/dsp/dsp_dma.c ${NC_APU}/dsp/gp_ep.c
	${NC_APU}/dsp/gp_probe.c
	${NC_APU}/vp/resample.c ${NC_APU}/vp/resample_sinc.c ${NC_APU}/vp/simd.c ${NC_APU}/vp/vp.c
	${PSHAKUX_HX}/apu/out_ps5.c)
hakux_target(hakux_apu)
# OGBox 0.0.4: the DSP56300 JIT (Matt Borgerson's dsp56300 crate, MIT, on Cranelift: its C FFI as a
# static library, built for the console by tools/build-deps.sh with ps5/patches/dsp56300). Full
# DSP runs the GP's and EP's programs translated to x86-64 instead of interpreted; without the
# library (no Rust toolchain) the interpreter runs, as before.
set(PSHAKUX_DSP56300_LIB "${PSHAKUX_PORTS}/lib/libdsp56300_emu_ffi.a")
if(EXISTS "${PSHAKUX_DSP56300_LIB}" AND EXISTS "${PSHAKUX_PORTS}/include/dsp56300.h")
	target_sources(hakux_apu PRIVATE ${NC_APU}/dsp/jit.c ${PSHAKUX_HX}/apu/dsp_jit_mem_ps5.c)
	target_compile_definitions(hakux_apu PRIVATE NC_HAVE_DSP_JIT=1)
	target_include_directories(hakux_apu PRIVATE ${PSHAKUX_PORTS}/include)
	set(PSHAKUX_DSP_ARCHIVES "${PSHAKUX_DSP56300_LIB}")
	message(STATUS "OGBox: the DSP56300 JIT (${PSHAKUX_DSP56300_LIB})")
else()
	target_compile_definitions(hakux_apu PRIVATE NC_HAVE_DSP_JIT=0)
	set(PSHAKUX_DSP_ARCHIVES "")
	message(STATUS "OGBox: no DSP56300 JIT library (tools/build-deps.sh, needs cargo): the DSP interpreter")
endif()
target_include_directories(hakux_apu PRIVATE ${HAKUX_DIR}/native/compat/glib)
target_compile_options(hakux_apu PRIVATE -Wno-unused-variable)

# ---- the PS5 side of the core (ps5/hakux/compat: the properties, the Linux calls)
add_library(hakux_ps5 OBJECT ${PSHAKUX_HX}/compat/hakux_ps5.c ${PSHAKUX_HX}/compat/rust_std_stubs.c)
hakux_target(hakux_ps5)
target_include_directories(hakux_ps5 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(PSHAKUX_HAKUX_OBJECTS
	$<TARGET_OBJECTS:hakux_glib> $<TARGET_OBJECTS:hakux_slirp> $<TARGET_OBJECTS:hakux_chdr> $<TARGET_OBJECTS:hakux_core>
	$<TARGET_OBJECTS:hakux_hle> $<TARGET_OBJECTS:hakux_apu> $<TARGET_OBJECTS:hakux_ps5>)
set(PSHAKUX_HAKUX_TARGETS hakux_glib hakux_slirp hakux_chdr hakux_core hakux_hle hakux_apu hakux_ps5)
