#version 450
// FidelityFX Super Resolution 1.0, Robust Contrast Adaptive Sharpening (RCAS): AMD's ffx_fsr1.h
// FsrRcasF (the 32-bit path, v1.20210629), ported to GLSL for OGBox's presenter.
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved. MIT (ps5/licenses/FidelityFX-FSR-LICENSE.txt).
// OGBox: FSR1's second pass, after EASU (fsr1.frag), which the presenter draws into an image at the
// screen's size first; this pass reads it 1:1 at the fragment's own pixel and draws into the screen
// (or HDR's picture layer, before HDR's encoding). The push constant's .x is FsrRcasCon's con.x (the
// sharpness as a linear value: exp2(-stops)). No denoise (FSR_RCAS_DENOISE off, AMD's default), no
// alpha pass-through. OGBox's changes: APrxMedRcpF1 kept for the resolve as AMD has it; the limiters'
// two divisions are guarded where AMD's divide by zero (a channel all 0 around the pixel, or all 1),
// taking the other limit there as a GPU's IEEE min/max with the NaN does. Pixels outside the image
// read its edge; the letterbox is black (0), which stops the sharpening at the picture's border.
// Copyright (C) 2026 Press5elect, GPL-3.0-or-later; keep the attribution "OGBox by Press5elect" (NOTICE).
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (push_constant) uniform PC { vec4 metrics; } pc;
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;

#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))

ivec2 g_max;
vec3 tex(ivec2 p) { return texelFetch(u_tex, clamp(p, ivec2(0), g_max), 0).rgb; }

// ffx_a.h APrxMedRcpF1
float prxMedRcp(float a)
{
	float b = uintBitsToFloat(uint(0x7ef19fff) - floatBitsToUint(a));
	return b * (-b * a + 2.0);
}

// one channel's lobe: max(-hitMin, hitMax), each left out where its divisor is 0
float lobeOf(float mn4, float mx4, float e)
{
	float lo = mx4 > 0.0 ? -(min(mn4, e) * (1.0 / (4.0 * mx4))) : -1.0;
	float hi = mn4 < 1.0 ? (1.0 - max(mx4, e)) * (1.0 / (4.0 * mn4 - 4.0)) : -1.0;
	return max(lo, hi);
}

void main()
{
	g_max = textureSize(u_tex, 0) - 1;
	ivec2 sp = ivec2(gl_FragCoord.xy);
	//    b
	//  d e f
	//    h
	vec3 b = tex(sp + ivec2(0, -1));
	vec3 d = tex(sp + ivec2(-1, 0));
	vec3 e = tex(sp);
	vec3 f = tex(sp + ivec2(1, 0));
	vec3 h = tex(sp + ivec2(0, 1));
	vec3 mn4 = min(min(b, d), min(f, h));
	vec3 mx4 = max(max(b, d), max(f, h));
	float lobe = max(lobeOf(mn4.r, mx4.r, e.r), max(lobeOf(mn4.g, mx4.g, e.g), lobeOf(mn4.b, mx4.b, e.b)));
	lobe = max(-FSR_RCAS_LIMIT, min(lobe, 0.0)) * pc.metrics.x;
	float rcpL = prxMedRcp(4.0 * lobe + 1.0);
	o = vec4((lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL, 1.0);
}
