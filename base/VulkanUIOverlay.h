/*
* UI overlay class using ImGui
*
* Copyright (C) 2017-2026 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <sstream>
#include <iomanip>

#include <vulkan/vulkan.h>
#include "VulkanTools.h"
#include "VulkanDebug.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"

#include "../external/imgui/imgui.h"

#if defined(VK_EXAMPLE_PS5)
#include <string>
// PS5: the title's theme for the overlay. The UI module (ps5/ui/overlay_theme.cpp)
// defines both; without it ps5/src/example_ps5.cpp's weak ones keep upstream's look.
void ps5StyleOverlay(ImGuiStyle &style);
bool ps5OverlayFont(std::string &path, float &size);
// OGBox 0.1.52: a second typeface for a second theme (io.Fonts->Fonts[1]); path holds the first's
bool ps5OverlayExtraFont(std::string &path, float &size);
// OGBox 0.0.3: the menus' typefaces at their sizes (after the others); dir: the assets' folder
void ps5OverlayMoreFonts(ImFontAtlas *atlas, const std::string &dir);
#endif

#if defined(__ANDROID__)
#include "VulkanAndroid.h"
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace vks
{
	class UIOverlay
	{
	public:
		vks::VulkanDevice* device{ nullptr };
		VkQueue queue{ VK_NULL_HANDLE };

		VkSampleCountFlagBits rasterizationSamples{ VK_SAMPLE_COUNT_1_BIT };
		uint32_t subpass{ 0 };

		struct Buffers {
			vks::Buffer vertexBuffer;
			vks::Buffer indexBuffer;
			int32_t vertexCount{ 0 };
			int32_t indexCount{ 0 };
		};
		std::vector<Buffers> buffers;
		uint32_t maxConcurrentFrames{ 0 };
		uint32_t currentBuffer{ 0 };

		std::vector<VkPipelineShaderStageCreateInfo> shaders;

		VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
		VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
		VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
		VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
		VkPipeline pipeline{ VK_NULL_HANDLE };
		// OGBox 0.0.9: the same, its alpha the menus' cover (src + dst * (1 - src)), for HDR output's menu layer
		// (ogbox_main.cpp resolveHdr): drawn with it while coverage is set. pipeline's alpha stays as it was.
		VkPipeline pipelineCoverage{ VK_NULL_HANDLE };
		bool coverage{ false };

		VkDeviceMemory fontMemory{ VK_NULL_HANDLE };
		VkImage fontImage{ VK_NULL_HANDLE };
		VkImageView fontView{ VK_NULL_HANDLE };
		VkSampler sampler{ VK_NULL_HANDLE };

		struct PushConstBlock {
			glm::vec2 scale;
			glm::vec2 translate;
		} pushConstBlock;

		// OGBox 0.1.45: images ImGui::Image draws (their descriptor set is the ImTextureID)
		static constexpr uint32_t kUserTextures = 96;
		VkDescriptorSet addTexture(VkSampler sampler, VkImageView view);
		void freeTexture(VkDescriptorSet set);
		bool visible{ true };
		float scale{ 1.0f };

		VkRenderPass renderPass{ VK_NULL_HANDLE };
		VkFormat colorFormat{ VK_FORMAT_UNDEFINED };
		VkFormat depthFormat{ VK_FORMAT_UNDEFINED };

		UIOverlay();
		~UIOverlay();

		void prepare();

		void update(uint32_t currentBuffer);
		void draw(const VkCommandBuffer commandBuffer, uint32_t currentBuffer);
		void resize(uint32_t width, uint32_t height);

		void freeResources();

		bool header(const char* caption);
		bool checkBox(const char* caption, bool* value);
		bool checkBox(const char* caption, int32_t* value);
		bool radioButton(const char* caption, bool value);
		bool inputFloat(const char* caption, float* value, float step, uint32_t precision);
		bool sliderFloat(const char* caption, float* value, float min, float max);
		bool sliderInt(const char* caption, int32_t* value, int32_t min, int32_t max);
		bool comboBox(const char* caption, int32_t* itemindex, std::vector<std::string> items);
		bool button(const char* caption);
		bool colorPicker(const char* caption, float* color);
		void text(const char* formatstr, ...);
	};
}
