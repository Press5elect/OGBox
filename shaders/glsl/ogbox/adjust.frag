#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.9: Brightness, Contrast and Colour (Settings > Graphics, a game's own), over the game's
// picture at its size before it is scaled (after SMAA and Debanding), on its display values:
//  - brightness: a power curve, out = in ^ params.x (2 ^ (-setting / 50): +50 is 0.5, -50 is 2): the
//    middle tones lighter or darker, black and white where they were, nothing clipped;
//  - contrast: about the middle grey, (c - 0.5) * params.y + 0.5 (1 + setting / 100);
//  - colour: away from or toward the pixel's grey (BT.709's luma), params.z (1 + setting / 50; 0 is
//    black and white).
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (push_constant) uniform PC { vec4 params; } pc;
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;

void main()
{
	vec3 c = texelFetch(u_tex, ivec2(gl_FragCoord.xy), 0).rgb;   // the same size: one texel a pixel
	c = pow(clamp(c, 0.0, 1.0), vec3(pc.params.x));
	c = (c - 0.5) * pc.params.y + 0.5;
	float grey = dot(c, vec3(0.2126, 0.7152, 0.0722));
	c = mix(vec3(grey), c, pc.params.z);
	o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
