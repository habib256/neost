# Piloter NeoST depuis l'extérieur (exploration d'états, fuzzing, Go-Explore)

(c) 2026 VERHILLE Arnaud — projet NeoST.

Ce document décrit comment un **programme tiers** (planner, fuzzer, explorateur d'états)
peut conduire `neost-headless` de façon **reproductible** : injecter des entrées trame par
trame, observer quelques variables, sauvegarder puis reprendre un état, recommencer.

Le besoin vient d'un usage réel : brancher un moteur d'exploration d'états
([OpenDST](https://github.com/pingidentity/opendst), Yannick Lecaillez) sur un jeu Atari ST
émulé par NeoST, façon [Go-Explore](https://www.uber.com/fr/en/blog/go-explore/) — archive
de cellules, critère de nouveauté, reprise depuis la meilleure cellule connue. Les outils
décrits ici sont génériques : ils servent tout autant nos propres campagnes de fuzzing.

⚠ **Aucune ROM TOS ni image de jeu ne peut être fournie avec NeoST.** EmuTOS mis à part
(livré, libre), il faut ses propres dumps. Les recettes ci-dessous nomment des fichiers qui
ne sont pas redistribuables.

## 1. Le contrat de déterminisme

NeoST est déterministe **à configuration figée**. Le déterminisme n'est pas une propriété
absolue de l'émulateur, c'est une propriété du couple (binaire, configuration). À figer,
une fois pour toutes, pour toute la durée d'une campagne :

| À figer | Pourquoi |
|---|---|
| la révision de NeoST (un tag, pas `main`) | le projet bouge vite ; une datation qui change déplace tout |
| `--machine`, `--mem`, la ROM TOS | `adjustMachineForTos` peut rétrograder la machine ; la ROM fixe aussi 50/60 Hz |
| l'image disque, `--fastfdc` | le FDC rapide change les temps de chargement, donc les trames |
| l'absence de tout accès hôte | voir ci-dessous |

**Interdit pendant une campagne** — ces options branchent le monde extérieur, donc
l'horloge murale et le réseau, et détruisent la reproductibilité : `--slirp`,
`--slirp-restricted`, `--modem`, `--midi-net`, `--ethernec`/`--netusbee` avec un vrai
backend, et l'horloge de l'UltraSatan (`--ultrasatan`). Elles sont toutes **OFF par
défaut** : il suffit de ne pas les demander.

Vérification en une commande, à refaire après tout changement de configuration :

```sh
./build/neost-headless <rom> --machine st --disk <image> --fastfdc \
    --frames 900 --save-state-test
# → [save-state-det] screen OK (identical) | re-serialized state OK (identical)
```

Ce test fait le vrai travail : il exécute N trames, sauvegarde, poursuit 200 trames, puis
**restaure et rejoue les mêmes 200 trames**. L'état re-sérialisé ET l'écran doivent être
identiques — c'est exactement la primitive dont dépend une archive de cellules.

## 2. Ce que ça vaut, mesuré

Mesures relevées le 2026-09-04 sur un cœur (Linux x86-64, build Release), sur le boot d'un
jeu réel (Rick Dangerous, 900 trames, `--fastfdc`) :

| Grandeur | Valeur |
|---|---|
| Une trame | **1,45 ms** (EmuTOS au bureau comme Rick Dangerous en jeu — mesuré dans les deux cas) |
| Reproductibilité | deux exécutions → PPM **et** dump de 32 Ko de RAM au **même md5** |
| Save-state | **1,35 Mo** en 512 Ko de RAM (non compressé) |
| Reprise d'un état | **5,1 ms** (`load` en mémoire) — était 21,6 ms avant l'optimisation du CRC |
| Sauvegarde d'un état | **3,1 ms** — était 10,2 ms |
| Itération « reprise + 60 trames + sauvegarde » | 5,1 + 60 × 1,45 + 3,1 ≈ **95 ms**, dont **91 %** d'émulation pure |

(Mesures du 2026-09-05 sur le code publié, machine au repos ; les chiffres « était » viennent
de la lignée précédente, même code.)

Une itération d'exploration coûte donc quelques dizaines de millisecondes, dominées par
l'émulation elle-même. Les itérations sont indépendantes : elles se parallélisent en
lançant N processus (ou N serveurs).

**Où est passé le temps.** Le `load` coûtait 21,6 ms, soit *quinze trames émulées*, pour
un état de 1,35 Mo. La cause n'était ni le disque ni le processus : le CRC-32 du format
était calculé **bit à bit**, et `loadState` en payait deux plus une sérialisation complète
(le filet de sécurité qui permet de rejouer l'état courant si la restauration échoue en
cours de route). Table de CRC + sauvegarde de filet sérialisée sans CRC — mêmes valeurs,
même format, les états déjà écrits restent lisibles — et la reprise tombe à 3,9 ms. Le
mode GUI (F7/F8) en profite autant.

## 3. Injecter les entrées

### Scripts joystick — `--joy-script N "SCRIPT"`, `--joy-script-file N FICHIER`

Un token = une trame, à partir de la trame N. Grammaire complète et raisons du choix dans
[`src/util/JoyScript.hpp`](../src/util/JoyScript.hpp) :

| Token | Effet |
|---|---|
| `U` `D` `L` `R` `F` | haut, bas, gauche, droite, feu — une trame |
| `.` | neutre, une trame |
| `[UF]` `[DL]` | **combinaison** sur une trame : feu + direction, diagonales |
| `[$88]` `[0x88]` | la même trame en masque brut (préfixe `$`/`0x` **obligatoire** : sans lui, `DF` est bas+feu, pas la valeur `$DF`) |
| `TOKEN*N` | répète le token N fois **au total** (`R*30` = 30 trames à droite) |

Blancs et sauts de ligne ignorés, `#` commente jusqu'à la fin de la ligne. Un script fautif
est **refusé avant le démarrage de la machine**, avec un message — pas silencieusement
traduit en « neutre » comme le faisait l'ancien parseur.

Les combinaisons ne sont pas un confort : sans elles, un jeu d'action est injouable. Dans
Rick Dangerous, le tir est feu+haut et la dynamite feu+bas ; l'ancien script ne posait
qu'un bit à la fois et ne pouvait donc produire ni l'un ni l'autre.

Autres injections datées : `--joy-at N MASQUE` (état tenu), `--keys-at N "STR"`,
`--key-down`/`--key-up` (touche tenue), `--mouse-at N "SCRIPT"`.

## 4. Observer — `--probe`, `--probe-every`, `--hash-ram`

```sh
--probe rick_x=1A34:2 --probe ammo=1A40:1 --probe-every 10 --hash-ram 1A00:200
```

Émet sur **stdout** une ligne par échantillon, format stable et bête à analyser :

```
probe frame=120 screen=b8226b05dc894991 ram=5c1c18311829437f rick_x=0x0058 ammo=0x06
```

- `NOM=ADRESSE:LONGUEUR` — longueur 1, 2 ou 4 octets, big-endian (68000) ; adresse en hexa
  (`$`/`0x` optionnels) ; `NOM=` optionnel. Option **répétable**.
- `screen=` — FNV-1a 64 bits du framebuffer décodé : une **clé de cellule** prête à
  l'emploi, sans sortir la moindre image.
- `ram=` (`--hash-ram ADR:LEN`, les deux en hexa) — même hachage sur une tranche de RAM :
  clé plus fine que l'écran, qui ignore l'état caché.
- L'échantillon de la trame `N` est l'état **après N trames exécutées**, avant les
  injections de la trame courante — c'est ce que « voyait » la machine au moment où le
  pilote a décidé de l'entrée suivante. Un dernier échantillon est toujours émis à la fin
  du run, sur l'état que `--save-state` grave.

**Les sondes sont sans effet de bord** : lecture par `Bus::peek8`, comme un débogueur — ni
dispatch MMIO, ni wait state, ni bus error. Une sonde ne doit rien changer à ce qu'elle
observe, sinon elle détruit le déterminisme qu'elle sert à mesurer. Contrepartie assumée :
**l'espace I/O `$FF8000+` n'est pas sondable** (il se lit `$FF`). Pour observer une puce,
c'est `--dump-at` ou `--trace`.

Vérifié : `--probe-every 1` sur 900 trames, avec hachage de 512 Ko de RAM à chaque trame,
rend **exactement la même capture d'écran** qu'un run sans sonde.

⚠ Les journaux vont sur **stderr**, les données sur **stdout** : un pilote externe lit
stdout au fil de l'eau (la sortie est vidée à chaque ligne). Ne pas combiner avec
`--trace -`, qui écrit aussi sur stdout.

## 5. Le mode serveur — `--server`

`--server` remplace la boucle `--frames` par une **boucle de commandes** : une commande par
ligne sur stdin, une réponse par ligne sur stdout (`ok …` ou `err …`). Le processus vit, la
ROM n'est chargée qu'une fois, et les états tiennent dans des **emplacements en mémoire**.

| Commande | Effet |
|---|---|
| `hello` | identité de la configuration figée : version, machine, RAM, médias |
| `run N` | exécute N trames, entrées inchangées |
| `play SCRIPT` | script joystick (même grammaire qu'`--joy-script`) : un masque par trame |
| `joy P1 [P0]` | état joystick **tenu** (masques hexa) |
| `key make\|break SC` | touche, scancode ST en hexa (`39` = espace) |
| `mouse DX DY BTN` | souris relative ; `BTN` bit 0 = gauche, bit 1 = droite |
| `peek ADR LEN` | LEN octets (≤ 4096) en hexa, sans effet de bord |
| `observe` | un échantillon **sans avancer d'une trame** |
| `save N` / `load N` | emplacement d'état en mémoire (`--server-slots`, 64 par défaut) |
| `export N FICHIER` | grave un emplacement — le fichier est relisible par `--load-state` |
| `import N FICHIER` | charge un fichier d'état dans un emplacement |
| `probe SPEC` | ajoute une sonde à chaud (`NOM=ADR:LEN`) |
| `shot FICHIER.ppm` | capture d'écran |
| `slots` | occupation des emplacements |
| `quit` | fin de session (la fin de stdin fait pareil) |

`run`, `play`, `load` et `observe` répondent avec **les champs d'observation** (`frame=`,
`screen=`, `ram=`, sondes) : un rollout entier ne coûte qu'un aller-retour.

Deux précisions de contrat : `hello` est **informatif** — ses chemins peuvent contenir des
espaces, ne pas le découper en `clé=valeur` ; et `import` remet la datation de l'emplacement
à **0** (un fichier ne porte pas de numéro de trame), là où `save`/`load` la conservent.
`play` pilote le port 1 et **neutralise le port 0**, comme `--joy-script`.

```
$ printf 'hello\nplay R*20 [DF]*8\nsave 0\nrun 40\nload 0\nquit\n' \
      | ./build/neost-headless <rom> --machine st --disk <jeu> --fastfdc \
                               --probe x=1A34:2 --server
ok neost=0.5.2 machine=st ram=512k tos=… disk=… fastfdc=1
ok frame=28 screen=7520a6b0409ae583 x=0x0058
ok bytes=1349097
ok frame=68 screen=… x=0x0071
ok frame=28 screen=7520a6b0409ae583 x=0x0058
ok bye
```

**Ce que le serveur fait gagner, mesuré** (itérations par seconde, un cœur) :

| Itération | Ligne de commande | Serveur |
|---|---|---|
| reprise seule | 129/s | **249/s** |
| reprise + 5 trames | 79/s | **109/s** |
| reprise + 60 trames + sauvegarde | 15,1/s | **16,3/s** |

(Rapport mesuré sur la lignée précédente, Rick Dangerous en jeu ; les valeurs absolues du
tableau de § 2 sont plus récentes — c'est le RAPPORT qui compte ici.) Autrement dit : le
serveur gagne franchement sur les **rollouts courts** et sur les boucles qui sauvegardent
beaucoup, et presque rien quand l'émulation domine — ce qui est la vérité et pas une
déception : à 1,45 ms la trame, 60 trames coûtent 87 ms qu'aucune tuyauterie ne fera
disparaître. Le vrai gain de ce lot, pour les deux modes, vient de l'optimisation du
CRC ci-dessus.

**Le contrat, vérifié en CI.** Une session serveur rend exactement ce que rend la boucle
`--frames` : mêmes champs d'observation, même capture d'écran au bit près, et un état
`export`é se relit par `--load-state`. C'est le rôle de `tools/run_server_equiv.py`, câblé
au palier `fast` de `run_all.py` — sans lui, une divergence entre les deux chemins
fausserait toute l'archive d'un pilote, silencieusement, puisque les deux « marchent ».

⚠ Après `play`, le dernier masque du script **reste posé** (comme la boucle `--frames`
quand le script s'épuise) : terminer par `.` pour relâcher. `load` repose l'état joystick
que le client tient, sinon un `joy 80` suivi d'un `load` relâcherait le feu en silence.

## 6. La boucle d'exploration, en ligne de commande

```sh
# 1. Une fois : atteindre le point de départ et le graver
./build/neost-headless <rom> --machine st --disk <jeu> --fastfdc \
    --frames 32000 <injections de boot> --save-state cell0.sav

# 2. N fois : reprendre une cellule, jouer un rollout, observer
./build/neost-headless <rom> --machine st --disk <jeu> --fastfdc \
    --load-state cellK.sav --frames 120 \
    --joy-script 0 "R*40 [DF]*8 .*72" \
    --probe-every 10 --probe x=1A34:2 --hash-ram 1A00:200 \
    --save-state cellK+1.sav
```

Après `--load-state`, **le compteur de trames repart à 0** : `--joy-script 0 …` est donc
relatif au point de reprise, et toutes les injections datées le sont aussi.

## 7. Recette vérifiée — Rick Dangerous jusqu'en jeu

Reproduite le 2026-09-04, `machine=st`, `tos102uk`, image `disks/st/Rick Dangerous
(1989)(Core Design - Firebird)[cr TDA][m EMT][t +4].st` (non redistribuable) :

| Trame | Action | Écran obtenu |
|---|---|---|
| 4000 | `--keys-at 4000 " "` | la cracktro E.M.T. rend la main au **bureau GEM** (fenêtre `A:\` ouverte sur `RICKLOAD.TOS`) |
| 6000 | `--mouse-at 6000 "DD....1.1..........."` | **double-clic** sur `RICKLOAD.TOS` → intro T.D.A. |
| 10000→22000 | `--keys-at <f> " "` tous les 2000 | l'intro passe la main au jeu (**Hall of Fame**) |
| 26000 | `--joy-script-file 26000 pulse.joy` (feu pulsé : `.*36 F*4` répété) | **niveau 1 en jeu** vers la trame 32000 |

Coût : ~40 s pour les 32 000 trames. On grave alors `--save-state rick_l1.sav`, et toute
l'exploration repart de là en ~60 ms par itération.

Le feu **seul** ne fait rien à ce point du niveau, et `D` seul non plus — mais `[DF]`
(dynamite) produit un écran distinct des deux, et `[$82]` donne le **même hachage** que
`[DF]`, aux deux notations près. C'est la preuve de bout en bout que la combinaison
atteint bien la machine.

## 8. Pièges connus

- `--shot-every N PRÉFIXE` prend **deux** arguments : intercaler une autre option entre les
  deux fait passer le préfixe pour l'option et la ROM pour autre chose.
- `--dump-at` lit par `read8` (avec dispatch MMIO) : sur un registre whitelisté en accès
  octet (pads STE `$FF9200`, FDC `$FF8604-07`), la bus error est levée **hors** du
  `try/catch` de Moira et **termine le processus**. Les sondes `--probe`, elles, ne
  peuvent pas tomber dans ce piège (cf. §4).
- La ROM fixe la fréquence de balayage : suffixe `us` → 60 Hz NTSC, `uk`/`fr`/`de`/`es` →
  50 Hz PAL. Ça change l'image ET le nombre de trames par seconde de jeu.
- Un save-state ne se relit qu'avec **la même configuration machine** (type, RAM, ROM).

## 9. Oracle différentiel — `tools/opendst_oracle.py`

La propriété intéressante n'est pas « le jeu ne plante pas », c'est :

> pour tout script d'entrées, **NeoST ≡ Hatari**.

C'est la forme *property-based* du problème, et c'est ce qui transforme un explorateur
d'états en **chercheur de divergences** : chaque situation nouvelle qu'il atteint devient
un point de comparaison gratuit contre la référence matérielle.

```sh
python3 tools/opendst_oracle.py --rom roms/tos102uk.img --disk <jeu.st> \
    --frames 2600 --joy-at 1500 --joy-script-file rollout.joy
# → alignement : trame NeoST 1500 == trame Hatari 1390  →  décalage -110 (unique sur 301 images)
# → VERDICT : IDENTIQUE — trame NeoST 2600 == trame Hatari 2465 (décalage -135, alignement -110)
```

**Deux obstacles, tous deux traités.**

1. *Hatari ne sait pas injecter un joystick daté.* `--cmd-fifo` ne connaît que des touches,
   tourne en temps réel et ne pose rien à la trame près. Le patch versionné
   [`tools/hatari_neost_oracle.patch`](../tools/hatari_neost_oracle.patch) ajoute à Hatari
   la lecture d'un script joystick **indexé sur la VBL** (`NEOST_JOY_SCRIPT`,
   `NEOST_JOY_START`). Le fichier est produit par `neost-headless --joy-script-compile` :
   la grammaire n'a **qu'une** implémentation, des deux côtés.
2. *Hatari n'est pas déterministe d'un run à l'autre.* Le même patch rend la graine
   figeable (`HATARI_SEED`), ce qui autorise enfin à mesurer un décalage dans un run et à
   le réutiliser dans le suivant.

**L'alignement en deux passes**, et pourquoi il ne suffit pas d'« ancrer sur une attente » :

- passe A — sans aucune entrée, on cherche la trame Hatari identique à la trame NeoST
  d'ancrage, sur **toute** la fenêtre. Le décalage n'est retenu que si cette image est
  **unique** : sur une scène statique (bureau, écran-titre figé) toutes les images de la
  fenêtre sont identiques et « la plus petite » n'est que la borne basse — le décalage
  vaudrait mécaniquement `−scan` et le verdict final serait trivialement « identique »
  (mesuré : 121 images sur 121). L'outil **refuse** alors (`ALIGNEMENT AMBIGU`) et demande
  une ancre sur une scène qui bouge d'une trame à l'autre, avant toute entrée ;
- passe B — on rejoue avec `NEOST_JOY_START = ancre + décalage`, si bien que chaque entrée
  tombe au même instant-programme des deux côtés.

Mesuré sur Super Sprint : la première entrée tombe bien pendant l'attente des deux côtés,
mais les machines en sortent à des trames différentes, et toutes les entrées suivantes,
datées en absolu, arriveraient sinon décalées — les deux trajectoires de jeu divergent
alors pour de bon, sans qu'aucune divergence d'émulation soit en cause.

⚠ **Limite mesurée, à connaître avant d'accuser l'émulateur.** Le décalage n'est pas
constant : il **saute à chaque chargement disque** — Hatari charge plus vite, `--fastfdc`
des deux côtés n'étant pas la même approximation. Graine 1, Super Sprint, **sans aucune
entrée** :

| trame NeoST | 600 | 1000 | 1500 | 2000 | 2500 |
|---|---|---|---|---|---|
| décalage Hatari | −7 | −11 | −110 | −200 ⚠ | *aucune trame identique* |

⚠ Le point **−200 à la trame 2000** vaut exactement la demi-fenêtre utilisée (±200) : il a été
mesuré AVANT que l'outil ne détecte les ancres statiques, et peut n'être que l'artefact décrit
ci-dessus. À re-mesurer avec la version actuelle ; les trois premiers points (≠ −scan) ne sont
pas concernés.

Conséquence : une comparaison n'est fiable que si l'ancre et la cible ne sont **pas séparées
par un chargement**.

**Deux points de mesure qu'il faut lire pour ce qu'ils sont — des mesures, pas des
verdicts d'émulation :**

- trame 2500, sans entrée : aucune trame identique sur **901 images** (±450) ; la plus
  proche à **305 px** (0,27 % de la zone comparée), et l'écart est *localisé* — 22
  scanlines de 16 px, soit un petit élément animé déphasé, pas un défaut de rendu réparti.
  Soit le décalage a dépassé la fenêtre, soit les deux programmes ne sont plus dans le même
  état après avoir chargé à des vitesses différentes. Ce n'est **pas** une divergence
  d'émulation établie ;
- même chose en pleine course (trame 3400, script de conduite, ±300 soit 601 images) :
  aucune identique, la plus proche à 587 px, et **au centre de la fenêtre** — un simple
  décalage de numérotation ferait chuter l'écart autour d'une trame précise. Entre l'ancre
  (au titre) et la cible il y a un chargement de piste : les entrées de conduite tombent
  donc à des instants-programme différents, ce qui suffit à expliquer deux courses
  différentes.

Pour aller au-delà, il faudrait **réancrer après le chargement** (feu au titre, puis
réalignement sur l'écran d'attente, puis conduite) : l'outil ne sait aujourd'hui aligner
que sur une trame sans entrée.

**L'oracle sait échouer** — sans quoi un « IDENTIQUE » ne vaudrait rien. Contrôle négatif
fait : NeoST avec le script de tir contre Hatari **sans** script, sur 241 trames de
fenêtre, aucune trame identique (la plus proche à 2 280 px). Avec le script des deux
côtés, **0 px**.

## 10. Client d'exemple — `tools/opendst_explore.py`

Une boucle Go-Explore minimale, ~200 lignes, qui pilote le serveur : archive de cellules,
reprise d'une cellule peu visitée, rollout d'actions **tenues** (pas du bruit par trame),
archivage de toute clé jamais vue.

```sh
python3 tools/opendst_explore.py --rom roms/tos102uk.img --disk <jeu.st> --machine st \
    --load-state cell0.state --iterations 200 --rollout 40 --cell-from screen --out /tmp/x
```

Mesuré sur Rick Dangerous depuis un état en jeu : **60 itérations en 13,5 s**, 56 cellules
gravées, chacune relisible par `--load-state` — et l'explorateur pose de la dynamite tout
seul (la table d'actions contient les combinaisons feu+direction).

Ce n'est **pas** une politique d'exploration sérieuse, et c'est délibéré : le sujet est
l'interface avec l'émulateur. Deux réglages font toute la différence dans un vrai usage :

- `--cell-from screen` est trop fin — chaque trame d'animation est une cellule distincte.
  Pour aller loin, prendre `--cell-from probes` avec les variables du jeu (numéro d'écran,
  position quantifiée, munitions) : c'est la clé de cellule qui fait marcher Go-Explore ;
- `--score NOM` (une sonde à maximiser) remplace la nouveauté pure par un critère de
  qualité — reprendre la cellule où l'on avait le plus de bombes, pas seulement une
  situation jamais vue.

## 11. Rapporter une divergence

Toute divergence ou plantage trouvé par ces outils doit être rapporté avec **le save-state
et le script d'entrées** : c'est un cas de reproduction parfait, dans la monnaie que le
projet utilise déjà. Ajouter la graine Hatari (`--seed`) et le décalage d'alignement
affiché par l'oracle : sans eux, la comparaison n'est pas rejouable.
