# Banc du blitter — NeoST contre `stBlitter.sv` et fx68k

(c) 2026 VERHILLE Arnaud. **Ce que le blitter de NeoST fait de différent d'un RTL, accès par accès.**
Posé le 2026-11-04. Instrument : `tools/run_stblitter_bench.py`.

- Échelon : `rtl`, grade **D** ([`REFERENCE_LADDER.md`](REFERENCE_LADDER.md)). Un accord dit « d'accord
  avec un modèle du circuit », pas « fidèle au silicium ». Aucun relevé sur machine réelle ne le soutient.
- Ce que le banc ne voit pas : le Mega STE à 16 MHz et son cache, le DMA disquette ou son qui
  dispute le bus, les bus errors, les interruptions. La mémoire est une RAM à créneaux de 4 cycles.

## Pourquoi

`stBlitter.sv` (Jorge Cwik) est un bloc « fort » de l'échelon `rtl`, resté sans banc. Le blitter de
NeoST est un port d'Hatari : tout désaccord est donc une question qu'aucun des étalons `hatari` ne pouvait poser.
L'arbitrage dépend du 68000 d'en face : le banc câble les deux RTL du même auteur, comme MiSTery,
et les confronte au code réel de NeoST (`Cpu68k`, `Bus`, `Blitter`, `Scheduler`).

## Comment

| Pièce | Rôle |
|---|---|
| `tools/make_blitter_programs.py` | Assemble les programmes : registres, départ, attente de BUSY, relecture. Source et destination pseudo-aléatoires. |
| `tools/stblitter_bench_top.sv` | fx68k + stBlitter sur un même bus, mux de `atarist_sdram.sv` @ `a00deb5b`. |
| `tools/stblitter_bench.cpp` | Pilote Verilator. RAM plate, ou créneaux de 4 cycles avec `--slot`. |
| `tools/blitter_bare_bench.cpp` (cible `neost-blitter-bare`) | Le même programme sur le code de NeoST, sans `Machine`. |
| `tools/run_stblitter_bench.py` | Compare, et garde les verdicts dans `tests/reference/stblitter_bench.json`. Palier `full` ; SKIP (77) sans Verilator ou sans les RTL. |

**Ce qui est comparé** : chaque accès du blitter (sens, adresse, donnée, date), et les écritures du CPU
dans une fenêtre témoin en `$3000`. Une écriture juste avant le départ date la prise de bus ; la
relecture des registres passe par cette fenêtre. Les lectures et préfetchs du CPU ne sont pas tracés côté NeoST.
Les dates comparées sont les intervalles entre accès consécutifs de cette liste.

```sh
cmake --build build --target neost-blitter-bare
python3 tools/run_stblitter_bench.py                 # verdicts contre la référence
python3 tools/run_stblitter_bench.py --flat          # sans créneaux, pour lire un écart
python3 tools/run_stblitter_bench.py --only gap2_exg --verbose
```

## Ce que le banc a trouvé (2026-11-04)

**78 programmes sur 78 concordent** avec les créneaux, accès, données et dates. Avant correction, les
données écrites concordaient déjà partout : les 16 opérations logiques, les 4 HOP, FXSR, NFSR, SMUDGE,
lignes de 1 et 2 mots, incréments négatifs ou impairs, masques qui forcent la lecture, pause et reprise.
Le mode HOG concordait aussi au cycle près.

**Cinq défauts** corrigés, par la règle 2 de [`REFERENCE_LADDER.md`](REFERENCE_LADDER.md) : la netlist
d'abord, puis le RTL d'un bloc « fort », à titre provisoire. La netlist est ST-4082 « BLT CELL »,
15 sept. 1988 (`4082.pdf`, sha256 `bfb30f91…`), révision du ST non repérée.

| Défaut | Avant | RTL, et NeoST maintenant | Source |
|---|---|---|---|
| SMUDGE en HOP 0 | Source non lue | Source lue, résultat inchangé | [B] feuille 1, HOP<1> OU SMUDGE |
| Adresse source relue en plein mot | Avance en fin de mot | Avance avec la lecture | [D] `bltCounters` |

| Fenêtre qui coûte un accès à la tranche | 4 cycles après le 64ᵉ accès CPU | 2 cycles : seul l'accès qui suit sans cycle interne compte | [D] fx68k + `stBlitter.sv` ; [B] feuille 1 pour le compteur, § 5.2.2 pour le mécanisme |
| Accès CPU retenu par la prise de bus | Hors des 64 du CPU | Compté : tout front d'AS compte | [B] feuille 1, compteur cadencé par AS, remis à zéro par BUSY |
| Crédit de cycles recouverts | Absorbait l'accès retenu, qui durait 0 cycle | Ne couvre que des cycles internes | [D] banc `gap2_exg` |

Le tableau qui en résulte, tranche après tranche, à +k cycles entre deux accès du CPU :

| Accès CPU suivant | Tranche | Avant le correctif |
|---|---|---|
| +0 (NOP) | 63 accès | 63 |
| +2 (EXG) | 64 accès | 63 |
| +4 et au-delà (ADDQ.L, LSL) | 64 accès | 64 |

[B] M68000UM Rev 8 § 5.2.2 p. 5-15 donne le mécanisme sans le chiffrer : BG est retardé seulement si le
processeur a déjà décidé son prochain cycle. Hatari (`blitter.c:69-79 @ f0736b24`) annonce 4 cycles
et précise « my guess ». **Aucun relevé sur machine ne tranche la largeur de la fenêtre.** Le test de
vérité serait un STE qui compte les accès d'une tranche sous une boucle d'EXG.

**Écart avec Hatari qui en résulte**, accepté et consigné dans `HATARI_DIVERGENCES.md` : 122 px sur
`blitter_timer`, 20 cycles de durée pour `blitter_shared_large` et 8 pour `blitter_shared_ram`. Les
autres étalons et la suite d'Hatari restent à leur verdict.

## Ce qui manque

- **Mega STE à 16 MHz** : NeoST y garde la fenêtre de 4 cycles d'Hatari, seul témoin. La porte
  d'échange y rejoue, NeoST contre NeoST, le blitter, le DMA disquette et le blit sur ses propres
  registres (`docs/CPU_SWAP_GATE.md`) ; aucun verdict extérieur.
- **Disputes de bus** : son DMA, disquette, et l'attente du blitter pendant ces accès.
- **Le blitter qui s'écrit lui-même** ou qui lit ses registres, et les zones fautives.
- **Le mode `--flat`** reste à 1 ou 2 cycles d'écart dans l'arbitrage : le CPU y est en phase impaire, ce
  qui n'arrive pas sur un ST, dont la RAM aligne les accès. Écart non poursuivi.
