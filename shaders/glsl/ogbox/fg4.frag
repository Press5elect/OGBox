#version 460 core
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require
// OGBox 0.0.7: Crux2's own frame generation, pass 4 (framegen.glsl says what each pass does).
#define FRAGMENT_SHADER 1
#define FG_PASS 4
#include "framegen.glsl"
