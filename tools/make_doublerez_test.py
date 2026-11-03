#!/usr/bin/env python3
"""Disquette libre, STE : résolutions mêlées dans une trame, compteur écrit en bordure droite.

Reproduit la technique de DHS Double Rez Trouble (Sommarhack 2025, STE 1 Mo), que ce banc
garde sans dépendre du disque de la démo. À chaque ligne :
  - impulsion haute résolution aux cycles 504 puis 4 : retrait gauche court du STE (left+2) ;
  - 60 Hz aux cycles 376 puis 384 : retrait droit ;
  - compteur vidéo $FF8207/09 écrit en bordure droite, repris tel quel à la ligne suivante.
La moitié haute repasse en basse résolution au cycle 4, le bas en moyenne, et la trame se
termine en res=1 : la suivante démarre donc en moyenne résolution avec des lignes basses.

La synchronisation lit $FF8209 puis compense par lsl.w : les écritures tombent au cycle près.

Usage : make_doublerez_test.py sortie.st
Outil de test NeoST, domaine public.
"""
from pathlib import Path
import struct
import sys

SCREEN = 0x020000
STRIDE = 0xE0          # une ligne repart de base + n·$E0, quelle que soit la longueur lue
TABLE = 0x010000
RES_TABLE = 0x011000
LOW_LINES = MED_LINES = 90
# Mesuré à la trace Hatari `video_res,video_sync` : hi à 504 sur la première ligne.
LEAD_CYCLES = 336


def make_disk(out: Path) -> None:
    code = bytearray()
    labels, fixups = {}, []

    def w(*words):
        for word in words:
            code.extend(struct.pack('>H', word & 0xFFFF))

    def label(name):
        labels[name] = len(code)

    def relative(name, short=False):
        fixups.append((len(code), name, short))
        w(0x6000 if short else 0)

    def delay(cycles):
        """Délai exact, multiple de 4 : moveq + lsl.l Dx,Dx coûtent 12 + 4k cycles pour 2k."""
        assert cycles % 4 == 0
        while cycles >= 16:
            chunk = min(cycles, 136)
            w(0x7A00 | (chunk - 12) // 2, 0xEBAD)   # moveq #n,d5 ; lsl.l d5,d5
            cycles -= chunk
        for _ in range(cycles // 4):
            w(0x4E71)

    def lines(name, count):
        """Boucle de 512 cycles par ligne ; d1 = 2, d0 = 0, a1 = $8260, a2 = $820A."""
        w(0x3C3C, count - 1)              # move.w #count-1,d6
        label(name)
        w(0x1281)                         # move.b d1,(a1)      hi, cycle 504
        w(0x129E)                         # move.b (a6)+,(a1)   retour lu en table, cycle 4
        delay(364)
        w(0x1480)                         # move.b d0,(a2)      60 Hz, cycle 376
        w(0x1481)                         # move.b d1,(a2)      50 Hz, cycle 384
        delay(76)
        w(0x189B)                         # move.b (a3)+,(a4)   $FF8207, cycle 472
        w(0x1A9B)                         # move.b (a3)+,(a5)   $FF8209, cycle 484
        w(0x51CE); relative(name)         # dbra d6,name

    relative('code', short=True)
    code.extend(bytes(0x1E - len(code)))
    label('code')
    w(0x46FC, 0x2700, 0x4238, 0x8260)     # superviseur IPL7, basse résolution
    w(0x11FC, 2, 0x820A)                  # PAL
    w(0x11FC, SCREEN >> 16, 0x8201, 0x4238, 0x8203, 0x4238, 0x820D)
    for reg in range(16):                 # 16 couleurs distinctes, index 0 vert
        w(0x31FC, 0x0070 if reg == 0 else 0x0100 * (reg & 7) + 0x0011 * (reg >> 1), 0x8240 + reg * 2)
    # Motif : longs successifs, le décalage d'une ligne se voit.
    w(0x207C, SCREEN >> 16, SCREEN & 0xFFFF, 0x303C, 0x3FFF, 0x72FF)
    label('fill')
    w(0x20C1, 0x5281, 0x51C8); relative('fill')
    # Table des adresses de ligne (mot : octet moyen, octet bas).
    w(0x207C, TABLE >> 16, TABLE & 0xFFFF, 0x323C, STRIDE, 0x303C, LOW_LINES + MED_LINES + 1)
    label('table')
    w(0x30C1, 0xD27C, STRIDE, 0x51C8); relative('table')
    # Résolution de chaque ligne : basse, puis moyenne.
    w(0x207C, RES_TABLE >> 16, RES_TABLE & 0xFFFF, 0x303C, LOW_LINES + MED_LINES - 1)
    label('restable')
    w(0x10FC, 0, 0x51C8); relative('restable')
    w(0x207C, (RES_TABLE + LOW_LINES) >> 16, (RES_TABLE + LOW_LINES) & 0xFFFF, 0x303C, MED_LINES - 1)
    label('resmed')
    w(0x10FC, 1, 0x51C8); relative('resmed')
    w(0x4238, 0xFA07, 0x4238, 0xFA09)     # couper les IRQ MFP
    w(0x43FA); relative('vbl')            # lea vbl(pc),a1
    w(0x21C9, 0x0070)
    label('idle')
    w(0x4E72, 0x2300); relative('idle', short=True)
    label('vbl')
    w(0x7000, 0x7202, 0x7401)             # d0 = 0, d1 = 2, d2 = 1
    w(0x43F8, 0x8260, 0x45F8, 0x820A)     # lea $8260.w,a1 ; lea $820A.w,a2
    w(0x47F9, TABLE >> 16, TABLE & 0xFFFF)        # lea table.l,a3
    w(0x4DF9, RES_TABLE >> 16, RES_TABLE & 0xFFFF)  # lea résolutions.l,a6
    w(0x49F8, 0x8207, 0x4BF8, 0x8209)     # lea $8207.w,a4 ; lea $8209.w,a5
    w(0x7E00)                             # moveq #0,d7
    label('sync')
    w(0x1E15, 0x67FC)                     # move.b (a5),d7 ; beq.s sync
    w(0x7620, 0x9607, 0xE76B)             # moveq #32,d3 ; sub.b d7,d3 ; lsl.w d3,d3
    delay(LEAD_CYCLES)
    lines('line', LOW_LINES + MED_LINES)
    w(0x1282)                             # res=1 : la trame suivante démarre en moyenne
    w(0x4E73)

    for offset, target, short in fixups:
        displacement = labels[target] - offset - (2 if short else 0)
        if short:
            assert -128 <= displacement <= 127 and displacement
            code[offset + 1] = displacement & 0xFF
        else:
            struct.pack_into('>h', code, offset, displacement)
    assert len(code) <= 510, len(code)
    boot = bytearray(512)
    boot[:len(code)] = code
    struct.pack_into('<H', boot, 0x0B, 512)
    boot[0x0D] = 2
    struct.pack_into('<H', boot, 0x0E, 1)
    boot[0x10] = 2
    struct.pack_into('<H', boot, 0x11, 112)
    struct.pack_into('<H', boot, 0x13, 1440)
    boot[0x15] = 0xF9
    for offset, value in ((0x16, 5), (0x18, 9), (0x1A, 2)):
        struct.pack_into('<H', boot, offset, value)
    checksum = sum(struct.unpack('>256H', boot)) & 0xFFFF
    struct.pack_into('>H', boot, 0x1FE, (0x1234 - checksum) & 0xFFFF)
    image = bytearray(1440 * 512)
    image[:512] = boot
    out.write_bytes(image)
    print(f'{out}: {len(code)} octets de code, {len(image)} octets')


if __name__ == '__main__':
    make_disk(Path(sys.argv[1]))
