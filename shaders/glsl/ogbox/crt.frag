#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.9: the CRT look: the game's picture as a TV of the time showed it, drawn to the screen
// in place of the scaling filters. Each of the game's own lines (metrics.x: 480 for most games,
// whatever the internal resolution) is a beam, brighter lines wider (the tube's bloom); between
// them, the dark gap of a scanline. Strong adds an aperture grille, red, green and blue stripes a
// screen pixel each. The beam's and the mask's loss of light is given back, so the picture keeps
// its brightness. metrics.y: 0.5 Light, 1 Strong; .zw the viewport's size.
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (push_constant) uniform PC { vec4 metrics; } pc;
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;

vec3 toLinear(vec3 c) { return c * c; }
vec3 toGamma(vec3 c) { return sqrt(max(c, vec3(0.0))); }

/* the line's colour at x: the game's pixel there, a little soft as a beam is */
vec3 lineColour(float lineV, float u, float pixelU)
{
	vec3 c = vec3(0.0);
	for (int i = 0; i < 4; i++) {
		c += toLinear(textureLod(u_tex, vec2(u + (float(i) - 1.5) * 0.33 * pixelU, lineV), 0.0).rgb);
	}
	return c * 0.25;
}

void main()
{
	vec2 tex = vec2(textureSize(u_tex, 0));
	float lines = max(pc.metrics.x, 1.0);
	float strength = clamp(pc.metrics.y, 0.0, 1.0);
	float pixelU = 1.0 / max(lines * tex.x / tex.y, 1.0);   // a game pixel's width (square pixels)
	float y = v_uv.y * lines - 0.5;                           // in lines, centres at whole numbers
	float y0 = floor(y);
	float f = y - y0;                                         // 0 at this line's centre, 1 at the next
	vec3 a = lineColour((y0 + 0.5) / lines, v_uv.x, pixelU);
	vec3 b = lineColour((y0 + 1.5) / lines, v_uv.x, pixelU);
	/* each beam a gaussian across the line, wider the brighter it is */
	vec3 wa = mix(vec3(0.22), vec3(0.34), sqrt(a));
	vec3 wb = mix(vec3(0.22), vec3(0.34), sqrt(b));
	vec3 beamA = exp(-0.5 * (f * f) / (wa * wa));
	vec3 beamB = exp(-0.5 * ((1.0 - f) * (1.0 - f)) / (wb * wb));
	vec3 lit = a * beamA + b * beamB;
	/* the light the scanlines take, given back (the beams' mean over a line ~ width * sqrt(2 pi)) */
	vec3 mean = (wa + wb) * 0.5 * 2.5066;
	vec3 scan = lit / max(mean, vec3(0.3));
	vec3 flat_ = mix(a, b, f);
	vec3 c = mix(flat_, scan, strength);
	if (strength > 0.75) {
		/* the aperture grille: one stripe of each colour a screen pixel, the others dimmed */
		int m = int(mod(gl_FragCoord.x, 3.0));
		vec3 mask = vec3(0.68);
		mask[m] = 1.0;
		c *= mask * 1.26;
	}
	o = vec4(clamp(toGamma(c), 0.0, 1.0), 1.0);
}
