# Banc CPU + GSTMCU — ce que le 68000 voit du faisceau

(c) 2026 VERHILLE Arnaud. **Le même programme sur fx68k + GSTMCU et dans NeoST, et ce qu'il a relevé en RAM.** Posé le 2026-11-04.
Instrument : `tools/run_stcpu_bench.py`.

- Échelon : `rtl`, deux blocs « forts », grade **D** ([`REFERENCE_LADDER.md`](REFERENCE_LADDER.md)). Un accord dit « d'accord avec un modèle du circuit ».
- **STE seulement.** ROM de test sans TOS, sans MFP, sans FDC. Les HBL et VBL y sont les seules interruptions, RAM à créneaux par le modèle lui-même.
- Le modèle GSTMCU n'exécute le Shifter et le compteur qu'aux dates qu'il calcule : aucun relevé sur machine réelle n'est en jeu.

## Pourquoi

Une démo synchronisée lit `$FF8205-09`, ou attend une HBL, puis écrit à un cycle précis. Tout le raster de NeoST repose sur cette relation, calée jusqu'ici sur Hatari. La question ouverte du banc
fx68k (« date limite de la broche IPL ») ne pouvait pas se trancher : aucun étalon n'y était sensible. Ici, chaque lecture du compteur date l'instruction à 4 cycles près.

## Comment

| Pièce | Rôle |
|---|---|
| `tools/stcpu_bench_top.v`, `tools/stcpu_bench.cpp` | fx68k + `ste_tb` (GSTMCU + Shifter), câblés comme `atarist_sdram.sv` @ `a00deb5b`. RAM pilotée par RAS/CAS, ROM de 256 Ko. |
| `tools/make_stcpu_rom.py` | La ROM : pose l'écran, attend la VBL par STOP, remplit les tables. |
| `tools/run_stcpu_bench.py` | Exécute des deux côtés, compare à l'octet, garde les écarts dans `tests/reference/stcpu_bench.json`. Palier `full`. |
| `tools/make_stcpu_raster.py`, `tools/run_stcpu_raster.py` | Le pixel, voir plus bas. Écarts dans `tests/reference/stcpu_raster.json`. Palier `full`. |

Les tables : trois balayages de 16 000 lectures de `$FF8209`, décalés de 4 et 8 cycles (`move.b (a1),(a0)+` et DBRA, 24 cycles par lecture), soit 2,4 trames chacun. Puis une lecture par HBL en plein DE, sur
quatre formes de boucle interrompue : NOP et Bcc, longue suite de NOP, STOP, MULU. Enfin une lecture par VBL.

## Résultat (2026-11-04)

| Table | Identiques à l'octet |
|---|---|
| Balayages | 47 999 sur 48 000 |
| HBL, boucle courte | 256 sur 256 |
| HBL, NOP | 256 sur 256 |
| HBL, STOP | 256 sur 256 |
| HBL, MULU | 256 sur 256 |
| VBL | 32 sur 32 |

À 60 Hz, le même résultat : une lecture différente sur 49 000 et HBL à 256 sur 256, avec `NEOST_ECLOCK_PHASE` à 8. La phase à la sortie du reset est une constante libre entre les deux, comme le tirage sur une machine réelle : une seule valeur donne ces tables, et le banc l'impose. Le tableau ci-dessus est à 50 Hz.

**Le compteur vidéo et la phase de l'E-clock concordaient déjà** : 47 999 sur 48 000 dès le premier passage, et `NEOST_ECLOCK_PHASE` à toute autre valeur que 0 en casse plus de 12 000. L'unique écart est la première lecture après un réveil, au tout début de la deuxième table : 166 contre 168, soit 4 cycles, non expliqué.

**La prise d'interruption ne concordait pas.** Avant correction : boucle courte 216 sur 256, MULU 63 sur 256. NeoST, comme Hatari, levait l'IPL au dispatch d'événement, à la frontière d'instruction, et la commitait aussitôt. Le RTL échantillonne la broche à un point précis de l'instruction, avant le long calcul de MULU : une IRQ qui monte après ce point attend l'instruction suivante.

| Interruption | Avant, comme Hatari | RTL, et NeoST maintenant sur STE |
|---|---|---|
| Broche | Levée au dispatch d'événement, frontière d'instruction | Levée au cycle exact, en cours d'instruction (`armHblPinAt`), vue par `POLL_IPL` |
| Commit | HBL et VBL commitées au dispatch | Aucun, sauf HBL pendant un STOP |

Exception depuis le 2026-11-04 : hors STOP, la VBL du STE se prend au dispatch, car l'intro de Vroom `[cr Elite]` se fige à la broche. Les verdicts du banc n'ont pas bougé.

Le mécanisme existait, en essai (`NEOST_PIN_ARM`), écarté le 2026-07-02 parce qu'il s'écartait d'Hatari. Ce banc mesure leur désaccord. `POLL_IPL` de Moira concorde avec le témoin sur ces cas : le « +3 » de `FX68K_BENCH.md` n'apparaît pas dans NeoST à cette résolution de 4 cycles.

## Le pixel : une écriture du CPU, la trame qui sort

`tools/run_stcpu_raster.py` : le programme affiche un écran pseudo-aléatoire, se cale sur le compteur sans gigue, et pose une impulsion 60 Hz à une date de la ligne, une date par exécution. Chaque pixel hors BLANK est comparé. **60 dates sur 64 donnent une trame identique au pixel**, sur 113 568 pixels.

**Les 4 autres posent une question sans réponse.** Le modèle réagit à `$FF820A` plus tôt que NeoST : à la chute d'AS + 5 (`tools/run_glue_sweep.py`), alors que NeoST, comme Hatari, date cette écriture à AS + 8. Avancer de 4 donne 62 dates sur 64, **et casse des démos écrites sur de vrais STE** : DHS More or Less Zero à 77 % de pixels faux à la trame 4000, Double Rez Trouble à 10 %, contre 0 % et 4,52 % d'arrondis. Le logiciel réel prime : la date reste celle d'Hatari, et l'écart reste à expliquer. Tranche : une capture AS et HSYNC sur un STE. → `TODO.md` § 2.2

**Haute résolution hors verdict.** Après une impulsion, le Shifter du modèle (bloc « moyen ») garde une phase de 8 pixels. Le GLUE et le compteur n'y sont pour rien, et `GLUE_SWEEP.md` les compare sans passer par le pixel.

**D'autres écritures, hors verdict** (`--kind`, 64 dates chacune) :

| Écriture | Dates identiques | Ce que fait le modèle, et pas NeoST |
|---|---|---|
| `$FF820F` (`offset`) | 64 sur 64 | Identique depuis la coupure 8 cycles plus tôt, sans effet sur Pacemaker, More or Less Zero et Double Rez Trouble |
| Couleur 0 (`palette`) | 37 sur 64 | La couleur change 4 pixels plus tôt, en bordure comme dans l'image. **Le modèle a tort** : 13 images Spectrum 512 réelles sortent exactes à 0 pixel avec NeoST, et de 10 à 1 118 pixels faux avec ce décalage (`tools/run_spec512_truth.py`). |
| `$FF8265` en plein DE (`scroll`) | 21 sur 64 | Les pixels se décalent aussitôt, le temps de l'impulsion ; NeoST attend la ligne suivante |
| `$FF8207` en plein DE (`counter`) | 23 sur 64 | Le reste de la ligne vient de la nouvelle adresse ; NeoST attend la ligne suivante, comme Hatari. Hors DE : identique. |

## Le compteur du son DMA

`tools/make_stcpu_snd.py` lit `$FF890D` en boucle, son en lecture. Sur le modèle, le compteur avance **mot par mot dès la fin de DE**, et plus du tout pendant DE. NeoST et Hatari le faisaient bondir d'un coup au HBL. [B] ST-4081 feuille 20 : `SNDEN` = `HDE1` latché bas et `SREQ`.

Sur STE, `DmaSound::liveCounter` refait maintenant le plein à la lecture quand le faisceau est hors de DE. Marches par ligne, 50 kHz stéréo : 1538 contre 1650 sur le modèle, 750 avant. La valeur n'est pas comparée : le modèle a une FIFO de 7 mots par choix d'auteur (`gstshifter.v:414-419`), et NeoST garde les 8 octets d'Hatari. Le banc vérifie la forme : aucune marche pendant 288 cycles au moins, des deux côtés.

**Fin de trame** (2026-11-12) : le fetch du dernier mot a son événement, `Scheduler::DMASND` (`DmaSound::armFrameEnd`). Il est daté à l'instant où le DAC a libéré la place des octets restants, repoussé à la fin de DE ; XSINT, Timer A et GPIP7 suivent. Avant, au HBL : jusqu'à 132 cycles (50 Hz) ou 136 (60 Hz) de retard. Table de vérité : `neost-selftest`, section son DMA, cas 9 (trame mono de 64 octets finie au cycle 9080 au lieu de 9212). La date dépend encore de la profondeur de FIFO (8 octets, choix d'auteur, [F]). Le Mega STE garde la fin au HBL.

**Limite** : les fetchs intermédiaires restent groupés au HBL (ou à la lecture du compteur, ou à l'événement de fin) ; seul un programme qui réécrit le tampon dans la ligne même du fetch verrait la différence.

## Ce que ça ne dit pas

- **ST et Mega STE** gardent le commit au dispatch, à 0 px d'Hatari : aucun modèle ne les couvre. `[F]` Le 68000 est le même, le GLUE non : rien ne dit qu'ils s'écartent, rien ne le réfute.
- **MFP** : `Vste_tb` n'en a pas, l'IACK vectorisé et le Timer B restent aux dates d'Hatari.
- **Écart accepté avec Hatari** : `timer_b_event`, 14 cycles de première IRQ. Aucun étalon ne bouge sur 55.

## Audit du témoin VBL (2026-11-04)

Les 16/19 PC du test `run_vbl_entry_probe.py` constituent un **désaccord entre
modèles**, pas trois erreurs matérielles établies dans NeoST. Le score 19/19
de la variante préarmée ne prouve pas sa fidélité.

Points vérifiés dans GSTMCU @ `4a6db7cc` : `gstmcu.v:918-926` instancie un
VSYNC asynchrone, mais sa sortie `iivsync_a` ne pilote pas la VBL ; le chemin
actif est la réécriture synchrone `iivsync` (`gstmcu.v:930-962`). Le verrou VBL
(`gstmcu.v:322`) passe par `register.v`, dont l'en-tête annonce un retard d'un
demi-cycle de l'horloge de simulation. Il s'agit de choix d'implémentation à
contrôler, pas d'une transcription électrique exacte garantie par le schéma.

Contre-épreuve isolée : remplacer **seulement** le verrou VBL par le registre
sur front natif présenté en commentaire dans `register.v:18-26`, reconstruire
le témoin puis rejouer la même ROM. **Les 19 PC sont inchangés** ; ce retard
ne suffit donc pas à expliquer les trois écarts de cette sonde. Sources,
binaire et résultat dans `build/tos206-vbl/rtl-audit/`.

Restent à vérifier : la réécriture VSYNC, les phases d'horloge/reset et
l'échantillonnage IPL de fx68k. Le blocage Vroom contredit la compatibilité de
la variante NeoST, mais ne démontre pas à lui seul une erreur du RTL : une autre
interaction de NeoST peut être en cause. La collision TOS reste ouverte.

## Audit contradictoire de la chaîne VBL (2026-11-05)

**Contrainte matérielle rappelée par l'utilisateur : les jeux fonctionnent sur
le vrai matériel.** Un correctif qui bloque Vroom Elite pour obtenir 19/19 au
banc est rejeté. Le résultat du banc ne prévaut pas sur ce fonctionnement.
Il reste à localiser la responsabilité entre le témoin, son intégration et
NeoST ; les 3 PC différents ne désignent pas trois fautes matérielles établies.

### Sources relues

- **[B] Atari ST-4081, révision A, 27 octobre 1988, feuille 28**, blocs
  PS036/PS037/PS038/PS039 : VINTB mémorisé, remise à un par VCLRB et les resets,
  chemin depuis la synchro verticale. La structure logique du verrou RTL est
  cohérente avec cette feuille ; ses délais de propagation électriques ne sont
  pas validés. `ST4081S.PDF`, SHA-256
  `935c4dd9a09a9af8f738276ccfeb32ee8d8e254a8d0985dad57a23326fcdfc9f`.
- **[B] Motorola M68000 User's Manual, rév. 8 (1993), § 3.5 et § 6.3.2** :
  IPL reste demandé jusqu'à IACK, la prise d'exception se décide entre
  instructions, le PC sauvé désigne l'instruction qui aurait été exécutée.
  § 5, figure 5-37, pp. 5-38/39 : synchronisation des entrées asynchrones.
  Ces passages ne fixent pas le point interne de sondage pour chacun des
  opcodes de notre boucle. SHA-256 du PDF :
  `e41cbe7e14dc7cb853f1185adb1dcd7043d2a2a3f43cdc909e0f6c146007a8e5`.
- **[D] GSTMCU @ 4a6db7cc** : `gstmcu.v:168-175,314-322,918-962`,
  `register.v:18-43` ; **fx68k @ 0602ee46**, `fx68k.sv:219-224` : deux
  étages IPL sur `enPhi2`. Le câblage `enPhi1/enPhi2` du banc correspond au
  mode 8 MHz de `atarist_sdram.sv:490-491,525-555 @ a00deb5b` ; cela confirme
  l'intégration prévue par cet autre modèle, pas sa phase électrique réelle.

### Contre-épreuves séparées

Recette : `python3 tools/audit_vbl_witness.py`. Les variantes sont reconstruites
dans `build/vbl-witness-audit/`, sans modifier les dépendances ni le cœur.
`report.json` garde commandes, paramètres, révisions, empreintes et tables ;
`signals.txt` relève VSYNC synchrone/asynchrone, IPL2 et les deux étages de fx68k.
Les captures de cette passe sont dans `build/vbl-witness-audit-verified/`.

| Contre-épreuve | Observation |
|---|---|
| Verrou VBL natif au lieu de `register.v` | 19 PC inchangés |
| VSYNC asynchrone à la place de la réécriture synchrone, reset explicite | 19 PC inchangés |
| Les deux substitutions, reset explicite | 19 PC inchangés |
| Durée de reset allongée de 1, 2 ou 3 ticks à 32 MHz | 19 PC inchangés |
| Ancienne initialisation + VSYNC asynchrone | 6 PC changés ; artefact de reset isolé ci-dessous |
| NeoST, phases E-clock 0/2/4/6/8 | Respectivement 16/5/14/9/8 PC identiques au témoin |

**Défaut confirmé du harnais, corrigé.** Le pilote posait directement les resets
à zéro. Le bloc VSYNC asynchrone, sensible au front de reset et à son horloge
verticale, pouvait donc garder le zéro d'initialisation de Verilator. Au tick
33 après la sortie du reset, sa montée créait une VBL initiale absente du chemin
synchrone. Les VSYNC concordaient ensuite sur toute la capture. Produire un
front explicite 1 → 0 à l'initialisation supprime ces six écarts ; le témoin
synchrone courant garde exactement sa table précédente. Ce défaut explique
la fausse divergence de la variante asynchrone, **pas** les trois écarts NeoST.

Le pilote refusait aussi trop peu d'exécutions invalides : ROM tronquée,
arguments/range RAM invalides, CPU arrêté ou nombre de VSYNC non atteint.
Il rend désormais un échec avant de produire le dump, et vérifie son écriture.
`tools/test_stcpu_driver.py` couvre sept arguments invalides, une ROM tronquée
et une ROM qui provoque une double faute. Une capture interrompue n'est pas
une référence réussie.

**Lecture des signaux.** Dans la capture normale, la VBL observée au tick
641041 atteint `rIpl` au tick 641045 puis `iIpl` au tick 641049 : un cycle CPU
par étage dans ce câblage. Les deux VSYNC sont identiques avec le reset
explicite. Cela vérifie le fonctionnement du modèle ; cela ne mesure pas le
point de reconnaissance du véritable 68000.

**Limites conservées.** La sonde compare des chronologies cumulées, pas 19
mesures indépendantes du silicium. Elle remplace MOVEA vers A7 par MOVEA vers
A4 pour garder une pile sûre, n'a ni MFP ni FDC, et n'exécute ni TOS ni Vroom.
L'horloge et les buffers idéaux ne représentent pas les temps de propagation
réels. Le balayage E-clock montre une sensibilité, et ne sert pas à choisir
une phase qui maximise le score. Le correctif de la collision TOS reste ouvert ;
aucun comportement du cœur n'est changé par cet audit.

Validation de la passe : les neuf cas de refus de `test_stcpu_driver.py`
passent (trois tests, sept sous-cas d'arguments) ; `run_stcpu_bench.py` est
vert contre ses références inchangées, PAL/NTSC et son DMA compris. Les
variantes reconstruites sont comparées dans `report.json`, sans verdict de
fidélité matérielle. Aucune conclusion nouvelle sur le rendu des jeux.
