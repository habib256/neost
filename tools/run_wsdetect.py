#!/usr/bin/env python3
# =============================================================================
#  run_wsdetect.py — Le détecteur de wakestate de Troed/SYNC juge la GLUE, sans Hatari.
#
#  POURQUOI. WSDETECT.S compose une seule ligne avec quatre bascules de fréquence et de résolution, et
#  mesure ce qu'elle consomme par le compteur vidéo. Son source donne le résultat par machine : 162, 54,
#  206 et 204 octets en WS1 à WS4, 180 sur STE. Tout est en jeu d'un coup, grille de la GLUE, dates
#  d'écriture et lecture du compteur. Échelon `rom` : le programme vient d'un auteur qui l'a écrit pour le
#  fer, mais NeoST n'a pas de relevé de ce disque sur machine réelle.
#
#  COMMENT. Lance disks/wsdetect.st (CC0) sous EmuTOS, lit D4 dans la trace à la première comparaison du
#  programme (cmp.w #54,d4), et arrête le run là. Sans ROM : SKIP (77).
#
#  Usage : run_wsdetect.py
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "neost-headless"
DISK = "disks/wsdetect.st"
ST_ROM, STE_ROM = "roms/etos192fr.img", "roms/etos256fr.img"
# (nom, machine, ROM, wakestate, octets attendus d'après WSDETECT.S, commentaire de _detect_ws)
CASES = [("st_ws1", "st", ST_ROM, 1, 162), ("st_ws2", "st", ST_ROM, 2, 54), ("st_ws3", "st", ST_ROM, 3, 206),
         ("st_ws4", "st", ST_ROM, 4, 204), ("megast_defaut", "megast", ST_ROM, 0, 206),
         ("ste", "ste", STE_ROM, 0, 180), ("megaste", "megaste", STE_ROM, 0, 180)]
MATCH = re.compile(r"cmp\.w\s+#\$36,\s*D4")


def measure(case) -> tuple[str, int, int | None]:
    name, machine, rom, ws, want = case
    env = dict(os.environ)
    if ws:
        env["NEOST_WS"] = str(ws)
    cmd = [str(HEADLESS), rom, "--machine", machine, "--mem", "1m", "--disk", DISK, "--auto", "A:\\WSDETECT.PRG",
           "--frames", "1500", "--trace", "-", "--regs"]
    with subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                          text=True, errors="replace") as proc:
        assert proc.stdout
        try:
            for line in proc.stdout:
                if MATCH.search(line):
                    return name, want, int(re.search(r"D4=([0-9A-F]+)", line).group(1), 16)
        finally:
            proc.kill()
    return name, want, None


def main() -> int:
    if not HEADLESS.exists() or not (ROOT / ST_ROM).exists() or not (ROOT / STE_ROM).exists():
        print("SKIP — wsdetect : neost-headless ou ROM EmuTOS absents")
        return 77
    with ThreadPoolExecutor(4) as ex:
        rows = list(ex.map(measure, CASES))
    bad = 0
    for name, want, got in rows:
        ok = got == want
        bad += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {name} : {got} octets, attendu {want}")
    print("WSDETECT OK" if not bad else "WSDETECT EN ÉCHEC")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
