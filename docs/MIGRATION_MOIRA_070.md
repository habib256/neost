# NeoST 0.7.0 — migration du cœur 68000

Plan du 28 novembre 2026, avant modification du cœur. La 0.6.5 a été publiée le
13 novembre (tag `0.6.5`, `b3ced823`). Depuis, le Moira actuel a continué à
faire tourner ST, Mega ST, STE et Mega STE sur `main`. Le portage 0.7.0
conserve ces quatre machines en mode 68000.

## Source et périmètre

Vendoriser le répertoire `Moira/` et la licence du master officiel de Dirk W.
Hoffmann au commit `bf98107ffa3bb24d576bbe99ef684c77e33de938` (6.0b1).
Cette révision comprend les PR de habib256 #42 à #49, notamment le
remplissage après MOVE vers SR et les corrections de trames FPU. Elle contient
le 68030, le 68040, leurs MMU et la FPU. Le JIT de POM68K et les cartes mères
TT/Falcon sont des chantiers distincts. Le FPU périphérique du Mega STE garde
son interface de bus actuelle. [Source officielle au pin](https://github.com/dirkwhoffmann/Moira/tree/bf98107ffa3bb24d576bbe99ef684c77e33de938/Moira) · [Fusion #49](https://github.com/dirkwhoffmann/Moira/pull/49)

## Ordre de livraison

1. Fusionner et pousser ce plan sur `main`. Développer ensuite sur une branche
   isolée. Construire le binaire de référence avant l'import et conserver
   les sorties hors Git. La référence historique se prend au tag 0.6.5. Pour
   isoler le changement de cœur, enregistrer séparément la référence de la
   base de la branche de migration. Employer les mêmes scénarios, médias,
   configurations et entrées lors du contrôle. Noter le SHA, la version du
   binaire, le nombre de scénarios exercés et chaque média manquant.
2. Construire le cœur officiel et rebaser les adaptations NeoST. Documenter
   dans `extern/moira/NEOST_VENDOR.md` le pin de départ historique
   `1efd69467ca13b27b2fb40febd5cb31dbecdea5f`, le nouveau pin et le sort des
   adaptations ci-dessous. Utiliser la liste amont des unités C++20, dont
   les trois cœurs d'exécution, Cache, FPU et MMU. Répliquer les sous-dossiers
   dans le staging CMake. Appliquer les options dans la copie de build.
3. Adapter `NeostMoira`, le banc nu et la lecture des états. Vérifier les
   getters et le PC architectural au reset, après une instruction, au TRAP,
   à la faute et au STOP. La nouvelle convention du PC interne à +2 affecte
   le désassembleur, le traceur, les empreintes et le contrat GEMDOS. Utiliser
   le modèle M68000 pour chaque machine existante.
4. Passer les batteries ci-dessous. Localiser chaque divergence dès la
   première trame, puis à l'instruction et au cycle de bus. Distinguer un
   défaut d'adaptateur d'une évolution étayée du cœur. Une nouvelle empreinte
   requiert une explication et une preuve. Les relevés sur machine et les
   documents primaires priment sur le RTL et Hatari.
5. Porter le numéro de projet à 0.7.0, vérifier le numéro du binaire reconstruit,
   documenter la provenance, les changements observables et le format des états.
   Garder le tag et les paquets 0.6.5 comme repli. La préparation de 0.7.0 et
   la publication d'une release avec tag et paquets sont deux opérations.

## Contrats et témoins

| Contrat | Adaptation à vérifier | Témoin |
|---|---|---|
| Horloge et bus | Timing précis, accès 8/16 et accès longs en deux mots ordonnés en 68000, attente avant/après chaque transfert, DB vidéo, TAS verrouillé, arbitrage blitter et cache Mega STE. Le hook amont par opérande intervient avant traduction. | Bancs fx68k, blitter, STCPU, HOG garé et Mega STE 8/16 MHz avec/sans cache |
| Interruptions et reset | Recaler les points de sondage IPL dans le préfetch et au STOP, la retenue STOP+4 et l'IACK en deux phases. Préserver autovecteur HBL/VBL, vecteur MFP/SCC, overlay de reset, double faute et halt. | HALT, STOP, IACK, diagnostic Field Service et parcours Vroom |
| Instructions | Confronter le préfetch MOVE/SR/CCR et immédiats, MOVEM, ADD/SUB.L mémoire, ADDX/SUBX, DBcc expiré, JSR, trame groupe 0, reset et ordre DIVS/DIVU. Garder les corrections amont équivalentes et porter les comportements mesurés restants. | Tests de logique, cycle-bench, vecteurs CPU amont, 48 variantes fx68k et `cpu_div_ram` |
| Hôte et débogueur | Portes GEMDOS, observateur TRAP, masquage 24 bits des watchpoints, provenance de la dernière faute, lecture sans effet de bord, abandon d'une fibre à travers `execute`. | Tests GEMDOS/trace, bus errors par machine, `check_hog_park.py` |
| États et empreintes | Relever un état v68 avec l'ancien binaire. Vérifier les nouveaux registres, préfetch, IPL, flags hôte et clocks par champ. Versionner un format incompatible, refuser v68 explicitement avant restauration et tester la reprise avec le nouveau cœur. Garder l'empreinte architecturale indépendante de la sérialisation CPU. | `run_determinism.py`, état v68 réel, rewind, libretro, `cpu_swap_gate.py` |

## Recette d'acceptation

Dans chaque arbre, construire les cibles natives Release et leurs bancs. Les
médias privés et les références générées restent hors Git. Enregistrer les
journaux et codes retour, distinguer succès, divergence, erreur et dépendance
absente. Les étapes longues peuvent tourner sur des lots distincts ; réserver
la mesure de débit à une machine libre.

- Avant le portage, `python3 tools/run_all.py --tier full` puis
  `python3 tools/cpu_swap_gate.py --record <base.json>`. Conserver un état
  de sauvegarde et le relevé de performance. Distinguer la référence de release
  du fichier de comparaison avant/après.
- Après le portage, `python3 tools/run_all.py --tier fast`, puis `--tier full`.
  Contrôler les étalons pixel, les rapports série, fx68k, GSTMCU, le blitter
  et le Mega STE. Rejouer les quatre suites de la porte avec
  `python3 tools/cpu_swap_gate.py --check <base.json>` et les replays avec
  `/usr/bin/python3 tools/run_game_replay.py` (Hatari et Pillow).
- Exécuter les tests CPU du pin officiel avec les adaptations NeoST et la
  configuration du banc explicitée. Adapter `tools/run_moira_runner.sh` au
  nouveau répertoire `Tests/`. Vérifier `tools/run_determinism.py`, les états
  anciens et nouveaux, ainsi que les chemins libretro et web. Comparer
  `tools/run_perfbench.py --budget` à la référence sur le même hôte.
- La CI vérifie les builds des plateformes configurées, les sanitizers, le
  RTL et libretro. Consigner séparément les cibles uniquement compilées.
  Une régression inexpliquée laisse la branche de migration ouverte et le
  tag 0.6.5 disponible.

Contrats détaillés et hiérarchie des témoins dans [CPU_SWAP_GATE.md](CPU_SWAP_GATE.md),
[FX68K_BENCH.md](FX68K_BENCH.md) et [REFERENCE_LADDER.md](REFERENCE_LADDER.md).

## Relevés du port sur le commit de code `ea55549d`

Le binaire Release annonce NeoST 0.7.0. ST, Mega ST, STE et Mega STE
sélectionnent `M68000`. Les tests des modèles 68030 et 68040 s'exécutent
dans les bancs CPU de Moira.

| Contrôle | Résultat observé |
|---|---|
| Compatibilité 68000, profil MAME | 261 894 cas standard et 55 606 erreurs d'adresse conformes sur les registres, les cycles et le bus. |
| Reprise sur faute des 68030 et 68040 | 9 783 cas sur 68030 et 9 784 sur 68040 réussissent, selon le banc officiel `RetryMoira`. |
| Bus physique du 68000 | 37/48 variantes en accord cycle à cycle avec le RTL fx68k ; 34/48 sur la base. Les trois gains concernent les doubles fautes. Les 11 écarts restants sont détaillés dans [le banc fx68k](FX68K_BENCH.md). |
| États et déterminisme | 16 empreintes reproductibles, puis 8 identiques après reprise à la trame 400. Le binaire parent relit son état v68 et le candidat le refuse. Un blit HOG suspend l'instruction pendant 5 trames et les deux exécutions se rejoignent ensuite. |
| Libretro | 35/35 contrôles réussis. Un second client rejoint l'hôte à la trame 500, charge son état et reproduit les 400 trames suivantes. |
| Web | Chrome 158 et Emscripten 6.0.8 passent les 99 contrôles, dont les huit démos, les sauvegardes, la reprise après exception et le repli sans JSPI. |
| CI | Les sept jobs applicables de [Tests](https://github.com/habib256/neost/actions/runs/46104418324) passent. La [matrice des artefacts](https://github.com/habib256/neost/actions/runs/46104937659) passe après relance isolée du job Windows : les huit ports Winsock du test NAT étaient indisponibles sur le premier runner. |

La base de comparaison `4bccff0f` contient 433 scénarios et 162 354
empreintes machine/CPU. Les 68 étalons fournissent 15 606 empreintes, les
29 parcours 27 644, les 294 démarrages de la collection 117 596 et les
42 sondes Mega STE 1 508. Aucun média absent. SHA-256 du JSON de référence
privé : `5bd25113b9116b0392e5686f700ef5cf3f207dba12e60c5b3ce5af10704b8df1`.

Le tag publié `0.6.5` (`b3ced823`) a été construit et mesuré séparément :
422 scénarios, 155 581 empreintes, aucun saut. Il contient 57 étalons, 29 parcours,
294 démarrages et 42 sondes Mega STE. Les onze étalons ajoutés sur `main` expliquent
les 433 scénarios utilisés pour l'import. SHA-256 du JSON historique privé :
`5a472b36c034c28bb69b2e4323e68a4fe3352ef89a6d6ea756f8127722632eff`.

### Verdict du comparatif avec le parent

Les paliers natifs `fast` et `full` passent, avec les 68 étalons pixel.
Le palier de jeux passe ses 28 parcours de compatibilité. Le diagnostic
facultatif du retour à gauche de Magic Pockets reproduit son écart historique
de 29,63 % à la trame 9 000.

| Suite | Scénarios de l'ancien cœur | Accord strict sur toutes les trames |
|---|---:|---:|
| Étalons | 68 | 52 |
| Parcours rejoués | 29 | 20 |
| Collection, trois machines | 294 | 239 |
| Sondes Mega STE | 42 | 42 |
| **Total** | **433** | **353** |

La porte stricte termine avec le code 1 sur 80 scénarios. Elle hache aussi
des données de débogage présentes dans l'état sérialisé. Dans 65 de ces
scénarios, la première divergence touche uniquement le hachage machine.
Un second passage a isolé le champ `Shifter::ColorWrite::pc` : ce PC indique
pour les journaux quelle instruction a écrit la palette. Sur `mfp_poll`,
l'écriture de la trame 240 a le même cycle (50 746), le même index (0)
et la même couleur. Le nouveau cœur indique le `CLR.W` auteur en `$1042` ;
l'ancien indiquait déjà l'instruction suivante en `$1046`.

Dans deux binaires diagnostiques construits séparément, seul ce champ de
provenance est mis à zéro dans le tampon temporaire de hachage. Les cycles,
index et couleurs demeurent dans la comparaison ; la production et les fichiers
d'état gardent leur représentation complète. Le témoin `mfp_poll` à la trame
241 produit le même fichier d'état à l'octet que son binaire de production
respectif. Les **36 207 points** des 65 scénarios ont été rejoués avec ce contrôle.
**48 scénarios concordent alors intégralement** et 1 041 points restent en écart
sur 17 scénarios. La référence stricte du parent est conservée.

Les 32 scénarios qui présentent une différence au-delà de ce PC de provenance
se répartissent ainsi ; le nombre compte les configurations et exemplaires de
disque, y compris les deux copies de Stardust.

| Nombre | Première différence expliquée |
|---:|---|
| 5 | Sonde RAM du TOS, décrément de registre et PC empilé lors d'une erreur de bus. |
| 12 | The Union Demo STX sur trois machines et trois disques Fuzion sur ces trois machines. Les sondes reproduisent les opcodes et leur phase de faute. |
| 3 | BERR en mode ST de DHS Just Musix et E605 ; lecture fautive de `MOVE.B -(A3),d8(A4,D3.W)` dans Lemmings sur Mega STE. |
| 7 | Durée et trames de faute au retour sur RTS/RTE dans Epic (trois machines), Blood Money (trois machines) et James Pond II (Mega STE). |
| 5 | PC et, selon la faute, file de prélecture au HALT, détaillés ci-dessous. |

Les sondes de groupe 0 de The Union Demo (`48D0 00FF`) et de Fuzion
(`22B8 0030`) confirment le PC empilé du candidat contre fx68k.
Du BERR à l'écriture du PC, on mesure 14 cycles sur les deux modèles et
10 sur l'ancien cœur nu. Jusqu'à la lecture du vecteur 2, les durées sont
de 42, 42 et 40 cycles. Pour Lemmings, le cycle fautif est **une lecture
d'octet** en FC5 sur la source prédécrémentée ; dans la sonde, l'adresse
est déplacée vers la fenêtre BERR du banc. Le PC empilé est `$1CEC` chez
fx68k et Moira 6, `$1CEA` chez le parent. Les traces et la hiérarchie des
témoins figurent dans [le banc fx68k](FX68K_BENCH.md).

### Parcours Pacemaker sur STE

Le témoin de la trame 12 000 reproduit le générique de la démo Paradox sur
le tag 0.6.5 (empreinte RGB `1beba5d3…`). Entre ce tag et la base du
portage, sept pixels noirs ont pris une couleur sur la ligne 268, entre
les colonnes 54 et 88 du tampon 416 × 276. Le texte de remerciements et le
motif bleu sont visibles dans les deux captures. La base et Moira 6 rendent
la même image (`e55c6530…`) ainsi que les trois scènes précédentes.
Hatari montre les quatre mêmes images, au pixel près, huit trames plus tard.
Les 1 200 empreintes machine/CPU concordent entre les deux cœurs. Le manifeste
du replay conserve désormais le témoin du parent. La comparaison caractérise
une évolution vidéo antérieure au port, sans mesure de cette scène sur STE réel.

### Débit sur le poste macOS arm64

Comparaison entre `4bccff0f` et `143b2774` en Release, avec le même compilateur,
sans libretro, sur les mêmes médias vérifiés par SHA-256. Pour chaque charge de
`run_perfbench.py`, cinq paires de 600 trames ont alterné l'ordre des binaires.
Le temps CPU est la somme utilisateur + système du processus headless.

| Charge | Variation médiane du temps CPU | Plage sur cinq paires | Variation médiane du temps écoulé |
|---|---:|---:|---:|
| Boot EmuTOS | +1,3 % | −0,8 à +2,8 % | +1,1 % |
| Blitter STE | +0,7 % | −2,4 à +9,6 % | +1,8 % |
| Lecture du MFP | +5,1 % | +0,6 à +7,9 % | +2,4 % |

Dans la passe séparée `--budget --json`, la charge la plus lente du candidat
atteint 360,9 trames/s à 50 Hz (7,22 fois le temps réel). Le parent atteint
422,9 trames/s sur cette charge. Le poste exécutait d'autres travaux. Sur le
boot, le ratio de temps écoulé candidat/parent varie de 0,96 à 1,47 selon la
paire. Ces débits muraux décrivent ce poste pendant la mesure.

### Retours d'exception dans la collection

- **Blood Money, ST, STE et Mega STE**. Le premier écart temporel apparaît
  après `RTS` à `$002D80`, sur le retour à une adresse impaire.
  L'entrée dans le gestionnaire du vecteur 3 (`$0051D8`) passe de 68 à
  72 cycles bus dans les trois machines. Sur STE, la première différence
  de registre survient plus tard à la lecture du statut ACIA en `$FFFC00`.
  Une sonde nue (`MOVE.L #$1235,-(A7)`, `RTS`, vecteur 3 en `$1100`)
  mesure du dernier mot lu sur la pile à la première lecture du vecteur
  44 cycles sur fx68k, 42 sur l'ancien Moira et 44 sur Moira 6.
  Du dernier mot lu au premier mot de trame écrit, les durées sont
  respectivement 16, 12 et 14 cycles. La date du premier strobe reste
  une question ouverte sur le matériel.
- **James Pond II, Mega STE**. Le premier écart suit le `RTE` du TOS en
  `$E036BE`. Les trois lectures de pile ont lieu aux mêmes cycles. Entre
  la dernière (`$2E01EC`) et le premier mot du vecteur 3, il s'écoule 44
  cycles bus sur le parent et 48 sur le candidat. Après l'interruption HBL
  de niveau 2, le premier opcode de son gestionnaire (`$E00BF0`) accuse
  12 cycles d'écart. Le témoin RTE sur cœur nu et les relevés d'Epic
  sont détaillés dans [le banc fx68k](FX68K_BENCH.md).

### Doubles fautes déjà présentes dans les deux cœurs

Le démarrage des deux copies de Stardust sur ST, de Pacemaker sur ST et
de Faster Atari STE et de Nitro sur Mega STE aboutit au même HALT dans les deux
versions. Les traces concordent sur chaque instruction exécutée et sur
ses registres jusqu'au `MOVE.L A3,-(A7)` fautif. Le décodage des états v68
et v69 à HALT reproduit leurs empreintes CPU respectives.

| Scénario | PC au HALT, parent → candidat | IRC / IRD, parent → candidat | Horloge bus commune |
|---|---|---|---:|
| Stardust sur ST, deux copies | `FC9376 → FC9374` | `4CED / 3F00` dans les deux | 293 136 040 |
| Pacemaker sur ST | `FC9376 → FC9374` | `4CED / 3F00 → 3F00 / 2F0B` | 224 279 464 |
| Faster Atari STE sur Mega STE | `E0FBBC → E0FBBA` | `4CED / 3F00 → 3F00 / 2F0B` | 482 291 624 |
| Nitro sur Mega STE (trame 1 042) | `E0FBBC → E0FBBA` | `4CED / 3F00 → 3F00 / 2F0B` | 166 907 816 |

Tous les autres champs de l'empreinte CPU concordent, y compris le registre
d'état et les pointeurs de pile. Moira 6 conserve l'adresse de l'instruction
fautive. Pour Pacemaker, Faster Atari STE et Nitro, l'erreur de bus sur le premier mot
interrompt l'écriture avant le transfert dans IRD. Les valeurs internes de
la file à HALT n'ont pas été relevées sur un 68000 physique.
