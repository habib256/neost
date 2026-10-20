#!/usr/bin/env python3
# =============================================================================
#  make_spec512_view.py — disquette .ST bootable qui affiche une image à la
#  manière d'un visualiseur Spectrum 512 : 48 couleurs par ligne, les 16
#  registres de palette rechargés TROIS fois par ligne par rafales movem.l.
#  Chantier A10.
#
#  POURQUOI. `spec512_bands` (make_spec512_test.py) rend la couverture « palette
#  changée en cours de ligne », mais sur UN registre écrit par move.w. Les trois
#  étalons spectrum512_diapo* exerçaient le vrai motif d'un visualiseur — rafales
#  movem.l de 16 couleurs, chaque registre changé à son propre cycle — et ils
#  dépendent d'un TOS Atari (diapo lancée par un dossier AUTO qu'EmuTOS n'exécute
#  pas jusqu'au bout). Celui-ci rend ce motif sans une ligne de code propriétaire.
#
#  COMMENT. Basse résolution 50 Hz. RAM écran : blocs de 16 pixels, le bloc b
#  portant l'index b mod 16 (20 blocs par ligne). Un tampon de 199 × 48 couleurs
#  est calculé au boot (valeur courante += $0135, masquée à $777). Le handler de
#  VBL, ancré par `stop #$2300` (latence d'exception fixe, cf. make_spec512_test),
#  attend le haut de l'écran puis, pour chaque ligne :
#      3 × ( lea $FF8240,a0 ; movem.l (a4)+,d0-d6/a1 ; movem.l d0-d6/a1,(a0) )
#  Une ligne de boucle ne dure pas exactement 512 cycles : le motif glisse d'une
#  ligne à l'autre, mais il est identique à chaque trame. C'est le rendu comparé
#  à l'oracle Hatari qui fait foi, pas une image « attendue ».
#
#  Le 68000 est BIG-ENDIAN : tous les mots sont assemblés octet par octet.
#  Secteur de boot exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  Usage : make_spec512_view.py [sortie.st] [délai_avant_première_ligne]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import struct
import sys

code = bytearray()
labels = {}
fixups = []


def w(*words):
    for x in words:
        code.extend(struct.pack('>H', x & 0xFFFF))


def l(x):
    code.extend(struct.pack('>I', x & 0xFFFFFFFF))


def label(name):
    labels[name] = len(code)


def bcc_s(op, name):           # bra.s / bne.s : déplacement 8 bits
    fixups.append((len(code), name))
    code.extend(bytes([op, 0x00]))


def dbra(reg, name):           # dbra Dn,label — déplacement 16 bits
    w(0x51C8 | (reg & 7))
    fixups.append((len(code), name))
    code.extend(b'\x00\x00')


SCREEN = 0x020000
PALBUF = 0x030000              # après les 32 000 octets d'écran
LINES = 199
# Itérations de dbra (~10 cycles) entre l'entrée du handler et la première ligne :
# la bordure haute dure ~63 lignes de 512 cycles. Réglable en 2ᵉ argument.
DELAY = int(sys.argv[2]) if len(sys.argv) > 2 else 2900
MOVEM_REGS = 0x027F            # d0-d6/a1 : 8 longs = 16 couleurs

# ---- secteur de boot : offset 0 = bra.s vers le code (saute le BPB) ----------
bcc_s(0x60, 'code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr   (superviseur, IPL7)
w(0x4238, 0x8260)                 # clr.b $8260.w      → basse résolution
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w  → 50 Hz
w(0x11FC, SCREEN >> 16, 0x8201)   # base vidéo haute
w(0x4238, 0x8203)                 # base vidéo moyenne = 0

# ---- RAM écran : bloc b → index b mod 16 ----------------------------------
w(0x207C); l(SCREEN)              # movea.l #SCREEN,a0
w(0x303C, 200 - 1)                # move.w #199,d0     (200 lignes)
label('scrline')
w(0x7A00)                         # moveq #0,d5        (bloc courant)
label('block')
for plane in range(4):            # plan p = bit p de l'index
    w(0x0805, plane)              # btst #p,d5
    w(0x56C1)                     # sne d1
    w(0x4881)                     # ext.w d1           → $FFFF ou 0
    w(0x30C1)                     # move.w d1,(a0)+
w(0x5245)                         # addq.w #1,d5
w(0x0C45, 20)                     # cmpi.w #20,d5
bcc_s(0x66, 'block')              # bne.s block
dbra(0, 'scrline')

# ---- tampon de palettes : 199 lignes × 48 couleurs ------------------------
w(0x227C); l(PALBUF)              # movea.l #PALBUF,a1
w(0x303C, LINES * 48 - 1)         # move.w #(199*48-1),d0
w(0x7800)                         # moveq #0,d4
label('pal')
w(0x3204)                         # move.w d4,d1
w(0x0241, 0x0777)                 # andi.w #$777,d1
w(0x32C1)                         # move.w d1,(a1)+
w(0x0644, 0x0135)                 # addi.w #$0135,d4
dbra(0, 'pal')

# ---- ancrage : VBL + STOP (latence d'exception fixe) ----------------------
w(0x4238, 0xFA07)                 # clr.b $fffa07.w   (IERA)
w(0x4238, 0xFA09)                 # clr.b $fffa09.w   (IERB)
w(0x43FA)                         # lea vbl(pc),a1
fixups.append((len(code), 'vbl')); code.extend(b'\x00\x00')
w(0x21C9, 0x0070)                 # move.l a1,$70.w   (autovecteur niveau 4)
w(0x46FC, 0x2300)                 # move.w #$2300,sr
label('idle')
w(0x4E72, 0x2300)                 # stop #$2300
bcc_s(0x60, 'idle')

label('vbl')
w(0x287C); l(PALBUF)              # movea.l #PALBUF,a4
w(0x363C, DELAY)                  # move.w #DELAY,d3
label('wait')
dbra(3, 'wait')
w(0x3E3C, LINES - 1)              # move.w #198,d7
label('line')
for _ in range(3):
    w(0x41F8, 0x8240)             # lea $ff8240.w,a0
    w(0x4CDC, MOVEM_REGS)         # movem.l (a4)+,d0-d6/a1
    w(0x48D0, MOVEM_REGS)         # movem.l d0-d6/a1,(a0)
dbra(7, 'line')
w(0x4E73)                         # rte → retour au stop

# ---- résolution des déplacements -------------------------------------------
for off, name in fixups:
    target = labels[name]
    op = code[off]
    if op in (0x60, 0x66):                        # bra.s / bne.s : 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127, f"branche courte hors portée {name} : {disp}"
        code[off + 1] = disp & 0xFF
    else:                                          # dbra / lea(pc) : 16 bits
        disp = target - off
        assert -32768 <= disp <= 32767, f"déplacement hors portée {name} : {disp}"
        struct.pack_into('>h', code, off, disp)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot 512 o (BPB 720 Ko, little-endian) ---------------------
boot = bytearray(512)
boot[0:len(code)] = code
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


def wsum(b):
    return sum(struct.unpack('>256H', bytes(b))) & 0xFFFF


struct.pack_into('>H', boot, 0x1FE, 0)
struct.pack_into('>H', boot, 0x1FE, (0x1234 - wsum(boot)) & 0xFFFF)
assert wsum(boot) == 0x1234, "secteur de boot non exécutable (somme ≠ $1234)"

img = bytearray(1440 * 512)
img[0:512] = boot
out = sys.argv[1] if len(sys.argv) > 1 else "disks/etalons/spec512_view.st"
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; code={len(code)} o ; délai={DELAY} ; checksum OK (0x1234)")
