#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.9: Debanding. Xbox games draw many gradients (skies, fog, lights in the dark) in 16-bit
// colour or from 16-bit textures, and even 32-bit ones show steps in dark gradients; at a high internal
// resolution the steps are wide bands. Here each pixel is compared with the average of four points
// around it, at four reaches up to about a band's width (pc.params.z texels) in random directions;
// wherever that average is within a band's step (pc.params.x) of the pixel, the pixel takes it, the
// farther reach last. A little triangular noise (pc.params.y) then hides what steps are left. Edges and
// detail differ from their surroundings by more than the step and are kept. Done at the picture's size,
// before it is scaled (checked on test gradients: 8-bit steps 3 times, 16-bit ones 6 times smaller).
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (push_constant) uniform PC { vec4 params; } pc;   // .x the step, .y the dither, .z the reach in texels
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main()
{
	vec2 texel = 1.0 / vec2(textureSize(u_tex, 0));
	vec3 c = textureLod(u_tex, v_uv, 0.0).rgb;
	vec3 r = c;
	float seed = hash(gl_FragCoord.xy);
	for (int k = 1; k <= 4; k++) {
		float dist = pc.params.z * float(k) / 4.0 * (0.5 + 0.5 * hash(gl_FragCoord.xy + float(k) * 17.0));
		float ang = 6.2831853 * fract(seed + float(k) * 0.618034);
		vec2 off = vec2(cos(ang), sin(ang)) * dist * texel;
		vec2 per = vec2(-off.y, off.x) * vec2(textureSize(u_tex, 0)).yx * texel;   // (at right angles, in texels)
		vec3 avg = (textureLod(u_tex, v_uv + off, 0.0).rgb + textureLod(u_tex, v_uv - off, 0.0).rgb +
			textureLod(u_tex, v_uv + per, 0.0).rgb + textureLod(u_tex, v_uv - per, 0.0).rgb) * 0.25;
		vec3 diff = abs(avg - c);
		if (max(diff.r, max(diff.g, diff.b)) <= pc.params.x) {
			r = avg;
		}
	}
	float noise = (hash(gl_FragCoord.xy + 0.37) + hash(gl_FragCoord.xy + 1.91) - 1.0) * pc.params.y;
	o = vec4(clamp(r + noise, 0.0, 1.0), 1.0);
}
