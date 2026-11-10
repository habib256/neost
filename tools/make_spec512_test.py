#!/usr/bin/env python3
# =============================================================================
#  make_spec512_test.py — disquette .ST bootable qui change la PALETTE PLUSIEURS
#  FOIS PAR LIGNE (principe du Spectrum 512), pour valider le re-rendu
#  intra-ligne de NeoST contre l'oracle Hatari. Chantier A10.
#
#  POURQUOI CE GÉNÉRATEUR EXISTE. Les trois étalons Spectrum 512 du projet
#  s'appuient sur `disks/etalons/spectrum_512_auto_diapo.st`, qui exige un TOS
#  Atari : ce disque n'a PAS de secteur de boot exécutable, sa diapo est lancée
#  par un dossier AUTO qu'EmuTOS n'exécute pas jusqu'au bout (réfuté à l'oracle
#  le 2026-08-28 — Hatari + EmuTOS retombe sur le bureau, exactement comme NeoST).
#  Le jour de la purge, ces trois étalons deviennent des SKIP recensés et TOUTE la
#  couverture « palette changée en cours de ligne » disparaît. Celui-ci la rend,
#  sans une ligne de code propriétaire.
#
#  CE QU'IL EXERCE, et que rien d'autre n'exerce sans ROM Atari :
#    · la capture des écritures palette DATÉES au cycle (recordColorWrite) ;
#    · la bascule en re-rendu par ligne (spec512Active_) ;
#    · la position HORIZONTALE exacte à laquelle une écriture palette prend effet
#      — c'est le seul endroit du rendu où un cycle de CPU se voit à l'œil.
#
#  COMMENT. Écran basse résolution 50 Hz, RAM écran remplie de l'index 1 partout
#  (plan 0 à 1, plans 1-3 à 0). La boucle principale se resynchronise en haut de
#  trame sur le compteur vidéo ($FF8207), puis martèle palette[1] ($FF8242) avec
#  trois couleurs séparées par un délai FIXE. Le motif est donc identique à chaque
#  trame → image STATIQUE, et la position des bandes dépend du modèle de cycle :
#  c'est précisément la mesure qu'on veut.
#
#  Le 68000 est BIG-ENDIAN : tous les mots sont assemblés octet par octet.
#  Secteur de boot exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  Usage : make_spec512_test.py [sortie.st] [cycles_entre_ecritures]
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


def bra_s(name):
    fixups.append((len(code), name))
    code.extend(b'\x60\x00')


def bcc_s(op, name):
    fixups.append((len(code), name))
    code.extend(bytes([op, 0x00]))


def dbra(reg, name):          # dbra Dn,label — déplacement 16 bits
    w(0x51C8 | (reg & 7))
    fixups.append((len(code), name))
    code.extend(b'\x00\x00')


# Délai entre deux écritures palette, en itérations de dbra (≈10 cycles chacune).
# 5 → ~64 cycles, soit ~8 changements de couleur par ligne de 512 cycles. Réglable
# en 2ᵉ argument : c'est le bouton qui rend le motif plus ou moins fin.
DELAY = int(sys.argv[2]) if len(sys.argv) > 2 else 5

# ---- secteur de boot : offset 0 = bra.s vers le code (saute le BPB) ----------
bra_s('code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr   (superviseur, IPL7 : pas d'IRQ)
w(0x4238, 0x8260)                 # clr.b $8260.w      → basse résolution
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w  → 50 Hz
w(0x11FC, 0x0002, 0x8201)         # move.b #2,$8201.w  → base vidéo $02xxxx
w(0x4238, 0x8203)                 # clr.b $8203.w      → base vidéo = $020000
w(0x4278, 0x8240)                 # clr.w $8240.w      → palette[0] = noir (bordure)

# ---- RAM écran : index 1 PARTOUT ------------------------------------------
# Basse rés : 4 mots entrelacés pour 16 pixels (plan 0, 1, 2, 3). Plan 0 à $FFFF
# et les trois autres à 0 → tous les pixels portent l'index 1, donc TOUS suivent
# palette[1]. Un seul registre de palette à marteler, et l'écran entier réagit.
w(0x207C); l(0x00020000)          # movea.l #$00020000,a0
w(0x303C, 32000 // 8 - 1)         # move.w #3999,d0    (4 000 groupes de 8 octets)
w(0x223C); l(0xFFFF0000)          # move.l #$FFFF0000,d1
label('fill')
w(0x20C1)                         # move.l d1,(a0)+    → plans 0 et 1
w(0x4298)                         # clr.l (a0)+        → plans 2 et 3
dbra(0, 'fill')

# ---- ancrage : VBL + STOP (2026-09-01) ------------------------------------
# La première version se resynchronisait en haut de trame en scrutant le compteur
# vidéo ($FF8207). L'image était stable d'une trame à l'autre, mais PAS d'un run
# à l'autre chez Hatari : son RNG de boot (position angulaire de la disquette)
# décale le démarrage du programme de quelques cycles, et une boucle de scrutation
# ne se recale qu'à ~20 cycles près. Mesuré le 2026-09-01 : deux runs de la même
# ligne de commande → deux jeux de phases entièrement disjoints, la moins pire à
# 2 460 px de la référence, couleurs permutées sur 4 px au bord de chaque bande.
# L'oracle de cet étalon n'était donc pas re-dérivable.
#
# Remède : la séquence vit dans le HANDLER DE VBL et le programme attend en
# `stop #$2300`. Une interruption prise depuis STOP a une latence FIXE — là où une
# boucle `bra.s` la prend à une frontière d'instruction, avec jusqu'à 10 cycles de
# jitter. Le handler démarre donc toujours au même cycle de la trame, quel que
# soit le boot. Les IRQ MFP sont coupées à la source (IERA/IERB), IPL 3 ne laisse
# passer que le niveau 4 (VBL) ; l'HBL (niveau 2) reste masquée.
#
# Mesuré après ce changement (2026-09-01) : Hatari rend les MÊMES images d'un run à
# l'autre (2 runs, 3 phases identiques), et NeoST rend les mêmes trois images
# qu'Hatari (matrice 3×3 : zéro sur la diagonale). L'image n'est pas figée d'une
# trame à l'autre — le handler démarre 4 cycles plus tard à chaque trame (première
# écriture à cyc 138, 142, 146…), période 5 trames, IDENTIQUE chez Hatari : c'est
# le programme, pas un émulateur. oracle_scan retient la trame identique à la
# capture NeoST ; elle existe dans toute fenêtre d'au moins 5 trames.
w(0x4238, 0xFA07)                 # clr.b $fffa07.w   (IERA)
w(0x4238, 0xFA09)                 # clr.b $fffa09.w   (IERB)
w(0x43FA)                         # lea vbl(pc),a1    (adresse de boot inconnue à l'assemblage)
fixups.append((len(code), 'vbl')); code.extend(b'\x00\x00')
w(0x21C9, 0x0070)                 # move.l a1,$70.w   (autovecteur niveau 4)
w(0x46FC, 0x2300)                 # move.w #$2300,sr
label('idle')
w(0x4E72, 0x2300)                 # stop #$2300       → réveil à la VBL, latence fixe
bra_s('idle')

label('vbl')
# ---- verrou sur le compteur vidéo (2026-11-10) -----------------------------
# La latence d'une VBL prise depuis STOP n'est pas fixe : l'autovecteur attend l'E-clock,
# dont la phase tourne de 4 cycles par trame (cycle de 5 trames). La capture dépendait
# donc de la phase du boot, et un cœur qui la déplace aurait fait rougir l'étalon sans
# défaut. Verrou des démos : scruter l'octet bas du compteur ($FF8209) jusqu'au début de
# DE de la première ligne affichée, puis LSL.W de durée 6+2n avec n = 32 − valeur lue.
# Un cran du compteur (2 octets) vaut 4 cycles, deux crans de n aussi : la gigue de la
# scrutation (une boucle de 20 cycles) s'annule, et chaque trame part au même cycle.
w(0x41F8, 0x8209)                 # lea $ffff8209.w,a0
w(0x7000)                         # moveq #0,d0
label('sync')
w(0x1010)                         # move.b (a0),d0
bcc_s(0x67, 'sync')               # beq.s sync         → DE commencé
w(0x7220)                         # moveq #32,d1
w(0x9240)                         # sub.w d0,d1
w(0xE368)                         # lsl.w d1,d0        → 6+2n cycles, n = 32 − valeur lue
# Nombre d'itérations : chacune coûte ~300 cycles (3 écritures + 3 délais), mesuré
# à la trace NEOST_PAL_TRACE. Une trame PAL fait 313×512 = 160 256 cycles, donc ~500
# itérations la couvrent en laissant de quoi se resynchroniser. Trop peu → le bas de
# l'écran garde la dernière couleur ; trop → la boucle déborde sur la trame suivante
# et le resync ne voit qu'une trame sur deux (constaté avec 2 600).
w(0x363C, 400)                    # move.w #400,d3   (le verrou part de la 1re ligne affichée)
label('band')
w(0x31FC, 0x0700, 0x8242)         # move.w #$0700,$8242.w   → palette[1] = ROUGE
w(0x383C, DELAY)                  # move.w #DELAY,d4
label('w1')
dbra(4, 'w1')
w(0x31FC, 0x0070, 0x8242)         # palette[1] = VERT
w(0x383C, DELAY)
label('w2')
dbra(4, 'w2')
w(0x31FC, 0x0007, 0x8242)         # palette[1] = BLEU
w(0x383C, DELAY)
label('w3')
dbra(4, 'w3')
dbra(3, 'band')
w(0x4E73)                         # rte → retour au stop

# ---- résolution des déplacements -------------------------------------------
for off, name in fixups:
    target = labels[name]
    op = code[off]
    if op in (0x60, 0x65, 0x64, 0x67):            # bra.s / bcs.s / bcc.s / beq.s : 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127, f"branche courte hors portée {name} : {disp}"
        code[off + 1] = disp & 0xFF
    else:                                          # dbra / lea(pc) : 16 bits, relatif au mot
        disp = target - off
        assert -32768 <= disp <= 32767, f"déplacement hors portée {name} : {disp}"
        struct.pack_into('>h', code, off, disp)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot 512 o --------------------------------------------------
boot = bytearray(512)
boot[0:len(code)] = code
# BPB minimal (720 Ko, 9 secteurs/piste, 2 faces, 80 pistes), champs little-endian :
# le TOS doit pouvoir lire la disquette sans erreur avant d'exécuter le secteur.
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
out = sys.argv[1] if len(sys.argv) > 1 else "disks/etalons/spec512_bands.st"
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; code={len(code)} o ; délai={DELAY} ; checksum OK (0x1234)")
