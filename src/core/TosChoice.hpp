// =============================================================================
//  TosChoice.hpp — Choisir le TOS qui va avec une machine.
//
//  Demande de l'auteur (2026-11-13) : quand on choisit STE (ou une autre machine)
//  dans la configuration, NeoST doit prendre lui-même le TOS ou l'EmuTOS approprié
//  s'il existe dans roms/. Sans cela, `machine=ste` + `tos102uk` démarrait en ST
//  (adjustMachineForTos rétrograde un TOS ≤ 1.04) : Systematic Error, démo STE,
//  plantait alors en deux bombes sur le blitter absent ($FF8A28) — et l'utilisateur
//  croyait être en STE.
//
//  Logique pure (pas de GUI, pas d'E/S hors lecture d'en-tête) : testée par
//  neost-selftest.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/MachineType.hpp"

namespace neost {

// Ce que l'en-tête d'une image TOS dit d'elle (cf. MachineType.hpp).
struct TosInfo {
    uint16_t    ver = 0;          // version (offset 2)
    bool        emuTos = false;   // signature « ETOS » à $2C
    bool        ntsc = false;     // os_conf bit 0 = 0 → 60 Hz
    int         country = -1;     // os_conf >> 1 (0 US, 1 DE, 2 FR, 3 UK, 4 ES, 5 IT…)
    std::size_t size = 0;         // taille du fichier
};

// Lit l'en-tête de `path`. Faux si illisible ou trop court.
bool readTosInfo(const std::string& path, TosInfo& out);

// Ce TOS fait-il tourner `m` tel quel ? Mêmes bornes que Machine::adjustMachineForTos
// (TOS ≤ 1.04 : ST seulement ; 1.06/1.62 : STE seulement ; Mega STE : ≥ 2.00) et que
// CLAUDE.md (EmuTOS 256 Ko : STE/Mega STE), sans TOS du TT ni du Falcon.
bool tosRunsMachine(const TosInfo& t, MachineType m);

// Meilleur candidat pour `m` : indice dans `cands`, ou -1 si aucun ne convient.
// Ordre de préférence, à partir du TOS courant `cur` (nullptr si inconnu) :
//  1. la même famille (TOS Atari / EmuTOS) — on ne remplace pas un TOS Atari par
//     EmuTOS quand un TOS Atari convient, ni l'inverse ;
//  2. le même pays (clavier, fréquence), à défaut la même fréquence (PAL/NTSC) ;
//  3. le TOS d'ORIGINE de la machine : 1.04 puis 1.02 puis 1.00 (ST, Mega ST) ;
//     1.62 puis 1.06 puis 2.06 puis 2.05 (STE) ; 2.06 puis 2.05 (Mega STE).
// Égalité : l'ordre de `cands` (l'appelant les trie par nom).
int pickTosForMachine(MachineType m, const TosInfo* cur, const std::vector<TosInfo>& cands);

}  // namespace neost
