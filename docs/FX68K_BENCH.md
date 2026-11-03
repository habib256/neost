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
[B] M68000UM Rev 8 § 5.1.1 p. 5-4 : AS tombe au front montant de S2, un cycle après S0. Côté
fx68k c'est observé sur la broche ; côté Moira c'est `clock − 1` au rappel `read16`/`write16`,
parce que Moira encadre chaque cycle bus de `willAccessBus` (S0), `SYNC(2)`, accès, `SYNC(2)`
(`MoiraDataflow_cpp.h`). Le second transfert d'un cycle lecture-modification-écriture (TAS,
AS reste bas) est daté à la retombée de DS moins un cycle : [B] § 5.1.2 p. 5-6, une écriture
pose DS à S4, un cycle après AS ; § 5.1.3 p. 5-9, le RMW le pose à S16. L'IACK est daté
`clock − 3` côté Moira : les quatre cycles qui précèdent la lecture du vecteur
(`SYNC(iackSyncBefore = 4)`) sont le cycle bus d'IACK. Son écart est rapporté à part
(`iack_skew`).

**Comparaison.** Les deux traces sont alignées sur leur premier accès (lecture du vecteur de
reset), puis comparées sur la **nature** de chaque accès (FC, sens, taille, adresse,
donnée) et sur l'**intervalle** avec l'accès précédent. Des intervalles plutôt que des dates
absolues : un écart ne masque pas les suivants, et le rapport compte. Deux mots d'un même
long écrits dans l'ordre inverse comptent comme un `swap` ; un accès présent d'un seul côté
est un `extra` et la comparaison se resynchronise dessus. La séquence de reset n'est datée
que dans `reset_nop`. Les programmes à IRQ sont balayés sur huit phases (`+k` = la broche
monte k cycles plus tard), et rejoués avec `--ipl-fetch`. **L'instant de la broche se compte
depuis le premier accès après le reset**, jamais depuis le début de la trace : les deux cœurs
diffèrent de 2 cycles dans le reset, et la broche tomberait à une autre phase du programme.
`--ipl-sweep` balaie cet instant et affiche la frontière prise par chaque cœur.

**Référence.** Sans `tests/reference/fx68k_bench.json`, un écart fait échouer. Avec lui,
c'est un **changement** de verdict qui fait échouer, dans les deux sens : un écart qui se
referme se juge puis se re-pose (`--write-refs`), comme un étalon.

```sh
cmake --build build --target neost-moira-bare
python3 tools/run_fx68k_bench.py                 # verdicts contre la référence
python3 tools/run_fx68k_bench.py --only stop_pending --verbose
paste -d'|' tests/out/fx68k_bench/stop_pending+0.fx.txt tests/out/fx68k_bench/stop_pending+0.moira.txt
```

## État (2026-11-03)

26 variantes sur 35 concordent cycle à cycle. Instructions ordinaires, exceptions, trace, écritures
longues sur tous les modes (`rmw_long`), reset et STOP avec IRQ pendante sont alignés.

**Corrigé dans Moira le 2026-11-03**, avec fx68k @ `0602ee46` et un autre témoin à chaque fois,
Hatari @ `f0736b24` (`cpuemu_13.c`, généré) ou le microcode (`extern/datasheets/m68k_microcode_all.txt`).
Tous de grade D ; le microcode et fx68k ne sont pas indépendants. Étalons pixel inchangés.

| Correction | Autre témoin |
|---|---|
| STOP dure 4 cycles : ils avaient disparu avec le préfetch retiré le 2026-10-22 | [B] M68000UM § 8.11 ; microcode `stop1`/`aaa01` ; Hatari `op_4e72_13_ff` |
| Mot bas écrit d'abord : `MOVEM.L -(An)`, ADD/SUB/ADDI/SUBI/ADDQ/SUBQ.L en mémoire | Hatari `op_48e0_13_ff`, `op_5090_13_ff`, `op_0690_13_ff` |
| Mot bas lu d'abord : ADDX/SUBX.L `-(Ay),-(Ax)` | Hatari `op_d188_13_ff` |
| DBcc expiré : premier préfetch à l'adresse de branchement | Microcode `dbcc2` ; Hatari `op_51c8_13_ff` |
| MOVEM.L mémoire→registres : lecture en plus après le dernier mot | Hatari `op_4cd8_13_ff` |
| JSR : préfetch de la cible avant l'empilement | Microcode `jsra1` ; Hatari `op_4e90_13_ff` |
| Trame des erreurs de bus et d'adresse : ordre d'écriture | Hatari `Exception_ce000` |
| Reset : 2 cycles entre les deux premiers préfetchs | Microcode `trap8`/`trap9`/`b` ; [B] M68000UM § 8.12 pour le total |

**Restent ouverts**, sur le cœur nu. Dans NeoST, la chaîne MFP→IPL ajoute ses propres délais, calés sur
Hatari : le banc ne dit pas à lui seul ce que NeoST fait sur une IRQ réelle.

| Écart | Détail |
|---|---|
| Date limite de la broche IPL (`irq_nop`, `irq_mulu`, `stop_irq`) | Voir ci-dessous. `fx68k.sv:222-223` montre deux étages d'échantillonnage. |
| Erreur d'adresse : cycle amorcé | fx68k pose AS sans strobe à l'adresse fautive, pas Moira ni Hatari. Sans effet connu sur un ST. Un seul témoin. Seule cause du `diverge` d'`exceptions`. |

Mesure, résolution ±1 cycle (`--ipl-sweep`). Date la plus tardive de la broche pour qu'une frontière
donnée prenne encore l'IRQ, relativement à fx68k :

| Programme | Moira | `NEOST_IPLFETCH` |
|---|---|---|
| `irq_nop` | +3 | −1 |
| `irq_mulu` | +3 | une instruction de retard (−65) |
| `stop_irq` | +1 | −3 |

`iack_skew` vaut 0 partout : la place du cycle IACK dans le bloc de Moira correspond à fx68k.

## Ce qui manque au banc

- **Bus error** (BERR, et la relance par HALT, non implémentée dans fx68k).
- **Autovecteur par VPA** et synchronisation E-clock : ici l'IACK est vectorisé par le
  périphérique ; sur ST, HBL et VBL passent par VPA.
- **RESET** (l'instruction), Bcc.w/BSR.s pris et non pris en séries,
  `CHK` et `DIVS` en trap, `RTR`, `TRAPV` pris, `LINE A` en mode utilisateur.
- Un **second RTL** pour les mêmes programmes : `stBlitter.sv` n'a pas de banc, et le GSTMCU
  (`run_gstmcu_bench.py`) n'exécute pas de code.
- Verilator **en CI** : le banc est SKIP sur les runners. `run_gstmcu_bench.py` n'est, lui,
  branché sur aucun palier.
