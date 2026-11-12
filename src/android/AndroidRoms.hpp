// =============================================================================
//  AndroidRoms.hpp — Les ROM système que l'APK embarque, et le choix persistant.
//
//  L'APK déballe QUATRE EmuTOS (packaging/android/stage_assets.sh) ; jusqu'au
//  2026-11-12 le frontend ne démarrait que sur etos192us, les trois autres
//  dormaient dans le stockage interne. Cette table est l'UNIQUE liste : le
//  déballage (prepareData), la page ROM du menu et la lecture du choix
//  enregistré la parcourent toutes — une ROM ajoutée ici l'est partout.
//
//  Logique PURE (ni SDL, ni ImGui, ni E/S) : neost-selftest la verrouille, y
//  compris contre les vraies ROM du dépôt (taille ↔ machine, pays du clavier).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <string>

#include "core/MachineType.hpp"

namespace neost {

struct AndroidRom {
    const char* file;      // nom PLAT dans les assets et dans le dossier de données
    const char* label;     // libellé du menu (anglais : interface)
    MachineType machine;   // machine demandée pour cette ROM
};

// [C] Les builds EmuTOS 192 Ko se présentent en TOS 1.04 « Atari ST » et ne
//     programment que le matériel ST ; les 256 Ko programment le SCU du STE —
//     c'est pourquoi le bureau rétrograde un TOS <= 1.04 en ST (adjustMachineForTos)
//     et pourquoi un 256 Ko est servi ici sur STE (CLAUDE.md § Disquettes & ROM).
// [C] Le suffixe fixe la fréquence de balayage : « us » = 60 Hz NTSC, « fr » =
//     50 Hz PAL (en-tête ROM, os_conf bit 0) — et le pays du clavier (os_conf >> 1).
inline constexpr AndroidRom kAndroidRoms[] = {
    {"etos192us.img", "EmuTOS 192K US  -  ST, 60 Hz",  MachineType::St},
    {"etos192fr.img", "EmuTOS 192K FR  -  ST, 50 Hz",  MachineType::St},
    {"etos256us.img", "EmuTOS 256K US  -  STE, 60 Hz", MachineType::Ste},
    {"etos256fr.img", "EmuTOS 256K FR  -  STE, 50 Hz", MachineType::Ste},
};
inline constexpr int kAndroidRomCount = int(sizeof kAndroidRoms / sizeof kAndroidRoms[0]);
inline constexpr int kAndroidRomDefault = 0;   // etos192us : la machine de la démo web

// Fichier du choix, dans le dossier de données interne (à côté des ROM).
inline constexpr const char* kAndroidRomChoiceFile = "rom.cfg";

// Index d'une ROM par son nom de fichier ; -1 si elle n'est pas dans la table.
inline int androidRomIndex(const std::string& file) {
    for (int i = 0; i < kAndroidRomCount; ++i)
        if (file == kAndroidRoms[i].file) return i;
    return -1;
}

// Contenu du fichier de choix → index. Blancs et fin de ligne (CR/LF) ignorés ;
// fichier absent, vide ou nom inconnu (APK plus ancien, fichier abîmé) → ROM par
// défaut, JAMAIS un chemin arbitraire : seule une ROM de la table est chargée.
inline int parseAndroidRomChoice(const std::string& content) {
    std::size_t b = 0, e = content.size();
    while (b < e && (content[b] == ' ' || content[b] == '\t' || content[b] == '\r' || content[b] == '\n')) ++b;
    while (e > b && (content[e - 1] == ' ' || content[e - 1] == '\t' || content[e - 1] == '\r' || content[e - 1] == '\n')) --e;
    const int i = androidRomIndex(content.substr(b, e - b));
    return i >= 0 ? i : kAndroidRomDefault;
}

// Index → contenu du fichier de choix (relu à l'identique par parseAndroidRomChoice).
inline std::string formatAndroidRomChoice(int index) {
    if (index < 0 || index >= kAndroidRomCount) index = kAndroidRomDefault;
    return std::string(kAndroidRoms[index].file) + "\n";
}

}  // namespace neost
