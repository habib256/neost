#!/usr/bin/env python3
# =============================================================================
#  make_rtl_screen.py — Disquette .ST bootable qui affiche l'écran du banc GSTMCU.
#
#  POURQUOI. Pour comparer NeoST au modèle RTL (tools/run_gstmcu_bench.py), il faut le
#  MÊME écran des deux côtés. La mémoire vidéo est une suite xorshift 16 bits : assez
#  chaotique pour qu'un seul alignement coïncide, et assez courte pour tenir dans le
#  secteur de boot. Couleur 0 non noire : elle distingue bordure et noir de BLANK.
#
#  Usage : make_rtl_screen.py <sortie.st>   (« ntsc » dans le nom → 60 Hz, « med » → moyenne résolution)
#
#  Le 68000 est BIG-ENDIAN : mots assemblés octet par octet. Secteur de boot
#  exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import struct
import sys

PALETTE = [0x0A5, 0xF00, 0x0F0, 0x00F, 0xFF0, 0xF0F, 0x0FF, 0x123,
           0x456, 0x789, 0xABC, 0xDEF, 0x314, 0x159, 0x265, 0x358]
SEED = 0xACE1
SCREEN = 0x020000
WORDS = 16000


def screen_words(seed: int = SEED) -> list[int]:
    out, x = [], seed
    for _ in range(WORDS):
        x ^= (x << 7) & 0xFFFF
        x ^= x >> 9
        x ^= (x << 8) & 0xFFFF
        out.append(x)
    return out


def boot_disk(ntsc: bool, med: bool = False) -> bytes:
    code = bytearray()

    def w(*words):
        for x in words:
            code.extend(struct.pack('>H', x & 0xFFFF))

    w(0x601C)                          # bra.s code (saute le BPB)
    while len(code) < 0x1E:
        code.append(0)
    w(0x46FC, 0x2700)                  # move.w #$2700,sr
    if med:
        w(0x11FC, 0x0001, 0x8260)      # move.b #1,$8260.w  → moyenne résolution
    else:
        w(0x4238, 0x8260)              # clr.b $8260.w      → basse résolution
    if ntsc:
        w(0x4238, 0x820A)              # clr.b $820a.w      → 60 Hz
    else:
        w(0x11FC, 0x0002, 0x820A)      # move.b #2,$820a.w  → 50 Hz
    w(0x11FC, SCREEN >> 16, 0x8201)    # base vidéo haute
    w(0x4238, 0x8203)                  # base vidéo moyenne
    w(0x4238, 0x820D)                  # base vidéo basse (STE)
    w(0x4238, 0x8265)                  # scroll fin (STE)
    w(0x4238, 0x820F)                  # décalage de ligne (STE)
    for i, color in enumerate(PALETTE):
        w(0x31FC, color, 0x8240 + 2 * i)   # move.w #couleur,$82xx.w
    w(0x207C, SCREEN >> 16, SCREEN & 0xFFFF)   # movea.l #SCREEN,a0
    w(0x303C, WORDS - 1)               # move.w #WORDS-1,d0
    w(0x323C, SEED)                    # move.w #SEED,d1
    w(0x7609)                          # moveq #9,d3
    loop = len(code)
    w(0x3401, 0xEF4A, 0xB541)          # move.w d1,d2 ; lsl.w #7,d2 ; eor.w d2,d1
    w(0x3401, 0xE66A, 0xB541)          # move.w d1,d2 ; lsr.w d3,d2 ; eor.w d2,d1
    w(0x3401, 0xE14A, 0xB541)          # move.w d1,d2 ; lsl.w #8,d2 ; eor.w d2,d1
    w(0x30C1)                          # move.w d1,(a0)+
    w(0x51C8, (loop - (len(code) + 2)) & 0xFFFF)   # dbra d0,boucle (base : mot d'extension)
    w(0x60FE)                          # bra.s *
    assert len(code) <= 510

    boot = bytearray(512)
    boot[:len(code)] = code
    struct.pack_into('<H', boot, 0x0B, 512)
    boot[0x0D] = 2
    struct.pack_into('<H', boot, 0x0E, 1)
    boot[0x10] = 2
    struct.pack_into('<H', boot, 0x11, 112)
    struct.pack_into('<H', boot, 0x13, 1440)
    boot[0x15] = 0xF9
    struct.pack_into('<H', boot, 0x16, 5)
    struct.pack_into('<H', boot, 0x18, 9)
    struct.pack_into('<H', boot, 0x1A, 2)
    struct.pack_into('>H', boot, 0x1FE, 0)
    total = sum(struct.unpack('>256H', bytes(boot))) & 0xFFFF
    struct.pack_into('>H', boot, 0x1FE, (0x1234 - total) & 0xFFFF)
    return bytes(boot) + bytes(1439 * 512)


if __name__ == "__main__":
    path = sys.argv[1]
    with open(path, 'wb') as f:
        f.write(boot_disk('ntsc' in path, 'med' in path))
    print(f"écrit {path}")
