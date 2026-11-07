#version 450
// FidelityFX Super Resolution 1.0, Edge Adaptive Spatial Upsampling (EASU): AMD's ffx_fsr1.h
// FsrEasuF (the 32-bit path), ported to GLSL for OGBox's presenter.
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved. MIT (ps5/licenses/FidelityFX-FSR-LICENSE.txt).
// OGBox 0.0.6: one pass from the picture to its place on the screen (the presenter's triangle):
// the input position is v_uv * the picture's size - 0.5 (what EASU's con0 makes of an output
// pixel), the twelve taps read with texelFetch, the result clamped to the four nearest texels as
// EASU does. OGBox's changes: Copyright (C) 2026 Press5elect, GPL-3.0-or-later; keep the
// attribution "OGBox by Press5elect" (NOTICE).
layout (set = 0, binding = 0) uniform sampler2D u_tex;
layout (location = 0) in vec2 v_uv;
layout (location = 0) out vec4 o;

ivec2 g_max;
vec3 tex(ivec2 p) { return texelFetch(u_tex, clamp(p, ivec2(0), g_max), 0).rgb; }
float luma(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }

void easuSet(inout vec2 dir, inout float len, vec2 pp, bool biS, bool biT, bool biU, bool biV,
	float lA, float lB, float lC, float lD, float lE)
{
	float w = 0.0;
	if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
	if (biT) w = pp.x * (1.0 - pp.y);
	if (biU) w = (1.0 - pp.x) * pp.y;
	if (biV) w = pp.x * pp.y;
	float dc = lD - lC, cb = lC - lB;
	float lenX = max(abs(dc), abs(cb));
	lenX = 1.0 / max(lenX, 1.0 / 65536.0);
	float dirX = lD - lB;
	dir.x += dirX * w;
	lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
	lenX *= lenX;
	len += lenX * w;
	float ec = lE - lC, ca = lC - lA;
	float lenY = max(abs(ec), abs(ca));
	lenY = 1.0 / max(lenY, 1.0 / 65536.0);
	float dirY = lE - lA;
	dir.y += dirY * w;
	lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
	lenY *= lenY;
	len += lenY * w;
}

void easuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c)
{
	vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x);
	v *= len;
	float d2 = min(v.x * v.x + v.y * v.y, clp);
	float wB = 2.0 / 5.0 * d2 - 1.0;
	float wA = lob * d2 - 1.0;
	wB *= wB;
	wA *= wA;
	wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
	float w = wB * wA;
	aC += c * w;
	aW += w;
}

void main()
{
	ivec2 size = textureSize(u_tex, 0);
	g_max = size - 1;
	vec2 pp = v_uv * vec2(size) - 0.5;
	vec2 fp = floor(pp);
	pp -= fp;
	ivec2 f0 = ivec2(fp);
	//    b c
	//  e f g h
	//  i j k l
	//    n o
	vec3 bC = tex(f0 + ivec2(0, -1)), cC = tex(f0 + ivec2(1, -1));
	vec3 eC = tex(f0 + ivec2(-1, 0)), fC = tex(f0), gC = tex(f0 + ivec2(1, 0)), hC = tex(f0 + ivec2(2, 0));
	vec3 iC = tex(f0 + ivec2(-1, 1)), jC = tex(f0 + ivec2(0, 1)), kC = tex(f0 + ivec2(1, 1)), lC = tex(f0 + ivec2(2, 1));
	vec3 nC = tex(f0 + ivec2(0, 2)), oC = tex(f0 + ivec2(1, 2));
	float bL = luma(bC), cL = luma(cC), eL = luma(eC), fL = luma(fC), gL = luma(gC), hL = luma(hC);
	float iL = luma(iC), jL = luma(jC), kL = luma(kC), lL = luma(lC), nL = luma(nC), oL = luma(oC);
	vec2 dir = vec2(0.0);
	float len = 0.0;
	easuSet(dir, len, pp, true, false, false, false, bL, eL, fL, gL, jL);
	easuSet(dir, len, pp, false, true, false, false, cL, fL, gL, hL, kL);
	easuSet(dir, len, pp, false, false, true, false, fL, iL, jL, kL, nL);
	easuSet(dir, len, pp, false, false, false, true, gL, jL, kL, lL, oL);
	vec2 dir2 = dir * dir;
	float dirR = dir2.x + dir2.y;
	bool zro = dirR < 1.0 / 32768.0;
	dirR = inversesqrt(max(dirR, 1.0 / 32768.0));
	dirR = zro ? 1.0 : dirR;
	dir.x = zro ? 1.0 : dir.x;
	dir *= dirR;
	len = len * 0.5;
	len *= len;
	float stretch = (dir.x * dir.x + dir.y * dir.y) / max(max(abs(dir.x), abs(dir.y)), 1.0 / 65536.0);
	vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
	float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
	float clp = 1.0 / lob;
	vec3 min4 = min(min(fC, gC), min(jC, kC));
	vec3 max4 = max(max(fC, gC), max(jC, kC));
	vec3 aC = vec3(0.0);
	float aW = 0.0;
	easuTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, bC);
	easuTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, cC);
	easuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, iC);
	easuTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, jC);
	easuTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, fC);
	easuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, eC);
	easuTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, kC);
	easuTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, lC);
	easuTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, hC);
	easuTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, gC);
	easuTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, oC);
	easuTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, nC);
	o = vec4(min(max4, max(min4, aC / aW)), 1.0);
}
