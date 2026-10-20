# Performance du cœur NeoST — profil, optimisations, recette de build

Ce document raconte une campagne d'optimisation menée au **callgrind**, avec ses
mesures avant/après et le raisonnement de chaque changement. Il sert deux buts :
donner la **recette pour refaire le profil**, et éviter qu'on ne « ré-optimise »
au jugé du code déjà traité — ou qu'on ne défasse une optimisation sans savoir
ce qu'elle payait.

**Contrainte non négociable de tout ce qui suit** : NeoST est un émulateur
*cycle-exact*. Aucune de ces optimisations ne change une seule valeur produite.
Chacune a été validée par la suite d'étalons pixel-exacts
(`python3 tools/run_etalons.py`, 15 étalons, tolérance **0 pixel**), et les
variantes de compilation ont été vérifiées **octet-identiques** sur des captures
de 6801 et 29500 trames.

---

## 1. Refaire le profil

```sh
# Binaire de profilage : mêmes optimisations que la release, plus les symboles.
cmake -B build-prof -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer" \
      -DCMAKE_C_FLAGS="-g -fno-omit-frame-pointer"
cmake --build build-prof -j

# Charge représentative : un boot TOS (chemin universel) ET un jeu (rendu + son
# + disquette). Les deux profils ont la MÊME forme — les points chauds du cœur ne
# dépendent pas du logiciel émulé.
valgrind --tool=callgrind --callgrind-out-file=boot.out \
    ./build-prof/neost-headless roms/tos162uk.img --frames 300

callgrind_annotate --threshold=70 boot.out            # par fonction
callgrind_annotate --auto=yes --include=$PWD/src boot.out   # par LIGNE (le plus utile)
```

`neost-headless` est **déterministe** : deux exécutions donnent exactement le même
décompte d'instructions, ce qui rend les comparaisons avant/après fiables au
pour-cent près — bien plus que le temps mural.

> **Ir (instructions retirées) plutôt que le temps** pour comparer deux versions,
> mais **le temps mural** pour juger un changement qui échange des instructions
> contre des accès mémoire ou des branchements (table de conversion, code sans
> branche). Les deux ne disent pas la même chose : cf. § 3.1, où une version à
> *moins* de branchements coûtait *plus* d'instructions.

---

## 2. Le profil de départ (boot TOS 1.62, 300 trames)

| Poste | % des instructions | Cause |
|-------|--------------------|-------|
| `Scheduler::runTo` + `scanNextDue` | **17,7 %** | deux balayages linéaires des 19 sources pour trouver le minimum |
| `Bus::read16` / `read8` + `mmuTranslate` | **20 %** | décodage MMU **refait à chaque octet** |
| `Shifter::decodeLineIndices` | 6,6 % | dé-entrelacement des bitplanes **bit à bit** |
| `Bus::busFaultN` | 5,5 % | testé à chaque accès CPU, presque toujours pour rien |
| `Shifter::renderLine` | 4,8 % | conversion `$0RGB → ARGB` **par pixel** |
| `NeostMoira::sync` | 7,4 % | appelé à chaque groupe de cycles |

Total : **6 665 M** instructions.

---

## 3. Les optimisations, et ce qu'elles paient

### 3.1 Ordonnanceur — du balayage linéaire au minimum tenu à jour

`Scheduler` garde une échéance par source (19 sources à l'époque de cette campagne ;
31 aujourd'hui, cf. `Scheduler::SRC_COUNT`). Deux endroits balayaient
tout le tableau : la sélection du prochain événement dans `runTo`, et
`scanNextDue()` qui rafraîchit le cache `nextDue_`.

Trois changements successifs, dont **le deuxième a été une fausse piste
instructive** :

1. **Masque `armed_`** — un bit par source armée, parcouru par `__builtin_ctz`.
   Gain réel mais limité : en régime établi **~14 des 19 sources sont armées**,
   le masque ne retire qu'un quart des tours. − 9,7 % au total.

2. **Minimum sans branche** (essayé puis **abandonné**) — un miroir du tableau où
   « inactif » vaut `INT64_MAX`, balayé sans aucun test. Séduisant sur le papier
   (vectorisable, zéro mauvaise prédiction), mais **+1,4 % d'instructions** :
   parcourir systématiquement 19 entrées coûte plus que d'en sauter 5. Retiré.

3. **Le vrai coupable** — le balayage chaud n'était pas celui qu'on croyait. Le
   profil l'attribuait à `Machine::runFrame`, parce que l'appelant chaud est
   `sched.nextDue()` **dans la boucle de trame**, une fois par bloc CPU. La
   correction n'est donc pas d'accélérer le balayage mais de **ne plus le faire** :
   - `runTo` calcule le minimum des échéances **pendant** sa passe de dispatch,
     qui parcourt déjà les mêmes sources — la dernière passe (celle qui ne trouve
     plus rien à déclencher, donc après que tous les callbacks ont replanifié)
     donne exactement la valeur cherchée ;
   - `schedule()` maintient `nextDue_` **exact** et non plus seulement minorant :
     une échéance repoussée alors qu'elle portait le minimum déclenche un
     rebalayage — cas rare, car une source périodique qui se replanifie depuis son
     propre callback a déjà été désarmée par `runTo` ;
   - `nextDue()` répond alors en **O(1)**.

   Un `assert` compare en debug le cache au balayage complet : la
   désynchronisation est la seule faute possible de ce montage.

### 3.2 Bus — cache du décodage MMU et chemin rapide en ligne

`mmuTranslate()` refaisait, **à chaque octet lu ou écrit**, la totalité du
décodage de `$FF8001` : lecture de la config, deux `switch` de taille de banque,
une division pour la taille de RAM posée, puis le remappage RAS/CAS. 12,8 millions
d'appels pour 300 trames de boot, ~39 instructions chacun.

Or ce décodage ne dépend que de **deux entrées** : l'octet de config et la taille
de `ram[]`. On le mémorise, et on le **revalide par comparaison de ces deux
entrées** plutôt que par une invalidation posée aux sites d'écriture — impossible
d'en oublier un (écriture `$FF8001`, changement de taille RAM, reset, chargement
de save-state), au prix de deux comparaisons.

Le cache retient une seule chose : **la longueur du préfixe où la traduction est
l'identité**. C'est le cas dès qu'une banque est annoncée à sa taille réelle, ce
que fait tout TOS après son sizing mémoire — la démonstration est en commentaire
dans `Bus::rebuildMmuCache`, avec un contrôle en debug qui compare le raccourci au
décodage complet aux bornes.

`read8`/`read16`/`write8` passent alors **dans l'en-tête**, réduits à une
comparaison et un accès tableau, et s'inlinent chez l'appelant (Moira, blitter,
DMA). Le décodage complet reste dans `Bus.cpp`, sous le nom `*Slow`.

> **Piège rencontré** : la première version ne traitait que la RAM et ne rendait
> que −4,4 %. Le profil montrait `read8Slow` toujours chaud — parce que **le code
> du TOS s'exécute depuis la ROM** : chaque mot d'opcode passait par le chemin
> lent. Ajouter la fenêtre ROM au chemin rapide a fait passer le gain à −20 %.

### 3.3 `busFaultN` — sortie anticipée pour la RAM ordinaire

Appelé à chaque accès CPU. La plage `[$800, $400000)` — la RAM ordinaire — ne
faute **jamais**, ni en lecture ni en écriture, ni en mode utilisateur : aucun des
trois étages du modèle ne la concerne. Un test en ligne dans l'en-tête suffit, la
lecture en ROM (jamais fautive) est traitée de même. L'appel hors ligne disparaît
du chemin chaud.

### 3.4 Shifter — deux tables au lieu de deux boucles

- **Dé-entrelacement des bitplanes** (`decodeLineIndices`) : la boucle bit à bit
  produisait un pixel par tour, 4 décalages et 4 masques chacun. Remplacée par une
  table de 256 entrées (`kSpread`) qui éclate un octet de bitplane en 8 octets
  « 0 ou 1 » ; les quatre plans se composent alors en **une seule opération 64
  bits**, 8 pixels d'un coup. Comme chaque octet de la table vaut 0 ou 1, les
  décalages de 1, 2 et 3 restent confinés à leur octet : la composition est
  correcte quel que soit le boutisme de l'hôte, et la table est construite par
  `memcpy` pour que sa disposition mémoire le soit aussi.
- **Palette** (`renderLine`) : `stColorToArgb` était appelée pour **chacun des 320
  à 640 pixels** de chaque ligne. Or `palette` ne peut pas changer pendant
  l'émission (aucune émulation ne tourne à ce moment ; les changements en cours de
  ligne passent par le re-rendu Spectrum 512, qui rappelle `renderLine`).
  16 conversions par ligne au lieu de W.

### 3.5 `busDiag` — un diagnostic éteint qui coûtait à chaque accès bus

Le drapeau était une **variable statique locale** initialisée par un `getenv` : à
chaque accès bus du CPU on franchissait la garde d'initialisation du singleton
**et** on évaluait `getClock()` pour l'argument, alors que le diagnostic est
désactivé en exploitation. Passé en drapeau de namespace, avec le test remonté au
site d'appel.

---

## 4. Résultat des optimisations de code

Mesures à drapeaux de compilation **identiques** (`-O3 -g`), même machine :

| Charge | Avant | Après | Gain |
|--------|-------|-------|------|
| Boot TOS 1.62, 500 trames | 0,79 s | **0,49 s** | −38 % |
| Enchanted Land, 900 trames | 1,61 s | **1,04 s** | −35 % |
| Instructions (boot 300 trames) | 6 665 M | **3 987 M** | −40 % |

---

## 5. La recette de build : PGO + LTO

C'est le **plus gros gain unitaire de toute la campagne**, et il ne touche pas une
ligne de code d'émulation.

La boucle chaude de NeoST est l'interpréteur Moira : un branchement indirect sur
l'opcode, puis un très grand nombre de branches conditionnelles rarement prises.
Sans profil, GCC suppose les deux issues équiprobables. Avec, il range les blocs
pour que le cas fréquent tombe en séquence — moins de sauts pris, et surtout un
**cache d'instructions bien mieux utilisé**. Ce qui compte double sur un
Cortex-A72 (32 Ko de L1i, prédicteur modeste face à un cœur x86 de bureau).

| Variante | Boot 500 tr. | Enchanted Land 900 tr. |
|----------|--------------|------------------------|
| `-O3` seul | 0,495 s | 1,065 s |
| `-O3` + PGO | 0,40 s (**−20 %**) | 0,84 s (**−21 %**) |
| `-O3` + PGO + LTO | **0,325 s (−34 %)** | **0,715 s (−33 %)** |

Sur le Pi, tout passe par les scripts :

```sh
packaging/raspberry/build_native_pi.sh --pgo            # sur le Pi (2 passes + LTO)
packaging/raspberry/build_native_pi.sh --pgo --install  # + /opt/neost
```

et la CI (`.github/workflows/pi-borne.yml`) le fait déjà pour l'artefact
`cortex-a72` : l'entraînement tourne sur le runner ARM64, pas sur le Pi.

Le parcours d'entraînement est `packaging/raspberry/pgo_train.sh`. Il couvre
volontairement plusieurs familles de charge (boot ST et STE, 50 et 60 Hz, haute
résolution monochrome, un jeu, une démo à retraits de bordure, les auto-tests) :
**un profil trop étroit est pire que pas de profil**, il fait déclarer « froid » du
code qui ne l'est pas.

### ⚠ Le piège du PGO, qui coûte le gain sans rien dire

GCC nomme chaque fichier `.gcda` d'après le **chemin absolu de l'objet compilé**.
Instrumenter dans `build-A` puis relire depuis `build-B` ne trouve **aucun
profil** — et `-Wno-missing-profile` (indispensable par ailleurs, pour les objets
du frontend GUI qui ne sont pas entraînés) rend l'échec **totalement silencieux** :
le binaire sort sans le moindre gain et sans le moindre message.

C'est arrivé pendant cette campagne : une première mesure annonçait « PGO = −4 % »,
qui n'était que du bruit. Les deux passes partagent donc désormais **le même
répertoire de build**, et les scripts **échouent** si aucun profil n'a été collecté
pour `Cpu68k`, `Bus`, `Shifter` et `Moira`.

---

## 6. Ce qui reste sur la table

Par ordre de poids dans le profil final, avec la raison de ne pas y avoir touché :

| Poste | Part | Pourquoi c'est resté |
|-------|------|----------------------|
| `NeostMoira::sync` | 12 % | appelé à chaque groupe de cycles ; le réduire demande de toucher la granularité SYNC de Moira, c'est-à-dire le cœur du modèle cycle-exact |
| `Scheduler::runTo` (dispatch) | 10 % | ~2 passes par appel, l'une pour déclencher l'autre pour constater qu'il n'y a plus rien ; supprimer la seconde suppose de savoir qu'un callback n'a pas touché l'ordonnanceur |
| `NeostMoira::read16` | 9 % | déjà réduit à l'essentiel (fenêtre blitter, bus error, créneau bus, latch) ; chaque test correspond à un comportement matériel |
| `Moira::read<>` | 8 % | code tiers vendorisé |

Une piste mesurable sans risque sémantique : convertir l'échéance de broche IRQ
(`pinNextDue`, champ de l'état CPU lu par `g_cur->pinNextDue` dans `Cpu68k.cpp`) en horloge cœur pour que `sync()` ne fasse plus qu'une
comparaison — à valider contre le changement de vitesse Mega STE, qui déplace la
conversion.
