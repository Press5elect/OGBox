/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * FSR1's two passes on the PC, on a software Vulkan driver (lavapipe), as ogbox_main.cpp's
 * buildCommandBuffer records them with FSR1's sharpening on: a small picture through fsr1.frag
 * (EASU) into an image of the screen's format and size where the picture goes (the rest cleared to
 * black), then rcas.frag (RCAS) from it into the screen at the same viewport and scissor. Checked:
 *  - EASU into the image is EASU into the screen (the pass before RCAS changes nothing of it);
 *  - every pixel of RCAS's result is AMD's FsrRcasF (ffx_fsr1.h, 32-bit, no denoise) done here on
 *    the read-back EASU image, with IEEE fmin/fmax where AMD's limiters divide by zero, within a code;
 *  - the letterbox stays black; Low < Medium < High in the edges' contrast, each above EASU alone;
 *  - a black picture and pure primaries (a channel 0 everywhere, another 255) give no NaN;
 *  - no validation-layer message (when the layer is installed).
 *   fsr1_host_test <shaders/glsl folder>
 */
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define VKC(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { std::fprintf(stderr, "%s: %d\n", #x, (int)r_); std::exit(2); } } while (0)

static const uint32_t W = 200, H = 160;           // the screen
static const uint32_t PW = 64, PH = 48;           // the picture, drawn 3x
static const int VX = 4, VY = 8, VW = 192, VH = 144;   // its place (a letterbox around it)

static VkInstance instance;
static VkPhysicalDevice gpu;
static VkDevice device;
static VkQueue queue;
static int layerMessages = 0;

static VKAPI_ATTR VkBool32 VKAPI_CALL onMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
	const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
	if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		std::printf("  validation: %s\n", data->pMessage);
		layerMessages++;
	}
	return VK_FALSE;
}

static uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags want)
{
	VkPhysicalDeviceMemoryProperties p;
	vkGetPhysicalDeviceMemoryProperties(gpu, &p);
	for (uint32_t i = 0; i < p.memoryTypeCount; i++) {
		if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & want) == want) {
			return i;
		}
	}
	std::fprintf(stderr, "no memory type\n");
	std::exit(2);
}

struct Image {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
};

static Image makeImage(VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage)
{
	Image im{};
	VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ci.imageType = VK_IMAGE_TYPE_2D;
	ci.format = format;
	ci.extent = { w, h, 1 };
	ci.mipLevels = 1;
	ci.arrayLayers = 1;
	ci.samples = VK_SAMPLE_COUNT_1_BIT;
	ci.tiling = VK_IMAGE_TILING_OPTIMAL;
	ci.usage = usage;
	VKC(vkCreateImage(device, &ci, nullptr, &im.image));
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(device, im.image, &req);
	VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VKC(vkAllocateMemory(device, &ai, nullptr, &im.memory));
	VKC(vkBindImageMemory(device, im.image, im.memory, 0));
	VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	vi.image = im.image;
	vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vi.format = format;
	vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VKC(vkCreateImageView(device, &vi, nullptr, &im.view));
	return im;
}

struct Buffer {
	VkBuffer buffer;
	VkDeviceMemory memory;
	void *mapped;
};

static Buffer makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
	Buffer b{};
	VkBufferCreateInfo ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	ci.size = size;
	ci.usage = usage;
	VKC(vkCreateBuffer(device, &ci, nullptr, &b.buffer));
	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, b.buffer, &req);
	VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VKC(vkAllocateMemory(device, &ai, nullptr, &b.memory));
	VKC(vkBindBufferMemory(device, b.buffer, b.memory, 0));
	VKC(vkMapMemory(device, b.memory, 0, size, 0, &b.mapped));
	return b;
}

static void barrier(VkCommandBuffer cmd, VkImage image, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkImageLayout from,
	VkImageLayout to, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
	VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	b.srcAccessMask = srcAccess;
	b.dstAccessMask = dstAccess;
	b.oldLayout = from;
	b.newLayout = to;
	b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = image;
	b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

static VkShaderModule shader(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	std::vector<char> code((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (code.empty()) {
		std::fprintf(stderr, "no %s\n", path.c_str());
		std::exit(2);
	}
	VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	ci.codeSize = code.size();
	ci.pCode = reinterpret_cast<const uint32_t *>(code.data());
	VkShaderModule m;
	VKC(vkCreateShaderModule(device, &ci, nullptr, &m));
	return m;
}

/* ---- AMD's FsrRcasF, literally (float, IEEE fmin/fmax where it meets 0 * inf) -------------------- */
static float prxMedRcp(float a)
{
	uint32_t u;
	std::memcpy(&u, &a, 4);
	u = 0x7ef19fffu - u;
	float b;
	std::memcpy(&b, &u, 4);
	return b * (-b * a + 2.0f);
}

/* img: B, G, R, A bytes, W x H; the result's R, G, B 0..255 */
static void rcasRef(const std::vector<uint8_t> &img, int x, int y, float con, double out[3])
{
	auto at = [&](int dx, int dy, int c) {
		const int xx = std::clamp(x + dx, 0, int(W) - 1), yy = std::clamp(y + dy, 0, int(H) - 1);
		return img[(size_t(yy) * W + size_t(xx)) * 4 + size_t(2 - c)] / 255.0f;   // c: 0 R, 1 G, 2 B
	};
	float b[3], d[3], e[3], f[3], h[3], lobeC[3];
	for (int c = 0; c < 3; c++) {
		b[c] = at(0, -1, c), d[c] = at(-1, 0, c), e[c] = at(0, 0, c), f[c] = at(1, 0, c), h[c] = at(0, 1, c);
		const float mn4 = std::fmin(std::fmin(std::fmin(b[c], d[c]), f[c]), h[c]);
		const float mx4 = std::fmax(std::fmax(std::fmax(b[c], d[c]), f[c]), h[c]);
		const float hitMin = std::fmin(mn4, e[c]) * (1.0f / (4.0f * mx4));
		const float hitMax = (1.0f - std::fmax(mx4, e[c])) * (1.0f / (4.0f * mn4 - 4.0f));
		lobeC[c] = std::fmax(-hitMin, hitMax);
	}
	const float limit = 0.25f - 1.0f / 16.0f;
	const float lobe = std::fmax(-limit, std::fmin(std::fmax(lobeC[0], std::fmax(lobeC[1], lobeC[2])), 0.0f)) * con;
	const float rcpL = prxMedRcp(4.0f * lobe + 1.0f);
	for (int c = 0; c < 3; c++) {
		out[c] = double((lobe * b[c] + lobe * d[c] + lobe * h[c] + lobe * f[c] + e[c]) * rcpL) * 255.0;
	}
}

/* the edges' contrast: the summed squared steps between neighbours inside the picture's place */
static double contrast(const std::vector<uint8_t> &img)
{
	double s = 0.0;
	for (int y = VY; y < VY + VH - 1; y++) {
		for (int x = VX; x < VX + VW - 1; x++) {
			for (int c = 0; c < 3; c++) {
				const double v = img[(size_t(y) * W + size_t(x)) * 4 + size_t(c)];
				const double r = img[(size_t(y) * W + size_t(x + 1)) * 4 + size_t(c)];
				const double u = img[(size_t(y + 1) * W + size_t(x)) * 4 + size_t(c)];
				s += (v - r) * (v - r) + (v - u) * (v - u);
			}
		}
	}
	return s;
}

int main(int argc, char **argv)
{
	const std::string dir = argc > 1 ? argv[1] : "shaders/glsl/";
	VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.pApplicationInfo = &app;
	const char *validation = "VK_LAYER_KHRONOS_validation";
	const char *debugExt = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	uint32_t layerCount = 0;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	std::vector<VkLayerProperties> layers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
	bool validate = false;
	for (const auto &l : layers) {
		validate = validate || std::strcmp(l.layerName, validation) == 0;
	}
	VkDebugUtilsMessengerCreateInfoEXT mci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
	mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
	mci.pfnUserCallback = onMessage;
	if (validate) {
		ici.enabledLayerCount = 1;
		ici.ppEnabledLayerNames = &validation;
		ici.enabledExtensionCount = 1;
		ici.ppEnabledExtensionNames = &debugExt;
		ici.pNext = &mci;   // (its messages for the whole instance's life)
	}
	std::printf("fsr1 on the PC: the validation layer %s\n", validate ? "on (any message fails the test)" : "not installed");
	VKC(vkCreateInstance(&ici, nullptr, &instance));
	uint32_t n = 1;
	VkResult er = vkEnumeratePhysicalDevices(instance, &n, &gpu);
	if (n == 0 || (er != VK_SUCCESS && er != VK_INCOMPLETE)) {
		std::printf("fsr1 on the PC: NOT RUN (no Vulkan device)\n");
		return 0;
	}
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(gpu, &props);
	std::printf("fsr1 on the PC: %s\n", props.deviceName);
	float priority = 1.0f;
	VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	qci.queueFamilyIndex = 0;
	qci.queueCount = 1;
	qci.pQueuePriorities = &priority;
	VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	f13.dynamicRendering = VK_TRUE;
	VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dci.pNext = &f13;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	VKC(vkCreateDevice(gpu, &dci, nullptr, &device));
	vkGetDeviceQueue(device, 0, 0, &queue);

	const VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;   // the console's swapchain's (and so rcasIn's)
	Image picture = makeImage(fmt, PW, PH, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	const VkImageUsageFlags target = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	Image mid = makeImage(fmt, W, H, target), screen = makeImage(fmt, W, H, target);
	Buffer up = makeBuffer(PW * PH * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	Buffer downMid = makeBuffer(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT), downScreen = makeBuffer(W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

	VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;   // (the presenter's pointSampler; both shaders use texelFetch)
	sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	VkSampler sampler;
	VKC(vkCreateSampler(device, &sci, nullptr, &sampler));
	VkDescriptorSetLayoutBinding binding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	VkDescriptorSetLayoutCreateInfo dlci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	dlci.bindingCount = 1;
	dlci.pBindings = &binding;
	VkDescriptorSetLayout setLayout;
	VKC(vkCreateDescriptorSetLayout(device, &dlci, nullptr, &setLayout));
	VkPushConstantRange push{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };   // the presenter's pipelineLayout
	VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &setLayout;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &push;
	VkPipelineLayout layout;
	VKC(vkCreatePipelineLayout(device, &plci, nullptr, &layout));
	VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
	VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	dpci.maxSets = 2;
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes = &ps;
	VkDescriptorPool pool;
	VKC(vkCreateDescriptorPool(device, &dpci, nullptr, &pool));
	VkDescriptorSet sets[2];
	const VkDescriptorSetLayout twoLayouts[2] = { setLayout, setLayout };
	VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	dsai.descriptorPool = pool;
	dsai.descriptorSetCount = 2;
	dsai.pSetLayouts = twoLayouts;
	VKC(vkAllocateDescriptorSets(device, &dsai, sets));
	const VkDescriptorSet fromPicture = sets[0], fromMid = sets[1];
	VkDescriptorImageInfo infos[2] = { { sampler, picture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ sampler, mid.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } };
	VkWriteDescriptorSet writes[2] = {};
	for (int i = 0; i < 2; i++) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = sets[i];
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[i].pImageInfo = &infos[i];
	}
	vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

	VkPipelineShaderStageCreateInfo stages[2] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, shader(dir + "ogbox/present.vert.spv"), "main", nullptr },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE, "main", nullptr },
	};
	VkPipelineVertexInputStateCreateInfo vin{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	vp.viewportCount = vp.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rs.cullMode = VK_CULL_MODE_NONE;
	rs.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = 0xF;
	VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	cb.attachmentCount = 1;
	cb.pAttachments = &cba;
	VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	ds.dynamicStateCount = 2;
	ds.pDynamicStates = dyn;
	VkPipelineRenderingCreateInfo rci{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	rci.colorAttachmentCount = 1;
	rci.pColorAttachmentFormats = &fmt;
	VkGraphicsPipelineCreateInfo gci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	gci.pNext = &rci;
	gci.stageCount = 2;
	gci.pStages = stages;
	gci.pVertexInputState = &vin;
	gci.pInputAssemblyState = &ia;
	gci.pViewportState = &vp;
	gci.pRasterizationState = &rs;
	gci.pMultisampleState = &ms;
	gci.pColorBlendState = &cb;
	gci.pDynamicState = &ds;
	gci.layout = layout;
	VkPipeline easu, rcas;
	stages[1].module = shader(dir + "ogbox/fsr1.frag.spv");
	VKC(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gci, nullptr, &easu));
	stages[1].module = shader(dir + "ogbox/rcas.frag.spv");
	VKC(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gci, nullptr, &rcas));

	VkCommandPoolCreateInfo cpci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkCommandPool cmdPool;
	VKC(vkCreateCommandPool(device, &cpci, nullptr, &cmdPool));
	VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cbai.commandPool = cmdPool;
	cbai.commandBufferCount = 1;
	VkCommandBuffer cmd;
	VKC(vkAllocateCommandBuffers(device, &cbai, &cmd));

	/* one frame: the picture up, then (con > 0) EASU into mid and RCAS from it into the screen, or
	 * (con 0) EASU into mid and straight into the screen; both read back */
	auto frame = [&](float con) {
		VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		VKC(vkResetCommandBuffer(cmd, 0));
		VKC(vkBeginCommandBuffer(cmd, &bi));
		VkBufferImageCopy pr{};
		pr.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		pr.imageExtent = { PW, PH, 1 };
		barrier(cmd, picture.image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		vkCmdCopyBufferToImage(cmd, up.buffer, picture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &pr);
		barrier(cmd, picture.image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		const VkViewport viewport{ float(VX), float(VY), float(VW), float(VH), 0, 1 };
		const VkRect2D scissor{ { VX, VY }, { uint32_t(VW), uint32_t(VH) } };
		auto pass = [&](const Image &into, VkPipeline pipe, VkDescriptorSet set, bool read) {
			barrier(cmd, into.image, read ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
			VkRenderingAttachmentInfo att{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			att.imageView = into.view;
			att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // black, as beginHdrLayer and beginDynamicRendering clear
			att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
			ri.renderArea = { { 0, 0 }, { W, H } };
			ri.layerCount = 1;
			ri.colorAttachmentCount = 1;
			ri.pColorAttachments = &att;
			vkCmdBeginRendering(cmd, &ri);
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
			const float metrics[4] = { con, 0.0f, viewport.width, viewport.height };
			vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(metrics), metrics);
			vkCmdDraw(cmd, 3, 1, 0, 0);
			vkCmdEndRendering(cmd);
			barrier(cmd, into.image, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		};
		pass(mid, easu, fromPicture, true);
		if (con > 0.0f) {
			pass(screen, rcas, fromMid, true);
		} else {
			pass(screen, easu, fromPicture, true);
		}
		VkBufferImageCopy region{};
		region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.imageExtent = { W, H, 1 };
		for (const auto &rb : { std::make_pair(mid.image, downMid.buffer), std::make_pair(screen.image, downScreen.buffer) }) {
			barrier(cmd, rb.first, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
			vkCmdCopyImageToBuffer(cmd, rb.first, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.second, 1, &region);
		}
		VKC(vkEndCommandBuffer(cmd));
		VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cmd;
		VKC(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
		VKC(vkQueueWaitIdle(queue));
	};
	auto bytes = [](const Buffer &b) {
		const uint8_t *p = static_cast<const uint8_t *>(b.mapped);
		return std::vector<uint8_t>(p, p + W * H * 4);
	};

	/* the pictures (B, G, R, A): a game's (edges, a gradient, a thin diagonal, colours, text-like
	 * dots), all black, and pure primaries in stripes */
	enum Kind { Game, Black, Primaries };
	auto fill = [&](Kind k) {
		uint8_t *p = static_cast<uint8_t *>(up.mapped);
		for (uint32_t y = 0; y < PH; y++) {
			for (uint32_t x = 0; x < PW; x++) {
				uint8_t *q = &p[(y * PW + x) * 4];
				uint8_t r = 0, g = 0, b = 0;
				if (k == Game) {
					const bool check = ((x / 6) + (y / 6)) & 1;
					r = uint8_t(x < 32 ? (check ? 220 : 40) : x * 4);
					g = uint8_t(y < 24 ? (check ? 200 : 30) : 255 - y * 5);
					b = uint8_t(x == y || x == y + 1 ? 250 : ((x * 7 + y * 3) % 13 == 0 ? 180 : 60));
				} else if (k == Primaries) {
					const uint32_t s = (x / 5) % 3;
					r = s == 0 ? 255 : 0;
					g = s == 1 ? 255 : 0;
					b = s == 2 && y > 20 ? 255 : 0;
				}
				q[0] = b, q[1] = g, q[2] = r, q[3] = 255;
			}
		}
	};

	int failed = 0;
	auto check = [&](const char *name, bool ok, const char *detail) {
		std::printf("  %-58s %s%s%s\n", name, ok ? "ok" : "FAIL", detail[0] ? ": " : "", detail);
		failed += ok ? 0 : 1;
	};
	char line[256];

	/* RCAS against AMD's maths over the read-back EASU image; the letterbox black */
	auto rcasCase = [&](const char *name, Kind k, float con, std::vector<uint8_t> *result) {
		fill(k);
		frame(con);
		const std::vector<uint8_t> easuImg = bytes(downMid), out = bytes(downScreen);
		double worst = 0.0;
		int bad = 0, outside = 0, nan = 0;
		for (int y = 0; y < int(H); y++) {
			for (int x = 0; x < int(W); x++) {
				const uint8_t *g = &out[(size_t(y) * W + size_t(x)) * 4];
				const bool in = x >= VX && x < VX + VW && y >= VY && y < VY + VH;
				if (!in) {
					outside += (g[0] | g[1] | g[2]) ? 1 : 0;
					continue;
				}
				double want[3];
				rcasRef(easuImg, x, y, con, want);
				for (int c = 0; c < 3; c++) {
					if (!std::isfinite(want[c])) {
						nan++;
						continue;
					}
					const double d = std::fabs(double(g[2 - c]) - want[c]);
					worst = std::max(worst, d);
					bad += d > 1.0 ? 1 : 0;
				}
			}
		}
		std::snprintf(line, sizeof(line), "worst %.2f codes, %d off by more than one, %d NaN in the reference, %d letterbox pixels lit",
			worst, bad, nan, outside);
		check(name, bad == 0 && outside == 0 && nan == 0, line);
		if (result) {
			*result = out;
		}
	};

	/* EASU into the image is EASU into the screen */
	fill(Game);
	frame(0.0f);
	{
		const std::vector<uint8_t> a = bytes(downMid), b = bytes(downScreen);
		check("EASU into RCAS's image = EASU into the screen", a == b, "");
		std::vector<uint8_t> easuAlone = b, low, medium, high;
		rcasCase("RCAS Low (2 stops), the game's picture", Game, std::exp2(-2.0f), &low);
		rcasCase("RCAS Medium (1 stop), the game's picture", Game, std::exp2(-1.0f), &medium);
		rcasCase("RCAS High (0.5 stop), the game's picture", Game, std::exp2(-0.5f), &high);
		const double c0 = contrast(easuAlone), c1 = contrast(low), c2 = contrast(medium), c3 = contrast(high);
		std::snprintf(line, sizeof(line), "EASU %.0f, Low %.0f, Medium %.0f, High %.0f", c0, c1, c2, c3);
		check("the edges' contrast: EASU < Low < Medium < High", c0 < c1 && c1 < c2 && c2 < c3, line);
	}
	rcasCase("RCAS Medium, an all-black picture", Black, 0.5f, nullptr);
	rcasCase("RCAS High, pure primaries (channels all 0 and all 255)", Primaries, std::exp2(-0.5f), nullptr);
	fill(Black);
	frame(0.5f);
	{
		const std::vector<uint8_t> out = bytes(downScreen);
		check("an all-black picture stays black", std::all_of(out.begin(), out.end(), [i = 0](uint8_t v) mutable {
			return (i++ % 4) == 3 || v == 0;
		}), "");
	}
	vkDeviceWaitIdle(device);
	if (validate) {
		std::snprintf(line, sizeof(line), "%d", layerMessages);
		check("no validation-layer messages", layerMessages == 0, line);
	}
	std::printf("fsr1 on the PC: %s\n", failed ? "FAILED" : "passed");
	return failed ? 1 : 0;
}
