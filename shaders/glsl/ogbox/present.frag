#version 450
// Kelvin - original Xbox emulator for PS5
// Copyright (C) 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional terms (GPLv3 section 7(b)): credit to Press5elect, see LICENSE.md
//
// OGBox: Kelvin's (3061bf1) presenter shader, unchanged; OGBox's picture is the HLE GPU's
// front frame (RGBA8, the display's gamma ramp already applied). OGBox by Press5elect.
//
// The presenter: the guest framebuffer as scan-out shows it (already turned
// into B8G8R8A8 by the presenter, through the DAC's gamma ramp). The sampler
// is the scaling: bilinear by default, nearest when chosen. No other change.

layout (set = 0, binding = 0) uniform sampler2D picture;

layout (location = 0) in vec2 inUV;
layout (location = 0) out vec4 outFragColor;

void main()
{
	outFragColor = vec4(texture(picture, inUV).rgb, 1.0);
}
