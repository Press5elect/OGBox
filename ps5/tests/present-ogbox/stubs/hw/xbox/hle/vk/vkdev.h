/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The test's stand-in for what ps5/hakux/hle/present_host.c takes of hakuX's HLE back end (the
 * names and shapes as in hakux-native's hw/xbox/hle/vk/vkdev.h, back_int.h and display.h, only what
 * the presenter uses), so the presenter itself is compiled and run against the fake Vulkan device and
 * display of present_host_test.cpp. Every one of base.h, hle.h, back.h, back_int.h, display.h and
 * swapchain.h includes this file.
 */
#ifndef PRESENT_TEST_STUB_H
#define PRESENT_TEST_STUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HleVkTable {
    PFN_vkUnmapMemory vkUnmapMemory;
    PFN_vkDestroyBuffer vkDestroyBuffer;
    PFN_vkFreeMemory vkFreeMemory;
    PFN_vkCreateCommandPool vkCreateCommandPool;
    PFN_vkDestroyCommandPool vkDestroyCommandPool;
    PFN_vkCreateFence vkCreateFence;
    PFN_vkDestroyFence vkDestroyFence;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
    PFN_vkCreateBuffer vkCreateBuffer;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
    PFN_vkAllocateMemory vkAllocateMemory;
    PFN_vkBindBufferMemory vkBindBufferMemory;
    PFN_vkMapMemory vkMapMemory;
    PFN_vkGetMemoryHostPointerPropertiesEXT vkGetMemoryHostPointerPropertiesEXT;
    PFN_vkGetFenceStatus vkGetFenceStatus;
    PFN_vkWaitForFences vkWaitForFences;
    PFN_vkResetFences vkResetFences;
    PFN_vkResetCommandBuffer vkResetCommandBuffer;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer vkEndCommandBuffer;
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
    PFN_vkQueueSubmit vkQueueSubmit;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
} HleVkTable;

typedef struct HleVk {
    HleVkTable t;
    VkDevice dev;
    VkQueue queue;
    uint32_t qfam;
    struct {
        bool host_ptr;
        VkDeviceSize host_ptr_align;
    } caps;
} HleVk;
extern HleVk g_hvk;

bool hle_vk_ready(void);
bool hle_vk_lost(void);
bool hle_vk_check(VkResult r, const char *what);
void hle_vk_queue_lock(void);
void hle_vk_queue_unlock(void);
int hle_vk_memtype(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid);
void hle_log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void hle_img_barrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect, VkImageLayout from,
                     VkImageLayout to);

typedef struct HleDispFrame {
    int slot;
    uint32_t gen, swap_id, w, h;
    uint64_t value;
    VkImage img;
    VkImageView view;
} HleDispFrame;
typedef struct HleDispStats {
    uint64_t presented, dropped;
} HleDispStats;
bool hle_disp_front(uint32_t *gen, uint32_t *swap_id);
bool hle_disp_acquire(HleDispFrame *f);
void hle_disp_release(int slot);
void hle_disp_use_begin(void);
void hle_disp_use_end(void);
void hle_disp_size(uint32_t *w, uint32_t *h);
HleDispStats hle_disp_stats(void);

/* the one-device half (not run by the test; declared so the file compiles) */
typedef struct HleVkShare {
    VkInstance inst;
    uint32_t api;
    VkPhysicalDevice phys;
    VkDevice dev;
    uint32_t qfam;
    VkQueue queue;
    void (*lock)(void);
    void (*unlock)(void);
    const char *const *exts;
    uint32_t n_exts;
    const VkPhysicalDeviceFeatures2 *features;
} HleVkShare;
typedef struct HleVkNeeds {
    uint32_t qfam, n_exts;
    const char *const *exts;
    void *features;
} HleVkNeeds;
int hle_vk_needs(const HleVkShare *host, HleVkNeeds *n, char *why, unsigned why_size);
void hle_vk_share(const HleVkShare *host);
bool hle_vk_shared(void);
uint64_t hle_tl_submitted(void);
VkSemaphore hle_tl_sem(void);

struct ANativeWindow;
bool hle_present_unchanged(struct ANativeWindow *win, uint64_t key);
bool hle_present_begin(struct ANativeWindow *win, uint32_t *w, uint32_t *h);
bool hle_present_frame(uint32_t *w, uint32_t *h);
void hle_present_end(float sx, float sy, bool fxaa, int upscale, bool average, uint64_t key);

#ifdef __cplusplus
}
#endif

#endif
