#!/usr/bin/env python3
# =============================================================================
#  check_server_media.py — Garde des commandes serveur « insert / eject » et de
#  l'option --drive-b off (demandes des bancs TOS File Cmd, 2026-10-01).
#
#  Ce qu'on vérifie, c'est ce que voit le TOS, pas seulement le FDC. Le changement à
#  chaud a un piège : le TOS ne le remarque qu'à la bascule du bit WPRT, qu'il lit une
#  VBL sur 8 en alternant les lecteurs (EmuTOS flopvbl), soit toutes les 16 VBL pour A.
#  Une fenêtre de transition trop courte (4 trames avant le 2026-10-01, 18 VBL chez
#  Hatari) faisait relister l'ancien contenu après un « insert ».
#
#  Le témoin : C:\AUTO\MEDIA.PRG (disque hôte --gemdos) fait en boucle
#  Fsfirst("A:\AUTO\*.*") et écrit le nom trouvé (ou « ERR ») dans C:\MEDIA.TXT,
#  que ce script relit sur l'hôte. Deux disquettes générées, AUTO\DISKONE.PRG et
#  AUTO\DISKTWO.PRG, se distinguent ainsi par leur seul contenu.
#
#    1. boot avec DISKONE en A                      → DISKONE.PRG
#    2. insert A DISKTWO                            → DISKTWO.PRG   (changement vu)
#    3. eject A                                     → ERR           (lecteur vide)
#    4. insert A DISKONE                            → DISKONE.PRG   (réinsertion)
#    + « hello » suit les médias, les refus de syntaxe et d'image laissent le lecteur
#      intact, --drive-b off → _nflops ($4A6) = 1 et « insert B » refusé ; défaut = 2.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from make_usatan_test import Asm, build_floppy  # noqa: E402

HEADLESS = ROOT / "build" / ("neost-headless.exe" if os.name == "nt" else "neost-headless")
ROM = ROOT / "roms" / "etos192us.img"


def media_prg() -> bytes:
    a = Asm(0)
    a.lbl('loop')
    a.w(0x4879); a.abs32('dta'); a.w(0x3F3C, 0x001A, 0x4E41, 0x5C8F)   # Fsetdta(dta)
    a.w(0x4267); a.w(0x4879); a.abs32('spec')                         # Fsfirst(spec, 0)
    a.w(0x3F3C, 0x004E, 0x4E41, 0x508F)
    a.lea_lbl('s_err', 3)
    a.w(0x4A80); a.br(0x6600, 'write')                                # tst.l d0 ; bne → « ERR »
    a.lea_lbl('dta', 3); a.w(0x47EB, 0x001E)                          # lea dta+30,a3 (nom)
    a.lbl('write')
    a.w(0x4267); a.w(0x4879); a.abs32('out')                          # Fcreate(out, 0)
    a.w(0x3F3C, 0x003C, 0x4E41, 0x508F)
    a.w(0x4A80); a.br(0x6B00, 'wait')                                 # échec → on retente plus tard
    a.w(0x3E00)                                                       # move.w d0,d7
    a.w(0x204B, 0x7CFF)                                               # move.l a3,a0 ; moveq #-1,d6
    a.lbl('slen'); a.w(0x5286, 0x4A18); a.br(0x6600, 'slen')          # strlen → d6
    a.w(0x2F0B, 0x2F06, 0x3F07, 0x3F3C, 0x0040, 0x4E41, 0x4FEF, 0x000C)   # Fwrite(d7, d6, a3)
    a.w(0x3F07, 0x3F3C, 0x003E, 0x4E41, 0x588F)                       # Fclose(d7)
    a.lbl('wait')
    # Un accès toutes les ~0,7 s, PAS plus vite : EmuTOS flop_mediach répond « pas de
    # changement » (et efface le loquet WPRT) si le dernier accès date de < 0,5 s alors
    # que WPRT est encore haut — un programme qui martèle le lecteur pendant l'échange
    # peut le rater, sur un vrai ST comme sous Hatari.
    a.w(0x3A3C, 0x0028)                                               # move.w #40,d5
    a.lbl('vs'); a.w(0x3F3C, 0x0025, 0x4E4E, 0x548F); a.dbra(5, 'vs')  # Vsync ×41
    a.br(0x6000, 'loop')
    a.string('spec', 'A:\\AUTO\\*.*')
    a.string('out', 'C:\\MEDIA.TXT')
    a.string('s_err', 'ERR')
    a.lbl('dta'); a.w(*([0] * 22))
    text = a.assemble()
    import struct
    reloc = bytearray(struct.pack('>I', a.relocs[0]))
    prev = a.relocs[0]
    for r in a.relocs[1:]:
        d = r - prev
        while d > 254:
            reloc.append(1); d -= 254
        reloc.append(d); prev = r
    reloc.append(0)
    return struct.pack('>HIIIIIIH', 0x601A, len(text), 0, 0, 0, 0, 0, 0) + text + bytes(reloc)


class Session:
    def __init__(self, *args):
        self.p = subprocess.Popen([str(HEADLESS), str(ROM), "--machine", "st", *args, "--server"],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, bufsize=1)

    def cmd(self, line: str) -> str:
        self.p.stdin.write(line + "\n"); self.p.stdin.flush()
        r = self.p.stdout.readline().strip()
        if not r:
            raise RuntimeError(f"{line!r}: no answer (emulator died)")
        return r

    def close(self):
        try:
            self.cmd("quit")
        finally:
            self.p.wait(timeout=30)


def main() -> int:
    if not HEADLESS.exists():
        print(f"neost-headless absent ({HEADLESS})"); return 1
    work = Path(tempfile.mkdtemp(prefix="neost-media-"))
    fails = []

    def check(what, ok, got=""):
        print(f"  {'ok  ' if ok else 'FAIL'} {what}" + ("" if ok else f" — got {got!r}"))
        if not ok:
            fails.append(what)

    try:
        hd = work / "hd"; (hd / "AUTO").mkdir(parents=True)
        (hd / "AUTO" / "MEDIA.PRG").write_bytes(media_prg())
        one, two = work / "ONE.ST", work / "TWO.ST"
        # Numéros de série DISTINCTS (secteur de boot, octets 8-10), comme deux vraies
        # disquettes formatées : sans ça, secteur de boot et FAT sont identiques et
        # flop_mediach conclut légitimement « même disquette ».
        for path, name, serial in ((one, "DISKONE", b"\x11\x22\x33"), (two, "DISKTWO", b"\x44\x55\x66")):
            img = bytearray(build_floppy(b"\x00" * 16, name))
            img[8:11] = serial
            path.write_bytes(bytes(img))
        out = hd / "MEDIA.TXT"

        def seen(s: Session, frames: int) -> str:
            out.unlink(missing_ok=True)
            s.cmd(f"run {frames}")
            return out.read_text(errors="replace") if out.exists() else "(no MEDIA.TXT)"

        print("Disquette changée à chaud (insert/eject), vue par le TOS :")
        s = Session("--gemdos", str(hd), "--disk", str(one))
        try:
            got = seen(s, 400);                       check("boot : DISKONE en A", got == "DISKONE.PRG", got)
            r = s.cmd(f"insert A {two}");             check("insert A → ok", r == f"ok drive=A path={two}", r)
            check("hello annonce DISKTWO", f" disk={two} diskb=- " in s.cmd("hello"))
            got = seen(s, 200);                       check("le TOS voit DISKTWO", got == "DISKTWO.PRG", got)
            for bad, want in ((f"insert A {work / 'nope.st'}", "err cannot mount"),
                              ("insert Q x", "err insert expects"), ("insert A", "err insert expects"),
                              ("eject", "err eject expects"), ("eject A x", "err eject expects")):
                r = s.cmd(bad);                       check(f"{bad!r} refusé", r.startswith(want), r)
            got = seen(s, 100);                        check("refus : DISKTWO toujours là", got == "DISKTWO.PRG", got)
            r = s.cmd("eject A");                     check("eject A → ok", r == "ok drive=A path=-", r)
            check("hello annonce A vide", " disk=- diskb=- " in s.cmd("hello"))
            got = seen(s, 600);                       check("le TOS voit A vide", got == "ERR", got)
            s.cmd(f"insert A {one}")
            got = seen(s, 200);                       check("réinsertion : DISKONE", got == "DISKONE.PRG", got)
            # Toutes les PHASES : le TOS ne lit WPRT de A qu'une VBL sur 16, donc un seul
            # échange peut réussir par chance. 16 échanges décalés d'une trame chacun
            # couvrent toutes les phases — l'ancienne fenêtre de 4 trames en ratait.
            missed = []
            for k in range(16):
                s.cmd(f"run {k + 1}")
                disk, name = (two, "DISKTWO.PRG") if k % 2 == 0 else (one, "DISKONE.PRG")
                s.cmd(f"insert A {disk}")
                got = seen(s, 200)
                if got != name:
                    missed.append(k)
            check("16 échanges, toutes phases VBL : tous vus", not missed, f"ratés aux décalages {missed}")
        finally:
            s.close()

        print("Lecteur B débranché (--drive-b off) :")
        for flag, want in (((), "ok 0002"), (("--drive-b", "off"), "ok 0001")):
            s = Session(*flag)
            try:
                s.cmd("run 300")
                r = s.cmd("peek 4A6 2");              check(f"_nflops {' '.join(flag) or '(défaut)'} → {want[3:]}", r == want, r)
                if flag:
                    r = s.cmd(f"insert B {two}");     check("insert B refusé", r.startswith("err drive B is disconnected"), r)
                    check("hello dit driveb=off", " driveb=off " in s.cmd("hello"))
            finally:
                s.close()
    finally:
        shutil.rmtree(work, ignore_errors=True)

    print("TOUS OK" if not fails else f"{len(fails)} ÉCHEC(S)")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
