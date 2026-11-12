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
import time
from datetime import date
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
    ("Fuzzing des parseurs d'images disquette (.msa / .dim / .stx / .ipf, .wd1772, FDC)",
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
    # Un blit HOG de plusieurs trames : l'instruction est GARÉE à chaque fin de trame
    # (core/EmuFiber), à l'octet près comme d'un seul tenant ; état refusé, reset propre.
    ("Blit HOG de plusieurs trames (instruction garée, accord à l'octet, reset)",
     [sys.executable, str(TOOLS / "check_hog_park.py")]),
    ("Verdicts des rejeux (halt, captures manquantes, diagnostics séparés)",
     [sys.executable, str(TOOLS / "check_game_replays.py")]),
    ("Verdicts Spectrum 512 (ROM absentes, couverture partielle, références préservées)",
     [sys.executable, str(TOOLS / "check_spec512_verdicts.py")]),
    ("Comparateur de titres (pixels RGB, boutons et déplacements datés)",
     [sys.executable, str(TOOLS / "check_compare_titles.py")]),
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
    # Disquette changée à chaud par le serveur (insert/eject) et lecteur B débranché
    # (--drive-b off) : on vérifie ce que voit le TOS, à toutes les phases VBL — une
    # fenêtre de changement de média trop courte passait entre ses lectures de WPRT.
    ("Serveur : disquette à chaud vue par le TOS, lecteur B débranché (_nflops = 1)",
     [sys.executable, str(TOOLS / "check_server_media.py")]),
    # Disquette livrée dans TOUS les paquets : elle avait été écrasée par un test
    # d'écriture secteur et n'était plus lisible sous TOS (issue #38), sans qu'aucun
    # palier ne relise jamais cette image.
    ("Disquette livrée (disks/diskA.st : FAT12 valide, conforme au générateur)",
     [sys.executable, str(TOOLS / "check_disk_assets.py")]),
    # Détecteur de wakestate de Troed : longueur d'une ligne composée, sans Hatari ni disque privé.
    ("WSDETECT (Troed/SYNC : longueur de ligne par wakestate et par machine)",
     [sys.executable, str(TOOLS / "run_wsdetect.py")]),
    # Retour arrière : média écrit, changé et éjecté annulé ; état à l'octet ; reprise identique.
    ("Retour arrière (disquette restaurée, état à l'octet, reprise identique)",
     # --disk explicite : sans disquette, la partie « média éjecté puis rendu » est sautée.
     [str(HEADLESS), "roms/etos192fr.img", "--machine", "st", "--disk", "disks/diskA.st",
      "--disk-ro", "--frames", "400", "--rewind-test"]),
    # Relevés HWTST de tiers sur de vrais Mega STE : boucle registre à 8, 16 et 16 MHz + cache.
    ("HWTST (Mega STE réel : rapport de vitesse 16 MHz sans cache et avec cache)",
     [sys.executable, str(TOOLS / "run_hwtst_loop.py")]),
    # Le vrai pilote ENEC.STX sur la NE2000, sans réseau.
    ("STinG + ENEC.STX (le vrai pilote trouve la NE2000, hors réseau)",
     [sys.executable, str(TOOLS / "run_sting_boot.py")]),
    # Empreinte de l'état complet : deux passes et une reprise de save-state, mêmes empreintes.
    ("Déterminisme de l'état complet (empreintes, reprise de save-state)",
     [sys.executable, str(TOOLS / "run_determinism.py")]),
    # Séquenceur MIDI de bout en bout : Cubase Lite (TOS 1.04, MROS) importe un SMF et
    # le joue ; ce qui sort de l'ACIA est comparé note à note au fichier (tempo, gigue,
    # vélocités, pédale). Couvre ACIA 6850 + Timer A + GEMDOS HD + midi_simplify.py.
    ("Séquenceur MIDI (Cubase Lite joue un SMF → notes/tempo comparés)",
     [sys.executable, str(TOOLS / "run_midi_sequencer.py")]),
]

# Les tests réseau utilisent des pairs locaux et des résolveurs simulés.
# Aucun accès Internet ; le build headless les compile quand NEOST_WITH_NET est actif.
_cache = ROOT / "build" / "CMakeCache.txt"
_net_off = _cache.exists() and "NEOST_WITH_NET:BOOL=OFF" in _cache.read_text()
NAT_STEPS = [
    ("NAT intégré : protocoles, fragments, IPv6 et redirections locales",
     [str(ROOT / "build" / ("neost-nat-test" + _EXE))]),
    ("NAT : résolveur asynchrone, cache et durée de vie",
     [str(ROOT / "build" / ("neost-resolver-test" + _EXE))]),
    ("NAT : adaptateur NeoST, options et échecs de publication",
     [str(ROOT / "build" / ("neost-nat-adapter-test" + _EXE))]),
    # Anneau MIDI (extension NeoST, docs/EXTENSIONS.md) : orphelin jusqu'au 2026-11-11,
    # cité comme « contrôle local » sans qu'aucun palier ne le lance. Sockets hôte requis.
    ("Anneau MIDI : datagrammes v1 séquencés, doublons et sources étrangères rejetés",
     [str(ROOT / "build" / ("neost-midi-ring-test" + _EXE))]),
    ("Modem Hayes : ATDT asynchrone, DTR (&D2/&D0), abandon, reset du PSG",
     [str(ROOT / "build" / ("neost-modem-test" + _EXE))]),
    ("NAT à travers la NE2000 : ARP, DHCP et UDP loopback",
     [str(HEADLESS), "roms/etos192us.img", "--nat-selftest"]),
    ("STinG + NAT : DNS localhost et HTTP sur serveur local",
     [sys.executable, str(TOOLS / "run_nat_sting.py")]),
]
if not _net_off:
    FAST[1:1] = NAT_STEPS

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
# Sorties qu'une étape DOIT recréer : effacées avant l'étape, exigées après. Sans cela,
# un GUI qui sortait 0 sans rien capturer laissait l'étape suivante valider la capture
# d'un run précédent.
FRESH_OUTPUTS = {GUI_STEPS[0][0]: _GUI_SHOT}


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
    ("NetUSBee USB4TOS (clavier, souris, fichiers disque via les pilotes réels)",
     [sys.executable, str(TOOLS / "run_netusbee_boot.py")]),
    # Traces temporelles contre Hatari : références publiques, pas d'oracle local
    # requis. La CI oracle vérifie séparément leur reproductibilité à l'épingle.
    ("Comparateur temporel (IRQ absentes, perdues ou dérivantes)",
     [sys.executable, str(TOOLS / "test_timing_oracle.py")]),
    ("Timings MFP, DMA et blitter (Hatari ; cadence FDC selon fiche WD1772)",
     [sys.executable, str(TOOLS / "run_timing_oracle.py")]),
    ("MegaSTE 16 MHz : bus et cache (Hatari, égalité exacte)",
     [sys.executable, str(TOOLS / "run_megaste_bus_oracle.py")]),
    ("MegaSTE blitter/IRQ : données, répétabilité et conservation des cycles bus",
     [sys.executable, str(TOOLS / "run_megaste_blitter_checks.py")]),
    # Programmes de la suite de tests d'Hatari (cycles, blitter, bus errors par
    # machine) avec leurs résultats attendus versionnés. Arbre Hatari absent → SKIP.
    ("Suite de tests d'Hatari (cycles, blitter, bus errors ST/MegaST/STE/MegaSTE)",
     [sys.executable, str(TOOLS / "run_hatari_suite.py")]),
    # Oracles FPU : CIR NeoST contre le vrai SoftFloat d'Hatari compilé (orphelins
    # jusqu'au 2026-10-20 : aucun palier ne les lançait).
    ("Oracles FPU bit-exact (SoftFloat et FPSP d'Hatari compilés)",
     [sys.executable, str(TOOLS / "run_fpu_oracles.py")]),
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
    # Banc différentiel Moira↔fx68k (échelon rtl) : le cœur 68000 nu contre le RTL
    # cycle-exact, accès bus par accès bus, prise d'IPL et STOP compris. Verdicts posés
    # dans tests/reference/fx68k_bench.json : un verdict qui CHANGE fait échouer, dans les
    # deux sens. Verilator ou extern/fx68k absents → SKIP recensé (77).
    ("Banc Moira↔fx68k (cycles bus du 68000 nu contre le RTL)",
     [sys.executable, str(TOOLS / "run_fx68k_bench.py")]),
    # GSTMCU (STE) simulé : écrans connus, dates des signaux, latence de DTACK par zone.
    ("Banc GSTMCU (écrans calculés, latence de DTACK par zone)",
     [sys.executable, str(TOOLS / "run_gstmcu_bench.py")]),
    # 68000 + GSTMCU (STE) : compteur vidéo relevé par le CPU, en boucle, en HBL et en VBL.
    ("Banc CPU + GSTMCU (ce que le CPU voit du faisceau, contre les RTL)",
     [sys.executable, str(TOOLS / "run_stcpu_bench.py")]),
    # Images Spectrum 512 réelles contre la formule du format : aucun autre émulateur ni RTL en jeu.
    ("Images Spectrum 512 contre leur décodage (logiciel réel, 0 pixel faux)",
     [sys.executable, str(TOOLS / "run_spec512_truth.py")]),
    # Une écriture du CPU, la trame qui sort : NeoST contre les RTL, pixel par pixel.
    ("Banc au pixel (fx68k + GSTMCU, une impulsion 60 Hz à chaque date)",
     [sys.executable, str(TOOLS / "run_stcpu_raster.py")]),
    # Machine GLUE du STE contre le modèle : une impulsion à chaque phase de la ligne.
    ("Balayage GLUE STE contre le modèle GSTMCU (DE et octets par écriture)",
     [sys.executable, str(TOOLS / "run_glue_sweep.py")]),
    # Blitter de NeoST contre fx68k + stBlitter.sv : accès, données et dates. Sans Verilator ou
    # sans les RTL épinglés → SKIP recensé (77).
    ("Banc blitter ↔ stBlitter.sv (accès et dates, contre le RTL)",
     [sys.executable, str(TOOLS / "run_stblitter_bench.py")]),
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
# Le 77 est réservé à ce qui est réellement privé (TOS Atari, disquettes commerciales,
# arbre Hatari, outils RTL) : une ROM EmuTOS versionnée ou un binaire de build/ absents
# sont une CASSE du dépôt, et l'outil sort 2 (échec), jamais 77.
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


# A38 : chaque contrôle du palier fast est inscrit, avec son dernier déclenchement
# RÉEL. Le registre vit à côté des étapes ; une étape non inscrite est refusée.
GUARDS = TOOLS / "fast_guards.json"


def check_guard_registry(labels) -> int:
    reg = json.loads(GUARDS.read_text())["guards"]
    # Les étapes NAT restent inscrites lorsque le build coupe le réseau hôte.
    expected = set(labels) | {label for label, _ in NAT_STEPS}
    missing = sorted(label for label in expected if label not in reg)
    stale = [label for label in reg if label not in expected]
    for label in missing:
        print(f"✗ étape fast absente de {GUARDS.name} : {label}", file=sys.stderr)
    for label in stale:
        print(f"✗ {GUARDS.name} inscrit une étape qui n'existe plus : {label}", file=sys.stderr)
    return 2 if missing or stale else 0


def report_guards(durations) -> None:
    """Mur du palier, puis les contrôles à rejustifier (information, jamais un échec)."""
    data = json.loads(GUARDS.read_text())
    limit = data["review_after_days"]
    total = sum(durations.values())
    print(f"\nMur du palier fast : {total:.1f} s, {len(durations)} étapes ; les plus lentes :")
    for label, seconds in sorted(durations.items(), key=lambda kv: -kv[1])[:5]:
        print(f"    {seconds:6.1f} s  {label}")
    review = []
    for label in durations:
        entry = data["guards"][label]
        catch = entry["last_real_catch"]
        ref = date.fromisoformat(catch["date"] if catch else entry["since"])
        age = (date.today() - ref).days
        if age > limit:
            review.append((age, label, "dernier déclenchement" if catch else "posé"))
    if review:
        print(f"Contrôles sans déclenchement réel depuis plus de {limit} jours "
              f"(à rejustifier ou retirer, cf. {GUARDS.name}) :")
        for age, label, what in sorted(review, reverse=True):
            print(f"    {label} — {what} il y a {age} jours")


# Le 77 ne dit pas si l'étape a tourné en partie (2026-11-12). En CI, « P2 étalons pixel »
# exécutait 44 étalons et en sautait 12 faute de ROM, et le bilan la rangeait parmi les
# étapes « NON EXÉCUTÉE(S) », comme si rien n'avait tourné. Les sous-suites qui ont
# exécuté une partie de leurs cas l'écrivent avant de sortir en 77 ; on lit leur sortie
# au passage pour ranger l'étape en « PARTIELLE ». Libellé et compte seulement : le
# code de sortie et les seuils ne changent pas.
PARTIAL_RE = re.compile(r"couverture AMPUTÉE|verdicts présents identiques")


def run_step(cmd) -> tuple[int, bool]:
    """Lance une étape en recopiant sa sortie ; rend (code, couverture partielle annoncée)."""
    partial = False
    env = dict(os.environ, PYTHONUNBUFFERED="1")   # sortie au fil de l'eau malgré le tube
    with subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, env=env,
                          text=True, errors="replace", bufsize=1) as proc:
        for line in proc.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            partial = partial or bool(PARTIAL_RE.search(line))
    return proc.returncode, partial


def run_tier(steps, skipped=(), fast=False) -> int:
    ok = True
    skipped = list(skipped)
    partial = []
    durations = {}
    for label, cmd in steps:
        print(f"\n########## {label} ##########", flush=True)
        start = time.monotonic()
        fresh = FRESH_OUTPUTS.get(label)
        if fresh is not None:
            fresh.unlink(missing_ok=True)
        rc, ran_part = run_step(cmd)
        if fresh is not None and rc == 0 and not fresh.is_file():
            print(f"  {fresh.relative_to(ROOT)} non recréé par l'étape")
            rc = 1
        durations[label] = time.monotonic() - start
        if rc == EXIT_SKIPPED and ran_part:
            partial.append(label)
            print(f"  → PARTIELLE, recensée ({label})")
        elif rc == EXIT_SKIPPED:
            skipped.append(label)
            print(f"  → SAUTÉE, recensée ({label})")
        elif rc != 0:
            ok = False
            print(f"  → ÉCHEC ({label})")
    print("\n" + ("=" * 60))
    if fast:
        report_guards(durations)
    if partial:
        print(f"⚠ {len(partial)} étape(s) PARTIELLE(S) — exécutées, mais une partie de leurs cas "
              "sautée faute de données non redistribuables (détail dans leur sortie) :")
        for label in partial:
            print(f"    · {label}")
    if skipped:
        print(f"⚠ {len(skipped)} étape(s) NON EXÉCUTÉE(S) — données non redistribuables "
              "absentes (restauration : tools/private_assets.sh unpack) :")
        for label in skipped:
            print(f"    · {label}")
    if not ok:
        print("ÉCHEC — voir ci-dessus")
        return 1
    print("TOUS LES PALIERS OK" + (" — COUVERTURE AMPUTÉE (voir les étapes partielles ou sautées)"
                                   if skipped or partial else ""))
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
    if args.tier == "fast":
        rc = check_guard_registry([label for label, _ in FAST + GUI_STEPS])
        if rc:
            return rc
    skipped = []
    skip = gui_available()
    if skip is None:
        steps += GUI_STEPS
    else:
        print(f"⚠ Boot GUI SAUTÉ (recensé) : {skip}")
        skipped.append(f"Boot GUI ({skip})")
    return run_tier(steps, skipped, fast=args.tier == "fast")


if __name__ == "__main__":
    sys.exit(main())
