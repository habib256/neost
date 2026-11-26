#!/usr/bin/env python3
"""Campagne de compatibilité du cœur libretro sur une collection de disquettes.

Chaque image passe dans le module (un processus par image, frontend ctypes de
tools/libretro_frontend.py), joue N trames avec un appui sur Espace et un tir de
temps en temps, puis l'écran final est classé :
  game     — une image riche (plus de 6 couleurs) : titre, présentation ou jeu ;
  desktop  — le bureau GEM (trame verte et blanche, peu de couleurs) ;
  black    — écran noir ;
  other    — le reste (message, alerte, écran à peu de couleurs).
Le classement est une première passe : la planche contact (--sheet, Pillow) sert à
le vérifier à l'œil.

Médias PRIVÉS (disks/st, disks/stx…) : outil local, jamais en CI. Rien n'est écrit à
côté des originaux : le module les copie dans le dossier de sauvegardes de l'essai.

  python3 tools/libretro_corpus.py --tos emutos --out build-libretro/corpus-emutos
  python3 tools/libretro_corpus.py --tos atari  --out build-libretro/corpus-atari \\
      --sheet --disks disks/st disks/stx

(c) 2026 VERHILLE Arnaud — projet NeoST.
"""
import argparse
import json
import multiprocessing as mp
import shutil
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

EXTS = {".st", ".msa", ".dim", ".stx", ".ipf", ".zip", ".7z", ".gz"}
EMUTOS = ["etos192fr.img", "etos256fr.img"]
ATARI = ["tos104uk.img", "tos162uk.img", "tos206uk.img"]
# --tos tos102 : le même jeu avec TOS 1.02 à la place de 1.04 pour le ST.
TOS_SETS = {"emutos": [], "atari": ATARI, "tos102": ["tos102uk.img", "tos162uk.img", "tos206uk.img"]}


def classify(frame):
    w, h, raw = frame
    px = [(raw[i + 2], raw[i + 1], raw[i]) for i in range(0, len(raw), 4 * 3)]
    colours = Counter(px)
    black = sum(n for c, n in colours.items() if max(c) < 24) / len(px)
    if black > 0.97:
        return "black", len(colours)
    if len(colours) > 6:
        return "game", len(colours)
    greenish = sum(n for c, n in colours.items() if c[1] > 150 and c[0] < 120 and c[2] < 120)
    whiteish = sum(n for c, n in colours.items() if min(c) > 200)
    if (greenish + whiteish) / len(px) > 0.6 and greenish / len(px) > 0.15:
        return "desktop", len(colours)
    return "other", len(colours)


def run_one(args):
    module, image, tos, frames, workdir = args
    from libretro_frontend import Core
    work = Path(tempfile.mkdtemp(prefix="neost-corpus-", dir=workdir))
    try:
        system = work / "system" / "neost"
        system.mkdir(parents=True)
        for t in EMUTOS + TOS_SETS[tos]:
            if (ROOT / "roms" / t).exists():
                shutil.copy(ROOT / "roms" / t, system / t)
        (work / "saves").mkdir()
        c = Core(module, work / "system", work / "saves", {"neost_drive_sound": "disabled"})
        ok = c.load(image)
        if not ok:
            return {"image": str(image), "result": "refused", "messages": c.messages}
        for n in range(frames):
            if n % 600 == 300:
                c.key("SPACE", True)
            elif n % 600 == 306:
                c.key("SPACE", False)
            c.pad[0] = {"B"} if n % 600 in range(450, 456) else set()
            c.run(1)
        result, colours = classify(c.frame)
        thumb = work.parent / (Path(image).stem[:60] + f".{tos}.ppm")
        c.write_ppm(thumb)
        return {"image": str(image), "result": result, "colours": colours, "messages": c.messages,
                "size": list(c.frame[:2]), "thumb": str(thumb)}
    except Exception as e:   # un titre qui fait tomber le frontend ne doit pas arrêter la campagne
        return {"image": str(image), "result": "crash", "error": repr(e)}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def contact_sheet(results, path, cols=10, tw=192, th=120):
    from PIL import Image, ImageDraw
    rows = (len(results) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, rows * (th + 14)), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    colour = {"game": (90, 200, 90), "desktop": (220, 200, 60), "black": (200, 80, 80),
              "other": (200, 120, 200), "refused": (255, 0, 0), "crash": (255, 0, 0)}
    for i, r in enumerate(results):
        x, y = (i % cols) * tw, (i // cols) * (th + 14)
        if r.get("thumb") and Path(r["thumb"]).exists():
            sheet.paste(Image.open(r["thumb"]).resize((tw - 4, th - 4)), (x + 2, y + 2))
        draw.text((x + 3, y + th), f"{i:02d} {r['result']}", fill=colour.get(r["result"], (255, 255, 255)))
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", type=Path)
    ap.add_argument("--disks", nargs="+", default=["disks/st", "disks/stx"])
    ap.add_argument("--tos", choices=sorted(TOS_SETS), default="emutos")
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--jobs", type=int, default=max(1, (mp.cpu_count() or 2) - 1))
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--sheet", action="store_true")
    a = ap.parse_args()
    from run_libretro import default_module
    module = (a.module or default_module()).resolve()
    images = []
    for d in a.disks:
        for p in sorted(Path(d).iterdir()):
            if p.is_file() and p.suffix.lower() in EXTS:
                images.append(p.resolve())
    a.out.mkdir(parents=True, exist_ok=True)
    jobs = [(module, img, a.tos, a.frames, str(a.out)) for img in images]
    ctx = mp.get_context("spawn")
    with ctx.Pool(a.jobs, maxtasksperchild=1) as pool:
        results = pool.map(run_one, jobs, chunksize=1)
    (a.out / "results.json").write_text(json.dumps(results, indent=1))
    tally = Counter(r["result"] for r in results)
    for i, r in enumerate(results):
        extra = " | " + r["messages"][-1] if r.get("messages") and len(r["messages"]) > 1 else ""
        print(f"{i:02d} {r['result']:8s} {Path(r['image']).name[:70]}{extra}")
    print("\n" + ", ".join(f"{k}: {v}" for k, v in sorted(tally.items())) + f" ({len(results)} images, TOS {a.tos})")
    if a.sheet:
        contact_sheet(results, a.out / "sheet.png")
        print(f"sheet: {a.out / 'sheet.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
