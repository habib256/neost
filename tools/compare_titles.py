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
#            [--joy TRAME:FICHIER] [--mouse TRAME:SCRIPT] [--jobs 4]
#            [--fdc-seed N] [--hatari-seed N]
#  Les graines sont indépendantes : une même valeur ne donne pas le même angle dans les deux émulateurs.
#  --joy : script joystick daté, port 1, grammaire de neost-headless --joy-script-file. La même suite de masques
#  des deux côtés : compilée par NeoST, jouée par Hatari via son patch NEOST_JOY_SCRIPT (docs/HATARI_AUTOMATION.md).
#  --key : touche ST tenue --key-hold trames (8 par défaut ; scancode décimal, 57 = espace). Côté Hatari, par sa
#  fifo de commande à un arrêt du débogueur, comme tools/hatari_oracle.sh.
#  --mouse : .LRUDlrud123, un caractère par trame comme --mouse-at ; déplacement relatif de 8 unités ;
#            lrud déplace bouton gauche tenu (glisser-déposer).
#  Requiert le patch mousemove de l’oracle ; mêmes boutons et relâchement final des deux côtés.
#  Sorties : SORTIE/summary.json, SORTIE/sheet.png. Exige Pillow et ffmpeg ; code 77 sinon.
#  Les disques sont copiés avant chaque exécution : un programme peut y écrire.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

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
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw
except ImportError:
    print("SKIP — compare_titles : Pillow absent")
    sys.exit(77)

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "neost-headless"
HATARI = ROOT / "extern" / "hatari" / "build" / "src" / "hatari"


from title_cmd import (KEY_HOLD, MEM_KB, build_parser, hatari_memsize, finish_args, mouse_events, neost_cmd,  # noqa: E402,F401
                       parse_key, parse_mouse, parse_seed, parse_swap, swap_copy)


def run_hatari_keys(cmd: list[str], keys, work: Path, env: dict, key_hold: int = KEY_HOLD, mouse=(), mouse_step=8) -> None:
    """Hatari avec des touches datées : un arrêt du débogueur par événement, puis l'événement dans la
    fifo et « c » sur stdin. Mesuré sur le build épinglé, cf. le bandeau de tools/hatari_oracle.sh."""
    events = sorted([(at, f"keydown {code}") for at, code in keys]
                    + [(at + key_hold, f"keyup {code}") for at, code in keys]
                    + mouse_events(mouse, mouse_step), key=lambda event: event[0])
    parse = work / "start.ini"
    with parse.open("a") as f:
        if events:
            f.write(f"b VBL = {events[0][0]} :once\n")
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
                _, event = events[sent]
                with fifo.open("w") as f:
                    f.write(f"hatari-event {event}\n")
                sent += 1
            try:
                # Un seul arrêt d'entrée actif : des dizaines de pas souris ne
                # doivent pas ralentir toutes les instructions du démarrage.
                if sent < len(events):
                    proc.stdin.write(f"b VBL = {events[sent][0]} :once\n")
                proc.stdin.write("c\n")
                proc.stdin.flush()
            except BrokenPipeError:
                break
        time.sleep(0.02)
    if proc.poll() is None:
        proc.kill()
        proc.wait()
        raise TimeoutError("Hatari timed out while replaying keyboard input")
    if proc.wait() != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)
    if "ERROR: unrecognized event:" in log.read_text(errors="replace"):
        raise RuntimeError("Hatari rejected an input event; rebuild its NeoST oracle instrumentation")


def read_neost_frames(prefix: Path, frames) -> dict[int, Image.Image]:
    """Les noms du headless portent les numéros réels, pas des indices de capture."""
    out = {}
    for frame in frames:
        path = Path(f"{prefix}{frame:05d}.ppm")
        if not path.exists():
            raise RuntimeError(f"Missing NeoST capture at frame {frame}: {path.name}")
        with Image.open(path) as img:
            out[frame] = img.convert("RGB").copy()
    return out


def neost_frames(disk: Path, args, work: Path) -> dict[int, Image.Image]:
    prefix = work / "n_"
    cmd = neost_cmd(disk, args, work, prefix)
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=1200)
    (work / "neost.log").write_text(done.stdout + done.stderr)
    done.check_returncode()
    shots = sorted(work.glob("n_*"))
    if not shots:
        raise RuntimeError("NeoST n'a produit aucune capture")
    # Ne jamais remplacer une capture absente par la dernière disponible :
    # cela cachait un arrêt prématuré et fabriquait des trames dans summary.json.
    out = read_neost_frames(prefix, args.frames)
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
    # --hatari-delay : l'angle de disquette tiré par chaque émulateur change la durée du
    # chargement ; décaler les entrées d'Hatari du même nombre de trames lui donne les mêmes
    # commandes au même instant du JEU. NeoST n'est pas touché.
    shift = lambda at: at + args.hatari_delay if at >= args.hatari_delay_from else at
    swaps = {shift(at): swap_copy(other, work, f"hs{i}") for i, (at, other) in enumerate(args.swap)}
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
    # Comparer le TOS original, y compris ses tests RAM et attentes de boot.
    # --patch-tos contrôle aussi les variables système préremplies par Hatari.
    cmd = [str(HATARI), "--machine", args.machine, "--tos", str(ROOT / args.rom), "--monitor", "rgb",
           "--memsize", hatari_memsize(args.mem), "--cpu-exact", "on", "--compatible", "on",
           "--patch-tos", "off", "--fast-boot", "off", "--fastfdc", "off",
           *(["--disk-a", str(copy)] if not args.gemdos else ["--harddrive", str(args.gemdos.resolve())]),
           "--sound", "25033", "--fast-forward", "on", "--confirm-quit", "off", "--statusbar", "off",
           "--drive-led", "off", "--frameskips", "0", "--alert-level", "fatal",
           "--run-vbls", str(max(args.frames) + args.offset + args.window + 2), "--parse", str(parse)]
    if args.disk_b:
        cmd += ["--disk-b", str(swap_copy(args.disk_b, work, "hb"))]
    if args.auto:
        cmd += ["--auto", args.auto]
    if args.hatari_trace:
        # Trace Hatari avec les mêmes entrées que la comparaison (fichier : <sortie>/<id>_hatari_trace.txt).
        cmd += ["--trace", args.hatari_trace, "--trace-file", str(work / "hatari_trace.txt")]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", HATARI_SEED=str(args.hatari_seed))
    if args.joy:
        env.update(NEOST_JOY_SCRIPT=str(args.joy[2]), NEOST_JOY_START=str(shift(args.joy[0])))
    if args.key or args.mouse:
        run_hatari_keys(cmd, [(shift(at), code) for at, code in args.key], work, env, args.key_hold,
                        [(shift(at), script) for at, script in args.mouse], args.mouse_step)
    else:
        done = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=1800, env=env, cwd=work,
                              stdin=subprocess.DEVNULL)
        (work / "hatari.log").write_text(done.stdout + done.stderr)
        done.check_returncode()
    return shots


def reduce_2x(img: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Réduit l'image 2× d'Hatari en moyennant les pixels, comme le buffer de NeoST : sur une
    ligne en moyenne résolution, il émet la moyenne des deux pixels de chaque cycle."""
    # Écran pur en moyenne résolution : NeoST sort 640×200 sans bordure, Hatari 832×552 avec. Sa zone active
    # commence en (96, 58), mesuré à 0,0 % sur Notator ; il double les lignes, sans les moyenner.
    if size == (640, 200) and img.size == (832, 552):
        return img.crop((96, 58, 736, 458)).resize(size, Image.NEAREST)
    return img.reduce((img.width // size[0], img.height // size[1]))


def pixel_difference(a: Image.Image, b: Image.Image) -> float:
    """Pourcentage de pixels RGB différents, sans arrondi en luminance.

    Une différence bleue de 1 devenait zéro avec convert('L') : deux images
    différentes pouvaient donc être annoncées identiques.
    """
    if a.size != b.size:
        raise ValueError(f"Capture dimensions differ: {a.size} != {b.size}")
    red, green, blue = ImageChops.difference(a, b).split()
    hist = ImageChops.lighter(ImageChops.lighter(red, green), blue).histogram()
    return 100.0 * (sum(hist) - hist[0]) / sum(hist)


def best_match(shots: Path, frame: int, args, ref: Image.Image):
    best, best_img, best_vbl = 100.0, None, None
    for vbl in range(max(1, frame + args.offset - args.window),
                     frame + args.offset + args.window + 1, args.step):
        path = shots / f"{vbl:06d}.png"
        if not path.exists():
            continue
        small = reduce_2x(Image.open(path).convert("RGB"), ref.size)
        pct = pixel_difference(ref, small)
        if pct < best:
            best, best_img, best_vbl = pct, small.copy(), vbl
    return round(best, 2), best_img, best_vbl


def describe_error(exc: Exception, work: Path) -> str:
    """La CAUSE d'un échec, pas le début de la ligne de commande : un CalledProcessError
    commence par la commande entière, et la tronquer au début cachait le code de sortie."""
    if isinstance(exc, subprocess.TimeoutExpired):
        return f"{Path(str(exc.cmd[0])).name} : délai de {exc.timeout} s dépassé"
    if isinstance(exc, subprocess.CalledProcessError):
        name = Path(str(exc.cmd[0])).name
        log = work / ("hatari.log" if name.startswith("hatari") else "neost.log")
        tail = ""
        if log.exists():
            lines = [l for l in log.read_text(errors="replace").splitlines() if l.strip()]
            tail = " | ".join(lines[-3:])
        return f"{name} : code de sortie {exc.returncode}" + (f" — {tail[-400:]}" if tail else "")
    return str(exc)[-300:]


def one(disk: Path, args, out: Path) -> dict:
    res = {"disk": disk.name, "fdc_seed": args.fdc_seed, "hatari_seed": args.hatari_seed,
           "key_hold": args.key_hold, "offset": args.offset,
           "window": args.window, "step": args.step, "mouse": args.mouse, "mouse_step": args.mouse_step, "frames": {}}
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        work = Path(tmp)
        key = hashlib.md5(disk.name.encode()).hexdigest()[:8]
        # Les copies de travail peuvent être écrites par les logiciels émulés ;
        # les médias fournis, ROM comprise, doivent garder leur empreinte.
        paths = [disk, ROOT / args.rom]
        if args.disk_b:
            paths.append(args.disk_b)
        paths.extend(other for _, other in args.swap)
        fingerprints = {}
        try:
            fingerprints = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
            res["media_sha256"] = fingerprints
            shots = neost_frames(disk, args, work)
            shots_h = hatari_shots(disk, args, work)
            if not any(shots_h.iterdir()):
                raise RuntimeError("Hatari n'a produit aucune capture")
            for f, img in shots.items():
                pct, match, vbl = best_match(shots_h, f, args, img)
                img.save(out / f"{key}_{f}_n.png")
                if match:
                    match.save(out / f"{key}_{f}_h.png")
                else:
                    # Aucune image Hatari dans la fenêtre : l'image d'une passe PRÉCÉDENTE
                    # dans le même dossier de sortie restait là, et la planche la montrait
                    # en regard de NeoST comme si elle venait de ce run.
                    (out / f"{key}_{f}_h.png").unlink(missing_ok=True)
                res["frames"][str(f)] = {"diff": pct, "hatari_vbl": vbl,
                                         "colors": len(img.getcolors(1 << 20) or []),
                                         "neost_rgb_sha256": hashlib.sha256(img.tobytes()).hexdigest(),
                                         "key": f"{key}_{f}"}
        except Exception as exc:
            res["error"] = describe_error(exc, work)
        finally:
            for name, suffix in (("neost", "n"), ("hatari", "h")):
                log = work / f"{name}.log"
                if log.exists():
                    message = log.read_text(errors="replace")
                    # Le headless rend actuellement 0 même après une double faute.
                    # Un écran identique et figé n'est pas une validation du jeu.
                    res[f"{name}_halted"] = "CPU halted" in message
                    shutil.copyfile(log, out / f"{key}_{suffix}.log")
            if (work / "hatari_trace.txt").exists():
                shutil.copyfile(work / "hatari_trace.txt", out / f"{key}_hatari_trace.txt")
            for path, expected in fingerprints.items():
                try:
                    intact = hashlib.sha256(Path(path).read_bytes()).hexdigest() == expected
                except OSError:
                    intact = False
                if not intact:
                    res["error"] = f"Source media changed during comparison: {path}"
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
    ap = build_parser(__doc__)
    args = finish_args(ap, ap.parse_args())
    if shutil.which("ffmpeg") is None or not HATARI.exists() or not HEADLESS.exists():
        print("SKIP — compare_titles : ffmpeg, Hatari ou neost-headless absent")
        return 77
    args.out.mkdir(parents=True, exist_ok=True)
    if args.joy:
        at, path = args.joy
        compiled = args.out.resolve() / "joy.bin"
        subprocess.run([str(HEADLESS), args.rom, "--joy-script-compile", str(compiled), "--joy-script-file", str(at), str(path)],
                       cwd=ROOT, capture_output=True, text=True, check=True)
        args.joy = (at, path, compiled)
    with ThreadPoolExecutor(args.jobs) as ex:
        rows = list(ex.map(lambda d: one(d, args, args.out), args.disks))
    (args.out / "summary.json").write_text(json.dumps(rows, indent=1, ensure_ascii=False))
    for r in rows:
        cols = "  ".join(f"{f}:{v['diff']:>6}%@{v['hatari_vbl']}" for f, v in r["frames"].items())
        print(f"{r['disk'][:42]:42} {cols} {r.get('error', '')}")
    sheet(rows, args.out)
    # Une ligne en erreur n'a rien comparé : le code de sortie le dit. Avant, une passe
    # dont TOUTES les lignes avaient échoué (binaire absent, Hatari planté) rendait 0.
    failed = [r for r in rows if r.get("error")]
    for r in failed:
        print(f"[compare_titles] {r['disk']}: {r['error']}", file=sys.stderr)
    if failed:
        print(f"[compare_titles] {len(failed)}/{len(rows)} title(s) not compared", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
