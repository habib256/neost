#!/usr/bin/env python3
# =============================================================================
#  make_usatan_test.py — Programme de test UltraSatan + NetUSBee à VERDICT SÉRIE,
#  livré en USTEST.PRG dans un dossier AUTO (carte SD du slot 1 ou disquette).
#
#  Pourquoi AUTO : EmuTOS amorce le disque dur dès qu'il en trouve un (la
#  disquette n'est plus consultée, bios/blkdev.c), et n'exécute un secteur racine
#  que sur un disque SANS partition reconnue (bios/disk.c disk_try_dmaboot) ; en
#  revanche il lance TOUJOURS les C:\AUTO\*.PRG du lecteur d'amorçage — comme le
#  fait un vrai utilisateur d'UltraSatan avec ses outils. Le programme passe en
#  superviseur (Super), parle aux DEUX extensions COMME LES LOGICIELS D'ÉPOQUE,
#  puis Pterm0 :
#
#    · UltraSatan : séquence LongRW de l'outil US_CONF (dma.h) — 1er octet A1
#      bas, octets suivants A1 haut avec attente de l'IRQ (GPIP bit 5), bascule
#      R/W + compteur de secteurs AVANT le dernier octet, DMA d'un secteur ;
#        usfw   — ICD $20 'USCurntFW' (ID 0) → le secteur commence par « UltraSatan »
#        usinq  — INQUIRY (classe 0) du slot 2 (ID 1) → « JOOK…», slot '2'
#        usrtc  — 'USRdClRTC' → signature 'RTC', mois 1..12
#        uscdrv — _drvbits ($4C2) bit 2 : EmuTOS a monté C: depuis la carte SD
#    · NetUSBee : accès du pilote FreeMiNT (isp116x.h, lectures MOT, primitives raw) —
#        nubid  — HcChipID & $FF00 == $6100 (ISP1160)
#        nubscr — HcScratch : aller-retour 16 bits
#        nubnic — NE2000 : BNRY (page 0) aller-retour (écrit /ROM3 $FB, relit /ROM4 $FA)
#
#  Une ligne « NEOST-TEST: <nom> PASS|FAIL » par test sur l'UDR ($FFFA2F), lue
#  par tools/run_selftests.py (--serial-dump). Le PRG est RELOGEABLE (table de
#  relocation TOS générée par l'assembleur maison : chaque référence absolue est
#  consignée) — il tourne depuis n'importe quelle adresse de chargement.
#
#  Usage : make_usatan_test.py OUT.prg  (ou OUT.st : disquette FAT12 avec AUTO\USTEST.PRG,
#          pour tester SANS carte SD — uscdrv échoue alors, c'est attendu).
#  La carte SD de la suite (tools/make_usatan_hd.py) importe build_prg().
#
#  68000 big-endian. (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import struct
import sys

SECT = 512
UDR = 0x00FFFA2F
GPIP = 0x00FFFA01
DMA = 0x00FF8600             # +4 DATA, +6 MODE, +9/+B/+D ADDR hi/mid/lo
FLOCK = 0x043E
DRVBITS = 0x04C2

# ISP1160 du NetUSBee (isp116x.h)
LSB_WRITE, DATA_READ, MSB_DATA_WRITE, MSB_CMD_WRITE = 0xFA0000, 0xFA8000, 0xFB8000, 0xFBC000


class Asm:
    def __init__(self, base: int):
        self.base = base
        self.items = []            # ('raw',[words]) | ('label',name) | ('br',op,name) | ('abs32',name)
        self.labels = {}

    def w(self, *words): self.items.append(('raw', list(words)))
    def l32(self, v): self.w((v >> 16) & 0xFFFF, v & 0xFFFF)
    def lbl(self, name): self.items.append(('label', name))
    def br(self, op, name): self.items.append(('br', op, name))        # Bcc.W / BSR.W
    def dbra(self, reg, name): self.items.append(('br', 0x51C8 | reg, name))
    def abs32(self, name): self.items.append(('abs32', name))         # adresse absolue d'un label
    # Pseudo-instructions
    def lea_lbl(self, name, areg): self.w(0x41F9 | (areg << 9)); self.abs32(name)   # lea lbl.l,An
    def movel_imm(self, v, dreg): self.w(0x203C | (dreg << 9)); self.l32(v)        # move.l #v,Dn
    def movel_imm_a(self, v, areg): self.w(0x207C | (areg << 9)); self.l32(v)      # movea.l #v,An
    def tst_w_abs(self, addr): self.w(0x4A79); self.l32(addr)                       # tst.w addr.l (lecture MOT)
    def tst_b_abs(self, addr): self.w(0x4A39); self.l32(addr)                       # tst.b addr.l (lecture octet)
    def string(self, name, text):
        self.lbl(name)
        b = text.encode('ascii') + b'\x00'
        if len(b) % 2: b += b'\x00'
        self.w(*[(b[i] << 8) | b[i + 1] for i in range(0, len(b), 2)])

    def assemble(self) -> bytes:
        # Renvoie le code ; self.relocs = offsets des longs à reloger (références
        # absolues à des labels, base + offset) — table de relocation TOS du PRG.
        off = 0
        for it in self.items:
            if it[0] == 'label': self.labels[it[1]] = off
            elif it[0] == 'raw': off += 2 * len(it[1])
            elif it[0] in ('br', 'abs32'): off += 4
        out = []
        self.relocs = []
        off = 0
        for it in self.items:
            if it[0] == 'label': continue
            if it[0] == 'raw': out += it[1]; off += 2 * len(it[1])
            elif it[0] == 'abs32':
                a = self.base + self.labels[it[1]]; out += [(a >> 16) & 0xFFFF, a & 0xFFFF]
                self.relocs.append(off); off += 4
            elif it[0] == 'br':
                d = self.labels[it[2]] - (off + 2)
                assert -32768 <= d <= 32767, (it[2], d)
                out += [it[1], d & 0xFFFF]; off += 4
        return b''.join(struct.pack('>H', x & 0xFFFF) for x in out)


def build_code() -> 'Asm':
    a = Asm(0)                                          # base 0 : le TOS reloge (table PRG)
    # Super(0) : les registres matériels exigent le mode superviseur. Ancien SSP → 'oldssp'.
    a.w(0x42A7); a.w(0x3F3C, 0x0020); a.w(0x4E41); a.w(0x5C8F)     # clr.l -(sp) ; move.w #$20,-(sp) ; trap #1 ; addq.l #6,sp
    a.w(0x23C0); a.abs32('oldssp')                      # move.l d0,oldssp.l
    # Registres fixes : a4 = UDR, a5 = DMA ($FF8600), a6 = tampon DMA (512 octets, dans le programme)
    a.movel_imm_a(UDR, 4)
    a.movel_imm_a(DMA, 5)
    a.lea_lbl('buf', 6)

    # ---- usfw : 'USCurntFW' sur l'ID 0 ------------------------------------------
    a.lea_lbl('cmd_fw', 0); a.w(0x7C0B)                 # lea cmd_fw,a0 ; moveq #11,d6
    a.br(0x6100, 'acsi_rd')                             # bsr acsi_rd → d0 = statut (-1 = timeout)
    a.w(0x4A40); a.br(0x6600, 'usfw_fail')              # tst.w d0 ; bne fail
    a.w(0x0C96); a.l32(0x556C7472); a.br(0x6600, 'usfw_fail')          # cmpi.l #'Ultr',(a6)
    a.w(0x0CAE); a.l32(0x61536174); a.w(0x0004); a.br(0x6600, 'usfw_fail')   # cmpi.l #'aSat',4(a6)
    a.lea_lbl('s_usfw_p', 3); a.br(0x6100, 'emit'); a.br(0x6000, 'usinq')
    a.lbl('usfw_fail'); a.lea_lbl('s_usfw_f', 3); a.br(0x6100, 'emit')

    # ---- usinq : INQUIRY classe 0 sur l'ID 1 (slot 2) ----------------------------
    a.lbl('usinq')
    a.lea_lbl('cmd_inq', 0); a.w(0x7C06)                # 6 octets
    a.br(0x6100, 'acsi_rd')
    a.w(0x4A40); a.br(0x6600, 'usinq_fail')
    a.w(0x0CAE); a.l32(0x4A4F4F4B); a.w(0x0008); a.br(0x6600, 'usinq_fail')  # cmpi.l #'JOOK',8(a6)
    a.w(0x0C2E, 0x0032, 0x001B); a.br(0x6600, 'usinq_fail')                  # cmpi.b #'2',27(a6)
    a.lea_lbl('s_usinq_p', 3); a.br(0x6100, 'emit'); a.br(0x6000, 'usrtc')
    a.lbl('usinq_fail'); a.lea_lbl('s_usinq_f', 3); a.br(0x6100, 'emit')

    # ---- usrtc : 'USRdClRTC' -------------------------------------------------------
    a.lbl('usrtc')
    a.lea_lbl('cmd_rtc', 0); a.w(0x7C0B)
    a.br(0x6100, 'acsi_rd')
    a.w(0x4A40); a.br(0x6600, 'usrtc_fail')
    a.w(0x0C56, 0x5254); a.br(0x6600, 'usrtc_fail')     # cmpi.w #'RT',(a6)
    a.w(0x0C2E, 0x0043, 0x0002); a.br(0x6600, 'usrtc_fail')   # cmpi.b #'C',2(a6)
    a.w(0x102E, 0x0004)                                 # move.b 4(a6),d0  (mois)
    a.br(0x6700, 'usrtc_fail')                          # beq fail (0)
    a.w(0x0C00, 0x000C); a.br(0x6200, 'usrtc_fail')     # cmpi.b #12,d0 ; bhi fail
    a.lea_lbl('s_usrtc_p', 3); a.br(0x6100, 'emit'); a.br(0x6000, 'uscdrv')
    a.lbl('usrtc_fail'); a.lea_lbl('s_usrtc_f', 3); a.br(0x6100, 'emit')

    # ---- uscdrv : _drvbits bit 2 (C: monté par le TOS depuis la carte SD) ---------
    a.lbl('uscdrv')
    a.w(0x2038, DRVBITS)                                # move.l $4C2.w,d0
    a.w(0x0800, 0x0002); a.br(0x6700, 'uscdrv_fail')    # btst #2,d0 ; beq fail
    a.lea_lbl('s_uscdrv_p', 3); a.br(0x6100, 'emit'); a.br(0x6000, 'nubid')
    a.lbl('uscdrv_fail'); a.lea_lbl('s_uscdrv_f', 3); a.br(0x6100, 'emit')

    # ---- nubid : HcChipID ($27) via write_addr + raw_read_data16 -------------------
    a.lbl('nubid')
    a.tst_w_abs(LSB_WRITE + (0x27 << 1)); a.tst_w_abs(MSB_CMD_WRITE)
    a.w(0x3039); a.l32(DATA_READ)                       # move.w DATA_READ.l,d0
    a.w(0x0240, 0xFF00)                                 # andi.w #$FF00,d0
    a.w(0x0C40, 0x6100); a.br(0x6600, 'nubid_fail')     # cmpi.w #$6100,d0
    a.lea_lbl('s_nubid_p', 3); a.br(0x6100, 'emit'); a.br(0x6000, 'nubscr')
    a.lbl('nubid_fail'); a.lea_lbl('s_nubid_f', 3); a.br(0x6100, 'emit')

    # ---- nubscr : HcScratch ($28) aller-retour $5AC3 (raw : LSB ← bas, MSB ← haut) --
    a.lbl('nubscr')
    a.tst_w_abs(LSB_WRITE + (0xA8 << 1)); a.tst_w_abs(MSB_CMD_WRITE)      # write_addr($28 | $80)
    a.tst_w_abs(LSB_WRITE + (0xC3 << 1)); a.tst_w_abs(MSB_DATA_WRITE + (0x5A << 1))
    a.tst_w_abs(LSB_WRITE + (0x28 << 1)); a.tst_w_abs(MSB_CMD_WRITE)      # write_addr($28)
    a.w(0x3039); a.l32(DATA_READ)
    a.w(0x0C40, 0x5AC3); a.br(0x6600, 'nubscr_fail')
    a.lea_lbl('s_nubscr_p', 3); a.br(0x6100, 'emit'); a.br(0x6000, 'nubnic')
    a.lbl('nubscr_fail'); a.lea_lbl('s_nubscr_f', 3); a.br(0x6100, 'emit')

    # ---- nubnic : NE2000 page 0, BNRY ($03) ← $46 puis relecture ------------------
    # Protocole fil EtherNEC (BUSENEC.I) : écrire = fausse lecture /ROM3
    # ($FB0000 + reg*512 + data*2), lire = /ROM4 ($FA0000 + reg*512).
    a.lbl('nubnic')
    a.tst_b_abs(0xFB0000 + 0 * 512 + 0x21 * 2)          # CR = $21 (page 0, stop)
    a.tst_b_abs(0xFB0000 + 3 * 512 + 0x46 * 2)          # BNRY = $46
    a.w(0x1039); a.l32(0xFA0000 + 3 * 512)              # move.b $FA0600.l,d0
    a.w(0x0C00, 0x0046); a.br(0x6600, 'nubnic_fail')
    a.lea_lbl('s_nubnic_p', 3); a.br(0x6100, 'emit'); a.br(0x6000, 'done')
    a.lbl('nubnic_fail'); a.lea_lbl('s_nubnic_f', 3); a.br(0x6100, 'emit')

    a.lbl('done')
    a.w(0x2F39); a.abs32('oldssp')                      # move.l oldssp.l,-(sp)
    a.w(0x3F3C, 0x0020); a.w(0x4E41); a.w(0x5C8F)       # Super(oldssp)
    a.w(0x4267); a.w(0x4E41)                            # Pterm0 : clr.w -(sp) ; trap #1

    # ---- acsi_rd : LongRW(lecture) — a0 = paquet, d6 = nb d'octets, a6 = tampon ----
    # Renvoie d0 = statut ACSI (octet), ou -1 si aucune IRQ dans le délai.
    a.lbl('acsi_rd')
    a.w(0x31FC, 0xFFFF, FLOCK)                          # move.w #-1,FLOCK.w
    a.w(0x200E)                                         # move.l a6,d0
    a.w(0x1B40, 0x000D)                                 # move.b d0,$D(a5)  (adresse basse)
    a.w(0xE088); a.w(0x1B40, 0x000B)                    # lsr.l #8,d0 ; move.b d0,$B(a5)
    a.w(0xE088); a.w(0x1B40, 0x0009)                    # lsr.l #8,d0 ; move.b d0,9(a5)
    a.w(0x3B7C, 0x0088, 0x0006)                         # move.w #$88,MODE  (NO_DMA|HDC, A1 bas)
    a.w(0x4240, 0x1018, 0x3B40, 0x0004)                 # clr.w d0 ; move.b (a0)+,d0 ; move.w d0,DATA
    a.w(0x3B7C, 0x008A, 0x0006)                         # move.w #$8A,MODE  (A1 haut)
    a.w(0x5546)                                         # subq.w #2,d6  (octets du milieu)
    a.w(0x5346)                                         # subq.w #1,d6  (dbra)
    a.lbl('acsi_mid')
    a.br(0x6100, 'wait_irq'); a.w(0x4A80); a.br(0x6B00, 'acsi_fail')   # bsr ; tst.l d0 ; bmi
    a.w(0x4240, 0x1018, 0x3B40, 0x0004)                 # octet suivant
    a.w(0x3B7C, 0x008A, 0x0006)
    a.dbra(6, 'acsi_mid')
    a.br(0x6100, 'wait_irq'); a.w(0x4A80); a.br(0x6B00, 'acsi_fail')
    a.w(0x3B7C, 0x0190, 0x0006)                         # MODE = DMA_WR|NO_DMA|SC_REG : bascule R/W
    a.w(0x3B7C, 0x0090, 0x0006)                         # MODE = NO_DMA|SC_REG
    a.w(0x3B7C, 0x0001, 0x0004)                         # SECT_CNT = 1
    a.w(0x3B7C, 0x008A, 0x0006)                         # MODE = NO_DMA|HDC|A0
    a.w(0x4240, 0x1018, 0x3B40, 0x0004)                 # dernier octet → la commande part
    a.w(0x426D, 0x0006)                                 # clr.w MODE : démarre le DMA (lecture)
    a.br(0x6100, 'wait_irq'); a.w(0x4A80); a.br(0x6B00, 'acsi_fail')   # fin de transfert
    a.w(0x3B7C, 0x008A, 0x0006)                         # endcmd : MODE puis statut
    a.w(0x302D, 0x0004)                                 # move.w DATA,d0
    a.w(0x0240, 0x00FF)                                 # andi.w #$FF,d0
    a.br(0x6000, 'acsi_done')
    a.lbl('acsi_fail'); a.w(0x70FF)                     # moveq #-1,d0
    a.lbl('acsi_done')
    a.w(0x3B7C, 0x0080, 0x0006)                         # hdone : MODE = NO_DMA
    a.w(0x4278, FLOCK)                                  # clr.w FLOCK.w
    a.w(0x4E75)

    # ---- wait_irq : attend GPIP bit 5 = 0 (IRQ DMA), d0 = 0 OK / -1 délai dépassé -----
    a.lbl('wait_irq')
    a.w(0x323C, 0xFFFF)                                 # move.w #$FFFF,d1
    a.lbl('wait_loop')
    a.w(0x0839, 0x0005); a.l32(GPIP)                    # btst #5,GPIP.l
    a.br(0x6700, 'wait_ok')                             # beq ok  (bit à 0 = IRQ)
    a.dbra(1, 'wait_loop')
    a.w(0x70FF, 0x4E75)                                 # moveq #-1,d0 ; rts
    a.lbl('wait_ok'); a.w(0x7000, 0x4E75)               # moveq #0,d0 ; rts

    # ---- emit : a3 = chaîne 0-terminée → UDR (a4) ------------------------------------
    a.lbl('emit')
    # Émetteur réel : activer 8N1/TE et attendre le TDR au lieu de l'écraser.
    a.w(0x13FC, 0x0088); a.l32(0x00FFFA29)  # move.b #$88,UCR
    a.w(0x13FC, 0x0001); a.l32(0x00FFFA2D)  # move.b #1,TSR
    a.lbl('emit_next')
    a.w(0x121B); a.br(0x6700, 'emit_ret')
    a.lbl('emit_wait')
    a.w(0x0839, 7); a.l32(0x00FFFA2D)        # btst #7,TSR
    a.br(0x6700, 'emit_wait')
    a.w(0x1881); a.br(0x6000, 'emit_next')
    a.lbl('emit_ret'); a.w(0x4E75)

    # ---- données ----------------------------------------------------------------------
    a.lbl('cmd_fw');  a.w(0x1F20, 0x5553, 0x4375, 0x726E, 0x7446, 0x5700)   # $1F $20 'USCurntFW' (+pad)
    a.lbl('cmd_rtc'); a.w(0x1F20, 0x5553, 0x5264, 0x436C, 0x5254, 0x4300)   # $1F $20 'USRdClRTC'
    a.lbl('cmd_inq'); a.w(0x3200, 0x0000, 0x2C00)                           # (1<<5)|$12, 0,0,0,44,0
    for n in ('usfw', 'usinq', 'usrtc', 'uscdrv', 'nubid', 'nubscr', 'nubnic'):
        a.string(f's_{n}_p', f'NEOST-TEST: {n} PASS\r\n')
        a.string(f's_{n}_f', f'NEOST-TEST: {n} FAIL\r\n')
    a.lbl('oldssp'); a.w(0, 0)
    a.lbl('buf')                                        # tampon DMA de 512 octets (aligné mot)
    a.w(*([0] * 256))
    return a


def build_prg() -> bytes:
    # Exécutable GEMDOS : en-tête 28 octets (magic $601A, tailles TEXT/DATA/BSS/SYM,
    # réservé, drapeaux, absflag = 0 → table de relocation présente) + TEXT + table
    # de relocation (1er offset sur 32 bits, puis deltas d'un octet ; 1 = +254 ; 0 = fin).
    a = build_code()
    text = a.assemble()
    reloc = bytearray()
    if a.relocs:
        reloc += struct.pack('>I', a.relocs[0])
        prev = a.relocs[0]
        for r in a.relocs[1:]:
            d = r - prev
            while d > 254:
                reloc.append(1); d -= 254
            reloc.append(d); prev = r
    reloc.append(0)
    hdr = struct.pack('>HIIIIIIH', 0x601A, len(text), 0, 0, 0, 0, 0, 0)
    return hdr + text + bytes(reloc)


def build_floppy(prg: bytes, name: str = 'USTEST') -> bytes:
    # Disquette 720 Ko FAT12 (BPB comme tools/make_floppy.py), dossier AUTO avec USTEST.PRG.
    # Secteur de boot NON amorçable : le TOS monte A:, et lance A:\AUTO s'il amorce sur A:.
    SPT, SIDES, TRACKS = 9, 2, 80
    TOTAL = TRACKS * SIDES * SPT
    img = bytearray(TOTAL * SECT)
    img[0:2] = b'\x60\x1c'; img[2:8] = b'NeoST '; img[8:11] = b'UST'
    struct.pack_into('<H', img, 0x0B, SECT); img[0x0D] = 2
    struct.pack_into('<H', img, 0x0E, 1); img[0x10] = 2
    struct.pack_into('<H', img, 0x11, 112); struct.pack_into('<H', img, 0x13, TOTAL)
    img[0x15] = 0xF9; struct.pack_into('<H', img, 0x16, 3)
    struct.pack_into('<H', img, 0x18, SPT); struct.pack_into('<H', img, 0x1A, SIDES)
    RES, NFAT, SPF, NDIRS, SPC = 1, 2, 3, 112, 2
    fat1 = RES * SECT
    root = (RES + NFAT * SPF) * SECT
    data = root + ((NDIRS * 32 + SECT - 1) // SECT) * SECT

    def set_fat(idx, val):
        for base in (fat1, fat1 + SPF * SECT):
            off = base + idx * 3 // 2
            if idx & 1:
                img[off] = (img[off] & 0x0F) | ((val << 4) & 0xF0); img[off + 1] = (val >> 4) & 0xFF
            else:
                img[off] = val & 0xFF; img[off + 1] = (img[off + 1] & 0xF0) | ((val >> 8) & 0x0F)
    set_fat(0, 0xFF9); set_fat(1, 0xFFF)
    write_fs(img, root, data, SPC, set_fat, 0xFFF, prg, name)
    return bytes(img)


def write_fs(img, root, data, spc, set_fat, eoc, prg, name='USTEST'):
    # Racine : dossier AUTO (cluster 2) ; AUTO : '.', '..', USTEST.PRG (clusters 3..).
    DATE = (46 << 9) | (8 << 5) | 21; TIME = (12 << 11)          # 2026-08-21 12:00 (fixe)
    csize = spc * SECT
    def entry(at, name, ext, attr, cluster, size):
        img[at:at + 8] = name.ljust(8).encode(); img[at + 8:at + 11] = ext.ljust(3).encode()
        img[at + 11] = attr
        struct.pack_into('<H', img, at + 22, TIME); struct.pack_into('<H', img, at + 24, DATE)
        struct.pack_into('<H', img, at + 26, cluster); struct.pack_into('<I', img, at + 28, size)
    entry(root, 'AUTO', '', 0x10, 2, 0)
    set_fat(2, eoc)
    d = data                                                     # cluster 2
    entry(d, '.', '', 0x10, 2, 0); entry(d + 32, '..', '', 0x10, 0, 0)
    entry(d + 64, name, 'PRG', 0x20, 3, len(prg))
    nclu = (len(prg) + csize - 1) // csize
    for k in range(nclu):
        c = 3 + k
        o = data + (c - 2) * csize
        img[o:o + min(csize, len(prg) - k * csize)] = prg[k * csize:(k + 1) * csize]
        set_fat(c, eoc if k == nclu - 1 else c + 1)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: make_usatan_test.py OUT.prg | OUT.st", file=sys.stderr)
        return 2
    prg = build_prg()
    out = sys.argv[1]
    data = build_floppy(prg) if out.lower().endswith('.st') else prg
    with open(out, 'wb') as f:
        f.write(data)
    print(f"UltraSatan/NetUSBee test -> {out} ({len(prg)} bytes PRG"
          f"{', on a 720 KB floppy in AUTO' if data is not prg else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
