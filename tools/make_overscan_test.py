#!/usr/bin/env python3
# =============================================================================
#  make_overscan_test.py — Génère une disquette .ST bootable qui RETIRE la
#  bordure HAUTE (overscan top), pour valider la machine Glue de NeoST contre
#  l'oracle Hatari (trace video_border_v → "detect remove top").
#
#  Pourquoi le retrait HAUT : c'est le trick le plus tolérant au timing — la
#  fenêtre du switch 60 Hz est large (lignes ~1..32). Technique (port du modèle
#  Hatari Video_Update_Glue_State, section top border) :
#    • écran 50 Hz basse rés, base vidéo $020000, RAM écran remplie $FF (index 15
#      partout → blanc) ; palette[0]=noir (bordure), palette[15]=blanc.
#    • chaque trame : se resync sur le HAUT de trame en pollant le compteur vidéo
#      $FF8207 (octet médian : grand pendant l'affichage, ~0 juste après le VBL),
#      puis écrit 60 Hz ($FF820A=0) tôt (ligne ~5, < 33) → la GLUE place nStartHBL
#      à 34 (position 60 Hz) ; comme l'écran est 50 Hz, nStartHBL 34 < 63 →
#      V_OVERSCAN_NO_TOP (bordure haute retirée, +29 lignes). Puis 50 Hz de retour
#      (ligne ~45) pour ne pas perturber le bas.
#  Résultat attendu : les 29 lignes de bordure haute affichent du blanc.
#
#  Le 68000 est BIG-ENDIAN : tous les mots sont assemblés octet par octet.
#  Secteur de boot exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import struct, sys

# ---- petit assembleur : on encode chaque instruction à la main (opcodes 68000)
code = bytearray()
labels = {}
fixups = []   # (offset_dans_code, label, taille_bra)

def w(*words):           # ajoute des mots 16 bits (big-endian)
    for x in words:
        code.extend(struct.pack('>H', x & 0xFFFF))

def l(x):                # ajoute un long 32 bits
    code.extend(struct.pack('>I', x & 0xFFFFFFFF))

def label(name):
    labels[name] = len(code)

def bra_s(name):         # bra.s label (déplacement 8 bits depuis PC+2)
    fixups.append((len(code), name))
    code.extend(b'\x60\x00')   # patché ensuite

def bcc_s(op, name):     # branche conditionnelle courte : op = octet d'opcode haut
    fixups.append((len(code), name))
    code.extend(bytes([op, 0x00]))

# Le secteur de boot : offset 0 = bra.s vers le code (saute le BPB).
# bra.s code  → code à 0x1E
bra_s('code')                       # 60 xx  (à 0x00)
# remplissage jusqu'à 0x1E (zone BPB, non exécutée)
while len(code) < 0x1E:
    code.append(0x00)

label('code')
# move.w #$2700,sr   (superviseur, IPL7 : pas d'IRQ)
w(0x46FC, 0x2700)
# clr.b $8260.w          → $FFFF8260 = 0 (basse rés)
w(0x4238, 0x8260)
# move.b #2,$820a.w      → 50 Hz
w(0x11FC, 0x0002, 0x820A)
# move.b #2,$8201.w      → base vidéo octet haut = $02
w(0x11FC, 0x0002, 0x8201)
# clr.b $8203.w          → base vidéo octet médian = $00  (base = $020000)
w(0x4238, 0x8203)
# clr.w $8240.w          → palette[0] = $000 (noir, bordure)
w(0x4278, 0x8240)
# move.w #$0777,$825e.w  → palette[15] = $777 (blanc)
w(0x31FC, 0x0777, 0x825E)
# movea.l #$00020000,a0
w(0x207C); l(0x00020000)
# move.w #$3FFF,d0       → 16384 longs = 65536 octets
w(0x303C, 0x3FFF)
# moveq #-1,d1
w(0x72FF)
label('fill')
# move.l d1,(a0)+
w(0x20C1)
# dbra d0,fill
w(0x51C8); fixups.append((len(code), 'fill')); code.extend(b'\x00\x00')  # dbra disp16

# ---- boucle principale : resync haut de trame, switch 60/50 Hz ----
label('sync')
# --- attendre d'être bien dans l'affichage (octet médian compteur grand) ---
label('s1')
# move.b $8207.w,d2
w(0x1438, 0x8207)
# cmp.b #$40,d2     (cmpi.b #$40,d2)
w(0x0C02, 0x0040)
# bcs.s s1   (médian < $40 → encore trop tôt, reboucle)  bcs=0x65
bcc_s(0x65, 's1')
# --- attendre le reset du compteur (haut de trame) ---
label('s2')
# move.b $8207.w,d2
w(0x1438, 0x8207)
# cmp.b #$04,d2
w(0x0C02, 0x0004)
# bcc.s s2   (médian >= $04 → pas encore resync)  bcc=0x64
bcc_s(0x64, 's2')
# mode : 'top' (défaut) retire la bordure HAUTE (switch 60Hz ligne ~5, 50Hz ligne ~37) ;
#        'bottom' retire la bordure BASSE (switch 60Hz ligne ~259, 50Hz ligne ~265).
mode = sys.argv[2] if len(sys.argv) > 2 else 'top'
d1cnt, d2cnt = (150, 1600) if mode == 'top' else (13000, 600)
# --- haut de trame : petit (top) ou grand (bottom) délai → ligne cible ---
w(0x363C, d1cnt & 0xFFFF)        # move.w #d1cnt,d3
label('d1')
w(0x51CB); fixups.append((len(code), 'd1')); code.extend(b'\x00\x00')   # dbra d3,d1
# move.b #0,$820a.w   → 60 Hz (retire la bordure : haute si tôt, basse si ligne ~259)
w(0x11FC, 0x0000, 0x820A)
# --- second délai → ligne de rétablissement ---
w(0x363C, d2cnt & 0xFFFF)        # move.w #d2cnt,d3
label('d2')
w(0x51CB); fixups.append((len(code), 'd2')); code.extend(b'\x00\x00')   # dbra d3,d2
# move.b #2,$820a.w   → 50 Hz (rétabli)
w(0x11FC, 0x0002, 0x820A)
# bra.s sync
bra_s('sync')

# ---- résolution des fixups (déplacements) ----
for off, name in fixups:
    target = labels[name]
    op = code[off]
    if op in (0x60, 0x65, 0x64):              # bra.s / bcs.s / bcc.s : disp 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127, f"bra.s hors portée {name}:{disp}"
        code[off+1] = disp & 0xFF
    else:                                       # dbra : disp 16 bits, RELATIF à l'adresse
        disp = target - off                     #   du mot de déplacement (off = ce mot)
        assert -32768 <= disp <= 32767, f"dbra hors portée {name}:{disp}"
        struct.pack_into('>h', code, off, disp)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot 512 o ----
boot = bytearray(512)
boot[0:len(code)] = code
# BPB minimal (disquette 720K, 9 secteurs/piste, 2 faces, 80 pistes) — pour que
# TOS lise la disquette sans erreur. Champs little-endian (offsets DOS/Atari).
struct.pack_into('<H', boot, 0x0B, 512)     # octets/secteur
boot[0x0D] = 2                               # secteurs/cluster
struct.pack_into('<H', boot, 0x0E, 1)       # secteurs réservés (boot)
boot[0x10] = 2                              # nb FAT
struct.pack_into('<H', boot, 0x11, 112)     # entrées racine
struct.pack_into('<H', boot, 0x13, 1440)    # total secteurs (720K)
boot[0x15] = 0xF9                           # media descriptor
struct.pack_into('<H', boot, 0x16, 5)       # secteurs/FAT
struct.pack_into('<H', boot, 0x18, 9)       # secteurs/piste
struct.pack_into('<H', boot, 0x1A, 2)       # têtes

# checksum exécutable : somme des 256 mots == 0x1234 (on ajuste le dernier mot)
def wsum(b):
    return sum(struct.unpack('>256H', bytes(b))) & 0xFFFF
struct.pack_into('>H', boot, 0x1FE, 0)
need = (0x1234 - wsum(boot)) & 0xFFFF
struct.pack_into('>H', boot, 0x1FE, need)
assert wsum(boot) == 0x1234

# ---- image 720K : boot + reste à 0 (FAT vide suffit, on ne lit pas de fichier) ----
img = bytearray(1440 * 512)
img[0:512] = boot
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/s512/overscan_top.st"
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; code={len(code)} o ; checksum OK (0x1234)")
