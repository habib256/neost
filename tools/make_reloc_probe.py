#!/usr/bin/env python3
# =============================================================================
#  make_reloc_probe.py — Disquette de mesure : Pexec et une table de relocation de 2 octets.
#
#  POURQUOI. AUTO\VROOM.PRG, sur l'image Vroom (1991)(Lankhor).st, a 2 octets de relocation au lieu de 4. [C] TOS 1.62
#  $E08A34-$E08A50 en lit 4 sans tester le résultat : le mot bas vient de la pile. Sous NeoST et Hatari, le programme
#  en sort corrompu selon le moment des interruptions ; sur un vrai STE, Vroom ne plante pas (mainteneur, 2026-11-04).
#  Cette sonde mesure ce seul point sur le fer, sans le jeu, et le programme n'a rien de privé.
#
#  COMMENT. Un PRG de la même forme : 2442 octets, 2412 de texte, 2 de relocation à zéro, et dans A:\AUTO. Le texte
#  est une nappe de NOP. Le programme y cherche le premier mot changé et affiche son décalage, puis attend une touche.
#  Résultat aussi en RAM pour le headless : $3F0.l = décalage ou -1, $3F4.w = version de TOS.
#
#  Usage : make_reloc_probe.py [SORTIE.st]    Défaut : tests/out/reloc_probe/RELOCPRB.ST
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_usatan_test import Asm, build_floppy   # noqa: E402

TEXT_SIZE = 2412        # comme le VROOM.PRG d'origine
CODE_AT = 0x800         # la nappe couvre tout ce qui précède, dont le décalage $E0 attendu
NOP = 0x4E71


def build_text() -> bytes:
    a = Asm(0)
    a.lbl("start")
    a.br(0x6000, "code")                                    # bra.w code
    a.w(*[NOP] * ((CODE_AT - 4) // 2))
    a.lbl("code")
    a.br(0x4DFA, "start")                                   # lea start(pc),a6
    a.w(0x42A7, 0x3F3C, 0x0020, 0x4E41, 0x5C8F, 0x2A40)     # Super(0) ; ancienne pile en a5
    a.w(0x2078, 0x04F2, 0x3E28, 0x0002)                     # _sysbase, os_version dans d7
    a.w(0x43EE, 0x0004)                                     # lea 4(a6),a1
    a.w(0x3C3C, (CODE_AT - 4) // 2 - 1)                     # move.w #mots-1,d6
    a.lbl("scan")
    a.w(0x0C59, NOP)                                        # cmpi.w #NOP,(a1)+
    a.br(0x56CE, "scan")                                    # dbne d6,scan
    a.br(0x6600, "found")                                   # bne.w found
    a.w(0x7AFF)                                             # moveq #-1,d5
    a.br(0x6000, "store")
    a.lbl("found")
    a.w(0x2A09, 0x9A8E, 0x5585)                             # move.l a1,d5 ; sub.l a6,d5 ; subq.l #2,d5
    a.lbl("store")
    a.w(0x21C5, 0x03F0, 0x31C7, 0x03F4)                     # move.l d5,$3F0.w ; move.w d7,$3F4.w
    a.w(0x2F0D, 0x3F3C, 0x0020, 0x4E41, 0x5C8F)             # Super(ancienne pile)
    a.br(0x41FA, "s_tos")
    a.br(0x6100, "puts")
    a.w(0x3807)                                             # move.w d7,d4
    a.br(0x6100, "hex4")
    a.w(0x4A85)                                             # tst.l d5
    a.br(0x6B00, "intact")                                  # bmi.w intact
    a.br(0x41FA, "s_mod")
    a.br(0x6100, "puts")
    a.w(0x3805)                                             # move.w d5,d4
    a.br(0x6100, "hex4")
    a.br(0x6000, "wait")
    a.lbl("intact")
    a.br(0x41FA, "s_ok")
    a.br(0x6100, "puts")
    a.lbl("wait")
    a.br(0x41FA, "s_key")
    a.br(0x6100, "puts")
    a.w(0x3F3C, 0x0001, 0x4E41, 0x548F)                     # Cconin
    a.w(0x4267, 0x4E41)                                     # Pterm0
    a.lbl("puts")
    a.w(0x2F08, 0x3F3C, 0x0009, 0x4E41, 0x5C8F, 0x4E75)     # Cconws(a0) ; rts
    a.lbl("hex4")
    a.w(0x7603)                                             # moveq #3,d3
    a.lbl("digit")
    a.w(0xE95C, 0x3404, 0x0242, 0x000F)                     # rol.w #4,d4 ; move.w d4,d2 ; andi.w #$F,d2
    a.br(0x43FA, "s_hex")                                   # lea s_hex(pc),a1
    a.w(0x1431, 0x2000)                                     # move.b 0(a1,d2.w),d2
    a.w(0x3F02, 0x3F3C, 0x0002, 0x4E41, 0x588F)             # Cconout
    a.dbra(3, "digit")
    a.w(0x4E75)
    a.lbl("s_hex")
    a.w(0x3031, 0x3233, 0x3435, 0x3637, 0x3839, 0x4142, 0x4344, 0x4546)
    a.string("s_tos", "\r\nRELOC PROBE TOS $")
    a.string("s_ok", " INTACT")
    a.string("s_mod", " CHANGED +$")
    a.string("s_key", "\r\nNote this line, then reset.\r\n")
    text = a.assemble()
    if len(text) > TEXT_SIZE:
        raise ValueError(f"texte trop long : {len(text)}")
    return text + struct.pack(">H", NOP) * ((TEXT_SIZE - len(text)) // 2)


def build_prg() -> bytes:
    text = build_text()
    header = struct.pack(">HIIIIIIH", 0x601A, len(text), 0, 0, 0, 0, 0, 0)
    return header + text + b"\x00\x00"                      # 2 octets de relocation, le défaut à mesurer


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path(__file__).resolve().parent.parent / "tests" / "out" / "reloc_probe" / "RELOCPRB.ST")
    out.parent.mkdir(parents=True, exist_ok=True)
    prg = build_prg()
    if len(prg) != 2442:
        raise ValueError(f"PRG de {len(prg)} octets, 2442 attendus")
    out.write_bytes(build_floppy(prg, "RELOCPRB"))
    print(f"{out} : AUTO\\RELOCPRB.PRG, {len(prg)} octets")
    return 0


if __name__ == "__main__":
    sys.exit(main())
