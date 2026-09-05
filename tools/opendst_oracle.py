#!/usr/bin/env python3
# =============================================================================
#  opendst_oracle.py — ORACLE DIFFÉRENTIEL : rejoue LE MÊME script d'entrées
#  sous NeoST et sous Hatari, et compare les images.
#
#  La propriété testée n'est pas « le jeu ne plante pas » mais :
#      pour tout script d'entrées, NeoST ≡ Hatari.
#  C'est la forme « property-based » du problème, et c'est ce qui transforme un
#  explorateur d'états externe (cf. docs/OPENDST.md) en CHERCHEUR DE
#  DIVERGENCES : chaque situation nouvelle qu'il atteint devient un point de
#  comparaison gratuit contre la référence matérielle.
#
#  Deux obstacles, tous deux traités ici :
#
#  1. Hatari ne sait pas injecter un joystick daté. `--cmd-fifo` ne connaît que
#     des touches, tourne en temps réel et ne peut pas poser une entrée à la
#     trame près. D'où `tools/hatari_neost_oracle.patch`, qui ajoute à Hatari la
#     lecture d'un script joystick indexé sur la VBL. Le script est COMPILÉ par
#     NeoST (`--joy-script-compile`) : une seule implémentation de la grammaire.
#
#  2. Hatari n'est pas déterministe d'un run à l'autre (`Hatari_srand(time)` →
#     position angulaire initiale de la disquette, donc durée de boot). On ne
#     compare donc JAMAIS un numéro de trame figé mais une FENÊTRE, et on retient
#     la trame IDENTIQUE, jamais la moins pire. Cf. docs/HATARI_AUTOMATION.md.
#
#  Corollaire de méthode : ancrer le script sur une scène d'ATTENTE (titre,
#  menu) NE SUFFIT PAS. Mesuré sur Super Sprint : la première entrée tombe bien
#  pendant l'attente des deux côtés, mais les deux machines en sortent à des
#  numéros de trame différents (49 trames d'écart au boot) ; toutes les entrées
#  SUIVANTES, datées en absolu, arrivent alors à des instants-programme
#  différents et les deux trajectoires de jeu divergent pour de bon. Une image
#  « presque juste » en fin de course n'est donc pas une divergence
#  d'émulation, c'est une entrée décalée.
#
#  D'où l'ALIGNEMENT EN DEUX PASSES fait ici :
#    A. sans aucune entrée, on cherche la trame Hatari identique à la trame
#       NeoST d'ancrage → décalage de boot `d`, mesuré et non supposé ;
#    B. on rejoue avec NEOST_JOY_START = ancre + d, si bien que chaque entrée
#       tombe au même instant-programme des deux côtés.
#  Ceci n'a de sens que parce que HATARI_SEED (cf. le patch) fige la graine :
#  sans elle, le `d` mesuré en A ne vaudrait plus pour la passe B.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
HEADLESS = os.path.join(ROOT, "build", "neost-headless")
HATARI_SH = os.path.join(TOOLS, "hatari_oracle.sh")
COMPARE = os.path.join(TOOLS, "compare_screenshot.py")


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def main():
    ap = argparse.ArgumentParser(description="Oracle différentiel NeoST vs Hatari")
    ap.add_argument("--rom", required=True)
    ap.add_argument("--disk", required=True)
    ap.add_argument("--frames", type=int, required=True,
                    help="trame NeoST à comparer (et longueur du run)")
    ap.add_argument("--joy-at", type=int, default=0,
                    help="trame de la PREMIÈRE entrée du script (ancrer sur une attente)")
    ap.add_argument("--joy-script", default="", help="script joystick (grammaire JoyScript)")
    ap.add_argument("--joy-script-file", default="", help="idem, depuis un fichier")
    ap.add_argument("--machine", default="st")
    ap.add_argument("--memsize", default="0", help="Hatari --memsize (0 = 512 Ko, 1 = 1 Mo…)")
    ap.add_argument("--mem", default="512k", help="NeoST --mem")
    ap.add_argument("--scan", type=int, default=150,
                    help="demi-fenêtre de recherche du décalage de boot (passe A)")
    ap.add_argument("--scan-final", type=int, default=25,
                    help="demi-fenêtre de la comparaison finale (passe B, alignée)")
    ap.add_argument("--seed", default="1",
                    help="HATARI_SEED : fige la graine de Hatari (déterminisme run-à-run)")
    ap.add_argument("--align-at", type=int, default=-1,
                    help="trame d'ancrage pour mesurer le décalage (défaut : --joy-at)")
    ap.add_argument("--crop", default="buffer_noled")
    ap.add_argument("--out-dir", default="")
    args = ap.parse_args()

    if not args.joy_script and not args.joy_script_file:
        sys.exit("il faut --joy-script ou --joy-script-file (sans entrée, "
                 "run_etalons.py --oracle fait déjà le travail)")
    for p in (HEADLESS, HATARI_SH, COMPARE):
        if not os.path.exists(p):
            sys.exit("absent : %s" % p)
    hatari = os.path.join(ROOT, "extern", "hatari", "build", "src", "hatari")
    if not os.path.exists(hatari):
        sys.exit("oracle Hatari absent (%s) — cf. docs/HATARI_AUTOMATION.md" % hatari)

    out = args.out_dir or tempfile.mkdtemp(prefix="neost-oracle-")
    os.makedirs(out, exist_ok=True)
    temp_out = not args.out_dir      # ménage sur verdict IDENTIQUE seulement (cf. fin)
    compiled = os.path.join(out, "script.bin")
    neost_ppm = os.path.join(out, "neost.ppm")
    hatari_png = os.path.join(out, "hatari.png")

    joy_args = (["--joy-script", str(args.joy_at), args.joy_script] if args.joy_script
                else ["--joy-script-file", str(args.joy_at), args.joy_script_file])

    # 1. Compilation du script — la MÊME grammaire des deux côtés, par construction.
    r = run([HEADLESS, args.rom, "--joy-script-compile", compiled] + joy_args)
    if r.returncode != 0:
        sys.exit("compilation du script en échec :\n" + r.stderr)
    nmasks = os.path.getsize(compiled)
    print("script compilé : %d trames, première entrée à la trame %d" % (nmasks, args.joy_at))

    def neost_shot(frames, path, with_script):
        cmd = [HEADLESS, args.rom, "--machine", args.machine, "--mem", args.mem,
               "--disk", args.disk, "--fastfdc", "--frames", str(frames),
               "--screenshot", path]
        if with_script:
            cmd += joy_args
        rr = run(cmd)
        if rr.returncode != 0 or not os.path.exists(path):
            sys.exit("run NeoST en échec :\n" + rr.stderr[-2000:])
        return path

    def hatari_window(target, half, tag, script_start=None):
        """Fenêtre d'images Hatari autour de `target`. script_start=None : AUCUNE
        entrée (passe d'alignement)."""
        env = dict(os.environ)
        env["HATARI_SEED"] = args.seed
        env["HATARI_ORACLE_SCAN"] = str(half)
        if script_start is None:
            env.pop("NEOST_JOY_SCRIPT", None)
        else:
            env["NEOST_JOY_SCRIPT"] = compiled
            env["NEOST_JOY_START"] = str(script_start)
        png = os.path.join(out, "hatari_%s.png" % tag)
        rr = run(["bash", HATARI_SH, args.rom, args.disk, str(target + half + 20),
                  str(target), png, args.machine, "fastfdc", args.memsize], env=env)
        if rr.returncode != 0:
            sys.exit("run Hatari en échec :\n" + rr.stdout + rr.stderr)
        if script_start is not None and "joystick script loaded" not in rr.stdout:
            sys.exit("Hatari n'a pas chargé le script : patch appliqué et recompilé ?")
        d = png[:-4] + ".scan"
        # Tri NUMÉRIQUE : « f_100000.png » se classe avant « f_99999.png » en
        # lexicographique, et « la plus petite trame identique » devenait fausse.
        files = [f for f in os.listdir(d) if f.endswith(".png")]
        return d, sorted(files, key=lambda f: int(re.search(r"(\d+)", f).group(1)))

    def compare_one(path):
        rr = run([sys.executable, COMPARE, ref_holder[0], path,
                  "--crop", args.crop, "--max", "0", "--report"])
        # « diff_px=N / TOTAL » : la SEULE forme qui donne l'écart total. Le
        # rapport contient aussi des « 86/416 px » par scanline — les confondre
        # afficherait quelques pixels là où l'image entière diverge.
        m = re.search(r"diff_px=(\d+)", rr.stdout)
        return rr.returncode == 0, (int(m.group(1)) if m else None)

    ref_holder = [None]

    def find_exact(ref_ppm, scan_dir, files, exhaustive=False):
        """Trame IDENTIQUE, jamais « la moins pire » : accepter une image presque
        juste est exactement la façon dont un oracle cesse de servir.

        Comparaisons par LOTS parallèles — une fenêtre large fait plusieurs
        centaines d'images, et un sous-processus par image mettait l'oracle à la
        dizaine de minutes. Le résultat ne dépend pas de l'ordre d'arrivée : c'est
        toujours la plus petite trame identique qui gagne.

        exhaustive=True (passe d'ALIGNEMENT) : on parcourt TOUTE la fenêtre et on
        rend aussi le NOMBRE d'images identiques. Sur une scène statique — bureau,
        écran-titre figé — toutes le sont, et « la plus petite » n'est que la borne
        basse de la fenêtre : le décalage vaudrait mécaniquement −scan, et le
        verdict final serait trivialement « identique » (mesuré : 121 images sur
        121). Un alignement n'est mesuré que s'il est UNIQUE."""
        ref_holder[0] = ref_ppm
        best, best_n = None, None
        hits_all = []
        batch = max(1, (os.cpu_count() or 4))
        for i in range(0, len(files), batch):
            chunk = [(int(re.search(r"(\d+)", f).group(1)), os.path.join(scan_dir, f))
                     for f in files[i:i + batch]]
            with ThreadPoolExecutor(max_workers=batch) as ex:
                results = list(ex.map(compare_one, [p for _, p in chunk]))
            hits = [n for (n, _), (ok, _) in zip(chunk, results) if ok]
            hits_all += hits
            if hits and not exhaustive:
                return min(hits), 0
            for (n, _), (_, d) in zip(chunk, results):
                if d is not None and (best is None or d < best):
                    best, best_n = d, n
        if hits_all:
            return min(hits_all), len(hits_all)
        return None, (best, best_n)

    # --- Passe A : mesurer le décalage de boot, SANS aucune entrée -------------
    anchor = args.align_at if args.align_at >= 0 else args.joy_at
    align_ppm = neost_shot(anchor, os.path.join(out, "align.ppm"), with_script=False)
    sdir, files = hatari_window(anchor, args.scan, "align", script_start=None)
    hit, info = find_exact(align_ppm, sdir, files, exhaustive=True)
    if hit is not None and info > 1:
        print("\nALIGNEMENT AMBIGU : %d images de la fenêtre sont identiques à la trame NeoST %d."
              % (info, anchor))
        print("  La scène d'ancrage est STATIQUE — le décalage ne peut pas être mesuré, il")
        print("  vaudrait la borne basse de la fenêtre. Choisir --align-at sur une scène qui")
        print("  BOUGE d'une trame à l'autre (défilement, animation), avant toute entrée.")
        return 1
    if hit is None:
        best, best_n = info
        print("\nÉCHEC D'ALIGNEMENT : aucune trame Hatari identique à la trame NeoST %d"
              " sans entrée." % anchor)
        # `best` reste None si la fenêtre est vide ou si aucune comparaison n'a rendu
        # de « diff_px= » : sans cette garde, le diagnostic devenait un TypeError.
        if best is not None:
            print("  la plus proche : %d, %d px — élargir --scan, ou choisir --align-at sur"
                  " une scène ANIMÉE mais déterministe (pas un écran figé)." % (best_n, best))
        else:
            print("  aucune image comparable — fenêtre vide ? élargir --scan et vérifier"
                  " que Hatari a bien produit un AVI.")
        print("  (une divergence dès AVANT toute entrée est un vrai signal : la comparer"
              " d'abord avec run_etalons.py --oracle)")
        return 1
    delta = hit - anchor
    print("alignement : trame NeoST %d == trame Hatari %d  →  décalage %+d (unique sur %d images)"
          % (anchor, hit, delta, len(files)))

    # --- Passe B : rejeu avec les entrées posées au MÊME instant-programme -----
    neost_shot(args.frames, neost_ppm, with_script=True)
    print("NeoST     : trame %d → %s" % (args.frames, neost_ppm))
    scan_dir, frames = hatari_window(args.frames + delta, args.scan_final, "run",
                                     script_start=args.joy_at + delta)
    print("Hatari    : %d images autour de la trame %d (script décalé de %+d)"
          % (len(frames), args.frames + delta, delta))
    exact, info = find_exact(neost_ppm, scan_dir, frames)
    best, best_n = (None, None) if exact is not None else info

    print()
    if exact is not None:
        print("VERDICT : IDENTIQUE — trame NeoST %d == trame Hatari %d (décalage %+d,"
              " alignement %+d)" % (args.frames, exact, exact - args.frames, delta))
        # Ménage sur SUCCÈS seulement : une fenêtre par défaut, c'est ~300 PNG plein
        # écran par run, laissés dans /tmp. En divergence on garde tout — les images
        # SONT le diagnostic — et on dit où.
        if temp_out:
            shutil.rmtree(out, ignore_errors=True)
        return 0
    if temp_out:
        print("  artefacts conservés dans %s" % out)
    print("VERDICT : DIVERGENCE — aucune trame de la fenêtre n'est identique.")
    if best is not None:
        print("  la plus proche : trame Hatari %d, %d px d'écart" % (best_n, best))
        print("  → %s  vs  %s/f_%05d.png" % (neost_ppm, scan_dir, best_n))
    print("  À écarter AVANT de crier à la divergence : élargir --scan-final ; vérifier que")
    print("  l'ancre (--align-at) précède bien toute entrée ; refaire avec une autre --seed")
    print("  (une divergence qui ne survit pas au changement de graine est un artefact).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
