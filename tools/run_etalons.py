#!/usr/bin/env python3
# =============================================================================
#  run_etalons.py — Suite headless des logiciels étalons (captures + régression).
#
#  Workflow :
#    1. python3 tools/fetch_etalons.py          # rapatrie les disques freeware
#    2. python3 tools/run_etalons.py --update-ref   # génère tests/reference/*.ppm
#    3. python3 tools/run_etalons.py            # compare NeoST vs références
#
#  Options :
#    --list              liste les étalons
#    --only ID[,ID…]     sous-ensemble
#    --fetch             fetch avant exécution
#    --update-ref        enregistre la capture NeoST comme référence
#    --oracle            régénère la référence via Hatari (si disque + params)
#    --no-compare        exécute seulement (pas de diff)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = Path(__file__).resolve().parent / "etalons.json"
OUT_DIR = ROOT / "tests" / "out"
REF_DIR = ROOT / "tests" / "reference"
HEADLESS = ROOT / "build" / "neost-headless"
COMPARE = ROOT / "tools" / "compare_screenshot.py"
HATARI_ORACLE = ROOT / "tools" / "hatari_oracle.sh"


def load_manifest() -> list[dict]:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))["etalons"]


def ensure_disk(entry: dict) -> bool:
    gen = entry.get("disk_generate")
    disk = entry.get("disk")
    if not disk:
        return True
    path = ROOT / disk
    if path.exists():
        return True
    if gen:
        script = ROOT / gen
        print(f"  [gen] {script.relative_to(ROOT)} → {path.relative_to(ROOT)}")
        path.parent.mkdir(parents=True, exist_ok=True)
        out_arg = str(path)
        subprocess.run([sys.executable, str(script), out_arg], cwd=ROOT, check=True)
        return path.exists()
    return False


def run_glue_selftest(cpu: str) -> int:
    cmd = [str(HEADLESS), "roms/etos256us.img", "--glue-selftest", "--cpu", cpu]
    print("  $", " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode


def run_headless_capture(entry: dict, out_ppm: Path) -> int:
    rom = entry.get("rom", "roms/etos192us.img")
    cmd = [str(HEADLESS), str(ROOT / rom), "--cpu", entry.get("cpu", "moira"),
           "--machine", entry.get("machine", "ste"),
           "--mem", entry.get("mem", "512k"),
           "--frames", str(entry.get("frames", 200)),
           "--screenshot", str(out_ppm)]
    if entry.get("fastfdc"):
        cmd.append("--fastfdc")
    disk = entry.get("disk")
    if disk:
        cmd += ["--disk", str(ROOT / disk)]
    if entry.get("keys"):
        cmd += ["--keys", entry["keys"]]
    if entry.get("cart"):
        cmd += ["--cart", str(ROOT / entry["cart"])]
    print("  $", " ".join(cmd))
    r = subprocess.run(cmd, cwd=ROOT)
    return r.returncode


def run_hatari_oracle(entry: dict, out_png: Path) -> int:
    if not HATARI_ORACLE.exists():
        print("  [oracle] hatari_oracle.sh introuvable", file=sys.stderr)
        return 1
    rom = str(ROOT / entry.get("rom", "roms/etos192us.img"))
    disk = str(ROOT / entry["disk"])
    vbls = str(entry.get("frames", 400))
    frame = str(entry.get("frame", int(vbls) - 10))
    machine = entry.get("machine", "st")
    cmd = ["bash", str(HATARI_ORACLE), rom, disk, vbls, frame, str(out_png), machine]
    print("  $", " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode


def compare_shots(neost: Path, ref: Path, entry: dict) -> int:
    crop = entry.get("crop", "active")
    mx = entry.get("max_diff_px", 0)
    cmd = [sys.executable, str(COMPARE), str(neost), str(ref),
           "--crop", crop, "--max", str(mx)]
    print("  $", " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode


def run_one(entry: dict, args) -> bool:
    eid = entry["id"]
    print(f"\n=== {eid} — {entry['name']} ===")

    if entry.get("type") == "glue_selftest":
        rc = run_glue_selftest(entry.get("cpu", "moira"))
        if rc != 0:
            print(f"  ÉCHEC glue_selftest (exit {rc})")
            return False
        print("  OK glue_selftest")
        return True

    if entry.get("disk") and not ensure_disk(entry):
        msg = f"  disque manquant : {entry['disk']}"
        if entry.get("optional"):
            print(msg + " (optionnel — SKIP)")
            return True
        print(msg, file=sys.stderr)
        return False

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    neost_ppm = OUT_DIR / f"{eid}_neost.ppm"
    ref_ppm = REF_DIR / f"{eid}.ppm"
    ref_png = REF_DIR / f"{eid}.png"

    if args.oracle and entry.get("disk"):
        REF_DIR.mkdir(parents=True, exist_ok=True)
        tmp_png = OUT_DIR / f"{eid}_oracle.png"
        if run_hatari_oracle(entry, tmp_png) != 0:
            return False
        shutil.copy2(tmp_png, ref_png)
        print(f"  référence oracle → {ref_png.relative_to(ROOT)}")

    rc = run_headless_capture(entry, neost_ppm)
    if rc != 0 or not neost_ppm.exists():
        print(f"  ÉCHEC capture NeoST (exit {rc})")
        return False
    print(f"  capture → {neost_ppm.relative_to(ROOT)}")

    if args.update_ref:
        REF_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copy2(neost_ppm, ref_ppm)
        print(f"  référence → {ref_ppm.relative_to(ROOT)}")
        return True

    if args.no_compare:
        return True

    ref = ref_ppm if ref_ppm.exists() else (ref_png if ref_png.exists() else None)
    if not ref:
        print(f"  SKIP diff : pas de référence ({ref_ppm.name}) — lancer --update-ref")
        return entry.get("optional", False)

    if compare_shots(neost_ppm, ref, entry) != 0:
        print(f"  ÉCHEC diff {eid}")
        return False
    print(f"  OK {eid}")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description="Suite étalons NeoST (headless)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", help="IDs séparés par des virgules")
    ap.add_argument("--fetch", action="store_true", help="fetch_etalons.py d'abord")
    ap.add_argument("--update-ref", action="store_true", help="sauve la capture NeoST en référence")
    ap.add_argument("--oracle", action="store_true", help="capture Hatari comme référence PNG")
    ap.add_argument("--no-compare", action="store_true")
    args = ap.parse_args()

    if not HEADLESS.exists():
        print(f"Build requis : cmake --build build  ({HEADLESS} absent)", file=sys.stderr)
        return 2

    entries = load_manifest()
    if args.list:
        for e in entries:
            opt = " [opt]" if e.get("optional") else ""
            print(f"  {e['id']:20} {e.get('subsystem','?'):16} {e['name']}{opt}")
        return 0

    if args.fetch:
        ids = args.only.split(",") if args.only else []
        cmd = [sys.executable, str(ROOT / "tools" / "fetch_etalons.py")] + ids
        subprocess.run(cmd, cwd=ROOT, check=False)

    want = set(args.only.split(",")) if args.only else None
    ok = True
    for entry in entries:
        if want and entry["id"] not in want:
            continue
        if not run_one(entry, args):
            ok = False
    print("\n" + ("TOUS OK" if ok else "ÉCHECS — voir ci-dessus"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
