#!/usr/bin/env python3
# Chemins relatifs « X:NOM » (disquette ou dossier) et noms hôtes longs (dossier seul).
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import *

def prg(tlen_code, data=b'', bss=0, reloc=b'\0\0\0\0'):
    return struct.pack('>HIIIIIIH', 0x601A, len(tlen_code), len(data), bss, 0, 0, 0, 0) + tlen_code + data + reloc

SMALL = prg(b'\x4e\x75\x4e\x75', b'\x12\x34', 8)
BIG = prg(b'\x4e\x75\x4e\x75', b'', 0x00300000)

def mk_rel(drv):
    d = Data()
    for n, s in [('ld', 'LOWDIR2'), ('inner', 'LOWDIR2\\INNER.TXT'), ('dinner', drv + ':INNER.TXT'),
                 ('dstar', drv + ':*.*'), ('donly', drv + ':'), ('back', '\\'), ('small', 'SMALL.PRG'),
                 ('big', 'BIG.PRG'), ('empty', ''), ('cmd', b'\x03abc'), ('q', 'Q.TXT')]:
        d.s(n, s)
    d.b('smallb', 64, SMALL); d.b('bigb', 64, BIG)
    d.b('dta1', 44); d.b('pb', 64)
    T = []
    def t(id, fn, args, **kw):
        kw.setdefault('save', 0xFFFF); T.append(dict(id=id, fn=fn, args=args, **kw))
    t(0x01, 0x1A, [A('dta1')])
    t(0x02, 0x39, [A('ld')])
    t(0x03, 0x3C, [A('inner'), W(0)], save=1)
    t(0x04, 0x3E, [SW(1)])
    t(0x05, 0x3B, [A('ld')])
    t(0x06, 0x3D, [A('dinner'), W(0)], save=1)       # X:INNER.TXT relatif au dossier courant de X:
    t(0x07, 0x3E, [SW(1)])
    t(0x08, 0x4E, [A('dstar'), W(0)], dump=('a', 'dta1', 30, 12))
    t(0x09, 0x3B, [A('donly')])                      # Dsetpath("X:")
    t(0x0A, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 10))
    t(0x0B, 0x3B, [A('back')])
    t(0x0C, 0x3C, [A('small'), W(0)], save=1)
    t(0x0D, 0x40, [SW(1), L(len(SMALL)), A('smallb')])
    t(0x0E, 0x3E, [SW(1)])
    t(0x0F, 0x3C, [A('big'), W(0)], save=1)
    t(0x10, 0x40, [SW(1), L(len(BIG)), A('bigb')])
    t(0x11, 0x3E, [SW(1)])
    t(0x12, 0x48, [L(-1)])
    t(0x13, 0x4B, [W(0), A('big'), A('empty'), L(0)])
    t(0x14, 0x48, [L(-1)])
    t(0x15, 0x4B, [W(0), A('small'), A('cmd'), L(0)])
    t(0x16, 0x48, [L(-1)])
    t(0x17, 0x4B, [W(3), A('small'), A('cmd'), L(0)], save=2, dump=('s', 2, 8, 24))
    t(0x18, 0x48, [L(-1)])                           # (pour voir)
    t(0x19, 0x48, [L(0)], dump=('s', 2, 0x80, 6))    # Malloc(0) ; dump de la ligne de commande
    t(0x1A, 0x48, [L(0)], dump=('s', 2, 0x100, 16))  # texte+données+bss relogés
    t(0x1B, 0x48, [L(0)], dump=('s', 2, 0x24, 8))    # p_parent, p_resrvd0(env)
    p = Prog(d); p.table('main', T)
    return build(p)

def mk_host():
    d = Data()
    for n, s in [('all', '*.*'), ('lowdir', 'LOWDIR'), ('longdir', 'LONGDIRE'), ('back', '\\'),
                 ('lf', 'LONGFILE.TXT'), ('sp', 'MY FILE.TXT'), ('newf', 'NEWFILE.TXT'),
                 ('arch', 'ARCHIVE+.GZ'), ('deep', 'LONGDIRE\\SUBDIREC')]:
        d.s(n, s)
    d.b('dta1', 44); d.b('pb', 96); d.b('buf', 32)
    T = []
    def t(id, fn, args, **kw):
        kw.setdefault('save', 0xFFFF); T.append(dict(id=id, fn=fn, args=args, **kw))
    t(0x01, 0x1A, [A('dta1')])
    t(0x02, 0x4E, [A('all'), W(0x10)], dump=('a', 'dta1', 21, 23))
    for i in range(16): t(0x03 + i, 0x4F, [], dump=('a', 'dta1', 21, 23))
    t(0x20, 0x3B, [A('lowdir')]);  t(0x21, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 12))
    t(0x22, 0x3B, [A('back')])
    t(0x23, 0x3B, [A('longdir')]); t(0x24, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 24))
    t(0x25, 0x3B, [A('back')])
    t(0x26, 0x3B, [A('deep')]); t(0x27, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 40))
    t(0x28, 0x3B, [A('back')])
    t(0x29, 0x3D, [A('lf'), W(0)], save=1)
    t(0x2A, 0x3F, [SW(1), L(16), A('buf')], dump=('a', 'buf', 0, 8)); t(0x2B, 0x3E, [SW(1)])
    t(0x2C, 0x3D, [A('sp'), W(0)], save=1); t(0x2D, 0x3E, [SW(1)])
    t(0x2E, 0x3D, [A('arch'), W(0)], save=1); t(0x2F, 0x3E, [SW(1)])
    t(0x30, 0x3C, [A('newf'), W(0)], save=1); t(0x31, 0x3E, [SW(1)])
    p = Prog(d); p.table('main', T)
    return build(p)
