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
#  tools/make_cycle_bench.py. [B] Encodages : M68000UM Rev 8 § 8 (Instruction Format
#  Summary) ; un encodage faux se voit dans la trace (vecteur 4 lu par les deux cœurs).
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
    # Groupe 0 (bus/adresse) : trame de 7 mots ; [B] M68000UM § 6.2.5 fig. 6-6.
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


# (nom, générateur, options) — options : irq = [(T, L)], sweep = (nb, pas) pour répéter
# avec T + k·pas, k ∈ [0, nb). T compté depuis le premier accès bus. reset = True : les
# intervalles de la séquence de reset comptent (sinon ils sont ignorés, cf. le comparateur).
PROGRAMS = [
    ("reset_nop", prog_reset_nop, {"reset": True}),     # seul à dater la séquence de reset
    ("alu_loops", prog_alu_loops, {}),
    ("mixed", prog_mixed, {}),
    ("exceptions", prog_exceptions, {}),
    ("trace", prog_trace, {}),
    ("tas", prog_tas, {}),
    ("irq_nop", prog_irq_nop, {"irq": [(120, 5)], "sweep": (8, 1)}),
    ("irq_mulu", prog_irq_mulu, {"irq": [(140, 5)], "sweep": (8, 1)}),
    ("stop_irq", prog_stop_irq, {"irq": [(160, 5)], "sweep": (8, 1)}),
    ("stop_pending", prog_stop_pending, {"irq": [(100, 5)], "sweep": (4, 1)}),
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
