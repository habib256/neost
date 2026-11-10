#!/usr/bin/env python3
"""Banc [C] USB4TOS 098f002a : clavier, souris et fichiers disque, sans réseau.

Code 77 sans archive officielle locale ou EmuTOS ; aucun téléchargement implicite.
"""
import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import make_netusbee_test
import make_usatan_hd

ROOT = Path(__file__).resolve().parent.parent
ARCHIVE = ROOT / 'extern/usb4tos/usb4tos-1-19-098f002a.zip'
SHA256 = '5aa775555b3a8a73d1b122c131ece1fb24f3b09a7654f8faf1125d13d0110e99'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--drivers',type=Path,default=ARCHIVE)
    args = ap.parse_args()
    headless = ROOT/'build'/('neost-headless.exe' if sys.platform == 'win32' else 'neost-headless')
    rom = ROOT/'roms/etos256us.img'
    if not args.drivers.exists() or not rom.exists() or not headless.exists():
        print('SKIP — NetUSBee USB4TOS : archive officielle locale, EmuTOS ou headless absent')
        return 77
    if hashlib.sha256(args.drivers.read_bytes()).hexdigest() != SHA256:
        print('FAIL — archive USB4TOS différente de 098f002a'); return 1
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        disk = tmp/'usb.img'; disk.write_bytes(make_usatan_hd.build())
        for hid in (False,True):
            floppy, serial = tmp/'drivers.st', tmp/'serial.txt'
            floppy.write_bytes(make_netusbee_test.build(args.drivers,hid))
            cmd = [str(headless),str(rom),'--mem','4m','--netusbee','--usb1','keyboard',
                   '--usb2','mouse' if hid else 'disk:' + str(disk),
                   '--disk',str(floppy),'--frames','2600','--keys-at','1800','a',
                   '--serial-dump',str(serial)]
            if hid: cmd += ['--mouse-at','1830','RRRRR']
            run = subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,timeout=120)
            text = serial.read_text(errors='replace') if serial.exists() else ''
            wanted = ['usbkey','usbmouse' if hid else 'usbdisk']
            if run.returncode or 'FAIL' in text or any(f'NEOST-TEST: {w} PASS' not in text for w in wanted):
                print('FAIL — USB4TOS ' + ('HID' if hid else 'disk'))
                print(text); print(run.stderr[-3000:]); return 1
            if not hid:
                # Le Fwrite invité doit avoir produit un vrai fichier FAT16.
                image = disk.read_bytes()
                root = (make_usatan_hd.PART_START + make_usatan_hd.RES +
                        make_usatan_hd.NFAT * make_usatan_hd.SPF) * 512
                data = root + make_usatan_hd.NROOT * 32
                entries = [image[p:p+32] for p in range(root,root+make_usatan_hd.NROOT*32,32)]
                entry = next((e for e in entries if e[:11] == b'USBTEST TXT'),None)
                cluster = struct.unpack_from('<H',entry,26)[0] if entry else 0
                size = struct.unpack_from('<I',entry,28)[0] if entry else 0
                offset = data + (cluster-2)*make_usatan_hd.SPC*512
                if cluster < 2 or size != 4 or image[offset:offset+4] != b'USB!':
                    print('FAIL — fichier USBTEST.TXT non persisté dans l’image'); return 1
    print('NETUSBEE USB4TOS OK : GEMDOS read/write + persisted file, keyboard Bconin, mousevec')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
