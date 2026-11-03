#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.58: SSAA's downsampling: a picture rendered larger than its place on the screen
// (the resolution scale above the screen's) averaged over every texel a screen pixel covers,
// where one bilinear tap (present.frag) reads only the four nearest and lets the rest alias.
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (push_constant) uniform PC { vec4 metrics; } pc;   // the viewport's w, h in .zw
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;
void main()
{
	vec2 tex = vec2(textureSize(u_tex, 0));
	vec2 ratio = max(tex / max(pc.metrics.zw, vec2(1.0)), vec2(1.0));   // texels per screen pixel
	ivec2 n = min(ivec2(ceil(ratio * 0.5)), ivec2(4));                   // bilinear taps, 2 texels each
	vec2 step = ratio / vec2(n) / tex;
	vec2 start = v_uv - 0.5 * ratio / tex + 0.5 * step;
	vec3 sum = vec3(0.0);
	for (int y = 0; y < n.y; y++) {
		for (int x = 0; x < n.x; x++) {
			sum += textureLod(u_tex, start + step * vec2(x, y), 0.0).rgb;
		}
	}
	o = vec4(sum / float(n.x * n.y), 1.0);
}
