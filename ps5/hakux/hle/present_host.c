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
#include "hw/xbox/hle/vk/back.h"
#include "hw/xbox/hle/vk/back_int.h"
#include "hw/xbox/hle/vk/display.h"
#include "hw/xbox/hle/vk/swapchain.h"
#include "hw/xbox/hle/vk/vkdev.h"

#include "present_host.h"
#include "core/ogbox_core.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

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
    bool pending;               /* the fence's submit not seen done (a wait that failed): nothing reused */
    /* OGBox 1.0.5: the copy submitted and not yet seen done (one at a time): its slot held and the
     * display's use taken (hle_disp_use_begin) until then, on the title's render thread */
    struct {
        bool on, direct;        /* direct: written into dst itself (an import), else into buf */
        HleDispFrame f;
        void *dst;
        uint32_t pitch, said;   /* said: the seconds a "not done" line was logged for */
        uint64_t at;            /* submitted (CLOCK_MONOTONIC, ns) */
    } copy;
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

static int copy_poll(bool wait, uint64_t *seen, uint32_t *w, uint32_t *h);

void pshx_present_forget(void *dst)
{
    pthread_mutex_lock(&g_ph.lock);
    /* 1.0.5: a copy in flight (pshx_present_take_start) may write into dst: waited for first (the
     * title's frame from it is lost: the title is letting go of dst, and takes the frame again) */
    if (g_ph.copy.on && g_hvk.dev) {
        uint64_t seen = 0;
        uint32_t w, h;
        copy_poll(true, &seen, &w, &h);
    }
    /* a copy whose wait failed (pending) may still write into the buffers: then they are only
     * forgotten (never matched again; dropped as the oldest by imp_buffer, which take calls once
     * the fence is seen signalled, or at shutdown after the device is idle) */
    const bool busy = g_ph.pending && g_hvk.dev && T.vkGetFenceStatus(g_hvk.dev, g_ph.fence) != VK_SUCCESS;
    for (int i = 0; i < IMPORTS; i++) {
        if (g_imp[i].ptr == dst && g_hvk.dev) {
            if (busy) {
                g_imp[i].ptr = NULL;
                g_imp[i].used = 0;
            } else {
                imp_drop(i);    /* (no copy is in flight: waited for above) */
            }
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

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* A failed submit or wait (the GPU lost) left the fence unsignalled: nothing of the copy's is
 * reused before it reads signalled. Under g_ph.lock; true: free to submit. */
static bool settled(void)
{
    if (g_ph.pending) {
        if (T.vkGetFenceStatus(g_hvk.dev, g_ph.fence) != VK_SUCCESS) {
            return false;
        }
        g_ph.pending = false;
    }
    return true;
}

/* The front frame's copy into dst (or this presenter's buffer) submitted on the HLE GPU's queue,
 * when it is newer than *seen. true: in flight (g_ph.copy): its slot held and inside
 * hle_disp_use_begin until copy_end; false: nothing submitted, nothing held. Under g_ph.lock. */
static bool copy_begin(const uint64_t *seen, void *dst, size_t dst_bytes, uint32_t pitch, uint32_t max_w,
                       uint32_t max_h)
{
    uint32_t gen = 0, swap_id = 0;
    if (g_ph.copy.on || !hle_disp_front(&gen, &swap_id) || ((uint64_t)gen << 32 | swap_id) == *seen || !settled()) {
        return false;
    }
    HleDispFrame f;
    /* the slots' images stay until the copy is done: inside use before the frame is taken, as
     * swapchain.c's hle_present_frame (a display resize between the two destroys its image) */
    hle_disp_use_begin();
    if (!make() || !hle_disp_acquire(&f)) {
        hle_disp_use_end();
        return false;
    }
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
    if (f.w > max_w || f.h > max_h || !(direct || ensure_buffer(size))) {
        hle_disp_release(f.slot);
        hle_disp_use_end();
        return false;
    }
    const VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    T.vkResetCommandBuffer(g_ph.cb, 0);
    T.vkBeginCommandBuffer(g_ph.cb, &bi);
    hle_img_barrier(g_ph.cb, f.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    const VkBufferImageCopy r = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                 .imageExtent = {f.w, f.h, 1}};
    T.vkCmdCopyImageToBuffer(g_ph.cb, f.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, direct ? direct : g_ph.buf, 1, &r);
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
    if (!hle_vk_check(sr, "vkQueueSubmit (host present)")) {
        hle_disp_release(f.slot);       /* (nothing submitted: the next copy resets the fence as before) */
        hle_disp_use_end();
        return false;
    }
    g_ph.copy.on = true;
    g_ph.copy.f = f;
    g_ph.copy.dst = dst;
    g_ph.copy.pitch = pitch;
    g_ph.copy.direct = direct != VK_NULL_HANDLE;
    g_ph.copy.at = now_ns();
    g_ph.copy.said = 0;
    return true;
}

/* The copy in flight let go: its slot released, the display's use ended. done: its fence read
 * signalled, the frame is in dst (rows copied here when it went through this presenter's buffer);
 * otherwise (the GPU lost) pending keeps the command buffer, fence and buffers as they are.
 * Under g_ph.lock. */
static bool copy_end(bool done, uint64_t *seen, uint32_t *w, uint32_t *h)
{
    const HleDispFrame f = g_ph.copy.f;
    if (done) {
        if (!g_ph.copy.direct) {
            const uint8_t *src = g_ph.map;
            for (uint32_t y = 0; y < f.h; y++) {
                memcpy((uint8_t *)g_ph.copy.dst + (size_t)y * g_ph.copy.pitch, src + (size_t)y * f.w * 4,
                       (size_t)f.w * 4);
            }
        }
        *w = f.w;
        *h = f.h;
        *seen = (uint64_t)f.gen << 32 | f.swap_id;
        __atomic_add_fetch(&g_ph.taken, 1, __ATOMIC_RELAXED);
    } else {
        g_ph.pending = true;
    }
    g_ph.copy.on = false;
    hle_disp_release(f.slot);
    hle_disp_use_end();
    return done;
}

/* The copy in flight: its fence read, or (wait) waited for until it signals. Until then the GPU
 * still reads the slot's image and writes the buffer, and the command buffer and fence are in use:
 * none of them is let go, reset or reused, so a slow copy is waited for (a line each 2 s), never
 * given up on; only the GPU lost ends it without the fence. Under g_ph.lock. */
static int copy_poll(bool wait, uint64_t *seen, uint32_t *w, uint32_t *h)
{
    if (!g_ph.copy.on) {
        return PSHX_TAKE_NONE;
    }
    for (;;) {
        const VkResult r = wait ? T.vkWaitForFences(g_hvk.dev, 1, &g_ph.fence, VK_TRUE, 2000000000ull)
                                : T.vkGetFenceStatus(g_hvk.dev, g_ph.fence);
        if (r == VK_SUCCESS) {
            copy_end(true, seen, w, h);
            return PSHX_TAKE_DONE;
        }
        if (r != VK_TIMEOUT && r != VK_NOT_READY) {
            hle_vk_check(r, wait ? "vkWaitForFences (host present)" : "vkGetFenceStatus (host present)");
            copy_end(false, seen, w, h);
            return PSHX_TAKE_FAILED;
        }
        const unsigned s = (unsigned)((now_ns() - g_ph.copy.at) / 2000000000ull) * 2;
        if (s > g_ph.copy.said) {
            g_ph.copy.said = s;
            hle_log("hakuX-hle-back", "present (host image): the frame's copy not done after %u s: waiting on", s);
        }
        if (hle_vk_lost()) {
            copy_end(false, seen, w, h);
            return PSHX_TAKE_FAILED;
        }
        if (!wait) {
            return PSHX_TAKE_PENDING;
        }
    }
}

bool pshx_present_take_into(uint64_t *seen, void *dst, size_t dst_bytes, uint32_t pitch, uint32_t max_w,
                            uint32_t max_h, uint32_t *w, uint32_t *h)
{
    if (!pshx_present_ready()) {
        return false;
    }
    pthread_mutex_lock(&g_ph.lock);
    /* (a copy pshx_present_take_start left in flight is waited for first: one at a time; the two
     * aren't mixed, NC_PRESENT_WAIT picks one for the run) */
    uint32_t pw = 0, ph = 0;
    uint64_t pseen = *seen;
    copy_poll(true, &pseen, &pw, &ph);
    const bool ok = copy_begin(seen, dst, dst_bytes, pitch, max_w, max_h) && copy_poll(true, seen, w, h) == PSHX_TAKE_DONE;
    pthread_mutex_unlock(&g_ph.lock);
    return ok;
}

int pshx_present_take_start(const uint64_t *seen, void *dst, size_t dst_bytes, uint32_t pitch, uint32_t max_w,
                            uint32_t max_h)
{
    if (!pshx_present_ready()) {
        return PSHX_TAKE_NONE;
    }
    pthread_mutex_lock(&g_ph.lock);
    const int r = g_ph.copy.on ? PSHX_TAKE_PENDING
                  : copy_begin(seen, dst, dst_bytes, pitch, max_w, max_h) ? PSHX_TAKE_STARTED : PSHX_TAKE_NONE;
    pthread_mutex_unlock(&g_ph.lock);
    return r;
}

int pshx_present_take_poll(uint64_t *seen, uint32_t *w, uint32_t *h)
{
    pthread_mutex_lock(&g_ph.lock);
    const int r = g_ph.copy.on && g_hvk.dev ? copy_poll(false, seen, w, h) : PSHX_TAKE_NONE;
    pthread_mutex_unlock(&g_ph.lock);
    return r;
}

void pshx_present_shutdown(void)
{
    pthread_mutex_lock(&g_ph.lock);
    if (g_ph.made && g_hvk.dev) {
        T.vkDeviceWaitIdle(g_hvk.dev);
        if (g_ph.copy.on) {
            uint64_t seen = 0;
            uint32_t w, h;
            copy_end(false, &seen, &w, &h);     /* (the device idle: its slot and the display let go) */
        }
        drop_buffer();
        for (int i = 0; i < IMPORTS; i++) {
            imp_drop(i);
        }
        T.vkDestroyFence(g_hvk.dev, g_ph.fence, NULL);
        T.vkDestroyCommandPool(g_hvk.dev, g_ph.pool, NULL);
        g_ph.made = false;
        g_ph.pending = false;
    }
    pthread_mutex_unlock(&g_ph.lock);
}

/* ---- one device (NC_HLE_ONE_DEVICE=1): the title's device is the HLE GPU's ----------------------
 * The frame then never leaves the GPU: the title's own command buffer copies the display slot into
 * its staging buffer (what the upload, frame generation and a screenshot read), on the one queue the
 * two share, after the slot's timeline value; no fence waited on the title's render thread. */
bool pshx_present_gpu_needs(void *instance, uint32_t api_version, void *physical_device, struct PshxGpuNeeds *needs,
                            char *why, unsigned why_size)
{
    const HleVkShare host = {.inst = (VkInstance)instance, .api = api_version, .phys = (VkPhysicalDevice)physical_device};
    HleVkNeeds n;
    if (hle_vk_needs(&host, &n, why, why_size)) {
        return false;
    }
    needs->queue_family = n.qfam;
    needs->extension_count = n.n_exts;
    needs->extensions = n.exts;
    needs->features = n.features;
    return true;
}

void pshx_present_gpu_share(const struct PshxGpuShare *share)
{
    if (!share) {
        hle_vk_share(NULL);
        return;
    }
    const HleVkShare host = {.inst = (VkInstance)share->instance, .api = share->api_version,
                             .phys = (VkPhysicalDevice)share->physical_device, .dev = (VkDevice)share->device,
                             .qfam = share->queue_family, .queue = (VkQueue)share->queue,
                             .lock = share->queue_lock, .unlock = share->queue_unlock,
                             .exts = share->extensions, .n_exts = share->extension_count,
                             .features = (const VkPhysicalDeviceFeatures2 *)share->features};
    hle_vk_share(&host);
}

bool pshx_present_gpu_shared(void)
{
    return hle_vk_ready() && hle_vk_shared();   /* (set before the back end is ready) */
}

static struct {
    bool held;                  /* a slot taken: inside hle_disp_use_begin/end until done */
    int slot;
} g_slot;

bool pshx_present_slot_take(uint64_t *seen, struct PshxSlotFrame *frame)
{
    if (!pshx_present_ready() || !hle_vk_shared()) {
        return false;
    }
    uint32_t gen = 0, swap_id = 0;
    if (!hle_disp_front(&gen, &swap_id) || ((uint64_t)gen << 32 | swap_id) == *seen) {
        return false;
    }
    if (g_slot.held) {
        pshx_present_slot_done(g_slot.slot);    /* (the last one never submitted: nothing reads it) */
    }
    /* the slots' images stay until done: a display resize (hle_disp_init) waits for the use */
    hle_disp_use_begin();
    HleDispFrame f;
    if (!hle_disp_acquire(&f)) {
        hle_disp_use_end();
        return false;
    }
    /* A front frame whose write isn't submitted yet (a present before the render thread got to it,
     * hle_disp_present_late): the title's submit waiting for its value would be ahead of the signal
     * on the one queue. Taken at a later refresh. */
    if (f.value > hle_tl_submitted()) {
        hle_disp_release(f.slot);
        hle_disp_use_end();
        return false;
    }
    frame->slot = f.slot;
    frame->width = f.w;
    frame->height = f.h;
    frame->image = (void *)f.img;
    frame->semaphore = (void *)hle_tl_sem();
    frame->value = f.value;
    *seen = (uint64_t)f.gen << 32 | f.swap_id;
    g_slot.held = true;
    g_slot.slot = f.slot;
    __atomic_add_fetch(&g_ph.taken, 1, __ATOMIC_RELAXED);
    return true;
}

void pshx_present_slot_done(int slot)
{
    if (!g_slot.held || slot != g_slot.slot) {
        return;
    }
    g_slot.held = false;
    /* the title's reads are submitted: the render thread's next write of this slot is submitted after
     * them on the same queue, and its first barrier (ALL_COMMANDS to ALL_COMMANDS) waits for them */
    hle_disp_release(slot);
    hle_disp_use_end();
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
