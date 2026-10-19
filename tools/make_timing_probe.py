#!/usr/bin/env python3
"""Cartouches libres de mesure des entrées IRQ (timers MFP et son DMA STE).

Chaque cartouche prend la main avant TOS, arme une seule source et attend en
STOP IPL5. L'adresse du RTE est le repère temporel : aucune dépendance aux
messages de diagnostic propres aux deux émulateurs.
"""
import struct
from timing_extended import EXTENDED, build_extended

CASES = ("timer_a", "timer_b", "timer_c", "timer_d", "dma_sound") + EXTENDED


def build(case):
    if case in EXTENDED:
        return build_extended(case)
    if case not in CASES:
        raise ValueError(case)
    code = bytearray(struct.pack(">I", 0xFA52235F))

    def w(*words):
        for word in words:
            code.extend(struct.pack(">H", word & 0xFFFF))

    def long(value):
        code.extend(struct.pack(">I", value))

    def byte(address, value):
        w(0x11FC, value, address)  # move.b #imm,abs.w

    w(0x46FC, 0x2700)             # IRQ masquées pendant l'initialisation
    w(0x2E7C); long(0x10000)      # movea.l #$10000,sp
    byte(0x820A, 2)              # 50 Hz PAL
    for address in (0xFA07, 0xFA09, 0xFA13, 0xFA15, 0xFA19, 0xFA1B, 0xFA1D):
        byte(address, 0)
    byte(0xFA17, 0x40)           # vecteurs $40, EOI automatique
    byte(0xFA03, 0)              # TAI sur front descendant
    timer = "a" if case == "dma_sound" else case[-1]
    vector, enable, mask, bit, data, control, value, reload = {
        "a": (0x134, 0xFA07, 0xFA13, 0x20, 0xFA1F, 0xFA19, 1, 192),
        "b": (0x120, 0xFA07, 0xFA13, 1, 0xFA21, 0xFA1B, 2, 83),
        "c": (0x114, 0xFA09, 0xFA15, 0x20, 0xFA23, 0xFA1D, 0x50, 37),
        "d": (0x110, 0xFA09, 0xFA15, 0x10, 0xFA25, 0xFA1D, 7, 17),
    }[timer]
    w(0x21FC); handler_fix = len(code); long(0); w(vector)
    byte(enable, bit)
    byte(mask, bit)
    byte(data, 1 if case == "dma_sound" else reload)
    arm = 0xFA0000 + len(code)
    byte(control, 8 if case == "dma_sound" else value)
    if case == "dma_sound":
        # 256 octets à 50066 Hz stéréo, repeat ; RAM initialisée par reset.
        # Le contenu audio n'entre pas dans la mesure des fronts XSINT → TAI.
        # Sous 128 Ko : la MMU n'a pas encore été configurée par TOS.
        for address, value in ((0x8903, 1), (0x8905, 0x80), (0x8907, 0),
                               (0x890F, 1), (0x8911, 0x81), (0x8913, 0),
                               (0x8921, 3), (0x8901, 3)):
            if address == 0x8901:
                arm = 0xFA0000 + len(code)
            byte(address, value)
    idle = len(code)
    w(0x4E72, 0x2500)             # STOP : seule l'IRQ MFP niveau 6 réveille
    w(0x60FA)                     # bra.s idle
    handler = 0xFA0000 + len(code)
    w(0x4E73)                     # RTE, horodaté à l'entrée du handler
    struct.pack_into(">I", code, handler_fix, handler)
    assert code[idle:idle+2] == b"\x4e\x72"
    return bytes(code), {"handler": handler, "arm": arm}
