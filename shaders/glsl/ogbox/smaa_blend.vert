#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.58: SMAA pass 3 of 3: neighbourhood blending.
#extension GL_GOOGLE_include_directive : require
layout (push_constant) uniform PC { vec4 metrics; } pc;   // 1/w, 1/h, w, h of the picture
#define SMAA_RT_METRICS pc.metrics
#define SMAA_GLSL_4
#define SMAA_PRESET_HIGH
#define SMAA_INCLUDE_PS 0
#include "SMAA.hlsl"
layout (location = 0) out vec2 v_uv;
layout (location = 1) out vec4 v_off;
void main()
{
	v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
	SMAANeighborhoodBlendingVS(v_uv, v_off);
}
