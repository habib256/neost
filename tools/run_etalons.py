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
#  Codes : 0 tout vérifié, 1 échec, 2 usage, 77 rien n'a échoué mais des étalons
#  ont été sautés (ROM propriétaire, disque optionnel ou référence absents).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Suffixe .exe sous Windows : sans lui, le binaire est cherché sous un nom qui
# n'existe pas et la suite se déclare « non bâtie » — la seule plateforme livrée
# où les tests ne pouvaient pas tourner du tout.
_EXE = ".exe" if sys.platform == "win32" else ""
MANIFEST = Path(__file__).resolve().parent / "etalons.json"
OUT_DIR = ROOT / "tests" / "out"
REF_DIR = ROOT / "tests" / "reference"
HEADLESS = ROOT / "build" / ("neost-headless" + _EXE)
COMPARE = ROOT / "tools" / "compare_screenshot.py"
HATARI_ORACLE = ROOT / "tools" / "hatari_oracle.sh"

# Garde-fou A18 (audit 2026-08-27) : AUCUN appel à l'émulateur n'avait de timeout —
# un 68000 qui boucle (la régression même que ces tests cherchent) consommait les
# 45 min du job CI sans diagnostic. Verdict 124 (convention de timeout(1)) pour les
# appels à code de retour ; sortie 2 immédiate pour les appels check=True (leur
# échec est de toute façon fatal au run).
def run_timed(cmd, limit_s, **kw):
    try:
        return subprocess.run(cmd, timeout=limit_s, **kw)
    except subprocess.TimeoutExpired:
        print(f"  TIMEOUT après {limit_s}s : " + " ".join(map(str, cmd)),
              file=sys.stderr, flush=True)
        if kw.get("check"):
            sys.exit(2)
        return subprocess.CompletedProcess(cmd, 124)

BUFFER_W = 416   # largeur du buffer NeoST (overscan) ; un oracle Hatari est en 2× (≥832)


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
        run_timed([sys.executable, str(script), out_arg], 300, cwd=ROOT, check=True)
        return path.exists()
    return False


def run_selftest(flag: str, cpu: str, extra_env: dict | None = None) -> int:
    # Auto-tests logique pure (P0) : pas de boot, pas d'oracle. La ROM sert juste à
    # construire la machine (RAM) ; --glue-selftest/--spec512-selftest court-circuitent
    # le boot et renvoient le verdict via le code de sortie.
    # `env` du manifeste (A16b, 2026-08-28) : rejouer le MÊME auto-test avec un verrou
    # d'émulation armé. Un chemin opt-in que personne n'exécute pourrit — celui-ci
    # segfautait depuis des semaines sans qu'aucun palier ne puisse le voir.
    cmd = [str(HEADLESS), "roms/etos256us.img", flag, "--cpu", cpu]
    env = None
    prefix = ""
    if extra_env:
        env = dict(os.environ, **{k: str(v) for k, v in extra_env.items()})
        prefix = " ".join(f"{k}={v}" for k, v in extra_env.items()) + " "
    print("  $ " + prefix + " ".join(cmd))
    return run_timed(cmd, 300, cwd=ROOT, env=env).returncode


def headless_cmd(entry: dict) -> list[str]:
    """Commande neost-headless d'un étalon pixel, sans la capture. Partagée avec
    tools/cpu_swap_gate.py : la porte d'échange du cœur rejoue les mêmes scénarios."""
    rom = entry.get("rom", "roms/etos192us.img")
    cmd = [str(HEADLESS), str(ROOT / rom), "--cpu", entry.get("cpu", "moira"),
           "--machine", entry.get("machine", "ste"),
           "--mem", entry.get("mem", "512k"),
           "--frames", str(entry.get("frames", 200))]
    if entry.get("fastfdc"):
        cmd.append("--fastfdc")
    disk = entry.get("disk")
    if disk:
        # A14 : --disk-ro sur TOUTE capture d'étalon. 13 images de disks/etalons/ sont
        # SUIVIES PAR GIT : une écriture invitée sur l'une d'elles modifierait la
        # donnée d'entrée d'un étalon dans l'arbre, en silence, et la référence
        # dériverait sans qu'un seul fichier de code ait bougé. L'option ne change
        # RIEN pour la machine invitée (les écritures restent en RAM, les relectures
        # les voient) — vérifié à l'octet près sur le diagnostic MegaSTE, dont le
        # test F formate vraiment les deux disquettes : même dump série, fichiers
        # intacts. Elle ne coûte donc aucune fidélité, et elle ferme le piège.
        cmd += ["--disk", str(ROOT / disk), "--disk-ro"]
    if entry.get("keys"):
        cmd += ["--keys", entry["keys"]]
    if entry.get("keys_at"):                 # [trame, "touches"] ou liste de paires — pilotage daté
        ka = entry["keys_at"]
        pairs = ka if isinstance(ka[0], list) else [ka]
        for n, keys in pairs:
            cmd += ["--keys-at", str(n), keys]
    if entry.get("cart"):
        cmd += ["--cart", str(ROOT / entry["cart"])]
    return cmd


def run_headless_capture(entry: dict, out_ppm: Path) -> int:
    cmd = headless_cmd(entry) + ["--screenshot", str(out_ppm)]
    print("  $", " ".join(cmd))
    r = run_timed(cmd, 900, cwd=ROOT)
    return r.returncode


def oracle_env(entry: dict, **extra) -> dict:
    """Environnement d'un run hatari_oracle.sh : `oracle_keys` du manifeste devient
    HATARI_ORACLE_KEYS (« down:up:scancode … », touche ENFONCÉE à la VBL down et
    RELÂCHÉE à up — cf. le bandeau PILOTAGE CLAVIER du script). C'est ce qui rend
    re-dérivable un oracle qui exige un appui touche (nocooper)."""
    env = dict(os.environ, **{k: str(v) for k, v in extra.items()})
    keys = entry.get("oracle_keys")
    if keys:
        env["HATARI_ORACLE_KEYS"] = " ".join(f"{d}:{u}:{sc}" for d, u, sc in keys)
    return env


def run_hatari_oracle(entry: dict, out_png: Path) -> int:
    if not HATARI_ORACLE.exists():
        print("  [oracle] hatari_oracle.sh introuvable", file=sys.stderr)
        return 1
    rom = str(ROOT / entry.get("rom", "roms/etos192us.img"))
    disk = str(ROOT / entry["disk"])
    frames = int(entry.get("frames", 400))
    frame = int(entry.get("frame", frames - 10))
    # MARGE obligatoire : Hatari doit tourner AU-DELÀ de la trame extraite. Avec
    # run-vbls == frame, l'image demandée est la toute dernière de l'AVI — souvent
    # absente ou incomplète, et ffmpeg rend alors une image NOIRE. C'est ainsi que la
    # référence « oracle » de spectrum512_diapo était devenue entièrement noire : le
    # premier étalon spec512 du projet ne validait plus AUCUN pixel de rendu.
    vbls = str(max(frames, frame + 25))
    frame = str(frame)
    machine = entry.get("machine", "st")
    # Traduction de « mem » vers --memsize d'Hatari (0 = 512 Ko, sinon la taille en Mo).
    memTxt = str(entry.get("mem", "512k")).lower()
    memArg = "0" if memTxt.startswith("512") else memTxt.rstrip("m")
    cmd = ["bash", str(HATARI_ORACLE), rom, disk, vbls, frame, str(out_png), machine]
    # oracle_fastfdc : aligne la timeline Hatari sur le run NeoST --fastfdc (sans quoi
    # les numéros de trame divergent — le FDC réel charge chaque image bien plus tard).
    cmd.append("fastfdc" if entry.get("oracle_fastfdc") else "-")   # 7e arg positionnel
    cmd.append(memArg)                                             # 8e : taille RAM
    print("  $", " ".join(cmd))
    return run_timed(cmd, 1800, cwd=ROOT, env=oracle_env(entry)).returncode


def oracle_scan_pick(entry: dict, out_png: Path, neost_ppm: Path, scan: int) -> bool:
    """Régénère une référence oracle pour un étalon qui BOOTE UN DISQUE.

    Pourquoi une FENÊTRE et pas une trame : Hatari sème son RNG sur l'horloge hôte
    (`Hatari_srand(time(NULL))`, sdl/main_sdl.c) et s'en sert notamment pour la
    POSITION ANGULAIRE INITIALE de la disquette (fdc.c). La durée du boot varie donc
    d'un run à l'autre, et avec elle la numérotation des trames de l'AVI : mesuré le
    2026-08-19 sur cuddly_demos, la même trame NeoST tombait sur n+61 dans un run et
    n-2 dans un autre. On extrait donc [frame-scan, frame+scan] et on RETIENT la trame
    qui correspond à la capture NeoST — en s'arrêtant à la PREMIÈRE correspondance
    EXACTE, jamais à la « moins pire » : installer une image simplement proche
    figerait un écart au lieu de le signaler.
    """
    env = oracle_env(entry, HATARI_ORACLE_SCAN=scan)
    rom = str(ROOT / entry.get("rom", "roms/etos192us.img"))
    disk = str(ROOT / entry["disk"])
    frames = int(entry.get("frames", 400))
    frame = int(entry.get("frame", frames - 10))
    vbls = str(max(frames, frame + scan + 25))
    memTxt = str(entry.get("mem", "512k")).lower()
    memArg = "0" if memTxt.startswith("512") else memTxt.rstrip("m")
    cmd = ["bash", str(HATARI_ORACLE), rom, disk, vbls, str(frame), str(out_png),
           entry.get("machine", "st"),
           "fastfdc" if entry.get("oracle_fastfdc") else "-", memArg]
    print("  $ HATARI_ORACLE_SCAN=%d %s" % (scan, " ".join(cmd)))
    if run_timed(cmd, 1800, cwd=ROOT, env=env).returncode != 0:
        return False
    scan_dir = Path(str(out_png)[:-4] + ".scan")
    shots = sorted(scan_dir.glob("f_*.png"))
    if not shots:
        print(f"  [oracle] fenêtre vide : {scan_dir}", file=sys.stderr)
        return False
    # Ordre d'essai : la trame nominale d'abord, puis en s'écartant — le décalage est
    # petit dans la plupart des runs, inutile de comparer 2×scan images à chaque fois.
    by_num = {int(p.stem[2:]): p for p in shots}
    order = [frame] + [frame + d for k in range(1, scan + 1) for d in (k, -k)]
    tried = 0
    for n in order:
        p = by_num.get(n)
        if p is None:
            continue
        tried += 1
        if compare_shots(neost_ppm, p, entry, quiet=True) == 0:
            shutil.copy2(p, out_png)
            print(f"  [oracle] trame {n} retenue (décalage {n - frame:+d} vs « frame », "
                  f"{tried} image(s) comparée(s)) — identique à la capture NeoST")
            return True
    print(f"  [oracle] AUCUNE des {tried} trames de la fenêtre "
          f"[{frame - scan}, {frame + scan}] ne correspond à la capture NeoST — "
          f"c'est une VRAIE divergence, pas un décalage de numérotation", file=sys.stderr)
    return False


def compare_shots(neost: Path, ref: Path, entry: dict, quiet: bool = False) -> int:
    crop = entry.get("crop", "active")
    mx = entry.get("max_diff_px", 0)
    cmd = [sys.executable, str(COMPARE), str(neost), str(ref),
           "--crop", crop, "--max", str(mx)]
    if quiet:
        # Balayage de fenêtre : une centaine de diffs, dont on ne veut QUE le verdict.
        return run_timed(cmd, 300, cwd=ROOT, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL).returncode
    cmd.append("--report")
    print("  $", " ".join(cmd))
    return run_timed(cmd, 300, cwd=ROOT).returncode


# L'ÉCHELLE DES RÉFÉRENCES, du plus fort au plus faible → docs/REFERENCE_LADDER.md.
# Un vert se lit selon l'échelon : « hatari » veut dire « d'accord avec Hatari », pas
# « fidèle », et « snapshot » veut seulement dire « n'a pas bougé ».
#   hardware  [A] relevé sur machine réelle, propre ou publié par un tiers nommé
#   primary   [B] rendu calculé depuis une fiche, un schéma, une netlist Atari
#   rom       [C] verdict d'un programme ou d'une ROM Atari
#   rtl       [D] simulation d'un RTL dérivé du silicium ou d'un schéma
#   hatari    [D] capture de l'Hatari épinglé
#   snapshot  ─   self-capture NeoST : non-régression, aucune preuve
LADDER = ("hardware", "primary", "rom", "rtl", "hatari", "snapshot")
EXTERNAL_RUNGS = tuple(k for k in LADDER if k != "snapshot")


def ladder_errors(entries: list[dict]) -> list[str]:
    """Un étalon pixel sans échelon valide ne se lit pas : refus, pas de défaut."""
    return [f"{e['id']}: ref_kind={e.get('ref_kind')!r} (attendu : {'|'.join(LADDER)})"
            for e in entries if not e.get("type") and e.get("ref_kind") not in LADDER]


def ladder_summary(entries: list[dict]) -> str:
    kinds = [e.get("ref_kind") for e in entries if not e.get("type")]
    return "ÉCHELLE : " + " · ".join(f"{k} {kinds.count(k)}" for k in LADDER)


def resolve_ref(entry: dict, ref_ppm: Path, ref_png: Path):
    # Provenance EXPLICITE de la référence, validée au chargement (ladder_errors) :
    #   échelon externe → le .png venu de cette source, JAMAIS la self-capture ;
    #   snapshot        → la self-capture NeoST .ppm (non-régression).
    kind = entry["ref_kind"]
    if kind in EXTERNAL_RUNGS:
        return (ref_png, kind) if ref_png.exists() else (None, kind)
    # Self-capture NeoST : .ppm de préférence, sinon .png (certaines réfs V2 archivées).
    if ref_ppm.exists():
        return ref_ppm, kind
    return (ref_png, kind) if ref_png.exists() else (None, kind)


# Étalons dont la comparaison n'a PAS eu lieu (référence absente) : recensés pour
# que « TOUS OK » ne puisse jamais masquer une couverture creuse.
SKIPPED = []
# Étalons NON EXÉCUTÉS faute de ROM propriétaire (roms/tos*.img). Même règle : recensés,
# jamais fondus dans le vert. Cf. rom_is_free().
SKIPPED_ROM = []
# Étalons NON EXÉCUTÉS faute d'un disque `optional` absent : sautés, donc recensés aussi.
SKIPPED_DISK = []
# Sortie « rien d'échoué, mais des étalons sautés » : run_all.py la recense comme étape
# NON EXÉCUTÉE au lieu de la fondre dans le vert. Même valeur que l'EXIT_SKIPPED des
# autres outils ; nom distinct à dessein : le fil-piège de run_all.py
# (fast_tier_proprietary) exempte tout outil qui définit EXIT_SKIPPED, et celui-ci,
# lancé au palier fast, doit rester inspecté.
EXIT_SKIPPED_RUN = 77


def rom_is_free(rom: str) -> bool:
    """Une ROM LIBRE (EmuTOS, `roms/etos*.img`) est livrée avec le dépôt : son absence
    est une CASSE du dépôt, pas une config utilisateur. Les TOS Atari, eux, sont
    propriétaires — le dépôt ne peut pas garantir leur présence (et ne devrait pas les
    redistribuer), donc un étalon qui en dépend se SAUTE au lieu d'échouer."""
    return Path(rom).name.startswith("etos")


# A27 : worker du mode parallèle. Chaque étalon tourne dans SON processus, sa
# sortie est bufferisée (redirect process-local, sûr) et rejouée par le parent DANS
# L'ORDRE DU MANIFESTE — le journal reste lisible et déterministe. Les listes de
# recensement (SKIPPED/SKIPPED_ROM), globales au processus fils, sont RETOURNÉES
# et re-fusionnées par le parent : sans cela le parallélisme aurait avalé les SKIP
# en silence — exactement le vert creux que ce fichier combat.
def _run_one_buffered(entry: dict, args):
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
        ok = run_one(entry, args)
    return entry["id"], ok, buf.getvalue(), list(SKIPPED), list(SKIPPED_ROM), list(SKIPPED_DISK)


def run_one(entry: dict, args) -> bool:
    eid = entry["id"]
    print(f"\n=== {eid} — {entry['name']} ===")

    selftest_flag = {"glue_selftest": "--glue-selftest",
                     "spec512_selftest": "--spec512-selftest",
                     "bus_selftest": "--bus-selftest",
                     "mfp_selftest": "--mfp-selftest",
                     "serloop_selftest": "--serloop-selftest",
                     "msa_selftest": "--msa-selftest",
                     "gemdos_selftest": "--gemdos-selftest",
                     "enec_selftest": "--enec-selftest",
                     "usatan_selftest": "--usatan-selftest",
                     "netusbee_selftest": "--netusbee-selftest"}.get(entry.get("type"))
    if selftest_flag:
        rc = run_selftest(selftest_flag, entry.get("cpu", "moira"), entry.get("env"))
        if rc != 0:
            print(f"  ÉCHEC {eid} ({entry['type']}, exit {rc})")
            return False
        # L'ID, pas le TYPE : deux entrées peuvent partager le type (glue_selftest et
        # glue_selftest_attr rejouent le même auto-test avec un verrou différent) et
        # le journal affichait alors deux fois la même ligne.
        print(f"  OK {eid}")
        return True

    # ROM absente : distinguer les deux cas AVANT de lancer quoi que ce soit. Sans cela,
    # retirer les TOS propriétaires du dépôt faisait ÉCHOUER 8 étalons (neost-headless
    # sort en erreur sur une ROM introuvable) — c'est ce couplage qui rendait le nettoyage
    # juridique « impossible sans casser la CI ».
    rom = entry.get("rom")
    if rom and not (ROOT / rom).exists():
        if rom_is_free(rom):
            print(f"  ROM LIBRE absente : {rom} — dépôt incomplet", file=sys.stderr)
            return False
        print(f"  ROM propriétaire absente : {rom} (SKIP — non redistribuable)")
        SKIPPED_ROM.append(eid)
        return True

    if entry.get("disk") and not ensure_disk(entry):
        msg = f"  disque manquant : {entry['disk']}"
        if entry.get("optional"):
            print(msg + " (optionnel — SKIP)")
            SKIPPED_DISK.append(eid)
            return True
        print(msg, file=sys.stderr)
        return False

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    neost_ppm = OUT_DIR / f"{eid}_neost.ppm"
    ref_ppm = REF_DIR / f"{eid}.ppm"
    ref_png = REF_DIR / f"{eid}.png"

    # La capture NeoST vient EN PREMIER : le mode « scan » de l'oracle (ci-dessous) en a
    # besoin pour choisir, dans la fenêtre Hatari, la trame qui correspond.
    rc = run_headless_capture(entry, neost_ppm)
    if rc != 0 or not neost_ppm.exists():
        print(f"  ÉCHEC capture NeoST (exit {rc})")
        return False
    print(f"  capture → {neost_ppm.relative_to(ROOT)}")

    # --oracle écrase le .png : jamais sur un échelon au-dessus d'Hatari, sinon une
    # capture d'émulateur remplacerait en silence une preuve plus forte.
    if args.oracle and entry.get("ref_kind") in ("hardware", "primary", "rom", "rtl"):
        print(f"  ✗ {eid}: ref_kind={entry['ref_kind']} — --oracle refusé "
              f"(Hatari n'écrase pas un échelon supérieur)", file=sys.stderr)
        return False

    if (args.oracle or args.oracle_check) and entry.get("disk"):
        REF_DIR.mkdir(parents=True, exist_ok=True)
        tmp_png = OUT_DIR / f"{eid}_oracle.png"
        scan = int(entry.get("oracle_scan", 0) or 0)
        if scan:
            found = oracle_scan_pick(entry, tmp_png, neost_ppm, scan)
            if not found:
                return False
        elif run_hatari_oracle(entry, tmp_png) != 0:
            return False
        if args.oracle_check:
            # A11 — mode CI : on ne TOUCHE PAS à la référence commise, on la confronte
            # à l'oracle qu'Hatari produit AUJOURD'HUI. Deux propriétés distinctes se
            # vérifient dans ce seul passage, et il faut les deux :
            #   1. l'étape ci-dessus a déjà exigé qu'Hatari rende une image identique à
            #      la capture NeoST du jour (oracle_scan_pick s'arrête à l'égalité
            #      EXACTE, jamais à la « moins pire ») ;
            #   2. la comparaison ci-dessous exige que la référence COMMISE dise encore
            #      la même chose — sans quoi un fichier de référence pourrait dériver
            #      (régénéré à la main contre un autre oracle, pin déplacé, ffmpeg
            #      différent) sans que rien ne le signale.
            # Ensemble : NeoST == référence == Hatari. C'est la boucle qu'A11 ferme.
            if not ref_png.exists():
                print(f"  ✗ {eid}: ref_kind=hatari mais {ref_png.name} absent — "
                      f"rien à confronter", file=sys.stderr)
                return False
            if compare_shots(tmp_png, ref_png, entry) != 0:
                print(f"  ÉCHEC oracle-check {eid} : l'oracle Hatari REGÉNÉRÉ diffère de "
                      f"la référence commise {ref_png.name}")
                return False
            print(f"  OK oracle-check {eid} — oracle régénéré ≡ {ref_png.name}")
            # On NE sort PAS ici : la comparaison NeoST ↔ référence suit, plus bas.
        else:
            shutil.copy2(tmp_png, ref_png)
            print(f"  référence oracle → {ref_png.relative_to(ROOT)}")

    if args.update_ref:
        # Seul un snapshot compare la self-capture : sur un échelon externe, y copier un
        # .ppm était un no-op trompeur (« référence → … » affiché, baseline inchangée,
        # fichier orphelin dans tests/reference/). Une référence Hatari se régénère
        # avec --oracle, pas --update-ref.
        if entry.get("ref_kind") != "snapshot":
            print(f"  ✗ {eid}: ref_kind={entry.get('ref_kind')} — --update-ref sans effet "
                  f"(réservé aux snapshot ; --oracle régénère une référence Hatari)")
            return False
        REF_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copy2(neost_ppm, ref_ppm)
        print(f"  référence → {ref_ppm.relative_to(ROOT)}")
        return True

    if args.no_compare:
        return True

    ref, kind = resolve_ref(entry, ref_ppm, ref_png)
    if not ref:
        want = ref_ppm.name if kind == "snapshot" else f"{ref_png.name} ({kind})"
        print(f"  SKIP diff : pas de référence {want} (ref_kind={kind}) — "
              + ("lancer --update-ref" if kind == "snapshot"
                 else "lancer --oracle" if kind == "hatari"
                 else f"poser la référence de l'échelon {kind}"))
        # La capture a EU LIEU mais il n'y a rien à quoi la comparer : ce n'est pas
        # la même chose qu'une donnée d'entrée absente, et le compter comme une
        # réussite transformait l'étalon en no-op parfaitement vert. On le RECENSE.
        SKIPPED.append(eid)
        # `optional` n'excuse que l'absence de DONNÉE D'ENTRÉE (disque non rapatriable).
        # Si le disque est là et que seule la référence manque, l'étalon prétend
        # surveiller quelque chose qu'il ne surveille pas : c'est un ÉCHEC. Sans cette
        # distinction, cuddly_demos — dont le disque EST suivi par git — restait un
        # no-op vert permanent, et la mutation « mauvais disque + 5 trames » passait.
        disk = entry.get("disk")
        if entry.get("optional") and disk and (ROOT / disk).exists():
            print(f"  ⚠ {eid}: disque présent mais référence absente — "
                  f"« optional » ne couvre PAS ce cas")
            return False
        return entry.get("optional", False)
    print(f"  référence : {ref.name} (ref_kind={kind})")

    if compare_shots(neost_ppm, ref, entry) != 0:
        print(f"  ÉCHEC diff {eid}")
        return False
    print(f"  OK {eid}")
    return True


def _png_dims(path: Path):
    import struct
    d = path.read_bytes()[:24]
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", d[16:24])


def _ref_content(path: Path) -> tuple[int, float] | None:
    """(nombre de couleurs plafonné à 9, part de la couleur MINORITAIRE) d'une référence.

    La part minoritaire départage une image réellement porteuse d'information d'une
    image quasi vide : un étalon de scroll est bichrome PAR CONSTRUCTION et valide
    pourtant chaque pixel, tandis qu'un écran noir avec trois pixels parasites ne
    valide rien. Renvoie None si l'image est illisible (ffmpeg absent, format
    inconnu) — l'appelant traite alors le contrôle comme non concluant.
    """
    try:
        sys.path.insert(0, str(Path(__file__).parent))
        import compare_screenshot as cs
        _w, _h, px = cs._load_image(path)
    except Exception:
        return None
    from collections import Counter
    c = Counter(px[i : i + 3] for i in range(0, len(px), 3))
    total = sum(c.values()) or 1
    return min(len(c), 9), (total - c.most_common(1)[0][1]) / total


def verify_refs(entries: list[dict]) -> int:
    # Contrôle de provenance (P2) : chaque entrée ref_kind=hatari doit avoir un .png
    # aux dimensions d'un oracle Hatari (2× le buffer NeoST, ex. 832×552) — sinon c'est
    # une self-capture renommée (piège : figerait un bug). Les snapshot doivent avoir un .ppm.
    bad = 0
    for e in entries:
        if e.get("type"):                 # selftests : pas de réf image
            continue
        eid = e["id"]; kind = e.get("ref_kind"); opt = e.get("optional")
        png = REF_DIR / f"{eid}.png"; ppm = REF_DIR / f"{eid}.ppm"
        if kind in EXTERNAL_RUNGS and kind != "hatari":
            # Aucun format imposé : un relevé matériel n'a pas la géométrie d'Hatari.
            if png.exists():
                print(f"  ✓ {eid}: {kind} {png.name}")
            elif opt:
                print(f"  · {eid}: {kind} .png absent (optionnel)")
            else:
                print(f"  ✗ {eid}: ref_kind={kind} mais {png.name} absent"); bad += 1
        elif kind == "hatari":
            if not png.exists():
                if opt:
                    print(f"  · {eid}: oracle .png absent (optionnel — fetch/oracle à la demande)")
                else:
                    print(f"  ✗ {eid}: ref_kind=hatari mais {png.name} absent"); bad += 1
                continue
            dims = _png_dims(png)
            if not dims or dims[0] < 2 * BUFFER_W:
                print(f"  ✗ {eid}: {png.name} {dims} n'a pas la taille d'un oracle Hatari "
                      f"(≥ {2*BUFFER_W}px de large) — self-capture renommée ?"); bad += 1
            else:
                print(f"  ✓ {eid}: oracle {png.name} {dims[0]}×{dims[1]}")
        elif kind == "snapshot":
            if ppm.exists() or png.exists():
                print(f"  ✓ {eid}: snapshot {(ppm if ppm.exists() else png).name}")
            elif opt:
                print(f"  · {eid}: snapshot absent (optionnel — fetch à la demande)")
            else:
                print(f"  ✗ {eid}: ref_kind=snapshot mais aucune réf ({ppm.name}/{png.name})"); bad += 1

        # GARDE-FOU DE VACUITÉ. Une référence UNIFORME (écran noir) valide zéro pixel
        # tout en affichant « OK » : c'est arrivé deux fois sur cet étalon spec512, et
        # le contrôle de provenance ne regardait que les dimensions. Une image de moins
        # de 3 couleurs n'est pas une preuve — sauf pour les étalons dont c'est le
        # signal voulu (trace_odd peint l'écran en vert/rouge selon le verdict,
        # overscan_top n'a que 2 couleurs par construction) : "uniform_ok": true.
        # MÊME résolution que la comparaison réelle (resolve_ref) : le raccourci
        # « png d'abord » inspectait un .png inutilisé quand un étalon snapshot
        # possède les deux fichiers — un .ppm noirci passait le contrôle.
        ref, _k = resolve_ref(e, ppm, png)
        if ref is not None and not e.get("uniform_ok"):
            got = _ref_content(ref)
            if got is None:
                # ÉCHEC, pas avertissement : un contrôle qui ne peut pas s'exécuter ne
                # prouve RIEN, et il échouait jusqu'ici « en mode ça passe ». Sans
                # ffmpeg, les 6 références .png — dont les TROIS oracles spec512,
                # justement l'étalon dont la référence a été noire deux fois — sortaient
                # « RÉFS OK », code 0. Une référence noircie repassait donc verte sur
                # toute machine sans ffmpeg. Le remède est d'installer ffmpeg, pas de
                # laisser le garde-fou s'auto-désarmer.
                print(f"  ✗ {eid}: vacuité NON VÉRIFIABLE ({ref.name} illisible — "
                      f"ffmpeg absent ?) — contrôle non concluant, donc refusé")
                bad += 1
            else:
                nc, minor = got
                if nc <= 1 or minor < 0.005:
                    print(f"  ✗ {eid}: référence {ref.name} QUASI UNIFORME "
                          f"({nc} couleur(s), minoritaire {minor:.3%}) — "
                          f"elle ne valide aucun pixel"); bad += 1
    print("\n" + ("RÉFS OK" if bad == 0 else f"{bad} réf(s) suspecte(s)"))
    return 0 if bad == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Suite étalons NeoST (headless)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", help="IDs séparés par des virgules")
    ap.add_argument("--fetch", action="store_true", help="fetch_etalons.py d'abord")
    ap.add_argument("--update-ref", action="store_true", help="sauve la capture NeoST en référence")
    ap.add_argument("--oracle", action="store_true", help="capture Hatari comme référence PNG")
    ap.add_argument("--oracle-check", action="store_true",
                    help="A11 : régénère l'oracle Hatari et le CONFRONTE à la référence "
                         "commise sans l'écraser (mode CI). Ne retient que les étalons "
                         "ref_kind=hatari qui bootent un disque.")
    ap.add_argument("--verify-refs", action="store_true",
                    help="contrôle la provenance des références (oracle vs snapshot)")
    ap.add_argument("--no-compare", action="store_true")
    ap.add_argument("--jobs", type=int, default=0,
                    help="étalons en parallèle (0 = auto, min(4, cpus) ; 1 = séquentiel ; "
                         "forcé à 1 avec --oracle et --update-ref)")
    args = ap.parse_args()

    if not HEADLESS.exists() and not args.verify_refs:
        print(f"Build requis : cmake --build build  ({HEADLESS} absent)", file=sys.stderr)
        return 2

    entries = load_manifest()
    bad_rungs = ladder_errors(entries)
    if bad_rungs:
        print("Échelon de référence invalide dans etalons.json :", file=sys.stderr)
        print("\n".join("  ✗ " + m for m in bad_rungs), file=sys.stderr)
        return 2
    if args.verify_refs:
        # Même discipline --only que le chemin d'exécution (strip + IDs inconnus
        # refusés) : « --verify-refs --only bogus » sortait « RÉFS OK », code 0,
        # en n'ayant vérifié AUCUNE référence.
        if args.only:
            want = {t.strip() for t in args.only.split(",") if t.strip()}
            unknown = want - {e["id"] for e in entries}
            if unknown:
                print("ID inconnu(s) : " + ", ".join(sorted(unknown)))
                return 2
            sel = [e for e in entries if e["id"] in want]
        else:
            sel = entries
        if not sel:
            print("aucun étalon sélectionné — rien vérifié")
            return 2
        print(ladder_summary(sel))
        return verify_refs(sel)
    if args.list:
        for e in entries:
            opt = " [opt]" if e.get("optional") else ""
            print(f"  {e['id']:20} {e.get('subsystem','?'):16} {e['name']}{opt}")
        return 0

    if args.fetch:
        ids = args.only.split(",") if args.only else []
        cmd = [sys.executable, str(ROOT / "tools" / "fetch_etalons.py")] + ids
        run_timed(cmd, 600, cwd=ROOT, check=False)

    # strip() : « --only "a, b" » traitait « b » (avec son espace) comme un ID inconnu
    # et refusait de tourner, en DÉSIGNANT un identifiant pourtant valide.
    want = {t.strip() for t in args.only.split(",") if t.strip()} if args.only else None
    # Un ID inconnu ne doit PAS rendre « TOUS OK » sur zéro étalon exécuté : run_all.py
    # passe au palier P0 une liste d'IDs CODÉE EN DUR, donc un simple renommage dans le
    # manifeste viderait le garde-fou logique du hook pre-push sans un mot.
    if want:
        unknown = want - {e["id"] for e in entries}
        if unknown:
            print("ID inconnu(s) : " + ", ".join(sorted(unknown)))
            return 2
    ok = True
    ran = 0
    disabled = []
    runnable = []
    for entry in entries:
        if want and entry["id"] not in want:
            continue
        # Étalon DÉSACTIVÉ : il ne valide rien et le dit. Mieux vaut l'assumer ainsi
        # qu'un « optional » vert qui laisse croire à une couverture inexistante — la
        # raison est obligatoire et s'affiche à chaque exécution.
        if entry.get("disabled"):
            disabled.append((entry["id"], entry["disabled"]))
            continue
        runnable.append(entry)
    # A11 : le mode CI ne concerne QUE les étalons dont la référence EST un oracle
    # Hatari et qui bootent un disque (les autres n'ont rien à régénérer). On dit
    # explicitement ce qu'on écarte : « TOUS OK » sur 2 étalons au lieu de 7 doit se
    # voir, sinon un renommage de ref_kind viderait le job sans un mot.
    if args.oracle_check:
        keep, blocked = [], []
        for e in runnable:
            if e.get("ref_kind") != "hatari" or not e.get("disk"):
                continue                       # rien à re-dériver : hors sujet, pas un manque
            # `oracle_check: false` = référence NON RE-DÉRIVABLE par le script, pour une
            # raison écrite et obligatoire. Deux cas le 2026-09-01 (A11), tous deux réglés
            # le jour même — `nocooper` (touche tenue → hatari_oracle.sh pilote le clavier,
            # `oracle_keys`) et `spec512_bands` (Hatari ne se reproduisait pas lui-même →
            # le générateur s'ancre sur la VBL par `stop #$2300`). Aucun cas ouvert depuis ;
            # le mécanisme reste, parce qu'exclure en silence serait exactement le vert
            # creux que ce fichier combat — on nomme, avec la raison, à chaque exécution.
            if e.get("oracle_check") is False:
                why = e.get("oracle_check_note", "").strip()
                if not why:
                    print(f"  ✗ {e['id']}: oracle_check=false SANS oracle_check_note — "
                          f"une exclusion sans raison écrite est refusée", file=sys.stderr)
                    return 2
                blocked.append((e["id"], why))
                continue
            keep.append(e)
        if blocked:
            print(f"\n⊘ NON RE-DÉRIVABLES ({len(blocked)}) — référence oracle conservée, "
                  f"mais le script ne peut pas la régénérer :")
            for eid, why in blocked:
                head = why.split(" : ")[0] if " : " in why else why[:110]
                print(f"  · {eid} : {head}")
        runnable = keep
        print(f"→ --oracle-check sur {len(runnable)} étalon(s) : "
              + ", ".join(e["id"] for e in runnable))
    # A27 : les étalons sont indépendants (capture PPM propre à chaque id, disques en
    # lecture) — le mur d'horloge du palier pixel était la SOMME des durées, dominée
    # par nocooper_greetings (~50 s sur 73). En parallèle, il devient ~le max. Reste
    # SÉQUENTIEL : --oracle et --update-ref (Hatari et l'écriture de réfs partagent
    # des chemins), et les générateurs de disques — appelés AVANT le pool, une fois,
    # pour que deux étalons ne régénèrent pas le même fichier en même temps.
    jobs = args.jobs if args.jobs > 0 else min(4, os.cpu_count() or 1)
    if args.oracle or args.oracle_check or args.update_ref:
        jobs = 1
    if jobs > 1 and len(runnable) > 1:
        for entry in runnable:
            if entry.get("disk"):
                ensure_disk(entry)          # rejoué dans run_one, idempotent
        import concurrent.futures
        with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
            futs = [pool.submit(_run_one_buffered, e, args) for e in runnable]
            for fut in futs:                 # ordre de soumission = ordre du manifeste
                _eid, one_ok, text, skipped, skipped_rom, skipped_disk = fut.result()
                sys.stdout.write(text)
                ran += 1
                if not one_ok:
                    ok = False
                for x in skipped:
                    if x not in SKIPPED:
                        SKIPPED.append(x)
                for x in skipped_rom:
                    if x not in SKIPPED_ROM:
                        SKIPPED_ROM.append(x)
                for x in skipped_disk:
                    if x not in SKIPPED_DISK:
                        SKIPPED_DISK.append(x)
    else:
        for entry in runnable:
            ran += 1
            if not run_one(entry, args):
                ok = False
    if ran == 0:
        print("\nAUCUN étalon exécuté — filtre trop restrictif ?")
        return 2
    if disabled:
        print(f"\n⛔ DÉSACTIVÉS ({len(disabled)}) — ne valident RIEN :")
        for eid, why in disabled:
            print(f"  · {eid} : {why}")
    if SKIPPED:
        print(f"\n⚠ NON COMPARÉS ({len(SKIPPED)}) : " + ", ".join(SKIPPED)
              + "\n  (capture faite, mais aucune référence — ces étalons ne valident RIEN)")
    if SKIPPED_ROM:
        print(f"\n⚠ NON EXÉCUTÉS — ROM propriétaire absente ({len(SKIPPED_ROM)}) : "
              + ", ".join(SKIPPED_ROM)
              + "\n  (ces étalons ne valident RIEN dans cette installation ; les ROM Atari"
                "\n   ne sont pas redistribuables — les déposer dans roms/ pour les activer)")
    if SKIPPED_DISK:
        print(f"\n⚠ NON EXÉCUTÉS — disque optionnel absent ({len(SKIPPED_DISK)}) : "
              + ", ".join(SKIPPED_DISK)
              + "\n  (ces étalons ne valident RIEN dans cette installation)")
    print("\n" + ladder_summary(runnable))
    if not ok:
        print("\nÉCHECS — voir ci-dessus")
        return 1
    # Un étalon sauté (ROM propriétaire, disque optionnel, référence absente) ne valide
    # rien : la sortie 77 le fait remonter au bilan « NON EXÉCUTÉE(S) » de run_all.py
    # au lieu d'un 0 indiscernable d'une couverture complète.
    if SKIPPED or SKIPPED_ROM or SKIPPED_DISK:
        print("\nTOUS OK (couverture AMPUTÉE — voir ci-dessus)")
        return EXIT_SKIPPED_RUN
    print("\nTOUS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
