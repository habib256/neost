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

## Le JOUR J 0.6.2 — dossier préparé le 2026-10-20

✅ **Exécuté le 2026-10-20.** Tag annoté `0.6.2` posé sur `6dbac89`, Release GitHub publiée
(8 paquets + `SHA256SUMS.txt`), notes remplacées par le texte anglais ci-dessous.
⚠ Un premier tag avait été poussé sur `994e1f2` alors que la CI Linux était rouge
(`cpu_div_ram` : `abs(INT32_MIN)` indéfini dans le calcul de cycles DIVS de Moira, vert sous
clang/macOS, rouge sous GCC/Linux). Les runs ont été annulés AVANT toute publication, le
correctif validé par la PR #43 (CI complète verte), puis le tag reposé. Leçon : **vérifier
que la CI de `main` est verte AVANT de taguer** — un `--tier full` local sous macOS ne
couvre pas GCC.

**Le numéro.** La 0.6.2 emporte des fonctionnalités (pilotage externe, RAM 2176 Ko/2,5 Mo,
`$FF8261`/res 3), ce qui selon le pas 1 appellerait une **MINOR** (0.7.0). Le mainteneur
a choisi **0.6.2** le 2026-10-20.

**Le tag** (message en anglais) :

```sh
git tag -a 0.6.2 -m "NeoST 0.6.2 — closer to the metal"
git push origin main 0.6.2
```

### Notes de release, EN ANGLAIS, prêtes à coller

À appliquer après le job `publish` : `gh release edit 0.6.2 --notes-file <fichier>`.

```markdown
## NeoST 0.6.2 — closer to the metal

Seven weeks of hardware fidelity work since 0.6.1.

**Deterministic external control.** A `--server` mode, joystick scripts, memory probes
and state exploration: the same script replays the same game down to the cycle. Disks
can be swapped while running and drive B unplugged from the server.

**Serial and keyboard chips rebuilt to match the hardware.** The MFP USART, the Mega STE
SCC, the MIDI and keyboard ACIAs, the IKBD protocol and the real-time clock. Custom 6301
keyboard programs are now recognised: *Froggies over the Fence* runs on all three disks.

**Floppies and hard disks.** More faithful STX copy protections, WD1772 write-protect
timing, and ACSI transfers that honour the DMA sector count.

**68881 FPU.** Transcendental functions are bit-exact.

**Video and memory.** STE 336-pixel mode, the 22-bit video counter wrap, the
"stopped Shifter" trick (resolution 3), and two new RAM sizes: 2176 KB and 2.5 MB.

**Sound.** The PSG crackle in *Super Hang-On* is gone, and STE DMA sound starts on the
exact sample of the PLAY write.

**Comfort.** Alt+Enter toggles fullscreen with mouse capture, mouse sensitivity is
adjustable, the GEMDOS host drive is reliable (accented file names on macOS, no call
served twice), and the CRT curvature is subtler by default.

**Save states** move to format v51: states from earlier versions are refused.

macOS is still unsigned by Apple: right-click the app and choose Open on first launch.
```

## Le JOUR J 0.6.1 — dossier de la release précédente (préparé le 2026-09-01)

✅ **Exécuté** : le tag annoté `0.6.1` est posé le 2026-09-02 et la Release GitHub porte
des notes en anglais (« NeoST 0.6.1 — your settings stay put », complétées depuis le texte
ci-dessous). Le dossier est conservé comme modèle pour la suivante.

Rien n'est tagué ici. Ce paragraphe existe pour que la pose du tag soit une
**exécution**, pas une rédaction : c'est en rédigeant sous pression qu'on saute une
étape. Tout ce qui suit est écrit, mesuré ou vérifié à l'avance.

### Le numéro : `0.6.1`

**PATCH** — que des corrections, aucune fonctionnalité. Et il est **dû** : la 0.6
publiée ne conserve **aucun réglage** sur **5 paquets sur 8** (le `.dmg` et les quatre
AppImage). Ce n'est pas un confort, c'est un émulateur qui oublie sa configuration à
chaque fermeture. Cf. `CHANGELOG.md` (2026-09-01, chantier A12) et
[`docs/HW_VALIDATION.md`](HW_VALIDATION.md).

⚠ **Pas le même jour que la 0.6** (2026-09-01) sans écrire la raison : c'est le geste
exact que cette discipline a été créée pour empêcher (trois tags le 2026-08-10). Si le
même jour s'impose malgré tout, la raison s'écrit — « 0.6 ne conserve aucun réglage sur
5 paquets sur 8 » en est une qui se défend.

### Ce que la 0.6.1 emporte, déjà dans `main`

- **Le paquet conserve ses réglages** (`writeConfigAtomic` crée le dossier de
  destination) — 5 paquets sur 8 réparés. Gardé par `neost-selftest`, vérifié par
  mutation.
- **Le bundle web porte ses licences** — la non-conformité GPL est déjà éteinte SUR LE
  SITE (Pages redéploie au push ; vérifié en ligne le 2026-09-01, HTTP 200 sur les trois
  fichiers), elle le sera aussi dans le `.zip` de release.
- **Le `.dmg` s'installe par glisser-déposer** — il ne contenait que le `.app`, il porte
  désormais un lien vers `/Applications`. Le sceau du bundle est vérifié APRÈS la copie
  de présentation (`ditto`, pas `cp -R`).

### Les trois pas qu'on saute, dans l'ordre

1. **Bumper les DEUX endroits** : `CMakeLists.txt` et « Version courante » du
   `CHANGELOG.md`, **plus** l'en-tête `## 0.6.1 — …`. `check_release.py` exige les trois
   égales — c'est pour ça qu'aucune n'est bougée d'avance : une version bumpée sans tag
   ferait mentir le dépôt aussi sûrement que l'inverse.
2. ⚠ **Reconfigurer** : `cmake -B build -DNEOST_VERSION_STR=0.6.1` — variable de CACHE ;
   le ré-ensemencement automatique ne joue pas si une valeur a déjà été forcée, et
   `--version` ment alors.
3. **`python3 tools/run_all.py --tier full`** sur un poste AU REPOS. (Vert le
   2026-09-01 sur le contenu actuel de `main`, charge 1,66 — à refaire, le palier garde
   le rendu.)

### Notes de release, EN ANGLAIS, prêtes à coller

Le job `publish` les génère avec `--generate-notes`, donc à partir de titres de commits
FRANÇAIS : les remplacer par `gh release edit 0.6.1 --notes-file <fichier>`.

```markdown
## NeoST 0.6.1 — your settings stay put

A patch release. No new features, three fixes that matter.

**Settings are saved again.** On macOS and on every AppImage, NeoST is run from
read-only media, so it stores its configuration in your user directory — but it never
created that directory, so every setting was silently lost on exit. First launch after
this release creates it and keeps your machine profile, video, audio and MIDI setup.

**The web build ships its licences.** The browser bundle carried none; it now includes
GPL-3.0, GPL-2.0 and the third-party notices, like every other package.

**The macOS disk image installs by drag and drop.** It now shows an Applications
shortcut next to the app, instead of leaving you to copy the bundle by hand.

macOS is still unsigned by Apple: right-click the app and choose Open on first launch.
```

### ✅ Fait le 2026-09-02 — le dernier blocage est soldé

**Les releases GitHub 0.5.2 et 0.5.4 sont SUPPRIMÉES**, leurs 17 assets avec elles : leurs
paquets bureau contenaient `tos102uk.img` + `tos162uk.img`. Les **tags git sont conservés**
— la traçabilité des versions reste, seule la diffusion cesse. Décision du mainteneur du
2026-08-30, exécutée le 2026-09-02. La commande, pour mémoire :

```sh
gh release delete 0.5.2 --yes   # release + assets ; le TAG git reste
gh release delete 0.5.4 --yes
```

Puis vérifier que le `.zip` web de la 0.6.1 porte bien `licenses/` (la garde du job
`wasm` le fait déjà échouer sinon — verte à sa première exécution le 2026-09-01).

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
