#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.9: HDR output. The frame drawn as for an SDR screen (the picture, and the menus over
// it in a layer of their own) made into HDR10's pixels for the framebuffers VideoOut reads as PQ
// with BT.2020 primaries (ps5/patches/mesa-27/0019), as VLC-PS5 does for its own SDR menus
// (shaders/ui_pq.frag: the PQ curve of SMPTE ST 2084, BT.709's colours in BT.2020's, the
// graphics white of ITU-R BT.2408 at 203 nits).
//  - the SDR values decoded with a 2.2 gamma (as an SDR TV shows them), then BT.709 to BT.2020;
//  - white at metrics.x nits (the setting; BT.2408's 203 by default), the menus exactly so;
//  - the game's picture (metrics.w 1) with its highlights raised: linear luminance above 0.5 rises
//    on t^2 to metrics.y nits at full white, with the slope and value of the straight line at the
//    knee (no step), and every colour scaled by its luminance's gain, so hues keep and no channel
//    passes the peak; metrics.y at or below the white: none.
// metrics.z 1: the layers are sampled through sRGB views (already linear).
layout (set = 0, binding = 0) uniform sampler2D u_picture;
layout (set = 0, binding = 1) uniform sampler2D u_ui;     // premultiplied (ImGui's blending over clear 0)
layout (push_constant) uniform PC { vec4 metrics; } pc;
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;

const float M1 = 0.1593017578125, M2 = 78.84375;
const float C1 = 0.8359375, C2 = 18.8515625, C3 = 18.6875;
const float KNEE = 0.5;

/* BT.709 to BT.2020 (ITU-R BT.2087), columns as GLSL takes them */
const mat3 BT709_TO_2020 = mat3(
	0.6274, 0.0691, 0.0164,
	0.3293, 0.9195, 0.0880,
	0.0433, 0.0114, 0.8956);

vec3 pq(vec3 nits)
{
	vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(M1));
	return pow((C1 + C2 * y) / (1.0 + C3 * y), vec3(M2));
}

vec3 decode(vec3 c)
{
	return pc.metrics.z > 0.5 ? c : pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
	ivec2 at = ivec2(gl_FragCoord.xy);
	vec3 picture = texelFetch(u_picture, at, 0).rgb;
	vec4 ui = texelFetch(u_ui, at, 0);
	float white = max(pc.metrics.x, 1.0);
	float gain = 1.0;
	if (pc.metrics.w > 0.5 && pc.metrics.y > white) {
		float l = dot(decode(picture), vec3(0.2126, 0.7152, 0.0722));
		if (l > KNEE) {
			float t = (min(l, 1.0) - KNEE) / (1.0 - KNEE);
			gain = (l + (pc.metrics.y / white - 1.0) * t * t) / l;
		}
	}
	/* the menus over the picture as an SDR screen blends them (in its encoded values), the gain
	 * kept off them in proportion to their cover */
	vec3 sdr = ui.rgb + picture * (1.0 - ui.a);
	vec3 light = decode(sdr) * mix(gain, 1.0, ui.a);
	o = vec4(pq(max(BT709_TO_2020 * light, vec3(0.0)) * white), 1.0);
}
