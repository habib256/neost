#!/usr/bin/env python3
# =============================================================================
#  make_ref_traces.py — Traces de référence Hatari (et NeoST) pour tools/trace_diff.py.
#
#  POURQUOI. trace_diff.py compare deux traces du même titre, mais chaque enquête
#  refaisait sa capture Hatari à la main (fenêtre, entrées, drapeaux), sans trace de la
#  recette. Ce pilote les produit à la demande, pour les titres de
#  tests/traces/ref_traces.json, avec les MÊMES entrées que le parcours joué
#  (`replay` : un id de tests/replays/replays.json — touches, souris, joystick) ou, sans
#  `replay`, les champs d'un parcours écrits dans l'entrée (disk, machine, rom, mem, keys,
#  scancodes, mouse, auto…). Les changements de disquette ne sont pas encore pris en charge.
#
#  CE QUE C'EST. Une référence `hatari` (grade D), pas une preuve matérielle : un accord
#  dit « Hatari exécute la même chose », un écart se tranche selon CLAUDE.md (comparer les
#  sources, puis la fiche ou le fer). Les traces dérivent des médias privés (jeux, TOS
#  Atari) : elles ne se versionnent JAMAIS. Seule la recette l'est ; les traces se
#  régénèrent, comme les captures Hatari de run_game_replay.py.
#
#  COMMENT. Pour chaque entrée : NeoST trace les trames [neost_from, neost_from + frames]
#  (`--trace --trace-from --regs --irq`) ; Hatari, épinglé, trace les VBL
#  [hatari_from, hatari_from + frames + marge] (`cpu_disasm,cpu_regs`, armé et coupé par
#  son débogueur). Les deux fenêtres diffèrent du décalage mesuré entre les émulateurs
#  (angle de disquette, chargement). trace_diff.py aligne ensuite sur `align_pc`, flux
#  puis registres. Sorties dans --out (défaut build/ref-traces/, hors dépôt) :
#    <id>/neost.trace, <id>/hatari.trace, <id>/flow.txt, <id>/registers.txt,
#    <id>/capture.json (commandes, empreintes des médias, des binaires, révision Hatari).
#
#  Usage : make_ref_traces.py [--only ID[,ID…]] [--out DOSSIER] [--list]
#  Codes : 0 = toutes les captures faites (PAS « aucune divergence » : lire flow.txt),
#          1 = une capture a échoué, 2 = usage, 77 = Hatari, neost-headless ou Pillow
#          absent, ou aucun média présent.
#  Pillow : exigé par le pilote d'entrées de compare_titles.py (/usr/bin/python3 sous
#  macOS ; le python3 de Homebrew ne l'a pas).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
MANIFEST = ROOT / "tests" / "traces" / "ref_traces.json"
REPLAYS = ROOT / "tests" / "replays" / "replays.json"
HATARI_FLAGS = "cpu_disasm,cpu_regs"
MARGIN = 2           # VBL de plus côté Hatari : l'ancre peut tomber un peu plus tard


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source(entry: dict, replays: dict) -> dict:
    """Le parcours dont l'entrée reprend les entrées : `replay` (tests/replays/replays.json)
    ou, à défaut, les champs du parcours écrits dans l'entrée elle-même (disk, rom…)."""
    return dict(replays[entry["replay"]]) if "replay" in entry else dict(entry)


def entry_args(entry: dict, replays: dict, work: Path):
    """Arguments compare_titles/title_cmd du parcours de référence : mêmes entrées."""
    import run_game_replay as G
    import title_cmd as T
    r = source(entry, replays)
    end = entry["neost_from"] + entry["frames"]
    r["frames"] = [end]
    argv = [str(work / "out"), r["disk"], *G.compare_argv(r, work)]
    return r, T.parse(argv)


def hatari_cmd(r: dict, args, work: Path, disk: Path, run_vbls: int) -> list[str]:
    """Même configuration que compare_titles.hatari_shots (TOS original, FDC lent)."""
    from compare_titles import HATARI
    import title_cmd as T
    cmd = [str(HATARI), "--machine", args.machine, "--tos", str(ROOT / args.rom), "--monitor", "rgb",
           "--memsize", T.hatari_memsize(args.mem), "--cpu-exact", "on", "--compatible", "on",
           "--patch-tos", "off", "--fast-boot", "off", "--fastfdc", "off",
           "--disk-a", str(disk),
           "--sound", "25033", "--fast-forward", "on", "--confirm-quit", "off", "--statusbar", "off",
           "--drive-led", "off", "--frameskips", "0", "--alert-level", "fatal",
           "--run-vbls", str(run_vbls), "--parse", str(work / "start.ini"),
           "--trace-file", str(work / "hatari.trace")]
    if args.disk_b:
        cmd += ["--disk-b", str(T.swap_copy(args.disk_b, work, "hb"))]
    if args.auto:
        cmd += ["--auto", args.auto]
    return cmd


def capture(entry: dict, replays: dict, out: Path) -> dict:
    import compare_titles as C
    import title_cmd as T
    work = out / entry["id"]
    if work.exists():
        shutil.rmtree(work)                     # jamais une ancienne trace prise pour la nouvelle
    work.mkdir(parents=True)
    r, args = entry_args(entry, replays, work)
    disk = ROOT / r["disk"]
    media = [disk, ROOT / r["rom"], *[ROOT / d for _, d in r.get("swaps", [])]]
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in media}
    if args.joy:                                # joystick : la même suite de masques des deux côtés
        compiled = work / "joy.bin"
        subprocess.run([str(C.HEADLESS), args.rom, "--joy-script-compile", str(compiled),
                        "--joy-script-file", str(args.joy[0]), str(args.joy[1])],
                       cwd=ROOT, capture_output=True, text=True, check=True)
        args.joy = (args.joy[0], args.joy[1], compiled)

    # --- NeoST -----------------------------------------------------------------
    neo = T.neost_cmd(disk, args, work)
    neo += ["--trace", str(work / "neost.trace"), "--trace-from", str(entry["neost_from"]),
            "--regs", "--irq"]
    env = {k: v for k, v in os.environ.items() if not k.startswith(("NEOST_", "HATARI_"))}
    done = subprocess.run(neo, cwd=ROOT, capture_output=True, text=True, errors="replace",
                          timeout=3600, env=env)
    (work / "neost.log").write_text(done.stdout + done.stderr)
    done.check_returncode()

    # --- Hatari ----------------------------------------------------------------
    h0 = entry["hatari_from"]
    h1 = h0 + entry["frames"] + MARGIN
    (work / "start.ini").write_text(f"b VBL = {h0} :once :trace :file {work / 'begin.ini'}\n")
    (work / "begin.ini").write_text(f"trace {entry.get('hatari_trace', HATARI_FLAGS)}\n"
                                    f"b VBL = {h1} :once :trace :file {work / 'end.ini'}\n")
    (work / "end.ini").write_text("trace none\nquit\n")
    copy = work / ("h" + disk.suffix)
    shutil.copy(disk, copy)
    hat = hatari_cmd(r, args, work, copy, h1 + 2)
    henv = dict(env, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", HATARI_SEED=str(args.hatari_seed))
    shift = lambda at: at + args.hatari_delay if at >= args.hatari_delay_from else at
    if args.joy:
        henv.update(NEOST_JOY_SCRIPT=str(args.joy[2]), NEOST_JOY_START=str(shift(args.joy[0])))
    if args.swap:
        raise SystemExit(f"{entry['id']}: disk swaps are not supported by this driver yet")
    keys = [(shift(at), code) for at, code in args.key if shift(at) < h1]
    mouse = [(shift(at), s) for at, s in args.mouse if shift(at) < h1]
    if keys or mouse:
        C.run_hatari_keys(hat, keys, work, henv, args.key_hold, mouse, args.mouse_step)
    else:
        done = subprocess.run(hat, capture_output=True, text=True, errors="replace", timeout=3600,
                              env=henv, cwd=work, stdin=subprocess.DEVNULL)
        (work / "hatari.log").write_text(done.stdout + done.stderr)
        done.check_returncode()
    for p, digest in hashes.items():
        if sha(ROOT / p) != digest:
            raise RuntimeError(f"source media changed: {p}")

    # --- Comparaison -------------------------------------------------------------
    verdicts = {}
    for name, extra in (("flow", []), ("registers", ["--regs"])):
        cmd = [sys.executable, str(ROOT / "tools" / "trace_diff.py"), str(work / "neost.trace"),
               str(work / "hatari.trace"), "--no-color", "-C", "6", "--allow-prefix"]
        if entry.get("align_pc"):
            cmd += ["--align-pc", entry["align_pc"]]
        res = subprocess.run(cmd + extra, capture_output=True, text=True)
        (work / f"{name}.txt").write_text(res.stdout + res.stderr)
        verdicts[name] = {0: "identical", 1: "diverges"}.get(res.returncode, f"error {res.returncode}")
    hatari_dir = ROOT / "extern" / "hatari"
    rev = subprocess.run(["git", "-C", str(hatari_dir), "rev-parse", "HEAD"],
                         capture_output=True, text=True).stdout.strip()
    report = dict(id=entry["id"], replay=entry.get("replay"), note=entry.get("note", ""),
                  neost_window=[entry["neost_from"], entry["neost_from"] + entry["frames"]],
                  hatari_window=[h0, h1], align_pc=entry.get("align_pc"),
                  hatari_trace=entry.get("hatari_trace", HATARI_FLAGS),
                  media_sha256=hashes, hatari_revision=rev,
                  binaries_sha256={str(p.relative_to(ROOT)): sha(p) for p in (C.HEADLESS, C.HATARI)},
                  commands=dict(neost=neo, hatari=hat), verdicts=verdicts,
                  halted=dict(neost="CPU halted" in (work / "neost.log").read_text(errors="replace")),
                  limits=["Hatari agreement is not hardware validation.",
                          "Windows are matched by a measured frame offset, not by pin cycles."])
    (work / "capture.json").write_text(json.dumps(report, indent=1, ensure_ascii=False) + "\n")
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description="Traces de référence Hatari pour trace_diff.py")
    ap.add_argument("--only", help="identifiants séparés par des virgules")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "ref-traces")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    entries = json.loads(MANIFEST.read_text())["traces"]
    replays = {r["id"]: r for r in json.loads(REPLAYS.read_text())["replays"]}
    if a.list:
        for e in entries:
            print(f'{e["id"]:28} replay={e.get("replay", "-"):24} NeoST {e["neost_from"]}+{e["frames"]}'
                  f'  Hatari {e["hatari_from"]}  ancre {e.get("align_pc", "-")}')
        return 0
    wanted = set(a.only.split(",")) if a.only else None
    if wanted and wanted - {e["id"] for e in entries}:
        ap.error("unknown id: " + ", ".join(sorted(wanted - {e["id"] for e in entries})))
    try:
        import compare_titles as C
    except SystemExit:                          # compare_titles sort en 77 sans Pillow
        print("SKIP — make_ref_traces : Pillow absent")
        return 77
    if not C.HATARI.exists() or not C.HEADLESS.exists():
        print("SKIP — make_ref_traces : Hatari ou neost-headless absent")
        return 77
    os.chdir(ROOT)
    ran = failed = 0
    for e in entries:
        if wanted and e["id"] not in wanted:
            continue
        r = source(e, replays)
        missing = [p for p in (r["disk"], r["rom"]) if not (ROOT / p).exists()]
        if missing:
            print(f'SKIP  {e["id"]:28} missing {", ".join(missing)}')
            continue
        ran += 1
        try:
            rep = capture(e, replays, a.out.resolve())
            print(f'OK    {e["id"]:28} flow={rep["verdicts"]["flow"]} '
                  f'registers={rep["verdicts"]["registers"]}  → {a.out / e["id"]}')
        except Exception as exc:                # noqa: BLE001 — une capture ratée n'arrête pas les autres
            failed += 1
            print(f'FAIL  {e["id"]:28} {type(exc).__name__}: {str(exc)[-300:]}')
    if not ran:
        print("SKIP — make_ref_traces : aucun média présent")
        return 77
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
