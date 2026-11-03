# Banc différentiel Moira ↔ fx68k — le 68000 nu contre le RTL

(c) 2026 VERHILLE Arnaud. **Ce que le cœur 68000 de NeoST fait de différent d'un RTL
cycle-exact, accès bus par accès bus.** Posé le 2026-11-03. Instrument : `tools/run_fx68k_bench.py`.

- Échelon : `rtl`, grade **D** des deux côtés ([`REFERENCE_LADDER.md`](REFERENCE_LADDER.md)). Un
  écart **localise** une question ; il ne dit pas qui a raison. Rien ici n'ajuste Moira.
- Ce que le banc ne voit pas : la carte mère. Créneaux RAM, états d'attente, E-clock, bus
  errors du GLUE sont hors du modèle (RAM plate, DTACK immédiat). C'est voulu : un écart
  ici est un écart **de cœur**, pas de Bus.
- Les questions ouvertes qui en sortent vivent dans [`../TODO.md`](../TODO.md) § 2.1.

## Pourquoi

Le 68000 porte tout le reste : chaque datation vidéo, MFP ou blitter de NeoST est posée sur
l'horloge de Moira. Jusqu'ici Moira n'était confronté qu'à Hatari (grade D, autre
émulateur, `run_timing_oracle.py`) et aux totaux de cycles de la table de Musashi
(`run_moira_runner.sh`). fx68k (Jorge Cwik, épingle `0602ee46`) est un RTL dérivé du microcode,
« cycle exact » selon son auteur, bloc **fort** de l'échelon `rtl`. Le simuler sous Verilator
donne, pour la première fois, la **position** de chaque cycle bus, la prise d'IPL et les
séquences d'exception, sur le même programme que Moira.

## Comment

| Pièce | Rôle |
|---|---|
| `tools/make_fx68k_programs.py` | Assemble les programmes (vecteurs en $0, code en $1000, données en $2000, pile sous $8000, sentinelle = écriture dans $F00000). Pas d'assembleur 68000 dans la chaîne : encodages à la main, [B] M68000UM § 8. |
| `tools/fx68k_bench.cpp` | Pilote Verilator de `fx68k.sv`. RAM plate 16 Mio, DTACK sans attente, interruptions vectorisées ($40+niveau sur D0-D7, broche relâchée à l'IACK). `--no-assert-case` : `fx68kAlu.sv:313 @ 0602ee46` est un `unique case` qui ne couvre pas l'état d'avant reset. |
| `tools/moira_bare_bench.cpp` (cible CMake `neost-moira-bare`) | Moira **tel que NeoST le compile** (MoiraConfig.h de `build/generated`), sur le même modèle mémoire, même format de trace. `--ipl-fetch` active le port de WinUAE `ipl_fetch_next` (ce que fait `NEOST_IPLFETCH=1`). |
| `tools/run_fx68k_bench.py` | Bâtit, exécute, compare, et garde les verdicts dans `tests/reference/fx68k_bench.json`. Palier `full` de `run_all.py` ; SKIP (77) sans Verilator ou sans `extern/fx68k` (`tools/setup_silicon_refs.sh`). |

**Datation.** Chaque accès est daté au **front descendant d'AS**, en cycles CPU.
[B] M68000UM Rev 8 § 5.1 : AS tombe au début de S2, un cycle après S0. Côté fx68k c'est
observé sur la broche ; côté Moira c'est `clock − 1` au rappel `read16`/`write16`, parce que
Moira encadre chaque cycle bus de `willAccessBus` (S0), `SYNC(2)`, accès, `SYNC(2)`
(`MoiraDataflow_cpp.h`). Le second transfert d'un cycle lecture-modification-écriture (TAS,
AS reste bas) est daté à la retombée de DS moins un cycle (fig. 5-5 : à l'écriture, DS
tombe à S4). L'IACK est daté `clock − 3` côté Moira : les quatre cycles qui précèdent la
lecture du vecteur (`SYNC(iackSyncBefore = 4)`) sont le cycle bus d'IACK, et fx68k
l'enchaîne aussitôt après l'écriture du PC bas. Son écart est de toute façon rapporté à part
(`iack_skew`).

**Comparaison.** Les deux traces sont alignées sur leur premier accès (lecture du vecteur de
reset), puis comparées sur la **nature** de chaque accès (FC, sens, taille, adresse,
donnée) et sur l'**intervalle** avec l'accès précédent. Des intervalles plutôt que des dates
absolues : un écart ne masque pas les suivants, et le rapport compte. Deux mots d'un même
long écrits dans l'ordre inverse comptent comme un `swap` ; un accès présent d'un seul côté
est un `extra` et la comparaison se resynchronise dessus. La séquence de reset n'est datée
que dans `reset_nop`. Les programmes à IRQ sont balayés sur huit phases (`+k` = la broche
monte k cycles plus tard), et rejoués avec `--ipl-fetch`.

**Référence.** Sans `tests/reference/fx68k_bench.json`, un écart fait échouer. Avec lui,
c'est un **changement** de verdict qui fait échouer, dans les deux sens : un écart qui se
referme se juge puis se re-pose (`--write-refs`), comme un étalon.

```sh
cmake --build build --target neost-moira-bare
python3 tools/run_fx68k_bench.py                 # verdicts contre la référence
python3 tools/run_fx68k_bench.py --only stop_pending --verbose
paste -d'|' tests/out/fx68k_bench/stop_pending+0.fx.txt tests/out/fx68k_bench/stop_pending+0.moira.txt
```

## Ce que la première passe a trouvé (2026-11-03)

22 variantes sur 34 en accord cycle à cycle. **Tout ce que le banc exerce des instructions
ordinaires concorde** : les 14 corps du cycle-bench, MULU/MULS/DIVU/DIVS, décalages,
LINK/UNLK, PEA/LEA, BCD, bits, Scc, MOVEP, MOVE SR/CCR, TAS (cycle RMW de 10 cycles),
l'exception trace, TRAP/TRAPV/CHK/division par zéro/ILLEGAL/ligne A et F (trames et durées),
et la prise d'IRQ sur 6 phases sur 8. Les écarts, tous **[D] fx68k @ 0602ee46 contre Moira
@ pin vendorisé**, par ordre d'intérêt pour un ST :

| # | Écart | fx68k | Moira | Ce qui tranche |
|---|---|---|---|---|
| 1 | **STOP avec IRQ déjà pendante et masquée** (`stop_pending`) : du mot d'extension du STOP à la première écriture de trame | **18 cycles** (reconnaissance à STOP+12) | 14 (STOP+8) | La décision du 2026-10-22 (« STOP+8, fx68k et Hatari divergent, le silicium prime ») reposait sur une lecture du RTL ; sa **simulation** donne +12, comme Hatari. Rouvrir. Tranche au-dessus : microcode (og.kervella.org, entrée `4e72`), relevé sur machine. |
| 2 | **Prise d'IPL à l'instant d'une frontière** (`irq_nop`) | Synchroniseur à deux étages (`fx68k.sv:222-223`) : aux phases 0 et 4, l'IRQ part une instruction plus tard | Voit la broche aussitôt : une instruction (4 cycles) plus tôt à 2 phases sur 8 | Avec `--ipl-fetch` (port WinUAE, `NEOST_IPLFETCH`), les phases 0 et 4 concordent et les six autres divergent (IRQ une instruction plus tard) : ni l'un ni l'autre n'est le modèle du RTL. Modèle à écrire d'après `fx68k.sv`, puis qualifier aux étalons (Timer B rasters). |
| 3 | **Réveil du STOP** (`stop_irq`) | Phases 3 et 7 : réveil 4 cycles plus tôt que Moira | Quantification sur l'itération de 4 cycles, portée le 2026-10-22, décalée d'un cycle à 2 phases sur 8 | Même mécanisme que le n° 2, à qualifier ensemble. `--ipl-fetch` n'y change rien (1 écart à toutes les phases). |
| 4 | **Ordre des deux mots d'une écriture longue** | Mot **bas d'abord** pour `MOVEM.L -(An)` (15 swaps) et `ADDQ.L #,<mem>` (6 swaps) | Mot haut d'abord | Comportement connu du 68000 pour `MOVE.L Dn,-(An)` (Moira l'a, drapeau `REVERSE`) ; à confirmer pour MOVEM et les RMW longs : yacht.txt, WinUAE CE (`cpuemu_11`), microcode. Visible sur un ST si la cible est un registre matériel ou une zone à bus error. |
| 5 | **Lecture fantôme du DBcc expiré** (`alu_loops`, 14 cas) | Préfetch à l'**adresse de branchement**, puis la suite | Relit deux fois le mot de la suite | Yacht : DBcc « cc false, compteur expiré » 14(3/0). Adresse du 3ᵉ accès à confirmer au microcode (`51c8`). Visible si la cible est en zone à bus error. |
| 6 | **Lecture en plus de MOVEM mémoire → registres** (`mixed`) | Lit le mot **après le dernier** transféré ($8000 ici) | Relit le **premier** mot | Comportement connu du 68000 (lecture au-delà) ; à confirmer : yacht.txt, M68000UM. Visible si le mot suivant est un registre matériel ou une zone à bus error (fin de RAM, $FF8xxx). |
| 7 | **JSR/BSR : ordre lecture-cible / empilement** | Lit le premier mot de la cible **avant** d'empiler l'adresse de retour | Empile puis lit | Yacht JSR : « n nS ns np ». Durées égales. Visible si la cible est en zone à bus error (ordre bus error / pile). |
| 8 | **Erreur d'adresse** (`exceptions`) | Cycle bus **amorcé** (AS sans strobe) à l'adresse fautive, puis trame : PC bas, SR, PC haut, IR, adresse basse, **mot d'état, adresse haute** | Pas de cycle amorcé ; trame PC bas, PC haut, SR, IR, adresse basse, adresse haute, mot d'état ; même total (44 cycles) | [B] M68000UM § 6.2.5 fig. 6-6 donne le format, pas l'ordre d'écriture. Microcode. Visible par la trame en RAM à l'instant d'une seconde faute (double faute → halt). |
| 9 | **Reset** (`reset_nop`) | 6 cycles entre les deux premiers préfetchs | 4 | Table 8-? « RESET 40(6/0) » ne fixe pas la place des cycles internes. Déplace de 2 cycles la phase du faisceau au boot (étalons à phase E-clock). Microcode. |

Les `iack_skew` sont nuls : la place du cycle IACK dans le bloc de Moira correspond à
fx68k pour `iackSyncBefore = 4`.

## Ce qui manque au banc

- **Bus error** (BERR, et la relance par HALT, non implémentée dans fx68k).
- **Autovecteur par VPA** et synchronisation E-clock : ici l'IACK est vectorisé par le
  périphérique ; sur ST, HBL et VBL passent par VPA.
- **RESET** (l'instruction), MOVE16 non (68000), Bcc.w/BSR.s pris et non pris en séries,
  `CHK` et `DIVS` en trap, `RTR`, `TRAPV` pris, `LINE A` en mode utilisateur.
- Un **second RTL** pour les mêmes programmes : `stBlitter.sv` n'a pas de banc, et le GSTMCU
  (`run_gstmcu_bench.py`) n'exécute pas de code.
- Verilator **en CI** : le banc est SKIP sur les runners, comme `run_gstmcu_bench.py`.
