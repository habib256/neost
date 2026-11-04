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
#            [--mem 1] [--frames 1000,2000,4000] [--window 60] [--offset 0] [--step 1]
#            [--auto A:\\PROG.PRG] [--gemdos DOSSIER] [--disk-b DISQUE] [--swap TRAME:DISQUE]... [--key TRAME:SCANCODE]...
#            [--joy TRAME:FICHIER] [--jobs 4]
#  --joy : script joystick daté, port 1, grammaire de neost-headless --joy-script-file. La même suite de masques
#  des deux côtés : compilée par NeoST, jouée par Hatari via son patch NEOST_JOY_SCRIPT (docs/HATARI_AUTOMATION.md).
#  --key : touche ST tenue KEY_HOLD trames (scancode décimal, 57 = espace). Côté Hatari, par sa
#  fifo de commande à un arrêt du débogueur, comme tools/hatari_oracle.sh.
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
import re
import shutil
import subprocess
import sys
import tempfile
import time
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


KEY_HOLD = 8                    # trames d'appui, les mêmes des deux côtés


def parse_key(text: str) -> tuple[int, int]:
    at, _, code = text.partition(":")
    return int(at), int(code)


def run_hatari_keys(cmd: list[str], keys, work: Path, env: dict) -> None:
    """Hatari avec des touches datées : un arrêt du débogueur par événement, puis l'événement dans la
    fifo et « c » sur stdin. Mesuré sur le build épinglé, cf. le bandeau de tools/hatari_oracle.sh."""
    events = sorted([(at, "keydown", code) for at, code in keys] + [(at + KEY_HOLD, "keyup", code) for at, code in keys])
    parse = work / "start.ini"
    with parse.open("a") as f:
        for vbl, _, _ in events:
            f.write(f"b VBL = {vbl} :once\n")
    fifo = work / "cmd.fifo"
    log = work / "hatari.log"
    with log.open("w") as out:
        proc = subprocess.Popen(cmd + ["--cmd-fifo", str(fifo)], stdin=subprocess.PIPE, stdout=out, stderr=subprocess.STDOUT,
                                text=True, env=env, cwd=work)
    assert proc.stdin
    deadline, seen, sent = time.time() + 1800, 0, 0
    while proc.poll() is None and time.time() < deadline:
        text = log.read_text(errors="replace")
        prompts = re.findall(r"VBL=(\d+)", text)
        if len(prompts) > seen:
            seen = len(prompts)
            vbl = int(prompts[-1])
            while sent < len(events) and events[sent][0] <= vbl:
                _, kind, code = events[sent]
                with fifo.open("w") as f:
                    f.write(f"hatari-event {kind} {code}\n")
                sent += 1
            try:
                proc.stdin.write("c\n")
                proc.stdin.flush()
            except BrokenPipeError:
                break
        time.sleep(0.02)
    if proc.poll() is None:
        proc.kill()
    proc.wait()


def swap_copy(disk: Path, work: Path, tag: str) -> Path:
    copy = work / (tag + disk.suffix)
    shutil.copy(disk, copy)
    return copy


def parse_swap(text: str) -> tuple[int, Path]:
    at, _, path = text.partition(":")
    return int(at), Path(path)


def neost_frames(disk: Path, args, work: Path) -> dict[int, Image.Image]:
    copy = work / ("n" + disk.suffix)
    shutil.copy(disk, copy)
    prefix = work / "n_"
    # Une capture tous les `step` trames : le plus grand pas commun aux trames demandées.
    step = reduce(gcd, (f - args.frames[0] for f in args.frames[1:]), 0) or 1
    cmd = [str(HEADLESS), args.rom, "--machine", args.machine, "--mem", f"{args.mem}m",
           *(["--disk", str(copy)] if not args.gemdos else ["--gemdos", str(args.gemdos.resolve())]),
           "--frames", str(max(args.frames) + 1),
           "--shot-from", str(min(args.frames)), "--shot-every", str(step), str(prefix)]
    if args.disk_b:
        cmd += ["--diskb", str(swap_copy(args.disk_b, work, "nb"))]
    if args.auto:
        cmd += ["--auto", args.auto]
    if args.joy:
        cmd += ["--joy-script-file", str(args.joy[0]), str(args.joy[1])]
    for i, (at, other) in enumerate(args.swap):
        cmd += ["--disk-at", str(at), str(swap_copy(other, work, f"ns{i}"))]
    if args.key:
        cmd += ["--key-hold", str(KEY_HOLD)]
        for at, code in args.key:
            cmd += ["--scancode-at", str(at), f"{code:x}"]
    subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=1200, check=True)
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
                     for v in range(max(1, f + args.offset - args.window),
                                    f + args.offset + args.window + 1, args.step)})
    # Un changement de disquette est un arrêt de plus, en ordre de VBL avec les captures.
    swaps = {at: swap_copy(other, work, f"hs{i}") for i, (at, other) in enumerate(args.swap)}
    wanted = sorted(set(wanted) | set(swaps))
    shoot = {v for f in args.frames
             for v in range(max(1, f + args.offset - args.window),
                            f + args.offset + args.window + 1, args.step)}
    for i, vbl in enumerate(wanted):
        lines = []
        if vbl in swaps:
            lines.append(f"setopt --disk-a {swaps[vbl]}")
        if vbl in shoot:
            lines.append(f"screenshot {shots / f'{vbl:06d}.png'}")
        if i + 1 < len(wanted):
            lines.append(f"b VBL = {wanted[i + 1]} :once :trace :file {work / f's{wanted[i + 1]}.ini'}")
        (work / f"s{vbl}.ini").write_text("\n".join(lines) + "\n")
    parse = work / "start.ini"
    parse.write_text(f"b VBL = {wanted[0]} :once :trace :file {work / f's{wanted[0]}.ini'}\n")
    cmd = [str(HATARI), "--machine", args.machine, "--tos", str(ROOT / args.rom), "--monitor", "rgb",
           "--memsize", str(args.mem), "--cpu-exact", "on", "--compatible", "on",
           *(["--disk-a", str(copy)] if not args.gemdos else ["--harddrive", str(args.gemdos.resolve())]),
           "--sound", "25033", "--fast-forward", "on", "--confirm-quit", "off", "--statusbar", "off",
           "--drive-led", "off", "--frameskips", "0", "--alert-level", "fatal",
           "--run-vbls", str(max(args.frames) + args.offset + args.window + 2), "--parse", str(parse)]
    if args.disk_b:
        cmd += ["--disk-b", str(swap_copy(args.disk_b, work, "hb"))]
    if args.auto:
        cmd += ["--auto", args.auto]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", HATARI_SEED="1")
    if args.joy:
        env.update(NEOST_JOY_SCRIPT=str(args.joy[2]), NEOST_JOY_START=str(args.joy[0]))
    if args.key:
        run_hatari_keys(cmd, args.key, work, env)
    else:
        subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=1800, env=env, cwd=work,
                       stdin=subprocess.DEVNULL)
    return shots


def reduce_2x(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Réduit l'image 2× d'Hatari en moyennant les pixels, comme le buffer de NeoST : sur une
    ligne en moyenne résolution, il émet la moyenne des deux pixels de chaque cycle."""
    # Écran pur en moyenne résolution : NeoST sort 640×200 sans bordure, Hatari 832×552 avec. Sa zone active
    # commence en (96, 58), mesuré à 0,0 % sur Notator ; il double les lignes, sans les moyenner.
    if size == (640, 200) and img.size == (832, 552):
        return img.crop((96, 58, 736, 458)).resize(size, Image.NEAREST)
    return img.reduce((img.width // size[0], img.height // size[1]))


def best_match(shots: Path, frame: int, args, ref: Image.Image):
    best, best_img, best_vbl = 100.0, None, None
    for vbl in range(max(1, frame + args.offset - args.window),
                     frame + args.offset + args.window + 1, args.step):
        path = shots / f"{vbl:06d}.png"
        if not path.exists():
            continue
        small = reduce_2x(Image.open(path).convert("RGB"), ref.size)
        hist = ImageChops.difference(ref, small).convert("L").histogram()
        pct = 100.0 * (sum(hist) - hist[0]) / sum(hist)
        if pct < best:
            best, best_img, best_vbl = pct, small.copy(), vbl
    return round(best, 2), best_img, best_vbl


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
                pct, match, vbl = best_match(shots_h, f, args, img)
                img.save(out / f"{key}_{f}_n.png")
                if match:
                    match.save(out / f"{key}_{f}_h.png")
                res["frames"][str(f)] = {"diff": pct, "hatari_vbl": vbl,
                                         "colors": len(img.getcolors(1 << 20) or []),
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
    ap.add_argument("--offset", type=int, default=0, help="centre de la fenêtre Hatari : trame + OFFSET")
    ap.add_argument("--step", type=int, default=1, help="pas de capture dans la fenêtre")
    ap.add_argument("--auto")
    ap.add_argument("--gemdos", type=Path, help="dossier monté en C: à la place de la disquette")
    ap.add_argument("--disk-b", type=Path, help="disquette du lecteur B, pour un titre qui la demande")
    ap.add_argument("--swap", type=parse_swap, action="append", default=[],
                    help="TRAME:DISQUE, change la disquette du lecteur A à cette trame (répétable)")
    ap.add_argument("--key", type=parse_key, action="append", default=[],
                    help="TRAME:SCANCODE, touche tenue à cette trame (répétable)")
    ap.add_argument("--joy", help="TRAME:FICHIER, script joystick du port 1 à partir de cette trame")
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args()
    args.frames = sorted(int(x) for x in args.frames.split(","))
    if shutil.which("ffmpeg") is None or not HATARI.exists() or not HEADLESS.exists():
        print("SKIP — compare_titles : ffmpeg, Hatari ou neost-headless absent")
        return 77
    args.out.mkdir(parents=True, exist_ok=True)
    if args.joy:
        at, _, path = args.joy.partition(":")
        compiled = args.out.resolve() / "joy.bin"
        subprocess.run([str(HEADLESS), args.rom, "--joy-script-compile", str(compiled), "--joy-script-file", at, path],
                       cwd=ROOT, capture_output=True, text=True, check=True)
        args.joy = (int(at), Path(path).resolve(), compiled)
    with ThreadPoolExecutor(args.jobs) as ex:
        rows = list(ex.map(lambda d: one(d, args, args.out), args.disks))
    (args.out / "summary.json").write_text(json.dumps(rows, indent=1, ensure_ascii=False))
    for r in rows:
        cols = "  ".join(f"{f}:{v['diff']:>6}%@{v['hatari_vbl']}" for f, v in r["frames"].items())
        print(f"{r['disk'][:42]:42} {cols} {r.get('error', '')}")
    sheet(rows, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
