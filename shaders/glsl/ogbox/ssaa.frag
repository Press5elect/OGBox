#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.58: SSAA's downsampling: a picture rendered larger than its place on the screen
// (the resolution scale above the screen's) averaged over every texel a screen pixel covers,
// where one bilinear tap (present.frag) reads only the four nearest and lets the rest alias.
// OGBox 0.0.9: metrics.x 1: Sharp, a Lanczos-2 filter as wide as two screen pixels: the texels
// weighted by their distance from the pixel's centre (negative lobes past one pixel), not averaged
// flat. Measured against the even average (a port of both on test pictures): edges crisper (a 3%
// over- and undershoot), detail finer than a screen pixel can hold suppressed rather than kept as
// shimmer (moire); no more real detail than the average keeps.
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (push_constant) uniform PC { vec4 metrics; } pc;   // .x 1: sharp; the viewport's w, h in .zw
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;

const float PI = 3.14159265;

float lanczos2(float x)
{
	x = abs(x);
	if (x < 1e-4) {
		return 1.0;
	}
	if (x >= 2.0) {
		return 0.0;
	}
	float px = PI * x;
	return 2.0 * sin(px) * sin(px * 0.5) / (px * px);
}

void main()
{
	vec2 tex = vec2(textureSize(u_tex, 0));
	vec2 ratio = max(tex / max(pc.metrics.zw, vec2(1.0)), vec2(1.0));   // texels per screen pixel
	if (pc.metrics.x > 0.5) {
		/* the texels within two screen pixels of the pixel's centre, at most 12 a side */
		vec2 c = v_uv * tex - 0.5;                       // the centre in texel coordinates
		vec2 reach = min(2.0 * ratio, vec2(6.0));
		ivec2 lo = ivec2(floor(c - reach)) + 1, hi = ivec2(floor(c + reach));
		ivec2 size = ivec2(tex) - 1;
		vec3 sum = vec3(0.0);
		float wsum = 0.0;
		for (int y = lo.y; y <= hi.y; y++) {
			float wy = lanczos2((float(y) - c.y) / ratio.y);
			if (wy == 0.0) {
				continue;
			}
			for (int x = lo.x; x <= hi.x; x++) {
				float w = wy * lanczos2((float(x) - c.x) / ratio.x);
				sum += w * texelFetch(u_tex, clamp(ivec2(x, y), ivec2(0), size), 0).rgb;
				wsum += w;
			}
		}
		o = vec4(clamp(sum / max(wsum, 1e-4), 0.0, 1.0), 1.0);
		return;
	}
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
