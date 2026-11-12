// =============================================================================
//  RomProbe — « ce fichier est-il un TOS ? une disquette ? » (2026-11-12).
//
//  Le GUI prenait son premier argument positionnel pour une ROM SANS le regarder :
//  `neost jeu.st` chargeait la disquette dans la fenêtre ROM (Bus::loadTos accepte
//  tout fichier de 1 octet à 1 Mo), le 68000 partait dans le décor, et le rom= de
//  neost.cfg était réécrit avec le chemin de la disquette — le lancement suivant,
//  sans argument, repartait sur la même ROM absurde. Le glisser-déposer, lui,
//  tranchait déjà sur l'en-tête et la taille ; la règle vit désormais ICI, une seule
//  fois, sans GLFW ni ImGui, pour que neost-selftest la verrouille.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace neost::romprobe {

// [C] En-tête système du TOS (TOS 1.00 à 4.x, EmuTOS) : BRA.S vers le code de
// démarrage en premier mot — $601E sur TOS 1.00, $602E ensuite — puis la version
// en mot big-endian à l'offset 2, plausible de 1.00 à 4.xx.
bool tosHeader(const uint8_t h[4]);

// Taille d'une image ROM connue : 192, 256, 512 Ko ou 1 Mo, à 1 Ko près (des dumps
// à un octet près circulent — tos104de.img fait 196 607 octets — et Bus::loadTos
// les accepte), jamais au-delà de la fenêtre ROM de 1 Mo.
bool tosRomSize(std::uintmax_t size);

// Le fichier a-t-il l'en-tête d'un TOS ? `size` reçoit sa taille (0 si illisible).
bool looksLikeTosHeader(const std::string& path, std::uintmax_t& size);

// En-tête ET taille : le fichier sera chargé comme un TOS.
bool looksLikeTosRom(const std::string& path);

// Extension d'une image disquette que le lecteur monte (.st .msa .dim .stx .ipf,
// casse ignorée) — la même liste que le glisser-déposer et les bibliothèques.
bool isFloppyImageName(const std::string& path);

// Premier argument positionnel du GUI : ROM, disquette, ou ni l'un ni l'autre.
enum class Positional { Rom, Floppy, Invalid };
Positional classifyPositional(const std::string& resolvedPath);

} // namespace neost::romprobe
