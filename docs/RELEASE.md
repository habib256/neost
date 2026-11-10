# Discipline de release — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qu'il faut faire, dans l'ordre, pour poser une version.**
Écrit dans le cadre du chantier **A37** (2026-08-28).

## Pourquoi ce fichier

L'audit du 2026-08-27 relevait trois symptômes d'une seule cause — rien n'écrivait la
procédure, donc rien ne pouvait la vérifier :

- **trois tags le même jour** (0.5 → 0.5.1 → 0.5.2, le 2026-08-10) ;
- **0.5.3 sautée sans une ligne pour le dire** (elle est désormais consignée au
  `CHANGELOG.md`, § *Numéros de version sautés*) ;
- **le travail majeur depuis le 2026-08-23 n'est pas tagué** — MegaSTE 12/12,
  CAB/theoldnet, l'audit et le plan A16-A37, puis A10/A14/A16b/A28-A36 et les pas 2 et 5
  de la purge.

`tools/check_release.py` (palier `fast`) garde ce qui est vérifiable par une machine ;
le reste est ci-dessous.

## La procédure

1. **Décider le numéro.** `MAJOR.MINOR.PATCH`. Un correctif de paquet qui ne change pas
   l'émulation → PATCH ; une fonctionnalité → MINOR. Ne pas sauter de numéro ; si un
   numéro est brûlé malgré tout, l'écrire au § *Numéros de version sautés* du
   `CHANGELOG.md` — le contrôle échoue tant que ce n'est pas fait.
2. **Bumper les DEUX endroits, ensemble** : `project(NeoST VERSION x.y.z)` dans
   `CMakeLists.txt` et « Version courante : **x.y.z** » en tête du `CHANGELOG.md`.
   `check_release.py` exige qu'ils soient égaux **et** égaux à la dernière en-tête
   `## x.y.z — …`.
3. ⚠ **Reconfigurer, puis contrôler `--version`.** `NEOST_VERSION_STR` est une variable de
   **cache** CMake. Depuis le 2026-08-22, `CMakeLists.txt` la ré-ensemence tout seul quand
   `project(VERSION)` change (`NEOST_VERSION_SEED`, message « version ré-ensemencée ») —
   **sauf** si une valeur a été forcée par `-DNEOST_VERSION_STR=…` dans ce dossier de build :
   elle est alors préservée et il faut repasser `-DNEOST_VERSION_STR=x.y.z`, sinon
   `--version` **ment**. (Déjà écrit dans `CLAUDE.md` ; répété ici parce que c'est
   l'étape qu'on saute.) Le `(commit …, built …)` qui suit la version, lui, ne ment pas :
   il est regénéré à chaque build par `cmake/BuildInfo.cmake` — un `+xxxxxxxx` derrière le
   hash signale un arbre de travail modifié, donc un binaire qui n'est PAS celui du tag.
4. **Écrire l'entrée de release** dans le `CHANGELOG.md` : ce que l'utilisateur gagne,
   pas la liste des commits. Les chantiers datés vivent déjà en dessous.
5. **`python3 tools/run_all.py --tier full`** — vert, sur un poste au repos. Le palier
   `full` porte les étalons pixel : c'est le seul qui interdit de publier une régression
   de rendu.
6. **Taguer** : un tag annoté par release, `git tag -a x.y.z -m "…"`. Un seul par jour,
   sauf raison écrite. ⚠ **Le message du tag s'écrit en ANGLAIS**, comme l'interface et
   les journaux — c'est une publication, elle s'adresse au public, pas au mainteneur.
   Idem pour les **notes de la Release GitHub** : le job `publish` les génère avec
   `--generate-notes`, donc à partir de titres de commits FRANÇAIS ; les remplacer
   ensuite par un texte anglais (`gh release edit x.y.z --notes-file …`). Le CHANGELOG,
   lui, reste en français : c'est de la documentation, pas une publication.
   (Règle posée par le mainteneur le 2026-09-01, à la sortie de la 0.6.)
7. **Publier** : la CI construit les paquets. Vérifier que chacun porte ses licences
   (`GPL-3.0.txt`, `GPL-2.0.txt`, `THIRD-PARTY.txt` — huit jobs le vérifient) et que
   `check_licenses.py` est vert : tout composant livré doit être nommé avec sa licence.

## Dossier 0.6.3 — préparé le 2026-11-10

Numéro **0.6.3**, explicitement demandé par le mainteneur, y compris pour les
fonctionnalités NetUSBee/NAT (exception à la règle MINOR du pas 1).
CMake et CHANGELOG sont alignés ; les [notes publiques en anglais](release-notes/0.6.3.md)
sont prêtes. Le format de sauvegarde passe à v59. L'issue #44 est corrigée dans
le code et couverte par le palier fast ; sa fermeture GitHub attend la livraison.

La préparation locale ne pose aucun tag et ne publie aucun paquet. Avant de
taguer, vérifier le palier full et la CI du commit destiné à la release, puis
appliquer les pas 6–7 ci-dessus. Message prévu :

```sh
git tag -a 0.6.3 -m "NeoST 0.6.3 — NetUSBee USB and built-in networking"
```

Après publication : `gh release edit 0.6.3 --notes-file docs/release-notes/0.6.3.md`.
Les vérifications exécutées pendant cette préparation sont consignées dans
le bilan ci-dessous ; ce dossier ne prétend pas que la CI ou les paquets d'un
futur tag ont été vérifiés.

**Validation locale, 2026-11-10, macOS Release** : GUI et headless annoncent
`0.6.3`. Le palier `full` passe avec les comparaisons pixel, les bancs RTL,
le boot GUI et les sockets NAT. Un contrôle optionnel est recensé SKIP :
`check_compare_titles.py`, faute de Pillow dans Python. Les autres contrôles
incluent 647 vérifications de logique, les pilotes USB4TOS (fichier FAT16
persisté, Bconin, mousevec), les 13 cas de verdict Spectrum 512 et les
39 contrôles NetUSBee du binaire final. STinG s'active aussi en mode NetUSBee.
La reproduction avec ROM STE absente conserve les sept images ST à zéro
pixel d'écart et retourne 77. La reprise d'un état avec les deux HID est
déterministe. Graphify et son wiki sont rafraîchis sans API. Aucun tag,
publication, fermeture distante d'issue ni validation CI de ce changement.

## Dossiers des versions passées

0.6.2 et 0.6.1 : [`archive/RELEASES_PASSEES.md`](archive/RELEASES_PASSEES.md).

## Ce qui BLOQUE encore une release publique

- ✅ **CLOS le 2026-09-02** : 0.5.2 et 0.5.4 supprimées, plus aucun canal public ne
  distribue de ROM Atari. Le récit ci-dessous est conservé pour la mémoire du chantier.
- ~~Le § BLOQUANT du `TODO.md`~~ **Purgé le 2026-08-30** : l'historique est réécrit
  (`git filter-repo`), le dépôt ne suit plus ni ROM Atari, ni jeux, ni cartouches, ni
  Cubase Lite. Le pas **4** est fait le même jour : les paquets sont **100 % libres par
  défaut** (défaut inversé dans `stage_free_data.sh` et les gardes CI ;
  `NEOST_PACKAGE_NO_ATARI_TOS=0` ré-embarque des copies locales, usage personnel
  seulement). Les **assets des releases 0.5.2/0.5.4** (paquets bureau avec
  `tos102uk.img` + `tos162uk.img` — vérifié) sont assumés tels quels : **les deux
  releases seront supprimées à la sortie de la 0.6** (décision du mainteneur,
  2026-08-30). ⏳ **Le tag `0.6` est posé le 2026-09-01** — la suppression est due dès
  que ses paquets sont publiés.
- **Signature / notarisation** — ◐ **palier 0 fait le 2026-09-01, palier 1 ouvert.**
  Mesuré sur le `.dmg` 0.6 publié : `codesign --verify` répondait « code object is not
  signed at all » et `spctl` « no usable signature ». Le seul cachet était celui du
  LINKER sur les Mach-O (`adhoc, linker-signed`), `Info.plist=not bound`,
  `Sealed Resources=none` — le bundle n'était pas scellé, d'où « NeoST est endommagé,
  placez-le dans la corbeille », qui est un **cul-de-sac** pour l'utilisateur.
  `package_macos.sh` scelle désormais le bundle par une **signature ad-hoc** (binaire
  secondaire d'abord, bundle ensuite, garde `--verify --deep --strict`). Gatekeeper
  refuse toujours — il n'y a pas de Developer ID, aucune signature gratuite n'y change
  rien — mais le refus devient « développeur non identifié », qui a une sortie : clic
  droit → Ouvrir.
  **Reste le palier 1** : Developer ID + notarisation, **99 $/an** (Apple Developer
  Program). Recette : `codesign --options runtime --timestamp` sur les deux binaires
  puis le `.app`, signer aussi le `.dmg`, `xcrun notarytool submit --wait` (clé API App
  Store Connect), `xcrun stapler staple` sur les deux — l'agrafe est ce qui fait que le
  premier lancement marche hors ligne. En CI : 4 secrets et un trousseau temporaire.
  Risque technique faible ici : Moira est un interpréteur, donc **pas de JIT**, l'écueil
  habituel du hardened runtime.
  Côté **Windows**, le `.zip` reste non signé : depuis juin 2023 la clé doit vivre sur un
  token FIPS ou un HSM, donc signer en CI impose un service de signature cloud ; un
  certificat OV (~200-400 €/an) n'éteint même pas l'avertissement SmartScreen tant que la
  réputation n'est pas bâtie. Et SmartScreen, lui, est un clic — pas un cul-de-sac.
  Mauvais rapport, à faire en dernier.

## Ce que la machine vérifie déjà

| Contrôle | Ce qu'il garde |
|---|---|
| `tools/check_release.py` | les trois numéros de version disent la même chose ; aucun numéro sauté en silence |
| `tools/check_licenses.py` | tout composant livré est nommé avec sa licence, README **et** `THIRD-PARTY.txt` |
| `tools/check_doc_claims.py` | les chiffres cités dans la doc se recomptent |
| `tools/check_doc_anchors.py` | les symboles cités existent encore |
| 8 jobs de CI | les fichiers de licence accompagnent chaque paquet |
