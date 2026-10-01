#!/usr/bin/env python3
# =============================================================================
#  run_selftests.py — Suite d'auto-tests à VERDICT SÉRIE (P1).
#
#  Chaque entrée de tools/selftests.json désigne une ROM (cartouche diagnostic
#  $FA52235F ou secteur de boot) qui écrit sur le port série RS-232 des lignes :
#      NEOST-TEST: <nom> PASS
#      NEOST-TEST: <nom> FAIL <détail>
#  On lance neost-headless, on capture le série (--serial-dump), et on vérifie que
#  CHAQUE <nom> attendu est présent avec PASS. Déterministe, sans oracle, en secondes.
#
#  Usage :
#    python3 tools/run_selftests.py            # tous
#    python3 tools/run_selftests.py --list
#    python3 tools/run_selftests.py --only diag_cart
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Suffixe .exe sous Windows : sans lui, le binaire est cherché sous un nom qui
# n'existe pas et la suite se déclare « non bâtie » — la seule plateforme livrée
# où les tests ne pouvaient pas tourner du tout.
_EXE = ".exe" if sys.platform == "win32" else ""
MANIFEST = Path(__file__).resolve().parent / "selftests.json"
HEADLESS = ROOT / "build" / ("neost-headless" + _EXE)

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

OUT_DIR = ROOT / "tests" / "out"

VERDICT_RE = re.compile(r"NEOST-TEST:[ \t]*(\S+)[ \t]+(PASS|FAIL)\b[ \t]*(.*)")

# Purge § BLOQUANT RELEASE, pas 2 (2026-08-28) : ce palier tourne dans le `fast`, donc
# il ne doit dépendre d'AUCUN fichier propriétaire — ni pour passer, ni pour échouer.
# Le défaut est désormais une ROM LIBRE, et un étalon qui nommerait quand même un TOS
# Atari se SAUTE (recensé) au lieu de rougir. Même règle et même mot que
# run_etalons.rom_is_free() : une ROM EmuTOS absente reste une CASSE du dépôt.
DEFAULT_ROM = "roms/etos192us.img"
# Auto-tests NON EXÉCUTÉS faute d'un fichier propriétaire. Recensés, jamais fondus
# dans le vert : c'est exactement le « vert creux » que ce palier doit interdire.
SKIPPED_ROM = []
# Code de sortie « sauté, recensé » (convention automake/`timeout`-like retenue par
# le projet le 2026-08-28) : 0 = tout a tourné, 1 = échec, 77 = il manque des données
# NON REDISTRIBUABLES et run_all.py doit le DIRE dans son bilan.
EXIT_SKIPPED = 77


def rom_is_free(rom: str) -> bool:
    """EmuTOS (`roms/etos*.img`) est livré avec le dépôt : son absence est une casse.
    Un TOS Atari, lui, ne peut pas être garanti présent — on saute et on le dit."""
    return Path(rom).name.startswith("etos")


def parse_verdicts(serial: str) -> dict:
    # Une ligne par verdict : on scanne ligne à ligne (splitlines gère \r\n) pour
    # éviter qu'un '.*' gourmand n'avale la ligne suivante.
    out = {}
    for line in serial.splitlines():
        m = VERDICT_RE.search(line)
        if m:
            out[m.group(1)] = (m.group(2), m.group(3).strip())
    return out


def load_manifest():
    import json
    return json.loads(MANIFEST.read_text(encoding="utf-8"))["selftests"]


def ensure_rom_asset(entry) -> bool:
    # Génère la cartouche/disque de test si un générateur est fourni et le fichier absent.
    for key, gen_key in (("cart", "cart_generate"), ("disk", "disk_generate"),
                         ("rom", "rom_generate"), ("sd1", "sd1_generate"),
                         ("sd2", "sd2_generate")):
        path = entry.get(key)
        gen = entry.get(gen_key)
        if not path:
            continue
        full = ROOT / path
        if full.exists():
            continue
        if not gen:
            print(f"  asset manquant : {path}", file=sys.stderr)
            return False
        full.parent.mkdir(parents=True, exist_ok=True)
        print(f"  [gen] {gen} → {path}")
        run_timed([sys.executable, str(ROOT / gen), str(full)], 300, cwd=ROOT, check=True)
    return True


def run_one(entry, args) -> bool:
    eid = entry["id"]
    print(f"\n=== {eid} — {entry['name']} ===")
    # Distinguer AVANT tout lancement les deux absences (cf. rom_is_free) : sans cela,
    # retirer les TOS Atari du dépôt ferait ROUGIR le palier `fast` au lieu de le
    # laisser dire ce qu'il n'a pas pu vérifier.
    rom = entry.get("rom", DEFAULT_ROM)
    # ⚠ Une ROM GÉNÉRÉE (rom_generate) n'est jamais un « TOS Atari absent » : sans cette
    # exclusion, effacer disks/etalons/fpu_testrom.img faisait passer l'auto-test FPU en
    # SKIP « copyright Atari » — un verdict vert qui n'a rien exécuté — au lieu de le
    # laisser se régénérer par ensure_rom_asset juste en dessous.
    if not (ROOT / rom).exists() and not rom_is_free(rom) and not entry.get("rom_generate"):
        print(f"  SKIP recensé : {rom} absent (copyright Atari — cf. TODO "
              "§ BLOQUANT RELEASE) — cet auto-test n'a PAS tourné")
        SKIPPED_ROM.append(eid)
        return True
    if not ensure_rom_asset(entry):
        return False

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    serial_path = OUT_DIR / f"{eid}_serial.txt"
    cmd = [str(HEADLESS), str(ROOT / entry.get("rom", DEFAULT_ROM)),
           "--machine", entry.get("machine", "st"),
           "--mem", entry.get("mem", "512k"),
           "--frames", str(entry.get("frames", 40)),
           "--serial-dump", str(serial_path)]
    if entry.get("cart"):
        cmd += ["--cart", str(ROOT / entry["cart"])]
    if entry.get("disk"):
        cmd += ["--disk", str(ROOT / entry["disk"])]
    if entry.get("fastfdc"):
        cmd.append("--fastfdc")
    if entry.get("fpu"):
        cmd.append("--fpu")
    # Extensions NeoST (UltraSatan sur le bus ACSI, NetUSBee sur le port cartouche) :
    # le programme de test leur parle comme les logiciels d'époque (cf. make_usatan_test.py).
    if entry.get("ultrasatan"):
        cmd.append("--ultrasatan")
    if entry.get("sd1"):
        cmd += ["--sd1", str(ROOT / entry["sd1"])]
    if entry.get("sd2"):
        cmd += ["--sd2", str(ROOT / entry["sd2"])]
    if entry.get("netusbee"):
        cmd.append("--netusbee")
    # Disque hôte GEMDOS : dossier RÉGÉNÉRÉ à chaque passage dans tests/out (le
    # programme y écrit et y efface un fichier — rien de committé, rien de périmé).
    if entry.get("gemdos_generate"):
        hd = OUT_DIR / f"{eid}_hd"
        shutil.rmtree(hd, ignore_errors=True)
        run_timed([sys.executable, str(ROOT / entry["gemdos_generate"]), str(hd)],
                  60, cwd=ROOT, check=True)
        cmd += ["--gemdos", str(hd)]
    print("  $", " ".join(cmd))
    # Le dump série n'est écrit qu'À LA FIN de main() côté headless : s'il reste celui
    # du run PRÉCÉDENT, un émulateur qui segfaute (ou qui sort tôt) laisse le runner
    # relire un verdict périmé et conclure au succès. On l'efface AVANT, et on lit le
    # code de retour — c'est tout l'intérêt du palier P1 : attraper une régression qui
    # fait crasher le cœur.
    try:
        serial_path.unlink()
    except FileNotFoundError:
        pass
    log_path = serial_path.with_name(serial_path.stem + "_run.log")
    with open(log_path, "wb") as log:
        rc = run_timed(cmd, 300, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT).returncode
    if rc != 0:
        print(f"  ÉCHEC : neost-headless a rendu {rc} (voir {log_path.relative_to(ROOT)})")
        return False
    if not serial_path.exists():
        print(f"  ÉCHEC : aucun dump série produit ({serial_path.relative_to(ROOT)})")
        return False

    serial = serial_path.read_text(encoding="latin-1")
    verdicts = parse_verdicts(serial)

    ok = True
    expect = entry.get("expect", [])
    for name in expect:
        if name not in verdicts:
            print(f"  MANQUANT : verdict '{name}' absent du série")
            ok = False
        elif verdicts[name][0] != "PASS":
            print(f"  FAIL : {name} → {verdicts[name][0]} {verdicts[name][1]}")
            ok = False
        else:
            print(f"  OK : {name} PASS")
    # Verdicts inattendus en FAIL = échec aussi (attrape les tests non listés).
    for name, (res, detail) in verdicts.items():
        if name not in expect and res == "FAIL":
            print(f"  FAIL (non listé) : {name} {detail}")
            ok = False
    if not verdicts:
        print("  AUCUN verdict série capturé — la ROM n'a-t-elle pas démarré ?")
        ok = False
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Auto-tests à verdict série NeoST")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", help="IDs séparés par des virgules")
    args = ap.parse_args()

    if not HEADLESS.exists():
        print(f"Build requis : cmake --build build  ({HEADLESS} absent)", file=sys.stderr)
        return 2

    entries = load_manifest()
    if args.list:
        for e in entries:
            print(f"  {e['id']:16} {e['name']}  (attend: {', '.join(e.get('expect', []))})")
        return 0

    # strip() : « --only "a, b" » traitait « b » (avec son espace) comme un ID inconnu
    # et refusait de tourner, en DÉSIGNANT un identifiant pourtant valide.
    want = {t.strip() for t in args.only.split(",") if t.strip()} if args.only else None
    # Un ID inconnu (faute de frappe, entrée renommée dans le manifeste) ne doit PAS
    # donner « TOUS OK » sur zéro test exécuté — c'est un vert parfaitement muet.
    if want:
        unknown = want - {e["id"] for e in entries}
        if unknown:
            print("ID inconnu(s) : " + ", ".join(sorted(unknown)))
            return 2
    ok = True
    ran = 0
    for entry in entries:
        if want and entry["id"] not in want:
            continue
        ran += 1
        if not run_one(entry, args):
            ok = False
    if ran == 0:
        print("\nAUCUN auto-test exécuté — filtre trop restrictif ?")
        return 2
    if not ok:
        print("\nÉCHECS — voir ci-dessus")
        return 1
    if SKIPPED_ROM:
        # « TOUS OK » alors qu'un auto-test n'a pas tourné serait un vert creux : on
        # le nomme, et le code 77 le fait remonter jusqu'au bilan de run_all.py.
        print(f"\n⚠ NON EXÉCUTÉS — ROM propriétaire absente ({len(SKIPPED_ROM)}) : "
              + ", ".join(SKIPPED_ROM))
        print("TOUS OK (couverture AMPUTÉE — voir ci-dessus)")
        return EXIT_SKIPPED
    print("\nTOUS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
