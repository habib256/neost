#!/usr/bin/env python3
# =============================================================================
#  make_fpu_testrom.py — mini-ROM de validation du MC68881 périphérique
#  ($FFFA40, socket Mega STE). Dialogue CIR « façon SFP004 » : écrire le mot
#  de commande dans $FFFA4A, scruter $FFFA40 tant qu'il vaut $8900, transférer
#  les opérandes par $FFFA50. Cf. src/io/Fpu.{hpp,cpp}.
#
#  En-tête : SSP = $00010200 → le mot à l'offset 2 ($0200) sert de « version
#  TOS » 2.00, sinon adjustMachineForTos rétrograde MegaSTE→ST. PC = $E00008.
#
#  Verdict dans la trace headless (--trace --regs) : boucle finale sur
#    PASS : PC stable avec D7 = $0000002A (moveq #42)
#    FAIL : PC stable avec D7 = $FFFFFFxx (moveq #-(numéro du test))
#
#  Usage : python3 tools/make_fpu_testrom.py [sortie.img]
#          ./build/neost-headless sortie.img --machine megaste --fpu \
#              --frames 30 --trace /tmp/fpu.txt --regs
# =============================================================================
import struct
import sys

ROM_BASE = 0xE00000
ROM_SIZE = 256 * 1024

CMD, RESP, OPER = 0xFFFA4A, 0xFFFA40, 0xFFFA50

code = bytearray()
fixups = []   # (offset du mot de déplacement, label, base PC du déplacement)
labels = {}


def w16(v): code.extend(struct.pack(">H", v & 0xFFFF))
def w32(v): code.extend(struct.pack(">I", v & 0xFFFFFFFF))


def movew_imm_absl(imm, addr):          # move.w #imm,addr.l
    w16(0x33FC); w16(imm); w32(addr)


def movel_imm_absl(imm, addr):          # move.l #imm,addr.l
    w16(0x23FC); w32(imm); w32(addr)


def poll():                             # cmpi.w #$8900,RESP.l ; beq.s -10
    w16(0x0C79); w16(0x8900); w32(RESP)
    w16(0x67F6)


def movel_absl_dn(addr, dn):            # move.l addr.l,Dn
    w16(0x2039 | (dn << 9)); w32(addr)


def movew_absl_dn(addr, dn):            # move.w addr.l,Dn
    w16(0x3039 | (dn << 9)); w32(addr)


def cmpil_dn(imm, dn):                  # cmpi.l #imm,Dn
    w16(0x0C80 | dn); w32(imm)


def cmpiw_dn(imm, dn):                  # cmpi.w #imm,Dn
    w16(0x0C40 | dn); w16(imm)


def bne_to(label):                      # bne.w label (fixup différé)
    w16(0x6600)
    fixups.append((len(code), label, len(code)))
    w16(0)


def command(cmd):                       # écrire le Command CIR puis attendre
    movew_imm_absl(cmd, CMD)
    poll()


# ---- programme ---------------------------------------------------------------
# Test 1 : FADD.S — 2.5 + 1.25 = 3.75 ($40700000)
fail_n = 1
command(0x4400)                          # FMOVE.S <ea> → FP0
movel_imm_absl(0x40200000, OPER)         # 2.5f
command(0x4422)                          # FADD.S
movel_imm_absl(0x3FA00000, OPER)         # 1.25f
command(0x6400)                          # FMOVE.S FP0 → mem
movel_absl_dn(OPER, 0)
cmpil_dn(0x40700000, 0)                  # 3.75f
bne_to("fail1")

# Test 2 : FDIV.D — 1.0 / 4.0 = 0.25 ($3FD00000_00000000)
command(0x5400)                          # FMOVE.D <ea> → FP0
movel_imm_absl(0x3FF00000, OPER); movel_imm_absl(0, OPER)        # 1.0
command(0x5420)                          # FDIV.D
movel_imm_absl(0x40100000, OPER); movel_imm_absl(0, OPER)        # 4.0
command(0x7400)                          # FMOVE.D FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x3FD00000, 0); bne_to("fail2")
movel_absl_dn(OPER, 1); cmpil_dn(0x00000000, 1); bne_to("fail2")

# Test 3 : FMOVECR pi → FMOVE.D = $400921FB_54442D18
command(0x5C00)                          # FMOVECR #0 (pi) → FP0
command(0x7400)
movel_absl_dn(OPER, 0); cmpil_dn(0x400921FB, 0); bne_to("fail3")
movel_absl_dn(OPER, 1); cmpil_dn(0x54442D18, 1); bne_to("fail3")

# Test 4 : FSQRT.D — sqrt(2.0) = $3FF6A09E_667F3BCD
command(0x5400)
movel_imm_absl(0x40000000, OPER); movel_imm_absl(0, OPER)        # 2.0
command(0x5404)                          # FSQRT.D (source FP0 rechargée ? non :
movel_imm_absl(0x40000000, OPER); movel_imm_absl(0, OPER)        # <ea> source)
command(0x7400)
movel_absl_dn(OPER, 0); cmpil_dn(0x3FF6A09E, 0); bne_to("fail4")
movel_absl_dn(OPER, 1); cmpil_dn(0x667F3BCD, 1); bne_to("fail4")

# Test 5 : FINTRZ.D + FMOVE.L — trunc(-3.75) = -3
command(0x5403)                          # FINTRZ.D <ea> → FP0
movel_imm_absl(0xC00E0000, OPER); movel_imm_absl(0, OPER)        # -3.75
command(0x6000)                          # FMOVE.L FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0xFFFFFFFD, 0); bne_to("fail5")

# Test 6 : FCMP.S + Condition CIR — 2.5 == 2.5 → prédicat EQ vrai (TF=1)
command(0x4400)                          # FMOVE.S <ea> → FP0
movel_imm_absl(0x40200000, OPER)         # 2.5f
command(0x4438)                          # FCMP.S <ea>,FP0
movel_imm_absl(0x40200000, OPER)         # 2.5f
movew_imm_absl(0x0001, 0xFFFA4E)         # Condition CIR : prédicat EQ
movew_absl_dn(RESP, 0)
cmpiw_dn(0x0803, 0)                      # null : PF=1, TF=1
bne_to("fail6")

# Test 7 : FMOVEM FPCR aller-retour — écrire $10 (mode RZ), relire, remettre 0
command(0x9000)                          # FMOVEM <ea> → FPCR
movel_imm_absl(0x00000010, OPER)
command(0xB000)                          # FMOVEM FPCR → <ea>
movel_absl_dn(OPER, 0); cmpil_dn(0x00000010, 0); bne_to("fail7")
command(0x9000)                          # FPCR ← 0 (ne pas polluer la suite)
movel_imm_absl(0x00000000, OPER)

# PASS : D7 = 42, boucle stable.
w16(0x7E2A)                              # moveq #42,d7
w16(0x60FE)                              # bra.s *

for n in range(1, 8):                    # FAILn : D7 = -n, boucle stable
    labels[f"fail{n}"] = len(code)
    w16(0x7E00 | ((-n) & 0xFF))          # moveq #-n,d7
    w16(0x60FE)                          # bra.s *

for off, label, base in fixups:          # résoudre les bne.w
    disp = labels[label] - base
    code[off:off + 2] = struct.pack(">h", disp)

# ---- image ROM ----------------------------------------------------------------
rom = bytearray(b"\xFF" * ROM_SIZE)
rom[0:4] = struct.pack(">I", 0x00010200)         # SSP (+ « version TOS 2.00 »)
rom[4:8] = struct.pack(">I", ROM_BASE + 8)       # PC initial
rom[8:8 + len(code)] = code

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fpu_testrom.img"
with open(out, "wb") as f:
    f.write(rom)
print(f"{out} : {len(code)} octets de code, ROM {ROM_SIZE // 1024} Ko @ ${ROM_BASE:06X}")
