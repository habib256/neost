#!/usr/bin/env python3
# =============================================================================
#  title_cmd.py — La ligne de commande NeoST d'un titre joué, sans Hatari ni Pillow.
#
#  POURQUOI. compare_titles.py joue un titre dans NeoST et dans Hatari ; la porte
#  d'échange du cœur (cpu_swap_gate.py) doit rejouer EXACTEMENT la même partie dans
#  NeoST seul, sur une machine où ni Hatari ni Pillow ne sont installés. Une seule
#  fabrique de commande pour les deux : un parcours qui change change les deux.
#
#  COMMENT. build_parser() est l'analyseur de compare_titles ; finish_args() valide et
#  normalise ; neost_cmd() rend la commande neost-headless, disquettes copiées dans
#  le dossier de travail (un programme peut y écrire).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import argparse
import shutil
from functools import reduce
from math import gcd
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "neost-headless"

KEY_HOLD = 8                    # défaut en trames d'appui, le même des deux côtés


def parse_seed(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("seed must be an unsigned 32-bit integer")
    return value


# Tailles de ST-RAM : le libellé de neost-headless --mem → la valeur de Hatari --memsize
# (en Kio, [D] Hatari stMemory.c STMemory_RAM_Validate_Size_KB @ épingle de
# docs/HATARI_AUTOMATION.md). Seules les tailles que les DEUX émulateurs acceptent.
MEM_KB = {"256k": 256, "512k": 512, "1m": 1024, "2m": 2048, "2176k": 2176, "2560k": 2560, "4m": 4096}
# Ancienne notation entière (en Mio, 0 = 512 Kio comme Hatari) : gardée pour les manifestes.
_MEM_LEGACY = {"0": "512k", "1": "1m", "2": "2m", "4": "4m"}


def parse_mem(text: str) -> str:
    """--mem : un libellé NeoST (512k, 1m…) ou l'entier historique 0/1/2/4 (Mio).

    « --mem 3 » devenait « 3m » côté NeoST — taille inconnue, repliée sur 512k avec
    un simple avertissement — et 3 Mio côté Hatari : les deux machines comparées
    n'avaient pas la même mémoire. Refusé désormais.
    """
    label = _MEM_LEGACY.get(text, text.lower())
    if label not in MEM_KB:
        raise argparse.ArgumentTypeError(
            f"unsupported RAM size '{text}' (use {', '.join(MEM_KB)} or 0/1/2/4 MiB)")
    return label


def parse_key(text: str) -> tuple[int, int]:
    at, _, code = text.partition(":")
    return int(at), int(code)


def parse_mouse(text: str) -> tuple[int, str]:
    at, sep, script = text.partition(":")
    if not sep or not at.isdecimal() or int(at) < 1 or not script or set(script) - set(".LRUDlrud123"):
        raise argparse.ArgumentTypeError("mouse input must be FRAME:SCRIPT (FRAME >= 1; .LRUDlrud123, one character per frame)")
    return int(at), script


def parse_swap(text: str) -> tuple[int, Path]:
    at, _, path = text.partition(":")
    return int(at), Path(path)


def mouse_events(scripts, step=8) -> list[tuple[int, str]]:
    """Même grammaire et relâchement final que --mouse-at du headless.

    Les scripts ne se chevauchent pas : autrement leur ordre pourrait remplacer
    un bouton encore tenu par le script précédent dans un seul des deux témoins.
    """
    events = []
    previous_end = -1
    for start, script in sorted(scripts):
        if start <= previous_end:
            raise ValueError("mouse scripts overlap (including the final release frame)")
        previous_end = start + len(script)
        buttons = 0
        for offset, char in enumerate(script + "."):
            # l/r/u/d : déplacement bouton gauche tenu (glisser), comme le headless.
            current = {"1": 1, "2": 2, "3": 3, "l": 1, "r": 1, "u": 1, "d": 1}.get(char, 0)
            for bit, name in ((1, "left"), (2, "right")):
                if (buttons ^ current) & bit:
                    events.append((start + offset, name + ("down" if current & bit else "up")))
            buttons = current
            dx, dy = {"L": (-step, 0), "R": (step, 0), "U": (0, -step), "D": (0, step),
                      "l": (-step, 0), "r": (step, 0), "u": (0, -step), "d": (0, step)}.get(char, (0, 0))
            if dx or dy:
                events.append((start + offset, f"mousemove {dx} {dy}"))
    return events


def swap_copy(disk: Path, work: Path, tag: str) -> Path:
    copy = work / (tag + disk.suffix)
    shutil.copy(disk, copy)
    return copy


def build_parser(description: str | None = None) -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=description)
    ap.add_argument("out", type=Path)
    ap.add_argument("disks", nargs="+", type=Path)
    ap.add_argument("--machine", default="ste")
    ap.add_argument("--rom", default="roms/tos162uk.img")
    ap.add_argument("--mem", type=parse_mem, default="1m", help="ST-RAM : 512k, 1m, 2m, 4m… (ou 0/1/2/4 Mio)")
    ap.add_argument("--fdc-seed", type=parse_seed, help="graine de l'angle disquette NeoST (défaut du cœur si omise)")
    ap.add_argument("--hatari-seed", type=parse_seed, default=1, help="graine indépendante d'Hatari (défaut : 1)")
    ap.add_argument("--frames", default="1000,2000,4000")
    ap.add_argument("--window", type=int, default=60)
    ap.add_argument("--offset", type=int, default=0, help="centre de la fenêtre Hatari : trame + OFFSET")
    ap.add_argument("--hatari-delay", type=int, default=0,
                    help="retarde de N trames les entrées d'Hatari (touches, souris, joystick, disquettes) datées "
                         "à partir de --hatari-delay-from : son chargement a pris N trames de plus que celui de NeoST")
    ap.add_argument("--hatari-delay-from", type=int, default=0, help="première trame d'entrée décalée (défaut : 0)")
    ap.add_argument("--step", type=int, default=1, help="pas de capture dans la fenêtre")
    ap.add_argument("--auto")
    ap.add_argument("--gemdos", type=Path, help="dossier monté en C: à la place de la disquette")
    ap.add_argument("--disk-b", type=Path, help="disquette du lecteur B, pour un titre qui la demande")
    ap.add_argument("--swap", type=parse_swap, action="append", default=[],
                    help="TRAME:DISQUE, change la disquette du lecteur A à cette trame (répétable)")
    ap.add_argument("--key", type=parse_key, action="append", default=[],
                    help="TRAME:SCANCODE, touche tenue à cette trame (répétable)")
    ap.add_argument("--key-hold", type=int, default=KEY_HOLD,
                    help="durée d'appui en trames, identique dans les deux émulateurs (défaut : 8)")
    ap.add_argument("--joy", help="TRAME:FICHIER, script joystick du port 1 à partir de cette trame")
    ap.add_argument("--mouse", type=parse_mouse, action="append", default=[],
                    help="TRAME:SCRIPT, souris .LRUDlrud123 comme --mouse-at (lrud : bouton gauche tenu) (répétable, sans chevauchement)")
    ap.add_argument("--mouse-step", type=int, default=8, help="déplacement par pas L/R/U/D (1..127, défaut : 8)")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--hatari-trace", metavar="FLAGS",
                    help="drapeaux --trace d'Hatari (ex. video_sync), relevés avec les mêmes entrées")
    return ap


def finish_args(ap: argparse.ArgumentParser, args: argparse.Namespace) -> argparse.Namespace:
    """Validation commune ; --joy devient (trame, chemin absolu), sans compilation."""
    if args.key_hold < 1:
        ap.error("--key-hold must be at least 1 frame")
    if not 1 <= args.mouse_step <= 127:
        ap.error("--mouse-step must be between 1 and 127")
    try:
        mouse_events(args.mouse)
    except ValueError as exc:
        ap.error(str(exc))
    args.frames = sorted(int(x) for x in args.frames.split(","))
    if args.joy:
        at, _, path = args.joy.partition(":")
        args.joy = (int(at), Path(path).resolve())
    return args


def parse(argv: list[str]) -> argparse.Namespace:
    ap = build_parser()
    return finish_args(ap, ap.parse_args(argv))


def neost_cmd(disk: Path, args: argparse.Namespace, work: Path, shot_prefix: Path | None = None) -> list[str]:
    """Commande neost-headless du titre, jusqu'à la dernière trame demandée.

    Avec shot_prefix, une capture aux trames voulues : le headless capture les
    multiples absolus du pas, pas depuis --shot-from ; le PGCD des seuls écarts
    décalait par exemple [101,201] vers [200,300]. Depuis le 2026-11-11, P<F>.ppm est
    l'écran après F trames (celui de « --frames F --screenshot ») ; il montrait
    auparavant l'écran après F+1 trames.
    """
    copy = work / ("n" + disk.suffix)
    shutil.copy(disk, copy)
    cmd = [str(HEADLESS), args.rom, "--machine", args.machine, "--mem", args.mem,
           *(["--disk", str(copy)] if not args.gemdos else ["--gemdos", str(args.gemdos.resolve())]),
           "--frames", str(max(args.frames) + 1)]
    if shot_prefix is not None:
        step = reduce(gcd, args.frames) or 1
        cmd += ["--shot-from", str(min(args.frames)), "--shot-every", str(step), str(shot_prefix)]
    if args.disk_b:
        cmd += ["--diskb", str(swap_copy(args.disk_b, work, "nb"))]
    if args.fdc_seed is not None:
        cmd += ["--fdc-seed", str(args.fdc_seed)]
    if args.auto:
        cmd += ["--auto", args.auto]
    if args.joy:
        cmd += ["--joy-script-file", str(args.joy[0]), str(args.joy[1])]
    for at, script in args.mouse:
        cmd += ["--mouse-at", str(at), script]
    if args.mouse:
        cmd += ["--mouse-step", str(args.mouse_step)]
    for i, (at, other) in enumerate(args.swap):
        cmd += ["--disk-at", str(at), str(swap_copy(other, work, f"ns{i}"))]
    if args.key:
        cmd += ["--key-hold", str(args.key_hold)]
        for at, code in args.key:
            cmd += ["--scancode-at", str(at), f"{code:x}"]
    return cmd
