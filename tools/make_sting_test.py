#!/usr/bin/env python3
# =============================================================================
#  make_sting_test.py — Validation bout-en-bout « un ST émulé surfe » : disquette
#  de boot STinG + EtherNEC pour le backend SLIRP de NeoST.
#
#  La disquette contient :
#    AUTO\STING.PRG    noyau STinG 1.26 (freeware, P. Rottengatter / U. R. Andersson)
#    AUTO\STING.INF    « A:\STING\ » — chemin des modules
#    AUTO\STGTEST.PRG  NOTRE programme (assemblé ici) : il configure le port
#                      « EtherNet » d'ENEC.STX par l'API STinG (cookie 'STiK'),
#                      puis prouve la pile : resolve() DNS + GET HTTP en TCP.
#    STING\TCP.STX UDP.STX RESOLVE.STX   modules STinG 1.26
#    STING\ENEC.STX    pilote EtherNEC (GPL, Dr. Thomas Redelberger, v1.15)
#    STING\DEFAULT.CFG NAMESERVER = 10.0.2.3 (relais DNS de SLIRP), DNS_SAVE=FALSE
#    STING\ROUTE.TAB   défaut via la passerelle SLIRP 10.0.2.2, port EtherNet
#
#  Pourquoi un programme et pas STNGPORT.CPX : l'IP du port ne se règle
#  normalement qu'au CPX (accessoire GEM, STING.PRT) — inutilisable en AUTO ni
#  en headless. STGTEST fait la même chose que le CPX par l'API documentée :
#    cookie 'STiK' → DRV_LIST.get_dftab("TRANSPORT_TCPIP") → TPL
#                  → get_dftab("MODULE_LAYER")             → STX
#    STX.query_chains(&ports,0,0) → PORT « EtherNet » → ip_addr/sub_mask pokés
#    TPL.on_port("EtherNet") → STX.load_routing_table() → TPL.resolve()
#    → TPL.TCP_open/TCP_wait_state/TCP_send/CNget_char (GET / HTTP/1.0)
#  Offsets vérifiés sur les sources 1.26 (github.com/th-otto/STinG : transprt.h,
#  port.h, sting/setup.c) et le pilote (etherne.zip : SRC/ENESTNG.C).
#
#  Tout est écrit sur la console ET recopié sur RS-232 (Bconout AUX) : le verdict
#  se lit en headless dans --serial-dump (« DNS=a.b.c.d », puis « HTTP/1. »).
#
#  L'attente réseau est comptée en TICS 200 Hz ($4BA, temps ÉMULÉ) comme dans
#  make_net_test.py : en headless le CPU tourne plus vite que le mur, ce sont
#  les trames qui pompent SLIRP.
#
#  Usage :
#    python3 tools/make_sting_test.py OUT.st STING126_DIR ETHERNE_DIR [hôte]
#      STING126_DIR : dossier « sting126 » extrait de STING126.LZH
#          (web.archive.org/web/20060220141848id_/http://www.ettnet.se/~dlanor/sting/r_000706/sting126.lzh)
#      ETHERNE_DIR  : dossier contenant ENEC.STX, extrait d'etherne.zip
#          (web222.webclient5.de/prj/atari/download/etherne.zip)
#      hôte : nom à résoudre + serveur du GET (défaut theoldnet.com)
#
#  68000 big-endian. (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_usatan_test import Asm   # noqa: E402  (assembleur maison + relocation PRG)

SECT = 512
HZ200 = 0x000004BA               # compteur 200 Hz du TOS (temps émulé)

# Offsets STinG 1.26 (vérifiés dans les en-têtes cités ci-dessus).
DRV_GET_DFTAB = 10               # DRV_LIST : magic[10] puis get_dftab
TPL_TCP_OPEN = 40
TPL_TCP_SEND = 48
TPL_TCP_WAIT_STATE = 52
TPL_CNGET_CHAR = 80
TPL_GETVSTR = 32
TPL_RESOLVE = 96
TPL_ON_PORT = 120
STX_LOAD_ROUTES = 20
STX_SET_SYSVARS = 24
STX_QUERY_CHAINS = 28
STX_TIMER_NOW = 72
STX_GET_ROUTE = 84
PORT_IP = 12                     # PORT.ip_addr
PORT_MASK = 16                   # PORT.sub_mask
PORT_NEXT = 46                   # PORT.next
TESTABLISH = 4
E_NODATA = -2

IP_ST = 0x0A00020F               # 10.0.2.15 (1re adresse du NAT SLIRP)
IP_MASK = 0xFFFFFF00             # 255.255.255.0


def build_code(host: str, fetch: bool = True, port: int = 80, connect_ip: int | None = None) -> Asm:
    # fetch=False : PRG de CONFIGURATION seule (port + routes), sans resolve ni
    # GET — pour préparer STinG à un VRAI client (CAB…) sans laisser derrière
    # soi une connexion TCP ouverte qui polluerait les traces.
    a = Asm(0)

    # ---- cookie 'STiK' : lu en superviseur, puis RETOUR EN USER -------------
    # Les clients STinG (PING, ConfSTinG…) tournent en mode utilisateur ; le
    # noyau joue avec le vecteur Privilege Violation pour ses entrées d'API.
    # Rester en Super() pendant resolve() gelait le moteur — tout l'appelant
    # repasse donc en user, seul le tour de cookie est privilégié.
    a.w(0x42A7)                                   # clr.l -(sp)
    a.w(0x3F3C, 0x0020); a.w(0x4E41); a.w(0x5C8F)  # Super(0)
    a.w(0x23C0); a.abs32('oldssp')                # move.l d0,oldssp
    a.lea_lbl('m_start', 3); a.br(0x6100, 'puts2')
    a.w(0x2078, 0x05A0)                           # movea.l $5A0.w,a0
    a.w(0x2008)                                   # move.l a0,d0
    a.br(0x6700, 'f_cookie')
    a.lbl('ck_loop')
    a.w(0x0C90); a.l32(0x5354694B)                # cmpi.l #'STiK',(a0)
    a.br(0x6700, 'ck_found')
    a.w(0x4A90)                                   # tst.l (a0)
    a.br(0x6700, 'f_cookie')
    a.w(0x5088)                                   # addq.l #8,a0
    a.br(0x6000, 'ck_loop')
    a.lbl('ck_found')
    a.w(0x2028, 0x0004)                           # move.l 4(a0),d0 = DRV_LIST
    a.w(0x23C0); a.abs32('drvlist')               # move.l d0,drvlist
    a.w(0x2F39); a.abs32('oldssp')                # move.l oldssp,-(sp)
    a.w(0x3F3C, 0x0020); a.w(0x4E41); a.w(0x5C8F)  # Super(oldssp) → mode user
    a.w(0x2479); a.abs32('drvlist')               # movea.l drvlist,a2

    # ---- get_dftab("TRANSPORT_TCPIP") → a5 = TPL ---------------------------
    a.w(0x4879); a.abs32('s_tpl')                 # pea s_tpl
    a.w(0x206A, DRV_GET_DFTAB)                    # movea.l 10(a2),a0
    a.w(0x4E90); a.w(0x588F)                      # jsr (a0) ; addq.l #4,sp
    a.w(0x2A40)                                   # movea.l d0,a5
    a.w(0x200D)                                   # move.l a5,d0
    a.br(0x6700, 'f_tpl')

    # ---- get_dftab("MODULE_LAYER") → a6 = STX ------------------------------
    a.w(0x4879); a.abs32('s_mod')
    a.w(0x206A, DRV_GET_DFTAB)
    a.w(0x4E90); a.w(0x588F)
    a.w(0x2C40)                                   # movea.l d0,a6
    a.w(0x200E)                                   # move.l a6,d0
    a.br(0x6700, 'f_mod')

    # ---- query_chains(&ports, 0, 0) → marcher jusqu'au port « EtherNet » ---
    a.w(0x42A7); a.w(0x42A7)                      # clr.l -(sp) ×2 (layers, drivers)
    a.w(0x4879); a.abs32('portsv')                # pea portsv
    a.w(0x206E, STX_QUERY_CHAINS)                 # movea.l 28(a6),a0
    a.w(0x4E90); a.w(0x4FEF, 0x000C)              # jsr ; lea 12(sp),sp
    a.w(0x2679); a.abs32('portsv')                # movea.l portsv,a3
    a.lbl('walkp')
    a.w(0x200B)                                   # move.l a3,d0
    a.br(0x6700, 'f_port')
    a.w(0x2053)                                   # movea.l (a3),a0   = PORT.name
    a.w(0x43F9); a.abs32('s_enet')                # lea s_enet,a1
    a.lbl('cmp_loop')
    a.w(0x1018)                                   # move.b (a0)+,d0
    a.w(0x1219)                                   # move.b (a1)+,d1
    a.w(0xB001)                                   # cmp.b d1,d0
    a.br(0x6600, 'next_port')
    a.w(0x4A00)                                   # tst.b d0
    a.br(0x6600, 'cmp_loop')
    a.br(0x6000, 'found')                         # les deux NUL ensemble : trouvé
    a.lbl('next_port')
    a.w(0x266B, PORT_NEXT)                        # movea.l 46(a3),a3
    a.br(0x6000, 'walkp')

    # ---- IP/masque pokés (ce que fait le CPX via CTL_GENERIC_SET_IP/MASK) --
    a.lbl('found')
    a.w(0x284B)                                   # movea.l a3,a4
    a.w(0x297C); a.l32(IP_ST); a.w(PORT_IP)       # move.l #IP,12(a4)
    a.w(0x297C); a.l32(IP_MASK); a.w(PORT_MASK)   # move.l #masque,16(a4)

    # ---- on_port("EtherNet") : set_state(1) du pilote = init de la NE2000 --
    a.w(0x4879); a.abs32('s_enet')
    a.w(0x206D, TPL_ON_PORT)                      # movea.l 120(a5),a0
    a.w(0x4E90); a.w(0x588F)
    a.w(0x4A40)                                   # tst.w d0 (TRUE = 1)
    a.br(0x6700, 'f_on')
    a.lea_lbl('m_up', 3); a.br(0x6100, 'puts2')

    # ---- diagnostic : valeur d'ACTIVATE vue par le noyau -------------------
    a.lea_lbl('m_act', 3); a.br(0x6100, 'puts2')
    a.w(0x4879); a.abs32('s_activate')            # pea "ACTIVATE"
    a.w(0x206D, TPL_GETVSTR)                      # movea.l 32(a5),a0
    a.w(0x4E90); a.w(0x588F)                      # jsr ; addq.l #4,sp
    a.w(0x2640)                                   # movea.l d0,a3
    a.w(0x200B)                                   # move.l a3,d0
    a.br(0x6700, 'act_nul')
    a.br(0x6100, 'puts2')
    a.lbl('act_nul')
    a.lea_lbl('m_nl', 3); a.br(0x6100, 'puts2')

    # ---- moteur forcé : set_sysvars(1, 10) = threading actif (ceinture) ----
    a.w(0x3F3C, 0x000A)                           # move.w #10,-(sp)  valeur
    a.w(0x3F3C, 0x0001)                           # move.w #1,-(sp)   which
    a.w(0x206E, STX_SET_SYSVARS)                  # movea.l 24(a6),a0
    a.w(0x4E90); a.w(0x588F)                      # jsr ; addq.l #4,sp

    # ---- routes rechargées maintenant que le port existe et est actif ------
    a.w(0x206E, STX_LOAD_ROUTES)                  # movea.l 20(a6),a0
    a.w(0x4E90)                                   # jsr (a0)
    a.lea_lbl('m_route', 3); a.br(0x6100, 'puts2')
    a.w(0x3E00)                                   # move.w d0,d7 (retour du chargement)
    a.w(0x3007); a.br(0x6100, 'psdec')            # imprimé…
    a.w(0x323C, ord(' ')); a.br(0x6100, 'putc2')
    # …puis la route 0 relue : get_route_entry(0, &net, &mask, &port, &gw)
    a.w(0x4879); a.abs32('rgw')                   # pea rgw
    a.w(0x4879); a.abs32('rport')                 # pea rport
    a.w(0x4879); a.abs32('rmask')                 # pea rmask
    a.w(0x4879); a.abs32('rnet')                  # pea rnet
    a.w(0x3F3C, 0x0000)                           # move.w #0,-(sp)  index
    a.w(0x206E, STX_GET_ROUTE)                    # movea.l 84(a6),a0
    a.w(0x4E90); a.w(0x4FEF, 0x0012)              # jsr ; lea 18(sp),sp
    a.w(0x3E00)                                   # move.w d0,d7
    a.w(0x3007); a.br(0x6100, 'psdec')
    a.lea_lbl('m_nl', 3); a.br(0x6100, 'puts2')

    if not fetch:
        a.br(0x6000, 'done')

    # ---- resolve(hôte, NULL, iplist, 1) ------------------------------------
    a.lea_lbl('m_dns', 3); a.br(0x6100, 'puts2')
    a.w(0x3F3C, 0x0001)                           # move.w #1,-(sp)   listlen
    a.w(0x4879); a.abs32('iplist')                # pea iplist
    a.w(0x42A7)                                   # clr.l -(sp)       real = NULL
    a.w(0x4879); a.abs32('s_host')                # pea hôte
    a.w(0x206D, TPL_RESOLVE)                      # movea.l 96(a5),a0
    a.w(0x4E90); a.w(0x4FEF, 0x000E)              # jsr ; lea 14(sp),sp
    a.w(0x3E00)                                   # move.w d0,d7
    a.br(0x6F00, 'f_dns')                         # ble : 0 adresse ou erreur
    # « DNS=a.b.c.d » en décimal pointé, octet par octet (big-endian).
    a.lea_lbl('m_dnseq', 3); a.br(0x6100, 'puts2')
    a.w(0x49F9); a.abs32('iplist')                # lea iplist,a4
    for i in range(4):
        a.w(0x7000)                               # moveq #0,d0
        a.w(0x101C)                               # move.b (a4)+,d0
        a.br(0x6100, 'pdec')
        if i < 3:
            a.w(0x323C, ord('.'))                 # move.w #'.',d1
            a.br(0x6100, 'putc2')
    a.lea_lbl('m_nl', 3); a.br(0x6100, 'puts2')

    # ---- TCP_open(ip, 80, 0, 2000) -----------------------------------------
    a.w(0x3F3C, 2000)                             # move.w #2000,-(sp)  buffer
    a.w(0x4267)                                   # clr.w -(sp)         tos
    a.w(0x3F3C, port)                             # move.w #port,-(sp)
    if connect_ip is None:
        a.w(0x2F39); a.abs32('iplist')            # move.l iplist,-(sp) hôte
    else:
        # Le banc local peut vérifier le DNS puis contacter la passerelle NAT,
        # sans envoyer 127/8 dans la boucle locale de la pile invitée.
        a.w(0x2F3C); a.l32(connect_ip)            # move.l #ip,-(sp)
    a.w(0x206D, TPL_TCP_OPEN)                     # movea.l 40(a5),a0
    a.w(0x4E90); a.w(0x4FEF, 0x000A)              # jsr ; lea 10(sp),sp
    a.w(0x3C00)                                   # move.w d0,d6 = handle
    a.w(0x3E00)                                   # move.w d0,d7 (pour l'erreur)
    a.br(0x6B00, 'f_tcp')                         # bmi

    # ---- TCP_wait_state(handle, TESTABLISH, 15 s) --------------------------
    a.w(0x3F3C, 15)                               # move.w #15,-(sp)  timeout s
    a.w(0x3F3C, TESTABLISH)                       # move.w #4,-(sp)
    a.w(0x3F06)                                   # move.w d6,-(sp)
    a.w(0x206D, TPL_TCP_WAIT_STATE)               # movea.l 52(a5),a0
    a.w(0x4E90); a.w(0x5C8F)                      # jsr ; addq.l #6,sp
    a.w(0x3E00)                                   # move.w d0,d7
    a.w(0x4A40)                                   # tst.w d0 (E_NORMAL = 0)
    a.br(0x6600, 'f_wait')
    a.lea_lbl('m_conn', 3); a.br(0x6100, 'puts2')

    # ---- TCP_send(handle, requête, len) ------------------------------------
    req = f"GET / HTTP/1.0\r\nHost: {host}\r\n\r\n"
    a.w(0x3F3C, len(req))                         # move.w #len,-(sp)
    a.w(0x4879); a.abs32('s_req')                 # pea s_req
    a.w(0x3F06)                                   # move.w d6,-(sp)
    a.w(0x206D, TPL_TCP_SEND)                     # movea.l 48(a5),a0
    a.w(0x4E90); a.w(0x508F)                      # jsr ; addq.l #8,sp

    # ---- lecture : 500 caractères max, 20 s (émulées) max ------------------
    # Échéance en ms via TIMER_now (STX) : lisible en mode USER, contrairement
    # à $4BA — et c'est la même horloge sting_clock que le moteur.
    a.w(0x206E, STX_TIMER_NOW)                    # movea.l 72(a6),a0
    a.w(0x4E90)                                   # jsr (a0) → d0.l = ms
    a.w(0x2A00)                                   # move.l d0,d5
    a.w(0x0685); a.l32(20000)                     # addi.l #20000,d5
    a.w(0x383C, 500)                              # move.w #500,d4
    a.lbl('rd_loop')
    a.w(0x3F06)                                   # move.w d6,-(sp)
    a.w(0x206D, TPL_CNGET_CHAR)                   # movea.l 80(a5),a0
    a.w(0x4E90); a.w(0x548F)                      # jsr ; addq.l #2,sp
    a.w(0x4A40)                                   # tst.w d0
    a.br(0x6B00, 'rd_none')                       # bmi
    a.w(0x3200)                                   # move.w d0,d1
    a.w(0x0241, 0x00FF)                           # andi.w #$FF,d1
    a.br(0x6100, 'putc2')
    a.w(0x5344)                                   # subq.w #1,d4
    a.br(0x6600, 'rd_loop')
    a.br(0x6000, 'done')
    a.lbl('rd_none')
    a.w(0x0C40, E_NODATA & 0xFFFF)                # cmpi.w #E_NODATA,d0
    a.br(0x6600, 'done')                          # EOF/erreur : fini
    a.w(0x206E, STX_TIMER_NOW)                    # movea.l 72(a6),a0
    a.w(0x4E90)                                   # jsr (a0) → d0.l = ms
    a.w(0xB085)                                   # cmp.l d5,d0
    a.br(0x6D00, 'rd_loop')                       # blt : on attend encore
    a.lbl('done')
    a.lea_lbl('m_done', 3); a.br(0x6100, 'puts2')
    a.br(0x6000, 'quit')

    # ---- échecs : message + code décimal signé (d7) ------------------------
    # f_cookie est ENCORE en superviseur (seul chemin d'échec avant le retour
    # user) : il repasse en user avant de sortir.
    a.lbl('f_cookie')
    a.lea_lbl('m_fcookie', 3); a.br(0x6100, 'puts2')
    a.w(0x2F39); a.abs32('oldssp')
    a.w(0x3F3C, 0x0020); a.w(0x4E41); a.w(0x5C8F)  # Super(oldssp)
    a.br(0x6000, 'quit')
    for lbl, msg in [('f_tpl', 'm_ftpl'),
                     ('f_mod', 'm_fmod'), ('f_port', 'm_fport'), ('f_on', 'm_fon')]:
        a.lbl(lbl)
        a.lea_lbl(msg, 3); a.br(0x6100, 'puts2')
        a.br(0x6000, 'quit')
    for lbl, msg in [('f_dns', 'm_fdns'), ('f_tcp', 'm_ftcp'), ('f_wait', 'm_fwait')]:
        a.lbl(lbl)
        a.lea_lbl(msg, 3); a.br(0x6100, 'puts2')
        a.w(0x3007)                               # move.w d7,d0
        a.br(0x6100, 'psdec')
        # État du port au moment de l'échec : file d'émission ('Q' = datagrammes
        # coincés, '-' = vide), octets émis, datagrammes jetés — départage
        # « rien n'est routé » / « routé mais jamais émis » / « émis mais perdu ».
        a.lea_lbl('m_pstat', 3); a.br(0x6100, 'puts2')
        a.w(0x2C2C, 0x001C)                       # move.l 28(a4),d6  PORT.send
        a.w(0x323C, ord('-'))                     # move.w #'-',d1
        a.w(0x4A86)                               # tst.l d6
        a.br(0x6700, lbl + '_qe')
        a.w(0x323C, ord('Q'))
        a.lbl(lbl + '_qe')
        a.br(0x6100, 'putc2')
        a.w(0x323C, ord(' ')); a.br(0x6100, 'putc2')
        a.w(0x302C, 0x001A)                       # move.w 26(a4),d0  stat_sd bas-mot… (voir note)
        a.br(0x6100, 'psdec')
        a.w(0x323C, ord(' ')); a.br(0x6100, 'putc2')
        a.w(0x302C, 0x0028)                       # move.w 40(a4),d0  stat_dropped
        a.br(0x6100, 'psdec')
        a.lea_lbl('m_nl', 3); a.br(0x6100, 'puts2')
        a.br(0x6000, 'quit')

    # ---- épilogue (mode user depuis la lecture du cookie) ------------------
    a.lbl('quit')
    a.w(0x4267); a.w(0x4E41)                      # Pterm0

    # ---- sous-programmes ----------------------------------------------------
    # putc2 : d1.w → console (dev 2) ET RS-232 (dev 1). Le BIOS peut écraser
    # d0-d2/a0-a2, d'où le caractère resauvé entre les deux appels.
    a.lbl('putc2')
    a.w(0x3F01)                                   # move.w d1,-(sp)  (sauvegarde)
    a.w(0x3F01)                                   # move.w d1,-(sp)
    a.w(0x3F3C, 0x0002)                           # console
    a.w(0x3F3C, 0x0003)                           # Bconout
    a.w(0x4E4D); a.w(0x5C8F)                      # trap #13 ; addq.l #6,sp
    a.w(0x321F)                                   # move.w (sp)+,d1
    a.w(0x3F01)
    a.w(0x3F3C, 0x0001)                           # AUX (RS-232)
    a.w(0x3F3C, 0x0003)
    a.w(0x4E4D); a.w(0x5C8F)
    a.w(0x4E75)                                   # rts

    # puts2 : chaîne 0-terminée en a3 → putc2.
    a.lbl('puts2')
    a.w(0x121B)                                   # move.b (a3)+,d1
    a.br(0x6700, 'puts2_ret')
    a.w(0x0241, 0x00FF)                           # andi.w #$FF,d1
    a.br(0x6100, 'putc2')
    a.br(0x6000, 'puts2')
    a.lbl('puts2_ret'); a.w(0x4E75)

    # psdec : d0.w signé → décimal ('-' + valeur absolue). pdec : d0.w non signé.
    # Les chiffres sont poussés sur la pile puis dépilés (d3 = compte).
    a.lbl('psdec')
    a.w(0x4A40)                                   # tst.w d0
    a.br(0x6A00, 'pdec')                          # bpl
    a.w(0x3F00)                                   # move.w d0,-(sp)
    a.w(0x323C, ord('-'))                         # move.w #'-',d1
    a.br(0x6100, 'putc2')
    a.w(0x301F)                                   # move.w (sp)+,d0
    a.w(0x4440)                                   # neg.w d0
    a.lbl('pdec')
    a.w(0x7600)                                   # moveq #0,d3
    a.lbl('pd_loop')
    a.w(0x0280); a.l32(0xFFFF)                    # andi.l #$FFFF,d0
    a.w(0x80FC, 0x000A)                           # divu #10,d0
    a.w(0x4840)                                   # swap d0 (reste ↔ quotient)
    a.w(0x3F00)                                   # move.w d0,-(sp)  chiffre
    a.w(0x5243)                                   # addq.w #1,d3
    a.w(0x4240)                                   # clr.w d0
    a.w(0x4840)                                   # swap d0 (quotient en bas)
    a.w(0x4A40)                                   # tst.w d0
    a.br(0x6600, 'pd_loop')
    a.lbl('pd_out')
    a.w(0x321F)                                   # move.w (sp)+,d1
    a.w(0x0641, ord('0'))                         # addi.w #'0',d1
    a.br(0x6100, 'putc2')
    a.w(0x5343)                                   # subq.w #1,d3
    a.br(0x6600, 'pd_out')
    a.w(0x4E75)                                   # rts

    # ---- données ------------------------------------------------------------
    a.string('s_tpl', 'TRANSPORT_TCPIP')
    a.string('s_mod', 'MODULE_LAYER')
    a.string('s_enet', 'EtherNet')
    a.string('s_activate', 'ACTIVATE')
    a.string('s_host', host)
    a.string('s_req', req)
    a.string('m_start', '\r\nSTG: config EtherNet 10.0.2.15/24\r\n')
    a.string('m_up', 'STG: port up\r\n')
    a.string('m_act', 'STG: ACTIVATE=')
    a.string('m_route', 'STG: routes load/get0: ')
    a.string('m_pstat', ' port[queue sent dropped]=')
    a.string('m_dns', 'STG: resolving...\r\n')
    a.string('m_dnseq', 'DNS=')
    a.string('m_conn', 'STG: TCP connected\r\n')
    a.string('m_done', '\r\nSTG: done\r\n')
    a.string('m_nl', '\r\n')
    a.string('m_fcookie', 'STG: FAIL no STiK cookie\r\n')
    a.string('m_ftpl', 'STG: FAIL no TRANSPORT_TCPIP\r\n')
    a.string('m_fmod', 'STG: FAIL no MODULE_LAYER\r\n')
    a.string('m_fport', 'STG: FAIL no EtherNet port\r\n')
    a.string('m_fon', 'STG: FAIL on_port\r\n')
    a.string('m_fdns', 'STG: FAIL resolve err ')
    a.string('m_ftcp', 'STG: FAIL TCP_open err ')
    a.string('m_fwait', 'STG: FAIL TCP wait err ')
    a.lbl('oldssp'); a.w(0, 0)
    a.lbl('drvlist'); a.w(0, 0)
    a.lbl('rnet'); a.w(0, 0)
    a.lbl('rmask'); a.w(0, 0)
    a.lbl('rport'); a.w(0, 0)
    a.lbl('rgw'); a.w(0, 0)
    a.lbl('portsv'); a.w(0, 0)
    a.lbl('iplist'); a.w(0, 0)
    return a


def build_prg(a: Asm) -> bytes:
    # Même format que make_usatan_test.build_prg : en-tête $601A + TEXT + relocation.
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


# =============================================================================
#  Disquette FAT12 720 Ko avec arborescence (BPB identique à make_floppy.py).
#  `tree` : nom → liste [(fichier, contenu)] = DOSSIER ; nom → bytes = fichier
#  RACINE (EMUDESK.INF…). L'ORDRE des fichiers d'AUTO est l'ordre des entrées :
#  STING.PRG doit précéder STGTEST.PRG (le TOS exécute AUTO dans l'ordre du
#  répertoire).
# =============================================================================
def build_floppy(tree: dict) -> bytes:
    SPT, SIDES, TRACKS, SPC = 9, 2, 80, 2
    TOTAL = TRACKS * SIDES * SPT
    img = bytearray(TOTAL * SECT)
    img[0:2] = b'\x60\x1c'; img[2:8] = b'NeoST '; img[8:11] = b'STG'
    struct.pack_into('<H', img, 0x0B, SECT); img[0x0D] = SPC
    struct.pack_into('<H', img, 0x0E, 1); img[0x10] = 2
    struct.pack_into('<H', img, 0x11, 112); struct.pack_into('<H', img, 0x13, TOTAL)
    img[0x15] = 0xF9; struct.pack_into('<H', img, 0x16, 3)
    struct.pack_into('<H', img, 0x18, SPT); struct.pack_into('<H', img, 0x1A, SIDES)
    RES, NFAT, SPF, NDIRS = 1, 2, 3, 112
    fat1 = RES * SECT
    root = (RES + NFAT * SPF) * SECT
    data = root + ((NDIRS * 32 + SECT - 1) // SECT) * SECT
    csize = SPC * SECT

    def set_fat(idx, val):
        for base in (fat1, fat1 + SPF * SECT):
            off = base + idx * 3 // 2
            if idx & 1:
                img[off] = (img[off] & 0x0F) | ((val << 4) & 0xF0)
                img[off + 1] = (val >> 4) & 0xFF
            else:
                img[off] = val & 0xFF
                img[off + 1] = (img[off + 1] & 0xF0) | ((val >> 8) & 0x0F)
    set_fat(0, 0xFF9); set_fat(1, 0xFFF)

    nxt = [2]                                    # prochain cluster libre

    def alloc(nbytes: int) -> int:
        n = max(1, (nbytes + csize - 1) // csize)
        first = nxt[0]
        for i in range(n):
            set_fat(first + i, 0xFFF if i == n - 1 else first + i + 1)
        nxt[0] += n
        return first

    def cl_off(c): return data + (c - 2) * csize

    DATE = (46 << 9) | (8 << 5) | 27; TIME = (12 << 11)   # 2026-08-27 12:00 (fixe)

    def entry(buf, at, name, ext, attr, cluster, size):
        buf[at:at + 8] = name.ljust(8).encode()[:8]
        buf[at + 8:at + 11] = ext.ljust(3).encode()[:3]
        buf[at + 11] = attr
        struct.pack_into('<H', buf, at + 22, TIME)
        struct.pack_into('<H', buf, at + 24, DATE)
        struct.pack_into('<H', buf, at + 26, cluster)
        struct.pack_into('<I', buf, at + 28, size)

    slot = 0
    for dirname, files in tree.items():
        if isinstance(files, (bytes, bytearray)):          # fichier à la RACINE
            base, _, ext = dirname.upper().partition('.')
            fcl = alloc(len(files))
            img[cl_off(fcl):cl_off(fcl) + len(files)] = files
            entry(img, root + slot * 32, base, ext, 0x20, fcl, len(files))
            slot += 1
            continue
        dcl = alloc(csize)                       # 1 cluster = 32 entrées, assez
        entry(img, root + slot * 32, dirname.upper(), '', 0x10, dcl, 0)
        slot += 1
        at = cl_off(dcl)
        entry(img, at, '.', '', 0x10, dcl, 0)
        entry(img, at + 32, '..', '', 0x10, 0, 0)
        for i, (fname, content) in enumerate(files):
            base, _, ext = fname.upper().partition('.')
            fcl = alloc(len(content))
            img[cl_off(fcl):cl_off(fcl) + len(content)] = content
            entry(img, at + 64 + i * 32, base, ext, 0x20, fcl, len(content))
    return bytes(img)


def main() -> int:
    args = [a for a in sys.argv[1:] if a != '--cab']
    cab = '--cab' in sys.argv[1:]
    if len(args) < 3:
        print('usage: make_sting_test.py [--cab] OUT.st STING126_DIR ETHERNE_DIR [hôte]\n'
              '  --cab : ajoute A:\\EMUDESK.INF avec autostart #Z de C:\\CAB\\CAB.APP\n'
              '          (CAB 1.5 + CAB.OVL sur un disque GEMDOS C:, cf. docs/EXTENSIONS.md)')
        return 2
    out, sting_dir, enec_dir = args[0], args[1], args[2]
    host = args[3] if len(args) > 3 else 'theoldnet.com'

    def rd(*p) -> bytes:
        with open(os.path.join(*p), 'rb') as f:
            return f.read()

    # DEFAULT.CFG repris du paquet, NAMESERVER pointé sur le relais SLIRP et
    # cache DNS non écrit (déterminisme : la disquette ne doit pas changer).
    cfg = rd(sting_dir, 'sting', 'default.cfg').decode('latin-1')
    cfg = cfg.replace('NAMESERVER  = ', 'NAMESERVER  = 10.0.2.3')
    cfg = cfg.replace('DNS_SAVE    = TRUE', 'DNS_SAVE    = FALSE')
    route = ('# defaut : tout part au NAT SLIRP par le port EtherNet (ENEC.STX)\n'
             '0.0.0.0\t0.0.0.0\tEtherNet\t10.0.2.2\n')

    tree = {
        'AUTO': [
            ('STING.PRG', rd(sting_dir, 'auto', 'sting.prg')),      # ordre = exécution
            ('STING.INF', b'A:\\STING\\\r\n'),
            ('STGTEST.PRG', build_prg(build_code(host))),
        ],
        'STING': [
            ('TCP.STX', rd(sting_dir, 'sting', 'tcp.stx')),
            ('UDP.STX', rd(sting_dir, 'sting', 'udp.stx')),
            ('RESOLVE.STX', rd(sting_dir, 'sting', 'resolve.stx')),
            ('ENEC.STX', rd(enec_dir, 'ENEC.STX')),
            ('DEFAULT.CFG', cfg.encode('latin-1')),
            ('ROUTE.TAB', route.encode('ascii')),
        ],
    }
    if cab:
        # EmuDesk (EmuTOS) lit EMUDESK.INF à la racine du lecteur de boot ;
        # #Z 01 = autostart d'une application GEM après la fin de l'AUTO.
        tree['EMUDESK.INF'] = (
            '#Z 01 C:\\CAB\\CAB.APP@\r\n'
            '#M 00 00 00 FF A FLOPPY DISK@ @\r\n'
            '#M 01 00 00 FF C HARD DISK@ @\r\n'
            '#T 00 03 02 FF   TRASH@ @\r\n').encode('ascii')
    with open(out, 'wb') as f:
        f.write(build_floppy(tree))
    print(f'{out}: STinG 1.26 + ENEC.STX, cible {host} (verdict sur RS-232)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
