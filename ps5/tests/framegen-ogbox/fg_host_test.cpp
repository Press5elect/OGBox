/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.7: ogbox_framegen.cpp's four methods on the PC, on a software Vulkan driver (lavapipe): two
 * frames of a moving square go in, the frame at t = 0.5 comes out and is read back. It has to be nearer
 * the scene at that moment than either frame, and t = 1 has to give the newest frame as it is.
 *   fg_host_test <shaders/glsl folder>
 */
#include "ogbox_framegen.h"

#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void pshakux_log(const char *format, ...)
{
	va_list a;
	va_start(a, format);
	std::vfprintf(stderr, format, a);
	std::fputc('\n', stderr);
	va_end(a);
}

#define VKC(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { std::fprintf(stderr, "%s: %d\n", #x, (int)r_); std::exit(2); } } while (0)

static const uint32_t W = 640, H = 480;

// A smooth textured background that stays, and a textured square whose left edge is at x.
static void scene(std::vector<uint8_t> &px, float x)
{
	px.assign(W * H * 4, 0);
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t i = 0; i < W; i++) {
			uint8_t *p = &px[(y * W + i) * 4];
			float v = 110.0f + 50.0f * std::sin(i * 0.031f + std::sin(y * 0.017f) * 2.0f) * std::cos(y * 0.043f + i * 0.007f) +
				22.0f * std::sin(i * 0.19f + y * 0.11f);
			if (i >= x && i < x + 120 && y >= 200 && y < 320) {
				v = 170.0f + 60.0f * std::sin((i - x) * 0.23f) * std::cos(y * 0.21f);
			}
			v = std::fmax(0.0f, std::fmin(255.0f, v));
			p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v * 0.9f); p[2] = static_cast<uint8_t>(v * 0.7f); p[3] = 255;
		}
	}
}

static double mse(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
	double s = 0;
	for (size_t i = 0; i < a.size(); i++) {
		if (i % 4 == 3) continue;
		double d = double(a[i]) - double(b[i]);
		s += d * d;
	}
	return s / (a.size() * 0.75);
}

int main(int argc, char **argv)
{
	if (argc < 2) { std::fprintf(stderr, "usage: fg_host_test <shaders/glsl folder>\n"); return 2; }
	VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.pApplicationInfo = &app;
	VkInstance inst;
	VKC(vkCreateInstance(&ici, nullptr, &inst));
	uint32_t n = 1;
	VkPhysicalDevice phys;
	VKC(vkEnumeratePhysicalDevices(inst, &n, &phys));
	VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	f13.dynamicRendering = VK_TRUE;
	f13.synchronization2 = VK_TRUE;
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	qci.queueFamilyIndex = 0; qci.queueCount = 1; qci.pQueuePriorities = &prio;
	VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dci.pNext = &f13; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
	VkDevice dev;
	VKC(vkCreateDevice(phys, &dci, nullptr, &dev));
	VkQueue queue;
	vkGetDeviceQueue(dev, 0, 0, &queue);
	VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkCommandPool pool;
	VKC(vkCreateCommandPool(dev, &pci, nullptr, &pool));
	VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
	sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VkSampler sampler;
	VKC(vkCreateSampler(dev, &sci, nullptr, &sampler));

	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(phys, &mp);
	auto memtype = [&](uint32_t bits, VkMemoryPropertyFlags want) {
		for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
			if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
		std::exit(3);
	};
	const VkDeviceSize bytes = W * H * 4;
	auto buffer = [&](VkBufferUsageFlags usage, VkBuffer &b, void **map) {
		VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bi.size = bytes; bi.usage = usage;
		VKC(vkCreateBuffer(dev, &bi, nullptr, &b));
		VkMemoryRequirements r;
		vkGetBufferMemoryRequirements(dev, b, &r);
		VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		ai.allocationSize = r.size;
		ai.memoryTypeIndex = memtype(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkDeviceMemory m;
		VKC(vkAllocateMemory(dev, &ai, nullptr, &m));
		VKC(vkBindBufferMemory(dev, b, m, 0));
		VKC(vkMapMemory(dev, m, 0, VK_WHOLE_SIZE, 0, map));
	};
	VkBuffer up, down;
	void *upMap, *downMap;
	buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, up, &upMap);
	buffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, down, &downMap);

	VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_SRGB; ii.extent = { W, H, 1 };
	ii.mipLevels = ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
	ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	VkImage picture;
	VKC(vkCreateImage(dev, &ii, nullptr, &picture));
	{
		VkMemoryRequirements r;
		vkGetImageMemoryRequirements(dev, picture, &r);
		VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		ai.allocationSize = r.size; ai.memoryTypeIndex = memtype(r.memoryTypeBits, 0);
		VkDeviceMemory m;
		VKC(vkAllocateMemory(dev, &ai, nullptr, &m));
		VKC(vkBindImageMemory(dev, picture, m, 0));
	}

	std::vector<uint8_t> s0, s1, mid, got;
	scene(s0, 200); scene(s1, 240); scene(mid, 220);
	int failed = 0;
	const struct { const char *name; ogbox::FgMethod m; } methods[] = { { "own", ogbox::FgMethod::Own },
		{ "flow", ogbox::FgMethod::Flow }, { "neural", ogbox::FgMethod::Neural }, { "blend", ogbox::FgMethod::Blend } };
	for (const auto &method : methods) {
		ogbox::FrameGen fg;
		ogbox::FrameGen::Init in;
		in.physical = phys; in.device = dev; in.linear = sampler; in.shaderDir = argv[1]; in.slots = 2; in.quality = 1;
		if (!fg.create(in, method.m, W, H)) {
			std::printf("%-7s FAILED to create: %s\n", method.name, fg.error().c_str());
			failed++;
			continue;
		}
		VkCommandBuffer cmd;
		VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
		VKC(vkAllocateCommandBuffers(dev, &cai, &cmd));
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		auto run = [&](const std::vector<uint8_t> &frame, float t, std::vector<uint8_t> &out) {
			std::memcpy(upMap, frame.data(), bytes);
			VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
			VKC(vkBeginCommandBuffer(cmd, &bi));
			fg.push(cmd, up);
			fg.show(cmd, 0, t, picture, layout);
			layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			b.oldLayout = layout; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = picture;
			b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
			VkBufferImageCopy c{};
			c.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			c.imageExtent = { W, H, 1 };
			vkCmdCopyImageToBuffer(cmd, picture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, down, 1, &c);
			layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			VKC(vkEndCommandBuffer(cmd));
			VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
			si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
			VKC(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
			VKC(vkQueueWaitIdle(queue));
			VKC(vkResetCommandBuffer(cmd, 0));
			out.assign(static_cast<uint8_t *>(downMap), static_cast<uint8_t *>(downMap) + bytes);
		};
		std::vector<uint8_t> first, second;
		run(s0, 1.0f, first);
		run(s1, 0.5f, got);                      // the picture between s0 and s1
		const double toMid = mse(got, mid), toOld = mse(got, s0), toNew = mse(got, s1);
		const bool blend = method.m == ogbox::FgMethod::Blend;
		const bool ok = fg.havePair() && (blend ? (toMid < 1000.0) : (toMid < toOld && toMid < toNew));
		std::printf("%-7s made: error to the scene at that moment %.1f, to the older %.1f, to the newer %.1f  %s\n", method.name,
			toMid, toOld, toNew, ok ? "ok" : "FAILED");
		failed += ok ? 0 : 1;
		// (the first call, t = 1, gave the newest frame as it was)
		const bool same = mse(first, s0) < 0.5;
		std::printf("%-7s t = 1 gave the newest frame as it is: %s\n", method.name, same ? "ok" : "FAILED");
		failed += same ? 0 : 1;
		fg.destroy();
	}
	std::printf("framegen on the PC: %s\n", failed ? "FAILED" : "all ok");
	return failed ? 1 : 0;
}
