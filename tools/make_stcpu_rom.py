#!/usr/bin/env python3
# =============================================================================
#  make_stcpu_rom.py — ROM du banc 68000 + GSTMCU : le CPU relève le compteur vidéo en RAM.
#
#  Le programme ne dépend ni de TOS ni du matériel absent (MFP, FDC). Il pose l'écran, attend la VBL par
#  STOP, puis remplit des tables. Assemblage à la main ; [F] encodages relus sans outil.
#
#  Tables, en RAM : voir LAYOUT. Le compteur $FF8209 est lu par « move.b (a1),(a0)+ », 12 cycles.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROM = 0xE00000
SCREEN = 0x10000
POLL0 = 0x20000     # 3 tables de 16000 octets, décalées de 4 cycles chacune
POLL_LEN = 16000
HBLTAB = 0x30000    # compteur lu à chaque HBL, en plein DE : 256 octets par forme de boucle
VBLTAB = 0x30400    # compteur lu à chaque VBL, 32 octets
HBL_LOOPS = 4       # NOP + BRA ; NOP seuls ; STOP ; MULU
LAYOUT = {"poll": POLL0, "hbl": HBLTAB, "vbl": VBLTAB}
TOTAL = 0x30600 - POLL0


class P:
    def __init__(self):
        self.c = bytearray(0x30)
        self.c[0:4] = struct.pack(">HH", 0x602E, 0x0206)         # en-tête façon TOS 2.06
        self.c[4:8] = struct.pack(">I", ROM + 0x30)              # PC de reset
        self.labels, self.fix = {}, []

    def w(self, *ws):
        for x in ws:
            self.c += struct.pack(">H", x & 0xFFFF)
        return self

    def l(self, x):
        self.c += struct.pack(">I", x & 0xFFFFFFFF)
        return self

    def label(self, n):
        self.labels[n] = len(self.c)
        return self

    def b8(self, op, target):
        self.fix.append((len(self.c), target))
        return self.w(op)

    def addr(self, n):
        self.fix.append((len(self.c), "=" + n))
        return self.l(0)

    def done(self):
        for off, t in self.fix:
            if t[0] == "=":
                struct.pack_into(">I", self.c, off, ROM + self.labels[t[1:]])
            else:
                d = self.labels[t] - (off + 2)
                assert -128 <= d <= 127 and d != 0, t
                self.c[off + 1] = d & 0xFF
        return bytes(self.c).ljust(256 * 1024, b"\xFF")


def movebi(p, val, addr):           # move.b #val,addr.l
    p.w(0x13FC, val & 0xFF).l(addr)


def lea_a0(p, addr):                # lea addr.l,a0
    p.w(0x41F9).l(addr)


def build() -> bytes:
    p = P()
    p.w(0x46FC, 0x2700)                                # move #$2700,sr
    p.w(0x4FF9).l(0x8000)                              # lea $8000,sp
    movebi(p, 0x0A, 0xFF8001)                          # MMU : deux bancs de 2 Mo
    movebi(p, SCREEN >> 16, 0xFF8201)
    movebi(p, (SCREEN >> 8) & 0xFF, 0xFF8203)
    movebi(p, SCREEN & 0xFF, 0xFF820D)
    movebi(p, 0, 0xFF8260)
    movebi(p, 2, 0xFF820A)
    p.w(0x21FC).addr("hbl"); p.w(0x0068)               # move.l #hbl,$68.w
    p.w(0x21FC).addr("vbl"); p.w(0x0070)               # move.l #vbl,$70.w
    p.w(0x45F9).l(VBLTAB)                              # lea VBLTAB,a2 (table de la VBL)
    p.w(0x47F9).l(HBLTAB)                              # lea HBLTAB,a3 (table de la HBL)
    p.w(0x43F9).l(0xFF8209)                            # lea $FF8209,a1
    for _ in range(3):                                 # laisser le mode se poser
        p.w(0x4E72, 0x2300)                            # stop #$2300 : réveil par la VBL
    # Trois tables : la même boucle, partie 0, 4 ou 8 cycles plus tard après la VBL.
    for k in range(3):
        p.w(0x4E72, 0x2300)
        p.w(0x46FC, 0x2700)
        p.w(*([0x4E71] * k))                           # nop
        lea_a0(p, POLL0 + k * POLL_LEN)
        p.w(0x303C, POLL_LEN - 1)                      # move.w #n,d0
        p.label(f"poll{k}")
        p.w(0x10D1)                                    # move.b (a1),(a0)+
        p.fix.append((len(p.c), f"poll{k}"))
        p.w(0x51C8, 0)                                 # dbra d0
    # Relevés par HBL : le gestionnaire attend le milieu de DE, où le compteur date sa lecture à
    # 4 cycles près. Une table par forme de boucle, car la latence dépend de l'instruction coupée.
    # Le gestionnaire compte ses 256 lectures dans D6 ; la boucle teste D6 et passe à la suivante.
    for k in range(HBL_LOOPS):
        p.w(0x47F9).l(HBLTAB + k * 256)                # lea table,a3
        p.w(0x7C00)                                    # moveq #0,d6
        p.w(0x4E72, 0x2300)                            # stop #$2300 : part sur une VBL
        p.w(0x46FC, 0x2100)                            # HBL autorisée
        p.label(f"loop{k}")
        if k == 0:
            p.w(0x4E71)
        elif k == 1:
            p.w(*([0x4E71] * 60))
        elif k == 2:
            p.w(0x4E72, 0x2100)                        # stop #$2100
        else:
            p.w(0x7EFF)                                # moveq #-1,d7
            p.w(0xCEC7)                                # mulu d7,d7
        p.w(0x0C46, 0x00FF)                            # cmpi.w #255,d6
        p.fix.append((len(p.c), f"loop{k}"))
        p.w(0x6F00, 0)                                 # ble.w loop
        p.w(0x46FC, 0x2700)
    p.w(0x4E72, 0x2300)                                # fin : VBL seules
    p.label("end_")
    p.w(0x4E72, 0x2300)
    p.b8(0x6000, "end_")
    p.label("hbl")
    p.w(*([0x4E71] * 40))                              # 160 cycles : plein DE
    p.w(0x16D1)                                        # move.b (a1),(a3)+
    p.w(0x5246)                                        # addq.w #1,d6
    p.w(0x4E73)                                        # rte
    p.label("vbl")
    p.w(0x14D1)                                        # move.b (a1),(a2)+
    p.w(0x4E73)
    # Les DBRA ci-dessus : déplacement relatif sur 16 bits, résolu ici.
    img = bytearray(p.done_dbra())
    return bytes(img)


def done_dbra(self):
    for off, t in self.fix:
        if t[0] == "=":
            struct.pack_into(">I", self.c, off, ROM + self.labels[t[1:]])
        elif self.c[off] in (0x51, 0x6F):
            struct.pack_into(">h", self.c, off + 2, self.labels[t] - (off + 2))
        else:
            d = self.labels[t] - (off + 2)
            assert -128 <= d <= 127 and d != 0, t
            self.c[off + 1] = d & 0xFF
    return bytes(self.c).ljust(256 * 1024, b"\xFF")


P.done_dbra = done_dbra

if __name__ == "__main__":
    Path(sys.argv[1]).write_bytes(build())
