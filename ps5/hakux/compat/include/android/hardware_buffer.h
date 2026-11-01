/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The console has no AHardwareBuffer: hakuX's UMA gives none (hw/xbox/hle/uma/uma.c's own
 * non-Android path), and the HLE GPU takes its copy fallback for guest RAM
 * (hw/xbox/hle/vk/guest_ram.c, "RAM is not an AHB"). The type and the Vulkan structures its import
 * names are declared here so that file compiles unchanged; nothing reaches them.
 */
#ifndef PSHAKUX_ANDROID_HARDWARE_BUFFER_H
#define PSHAKUX_ANDROID_HARDWARE_BUFFER_H

typedef struct AHardwareBuffer AHardwareBuffer;

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#endif
