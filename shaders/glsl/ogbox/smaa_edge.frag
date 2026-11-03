#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.58: SMAA pass 1 of 3: luma edges of the picture into an RG8 image.
#extension GL_GOOGLE_include_directive : require
layout (push_constant) uniform PC { vec4 metrics; } pc;   // 1/w, 1/h, w, h of the picture
#define SMAA_RT_METRICS pc.metrics
#define SMAA_GLSL_4
#define SMAA_PRESET_HIGH
#define SMAA_INCLUDE_VS 0
#include "SMAA.hlsl"
layout (set = 0, binding = 0) uniform sampler2D u_color;
layout (location = 0) in vec2 v_uv;
layout (location = 1) in vec4 v_off[3];
layout (location = 0) out vec4 o;
void main()
{
	o = vec4(SMAALumaEdgeDetectionPS(v_uv, v_off, u_color), 0.0, 0.0);
}
