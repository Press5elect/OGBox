#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.7: the start's logo, alive: it fades in (params.x, 0..1) and a band of light crosses it
// from left to right (params.y: the band's middle, in the logo's widths), on the logo's green only.

layout (set = 0, binding = 0) uniform sampler2D picture;
layout (push_constant) uniform Params { vec4 params; } pc;

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outFragColor;

void main()
{
	const vec3 c = texture(picture, inUV).rgb;
	// where the logo is (it stands on black): its brightest channel, eased in
	const float mask = smoothstep(0.03, 0.30, max(c.r, max(c.g, c.b)));
	// the band leans a little, as light coming from the top left
	const float d = inUV.x + (inUV.y - 0.5) * 0.35 - pc.params.y;
	const float band = exp(-d * d / (2.0 * 0.045 * 0.045));
	const float halo = exp(-d * d / (2.0 * 0.16 * 0.16)) * 0.25;
	const vec3 light = vec3(0.80, 1.0, 0.62) * (band * 0.75 + halo) * mask;
	outFragColor = vec4((c + light) * pc.params.x, 1.0);
}
