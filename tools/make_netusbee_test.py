#!/usr/bin/env python3
"""Disquette AUTO USB4TOS + test GEMDOS du disque USB et du clavier.

Usage : make_netusbee_test.py USB4TOS.zip OUT.st
Le pilote n'est pas livré dans le dépôt. Archive officielle épinglée :
https://atari.joska.no/snapshots/freemint/usb4tos/usb4tos-1-19-098f002a.zip
Ordre AUTO : USB, classes, NetUSBee 68000, puis probe. Le disque USB attendu est
celui de make_usatan_hd.py (README.TXT). Aucun accès aux registres USB depuis le
probe : Fopen/Fread/Fcreate/Fwrite et Bconin passent par les vrais pilotes.
[C] USB4TOS 098f002a — contrat logiciel, pas mesure du silicium.
"""
import struct
import sys
import zipfile
from pathlib import Path

from make_usatan_test import Asm, build_floppy


def probe(hid=False):
    a = Asm(0)
    if hid: a.br(0x6000,"keyboard")
    # Fopen("C:\\README.TXT",0), Fread 4 octets : "Ultr".
    a.w(0x4267); a.lea_lbl("readme",0); a.w(0x2F08,0x3F3C,0x3D,0x4E41,0x508F)
    a.w(0x4A80); a.br(0x6B00,"fail"); a.w(0x3C00)
    a.lea_lbl("buffer",0); a.w(0x2F08,0x2F3C); a.l32(4)
    a.w(0x3F06,0x3F3C,0x3F,0x4E41,0x4FEF,12)
    a.w(0x0C80); a.l32(4); a.br(0x6600,"fail")
    a.lea_lbl("buffer",0); a.w(0x0C90); a.l32(0x556C7472); a.br(0x6600,"fail")
    a.w(0x3F06,0x3F3C,0x3E,0x4E41,0x588F)
    # Fcreate/Fwrite : les secteurs doivent être relus depuis l'image hôte.
    a.w(0x4267); a.lea_lbl("output",0); a.w(0x2F08,0x3F3C,0x3C,0x4E41,0x508F)
    a.w(0x4A80); a.br(0x6B00,"fail"); a.w(0x3C00)
    a.lea_lbl("message",0); a.w(0x2F08,0x2F3C); a.l32(4)
    a.w(0x3F06,0x3F3C,0x40,0x4E41,0x4FEF,12)
    a.w(0x0C80); a.l32(4); a.br(0x6600,"fail")
    a.w(0x3F06,0x3F3C,0x3E,0x4E41,0x588F)
    a.lea_lbl("diskpass",3); a.br(0x6100,"emit")
    # Bconin(2) : attend le 'a' envoyé par --keys-at, donc livré par keyboard.prg.
    a.lbl("keyboard"); a.w(0x3F3C,2,0x3F3C,2,0x4E4D,0x588F,0x0C00,ord('a'))
    a.br(0x6600,"fail"); a.lea_lbl("keypass",3); a.br(0x6100,"emit")
    if hid:
        # Avant GEM, son pointeur n'est pas initialisé. Sonder mousevec de
        # Kbdvbase : c'est le vrai paquet IKBD construit par mouse.prg.
        a.w(0x42A7,0x3F3C,0x20,0x4E41,0x5C8F) # Super(0)
        a.w(0x3F3C,0x22,0x4E4E,0x548F,0x2A40,0x2C2D,16)
        a.lea_lbl("capturemouse",0); a.w(0x2B48,16,0x7E50)
        a.lbl("waitmouse"); a.w(0x3F3C,37,0x4E4E,0x548F); a.dbra(7,"waitmouse")
        a.w(0x2B46,16); a.lea_lbl("packet",0); a.w(0x4A28,1); a.br(0x6700,"fail")
        a.lea_lbl("mousepass",3); a.br(0x6100,"emit")
    a.br(0x6000,"exit")
    a.lbl("fail"); a.lea_lbl("failure",3); a.br(0x6100,"emit")
    a.lbl("exit"); a.w(0x4267,0x4E41)
    a.lbl("emit"); a.w(0x121B); a.br(0x6700,"ret")
    a.w(0x3F01,0x3F3C,1,0x3F3C,3,0x4E4D,0x5C8F); a.br(0x6000,"emit")
    a.lbl("ret"); a.w(0x4E75)
    if hid:
        a.lbl("capturemouse"); a.w(0x33D0); a.abs32("packet"); a.w(0x4E75)
        a.lbl("packet"); a.w(0)
    for label, text in [("readme","C:\\README.TXT"),("output","C:\\USBTEST.TXT"),
                        ("message","USB!"),("diskpass","NEOST-TEST: usbdisk PASS\r\n"),
                        ("mousepass","NEOST-TEST: usbmouse PASS\r\n"),
                        ("keypass","NEOST-TEST: usbkey PASS\r\n"),("failure","NEOST-TEST: usb FAIL\r\n")]:
        a.string(label,text)
    a.lbl("buffer"); a.w(0,0)
    text = a.assemble()
    reloc = bytearray(struct.pack('>I',a.relocs[0]))
    for before, after in zip(a.relocs,a.relocs[1:]):
        delta = after - before
        while delta > 254: reloc.append(1); delta -= 254
        reloc.append(delta)
    reloc.append(0)
    return struct.pack('>HIIIIIIH',0x601A,len(text),0,0,0,0,0,0) + text + reloc


def build(archive, hid=False):
    image = bytearray(build_floppy(b''))
    root, data = 7 * 512, 14 * 512
    image[root:root + 112 * 32] = bytes(112 * 32)
    image[data:data + 1024] = bytes(1024)
    image[root:root + 11] = b'AUTO       '; image[root + 11] = 16
    struct.pack_into('<H',image,root + 26,2)

    def fat(cluster, value):
        for base in (512,2048):
            off = base + cluster * 3 // 2
            if cluster & 1:
                image[off] = (image[off] & 15) | ((value << 4) & 240); image[off+1] = value >> 4
            else:
                image[off] = value & 255; image[off+1] = (image[off+1] & 240) | (value >> 8)

    with zipfile.ZipFile(archive) as z:
        files = []
        for name in ('usb','keyboard','mouse','storage','netus000'):
            entry = next(n for n in z.namelist() if n.lower().endswith('/' + name + '.prg'))
            files.append((name.upper(),z.read(entry)))
    files.append(('USBPROBE',probe(hid)))
    cluster = 3
    fat(2,0xFFF)
    for slot, (name, content) in enumerate(files):
        at = data + slot * 32
        image[at:at+11] = name.ljust(8).encode() + b'PRG'; image[at+11] = 32
        struct.pack_into('<HI',image,at+26,cluster,len(content))
        count = (len(content) + 1023) // 1024
        for n in range(count): fat(cluster+n,0xFFF if n+1 == count else cluster+n+1)
        off = data + (cluster-2) * 1024
        image[off:off+len(content)] = content
        cluster += count
    return image


if __name__ == '__main__':
    if len(sys.argv) not in (3,4):
        print(__doc__); sys.exit(2)
    Path(sys.argv[2]).write_bytes(build(sys.argv[1], '--hid' in sys.argv[3:]))
