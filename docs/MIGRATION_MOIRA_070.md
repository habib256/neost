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
