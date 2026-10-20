#!/usr/bin/env python3
# =============================================================================
#  run_hatari_suite.py — Rejoue sous NeoST les programmes de la suite de tests
#  d'Hatari (`extern/hatari/tests`) qui écrivent leur verdict dans un fichier.
#
#  POURQUOI. Ces programmes sont écrits par les auteurs d'Hatari, avec un
#  résultat attendu VERSIONNÉ à côté (relevé sur matériel pour une partie
#  d'entre eux, cf. les readme). Ils mesurent des cycles CPU, l'émulation du
#  blitter et la carte des bus errors par machine : une couverture que NeoST
#  n'avait qu'en partie, et jamais sous cette forme.
#
#  COMMENT. Hatari les lance par `--tos none` (démarrage automatique). NeoST
#  n'a pas ce mode : le programme est posé dans C:\AUTO d'un disque hôte
#  (--gemdos) et EmuTOS l'exécute au boot. Le fichier qu'il écrit sur C: est
#  comparé à l'attendu, ligne pour ligne (les lignes « ( … ) » des résultats
#  bus error sont des commentaires, retirés comme dans leur run_test.sh).
#
#  Restent hors banc : cpu/, gemdos/, xbios/, mem_end/ (verdict sur la console
#  de l'émulateur, `--bios-intercept`), serial/ (pas de capture série brute) et
#  screen/ (référence PNG au format de capture d'Hatari).
#
#  extern/hatari est gitignoré : absent → étape SAUTÉE et recensée (code 77).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITE = ROOT / "extern" / "hatari" / "tests"
HEADLESS = ROOT / "build" / ("neost-headless" + (".exe" if sys.platform == "win32" else ""))
EXIT_SKIPPED = 77

ROM_ST, ROM_STE = "roms/etos192fr.img", "roms/etos256fr.img"

# (nom, programme, fichier produit, attendu, machine, mémoire, ROM)
CASES = [("cycles", "cycles/cyccheck.prg", "RESULTS.TXT", "cycles/test_out.txt",
          "st", "1m", ROM_ST),
         ("blitter", "blitter/blitemu.ttp", "BLITEMU.TXT", "blitter/test_out.txt",
          "ste", "1m", ROM_STE)]
for machine, mem, rom, ref in (("st", "1m", ROM_ST, "st"), ("megast", "4m", ROM_ST, "mst4"),
                               ("ste", "1m", ROM_STE, "ste"), ("megaste", "4m", ROM_STE, "mste4")):
    for width in ("b", "w"):
        CASES.append((f"buserror-{machine}-{width}", f"buserror/buserr_{width}.prg",
                      f"BUSERR_{width.upper()}.TXT", f"buserror/results/{ref}_{width}.txt",
                      machine, mem, rom))


def expected_lines(path: Path) -> list[str]:
    return [line for line in path.read_text(errors="replace").splitlines()
            if not line.startswith("(")]


def run_case(name, prg, produced, reference, machine, mem, rom) -> bool:
    with tempfile.TemporaryDirectory(prefix="neost-hatari-suite-") as tmp:
        drive = Path(tmp)
        (drive / "AUTO").mkdir()
        # Seuls les *.PRG du dossier AUTO sont lancés : un .TTP est renommé.
        shutil.copy(SUITE / prg, drive / "AUTO" / (Path(prg).stem.upper() + ".PRG"))
        cmd = [str(HEADLESS), str(ROOT / rom), "--machine", machine, "--mem", mem,
               "--gemdos", str(drive), "--frames", "1500"]
        log = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=300)
        out = drive / produced
        if not out.exists():
            print(f"  FAIL {name}: {produced} absent (code {log.returncode})")
            print("\n".join("    " + l for l in (log.stdout + log.stderr).splitlines()[-5:]))
            return False
        got = out.read_text(errors="replace").splitlines()
        want = expected_lines(SUITE / reference)
        if got == want:
            print(f"  OK   {name} ({len(want)} lignes)")
            return True
        print(f"  FAIL {name}: sortie différente de {reference}")
        for i, (g, w) in enumerate(zip(got, want)):
            if g != w:
                print(f"    ligne {i + 1} : attendu « {w} », obtenu « {g} »")
        if len(got) != len(want):
            print(f"    {len(want)} lignes attendues, {len(got)} obtenues")
        return False


def main() -> int:
    if not SUITE.is_dir():
        print(f"SAUTÉ : {SUITE.relative_to(ROOT)} absent (tools/setup_hatari.sh)")
        return EXIT_SKIPPED
    if not HEADLESS.exists():
        print("Build requis : cmake --build build", file=sys.stderr)
        return 2
    only = set(sys.argv[1:])
    selected = [c for c in CASES if not only or c[0] in only]
    print(f"SUITE HATARI : {len(selected)} programmes rejoués sous NeoST")
    ok = all([run_case(*c) for c in selected])
    print("SUITE HATARI OK" if ok else "SUITE HATARI FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
