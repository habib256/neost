#!/usr/bin/env python3
# =============================================================================
#  check_hog_park.py — un blit HOG de plusieurs trames rend la main à chaque trame
#
#  Un programme lance un blit HOG d'environ cinq trames sur Mega STE (EmuTOS US,
#  dossier GEMDOS + autostart). Pendant le blit, l'instruction qui l'a lancé est
#  GARÉE (core/EmuFiber) : runFrame rend la main à chaque fin de trame au lieu de
#  la fin de l'instruction. Contrôles :
#    1. des trames garées existent (« state-hash frame=N parked ») ;
#    2. l'émulation est identique à l'exécution d'un seul tenant (NEOST_NO_FIBER=1) :
#       mêmes empreintes avant le blit, et chaque empreinte prise hors instruction
#       garée se retrouve dans la suite d'un seul tenant (décalée des trames que
#       l'appel unique a avalées) ;
#    3. une VBL par trame pendant le blit (passage de trame à l'heure) ;
#    4. --save-state pendant une instruction garée est refusé proprement, et la
#       sortie (destruction de la Machine) déroule l'instruction sans planter ;
#    5. un reset à chaud pendant une instruction garée l'abandonne ; le TOS redémarre,
#       relance le programme, et ce second blit va à son terme.
#
#  Usage : check_hog_park.py   (code 1 sur échec)
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / ("neost-headless.exe" if os.name == "nt" else "neost-headless")
ROM = ROOT / "roms" / "etos256us.img"
FRAMES = 360


def hog_prg(path: Path, xcount: int, ycount: int) -> None:
    """PRG superviseur : blit HOG xcount×ycount mots, puis compteur en $600."""
    code: list[int] = []
    w = code.extend
    w([0x42A7, 0x3F3C, 0x0020, 0x4E41, 0x5C8F])        # Super(0)
    w([0x41F9, 0x00FF, 0x8A00])                         # lea $FF8A00,a0
    def mvw(v, off): w([0x317C, v & 0xFFFF, off])
    def mvl(v, off): w([0x217C, (v >> 16) & 0xFFFF, v & 0xFFFF, off])
    def mvb(v, off): w([0x117C, v & 0xFF, off])
    mvw(2, 0x20); mvw(2, 0x22); mvl(0x080000, 0x24)
    mvw(0xFFFF, 0x28); mvw(0xFFFF, 0x2A); mvw(0xFFFF, 0x2C)
    mvw(2, 0x2E); mvw(2, 0x30); mvl(0x0C0000, 0x32)
    mvw(xcount, 0x36); mvw(ycount, 0x38)
    mvb(2, 0x3A); mvb(3, 0x3B); mvb(0, 0x3D)
    w([0x4E71]); mvb(0xC0, 0x3C); w([0x4E71])           # BUSY|HOG
    w([0x52B8, 0x0600, 0x60FA])                         # addq.l #1,$600.w ; bra.s
    body = b"".join(struct.pack(">H", x) for x in code)
    hdr = struct.pack(">HIIIIIIH", 0x601A, len(body), 0, 0, 0, 0, 0, 0)
    path.write_bytes(hdr + body + b"\0\0\0\0")


def run(gem: Path, extra: list[str], env: dict | None = None) -> subprocess.CompletedProcess:
    cmd = [str(HEADLESS), str(ROM), "--machine", "megaste", "--gemdos", str(gem),
           "--auto", "C:\\HOGBLIT.PRG", *extra]
    e = dict(os.environ)
    e.pop("NEOST_NO_FIBER", None)
    if env: e.update(env)
    return subprocess.run(cmd, capture_output=True, text=True, env=e, timeout=300)


def hashes(out: str) -> list[str]:
    return [ln.split()[2] for ln in out.splitlines() if ln.startswith("state-hash ")]


def main() -> int:
    if not HEADLESS.exists() or not ROM.exists():
        print(f"SKIP : {HEADLESS.name} ou {ROM.name} absent", file=sys.stderr)
        return 1
    fails: list[str] = []
    def check(ok: bool, what: str) -> None:
        print(f"[hog-park] {what:<62} {'OK' if ok else 'FAIL'}")
        if not ok: fails.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        gem = Path(tmp) / "gem"; gem.mkdir()
        hog_prg(gem / "HOGBLIT.PRG", 4096, 20)
        fib = run(gem, ["--frames", str(FRAMES), "--state-hash-every", "1"],
                  {"NEOST_VBL_TRACE": "1"})
        one = run(gem, ["--frames", str(FRAMES), "--state-hash-every", "1"],
                  {"NEOST_NO_FIBER": "1"})
        check(fib.returncode == 0 and one.returncode == 0, "les deux exécutions aboutissent")
        hf, ho = hashes(fib.stdout), hashes(one.stdout)
        parked = [i for i, h in enumerate(hf) if h == "parked"]
        check(len(parked) >= 3, f"instruction garée pendant le blit ({len(parked)} trames)")
        first = parked[0] if parked else 0
        check(hf[:first] == ho[:first] and first > 300, "empreintes identiques avant le blit")
        live = [h for h in hf if h != "parked"]
        check(bool(live) and all(h in set(ho) for h in live),
              "chaque empreinte hors garage existe dans l'exécution d'un seul tenant")
        tail = live[first:]
        k = ho.index(tail[0]) if tail and tail[0] in ho else -1
        check(k >= 0 and ho[k:k + len(tail)] == tail[:len(ho) - k],
              "après le blit, même suite d'états (décalée)")
        vbls = sum(1 for ln in fib.stderr.splitlines() if ln.startswith("[vbl]"))
        late = [ln for ln in fib.stderr.splitlines()
                if ln.startswith("[vbl]") and int(ln.split()[1].split("=")[1]) > 200]
        check(vbls == FRAMES and not late, f"une VBL à l'heure par trame ({vbls}, {len(late)} en retard)")

        stop = first + 2       # la run s'arrête instruction garée
        st = Path(tmp) / "parked.state"
        sv = run(gem, ["--frames", str(stop), "--save-state", str(st)])
        check(sv.returncode == 1 and "halted by a blitter transfer" in sv.stderr
              and not st.exists(), "--save-state refusé (motif dit) pendant une instruction garée")
        check("parked instruction abandoned" in sv.stderr,
              "sortie : l'instruction garée est déroulée, sans plantage")

        rs = run(gem, ["--frames", "900", "--reset-at", str(first + 2),
                       "--probe", "cnt=600:4", "--probe-every", "50",
                       "--state-hash-every", "50"])
        check(rs.returncode == 0 and "parked instruction abandoned" in rs.stderr,
              "reset à chaud : instruction garée abandonnée")
        # Après le reset, EmuTOS redémarre et relance le programme (autostart) : un
        # second blit HOG, garé puis mené à terme, prouve que le cœur et le blitter
        # sont sains après l'abandon (garde de tranche baissée, bloc CPU clos).
        served = rs.stderr.count("served for C:\\HOGBLIT.PRG")
        check(served == 2, f"le TOS redémarre et relance le programme ({served} lancements)")
        probes = [ln for ln in rs.stdout.splitlines() if ln.startswith("probe ")]
        check(bool(probes) and "cnt=0x00000000" not in probes[-1],
              "le second blit se termine et le programme reprend")

    print(f"[hog-park] {'ÉCHEC : ' + ', '.join(fails) if fails else 'tout est vert'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
