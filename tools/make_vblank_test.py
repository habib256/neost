#!/usr/bin/env python3
"""Disquette libre : retrait bas PAL et VBlank, ancrés par VBL/STOP.

on : impulsion60Hz en fin de ligne262, retour50Hz avant DE ligne263.
     La bordure basse est ouverte mais VBlank masque308/309 (deux dernières
     lignes du buffer416×276), à l'indexpalette0 VERT et non au noir physique.
off : seconde impulsion307/308 qui fait manquer le check50Hz au cycle502 ;
      les deux lignes restent alors blanches, à géométrie identique.

Usage : make_vblank_test.py sortie.st [on|off]
Le mode est inféré du nom (...vblank_off...) pour le runner d'étalons.
Les délais sont larges devant les comparateurs : les trois phases4cycles de
la VBL donnent le même résultat. Aucun ajustement de l'émulateur n'est requis.
Outil de test du projet NeoST, domaine public.
"""
import struct
import sys

out = sys.argv[1] if len(sys.argv) > 1 else '/tmp/vblank_on.st'
mode = sys.argv[2] if len(sys.argv) > 2 else ('off' if 'vblank_off' in out else 'on')
assert mode in ('on', 'off')
code = bytearray()
labels, fixups = {}, []


def w(*words):
    for word in words:
        code.extend(struct.pack('>H', word & 0xFFFF))


def label(name):
    labels[name] = len(code)


def bra(name):
    fixups.append((len(code), name, True))
    w(0x6000)


def relative(name):
    fixups.append((len(code), name, False))
    w(0)


def delay(name, count):
    w(0x363C, count)                 # move.w #count,d3
    label(name)
    w(0x51CB); relative(name)        # dbra d3,name


bra('code')
code.extend(bytes(0x1E - len(code)))
label('code')
w(0x46FC, 0x2700, 0x4238, 0x8260)  # superviseur IPL7 ; basse résolution
w(0x11FC, 2, 0x820A)               # PAL50Hz
w(0x11FC, 2, 0x8201, 0x4238, 0x8203)
w(0x31FC, 0x0070, 0x8240)          # index0 vert, distingue VBlank du noir
for reg in range(1, 16):
    w(0x31FC, 0x0777, 0x8240 + reg * 2)
# 64Ko blancs : le contenu ne dépend pas du pointeur lors de la transition.
w(0x207C, 0x0002, 0x0000, 0x303C, 0x3FFF, 0x72FF)
label('fill')
w(0x20C1, 0x51C8); relative('fill')
w(0x4238, 0xFA07, 0x4238, 0xFA09)  # couper les IRQ MFP
w(0x43FA); relative('vbl')         # lea vbl(pc),a1
w(0x21C9, 0x0070)                 # autovecteur VBL
label('idle')
w(0x4E72, 0x2300); bra('idle')     # interruption depuis STOP : latence fixe
label('vbl')
delay('bottom', 11200)             # frq60 : ligne262, cycle412..420
w(0x11FC, 0, 0x820A)
delay('restore', 8)                # frq50 : ligne263, cycle36..44 (<DE)
w(0x11FC, 2, 0x820A)
if mode == 'off':
    delay('avoid_vblank', 1906)     # frq60 : ligne307, avant502
    w(0x11FC, 0, 0x820A)
    delay('restore_vblank', 8)      # frq50 : ligne308, après le check
    w(0x11FC, 2, 0x820A)
w(0x4E73)

for offset, target, short in fixups:
    displacement = labels[target] - offset - (2 if short else 0)
    if short:
        assert -128 <= displacement <= 127 and displacement
        code[offset + 1] = displacement & 0xFF
    else:
        struct.pack_into('>h', code, offset, displacement)
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
for offset, value in ((0x16, 5), (0x18, 9), (0x1A, 2)):
    struct.pack_into('<H', boot, offset, value)
checksum = sum(struct.unpack('>256H', boot)) & 0xFFFF
struct.pack_into('>H', boot, 0x1FE, (0x1234 - checksum) & 0xFFFF)
image = bytearray(1440 * 512)
image[:512] = boot
with open(out, 'wb') as handle:
    handle.write(image)
print(f'{out}: VBlank {mode}, {len(code)} octets de code, {len(image)} octets')
