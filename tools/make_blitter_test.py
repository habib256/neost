#!/usr/bin/env python3
# =============================================================================
#  make_blitter_test.py — Génère une disquette .ST bootable STE qui exerce le
#  BLITTER en mode NON-HOG pendant qu'un timer MFP tourne, et rend le résultat
#  des deux (données ET datation) dans une seule image comparable au pixel.
#
#  POURQUOI CET ÉTALON EXISTE (chantier A2). Mesuré le 2026-08-25 :
#  `NEOST_BLIT_TRACE=1` rend **0 blit** sur l'INTÉGRALITÉ du corpus pixel — il
#  est tout entier en `machine=st`, où le blitter n'existe pas. Le sous-système
#  le plus délicat de l'émulateur n'avait donc AUCUNE couverture automatisée :
#  les chantiers BL3 (cycles de stall facturés hors de l'horloge de
#  l'ordonnanceur) et BL4 (dispatch par accès) ont dû être prouvés à la main,
#  sur un `.stx` commercial NON REDISTRIBUABLE. Cet étalon referme le trou, et
#  il tourne sur ROM LIBRE (EmuTOS 256 Ko) — il survit donc au retrait des TOS
#  Atari propriétaires du dépôt (chantier A3).
#
#  CE QUE L'IMAGE CONTRAINT, ligne par ligne :
#
#   • LIGNES 0-99, 8 premiers pixels — LA DATATION (BL3/BL4). Chaque ligne porte
#     l'octet TADR (Timer A Data Register, $FFFFFA1F) RELU JUSTE APRÈS un blit
#     non-hog. Le timer tourne en mode délai avec prescaler /200 : un tic vaut
#     200 cycles MFP, soit environ 653 cycles CPU à 8 MHz (ratio 31333/9600).
#     La signature de BL3 était une lecture de TADR faussée par des cycles
#     blitter non facturés à l'ordonnanceur — 1088 cycles de dette, soit environ
#     1,7 tic. Ici, tout écart d'UN SEUL
#     TIC change l'octet, donc les 8 pixels, donc l'image.
#     ⚠ Choix délibéré : la quantification par le prescaler rend l'étalon
#     INSENSIBLE au jitter sous-tic (< 653 cycles CPU) et SENSIBLE à la classe de bug
#     visée. Un étalon qui rendrait le compteur de cycles brut serait rouge à la
#     moindre modification du cœur, donc inutilisable.
#
#   • LIGNES 120-127 — LES DONNÉES. La destination des blits : 8 lignes de 16
#     mots recopiées depuis un tampon source à motif croissant (HOP=2 source,
#     LOP=3 « D = S », endmasks $FFFF). Un blitter qui se trompe d'incrément, de
#     masque de bord ou d'ordre de plan salit ce bloc.
#
#  DEUX MODES (arg 2, ou déduit du nom de fichier) : `nonhog` (défaut) et `hog`.
#  Le mode HOG n'était exercé par AUCUN test NI AUCUN titre du dépôt — recensement
#  sur Lethal Xcess : 5764 blits, TOUS ctrl=$80. Il transfère un bloc sans rendre
#  le bus et mérite donc son propre étalon.
#  Depuis le 2026-10-19, PRE_START et le recouvrement CPU sont communs aux deux modes.
#
#  MODE NON-HOG : le bit HOG ($40) de $FF8A3C est LAISSÉ À ZÉRO, donc le
#  blitter rend le bus tous les 64 accès. Chaque blit fait 16×8 mots = 128
#  lectures + 128 écritures = 256 accès bus, soit ~4 TRANCHES par blit et ~400
#  tranches sur la boucle. C'est exactement le chemin `Blitter::onSlice`
#  (callback de l'échéance `Scheduler::BLITTER`) que BL3 et BL4 ont corrigé.
#  Le chemin HOG est exercé séparément par blitter_hog et les sondes temporelles.
#
#  Usage  : make_blitter_test.py <out.st>
#  Oracle : run_etalons.py --oracle --only blitter_timer
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

def dbra(reg, name):
    w(0x51C8 | reg)
    fixups.append((len(code), name))
    code.extend(b'\x00\x00')

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/blitter_test.st"
# Mode explicite (argv[2]) ou INFÉRÉ du nom de fichier — le runner d'étalons
# (run_etalons.py, disk_generate) ne passe que le chemin. Même convention que
# make_scroll_test.py.
if len(sys.argv) > 2:
    mode = sys.argv[2]
elif 'hog' in out and 'nonhog' not in out:
    mode = 'hog'
else:
    mode = 'nonhog'
assert mode in ('nonhog', 'hog'), mode
# $80 = BUSY seul → NON-HOG : le blitter rend le bus tous les 64 accès.
# $C0 = BUSY + HOG  → il GARDE le bus jusqu'à y_count = 0, en un seul bloc.
CTRL = 0xC0 if mode == 'hog' else 0x80

SCREEN  = 0x00020000       # base écran (posée par le programme, pas par le TOS)
SRCBUF  = 0x00030000       # tampon source des blits
ITERS   = 100              # nombre de blits (= nombre de lignes de TADR)
XCOUNT  = 16               # mots par ligne de blit
YCOUNT  = 8                # lignes par blit
DSTLINE = 120              # ligne écran où atterrissent les blits
DST     = SCREEN + DSTLINE * 160
BPL     = 160              # octets par ligne écran (basse résolution)

# bra.s code (saute le BPB)
bra_s('code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
w(0x46FC, 0x2700)                  # move.w #$2700,sr   → IRQ masquées (le timer TOURNE quand même)

# ---- vidéo : basse résolution, 50 Hz, base écran $020000 --------------------
w(0x4238, 0x8260)                  # clr.b $8260.w      → basse résolution
w(0x11FC, 0x0002, 0x820A)          # move.b #2,$820a.w  → 50 Hz
w(0x11FC, 0x0002, 0x8201)          # move.b #2,$8201.w  → base vidéo = $020000
w(0x4238, 0x8203)                  # clr.b $8203.w
w(0x4238, 0x820D)                  # clr.b $820d.w      → octet bas STE = 0
w(0x4238, 0x820F)                  # clr.b $820f.w      → line-offset STE = 0
w(0x4238, 0x8265)                  # clr.b $8265.w      → scroll fin STE = 0
w(0x4278, 0x8240)                  # clr.w $8240.w      → palette[0] = noir
w(0x31FC, 0x0777, 0x8242)          # move.w #$777,$8242.w → palette[1] = blanc
w(0x31FC, 0x0700, 0x8244)          # move.w #$700,$8244.w → palette[2] = rouge
w(0x31FC, 0x0070, 0x8246)          # move.w #$070,$8246.w → palette[3] = vert

# ---- écran mis à zéro (32000 o = 2000 × 16) --------------------------------
w(0x207C); l(SCREEN)               # movea.l #SCREEN,a0
w(0x303C, 1999)                    # move.w #1999,d0
label('cls')
for _ in range(4):
    w(0x4298)                      # clr.l (a0)+
dbra(0, 'cls')

# ---- tampon source : motif croissant (512 mots) ----------------------------
# Un blitter qui se trompe d'incrément ou de masque produit un bloc VISIBLEMENT
# différent : le motif n'a aucune symétrie qui pourrait masquer l'erreur.
w(0x207C); l(SRCBUF)               # movea.l #SRCBUF,a0
w(0x303C, 511)                     # move.w #511,d0
w(0x7200)                          # moveq #0,d1
label('fillsrc')
w(0x30C1)                          # move.w d1,(a0)+
# Pas ODD et non trivial ($3B27) plutôt qu'un +1 : un compteur simple ne remplit
# que les bits de POIDS FAIBLE, et le bloc destination n'allumait alors qu'une
# poignée de pixels — insuffisant pour qu'une erreur de plan, d'incrément ou de
# masque de bord se VOIE. Ce pas balaie les 16 bits et les 4 plans.
w(0x0641, 0x3B27)                  # addi.w #$3b27,d1
dbra(0, 'fillsrc')

# ---- Timer A du MFP en mode DÉLAI, prescaler /200 --------------------------
# IERA/IMRA bit 5 = Timer A : armés pour que le timer soit ORDONNANCÉ comme dans
# un programme réel (les IRQ restent masquées par SR=$2700, on ne veut que le
# COMPTEUR). TADR est écrit AVANT TACR : c'est l'écriture de TACR qui démarre le
# timer en chargeant TDR.
w(0x11FC, 0x0020, 0xFA07)          # move.b #$20,$fa07.w  → IERA, Timer A
w(0x11FC, 0x0020, 0xFA13)          # move.b #$20,$fa13.w  → IMRA, Timer A
w(0x11FC, 0x00FF, 0xFA1F)          # move.b #$ff,$fa1f.w  → TADR = 255
w(0x11FC, 0x0007, 0xFA19)          # move.b #7,$fa19.w    → TACR = 7 (prescaler /200)

# ---- blitter : réglages CONSTANTS (posés une seule fois) -------------------
w(0x31FC, 0x0002, 0x8A20)          # move.w #2,$8a20.w    → src X inc = 2 (contigu)
w(0x31FC, 0x0002, 0x8A22)          # move.w #2,$8a22.w    → src Y inc = 2 (contigu)
w(0x31FC, 0xFFFF, 0x8A28)          # move.w #$ffff,$8a28.w → endmask 1
w(0x31FC, 0xFFFF, 0x8A2A)          # move.w #$ffff,$8a2a.w → endmask 2
w(0x31FC, 0xFFFF, 0x8A2C)          # move.w #$ffff,$8a2c.w → endmask 3
w(0x31FC, 0x0002, 0x8A2E)          # move.w #2,$8a2e.w    → dst X inc = 2
# dst Y inc : de la DERNIÈRE colonne d'une ligne au 1er mot de la suivante.
w(0x31FC, BPL - (XCOUNT - 1) * 2, 0x8A30)   # move.w #130,$8a30.w
w(0x31FC, XCOUNT, 0x8A36)          # move.w #16,$8a36.w   → X count (mots/ligne)
w(0x11FC, 0x0002, 0x8A3A)          # move.b #2,$8a3a.w    → HOP = source seule
w(0x11FC, 0x0003, 0x8A3B)          # move.b #3,$8a3b.w    → LOP = 3 (D = S)
w(0x4238, 0x8A3D)                  # clr.b $8a3d.w        → skew = 0

# ---- boucle : blit non-hog, puis relève de TADR ---------------------------
w(0x227C); l(SCREEN)               # movea.l #SCREEN,a1   → pointeur de relève
w(0x3E3C, ITERS - 1)               # move.w #99,d7

label('loop')
# ⚠ Le blitter AUTO-INCRÉMENTE ses registres d'adresse pendant le transfert : à la
# fin d'un blit, $8A24 et $8A32 pointent APRÈS le bloc traité. Il faut donc les
# réarmer TOUS LES DEUX à chaque itération. Ne réarmer que la destination laissait
# la source dériver de 256 o par blit et lire de la RAM vierge dès le 5ᵉ tour —
# le bloc destination finissait uniformément à zéro, et l'étalon ne contraignait
# plus rien du chemin de DONNÉES.
w(0x21FC); l(SRCBUF); w(0x8A24)    # move.l #SRCBUF,$8a24.w → source réarmée
w(0x21FC); l(DST); w(0x8A32)       # move.l #DST,$8a32.w    → destination réarmée
w(0x31FC, YCOUNT, 0x8A38)          # move.w #8,$8a38.w    → Y count
# Bit 6 (HOG) : à ZÉRO en mode `nonhog` (le blitter rend le bus tous les 64 accès →
# Scheduler::BLITTER / Blitter::onSlice), à UN en mode `hog` (il garde le bus jusqu'au
# bout → Blitter::onSlice avec budget illimité). Les deux modes suivent PRE_START.
w(0x11FC, CTRL, 0x8A3C)            # move.b #CTRL,$8a3c.w

label('wait')
w(0x0838, 0x0007, 0x8A3C)          # btst #7,$8a3c.w      → encore occupé ?
fixups.append((len(code), 'wait')); code.extend(b'\x66\x00')   # bne.s wait

# TADR relu APRÈS le blit : c'est CETTE lecture que BL3 faussait.
w(0x12B8, 0xFA1F)                  # move.b $fa1f.w,(a1)
w(0x43E9, BPL)                     # lea 160(a1),a1       → ligne écran suivante
dbra(7, 'loop')

label('halt')
bra_s('halt')                      # bra.s * → image statique

# ---- fixups ----------------------------------------------------------------
for off, name in fixups:
    target = labels[name]
    op = code[off]
    if op in (0x60, 0x66):                          # bra.s / bne.s : disp 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127, f"branche hors portée vers {name} ({disp})"
        code[off + 1] = disp & 0xFF
    else:                                           # dbra : disp 16 bits
        disp = target - off
        struct.pack_into('>h', code, off, disp)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot + image 720K -----------------------------------------
boot = bytearray(512)
boot[0:len(code)] = code
struct.pack_into('<H', boot, 0x0B, 512)      # octets/secteur
boot[0x0D] = 2                               # secteurs/cluster
struct.pack_into('<H', boot, 0x0E, 1)        # secteurs réservés
boot[0x10] = 2                               # nb de FAT
struct.pack_into('<H', boot, 0x11, 112)      # entrées racine
struct.pack_into('<H', boot, 0x13, 1440)     # secteurs totaux
boot[0x15] = 0xF9                            # descripteur de média
struct.pack_into('<H', boot, 0x16, 5)        # secteurs/FAT
struct.pack_into('<H', boot, 0x18, 9)        # secteurs/piste
struct.pack_into('<H', boot, 0x1A, 2)        # faces

def wsum(b):
    return sum(struct.unpack('>256H', bytes(b))) & 0xFFFF

struct.pack_into('>H', boot, 0x1FE, 0)
struct.pack_into('>H', boot, 0x1FE, (0x1234 - wsum(boot)) & 0xFFFF)
assert wsum(boot) == 0x1234, "secteur de boot non exécutable (checksum != $1234)"

img = bytearray(1440 * 512)
img[0:512] = boot
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; mode={mode} ; {ITERS} blits de {XCOUNT}x{YCOUNT} mots "
      f"(ctrl=${CTRL:02X}) ; code={len(code)} o")
