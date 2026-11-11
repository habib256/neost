#!/usr/bin/env python3
# Programme principal du banc : fichiers, dossiers, DTA, Fdatime, attributs, Fforce,
# Pexec (héritage du dossier courant, Ptermres) — et le lanceur de fuite mk_leak.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import *

def mk_main():
    d = Data()
    for n, s in [('f1', 'F1.TXT'), ('f3', 'F3.TXT'), ('ro', 'RO.TXT'), ('ro2', 'RO2.TXT'),
                 ('subd', 'SUBD'), ('subx', 'SUBX'), ('f2', 'SUBD\\F2.TXT'), ('f2r', 'F2.TXT'),
                 ('nodirx', 'NODIR\\X.TXT'), ('nofile', 'NOFILE.TXT'), ('nodir', 'NODIR'),
                 ('nodirsub', 'NODIR\\SUB'), ('all', '*.*'), ('star', '*'), ('fq', 'F?.TXT'),
                 ('suball', 'SUBD\\*.*'), ('dotdot', '..'), ('dot', '.'), ('back', '\\'),
                 ('bsubd', '\\SUBD'), ('dsubdot', '.\\SUBD\\.'), ('subf1', 'SUBD\\F1.TXT'),
                 ('noall', 'NOFILE.*'), ('nodirall', 'NODIR\\*.*'), ('tprg', 'T.PRG'),
                 ('noprg', 'NOFILE.PRG'), ('nodirprg', 'NODIR\\X.PRG'), ('out', 'OUT.TXT'),
                 ('digits', '0123456789'), ('abc', 'abc'), ('data4', 'data'), ('hello', 'hello'),
                 ('xyz', 'xyz'), ('empty', ''), ('dbsub', '\\SUBD\\'), ('g1', 'G1.TXT'), ('g2', 'G2.TXT'), ('g3', 'G3.TXT')]:
        d.s(n, s)
    d.s('cmdP', b'\x01P'); d.s('cmdD', b'\x01D'); d.s('cmdQ', b'\x01Q'); d.s('cmdI', b'\x01I')
    d.s('btprg', '\\T.PRG'); d.s('h1', 'H1.TXT')
    d.b('buf', 64); d.b('dta1', 44); d.b('dta2', 44); d.b('dfb', 16); d.b('pb', 64)
    d.b('dtset', 4, bytes([0x60, 0x00, (10 << 1) | 0, (1 << 5) | 1]))   # 12:00:00, 1990-01-01
    d.b('dtget', 4)
    T = []
    def t(id, fn, args, **kw):
        kw.setdefault('save', 0xFFFF); T.append(dict(id=id, fn=fn, args=args, **kw))
    DTA1 = ('a', 'dta1', 21, 23); DTA2 = ('a', 'dta2', 21, 23)
    t(0x01, 0x19, [])                                          # Dgetdrv
    t(0x02, 0x3C, [A('f1'), W(0)], save=1)                     # Fcreate F1
    t(0x03, 0x40, [SW(1), L(10), A('digits')])
    t(0x04, 0x40, [SW(1), L(0), A('buf')])
    t(0x05, 0x42, [L(0), SW(1), W(1)])
    t(0x06, 0x42, [L(5), SW(1), W(0)])
    t(0x07, 0x42, [L(20), SW(1), W(0)])
    t(0x08, 0x42, [L(-1), SW(1), W(0)])
    t(0x09, 0x42, [L(0), SW(1), W(2)])
    t(0x0A, 0x42, [L(-3), SW(1), W(2)])
    t(0x0B, 0x42, [L(2), SW(1), W(2)])
    t(0x0C, 0x3F, [SW(1), L(100), A('buf')], dump=('a', 'buf', 0, 4))
    t(0x0D, 0x3F, [SW(1), L(10), A('buf')])
    t(0x0E, 0x42, [L(0), SW(1), W(0)])
    t(0x0F, 0x3F, [SW(1), L(-1), A('buf')], dump=('a', 'buf', 0, 4))
    t(0x10, 0x42, [L(0), SW(1), W(1)])
    t(0x11, 0x3E, [SW(1)])
    t(0x12, 0x3E, [SW(1)])                                     # double Fclose
    t(0x13, 0x3D, [A('f1'), W(0)], save=2)
    t(0x14, 0x40, [SW(2), L(3), A('abc')])                     # Fwrite sur mode 0
    t(0x15, 0x3E, [SW(2)])
    t(0x16, 0x3D, [A('nofile'), W(0)])
    t(0x17, 0x3D, [A('nodirx'), W(0)])
    t(0x18, 0x3C, [A('nodirx'), W(0)])
    t(0x19, 0x3C, [A('ro'), W(1)], save=3)                     # Fcreate RO
    t(0x1A, 0x40, [SW(3), L(4), A('data4')])
    t(0x1B, 0x3E, [SW(3)])
    t(0x1C, 0x43, [A('ro'), W(0), W(0)])
    t(0x1D, 0x3D, [A('ro'), W(2)], save=4)
    t(0x1E, 0x3E, [SW(4)])
    t(0x1F, 0x3D, [A('ro'), W(1)], save=4)
    t(0x20, 0x3E, [SW(4)])
    t(0x21, 0x3C, [A('ro'), W(0)], save=4)                     # Fcreate sur RO existant
    t(0x22, 0x3E, [SW(4)])
    t(0x23, 0x56, [W(0), A('ro'), A('ro2')])                   # Frename RO
    t(0x24, 0x56, [W(0), A('ro2'), A('ro')])
    t(0xC8, 0x3D, [A('ro'), W(0)], save=4)                     # Fopen RO mode 0
    t(0xC9, 0x3E, [SW(4)])
    t(0xCB, 0x41, [A('ro')])                                   # Fdelete RO
    t(0x25, 0x43, [A('f1'), W(0), W(0)])
    t(0x26, 0x43, [A('f1'), W(1), W(0x02)])
    t(0x27, 0x43, [A('f1'), W(0), W(0)])
    t(0x28, 0x43, [A('f1'), W(1), W(0x00)])
    t(0x29, 0x43, [A('nodirx'), W(0), W(0)])
    t(0x2A, 0x43, [A('nofile'), W(0), W(0)])
    t(0x2B, 0x39, [A('subd')])
    t(0x2C, 0x39, [A('subd')])
    t(0x2D, 0x39, [A('nodirsub')])
    t(0x2E, 0x43, [A('subd'), W(0), W(0)])
    t(0x2F, 0x43, [A('subd'), W(1), W(0x00)])
    t(0x30, 0x43, [A('subd'), W(0), W(0)])
    t(0x31, 0x3C, [A('f2'), W(0)], save=5)
    t(0x32, 0x40, [SW(5), L(5), A('hello')])
    t(0x33, 0x3E, [SW(5)])
    t(0x34, 0x3A, [A('subd')])                                 # Ddelete non vide
    t(0x35, 0x3A, [A('f1')])                                   # Ddelete fichier
    t(0xCC, 0x3C, [A('subd'), W(0)])                           # Fcreate sur un dossier
    t(0xCD, 0x39, [A('f1')])                                   # Dcreate sur un fichier
    t(0xCE, 0x3D, [A('f1'), W(0)], save=4)
    t(0xCF, 0x42, [L(0), SW(4), W(3)])                         # Fseek mode 3
    t(0xD0, 0x3E, [SW(4)])
    t(0xD2, 0x1A, [A('dta1')])
    t(0xD3, 0x3C, [A('g1'), W(0)], save=4)                     # Fdatime sans écriture
    t(0xD4, 0x57, [A('dtset'), SW(4), W(1)])
    t(0xD5, 0x3E, [SW(4)])
    t(0xD6, 0x4E, [A('g1'), W(0)], dump=('a', 'dta1', 22, 8))
    t(0xD7, 0x3C, [A('g2'), W(0)], save=4)                     # Fdatime puis Fwrite
    t(0xD8, 0x57, [A('dtset'), SW(4), W(1)])
    t(0xD9, 0x40, [SW(4), L(3), A('xyz')])
    t(0xDA, 0x3E, [SW(4)])
    t(0xDB, 0x4E, [A('g2'), W(0)], dump=('a', 'dta1', 22, 8))
    t(0xDC, 0x3C, [A('g3'), W(0)], save=4)                     # Fwrite puis Fdatime
    t(0xDD, 0x40, [SW(4), L(3), A('xyz')])
    t(0xDE, 0x57, [A('dtset'), SW(4), W(1)])
    t(0xDF, 0x3E, [SW(4)])
    t(0xE0, 0x4E, [A('g3'), W(0)], dump=('a', 'dta1', 22, 8))
    t(0x36, 0x3A, [A('nodir')])
    t(0x37, 0x1A, [A('dta1')])                                 # Fsetdta
    t(0x38, 0x4E, [A('all'), W(0)], dump=DTA1)
    for i in range(6): t(0x39 + i, 0x4F, [], dump=DTA1)
    t(0x40, 0x4E, [A('all'), W(0x10)], dump=DTA1)
    for i in range(6): t(0x41 + i, 0x4F, [], dump=DTA1)
    t(0x48, 0x4E, [A('suball'), W(0x10)], dump=DTA1)
    for i in range(4): t(0x49 + i, 0x4F, [], dump=DTA1)
    t(0x4E, 0x4E, [A('star'), W(0x10)], dump=DTA1)
    t(0x4F, 0x4F, [], dump=DTA1)
    t(0x50, 0x4E, [A('fq'), W(0)], dump=DTA1)
    t(0x51, 0x4E, [A('noall'), W(0)])
    t(0x52, 0x4E, [A('nodirall'), W(0)])
    t(0x53, 0x4E, [A('all'), W(0x08)], dump=DTA1)
    t(0x54, 0x4E, [A('subd'), W(0x10)], dump=DTA1)
    t(0x55, 0x4E, [A('suball'), W(0)], dump=DTA1)             # DTA1 : SUBD\*.* fichiers
    t(0x56, 0x1A, [A('dta2')])
    t(0x57, 0x4E, [A('all'), W(0)], dump=DTA2)
    t(0x58, 0x1A, [A('dta1')])
    t(0x59, 0x4F, [], dump=DTA1)                               # suite de DTA1
    t(0x5A, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))     # Dgetpath
    t(0x5B, 0x3B, [A('subd')])
    t(0x5C, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))
    t(0x5D, 0x3D, [A('f2r'), W(0)], save=6)
    t(0x5E, 0x3E, [SW(6)])
    t(0x5F, 0x3B, [A('dotdot')])
    t(0x60, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))
    t(0x61, 0x3B, [A('nodir')])
    t(0x62, 0x3B, [A('f1')])                                   # Dsetpath sur un fichier
    t(0x63, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))
    t(0x64, 0x3B, [A('back')])
    t(0x65, 0x3B, [A('dsubdot')])
    t(0x66, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))
    t(0x67, 0x3B, [A('back')])
    t(0x68, 0x3B, [A('dbsub')])                                # "\SUBD\" (séparateur final)
    t(0x69, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))
    t(0x6A, 0x3B, [A('back')])
    # Fdatime
    t(0x70, 0x3D, [A('f1'), W(2)], save=6)
    t(0x71, 0x57, [A('dtset'), SW(6), W(1)])
    t(0x72, 0x57, [A('dtget'), SW(6), W(0)], dump=('a', 'dtget', 0, 4))
    t(0x73, 0x42, [L(0), SW(6), W(2)])
    t(0x74, 0x40, [SW(6), L(3), A('xyz')])                     # écriture APRÈS Fdatime
    t(0x75, 0x3E, [SW(6)])
    t(0x76, 0x4E, [A('f1'), W(0)], dump=('a', 'dta1', 22, 8))
    # Frename
    t(0x78, 0x3C, [A('f3'), W(0)], save=7)
    t(0x79, 0x3E, [SW(7)])
    t(0x7A, 0x56, [W(0), A('f1'), A('f3')])                    # vers un existant
    t(0x7B, 0x56, [W(0), A('f1'), A('subf1')])                 # entre dossiers
    t(0x7C, 0x56, [W(0), A('subf1'), A('f1')])
    t(0x7D, 0x56, [W(0), A('subd'), A('subx')])                # dossier
    t(0x7E, 0x56, [W(0), A('subx'), A('subd')])
    t(0x7F, 0x56, [W(0), A('nofile'), A('f3')])
    # Fdelete fichier ouvert
    t(0x80, 0x3D, [A('f3'), W(0)], save=7)
    t(0x81, 0x41, [A('f3')])
    t(0x82, 0x3E, [SW(7)])
    t(0x83, 0x41, [A('nofile')])
    t(0x84, 0x41, [A('nodirx')])
    # Fforce / Fdup
    t(0x88, 0x3C, [A('out'), W(0)], save=9)
    t(0x89, 0x45, [W(1)], save=10)                             # Fdup(1)
    t(0x8A, 0x46, [W(1), SW(9)])                               # Fforce(1, fichier)
    t(0x8B, 0x40, [W(1), L(5), A('hello')])
    t(0x8C, 0x46, [W(1), SW(10)])                              # Fforce(1, sauvegarde)
    t(0x8D, 0x40, [W(1), L(3), A('xyz')])
    t(0x8E, 0x3E, [SW(9)])
    t(0x8F, 0x3E, [SW(10)])
    t(0x90, 0x3D, [A('out'), W(0)], save=9)
    t(0x91, 0x3F, [SW(9), L(16), A('buf')], dump=('a', 'buf', 0, 8))
    t(0x92, 0x3E, [SW(9)])
    # Pexec
    t(0xA0, 0x4B, [W(0), A('noprg'), A('empty'), L(0)])
    t(0xA1, 0x4B, [W(0), A('nodirprg'), A('empty'), L(0)])
    t(0xA2, 0x4B, [W(0), A('f1'), A('empty'), L(0)])
    t(0xA4, 0x4B, [W(0), A('tprg'), A('cmdP'), L(0)])          # enfant : Dsetpath SUBD
    t(0xA5, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))
    t(0xA6, 0x3D, [A('f1'), W(0)], save=6)
    t(0xA7, 0x3E, [SW(6)])
    t(0xA8, 0x3B, [A('back')])
    t(0xA9, 0x4B, [W(0), A('tprg'), A('cmdD'), L(0)])          # enfant : Dsetdrv(1)
    t(0xAA, 0x19, [])
    t(0xAB, 0x3D, [A('f1'), W(0)], save=6)
    t(0xAC, 0x3E, [SW(6)])
    t(0xAD, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))
    # Héritage : l'enfant part du dossier du parent, son Dsetpath ne remonte pas
    t(0x93, 0x3B, [A('subd')])
    t(0x94, 0x4B, [W(0), A('btprg'), A('cmdI'), L(0)])         # enfant : Dgetpath, Dsetpath \
    t(0x95, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))     # parent : toujours \SUBD
    t(0x96, 0x3B, [A('back')])
    # Attributs caché / système / archive
    t(0xBC, 0x3C, [A('h1'), W(0x02)], save=6)                  # Fcreate caché
    t(0xBD, 0x3E, [SW(6)])
    t(0xBE, 0x43, [A('h1'), W(0), W(0)])
    t(0xBF, 0x4E, [A('h1'), W(0)], dump=('a', 'dta1', 21, 1))  # masque 0 : caché+archive listé ?
    t(0xC4, 0x43, [A('h1'), W(1), W(0x04)])                    # système seul
    t(0xC5, 0x43, [A('h1'), W(0), W(0)])
    t(0xC6, 0x4E, [A('h1'), W(0)])                             # masque 0 : système seul caché
    t(0xC7, 0x4E, [A('h1'), W(0x04)], dump=('a', 'dta1', 21, 1))
    t(0xCA, 0x41, [A('h1')])
    # Ddelete du dossier courant
    t(0xB0, 0x41, [A('f2')])
    t(0xB1, 0x3B, [A('subd')])
    t(0xB2, 0x3A, [A('bsubd')])
    t(0xB3, 0x47, [A('pb'), W(0)], dump=('a', 'pb', 0, 8))
    t(0xB4, 0x3B, [A('back')])
    t(0xB5, 0x3A, [A('subd')])
    t(0xB6, 0x36, [A('dfb'), W(0)], dump=('a', 'dfb', 0, 16))
    t(0xEE, 0x48, [L(-1)])                                     # Malloc(-1)
    t(0xF0, 0x4B, [W(3), A('tprg'), A('cmdQ'), L(0)], save=8, dump=('s', 8, 8, 24))
    t(0xF1, 0x4B, [W(3), A('tprg'), A('cmdQ'), L(0)], dump=('s', 8, 0x80, 4))
    t(0xF2, 0x48, [L(-1)])
    p = Prog(d)
    p.table('main', T)
    p.table('P', [dict(id=0xC1, fn=0x3B, args=[A('subd')]),
                  dict(id=0xC2, fn=0x47, args=[A('pb'), W(0)], dump=('a', 'pb', 0, 8)),
                  dict(id=0xC3, fn=0x4C, args=[W(7)])])
    p.table('D', [dict(id=0xD1, fn=0x0E, args=[W(5)]),
                  dict(id=0xD2, fn=0x4C, args=[W(9)])])
    p.table('Q', [dict(id=0xE1, fn=0x4C, args=[W(3)])])
    p.table('I', [dict(id=0x97, fn=0x47, args=[A('pb'), W(0)], dump=('a', 'pb', 0, 8)),
                  dict(id=0x98, fn=0x3B, args=[A('back')]),
                  dict(id=0x99, fn=0x4C, args=[W(4)])])
    p.table('R', [dict(id=0xE2, fn=0x31, args=[L(0x1000), W(5)])])   # Ptermres
    return build(p)

def mk_leak(prefix):
    # Lancé depuis A:\AUTO (TOS 1.0x) : Pexec 0 d'un enfant sur C: puis sur A:.
    d = Data()
    d.s('c', prefix + '\\T.PRG'); d.s('a', 'A:\\T.PRG'); d.s('cmdQ', b'\x01Q'); d.s('cmdR', b'\x01R')
    T = []
    def t(id, fn, args): T.append(dict(id=id, fn=fn, args=args))
    t(0x10, 0x48, [L(-1)])
    for i in range(3):
        t(0x11 + 2 * i, 0x4B, [W(0), A('c'), A('cmdQ'), L(0)]); t(0x12 + 2 * i, 0x48, [L(-1)])
    for i in range(3):
        t(0x21 + 2 * i, 0x4B, [W(0), A('a'), A('cmdQ'), L(0)]); t(0x22 + 2 * i, 0x48, [L(-1)])
    # Ptermres : le bloc gardé doit rester alloué, depuis C: comme depuis A:
    t(0x31, 0x4B, [W(0), A('c'), A('cmdR'), L(0)]); t(0x32, 0x48, [L(-1)])
    t(0x33, 0x4B, [W(0), A('a'), A('cmdR'), L(0)]); t(0x34, 0x48, [L(-1)])
    p = Prog(d); p.table('main', T)
    return build(p)
