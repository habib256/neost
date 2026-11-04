#!/usr/bin/env python3
# =============================================================================
#  make_stcpu_snd.py — ROM du banc 68000 + GSTMCU : le compteur du son DMA relevé par le CPU.
#
#  Le programme lance le son en boucle, à la fréquence et en mode demandés, attend une VBL par STOP, puis
#  remplit une table de lectures de $FF890D, 24 cycles par lecture. Même binaire sur les RTL et dans NeoST.
#  Échelon `rtl`, grade D. Assemblage à la main ; [F] encodages relus sans outil.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_stcpu_rom import P, movebi  # noqa: E402

TABLE = 0x20000
POLL_LEN = 16000
SAMPLES = 0x40000
SAMPLES_END = 0x44000


def build(mode: int = 3, shifts: int = 0) -> bytes:
    """mode : octet $FF8921 (bit 7 mono ; bits 1-0 : 6, 12, 25, 50 kHz). shifts : NOP avant la table."""
    p = P()
    p.w(0x46FC, 0x2700)
    p.w(0x4FF9).l(0x8000)
    movebi(p, 0x0A, 0xFF8001)
    movebi(p, 0x01, 0xFF8201)
    movebi(p, 0x00, 0xFF8203)
    movebi(p, 0x00, 0xFF820D)
    movebi(p, 0, 0xFF8260)
    movebi(p, 2, 0xFF820A)
    p.w(0x21FC).addr("vbl"); p.w(0x0070)
    movebi(p, SAMPLES >> 16, 0xFF8903)
    movebi(p, (SAMPLES >> 8) & 0xFF, 0xFF8905)
    movebi(p, SAMPLES & 0xFF, 0xFF8907)
    movebi(p, SAMPLES_END >> 16, 0xFF890F)
    movebi(p, (SAMPLES_END >> 8) & 0xFF, 0xFF8911)
    movebi(p, SAMPLES_END & 0xFF, 0xFF8913)
    movebi(p, mode, 0xFF8921)
    p.w(0x4E72, 0x2300)                        # stop : VBL
    p.w(0x4E72, 0x2300)
    movebi(p, 3, 0xFF8901)                     # lecture en boucle
    for _ in range(3):
        p.w(0x4E72, 0x2300)
    p.w(0x46FC, 0x2700)
    p.w(*([0x4E71] * shifts))
    p.w(0x43F9).l(0xFF890D)                    # lea $FF890D,a1
    p.w(0x41F9).l(TABLE)                       # lea table,a0
    p.w(0x303C, POLL_LEN - 1)                  # move.w #n,d0
    p.label("poll")
    p.w(0x10D1)                                # move.b (a1),(a0)+
    p.fix.append((len(p.c), "poll"))
    p.w(0x51C8, 0)                             # dbra d0
    p.label("end")
    p.w(0x4E72, 0x2700)                        # stop
    p.b8(0x6000, "end")
    p.label("vbl")
    p.w(0x4E73)
    return p.done_dbra()


if __name__ == "__main__":
    Path(sys.argv[1]).write_bytes(build())
