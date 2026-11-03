#!/usr/bin/env python3
# =============================================================================
#  make_blitter_programs.py — Programmes du banc du blitter (NeoST contre stBlitter.sv).
#
#  Chaque programme pose les registres du blitter, le lance et relit ses registres. Le témoin
#  est une fenêtre d'écritures CPU en $3000-$32FF, la seule partie du CPU que les deux bancs
#  relèvent : une écriture juste avant le départ date l'attente du bus, et la relecture des
#  registres passe par là. Fin de programme : écriture en $32FE. Code en $1000, vecteurs en
#  $0, source en $10000, destination en $18000.
#
#  Assemblage à la main, comme tools/make_fx68k_programs.py. [F] Encodages relus sans outil.
#  Tranché par : le désassembleur de Moira sur ces images.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import random
import struct
from pathlib import Path

CODE, STACK, SRC, DST = 0x1000, 0x8000, 0x10000, 0x18000
WIT, SENTINEL = 0x3000, 0x32FE
BLT = 0xFF8A00
NOP = 0x4E71


class P:
    def __init__(self):
        self.c = bytearray()

    def w(self, *ws):
        for x in ws:
            self.c += struct.pack(">H", x & 0xFFFF)
        return self

    def l(self, x):
        self.c += struct.pack(">I", x & 0xFFFFFFFF)
        return self

    def mw(self, reg, v):      # move.w #v,reg.l : [B] M68000UM § 8, MOVE, source #, dest abs.l
        return self.w(0x33FC, v).l(reg)

    def ml(self, reg, v):      # move.l #v,reg.l
        return self.w(0x23FC).l(v).l(reg)

    def mb(self, reg, v):      # move.b #v,reg.l
        return self.w(0x13FC, v & 0xFF).l(reg)

    def mark(self, off):       # move.w d7,$3000+off.l
        return self.w(0x33C7).l(WIT + off)

    def nops(self, n):
        return self.w(*([NOP] * n))


def setup(p, x=8, y=2, hop=2, op=3, skew=0, fxsr=False, nfsr=False, smudge=False, line=0,
          sxi=2, syi=2, dxi=2, dyi=2, em=(0xFFFF, 0xFFFF, 0xFFFF), src=SRC, dst=DST, ht=None):
    for i in range(16):
        p.mw(BLT + 2 * i, (ht or (lambda k: 0x1111 * k))(i))
    p.mw(BLT + 0x20, sxi).mw(BLT + 0x22, syi).ml(BLT + 0x24, src)
    p.mw(BLT + 0x28, em[0]).mw(BLT + 0x2A, em[1]).mw(BLT + 0x2C, em[2])
    p.mw(BLT + 0x2E, dxi).mw(BLT + 0x30, dyi).ml(BLT + 0x32, dst)
    p.mw(BLT + 0x36, x).mw(BLT + 0x38, y).mw(BLT + 0x3A, hop << 8 | op)
    return (0x20 if smudge else 0) << 8 | line << 8 | (0x80 if fxsr else 0) | (0x40 if nfsr else 0) | skew


def start(p, low, hog):
    p.w(0x7E00)                                     # moveq #0,d7
    p.mark(0)                                       # date le départ
    p.mw(BLT + 0x3C, (0xC000 if hog else 0x8000) | low)


def wait_busy(p):
    # .l: move.w $FF8A3C.l,d0 ; bmi.s .l. La lecture rend la main au blitter sans figer le CPU.
    p.w(0x3039).l(BLT + 0x3C).w(0x6BF8)


def dump(p):
    off = 2
    for r in (0x24, 0x26, 0x32, 0x34, 0x36, 0x38, 0x3C):
        p.w(0x3E39).l(BLT + r)                      # move.w reg.l,d7
        p.mark(off)
        off += 2
    p.w(0x7E00).mark(SENTINEL - WIT)


def simple(**kw):
    hog = kw.pop("hog", True)
    def gen(p):
        low = setup(p, **kw)
        start(p, low, hog)
        wait_busy(p)
        dump(p)
    return gen


def mulu_during_start(p):
    # Le CPU sort du bus pendant l'attente de l'arbitrage et une partie du blit.
    low = setup(p, x=64, y=4)
    p.w(0x303C, 0xFFFF)                             # move.w #$FFFF,d0
    start(p, low, False)
    p.w(0xC0C0, 0xC0C0, 0xC0C0)                     # mulu d0,d0 × 3
    wait_busy(p)
    dump(p)


def pause_resume(p):
    # BUSY effacé pendant le transfert, puis reposé : RTL et fiche disent une pause.
    low = setup(p, x=64, y=4)
    start(p, low, False)
    p.nops(30)
    p.mw(BLT + 0x3C, low)
    p.w(0x7E01).mark(0x40)                          # moveq #1,d7
    p.nops(8)
    p.mw(BLT + 0x3C, 0x8000 | low)
    wait_busy(p)
    dump(p)


def set_hog_while_running(p):
    low = setup(p, x=64, y=4)
    start(p, low, False)
    p.nops(90)
    p.mw(BLT + 0x3C, 0xC000 | low)
    wait_busy(p)
    dump(p)


def share_markers(p):
    # Écritures CPU en continu pendant le partage : la date de chaque tranche devient visible.
    low = setup(p, x=100, y=3)
    start(p, low, False)
    p.w(0x7E40)                                     # moveq #64,d7
    for i in range(50):
        p.mark(0x40 + 2 * i)
    wait_busy(p)
    dump(p)


def two_blits(p):
    # Le registre source et la pipeline survivent d'un blit à l'autre.
    low = setup(p, x=4, y=2, skew=4, fxsr=True)
    start(p, low, True)
    wait_busy(p)
    low = setup(p, x=4, y=2, skew=8, dst=DST + 0x400)
    start(p, low, True)
    wait_busy(p)
    dump(p)


def restart_done(p):
    # BUSY reposé après la fin : y_count à 0, rien ne doit repartir (protocole du TOS).
    low = setup(p, x=4, y=2)
    start(p, low, True)
    wait_busy(p)
    p.mw(BLT + 0x3C, 0x8000 | low)
    p.nops(16)
    wait_busy(p)
    dump(p)


def read_live(p):
    # Compteurs relus pendant le transfert : dates et valeurs passent par la fenêtre témoin.
    low = setup(p, x=100, y=3)
    start(p, low, False)
    off = 0x40
    for _ in range(12):
        for r in (0x24, 0x26, 0x32, 0x34, 0x36, 0x38, 0x3C):
            p.w(0x3E39).l(BLT + r)                  # move.w reg.l,d7
            p.mark(off)
            off += 2
    wait_busy(p)
    dump(p)


def pause_at(n):
    def gen(p):
        low = setup(p, x=64, y=4, op=6)
        start(p, low, False)
        p.nops(n)
        p.mw(BLT + 0x3C, low)
        p.w(0x3E39).l(BLT + 0x36).mark(0x40)        # compteur X figé ?
        p.w(0x3E39).l(BLT + 0x3C).mark(0x42)
        p.nops(6)
        p.mw(BLT + 0x3C, 0x8000 | low)
        wait_busy(p)
        dump(p)
    return gen


def edit_during_pause(p):
    # Registres réécrits pendant la pause : le transfert reprend-il avec eux ?
    low = setup(p, x=64, y=4)
    start(p, low, False)
    p.nops(70)
    p.mw(BLT + 0x3C, low)
    p.mw(BLT + 0x2A, 0x0FF0)
    p.mw(BLT + 0x3A, 0x0206)
    p.mw(BLT + 0x3C, 0x8000 | low | 3)
    wait_busy(p)
    dump(p)


def rewrite_y_during_pause(p):
    low = setup(p, x=64, y=4)
    start(p, low, False)
    p.nops(70)
    p.mw(BLT + 0x3C, low)
    p.mw(BLT + 0x38, 2)
    p.mw(BLT + 0x3C, 0x8000 | low)
    wait_busy(p)
    dump(p)


def write_hop_byte(p):
    low = setup(p, x=4, y=2, hop=0, op=0)
    p.mb(BLT + 0x3A, 2).mb(BLT + 0x3B, 3)
    start(p, low, True)
    wait_busy(p)
    dump(p)


def start_then(words):
    """Ce qui suit l'écriture de BUSY décide si le premier accès CPU compte pour le blitter."""
    def gen(p):
        low = setup(p, x=100, y=2)
        start(p, low, False)
        p.w(*words)
        wait_busy(p)
        dump(p)
    return gen


def gap(opcode):
    """Le CPU boucle sur une seule instruction pendant le partage : chaque coupure de tranche
    tombe à la même distance de l'accès CPU précédent. Compteur de 64 cycles bus (stBlitter.sv
    tmoCntr) contre fenêtre PRE_START de NeoST : c'est là que 63 et 64 accès se décident."""
    def gen(p):
        low = setup(p, x=100, y=4)
        start(p, low, False)
        p.w(*(opcode * 512))
        wait_busy(p)
        dump(p)
    return gen


PROGRAMS: dict[str, object] = {
    "start_nop": start_then([NOP]),                 # prochain accès à +0
    "start_bra": start_then([0x6000, 0x0002]),      # bra.w +2 : 2 cycles internes d'abord
    "start_tst": start_then([0x4A00, NOP]),         # tst.b d0 : accès à +0
    "gap0_nop": gap([NOP]),                         # 4 cycles, 1 accès
    "gap2_exg": gap([0xC141]),                      # exg d0,d1 : 6 cycles, 1 accès
    "gap4_addql": gap([0x5280]),                    # addq.l #1,d0 : 8 cycles, 1 accès
    "gap6_lsl2": gap([0xE548]),                     # lsl.w #2,d0 : 10 cycles, 1 accès
    "gap8_lsl3": gap([0xE748]),                     # lsl.w #3,d0 : 12 cycles, 1 accès
    "gapmix_nop_exg": gap([NOP, 0xC141]),
    "gapmix_nop_nop_lsl": gap([NOP, NOP, 0xE548]),
    "copy_hog": simple(),
    "copy_share_short": simple(hog=False, x=8, y=2),
    "copy_share_long": simple(hog=False, x=100, y=3),
    "share_rm_long": simple(hog=False, x=50, y=3, op=6),
    "halftone_only": simple(hop=1),
    "hop_zero": simple(hop=0),
    "hop_and": simple(hop=3),
    "smudge": simple(hop=3, smudge=True, skew=3, x=8, y=4),
    "line_up": simple(hop=1, line=7, dyi=-2, y=6, x=2),
    "x_one": simple(x=1, y=4),
    "x_two": simple(x=2, y=4),
    "mask_force_rd": simple(em=(0x00FF, 0xFFFF, 0xFF00)),
    "mask_center_only": simple(em=(0xFFFF, 0x0F0F, 0xFFFF)),
    "mask_x1": simple(x=1, y=3, em=(0x0FF0, 0xFFFF, 0xF00F)),
    "fxsr": simple(fxsr=True, skew=5),
    "nfsr": simple(nfsr=True, skew=5),
    "fxsr_nfsr": simple(fxsr=True, nfsr=True, skew=5),
    "fxsr_nfsr_x1": simple(x=1, y=3, fxsr=True, nfsr=True, skew=7),
    "fxsr_nfsr_x2": simple(x=2, y=3, fxsr=True, nfsr=True, skew=9),
    "nfsr_noop_src": simple(nfsr=True, op=0, skew=5),
    "reverse": simple(sxi=-2, dxi=-2, syi=-2, dyi=-2, src=SRC + 0x100, dst=DST + 0x100, fxsr=True, skew=3),
    "reverse_nfsr": simple(sxi=-2, dxi=-2, syi=-2, dyi=-2, src=SRC + 0x100, dst=DST + 0x100, nfsr=True, skew=3),
    "plane_step": simple(sxi=8, syi=-6, dxi=8, dyi=-6, x=6, y=3),
    "bit_inc_ignored": simple(sxi=3, dxi=5, syi=7, dyi=9, x=3, y=2),
    "mulu_during_start": mulu_during_start,
    "pause_resume": pause_resume,
    "set_hog_while_running": set_hog_while_running,
    "share_markers": share_markers,
    "two_blits": two_blits,
    "read_live": read_live,
    "edit_during_pause": edit_during_pause,
    "rewrite_y_during_pause": rewrite_y_during_pause,
    "write_hop_byte": write_hop_byte,
    "pause_early": pause_at(4),
    "pause_mid_word": pause_at(33),
    "pause_late": pause_at(90),
    "smudge_hop1": simple(hop=1, smudge=True, skew=2, x=4, y=2),
    "smudge_hop0": simple(hop=0, smudge=True, skew=2, x=4, y=2),
    "smudge_nfsr": simple(hop=3, smudge=True, nfsr=True, skew=11, x=6, y=3),
    "smudge_fxsr": simple(hop=3, smudge=True, fxsr=True, skew=4, x=6, y=3),
    "smudge_reverse": simple(hop=3, smudge=True, sxi=-2, dxi=-2, src=SRC + 0x100, dst=DST + 0x100, skew=6),
    "nfsr_reverse_fxsr": simple(fxsr=True, nfsr=True, sxi=-2, dxi=-2, syi=-2, dyi=-2, src=SRC + 0x100, dst=DST + 0x100, skew=6, x=5, y=3),
    "dest_only_force_mask": simple(op=0, em=(0xF000, 0xFFFF, 0x000F), x=3, y=3),
    "dst_same_as_src": simple(dst=SRC, x=8, y=2, op=6),
    "wide_x0": simple(x=0, y=1, sxi=0, dxi=0, op=3),
    "negative_y_share": simple(hog=False, x=70, y=3, syi=-200, dyi=-200, src=SRC + 0x1000, dst=DST + 0x1000),
    "share_x1": simple(hog=False, x=1, y=100),
    "share_nfsr_cut": simple(hog=False, x=50, y=3, nfsr=True, fxsr=True, skew=3, op=6),
    "restart_done": restart_done,
}
for _op in range(16):
    PROGRAMS[f"op_{_op:x}"] = simple(op=_op, x=4, y=2)
for _sk in (1, 8, 15):
    PROGRAMS[f"skew_{_sk}"] = simple(skew=_sk, x=4, y=2)


def build(name: str) -> bytes:
    p = P()
    PROGRAMS[name](p)
    code = bytes(p.c)
    assert CODE + len(code) < WIT, ("code sur la fenêtre témoin", name, len(code))
    img = bytearray(0x20000)
    img[0:8] = struct.pack(">II", STACK, CODE)
    img[CODE:CODE + len(code)] = code
    rnd = random.Random(0x1234)
    img[SRC:SRC + 0x8000] = bytes(rnd.randrange(256) for _ in range(0x8000))
    img[DST:DST + 0x8000] = bytes(rnd.randrange(256) for _ in range(0x8000))
    return bytes(img)


if __name__ == "__main__":
    import sys
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for n in PROGRAMS:
        (out / f"{n}.bin").write_bytes(build(n))
    print(f"{len(PROGRAMS)} programmes dans {out}")
