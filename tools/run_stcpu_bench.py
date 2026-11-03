#!/usr/bin/env python3
# =============================================================================
#  run_stcpu_bench.py — Ce que le CPU voit du faisceau : NeoST contre fx68k + GSTMCU.
#
#  POURQUOI. Toute la synchro des démos repose sur la relation entre le 68000 et le balayage, jusqu'ici
#  calée sur Hatari. Le même programme tourne sur les deux RTL (échelon `rtl`, blocs « forts », grade D)
#  et dans NeoST ; il relève $FF8209 en RAM, et on compare les tables à l'octet. STE seulement.
#
#  Usage : run_stcpu_bench.py [--rebuild] [--write-refs]
#  Code : 0 si les écarts égalent tests/reference/stcpu_bench.json ; 1 sinon ; 77 sans Verilator ou sans
#  les RTL. Détail : docs/STCPU_BENCH.md.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_stcpu_bench as builder  # noqa: E402
import make_stcpu_rom as rom  # noqa: E402
import make_stcpu_snd as snd  # noqa: E402

HEADLESS = ROOT / "build" / "neost-headless"
OUT = ROOT / "tests" / "out" / "stcpu_bench"
REF = ROOT / "tests" / "reference" / "stcpu_bench.json"
FRAMES = 22       # jusqu'au remplissage de la dernière table
LOOPS = ("nop", "nops", "stop", "mulu")


def tables(a: bytes, b: bytes) -> dict:
    res = {}
    for k in range(3):
        lo, hi = k * rom.POLL_LEN, (k + 1) * rom.POLL_LEN
        res[f"poll{k}"] = [i for i in range(lo, hi) if a[i] != b[i]]
    base = rom.HBLTAB - rom.POLL0
    for k, name in enumerate(LOOPS):
        lo = base + k * 256
        res[f"hbl_{name}"] = [i - lo for i in range(lo, lo + 256) if a[i] != b[i]]
    base = rom.VBLTAB - rom.POLL0
    res["vbl"] = [i - base for i in range(base, base + 32) if a[i] != b[i]]
    return res


def dma_steps(table: bytes) -> list[int]:
    """Phases dans la ligne où $FF890D change : 24 cycles par lecture, 512 par ligne."""
    return sorted({(i * 24) % 512 // 24 * 24 for i in range(1, len(table)) if table[i] != table[i - 1]})


def check_dma_sound() -> bool:
    """Le son DMA cherche ses mots hors de DE : aucune marche du compteur pendant 300 cycles de suite. La valeur
    elle-même n'est pas comparée : le modèle a une FIFO de 7 mots par choix d'auteur (gstshifter.v:414-419)."""
    image = OUT / "snd.img"
    image.write_bytes(snd.build(3, 0))
    a, b = OUT / "snd_rtl.bin", OUT / "snd_neost.bin"
    subprocess.run([str(builder.BINARY), str(image), "12", f"{snd.TABLE:x}", str(snd.POLL_LEN), str(a)],
                   check=True, capture_output=True, timeout=600, cwd=builder.BUILD)
    subprocess.run([str(HEADLESS), str(image), "--machine", "ste", "--mem", "4m", "--frames", "13", "--disk",
                    "disks/diskA.st", "--dump-at", "11", f"{snd.TABLE:x}", str(snd.POLL_LEN), str(b)],
                   check=True, capture_output=True, timeout=600, cwd=ROOT)
    ok = True
    for name, path in (("RTL", a), ("NeoST", b)):
        steps = dma_steps(path.read_bytes())
        gaps = [(y - x) for x, y in zip(steps, steps[1:])] + [steps[0] + 512 - steps[-1]]
        print(f"  son DMA    {name:5s} : marches sur {len(steps)} phases, plus grand trou {max(gaps)} cycles")
        ok = ok and max(gaps) >= 288
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rebuild", action="store_true")
    ap.add_argument("--write-refs", action="store_true")
    a = ap.parse_args()
    if not shutil.which("verilator"):
        print("SKIP — banc CPU + GSTMCU : verilator absent")
        return 77
    if not (builder.FX / "fx68k.sv").exists() or not (builder.G / "tb" / "ste_tb.v").exists():
        print("SKIP — banc CPU + GSTMCU : RTL absents (tools/setup_silicon_refs.sh)")
        return 77
    if a.rebuild or not builder.BINARY.exists():
        builder.build()
    OUT.mkdir(parents=True, exist_ok=True)
    res = {}
    # (nom, 50 Hz ?, trames, NEOST_ECLOCK_PHASE). La phase de l'E-clock à la sortie du reset est une constante
    # libre entre le modèle et NeoST, comme le tirage au reset d'une machine réelle. Sa valeur se cherche une
    # fois, en balayant : une seule donne ces tables, et les autres en cassent des milliers de lectures.
    for tag, hz50, frames, phase in (("50hz", True, FRAMES, 0), ("60hz", False, 24, 8)):
        image = OUT / f"rom_{tag}.img"
        image.write_bytes(rom.build(hz50))
        ra, nb = OUT / f"rtl_{tag}.bin", OUT / f"neost_{tag}.bin"
        subprocess.run([str(builder.BINARY), str(image), str(frames), f"{rom.POLL0:x}", str(rom.TOTAL), str(ra)],
                       check=True, capture_output=True, timeout=600, cwd=builder.BUILD)
        subprocess.run([str(HEADLESS), str(image), "--machine", "ste", "--mem", "4m", "--frames", str(frames + 1),
                        "--disk", "disks/diskA.st", "--dump-at", str(frames - 1), f"{rom.POLL0:x}", str(rom.TOTAL), str(nb)],
                       check=True, capture_output=True, timeout=600, cwd=ROOT, env=dict(os.environ, NEOST_ECLOCK_PHASE=str(phase)))
        A, B = ra.read_bytes(), nb.read_bytes()
        if len(A) != rom.TOTAL or len(B) != rom.TOTAL:
            print("FAIL banc CPU + GSTMCU : table incomplète")
            return 1
        for name, bad in tables(A, B).items():
            res[f"{tag}_{name}"] = bad
            total = rom.POLL_LEN if name.startswith("poll") else (32 if name == "vbl" else 256)
            print(f"  {tag} {name:10s} {total - len(bad):6d}/{total} lectures identiques")
    if not check_dma_sound():
        print("FAIL banc CPU + GSTMCU : le compteur du son DMA bouge pendant DE")
        return 1
    if a.write_refs:
        REF.write_text(json.dumps(res, indent=0, sort_keys=True) + "\n")
        print(f"référence écrite : {REF}")
        return 0
    if not REF.exists():
        print("Pas de référence : un écart fait échouer.")
        return 1 if any(res.values()) else 0
    if json.loads(REF.read_text()) != json.loads(json.dumps(res)):
        print("BANC CPU + GSTMCU : verdicts changés")
        return 1
    print("BANC CPU + GSTMCU OK : verdicts identiques à la référence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
