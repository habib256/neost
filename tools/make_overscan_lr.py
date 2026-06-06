#!/usr/bin/env python3
# =============================================================================
#  make_overscan_lr.py — Disquette .ST bootable qui retire les bordures
#  GAUCHE + DROITE (overscan horizontal), pour valider la machine Glue STF de
#  NeoST (LEFT_OFF / RIGHT_OFF_FULL) contre l'oracle Hatari (video_border_h).
#
#  Technique (port du modèle Hatari Video_Update_Glue_State, phase 2) : sur
#  chaque ligne affichée, une IMPULSION hi-res en milieu de ligne (cycle ~250,
#  fenêtre large 161-376) déclenche RIGHT_OFF_FULL → DE_end=512 (retrait droite
#  de CETTE ligne) ET LEFT_OFF sur la ligne SUIVANTE (DE_start=4). Le retour
#  lo-res se fait > cycle 462 pour ne PAS annuler le retrait. Résultat sur tout
#  l'écran : bordures gauche ET droite ouvertes (contenu blanc dans les côtés).
#
#  Synchro = gestionnaire d'interruption HBL (latence déterministe ligne à
#  ligne), MFP masqué, VBL → rte. Le 68000 est BIG-ENDIAN.
#  Secteur de boot exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  Args: [out.st] [pad1] [pad2]  (pad1/pad2 = nb d'itérations dbra, calibrables).
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import struct, sys

code = bytearray()
labels = {}
fixups = []   # (offset, label, kind)  kind: 'bra8','dbra','pcrel'

def w(*words):
    for x in words: code.extend(struct.pack('>H', x & 0xFFFF))
def l(x):
    code.extend(struct.pack('>I', x & 0xFFFFFFFF))
def label(n): labels[n] = len(code)
def bra_s(n):
    fixups.append((len(code), n, 'bra8')); code.extend(b'\x60\x00')
def dbra(reg, n):                          # dbra Dreg,label  (0x51C8|reg + disp16)
    w(0x51C8 | (reg & 7)); fixups.append((len(code), n, 'dbra')); code.extend(b'\x00\x00')
def lea_pc(n, areg):                        # lea (d16,pc),Areg
    w(0x41FA | ((areg & 7) << 9)); fixups.append((len(code), n, 'pcrel')); code.extend(b'\x00\x00')

# PAD1/PAD2 calibrent la position de l'impulsion hi-res DANS la ligne. Re-calibrés
# (20→12) après l'ajout des wait states périphériques PSG/MFP/ACIA : ceux-ci décalent
# le timing absolu CPU↔vidéo (le boot+setup paient désormais le coût réel des accès), ce
# qui déplace la phase d'entrée du HBL. PAD1=12 redonne un retrait L+D PLEIN et propre
# (white bord-à-bord, bordures haut/bas noires) ; cf. CHANGELOG §Wait states.
PAD1 = int(sys.argv[2]) if len(sys.argv) > 2 else 12   # entrée→hi-res  (~cycle 250)
PAD2 = int(sys.argv[3]) if len(sys.argv) > 3 else 23   # hi-res→lo-res  (~cycle 490)

# offset 0 : bra.s code (saute le BPB)
bra_s('code')
while len(code) < 0x1E: code.append(0)

label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr  (IPL7 pendant le setup)
w(0x4238, 0x8260)                 # clr.b $8260.w     (basse rés)
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w (50 Hz)
w(0x11FC, 0x0002, 0x8201)         # move.b #2,$8201.w (base vidéo $02xxxx)
w(0x4238, 0x8203)                 # clr.b $8203.w     (base = $020000)
w(0x4278, 0x8240)                 # clr.w $8240.w     (palette[0]=noir)
w(0x31FC, 0x0777, 0x825E)         # move.w #$0777,$825e.w (palette[15]=blanc)
w(0x207C); l(0x00020000)          # movea.l #$00020000,a0
w(0x303C, 0x3FFF)                 # move.w #$3FFF,d0
w(0x72FF)                         # moveq #-1,d1
label('fill')
w(0x20C1)                         # move.l d1,(a0)+
dbra(0, 'fill')                   # dbra d0,fill
# masque les interruptions MFP (IERA/IERB)
w(0x4238, 0xFA07)                 # clr.b $fa07.w  (IERA)
w(0x4238, 0xFA09)                 # clr.b $fa09.w  (IERB)
# vecteurs : VBL (autovec niv4, $70) → rte ; HBL (autovec niv2, $68) → handler
lea_pc('rtestub', 0)             # lea rtestub(pc),a0
w(0x21C8, 0x0070)                 # move.l a0,$0070.w
lea_pc('hbl', 0)                 # lea hbl(pc),a0
w(0x21C8, 0x0068)                 # move.l a0,$0068.w
w(0x46FC, 0x2100)                 # move.w #$2100,sr  (IPL1 → niveaux 2+ pris)
label('main')
bra_s('main')                    # main: bra.s main

label('rtestub')
w(0x4E73)                         # rte

label('hbl')
w(0x303C, PAD1 & 0xFFFF)          # move.w #PAD1,d0
label('p1'); dbra(0, 'p1')        # p1: dbra d0,p1   (entrée → ~cycle 250)
w(0x11FC, 0x0002, 0x8260)         # move.b #2,$8260.w (hi rés → RIGHT_OFF_FULL + next LEFT_OFF)
w(0x303C, PAD2 & 0xFFFF)          # move.w #PAD2,d0
label('p2'); dbra(0, 'p2')        # p2: dbra d0,p2   (→ ~cycle 490, > 462)
w(0x4238, 0x8260)                 # clr.b $8260.w     (basse rés, n'annule pas)
w(0x4E73)                         # rte

# ---- fixups ----
for off, name, kind in fixups:
    target = labels[name]
    if kind == 'bra8':
        disp = target - (off + 2)
        assert -128 <= disp <= 127, f"bra.s {name}:{disp}"
        code[off+1] = disp & 0xFF
    elif kind == 'dbra':
        disp = target - off
        assert -32768 <= disp <= 32767, f"dbra {name}:{disp}"
        struct.pack_into('>h', code, off, disp)
    elif kind == 'pcrel':
        disp = target - off
        assert -32768 <= disp <= 32767, f"pcrel {name}:{disp}"
        struct.pack_into('>h', code, off, disp)

assert len(code) <= 510, f"code trop gros : {len(code)}"

boot = bytearray(512)
boot[0:len(code)] = code
struct.pack_into('<H', boot, 0x0B, 512); boot[0x0D] = 2
struct.pack_into('<H', boot, 0x0E, 1);  boot[0x10] = 2
struct.pack_into('<H', boot, 0x11, 112); struct.pack_into('<H', boot, 0x13, 1440)
boot[0x15] = 0xF9; struct.pack_into('<H', boot, 0x16, 5)
struct.pack_into('<H', boot, 0x18, 9);  struct.pack_into('<H', boot, 0x1A, 2)
def wsum(b): return sum(struct.unpack('>256H', bytes(b))) & 0xFFFF
struct.pack_into('>H', boot, 0x1FE, 0)
struct.pack_into('>H', boot, 0x1FE, (0x1234 - wsum(boot)) & 0xFFFF)
assert wsum(boot) == 0x1234

img = bytearray(1440 * 512); img[0:512] = boot
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/s512/overscan_lr.st"
open(out, 'wb').write(img)
print(f"écrit {out} ; code={len(code)} o ; PAD1={PAD1} PAD2={PAD2} ; checksum OK")
