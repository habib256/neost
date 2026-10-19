#!/usr/bin/env python3
"""Disquette libre : lignes masquées, avec motif discriminant pour le compteur.

blank : impulsion 60 Hz à la sortie du blanking horizontal, puis retour PAL.
        Une phase VBL masque la ligne ; la RAM vidéo continue d'être consommée.
node : impulsion haute résolution avant la sortie du blanking, retour après.
       La ligne est masquée et le compteur ne doit pas avancer.
node_late : impulsion plus tardive, dans la fenêtre NO_DE du STF mais après
            le début de préchargement du STE ; contre-épreuve entre machines.
stop : impulsion haute résolution pendant DE, retour après sa fin raccourcie.
       Seul le début de la ligne est affiché ; 54 octets sont consommés.
nosync / sync_high : impulsion autour de HSYNC, ligne suivante sans DE et
                     fin d'affichage retardée d'une ligne.
nosync_cancel / sync_high_cancel : retour assez tôt pour annuler cet effet.

La VBL depuis STOP conserve plusieurs phases de quatre cycles. Le runner
compare une phase précise et cherche son équivalent dans une fenêtre Hatari.
Le fond vert distingue l'index 0 du noir ; les longs successifs de la RAM
rendent visible une avance erronée sur les lignes suivantes. La couleur
physique du blanking reste à mesurer : cet étalon compare les émulateurs.

Usage : make_blankline_test.py sortie.st [mode]
Sans mode explicite, le nom doit contenir blankline_<mode>.
Outil de test NeoST, domaine public.
"""
from pathlib import Path
import struct
import sys


def make_disk(out: Path, mode: str) -> None:
    nops_before = {'blank': 4, 'node': 0, 'node_late': 8, 'stop': 22,
                   'nosync': 102, 'nosync_cancel': 102,
                   'sync_high': 116, 'sync_high_cancel': 116}
    if mode not in nops_before:
        raise ValueError(f'mode inconnu : {mode}')
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

    def delay(name, count):
        w(0x363C, count)             # move.w #count,d3
        label(name)
        w(0x51CB); relative(name)    # dbra d3,name

    relative('code', short=True)
    code.extend(bytes(0x1E - len(code)))
    label('code')
    w(0x46FC, 0x2700, 0x4238, 0x8260)  # superviseur IPL7, basse résolution
    w(0x11FC, 2, 0x820A)               # PAL
    w(0x11FC, 2, 0x8201, 0x4238, 0x8203)  # écran $020000
    w(0x31FC, 0x0070, 0x8240)          # index 0 vert
    for reg in range(1, 16):
        w(0x31FC, 0x0777, 0x8240 + reg * 2)
    # 64 Ko : longs $FFFFFFFF, $00000000, $00000001... Le décalage
    # d'une ligne se voit dans le motif, contrairement à un aplat uniforme.
    w(0x207C, 0x0002, 0x0000, 0x303C, 0x3FFF, 0x72FF)
    label('fill')
    w(0x20C1, 0x5281, 0x51C8); relative('fill')  # move.l/addq.l/dbra
    w(0x4238, 0xFA07, 0x4238, 0xFA09)  # couper les IRQ MFP
    w(0x43FA); relative('vbl')          # lea vbl(pc),a1
    w(0x21C9, 0x0070)
    label('idle')
    w(0x4E72, 0x2300); relative('idle', short=True)
    label('vbl')
    delay('target', 4212)
    for _ in range(nops_before[mode]):
        w(0x4E71)
    register = 0x820A if mode == 'blank' else 0x8260
    w(0x11FC, 0 if mode == 'blank' else 2, register)
    # Retour avant DE pour node : conserve une ligne PAL de 512 cycles,
    # sans annuler NO_DE|BLANK. node_late conserve son raccourcissement.
    for _ in range({'node': 4, 'stop': 20, 'nosync': 11,
                    'nosync_cancel': 3, 'sync_high_cancel': 0}.get(mode, 8)):
        w(0x4E71)
    w(0x11FC, 2 if mode == 'blank' else 0, register)
    if mode.endswith('_cancel'):
        # Même couleur, après l'annulation : force le replay même sans masque
        # restant, pour qu'une extension basse oubliée reste visible au pixel.
        w(0x31FC, 0x0070, 0x8240)
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
    out.write_bytes(image)
    print(f'{out}: {mode}, {len(code)} octets de code, {len(image)} octets')


if __name__ == '__main__':
    out = Path(sys.argv[1])
    mode = sys.argv[2] if len(sys.argv) > 2 else next(
        (m for m in ('nosync_cancel', 'sync_high_cancel', 'sync_high', 'nosync',
                     'node_late', 'node', 'blank', 'stop') if f'blankline_{m}' in out.stem), '')
    make_disk(out, mode)
