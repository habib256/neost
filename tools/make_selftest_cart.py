#!/usr/bin/env python3
# =============================================================================
#  make_selftest_cart.py — Cartouche DIAGNOSTIC (magic $FA52235F, saut $FA0004 au
#  reset) qui exécute des auto-tests et émet un VERDICT sur le port série RS-232.
#
#  Convention de verdict (une ligne par test, scannée par tools/run_selftests.py) :
#      NEOST-TEST: <nom> PASS
#      NEOST-TEST: <nom> FAIL <détail optionnel>
#
#  Avantages du chemin cartouche diagnostic : pré-TOS, sans disque, déterministe,
#  aucun oracle. Le CPU saute à $FA0004 dès le reset (TOS 1.x / EmuTOS le vérifient),
#  masque les IRQ et écrit chaque octet de verdict dans l'UDR $FFFA2F (sink série
#  capturé par neost-headless). Tests inclus :
#    • cpu     — invariants arithmétiques (add.w, moveq #-1/cmpi.l) : garde-fou cœur 68000.
#    • timing  — le compteur vidéo $FF8209 n'est PAS figé (le moteur d'horloge/faisceau
#                tourne) : sentinelle anti-« clock morte » / trame gelée.
#
#  --break cpu|timing force le test correspondant à ÉCHOUER (pour valider que le
#  runner attrape bien les FAIL).
#
#  68000 big-endian. (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import argparse
import struct
import sys

CART_BASE = 0xFA0000
UDR       = 0x00FFFA2F      # MFP USART data register (émission série)
VIDLO     = 0x00FF8209      # compteur d'adresse vidéo, octet bas (avance avec le faisceau)

# Bande attendue de HBL par trame (test frame). La cartouche diagnostic tourne PRÉ-TOS
# (état vidéo par défaut du reset) : NeoST y prend 262 HBL/trame — DÉTERMINISTE et stable
# (ST/STE, tous TOS, tous budgets de trames, mesuré 2026-07-09 via --dump-at $1004). Bande
# serrée autour → flague une dérive GROSSIÈRE de trame (50 Hz→313, 71 Hz mono→501, horloge
# morte→0, ×2→~131/524) sans flakiness. ⚠ Re-calibrer si le modèle de trame HBL change.
FRAME_HBL_LO = 256
FRAME_HBL_HI = 268

# Colonne du compteur vidéo à la HBL 100, après délai (Display-Enable).
# Le compteur avance de 160 octets par ligne : son octet bas seul mélange la
# phase horizontale et le numéro de ligne (le débit série peut changer ce dernier).
# Lire d'abord le bas au cycle étalon, puis le milieu, composer l'adresse et
# prendre son reste modulo 160. Pré-TOS : base vidéo 0, compteur sous 64 Ko.
# L'ancien DBRA #30 échantillonnait après Display-Enable : le compteur figé
# confondait la ligne et la latence. DBRA #16 lit pendant DE (colonne 132).
# Mutation vérifiée : deux NOP avant la lecture ajoutent 8 cycles → colonne 136,
# hors bande. La sentinelle détecte donc une vraie variation de latence IPL.
IPL_POS_LO = 130        # mesuré 132 ; bande ±2, colonne indépendante de la ligne
IPL_POS_HI = 134


def build(break_test: str | None) -> bytes:
    # --- Mini-assembleur 2 passes (labels + branches/immédiats résolus) ---------
    items = []   # ('raw',[words]) | ('immL',[prefix],label) | ('br',opword,label) | ('label',name)
    labels = {}

    def raw(*words): items.append(('raw', list(words)))
    def lbl(name):   items.append(('label', name))
    def br(op, name): items.append(('br', op, name))
    def imm_addr(prefix, name):  # move.l #(CART_BASE+label),An : prefix + 32-bit adresse
        items.append(('immL', [prefix], name))
    def vec(handler, addr):      # move.l #(CART_BASE+handler),addr.l : installe un vecteur
        items.append(('vec', handler, addr))

    # move.l #imm32,An : opword 0x2N7C (N = 8+reg pour An en champ dest) — on passe l'opword.
    A3 = 0x267C  # a3
    A4 = 0x287C  # a4
    A5 = 0x2A7C  # a5
    A7 = 0x2E7C  # a7 (sp)

    # Détermine quelle égalité attendue casser (pour --break).
    add_expect = 0x1234 if break_test != 'cpu' else 0x9999   # add.w #$0234,#$1000 → $1234
    # Pour break timing : on échantillonne une adresse ROM STABLE (le magic de la
    # cartouche, $FA0000) au lieu du compteur vidéo → 3 échantillons identiques → FAIL
    # (sans fauter, contrairement aux registres MMIO sensibles).
    vidsrc = VIDLO if break_test != 'timing' else CART_BASE

    def raw32(opword, val):                 # move.l #imm32,An
        raw(opword, (val >> 16) & 0xFFFF, val & 0xFFFF)

    # ---------------- code (entrée = $FA0004) ----------------
    raw32(A7, 0x00008000)                   # move.l #$00008000,a7   (pile)
    raw32(A4, UDR)                          # move.l #$00FFFA2F,a4

    # -- test CPU --------------------------------------------------------------
    raw(0x303C, 0x1000)                     # move.w #$1000,d0
    raw(0x0640, 0x0234)                     # addi.w #$0234,d0
    raw(0x0C40, add_expect)                 # cmpi.w #expect,d0
    br(0x6600, 'cpu_fail')                  # bne cpu_fail
    raw(0x70FF)                             # moveq #-1,d0
    raw(0x0C80, 0xFFFF, 0xFFFF)             # cmpi.l #$FFFFFFFF,d0
    br(0x6600, 'cpu_fail')                  # bne cpu_fail
    imm_addr(A3, 'cpu_pass'); br(0x6100, 'emit')   # move.l #cpu_pass,a3 ; bsr emit
    br(0x6000, 'timing')                    # bra timing
    lbl('cpu_fail')
    imm_addr(A3, 'cpu_failstr'); br(0x6100, 'emit')

    # -- test timing (compteur vidéo non figé) ---------------------------------
    lbl('timing')
    raw32(A5, vidsrc)                       # move.l #vidsrc,a5
    raw(0x1415)                             # move.b (a5),d2   (échantillon 0)
    raw(0x3E3C, 0x2000)                     # move.w #$2000,d7
    lbl('dly1'); br(0x51CF, 'dly1')         # dbra d7,dly1  (boucle sur elle-même = délai)
    raw(0x1615)                             # move.b (a5),d3   (échantillon 1)
    raw(0x3E3C, 0x2000)                     # move.w #$2000,d7
    lbl('dly2'); br(0x51CF, 'dly2')         # dbra d7,dly2
    raw(0x1815)                             # move.b (a5),d4   (échantillon 2)
    raw(0xB602)                             # cmp.b d2,d3
    br(0x6600, 'tim_pass')                  # bne tim_pass  (a changé → vivant)
    raw(0xB802)                             # cmp.b d2,d4
    br(0x6600, 'tim_pass')                  # bne tim_pass
    imm_addr(A3, 'tim_failstr'); br(0x6100, 'emit')
    br(0x6000, 'frame')                     # échec timing → poursuit quand même le test frame
    lbl('tim_pass')
    imm_addr(A3, 'tim_passstr'); br(0x6100, 'emit')
    # (tim_pass tombe dans le test frame ; tim_fail y saute aussi via 'frame')

    # -- test frame : compte les HBL par trame via interruptions (cycle-exact) ---
    # Compteurs en RAM : $1000 hbl courant, $1004 hbl/trame latché, $1008 nb VBL.
    HBLC, LASTHBL, VBLC = 0x00001000, 0x00001004, 0x00001008
    lbl('frame')
    vec('hbl_handler', 0x68)                # move.l #hbl_handler,$68  (HBL autovect. niv2)
    vec('vbl_handler', 0x70)                # move.l #vbl_handler,$70  (VBL autovect. niv4)
    raw(0x42B9, HBLC >> 16, HBLC & 0xFFFF)  # clr.l $1000
    raw(0x42B9, LASTHBL >> 16, LASTHBL & 0xFFFF)  # clr.l $1004
    raw(0x42B9, VBLC >> 16, VBLC & 0xFFFF)  # clr.l $1008
    raw(0x46FC, 0x2000)                     # move.w #$2000,sr  (autorise IPL → IRQ)
    lbl('fwait')
    raw(0x2A39, VBLC >> 16, VBLC & 0xFFFF)  # move.l $1008,d5
    raw(0x0C85, 0x0000, 0x0003)             # cmpi.l #3,d5
    br(0x6500, 'fwait')                     # bcs fwait  (d5 < 3 → attend 3 trames)
    raw(0x46FC, 0x2700)                     # move.w #$2700,sr  (remasque)
    raw(0x2C39, LASTHBL >> 16, LASTHBL & 0xFFFF)  # move.l $1004,d6  (HBL de la dernière trame)
    # Bande attendue [LO,HI] (calibrée sur l'oracle NeoST ; large = anti-dérive grossière).
    lo, hi = (0, 0xFFFFFF) if break_test == 'frame' else (FRAME_HBL_LO, FRAME_HBL_HI)
    # break frame : on impose une bande IMPOSSIBLE ($10000+) → toujours FAIL.
    if break_test == 'frame':
        lo, hi = 0x100000, 0x100000
    raw(0x0C86, lo >> 16, lo & 0xFFFF)      # cmpi.l #LO,d6
    br(0x6500, 'frame_fail')                # bcs frame_fail  (d6 < LO)
    raw(0x0C86, hi >> 16, hi & 0xFFFF)      # cmpi.l #HI,d6
    br(0x6200, 'frame_fail')                # bhi frame_fail  (d6 > HI)
    imm_addr(A3, 'frm_passstr'); br(0x6100, 'emit')
    br(0x6000, 'ipl')
    lbl('frame_fail')
    imm_addr(A3, 'frm_failstr'); br(0x6100, 'emit')

    # -- test ipl : phase d'entrée d'exception HBL (latence IPL) ------------------
    # Le handler HBL de la ligne 100 a stocké la colonne vidéo en $100C.
    # On vérifie que cette phase horizontale tombe dans la bande calibrée.
    IPLPOS = 0x0000100C
    lbl('ipl')
    raw(0x3039, IPLPOS >> 16, IPLPOS & 0xFFFF)   # move.w $100C,d0
    ilo, ihi = (IPL_POS_LO, IPL_POS_HI)
    if break_test == 'ipl':
        ilo, ihi = 0x7FFE, 0x7FFF               # bande impossible → FAIL
    raw(0x0C40, ilo)                        # cmpi.w #ILO,d0
    br(0x6500, 'ipl_fail')                  # bcs ipl_fail
    raw(0x0C40, ihi)                        # cmpi.w #IHI,d0
    br(0x6200, 'ipl_fail')                  # bhi ipl_fail
    imm_addr(A3, 'ipl_passstr'); br(0x6100, 'emit')
    br(0x6000, 'done')
    lbl('ipl_fail')
    imm_addr(A3, 'ipl_failstr'); br(0x6100, 'emit')

    lbl('done')
    raw(0x4E72, 0x2700)                     # stop #$2700

    # -- handlers d'interruption -------------------------------------------------
    # HBL : incrémente hblCount ; à la 100ᵉ HBL, petit délai (→ Display-Enable) puis
    # capture la colonne vidéo (adresse modulo 160) en $100C.
    lbl('hbl_handler')
    raw(0x52B9, HBLC >> 16, HBLC & 0xFFFF)  # addq.l #1,$1000
    raw(0x0CB9, 0x0000, 0x0064, HBLC >> 16, HBLC & 0xFFFF)  # cmpi.l #100,$1000
    br(0x6600, 'hbl_ret')                   # bne hbl_ret
    raw(0x303C, 0x0010)                     # move.w #16,d0
    lbl('hbl_dly'); br(0x51C8, 'hbl_dly')   # dbra d0,hbl_dly  (~174 cyc → plein DE)
    raw(0x1239, 0x00FF, 0x8209)             # move.b $FF8209,d1
    # Lire le milieu APRÈS le bas conserve le cycle de l'échantillon étalon.
    # Les bas observés (36 ou 196) restent sous 256 pendant cette lecture :
    # pas de retenue entre les deux accès (écart borné à moins de 20 octets).
    raw(0x7400)                             # moveq #0,d2
    raw(0x1439, 0x00FF, 0x8207)             # move.b $FF8207,d2
    raw(0xE14A)                             # lsl.w #8,d2
    raw(0x0281, 0x0000, 0x00FF)             # andi.l #$FF,d1
    raw(0x8242)                             # or.w d2,d1
    raw(0x82FC, 0x00A0)                     # divu #160,d1
    raw(0x4841)                             # swap d1 (reste → mot bas)
    raw(0x33C1, IPLPOS >> 16, IPLPOS & 0xFFFF)  # move.w d1,$100C
    lbl('hbl_ret')
    raw(0x4E73)                             # rte
    lbl('vbl_handler')
    raw(0x23F9, HBLC >> 16, HBLC & 0xFFFF, LASTHBL >> 16, LASTHBL & 0xFFFF)  # move.l $1000,$1004
    raw(0x42B9, HBLC >> 16, HBLC & 0xFFFF)  # clr.l $1000
    raw(0x52B9, VBLC >> 16, VBLC & 0xFFFF)  # addq.l #1,$1008
    raw(0x4E73)                             # rte

    # -- sous-routine emit : a3 = chaîne (0-terminée), écrit sur UDR (a4) --------
    lbl('emit')
    # Initialiser seulement au verdict : conserver la phase des sondes raster.
    raw(0x13FC, 0x0088, 0x00FF, 0xFA29)      # move.b #$88,UCR.l (/16 8N1)
    raw(0x13FC, 0x0001, 0x00FF, 0xFA2D)      # move.b #1,TSR.l (TE)
    lbl('emit_next')
    raw(0x121B)                             # move.b (a3)+,d1
    br(0x6700, 'emit_ret')                  # beq emit_ret
    lbl('emit_wait')
    raw(0x0839, 0x0007, 0x00FF, 0xFA2D)      # btst #7,TSR.l (buffer vide)
    br(0x6700, 'emit_wait')                  # beq emit_wait
    raw(0x1881)                             # move.b d1,(a4)
    br(0x6000, 'emit_next')                 # bra emit_next
    lbl('emit_ret')
    raw(0x4E75)                             # rts

    # -- chaînes de verdict -----------------------------------------------------
    def stritem(name, text):
        lbl(name)
        b = text.encode('ascii') + b'\x00'
        if len(b) % 2: b += b'\x00'         # aligne (mots)
        words = [ (b[i] << 8) | b[i+1] for i in range(0, len(b), 2) ]
        raw(*words)
    stritem('cpu_pass',    'NEOST-TEST: cpu PASS\r\n')
    stritem('cpu_failstr', 'NEOST-TEST: cpu FAIL\r\n')
    stritem('tim_passstr', 'NEOST-TEST: timing PASS\r\n')
    stritem('tim_failstr', 'NEOST-TEST: timing FAIL\r\n')
    stritem('frm_passstr', 'NEOST-TEST: frame PASS\r\n')
    stritem('frm_failstr', 'NEOST-TEST: frame FAIL\r\n')
    stritem('ipl_passstr', 'NEOST-TEST: ipl PASS\r\n')
    stritem('ipl_failstr', 'NEOST-TEST: ipl FAIL\r\n')

    # --- passe 1 : offsets (le code commence à l'offset 4, après le magic) ------
    off = 4
    for it in items:
        if it[0] == 'label':
            labels[it[1]] = off
        elif it[0] == 'raw':
            off += 2 * len(it[1])
        elif it[0] == 'immL':
            off += 2 * len(it[1]) + 4        # prefix + adresse 32 bits
        elif it[0] == 'vec':
            off += 2 + 4 + 4                 # move.l #handler,addr : op + handler + vecteur
        elif it[0] == 'br':
            off += 4                         # opword + disp16

    # --- passe 2 : émission -----------------------------------------------------
    words = []
    off = 4
    for it in items:
        if it[0] == 'label':
            continue
        if it[0] == 'raw':
            words += it[1]; off += 2 * len(it[1])
        elif it[0] == 'immL':
            words += it[1]; off += 2 * len(it[1])
            addr = CART_BASE + labels[it[2]]
            words += [(addr >> 16) & 0xFFFF, addr & 0xFFFF]; off += 4
        elif it[0] == 'vec':
            handler = CART_BASE + labels[it[1]]; vaddr = it[2]
            words += [0x23FC, (handler >> 16) & 0xFFFF, handler & 0xFFFF,
                      (vaddr >> 16) & 0xFFFF, vaddr & 0xFFFF]; off += 10
        elif it[0] == 'br':
            op = it[1]
            disp = labels[it[2]] - (off + 2)
            words += [op, disp & 0xFFFF]; off += 4

    data = bytearray()
    data += struct.pack('>I', 0xFA52235F)    # magic diagnostic
    for wd in words:
        data += struct.pack('>H', wd & 0xFFFF)
    if len(data) % 2: data += b'\x00'
    return bytes(data)


def main() -> int:
    ap = argparse.ArgumentParser(description="Cartouche diagnostic auto-test NeoST (verdict série)")
    ap.add_argument("out", help="fichier .bin de sortie")
    ap.add_argument("--break", dest="brk", choices=("cpu", "timing", "frame", "ipl"),
                    help="force ce test à ÉCHOUER (validation du runner)")
    args = ap.parse_args()
    data = build(args.brk)
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"cartouche diagnostic → {args.out} ({len(data)} octets)"
          + (f" [BREAK {args.brk}]" if args.brk else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
