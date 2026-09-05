#!/usr/bin/env python3
# =============================================================================
#  opendst.py — LE point d'entrée du pilotage externe de NeoST : un menu, une
#  commande à retenir. Sans argument, il liste les outils ; avec un verbe, il
#  délègue au bon script en lui passant le reste de la ligne.
#
#    python3 tools/opendst.py                  → le menu
#    python3 tools/opendst.py memdiff --help   → l'aide de l'outil visé
#
#  Rien d'autre que de la délégation : la doc de référence est docs/OPENDST.md.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
HEADLESS = os.path.join(ROOT, "build", "neost-headless")

# verbe → (script ou None, résumé, exemple)
MENU = [
    ("server",  None,
     "lance neost-headless --server (le reste de la ligne = ses options)",
     "server roms/etos192fr.img --machine st --disk jeu.st --fastfdc --probe x=1A34:2"),
    ("memdiff", "opendst_memdiff.py",
     "TROUVER les variables du jeu en RAM par diff d'états (position, munitions…)",
     "memdiff --rom R --disk D --load-state cell0.state --input \"R*30\""),
    ("explore", "opendst_explore.py",
     "boucle Go-Explore minimale sur le serveur (archive de cellules)",
     "explore --rom R --disk D --load-state cell0.state --iterations 200 --cell-from probes --probe x=1A34:2"),
    ("oracle",  "opendst_oracle.py",
     "rejouer le MÊME script sous NeoST et Hatari, comparer (chercheur de divergences)",
     "oracle --rom R --disk D --frames 2600 --joy-at 1500 --joy-script-file rollout.joy"),
    ("compile", None,
     "compiler un script joystick en masques (un octet par trame)",
     "compile \"R*30 [DF]*8\" sortie.bin"),
    ("equiv",   "run_server_equiv.py",
     "le verdict : le serveur rend EXACTEMENT ce que rend la boucle --frames",
     "equiv"),
    ("hatari",  "setup_hatari.sh",
     "installer l'oracle Hatari épinglé, patch NeoST appliqué",
     "hatari"),
    ("doc",     None,
     "où lire : contrat de déterminisme, protocole, recettes, limites",
     "doc"),
]


def menu():
    print("NeoST — pilotage externe déterministe (OpenDST, Go-Explore, fuzzing)")
    print("usage : python3 tools/opendst.py <verbe> [arguments…]\n")
    for verb, _, summary, _ in MENU:
        print("  %-8s %s" % (verb, summary))
    print("\nexemples :")
    for verb, _, _, ex in MENU:
        print("  opendst.py " + ex)
    print("\nréférence : docs/OPENDST.md — protocole complet en § 5, démarrage en cinq commandes en tête.")
    print("binaire   : %s%s" % (HEADLESS, "" if os.path.exists(HEADLESS) else "  (ABSENT : cmake --build build)"))


def main(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        menu()
        return 0
    verb, rest = argv[0], argv[1:]
    entry = next((m for m in MENU if m[0] == verb), None)
    if entry is None:
        print("verbe inconnu : %s\n" % verb, file=sys.stderr)
        menu()
        return 2
    if verb == "server":
        if not os.path.exists(HEADLESS):
            sys.exit("absent : %s (cmake --build build)" % HEADLESS)
        return subprocess.call([HEADLESS] + rest + ["--server"])
    if verb == "compile":
        if len(rest) != 2:
            sys.exit("usage : opendst.py compile \"SCRIPT\" SORTIE.bin")
        return subprocess.call([HEADLESS, os.path.join(ROOT, "roms", "etos192fr.img"),
                                "--joy-script", "0", rest[0], "--joy-script-compile", rest[1]])
    if verb == "doc":
        print(os.path.join(ROOT, "docs", "OPENDST.md"))
        print("§ 1 contrat de déterminisme · § 3 scripts joystick · § 4 sondes · § 5 serveur et")
        print("protocole · § 6 boucle · § 7 recette Rick Dangerous · § 8 pièges · § 9 oracle · § 10 outils")
        return 0
    script = os.path.join(TOOLS, entry[1])
    runner = ["bash", script] if script.endswith(".sh") else [sys.executable, script]
    return subprocess.call(runner + rest)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
