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
#include <cstddef>
#include <cstdint>
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

// --- Taille de ST-RAM (256 Ko .. 4 Mo) --------------------------------------
// Choisie avant le boot ; EmuTOS la détecte en sondant la mémoire (la RAM répond
// jusqu'à sa taille, échoue au-delà → phystop correct, validé en headless).
inline std::size_t parseRamBytes(const std::string& s) {
    if (s == "256k") return 256u * 1024;
    if (s == "512k") return 512u * 1024;
    if (s == "1m")   return 1024u * 1024;
    if (s == "2m")   return 2048u * 1024;
    if (s == "4m")   return 4096u * 1024;
    return 512u * 1024;                          // défaut : 512 Ko
}
inline const char* ramLabel(std::size_t bytes) {
    switch (bytes / 1024) {
        case 256:  return "256k";
        case 512:  return "512k";
        case 1024: return "1m";
        case 2048: return "2m";
        case 4096: return "4m";
    }
    return "512k";
}
// Config MMU ($FF8001) approchée pour une taille (2 bits/banque : 00=128K,
// 01=512K, 10=2M). EmuTOS la RECALCULE via sa détection ; ce n'est qu'un défaut
// cohérent pour un logiciel qui lirait $FF8001 sans détection préalable.
inline uint8_t memConfigForBytes(std::size_t bytes) {
    switch (bytes / 1024) {
        case 256:  return 0x00;   // 128 + 128
        case 512:  return 0x04;   // 512 + (vide)
        case 1024: return 0x05;   // 512 + 512
        case 2048: return 0x08;   // 2M  + (vide)
        case 4096: return 0x0A;   // 2M  + 2M
    }
    return 0x04;
}

// Matériel optionnel présent selon le modèle.
inline bool machineHasDmaSound(MachineType t) {
    return t == MachineType::Ste || t == MachineType::MegaSte;   // son numérique STE
}
inline bool machineHasBlitter(MachineType t) {
    return t == MachineType::MegaSt || t == MachineType::MegaSte; // blitter (émulation à venir)
}
