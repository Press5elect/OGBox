/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.7: frame generation (ogbox_framegen.h). The own method's order and sizes are Crux2's
 * (GSDevice::FrameGenPush and FrameGenMake in ps5/patches/pcsx2/ps5.patch), the other two are
 * called as Crux2 calls them (DoOpticalFlowMake, DoMihawkGenMake).
 */
#include "ogbox_framegen.h"
#include "platform/ps5_bootlog.h"

#include "ps5_compute_fx.h"
#include "framegen.h"   // MihawkGen (third_party/mihawkgen)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <vector>

namespace PS5MihawkGen {
const void *Model();   // src/framegen/ps5_mihawkgen_model.cpp
size_t ModelSize();
}

namespace ogbox {

FgMethod fgMethodFromSetting(const std::string &v)
{
	if (v == "own") return FgMethod::Own;
	if (v == "flow") return FgMethod::Flow;
	if (v == "neural") return FgMethod::Neural;
	if (v == "blend") return FgMethod::Blend;
	return FgMethod::Off;
}

namespace {

struct Img {
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	uint32_t w = 0, h = 0;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

constexpr uint32_t kPasses = 8;   // the own method's fragment passes (the side picture's 8 is Crux2's alone)

}   // namespace

struct FrameGen::Impl {
	Init init{};
	FgMethod method = FgMethod::Off;
	uint32_t w = 0, h = 0;
	std::string err;
	VkPhysicalDeviceMemoryProperties memProps{};

	// the pictures: the two kept (newest at `newest`) and the one made
	Img ring[2], made;
	uint32_t newest = 1, count = 0;
	bool searched = false;

	// own method
	Img pairs[5], moves[7], ones[3];
	VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
	VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
	VkPipeline pipes[kPasses] = {};
	VkShaderModule vert = VK_NULL_HANDLE;
	VkDescriptorPool pools[8] = {};

	// the others
	PS5ComputeFX::OpticalFlowFG flow;
	fg_context *mihawk = nullptr;

	bool fail(const char *what)
	{
		err = what;
		return false;
	}

	uint32_t memoryType(uint32_t bits) const
	{
		for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
			if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
				return i;
			}
		}
		for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
			if (bits & (1u << i)) {
				return i;
			}
		}
		return 0;
	}

	bool makeImage(Img &img, uint32_t iw, uint32_t ih, VkFormat format, VkImageUsageFlags usage)
	{
		VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		ci.imageType = VK_IMAGE_TYPE_2D;
		ci.format = format;
		ci.extent = { iw, ih, 1 };
		ci.mipLevels = 1;
		ci.arrayLayers = 1;
		ci.samples = VK_SAMPLE_COUNT_1_BIT;
		ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		ci.usage = usage;
		ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vkCreateImage(init.device, &ci, nullptr, &img.image) != VK_SUCCESS) {
			return fail("an image could not be made");
		}
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(init.device, img.image, &req);
		VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		ai.allocationSize = req.size;
		ai.memoryTypeIndex = memoryType(req.memoryTypeBits);
		if (vkAllocateMemory(init.device, &ai, nullptr, &img.memory) != VK_SUCCESS ||
			vkBindImageMemory(init.device, img.image, img.memory, 0) != VK_SUCCESS) {
			return fail("memory for an image could not be had");
		}
		VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		vi.image = img.image;
		vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vi.format = format;
		vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(init.device, &vi, nullptr, &img.view) != VK_SUCCESS) {
			return fail("an image view could not be made");
		}
		img.w = iw;
		img.h = ih;
		img.format = format;
		img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		return true;
	}

	void dropImage(Img &img)
	{
		if (img.view) vkDestroyImageView(init.device, img.view, nullptr);
		if (img.image) vkDestroyImage(init.device, img.image, nullptr);
		if (img.memory) vkFreeMemory(init.device, img.memory, nullptr);
		img = Img{};
	}

	// A barrier for everything before and after on the queue; the layout changed, if it does. Coarse on
	// purpose: this runs a few dozen times a picture, on pictures that are mostly small.
	void to(VkCommandBuffer cmd, Img &img, VkImageLayout layout)
	{
		VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		b.oldLayout = img.layout == layout ? layout : (img.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED : img.layout);
		b.newLayout = layout;
		b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = img.image;
		b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
			nullptr, 1, &b);
		img.layout = layout;
	}

	void everything(VkCommandBuffer cmd)
	{
		VkMemoryBarrier b{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0,
			nullptr, 0, nullptr);
	}

	// ---- the own method --------------------------------------------------------------------------
	VkShaderModule loadModule(const std::string &file)
	{
		std::ifstream in(init.shaderDir + "ogbox/" + file, std::ios::binary);
		if (!in) {
			return VK_NULL_HANDLE;
		}
		std::vector<char> code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (code.empty() || code.size() % 4) {
			return VK_NULL_HANDLE;
		}
		VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		ci.codeSize = code.size();
		ci.pCode = reinterpret_cast<const uint32_t *>(code.data());
		VkShaderModule m = VK_NULL_HANDLE;
		return vkCreateShaderModule(init.device, &ci, nullptr, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
	}

	bool makeOwn()
	{
		const auto half = [](uint32_t s) { return std::max((s + 1) / 2, 1u); };
		const VkImageUsageFlags rt = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		uint32_t pw = half(half(w)), ph = half(half(h));
		for (Img &p : pairs) {
			if (!makeImage(p, pw, ph, VK_FORMAT_R16G16B16A16_SFLOAT, rt)) return false;
			pw = half(pw);
			ph = half(ph);
		}
		static constexpr uint32_t pairOf[7] = { 4, 4, 4, 3, 2, 1, 1 };
		for (uint32_t i = 0; i < 7; i++) {
			if (!makeImage(moves[i], pairs[pairOf[i]].w, pairs[pairOf[i]].h, VK_FORMAT_R16G16B16A16_SFLOAT, rt)) return false;
		}
		for (Img &o : ones) {
			if (!makeImage(o, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, rt)) return false;
		}

		VkDescriptorSetLayoutBinding bindings[4] = {};
		for (uint32_t i = 0; i < 4; i++) {
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		lci.bindingCount = 4;
		lci.pBindings = bindings;
		if (vkCreateDescriptorSetLayout(init.device, &lci, nullptr, &setLayout) != VK_SUCCESS) {
			return fail("a descriptor layout could not be made");
		}
		const VkPushConstantRange push{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, 48 };
		VkPipelineLayoutCreateInfo pci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pci.setLayoutCount = 1;
		pci.pSetLayouts = &setLayout;
		pci.pushConstantRangeCount = 1;
		pci.pPushConstantRanges = &push;
		if (vkCreatePipelineLayout(init.device, &pci, nullptr, &pipeLayout) != VK_SUCCESS) {
			return fail("a pipeline layout could not be made");
		}
		vert = loadModule("present.vert.spv");
		if (!vert) {
			return fail("the screen triangle's shader is missing");
		}
		for (uint32_t s = 0; s < init.slots; s++) {
			VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * 24 };
			VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
			dpi.maxSets = 24;
			dpi.poolSizeCount = 1;
			dpi.pPoolSizes = &size;
			if (vkCreateDescriptorPool(init.device, &dpi, nullptr, &pools[s]) != VK_SUCCESS) {
				return fail("a descriptor pool could not be made");
			}
		}
		for (uint32_t pass = 0; pass < kPasses; pass++) {
			char name[32];
			std::snprintf(name, sizeof(name), "fg%u.frag.spv", pass);
			VkShaderModule frag = loadModule(name);
			if (!frag) {
				err = std::string(name) + " is missing";
				return false;
			}
			VkPipelineShaderStageCreateInfo stages[2] = {};
			stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr };
			stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };
			VkPipelineVertexInputStateCreateInfo vin{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
			VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
			vp.viewportCount = vp.scissorCount = 1;
			VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode = VK_CULL_MODE_NONE;
			rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth = 1.0f;
			VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			VkPipelineColorBlendAttachmentState att{};
			att.colorWriteMask = 0xF;
			VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
			cb.attachmentCount = 1;
			cb.pAttachments = &att;
			const VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
			ds.dynamicStateCount = 2;
			ds.pDynamicStates = dyn;
			const VkFormat format = pass == 7 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT;
			VkPipelineRenderingCreateInfo rci{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
			rci.colorAttachmentCount = 1;
			rci.pColorAttachmentFormats = &format;
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
			gci.layout = pipeLayout;
			const VkResult r = vkCreateGraphicsPipelines(init.device, init.cache, 1, &gci, nullptr, &pipes[pass]);
			vkDestroyShaderModule(init.device, frag, nullptr);
			if (r != VK_SUCCESS) {
				pipes[pass] = VK_NULL_HANDLE;
				char text[64];
				std::snprintf(text, sizeof(text), "the driver could not make pass %u", pass);
				err = text;
				return false;
			}
		}
		return true;
	}

	// One pass: `from` sampled (their first's size is the shader's source), `target` drawn whole.
	void pass(VkCommandBuffer cmd, uint32_t slot, uint32_t number, Img &target, std::initializer_list<Img *> from,
		float t = 0.0f, float how = 0.0f, float soft = 0.0f, float reach = 0.0f)
	{
		for (Img *src : from) {
			to(cmd, *src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		to(cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		ai.descriptorPool = pools[slot];
		ai.descriptorSetCount = 1;
		ai.pSetLayouts = &setLayout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		if (vkAllocateDescriptorSets(init.device, &ai, &set) != VK_SUCCESS) {
			return;
		}
		VkDescriptorImageInfo infos[4];
		VkWriteDescriptorSet writes[4];
		uint32_t n = 0;
		for (Img *src : from) {
			infos[n] = { init.linear, src->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			writes[n] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			writes[n].dstSet = set;
			writes[n].dstBinding = n;
			writes[n].descriptorCount = 1;
			writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[n].pImageInfo = &infos[n];
			n++;
		}
		vkUpdateDescriptorSets(init.device, n, writes, 0, nullptr);

		VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		color.imageView = target.view;
		color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		info.renderArea = { { 0, 0 }, { target.w, target.h } };
		info.layerCount = 1;
		info.colorAttachmentCount = 1;
		info.pColorAttachments = &color;
		vkCmdBeginRendering(cmd, &info);
		const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(target.w), static_cast<float>(target.h), 0.0f, 1.0f };
		const VkRect2D scissor{ { 0, 0 }, { target.w, target.h } };
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes[number]);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1, &set, 0, nullptr);
		const float dw = static_cast<float>(target.w), dh = static_cast<float>(target.h);
		const Img &first = **from.begin();
		const float sw = static_cast<float>(first.w), sh = static_cast<float>(first.h);
		const float constants[12] = { dw, dh, 1.0f / dw, 1.0f / dh, sw, sh, 1.0f / sw, 1.0f / sh, t, how, soft, reach };
		vkCmdPushConstants(cmd, pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), constants);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRendering(cmd);
	}

	void makeOwnPicture(VkCommandBuffer cmd, uint32_t slot, float time)
	{
		Img &older = ring[newest ^ 1], &newer = ring[newest];
		Img &first64 = moves[0], &move64 = moves[1], &spread = moves[2], &move32 = moves[3], &move16 = moves[4],
			&move8 = moves[5], &all = moves[6];
		Img &camera0 = ones[0], &camera = ones[1], &whole = ones[2];
		if (!searched) {
			pass(cmd, slot, 0, pairs[0], { &older, &newer });
			for (uint32_t i = 1; i < 5; i++) {
				pass(cmd, slot, 1, pairs[i], { &pairs[i - 1] });
			}
			pass(cmd, slot, 2, first64, { &pairs[4], &camera }, 0.0f, 0.0f, 1.0f, 4.0f);
			pass(cmd, slot, 3, camera0, { &first64, &camera }, 0.0f, 0.0f);
			pass(cmd, slot, 3, camera, { &first64, &camera0 }, 0.0f, 1.0f);
			pass(cmd, slot, 2, move64, { &pairs[4], &camera }, 0.0f, 1.0f, 1.0f, 4.0f);
			pass(cmd, slot, 4, spread, { &move64 });
			pass(cmd, slot, 4, move64, { &spread });
			pass(cmd, slot, 2, move32, { &pairs[3], &move64 }, 0.0f, 1.0f, 2.0f, 3.0f);
			pass(cmd, slot, 2, move16, { &pairs[2], &move32 }, 0.0f, 1.0f, 2.0f, 3.0f);
			pass(cmd, slot, 2, move8, { &pairs[1], &move16 }, 0.0f, 2.0f, 0.0f, 1.0f);
			pass(cmd, slot, 5, all, { &move8 });
			pass(cmd, slot, 6, whole, { &all, &move8 });
			searched = true;
		}
		pass(cmd, slot, 7, made, { &older, &newer, &all, &whole }, time);
	}

	void destroyOwn()
	{
		for (Img &p : pairs) dropImage(p);
		for (Img &m : moves) dropImage(m);
		for (Img &o : ones) dropImage(o);
		for (VkPipeline &p : pipes) {
			if (p) vkDestroyPipeline(init.device, p, nullptr);
			p = VK_NULL_HANDLE;
		}
		for (VkDescriptorPool &p : pools) {
			if (p) vkDestroyDescriptorPool(init.device, p, nullptr);
			p = VK_NULL_HANDLE;
		}
		if (pipeLayout) vkDestroyPipelineLayout(init.device, pipeLayout, nullptr);
		if (setLayout) vkDestroyDescriptorSetLayout(init.device, setLayout, nullptr);
		if (vert) vkDestroyShaderModule(init.device, vert, nullptr);
		pipeLayout = VK_NULL_HANDLE;
		setLayout = VK_NULL_HANDLE;
		vert = VK_NULL_HANDLE;
	}

	void destroyAll()
	{
		if (init.device) {
			vkDeviceWaitIdle(init.device);
		}
		destroyOwn();
		flow.Destroy();
		if (mihawk) {
			fg_context_destroy(mihawk);
			mihawk = nullptr;
		}
		for (Img &r : ring) dropImage(r);
		dropImage(made);
		method = FgMethod::Off;
		/* 0.0.9: the size goes too: width() 0 is "not made" (made again at the next frame), width() with
		 * method Off is "failed while running" (ogbox_main.cpp frameGenFor). Kept, a change in the quick menu
		 * (refreshFrameGen: destroy, then made again) read as a failure and frame generation went off. */
		w = h = 0;
		count = 0;
		searched = false;
		newest = 1;
	}
};

FrameGen::FrameGen() : impl(new Impl) {}
FrameGen::~FrameGen() { destroy(); }

bool FrameGen::ready() const { return impl->method != FgMethod::Off; }
uint32_t FrameGen::width() const { return impl->w; }
uint32_t FrameGen::height() const { return impl->h; }
FgMethod FrameGen::method() const { return impl->method; }
const std::string &FrameGen::error() const { return impl->err; }
bool FrameGen::havePair() const { return impl->count >= 2; }

void FrameGen::destroy()
{
	impl->destroyAll();
}

bool FrameGen::create(const Init &init, FgMethod method, uint32_t w, uint32_t h)
{
	Impl &d = *impl;
	d.destroyAll();
	d.init = init;
	d.init.slots = std::min(std::max(init.slots, 1u), 8u);
	d.err.clear();
	if (method == FgMethod::Off || w < 64 || h < 64) {
		d.err = "the picture is too small";
		return false;
	}
	d.w = w;
	d.h = h;
	vkGetPhysicalDeviceMemoryProperties(init.physical, &d.memProps);
	bool ok = d.makeImage(d.ring[0], w, h, VK_FORMAT_R8G8B8A8_UNORM,
				  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
		d.makeImage(d.ring[1], w, h, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT) &&
		d.makeImage(d.made, w, h, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	if (ok && method == FgMethod::Own) {
		ok = d.makeOwn();
	} else if (ok && method == FgMethod::Flow) {
		PS5ComputeFX::Device dev;
		dev.physicalDevice = init.physical;
		dev.device = init.device;
		dev.pipelineCache = init.cache;
		dev.framesInFlight = d.init.slots * 2;
		ok = d.flow.Create(dev, w, h, VK_FORMAT_R8G8B8A8_UNORM,
			static_cast<PS5ComputeFX::Quality>(std::min(init.quality, 2u)));
		if (!ok) {
			d.err = d.flow.LastError();
		}
	} else if (ok) {
		fg_create_info info = {};
		info.device = init.device;
		info.physical_device = init.physical;
		info.width = w;
		info.height = h;
		info.slots = d.init.slots;
		info.output_format = VK_FORMAT_R8G8B8A8_UNORM;
		char text[256] = {};
		VkResult r = fg_context_create(&info, &d.mihawk, text, sizeof(text));
		if (r == VK_SUCCESS && method == FgMethod::Neural) {
			r = fg_context_load_network(d.mihawk, PS5MihawkGen::Model(), PS5MihawkGen::ModelSize(), text, sizeof(text));
		}
		const fg_method want = method == FgMethod::Neural ? FG_METHOD_NEURAL : FG_METHOD_BLEND;
		if (r != VK_SUCCESS || !fg_method_available(d.mihawk, want)) {
			ok = false;
			d.err = std::string(text) + " (" + std::to_string(static_cast<int>(r)) + ")";
		}
	}
	if (!ok) {
		if (d.err.empty()) d.err = "unknown";
		const std::string why = d.err;
		d.destroyAll();
		d.err = why;
		return false;
	}
	d.method = method;
	static const char *names[] = { "off", "Crux2's own", "FSR 3 optical flow", "MihawkGen", "MihawkGen blend" };
	pshakux_log("framegen: %s works on %ux%u", names[static_cast<int>(method)], w, h);
	return true;
}

void FrameGen::push(VkCommandBuffer cmd, VkBuffer frame)
{
	Impl &d = *impl;
	d.everything(cmd);
	d.newest ^= 1;
	Img &into = d.ring[d.newest];
	d.to(cmd, into, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkBufferImageCopy copy{};
	copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copy.imageExtent = { d.w, d.h, 1 };
	vkCmdCopyBufferToImage(cmd, frame, into.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	d.to(cmd, into, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	d.count = std::min(d.count + 1, 2u);
	d.searched = false;
}

void FrameGen::show(VkCommandBuffer cmd, uint32_t slot, float t, VkImage dst, VkImageLayout dstLayout)
{
	Impl &d = *impl;
	d.everything(cmd);
	Img *src = &d.ring[d.newest];
	if (d.count >= 2 && t < 1.0f && d.method != FgMethod::Off) {
		slot = std::min(slot, d.init.slots - 1);
		Img &older = d.ring[d.newest ^ 1], &newer = d.ring[d.newest];
		d.to(cmd, older, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		d.to(cmd, newer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		switch (d.method) {
		case FgMethod::Own:
			vkResetDescriptorPool(d.init.device, d.pools[slot], 0);
			d.makeOwnPicture(cmd, slot, t);
			break;
		case FgMethod::Flow: {
			d.to(cmd, d.made, VK_IMAGE_LAYOUT_GENERAL);
			PS5ComputeFX::FrameGenInput in;
			in.previous = older.view;
			in.current = newer.view;
			in.output = d.made.view;
			in.t = t;
			in.showCurrentOnCut = true;
			if (!d.flow.Record(cmd, in)) {
				pshakux_log("framegen: optical flow failed (%s): the newest picture is shown", d.flow.LastError());
				d.method = FgMethod::Off;   // (the picture stays the newest from now on; settings bring it back)
			}
			break;
		}
		default: {
			d.to(cmd, d.made, VK_IMAGE_LAYOUT_GENERAL);
			fg_frame_pair pair = {};
			pair.prev_color = older.view;
			pair.next_color = newer.view;
			pair.t = t;
			fg_record_generate(d.mihawk, cmd, slot, d.method == FgMethod::Neural ? FG_METHOD_NEURAL : FG_METHOD_BLEND, &pair,
				d.made.view);
			break;
		}
		}
		if (d.method != FgMethod::Off) {
			src = &d.made;
		}
	}
	d.everything(cmd);
	d.to(cmd, *src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.oldLayout = dstLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED : dstLayout;
	b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = dst;
	b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
	VkImageCopy copy{};
	copy.srcSubresource = copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copy.extent = { d.w, d.h, 1 };
	vkCmdCopyImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
	b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
	// the picture kept for the next pair goes back to what the passes read; the made one to what writes it
	if (src == &d.made) {
		d.to(cmd, d.made, d.method == FgMethod::Own ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL);
	} else {
		d.to(cmd, *src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
}

} // namespace ogbox
