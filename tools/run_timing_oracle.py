#!/usr/bin/env python3
"""Compare les traces IRQ aux références Hatari (sans image ni ROM propriétaire).

Sans option : NeoST contre les références commises. --oracle-check : rejoue aussi
Hatari et exige sa reproductibilité. --record-reference : écrit une référence
après DEUX captures Hatari identiques et la comparaison NeoST réussie.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import trace_diff as td
from make_timing_probe import CASES, build
from timing_extended import DIV_PAIRS, STX_READ_TIME, disk_image, stx_image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/out/timing"
REFERENCE = ROOT / "tests/reference/timing_hatari.json"
ROM = ROOT / "roms/etos256fr.img"
NEOST = ROOT / "build" / ("neost-headless.exe" if sys.platform == "win32" else "neost-headless")
HATARI = ROOT / "extern/hatari/build/src/hatari"
FRAMES, WARMUP, SAMPLES = 40, 32, 256
# Les timers ont une quantification CPU de 4 cycles. Le DMA son est observé
# au refill HBL : une ligne PAL est la résolution de cette comparaison.
# La première IRQ est datée dans le même domaine Scheduler que l'armement.
LIMITS = {case: (512, 512) if case == "dma_sound" else (4, 4) for case in CASES}
# fdc_read_stx : la fin se mesure depuis la dernière détection, arrondie elle aussi
# à un tour de scrutation (24 cycles) faute de pouvoir aligner l'angle de départ.
# Le dernier bloc est observé par scrutation (24 cycles en lecture). La phase
# change avec la cadence matérielle, indépendante du stall DMA. La conversion
# SCLK STE vers BUS est maintenant appliquée aussi au délai CRC de NeoST.
LIMITS.update(blitter_hog=(4, 4), blitter_shared=(4, 4), fdc_read=(24, 24), fdc_read_stx=(24, 24),
              fdc_write=(24, 8))
SOURCES = ("src/mfp.c", "src/dmaSnd.c", "src/cycInt.c", "src/video.c",
           "src/cpu/newcpu.c", "src/m68000.c", "src/fdc.c", "src/floppies/stx.c",
           "src/blitter.c")


def sample_count(case):
    return 32 if case.startswith("fdc_") else 64 if case.startswith("blitter_") else SAMPLES


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, log, env=None):
    with log.open("w") as stream:
        subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                       stdout=stream, stderr=stream, check=True, timeout=90)


def identity():
    pin = re.search(r"^PIN=(\w+)", (ROOT / "tools/setup_hatari.sh").read_text(), re.M)[1]
    revision = subprocess.check_output(
        ["git", "-C", str(ROOT / "extern/hatari"), "rev-parse", "HEAD"], text=True).strip()
    if revision != pin:
        raise ValueError(f"Hatari non épinglé : {revision}, attendu {pin}")
    return {"commit": revision, "sources_sha256": {
        source: sha(ROOT / "extern/hatari" / source) for source in SOURCES}}


def capture(case, emulator, attempt=None, *, probe=None, machine="ste", out=None,
            frames=FRAMES, extra_env=None):
    output = OUT if out is None else out
    data, pcs = (build if probe is None else probe)(case)
    cart = output / (case + ".bin")
    cart.write_bytes(data)
    suffix = emulator if attempt is None else f"{emulator}-{attempt}"
    trace = output / f"{case}.{suffix}.trace"
    log = output / f"{case}.{suffix}.log"
    env = {**os.environ, "NEOST_TRACE_CYC": "1", "HATARI_SEED": "1"}
    if extra_env:
        env.update(extra_env)
    # Une image par sonde et par passage : fdc_write la modifie. Hatari reconnaît
    # le format à l'extension, d'où .stx pour la piste Pasti.
    disk = output / f"{case}.{suffix}.{'stx' if case.endswith('_stx') else 'st'}"
    if case.startswith("fdc_"):
        disk.write_bytes(media(case))
    if emulator == "neost":
        command = [NEOST, ROM, "--machine", machine, "--mem", "1m", "--cart", cart,
                   "--frames", frames, "--trace", trace, "--irq"]
        if case.startswith("fdc_"):
            command += ["--disk", disk]
        run(command, log, env)
    else:
        # Configuration et HOME isolés : aucune dépendance aux préférences locales.
        with tempfile.TemporaryDirectory(prefix="neost-timing-hatari-") as directory:
            config = Path(directory) / "empty.cfg"
            config.write_text("")
            env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", HOME=directory)
            command = [HATARI, "--configfile", config, "--tos", ROM, "--machine", machine,
                       "--memsize", "1", "--cartridge", cart, "--cpu-exact", "on",
                       "--compatible", "on", "--cpuclock", "8", "--monitor", "rgb",
                       "--sound", "off", "--timer-d", "off", "--fast-forward", "on",
                       "--confirm-quit", "off", "--alert-level", "fatal",
                       "--run-vbls", frames, "--trace", "cpu_disasm,cpu_video_cycles",
                       "--trace-file", trace]
            if case.startswith("fdc_"):
                protect = "off" if case == "fdc_write" else "on"
                command += ["--disk-a", disk, "--fastfdc", "off", "--protect-floppy", protect]
            run(command, log, env)
    text = log.read_text(errors="replace")
    if "Address Error" in text or "Bus Error" in text:
        raise ValueError(f"Exception pendant la sonde : {log}")
    # Hatari ne réécrit pas l'image en quittant par --run-vbls : chez lui, seul le
    # statut WD1772 contrôlé par le programme invité atteste l'écriture.
    if case == "fdc_write" and emulator == "neost":
        written = disk.read_bytes()[512:1024]  # piste 0, secteur 2
        if written != b"\xa5\x5a" * 256:
            raise ValueError(f"{disk} : secteur 2 non écrit par {emulator}")
    if case.startswith("blitter_") or case.startswith("fdc_"):
        return read_transfer_trace(trace, pcs, data, case)
    if case.startswith("cpu_"):
        return read_cpu_trace(trace, pcs, data)
    return read_trace(trace, pcs, data)


def media(case):
    return stx_image() if case.endswith("_stx") else disk_image()


def read_transfer_trace(trace, pcs, data, case):
    instructions, _ = td.parse(str(trace))
    def times(name):
        return [i.cyc for i in instructions if i.pc == pcs[name]]
    if "irq_while_busy" in pcs and times("irq_while_busy"):
        raise ValueError(f"{trace} : IRQ reçue pendant que le blitter est BUSY")
    if times("failure") or len(times("success")) == 0:
        raise ValueError(f"{trace} : transfert incorrect ou programme non terminé")
    arms, done, irq = times("arm"), times("handler"), times("irq")
    polls, count_poll = [], 0
    for instruction in instructions:
        if case.startswith("blitter_") and instruction.pc == pcs["arm"]:
            count_poll = 0
        if instruction.pc == pcs["poll"]:
            count_poll += 1
        if instruction.pc == pcs["handler"]:
            polls.append(count_poll)
            count_poll = 0
    if any(t is None for t in arms + done + irq):
        raise ValueError(f"{trace} : horodatage absent")
    if any(b <= a for seq in (arms, done, irq) for a, b in zip(seq, seq[1:])):
        raise ValueError(f"{trace} : horloge non strictement croissante")
    count = sample_count(case)
    if case.startswith("fdc_"):
        if len(arms) != 1 or len(done) != count or len(irq) != 1 or irq[0] <= done[-1]:
            raise ValueError(f"{trace} : attendu 1 commande, 32 rafales et 1 IRQ, "
                             f"reçu {len(arms)}, {len(done)}, {len(irq)}")
        result = {"disk_sha256": hashlib.sha256(media(case)).hexdigest(),
                  "cycles": [t - done[0] for t in done],
                  "polls": polls[1:],  # exclure l'attente rotationnelle initiale
                  "completion": irq[0] - done[-1]}
    else:
        if len(arms) != 72 or len(done) != 72 or len(irq) != 72:
            raise ValueError(f"{trace} : attendu 72 démarrages/fins/IRQ, "
                             f"reçu {len(arms)}, {len(done)}, {len(irq)}")
        if not all(a < i <= d for a, i, d in zip(arms, irq, done)):
            raise ValueError(f"{trace} : ordre démarrage/IRQ/fin incorrect")
        result = {"durations": [d-a for a, d in zip(arms, done)][8:],
                  "polls": polls[8:],
                  "irq_durations": [i-a for a, i in zip(arms, irq)][8:]}
    if any(p <= 0 for p in result["polls"]):
        raise ValueError(f"{trace} : boucle CPU absente pendant les transferts")
    return {"probe_sha256": hashlib.sha256(data).hexdigest(), **result}


def read_cpu_trace(trace, pcs, data):
    """Durée de chaque itération entre deux passages sur un même repère."""
    instructions, _ = td.parse(str(trace))
    if not any(i.pc == pcs["success"] for i in instructions):
        raise ValueError(f"{trace} : programme non terminé")
    result = {"probe_sha256": hashlib.sha256(data).hexdigest()}
    for mark in sorted(name for name in pcs if name.startswith("mark_")):
        times = [i.cyc for i in instructions if i.pc == pcs[mark]]
        if len(times) != len(DIV_PAIRS) or any(t is None for t in times):
            raise ValueError(f"{trace} : {mark} vu {len(times)} fois, attendu {len(DIV_PAIRS)}")
        result[mark] = [b - a for a, b in zip(times, times[1:])]
    return result


def read_trace(trace, pcs, data):
    instructions, _ = td.parse(str(trace))
    timestamps = [i.cyc for i in instructions if i.pc == pcs["handler"]]
    arms = [i.cyc for i in instructions if i.pc == pcs["arm"]]
    if len(arms) != 1 or len(timestamps) < WARMUP + SAMPLES:
        raise ValueError(f"{trace} : sonde incomplète ou redémarrée "
                         f"({len(arms)} armements, {len(timestamps)} IRQ)")
    if any(t is None for t in timestamps + arms):
        raise ValueError(f"{trace} : horodatages absents")
    if any(b <= a for a, b in zip(timestamps, timestamps[1:])):
        raise ValueError(f"{trace} : horloge non strictement croissante")
    times = timestamps[WARMUP:WARMUP + SAMPLES]
    return {"probe_sha256": hashlib.sha256(data).hexdigest(),
            "first_irq": timestamps[0] - arms[0],
            "cycles": [t - times[0] for t in times]}


# Écarts acceptés face à Hatari : le partage du bus de NeoST suit la netlist ST-4082 et
# stBlitter.sv, Hatari annonce 4 cycles en précisant « my guess » (docs/STBLITTER_BENCH.md).
# Décalages signés, la borne s'applique autour d'eux : un autre écart reste à juger.
ACCEPTED_BLITTER = {
    "blitter_shared_large": {"durations": -20, "irq_durations": -6, "polls": -1},
    "blitter_shared_ram": {"durations": 8, "irq_durations": 8},
}


# Première IRQ après une VBL sur STE : la broche est échantillonnée, comme le RTL, là où Hatari commit au
# dispatch (docs/STCPU_BENCH.md). Décalage signé, la borne s'applique autour de lui.
ACCEPTED_FIRST_IRQ = {"timer_b_event": -14}
ACCEPTED_COMPLETION = {"fdc_write": 184}


def compare(case, actual, expected, strict=False):
    if actual["probe_sha256"] != expected["probe_sha256"]:
        raise ValueError(f"{case} : cartouche modifiée, référence à revoir")
    if case.startswith("blitter_"):
        ok = True
        summaries = []
        for field in ("durations", "irq_durations", "polls"):
            a, b = actual[field], expected[field]
            if len(a) != sample_count(case) or len(b) != sample_count(case):
                raise ValueError(f"{case} : mesure incomplète")
            delta = [x-y for x, y in zip(a, b)]
            limit = 0 if field == "polls" else LIMITS[case][0]
            offset = ACCEPTED_BLITTER.get(case, {}).get(field, 0)
            ok = max(abs(d - offset) for d in delta) <= limit and ok
            summaries.append(f"{field} Δ={min(delta):+d}..{max(delta):+d} "
                             f"(borne ±{limit})")
        label = "OK" if ok else "FAIL"
        print(f"  {label} {case}: {sample_count(case)} blits, " + "; ".join(summaries))
        return ok
    if case.startswith("cpu_"):
        ok = True
        summaries = []
        for mark in sorted(name for name in expected if name.startswith("mark_")):
            delta = [x - y for x, y in zip(actual[mark], expected[mark])]
            if len(actual[mark]) != len(expected[mark]):
                raise ValueError(f"{case} : mesure incomplète")
            ok = not any(delta) and ok
            summaries.append(f"{mark[5:]} Δ={min(delta):+d}..{max(delta):+d}")
        print(f"  {'OK' if ok else 'FAIL'} {case}: {len(DIV_PAIRS) - 1} intervalles exacts, "
              + "; ".join(summaries))
        return ok
    a, b = actual["cycles"], expected["cycles"]
    if case.startswith("fdc_"):
        if actual["disk_sha256"] != expected["disk_sha256"]:
            raise ValueError("Disquette synthétique différente de la référence")
        if len(a) != 32 or len(b) != 32 or a[0] != 0 or b[0] != 0:
            raise ValueError("FDC : 32 rafales requises")
        # [B] WD177X-00 p. 1-1,1-5 : un octet MFM toutes les 256 horloges,
        # sans pause pendant le DMA RAM. Hatari ajoute 32 cycles par bloc ;
        # sa dérive n'est donc PLUS la référence de cadence. STX : ReadTime
        # de la fixture, exprimé en µs à 8 MHz (arrondi par octet conservé).
        period = STX_READ_TIME * 8 // 32 if case.endswith("_stx") else 16 * 256
        phase = max(abs(x - i * period) for i, x in enumerate(a))
        # [B] WD177X-00 p. 1-12, 1-13 : INTRQ 24 µs après le dernier CRC d'un Write Sector. Hatari finit ~1 µs après.
        tail = abs(actual["completion"] - expected["completion"] - ACCEPTED_COMPLETION.get(case, 0) + 32)
        if len(actual["polls"]) != 31 or len(expected["polls"]) != 31:
            raise ValueError("FDC : 31 intervalles de travail CPU requis")
        # Comparer le temps HORS scrutation : la période disque raccourcie
        # enlève du travail CPU, mais le coût du DMA doit rester. Les boucles
        # font 24 cycles (lecture RAM) / 22 (adresse DMA en écriture). Retirer
        # le stall CPU changerait ce résidu de 32 cycles et ferait échouer le test.
        loop = 22 if case == "fdc_write" else 24
        residual_a = [a[i+1] - a[i] - loop * p for i, p in enumerate(actual["polls"])]
        residual_b = [b[i+1] - b[i] - loop * p for i, p in enumerate(expected["polls"])]
        drift = max(abs(x-y) for x, y in zip(residual_a, residual_b))
        work = drift == 0
        ok = phase <= LIMITS[case][0] and tail <= LIMITS[case][1] and work
        print(f"  {'OK' if ok else 'FAIL'} {case}: 32 rafales, "
              f"phase vs cadence {period} max={phase}/24 cycles, IRQ après dernière rafale Δ={tail}/{LIMITS[case][1]}, "
              f"coût hors scrutation Δ={drift}/0 cycle")
        return ok
    if len(a) != SAMPLES or len(b) != SAMPLES or a[0] != 0 or b[0] != 0:
        raise ValueError(f"{case} : référence/mesure incomplète")
    phase = max(abs(x - y) for x, y in zip(a, b))
    first = abs(actual["first_irq"] - expected["first_irq"] - ACCEPTED_FIRST_IRQ.get(case, 0))
    phase_limit, first_limit = LIMITS[case]
    period = td.histo([y - x for x, y in zip(a, a[1:])])[0][0]
    wanted = td.histo([y - x for x, y in zip(b, b[1:])])[0][0]
    # Le mode seul masquerait une IRQ perdue ou une faible dérive répétée.
    ok = phase <= phase_limit and first <= first_limit and period == wanted
    print(f"  {'OK' if ok else 'FAIL'} {case:12} {SAMPLES} IRQ : "
          f"phase max={phase}/{phase_limit}, "
          f"première IRQ Δ={first}/{first_limit} cycles, période={period}/{wanted}, "
          f"dérive finale={a[-1]-b[-1]:+d}")
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--oracle-check", action="store_true")
    mode.add_argument("--record-reference", action="store_true")
    parser.add_argument("--strict", action="store_true",
                        help="comparaison stricte (désormais appliquée aussi sans ce drapeau)")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    live = args.oracle_check or args.record_reference
    oracle = identity() if live else None
    if args.record_reference:
        reference = {"schema": 2, "oracle": oracle, "rom_sha256": sha(ROM),
                     "frames": FRAMES, "warmup": WARMUP, "samples": SAMPLES, "cases": {}}
    else:
        reference = json.loads(REFERENCE.read_text())
        if (reference["schema"], reference["frames"], reference["warmup"], reference["samples"]) != (2, FRAMES, WARMUP, SAMPLES):
            raise ValueError("Protocole de référence incompatible")
        if reference["rom_sha256"] != sha(ROM) or set(reference["cases"]) != set(CASES):
            raise ValueError("ROM ou liste des sondes différente de la référence")
        if live and reference["oracle"] != oracle:
            raise ValueError("Source Hatari différente de la référence (pin ou empreintes)")
    print(f"TIMING : {len(CASES)} sondes, STE PAL 8 MHz, Hatari {reference['oracle']['commit'][:12]}", flush=True)
    ok = True
    for case in CASES:
        if live:
            first = capture(case, "hatari", 1)
            second = capture(case, "hatari", 2)
            if first != second:
                raise ValueError(f"{case} : Hatari ne reproduit pas ses propres horodatages")
            if args.record_reference:
                reference["cases"][case] = first
            elif first != reference["cases"][case]:
                raise ValueError(f"{case} : trace Hatari différente de la référence commise")
        ok = compare(case, capture(case, "neost"), reference["cases"][case], args.strict) and ok
    if args.record_reference and ok:
        REFERENCE.write_text(json.dumps(reference, indent=2) + "\n")
        print(f"Référence Hatari écrite : {REFERENCE.relative_to(ROOT)}")
    print("TIMING ORACLE OK" if ok else "TIMING ORACLE FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"TIMING ERROR: {error} (diagnostic : {OUT})", file=sys.stderr)
        sys.exit(2)
