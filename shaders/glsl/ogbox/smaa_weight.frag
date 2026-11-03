#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.58: SMAA pass 2 of 3: blending weights from the edges, SMAA's area and search
// textures (AreaTex.h, SearchTex.h: linear and point sampled).
#extension GL_GOOGLE_include_directive : require
layout (push_constant) uniform PC { vec4 metrics; } pc;   // 1/w, 1/h, w, h of the picture
#define SMAA_RT_METRICS pc.metrics
#define SMAA_GLSL_4
#define SMAA_PRESET_HIGH
#define SMAA_INCLUDE_VS 0
#include "SMAA.hlsl"
layout (set = 0, binding = 0) uniform sampler2D u_edges;
layout (set = 0, binding = 1) uniform sampler2D u_area;
layout (set = 0, binding = 2) uniform sampler2D u_search;
layout (location = 0) in vec2 v_uv;
layout (location = 1) in vec2 v_pix;
layout (location = 2) in vec4 v_off[3];
layout (location = 0) out vec4 o;
void main()
{
	o = SMAABlendingWeightCalculationPS(v_uv, v_pix, v_off, u_edges, u_area, u_search, vec4(0.0));
}
