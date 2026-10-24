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
#  cpu/ et gemdos/ écrivent leur verdict sur la CONSOLE (Cconws) : Hatari le lit
#  sur sa sortie en `--tos none`, NeoST le relève avec `--console-log` (copie de
#  Cconout/Cconws/Fwrite(1)/Bconout(CON) émis HORS ROM : ni le GEMDOS du TOS
#  repassant par le BIOS, ni le desktop n'y entrent). Verdict de leur run_test.sh
#  (ni « fail » ni « error »), durci : autant de lignes « Test '…' OK » que
#  d'entrées dans la table tests[] du source — un programme qui s'arrête en route
#  ne passe pas.
#
#  Restent hors banc : xbios/ et mem_end/ (ils appellent des extensions propres
#  à Hatari — Dbmsg en XBIOS 11, Scrdmp intercepté en capture —, pas du matériel
#  ST), serial/ (pas de capture série brute) et screen/ (référence PNG au format
#  de capture d'Hatari).
#
#  extern/hatari est gitignoré : absent → étape SAUTÉE et recensée (code 77).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import re
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

# (nom, programme, source C portant la table tests[], machine, mémoire, ROM)
CONSOLE_CASES = [("cpu-68000", "cpu/int_test.tos", "cpu/int_test.c", "st", "1m", ROM_ST),
                 ("gemdos", "gemdos/gmdostst.tos", "gemdos/gmdostst.c", "st", "1m", ROM_ST)]


def expected_lines(path: Path) -> list[str]:
    return [line for line in path.read_text(errors="replace").splitlines()
            if not line.startswith("(")]


def run_auto(drive: Path, prg, machine, mem, rom, extra=()) -> subprocess.CompletedProcess:
    (drive / "AUTO").mkdir()
    # Seuls les *.PRG du dossier AUTO sont lancés : un .TTP/.TOS est renommé.
    shutil.copy(SUITE / prg, drive / "AUTO" / (Path(prg).stem.upper() + ".PRG"))
    cmd = [str(HEADLESS), str(ROOT / rom), "--machine", machine, "--mem", mem,
           "--gemdos", str(drive), "--frames", "1500", *extra]
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=300)


def run_console_case(name, prg, source, machine, mem, rom) -> bool:
    with tempfile.TemporaryDirectory(prefix="neost-hatari-suite-") as tmp:
        drive, console = Path(tmp) / "C", Path(tmp) / "console.txt"
        drive.mkdir()
        log = run_auto(drive, prg, machine, mem, rom, ("--console-log", str(console)))
        text = console.read_text(errors="replace") if console.exists() else ""
        tests = [l.strip() for l in text.splitlines() if l.startswith("Test '")]
        want = len(re.findall(r'^\t\{ "', (SUITE / source).read_text(), re.M))
        bad = [l for l in tests if not l.endswith(" OK")]
        if re.search("fail|error", text, re.I) or bad or len(tests) != want:
            print(f"  FAIL {name}: {len(tests) - len(bad)}/{want} tests OK (code {log.returncode})")
            print("\n".join("    " + l for l in (bad or text.splitlines()[-5:])))
            return False
        print(f"  OK   {name} ({want} tests)")
        return True


def run_case(name, prg, produced, reference, machine, mem, rom) -> bool:
    with tempfile.TemporaryDirectory(prefix="neost-hatari-suite-") as tmp:
        drive = Path(tmp)
        log = run_auto(drive, prg, machine, mem, rom)
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
    console = [c for c in CONSOLE_CASES if not only or c[0] in only]
    print(f"SUITE HATARI : {len(selected) + len(console)} programmes rejoués sous NeoST")
    ok = all([run_case(*c) for c in selected] + [run_console_case(*c) for c in console])
    print("SUITE HATARI OK" if ok else "SUITE HATARI FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
