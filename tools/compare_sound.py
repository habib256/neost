#!/usr/bin/env python3
# =============================================================================
#  compare_sound.py — Passe sur le SON de vrais titres : NeoST contre l'Hatari épinglé.
#
#  POURQUOI. Les parcours et les étalons ne comparent que des images : un titre qui joue
#  faux, trop lentement ou plus du tout y reste vert. Outil de repérage, hors paliers : il
#  compare à Hatari (grade D) et ne prouve aucune fidélité.
#
#  COMMENT. Chaque émulateur écrit son WAV sur le même disque. Hatari tire au hasard la
#  position angulaire de la disquette et filtre son mix (HPF sous-sonique, IIR LMC,
#  docs/HATARI_AUTOMATION.md) : une comparaison d'échantillons ne voudrait rien dire. On
#  compare donc l'ENVELOPPE, RMS par fenêtres de 40 ms sans composante continue, au meilleur
#  décalage dans ±--window secondes.
#
#  Verdicts : silent (muet des deux côtés), neost-silent / hatari-silent, ok (corrélation
#  >= --min-corr), diverge. Un « diverge » reste à écouter : un titre qui tire sa musique au
#  hasard ou qui change d'écran à une autre date en donne aussi.
#
#  Usage : compare_sound.py SORTIE DISQUE... [--machine st] [--rom roms/etos192fr.img]
#            [--mem 1] [--frames 2500] [--window 12] [--min-corr 0.7] [--jobs 6]
#  Exige ffmpeg et l'Hatari épinglé ; code 77 sinon. Sortie : SORTIE/summary.json.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import argparse
import array
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / ("neost-headless.exe" if sys.platform == "win32" else "neost-headless")
HATARI = ROOT / "extern/hatari/build/src/hatari"
RATE = 8000           # l'enveloppe n'a pas besoin de plus
WINDOW = 320          # 40 ms à 8 kHz
SILENCE = 30.0        # RMS sur 32767 : sous ce seuil, une fenêtre est muette
MIN_ACTIVE = 0.02     # part de fenêtres sonores en dessous de laquelle un titre est muet


def envelope(wav: Path) -> list[float]:
    """RMS par fenêtre, sans composante continue, après passage à 8 kHz mono."""
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(wav), "-ac", "1", "-ar", str(RATE),
                          "-f", "s16le", "-"], capture_output=True, check=True).stdout
    samples = array.array("h")
    samples.frombytes(raw[:len(raw) // 2 * 2])
    if sys.byteorder == "big":
        samples.byteswap()
    out = []
    for start in range(0, len(samples) - WINDOW + 1, WINDOW):
        block = samples[start:start + WINDOW]
        mean = sum(block) / WINDOW
        out.append(math.sqrt(sum((v - mean) ** 2 for v in block) / WINDOW))
    return out


def correlation(a: list[float], b: list[float]) -> float | None:
    n = min(len(a), len(b))
    if n < 25:                                     # moins d'une seconde en commun
        return None
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((x - mb) ** 2 for x in b)
    if va == 0 or vb == 0:
        return None
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def best_shift(a: list[float], b: list[float], window_s: float) -> tuple[float, float]:
    """Meilleure corrélation de a et b, b décalé d'au plus window_s secondes."""
    limit = int(window_s * RATE / WINDOW)
    best, where = -2.0, 0
    for shift in range(-limit, limit + 1):
        c = correlation(a[shift:], b) if shift >= 0 else correlation(a, b[-shift:])
        if c is not None and c > best:
            best, where = c, shift
    return best, where * WINDOW / RATE


def run_neost(disk: Path, args, wav: Path) -> None:
    subprocess.run([str(HEADLESS), args.rom, "--machine", args.machine, "--mem", f"{args.mem}m",
                    "--disk", str(disk), "--disk-ro", "--frames", str(args.frames), "--sound-dump", str(wav)],
                   cwd=ROOT, capture_output=True, timeout=900, check=True)


def run_hatari(disk: Path, args, work: Path, wav: Path) -> None:
    copy = work / ("h" + disk.suffix)                # un programme peut écrire sur son disque
    shutil.copyfile(disk, copy)
    config = work / "hatari.cfg"
    config.write_text(f"[Sound]\nszYMCaptureFileName = {wav}\n")
    fifo = work / "cmd.fifo"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", HATARI_SEED="1", HOME=str(work))
    with (work / "hatari.log").open("w") as log:
        proc = subprocess.Popen([str(HATARI), "--configfile", str(config), "--machine", args.machine,
                                 "--tos", str(ROOT / args.rom), "--monitor", "rgb", "--memsize", str(args.mem),
                                 "--cpu-exact", "on", "--compatible", "on", "--patch-tos", "off", "--fast-boot", "off",
                                 "--fastfdc", "off", "--disk-a", str(copy), "--sound", "48000", "--fast-forward", "on",
                                 "--confirm-quit", "off", "--statusbar", "off", "--drive-led", "off", "--frameskips", "0",
                                 "--alert-level", "fatal", "--run-vbls", str(args.frames), "--cmd-fifo", str(fifo)],
                                stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, env=env, cwd=work)
    # Ouverture non bloquante : tant qu'Hatari n'a pas ouvert sa fifo, ENXIO, et un open
    # bloquant attendrait à jamais un Hatari mort avant d'y arriver.
    deadline = time.time() + 30
    while proc.poll() is None and time.time() < deadline:
        try:
            fd = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
        except OSError:
            time.sleep(0.02)
            continue
        os.write(fd, b"hatari-shortcut recsound\n")
        os.close(fd)
        break
    proc.wait(timeout=900)


def compare(disk: Path, args) -> dict:
    result = {"disk": disk.name}
    with tempfile.TemporaryDirectory(prefix="neost-sound-") as tmp:
        work = Path(tmp)
        neost_wav, hatari_wav = work / "n.wav", work / "h.wav"
        try:
            run_neost(disk, args, neost_wav)
            run_hatari(disk, args, work, hatari_wav)
            if not hatari_wav.exists():
                return {**result, "verdict": "error", "detail": "no Hatari WAV"}
            a, b = envelope(neost_wav), envelope(hatari_wav)
        except (OSError, subprocess.SubprocessError) as error:
            return {**result, "verdict": "error", "detail": str(error)[:200]}
    active_a = sum(v > SILENCE for v in a) / max(len(a), 1)
    active_b = sum(v > SILENCE for v in b) / max(len(b), 1)
    result.update(neost_s=round(len(a) * WINDOW / RATE, 1), hatari_s=round(len(b) * WINDOW / RATE, 1),
                  neost_active=round(active_a, 3), hatari_active=round(active_b, 3))
    if active_a < MIN_ACTIVE or active_b < MIN_ACTIVE:
        verdict = ("silent" if active_a < MIN_ACTIVE and active_b < MIN_ACTIVE
                   else "neost-silent" if active_a < MIN_ACTIVE else "hatari-silent")
        return {**result, "verdict": verdict}
    corr, shift = best_shift(a, b, args.window)
    loud_a = [v for v in a if v > SILENCE]
    loud_b = [v for v in b if v > SILENCE]
    result["level_ratio"] = round((sum(loud_a) / len(loud_a)) / (sum(loud_b) / len(loud_b)), 3)
    return {**result, "corr": round(corr, 3), "shift_s": round(shift, 2),
            "verdict": "ok" if corr >= args.min_corr else "diverge"}


def main() -> int:
    ap = argparse.ArgumentParser(description="Enveloppe sonore de vrais titres, NeoST contre Hatari")
    ap.add_argument("out", type=Path)
    ap.add_argument("disks", nargs="+", type=Path)
    ap.add_argument("--machine", default="st")
    ap.add_argument("--rom", default="roms/etos192fr.img")
    ap.add_argument("--mem", type=int, default=1)
    ap.add_argument("--frames", type=int, default=2500)
    ap.add_argument("--window", type=float, default=12.0)
    ap.add_argument("--min-corr", type=float, default=0.7)
    ap.add_argument("--jobs", type=int, default=6)
    args = ap.parse_args()
    if not HATARI.exists() or not HEADLESS.exists() or not shutil.which("ffmpeg"):
        print("SKIP: Hatari, neost-headless or ffmpeg missing")
        return 77
    args.out.mkdir(parents=True, exist_ok=True)
    disks = [d.resolve() for d in args.disks]
    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda d: compare(d, args), disks))
    (args.out / "summary.json").write_text(json.dumps(results, indent=1, ensure_ascii=False) + "\n")
    counts: dict[str, int] = {}
    for r in results:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
        if r["verdict"] != "ok":
            print(r)
    print("SOUND:", counts)
    return 0


if __name__ == "__main__":
    sys.exit(main())
