// =============================================================================
//  MachineType.hpp — Profils de machine Atari (ST / Mega ST / STE / Mega STE).
//
//  Le type de machine est choisi AVANT le démarrage (comme le cœur CPU). Il
//  décide quel matériel optionnel est présent — donc quels registres répondent
//  et où une bus error se produit. EmuTOS s'en sert pour détecter le modèle au
//  boot (ex. le son DMA STE à $FF8900). Aujourd'hui on distingue surtout :
//    - son DMA STE  : présent sur STE / Mega STE ;
//    - blitter      : présent sur Mega ST / Mega STE (émulation à venir).
//
//  Réf. : EmuTOS configuration.c, Hatari, MAME atarist.cpp. (c) 2026 NeoST.
// =============================================================================
#pragma once
#include <string>

enum class MachineType { St, MegaSt, Ste, MegaSte };

inline const char* machineName(MachineType t) {
    switch (t) {
        case MachineType::St:      return "st";
        case MachineType::MegaSt:  return "megast";
        case MachineType::Ste:     return "ste";
        case MachineType::MegaSte: return "megaste";
    }
    return "ste";
}

inline MachineType parseMachine(const std::string& s) {
    if (s == "st")      return MachineType::St;
    if (s == "megast")  return MachineType::MegaSt;
    if (s == "megaste") return MachineType::MegaSte;
    return MachineType::Ste;                 // défaut : 1040 STE (son DMA, pas de blitter)
}

// Matériel optionnel présent selon le modèle.
inline bool machineHasDmaSound(MachineType t) {
    return t == MachineType::Ste || t == MachineType::MegaSte;   // son numérique STE
}
inline bool machineHasBlitter(MachineType t) {
    return t == MachineType::MegaSt || t == MachineType::MegaSte; // blitter (émulation à venir)
}
