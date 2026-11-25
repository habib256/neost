#version 450

// NeoST — RetroArch slang port of the desktop CRT pass.
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Passe 1 : halo, sélection des points lumineux et flou horizontal.

#include "neost-bloom.inc"

void main()
{
    if (params.NEOST_BLOOM <= 0.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    FragColor = vec4(blur(vTexCoord, vec2(bloomStepX(), 0.0), params.NEOST_BLOOM_THRESHOLD), 1.0);
}
