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
//  Réf. : EmuTOS configuration.c, Hatari, MAME atarist.cpp @ ce391499d37d (révision de
//  vérification ; pilotes st, megast, ste, megaste). (c) 2026 NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

// Un accès qui FAUTE tombe-t-il sur un périphérique que le profil courant n'a pas ?
// Rend alors la puce visée et le profil minimal qui la porte, sinon nullptr.
//
// POURQUOI : un jeu STE-only lancé sur un profil ST lit un registre absent, prend une
// bus error, et le gestionnaire du TOS 1.x recharge un A7 corrompu → double faute →
// CPU halté, écran noir. Le comportement est FIDÈLE (Hatari halte à l'identique), mais
// le bandeau de l'interface se contentait de « check the machine profile first » : rien
// n'indiquait QUEL matériel manquait ni QUOI choisir. Cas vécu (Stardust, rapport du
// 2026-09-02) : lecture de $FF8900 — le son DMA du STE — sur un profil ST.
struct MissingHw { const char* chip; const char* needs; };
inline bool mmioNeedsBetterMachine(uint32_t addr, MachineType cur, MissingHw& out) {
    const uint32_t a = addr & 0x00FFFFFFu;          // le 68000 n'a que 24 bits d'adresse
    const bool ste  = (cur == MachineType::Ste  || cur == MachineType::MegaSte);
    const bool mega = (cur == MachineType::MegaSt || cur == MachineType::MegaSte);
    // Son DMA + Microwire/LMC1992 : STE et Mega STE seulement.
    if (a >= 0xFF8900u && a <= 0xFF893Fu && !ste) { out = {"STE DMA sound", "ste"}; return true; }
    if (a >= 0xFF8920u && a <= 0xFF8925u && !ste) { out = {"STE Microwire / LMC1992", "ste"}; return true; }
    // Joypads/paddles STE ($FF9200-$FF921F).
    if (a >= 0xFF9200u && a <= 0xFF921Fu && !ste) { out = {"STE joypads", "ste"}; return true; }
    // Blitter : Mega ST, STE et Mega STE — absent du ST nu.
    if (a >= 0xFF8A00u && a <= 0xFF8A3Fu && !(mega || ste)) { out = {"Blitter", "megast or ste"}; return true; }
    // SCU et SCC : Mega STE (et TT, non émulé).
    if (a >= 0xFF8E00u && a <= 0xFF8E0Fu && cur != MachineType::MegaSte) { out = {"Mega STE SCU", "megaste"}; return true; }
    if (a >= 0xFF8C80u && a <= 0xFF8C87u && cur != MachineType::MegaSte) { out = {"SCC 85C30", "megaste"}; return true; }
    // [D] Hatari ioMem.c:206-212 @ f0736b24 — registre cache/CPU du Mega STE ($FF8E21,
    // octets voisins $FF8E20,22,23 void), absent du STE ; c'est le premier accès d'un TOS 2.x
    // ou d'un programme qui règle les 16 MHz.
    if (a >= 0xFF8E20u && a <= 0xFF8E23u && cur != MachineType::MegaSte) { out = {"Mega STE cache/CPU control", "megaste"}; return true; }
    // [D] Hatari ioMem.c:231-232 et ioMemTabSTE.c:134 @ f0736b24 — densité DD/HD du
    // contrôleur disquette ($FF860E), câblée sur Mega STE ; le STE la lit « void »
    // (pas de faute, donc pas de bandeau), le ST et le Mega ST fautent.
    if (a >= 0xFF860Eu && a <= 0xFF860Fu && cur != MachineType::MegaSte) { out = {"floppy density (HD) register", "megaste"}; return true; }
    // MC68881 optionnel du Mega STE ($FFFA40-$FFFA5F, cf. io/Fpu.hpp). Il ne faute
    // QUE si le socket est vide : une faute ici, même sur Mega STE, veut dire « sans FPU ».
    if (a >= 0xFFFA40u && a <= 0xFFFA5Fu) { out = {"MC68881 FPU", "megaste with the FPU fitted"}; return true; }
    return false;
}

// --- En-tête système d'une image TOS ----------------------------------------
// [C] En-tête OSHEADER du TOS (TOS 1.00 à 4.x, EmuTOS ; relevé sur les images de
// roms/) : version en mot big-endian à l'offset 2, os_base (adresse où la ROM se
// croit implantée) en long big-endian à l'offset 8 ; EmuTOS signe « ETOS » à $2C.
inline uint16_t tosHeaderVersion(const uint8_t* img, std::size_t n) {
    return n >= 4 ? uint16_t((img[2] << 8) | img[3]) : 0;
}
inline bool tosHeaderIsEmuTos(const uint8_t* img, std::size_t n) {
    return n >= 0x30 && img[0x2C] == 'E' && img[0x2D] == 'T' && img[0x2E] == 'O' && img[0x2F] == 'S';
}

// Adresse d'implantation d'une image TOS ($FC0000 ou $E00000).
// L'en-tête fait foi : c'est lui que le code de la ROM suppose (vecteur PC de reset,
// adresses absolues). Avant, la TAILLE seule tranchait (≤ 192 Ko → $FC0000) et une
// image TOS 1.06 US de 192 Ko, qui s'annonce à $E00000 (PC $E00030), était posée à
// $FC0000 : le vecteur de reset pointait dans le vide, le 68000 haltait au reset.
// [D] Hatari tos.c:1028,1042-1045 @ f0736b24 — os_base lu à l'offset 8 ; seules
// $E00000 et $FC0000 sont admises, $FC0000 jusqu'à 224 Ko.
// Repli sur la taille quand l'en-tête est inexploitable (image de test sans en-tête,
// TOS 0.00 de 16 Ko) : c'était la règle d'avant, rien ne change pour ces images.
// `fromHeader` (optionnel) dit laquelle des deux règles a tranché.
inline uint32_t tosImageBase(const uint8_t* img, std::size_t n, bool* fromHeader = nullptr) {
    const uint32_t hdr = n >= 12
        ? (uint32_t(img[8]) << 24) | (uint32_t(img[9]) << 16) | (uint32_t(img[10]) << 8) | img[11]
        : 0;
    const bool okE00 = hdr == 0xE00000u;
    const bool okFC0 = hdr == 0xFC0000u && n <= 224u * 1024u;
    if (fromHeader) *fromHeader = okE00 || okFC0;
    if (okE00) return 0xE00000u;
    if (okFC0) return 0xFC0000u;
    return n <= 192u * 1024u ? 0xFC0000u : 0xE00000u;
}

// Machine NON émulée visée par un TOS : "TT" (TOS 3.0x), "Falcon" (TOS 4.x et 2.07,
// le TOS du prototype « Sparrow » du Falcon), sinon nullptr.
// [D] Hatari tos.c:857-879 @ f0736b24 — TOS_CheckSysConfig bascule ces versions en
// TT / Falcon ; tos.c:103,487 @ f0736b24 — 2.07 = matériel « Sparrow » (Falcon).
// Ces ROM sont du code 68030 qui touche des registres TT/Falcon (TOS 4.04 écrit
// $FF8940, absent du ST) : sur un ST elles glissent en bus error, écran noir, sans
// un mot. NeoST les refuse au chargement — le TT et le Falcon sont le cap suivant
// (docs/HYPOTHESES_ST.md), rien n'en est émulé.
// Exception, comme Hatari (tos.c:1054-1059 @ f0736b24) : EmuTOS ≥ 512 Ko sert toutes
// les machines et n'est pas jugé sur sa version.
inline const char* tosUnsupportedMachine(uint16_t ver, bool emuTos, std::size_t size) {
    if (emuTos && size >= 512u * 1024u) return nullptr;
    if ((ver & 0x0F00u) == 0x0300u) return "TT";
    if ((ver & 0x0F00u) == 0x0400u || ver == 0x0207u) return "Falcon";
    return nullptr;
}

inline MachineType parseMachine(const std::string& s) {
    if (s == "st")      return MachineType::St;
    if (s == "megast")  return MachineType::MegaSt;
    if (s == "megaste") return MachineType::MegaSte;
    // Valeur inconnue → défaut STE, mais EN LE DISANT : « mega-ste » ou « ST » (casse)
    // silencieusement remplacés faisaient diffier l'utilisateur contre le mauvais profil.
    if (s != "ste" && !s.empty())
        std::fprintf(stderr, "[config] unknown machine \"%s\" → ste (valid: st, megast, ste, megaste)\n", s.c_str());
    return MachineType::Ste;                 // défaut : 1040 STE (son DMA, pas de blitter)
}

// --- Taille de ST-RAM (256 Ko .. 4 Mo) --------------------------------------
// Les tailles exposées sont les combinaisons RÉELLES des deux banques MMU
// (128K/512K/2M chacune, cf. Bus.cpp ramBanks / STMemory_RAM_SetBankSize) :
// 2176k = 2M + 128K et 2560k = 2M + 512K (extensions courantes à barrettes mixtes).
// Choisie avant le boot ; EmuTOS la détecte en sondant la mémoire (la RAM répond
// jusqu'à sa taille, échoue au-delà → phystop correct, validé en headless).
inline std::size_t parseRamBytes(const std::string& s) {
    if (s == "256k") return 256u * 1024;
    if (s == "512k") return 512u * 1024;
    if (s == "1m")   return 1024u * 1024;
    if (s == "2m")   return 2048u * 1024;
    if (s == "2176k") return 2176u * 1024;                    // 2M + 128K
    if (s == "2560k" || s == "2.5m") return 2560u * 1024;     // 2M + 512K
    if (s == "4m")   return 4096u * 1024;
    if (!s.empty())                              // même politique que parseMachine : défaut ANNONCÉ
        std::fprintf(stderr, "[config] unknown mem \"%s\" → 512k (valid: 256k, 512k, 1m, 2m, 2176k, 2560k, 4m)\n", s.c_str());
    return 512u * 1024;                          // défaut : 512 Ko
}
inline const char* ramLabel(std::size_t bytes) {
    switch (bytes / 1024) {
        case 256:  return "256k";
        case 512:  return "512k";
        case 1024: return "1m";
        case 2048: return "2m";
        case 2176: return "2176k";
        case 2560: return "2560k";
        case 4096: return "4m";
    }
    return "512k";
}
// Libellé LISIBLE d'une taille de RAM (barre d'état du GUI) : « 512 KB », « 1 MB »,
// « 2.5 MB », « 2176 KB ». Calculé depuis les OCTETS et non recopié d'une liste de
// noms : la chaîne de ternaires d'AppLoop affichait « 4 MB » pour 2176k/2560k,
// ajoutées ailleurs sans elle.
inline std::string ramDisplayName(std::size_t bytes) {
    const std::size_t kb = bytes / 1024;
    if (kb >= 1024 && kb % 1024 == 0) return std::to_string(kb / 1024) + " MB";
    if (kb >= 1024 && kb % 512 == 0)  return std::to_string(kb / 1024) + ".5 MB";
    return std::to_string(kb) + " KB";
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
        case 2176: return 0x08;   // 2M  + 128K (bits 0-1 = 00 → banque 1 en 128K)
        case 2560: return 0x09;   // 2M  + 512K
        case 4096: return 0x0A;   // 2M  + 2M
    }
    return 0x04;
}

// Matériel optionnel présent selon le modèle.
inline bool machineHasDmaSound(MachineType t) {
    return t == MachineType::Ste || t == MachineType::MegaSte;   // son numérique STE
}
inline bool machineHasBlitter(MachineType t) {
    // Blitter présent sur Mega ST, STE et Mega STE ; absent du STF d'origine.
    return t == MachineType::MegaSt || t == MachineType::Ste || t == MachineType::MegaSte;
}
// Machines « Mega » à chipset combiné IMP (GLUE+MMU+Shifter en un ASIC) : le
// décodage d'adresses diffère du ST/STE discret — certaines zones réservées y
// sont « void » (lecture sans bus error) au lieu de fauter. Réf. Hatari
// ioMem.c (IoMem_FixVoidAccessForMegaST). C'est un des signaux de détection de
// modèle qu'EmuTOS lit au boot.
// Famille STE (STE / Mega STE) : le MMU entrelace RAS/CAS différemment du STF
// (cf. Hatari STMemory_MMU_Translate_Addr_STE vs _STF).
inline bool machineIsSte(MachineType t) {
    return t == MachineType::Ste || t == MachineType::MegaSte;
}
inline bool machineIsMega(MachineType t) {
    return t == MachineType::MegaSt || t == MachineType::MegaSte;
}
