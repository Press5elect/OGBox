#version 450
//====================================================================================
//         Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
//                         SPDX-License-Identifier: BSD-3-Clause
//====================================================================================
// Snapdragon Game Super Resolution 1: Qualcomm's sgsr1_shader_mobile.frag (RGBA mode: green as
// luma), as hakuX uses it (hw/xbox/hle/vk/swapchain.c, k_fs_sgsr); OGBox 0.1.26: OGBox's
// presenter's input and output, ViewportInfo from the frame's size, no gamma ramp (the frame has
// it). OGBox's changes: Copyright (C) 2026 Press5elect, GPL-3.0-or-later; keep the attribution
// "OGBox by Press5elect" (NOTICE).

layout (set = 0, binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;

#define OperationMode 1
#define EdgeThreshold 8.0/255.0
#define EdgeSharpness 2.0

float fastLanczos2(float x)
{
	float wA = x - 4.0;
	float wB = x * wA - wA;
	wA *= wA;
	return wB * wA;
}

vec2 weightY(float dx, float dy, float c, float std)
{
	float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * std, 0.0, 1.0);
	float w = fastLanczos2(x);
	return vec2(w, w * c);
}

void main()
{
	const int mode = OperationMode;
	float edgeThreshold = EdgeThreshold;
	float edgeSharpness = EdgeSharpness;
	vec2 size = vec2(textureSize(u_tex, 0));
	vec4 ViewportInfo = vec4(1.0 / size, size);
	vec4 color;
	color.xyz = textureLod(u_tex, v_uv, 0.0).xyz;
	vec2 imgCoord = ((v_uv * ViewportInfo.zw) + vec2(-0.5, 0.5));
	vec2 imgCoordPixel = floor(imgCoord);
	vec2 coord = (imgCoordPixel * ViewportInfo.xy);
	vec2 pl = (imgCoord + (-imgCoordPixel));
	vec4 left = textureGather(u_tex, coord, mode);
	float edgeVote = abs(left.z - left.y) + abs(color[mode] - left.y) + abs(color[mode] - left.z);
	if (edgeVote > edgeThreshold) {
		coord.x += ViewportInfo.x;
		vec4 right = textureGather(u_tex, coord + vec2(ViewportInfo.x, 0.0), mode);
		vec4 upDown;
		upDown.xy = textureGather(u_tex, coord + vec2(0.0, -ViewportInfo.y), mode).wz;
		upDown.zw = textureGather(u_tex, coord + vec2(0.0, ViewportInfo.y), mode).yx;
		float mean = (left.y + left.z + right.x + right.w) * 0.25;
		left = left - vec4(mean);
		right = right - vec4(mean);
		upDown = upDown - vec4(mean);
		color.w = color[mode] - mean;
		float sum = (((((abs(left.x) + abs(left.y)) + abs(left.z)) + abs(left.w)) +
			(((abs(right.x) + abs(right.y)) + abs(right.z)) + abs(right.w))) +
			(((abs(upDown.x) + abs(upDown.y)) + abs(upDown.z)) + abs(upDown.w)));
		float std = 2.181818 / sum;
		vec2 aWY = weightY(pl.x, pl.y + 1.0, upDown.x, std);
		aWY += weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, std);
		aWY += weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, std);
		aWY += weightY(pl.x, pl.y - 2.0, upDown.w, std);
		aWY += weightY(pl.x + 1.0, pl.y - 1.0, left.x, std);
		aWY += weightY(pl.x, pl.y - 1.0, left.y, std);
		aWY += weightY(pl.x, pl.y, left.z, std);
		aWY += weightY(pl.x + 1.0, pl.y, left.w, std);
		aWY += weightY(pl.x - 1.0, pl.y - 1.0, right.x, std);
		aWY += weightY(pl.x - 2.0, pl.y - 1.0, right.y, std);
		aWY += weightY(pl.x - 2.0, pl.y, right.z, std);
		aWY += weightY(pl.x - 1.0, pl.y, right.w, std);
		float finalY = aWY.y / aWY.x;
		float maxY = max(max(left.y, left.z), max(right.x, right.w));
		float minY = min(min(left.y, left.z), min(right.x, right.w));
		finalY = clamp(edgeSharpness * finalY, minY, maxY);
		float deltaY = finalY - color.w;
		deltaY = clamp(deltaY, -23.0 / 255.0, 23.0 / 255.0);
		color.x = clamp((color.x + deltaY), 0.0, 1.0);
		color.y = clamp((color.y + deltaY), 0.0, 1.0);
		color.z = clamp((color.z + deltaY), 0.0, 1.0);
	}
	o = vec4(color.xyz, 1.0);
}
