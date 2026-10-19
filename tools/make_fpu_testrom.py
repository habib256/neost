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


# --- branches/adresses par LABEL (verdict série) ------------------------------
addr_fixups = []   # (offset de l'immédiat 32 bits, label) → adresse absolue ROM


def label(name):
    labels[name] = len(code)


def _branch(op, name):                  # Bcc.w name (disp résolu en 2ᵉ passe)
    w16(op); fixups.append((len(code), name, len(code))); w16(0)


def bra_to(name): _branch(0x6000, name)
def bsr_to(name): _branch(0x6100, name)
def beq_to(name): _branch(0x6700, name)


def movel_label_a3(name):               # move.l #(adresse absolue du label),a3
    w16(0x267C); addr_fixups.append((len(code), name)); w32(0)


def emit_string(name, text):            # chaîne 0-terminée, alignée mot
    label(name)
    b = text.encode("ascii") + b"\x00"
    if len(b) % 2:
        b += b"\x00"
    code.extend(b)


def bne_to(label):                      # bne.w label (fixup différé)
    w16(0x6600)
    fixups.append((len(code), label, len(code)))
    w16(0)


def command(cmd):                       # écrire le Command CIR puis attendre
    movew_imm_absl(cmd, CMD)
    poll()


def fpcr(value):
    command(0x9000)
    movel_imm_absl(value, OPER)


def operand_x(se, man):                 # opérande étendu, mot réservé à zéro
    movel_imm_absl(se << 16, OPER)
    movel_imm_absl(man >> 32, OPER)
    movel_imm_absl(man & 0xFFFFFFFF, OPER)


def expect_x(n, se, man):
    command(0x6800)                     # FMOVE.X FP0 → mémoire
    for dn, value in enumerate((se << 16, man >> 32, man & 0xFFFFFFFF)):
        movel_absl_dn(OPER, dn)
        cmpil_dn(value, dn)
        bne_to(f"fail{n}")


def expect_fpsr(n, mask, value):
    command(0xA800)
    movel_absl_dn(OPER, 0)
    w16(0x0280); w32(mask)               # andi.l #mask,d0
    cmpil_dn(value, 0)
    bne_to(f"fail{n}")


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

# Test 8 : FDIV.X 1.0/3.0 relu en FMOVE.X — prouve la MANTISSE 64 BITS réelle.
# Étendu exact = $3FFD AAAAAAAA_AAAAAAAB (arrondi au plus près) ; un calcul en
# `double` (53 bits) donnerait $3FFD AAAAAAAA_AAAAA800 → l'octet final $AB tranche.
command(0x4800)                          # FMOVE.X <ea> → FP0
movel_imm_absl(0x3FFF0000, OPER); movel_imm_absl(0x80000000, OPER); movel_imm_absl(0x00000000, OPER)  # 1.0
command(0x4820)                          # FDIV.X
movel_imm_absl(0x40000000, OPER); movel_imm_absl(0xC0000000, OPER); movel_imm_absl(0x00000000, OPER)  # 3.0
command(0x6800)                          # FMOVE.X FP0 → mem (12 octets)
movel_absl_dn(OPER, 0); cmpil_dn(0x3FFD0000, 0); bne_to("fail8")   # signe/exposant
movel_absl_dn(OPER, 1); cmpil_dn(0xAAAAAAAA, 1); bne_to("fail8")   # mantisse 63..32
movel_absl_dn(OPER, 2); cmpil_dn(0xAAAAAAAB, 2); bne_to("fail8")   # mantisse 31..0 (64 bits !)

# Test 10 : DOUBLE ARRONDI de la conversion sortante — le cas qui tranche.
# FP0 = 0,5 + 2^-64, soit l'étendu IMMÉDIATEMENT SUPÉRIEUR à 0,5 :
#   $3FFE 80000000_00000001  (exposant -1, mantisse 1 + 2^-63)
# FMOVE.L au plus près doit rendre 1 : la valeur est STRICTEMENT au-dessus de 0,5,
# il n'y a pas d'égalité à départager. Mais un émulateur qui passe d'abord par un
# `double` (53 bits de mantisse) perd le bit 2^-64, retombe sur 0,5 EXACTEMENT,
# puis applique la règle du pair le plus proche → 0. C'est le double arrondi.
# Aucun des tests 1-9 ne le voyait : le test 8 sort en FMOVE.X, qui recopie la
# mantisse sans conversion, et les autres tiennent tous en 53 bits.
command(0x4800)                          # FMOVE.X <ea> → FP0
movel_imm_absl(0x3FFE0000, OPER); movel_imm_absl(0x80000000, OPER); movel_imm_absl(0x00000001, OPER)
command(0x6000)                          # FMOVE.L FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x00000001, 0); bne_to("fail10")

# Test 11 : INEX2 sur conversion entière inexacte. FMOVE.L de 1,5 rend 2 (pair le
# plus proche) ET DOIT armer INEX2 (FPSR bit 9) + le bit accumulé INEX (bit 3).
# Le 68881 lève INEX2 dès qu'une conversion perd de l'information ; du code qui
# teste l'exactitude d'un arrondi le lit.
command(0x8800)                          # FMOVEM <ea> → FPSR (efface les drapeaux)
movel_imm_absl(0x00000000, OPER)
command(0x4800)                          # FMOVE.X 1,5 → FP0  ($3FFF C0000000_00000000)
movel_imm_absl(0x3FFF0000, OPER); movel_imm_absl(0xC0000000, OPER); movel_imm_absl(0x00000000, OPER)
command(0x6000)                          # FMOVE.L FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x00000002, 0); bne_to("fail11")
command(0xA800)                          # FMOVEM FPSR → <ea>
movel_absl_dn(OPER, 0)
w16(0x0280); w32(0x00000208)             # andi.l #$208,d0   → INEX2 (bit9) | INEX (bit3)
cmpil_dn(0x00000208, 0); bne_to("fail11")

# Test 12 : le MODE D'ARRONDI DU FPCR s'applique à la conversion SORTANTE simple.
# FPCR ← $10 (RZ, vers zéro), FP0 ← 1/3 étendu ($3FFD AAAAAAAA_AAAAAAAB), puis
# FMOVE.S. En RZ la mantisse simple TRONQUE : $3EAAAAAA. Au plus près (le mode de
# l'HÔTE, qu'un `float(double)` applique quoi qu'en dise le FPCR) on obtiendrait
# $3EAAAAAB — un ulp au-dessus. C'est le test qui distingue les deux.
command(0x9000)                          # FMOVEM <ea> → FPCR
movel_imm_absl(0x00000010, OPER)         # RZ
command(0x4800)                          # FMOVE.X 1/3 → FP0
movel_imm_absl(0x3FFD0000, OPER); movel_imm_absl(0xAAAAAAAA, OPER); movel_imm_absl(0xAAAAAAAB, OPER)
command(0x6400)                          # FMOVE.S FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x3EAAAAAA, 0); bne_to("fail12")
command(0x9000)                          # FPCR ← 0 (ne pas polluer la suite)
movel_imm_absl(0x00000000, OPER)

# Test 13 : ±INFINI en DÉCIMAL EMPAQUETÉ. Le 68881 n'émet pas de BCD pour un infini
# ou un NaN : il recopie le motif étendu, dont les 12 bits bas du premier mot valent
# alors $FFF — l'exposant « spécial » du format P (≙ fp_from_pack, fpp_softfloat.c:702 :
# `if ((f.high & 0x7FFF) == 0x7FFF) { wrd[0] = f.high << 16; wrd[1..2] = f.low; }`).
# NeoST passait par snprintf("%+.*e") : sur un infini la libc rend « +inf », dont le
# parseur BCD tirait des chiffres arbitraires.
command(0x4800)                          # FMOVE.X +inf → FP0
movel_imm_absl(0x7FFF0000, OPER); movel_imm_absl(0x00000000, OPER); movel_imm_absl(0x00000000, OPER)
command(0x6C11)                          # FMOVE.P FP0 → mem, k statique = 17
movel_absl_dn(OPER, 0); cmpil_dn(0x7FFF0000, 0); bne_to("fail13")
movel_absl_dn(OPER, 1); cmpil_dn(0x00000000, 1); bne_to("fail13")
movel_absl_dn(OPER, 2); cmpil_dn(0x00000000, 2); bne_to("fail13")
# Même chemin pour un NaN, dont le PAYLOAD doit traverser intact. On prend un QNaN
# (bit 62 déjà posé) : un SNaN serait quiété par propagateFloatx80NaNOneArg côté
# oracle, ce qui testerait autre chose que le format P.
command(0x4800)                          # FMOVE.X QNaN → FP0
movel_imm_absl(0x7FFF0000, OPER); movel_imm_absl(0xC0000000, OPER); movel_imm_absl(0x0BADF00D, OPER)
command(0x6C11)                          # FMOVE.P FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x7FFF0000, 0); bne_to("fail13")
movel_absl_dn(OPER, 1); cmpil_dn(0xC0000000, 1); bne_to("fail13")
movel_absl_dn(OPER, 2); cmpil_dn(0x0BADF00D, 2); bne_to("fail13")   # payload intact

# Test 14 : k-factor > 17 → OPERR. Le format P ne porte que 17 chiffres significatifs ;
# au-delà, le 68881 arme OPERR (≙ floatx80_to_floatdecimal, softfloat_decimal.c:412 :
# `if (kfactor > 17) { kfactor = 17; float_raise(float_flag_invalid); }`). NeoST
# écrêtait à 17 EN SILENCE.
command(0x8800)                          # FPSR ← 0 (efface les drapeaux)
movel_imm_absl(0x00000000, OPER)
command(0x4800)                          # FMOVE.X 1.0 → FP0
movel_imm_absl(0x3FFF0000, OPER); movel_imm_absl(0x80000000, OPER); movel_imm_absl(0x00000000, OPER)
command(0x6C12)                          # FMOVE.P FP0 → mem, k statique = 18 (> 17)
command(0xA800)                          # FMOVEM FPSR → <ea>
movel_absl_dn(OPER, 0)
w16(0x0280); w32(0x00002080)             # andi.l #$2080,d0  → OPERR (bit13) | IOP (bit7)
cmpil_dn(0x00002080, 0); bne_to("fail14")

# Test 15 : FSGLMUL garde l'exposant ÉTENDU malgré sa mantisse 24 bits.
# 2^200 × 1,5 = 1,5 × 2^200, et non +inf comme avec roundAndPack(32).
command(0x4800); operand_x(0x40C7, 0x8000000000000000)
command(0x4827); operand_x(0x3FFF, 0xC000000000000000)
expect_fpsr(15, 0x0000FF00, 0)
expect_x(15, 0x40C7, 0xC000000000000000)

# Test 16 : même garde en bas de plage pour FSGLDIV ; 2^-200 / 3 est
# représentable en étendu et s'arrondit sur 24 bits, sans UNFL.
command(0x4800); operand_x(0x3F37, 0x8000000000000000)
command(0x4824); operand_x(0x4000, 0xC000000000000000)
expect_fpsr(16, 0x0000FF00, 0x00000200)  # INEX2 seulement
expect_x(16, 0x3F35, 0xAAAAAB0000000000)

# Test 17 : FSGLMUL tronque les ENTRÉES après normalisation, FSGLDIV garde
# leurs 64 bits et n'arrondit que le résultat (softfloat.c:2618 / 2762).
# 1 + 3*2^-25 : la multiplication par 1 tronque à 1 ; la division par 1
# doit arrondir à 1 + 2^-23. FPCR double ne change pas les 24 bits FSGLxxx.
fpcr(0x80)
command(0x4800); operand_x(0x3FFF, 0x800000C000000000)
command(0x4827); operand_x(0x3FFF, 0x8000000000000000)
expect_fpsr(17, 0x0000FF00, 0)
expect_x(17, 0x3FFF, 0x8000000000000000)
command(0x4800); operand_x(0x3FFF, 0x800000C000000000)
command(0x4824); operand_x(0x3FFF, 0x8000000000000000)
expect_fpsr(17, 0x0000FF00, 0x00000200)
expect_x(17, 0x3FFF, 0x8000010000000000)
fpcr(0)

# Test 18 : ne pas tronquer un NaN ni un dénormal AVANT les cas spéciaux.
# Le plus petit dénormal × 2^64 atteint exactement le minimum normal.
command(0x4800); operand_x(0, 1)
command(0x4827); operand_x(0x403F, 0x8000000000000000)
expect_x(18, 1, 0x8000000000000000)
command(0x4800); operand_x(0x7FFF, 0xC00000000BADF00D)
command(0x4827); operand_x(0x3FFF, 0x8000000000000000)
expect_x(18, 0x7FFF, 0xC00000000BADF00D)

# Test 19 : FMOD ré-arrondit le dividende même si |a| < |b|/2 (quotient 0).
# 1 + 3*2^-25 modulo 4 → 1 + 2^-23 en précision simple, avec INEX2.
command(0x4800); operand_x(0x3FFF, 0x800000C000000000)
fpcr(0x40)
command(0x8800); movel_imm_absl(0, OPER)
command(0x4821); operand_x(0x4001, 0x8000000000000000)
expect_fpsr(19, 0x00FFFFFF, 0x00000208)  # quotient nul ; INEX2 + AEXC_INEX
expect_x(19, 0x3FFF, 0x8000010000000000)
fpcr(0)

# Test 20 : FMOVECR arrondit pi à 24 bits APRÈS l'ajustement ROM RN/RZ/RM/RP.
# Relire en X expose les bits de registre ; une sortie S masquerait le défaut.
for mode, man in ((0x40, 0xC90FDB0000000000), (0x50, 0xC90FDA0000000000),
                  (0x60, 0xC90FDA0000000000), (0x70, 0xC90FDB0000000000)):
    fpcr(mode)
    command(0x5C00)
    expect_x(20, 0x4000, man)

# Test 21 : précision double (et motif FPCR réservé 11) → mantisse 53 bits.
for mode in (0x90, 0xD0):
    fpcr(mode)
    command(0x5C00)
    expect_x(21, 0x4000, 0xC90FDAA22168C000)

# Test 22 : FMOVECR garde aussi sa plage d'exposant ÉTENDUE en simple.
# 10^4096 ne doit pas déborder vers +inf à cause de la précision FPCR.
fpcr(0x40)
command(0x5C3F)
expect_fpsr(22, 0x0000FF00, 0x00000200)
expect_x(22, 0x7525, 0xC460520000000000)

# Test 23 : 10^16 est EXACT dans la table ROM mais perd des bits à 24 bits.
# L'arrondi supplémentaire doit donc armer INEX2/AEXC_INEX à lui seul.
command(0x8800); movel_imm_absl(0, OPER)
command(0x5C37)
expect_fpsr(23, 0x0000FFFF, 0x00000208)
expect_x(23, 0x4034, 0x8E1BCA0000000000)
fpcr(0)

# Tests 24–26 : FMOVE/FABS/FNEG arrondissent leur résultat, après changement
# du signe, dans chacun des quatre modes. 1 + 3*2^-25 est aux 3/4 d'un ulp
# simple ; une sortie X expose les bits de registre que masquerait FMOVE.S.
for n, op, source_se in ((24, 0x00, 0x3FFF), (25, 0x18, 0xBFFF), (26, 0x1A, 0x3FFF)):
    negative = op == 0x1A
    for mode in range(4):
        fpcr(0x40 | (mode << 4))
        command(0x8800); movel_imm_absl(0, OPER)
        command(0x4800 | op); operand_x(source_se, 0x800000C000000000)
        expect_fpsr(n, 0x0000FFFF, 0x00000208)
        increment = mode == 0 or mode == (2 if negative else 3)
        expect_x(n, 0xBFFF if negative else 0x3FFF,
                 0x8000010000000000 if increment else 0x8000000000000000)

# Test 27 : même contrôle en double (53 bits), et FPCR réservé 11 → double.
# 1 + 3*2^-54 perd ses bits bas, là où les opérations unaires ne faisaient
# auparavant que recopier/changer le signe de la mantisse étendue.
for precision in (0x80, 0xC0):
    for op in (0x00, 0x18, 0x1A):
        fpcr(precision)
        command(0x8800); movel_imm_absl(0, OPER)
        command(0x4800 | op); operand_x(0xBFFF if op == 0x18 else 0x3FFF, 0x8000000000000600)
        expect_fpsr(27, 0x0000FFFF, 0x00000208)
        expect_x(27, 0xBFFF if op == 0x1A else 0x3FFF, 0x8000000000000800)

# Test 28 : les unaires limitent AUSSI la plage d'exposant du FPCR, à la
# différence de FMOVECR/FSGLxxx. Une puissance de deux exacte déborde avec
# OVFL et AEXC_OVFL/INEX mais sans INEX2 (roundAndPackFloatx80 d'Hatari).
for op in (0x00, 0x18, 0x1A):
    fpcr(0x40)
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x4800 | op); operand_x(0xC0C7 if op == 0x18 else 0x40C7, 0x8000000000000000)
    expect_fpsr(28, 0x0000FFFF, 0x00001048)
    expect_x(28, 0xFFFF if op == 0x1A else 0x7FFF, 0)
    fpcr(0x90)                           # double, vers zéro → max fini
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x4800 | op); operand_x(0xC7CF if op == 0x18 else 0x47CF, 0x8000000000000000)
    expect_fpsr(28, 0x0000FFFF, 0x00001048)
    expect_x(28, 0xC3FE if op == 0x1A else 0x43FE, 0xFFFFFFFFFFFFF800)

# Test 29 : underflow simple inexact de 2^-200 ; le port 68881 conserve
# l'exposant $3F81 avec une mantisse nulle (motif brut vérifié contre Hatari).
# En RP, un résultat positif devient le plus petit subnormal simple.
for op in (0x00, 0x18, 0x1A):
    fpcr(0x40)
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x4800 | op); operand_x(0xBF37 if op == 0x18 else 0x3F37, 0x8000000000000000)
    expect_fpsr(29, 0x0000FFFF, 0x00000A28)
    expect_x(29, 0xBF81 if op == 0x1A else 0x3F81, 0)
fpcr(0x70)
command(0x8800); movel_imm_absl(0, OPER)
command(0x4800); operand_x(0x3F37, 0x8000000000000000)
expect_fpsr(29, 0x0000FFFF, 0x00000A28)
expect_x(29, 0x3F81, 0x0000010000000000)

# Test 30 : dénormal étendu normalisé AVANT l'arrondi ; le minimum étendu
# reste exact, avec UNFL seul (pas d'UNFL accumulé sans INEX2).
fpcr(0)
for op in (0x00, 0x18, 0x1A):
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x4800 | op); operand_x(0x8000 if op == 0x18 else 0, 1)
    expect_fpsr(30, 0x0000FFFF, 0x00000800)
    expect_x(30, 0x8000 if op == 0x1A else 0, 1)

# Test 31 : signes de zéro et infini, et NaN (signe/payload inchangés même
# sous FABS/FNEG ; SNaN quiété et SNAN/IOP). Ces cas contournent l'arrondi.
fpcr(0x40)
for op in (0x00, 0x18, 0x1A):
    for source_se, man in ((0, 0), (0x8000, 0), (0x7FFF, 0), (0xFFFF, 0x8000000000000000),
                           (0xFFFF, 0xC00000000BADF00D), (0xFFFF, 0x800000000BADF00D)):
        command(0x8800); movel_imm_absl(0, OPER)
        command(0x4800 | op); operand_x(source_se, man)
        nan = (source_se & 0x7FFF) == 0x7FFF and (man & 0x7FFFFFFFFFFFFFFF) != 0
        snan = nan and (man & 0x4000000000000000) == 0
        result_se = source_se if nan or op == 0 else ((source_se & 0x7FFF) if op == 0x18 else source_se ^ 0x8000)
        expect_fpsr(31, 0x0000FFFF, 0x00004080 if snan else 0)
        expect_x(31, result_se, man | 0x4000000000000000 if nan else man)

# Test 32 : INEX2 activé pendant un FMOVE à précision simple → exception
# CIR vecteur 49, exactement comme les opérations algébriques.
fpcr(0x240)
command(0x4800); operand_x(0x3FFF, 0x800000C000000000)
movew_absl_dn(RESP, 0); cmpiw_dn(0x7031, 0); bne_to("fail32")
fpcr(0)

# Tests 33–40 : offsets FMOVECR indéfinis, patterns et FPSR issus directement de
# fpu_get_constant + softfloat Hatari épinglé (tools/check_fmovecr_oracle.py).
# Le transfert X conserve les motifs bruts (zéro non canonique, NaN signalant,
# bit entier absent), là où une conversion simple/double les masquerait.
fpcr(0)
for off, se, man, cc in (
    (1, 0x4001, 0xFE00068200000000, 0),
    (2, 0x4001, 0xFFC0050380000000, 0),
    (3, 0x2000, 0x7FFFFFFF00000000, 0x01000000),
    (4, 0x0000, 0xFFFFFFFFFFFFFFFF, 0),
    (5, 0x3C00, 0xFFFFFFFFFFFFF800, 0),
    (6, 0x3F80, 0xFFFFFF0000000000, 0),
    (7, 0x0001, 0xF65D8D9C00000000, 0x01000000),
    (8, 0x7FFF, 0x001E000000000000, 0x01000000),
    (9, 0x43FF, 0x000E000000000000, 0),
    (10, 0x407F, 0x0006000000000000, 0),
    (0x10, 0x4000, 0, 0x04000000),
):
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x5C00 | off)
    expect_fpsr(33, 0x0FFFFFFF, cc)
    expect_x(33, se, man)

# Corrections du mot haut APRÈS arrondi : les ±$80 matériels sont visibles
# pour les quatre arrondis sur 1/7, et en RP sur 2.
for mode in range(4):
    fpcr(0x40 | (mode << 4))
    for off, se, man in (
        (1, 0x4001, 0xFE00070000000000 if mode == 3 else 0xFE00068000000000),
        (2, 0x4001, 0xFFC0058000000000 if mode == 3 else 0xFFC0050000000000),
    ):
        command(0x8800); movel_imm_absl(0, OPER)
        command(0x5C00 | off)
        expect_fpsr(34, 0x0FFFFFFF, 0x00000208)
        expect_x(34, se, man)

# CC I/NAN forcés, même si le motif brut d'offset3 reste fini.
for mode in range(4):
    fpcr(0x40 | (mode << 4))
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x5C03)
    upward = mode in (0, 3)
    expect_fpsr(35, 0x0FFFFFFF, (0x02000000 if upward else 0x01000000) | 0x208)
    expect_x(35, 0x2000, 0x8000000000000000 if upward else 0x7FFFFF0000000000)

for mode in range(4):
    fpcr(0x40 | (mode << 4))
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x5C07)
    expect_fpsr(36, 0x0FFFFFFF, 0x01000208)
    expect_x(36, 1, 0xF65D8E0000000000 if mode == 3 else 0xF65D8D8000000000)

# Le SNaN brut de ROM n'est ni quiété ni arrondi ; SNAN n'est pas levé.
for precision in (0, 0x40, 0x80, 0xC0):
    fpcr(precision)
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x5C08)
    expect_fpsr(37, 0x0FFFFFFF, 0x01000000)
    expect_x(37, 0x7FFF, 0x001E000000000000)

# Le pseudo-dénormal d'offset4 est normalisé AVANT l'arrondi ; RN/RP
# passent à l'exposant1, RZ/RM conservent le motif exp0 arrondi.
for precision in (0x40, 0x80, 0xC0):
    for mode in (0, 1):
        fpcr(precision | (mode << 4))
        command(0x8800); movel_imm_absl(0, OPER)
        command(0x5C04)
        expect_fpsr(38, 0x0FFFFFFF, 0x208)
        expect_x(38, 1 if mode == 0 else 0,
                 0x8000000000000000 if mode == 0 else
                 (0xFFFFFF0000000000 if precision == 0x40 else 0xFFFFFFFFFFFFF800))

# Une constante ne modifie pas le quotient ni les exceptions accumulées,
# et efface l'octet EXC de l'instruction précédente.
fpcr(0)
command(0x8800); movel_imm_absl(0x00AAFFF8, OPER)
command(0x5C10)
expect_fpsr(39, 0x0FFFFFFF, 0x04AA00F8)
expect_x(39, 0x4000, 0)

# INEX2 produit par la précision simple livre le vecteur49 ; le SNaN de
# ROM seul ne livre aucune exception même avec SNAN activé.
fpcr(0x240)
command(0x5C01)
movew_absl_dn(RESP, 0); cmpiw_dn(0x7031, 0); bne_to("fail40")
fpcr(0x4040)
command(0x5C08)
movew_absl_dn(RESP, 0); cmpiw_dn(0x0802, 0); bne_to("fail40")
fpcr(0)

# Tests 41–52 : décimal empaqueté, comparé au C Hatari épinglé par
# tools/test_fpu_decimal_oracle.py. Les mots attendus restent des fixtures,
# sans calcul flottant hôte dans le générateur.
def operand_p(words):
    for word in words:
        movel_imm_absl(word, OPER)


def expect_p(n, words, k=17, dynamic=False):
    command(0x7C00 if dynamic else 0x6C00 | (k & 0x7F))
    for dn, value in enumerate(words):
        movel_absl_dn(OPER, dn)
        cmpil_dn(value, dn)
        bne_to(f"fail{n}")


# 17 chiffres entrants exigent les 64 bits : l'ancien strtod perdait 11 bits.
for mode in range(4):
    fpcr(mode << 4)
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x4C00); operand_p((0x00000001, 0x23456789, 0x01234567))
    expect_fpsr(41, 0x0000FFFF, 0x108)
    expect_x(41, 0x3FFF, 0x9E06521462CFD859 if mode == 3 else 0x9E06521462CFD858)

# INEX1 est livré par le CIR ; l'instruction exacte suivante efface EXC
# tout en gardant INEX accumulé. Une entrée P dyadique n'est pas inexacte.
fpcr(0x100)
command(0x8800); movel_imm_absl(0, OPER)
command(0x4C00); operand_p((0x40010001, 0, 0))  # 0,1
movew_absl_dn(RESP, 0); cmpiw_dn(0x7031, 0); bne_to("fail42")
expect_fpsr(42, 0x0000FFFF, 0x108)
fpcr(0)
command(0x4800); operand_x(0x3FFF, 0x8000000000000000)
expect_fpsr(42, 0x0000FFFF, 0x008)
command(0x8800); movel_imm_absl(0, OPER)
command(0x4C00); operand_p((0x00000001, 0x25000000, 0))
expect_fpsr(42, 0x0000FFFF, 0)
expect_x(42, 0x3FFF, 0xA000000000000000)

# Une conversion P suivie de l'arrondi simple conserve INEX1 ET INEX2.
fpcr(0x40)
command(0x8800); movel_imm_absl(0, OPER)
command(0x4C00); operand_p((0x40010001, 0, 0))
expect_fpsr(43, 0x0000FFFF, 0x308)
expect_x(43, 0x3FFB, 0xCCCCCD0000000000)

# k=-1 : 1,25 à une décimale, égalité au pair et arrondis dirigés signés.
for negative in (False, True):
    for mode in range(4):
        fpcr(mode << 4)
        command(0x8800); movel_imm_absl(0, OPER)
        command(0x4800); operand_x(0xBFFF if negative else 0x3FFF, 0xA000000000000000)
        up = mode == (2 if negative else 3)
        expect_p(44, ((0x80000000 if negative else 0) | 1,
                      0x30000000 if up else 0x20000000, 0), k=-1)
        expect_fpsr(44, 0x0000FFFF, 0x208)

# k=0 est un arrondi à l'unité ; k=2 sélectionne deux chiffres significatifs.
fpcr(0)
for k, frac in ((0, 0), (2, 0x20000000)):
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x4800); operand_x(0x3FFF, 0xA000000000000000)
    expect_p(45, (1, frac, 0), k=k)
    expect_fpsr(45, 0x0000FFFF, 0x208)

# 1/3 étendu → 17 chiffres sans passage par les 53 bits du double hôte.
fpcr(0)
command(0x8800); movel_imm_absl(0, OPER)
command(0x4800); operand_x(0x3FFD, 0xAAAAAAAAAAAAAAAB)
expect_p(46, (0x40010003, 0x33333333, 0x33333333))
expect_fpsr(46, 0x0000FFFF, 0x208)

# Style fixe au-delà de l'unité : 12345,625 → 12345,62, égalité au pair.
command(0x8800); movel_imm_absl(0, OPER)
command(0x4800); operand_x(0x400C, 0xC0E6800000000000)
expect_p(47, (0x00040001, 0x23456200, 0), k=-2)
expect_fpsr(47, 0x0000FFFF, 0x208)

# En mode périphérique, Dn est inaccessible : k dynamique garde le défaut17.
command(0x8800); movel_imm_absl(0, OPER)
command(0x4800); operand_x(0x3FFD, 0xAAAAAAAAAAAAAAAB)
expect_p(48, (0x40010003, 0x33333333, 0x33333333), dynamic=True)
expect_fpsr(48, 0x0000FFFF, 0x208)

# Zéro signé ignore l'exposant BCD non nul ; pas de décalage de 64 bits.
for negative in (False, True):
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x4C00); operand_p((0xC9990000 if negative else 0x09990000, 0, 0))
    expect_fpsr(49, 0x0000FFFF, 0)
    expect_x(49, 0x8000 if negative else 0, 0)
    expect_p(49, (0x80000000 if negative else 0, 0, 0), k=63)
    expect_fpsr(49, 0x0000FFFF, 0)

# SNaN P entrant : payload quiété et SNAN, puis format P sortant bit à bit.
command(0x8800); movel_imm_absl(0, OPER)
command(0x4C00); operand_p((0x7FFF0000, 0, 0x12345))
expect_fpsr(50, 0x0000FFFF, 0x4080)
expect_p(50, (0x7FFF0000, 0x40000000, 0x12345))
for high in (0x7FFF, 0xFFFF):
    command(0x8800); movel_imm_absl(0, OPER)
    command(0x4C00); operand_p((high << 16, 0, 0))
    expect_fpsr(50, 0x0000FFFF, 0)
    expect_p(50, (high << 16, 0, 0))

# Les causes de l'opération priment sur INEX1 de la conversion, sans le perdre.
command(0xD080); operand_x(0x7FFF, 0x0000000000012345)  # FMOVEM brut → FP0 SNaN
command(0x8800); movel_imm_absl(0, OPER)
fpcr(0x4100)                           # SNAN et INEX1 activés
command(0x4C22); operand_p((0x40010001, 0, 0))
movew_absl_dn(RESP, 0); cmpiw_dn(0x7036, 0); bne_to("fail51")
expect_fpsr(51, 0x0000FFFF, 0x4188)
fpcr(0x2100)                           # OPERR et INEX1 activés
command(0x8800); movel_imm_absl(0, OPER)
command(0x4C04); operand_p((0xC0010001, 0, 0))  # sqrt(-0,1)
movew_absl_dn(RESP, 0); cmpiw_dn(0x7034, 0); bne_to("fail51")
expect_fpsr(51, 0x0000FFFF, 0x2188)
fpcr(0)

# Trou FMOVECR accessible par le CPU : unnormal → OPERR/NaN en sortie P.
# L'oracle décimal C ne garde pas cet encodage et peut décaler négativement.
command(0x8800); movel_imm_absl(0, OPER)
command(0x5C10)
expect_p(52, (0x7FFF0000, 0xFFFFFFFF, 0xFFFFFFFF))
expect_fpsr(52, 0x0000FFFF, 0x2080)
fpcr(0)

# Test 53 : FINTRZ conserve INEX2/INEX malgré son mode local vers zéro.
# FPCR ne change pas le résultat ; les quatre précisions et modes sont couverts.
for precision in range(4):
    for mode in range(4):
        fpcr((precision << 6) | (mode << 4))
        command(0x8800); movel_imm_absl(0, OPER)
        command(0x4803); operand_x(0xBFFF, 0xC000000000000000)  # -1,5 → -1
        expect_fpsr(53, 0x0000FFFF, 0x0208)
        expect_x(53, 0xBFFF, 0x8000000000000000)
fpcr(0x240)                            # INEX2 activé, précision simple
command(0x4803); operand_x(0x3FFF, 0xC000000000000000)
movew_absl_dn(RESP, 0); cmpiw_dn(0x7031, 0); bne_to("fail53")
fpcr(0)

# Test 54 : FINTRZ quiète le SNaN et conserve SNAN/IOP puis livre le vecteur54.
command(0x8800); movel_imm_absl(0, OPER)
command(0x4803); operand_x(0xFFFF, 0x800000000BADF00D)
expect_fpsr(54, 0x0000FFFF, 0x4080)
expect_x(54, 0xFFFF, 0xC00000000BADF00D)
fpcr(0x4000)                           # SNAN activé
command(0x4803); operand_x(0x7FFF, 0x800000000BADF00D)
movew_absl_dn(RESP, 0); cmpiw_dn(0x7036, 0); bne_to("fail54")
fpcr(0)

# Test 9 : livraison d'exception FP. On ACTIVE DZ dans le FPCR (bit10), puis FDIV
# par 0 → le Response CIR doit livrer « Take Pre-Instruction Exception » (CA=0,
# vecteur DZ $32 en octet bas) = $7032 au lieu du null $0802.
command(0x9000)                          # FMOVEM <ea> → FPCR
movel_imm_absl(0x00000400, OPER)         # enable DZ (bit 10)
command(0x4400)                          # FMOVE.S 1.0 → FP0
movel_imm_absl(0x3F800000, OPER)         # 1.0f
command(0x4420)                          # FDIV.S (poll sort sur $9501)
movel_imm_absl(0x00000000, OPER)         # ÷ 0.0f → DZ activée → response = take-exc
movew_absl_dn(RESP, 0)
cmpiw_dn(0x7032, 0)                      # take-pre-instruction-exc, vecteur DZ $32
bne_to("fail9")

# PASS : D7 = 42 (compat trace) + verdict série « fpu PASS », boucle stable.
w16(0x7E2A)                              # moveq #42,d7
movel_label_a3("pass_str")              # move.l #pass_str,a3
bsr_to("emit")
w16(0x60FE)                             # bra.s *

for n in range(1, 55):                   # FAILn : D7 = -n (compat trace) → verdict commun
    labels[f"fail{n}"] = len(code)
    w16(0x7E00 | ((-n) & 0xFF))          # moveq #-n,d7
    bra_to("fail_common")
label("fail_common")                     # verdict série « fpu FAIL », boucle stable
movel_label_a3("fail_str")
bsr_to("emit")
w16(0x60FE)                             # bra.s *

# Sous-routine emit : a3 = chaîne 0-terminée → écrit chaque octet dans l'UDR $FFFA2F.
label("emit")
# Le verdict utilise l'USART réel : /16, 8N1 et transmetteur activé.
w16(0x13FC); w16(0x0088); w32(0x00FFFA29)  # move.b #$88,UCR.l
w16(0x13FC); w16(0x0001); w32(0x00FFFA2D)  # move.b #1,TSR.l
label("emit_next")
w16(0x121B)                              # move.b (a3)+,d1
beq_to("emit_ret")                       # beq.w emit_ret
label("emit_wait")
w16(0x0839); w16(7); w32(0x00FFFA2D)       # btst #7,TSR.l (buffer vide)
beq_to("emit_wait")
w16(0x13C1); w32(0x00FFFA2F)             # move.b d1,$FFFA2F.l
bra_to("emit_next")
label("emit_ret")
w16(0x4E75)                              # rts
emit_string("pass_str", "NEOST-TEST: fpu PASS\r\n")
emit_string("fail_str", "NEOST-TEST: fpu FAIL\r\n")

for off, lname, base in fixups:          # résoudre les bne.w / bra / bsr / beq
    disp = labels[lname] - base
    code[off:off + 2] = struct.pack(">h", disp)
for off, lname in addr_fixups:           # résoudre les adresses absolues de chaînes
    code[off:off + 4] = struct.pack(">I", ROM_BASE + 8 + labels[lname])

# ---- image ROM ----------------------------------------------------------------
rom = bytearray(b"\xFF" * ROM_SIZE)
rom[0:4] = struct.pack(">I", 0x00010200)         # SSP (+ « version TOS 2.00 »)
rom[4:8] = struct.pack(">I", ROM_BASE + 8)       # PC initial
rom[8:8 + len(code)] = code

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fpu_testrom.img"
with open(out, "wb") as f:
    f.write(rom)
print(f"{out} : {len(code)} octets de code, ROM {ROM_SIZE // 1024} Ko @ ${ROM_BASE:06X}")
