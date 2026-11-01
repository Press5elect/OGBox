#version 450
// Kelvin - original Xbox emulator for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPLv3 section 7(b)): credit to Press5elect, see LICENSE.md
//
// OGBox: Kelvin's (3061bf1) presenter shader, unchanged; OGBox's picture is the HLE GPU's
// front frame (RGBA8, the display's gamma ramp already applied). OGBox by Press5elect.
//
// The presenter: one triangle that covers the viewport (the picture's
// letterboxed rectangle), with the guest framebuffer's coordinates 0..1.

layout (location = 0) out vec2 outUV;

void main()
{
	outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
