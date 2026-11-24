// =============================================================================
//  StJoyBits.hpp — Bits d'état d'un port joystick ST, SANS dépendance hôte.
//
//  L'octet que reçoivent Machine::setJoysticks (Ikbd::setJoystick et les ports
//  manette du STE, Bus::setStePadInput). Il vivait dans io/JoystickInput.hpp, qui
//  inclut GLFW : un frontend sans GLFW (Android, libretro — docs/LIBRETRO.md) devait
//  le recopier en littéraux.
//
//  Format d'octet IKBD (cf. Hatari ATARIJOY_BITMASK_*, includes/joy.h) :
//    bit0 = haut, bit1 = bas, bit2 = gauche, bit3 = droite, bit7 = feu.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

namespace stjoy {

// Bits d'état joystick ST (cf. Hatari joy.h).
enum : uint8_t { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, FIRE = 0x80 };

}  // namespace stjoy
