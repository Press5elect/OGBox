/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 1.0.5: the two devices' frame handoff on the PC. ps5/hakux/hle/present_host.c itself (its
 * take_start / take_poll, the blocking take_into, forget) runs against a fake Vulkan device whose
 * queue completes the copies only when the test says, and a fake display (slots, held flags, the
 * display's use); ps5/src/ogbox_take.h (the title's side) drives it as ogbox_main.cpp's fetchPicture
 * does, with fake title frames on two slots whose fences finish late. Checked on every pass: one copy
 * at a time; the display's use held and the slot held for as long as a copy is in flight (a display
 * resize waits); no copy into a staging buffer a title frame still reads or the CPU's picture is in;
 * no title frame reads a staging buffer a copy is writing; each picture picked up is the frame its
 * copy started with, whole.
 */
#include "ogbox_take.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <vector>

extern "C" {
#include "present_host.h"
#include "hw/xbox/hle/vk/vkdev.h"
}

static int g_fails;
#define CHECK(c)                                                                                \
	do {                                                                                        \
		if (!(c)) {                                                                             \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);          \
			g_fails++;                                                                          \
		}                                                                                       \
	} while (0)

/* ---- the fake device --------------------------------------------------------------------- */
struct FakeMem {
	std::vector<uint8_t> own;   // allocated (the presenter's buffer)
	uint8_t *ptr{ nullptr };    // what it maps to (own, or an imported host pointer)
};
struct FakeBuffer {
	VkDeviceSize size;
	FakeMem *mem{ nullptr };
};
struct FakeImage {
	int slot;
};
struct FakeFence {
	bool signalled{ false };
};
struct Op {
	int slot;
	uint32_t swap_id, w, h;
	FakeBuffer *dst;
	FakeFence *fence;
	int due;   // passes until done (-1: never, a hung GPU)
};
struct FakeCb {
	int slot{ -1 };
	FakeBuffer *dst{ nullptr };
};

static std::deque<Op> g_queue;
static bool g_lost;
static int g_dueNext;   // the next submit's latency, in gpu_tick()s
static int g_waitTimeouts;   // vkWaitForFences answers VK_TIMEOUT this many times first
static unsigned g_logs, g_notDoneLogs;

/* the display: 4 slots, the front one, its frame's pixels */
static struct {
	uint32_t gen{ 1 }, w{ 64 }, h{ 32 };
	int front{ -1 };
	uint32_t swap[4]{};
	bool held[4]{};
	int use{ 0 };       // the display's use (a mutex: never taken twice)
	FakeImage img[4]{ { 0 }, { 1 }, { 2 }, { 3 } };
	uint32_t imgW[4]{}, imgH[4]{};
	uint32_t nextSwap{ 0 };
	unsigned resizesWaited{ 0 }, resizes{ 0 };
} g_disp;

static uint8_t pixel(uint32_t swap, uint32_t x, uint32_t y, int c)
{
	return static_cast<uint8_t>(swap * 7 + x * 3 + y * 5 + c);
}

static void run_op(const Op &o)
{
	uint8_t *p = o.dst->mem->ptr;
	for (uint32_t y = 0; y < o.h; y++) {
		for (uint32_t x = 0; x < o.w; x++) {
			for (int c = 0; c < 4; c++) {
				p[(size_t(y) * o.w + x) * 4 + c] = pixel(o.swap_id, x, y, c);
			}
		}
	}
	o.fence->signalled = true;
}

/* the HLE GPU's queue moves on: copies whose time has come are done */
static void gpu_tick()
{
	while (!g_queue.empty() && g_queue.front().due >= 0 && g_queue.front().due-- == 0) {
		run_op(g_queue.front());
		g_queue.pop_front();
	}
}

static void gpu_finish()
{
	while (!g_queue.empty()) {
		run_op(g_queue.front());
		g_queue.pop_front();
	}
}

/* a copy in flight writes this memory */
static bool gpu_writes(const uint8_t *p, size_t bytes)
{
	for (const Op &o : g_queue) {
		const uint8_t *q = o.dst->mem->ptr;
		const size_t n = size_t(o.w) * o.h * 4;
		if (p < q + n && q < p + bytes) {
			return true;
		}
	}
	return false;
}

static void VKAPI_CALL f_unmap(VkDevice, VkDeviceMemory) {}
static void VKAPI_CALL f_destroy_buffer(VkDevice, VkBuffer b, const VkAllocationCallbacks *)
{
	delete reinterpret_cast<FakeBuffer *>(b);
}
static void VKAPI_CALL f_free(VkDevice, VkDeviceMemory m, const VkAllocationCallbacks *)
{
	delete reinterpret_cast<FakeMem *>(m);
}
static VkResult VKAPI_CALL f_create_pool(VkDevice, const VkCommandPoolCreateInfo *, const VkAllocationCallbacks *, VkCommandPool *p)
{
	*p = reinterpret_cast<VkCommandPool>(new int(0));
	return VK_SUCCESS;
}
static void VKAPI_CALL f_destroy_pool(VkDevice, VkCommandPool p, const VkAllocationCallbacks *)
{
	delete reinterpret_cast<int *>(p);
}
static VkResult VKAPI_CALL f_create_fence(VkDevice, const VkFenceCreateInfo *, const VkAllocationCallbacks *, VkFence *f)
{
	*f = reinterpret_cast<VkFence>(new FakeFence);
	return VK_SUCCESS;
}
static void VKAPI_CALL f_destroy_fence(VkDevice, VkFence f, const VkAllocationCallbacks *)
{
	delete reinterpret_cast<FakeFence *>(f);
}
static VkResult VKAPI_CALL f_alloc_cb(VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *cb)
{
	*cb = reinterpret_cast<VkCommandBuffer>(new FakeCb);
	return VK_SUCCESS;
}
static VkResult VKAPI_CALL f_create_buffer(VkDevice, const VkBufferCreateInfo *ci, const VkAllocationCallbacks *, VkBuffer *b)
{
	*b = reinterpret_cast<VkBuffer>(new FakeBuffer{ ci->size });
	return VK_SUCCESS;
}
static void VKAPI_CALL f_buffer_reqs(VkDevice, VkBuffer b, VkMemoryRequirements *r)
{
	*r = { reinterpret_cast<FakeBuffer *>(b)->size, 64, 1 };
}
static VkResult VKAPI_CALL f_alloc(VkDevice, const VkMemoryAllocateInfo *ai, const VkAllocationCallbacks *, VkDeviceMemory *m)
{
	auto *mem = new FakeMem;
	auto *imp = static_cast<const VkImportMemoryHostPointerInfoEXT *>(ai->pNext);
	if (imp && imp->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT) {
		mem->ptr = static_cast<uint8_t *>(imp->pHostPointer);
	} else {
		mem->own.resize(ai->allocationSize);
		mem->ptr = mem->own.data();
	}
	*m = reinterpret_cast<VkDeviceMemory>(mem);
	return VK_SUCCESS;
}
static VkResult VKAPI_CALL f_bind(VkDevice, VkBuffer b, VkDeviceMemory m, VkDeviceSize)
{
	reinterpret_cast<FakeBuffer *>(b)->mem = reinterpret_cast<FakeMem *>(m);
	return VK_SUCCESS;
}
static VkResult VKAPI_CALL f_map(VkDevice, VkDeviceMemory m, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void **p)
{
	*p = reinterpret_cast<FakeMem *>(m)->ptr;
	return VK_SUCCESS;
}
static VkResult VKAPI_CALL f_host_props(VkDevice, VkExternalMemoryHandleTypeFlagBits, const void *, VkMemoryHostPointerPropertiesEXT *p)
{
	p->memoryTypeBits = 1;
	return VK_SUCCESS;
}
static VkResult VKAPI_CALL f_fence_status(VkDevice, VkFence f)
{
	if (reinterpret_cast<FakeFence *>(f)->signalled) {
		return VK_SUCCESS;
	}
	return g_lost ? VK_ERROR_DEVICE_LOST : VK_NOT_READY;
}
static VkResult VKAPI_CALL f_wait(VkDevice, uint32_t, const VkFence *f, VkBool32, uint64_t)
{
	auto *fence = reinterpret_cast<FakeFence *>(*f);
	if (fence->signalled) {
		return VK_SUCCESS;
	}
	if (g_lost) {
		return VK_ERROR_DEVICE_LOST;
	}
	if (g_waitTimeouts > 0) {
		g_waitTimeouts--;
		return VK_TIMEOUT;
	}
	/* the wait lets the queue run up to this fence (a hung op stays hung: a timeout) */
	while (!g_queue.empty() && !fence->signalled) {
		if (g_queue.front().due < 0) {
			return VK_TIMEOUT;
		}
		run_op(g_queue.front());
		g_queue.pop_front();
	}
	return fence->signalled ? VK_SUCCESS : VK_TIMEOUT;
}
static VkResult VKAPI_CALL f_reset_fences(VkDevice, uint32_t, const VkFence *f)
{
	auto *fence = reinterpret_cast<FakeFence *>(*f);
	for (const Op &o : g_queue) {
		CHECK(o.fence != fence);   // never reset while its submit is on the queue
	}
	fence->signalled = false;
	return VK_SUCCESS;
}
static VkResult VKAPI_CALL f_reset_cb(VkCommandBuffer cb, VkCommandBufferResetFlags)
{
	CHECK(g_queue.empty());   // reset only with nothing of its on the queue (one copy at a time)
	*reinterpret_cast<FakeCb *>(cb) = FakeCb{};
	return VK_SUCCESS;
}
static VkResult VKAPI_CALL f_begin(VkCommandBuffer, const VkCommandBufferBeginInfo *) { return VK_SUCCESS; }
static VkResult VKAPI_CALL f_end(VkCommandBuffer) { return VK_SUCCESS; }
static void VKAPI_CALL f_copy(VkCommandBuffer cb, VkImage img, VkImageLayout, VkBuffer dst, uint32_t, const VkBufferImageCopy *r)
{
	auto *c = reinterpret_cast<FakeCb *>(cb);
	c->slot = reinterpret_cast<FakeImage *>(img)->slot;
	c->dst = reinterpret_cast<FakeBuffer *>(dst);
	CHECK(c->dst->size >= VkDeviceSize(r->imageExtent.width) * r->imageExtent.height * 4);
}
static void VKAPI_CALL f_barrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t,
	const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *) {}
static VkResult VKAPI_CALL f_submit(VkQueue, uint32_t, const VkSubmitInfo *si, VkFence f)
{
	auto *c = reinterpret_cast<FakeCb *>(si->pCommandBuffers[0]);
	CHECK(c->slot >= 0 && g_disp.held[c->slot] && g_disp.use == 1);
	g_queue.push_back({ c->slot, g_disp.swap[c->slot], g_disp.imgW[c->slot], g_disp.imgH[c->slot], c->dst,
		reinterpret_cast<FakeFence *>(f), g_dueNext });
	if (g_dueNext == 0 && g_queue.size() == 1) {
		gpu_tick();   // the queue was idle: done at once
	}
	return VK_SUCCESS;
}
static VkResult VKAPI_CALL f_idle(VkDevice)
{
	gpu_finish();
	return VK_SUCCESS;
}

HleVk g_hvk;

extern "C" {
bool hle_vk_ready(void) { return true; }
bool hle_vk_lost(void) { return g_lost; }
bool hle_vk_check(VkResult r, const char *what)
{
	if (r != VK_SUCCESS) {
		std::printf("  (vk: %s: %d)\n", what, static_cast<int>(r));
	}
	return r == VK_SUCCESS;
}
void hle_vk_queue_lock(void) {}
void hle_vk_queue_unlock(void) {}
int hle_vk_memtype(uint32_t, VkMemoryPropertyFlags, VkMemoryPropertyFlags) { return 0; }
void hle_log(const char *, const char *fmt, ...)
{
	g_logs++;
	g_notDoneLogs += std::strstr(fmt, "not done after") ? 1 : 0;
}
void hle_img_barrier(VkCommandBuffer, VkImage, VkImageAspectFlags, VkImageLayout, VkImageLayout) {}

bool hle_disp_front(uint32_t *gen, uint32_t *swap_id)
{
	if (g_disp.front < 0) {
		return false;
	}
	*gen = g_disp.gen;
	*swap_id = g_disp.swap[g_disp.front];
	return true;
}
bool hle_disp_acquire(HleDispFrame *f)
{
	CHECK(g_disp.use == 1);   // 1.0.3: the use taken before the slot
	if (g_disp.front < 0) {
		return false;
	}
	const int i = g_disp.front;
	g_disp.held[i] = true;
	*f = HleDispFrame{ i, g_disp.gen, g_disp.swap[i], g_disp.imgW[i], g_disp.imgH[i], 0,
		reinterpret_cast<VkImage>(&g_disp.img[i]), VK_NULL_HANDLE };
	return true;
}
void hle_disp_release(int slot)
{
	CHECK(g_disp.held[slot]);
	for (const Op &o : g_queue) {
		CHECK(g_lost || o.slot != slot);   // never let go while a copy still reads it (but the GPU lost)
	}
	g_disp.held[slot] = false;
}
void hle_disp_use_begin(void)
{
	CHECK(g_disp.use == 0);   // (a mutex: taken twice on one thread would hang)
	g_disp.use++;
}
void hle_disp_use_end(void)
{
	CHECK(g_disp.use == 1);
	g_disp.use--;
}
void hle_disp_size(uint32_t *w, uint32_t *h)
{
	*w = g_disp.w;
	*h = g_disp.h;
}
HleDispStats hle_disp_stats(void) { return HleDispStats{ g_disp.nextSwap, 0 }; }
int hle_vk_needs(const HleVkShare *, HleVkNeeds *, char *, unsigned) { return -1; }
void hle_vk_share(const HleVkShare *) {}
bool hle_vk_shared(void) { return false; }
uint64_t hle_tl_submitted(void) { return 0; }
VkSemaphore hle_tl_sem(void) { return VK_NULL_HANDLE; }
}

/* the render thread presents a new frame: into a slot not held (a free one), the front one before let go */
static void present_frame()
{
	for (int k = 1; k <= 4; k++) {
		const int i = (g_disp.front + k + 4) % 4;
		if (!g_disp.held[i] && i != g_disp.front) {
			g_disp.swap[i] = ++g_disp.nextSwap;
			g_disp.imgW[i] = g_disp.w;
			g_disp.imgH[i] = g_disp.h;
			g_disp.front = i;
			return;
		}
	}
}

/* a display resize (hle_disp_init): waits for the use; the slots made again (none front) */
static bool try_resize(uint32_t w, uint32_t h)
{
	if (g_disp.use) {
		g_disp.resizesWaited++;
		return false;
	}
	for (int i = 0; i < 4; i++) {
		CHECK(!g_disp.held[i]);
	}
	g_disp.w = w;
	g_disp.h = h;
	g_disp.gen++;
	g_disp.front = -1;
	g_disp.resizes++;
	return true;
}

static void setup()
{
	g_hvk = HleVk{};
	g_hvk.t = HleVkTable{ f_unmap, f_destroy_buffer, f_free, f_create_pool, f_destroy_pool, f_create_fence, f_destroy_fence,
		f_alloc_cb, f_create_buffer, f_buffer_reqs, f_alloc, f_bind, f_map, f_host_props, f_fence_status, f_wait,
		f_reset_fences, f_reset_cb, f_begin, f_end, f_copy, f_barrier, f_submit, f_idle };
	g_hvk.dev = reinterpret_cast<VkDevice>(&g_hvk);
	g_hvk.queue = reinterpret_cast<VkQueue>(&g_queue);
	g_hvk.caps.host_ptr = true;
	g_hvk.caps.host_ptr_align = 4096;
}

static bool frame_is(const uint8_t *p, uint32_t swap, uint32_t w, uint32_t h, uint32_t pitch)
{
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			for (int c = 0; c < 4; c++) {
				if (p[size_t(y) * pitch + x * 4 + c] != pixel(swap, x, y, c)) {
					return false;
				}
			}
		}
	}
	return true;
}

/* page-aligned memory, as the title's staging buffers (whole 64 KiB pages) */
struct Pages {
	uint8_t *p;
	size_t n;
	explicit Pages(size_t bytes) : n((bytes + 0xFFFF) & ~size_t(0xFFFF))
	{
		p = static_cast<uint8_t *>(std::aligned_alloc(0x10000, n));
		std::memset(p, 0, n);
	}
	~Pages() { std::free(p); }
};

/* ---- the calls themselves ---------------------------------------------------------------- */
static void test_calls(bool hostPtr)
{
	std::printf("calls (%s)\n", hostPtr ? "into the title's memory" : "through the presenter's buffer");
	g_hvk.caps.host_ptr = hostPtr;
	CHECK(try_resize(64, 32));   // (no front frame: a display made again)
	Pages a(64 * 32 * 4), b(64 * 32 * 4);
	uint64_t seen = 0;
	uint32_t w = 0, h = 0;
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_NONE);
	CHECK(pshx_present_take_start(&seen, a.p, a.n, 64 * 4, 64, 32) == PSHX_TAKE_NONE);   // no frame yet
	present_frame();
	g_dueNext = 2;
	CHECK(pshx_present_take_start(&seen, a.p, a.n, 64 * 4, 64, 32) == PSHX_TAKE_STARTED);
	const int s = g_disp.front;
	CHECK(g_disp.held[s] && g_disp.use == 1);
	CHECK(!try_resize(64, 32));   // a resize waits for the copy
	CHECK(pshx_present_take_start(&seen, b.p, b.n, 64 * 4, 64, 32) == PSHX_TAKE_PENDING);   // one at a time
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_PENDING);
	present_frame();   // a newer one comes meanwhile: the held slot is kept
	CHECK(g_disp.front != s && g_disp.held[s]);
	gpu_tick();
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_PENDING);
	gpu_tick();
	gpu_tick();
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_DONE);
	CHECK(w == 64 && h == 32 && seen == (uint64_t(g_disp.gen) << 32 | g_disp.swap[s]));
	CHECK(frame_is(a.p, g_disp.swap[s], 64, 32, 64 * 4));
	CHECK(!g_disp.held[s] && g_disp.use == 0);
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_NONE);
	/* the newer frame, done before the first poll (the queue was idle) */
	g_dueNext = 0;
	CHECK(pshx_present_take_start(&seen, b.p, b.n, 64 * 4, 64, 32) == PSHX_TAKE_STARTED);
	gpu_tick();
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_DONE);
	CHECK(frame_is(b.p, g_disp.swap[g_disp.front], 64, 32, 64 * 4));
	CHECK(pshx_present_take_start(&seen, b.p, b.n, 64 * 4, 64, 32) == PSHX_TAKE_NONE);   // the same frame: nothing
	/* a frame larger than the caller takes: refused, nothing held */
	CHECK(try_resize(128, 32));
	present_frame();
	CHECK(pshx_present_take_start(&seen, a.p, a.n, 64 * 4, 64, 32) == PSHX_TAKE_NONE);
	CHECK(g_disp.use == 0 && !g_disp.held[g_disp.front]);
	CHECK(try_resize(64, 32));
	/* the blocking take, with two 2 s timeouts before the copy is done */
	present_frame();
	g_dueNext = 5;
	g_waitTimeouts = 2;
	CHECK(pshx_present_take_into(&seen, a.p, a.n, 64 * 4, 64, 32, &w, &h));
	CHECK(frame_is(a.p, g_disp.swap[g_disp.front], 64, 32, 64 * 4) && g_disp.use == 0 && g_queue.empty());
	/* forget while a copy writes the memory: waited for first, then nothing in flight */
	present_frame();
	g_dueNext = 3;
	CHECK(pshx_present_take_start(&seen, a.p, a.n, 64 * 4, 64, 32) == PSHX_TAKE_STARTED);
	pshx_present_forget(a.p);
	CHECK(g_queue.empty() && g_disp.use == 0 && !g_disp.held[g_disp.front]);
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_NONE);
	CHECK(pshx_present_take_start(&seen, b.p, b.n, 64 * 4, 64, 32) == PSHX_TAKE_STARTED);   // (taken again: not seen)
	gpu_finish();
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_DONE);
	pshx_present_forget(a.p);
	pshx_present_forget(b.p);
}

/* a copy that never completes: waited for forever (no reuse), then the GPU lost */
static void test_hung()
{
	std::printf("a copy that never completes, then the GPU lost\n");
	Pages a(64 * 32 * 4);
	uint64_t seen = 0;
	uint32_t w = 0, h = 0;
	present_frame();
	g_dueNext = -1;
	CHECK(pshx_present_take_start(&seen, a.p, a.n, 64 * 4, 64, 32) == PSHX_TAKE_STARTED);
	for (int i = 0; i < 50; i++) {
		CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_PENDING);
		gpu_tick();
	}
	CHECK(g_disp.use == 1 && g_disp.held[g_disp.front]);
	g_lost = true;
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_FAILED);
	CHECK(g_disp.use == 0);
	present_frame();
	/* the fence never signalled: nothing reused (no reset, no submit) */
	CHECK(pshx_present_take_start(&seen, a.p, a.n, 64 * 4, 64, 32) == PSHX_TAKE_NONE);
	g_lost = false;
	CHECK(pshx_present_take_start(&seen, a.p, a.n, 64 * 4, 64, 32) == PSHX_TAKE_NONE);
	/* once it reads signalled, copies go on */
	g_queue.front().due = 0;
	gpu_tick();
	g_dueNext = 0;
	CHECK(pshx_present_take_start(&seen, a.p, a.n, 64 * 4, 64, 32) == PSHX_TAKE_STARTED);
	gpu_tick();
	CHECK(pshx_present_take_poll(&seen, &w, &h) == PSHX_TAKE_DONE);
	pshx_present_forget(a.p);
}

/* ---- the title's side (ogbox_take.h), as fetchPicture and buildCommandBuffer use it ---------- */
/* kStaging: the staging buffers (ogbox_main.cpp: ogbox::kTakeStaging, one more than the frame slots; 2, the
 * frame slots alone, as in FF, left none free every other pass while the GPU was slow) */
template <size_t kStaging>
static void test_title(unsigned seed, bool hostPtr)
{
	std::printf("the title's passes (seed %u, %s, %zu staging buffers)\n", seed, hostPtr ? "imported" : "through the CPU",
		kStaging);
	g_hvk.caps.host_ptr = hostPtr;
	std::mt19937 rng(seed);
	constexpr size_t kFrames = 2;
	std::vector<std::unique_ptr<Pages>> staging;
	for (size_t i = 0; i < kStaging; i++) {
		staging.push_back(std::make_unique<Pages>(128 * 64 * 4));
	}
	struct TitleFrame {
		bool busy{ false };
		int reads{ -1 };   // the staging buffer it reads
		int left{ 0 };     // passes until its fence signals
	} frames[kFrames];
	ogbox::TakeAsync<kStaging> take;
	uint64_t seen = 0, lastSwap = 0;
	int pictureBuffer = -1;
	uint32_t rowW = 0;
	unsigned pictures = 0, passes = 4000;
	uint64_t keptOld = 0;
	for (uint64_t pass = 1; pass <= passes; pass++) {
		const size_t slot = pass % kFrames;
		/* the GPUs move on; the game presents now and then; a resize now and then */
		gpu_tick();
		for (auto &f : frames) {
			if (f.busy && --f.left <= 0) {
				f.busy = false;
			}
		}
		if (rng() % 3) {
			present_frame();
		}
		if (rng() % 400 == 0) {
			const uint32_t w = rng() % 2 ? 128 : 64, h = rng() % 2 ? 64 : 32;
			if (try_resize(w, h)) {
				present_frame();
			}
		}
		/* prepareFrame: this slot's fence waited */
		frames[slot].busy = false;
		uint32_t w = 0, h = 0, fw = 0, fh = 0;
		hle_disp_size(&w, &h);
		g_dueNext = static_cast<int>(rng() % 4);
		const int got = take.pass(pass, static_cast<double>(pass) / 120.0, pictureBuffer, static_cast<int>(slot),
			[&]() { return pshx_present_take_poll(&seen, &fw, &fh); },
			[&](size_t i) {
				/* the copy goes only where no title frame on the GPU reads (the CPU's picture: checked below) */
				for (auto &f : frames) {
					CHECK(!(f.busy && f.reads == static_cast<int>(i)));
				}
				rowW = w;
				return pshx_present_take_start(&seen, staging[i]->p, staging[i]->n, w * 4, w, h);
			},
			[&](int k) { return !frames[k].busy; });
		CHECK(g_queue.size() <= 1);
		if (take.target >= 0) {
			CHECK(g_disp.use == 1);   // the display's use held across passes while a copy is in flight
		} else {
			CHECK(g_disp.use == 0 && g_queue.empty());
		}
		if (got >= 0) {
			/* a new picture: the frame its copy started with, whole, newer than the last */
			const uint32_t swap = static_cast<uint32_t>(seen);
			CHECK(swap > lastSwap || (seen >> 32) != 0);
			lastSwap = swap;
			CHECK(fw == rowW);
			CHECK(frame_is(staging[got]->p, swap, fw, fh, rowW * 4));
			CHECK(!gpu_writes(staging[got]->p, staging[got]->n));
			pictureBuffer = got;
			pictures++;
			/* buildCommandBuffer: the upload reads it until this slot's fence */
			take.reading(static_cast<size_t>(got), static_cast<int>(slot));
			frames[slot] = { true, got, 1 + static_cast<int>(rng() % 3) };
		} else {
			frames[slot] = { true, -1, 1 + static_cast<int>(rng() % 3) };
		}
		/* the CPU's reads of the picture (a screenshot, a save state's) never meet a copy */
		if (pictureBuffer >= 0) {
			CHECK(!gpu_writes(staging[pictureBuffer]->p, staging[pictureBuffer]->n));
		}
		keptOld = take.keptOld;
	}
	std::printf("  %u passes, %u pictures, %llu copies (same pass %llu, next %llu, later %llu), %llu passes kept the "
		"previous picture, %llu found no buffer free, %u resizes (%u waited for a copy); the oldest %llu passes\n",
		passes, pictures, static_cast<unsigned long long>(take.started), static_cast<unsigned long long>(take.samePass),
		static_cast<unsigned long long>(take.nextPass), static_cast<unsigned long long>(take.laterPass),
		static_cast<unsigned long long>(keptOld), static_cast<unsigned long long>(take.noBuffer), g_disp.resizes,
		g_disp.resizesWaited, static_cast<unsigned long long>(take.ageWorstPasses));
	CHECK(pictures > passes / 4);
	CHECK(take.samePass > 0 && take.nextPass > 0 && take.laterPass > 0 && take.keptOld > 0);
	CHECK(take.ageWorstPasses <= 4);   // (the copies' latency, 0..3 ticks)
	if (kStaging > kFrames) {
		CHECK(take.noBuffer == 0);     // one always free: the shown picture's and the other slot's at most taken
	}
	gpu_finish();
	uint32_t fw, fh;
	pshx_present_take_poll(&seen, &fw, &fh);
	for (auto &s : staging) {
		pshx_present_forget(s->p);
	}
}

int main()
{
	setup();
	test_calls(true);
	test_calls(false);
	test_hung();
	for (unsigned seed = 1; seed <= 3; seed++) {
		test_title<2>(seed, true);
		test_title<ogbox::kTakeStaging>(seed, true);
		test_title<ogbox::kTakeStaging>(seed + 10, false);
	}
	pshx_present_shutdown();
	if (g_fails) {
		std::printf("present: %d checks FAILED\n", g_fails);
		return 1;
	}
	std::printf("present: all checks passed\n");
	return 0;
}
