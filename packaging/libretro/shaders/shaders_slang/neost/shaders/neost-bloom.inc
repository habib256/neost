// NeoST — RetroArch slang port of the desktop CRT pass.
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Le halo (halation) partagé par neost-bloom-h et neost-bloom-v : la passe de POM2
// (pom2-bloom.inc), le rayon compté en points de la ligne de 640 du ST.
// [D] libretro glsl-shaders/bloom (hunterk, domaine public) ; genou et repliement
//     de Casso (`bloom_h.hlsl` @ 2d899adf), comme POM2.

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
    vec4 SourceSize;
    vec4 FinalViewportSize;
    float NEOST_BLOOM;
    float NEOST_BLOOM_RADIUS;
    float NEOST_BLOOM_THRESHOLD;
} params;

#pragma parameter NEOST_BLOOM "NeoST bloom (halation)" 0.0 0.0 1.0 0.01
#pragma parameter NEOST_BLOOM_RADIUS "NeoST bloom radius (dots of the 640-dot line)" 2.0 0.5 6.0 0.1
#pragma parameter NEOST_BLOOM_THRESHOLD "NeoST bloom threshold" 0.5 0.0 1.0 0.01

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = params.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

vec3 tap(vec2 uv, float threshold)
{
    vec3 c = texture(Source, clamp(uv, 0.0, 1.0)).rgb;
    if (threshold < 0.0) return c;
    float knee = max(threshold * 0.5, 0.0001);
    float lum  = max(c.r, max(c.g, c.b));
    float soft = clamp(lum - threshold + knee, 0.0, 2.0 * knee);
    float over = max(soft * soft / (4.0 * knee), lum - threshold);
    return c * (over / max(lum, 0.0001));
}

vec3 blur(vec2 uv, vec2 stp, float threshold)
{
    vec3 acc = tap(uv, threshold) * 0.227027;
    acc += (tap(uv + stp,       threshold) + tap(uv - stp,       threshold)) * 0.1945946;
    acc += (tap(uv + stp * 2.0, threshold) + tap(uv - stp * 2.0, threshold)) * 0.1216216;
    acc += (tap(uv + stp * 3.0, threshold) + tap(uv - stp * 3.0, threshold)) * 0.054054;
    acc += (tap(uv + stp * 4.0, threshold) + tap(uv - stp * 4.0, threshold)) * 0.016216;
    return acc;
}

float bloomStepX() { return params.NEOST_BLOOM_RADIUS / 640.0; }
float bloomStepY()
{
    return bloomStepX() * params.FinalViewportSize.x / max(params.FinalViewportSize.y, 1.0);
}
