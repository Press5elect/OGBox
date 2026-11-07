#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.6: Bilinear (Sharp), the upscaling filter: each texel drawn as a block the whole times
// it fits on the screen, with one bilinear sample only across the seams between blocks, so the
// picture stays crisp without the shimmer of nearest-neighbour scaling. The picture's place on
// the screen (the viewport) in the push constant's .zw, as ssaa.frag has it.
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (push_constant) uniform PC { vec4 metrics; } pc;
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;
void main()
{
	vec2 size = vec2(textureSize(u_tex, 0));
	vec2 scale = max(floor(pc.metrics.zw / size), vec2(1.0));
	vec2 texel = v_uv * size;
	vec2 centreDist = fract(texel) - 0.5;
	vec2 region = 0.5 - 0.5 / scale;
	vec2 f = (centreDist - clamp(centreDist, -region, region)) * scale + 0.5;
	o = vec4(textureLod(u_tex, (floor(texel) + f) / size, 0.0).rgb, 1.0);
}
