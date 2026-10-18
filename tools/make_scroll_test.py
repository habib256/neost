#!/usr/bin/env python3
# =============================================================================
#  make_scroll_test.py — Génère une disquette .ST bootable STE qui active le
#  SCROLL FIN matériel ($FF8264 sans prefetch / $FF8265 avec prefetch) sur un
#  motif discriminant, pour valider le rendu NeoST contre l'oracle Hatari.
#
#  Motif : RAM vidéo remplie en CONTIGU (160 octets/ligne logiques) d'une colonne
#  de pixels blancs (index 1, plan 0 seul) tous les 64 px (1 groupe de 16 px sur
#  4). Ce motif rend visibles LES DEUX effets à valider :
#    • le DÉCALAGE de la ligne par `scroll` pixels (position des colonnes) ;
#    • l'AVANCE DU COMPTEUR par ligne — avec prefetch ($FF8265) le shifter
#      consomme 1 mot PAR PLAN de plus par ligne (+8 octets en basse rés), donc
#      sur un remplissage contigu les colonnes dérivent de 16 px/ligne → DIAGONALE
#      nette ; un pas faux (ex. +2) désaligne les PLANS → couleurs parasites.
#      Sans prefetch ($FF8264) : aucune avance → colonnes VERTICALES, mais les
#      16 premiers px de chaque ligne sont couleur 0 (affichage inset).
#
#  Mode 336 : séquence $FF8265>0 puis $FF8264=0, affichage 16 px plus tôt
#  et stride de 168 octets. Les témoins 336_late (>40 cycles), 336_clear
#  (annulation par une nouvelle écriture) et 336_word (premier accès mot)
#  distinguent ce latch d'un simple scroll fin. 336_split l'active au milieu
#  de l'écran, à chaque VBL (STOP), puis touche la palette plus bas : le replay
#  doit connaître les latches antérieurs même sans capture RAM sur ces lignes.
#  Usage : make_scroll_test.py <out.st> <8265|8264|off|336[_late|_clear|_word|_split]> [scroll=4]
#  Oracle : tools/hatari_oracle.sh <tos> <out.st> 400 380 out.png ste
#
#  Le 68000 est BIG-ENDIAN : mots assemblés octet par octet. Secteur de boot
#  exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import struct, sys

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

def bra_s(name):
    fixups.append((len(code), name))
    code.extend(b'\x60\x00')

out    = sys.argv[1] if len(sys.argv) > 1 else "/tmp/scroll_test.st"
# Mode explicite (argv[2]) ou INFÉRÉ du nom de fichier (« …8264… » / « …8265… ») —
# le runner d'étalons (run_etalons.py, disk_generate) ne passe que le chemin.
if len(sys.argv) > 2:
    mode = sys.argv[2]
elif '8264' in out:
    mode = '8264'
elif '8265' in out:
    mode = '8265'
elif '336' in out:
    mode = next((m for m in ('336_late', '336_clear', '336_word', '336_split') if m in out), '336')
else:
    mode = 'off'
scroll = int(sys.argv[3]) if len(sys.argv) > 3 else 4
assert mode in ('8265', '8264', 'off', '336', '336_late', '336_clear', '336_word', '336_split')
assert 0 <= scroll <= 15

# bra.s code (saute le BPB)
bra_s('code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr
w(0x4238, 0x8260)                 # clr.b $8260.w        → basse résolution
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w    → 50 Hz
w(0x11FC, 0x0002, 0x8201)         # move.b #2,$8201.w    → base vidéo = $020000
w(0x4238, 0x8203)                 # clr.b $8203.w
w(0x4238, 0x820D)                 # clr.b $820d.w        → octet bas STE = 0
w(0x4238, 0x820F)                 # clr.b $820f.w        → line-offset ($FF820F) = 0
w(0x4278, 0x8240)                 # clr.w $8240.w        → palette[0] = noir
w(0x31FC, 0x0777, 0x8242)         # move.w #$777,$8242.w → palette[1] = blanc

# ---- remplissage : 32 Ko depuis $020000, période 32 octets ----
#   long0 = $80000000 (pixel blanc en tête de groupe, plan 0), longs 1-7 = 0.
w(0x207C); l(0x00020000)          # movea.l #$00020000,a0
w(0x303C, 0x03FF)                 # move.w #1023,d0      → 1024 périodes de 32 o
w(0x223C); l(0x80000000)          # move.l #$80000000,d1
label('fill')
w(0x20C1)                         # move.l d1,(a0)+
for _ in range(7):
    w(0x4298)                     # clr.l (a0)+
w(0x51C8); fixups.append((len(code), 'fill')); code.extend(b'\x00\x00')   # dbra d0,fill

# ---- scroll fin (écrit UNE fois ; statique pour toutes les trames) ----
if mode == '8265':
    w(0x11FC, scroll, 0x8265)     # move.b #scroll,$8265.w  (avec prefetch)
elif mode == '8264':
    w(0x11FC, scroll, 0x8264)     # move.b #scroll,$8264.w  (sans prefetch)
elif mode.startswith('336') and mode != '336_split':
    assert scroll > 0, 'le latch 336 exige $FF8265 > 0'
    if mode == '336_word':
        w(0x31FC, scroll, 0x8264) # move.w #scroll,$8264.w (haut=0, bas>0)
    else:
        w(0x11FC, scroll, 0x8265)
    if mode == '336_late':
        for _ in range(12):
            w(0x4E71)            # 48 cycles : hors fenêtre Hatari de 40 cycles
    w(0x4238, 0x8264)             # clr.b $8264.w → latch +16 pixels
    if mode == '336_clear':
        w(0x4238, 0x8265)         # toute nouvelle écriture annule le latch

if mode == '336_split':
    w(0x4238, 0xFA07, 0x4238, 0xFA09)  # IRQ MFP coupées (IERA/B)
    w(0x43FA)                    # lea vbl(pc),a1
    fixups.append((len(code), 'vbl')); w(0)
    w(0x21C9, 0x0070)            # move.l a1,$70.w
    label('idle')
    w(0x4E72, 0x2300)            # STOP : ancrage VBL indépendant du boot
    bra_s('idle')
    label('vbl')
    w(0x4238, 0x8265)            # moitié haute normale
    label('wait_middle')
    w(0x0C38, 0x0040, 0x8207)    # cmpi.b #$40,$8207.w (adresse vidéo >=$024000)
    fixups.append((len(code), 'wait_middle')); w(0x6500)  # bcs.s
    w(0x11FC, scroll, 0x8265, 0x4238, 0x8264)
    label('wait_palette')
    w(0x0C38, 0x0060, 0x8207)    # première palette seulement après l'adresse $026000
    fixups.append((len(code), 'wait_palette')); w(0x6500)
    w(0x31FC, 0x0777, 0x8242)    # force un replay des lignes déjà committées
    w(0x4E73)                    # rte
else:
    label('halt')
    bra_s('halt')                # bra.s * (image statique)

# ---- fixups ----
for off, name in fixups:
    target = labels[name]
    op = code[off]
    if 0x60 <= op <= 0x6F:                          # bcc.s : disp 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127
        code[off + 1] = disp & 0xFF
    else:                                           # dbra : disp 16 bits
        disp = target - off
        struct.pack_into('>h', code, off, disp)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot + image 720K ----
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
assert wsum(boot) == 0x1234

img = bytearray(1440 * 512)
img[0:512] = boot
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; mode={mode} scroll={scroll} ; code={len(code)} o")
