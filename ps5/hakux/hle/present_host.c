/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * The host-owned image presenter (present_host.h), in place of hakuX's
 * hw/xbox/hle/vk/swapchain.c: the front frame read back for the title, on the HLE GPU's own
 * queue, the way hw/xbox/hle/vk/display.c's hle_disp_read_front does for captures, but with the
 * buffer, the command buffer and the fence made once and kept (a frame's read is one submit and
 * one wait). The display's gamma ramp is baked into the frames by the render thread (no consumer
 * applies it here: hle_disp_gamma_by_consumer stays off).
 *
 * swapchain.c's window calls (hle_present_begin/frame/end/unchanged) are defined too, refusing:
 * nothing on the console has an Android window to give them.
 */
#include "hw/xbox/hle/base.h"
#include "hw/xbox/hle/hle.h"
#include "hw/xbox/hle/vk/back_int.h"
#include "hw/xbox/hle/vk/display.h"
#include "hw/xbox/hle/vk/swapchain.h"
#include "hw/xbox/hle/vk/vkdev.h"

#include "present_host.h"

#include <pthread.h>
#include <string.h>

#define T (g_hvk.t)

static struct {
    pthread_mutex_t lock;
    bool made, failed;
    VkBuffer buf;
    VkDeviceMemory mem;
    VkDeviceSize size;
    void *map;
    VkCommandPool pool;
    VkCommandBuffer cb;
    VkFence fence;
    uint64_t taken;
} g_ph = {.lock = PTHREAD_MUTEX_INITIALIZER};

bool pshx_present_ready(void)
{
    return hle_vk_ready() && !hle_vk_lost();
}

void pshx_present_size(uint32_t *w, uint32_t *h)
{
    *w = *h = 0;
    if (hle_vk_ready()) {
        hle_disp_size(w, h);
    }
}

void pshx_present_counts(uint64_t *presented, uint64_t *taken)
{
    *presented = hle_vk_ready() ? hle_disp_stats().presented : 0;
    *taken = __atomic_load_n(&g_ph.taken, __ATOMIC_RELAXED);
}

static void drop_buffer(void)
{
    if (g_ph.map) {
        T.vkUnmapMemory(g_hvk.dev, g_ph.mem);
        g_ph.map = NULL;
    }
    if (g_ph.buf) {
        T.vkDestroyBuffer(g_hvk.dev, g_ph.buf, NULL);
        g_ph.buf = VK_NULL_HANDLE;
    }
    if (g_ph.mem) {
        T.vkFreeMemory(g_hvk.dev, g_ph.mem, NULL);
        g_ph.mem = VK_NULL_HANDLE;
    }
    g_ph.size = 0;
}

/* The command pool, buffer and fence, once. */
static bool make(void)
{
    if (g_ph.made || g_ph.failed) {
        return g_ph.made;
    }
    const VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                         .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                         .queueFamilyIndex = g_hvk.qfam};
    const VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (T.vkCreateCommandPool(g_hvk.dev, &pci, NULL, &g_ph.pool) != VK_SUCCESS ||
        T.vkCreateFence(g_hvk.dev, &fci, NULL, &g_ph.fence) != VK_SUCCESS) {
        hle_log("hakuX-hle-back", "present (host image): no command pool or fence");
        g_ph.failed = true;
        return false;
    }
    const VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                            .commandPool = g_ph.pool,
                                            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = 1};
    if (T.vkAllocateCommandBuffers(g_hvk.dev, &ai, &g_ph.cb) != VK_SUCCESS) {
        g_ph.failed = true;
        return false;
    }
    g_ph.made = true;
    hle_log("hakuX-hle-back", "present: host-owned image (the title draws the front frame; read back, RGBA8)");
    return true;
}

/* A host-visible buffer of at least size bytes (cached when there is such memory: the CPU reads it). */
static bool ensure_buffer(VkDeviceSize size)
{
    if (g_ph.size >= size) {
        return true;
    }
    drop_buffer();
    const VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                                    .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (T.vkCreateBuffer(g_hvk.dev, &bci, NULL, &g_ph.buf) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements mr;
    T.vkGetBufferMemoryRequirements(g_hvk.dev, g_ph.buf, &mr);
    const VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    int mt = hle_vk_memtype(mr.memoryTypeBits, hv | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
    if (mt < 0) {
        mt = hle_vk_memtype(mr.memoryTypeBits, hv, 0);
    }
    const VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                      .allocationSize = mr.size, .memoryTypeIndex = (uint32_t)mt};
    if (mt < 0 || T.vkAllocateMemory(g_hvk.dev, &mai, NULL, &g_ph.mem) != VK_SUCCESS ||
        T.vkBindBufferMemory(g_hvk.dev, g_ph.buf, g_ph.mem, 0) != VK_SUCCESS ||
        T.vkMapMemory(g_hvk.dev, g_ph.mem, 0, size, 0, &g_ph.map) != VK_SUCCESS) {
        drop_buffer();
        return false;
    }
    g_ph.size = size;
    return true;
}

bool pshx_present_take(uint64_t *seen, void *dst, uint32_t pitch, uint32_t max_w, uint32_t max_h, uint32_t *w,
                       uint32_t *h)
{
    if (!pshx_present_ready()) {
        return false;
    }
    uint32_t gen = 0, swap_id = 0;
    if (!hle_disp_front(&gen, &swap_id)) {
        return false;
    }
    /* The frame's identity: the slots' generation (a resize) and the swap. */
    const uint64_t id = (uint64_t)gen << 32 | swap_id;
    if (id == *seen) {
        return false;
    }
    pthread_mutex_lock(&g_ph.lock);
    bool ok = false;
    HleDispFrame f;
    if (make() && hle_disp_acquire(&f)) {
        hle_disp_use_begin();           /* the slots' images stay until the copy is done */
        const VkDeviceSize size = (VkDeviceSize)f.w * f.h * 4;
        if (f.w <= max_w && f.h <= max_h && ensure_buffer(size)) {
            const VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            T.vkResetCommandBuffer(g_ph.cb, 0);
            T.vkBeginCommandBuffer(g_ph.cb, &bi);
            hle_img_barrier(g_ph.cb, f.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            const VkBufferImageCopy r = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                         .imageExtent = {f.w, f.h, 1}};
            T.vkCmdCopyImageToBuffer(g_ph.cb, f.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_ph.buf, 1, &r);
            hle_img_barrier(g_ph.cb, f.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            T.vkEndCommandBuffer(g_ph.cb);
            const VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1,
                                     .pCommandBuffers = &g_ph.cb};
            T.vkResetFences(g_hvk.dev, 1, &g_ph.fence);
            hle_vk_queue_lock();
            const VkResult sr = T.vkQueueSubmit(g_hvk.queue, 1, &si, g_ph.fence);
            hle_vk_queue_unlock();
            if (hle_vk_check(sr, "vkQueueSubmit (host present)") &&
                T.vkWaitForFences(g_hvk.dev, 1, &g_ph.fence, VK_TRUE, 2000000000ull) == VK_SUCCESS) {
                const uint8_t *src = g_ph.map;
                for (uint32_t y = 0; y < f.h; y++) {
                    memcpy((uint8_t *)dst + (size_t)y * pitch, src + (size_t)y * f.w * 4, (size_t)f.w * 4);
                }
                *w = f.w;
                *h = f.h;
                *seen = (uint64_t)f.gen << 32 | f.swap_id;
                __atomic_add_fetch(&g_ph.taken, 1, __ATOMIC_RELAXED);
                ok = true;
            }
        }
        hle_disp_use_end();
        hle_disp_release(f.slot);
    }
    pthread_mutex_unlock(&g_ph.lock);
    return ok;
}

void pshx_present_shutdown(void)
{
    pthread_mutex_lock(&g_ph.lock);
    if (g_ph.made && g_hvk.dev) {
        T.vkDeviceWaitIdle(g_hvk.dev);
        drop_buffer();
        T.vkDestroyFence(g_hvk.dev, g_ph.fence, NULL);
        T.vkDestroyCommandPool(g_hvk.dev, g_ph.pool, NULL);
        g_ph.made = false;
    }
    pthread_mutex_unlock(&g_ph.lock);
}

/* ---- swapchain.c's window presenter: no Android window on the console ---------------------- */

bool hle_present_unchanged(struct ANativeWindow *win, uint64_t key)
{
    (void)win;
    (void)key;
    return false;
}

bool hle_present_begin(struct ANativeWindow *win, uint32_t *w, uint32_t *h)
{
    (void)win;
    *w = *h = 0;
    return false;
}

bool hle_present_frame(uint32_t *w, uint32_t *h)
{
    *w = *h = 0;
    return false;
}

void hle_present_end(float sx, float sy, bool fxaa, int upscale, bool average, uint64_t key)
{
    (void)sx;
    (void)sy;
    (void)fxaa;
    (void)upscale;
    (void)average;
    (void)key;
}
