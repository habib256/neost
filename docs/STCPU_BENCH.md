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

**Le compteur vidéo et la phase de l'E-clock concordaient déjà** : 47 999 sur 48 000 dès le premier passage, et `NEOST_ECLOCK_PHASE` à toute autre valeur que 0 en casse plus de 12 000. L'unique écart est la première lecture après un réveil, au tout début de la deuxième table : 166 contre 168, soit 4 cycles, non expliqué.

**La prise d'interruption ne concordait pas.** Avant correction : boucle courte 216 sur 256, MULU 63 sur 256. NeoST, comme Hatari, levait l'IPL au dispatch d'événement, à la frontière d'instruction, et la commitait aussitôt. Le RTL échantillonne la broche à un point précis de l'instruction, avant le long calcul de MULU : une IRQ qui monte après ce point attend l'instruction suivante.

| Interruption | Avant, comme Hatari | RTL, et NeoST maintenant sur STE |
|---|---|---|
| Broche | Levée au dispatch d'événement, frontière d'instruction | Levée au cycle exact, en cours d'instruction (`armHblPinAt`), vue par `POLL_IPL` |
| Commit | HBL et VBL commitées au dispatch | Aucun, sauf HBL pendant un STOP |

Le mécanisme existait, en essai (`NEOST_PIN_ARM`), écarté le 2026-07-02 parce qu'il s'écartait d'Hatari. Ce banc le départage. `POLL_IPL` de Moira est donc confirmé sur ces cas : le « +3 » de `FX68K_BENCH.md` n'apparaît pas dans NeoST à cette résolution de 4 cycles.

## Le pixel : une écriture du CPU, la trame qui sort

`tools/run_stcpu_raster.py` : le programme affiche un écran pseudo-aléatoire, se cale sur le compteur sans gigue, et pose une impulsion 60 Hz à une date de la ligne, une date par exécution. Chaque pixel hors BLANK est comparé. **60 dates sur 64 donnent une trame identique au pixel**, sur 113 568 pixels.

**Les 4 autres posent une question sans réponse.** Le modèle réagit à `$FF820A` plus tôt que NeoST : à la chute d'AS + 5 (`tools/run_glue_sweep.py`), alors que NeoST, comme Hatari, date cette écriture à AS + 8. Avancer de 4 donne 62 dates sur 64, **et casse des démos écrites sur de vrais STE** : DHS More or Less Zero à 77 % de pixels faux à la trame 4000, Double Rez Trouble à 10 %, contre 0 % et 4,52 % d'arrondis. Le logiciel réel prime : la date reste celle d'Hatari, et l'écart reste à expliquer. Tranche : une capture AS et HSYNC sur un STE. → `TODO.md` § 2.2

**Haute résolution hors verdict.** Après une impulsion, le Shifter du modèle (bloc « moyen ») garde une phase de 8 pixels. Le GLUE et le compteur n'y sont pour rien, et `GLUE_SWEEP.md` les compare sans passer par le pixel.

**D'autres écritures, hors verdict** (`--kind`, 64 dates chacune) :

| Écriture | Dates identiques | Ce que fait le modèle, et pas NeoST |
|---|---|---|
| `$FF820F` (`offset`) | 62 sur 64 | Deux dates, 16 cycles d'écart sur la fin de fenêtre |
| Couleur 0 (`palette`) | 37 sur 64 | La couleur change 4 pixels plus tôt, en bordure comme dans l'image. Le Shifter est un bloc « moyen » : le RTL n'y prévaut pas, et aucune image réelle ne tranche à 4 pixels. |
| `$FF8265` en plein DE (`scroll`) | 21 sur 64 | Les pixels se décalent aussitôt, le temps de l'impulsion ; NeoST attend la ligne suivante |
| `$FF8207` en plein DE (`counter`) | 23 sur 64 | Le reste de la ligne vient de la nouvelle adresse ; NeoST attend la ligne suivante, comme Hatari. Hors DE : identique. |

## Ce que ça ne dit pas

- **ST et Mega STE** gardent le commit au dispatch, à 0 px d'Hatari : aucun modèle ne les couvre. `[F]` Le 68000 est le même, le GLUE non : rien ne dit qu'ils s'écartent, rien ne le réfute.
- **MFP** : `Vste_tb` n'en a pas, l'IACK vectorisé et le Timer B restent aux dates d'Hatari.
- **Écart accepté avec Hatari** : `timer_b_event`, 14 cycles de première IRQ. Aucun étalon ne bouge sur 55.
