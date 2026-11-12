#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.9: Quincunx, the original Xbox's own anti-aliasing (NVIDIA's, the NV2A's): each screen
// pixel is its middle at a half and its four corners at an eighth each. A corner is shared by four
// pixels, so the filter reaches past the pixel: edges come out very smooth and textures soft, as on
// the console. Here the samples come from a picture rendered larger than the screen (the internal
// resolution above it): the middle is everything the pixel covers (as ssaa.frag averages it), each
// corner the picture around that corner, a texel's width or two.
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (push_constant) uniform PC { vec4 metrics; } pc;   // the viewport's w, h in .zw
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;
void main()
{
	vec2 tex = vec2(textureSize(u_tex, 0));
	vec2 ratio = max(tex / max(pc.metrics.zw, vec2(1.0)), vec2(1.0));   // texels per screen pixel
	ivec2 n = min(ivec2(ceil(ratio * 0.5)), ivec2(4));
	vec2 step = ratio / vec2(n) / tex;
	vec2 start = v_uv - 0.5 * ratio / tex + 0.5 * step;
	vec3 middle = vec3(0.0);
	for (int y = 0; y < n.y; y++) {
		for (int x = 0; x < n.x; x++) {
			middle += textureLod(u_tex, start + step * vec2(x, y), 0.0).rgb;
		}
	}
	middle /= float(n.x * n.y);
	vec2 h = 0.5 / pc.metrics.zw;                                          // half a screen pixel
	vec3 corners = textureLod(u_tex, v_uv + vec2(-h.x, -h.y), 0.0).rgb + textureLod(u_tex, v_uv + vec2(h.x, -h.y), 0.0).rgb +
		textureLod(u_tex, v_uv + vec2(-h.x, h.y), 0.0).rgb + textureLod(u_tex, v_uv + vec2(h.x, h.y), 0.0).rgb;
	o = vec4(0.5 * middle + 0.125 * corners, 1.0);
}
