#!/usr/bin/env python3
# =============================================================================
#  compare_titles.py — Passe sur de vrais jeux et démos : NeoST contre l'Hatari épinglé.
#
#  POURQUOI. Les étalons figent un écran par titre. Cet outil balaie une liste de disques à
#  plusieurs trames, pour voir une divergence là où aucun étalon ne regarde. C'est un outil de
#  repérage : il compare à Hatari (grade D) et ne prouve aucune fidélité.
#
#  COMMENT. Hatari tire au hasard la position angulaire de la disquette : ses numéros de trame
#  ne s'alignent pas sur ceux de NeoST. Pour chaque trame NeoST, on cherche donc l'image Hatari
#  la plus proche dans une fenêtre autour de cette trame. 0 % = la même image existe chez
#  Hatari ; un fort pourcentage reste à regarder, car une animation lente ou aléatoire peut
#  l'expliquer. La planche montre NeoST à gauche et sa meilleure image Hatari à droite.
#
#  Usage : compare_titles.py SORTIE DISQUE... [--machine ste] [--rom roms/tos162uk.img]
#            [--mem 1] [--frames 1000,2000,4000] [--window 60]
#            [--auto A:\\PROG.PRG] [--jobs 4]
#  Sorties : SORTIE/summary.json, SORTIE/sheet.png. Exige Pillow et ffmpeg ; code 77 sinon.
#  Les disques sont copiés avant chaque exécution : un programme peut y écrire.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from functools import reduce
from math import gcd
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw
except ImportError:
    print("SKIP — compare_titles : Pillow absent")
    sys.exit(77)

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "neost-headless"
HATARI = ROOT / "extern" / "hatari" / "build" / "src" / "hatari"


def neost_frames(disk: Path, args, work: Path) -> dict[int, Image.Image]:
    copy = work / ("n" + disk.suffix)
    shutil.copy(disk, copy)
    prefix = work / "n_"
    # Une capture tous les `step` trames : le plus grand pas commun aux trames demandées.
    step = reduce(gcd, (f - args.frames[0] for f in args.frames[1:]), 0) or 1
    cmd = [str(HEADLESS), args.rom, "--machine", args.machine, "--mem", f"{args.mem}m",
           "--disk", str(copy), "--frames", str(max(args.frames) + 1),
           "--shot-from", str(min(args.frames)), "--shot-every", str(step), str(prefix)]
    if args.auto:
        cmd += ["--auto", args.auto]
    subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=1200, check=True)
    shots = sorted(work.glob("n_*"))
    if not shots:
        raise RuntimeError("NeoST n'a produit aucune capture")
    out = {}
    for f in args.frames:
        name = shots[min((f - args.frames[0]) // step, len(shots) - 1)]
        out[f] = Image.open(name).convert("RGB").copy()
    for s in shots:
        s.unlink()
    return out


def hatari_shots(disk: Path, args, work: Path) -> Path:
    """Captures par le débogueur d'Hatari, sur les fenêtres voulues. Pas d'AVI : son
    enregistrement s'arrête au premier changement de résolution, que ces démos font.
    Chaque capture pose le point d'arrêt suivant : garder un seul point actif évite de
    ralentir Hatari, qui évalue les conditions à chaque instruction."""
    copy = work / ("h" + disk.suffix)
    shutil.copy(disk, copy)
    shots = work / "hatari"
    shots.mkdir()
    wanted = sorted({v for f in args.frames
                     for v in range(max(1, f - args.window), f + args.window + 1)})
    for i, vbl in enumerate(wanted):
        lines = [f"screenshot {shots / f'{vbl:06d}.png'}"]
        if i + 1 < len(wanted):
            lines.append(f"b VBL = {wanted[i + 1]} :once :trace :file {work / f's{wanted[i + 1]}.ini'}")
        (work / f"s{vbl}.ini").write_text("\n".join(lines) + "\n")
    parse = work / "start.ini"
    parse.write_text(f"b VBL = {wanted[0]} :once :trace :file {work / f's{wanted[0]}.ini'}\n")
    cmd = [str(HATARI), "--machine", args.machine, "--tos", str(ROOT / args.rom), "--monitor", "rgb",
           "--memsize", str(args.mem), "--cpu-exact", "on", "--compatible", "on", "--disk-a", str(copy),
           "--sound", "off", "--fast-forward", "on", "--confirm-quit", "off", "--statusbar", "off",
           "--drive-led", "off", "--frameskips", "0", "--alert-level", "fatal",
           "--run-vbls", str(max(args.frames) + args.window + 2), "--parse", str(parse)]
    if args.auto:
        cmd += ["--auto", args.auto]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", HATARI_SEED="1")
    subprocess.run(cmd, capture_output=True, text=True, timeout=1800, env=env, cwd=work,
                   stdin=subprocess.DEVNULL)
    return shots


def reduce_2x(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Réduit l'image 2× d'Hatari en moyennant les pixels, comme le buffer de NeoST : sur une
    ligne en moyenne résolution, il émet la moyenne des deux pixels de chaque cycle."""
    return img.reduce((img.width // size[0], img.height // size[1]))


def best_match(shots: Path, frame: int, window: int, ref: Image.Image):
    best, best_img = 100.0, None
    for vbl in range(max(1, frame - window), frame + window + 1):
        path = shots / f"{vbl:06d}.png"
        if not path.exists():
            continue
        small = reduce_2x(Image.open(path).convert("RGB"), ref.size)
        hist = ImageChops.difference(ref, small).convert("L").histogram()
        pct = 100.0 * (sum(hist) - hist[0]) / sum(hist)
        if pct < best:
            best, best_img = pct, small.copy()
    return round(best, 2), best_img


def one(disk: Path, args, out: Path) -> dict:
    res = {"disk": disk.name, "frames": {}}
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        work = Path(tmp)
        try:
            shots = neost_frames(disk, args, work)
            shots_h = hatari_shots(disk, args, work)
            if not any(shots_h.iterdir()):
                raise RuntimeError("Hatari n'a produit aucune capture")
            key = hashlib.md5(disk.name.encode()).hexdigest()[:8]
            for f, img in shots.items():
                pct, match = best_match(shots_h, f, args.window, img)
                img.save(out / f"{key}_{f}_n.png")
                if match:
                    match.save(out / f"{key}_{f}_h.png")
                res["frames"][str(f)] = {"diff": pct, "colors": len(img.getcolors(1 << 20) or []),
                                         "key": f"{key}_{f}"}
        except Exception as exc:
            res["error"] = str(exc)[:300]
    return res


def sheet(rows: list[dict], out: Path) -> None:
    w, h, cols = 208, 138, 4
    cells = [(r["disk"], f, v) for r in rows for f, v in r["frames"].items()]
    nrows = max(1, (len(cells) + cols - 1) // cols)
    page = Image.new("RGB", (cols * (2 * w + 8), nrows * (h + 14)), (40, 40, 40))
    draw = ImageDraw.Draw(page)
    for i, (name, f, v) in enumerate(cells):
        x, y = (i % cols) * (2 * w + 8), (i // cols) * (h + 14)
        for j, side in enumerate("nh"):
            path = out / f"{v['key']}_{side}.png"
            if path.exists():
                page.paste(Image.open(path).resize((w, h), Image.NEAREST), (x + j * w, y + 14))
        draw.text((x + 2, y + 1), f"{v['diff']}% @{f} {name[:40]}", fill=(255, 255, 0))
    page.save(out / "sheet.png")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("out", type=Path)
    ap.add_argument("disks", nargs="+", type=Path)
    ap.add_argument("--machine", default="ste")
    ap.add_argument("--rom", default="roms/tos162uk.img")
    ap.add_argument("--mem", type=int, default=1)
    ap.add_argument("--frames", default="1000,2000,4000")
    ap.add_argument("--window", type=int, default=60)
    ap.add_argument("--auto")
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args()
    args.frames = sorted(int(x) for x in args.frames.split(","))
    if shutil.which("ffmpeg") is None or not HATARI.exists() or not HEADLESS.exists():
        print("SKIP — compare_titles : ffmpeg, Hatari ou neost-headless absent")
        return 77
    args.out.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(args.jobs) as ex:
        rows = list(ex.map(lambda d: one(d, args, args.out), args.disks))
    (args.out / "summary.json").write_text(json.dumps(rows, indent=1, ensure_ascii=False))
    for r in rows:
        cols = "  ".join(f"{f}:{v['diff']:>6}%" for f, v in r["frames"].items())
        print(f"{r['disk'][:42]:42} {cols} {r.get('error', '')}")
    sheet(rows, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
