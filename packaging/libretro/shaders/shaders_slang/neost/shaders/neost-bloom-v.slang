#version 450

// NeoST — RetroArch slang port of the desktop CRT pass.
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Passe 2 : halo, flou vertical d'une image déjà sélectionnée.

#include "neost-bloom.inc"

void main()
{
    if (params.NEOST_BLOOM <= 0.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    FragColor = vec4(blur(vTexCoord, vec2(0.0, bloomStepY()), -1.0), 1.0);
}
