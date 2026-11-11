#!/usr/bin/env python3
# =============================================================================
#  run_perfbench.py — Barrière de DÉBIT (chantier A6).
#
#  POURQUOI. Le projet avait un `cycle-bench` qui garde le MODÈLE de cycle, mais
#  rien qui garde le TEMPS MUR. Constaté le 2026-08-25 : le chantier BL4 a ajouté
#  un `syncTo` par accès bus du blitter — de l'ordre de 370 000 appels de plus sur
#  6000 trames — et personne ne l'a mesuré, ni la CI vue. Or le mode borne sur
#  Raspberry Pi est une cible déclarée du projet : un émulateur temps réel doit
#  avoir une barrière de débit, pas seulement de justesse.
#
#  ⚠ LE PIÈGE DE CE GENRE DE BANC, et comment il est contourné. Un seuil ABSOLU en
#  trames/s est ingérable : un runner de CI est 2 à 3 fois plus lent qu'un poste de
#  travail, et sa charge varie d'un run à l'autre. Un seuil serré serait donc
#  instable (donc désarmé au bout de trois faux rouges), et un seuil lâche ne
#  verrait rien. On garde donc des RATIOS entre charges mesurées DANS LE MÊME RUN :
#  le boot EmuTOS nu sert d'étalon de vitesse machine, et les charges lourdes sont
#  exprimées par rapport à lui. Un ratio est INDÉPENDANT de la vitesse de la
#  machine — il ne bouge que si le coût RELATIF d'un chemin change, ce qui est
#  précisément ce qu'on veut attraper.
#
#  Les débits absolus sont quand même AFFICHÉS à chaque run : ils ne gardent rien,
#  mais ils rendent une dérive visible à l'œil sur la durée.
#
#  MESURE DE RÉFÉRENCE (2026-08-26, poste macOS Silicon) — coût réel du chantier
#  blitter, la question laissée ouverte à l'époque :
#    boot EmuTOS sans blitter : 1733 → 1707 tr/s   (+1,5 % de temps)
#    blitter non-hog STE      : 1504 → 1523 tr/s   (−1,3 % de temps)
#    poll MFP Timer A         : 1319 → 1317 tr/s   (+0,2 % de temps)
#  Autrement dit : BL3+BL4 ne coûtent RIEN de mesurable. `Scheduler::syncTo` est
#  O(1) quand rien n'est dû (cache `nextDue_`), donc les appels supplémentaires
#  sont gratuits. L'inquiétude était légitime ; la mesure la lève.
#
#  LE MODE `--budget` (chantier A12, 2026-09-01). Les ratios ci-dessus sont
#  machine-INDÉPENDANTS à dessein : c'est ce qui en fait une bonne barrière de CI,
#  et c'est aussi ce qui les rend incapables de répondre à la seule question qu'on
#  pose d'une CIBLE DE LIVRAISON — « cette machine tient-elle le temps réel ? ».
#  `--budget` répond par un chiffre ABSOLU : le facteur temps réel, soit les
#  trames/s mesurées divisées par le balayage que la machine émulée annonce
#  elle-même (50 Hz PAL / 60 Hz NTSC — il dépend de la ROM, cf. CLAUDE.md, donc il
#  est LU sur la sortie et jamais supposé). ×1,0 = la cible émule tout juste à la
#  vitesse du vrai ST.
#  ⚠ Ce mode ne doit JAMAIS être câblé dans un palier de test : un seuil absolu sur
#  un runner de CI est précisément le piège décrit plus haut. Il s'exécute À LA MAIN
#  sur une cible, et son résultat se CONSIGNE (`docs/HW_VALIDATION.md`).
#
#  Usage : run_perfbench.py [--update-ratios | --budget [--json]]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import json
import os
import platform
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Suffixe .exe sous Windows : sans lui, le binaire est cherché sous un nom qui
# n'existe pas et la suite se déclare « non bâtie » — la seule plateforme livrée
# où les tests ne pouvaient pas tourner du tout.
_EXE = ".exe" if sys.platform == "win32" else ""
# Le binaire sous test est surchargeable (A12) : une passe de validation porte sur
# le binaire LIVRÉ — celui du paquet, universal2 et signé — et pas sur le build de
# l'arbre de dev, qui n'est ni le même binaire ni la même chaîne de compilation.
# Les DONNÉES, elles, restent celles du dépôt : c'est le binaire qu'on compare, pas
# le contenu du paquet.
HEADLESS = Path(os.environ.get("NEOST_HEADLESS") or
                (ROOT / "build" / ("neost-headless" + _EXE)))
RATIOS = Path(__file__).resolve().parent / "perfbench_ratios.json"

FRAMES = 600
REPS = 3          # on garde le MEILLEUR temps : le bruit d'ordonnancement ne fait
                  # que RALENTIR, jamais accélérer — le min est donc l'estimateur
                  # le moins bruité du coût réel.

# La première charge est l'ÉTALON DE VITESSE MACHINE (ratio 1 par construction).
LOADS = [
    # La disquette est NOMMÉE : les ratios de référence ont été calibrés quand le headless
    # montait disks/diskA.st d'office. Sans elle, EmuTOS boote ~20 % plus lentement
    # (1,3 s contre 1,1 s pour 600 trames) et mfp/boot sortait de la tolérance (CI du
    # 2026-11-11, après « lecteur A vide sans --disk »).
    ("boot", "boot EmuTOS nu (aucun blitter, disks/diskA.st)",
     ["roms/etos192fr.img", "--machine", "st", "--disk", "disks/diskA.st", "--disk-ro"]),
    ("blitter", "blitter non-hog STE (~400 tranches/run)",
     ["roms/etos256us.img", "--machine", "ste", "--mem", "512k",
      "--disk", "disks/etalons/blitter_timer.st"]),
    ("mfp", "poll IPRA du MFP pendant Timer A",
     ["roms/etos192fr.img", "--machine", "st", "--mem", "512k",
      "--disk", "disks/etalons/mfp_poll.st"]),
]

# Tolérance sur les ratios. Large À DESSEIN : ce banc n'est pas là pour détecter
# 5 % de mieux ou de moins — il est là pour attraper un chemin qui devient DEUX
# FOIS plus cher sans que personne ne le remarque. Un seuil serré serait désarmé
# à son troisième faux rouge, et ne garderait alors plus rien du tout.
TOLERANCE = 0.25

# Bandes du mode `--budget`. Ce ne sont pas des seuils de test — rien ne rougit
# ici — mais la lecture d'un chiffre, écrite une fois pour qu'elle soit la même
# d'une cible à l'autre.
#   < 1,0 : la cible n'émule même pas à la vitesse du vrai ST, SANS interface ni
#           son. Aucun réglage ne rattrape ça.
#   < 2,0 : le headless tient, mais il ne porte NI le rendu, NI l'audio, NI le
#           throttling thermique d'un boîtier passif. Une marge inférieure à ×2
#           doit être confirmée en GUI sur la cible avant d'être annoncée.
BUDGET_TENU = 1.0
BUDGET_CONFORT = 2.0

# Le balayage est annoncé par la machine émulée (« video: 416x276 @ 50 Hz ») et
# dépend de la ROM : le LIRE, jamais le supposer (cf. CLAUDE.md).
_HZ_RE = re.compile(r"video:.*@\s*(\d+)\s*Hz")

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



def measure_load(args) -> tuple:
    """(trames/s au meilleur des REPS, Hz de balayage annoncé par la machine).

    Le Hz sort de la sortie de l'émulateur lui-même — c'est la seule source qui
    ne mente pas quand la ROM change (us → 60 Hz NTSC, fr/uk → 50 Hz PAL).
    """
    ts, hz = [], 0
    for _ in range(REPS):
        t0 = time.perf_counter()
        pr = run_timed([str(HEADLESS)] + args + ["--frames", str(FRAMES)],
                       300, cwd=ROOT, capture_output=True)
        ts.append(time.perf_counter() - t0)
        out = (getattr(pr, "stdout", None) or b"") + (getattr(pr, "stderr", None) or b"")
        m = _HZ_RE.search(out.decode("utf-8", "replace"))
        if m:
            hz = int(m.group(1))
    return FRAMES / min(ts), hz


def measure(args) -> float:
    return measure_load(args)[0]


def measure_all() -> dict:
    """Un passage complet du banc → {clé: ratio vs boot}."""
    print(f"Banc de débit ({FRAMES} trames, meilleur de {REPS}) :")
    rates = {}
    for key, label, args in LOADS:
        rates[key] = measure(args)
        print(f"  {label:44s} {rates[key]:7.0f} tr/s")
    base = rates[LOADS[0][0]]
    return {k: v / base for k, v in rates.items()}


def compare(measured: dict, ref: dict) -> list:
    """Affiche les ratios et renvoie la liste des clés hors tolérance."""
    print(f"\nRatios (indépendants de la vitesse machine, tolérance ±{TOLERANCE:.0%}) :")
    bad = []
    for k, v in measured.items():
        if k not in ref:
            print(f"  ⚠ {k}/boot = {v:.3f} — pas de référence (ajouter via --update-ratios)")
            continue
        drift = (v - ref[k]) / ref[k]
        mark = "OK  " if abs(drift) <= TOLERANCE else "✗   "
        print(f"  {mark}{k}/boot = {v:.3f} (réf {ref[k]:.3f}, {drift:+.1%})")
        if abs(drift) > TOLERANCE:
            bad.append(k)
    return bad


def _cpu_name() -> str:
    """Nom du processeur, par la voie qui existe sur CHAQUE cible livrée."""
    try:
        if sys.platform == "darwin":
            return subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                                  capture_output=True, text=True,
                                  timeout=10).stdout.strip()
        if sys.platform.startswith("linux"):
            # Le Raspberry Pi ne met RIEN d'utile dans « model name » de
            # /proc/cpuinfo (souvent absent en ARM) : son identité est dans
            # l'arbre de périphériques. C'est la cible qui motive A12, donc
            # c'est elle qu'on interroge en premier.
            dt = Path("/proc/device-tree/model")
            if dt.exists():
                return dt.read_text(errors="replace").strip("\x00 \n")
            for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
                for key in ("model name", "Model"):
                    if line.startswith(key):
                        return line.split(":", 1)[1].strip()
    except Exception:
        pass
    return platform.processor() or platform.machine()


def _ram_bytes() -> int:
    try:
        if sys.platform == "darwin":
            return int(subprocess.run(["sysctl", "-n", "hw.memsize"],
                                      capture_output=True, text=True,
                                      timeout=10).stdout.strip())
        if sys.platform.startswith("linux"):
            for line in Path("/proc/meminfo").read_text().splitlines():
                if line.startswith("MemTotal:"):
                    return int(line.split()[1]) * 1024
    except Exception:
        pass
    return 0


def host_info() -> dict:
    """La CONFIG de la cible. Une mesure sans elle n'est pas une mesure —
    leçon du 2026-08-25, la même qui a fait rejeter les durées relevées sous
    charge non décrite."""
    ver = ""
    try:
        ver = subprocess.run([str(HEADLESS), "--version"], capture_output=True,
                             text=True, timeout=30).stdout.strip().splitlines()[0]
    except Exception:
        pass
    build_type = ""
    cache = ROOT / "build" / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(errors="replace").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:"):
                build_type = line.split("=", 1)[1].strip()
    commit = ""
    try:
        commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                                capture_output=True, text=True,
                                timeout=10).stdout.strip()
    except Exception:
        pass
    ram = _ram_bytes()
    # La CHARGE fait partie de la config. Le projet s'est déjà fait prendre :
    # les durées de migration du 2026-08-28, relevées sous charge non décrite,
    # sortaient à 46→90 s là où le poste au repos donnait 46→50 s. Un facteur
    # temps réel mesuré sur une machine occupée est un plancher, pas la mesure —
    # autant que le lecteur le sache sans avoir à le demander.
    try:
        load1 = round(os.getloadavg()[0], 2)   # absent sous Windows
    except (OSError, AttributeError):
        load1 = None
    return {
        "date": time.strftime("%Y-%m-%d %H:%M:%S%z"),
        "charge_1min": load1,
        "os": f"{platform.system()} {platform.release()}",
        "arch": platform.machine(),
        "cpu": _cpu_name(),
        "cores": os.cpu_count() or 0,
        "ram_gib": round(ram / (1 << 30), 1) if ram else 0,
        "binaire": ver,
        "build_type": build_type,
        "commit": commit,
    }


def budget(as_json: bool) -> int:
    """Passe de validation d'une CIBLE : facteur temps réel absolu par charge."""
    info = host_info()
    if not as_json:
        print("Cible :")
        for k, v in info.items():
            print(f"  {k:12s} {v}")
        print(f"\nBudget temps réel ({FRAMES} trames, meilleur de {REPS}) :")

    rows, worst = [], None
    for key, label, args in LOADS:
        fps, hz = measure_load(args)
        if not hz:
            print(f"  ✗ {key} : aucun « @ NN Hz » sur la sortie — cible non mesurable.",
                  file=sys.stderr)
            return 2
        factor = fps / hz
        rows.append({"charge": key, "libelle": label, "trames_s": round(fps, 1),
                     "balayage_hz": hz, "facteur_temps_reel": round(factor, 2)})
        worst = factor if worst is None else min(worst, factor)
        if not as_json:
            mark = "OK  " if factor >= BUDGET_CONFORT else (
                   "◐   " if factor >= BUDGET_TENU else "✗   ")
            print(f"  {mark}{label:44s} {fps:7.0f} tr/s @ {hz} Hz = "
                  f"×{factor:.2f} temps réel")

    verdict = ("confort" if worst >= BUDGET_CONFORT else
               "tenu-sans-marge" if worst >= BUDGET_TENU else "insuffisant")
    if as_json:
        print(json.dumps({"cible": info, "charges": rows,
                          "pire_facteur": round(worst, 2), "verdict": verdict},
                         indent=2, ensure_ascii=False))
        return 0

    print(f"\nPire charge : ×{worst:.2f} temps réel → {verdict.upper()}")
    if verdict == "confort":
        print("La cible porte le headless avec marge. L'interface et l'audio "
              "restent à confirmer en GUI — ce banc ne les mesure pas.")
    elif verdict == "tenu-sans-marge":
        print("Le headless tient, mais SANS marge pour le rendu, l'audio ni le "
              "throttling. Ne pas annoncer « temps réel » avant une passe GUI "
              "sur la cible.")
    else:
        print("La cible n'atteint pas la vitesse du vrai ST, interface exclue.")
    print("\n⚠ Chiffre dépendant de la machine ET de sa charge : le consigner "
          "avec sa config dans docs/HW_VALIDATION.md, jamais le comparer à une "
          "autre cible sans les deux configs sous les yeux.")
    return 0


def main() -> int:
    if not HEADLESS.exists():
        print(f"neost-headless absent ({HEADLESS}) — bâtir d'abord.")
        return 1
    if "--budget" in sys.argv:
        return budget("--json" in sys.argv)
    update = "--update-ratios" in sys.argv

    measured = measure_all()

    if update or not RATIOS.exists():
        RATIOS.write_text(json.dumps(measured, indent=2) + "\n", encoding="utf-8")
        print(f"\nRatios de référence écrits dans {RATIOS.name} :")
        for k, v in measured.items():
            print(f"  {k}/boot = {v:.3f}")
        return 0

    ref = json.loads(RATIOS.read_text(encoding="utf-8"))
    bad = compare(measured, ref)

    # SECONDE PASSE avant de rougir (2026-08-28). Le message d'échec disait déjà
    # « relancer avant de conclure » — il le disait à l'humain, et la CI, elle,
    # rougissait. Observé ce jour-là : un `--tier full` a rendu blitter/boot à
    # −32,0 % (donc ÉCHEC) alors que deux passages isolés du même binaire, aussitôt
    # après, donnaient −2,4 % et −6,8 % : la machine bâtissait un oracle Hatari en
    # parallèle. Les charges sont mesurées SÉQUENTIELLEMENT — une bouffée de charge
    # qui couvre une charge mais pas l'étalon de vitesse fausse le ratio, et le
    # « meilleur de REPS » n'y peut rien si la bouffée dure plus que les REPS.
    # La seconde passe ne coûte QUE dans le cas qui allait échouer, et un vrai
    # surcoût de chemin la franchit deux fois. Un garde-fou qui crie au loup finit
    # désarmé — c'est la leçon du 2026-08-25 sur les grandeurs dépendantes de la
    # charge, appliquée à l'outil qui la mesure.
    if bad:
        print(f"\n⚠ hors tolérance au 1ᵉʳ passage : {', '.join(bad)} — SECONDE PASSE "
              "(une bouffée de charge fausse un ratio ; un vrai surcoût, non).")
        measured2 = measure_all()
        bad2 = compare(measured2, ref)
        confirmed = [k for k in bad if k in bad2]
        if not confirmed:
            print("\nDÉBIT OK (le 1ᵉʳ passage était bruité — écart NON reproduit)")
            return 0
        print(f"\nÉCHEC — débit relatif hors tolérance aux DEUX passages : "
              f"{', '.join(confirmed)}.")
        print("Un chemin est devenu nettement plus cher. Si le changement est VOULU "
              "et mesuré, réétalonner avec --update-ratios.")
        return 1
    print("\nDÉBIT OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
