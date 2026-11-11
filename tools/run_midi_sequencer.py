#!/usr/bin/env python3
"""
Étalon « séquenceur MIDI » : Cubase Lite joue un morceau, on vérifie ce qui sort.

Scénario rejoué en headless (TOS 1.04 FR, Mega ST 1 Mo, mono) :
  1. boot sur un lecteur GEMDOS temporaire (copie de disks/midi/CUBLITE + DESKTOP.INF
     dont la ligne `#Z` auto-lance CB_LITE.PRG — fonction « Install Application /
     auto boot » du TOS 1.04) ;
  2. souris : File → Import… ; Return sur « Did you save the arrangement ? » ;
     SONG.MID + Return dans le sélecteur GEM (clavier AZERTY du TOS FR, --azerty) ;
  3. Enter du pavé numérique = Play ;
  4. `--midi-dump` journalise chaque octet MIDI OUT daté du cycle 68000 ;
  5. tools/midi_compare.py confronte notes, vélocités, durées, tempo et pédale au SMF.

Ce que ça garantit : ACIA 6850 (TDRE/TIE à 31 250 bauds), Timer A du MFP (horloge de
MROS), GEMDOS HD (Pexec, Fopen/Fread), et le convertisseur midi_simplify.py — toute
régression de tempo ou de note y est visible. ⚠ Il dépend des POSITIONS de menu de
Cubase Lite à 640×400 : si l'import ne démarre pas, relancer avec --keep pour
regarder la capture.

Usage : run_midi_sequencer.py [--song SMF] [--frames N] [--max-notes N] [--keep]
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Suffixe .exe sous Windows : sans lui, le binaire est cherché sous un nom qui
# n'existe pas et la suite se déclare « non bâtie » — la seule plateforme livrée
# où les tests ne pouvaient pas tourner du tout.
_EXE = ".exe" if sys.platform == "win32" else ""
HEADLESS = ROOT / "build" / ("neost-headless" + _EXE)
ROM = ROOT / "roms" / "tos104fr.img"
CUBLITE = ROOT / "disks" / "midi" / "CUBLITE"
DESKTOP_INF = ROOT / "disks" / "midi" / "DESKTOP.INF"
DEFAULT_SONG = ROOT / "disks" / "midi" / "BLUES" / "ALBERTAM.MID"
# Code de sortie « sauté, recensé » (cf. run_selftests.py) : run_all.py le distingue
# d'un succès ET d'un échec, et le fait remonter dans son bilan de fin.
EXIT_SKIPPED = 77

# Cubase est chargé vers la trame 1200 ; la souris part du centre (322,204) vers
# « File » (90,9) — le déplacement vertical est amorti par GEM, d'où l'excédent de U —
# puis descend sur « Import… » (y≈123 ; « Export… » est juste dessous).
MOUSE_AT = 1250
MOUSE = "L" * 29 + "U" * 32 + "RR" + "D" * 16 + "...1111......."
CONFIRM_AT, SELECT_AT, PLAY_AT = 1400, 1450, 1800


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--song", type=Path, default=DEFAULT_SONG, help="SMF déjà simplifié (8.3)")
    ap.add_argument("--frames", type=int, default=4000, help="≈ 56 s à 71 Hz (défaut 4000)")
    ap.add_argument("--max-notes", type=int, default=200,
                    help="notes comparées (la lecture est tronquée par --frames)")
    ap.add_argument("--tol-ms", type=float, default=5.0)
    ap.add_argument("--keep", action="store_true", help="garde le répertoire de travail")
    a = ap.parse_args()

    if not HEADLESS.exists():
        print(f"Build requis : cmake --build build  ({HEADLESS} absent)", file=sys.stderr)
        return 2
    # Le TOS 1.04 FR reste non redistribuable. Cubase Lite est le freeware Steinberg
    # du 26.04.1999 : la disquette disks/cubase_lite.st part dans les paquets, et
    # l'arbre GEMDOS disks/midi/CUBLITE (gitignoré) est ce que ce scénario monte.
    # ⚠ Cet étalon ne peut PAS migrer sur EmuTOS : le scénario repose sur
    # l'auto-lancement `#Z` de DESKTOP.INF (« Install Application » du TOS 1.04).
    # EmuTOS lit EMUDESK.INF et ne le déclenche pas — mesuré le 2026-08-28, on
    # reste sur le bureau, 0 octet MIDI. MROS panique aussi sous EmuTOS.
    skipped = False
    if not ROM.exists():
        print(f"  SKIP recensé : {ROM.relative_to(ROOT)} absent (non redistribuable "
              "— restauration : tools/private_assets.sh unpack)")
        skipped = True
    if not CUBLITE.exists():
        print(f"  SKIP recensé : {CUBLITE.relative_to(ROOT)} absent "
              "(arbre GEMDOS ; la disquette est disks/cubase_lite.st)")
        skipped = True
    if skipped:
        print("  ⚠ le séquenceur MIDI n'a PAS tourné — couverture amputée, pas verte.")
        return EXIT_SKIPPED
    for q in (DESKTOP_INF, a.song):
        if not q.exists():
            print(f"absent : {q}", file=sys.stderr)
            return 2

    work = Path(tempfile.mkdtemp(prefix="neost-midi-"))
    rc = 1
    try:
        hd = work / "C"
        shutil.copytree(CUBLITE, hd / "CUBLITE")
        shutil.copy(DESKTOP_INF, hd / "DESKTOP.INF")
        shutil.copy(a.song, hd / "CUBLITE" / "SONG.MID")
        log, shot = work / "midi.log", work / "end.ppm"
        # --disk explicite : les dates de --mouse-at et --keys-at ont été calées avec la
        # disquette publique dans A, que le headless montait d'office jusqu'au 2026-11-11.
        # --disk-ro : l'invité n'y voit aucune différence, le fichier livré reste intact.
        cmd = [str(HEADLESS), str(ROM), "--machine", "megast", "--mem", "1m", "--mono",
               "--disk", str(ROOT / "disks" / "diskA.st"), "--disk-ro",
               "--gemdos", str(hd), "--frames", str(a.frames), "--azerty",
               "--mouse-at", str(MOUSE_AT), MOUSE,
               "--keys-at", str(CONFIRM_AT), "\n",
               "--keys-at", str(SELECT_AT), "SONG.MID\n",
               "--keys-at", str(PLAY_AT), "|",
               "--midi-dump", str(log), "--screenshot", str(shot)]
        try:
            # Garde-fou A18 : 4000 trames GEMDOS, ~5 s mesurées — 600 s = marge CI ×100.
            r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=600)
        except subprocess.TimeoutExpired:
            print("  TIMEOUT après 600s : " + " ".join(map(str, cmd)), file=sys.stderr)
            return 2
        tail = [ln for ln in r.stderr.splitlines() if "MIDI OUT" in ln or "cannot" in ln]
        print("\n".join(tail), flush=True)
        if r.returncode != 0 or not log.exists():
            print(f"headless rc={r.returncode}", file=sys.stderr)
            print(r.stderr[-2000:], file=sys.stderr)
            return 1
        rc = subprocess.run([sys.executable, str(ROOT / "tools" / "midi_compare.py"), str(log),
                             str(a.song), "--max-notes", str(a.max_notes),
                             "--min-notes", str(a.max_notes), "--tol-ms", str(a.tol_ms)],
                            cwd=ROOT).returncode
        if rc != 0 or a.keep:
            print(f"répertoire de travail : {work}  (journal midi.log, capture end.ppm)")
        return rc
    finally:
        if not a.keep and rc == 0:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
