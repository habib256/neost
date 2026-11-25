#version 450

// NeoST — RetroArch slang port of the desktop CRT pass (src/gui/CrtEffectStack.cpp).
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dernière passe : recopie la passe « verre » à l'écran, 1:1. Elle existe parce que
// la passe verre relit sa propre sortie précédente (rémanence) et que RetroArch ne
// garde de texture de retour que pour une passe qui n'est pas la dernière (même
// montage que POM2, libretro/shaders/shaders_slang/pom2/shaders/pom2-present.slang).

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
} global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main()
{
    FragColor = vec4(texture(Source, vTexCoord).rgb, 1.0);
}
