# Dossiers des releases passées (archive)

> **Archive.** Extrait de [`../RELEASE.md`](../RELEASE.md) le 2026-11-10 : dossiers « jour J » et notes
> publiques des versions déjà publiées. La procédure en vigueur est dans `RELEASE.md`.

## Dossier 0.6.3 — préparé le 2026-11-10

✅ **Exécuté le 2026-11-10.** Tag `0.6.3` posé sur `355b6c0`, Release GitHub publiée
(9 paquets) ; issue #44 fermée.

Numéro **0.6.3**, explicitement demandé par le mainteneur, y compris pour les
fonctionnalités NetUSBee/NAT (exception à la règle MINOR du pas 1).
CMake et CHANGELOG sont alignés ; les [notes publiques en anglais](../release-notes/0.6.3.md)
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
[`docs/HW_VALIDATION.md`](../HW_VALIDATION.md).

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
