#!/usr/bin/env python3
# =============================================================================
#  make_gemdos_irq_test.py — Garde « un opcode magique GEMDOS = UN appel hôte »,
#  à VERDICT SÉRIE. Produit un dossier de disque hôte (--gemdos) contenant
#  AUTO\GDIRQ.PRG, lancé par EmuTOS au démarrage sur C:.
#
#  LE BUG GARDÉ (2026-10-01, signalé par TOSFC). Cpu68k::run servait l'appel
#  GEMDOS sur l'hôte AVANT execute(), puis remplaçait l'opcode magique ($0008) par
#  un NOP. Si une interruption était prenable à cette frontière d'instruction,
#  execute() la prenait au lieu du NOP : le PC empilé désignait encore $0008,
#  rejoué au rte → un Fread servi deux fois, un bloc de fichier sauté en silence.
#  Hatari traite l'opcode DANS son gestionnaire d'instruction (OpCode_GemDos),
#  donc après l'arbitrage des interruptions — NeoST fait de même depuis
#  (NeostMoira::illegalOpcodeHook).
#
#  Trois verdicts, une ligne « NEOST-TEST: <nom> PASS|FAIL » chacun sur l'UDR :
#    gdwrite  — Fcreate + 3000 Fwrite de 6 octets (enregistrement n° i, ~i) ;
#               après chacun, retour = 6 et Fseek(0,h,1) = 6·(i+1).
#    gdread   — Fopen + 3000 Fread : retour, contenu et position contrôlés.
#               VBL et Timer C tournent pendant ces deux boucles (stress).
#    gdforced — le cas EXACT, rendu déterministe : 24 Fread où le programme
#               masque les IRQ (SR=$2700), attend qu'un VBL et un Timer C soient
#               en attente, puis entre dans le gestionnaire GEMDOS de la
#               cartouche par un RTE qui rabaisse le masque à $2300. L'IRQ
#               devient prenable PILE à la frontière de l'opcode $0008. Avant le
#               correctif, chaque Fread y était servi deux fois (position +12).
#  Puis Fclose, Fdelete, Pterm0.
#
#  Usage : make_gemdos_irq_test.py OUT_DIR   (crée OUT_DIR/AUTO/GDIRQ.PRG)
#  Lancé par tools/run_selftests.py (entrée « gemdos_irq » de selftests.json).
#
#  68000 big-endian. (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_usatan_test import Asm  # noqa: E402  (assembleur maison relogeable)

UDR = 0x00FFFA2F
N_STRESS = 3000
N_FORCED = 24
REC = 6                      # octets par enregistrement : long i, word ~i


def gemdos_rw(a: Asm, fn: int):
    # Fread/Fwrite(d7 = handle, 6, a5) → d0
    a.w(0x2F0D)                                         # move.l a5,-(sp)
    a.w(0x2F3C); a.l32(REC)                             # move.l #6,-(sp)
    a.w(0x3F07)                                         # move.w d7,-(sp)
    a.w(0x3F3C, fn)                                     # move.w #fn,-(sp)
    a.w(0x4E41, 0x4FEF, 0x000C)                         # trap #1 ; lea 12(sp),sp


def ftell(a: Asm):
    # Fseek(0, d7, 1) → d0 = position courante
    a.w(0x3F3C, 0x0001)                                 # move.w #1,-(sp)
    a.w(0x3F07)                                         # move.w d7,-(sp)
    a.w(0x42A7)                                         # clr.l -(sp)
    a.w(0x3F3C, 0x0042)                                 # move.w #$42,-(sp)
    a.w(0x4E41, 0x4FEF, 0x000A)                         # trap #1 ; lea 10(sp),sp


def check_rw_pos(a: Asm, fail: str):
    # d0 = 6 attendu, puis position = d5 + 6
    a.w(0x0C80); a.l32(REC); a.br(0x6600, fail)         # cmpi.l #6,d0 ; bne fail
    a.w(0x5C85)                                         # addq.l #6,d5
    ftell(a)
    a.w(0xB085); a.br(0x6600, fail)                     # cmp.l d5,d0 ; bne fail


def check_record(a: Asm, fail: str):
    # (a5) = d6 et 4(a5) = ~d6.w
    a.w(0xBC95); a.br(0x6600, fail)                     # cmp.l (a5),d6 ; bne fail
    a.w(0x302D, 0x0004, 0x4640)                         # move.w 4(a5),d0 ; not.w d0
    a.w(0xB046); a.br(0x6600, fail)                     # cmp.w d6,d0 ; bne fail


def clear_record(a: Asm):
    a.w(0x4295, 0x426D, 0x0004)                         # clr.l (a5) ; clr.w 4(a5)


def verdict(a: Asm, name: str, nxt: str):
    a.lea_lbl(f's_{name}_p', 3); a.br(0x6100, 'emit'); a.br(0x6000, nxt)
    a.lbl(f'{name}_fail'); a.lea_lbl(f's_{name}_f', 3); a.br(0x6100, 'emit')
    a.br(0x6000, nxt)


def build_code() -> Asm:
    a = Asm(0)
    # Super(0) : l'UDR exige le superviseur. Le masque d'IRQ reste à 3 → VBL et
    # Timer C continuent de tomber pendant les boucles.
    a.w(0x42A7); a.w(0x3F3C, 0x0020); a.w(0x4E41); a.w(0x5C8F)
    a.w(0x23C0); a.abs32('oldssp')                      # move.l d0,oldssp.l
    a.movel_imm_a(UDR, 4)                               # a4 = UDR
    a.lea_lbl('rec', 5)                                 # a5 = enregistrement

    # ---- gdwrite -----------------------------------------------------------------
    a.w(0x4267); a.w(0x4879); a.abs32('fname')          # clr.w -(sp) ; pea fname
    a.w(0x3F3C, 0x003C, 0x4E41, 0x508F)                 # Fcreate ; addq.l #8,sp
    a.w(0x4A80); a.br(0x6B00, 'gdwrite_fail')           # tst.l d0 ; bmi fail
    a.w(0x3E00)                                         # move.w d0,d7 (handle)
    a.w(0x7C00, 0x7A00)                                 # moveq #0,d6 ; moveq #0,d5
    a.lbl('wloop')
    a.w(0x2A86)                                         # move.l d6,(a5)
    a.w(0x3006, 0x4640, 0x3B40, 0x0004)                 # move.w d6,d0 ; not.w d0 ; move.w d0,4(a5)
    gemdos_rw(a, 0x0040)                                # Fwrite
    check_rw_pos(a, 'gdwrite_fail')
    a.w(0x5286); a.w(0x0C86); a.l32(N_STRESS); a.br(0x6D00, 'wloop')   # addq ; cmpi ; blt
    a.w(0x3F07, 0x3F3C, 0x003E, 0x4E41, 0x588F)         # Fclose(d7)
    verdict(a, 'gdwrite', 'rd')

    # ---- gdread ------------------------------------------------------------------
    a.lbl('rd')
    a.w(0x3F3C, 0x0002); a.w(0x4879); a.abs32('fname')  # move.w #2,-(sp) ; pea fname
    a.w(0x3F3C, 0x003D, 0x4E41, 0x508F)                 # Fopen ; addq.l #8,sp
    a.w(0x4A80); a.br(0x6B00, 'gdread_fail')
    a.w(0x3E00)
    a.w(0x7C00, 0x7A00)
    a.lbl('rloop')
    clear_record(a)
    gemdos_rw(a, 0x003F)                                # Fread
    check_rw_pos(a, 'gdread_fail')
    check_record(a, 'gdread_fail')
    a.w(0x5286); a.w(0x0C86); a.l32(N_STRESS); a.br(0x6D00, 'rloop')
    verdict(a, 'gdread', 'fo')

    # ---- gdforced : IRQ prenable à la frontière de l'opcode $0008 ------------------
    # (le fichier reste ouvert : en cas d'échec de gdread, Fseek échoue et d0 < 0)
    a.lbl('fo')
    a.w(0x3F3C, 0x0000, 0x3F07, 0x42A7, 0x3F3C, 0x0042) # Fseek(0, d7, 0)
    a.w(0x4E41, 0x4FEF, 0x000A)
    a.w(0x4A80); a.br(0x6600, 'gdforced_fail')          # tst.l d0 ; bne fail
    a.w(0x7C00, 0x7A00)
    a.lbl('floop')
    clear_record(a)
    a.w(0x007C, 0x0700)                                 # ori.w #$0700,sr
    # Attente > une trame (≈18 cyc × 12000 ≈ 216 000 cyc) : VBL et Timer C en attente.
    a.w(0x223C); a.l32(12000)                           # move.l #12000,d1
    a.lbl('fwait'); a.w(0x5381); a.br(0x6600, 'fwait')  # subq.l #1,d1 ; bne
    # Trame d'un trap #1 superviseur : [SR=$2300][PC=fret][args Fread]
    a.w(0x2F0D); a.w(0x2F3C); a.l32(REC); a.w(0x3F07); a.w(0x3F3C, 0x003F)
    a.w(0x4879); a.abs32('fret')                        # pea fret
    a.w(0x3F3C, 0x2300)                                 # move.w #$2300,-(sp)
    # …et par-dessus, la trame du RTE qui entre dans le gestionnaire GEMDOS ($84).
    a.w(0x2F38, 0x0084)                                 # move.l $84.w,-(sp)
    a.w(0x3F3C, 0x2300)                                 # move.w #$2300,-(sp)
    a.w(0x4E73)                                         # rte
    a.lbl('fret')
    a.w(0x4FEF, 0x000C)                                 # lea 12(sp),sp
    check_rw_pos(a, 'gdforced_fail')
    check_record(a, 'gdforced_fail')
    a.w(0x5286); a.w(0x0C86); a.l32(N_FORCED); a.br(0x6D00, 'floop')
    verdict(a, 'gdforced', 'done')

    # ---- ménage ------------------------------------------------------------------
    a.lbl('done')
    a.w(0x3F07, 0x3F3C, 0x003E, 0x4E41, 0x588F)         # Fclose(d7)
    a.w(0x4879); a.abs32('fname')                       # pea fname
    a.w(0x3F3C, 0x0041, 0x4E41, 0x5C8F)                 # Fdelete ; addq.l #6,sp
    a.w(0x2F39); a.abs32('oldssp')                      # move.l oldssp.l,-(sp)
    a.w(0x3F3C, 0x0020, 0x4E41, 0x5C8F)                 # Super(oldssp)
    a.w(0x4267, 0x4E41)                                 # Pterm0

    # ---- emit : a3 = chaîne 0-terminée → UDR (a4) ----------------------------------
    a.lbl('emit')
    a.w(0x121B); a.br(0x6700, 'emit_ret'); a.w(0x1881); a.br(0x6000, 'emit')
    a.lbl('emit_ret'); a.w(0x4E75)

    # ---- données -------------------------------------------------------------------
    a.string('fname', 'C:\\GDIRQ.DAT')
    for n in ('gdwrite', 'gdread', 'gdforced'):
        a.string(f's_{n}_p', f'NEOST-TEST: {n} PASS\r\n')
        a.string(f's_{n}_f', f'NEOST-TEST: {n} FAIL\r\n')
    a.lbl('oldssp'); a.w(0, 0)
    a.lbl('rec'); a.w(0, 0, 0)
    return a


def build_prg() -> bytes:
    # Même format que make_usatan_test.build_prg (en-tête $601A + TEXT + relocation).
    a = build_code()
    text = a.assemble()
    reloc = bytearray(struct.pack('>I', a.relocs[0]))
    prev = a.relocs[0]
    for r in a.relocs[1:]:
        d = r - prev
        while d > 254:
            reloc.append(1); d -= 254
        reloc.append(d); prev = r
    reloc.append(0)
    hdr = struct.pack('>HIIIIIIH', 0x601A, len(text), 0, 0, 0, 0, 0, 0)
    return hdr + text + bytes(reloc)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: make_gemdos_irq_test.py OUT_DIR", file=sys.stderr)
        return 2
    auto = os.path.join(sys.argv[1], 'AUTO')
    os.makedirs(auto, exist_ok=True)
    prg = build_prg()
    with open(os.path.join(auto, 'GDIRQ.PRG'), 'wb') as f:
        f.write(prg)
    print(f"GEMDOS IRQ test -> {auto}/GDIRQ.PRG ({len(prg)} bytes)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
