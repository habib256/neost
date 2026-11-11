#!/usr/bin/env python3
# Banc d'audit GEMDOS : interprète de table 68000 (un appel par entrée, D0 + dump
# hexadécimal sur l'AUX via Bconout(1)). Même programme sur disquette (vrai GEMDOS)
# et sur le dossier --gemdos (NeoST) : la différence des journaux est la preuve.
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from make_usatan_test import Asm  # noqa

SECT = 512

class Data:
    def __init__(self):
        self.buf = bytearray(); self.lab = {}
    def align(self):
        if len(self.buf) & 1: self.buf.append(0)
    def s(self, name, text):
        if name in self.lab: return
        self.align(); self.lab[name] = len(self.buf)
        self.buf += (text.encode('latin-1') if isinstance(text, str) else text) + b'\0'
    def b(self, name, n, init=b''):
        self.align(); self.lab[name] = len(self.buf)
        self.buf += init + bytes(n - len(init))

def W(v): return ('w', v)
def L(v): return ('l', v)
def A(lbl): return ('a', lbl)
def SW(k): return ('sw', k)
def SL(k): return ('sl', k)

class Prog:
    def __init__(self, d):
        self.d = d; self.tables = {}
    def table(self, name, tests):
        self.tables[name] = tests

def enc_table(d, tests):
    out = bytearray()
    for t in tests:
        tid, fn, args = t['id'], t['fn'], t['args']
        trap = t.get('trap', 1); save = t.get('save', 0xFFFF)
        dump = t.get('dump')   # (kind, slot_or_label, off, len)
        pushes = list(reversed(args)) + [W(fn)]
        out += struct.pack('>HH', tid, len(pushes))
        for k, v in pushes:
            kind = {'w': 0, 'l': 1, 'a': 2, 'sw': 3, 'sl': 4}[k]
            val = d.lab[v] if k == 'a' else v
            out += struct.pack('>HI', kind, val & 0xFFFFFFFF)
        out += struct.pack('>HH', trap, save)
        if dump is None:
            out += struct.pack('>HHHH', 0, 0, 0, 0)
        elif dump[0] == 'a':
            out += struct.pack('>HHHH', 1, 0, d.lab[dump[1]] + dump[2], dump[3])
        else:   # ('s', slot, off, len)
            out += struct.pack('>HHHH', 2, dump[1], dump[2], dump[3])
    out += struct.pack('>H', 0xFFFF)
    return out

def build(prog, stack=2048):
    d = prog.d
    a = Asm(0)
    a.w(0x206F, 0x0004)                       # move.l 4(sp),a0 (basepage)
    a.w(0x2608)                               # move.l a0,d3
    a.w(0x4FF9); a.abs32('stack_top')         # lea stack_top,sp
    a.w(0x2F3C); a.abs32('prog_end')          # move.l #prog_end,-(sp)  (relogé)
    a.w(0x2017)                               # move.l (sp),d0
    a.w(0x9083)                               # sub.l d3,d0  → taille depuis basepage
    a.w(0x2E80)                               # move.l d0,(sp)
    a.w(0x2F03)                               # move.l d3,-(sp)
    a.w(0x4267, 0x3F3C, 0x004A, 0x4E41, 0x4FEF, 0x000C)   # clr.w ; Mshrink ; trap ; lea 12(sp),sp
    a.w(0x4BF9); a.abs32('data')              # lea data,a5
    a.w(0x47F9); a.abs32('slots')             # lea slots,a3
    a.w(0x2743, 0x0038)                       # move.l d3,56(a3)  (slot 14 = basepage)
    a.w(0x2043)                               # movea.l d3,a0
    a.w(0x1028, 0x0081)                       # move.b $81(a0),d0
    a.w(0x49F9); a.abs32('tab_main')          # lea tab_main,a4
    for ch in sorted(prog.tables):
        if ch == 'main': continue
        a.w(0x0C00, ord(ch)); a.br(0x6600, 'nt_' + ch)   # cmpi.b #ch,d0 ; bne
        a.w(0x49F9); a.abs32('tab_' + ch)
        a.lbl('nt_' + ch)
    a.lbl('loop')
    a.w(0x3C1C)                               # move.w (a4)+,d6
    a.w(0x0C46, 0xFFFF); a.br(0x6700, 'done')
    a.w(0x3A1C)                               # move.w (a4)+,d5  nargs
    a.w(0x7E00)                               # moveq #0,d7
    a.lbl('argloop')
    a.w(0x5345); a.br(0x6B00, 'argsdone')     # subq.w #1,d5 ; bmi
    a.w(0x301C)                               # move.w (a4)+,d0 kind
    a.w(0x221C)                               # move.l (a4)+,d1 value
    a.w(0x4A40); a.br(0x6600, 'k1')
    a.w(0x3F01, 0x5487); a.br(0x6000, 'argloop')
    a.lbl('k1'); a.w(0x0C40, 1); a.br(0x6600, 'k2')
    a.w(0x2F01, 0x5887); a.br(0x6000, 'argloop')
    a.lbl('k2'); a.w(0x0C40, 2); a.br(0x6600, 'k3')
    a.w(0x41F5, 0x1800, 0x2F08, 0x5887); a.br(0x6000, 'argloop')
    a.lbl('k3'); a.w(0x0C40, 3); a.br(0x6600, 'k4')
    a.w(0xE589, 0x2433, 0x1800, 0x3F02, 0x5487); a.br(0x6000, 'argloop')
    a.lbl('k4')
    a.w(0xE589, 0x2F33, 0x1800, 0x5887); a.br(0x6000, 'argloop')
    a.lbl('argsdone')
    a.w(0x301C)                               # move.w (a4)+,d0 trap
    a.w(0x0C40, 1); a.br(0x6600, 't13')
    a.w(0x4E41); a.br(0x6000, 'post')
    a.lbl('t13'); a.w(0x0C40, 13); a.br(0x6600, 't14')
    a.w(0x4E4D); a.br(0x6000, 'post')
    a.lbl('t14'); a.w(0x4E4E)
    a.lbl('post')
    a.w(0xDFC7)                               # adda.l d7,sp
    a.w(0x2740, 0x003C)                       # move.l d0,60(a3)
    a.w(0x321C); a.br(0x6B00, 'nosave')       # move.w (a4)+,d1 ; bmi
    a.w(0xE549, 0x2780, 0x1000)               # lsl.w #2,d1 ; move.l d0,0(a3,d1.w)
    a.lbl('nosave')
    a.w(0x3606, 0x4843, 0x7804); a.br(0x6100, 'phex')    # id
    a.w(0x7020); a.br(0x6100, 'putc')
    a.w(0x262B, 0x003C, 0x7808); a.br(0x6100, 'phex')    # D0
    a.w(0x381C)                               # move.w (a4)+,d4 kind
    a.w(0x321C)                               # move.w (a4)+,d1 slot
    a.w(0x341C)                               # move.w (a4)+,d2 off
    a.w(0x3A1C)                               # move.w (a4)+,d5 len
    a.w(0x4A44); a.br(0x6700, 'eol')          # tst.w d4 ; beq
    a.w(0x0C44, 1); a.br(0x6600, 'dslot')
    a.w(0x4DF5, 0x2000); a.br(0x6000, 'dgo')  # lea 0(a5,d2.w),a6
    a.lbl('dslot')
    a.w(0xE549, 0x2C73, 0x1000)               # lsl.w #2,d1 ; movea.l 0(a3,d1.w),a6
    a.w(0xDCC2)                               # adda.w d2,a6
    a.lbl('dgo')
    a.w(0x4A45); a.br(0x6700, 'eol')
    a.w(0x7020); a.br(0x6100, 'putc')
    a.lbl('dloop')
    a.w(0x161E, 0xE09B, 0x7802); a.br(0x6100, 'phex')
    a.w(0x5345); a.br(0x6600, 'dloop')
    a.lbl('eol')
    a.w(0x700D); a.br(0x6100, 'putc')
    a.w(0x700A); a.br(0x6100, 'putc')
    a.br(0x6000, 'loop')
    a.lbl('done')
    a.w(0x4267, 0x4E41)                       # Pterm0
    # phex : d3 valeur (alignée en haut), d4 = nombre de quartets
    a.lbl('phex')
    a.w(0xE99B, 0x3003, 0x0240, 0x000F, 0x0C40, 0x000A); a.br(0x6D00, 'phd')
    a.w(0x5E40)
    a.lbl('phd')
    a.w(0x0640, 0x0030); a.br(0x6100, 'putc')
    a.w(0x5344); a.br(0x6600, 'phex')
    a.w(0x4E75)
    a.lbl('putc')
    a.w(0x3F00, 0x3F3C, 0x0001, 0x3F3C, 0x0003, 0x4E4D, 0x5C8F, 0x4E75)
    # données
    a.lbl('slots'); a.w(*([0] * 32))
    for name, tests in prog.tables.items():
        tb = enc_table(d, tests)
        if len(tb) & 1: tb += b'\0'
        a.lbl('tab_' + name)
        a.w(*[(tb[i] << 8) | tb[i + 1] for i in range(0, len(tb), 2)])
    d.align()
    a.lbl('data')
    a.w(*[(d.buf[i] << 8) | d.buf[i + 1] for i in range(0, len(d.buf), 2)])
    a.w(*([0] * (stack // 2)))
    a.lbl('stack_top')
    a.w(0, 0)
    a.lbl('prog_end')
    text = a.assemble()
    reloc = bytearray(struct.pack('>I', a.relocs[0])); prev = a.relocs[0]
    for r in a.relocs[1:]:
        dd = r - prev
        while dd > 254: reloc.append(1); dd -= 254
        reloc.append(dd); prev = r
    reloc.append(0)
    hdr = struct.pack('>HIIIIIIH', 0x601A, len(text), 0, 0, 0, 0, 0, 0)
    return hdr + text + bytes(reloc)

# ---------------------------------------------------------------- disquette
def floppy(files, autofiles=()):
    SPT, SIDES, TRACKS = 9, 2, 80
    TOTAL = TRACKS * SIDES * SPT
    img = bytearray(TOTAL * SECT)
    img[0:2] = b'\x60\x1c'; img[2:8] = b'NeoST '; img[8:11] = b'GDA'
    struct.pack_into('<H', img, 0x0B, SECT); img[0x0D] = 2
    struct.pack_into('<H', img, 0x0E, 1); img[0x10] = 2
    struct.pack_into('<H', img, 0x11, 112); struct.pack_into('<H', img, 0x13, TOTAL)
    img[0x15] = 0xF9; struct.pack_into('<H', img, 0x16, 5)
    struct.pack_into('<H', img, 0x18, SPT); struct.pack_into('<H', img, 0x1A, SIDES)
    RES, NFAT, SPF, NDIRS, SPC = 1, 2, 5, 112, 2
    fat1 = RES * SECT
    root = (RES + NFAT * SPF) * SECT
    data = root + ((NDIRS * 32 + SECT - 1) // SECT) * SECT
    csize = SPC * SECT
    def set_fat(idx, val):
        for base in (fat1, fat1 + SPF * SECT):
            off = base + idx * 3 // 2
            if idx & 1:
                img[off] = (img[off] & 0x0F) | ((val << 4) & 0xF0); img[off + 1] = (val >> 4) & 0xFF
            else:
                img[off] = val & 0xFF; img[off + 1] = (img[off + 1] & 0xF0) | ((val >> 8) & 0x0F)
    set_fat(0, 0xFF9); set_fat(1, 0xFFF)
    DATE = (46 << 9) | (8 << 5) | 21; TIME = (12 << 11)
    nextc = [2]
    def entry(at, name, ext, attr, cluster, size):
        img[at:at + 8] = name.ljust(8).encode(); img[at + 8:at + 11] = ext.ljust(3).encode()
        img[at + 11] = attr
        struct.pack_into('<H', img, at + 22, TIME); struct.pack_into('<H', img, at + 24, DATE)
        struct.pack_into('<H', img, at + 26, cluster); struct.pack_into('<I', img, at + 28, size)
    def put(content):
        c0 = nextc[0]; n = max(1, (len(content) + csize - 1) // csize)
        for k in range(n):
            c = c0 + k; o = data + (c - 2) * csize
            chunk = content[k * csize:(k + 1) * csize]
            img[o:o + len(chunk)] = chunk
            set_fat(c, 0xFFF if k == n - 1 else c + 1)
        nextc[0] += n
        return c0
    slot = 0
    if autofiles:
        ac = put(bytes(csize))
        entry(root, 'AUTO', '', 0x10, ac, 0); slot = 1
        d = data + (ac - 2) * csize
        entry(d, '.', '', 0x10, ac, 0); entry(d + 32, '..', '', 0x10, 0, 0)
        for i, (fn, content) in enumerate(autofiles):
            n, e = fn.split('.')
            c = put(content)
            entry(d + 64 + 32 * i, n, e, 0x20, c, len(content))
    for i, (fn, content) in enumerate(files):
        n, e = fn.split('.')
        c = put(content)
        entry(root + 32 * (slot + i), n, e, 0x20, c, len(content))
    return bytes(img)
