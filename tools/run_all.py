#!/usr/bin/env python3
# =============================================================================
#  run_all.py — Orchestrateur des suites de test NeoST par PALIERS.
#
#    --tier fast : P0 (auto-tests logique : glue + spec512) + P1 (verdicts série).
#                  Secondes, sans oracle ni disque externe → garde de commit / hook.
#    --tier full : fast + P2 (tous les étalons pixel + contrôle de provenance des réfs).
#                  Nécessite les disques/oracles présents (les optionnels absents = SKIP).
#
#    --install-hook / --uninstall-hook : (dé)installe un hook git pre-push (opt-in)
#                  qui lance « run_all.py --tier fast » avant chaque push.
#
#  Chaque étape est une sous-suite existante ; run_all agrège les codes de sortie.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Suffixe .exe sous Windows : sans lui, le binaire est cherché sous un nom qui
# n'existe pas et la suite se déclare « non bâtie » — la seule plateforme livrée
# où les tests ne pouvaient pas tourner du tout.
_EXE = ".exe" if sys.platform == "win32" else ""
TOOLS = ROOT / "tools"
HEADLESS = ROOT / "build" / ("neost-headless" + _EXE)
SELFTEST = ROOT / "build" / ("neost-selftest" + _EXE)
GUI = ROOT / "build" / ("neost" + _EXE)


def selftest_ids() -> str:
    # A19 (audit 2026-08-27) : cette liste était CODÉE EN DUR — et elle avait produit
    # un ORPHELIN : serloop_selftest, présent au manifeste, n'était exécuté par AUCUN
    # palier, ni localement ni en CI, sans qu'aucun garde-fou puisse le voir (le refus
    # d'ID inconnu de run_etalons attrape les IDs SUPPRIMÉS, pas les oubliés).
    # Sélection PAR TYPE depuis etalons.json : un selftest ajouté au manifeste est
    # exécuté d'office.
    man = json.loads((TOOLS / "etalons.json").read_text())
    entries = man["etalons"] if isinstance(man, dict) and "etalons" in man else man
    ids = [e["id"] for e in entries if str(e.get("type", "")).endswith("_selftest")]
    if not ids:
        print("etalons.json : AUCUN *_selftest — manifeste cassé ?", file=sys.stderr)
        sys.exit(2)
    return ",".join(ids)


# Étapes par palier : (label, [argv]). Exécutées dans l'ordre ; tout code ≠ 0 = échec.
FAST = [
    # Logique PURE, sans machine ni ROM (chemins hôte, parseurs, formats) : le seul
    # palier qui exerce la sémantique de chemins WINDOWS depuis un Mac ou une CI
    # Linux. Sans lui, l'issue #37 restait invisible partout où on développe.
    ("P0 auto-test logique pure (hostpath — sémantiques POSIX ET Windows)",
     [str(SELFTEST)]),
    ("P0 auto-tests logique (tous les *_selftest du manifeste etalons.json)",
     [sys.executable, str(TOOLS / "run_etalons.py"), "--only", selftest_ids()]),
    # A30 : fuzzing des parseurs d'images disquette. decodeMsa/decodeDim/StxImage
    # ::parse sont les seules fonctions qui digèrent un fichier venu de l'extérieur.
    # 20 000 itérations déterministes (graine fixe) — le verdict est reproductible,
    # et le job `sanitizers` de la CI rend le harnais MORDANT (ASan/UBSan).
    ("Fuzzing des parseurs d'images disquette (.msa / .dim / .stx)",
     [str(ROOT / "build" / ("neost-fuzz-disk" + _EXE))]),
    # A20 : WRITE TRACK STX (reinterpretSaveTrack + round-trip .wd1772) sur une image
    # FORGÉE en mémoire — le seul test du parseur Pasti, resté EXCLUDE_FROM_ALL et
    # jamais lancé jusqu'à l'audit du 2026-08-27.
    ("P0 STX WRITE TRACK (image forgée, ré-interprétation + round-trip .wd1772)",
     [str(ROOT / "build" / ("neost-stx-test" + _EXE))]),
    # A27 : la boucle rapide était AVEUGLE au rendu — « avant de conclure, --tier
    # full » n'était qu'une discipline humaine, et des commits ont déjà sur-promis
    # sur cette base. Quatre étalons pixel COURTS (250-400 trames, ~2 s en parallèle),
    # quatre sous-systèmes distincts (bordures Glue, exceptions CPU, scroll STE,
    # blitter/ordonnanceur), TOUS sur ROM EmuTOS libre : le palier fast reste
    # prêt-à-purge (§ BLOQUANT RELEASE).
    ("Pixels rapides (overscan_top + trace_odd + scroll_8264 + blitter_timer)",
     [sys.executable, str(TOOLS / "run_etalons.py"), "--only",
      "overscan_top,trace_odd,scroll_8264,blitter_timer"]),
    # Intégrité des ANCRES de la doc (chantier A7). Instantané, aucune machine : la
    # convention du projet veut que le SYMBOLE fasse foi (les fichier:ligne dérivent),
    # or rien ne vérifiait qu'ils existent encore. Après le renommage
    # Blitter::stallCpu → billCycles, l'inventaire pointait dans le vide.
    ("Ancres de documentation (les symboles cités existent-ils encore ?)",
     [sys.executable, str(TOOLS / "check_doc_anchors.py")]),
    # Pictogrammes de l'interface : une icône se perd en SILENCE, de deux façons —
    # codepoint absent de la police d'icônes, ou revendiqué AUSSI par la police de
    # texte (DejaVu Sans occupe U+F000-F003, ImGui garde la 1re source qui sait
    # fournir le glyphe). Vécu le 2026-08-30 sur la note de la page MIDI. Logique
    # pure, sans machine ni ROM : sa place est ici.
    ("Pictogrammes de l'interface (présents dans la police, non masqués)",
     [sys.executable, str(TOOLS / "check_icon_glyphs.py")]),
    # A26 : les ancres gardent les SYMBOLES, ceci garde les CHIFFRES — la dérive
    # constatée par l'audit (huit affirmations périmées corrigées à la main) était
    # précisément là où personne ne recompte.
    ("Affirmations chiffrées de la doc (les nombres cités disent-ils encore vrai ?)",
     [sys.executable, str(TOOLS / "check_doc_claims.py")]),
    # Purge § BLOQUANT, pas 5 (2026-08-28) : les ancres gardent les symboles, les
    # chiffres gardent les nombres, ceci garde les LICENCES. Huit jobs de CI
    # vérifiaient que les fichiers de licence accompagnent les paquets ; aucun ne
    # lisait leur contenu, et GLFW — statique dans tous les paquets de bureau —
    # n'y était nommé nulle part.
    ("Composants tiers livrés (chacun est-il nommé, avec sa licence ?)",
     [sys.executable, str(TOOLS / "check_licenses.py")]),
    # A34 : les ~83 verrous NEOST_* du cœur sont-ils tous CLASSÉS — comportement
    # d'émulation ou simple trace ? Un verrou de comportement non recensé, c'est
    # quelque chose qui peut changer l'émulation sans qu'aucun fichier ne le dise.
    ("Verrous NEOST_* du cœur (comportement d'émulation vs trace)",
     [sys.executable, str(TOOLS / "check_env_locks.py")]),
    # A37 : les trois numéros de version (CMakeLists, « Version courante », dernière
    # en-tête de release) disent-ils la même chose ? Et un numéro sauté est-il
    # expliqué ? Trois tags le même jour et une 0.5.3 disparue sans trace venaient
    # d'une seule cause : rien ne le vérifiait. Cf. docs/RELEASE.md.
    ("Cohérence de version (CMakeLists ↔ CHANGELOG ↔ releases)",
     [sys.executable, str(TOOLS / "check_release.py")]),
    # Auto-tests du HARNAIS lui-même (chantier A4). L'instrument produit TOUTE la
    # preuve du projet, et il avait des pannes silencieuses : trois « bloquants » sur
    # huit venaient de lui lors du balayage du 2026-08-25 (options de pilotage non
    # répétables). Un instrument non testé fabrique des bugs qui n'existent pas.
    ("Harnais headless (options de pilotage : répétabilité, durée d'appui, scancodes)",
     [sys.executable, str(TOOLS / "check_headless_options.py")]),
    ("P1 verdicts série (cartouche diagnostic)",
     [sys.executable, str(TOOLS / "run_selftests.py")]),
    ("Cycle-bench (auto-régression du modèle de cycle 68000)",
     [sys.executable, str(TOOLS / "run_cyclebench.py")]),
    # Round-trip save-state en CONFIG PAR DÉFAUT : la régression v10 (mem_ NE2000
    # vide → tout .state rejeté au chargement, F7 mort) a vécu des semaines sans
    # qu'aucun palier ne la voie — ce verdict la rend impossible à reproduire.
    ("Save-state round-trip (config par défaut)",
     [str(HEADLESS), "roms/etos192us.img", "--frames", "30", "--save-state-test"]),
    # Le mode serveur (--server) doit rendre EXACTEMENT ce que rend la boucle
    # --frames : un pilote externe archive des cellules et rejoue des scripts au
    # tuyau ; une divergence entre les deux chemins fausserait toute son archive,
    # et silencieusement — les deux « marchent ». Verdict MUTATION-TESTÉ.
    ("Serveur == boucle --frames (observation, entrées, écran, état exporté)",
     [sys.executable, str(TOOLS / "run_server_equiv.py")]),
    # Disquette livrée dans TOUS les paquets : elle avait été écrasée par un test
    # d'écriture secteur et n'était plus lisible sous TOS (issue #38), sans qu'aucun
    # palier ne relise jamais cette image.
    ("Disquette livrée (disks/diskA.st : FAT12 valide, conforme au générateur)",
     [sys.executable, str(TOOLS / "check_disk_assets.py")]),
    # Séquenceur MIDI de bout en bout : Cubase Lite (TOS 1.04, MROS) importe un SMF et
    # le joue ; ce qui sort de l'ACIA est comparé note à note au fichier (tempo, gigue,
    # vélocités, pédale). Couvre ACIA 6850 + Timer A + GEMDOS HD + midi_simplify.py.
    ("Séquenceur MIDI (Cubase Lite joue un SMF → notes/tempo comparés)",
     [sys.executable, str(TOOLS / "run_midi_sequencer.py")]),
]

# Boot GUI (A9a, audit 2026-08-27) : build/neost était à 0 % de couverture LOCALE —
# seul le job CI xvfb le lançait. 400 trames EmuTOS + capture non uniforme, en
# harnais (--run-frames, qui depuis A9a ne réécrit PAS neost.cfg : un test ne doit
# laisser aucune trace dans l'état utilisateur). Sauté ET DIT quand il n'y a pas
# d'affichage (CI sans xvfb) ou pas de cible GUI — jamais un faux vert silencieux.
_GUI_SHOT = ROOT / "tests" / "out" / "gui_boot.ppm"
GUI_STEPS = [
    ("Boot GUI (400 trames EmuTOS, capture du framebuffer)",
     # La disquette est passée EXPLICITEMENT : sans elle, le GUI monte le lecteur A du
     # neost.cfg de l'utilisateur — un banc tiers y avait laissé une image qui rendait
     # une capture uniforme, et le palier accusait le boot (2026-09-23).
     [str(GUI), "roms/etos192us.img", "disks/diskA.st", "--run-frames", "400",
      "--shot", str(_GUI_SHOT)]),
    ("Boot GUI — la capture montre quelque chose",
     [sys.executable, str(TOOLS / "check_ppm_nonuniform.py"), str(_GUI_SHOT)]),
]


def gui_available() -> str | None:
    """None si le boot GUI peut tourner, sinon la raison du SKIP (recensée)."""
    if not GUI.exists():
        return "cible GUI non bâtie (cmake --build build)"
    # DISPLAY/WAYLAND_DISPLAY ne veulent rien dire hors X11/Wayland : macOS a Quartz,
    # Windows a toujours un bureau dans une session interactive. Sans cette exclusion,
    # le boot GUI était sauté sur DEUX des trois plateformes livrées — donc la cible
    # `neost` n'y était jamais lancée par la suite de tests.
    if sys.platform not in ("darwin", "win32") and not os.environ.get("DISPLAY") \
            and not os.environ.get("WAYLAND_DISPLAY"):
        return "pas d'affichage (DISPLAY/WAYLAND_DISPLAY absents — xvfb-run pour forcer)"
    return None
FULL = FAST + [
    # Barrière de DÉBIT (chantier A6). Dans le palier FULL et non FAST : il mesure du
    # temps mur, donc il coûte quelques secondes et il est le seul verdict du dépôt
    # sensible à la charge de la machine. Il garde des RATIOS entre charges mesurées
    # dans le MÊME run (le boot nu sert d'étalon de vitesse), ce qui le rend
    # indépendant de la vitesse du runner — un seuil absolu en trames/s serait
    # instable sur une CI partagée, donc désarmé au bout de trois faux rouges.
    ("Banc de débit (coût relatif des chemins blitter et MFP)",
     [sys.executable, str(TOOLS / "run_perfbench.py")]),
    # A25 : l'OBJECTIF du projet (MegaSTE, suite Q du diagnostic Field Service 12/12)
    # était validé à la main — désormais rejoué ici (~14 s). Dépend du TOS 2.06 et de
    # la cartouche Atari : absents → SKIP recensé (politique rom_is_free), donc dans
    # FULL et jamais dans fast (qui doit rester prêt-à-purge, § BLOQUANT RELEASE).
    ("Diagnostic MegaSTE Field Service (suite Q rejouée, verdicts série)",
     [sys.executable, str(TOOLS / "run_megaste_diag.py")]),
    ("P2 provenance des références",
     [sys.executable, str(TOOLS / "run_etalons.py"), "--verify-refs"]),
    ("P2 étalons pixel (oracle + snapshots)",
     [sys.executable, str(TOOLS / "run_etalons.py")]),
]

HOOK_BODY = ("#!/bin/sh\n"
             "# Hook NeoST (opt-in via tools/run_all.py --install-hook) : garde de push.\n"
             "exec python3 tools/run_all.py --tier fast\n")


def git_hooks_dir() -> Path | None:
    try:
        out = subprocess.run(["git", "rev-parse", "--git-path", "hooks"],
                             cwd=ROOT, capture_output=True, text=True, check=True)
        d = (ROOT / out.stdout.strip()).resolve()
        d.mkdir(parents=True, exist_ok=True)
        return d
    except Exception as e:
        print(f"git introuvable / hors dépôt : {e}", file=sys.stderr)
        return None


def install_hook(remove: bool) -> int:
    d = git_hooks_dir()
    if not d:
        return 2
    hook = d / "pre-push"
    if remove:
        if hook.exists() and "run_all.py --tier fast" in hook.read_text(errors="ignore"):
            hook.unlink()
            print(f"hook pre-push retiré : {hook}")
        else:
            print("aucun hook NeoST à retirer.")
        return 0
    if hook.exists() and "run_all.py --tier fast" not in hook.read_text(errors="ignore"):
        print(f"⚠ un pre-push existe déjà (non-NeoST) : {hook} — non écrasé.", file=sys.stderr)
        return 1
    hook.write_text(HOOK_BODY)
    hook.chmod(0o755)
    print(f"hook pre-push installé : {hook}\n  → lance « run_all.py --tier fast » avant chaque push.")
    return 0


# Purge § BLOQUANT RELEASE, pas 2 (2026-08-28). Une sous-suite qui n'a pas pu tourner
# faute d'un fichier NON REDISTRIBUABLE (TOS Atari, cartouche Field Service, Cubase
# Lite) sort 77 : ni 0 ni 1. Avant, elle sortait 0 — son SKIP était imprimé au milieu
# de centaines de lignes puis englouti par un « TOUS LES PALIERS OK » final. Le bilan
# doit dire ce qui n'a PAS été vérifié : c'est tout le sujet du « vert creux ».
EXIT_SKIPPED = 77


# Verrou de la purge (§ BLOQUANT RELEASE, pas 2) : le palier `fast` ne doit RE-acquérir
# aucune dépendance propriétaire en douce. Le couplage d'origine — un `roms/tos102uk.img`
# codé en dur dans deux outils du palier — est précisément ce qui rendait le nettoyage
# juridique « impossible sans casser la CI ». On relit donc le CODE des outils que le
# palier lance (les commentaires sont ignorés : ils racontent l'histoire, ils n'ouvrent
# pas de fichier) et les chemins de ROM passés en argument.
FREE_ROM_RE = re.compile(r'"roms/(?!etos)[^"]+\.img"|"roms"\s*/\s*"(?!etos)[^"]+\.img"')


def fast_tier_proprietary(steps) -> list[str]:
    hits = []
    for label, cmd in steps:
        for arg in cmd:
            if arg.startswith("roms/") and not Path(arg).name.startswith("etos"):
                hits.append(f"{label} : ROM propriétaire en argument ({arg})")
        for arg in cmd:
            src = Path(arg)
            if src.suffix != ".py" or not src.exists():
                continue
            text = src.read_text(errors="replace")
            # Un outil a le DROIT de nommer un fichier propriétaire s'il sait s'en
            # passer : la marque en est la constante EXIT_SKIPPED (sortie 77, SKIP
            # recensé remonté au bilan). C'est un fil-piège, pas une preuve — la
            # preuve, c'est le test du chemin de SKIP, fait à la main le 2026-08-28
            # sur run_midi_sequencer.py et run_megaste_diag.py.
            if "EXIT_SKIPPED" in text:
                continue
            for n, line in enumerate(text.splitlines(), 1):
                if line.lstrip().startswith("#"):
                    continue
                m = FREE_ROM_RE.search(line)
                if m:
                    hits.append(f"{src.name}:{n} : ROM propriétaire codée en dur "
                                f"({m.group(0)}) — le palier `fast` doit rester libre "
                                "(migrer sur EmuTOS, ou politique de SKIP recensé)")
    return hits


def run_tier(steps, skipped=()) -> int:
    ok = True
    skipped = list(skipped)
    for label, cmd in steps:
        print(f"\n########## {label} ##########")
        rc = subprocess.run(cmd, cwd=ROOT).returncode
        if rc == EXIT_SKIPPED:
            skipped.append(label)
            print(f"  → SAUTÉE, recensée ({label})")
        elif rc != 0:
            ok = False
            print(f"  → ÉCHEC ({label})")
    print("\n" + ("=" * 60))
    if skipped:
        print(f"⚠ {len(skipped)} étape(s) NON EXÉCUTÉE(S) — données non redistribuables "
              "absentes (cf. TODO § BLOQUANT RELEASE) :")
        for label in skipped:
            print(f"    · {label}")
    if not ok:
        print("ÉCHEC — voir ci-dessus")
        return 1
    print("TOUS LES PALIERS OK" + (" — COUVERTURE AMPUTÉE (voir les étapes sautées)"
                                   if skipped else ""))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Orchestrateur des tests NeoST par paliers")
    ap.add_argument("--tier", choices=("fast", "full"), default="fast")
    ap.add_argument("--install-hook", action="store_true", help="installe le hook git pre-push")
    ap.add_argument("--uninstall-hook", action="store_true")
    args = ap.parse_args()

    if args.install_hook or args.uninstall_hook:
        return install_hook(args.uninstall_hook)

    steps = list(FAST if args.tier == "fast" else FULL)
    # Binaires requis DÉDUITS des étapes, jamais listés à la main : la liste codée en
    # dur avait déjà divergé. A20 (2026-08-27) a ajouté `neost-stx-test` au palier ET
    # à la liste, mais les quatre jobs de CI qui lancent run_all ne bâtissaient que
    # `neost-headless neost-selftest` — ils sortaient donc en 2 « Build requis » avant
    # le moindre test. Corrigé côté CI le 2026-08-28, et rendu IMPOSSIBLE ici : la
    # liste se recalcule depuis les commandes qu'on est sur le point de lancer.
    needed = []
    for _label, cmd in steps:
        for arg in cmd:
            q = Path(arg)
            if q.parent == (ROOT / "build") and q not in needed:
                needed.append(q)
    for binary in needed:
        if not binary.exists():
            print(f"Build requis : cmake --build build  ({binary} absent)", file=sys.stderr)
            return 2

    hits = fast_tier_proprietary(FAST)
    if hits:
        print("✗ le palier `fast` a RE-acquis une dépendance propriétaire :",
              file=sys.stderr)
        for h in hits:
            print(f"    {h}", file=sys.stderr)
        return 2
    skipped = []
    skip = gui_available()
    if skip is None:
        steps += GUI_STEPS
    else:
        print(f"⚠ Boot GUI SAUTÉ (recensé) : {skip}")
        skipped.append(f"Boot GUI ({skip})")
    return run_tier(steps, skipped)


if __name__ == "__main__":
    sys.exit(main())
