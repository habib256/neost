#version 450

// NeoST — RetroArch slang port of the desktop CRT pass.
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Passe 3 : le tube. Le fragment « verre » de CrtEffectStack (src/gui/CrtEffectStack.cpp),
// porté comme POM2 l'a fait pour le sien (pom2-glass.slang) : baril, netteté, teinte,
// luminosité / contraste / saturation, gamma du phosphore, lignes de balayage, masque,
// halo, éclairage central, gain, rémanence, dans cet ordre. Mêmes noms de réglages
// que CrtParams (src/gui/CrtParams.h), mêmes défauts.
//
// Ce que le ST ajoute : le moniteur se choisit SEUL. Un shader slang ne reçoit du
// cœur que l'image ; or le tampon de NeoST garde une ligne par ligne de balayage, si
// bien que sa hauteur dit le moniteur (docs/LIBRETRO.md, « Shaders : ce qu'un shader
// slang peut savoir ») :
//  · plus de 300 lignes → SM124 monochrome (640 × 400 à 71 Hz) : pas de lignes de
//    balayage visibles, pas de masque, un blanc légèrement froid ;
//  · sinon → SC1224 couleur (200 lignes, avec ou sans bordures, zoom compris).
// NEOST_MONITOR force l'un ou l'autre.

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
    vec4 OutputSize;
    vec4 NeoSTCableSize;
    uint FrameCount;
    float NEOST_MONITOR;
    float NEOST_BRIGHTNESS;
    float NEOST_CONTRAST;
    float NEOST_SATURATION;
    float NEOST_HUE;
    float NEOST_SHARPNESS;
    float NEOST_PERSISTENCE;
    float NEOST_PHOSPHOR_GAMMA;
    float NEOST_SCANLINES;
    float NEOST_BARREL;
    float NEOST_CENTER_LIGHTING;
    float NEOST_SHADOW_MASK;
    float NEOST_SHADOW_STRENGTH;
    float NEOST_MASK_TRIADS;
    float NEOST_LUMINANCE_GAIN;
    float NEOST_BLOOM;
    float NEOST_MONO_TINT;
} params;

// Défauts : CrtParams (src/gui/CrtParams.h) ; plages : celles du panneau du bureau.
#pragma parameter NEOST_MONITOR "NeoST monitor: auto, SC1224 colour, SM124 mono" 0.0 0.0 2.0 1.0
#pragma parameter NEOST_BRIGHTNESS "NeoST brightness" 0.0 -0.5 0.5 0.01
#pragma parameter NEOST_CONTRAST "NeoST contrast" 1.0 0.5 1.5 0.01
#pragma parameter NEOST_SATURATION "NeoST saturation" 1.0 0.0 2.0 0.01
#pragma parameter NEOST_HUE "NeoST hue" 0.0 -0.5 0.5 0.01
#pragma parameter NEOST_SHARPNESS "NeoST sharpness (0.5 = neutral)" 0.5 0.0 1.0 0.01
#pragma parameter NEOST_PERSISTENCE "NeoST phosphor persistence" 0.4 0.0 0.95 0.01
#pragma parameter NEOST_PHOSPHOR_GAMMA "NeoST phosphor gamma" 1.0 0.6 2.6 0.05
#pragma parameter NEOST_SCANLINES "NeoST scanlines (colour monitor)" 0.25 0.0 1.0 0.01
#pragma parameter NEOST_BARREL "NeoST barrel (curvature)" 0.025 0.0 0.3 0.005
#pragma parameter NEOST_CENTER_LIGHTING "NeoST centre lighting (1 = flat)" 1.0 0.5 1.0 0.01
#pragma parameter NEOST_SHADOW_MASK "NeoST mask: off, triad, aperture grille, dot" 0.0 0.0 3.0 1.0
#pragma parameter NEOST_SHADOW_STRENGTH "NeoST mask strength" 0.5 0.0 1.0 0.01
#pragma parameter NEOST_MASK_TRIADS "NeoST mask triads across the picture" 420.0 120.0 900.0 10.0
#pragma parameter NEOST_LUMINANCE_GAIN "NeoST luminance gain" 1.0 1.0 2.0 0.01
#pragma parameter NEOST_BLOOM "NeoST bloom (halation)" 0.0 0.0 1.0 0.01
#pragma parameter NEOST_MONO_TINT "NeoST mono white tint (0 = neutral, 1 = cold)" 0.4 0.0 1.0 0.01

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
layout(set = 0, binding = 2) uniform sampler2D Source;              // halo, passe verticale
layout(set = 0, binding = 3) uniform sampler2D NeoSTCable;          // l'image après le câble
layout(set = 0, binding = 4) uniform sampler2D NeoSTGlassFeedback;  // cette passe, trame précédente

#define uSrcSize (params.NeoSTCableSize.xy)
#define uOutSize (params.OutputSize.xy)

float cubicWeight(float x)
{
    x = abs(x);
    if (x < 1.0) return x * x * (1.5 * x - 2.5) + 1.0;
    if (x < 2.0) return x * (x * (-0.5 * x + 2.5) - 4.0) + 2.0;
    return 0.0;
}

vec3 sampleSrc(vec2 uv)
{
    uv = clamp(uv, 0.0, 1.0);
    vec2 mag2 = uOutSize / uSrcSize;
    // Réduction horizontale (moyenne ou mono dans une fenêtre étroite) : moyenne des
    // colonnes couvertes.
    if (mag2.x < 0.995) {
        const int kBox = 8;
        float foot = (1.0 / mag2.x) / uSrcSize.x;
        vec3 acc = vec3(0.0);
        for (int i = 0; i < kBox; ++i) {
            float o = ((float(i) + 0.5) / float(kBox) - 0.5) * foot;
            acc += texture(NeoSTCable, vec2(clamp(uv.x + o, 0.0, 1.0), uv.y)).rgb;
        }
        return acc / float(kBox);
    }
    float mag = max(mag2.x, mag2.y);
    if (mag <= 1.25)
        return texture(NeoSTCable, uv).rgb;
    vec2 coord = uv * uSrcSize - 0.5;
    vec2 f = fract(coord);
    coord = floor(coord);
    vec3 col = vec3(0.0);
    float wsum = 0.0;
    for (int j = -1; j <= 2; ++j) {
        for (int i = -1; i <= 2; ++i) {
            vec2 offs = vec2(float(i), float(j));
            vec2 samp = (coord + offs + 0.5) / uSrcSize;
            float w = cubicWeight(offs.x - f.x) * cubicWeight(offs.y - f.y);
            col += texture(NeoSTCable, clamp(samp, 0.0, 1.0)).rgb * w;
            wsum += w;
        }
    }
    return col / max(wsum, 1e-4);
}

void main()
{
    int monitor = int(params.NEOST_MONITOR + 0.5);
    bool mono = monitor == 2 || (monitor == 0 && uSrcSize.y > 300.0);

    vec2 cuv = vTexCoord * 2.0 - 1.0;
    float r2 = dot(cuv, cuv);
    vec2 buv = cuv * (1.0 + params.NEOST_BARREL * r2);
    vec2 uv  = buv * 0.5 + 0.5;
    vec2  edge     = min(uv, 1.0 - uv);
    vec2  edgeFw   = max(fwidth(uv), vec2(1e-4));
    float edgeMask = clamp(min(edge.x / edgeFw.x, edge.y / edgeFw.y), 0.0, 1.0);
    vec3 rgb = sampleSrc(uv);

    {
        float amt = (params.NEOST_SHARPNESS - 0.5) * 2.0;
        if (amt != 0.0) {
            vec2 t = 1.0 / uSrcSize;
            vec3 blurred = (
                sampleSrc(uv + vec2(-t.x, 0.0)) +
                sampleSrc(uv + vec2( t.x, 0.0)) +
                sampleSrc(uv + vec2(0.0, -t.y)) +
                sampleSrc(uv + vec2(0.0,  t.y))) * 0.25;
            rgb = clamp(rgb + amt * (rgb - blurred), 0.0, 1.0);
        }
    }

    if (params.NEOST_HUE != 0.0) {
        float Y = dot(rgb, vec3( 0.299,    0.587,    0.114));
        float U = dot(rgb, vec3(-0.14713, -0.28886,  0.436));
        float V = dot(rgb, vec3( 0.615,   -0.51499, -0.10001));
        float a  = params.NEOST_HUE * 3.14159265;
        float cs = cos(a), sn = sin(a);
        float Ur = U * cs - V * sn;
        float Vr = U * sn + V * cs;
        rgb = vec3(Y                 + 1.139883 * Vr,
                   Y - 0.394642 * Ur - 0.580622 * Vr,
                   Y + 2.032062 * Ur);
    }

    rgb = (rgb - 0.5) * params.NEOST_CONTRAST + 0.5 + params.NEOST_BRIGHTNESS;
    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = mix(vec3(luma), rgb, clamp(params.NEOST_SATURATION, 0.0, 4.0));
    rgb = clamp(rgb, 0.0, 1.0);

    // SM124 : un écran blanc, teinté vers le froid. [F] Teinte réglée à l'œil, aucune
    // mesure du phosphore du SM124 ; tranché par : une photo calibrée d'un SM124.
    if (mono) {
        float y = dot(rgb, vec3(0.299, 0.587, 0.114));
        rgb = y * mix(vec3(1.0), vec3(0.92, 0.97, 1.0), params.NEOST_MONO_TINT);
    }

    if (params.NEOST_PHOSPHOR_GAMMA != 1.0)
        rgb = pow(max(rgb, vec3(0.0)), vec3(params.NEOST_PHOSPHOR_GAMMA));

    // Lignes de balayage : une ligne de la source = une ligne du faisceau. Sur le
    // SM124 (400 lignes), elles ne se voient pas : coupées.
    float outRow = uv.y * (uSrcSize.y * 2.0);
    float rowFw  = max(fwidth(outRow), 1e-4);
    float scanAA = clamp(1.0 - (rowFw - 0.5) / 0.5, 0.0, 1.0);
    float beam   = 0.5 - 0.5 * cos(3.14159265 * outRow);
    float scan   = mono ? 0.0 : params.NEOST_SCANLINES;
    rgb *= 1.0 - scan * (1.0 - beam) * scanAA;

    // Masque, attaché au verre : NEOST_MASK_TRIADS triades sur la largeur de l'image.
    // [F] 420 triades : ordre de grandeur d'un 14 pouces couleur, pas une mesure du
    // SC1224 ; tranché par : sa fiche technique (pas du masque) et sa largeur utile.
    int maskType = mono ? 0 : int(params.NEOST_SHADOW_MASK + 0.5);
    if (maskType != 0 && params.NEOST_SHADOW_STRENGTH > 0.0) {
        float oxBase = uv.x * params.NEOST_MASK_TRIADS * 3.0;
        float maskFw = max(fwidth(oxBase), 1e-4);
        float maskAA = clamp(1.0 - (maskFw - 1.0) / 2.0, 0.0, 1.0);
        float ox = oxBase;
        if (maskType == 3)
            ox += (mod(floor(outRow * 0.5), 2.0) < 1.0) ? 0.0 : 1.5;
        float strength = params.NEOST_SHADOW_STRENGTH * maskAA;
        int phase = int(mod(floor(ox), 3.0));
        const float maskDark = 0.5, maskLight = 1.5;
        vec3 mask = vec3(maskDark);
        if      (phase == 0) mask.r = maskLight;
        else if (phase == 1) mask.g = maskLight;
        else                 mask.b = maskLight;
        vec3 atten = mix(vec3(1.0), mask, strength);
        if (maskType == 1 || maskType == 3) {
            float vrow = mod(floor(outRow), 3.0);
            if (vrow < 1.0) atten *= mix(1.0, 0.7, strength);
        }
        rgb *= atten;
    }

    if (params.NEOST_BLOOM > 0.0)
        rgb += texture(Source, clamp(uv, 0.0, 1.0)).rgb * params.NEOST_BLOOM;

    {
        vec2 lighting = cuv * (1.0 / params.NEOST_CENTER_LIGHTING - 1.0);
        rgb *= exp(-dot(lighting, lighting));
    }

    rgb *= params.NEOST_LUMINANCE_GAIN;
    rgb *= edgeMask;

    // Rémanence, en dernier, sur la couleur finale ; aucune histoire à la première trame.
    // Comptée en trames PRÉSENTÉES : un contenu à 50 Hz sur un écran à 60 Hz voit des
    // trames doublées (docs/LIBRETRO.md).
    float retention = params.FrameCount == 0u ? 0.0 : clamp(params.NEOST_PERSISTENCE, 0.0, 0.98);
    vec3 prev = texture(NeoSTGlassFeedback, vTexCoord).rgb;
    rgb = max(rgb, prev * retention - 0.5 / 256.0);

    FragColor = vec4(rgb, 1.0);
}
