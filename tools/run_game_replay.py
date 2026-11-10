#!/usr/bin/env python3
# =============================================================================
#  run_game_replay.py — Des parties jouées, identiques à l'image près avec Hatari.
#
#  POURQUOI. Les étalons voient des écrans fixes et des démos. Une partie fait passer entrées, IRQ, FDC et
#  vidéo ensemble, sur des milliers de trames : une dérive d'un cycle finit par changer le jeu.
#
#  COMMENT. tests/replays/replays.json donne le disque, les touches, les changements de disquette et un
#  script joystick. Hatari et NeoST
#  jouent les mêmes masques (tools/compare_titles.py --joy), et chaque trame demandée doit rester sous
#  max_diff %. Échelon `hatari` : un vert dit « d'accord avec Hatari », pas « comme le fer ».
#  Les diagnostics d'échecs connus sont exclus par défaut, demandés par --only et
#  comptés à part. Ils ne valident jamais la compatibilité d'un jeu.
#  --manifest permet aussi les parcours natifs ref_kind=snapshot : image finale
#  inspectée + absence de halt + médias intacts, comptés séparément de Hatari.
#
#  Usage : run_game_replay.py [--only ID]
#  Code 77 sans Pillow, sans Hatari, ou si aucun disque n'est présent.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def play_snapshot(r: dict, work: Path) -> tuple[int, list[str]]:
    """Parcours natif inspecté : aucune conclusion d'accord avec Hatari ou le matériel."""
    try:
        from PIL import Image
    except ImportError:
        return 77, ["Pillow missing"]
    for path, expected in r["media_sha256"].items():
        if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != expected:
            return 1, [f"Unexpected source media: {path}"]
    shot = work / "native.ppm"
    cmd = [str(ROOT / "build/neost-headless"), *r["args"], "--screenshot", str(shot)]
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=1800)
    for path, expected in r["media_sha256"].items():
        if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != expected:
            return 1, [f"Source media changed during replay: {path}"]
    if done.returncode != 0 or "CPU halted" in done.stdout + done.stderr:
        return 1, [f"Native replay failed or halted: {done.stderr.strip()[-400:]}"]
    if not shot.exists():
        return 1, ["Missing final native capture"]
    with Image.open(shot) as img:
        rgb = img.convert("RGB")
        if list(rgb.size) != r["size"] or hashlib.sha256(rgb.tobytes()).hexdigest() != r["rgb_sha256"]:
            return 1, ["Unexpected final scene (snapshot checkpoint)"]
    return 0, ["snapshot only; no hardware or Hatari verdict"]


def compare_argv(r: dict, work: Path) -> list[str]:
    """Options de compare_titles.py pour un parcours du manifeste (sans SORTIE ni DISQUE).

    Partagé avec cpu_swap_gate.py, qui rejoue la même partie dans NeoST seul.
    """
    cmd = ["--machine", r["machine"], "--rom", r["rom"], "--mem", str(r["mem"]),
           "--frames", ",".join(map(str, r["frames"])), "--window", str(r.get("window", 6)), "--step", str(r.get("step", 2)),
           "--jobs", "1"]
    for field in ("fdc_seed", "hatari_seed", "key_hold", "offset", "mouse_step", "hatari_delay", "hatari_delay_from"):
        if field in r:
            cmd += ["--" + field.replace("_", "-"), str(r[field])]
    if "joy" in r:                                     # sans joystick : un titre qui se joue à la souris
        joy = work / "joy.txt"
        joy.write_text(" ".join([r["joy"]] * r.get("joy_repeat", 1)) + "\n")
        cmd += ["--joy", f'{r["joy_at"]}:{joy}']
    if "auto" in r:                                    # programme lancé sans passer par le bureau : TOS 1.04 ou plus
        cmd += ["--auto", r["auto"]]
    if "disk_b" in r:
        cmd += ["--disk-b", r["disk_b"]]
    for at in r.get("keys", []):
        cmd += ["--key", f"{at}:57"]
    for at, code in r.get("scancodes", []):            # autre touche que l'espace : [trame, scancode décimal]
        cmd += ["--key", f"{at}:{code}"]
    for at, script in r.get("mouse", []):
        cmd += ["--mouse", f"{at}:{script}"]
    for at, disk in r.get("swaps", []):                # changement de disquette : [trame, chemin]
        cmd += ["--swap", f"{at}:{disk}"]
    return cmd


def play(r: dict, work: Path) -> tuple[int, list[str]]:
    if r.get("ref_kind") == "snapshot":
        return play_snapshot(r, work)
    cmd = [sys.executable, str(ROOT / "tools" / "compare_titles.py"), str(work / "out"), r["disk"],
           *compare_argv(r, work)]
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=1800)
    if done.returncode == 77:
        return 77, [done.stdout.strip()]
    if done.returncode != 0:
        return 1, [done.stderr.strip()[-400:]]
    row = json.loads((work / "out" / "summary.json").read_text())[0]
    return assess(r, row)


def assess(r: dict, row: dict) -> tuple[int, list[str]]:
    """Un accord pixel ne suffit pas si le CPU est arrêté ou une capture manque."""
    if row.get("error"):
        return 1, [row["error"]]
    if not r.get("diagnostic"):
        halted = [name for name in ("neost", "hatari") if row.get(f"{name}_halted")]
        if halted:
            return 1, [f'CPU halted: {", ".join(halted)}']
    missing = set(map(str, r["frames"])) - row["frames"].keys()
    if missing:
        return 1, [f'Missing frames: {", ".join(sorted(missing))}']
    # Échelon snapshot, distinct de l'accord Hatari : les étapes inspectées
    # empêchent deux titres/menus identiques de valider une prétendue course.
    for frame, expected in r.get("scene_hashes", {}).items():
        if row["frames"].get(str(frame), {}).get("neost_rgb_sha256") != expected:
            return 1, [f"Unexpected scene at frame {frame} (snapshot checkpoint)"]
    bad = [f'trame {f} : {v["diff"]} %' for f, v in row["frames"].items() if v["diff"] > r["max_diff"]]
    return (1 if bad else 0), bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--only")
    ap.add_argument("--manifest", type=Path, default=ROOT / "tests/replays/replays.json")
    a = ap.parse_args()
    replays = json.loads(a.manifest.read_text())["replays"]
    selected = set(a.only.split(",")) if a.only else None
    if selected:
        unknown = selected - {r["id"] for r in replays}
        if unknown:
            ap.error(f'Unknown replay IDs: {", ".join(sorted(unknown))}')
    ran = failed = diagnostics = snapshots = 0
    for r in replays:
        if selected and r["id"] not in selected:
            continue
        if r.get("diagnostic") and not selected:
            print(f'SKIP {r["id"]}: diagnostic only; select it explicitly with --only')
            continue
        if not (ROOT / r["disk"]).exists() or not (ROOT / r["rom"]).exists():
            print(f'SKIP {r["id"]} : disque ou ROM absents')
            continue
        with tempfile.TemporaryDirectory() as tmp:
            code, notes = play(r, Path(tmp))
        if code == 77:
            print(f'SKIP {r["id"]} : {notes[0]}')
            continue
        ran += 1
        diagnostics += bool(r.get("diagnostic"))
        snapshots += r.get("ref_kind") == "snapshot" and not r.get("diagnostic")
        failed += code != 0
        verdict = "DIAG" if r.get("diagnostic") else "SNAP" if r.get("ref_kind") == "snapshot" else "OK  "
        print(f'{verdict if code == 0 else "FAIL"} {r["id"]} {"; ".join(notes)}')
    if not ran:
        return 77
    native = f", {snapshots} snapshot" if snapshots else ""
    print(f'REPLAYS: {ran - diagnostics - snapshots} compatibility{native}, {diagnostics} diagnostic, {failed} failed')
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
