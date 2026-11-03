#!/usr/bin/env python3
# =============================================================================
#  make_stcpu_raster.py — ROM du banc au pixel : de vraies impulsions écrites par le CPU.
#
#  Le programme affiche l'écran de tools/make_rtl_screen.py, se cale sur le compteur vidéo sans gigue,
#  puis pose une seule impulsion sur $FF8260 ou $FF820A, à une date fixée de la ligne. Le même binaire
#  tourne sur fx68k + GSTMCU et dans NeoST. Un écart au pixel est un écart de la chaîne entière : date
#  CPU, GLUE, compteur, Shifter.
#
#  Passage par le 60 Hz au départ : sans lui, le modèle garde une phase de plans décalée après le
#  reset (cf. tools/gstmcu_bench.cpp). Assemblage à la main ; [F] encodages relus sans outil.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_rtl_screen import PALETTE, screen_words  # noqa: E402
from make_stcpu_rom import P, ROM, movebi  # noqa: E402

SCREEN = 0x10000
SCREEN_ROM = 0x20000            # offset de l'image dans la ROM
NOP = 0x4E71
LINE = 512


def build(kind: str, width: int, date: int) -> bytes:
    """kind : hi ou f60. width : cycles entre le début des deux écritures, 8 au moins. date : cycles
    après une date fixe de la ligne."""
    assert width >= 8 and width % 4 == 0
    p = P()
    p.w(0x46FC, 0x2700)
    p.w(0x4FF9).l(0x8000)
    movebi(p, 0x0A, 0xFF8001)
    movebi(p, SCREEN >> 16, 0xFF8201)
    movebi(p, (SCREEN >> 8) & 0xFF, 0xFF8203)
    movebi(p, SCREEN & 0xFF, 0xFF820D)
    movebi(p, 0, 0xFF8260)
    movebi(p, 0, 0xFF820A)                     # 60 Hz d'abord : cf. en-tête
    for i, c in enumerate(PALETTE):
        p.w(0x33FC, c).l(0xFF8240 + 2 * i)     # move.w #c,reg.l
    p.w(0x21FC).addr("vbl"); p.w(0x0070)
    # Copie de l'écran depuis la ROM.
    p.w(0x41F9).l(ROM + SCREEN_ROM)            # lea image,a0
    p.w(0x43F9).l(SCREEN)                      # lea écran,a1
    p.w(0x303C, 32000 // 4 - 1)                # move.w #n,d0
    p.label("copy")
    p.w(0x22D8)                                # move.l (a0)+,(a1)+
    p.fix.append((len(p.c), "copy"))
    p.w(0x51C8, 0)                             # dbra d0
    for _ in range(3):
        p.w(0x4E72, 0x2300)                    # stop : VBL
    movebi(p, 2, 0xFF820A)                     # 50 Hz
    for _ in range(2):
        p.w(0x4E72, 0x2300)
    # Registres des écritures. a3 = cible ; d4 = activer ; d5 = rétablir.
    target, on, off = (0xFF8260, 2, 0) if kind == "hi" else (0xFF820A, 0, 2)
    p.w(0x47F9).l(target)                      # lea cible,a3
    p.w(0x7800 | on)                           # moveq #on,d4
    p.w(0x7A00 | off)                          # moveq #off,d5
    p.w(0x45F9).l(0xFF8209)                    # lea $FF8209,a2
    p.label("frame")
    p.w(0x4E72, 0x2300)                        # stop : VBL
    p.w(0x46FC, 0x2700)
    # Calage sans gigue sur le premier mot lu : 2 cycles par unité de décalage, le compteur avance de
    # 2 tous les 4 cycles, donc « lsl » rattrape exactement l'écart. Méthode classique des démos.
    p.label("sync")
    p.w(0x1012)                                # move.b (a2),d0
    p.b8(0x6700, "sync")                       # beq.s
    p.w(0x7420)                                # moveq #32,d2
    p.w(0x9400)                                # sub.b d0,d2
    p.w(0xE56E)                                # lsl.w d2,d6
    # La ligne reprend à une date fixe, sans gigue, sur les deux modèles.
    p.w(*([NOP] * (10 * LINE // 4)))           # dix lignes plus bas
    p.w(*([NOP] * (date // 4)))
    p.w(0x1684)                                # move.b d4,(a3)
    p.w(*([NOP] * ((width - 8) // 4)))
    p.w(0x1685)                                # move.b d5,(a3)
    p.w(0x4EF9).addr("frame")                  # jmp frame.l
    p.label("vbl")
    p.w(0x4E73)
    image = bytearray(p.done_dbra())
    assert len(p.c) < SCREEN_ROM, len(p.c)
    words = screen_words()
    image[SCREEN_ROM:SCREEN_ROM + 32000] = b"".join(struct.pack(">H", w) for w in words)
    return bytes(image)


if __name__ == "__main__":
    Path(sys.argv[1]).write_bytes(build(sys.argv[2], int(sys.argv[3]), int(sys.argv[4])))
