#!/usr/bin/env python3
# =============================================================================
#  check_headless_options.py — Auto-tests du HARNAIS lui-même (chantier A4).
#
#  POURQUOI. `neost-headless` EST le framework de test du projet : toute la preuve
#  produite ici passe par ses options. Or il avait des modes d'échec SILENCIEUX, et
#  la passe du 2026-08-25 l'a payé cher — TROIS « bloquants » sur huit venaient de
#  l'instrument et non de l'émulateur (Xenon 2, Flood, Dynamite Dux, tous jouables
#  une fois la repro corrigée), parce que `--joy-at` / `--joy-script` / `--mouse-at`
#  étaient des SCALAIRES : la dernière occurrence écrasait les précédentes sans le
#  moindre avertissement. Un verdict « confirmé à l'oracle » a même été rendu FAUX
#  par la durée d'appui (40 ms côté NeoST contre ~600 ms côté Hatari).
#
#  `neost-selftest` couvre « la logique pure » — et le parsing d'arguments EN EST.
#  Ces tests-ci sont en BOÎTE NOIRE (on lance le binaire et on lit ce qu'il fait)
#  parce que le parsing vit dans main_headless.cpp, hors de toute bibliothèque :
#  l'extraire pour le tester coûterait plus cher que la panne qu'on veut éviter.
#
#  RÈGLE : chaque test vérifie un COMPORTEMENT OBSERVABLE, jamais une implémentation.
#  Et chacun porte le bug qu'il empêche de revenir.
#
#  Usage : check_headless_options.py   (code de sortie 1 au premier échec)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import subprocess
import tempfile
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Suffixe .exe sous Windows : sans lui, le binaire est cherché sous un nom qui
# n'existe pas et la suite se déclare « non bâtie » — la seule plateforme livrée
# où les tests ne pouvaient pas tourner du tout.
_EXE = ".exe" if sys.platform == "win32" else ""
HEADLESS = ROOT / "build" / ("neost-headless" + _EXE)
ROM = ROOT / "roms" / "etos192fr.img"          # ROM LIBRE : ces tests survivent au
                                               # retrait des TOS Atari propriétaires.

fails = []


def run(args, frames=60):
    cp = subprocess.run([str(HEADLESS), str(ROM), "--frames", str(frames)] + args,
                        cwd=ROOT, capture_output=True, text=True, timeout=180)
    return cp.stdout + cp.stderr


def check(name, cond, why, detail=""):
    if cond:
        print(f"  OK   {name}")
    else:
        print(f"  ✗    {name}\n       {why}")
        if detail:
            print(f"       observé : {detail.strip()[:300]}")
        fails.append(name)


def main() -> int:
    if not HEADLESS.exists():
        print(f"neost-headless absent ({HEADLESS}) — bâtir d'abord.")
        return 1
    print("Auto-tests du harnais headless (options de pilotage) :")

    # --- OUTIL-1 : les options de pilotage sont RÉPÉTABLES --------------------
    # Le bug : scalaires, dernière occurrence gagnante, sans avertissement. Trois
    # faux « bloquants » en une seule passe de balayage.
    out = run(["--joy-at", "10", "0x80", "--joy-at", "30", "0x08"])
    n = out.count("joystick applied at frame")
    check("--joy-at est répétable", n == 2,
          "deux --joy-at doivent produire DEUX applications ; une seule = retour du "
          "scalaire « dernière occurrence gagnante » (OUTIL-1).",
          f"{n} ligne(s) « joystick applied »")

    # Le corollaire : c'est bien la PREMIÈRE qui a été jouée, pas seulement la dernière.
    check("--joy-at joue la PREMIÈRE occurrence",
          "frame 10: port1=$80" in out,
          "l'application de la trame 10 manque : les occurrences sont écrasées.",
          out)

    # --- Durée d'appui réglable (A4) -----------------------------------------
    # Le bug : 2 trames câblées en dur (~40 ms), incomparable au --cmd-fifo d'Hatari
    # (~600 ms). C'est ce qui a rendu FAUX un verdict « confirmé à l'oracle ».
    out = run(["--key-hold", "20", "--keys-at", "10", "a"], frames=60)
    check("--key-hold est accepté", "unknown option" not in out.lower(),
          "l'option de durée d'appui doit exister : sans elle, aucune A/B honnête "
          "contre l'oracle n'est possible.", out)

    # Le bug : --key-hold 0 (ou négatif) n'envoyait JAMAIS le relâchement (appui et
    # relâche sur la même phase, « else if ») — touches collées jusqu'à la fin.
    for bad in ("0", "-3"):
        out = run(["--key-hold", bad, "--keys-at", "10", "a"], frames=30)
        check(f"--key-hold {bad} est REFUSÉ", "--key-hold must be" in out,
              "une durée < 1 trame laissait la touche enfoncée tout le run.", out)

    # --- --dump-at sur une fenêtre MMIO qui faute ------------------------------
    # Le bug : bus.read8 hors instruction ; $FF8604 en octet faute → moira::BusError
    # lancée hors de execute() → std::terminate. Cpu68k::triggerBusError abandonne
    # désormais la faute hors de execute() ; --dump-at garde read8 (relit les puces).
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        dump = Path(td) / "mmio.bin"
        cp = subprocess.run([str(HEADLESS), str(ROM), "--frames", "3", "--dump-at", "2",
                             "FF8600", "16", str(dump)],
                            cwd=ROOT, capture_output=True, text=True, timeout=180)
        check("--dump-at sur $FF8600 (FDC/DMA) ne fait pas planter",
              cp.returncode == 0 and dump.exists() and dump.stat().st_size == 16,
              "le dump d'une fenêtre MMIO qui faute en octet tuait le processus.",
              f"code {cp.returncode} {cp.stderr[-300:]}")

        # --- --from-cfg rejoue AUSSI les extensions du GUI --------------------
        # Le bug : ultrasatan=/sd2=/dongle=/ports lus par le lecteur partagé puis
        # JAMAIS recopiés — le « rejeu fidèle » tournait sur un autre matériel.
        cfg = Path(td) / "neost.cfg"
        cfg.write_text("rom=%s\nmachine=st\nultrasatan=1\ndongle=cubase2\n"
                       "joy1=leaderboard\nprinter=prosound\nrs232=bogus\n" % ROM)
        cp = subprocess.run([str(HEADLESS), "--from-cfg", str(cfg), "--frames", "2"],
                            cwd=ROOT, capture_output=True, text=True, timeout=180)
        out = cp.stdout + cp.stderr
        for what, needle in (("ultrasatan=", "UltraSatan on ACSI targets 0-1"),
                             ("dongle=", "cartridge key (cubase2)"),
                             ("joy1=", "joy1: Leader Board"),
                             ("printer=", "printer: Pro Sound"),
                             ("identifiant inconnu ignoré", "unknown rs232=bogus ignored")):
            check(f"--from-cfg rejoue {what}", needle in out,
                  "la clé de neost.cfg doit atteindre l'option headless homologue.", out)

    # --- Scancodes bruts (A4) : le pavé numérique redevient atteignable -------
    out = run(["--scancode-at", "10", "70,6d,6e"], frames=60)
    check("--scancode-at est accepté", "unknown option" not in out.lower(),
          "les scancodes bruts doivent exister : stScancode() ne mappe pas le pavé "
          "numérique, ce qui rendait les menus de compilation impilotables.", out)

    # --- A14 : --disk-ro, l'écriture invitée ne touche plus le fichier hôte ---
    # Le bug : deux images SUIVIES PAR GIT modifiées dans l'arbre par des runs
    # (Eliminator le 2026-08-25, disks/diskA.st par le test F du diagnostic le
    # 2026-08-27). L'option coupe le write-through vers le fichier ; les écritures
    # restent en RAM, donc la machine invitée ne voit AUCUNE différence.
    # ⚠ CE TEST-CI ne prouve que l'existence et l'annonce de l'option — le palier
    # `fast` n'a aucun programme invité qui écrive vraiment sur une disquette (les
    # *-selftest sortent AVANT la configuration du FDC, l'option ne les concerne
    # pas). La preuve DE BOUT EN BOUT vit dans tools/run_megaste_diag.py (palier
    # `full`) : son test F formate les deux disquettes, et le runner échoue si les
    # images sacrificielles ont bougé d'un octet.
    out = run(["--disk-ro"], frames=30)
    check("--disk-ro est accepté et ANNONCÉ", "--disk-ro:" in out,
          "l'option doit exister et dire ce qu'elle fait : un run archivé doit "
          "porter la trace de ce qui a été protégé (A14).", out)

    # --- Garde-fou anti-« aide silencieuse » ---------------------------------
    # Piège zsh déjà consigné en mémoire projet : une variable multi-mots non quotée
    # devient UN argument, et le headless affiche son aide au lieu d'émuler. Un test
    # qui ne verrait pas la différence validerait du vide.
    out = run([])
    check("un run nominal ÉMULE (il n'affiche pas son aide)",
          "usage:" not in out.lower() and "video:" in out.lower(),
          "un run sans option doit émuler et rapporter « video: … Hz » ; s'il affiche "
          "l'aide, tous les autres tests mesureraient du vide.", out)

    # --- Lecteur A vide sans --disk (2026-11-11) -------------------------------
    # Le bug : « disks/diskA.st » était montée par défaut, résolue depuis le
    # RÉPERTOIRE COURANT et en écriture — la même commande rendait deux empreintes
    # selon le dossier de lancement. Une recette qui veut une disquette la nomme.
    check("sans --disk, le lecteur A reste VIDE (pas de disquette implicite)",
          "drive a: empty" in out.lower(),
          "un run sans --disk ne doit monter aucune image : sinon son résultat dépend "
          "du dossier d'où il est lancé.", out)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        # --- Un seul instant de fin de run, touches comprises (2026-11-11) ------
        # Le bug : --keys tournait APRÈS la boucle principale, donc après la sonde
        # finale, --save-state et --sound-dump ; seule la capture voyait les touches.
        # Preuve : --keys et l'injection datée équivalente (--keys-at à la trame
        # --frames, même cadence de 4 trames) doivent graver le MÊME état, octet
        # pour octet, et la même capture.
        a, b = tmp / "keys.state", tmp / "keysat.state"
        out_k = run(["--keys", "A", "--save-state", str(a), "--screenshot", str(tmp / "k.ppm")],
                    frames=60)
        out_a = run(["--keys-at", "60", "A", "--save-state", str(b),
                     "--screenshot", str(tmp / "a.ppm")], frames=60 + 4 + 60)
        same = (a.exists() and b.exists() and a.read_bytes() == b.read_bytes()
                and (tmp / "k.ppm").read_bytes() == (tmp / "a.ppm").read_bytes())
        check("--keys : save-state et capture décrivent le même instant",
              same, "l'état gravé avec --keys doit être celui de la fin du run, touches "
              "comprises — identique à --keys-at sur la même chronologie.",
              out_k[-300:] + out_a[-300:])

        # --- --shot-every numérote comme --screenshot (2026-11-11) --------------
        # Le bug : P<F>.ppm montrait l'écran après F+1 trames. On cherche une trame
        # où l'écran CHANGE, pour que l'égalité ne soit pas due à un écran figé.
        run(["--shot-every", "1", str(tmp / "p_")], frames=120)
        shots = {int(p.stem[2:]): p.read_bytes() for p in tmp.glob("p_*.ppm")}
        moving = [f for f in sorted(shots) if f - 1 in shots and shots[f] != shots[f - 1]]
        aligned = False
        if moving:
            f = moving[-1]
            run(["--screenshot", str(tmp / "one.ppm")], frames=f)
            aligned = (tmp / "one.ppm").read_bytes() == shots[f]
        check("--shot-every : P<F>.ppm est l'écran de --frames F --screenshot",
              aligned and 0 not in shots,
              "la capture périodique d'index F doit être celle de --frames F ; une trame "
              "d'écart fausse tout recalage contre Hatari.",
              f"index {sorted(shots)[:3]}…, trames mobiles {moving[-3:]}")
        check("--shot-every : la recherche a trouvé un écran qui bouge", bool(moving),
              "sans trame mobile entre 1 et 120, le test d'alignement ne prouve rien.")

        # --- Comparateur : un écart toléré l'est par sa ZONE (2026-11-11) -------
        # Le bug : `max_diff_px` tolérait un NOMBRE ; un autre jeu de pixels faux, de
        # même effectif, passait. Hors zone, un seul pixel doit faire échouer.
        w, h = 416, 276
        hdr = f"P6\n{w} {h}\n255\n".encode()
        ref = bytearray(w * h * 3)
        inzone, outzone = bytearray(ref), bytearray(ref)
        for x in range(w):
            for y in (274, 275):
                inzone[(y * w + x) * 3] = 0xFF
            for y in (10, 11):
                outzone[(y * w + x) * 3] = 0xFF
        (tmp / "r.ppm").write_bytes(hdr + ref)
        (tmp / "in.ppm").write_bytes(hdr + inzone)
        (tmp / "out.ppm").write_bytes(hdr + outzone)

        def cmp_rc(img):
            return subprocess.run([sys.executable, str(ROOT / "tools" / "compare_screenshot.py"),
                                   str(tmp / img), str(tmp / "r.ppm"), "--crop", "buffer",
                                   "--max", "0", "--tolerate", "0,274,416,2"],
                                  capture_output=True, timeout=120).returncode
        check("comparateur : un écart DANS la zone tolérée passe", cmp_rc("in.ppm") == 0,
              "--tolerate doit exclure les pixels de sa zone du compte.")
        check("comparateur : le même nombre de pixels HORS zone échoue", cmp_rc("out.ppm") == 1,
              "832 pixels faux ailleurs que dans la zone ne doivent pas passer pour l'écart connu.")

    # --- GUI : la seule surface testable SANS ÉCRAN (chantier A8) --------------
    # Le GUI est un angle mort total — aucun test ne le couvrait, et c'est pourtant
    # là que vivent les rapports utilisateur restants. Sa couche d'ARGUMENTS, elle,
    # est atteignable : `--help`, `--version` et le rejet d'options sortent AVANT
    # toute création de fenêtre. C'est peu, mais c'est la première couverture
    # automatisée de ce binaire.
    gui = ROOT / "build" / ("neost" + _EXE)
    if not gui.exists():
        print("  ·    GUI non bâti — tests d'arguments du GUI sautés (cible « neost »)")
    else:
        def run_gui(args):
            cp = subprocess.run([str(gui)] + args, cwd=ROOT,
                                capture_output=True, text=True, timeout=60)
            return cp.returncode, cp.stdout + cp.stderr

        rc, out = run_gui(["--help"])
        check("GUI : --help sort proprement (sans ouvrir de fenêtre)",
              rc == 0 and "Usage:" in out,
              "le GUI doit répondre à --help et sortir en 0 ; c'était l'un des points "
              "de conformité relevés au TODO.", f"code {rc}")

        # Le vrai piège : une faute de frappe était AVALÉE EN SILENCE, et l'utilisateur
        # croyait avoir activé l'option.
        rc, out = run_gui(["--kisok"])
        check("GUI : une option inconnue est REFUSÉE (pas avalée)",
              rc == 2 and "unknown option" in out,
              "toute option en '-' non reconnue doit produire un message ET un code "
              "non nul ; sinon une faute de frappe passe inaperçue.", f"code {rc}")

        rc, out = run_gui(["--version"])
        check("GUI : --version n'a pas régressé", rc == 0 and "NeoST" in out,
              "--version doit continuer de sortir l'identité de build en 0.", f"code {rc}")

    print()
    if fails:
        print(f"ÉCHEC — {len(fails)} test(s) du harnais : {', '.join(fails)}")
        print("L'instrument est en panne : tant qu'il l'est, AUCUNE mesure produite "
              "avec lui ne vaut quoi que ce soit.")
        return 1
    print("HARNAIS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
