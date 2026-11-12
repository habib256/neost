#!/usr/bin/env python3
# =============================================================================
#  run_closure_intro_guard.py — Le grand logo de Closure (Sync) sort-il SANS trame isolée ?
#
#  Défaut gardé (2026-11-12) : pendant le logo X-DISTING (trames ~1300-2300), NeoST sortait
#  de temps en temps UNE trame noire ou brouillée entre deux trames normales. La démo se cale
#  sur la HBL par « stop #$2100 » ×2, puis compense la gigue E-clock de l'IACK par une file de
#  cinq instructions (une par phase, 160 256 ≡ 6 mod 10). Le saut d'attente STOP de NeoST
#  posait l'horloge du 68000 sur l'échéance du Timer D (période 26,08 cycles, souvent impaire) :
#  le réveil HBL suivant tombait un cycle trop tard, l'IACK quatre cycles trop tard, la file
#  ne compensait plus, et la trame décrochait. Détail : docs/CLOSURE_CHANTIER.md § Cycle 7.
#
#  Deux verdicts sur la même exécution, contre NeoST seul (aucun oracle requis) :
#   1. INVARIANT interne de la démo : l'écriture $FF820A de pc=$2061A (ligne 33) tombe au
#      cycle 468 à CHAQUE trame — c'est aussi ce que montre Hatari (513 trames sur 513,
#      --trace video_sync @ f0736b24). Un écart de ±4/8 cycles est la cause directe d'une
#      trame fautive (mesuré en TOS 1.02 UK : chaque trame fautive suit un écart ; 86 écarts
#      sur 1 010 trames avant correction, aucun après).
#   2. PIXEL : capture trame par trame de la fenêtre, une trame est fautive si elle est
#      quasi noire (< 15 % de la médiane locale) ou si la bande gauche — ouverte par le
#      retrait de bordure — s'éteint (< 25 % de la médiane locale) alors que ses voisines
#      l'ont. La fenêtre évite les flashs voulus de la démo (paires sombres à période 4,
#      présentes chez Hatari aussi).
#
#  ROM LIBRE (EmuTOS 192 Ko FR), disquette versionnée : rien de propriétaire.
#  Usage : python3 tools/run_closure_intro_guard.py [--from A] [--to B]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import argparse
import os
import re
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_EXE = ".exe" if sys.platform == "win32" else ""
HEADLESS = ROOT / "build" / ("neost-headless" + _EXE)
ROM = "roms/etos192fr.img"
DISK = "disks/etalons/closure.msa"
EXIT_SKIPPED = 77
# Fenêtre par défaut (EmuTOS 192 FR, FDC normal) : choisie après les flashs voulus du logo,
# où l'ancien saut STOP faisait 36 trames fautives sur 301 (0 depuis). 8 s au repos, ~37 s machine chargée.
FROM, TO = 1700, 2000
SYNC_PC, SYNC_LINE, SYNC_CYC = "02061a", 33, 468


def parse_p6(data: bytes) -> tuple[int, int, bytes]:
    fields, pos = [], 2
    while len(fields) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while data[end:end + 1].isdigit():
            end += 1
        fields.append(int(data[pos:end]))
        pos = end
    return fields[0], fields[1], data[pos + 1:]


def features(path: Path) -> tuple[float, float]:
    """(moyenne de toute l'image, moyenne de la bande gauche x<24, y 40..229)."""
    w, h, px = parse_p6(path.read_bytes())
    total = sum(px[:3 * w * h]) / (3 * w * h)
    strip = 0
    y0, y1 = (40 * h) // 276, (230 * h) // 276          # repère 416x276 (buffer moitié)
    sw = (24 * w) // 416
    for y in range(y0, y1):
        off = 3 * w * y
        strip += sum(px[off:off + 3 * sw])
    return total, strip / (3 * sw * (y1 - y0))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="a", type=int, default=FROM)
    ap.add_argument("--to", dest="b", type=int, default=TO)
    ap.add_argument("--rom", default=ROM, help="autre ROM (diagnostic ; la garde reste sur EmuTOS)")
    ap.add_argument("--fastfdc", action="store_true", help="FDC rapide (diagnostic)")
    args = ap.parse_args()
    if not HEADLESS.exists():
        print("SKIP : neost-headless non bâti", file=sys.stderr)
        return EXIT_SKIPPED
    if not (ROOT / args.rom).exists():
        print(f"SKIP : {args.rom} absent", file=sys.stderr)
        return EXIT_SKIPPED
    t0 = time.time()
    with tempfile.TemporaryDirectory() as tmp:
        prefix = str(Path(tmp) / "c")
        cmd = [str(HEADLESS), args.rom, "--machine", "st", "--mem", "1m", "--disk", DISK, "--disk-ro",
               *(["--fastfdc"] if args.fastfdc else []), "--frames", str(args.b), "--shot-from", str(args.a), "--shot-every", "1", prefix]
        env = dict(os.environ, NEOST_SYNC_TRACE="1")
        p = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.DEVNULL,
                             stderr=subprocess.PIPE, text=True, errors="replace")
        feats: dict[int, tuple[float, float]] = {}
        sync_off: list[int] = []
        nsync = 0
        rx = re.compile(r"line=(\d+) cyc=(\d+) pc=" + SYNC_PC)
        # Les PPM sont lus au fil de l'eau par un second fil (la dernière capture peut être
        # en cours d'écriture) : la fenêtre complète tiendrait ~100 Mo sur le disque.
        done = threading.Event()

        def drain() -> None:
            while True:
                last = done.is_set()
                files = sorted(Path(tmp).glob("c*.ppm"))
                for f in (files if last else files[:-1]):
                    feats[int(f.stem[1:])] = features(f)
                    f.unlink()
                if last:
                    return
                time.sleep(0.2)
        th = threading.Thread(target=drain)
        th.start()
        for line in p.stderr:
            if SYNC_PC not in line:
                continue
            m = rx.search(line)
            if m and int(m.group(1)) == SYNC_LINE:
                nsync += 1
                if int(m.group(2)) != SYNC_CYC:
                    sync_off.append(int(m.group(2)))
        p.wait()
        done.set()
        th.join()
    if p.returncode != 0:
        print(f"ÉCHEC : neost-headless a rendu {p.returncode}", file=sys.stderr)
        return 1
    ks = sorted(feats)
    bad = []
    for i in range(10, len(ks) - 10):
        win = ks[i - 10:i + 11]
        ma = statistics.median(feats[g][0] for g in win)
        ms = statistics.median(feats[g][1] for g in win)
        a, s = feats[ks[i]]
        if a < 0.15 * ma:
            bad.append(f"{ks[i]} (noire)")
        elif s < 0.25 * ms:
            bad.append(f"{ks[i]} (bordure gauche revenue)")
    dt = time.time() - t0
    print(f"Closure, logo X-DISTING : {len(ks)} trames capturées ({args.a}-{args.b}), "
          f"{nsync} écritures $FF820A ligne {SYNC_LINE} relevées depuis le boot, {dt:.0f} s")
    ok = True
    if len(ks) < (args.b - args.a) // 2:
        print(f"✗ captures manquantes ({len(ks)})")
        ok = False
    if nsync < 50:
        print(f"✗ l'écriture pc=${SYNC_PC} n'apparaît que {nsync} fois : la démo n'a pas "
              "atteint le logo dans la fenêtre (chargement plus long ? fenêtre à revoir)")
        ok = False
    if sync_off:
        print(f"✗ {len(sync_off)} trames où l'écriture ligne {SYNC_LINE} quitte le cycle "
              f"{SYNC_CYC} : {sorted(set(sync_off))}")
        ok = False
    if bad:
        print(f"✗ {len(bad)} trames fautives : {', '.join(bad)}")
        ok = False
    if ok:
        print("✓ aucune trame isolée, calage de la démo constant")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
