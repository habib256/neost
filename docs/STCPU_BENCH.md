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

Le mécanisme existait, en essai (`NEOST_PIN_ARM`), écarté le 2026-07-02 parce qu'il s'écartait d'Hatari. Ce banc le départage. `POLL_IPL` de Moira est donc confirmé sur ces cas : le « +3 » de `FX68K_BENCH.md` n'apparaît pas dans NeoST à cette résolution de 4 cycles.

## Le pixel : une écriture du CPU, la trame qui sort

`tools/run_stcpu_raster.py` : le programme affiche un écran pseudo-aléatoire, se cale sur le compteur sans gigue, et pose une impulsion 60 Hz à une date de la ligne, une date par exécution. Chaque pixel hors BLANK est comparé. **60 dates sur 64 donnent une trame identique au pixel**, sur 113 568 pixels.

**Les 4 autres posent une question sans réponse.** Le modèle réagit à `$FF820A` plus tôt que NeoST : à la chute d'AS + 5 (`tools/run_glue_sweep.py`), alors que NeoST, comme Hatari, date cette écriture à AS + 8. Avancer de 4 donne 62 dates sur 64, **et casse des démos écrites sur de vrais STE** : DHS More or Less Zero à 77 % de pixels faux à la trame 4000, Double Rez Trouble à 10 %, contre 0 % et 4,52 % d'arrondis. Le logiciel réel prime : la date reste celle d'Hatari, et l'écart reste à expliquer. Tranche : une capture AS et HSYNC sur un STE. → `TODO.md` § 2.2

**Haute résolution hors verdict.** Après une impulsion, le Shifter du modèle (bloc « moyen ») garde une phase de 8 pixels. Le GLUE et le compteur n'y sont pour rien, et `GLUE_SWEEP.md` les compare sans passer par le pixel.

**D'autres écritures, hors verdict** (`--kind`, 64 dates chacune) :

| Écriture | Dates identiques | Ce que fait le modèle, et pas NeoST |
|---|---|---|
| `$FF820F` (`offset`) | 64 sur 64 | Identique depuis la coupure 8 cycles plus tôt, sans effet sur Pacemaker, More or Less Zero et Double Rez Trouble |
| Couleur 0 (`palette`) | 37 sur 64 | La couleur change 4 pixels plus tôt, en bordure comme dans l'image. **Le modèle a tort** : 13 images Spectrum 512 réelles sortent exactes à 0 pixel avec NeoST, et 812 à 1 344 pixels faux avec ce décalage (`tools/run_spec512_truth.py`). |
| `$FF8265` en plein DE (`scroll`) | 21 sur 64 | Les pixels se décalent aussitôt, le temps de l'impulsion ; NeoST attend la ligne suivante |
| `$FF8207` en plein DE (`counter`) | 23 sur 64 | Le reste de la ligne vient de la nouvelle adresse ; NeoST attend la ligne suivante, comme Hatari. Hors DE : identique. |

## Le compteur du son DMA

`tools/make_stcpu_snd.py` lit `$FF890D` en boucle, son en lecture. Sur le modèle, le compteur avance **mot par mot dès la fin de DE**, et plus du tout pendant DE. NeoST et Hatari le faisaient bondir d'un coup au HBL. [B] ST-4081 feuille 20 : `SNDEN` = `HDE1` latché bas et `SREQ`.

Sur STE, `DmaSound::liveCounter` refait maintenant le plein à la lecture quand le faisceau est hors de DE. Marches par ligne, 50 kHz stéréo : 1538 contre 1650 sur le modèle, 750 avant. La valeur n'est pas comparée : le modèle a une FIFO de 7 mots par choix d'auteur (`gstshifter.v:414-419`), et NeoST garde les 8 octets d'Hatari. Le banc vérifie la forme : aucune marche pendant 288 cycles au moins, des deux côtés.

**Limite** : la fin de trame et son IRQ restent datées au HBL, sauf si le programme lit le compteur. Le plein hors de DE n'a pas son propre événement. Écart d'au plus 136 cycles, non mesuré sur un logiciel.

## Ce que ça ne dit pas

- **ST et Mega STE** gardent le commit au dispatch, à 0 px d'Hatari : aucun modèle ne les couvre. `[F]` Le 68000 est le même, le GLUE non : rien ne dit qu'ils s'écartent, rien ne le réfute.
- **MFP** : `Vste_tb` n'en a pas, l'IACK vectorisé et le Timer B restent aux dates d'Hatari.
- **Écart accepté avec Hatari** : `timer_b_event`, 14 cycles de première IRQ. Aucun étalon ne bouge sur 55.
