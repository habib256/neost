# Banc différentiel Moira ↔ fx68k — le 68000 nu contre le RTL

(c) 2026 VERHILLE Arnaud. **Ce que le cœur 68000 de NeoST fait de différent d'un RTL
cycle-exact, accès bus par accès bus.** Posé le 2026-11-03. Instrument : `tools/run_fx68k_bench.py`.

- Échelon : `rtl`, grade **D** des deux côtés ([`REFERENCE_LADDER.md`](REFERENCE_LADDER.md)). Un
  écart **localise** une question ; il ne dit pas qui a raison. Rien ici n'ajuste Moira.
- Ce que le banc ne voit pas : la carte mère. Créneaux RAM, états d'attente, E-clock, bus
  errors du GLUE sont hors du modèle (RAM plate, DTACK immédiat). C'est voulu : un écart
  ici est un écart **de cœur**, pas de Bus.
- Les questions ouvertes qui en sortent vivent dans [`../TODO.md`](../TODO.md) § 9 (gelées jusqu'à l'échange du cœur).

## Pourquoi

Le 68000 porte tout le reste : chaque datation vidéo, MFP ou blitter de NeoST est posée sur
l'horloge de Moira. `run_timing_oracle.py` compare les programmes à Hatari (grade D).
`run_moira_runner.sh` rejoue les suites officielles, dont le corpus d'instructions
individuelles du 68000 enregistré sous MAME. fx68k (Jorge Cwik, épingle `0602ee46`)
est un RTL dérivé du microcode,
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

26 variantes sur 35 concordent cycle à cycle (34 sur 48 au 2026-11-10, bus errors comprises : § *Bus error et double faute*). Instructions ordinaires, exceptions, trace, écritures
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

| Programme | Moira | `NEOST_IPLFETCH` | `--ipl-lag 2` |
|---|---|---|---|
| `irq_nop` | +3 | −1 | +1 |
| `irq_mulu` | +3 | une instruction de retard (−65) | +1 |
| `stop_irq` | +1 | −3 | −1 |

**Pourquoi rien n'est porté dans NeoST.** Un retard de 2 cycles ramène le cœur nu à ±1 de fx68k, mais aucun étalon ne
discrimine la question : `NEOST_IPLLAG=40`, essayé le 2026-11-04, laissait tous les étalons et oracles verts, car
`neostUpdateIpl(commit)` pose la broche et l'échantillon ensemble. Un vert n'y prouverait rien. Et les témoins se
contredisent sur les instructions longues : sur MULU, fx68k échantillonne près de la fin, le modèle de WinUAE que porte
`NEOST_IPLFETCH` une instruction plus tôt. Pour trancher : une sonde sur machine réelle, IRQ datée autour d'un MULU.

`iack_skew` vaut 0 partout : la place du cycle IACK dans le bloc de Moira correspond à fx68k.

## Autovecteur par VPA et E-clock (2026-11-04)

Relevé sur fx68k seul, `Vfx68k_bench --vpa --irq T:5` sur `irq_nop`, RAM sans créneaux : c'est ainsi que HBL et VBL passent sur
ST. De l'écriture du PC bas à celle du SR, AS à AS, selon la phase du compteur d'E-clock :

| Date du PC bas, modulo 10 | 1 | 3 | 5 | 7 | 9 |
|---|---|---|---|---|---|
| Intervalle, cycles | 23 | 21 | 19 | 17 | 25 |

Une attente de 0 à 8 cycles par pas de 2, période 10. NeoST modélise `4 + attente + 14` (`iackSyncBefore`, `iackSyncAfter`), soit 18 à 26. Le cycle
d'écart est une parité : le compteur de fx68k démarre ici en phase impaire avec le bus, ce qu'aucune source ne fixe sur une machine réelle. Avec les
créneaux de 4 cycles d'un ST, les deux séries donnent les mêmes dates (20, 20, 24, 24, 28), par calcul : **ce banc ne les a pas exécutées avec créneaux**.

## La date limite d'IPL, confrontée dans NeoST (2026-11-04)

Le n° 1 du tableau a trouvé son banc : `tools/run_stcpu_bench.py`, où aucun retard n'a été nécessaire. Le « +3 » est celui de Moira **nu** dans sa propre convention de broche ; dans NeoST sur STE, la broche et `POLL_IPL` donnent 256/256 sur quatre boucles, à 4 cycles de résolution. → [`STCPU_BENCH.md`](STCPU_BENCH.md)

## Bus error et double faute (2026-11-10)

Les deux pilotes répondent par **BERR au lieu de DTACK** à tout accès hors IACK dans
`$E00000-$EFFFFF`, dès AS : pas de chien de garde, c'est le cœur qu'on mesure. Côté Moira,
`moira::BusError` porte la trame que construit NeoST (`Cpu68k.cpp`, `raiseBusError`) ; la
double faute est celle de Moira (`processException` → `halt`). Aucun programme antérieur
n'accède à la fenêtre : leurs verdicts n'ont pas bougé. Cinq programmes, aucun en accord :

| Programme | Ce qu'il fait |
|---|---|
| `berr` | Bus error sur lecture et écriture octet, mot, long ; CLR, TAS, ADDI en mémoire ; `(a2)+`, `-(a2)`, `add.w d0,(a2)`, `move.l -(a2),-(a2)` (A2 relevé après la faute) ; MOVEM dans les deux sens ; préfetch après JMP. |
| `double_fault` | Pile dans la fenêtre, puis une lecture fautive : la trame faute à son tour. |
| `double_fault_trap` | Pile dans la fenêtre, puis TRAP : l'empilement faute (bus error), puis la trame de la bus error. |
| `double_fault_addr` | Pile impaire, puis TRAP. |
| `double_fault_vec` | Vecteur 2 pointé dans la fenêtre : la trame passe, le préfetch du gestionnaire faute. |

Les cinq finissent sur `halt` des deux côtés, sauf `double_fault_addr` chez Moira (ci-dessous).
Écarts, du plus au moins témoigné. Hatari @ `f0736b24` (`cpuemu_13.c` généré, `newcpu.c`) est
indépendant de fx68k : deux témoins de grade D qui s'accordent contre Moira pèsent plus qu'un.

| Écart | fx68k | Moira | Autres témoins |
|---|---|---|---|
| Pile impaire à l'empilement d'un TRAP | erreur d'adresse, puis double faute : `halt` | trame écrite à l'adresse arrondie (`writeStackFrame0000` : `& ~1`), gestionnaire exécuté ; `halt` plus tard, sur le RTE | Hatari `newcpu.c:3088` et `exception3_notinstruction` : erreur d'adresse, puis `halt`. [B] M68000UM § 6.3.9 : une erreur d'adresse pendant le traitement d'une erreur de bus ou d'adresse arrête le processeur. |
| An après une faute sur `(An)+` ou `-(An)` | incrément ou décrément faits ($E00102, $E000FE) | An inchangé ($E00100) | Hatari `op_3018_13_ff` : `m68k_areg += 2` avant `exception2_read`. |
| PC empilé, faute sur `(An)+`, `add.w Dn,(An)`, `move.l -(An),-(An)` | instruction + 2 de plus que Moira | — | Hatari `op_3018_13_ff` : `m68k_incpci(2)` avant la faute. Les modes absolus concordent. |
| PC empilé, faute au préfetch après JMP | près du JMP (`$114C`) | la cible (`$E00200`) | Hatari `op_4ef9_13_ff` : `pcoffset = (oldpc − srca) + 2`, près du JMP. |
| Entrée de l'exception | 14 cycles de l'AS fautif à la première écriture de trame, puis 4 avant le vecteur | 10, puis 6 : 2 cycles de moins au total | Non confronté. Moira abandonne le `SYNC(2)` de fin du cycle fautif (`MoiraDataflow_cpp.h`). Dans NeoST sur STE, le chien de garde du GSTMCU (60 cycles) précède de toute façon la faute. TAS : 15 contre 10. |
| SP de la trame de bus error quand l'empilement d'un TRAP faute | SP d'avant le TRAP (première écriture à `$E000FE`) | SP déjà décrémenté de 6 (`$E000F8`) | Hatari comme Moira (`m68k_areg(7) -= 3*2` avant les écritures). Sans effet visible : la machine s'arrête. |

**Rien n'est porté** : le cœur est gelé jusqu'à l'échange (`TODO.md` § 9). Aucun titre connu ne
dépend de ces cas ; ce sont des **questions à requalifier sur POM68K**, que le banc rejouera
telles quelles. Un titre qui récupère d'une bus error en relisant An ou le PC empilé (protection,
sondage de mémoire) serait le premier à les révéler.

## Migration vers Moira 6 : fautes de groupe 0

Comparaison de l'ancien cœur NeoST et de Moira officiel `bf98107f` avec le même banc
fx68k `0602ee46`. Le banc passe de 34 à 37 accords sur 48 variantes. L'API officielle expose avec `getPC()` le PC interne moins deux
octets. Les accès mémoire du cœur lèvent désormais une faute par son adresse et
Moira termine la trame avec le contexte de l'instruction. La voie hors accès
mémoire du cœur conserve une trame complète, avec le PC interne.

| Variante | Arbitrage |
|---|---|
| `exceptions` | L'amont déplaçait deux cycles du départ vers le vecteur au début de l'empilement. Le report après la trame conserve les dates des sept écritures observées par fx68k. Reste un début de lecture sans strobe dans le RTL. |
| `berr` | L'entrée de trame de l'amont prend 14 cycles depuis l'accès fautif, puis quatre du dernier mot empilé au vecteur. Le microcode transcrit `state_bus_error` enchaîne la dernière écriture `trap5` et la lecture `trap6`. Les trames contextuelles corrigent notamment les PC de `MOVEM` dans les deux sens. |
| `double_fault` et `double_fault_vec` | L'arrêt, les mots écrits et les intervalles d'accès concordent avec fx68k. Le manuel Motorola Rev. 8 § 5.4.4 et § 6.3.9.1 impose l'arrêt à la seconde faute, y compris au premier préfetch du gestionnaire. Les dates de la broche HALT restent à mesurer. |
| `double_fault_trap` | Après une faute sur la première écriture du TRAP, l'amont remet SP à sa valeur initiale. Le deuxième empilement recommence à la même adresse, comme dans fx68k. Hatari garde le SP décrémenté. Aucun relevé sur machine réelle ne distingue ici ces deux chemins. |
| `double_fault_addr` | L'amont arrête le processeur sur SP impair sans écrire la trame arrondie de l'ancien cœur. Le manuel Motorola Rev. 8 § 6.3.10 confirme l'arrêt sur la seconde faute. Hatari arrive à la même issue. fx68k présente deux cycles amorcés sans strobe, absents de Moira. L'amont court-circuite également le premier empilement, donc la date de HALT et la notification de la première faute restent à vérifier. |

Pour `TAS` absolu long, le manuel Motorola Rev. 8 § 5.1.3 et § 6.3.9.1 distingue
les phases lecture et écriture du RMW. La faute de lecture préserve le bit R/W de la
lecture et le SR antérieur. La transcription de `4af9` par Kervella place le
transfert de l'incrément du PC après `abll3`, la lecture de l'opérande. Le réglage
NeoST conserve le PC vu par fx68k, avec une avance d'un cycle sur l'entrée de
trame. Hatari `cpuemu_13.c:44116-44121` et `newcpu.c:9077-9083` @ `36a8acf0`
empile un PC avancé. Le banc actuel ne date pas un `TAS` fautif sur le silicium.

### Démarrage TOS : prédécrément et PC sur BERR

Dans la comparaison `4bccff0f` → Moira 6, la première différence des cinq scénarios
ci-dessous arrive dans la sonde des 4 Mo. Le TOS écrit `MOVE.W D2,-(A1)` à
`$41FFFE` et prend le vecteur 2. La trace (`NEOST_TRACE_CYC=1`, `--trace`,
`--regs --irq`) donne la même date d'entrée du gestionnaire avec les deux cœurs.
L'ancien A1 reste à `$420000`. Moira 6 conserve `$41FFFE`.

| Scénario | PC de l'écriture | Entrée du gestionnaire | Cycle bus |
|---|---|---|---:|
| `boot_tos205fr` | `$E0019A` | `$E001B6` | 145484 |
| `boot_tos206uk` | `$E0019C` | `$E001B8` | 398852 |
| `boot_tos104uk_megast`, `whatifgame` | `$FC0170` | `$FC018C` | 399708 |
| `replay/rainbow_islands_4mb` | `$FC0182` | `$FC019E` | 399732 |

À `$FC018C`, un arrêt `--break FC018C` sur Mega ST 4 Mo donne la trame de groupe
0 à `$7FF2`. Ses sept mots concordent sauf le PC, `$FC0172` sur le parent et
`$FC0174` sur le candidat. [B] M68000UM Rev. 8 § 6.3.9.1 situe le PC empilé
de 2 à 10 octets après l'opcode et décrit le contenu de la trame. [D] Le cas
`berr` avec l'opcode `$3500` (`MOVE.W D0,-(A2)`) donne exactement sur fx68k
et Moira 6 un PC à opcode + 4 et un A2 décrémenté. Hatari @ `36a8acf0`
(`cpuemu_13.c:115698-115738`, `op_3100_14_ff`) fait de même sur 68000. Le
microcode transcrit `3100` (`rmmw1`, `mmmw2`) situe la mise à jour d'An au
cycle d'écriture. Le cas précis reste à mesurer sur le 68000 réel lors d'un BERR
externe.

### Démonstrations STE démarrées sur ST : PC empilé sur BERR

Le corpus collection lance les deux disquettes sur ST/TOS 1.04. Just Musix 2
(disque 1) tente `MOVE.W #2,$FF8A20` à `$1C16` (blitter). E605 tente
`MOVEP.L D0,1(A0)` à `$4482` avec `A0=$FFFF8900` et faute à `$FF8901`
(son DMA). Ces registres ne répondent pas sur le ST émulé. Les deux démonstrations
finissent dans une suite de trames de groupe 0 provoquées par
`MOVEM.L D0/A3-A5,$68(A6)` à `$FC92FC`, en écriture vers la ROM `$FC0B72`.
Les traces horodatées des instructions et des registres autour du premier
écart concordent à l'octet.

- **Just Musix 2**, trame 140. Douze PC de trame en RAM ont le mot bas
  `$9302` sur le parent et `$9304` sur le candidat. Deux copies du
  même mot subsistent dans l'historique du bus vidéo. La pile descend jusque
  dans la table des vecteurs. À la trame 148 (échantillon 150 dans la porte),
  `TRAP #1` à `$02FC0B50` lit le vecteur GEMDOS à `$84`, écrasé par le mot
  bas du PC de la faute puis par le début de la trame suivante. Les deux cœurs
  reprennent au même cycle 23550504, respectivement à `$930209FC` et
  `$930409FC`. Les états à la trame 151 confirment le vecteur corrompu. À ce
  point, le parent est en double faute et le candidat exécute encore des
  `ORI.B #0,D0`. À 180 les deux sont arrêtés, le parent avec
  `PC=$92830, SSP=$FFFFFFFA` et le candidat avec
  `PC=$9282C, SSP=$FFFFFFF8`. L'écart durable d'empreinte CPU suit donc
  l'usage par l'invité du PC empilé sur la faute MOVEM.
- **E605**, premier écart à la trame 1118, relevé à 1120. La RAM conserve
  52 mots bas de PC de trame `$9302` → `$9304` et le PC du premier
  BERR sur MOVEP à `$5F2` (`$4486` → `$4484`). L'historique du bus vidéo
  conserve le mot `$9302` → `$9304` et les sept écritures d'une trame à
  +4 cycles chacune. Cette datation correspond au début de trame BERR
  de Moira 6 observé au banc.

[B] M68000UM Rev. 8 § 6.3.9.1 donne la plage du PC empilé en cas de BERR.
[D] Deux sondes nues dans la fenêtre BERR `$E00000`–`$EFFFFF` exécutent les
opcodes exacts `$48EE` et `$01C8` à `$101C`. fx68k @ `0602ee46`
empile respectivement `$1024` (MOVEM) et `$101E` (MOVEP), comme Moira 6.
Le parent empilait `$1022` et `$1020`. Hatari @ `36a8acf0`
(`cpuemu_13.c:128663-128727`, `op_48e8_14_ff` et `cpuemu_13.c:91477-91531`,
`op_01c8_14_ff`) donne les mêmes PC que le candidat. Aucun relevé sur un
vrai ST ne fixe ces deux valeurs ou les sous-intervalles des écritures.

### Epic, disque 2 seul : retour sur une adresse impaire

Dans la collection automatisée, le disque 2 d'Epic lancé seul prend un BERR
à `$520761` en exécutant `MOVE.B -(A3),(A2)+` à `$392`. Le gestionnaire à
`$140` exécute un RTE direct. Ce RTE lit les six premiers octets de la trame
de groupe 0 comme SR et PC, et prend donc l'adresse fautive impaire pour PC.
Le vecteur 3 revient lui aussi à `$140`. Les valeurs des registres concordent à
l'entrée de chaque boucle, mais celle-ci prend 72 cycles sur le parent et
76 sur le candidat. La première empreinte différente arrive à la trame 768
en ST et à 701 en Mega STE.

[B] M68000UM Rev. 8 § 6.3.9.1 demande au gestionnaire de nettoyer la pile et
§ 6.3.10 décrit l'erreur d'adresse sur un fetch impair. Une sonde nue place
SR `$2415` et PC `$00520761` sur la pile, puis exécute RTE. [D] Entre la
lecture du dernier mot de pile et celle du vecteur 3, fx68k @ `0602ee46`
compte 44 cycles, l'ancien Moira 42 et le candidat 44. La date de la première
écriture de trame reste distincte (respectivement 16, 12 et 14 cycles après
la lecture du dernier mot de pile). fx68k présente aussi un cycle de lecture
amorcé sans strobe sur la cible impaire. Le gain de timing global du cœur nu
laisse les sous-intervalles et la date sur une machine réelle à vérifier.

## Bus prêté à un DMA, BR/BG (2026-11-11)

`Vfx68k_bench --br T:N` : un maître pose BR au cycle T (origine des `--irq`), prend le bus en trois fils
dès BG accordé, AS et DTACK retombés ([B] M68000UM Rev 8 § 5.2.3), le garde N cycles sous BGACK, puis le
rend ; les dates sortent sur stderr (`ARB …`). Relevé sur `irq_mulu` (prologue riche en accès, puis
boucle de MULU), N = 32 comme une rafale de 16 octets du DMA disque, T de 0 à 120 par pas de 4 :

| BR tombe pendant | BR → BG | Retard du CPU sur l'accès qui suit la remise |
|---|---|---|
| le prologue (accès bus serrés) | 2 cycles | 32 ou 33, 29 à deux phases |
| un MULU (calcul interne) | 2 cycles | **0** : le calcul continue, le prêt est masqué |

Le 68000 ne s'arrête pas : il rend le bus et ne bloque qu'au prochain accès dont il a besoin. NeoST
facture au CPU 32 cycles à chaque vidage de FIFO du DMA disque, sans condition (`Fdc::billDmaCycles`
→ `Bus::stealBusCycles`, port d'`M68000_AddCycles_CE` d'Hatari). Le blitter, lui, partage déjà le bus
de cette façon (crédit de recouvrement dans l'enveloppe `NeostMoira::sync`). Grade D (RTL) sur un
point que la fiche ne dit pas. Rien n'est porté : l'arbitrage vit dans l'enveloppe du cœur, gelée
jusqu'à l'échange (`TODO.md` § 9).

```sh
python3 -c "import sys; sys.path.insert(0,'tools'); import make_fx68k_programs as m; open('/tmp/mulu.bin','wb').write(m.build('irq_mulu'))"
(cd build-rtl/fx68k && ./Vfx68k_bench /tmp/mulu.bin /tmp/base.txt --limit 3000 \
   && ./Vfx68k_bench /tmp/mulu.bin /tmp/br.txt --limit 3000 --br 96:32)
```

## Ce qui manque au banc

- **Relance par HALT** après une bus error (non implémentée dans fx68k).
- **Autovecteur par VPA, dans le verdict** : relevé à part, ci-dessus, pas comparé accès par accès. Volontairement : la
  synchronisation E-clock vit dans l'enveloppe NeoST (`iackSyncBefore/After`), pas dans Moira, et le chemin complet
  (VPA, E-clock, IACK, prise de HBL et de VBL) est confronté dans la machine par le banc CPU + GSTMCU, 256 sur 256 et
  32 sur 32 ([`STCPU_BENCH.md`](STCPU_BENCH.md)).
- Le GSTMCU (`run_gstmcu_bench.py`) n'exécute pas de code. `stBlitter.sv` a son propre banc,
  [`STBLITTER_BENCH.md`](STBLITTER_BENCH.md).
