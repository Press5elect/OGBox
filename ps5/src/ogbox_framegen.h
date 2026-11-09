/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.7: frame generation, the three methods of Crux2 (also by Press5elect):
 *   Own      Crux2's own eight-pass motion search (shaders/glsl/ogbox/framegen.glsl);
 *   Flow     AMD FidelityFX FSR 3 optical flow, with Crux2's composite over it (src/framegen/ps5_compute_fx.h);
 *   Neural   MihawkGen's trained network (third_party/mihawkgen, GPL-3.0-or-later, by Mihawk);
 *   Blend    MihawkGen's plain mix of the two pictures (a baseline).
 * A game picture goes in as it comes; between two pictures a third is made at a time t, from the
 * pictures alone. The caller owns when (src/framegen/ps5_framegen_pacer.h) and what the screen shows.
 * Everything is recorded into the caller's command buffer, on its queue, with barriers of its own.
 */
#ifndef OGBOX_FRAMEGEN_H
#define OGBOX_FRAMEGEN_H

#include <cstdint>
#include <memory>
#include <string>

namespace ogbox {

enum class FgMethod { Off, Own, Flow, Neural, Blend };
FgMethod fgMethodFromSetting(const std::string &value);

class FrameGen {
public:
	struct Init {
		VkPhysicalDevice physical = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		VkPipelineCache cache = VK_NULL_HANDLE;
		VkSampler linear = VK_NULL_HANDLE;   // linear, clamp to edge
		std::string shaderDir;               // ".../shaders/glsl/" (fg0.frag.spv ... in ogbox/)
		uint32_t slots = 3;                  // command buffers that may wait for the GPU at once (<= 8)
		uint32_t quality = 1;                // Flow: 0 performance, 1 balanced, 2 best
	};

	FrameGen();
	~FrameGen();
	FrameGen(const FrameGen &) = delete;
	FrameGen &operator=(const FrameGen &) = delete;

	/* Pictures of w x h with this method; false (and nothing kept) when it cannot be made: error(). */
	bool create(const Init &init, FgMethod method, uint32_t w, uint32_t h);
	void destroy();
	bool ready() const;
	uint32_t width() const;
	uint32_t height() const;
	FgMethod method() const;
	const std::string &error() const;

	/* The frame in the buffer (RGBA8, rows packed) becomes the newest. */
	void push(VkCommandBuffer cmd, VkBuffer frame);
	/* Two pictures are kept (a picture can be made). */
	bool havePair() const;
	/* The picture at t (0 the older, 1 the newest) is made and put into dst (a transfer destination
	 * image of this size; its layout is `dstLayout` and becomes SHADER_READ_ONLY). t >= 1 or no pair:
	 * the newest picture as it is. */
	void show(VkCommandBuffer cmd, uint32_t slot, float t, VkImage dst, VkImageLayout dstLayout);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace ogbox

#endif
