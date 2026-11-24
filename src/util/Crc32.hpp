// =============================================================================
//  Crc32.hpp — CRC-32 IEEE (polynôme réfléchi $EDB88320, init $FFFFFFFF, inversion
//  finale), celui de zlib et des en-têtes de save-state NeoST.
//
//  Pourquoi un module : le CRC des états était calculé octet par octet, et il
//  pesait ~80 % d'un saveState et d'un loadState (mesuré le 2026-11-24 sur un état de
//  1,87 Mo : 6,3 ms et 7,6 ms, contre ~1 ms pour émuler une trame). Or l'exécution
//  anticipée et le jeu en réseau de libretro en font un de chaque PAR TRAME
//  (docs/LIBRETRO.md). Valeurs IDENTIQUES à l'octet près : le format des fichiers
//  .state ne bouge pas, les états déjà écrits restent lisibles.
//
//  Trois chemins, même résultat (gardé par neost-selftest) :
//    · crc32()          — le meilleur disponible : instruction CRC32 d'ARMv8 quand le
//                         compilateur l'annonce (__ARM_FEATURE_CRC32 : Apple Silicon),
//                         sinon le découpage par 8 octets ;
//    · crc32Sliced()    — découpage par 8 octets (« slicing-by-8 »), portable ;
//    · crc32Bytewise()  — la table octet par octet d'origine : la RÉFÉRENCE.
//  x86 n'a pas d'instruction pour CE polynôme (SSE 4.2 calcule le CRC-32C) : il prend
//  le découpage par 8.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace neost {

uint32_t crc32(const uint8_t* p, std::size_t n);
uint32_t crc32Sliced(const uint8_t* p, std::size_t n);
uint32_t crc32Bytewise(const uint8_t* p, std::size_t n);
// Vrai si crc32() passe par l'instruction matérielle (diagnostic, selftest).
bool crc32Hardware();

}  // namespace neost
