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

**2546 impulsions sur 2560 concordent**, largeurs 8 à 32 pour la haute résolution, 8 à 44 pour le 60 Hz, 8 à 32 pour la moyenne résolution, 8 à 44 pour le va-et-vient haute puis moyenne. Cela couvre les retraits usuels : gauche (`LEFT_OFF`, 186 octets),
gauche court (`LEFT_OFF_2_STE`, 180 octets), droite (204 octets), `STOP_MIDDLE`, `LEFT_PLUS_2`, `RIGHT_MINUS_2`, `NO_DE`, `BLANK`, ligne de 224 et de 508 cycles.

Trois corrections en sont sorties, toutes sur STE :

| Écart | Avant | Modèle, et NeoST maintenant | Hatari @ `f0736b24` |
|---|---|---|---|
| Fin de la fenêtre 60 Hz, `Line_Set_Pal`, relativement aux autres fenêtres 60 Hz | 56 | 58 | 56 |
| Haute résolution posée à 4 | `LEFT_OFF` | Ligne normale | `LEFT_OFF` |
| Haute résolution posée dans (40, 48], retirée à 56 au plus tard | `NO_DE` | Ligne affichée | `NO_DE` |

Aucun étalon ne bouge, ce qui dit aussi qu'aucun étalon n'exerçait ces phases.

## Sur un écran entier à 60 Hz

Une seconde passe rejoue tout sur une trame de base à 60 Hz (`NEOST_GLUE_60`, référence `tests/reference/glue_sweep_60.json`). L'impulsion de fréquence y devient un retour à 50 Hz. Les lignes
d'expérience s'arrêtent plus tôt, parce que la fenêtre verticale aussi.

**2518 impulsions sur 2560 concordent.** La bascule de fréquence et la moyenne résolution n'ont plus aucun écart. Le balayage avait trouvé un défaut hérité : les constantes d'octets d'Hatari
supposent une ligne de 56 à 376, alors qu'elle va de 52 à 372 à 60 Hz.

| Sur un écran entier à 60 Hz | Modèle, et NeoST maintenant | Hatari @ `f0736b24` |
|---|---|---|
| Écriture de la fréquence, sans effet sur la ligne | 160 octets | `RIGHT_MINUS_2`, 158 octets : le compteur dérive |
| Départ à 56, fin à 372 | 158 octets | 160 |
| Départ à 52, fin à 376 | 162 octets | 160 |
| `STOP_MIDDLE` de 52 à 164 | 56 octets | 54 |
| Gauche retirée, fin à 372 | 184 octets | 186 |

Les 42 écarts restants sont tous en haute résolution, les fenêtres du modèle y sont décalées de quelques cycles. Aucun programme connu n'y retire une bordure à 60 Hz. Ils restent inscrits.

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
