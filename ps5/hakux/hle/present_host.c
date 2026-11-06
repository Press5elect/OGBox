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

/* ---- OGBox 0.0.4: the title's upload buffer as the copy's destination ----
 * The frame went GPU -> this buffer -> (the CPU, row by row) -> the title's upload buffer: at 6x
 * (3840x2880) 44 MB a frame on the title's render thread, and at 60 fps its loop missed a refresh
 * now and then (BLACK with its 60 fps mod: two refreshes between pickups 217 times). The two
 * Vulkan devices share the console's one address space, so the HLE GPU's device takes the title's
 * mapped buffer as its own (VK_EXT_external_memory_host) and copies the frame straight into it. */
#define IMPORTS 4

static struct {
    void *ptr;
    VkDeviceSize bytes;
    VkDeviceMemory mem;
    VkBuffer buf;
    unsigned used;
} g_imp[IMPORTS];
static unsigned g_imp_clock;
static bool g_imp_said, g_imp_off;

static void imp_drop(int i)
{
    if (g_imp[i].buf) {
        T.vkDestroyBuffer(g_hvk.dev, g_imp[i].buf, NULL);
    }
    if (g_imp[i].mem) {
        T.vkFreeMemory(g_hvk.dev, g_imp[i].mem, NULL);
    }
    memset(&g_imp[i], 0, sizeof(g_imp[i]));
}

/* dst (bytes long) as a buffer of the HLE GPU's device, or VK_NULL_HANDLE (then the copy goes
 * through this presenter's own buffer, as before). Under g_ph.lock. */
static VkBuffer imp_buffer(void *dst, size_t bytes)
{
    const VkDeviceSize align = g_hvk.caps.host_ptr_align;
    if (g_imp_off || !g_hvk.caps.host_ptr || !align) {
        return VK_NULL_HANDLE;
    }
    if ((uintptr_t)dst % align || bytes < align) {
        g_imp_off = true;
        hle_log("hakuX-hle-back", "present: the title's upload buffer at %p (%zu bytes) isn't on %llu-byte pages: "
                "frames go through the CPU", dst, bytes, (unsigned long long)align);
        return VK_NULL_HANDLE;
    }
    const VkDeviceSize size = (VkDeviceSize)bytes / align * align;
    int free_i = 0;
    for (int i = 0; i < IMPORTS; i++) {
        if (g_imp[i].ptr == dst && g_imp[i].bytes == size) {
            g_imp[i].used = ++g_imp_clock;
            return g_imp[i].buf;
        }
        if (g_imp[i].used < g_imp[free_i].used) {
            free_i = i;
        }
    }
    imp_drop(free_i);
    VkMemoryHostPointerPropertiesEXT hp = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    const VkExternalMemoryBufferCreateInfo ebi = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                                                  .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    const VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &ebi, .size = size,
                                    .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkResult r = T.vkGetMemoryHostPointerPropertiesEXT
                     ? T.vkGetMemoryHostPointerPropertiesEXT(g_hvk.dev,
                                                             VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                                                             dst, &hp)
                     : VK_ERROR_FEATURE_NOT_PRESENT;
    if (r == VK_SUCCESS) {
        r = T.vkCreateBuffer(g_hvk.dev, &bci, NULL, &buf);
    }
    if (r == VK_SUCCESS) {
        VkMemoryRequirements mr;
        T.vkGetBufferMemoryRequirements(g_hvk.dev, buf, &mr);
        const uint32_t bits = mr.memoryTypeBits & hp.memoryTypeBits;
        int mt = hle_vk_memtype(bits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
        if (mt < 0) {
            mt = hle_vk_memtype(bits, 0, 0);
        }
        const VkImportMemoryHostPointerInfoEXT imp = {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, .pHostPointer = dst};
        const VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &imp,
                                          .allocationSize = size, .memoryTypeIndex = (uint32_t)mt};
        r = mt < 0 || mr.size > size ? VK_ERROR_FEATURE_NOT_PRESENT : T.vkAllocateMemory(g_hvk.dev, &mai, NULL, &mem);
        if (r == VK_SUCCESS) {
            r = T.vkBindBufferMemory(g_hvk.dev, buf, mem, 0);
        }
    }
    if (r != VK_SUCCESS) {
        if (buf) {
            T.vkDestroyBuffer(g_hvk.dev, buf, NULL);
        }
        if (mem) {
            T.vkFreeMemory(g_hvk.dev, mem, NULL);
        }
        g_imp_off = true;                  /* not again: the copy through this presenter's buffer */
        hle_log("hakuX-hle-back", "present: the title's upload buffer at %p (%zu bytes) can't be the copy's "
                "destination (%d): frames go through the CPU", dst, bytes, (int)r);
        return VK_NULL_HANDLE;
    }
    g_imp[free_i] = (typeof(g_imp[0])){dst, size, mem, buf, ++g_imp_clock};
    if (!g_imp_said) {
        g_imp_said = true;
        hle_log("hakuX-hle-back", "present: frames copied straight into the title's upload buffer (%p, %llu KiB; "
                "no CPU copy)", dst, (unsigned long long)(size >> 10));
    }
    return buf;
}

void pshx_present_forget(void *dst)
{
    pthread_mutex_lock(&g_ph.lock);
    for (int i = 0; i < IMPORTS; i++) {
        if (g_imp[i].ptr == dst && g_hvk.dev) {
            imp_drop(i);    /* (no copy is in flight: take waits for its own) */
        }
    }
    pthread_mutex_unlock(&g_ph.lock);
}

bool pshx_present_take_into(uint64_t *seen, void *dst, size_t dst_bytes, uint32_t pitch, uint32_t max_w,
                            uint32_t max_h, uint32_t *w, uint32_t *h);

bool pshx_present_take(uint64_t *seen, void *dst, uint32_t pitch, uint32_t max_w, uint32_t max_h, uint32_t *w,
                       uint32_t *h)
{
    return pshx_present_take_into(seen, dst, 0, pitch, max_w, max_h, w, h);
}

bool pshx_present_take_into(uint64_t *seen, void *dst, size_t dst_bytes, uint32_t pitch, uint32_t max_w,
                            uint32_t max_h, uint32_t *w, uint32_t *h)
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
        /* straight into dst: rows packed (pitch a row's bytes) and the import's whole pages hold it */
        VkBuffer direct = VK_NULL_HANDLE;
        if (dst_bytes && pitch == f.w * 4u && f.w <= max_w && f.h <= max_h) {
            direct = imp_buffer(dst, dst_bytes);
            if (direct) {
                for (int i = 0; i < IMPORTS; i++) {
                    if (g_imp[i].buf == direct && g_imp[i].bytes < size) {
                        direct = VK_NULL_HANDLE;    /* the frame is larger than its whole pages */
                    }
                }
            }
        }
        if (f.w <= max_w && f.h <= max_h && (direct || ensure_buffer(size))) {
            const VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                                 .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
            T.vkResetCommandBuffer(g_ph.cb, 0);
            T.vkBeginCommandBuffer(g_ph.cb, &bi);
            hle_img_barrier(g_ph.cb, f.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            const VkBufferImageCopy r = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                         .imageExtent = {f.w, f.h, 1}};
            T.vkCmdCopyImageToBuffer(g_ph.cb, f.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, direct ? direct : g_ph.buf,
                                     1, &r);
            hle_img_barrier(g_ph.cb, f.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            /* the copy's writes out of the GPU's caches before the fence: the host (and the title's
             * device, reading the same memory) sees them */
            const VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
            T.vkCmdPipelineBarrier(g_ph.cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0,
                                   NULL, 0, NULL);
            T.vkEndCommandBuffer(g_ph.cb);
            const VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1,
                                     .pCommandBuffers = &g_ph.cb};
            T.vkResetFences(g_hvk.dev, 1, &g_ph.fence);
            hle_vk_queue_lock();
            const VkResult sr = T.vkQueueSubmit(g_hvk.queue, 1, &si, g_ph.fence);
            hle_vk_queue_unlock();
            if (hle_vk_check(sr, "vkQueueSubmit (host present)") &&
                T.vkWaitForFences(g_hvk.dev, 1, &g_ph.fence, VK_TRUE, 2000000000ull) == VK_SUCCESS) {
                if (!direct) {
                    const uint8_t *src = g_ph.map;
                    for (uint32_t y = 0; y < f.h; y++) {
                        memcpy((uint8_t *)dst + (size_t)y * pitch, src + (size_t)y * f.w * 4, (size_t)f.w * 4);
                    }
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
        for (int i = 0; i < IMPORTS; i++) {
            imp_drop(i);
        }
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
