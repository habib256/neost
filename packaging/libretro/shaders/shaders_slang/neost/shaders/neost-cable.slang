#version 450

// NeoST — RetroArch slang port of the desktop CRT pass.
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Passe 0 : le câble RGB analogique (Péritel, moniteur). Le passe-bas sinc fenêtré
// de POM2 (pom2-cable.slang), horizontal, par canal, sur la grille de la SOURCE.
// Un même câble est une fraction différente de Nyquist selon la résolution, comme le
// vrai : l'horloge des points du ST se déduit de la taille de l'image reçue.
//  · ≤ 480 points de large : basse résolution, 8 MHz — la ligne active couvre 320
//    cycles CPU à 8 MHz pour 320 points (VideoGeometry, src/core/Shifter.hpp : DE de 56
//    à 376) ;
//  · plus de 300 lignes : mono, 32 MHz (640 points en 160 cycles CPU, DE de 0 à 160) ;
//  · sinon : moyenne résolution, 16 MHz (640 points en 320 cycles).
// 0 MHz, ou une coupure au-delà de Nyquist, laisse passer l'image telle quelle.

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
    vec4 SourceSize;
    float NEOST_RGB_BANDWIDTH;
} params;

#pragma parameter NEOST_RGB_BANDWIDTH "NeoST analog RGB bandwidth (MHz, 0 = off)" 0.0 0.0 16.0 0.1

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

const int   kTaps = 8;
const float kPi   = 3.14159265;

void main()
{
    float fsMHz = params.SourceSize.x <= 480.0 ? 8.0 : (params.SourceSize.y > 300.0 ? 32.0 : 16.0);
    float cutoff = params.NEOST_RGB_BANDWIDTH / fsMHz;   // cycles par point
    if (params.NEOST_RGB_BANDWIDTH <= 0.0 || cutoff >= 0.5) {
        FragColor = vec4(texture(Source, vTexCoord).rgb, 1.0);
        return;
    }
    float texel = params.SourceSize.z;
    float fc2   = 2.0 * cutoff;
    vec3  acc  = texture(Source, vTexCoord).rgb * fc2;
    float wsum = fc2;
    for (int k = 1; k <= kTaps; ++k) {
        float x = fc2 * float(k);
        float h = fc2 * (sin(kPi * x) / (kPi * x))
                      * (0.5 + 0.5 * cos(kPi * float(k) / float(kTaps + 1)));
        float dx = float(k) * texel;
        acc += (texture(Source, vec2(clamp(vTexCoord.x - dx, 0.0, 1.0), vTexCoord.y)).rgb +
                texture(Source, vec2(clamp(vTexCoord.x + dx, 0.0, 1.0), vTexCoord.y)).rgb) * h;
        wsum += 2.0 * h;
    }
    // Normalisé par la somme des poids : un aplat reste exactement plat.
    FragColor = vec4(acc / max(wsum, 1e-4), 1.0);
}
