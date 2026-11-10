#!/usr/bin/env python3
# =============================================================================
#  make_fx68k_programs.py — Programmes du banc différentiel Moira↔fx68k.
#
#  Chaque programme est une image mémoire plate (vecteurs en $0, code en $1000, données
#  en $2000, pile sous $8000) exécutée à l'identique par les deux cœurs nus
#  (tools/fx68k_bench.cpp, tools/moira_bare_bench.cpp) et comparée cycle bus par cycle
#  bus (tools/run_fx68k_bench.py). Fin de programme : une écriture dans $F00000
#  (sentinelle). Les interruptions sont vectorisées par le banc : niveau L → vecteur
#  $40+L, broche relâchée à l'IACK.
#
#  Assemblage à la main (pas d'assembleur 68000 dans la chaîne de build), comme
#  tools/make_cycle_bench.py. [F] Encodages relus à la main, sans outil : un opcode faux
#  mais légal ferait tester une autre instruction aux deux cœurs, sans écart visible.
#  Tranché par : le désassembleur de Moira sur ces images.
#
#  Usage : make_fx68k_programs.py <dossier>   → <dossier>/<nom>.bin + manifest.json
#          ou importé par run_fx68k_bench.py (PROGRAMS, build()).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

CODE = 0x1000
DATA = 0x2000
STACK = 0x8000
SENTINEL = 0xF00000


class Asm:
    """Assembleur minimal : mots bruts, labels, déplacements 8/16 bits résolus à la fin."""

    def __init__(self, org: int = CODE):
        self.org = org
        self.code = bytearray()
        self.labels: dict[str, int] = {}
        self.fixups: list[tuple[int, str, str]] = []

    def pc(self) -> int:
        return self.org + len(self.code)

    def w(self, *words: int) -> "Asm":
        for x in words:
            self.code.extend(struct.pack(">H", x & 0xFFFF))
        return self

    def l(self, x: int) -> "Asm":
        self.code.extend(struct.pack(">I", x & 0xFFFFFFFF))
        return self

    def label(self, name: str) -> "Asm":
        assert name not in self.labels, name
        self.labels[name] = self.pc()
        return self

    def bra8(self, opcode: int, target: str) -> "Asm":
        """Bcc.s / BRA.s / BSR.s : déplacement 8 bits dans l'opcode."""
        self.fixups.append((len(self.code), target, "d8"))
        self.w(opcode & 0xFF00)
        return self

    def bra16(self, opcode: int, target: str) -> "Asm":
        """Bcc.w / BSR.w / DBcc : mot de déplacement 16 bits."""
        self.w(opcode)
        self.fixups.append((len(self.code), target, "d16"))
        self.w(0)
        return self

    def addr(self, target: str) -> "Asm":
        """Adresse absolue 32 bits d'un label (mot long d'extension)."""
        self.fixups.append((len(self.code), target, "abs32"))
        self.l(0)
        return self

    def resolve(self) -> bytes:
        for off, name, kind in self.fixups:
            t = self.labels[name]
            if kind == "d8":
                d = t - (self.org + off + 2)
                assert -128 <= d <= 127 and d != 0, (name, d)
                self.code[off + 1] = d & 0xFF
            elif kind == "d16":
                d = t - (self.org + off)
                assert -32768 <= d <= 32767, (name, d)
                struct.pack_into(">h", self.code, off, d)
            else:
                struct.pack_into(">I", self.code, off, t)
        return bytes(self.code)


# --- Encodages utilisés (M68000UM § 8) -------------------------------------------
NOP = 0x4E71
RTE = 0x4E73
RTS = 0x4E75
ILLEGAL = 0x4AFC
RESET_SR_SUPER = (0x46FC, 0x2700)         # move.w #$2700,sr


def sentinel(a: Asm) -> None:
    a.w(0x33C0).l(SENTINEL)               # move.w d0,$F00000.l


def vectors(handlers: dict[int, str], default: str) -> Asm:
    """Table des vecteurs : 256 mots longs, chaque entrée pointe un label du code."""
    v = Asm(org=0)
    v.l(STACK).addr("start")
    for n in range(2, 256):
        v.addr(handlers.get(n, default))
    return v


def handlers(a: Asm) -> None:
    """Gestionnaires communs. PC empilé : groupe 0 → via la cellule $2000 (cont) ;
    illegal/privilège/ligne A/F → l'instruction elle-même (sauter 2 octets) ;
    trap/chk/trapv/div0/trace/IRQ → l'instruction suivante (rte simple)."""
    a.label("h_rte").w(RTE)
    a.label("h_skip2").w(0x54AF, 0x0002).w(RTE)          # addq.l #2,2(sp) ; rte
    # Groupe 0 (bus/adresse) : trame de 7 mots ; [B] M68000UM § 6.3.9 fig. 6-7.
    a.label("h_group0").w(0x508F)                        # addq.l #8,sp
    a.w(0x2F78, DATA, 0x0002).w(RTE)                     # move.l $2000.w,2(sp) ; rte


HANDLERS = {2: "h_group0", 3: "h_group0", 4: "h_skip2", 8: "h_skip2",
            10: "h_skip2", 11: "h_skip2"}


def prologue(a: Asm) -> None:
    a.label("start")
    a.w(*RESET_SR_SUPER)
    a.w(0x207C).l(DATA)                   # movea.l #$2000,a0
    a.w(0x227C).l(DATA + 0x100)           # movea.l #$2100,a1
    a.w(0x7000).w(0x7200).w(0x7402)       # moveq #0,d0 ; #0,d1 ; #2,d2


def prog_reset_nop(a: Asm) -> None:
    prologue(a)
    for _ in range(4):
        a.w(NOP)
    sentinel(a)


# Corps de boucle de tools/make_cycle_bench.py (a1 pointe la RAM ici, pas le MFP).
ALU_BODIES = [
    ("nop", [0x4E71]), ("mov_rr", [0x3200]), ("mov_ramR", [0x3210]),
    ("mov_ramW", [0x3081]), ("mov_ramL", [0x2210]), ("mov_b", [0x1211]),
    ("add_rr", [0xD240]), ("addq", [0x5241]), ("addi", [0x0641, 0x1234]),
    ("lsl4", [0xE949]), ("lsr_r", [0xE469]), ("tst", [0x4A41]),
    ("clr_ram", [0x4250]), ("cmp_imm", [0x0C41, 0x0001]),
]


def prog_alu_loops(a: Asm) -> None:
    prologue(a)
    for name, body in ALU_BODIES:
        a.w(0x7E03)                                      # moveq #3,d7
        a.label("L_" + name)
        a.w(*body)
        a.bra16(0x51CF, "L_" + name)                      # dbra d7,L
    sentinel(a)


def prog_mixed(a: Asm) -> None:
    """Instructions à chronologie interne non triviale : MOVEM, MUL/DIV, décalages,
    LINK/UNLK, JSR/BSR/RTS, PEA/LEA, EXG/SWAP, BCD, bits, Scc, MOVEP, MOVE SR/CCR."""
    prologue(a)
    a.w(0x2E7C).l(STACK)                                 # movea.l #$8000,sp
    a.w(0x2C7C).addr("sub")                              # movea.l #sub,a6
    a.w(0x48E7, 0xFFFE)                                  # movem.l d0-d7/a0-a6,-(sp)
    a.w(0x4CDF, 0x7FFF)                                  # movem.l (sp)+,d0-d7/a0-a6
    a.w(0x48D0, 0x0F0F)                                  # movem.l d0-d3/a0-a3,(a0)
    a.w(0x4C90, 0x0F0F)                                  # movem.w (a0),d0-d3/a0-a3
    a.w(0x207C).l(DATA)                                  # movea.l #$2000,a0
    a.w(0x227C).l(DATA + 0x100)                          # movea.l #$2100,a1
    a.w(0x323C, 0xFFFF)                                  # move.w #$FFFF,d1
    a.w(0x343C, 0x7FFF)                                  # move.w #$7FFF,d2
    a.w(0xC4C1)                                          # mulu d1,d2
    a.w(0x343C, 0x8001)                                  # move.w #$8001,d2
    a.w(0xC5C1)                                          # muls d1,d2
    a.w(0x243C).l(0x12345678)                            # move.l #$12345678,d2
    a.w(0x84FC, 0x0007)                                  # divu #7,d2
    a.w(0x243C).l(0x87654321)                            # move.l #$87654321,d2
    a.w(0x85FC, 0xFFF9)                                  # divs #-7,d2
    a.w(0x4842)                                          # swap d2
    a.w(0xC188).w(0xC188)                                # exg d0,a0 ×2
    a.w(0x45E8, 0x0008)                                  # lea 8(a0),a2
    a.w(0x4850)                                          # pea (a0)
    a.w(0x588F)                                          # addq.l #4,sp
    a.w(0x4E56, 0xFFF8)                                  # link a6,#-8
    a.w(0x4E5E)                                          # unlk a6
    a.bra16(0x6100, "sub")                               # bsr.w sub
    a.w(0x4EB9).addr("sub")                              # jsr sub.l
    a.w(0x4E96)                                          # jsr (a6)
    a.w(0x7001)                                          # moveq #1,d0
    a.bra8(0x6700, "nottaken")                           # beq.s (non pris)
    a.w(NOP)
    a.label("nottaken")
    a.bra8(0x6600, "taken")                              # bne.s (pris)
    a.w(ILLEGAL)
    a.label("taken")
    a.w(0x7603)                                          # moveq #3,d3
    a.w(0xE758)                                          # rol.w #3,d0
    a.w(0xE6B8)                                          # ror.l d3,d0
    a.w(0xE080)                                          # asr.l #8,d0
    a.w(0xE3A8)                                          # lsl.l d1,d0
    a.w(0xC300)                                          # abcd d0,d1
    a.w(0x8300)                                          # sbcd d0,d1
    a.w(0x4800)                                          # nbcd d0
    a.w(0x4440)                                          # neg.w d0
    a.w(0x4680)                                          # not.l d0
    a.w(0x4880).w(0x48C0)                                # ext.w d0 ; ext.l d0
    a.w(0x0800, 0x0003)                                  # btst #3,d0
    a.w(0x08D0, 0x0005)                                  # bset #5,(a0)
    a.w(0x0890, 0x0005)                                  # bclr #5,(a0)
    a.w(0x0350)                                          # bchg d1,(a0)
    a.w(0x54C0)                                          # scc d0
    a.w(0x40C0)                                          # move sr,d0
    a.w(0x44C1)                                          # move d1,ccr
    a.w(0x023C, 0x00FE)                                  # andi #$FE,ccr
    a.w(0x007C, 0x0700)                                  # ori #$0700,sr
    a.w(0xB141)                                          # eor.w d0,d1
    a.w(0x8241)                                          # or.w d1,d1
    a.w(0xC241)                                          # and.w d1,d1
    a.w(0x9280)                                          # sub.l d0,d1
    a.w(0xB281)                                          # cmp.l d1,d1
    a.w(0xB348)                                          # cmpm.w (a0)+,(a1)+
    a.w(0xD340)                                          # addx.w d0,d1
    a.w(0x9340)                                          # subx.w d0,d1
    a.w(0x4A98)                                          # tst.l (a0)+
    a.w(0x2220)                                          # move.l -(a0),d1
    a.w(0x7000)                                          # moveq #0,d0
    a.w(0x2230, 0x0000)                                  # move.l (a0,d0.w),d1
    a.w(0x3238, DATA)                                    # move.w $2000.w,d1
    a.w(0x2239).l(DATA)                                  # move.l $2000.l,d1
    a.w(0x123A, 0x0000)                                  # move.b 0(pc),d1
    a.w(0x7001)                                          # moveq #1,d0
    a.w(0x41BC, 0x0010)                                  # chk #16,d0 (pas de trap)
    a.w(0x0188, 0x0000)                                  # movep.w d0,0(a0)
    a.w(0x01C8, 0x0000)                                  # movep.l d0,0(a0)
    a.w(0x0308, 0x0000)                                  # movep.w 0(a0),d1
    a.w(0x0348, 0x0000)                                  # movep.l 0(a0),d1
    a.w(0x4EF9).addr("end")                              # jmp end.l
    a.label("sub").w(RTS)
    a.label("end")
    sentinel(a)


def prog_exceptions(a: Asm) -> None:
    """TRAP, TRAPV, CHK, division par zéro, ILLEGAL, ligne A/F, erreur d'adresse,
    violation de privilège (dernière : la suite tourne en mode utilisateur)."""
    prologue(a)
    a.w(0x2E7C).l(STACK)                                 # movea.l #$8000,sp
    a.w(0x4E40)                                          # trap #0
    a.w(0x44FC, 0x0002)                                  # move #2,ccr (V=1)
    a.w(0x4E76)                                          # trapv
    a.w(0x70FF)                                          # moveq #-1,d0
    a.w(0x41BC, 0x0010)                                  # chk #16,d0 → trap
    a.w(0x7000)                                          # moveq #0,d0
    a.w(0x82C0)                                          # divu d0,d1 → division par zéro
    a.w(ILLEGAL)                                         # illegal → h_skip2
    a.w(0xA000)                                          # ligne A → h_skip2
    a.w(0xF000)                                          # ligne F → h_skip2
    a.w(0x21FC).addr("cont").w(DATA)                     # move.l #cont,$2000.w
    a.w(0x3038, DATA + 1)                                # move.w $2001.w,d0 → erreur d'adresse
    a.w(NOP)
    a.label("cont")
    a.w(0x027C, 0xDFFF)                                  # andi #$DFFF,sr → mode utilisateur
    a.w(0x46FC, 0x2700)                                  # move #$2700,sr → privilège → h_skip2
    a.w(NOP)
    sentinel(a)


def prog_rmw_long(a: Asm) -> None:
    """Toutes les écritures longues en mémoire, sur six modes d'adressage : l'ordre des deux
    mots y est propre à chaque famille d'instructions."""
    prologue(a)
    a.w(0x2E7C).l(STACK)
    a.w(0x263C).l(0x01234567)                            # move.l #$01234567,d3
    # (mode, registre, mots d'extension) : (a0), (a0)+, -(a0), 4(a0), $2000.w, $2000.l
    modes = [(2, 0, ()), (3, 0, ()), (4, 0, ()), (5, 0, (4,)), (7, 0, (DATA,)), (7, 1, (0, DATA))]
    for m, r, ext in modes:
        ea = (m << 3) | r
        a.w(0x207C).l(DATA + 0x40)                       # movea.l #$2040,a0
        a.w(0x5080 | ea, *ext)                           # addq.l #8,<ea>
        a.w(0x5180 | ea, *ext)                           # subq.l #8,<ea>
        a.w(0x0680 | ea, 0x1234, 0x5678, *ext)           # addi.l
        a.w(0x0480 | ea, 0x1234, 0x5678, *ext)           # subi.l
        a.w(0x0080 | ea, 0x1234, 0x5678, *ext)           # ori.l
        a.w(0x0280 | ea, 0xFFFF, 0xFFFF, *ext)           # andi.l
        a.w(0x0A80 | ea, 0x1234, 0x5678, *ext)           # eori.l
        a.w(0x4480 | ea, *ext)                           # neg.l
        a.w(0x4080 | ea, *ext)                           # negx.l
        a.w(0x4680 | ea, *ext)                           # not.l
        a.w(0x4280 | ea, *ext)                           # clr.l
        a.w(0xD780 | ea, *ext)                           # add.l d3,<ea>
        a.w(0x9780 | ea, *ext)                           # sub.l d3,<ea>
        a.w(0xC780 | ea, *ext)                           # and.l d3,<ea>
        a.w(0x8780 | ea, *ext)                           # or.l d3,<ea>
        a.w(0xB780 | ea, *ext)                           # eor.l d3,<ea>
        a.w(0x2003 | (ea & 0x38) << 3 | (ea & 7) << 9, *ext)  # move.l d3,<ea> (champs inversés)
    a.w(0x207C).l(DATA + 0x80)
    a.w(0x267C).l(DATA + 0xC0)                           # movea.l #$20C0,a3
    a.w(0xD788)                                          # addx.l -(a0),-(a3)
    a.w(0x9788)                                          # subx.l -(a0),-(a3)
    sentinel(a)


def prog_trace(a: Asm) -> None:
    prologue(a)
    a.w(0x2E7C).l(STACK)                                 # movea.l #$8000,sp
    a.w(0x007C, 0x8000)                                  # ori #$8000,sr (T=1)
    a.w(NOP)
    a.w(0x3210)                                          # move.w (a0),d1
    a.w(0xC4C1)                                          # mulu d1,d2
    a.w(0x027C, 0x7FFF)                                  # andi #$7FFF,sr (T=0)
    sentinel(a)


def prog_tas(a: Asm) -> None:
    prologue(a)
    a.w(0x4AD0)                                          # tas (a0)
    a.w(0x4AC0)                                          # tas d0
    a.w(0x4AF8, DATA + 2)                                # tas $2002.w
    sentinel(a)


def prog_irq_nop(a: Asm) -> None:
    """IRQ pendant une suite de NOP : latence de prise en compte, séquence d'exception."""
    prologue(a)
    a.w(0x2E7C).l(STACK)                                 # movea.l #$8000,sp
    a.w(0x46FC, 0x2300)                                  # move #$2300,sr (masque 3)
    for _ in range(48):
        a.w(NOP)
    sentinel(a)


def prog_irq_mulu(a: Asm) -> None:
    """IRQ au milieu d'instructions longues (MULU 70 cycles)."""
    prologue(a)
    a.w(0x2E7C).l(STACK)
    a.w(0x46FC, 0x2300)
    a.w(0x323C, 0xFFFF)                                  # move.w #$FFFF,d1
    for _ in range(6):
        a.w(0xC4C1)                                      # mulu d1,d2
    sentinel(a)


def prog_stop_irq(a: Asm) -> None:
    """STOP puis IRQ : réveil et prise d'IPL (cf. STOP+8, CHANGELOG 2026-10-22)."""
    prologue(a)
    a.w(0x2E7C).l(STACK)
    a.w(0x4E72, 0x2300)                                  # stop #$2300
    a.w(NOP).w(NOP)
    sentinel(a)


def prog_stop_pending(a: Asm) -> None:
    """IRQ déjà pendante et masquée, démasquée par le STOP : part-elle à STOP+8 ?"""
    prologue(a)
    a.w(0x2E7C).l(STACK)
    for _ in range(8):
        a.w(NOP)                                         # la broche monte ici (masquée)
    a.w(0x4E72, 0x2300)                                  # stop #$2300 → démasque
    a.w(NOP).w(NOP)
    sentinel(a)


# --- Couverture systématique des modes d'adressage (2026-11-04) --------------------------
# (mode, registre, mots d'extension) ; a0 pointe $2040, d2 = 2. [B] M68000UM § 2 pour les modes.
EA_RW = [(2, 0, ()), (3, 0, ()), (4, 0, ()), (5, 0, (4,)), (6, 0, (0x2002,)), (7, 0, (DATA,)), (7, 1, (0, DATA))]
EA_PC = [(7, 2, (2,)), (7, 3, (0x2002,))]


def reload_a0(a: Asm) -> None:
    a.w(0x207C).l(DATA + 0x40)                           # movea.l #$2040,a0


def prelude_ea(a: Asm) -> None:
    prologue(a)
    a.w(0x2E7C).l(STACK)
    a.w(0x263C).l(0x01234567)                            # move.l #$01234567,d3


def prog_ea_move_w(a: Asm) -> None:
    """MOVE.W, toutes les sources vers toutes les destinations."""
    prelude_ea(a)
    for sm, sr, sx in EA_RW + EA_PC + [(7, 4, (0x1234,)), (0, 3, ())]:
        for dm, dr, dx in EA_RW:
            reload_a0(a)
            a.w(0x3000 | (dr << 9) | (dm << 6) | (sm << 3) | sr, *sx, *dx)
    sentinel(a)


def prog_ea_move_l(a: Asm) -> None:
    """MOVE.L, toutes les sources vers toutes les destinations."""
    prelude_ea(a)
    for sm, sr, sx in EA_RW + EA_PC + [(7, 4, (0x1234, 0x5678)), (0, 3, ())]:
        for dm, dr, dx in EA_RW:
            reload_a0(a)
            a.w(0x2000 | (dr << 9) | (dm << 6) | (sm << 3) | sr, *sx, *dx)
    sentinel(a)


def prog_ea_alu(a: Asm) -> None:
    """ADD, SUB, CMP, AND, OR (.W, .L) vers Dn, et ADDA, SUBA, CMPA, sur toutes les sources."""
    prelude_ea(a)
    for base in (0xD000, 0x9000, 0xB000, 0xC000, 0x8000):
        for size in (1, 2):
            for sm, sr, sx in EA_RW + EA_PC:
                reload_a0(a)
                a.w(base | (4 << 9) | (size << 6) | (sm << 3) | sr, *sx)
    for base in (0xD000, 0x9000, 0xB000):
        for opm in (3, 7):
            for sm, sr, sx in EA_RW:
                reload_a0(a)
                a.w(base | (2 << 9) | (opm << 6) | (sm << 3) | sr, *sx)
    sentinel(a)


def prog_ea_alu_mem(a: Asm) -> None:
    """ADD, SUB, AND, OR, EOR (.W, .L) de Dn vers la mémoire ; quick et immédiats .W sur la mémoire."""
    prelude_ea(a)
    for base in (0xD000, 0x9000, 0xC000, 0x8000, 0xB000):
        for opm in (5, 6):
            for m, r, x in EA_RW:
                reload_a0(a)
                a.w(base | (3 << 9) | (opm << 6) | (m << 3) | r, *x)
    for m, r, x in EA_RW:
        for op in (0x5240, 0x5340):                      # addq.w, subq.w
            reload_a0(a)
            a.w(op | (m << 3) | r, *x)
        for op in (0x0640, 0x0440, 0x0240, 0x0040, 0x0A40, 0x0C40):    # addi, subi, andi, ori, eori, cmpi .w
            reload_a0(a)
            a.w(op | (m << 3) | r, 0x1234, *x)
    sentinel(a)


def prog_ea_unary(a: Asm) -> None:
    """CLR, NEG, NEGX, NOT, TST, Scc, décalages mémoire, bits, LEA, PEA, JMP, JSR, TAS."""
    prelude_ea(a)
    for m, r, x in EA_RW:
        for op in (0x4240, 0x4280, 0x4440, 0x4480, 0x4040, 0x4640, 0x4A40, 0x4A80, 0x4800, 0x50C0, 0x51C0, 0x4AC0):
            reload_a0(a)
            a.w(op | (m << 3) | r, *x)
    for m, r, x in EA_RW:
        for op in (0xE0C0, 0xE1C0, 0xE2C0, 0xE3C0, 0xE4C0, 0xE5C0, 0xE6C0, 0xE7C0):    # décalages et rotations mémoire
            reload_a0(a)
            a.w(op | (m << 3) | r, *x)
    for m, r, x in EA_RW:
        for op in (0x0100, 0x0140, 0x0180, 0x01C0):      # btst, bchg, bclr, bset Dn,<ea>
            reload_a0(a)
            a.w(op | (2 << 9) | (m << 3) | r, *x)
        for op in (0x0800, 0x0840, 0x0880, 0x08C0):      # btst, bchg, bclr, bset #n,<ea>
            reload_a0(a)
            a.w(op | (m << 3) | r, 3, *x)
    for m, r, x in [e for e in EA_RW if e[0] not in (3, 4)] + EA_PC:
        reload_a0(a)
        a.w(0x43C0 | (m << 3) | r, *x)                   # lea <ea>,a1
        a.w(0x4840 | (m << 3) | r, *x)                   # pea <ea>
        a.w(0x588F)                                      # addq.l #4,sp
    sentinel(a)


def prog_flow(a: Asm) -> None:
    """Sauts : JMP et JSR sur tous les modes de contrôle, Bcc, BSR, DBcc, RTS, RTR, RTE, LINK, UNLK."""
    prelude_ea(a)
    a.w(0x47F9).addr("fsub")                             # lea fsub.l,a3
    a.w(0x4E93)                                          # jsr (a3)
    a.w(0x4EAB, 0)                                       # jsr 0(a3)
    a.w(0x4EB3, 0x2000)                                  # jsr 0(a3,d2.w) : d2 = 2, cf. fsub - 2
    a.w(0x4EB8, 0x1E00)                                  # jsr $1E00.w
    a.w(0x4EB9).l(0x1E00)                                # jsr $1E00.l
    a.w(0x4EBA, 0x0002).bra8(0x6000, "n1")               # jsr 2(pc) ; bra.s
    a.w(0x4E75)                                          # rts (cible du jsr pc)
    a.label("n1")
    a.w(0x44FC, 0x0000)                                  # move #0,ccr
    for cond in range(16):                               # Bcc.s, pris ou non selon CCR = 0
        a.bra8(0x6000 | (cond << 8) if cond else 0x6000, f"bs{cond}")
        a.w(NOP)
        a.label(f"bs{cond}")
    for cond in range(2, 16):                            # Bcc.w
        a.bra16(0x6000 | (cond << 8), f"bw{cond}")
        a.w(NOP)
        a.label(f"bw{cond}")
    a.bra8(0x6100, "bsrs").bra8(0x6000, "n2")            # bsr.s ; bra.s
    a.label("bsrs").w(0x4E75)
    a.label("n2")
    a.bra16(0x6100, "bsrw").bra8(0x6000, "n3")           # bsr.w ; bra.s
    a.label("bsrw").w(0x4E75)
    a.label("n3")
    for cond in (0, 1, 7, 6):                            # DBT, DBF, DBEQ, DBNE
        a.w(0x7E02)                                      # moveq #2,d7
        a.label(f"db{cond}")
        a.bra16(0x50C8 | (cond << 8) | 7, f"db{cond}")
    a.w(0x4E56, 0xFFF8)                                  # link a6,#-8
    a.w(0x4E5E)                                          # unlk a6
    a.w(0x4879).addr("rr").w(0x3F3C, 0x0000)             # pea rr.l ; move.w #0,-(sp)
    a.w(0x4E77)                                          # rtr
    a.label("rr")
    a.w(0x4879).addr("re").w(0x3F3C, 0x2700)             # pea re.l ; move.w #$2700,-(sp)
    a.w(RTE)
    a.label("re")
    sentinel(a)
    a.label("fsub").w(0x4E75, 0x4E75)                    # rts ; rts (cible avec index d2 = 2)


def prog_movem(a: Asm) -> None:
    """MOVEM .W et .L, dans les deux sens, tous les modes, liste courte et longue."""
    prelude_ea(a)
    masks = (0x0001, 0x0007, 0x00FF, 0x7F7F)
    for mask in masks:
        for size in (0, 1):
            for m, r, x in [(2, 0, ()), (3, 0, ()), (5, 0, (4,)), (6, 0, (0x2002,)), (7, 0, (DATA,)), (7, 1, (0, DATA))] + EA_PC:
                reload_a0(a)
                a.w(0x4C80 | (size << 6) | (m << 3) | r, mask, *x)        # movem <ea>,regs
            for m, r, x in [(2, 0, ()), (5, 0, (4,)), (6, 0, (0x2002,)), (7, 0, (DATA,)), (7, 1, (0, DATA))]:
                reload_a0(a)
                a.w(0x4880 | (size << 6) | (m << 3) | r, mask, *x)        # movem regs,<ea>
            reload_a0(a)
            a.w(0x4880 | (size << 6) | (4 << 3), mask)                    # movem regs,-(a0)
    sentinel(a)


def prog_misc_extra(a: Asm) -> None:
    """RESET, MOVE USP, EXG, EXT, SWAP, ADDX, SUBX, ABCD, SBCD, CMPM, MOVEP, CHK et DIVS à effet de bord."""
    prelude_ea(a)
    a.w(0x4E70)                                          # reset
    a.w(0x4E63).w(0x4E6B)                                # move a3,usp ; move usp,a3
    a.w(0xC143, 0xC34B, 0xC54B)                          # exg d0,d3 ; exg a1,a3 ; exg d2,a3
    a.w(0x4883, 0x48C3, 0x4843)                          # ext.w, ext.l, swap d3
    for op in (0xD148, 0xD548, 0xD588, 0x9148, 0x9548, 0x9588, 0xC108, 0x8108, 0xB148, 0xB588):
        # addx.b/.w/.l -(a0),-(a0) ; subx.b/.w/.l ; abcd, sbcd -(a0) ; cmpm.w/.l (a0)+,(a0)+
        reload_a0(a)
        a.w(op)
    for op, ext in ((0x0108, 4), (0x0148, 4), (0x0188, 4), (0x01C8, 4)):   # movep d0,4(a0) et inverses
        reload_a0(a)
        a.w(op, ext)
    # CHK dans la plage et hors de la plage, sur la mémoire aussi : le handler repart.
    a.w(0x7605)                                          # moveq #5,d3
    a.w(0x47BC, 0x0010)                                  # chk #16,d3
    reload_a0(a)
    a.w(0x4790)                                          # chk (a0),d3
    a.w(0x76FF)                                          # moveq #-1,d3
    a.w(0x47BC, 0x0010)                                  # chk #16,d3 → trap
    a.w(0x263C).l(0x7FFFFFFF)                            # move.l #$7FFFFFFF,d3
    a.w(0x87FC, 0x0001)                                  # divs #1,d3 : débordement
    a.w(0x7600)                                          # moveq #0,d3
    a.w(0x87C3)                                          # divs d3,d3 → division par zéro
    a.w(0x44FC, 0x0002).w(0x4E76)                        # move #2,ccr ; trapv pris
    a.w(0x027C, 0xDFFF)                                  # andi #$DFFF,sr → mode utilisateur
    a.w(0xA000)                                          # ligne A en utilisateur
    a.w(0xF000)                                          # ligne F en utilisateur
    a.w(0x4E40)                                          # trap #0 en utilisateur
    a.w(0x4E70)                                          # reset en utilisateur → privilège
    a.w(NOP)
    sentinel(a)


# (nom, générateur, options) — options : irq = [(T, L)], sweep = (nb, pas) pour répéter
# avec T + k·pas, k ∈ [0, nb). T compté depuis le premier accès après le reset. reset = True : les
# intervalles de la séquence de reset comptent (sinon ils sont ignorés, cf. le comparateur).
BERR = 0xE00000                           # fenêtre BERR des deux pilotes ($E00000-$EFFFFF)


def regs_dump(a: Asm) -> None:
    """D0-D7/A0-A6 vers $2100 : l'état laissé par les fautes devient une suite d'écritures comparées."""
    a.w(0x48F8, 0x7FFF, DATA + 0x100)                    # movem.l d0-d7/a0-a6,$2100.w


def prog_berr(a: Asm) -> None:
    """Bus error sur chaque forme d'accès : lecture et écriture, octet, mot, long, RMW (CLR,
    ADD, TAS), MOVEM, mode prédécrémenté et postincrémenté, préfetch (JMP). Chaque faute
    passe par h_group0, qui reprend à l'adresse posée en $2000 ; la trame empilée (mot
    d'état, adresse, IR, SR, PC) est comparée comme toute écriture. [B] M68000UM § 6.3.9."""
    prologue(a)
    a.w(0x2E7C).l(STACK)                                 # movea.l #$8000,sp
    a.w(0x203C).l(0x5A5AA5A5)                            # move.l #$5A5AA5A5,d0
    cases = [
        (0x3039, BERR),            # move.w $E00000.l,d0
        (0x33C0, BERR),            # move.w d0,$E00000.l
        (0x1039, BERR + 1),        # move.b $E00001.l,d0
        (0x13C0, BERR + 1),        # move.b d0,$E00001.l
        (0x2039, BERR),            # move.l $E00000.l,d0
        (0x23C0, BERR),            # move.l d0,$E00000.l
        (0x4279, BERR),            # clr.w $E00000.l (lit d'abord sur 68000)
        (0x4AF9, BERR),            # tas $E00000.l
        (0x0679, BERR, 0x1234),    # addi.w #$1234,$E00000.l
    ]
    for n, (op, addr, *imm) in enumerate(cases):
        a.w(0x21FC).addr(f"berr{n}").w(DATA)             # move.l #berrN,$2000.w
        a.w(op, *imm).l(addr)
        a.w(NOP)
        a.label(f"berr{n}")
    # Modes indirects : l'adresse de A2 après la faute dit si l'incrément a eu lieu.
    for n, op in enumerate((0x301A, 0x3500, 0xD152, 0x2522)):  # (a2)+ ; -(a2) ; add.w d0,(a2) ; move.l -(a2),-(a2)
        a.w(0x247C).l(BERR + 0x100)                      # movea.l #$E00100,a2
        a.w(0x21FC).addr(f"berri{n}").w(DATA)
        a.w(op)
        a.w(NOP)
        a.label(f"berri{n}")
        a.w(0x21CA, DATA + 0x180 + 4 * n)                # move.l a2,$2180+4n.w : A2 laissé par la faute
    a.w(0x21FC).addr("berrm").w(DATA)
    a.w(0x48F9, 0x0003).l(BERR)                          # movem.l d0-d1,$E00000.l
    a.w(NOP)
    a.label("berrm")
    a.w(0x21FC).addr("berrm2").w(DATA)
    a.w(0x4CF9, 0x0300).l(BERR)                          # movem.l $E00000.l,a0-a1
    a.w(NOP)
    a.label("berrm2")
    a.w(0x21FC).addr("berrj").w(DATA)
    a.w(0x4EF9).l(BERR + 0x200)                          # jmp $E00200.l → faute au préfetch
    a.w(NOP)
    a.label("berrj")
    regs_dump(a)
    sentinel(a)


def prog_double_fault(a: Asm) -> None:
    """Bus error dont la trame tombe elle-même dans la fenêtre BERR : double faute, HALT.
    [B] M68000UM § 6.3.9 : une bus error pendant le traitement d'une erreur de bus ou
    d'adresse arrête le processeur. Pas de sentinelle : la trace finit sur halt."""
    prologue(a)
    a.w(0x2E7C).l(BERR + 0x100)                          # movea.l #$E00100,sp
    a.w(0x3039).l(BERR)                                  # move.w $E00000.l,d0
    sentinel(a)


def prog_double_fault_trap(a: Asm) -> None:
    """TRAP dont l'empilement faute (bus error, groupe 0), puis la trame de la bus error
    faute à son tour : double faute, HALT."""
    prologue(a)
    a.w(0x2E7C).l(BERR + 0x100)                          # movea.l #$E00100,sp
    a.w(0x4E40)                                          # trap #0
    sentinel(a)


def prog_double_fault_addr(a: Asm) -> None:
    """Pile impaire : l'empilement du TRAP lève une erreur d'adresse, dont la trame tombe
    à la même adresse impaire : double faute, HALT."""
    prologue(a)
    a.w(0x2E7C).l(STACK - 1)                             # movea.l #$7FFF,sp
    a.w(0x4E40)                                          # trap #0
    sentinel(a)


def prog_double_fault_vec(a: Asm) -> None:
    """Vecteur de bus error pointé dans la fenêtre : la trame s'empile, puis le préfetch du
    gestionnaire faute. Faute pendant le traitement d'une erreur de bus : HALT attendu."""
    prologue(a)
    a.w(0x2E7C).l(STACK)
    a.w(0x21FC).l(BERR + 0x400).w(0x0008)                # move.l #$E00400,$0008.w
    a.w(0x3039).l(BERR)                                  # move.w $E00000.l,d0
    sentinel(a)


PROGRAMS = [
    ("reset_nop", prog_reset_nop, {"reset": True}),     # seul à dater la séquence de reset
    ("alu_loops", prog_alu_loops, {}),
    ("mixed", prog_mixed, {}),
    ("exceptions", prog_exceptions, {}),
    ("rmw_long", prog_rmw_long, {}),
    ("ea_move_w", prog_ea_move_w, {}),
    ("ea_move_l", prog_ea_move_l, {}),
    ("ea_alu", prog_ea_alu, {}),
    ("ea_alu_mem", prog_ea_alu_mem, {}),
    ("ea_unary", prog_ea_unary, {}),
    ("flow", prog_flow, {}),
    ("movem", prog_movem, {}),
    ("misc_extra", prog_misc_extra, {}),
    ("trace", prog_trace, {}),
    ("tas", prog_tas, {}),
    ("irq_nop", prog_irq_nop, {"irq": [(100, 5)], "sweep": (8, 1)}),
    ("irq_mulu", prog_irq_mulu, {"irq": [(120, 5)], "sweep": (8, 1)}),
    ("stop_irq", prog_stop_irq, {"irq": [(140, 5)], "sweep": (8, 1)}),
    ("stop_pending", prog_stop_pending, {"irq": [(70, 5)], "sweep": (4, 1)}),
    ("berr", prog_berr, {}),
    ("double_fault", prog_double_fault, {}),
    ("double_fault_trap", prog_double_fault_trap, {}),
    ("double_fault_addr", prog_double_fault_addr, {}),
    ("double_fault_vec", prog_double_fault_vec, {}),
]


def build(name: str) -> bytes:
    gen = {n: g for n, g, _ in PROGRAMS}[name]
    code = Asm(CODE)
    handlers(code)
    gen(code)
    code_bytes = code.resolve()
    vec = vectors(HANDLERS, "h_rte")
    # Les vecteurs IRQ utilisateur ($40-$47) et trace (9) partent sur h_rte ; les labels
    # du code sont partagés avec la table.
    vec.labels.update(code.labels)
    vec_bytes = vec.resolve()
    image = bytearray(STACK)
    image[0:len(vec_bytes)] = vec_bytes
    image[CODE:CODE + len(code_bytes)] = code_bytes
    # Cible de JSR abs de prog_flow : un RTS à une adresse fixe.
    image[0x1E00:0x1E02] = bytes.fromhex("4E75")
    assert CODE + len(code_bytes) < DATA, ("code déborde sur les données", name)
    # Données : motif non nul, pour que les lectures racontent quelque chose.
    for i in range(DATA, DATA + 0x200):
        image[i] = (i * 7 + 3) & 0xFF
    return bytes(image)


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build-rtl/fx68k/programs")
    out.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, _gen, opts in PROGRAMS:
        data = build(name)
        (out / f"{name}.bin").write_bytes(data)
        manifest.append({"name": name, "bytes": len(data), **opts})
        print(f"  {name:14} {len(data)} o")
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
