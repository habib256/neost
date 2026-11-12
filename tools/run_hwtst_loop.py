#!/usr/bin/env python3
"""Mega STE : vitesse d'une boucle registre à 8 MHz, 16 MHz sans cache et avec cache,
contre les relevés HWTST faits sur de vraies machines (échelon hardware, relevé de tiers).

[A] HWTST (ljbk), fil atari-forum t=24862 « Test your CPU speed (and Sound DMA speed) »,
    relu par web.archive.org (capture du 2025-01-23). Le programme compte, entre deux VBL,
    les tours d'une boucle « registres, branchement conditionnel et lecture du code » qui
    prend 24 cycles sur un ST ; « VBL CYCLES » vaut tours × 24. Relevés publiés :
      - bullis1, Mega STE NTSC (13 sept. 2013) : 133 606 à 8 MHz, 133 616 à 16 MHz sans
        cache, 291 478 et 291 476 à 16 MHz avec cache (cache réglé par le panneau du TOS) ;
      - AtariZoll, Mega STE PAL, TOS 2.06 (5 nov. 2013) : 160 268 à 8 MHz, 349 604 à
        16 MHz (cache non précisé ; rapport identique au cas « cache » de bullis1).
    Précision annoncée par l'auteur : ±2 cycles sur VBL CYCLES.
    Rapports mesurés : sans cache 133 606 / 133 616 = 0,9999 ; avec cache
    291 478 / 133 606 = 2,1816 et 349 604 / 160 268 = 2,1814.

[F] La boucle exacte d'HWTST n'est pas publiée (pièce jointe réservée aux membres). Celle-ci
    est une reconstruction : 22 cycles nominaux (addq.l + nop + dbf pris), arrondis à 24 par
    l'alignement du bus ST, et 22 cycles CPU (11 cycles bus) quand le cache sert tout à
    16 MHz — 24/11 = 2,1818, le rapport mesuré. Une boucle de 24 cycles nominaux (addq.l +
    cmp.l + bne.s) donnerait 2,0 : elle est datée aussi, pour montrer que la forme compte.
    Tranche : le source d'HWTST004.PRG.

Horloge CPU contre le quartz du MFP (option --quartz, TODO « Horloges NTSC et quartz STF »).
HWTST mesure aussi la fréquence du CPU contre le quartz du MFP (2,4576 MHz). La sonde ci-dessous
fait la même chose sur NeoST : Timer A en délai /200, donnée 256 (51 200 cycles MFP), le CPU en
STOP entre deux IRQ ; l'écart entre entrées d'IRQ donne les cycles CPU par 51 200 cycles MFP, donc
la fréquence CPU = cycles × 48. Elle doit RETROUVER le quartz choisi (à l'arrondi près de
l'entrée d'IRQ près : sa date après STOP varie de quelques cycles, soit ±10 Hz sur 39 périodes).
[A] Relevés HWTST (même fil) : STE et Mega STE PAL 8 020 7xx-8 021 2xx Hz ; Mega ST4, 520 STF
    FR et 1040 STF 8 009 7xx-8 010 1xx Hz ; Mega STE NTSC 8 054 0xx Hz. Le défaut PAL doit tomber
    dans la plage PAL ; les deux autres quartz sont affichés en face de leur relevé, sans verdict
    de matériel (le relevé est fait contre un quartz MFP lui-même à ±100 ppm près).

Sans Hatari : le verdict ne dépend que de NeoST et des relevés ci-dessus.
"""
import os
import sys
from pathlib import Path

import run_timing_oracle as timing
from timing_extended import Assembler

OUT = timing.ROOT / "tests/out/hwtst_loop"
N = 2000                      # tours par mesure
SCU = (("8MHz", 0), ("16_off", 2), ("16_cache", 3))   # $FF8E21 : bit1 16 MHz, bit0 cache
LOOPS = {
    # addq.l #1,d0 ; nop ; dbf d1,boucle — 8 + 4 + 10 = 22 cycles nominaux.
    "addq_nop_dbf": ([0x5280, 0x4E71, 0x51C9, 0xFFFA], N - 1),
    # addq.l #1,d0 ; cmp.l d1,d0 ; bne.s boucle — 8 + 6 + 10 = 24 cycles nominaux.
    "addq_cmp_bne": ([0x5280, 0xB081, 0x66FA], N),
}
# Attendus : rapports mesurés (ci-dessus), tolérance couvrant ±2 cycles sur ~133 000.
MEASURED = {"16_off": 0.9999, "16_cache": 2.1815}
TOLERANCE = {"16_off": 0.0005, "16_cache": 0.0010}
LOOP_BASE = 0x20000


def build(words, count, scu):
    """Cartouche : règle le SCU, recopie la boucle en RAM (le code d'HWTST tourne en RAM),
    l'exécute N fois, puis revient en cartouche."""
    a = Assembler()
    a.w(0x46fc, 0x2700, 0x2e7c); a.long(0x10000)
    a.byte(0x8e21, scu)
    a.w(0x207c); a.long(LOOP_BASE)                 # movea.l #LOOP_BASE,a0
    for word in words:
        a.w(0x30fc, word)                          # move.w #mot,(a0)+
    a.w(0x30fc, 0x4ef9, 0x30fc, 0x00fa, 0x30fc)    # jmp $00FAxxxx (retour en cartouche)
    patch = len(a.code)
    a.w(0)
    a.w(0x7000)                                    # moveq #0,d0
    a.w(0x223c); a.long(count)                     # move.l #count,d1
    a.w(0x4ef9); a.long(LOOP_BASE)                 # jmp LOOP_BASE
    a.label("back"); a.w(0x4e71)
    a.label("done"); a.w(0x4e72, 0x2700); a.branch(0x6000, "done")
    data, labels = a.finish()
    data = bytearray(data)
    data[patch:patch + 2] = (labels["back"] & 0xffff).to_bytes(2, "big")
    return bytes(data), labels["back"]


def measure(name, words, count, label, scu):
    data, back = build(words, count, scu)
    cart = OUT / f"{name}_{label}.bin"
    cart.write_bytes(data)
    trace = OUT / f"{name}_{label}.trace"
    timing.run([timing.NEOST, timing.ROM, "--machine", "megaste", "--mem", "1m", "--cart", cart,
                "--frames", "3", "--trace", trace], OUT / f"{name}_{label}.log",
               {**os.environ, "NEOST_TRACE_CYC": "1"})
    instructions, _ = timing.td.parse(str(trace))
    start = [i.cyc for i in instructions if i.pc == LOOP_BASE]
    end = [i.cyc for i in instructions if i.pc == back]
    if len(start) != N or len(end) != 1 or None in start + end:
        raise ValueError(f"{name} {label} : exécution incomplète ({trace})")
    return (end[0] - start[0]) / N


# Quartz → fréquence CPU attendue (core/Pacing.hpp, quartzInfo) et relevé HWTST en regard.
QUARTZ = {"pal": (8021248, "STE/Mega STE PAL 8 020 7xx-8 021 2xx"),
          "32042": (8010600, "Mega ST4/STF 8 009 7xx-8 010 1xx"),
          "32216": (8053976, "Mega STE NTSC 8 054 0xx")}
PAL_RANGE = (8020700, 8021300)        # [A] HWTST, STE et Mega STE PAL
CLOCK_PERIODS = 40


def build_clock():
    """Cartouche : Timer A délai /200, donnée 0 (256), IRQ sur niveau 6 ; STOP entre les IRQ."""
    a = Assembler()
    a.w(0x46fc, 0x2700, 0x2e7c); a.long(0x10000)
    for address in (0xfa07, 0xfa09, 0xfa13, 0xfa15, 0xfa19):
        a.byte(address, 0)
    a.byte(0xfa17, 0x40)                           # VR : base $40, fin d'IRQ automatique
    a.vector(0x134, "isr")                         # vecteur $4D : Timer A
    a.byte(0xfa1f, 0)                              # TADR = 0 → 256
    a.byte(0xfa07, 0x20); a.byte(0xfa13, 0x20)     # IERA/IMRA bit 5 : Timer A
    a.w(0x7200 | (CLOCK_PERIODS - 1))              # moveq #périodes-1,d1
    a.byte(0xfa19, 7)                              # TACR = 7 : délai /200
    a.label("wait"); a.w(0x4e72, 0x2500)           # stop #$2500 : seul le MFP (niveau 6) passe
    a.branch(0x51c9, "wait")                       # dbf d1,wait
    a.label("done"); a.w(0x4e72, 0x2700); a.branch(0x6000, "done")
    a.label("isr"); a.w(0x4e73)                    # rte
    return a.finish()


def measure_clock(quartz):
    data, labels = build_clock()
    cart = OUT / "clock.bin"
    cart.write_bytes(data)
    trace = OUT / f"clock_{quartz}.trace"
    timing.run([timing.NEOST, timing.ROM, "--machine", "ste", "--mem", "1m", "--cart", cart,
                "--quartz", quartz, "--frames", "60", "--trace", trace],
               OUT / f"clock_{quartz}.log", {**os.environ, "NEOST_TRACE_CYC": "1"})
    instructions, _ = timing.td.parse(str(trace))
    hits = [i.cyc for i in instructions if i.pc == labels["isr"]]
    if len(hits) != CLOCK_PERIODS or None in hits:
        raise ValueError(f"horloge {quartz} : {len(hits)} IRQ Timer A au lieu de "
                         f"{CLOCK_PERIODS} ({trace})")
    return (hits[-1] - hits[0]) / (len(hits) - 1) * 2457600 / 51200


def main():
    # neost-headless et EmuTOS 256 Ko (versionnée) : un manque est une casse du dépôt, pas
    # un saut légitime — sortie 2, jamais 77.
    missing = [str(p) for p in (timing.NEOST, timing.ROM) if not p.exists()]
    if missing:
        print("HWTST LOOP ERROR: absent(s) : " + ", ".join(missing), file=sys.stderr)
        return 2
    OUT.mkdir(parents=True, exist_ok=True)
    ok = True
    for name, (words, count) in LOOPS.items():
        per = {label: measure(name, words, count, label, scu) for label, scu in SCU}
        ratios = {k: per["8MHz"] / per[k] for k in ("16_off", "16_cache")}
        line = ", ".join(f"{k} {v:.3f}" for k, v in per.items())
        print(f"  {name} : cycles bus par tour {line} ; rapports "
              f"sans cache {ratios['16_off']:.4f}, cache {ratios['16_cache']:.4f}")
        if name != "addq_nop_dbf":
            continue
        if round(per["8MHz"]) != 24:
            print(f"  FAIL {name} : {per['8MHz']:.3f} cycles à 8 MHz, HWTST en attend 24")
            ok = False
        for k, want in MEASURED.items():
            good = abs(ratios[k] - want) <= TOLERANCE[k]
            print(f"  {'OK' if good else 'FAIL'} {name} {k} : NeoST {ratios[k]:.4f}, "
                  f"Mega STE réel {want:.4f} ± {TOLERANCE[k]}")
            ok = ok and good
    for quartz, (want, real) in QUARTZ.items():
        hz = measure_clock(quartz)
        good = abs(hz - want) <= 10
        if quartz == "pal":
            in_range = PAL_RANGE[0] <= hz <= PAL_RANGE[1]
            print(f"  {'OK' if in_range else 'FAIL'} horloge {quartz} : NeoST {hz:.0f} Hz, "
                  f"relevé réel {real}")
            good = good and in_range
        print(f"  {'OK' if good else 'FAIL'} quartz {quartz} : CPU mesuré contre le MFP "
              f"{hz:.0f} Hz, choisi {want} Hz (relevé HWTST : {real})")
        ok = ok and good
    print("HWTST LOOP OK" if ok else "HWTST LOOP FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"HWTST LOOP ERROR: {error}", file=sys.stderr)
        sys.exit(2)
