# Balayage GLUE — la machine de NeoST contre le modèle GSTMCU

(c) 2026 VERHILLE Arnaud. **Une impulsion 60 Hz ou haute résolution à chaque phase de la ligne, et ce que chaque côté en fait.**
Posé le 2026-11-04. Instrument : `tools/run_glue_sweep.py`.

- Échelon : `rtl`, grade **D** ([`REFERENCE_LADDER.md`](REFERENCE_LADDER.md)). Bloc « fort », schémas de Christian Zietz. Un accord dit « d'accord avec
  un modèle du circuit ». **STE seulement** : ni le ST ni le Mega STE n'ont de modèle. Le ST garde la table d'Hatari.
- Ce qui est comparé : DE (début et fin) et octets consommés sur les deux lignes qui suivent l'impulsion, en cycles dans une même grille. Pas le pixel, ni ce que HSYNC devient.

## Comment

| Pièce | Rôle |
|---|---|
| `Vste_tb --glue` (`tools/gstmcu_bench.cpp`) | Écritures datées sur le modèle, fronts de DE et compteur vidéo relevés depuis VSYNC. |
| `neost-glue-bare` (`tools/glue_bare_bench.cpp`) | Le même plan rejoué dans `Shifter::replayGlue`, sans CPU. |
| `tools/run_glue_sweep.py` | Compare, et garde les écarts dans `tests/reference/glue_sweep.json`. Palier `full` ; SKIP sans Verilator. |

**Le décalage.** Une écriture du banc est datée à la chute d'AS. Sa date dans la machine GLUE vaut AS + 5 pour `$FF820A`, AS + 8 pour `$FF8260`, qui attend aussi le créneau de 4 cycles : `--offsets` les a mesurés, en cherchant le minimum d'écarts par phase.
Le chemin CPU de NeoST date les deux à AS + 8. Pour `$FF8260`, c'est donc la même date. Pour `$FF820A`, le modèle réagit 3 cycles avant NeoST : une question ouverte, que de vraies démos empêchent de trancher en faveur du modèle. → [`STCPU_BENCH.md`](STCPU_BENCH.md)

**HSYNC parasite.** Une impulsion haute résolution vers le cycle 28 fait retomber HSYNC une fois de plus, sans commencer de ligne : DE reste en place, et le banc l'ignore
dans son comptage. NeoST ne modélise pas ce front, qu'un programme ne peut pas lire. Effet sur un moniteur réel inconnu.

## Résultat (2026-11-04)

**1654 impulsions sur 1664 concordent**, largeurs 8 à 32 pour la haute résolution, 8 à 44 pour le 60 Hz. Cela couvre les retraits usuels : gauche (`LEFT_OFF`, 186 octets),
gauche court (`LEFT_OFF_2_STE`, 180 octets), droite (204 octets), `STOP_MIDDLE`, `LEFT_PLUS_2`, `RIGHT_MINUS_2`, `NO_DE`, `BLANK`, ligne de 224 et de 508 cycles.

Trois corrections en sont sorties, toutes sur STE :

| Écart | Avant | Modèle, et NeoST maintenant | Hatari @ `f0736b24` |
|---|---|---|---|
| Fin de la fenêtre 60 Hz, `Line_Set_Pal`, relativement aux autres fenêtres 60 Hz | 56 | 58 | 56 |
| Haute résolution posée à 4 | `LEFT_OFF` | Ligne normale | `LEFT_OFF` |
| Haute résolution posée dans (40, 48], retirée à 56 au plus tard | `NO_DE` | Ligne affichée | `NO_DE` |

Aucun étalon ne bouge, ce qui dit aussi qu'aucun étalon n'exerçait ces phases.

## Les écarts restants, inscrits en référence

| Impulsion | Modèle | NeoST |
|---|---|---|
| Haute résolution posée à 44 | DE dès 44, 6 octets de plus | Ligne normale |
| Haute résolution à 44, large de 16 ou plus | DE de 44 à 176, puis ligne de 224 | Ligne de 224 sans DE |
| Haute résolution posée à 24-28, large de 32 | DE à 120 | Sans DE |
| Haute résolution posée à 456-460 ou 512, large de 32 | DE courte sur la ligne suivante | Sans DE, ou retrait gauche entier |

Au-delà de ces largeurs, une impulsion enjambe HSYNC et le rechargement du compteur : les écarts se multiplient, hors du verdict. Aucun logiciel connu ne les emploie.

**Fin de DE du retrait droit** : 464 dans le modèle, 460 chez NeoST (`HSync_On_Off_Low`) pour les mêmes 44 octets, soit la largeur juste chez le modèle. Hors du cadre rendu, qui s'arrête au
cycle 424. Tolérance écrite dans l'outil, code inchangé.
