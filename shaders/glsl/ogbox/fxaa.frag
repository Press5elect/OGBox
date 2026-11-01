#version 450
// OGBox - hakuX for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.26: FXAA for the presenter. hakuX's (hw/xbox/hle/vk/swapchain.c, k_fs_fxaa): FXAA 3.11
// Quality (Timothy Lottes) as PCSX2's fxaa.fx tunes it (GPL-3.0-or-later; edges only, no subpixel
// blur). hakuX runs it at the frame's size into an image and scales that; OGBox runs it in the
// presenter's one pass, at the frame's texel size, the result sampled bilinearly where the screen
// pixel falls.

layout (set = 0, binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;

const float EDGE_THRESHOLD = 0.063, EDGE_THRESHOLD_MIN = 0.0, SUBPIX = 0.0;
const float STEP[12] = float[12](1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0);
float luma(vec3 c) { return dot(c, vec3(0.2126729, 0.7151522, 0.0721750)); }
float luma_at(vec2 p) { return luma(textureLod(u_tex, p, 0.0).rgb); }
#define LUMA_OFF(x, y) luma(textureLodOffset(u_tex, posM, 0.0, ivec2(x, y)).rgb)

void main()
{
	vec2 rcp = 1.0 / vec2(textureSize(u_tex, 0));
	vec2 posM = v_uv;
	vec3 rgbM = textureLod(u_tex, posM, 0.0).rgb;
	float lumaM = luma(rgbM);
	float lumaS = LUMA_OFF(0, 1), lumaE = LUMA_OFF(1, 0), lumaN = LUMA_OFF(0, -1), lumaW = LUMA_OFF(-1, 0);
	float rangeMax = max(max(lumaN, lumaW), max(lumaE, max(lumaS, lumaM)));
	float rangeMin = min(min(lumaN, lumaW), min(lumaE, min(lumaS, lumaM)));
	float range = rangeMax - rangeMin;
	if (range < max(EDGE_THRESHOLD_MIN, rangeMax * EDGE_THRESHOLD)) {
		o = vec4(rgbM, 1.0);
		return;
	}
	float lumaNW = LUMA_OFF(-1, -1), lumaSE = LUMA_OFF(1, 1), lumaNE = LUMA_OFF(1, -1), lumaSW = LUMA_OFF(-1, 1);
	float lumaNS = lumaN + lumaS, lumaWE = lumaW + lumaE;
	float edgeHorz = abs(-2.0 * lumaW + lumaNW + lumaSW) + 2.0 * abs(-2.0 * lumaM + lumaNS) +
		abs(-2.0 * lumaE + lumaNE + lumaSE);
	float edgeVert = abs(-2.0 * lumaS + lumaSW + lumaSE) + 2.0 * abs(-2.0 * lumaM + lumaWE) +
		abs(-2.0 * lumaN + lumaNW + lumaNE);
	bool horzSpan = edgeHorz >= edgeVert;
	float subpixA = 2.0 * (lumaNS + lumaWE) + (lumaNW + lumaSW + lumaNE + lumaSE);
	float lengthSign = horzSpan ? rcp.y : rcp.x;
	if (!horzSpan) {
		lumaN = lumaW;
		lumaS = lumaE;
	}
	float subpixB = subpixA * (1.0 / 12.0) - lumaM;
	float gradientN = lumaN - lumaM, gradientS = lumaS - lumaM;
	float lumaNN = lumaN + lumaM, lumaSS = lumaS + lumaM;
	bool pairN = abs(gradientN) >= abs(gradientS);
	float gradient = max(abs(gradientN), abs(gradientS));
	if (pairN) lengthSign = -lengthSign;
	float subpixC = clamp(abs(subpixB) / range, 0.0, 1.0);
	vec2 offNP = horzSpan ? vec2(rcp.x, 0.0) : vec2(0.0, rcp.y);
	vec2 posB = posM;
	if (horzSpan) posB.y += lengthSign * 0.5; else posB.x += lengthSign * 0.5;
	vec2 posN = posB - offNP, posP = posB + offNP;
	float subpixF = (-2.0 * subpixC + 3.0) * subpixC * subpixC;
	if (!pairN) lumaNN = lumaSS;
	float gradientScaled = gradient * 0.25;
	bool lumaMLTZero = lumaM - lumaNN * 0.5 < 0.0;
	float lumaEndN = luma_at(posN) - lumaNN * 0.5, lumaEndP = luma_at(posP) - lumaNN * 0.5;
	bool doneN = abs(lumaEndN) >= gradientScaled, doneP = abs(lumaEndP) >= gradientScaled;
	if (!doneN) posN -= offNP * STEP[0];
	if (!doneP) posP += offNP * STEP[0];
	for (int i = 1; i < 12 && !(doneN && doneP); i++) {
		if (!doneN) lumaEndN = luma_at(posN) - lumaNN * 0.5;
		if (!doneP) lumaEndP = luma_at(posP) - lumaNN * 0.5;
		doneN = abs(lumaEndN) >= gradientScaled;
		doneP = abs(lumaEndP) >= gradientScaled;
		if (!doneN) posN -= offNP * STEP[i];
		if (!doneP) posP += offNP * STEP[i];
	}
	float dstN = horzSpan ? posM.x - posN.x : posM.y - posN.y;
	float dstP = horzSpan ? posP.x - posM.x : posP.y - posM.y;
	bool goodSpan = dstN < dstP ? (lumaEndN < 0.0) != lumaMLTZero : (lumaEndP < 0.0) != lumaMLTZero;
	float pixelOffset = goodSpan ? 0.5 - min(dstN, dstP) / (dstN + dstP) : 0.0;
	float offset = max(pixelOffset, subpixF * subpixF * SUBPIX);
	if (horzSpan) posM.y += offset * lengthSign; else posM.x += offset * lengthSign;
	o = vec4(textureLod(u_tex, posM, 0.0).rgb, 1.0);
}
