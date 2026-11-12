# Validation sur matériel réel — registre par cible

(c) 2026 VERHILLE Arnaud. **Chantier A12.** Une ligne par cible de livraison, avec
la CONFIG de la machine qui a servi. Ouvert le 2026-09-01.

## Pourquoi ce fichier

NeoST livre huit paquets et **aucun n'avait été validé sur du matériel réel** : la CI
construit, elle n'exécute pas ; le perfbench garde des *ratios* — machine-indépendants
par construction, donc incapables de dire si une cible tient le temps réel ; et l'APK
n'avait jamais quitté QEMU. Un émulateur temps réel qu'on n'a jamais lancé sur la
machine visée n'est pas un produit livré, c'est un pari.

Ce registre n'accepte que du **mesuré**. Une case vide vaut mieux qu'une case remplie
de bonne foi : c'est la règle qui donne sa valeur aux cases pleines.

## La mesure de débit : `--budget`

```sh
python3 tools/run_perfbench.py --budget          # lisible
python3 tools/run_perfbench.py --budget --json   # à archiver
NEOST_HEADLESS=/chemin/vers/le/binaire/LIVRÉ python3 tools/run_perfbench.py --budget
```

Il rend le **facteur temps réel** : trames/s mesurées ÷ balayage annoncé par la machine
émulée (50 Hz PAL / 60 Hz NTSC — lu sur la sortie, jamais supposé, cf. `CLAUDE.md`).
×1,0 = la cible émule tout juste à la vitesse du vrai ST. Bandes de lecture : **< 1,0**
la cible ne suit pas, interface exclue ; **< 2,0** le headless tient mais sans marge
pour le rendu, l'audio et le throttling ; **≥ 2,0** confort.

⚠ Il ne doit **jamais** être câblé dans un palier de test : un seuil absolu sur un
runner de CI est exactement le piège que l'en-tête de `run_perfbench.py` décrit. Il se
lance à la main, sur une cible, et son résultat se consigne ici. La **charge** de la
machine est capturée avec la mesure — une durée sans sa charge n'est pas une mesure.

## Le protocole d'une passe

Cinq pas. Une passe partielle se consigne comme partielle.

1. **Intégrité** — télécharger l'asset publié, vérifier sa somme contre `SHA256SUMS.txt`.
2. **Contenu** — le paquet porte ses licences ; il ne porte **aucune ROM Atari**
   (`roms/` = `etos*` seulement).
3. **Chaîne de confiance de la plateforme** — signature/scellement, et ce que le
   système RÉPOND à l'utilisateur (macOS : `codesign --verify --deep --strict`,
   `syspolicy_check distribution` ; Windows : SmartScreen ; Android : installation
   d'un APK debug).
4. **Exécution** — le binaire LIVRÉ démarre, `--version` dit la vraie version, il
   boote EmuTOS depuis les données du paquet, **et l'interface s'ouvre et rend une
   image** (ce dernier point demande une session graphique réelle).
5. **Débit** — `--budget` sur le binaire livré, résultat + config collés ici.

---

## Pas 1 et 2 — passés sur les HUIT paquets (2026-09-01)

Ces deux pas ne demandent **aucun matériel** : ils se font depuis n'importe quelle
machine, sur les assets publiés. Ils sont donc soldés pour toute la surface livrée,
là où les pas 3 à 5 dépendent de la cible.

| Paquet | Somme | ROM Atari | Disquettes | Licences |
|---|---|---|---|---|
| `macOS-universal2.dmg` | ✅ | ✅ aucune | 3 démos + `diskA` | ✅ 3 |
| `windows-x86_64.zip` | ✅ | ✅ aucune | 3 démos + `diskA` | ✅ 3 |
| `x86_64.AppImage` | ✅ | ✅ aucune | 3 démos + `diskA` | ✅ 3 |
| `aarch64.AppImage` | ✅ | ✅ aucune | 3 démos + `diskA` | ✅ 3 |
| `raspberry-aarch64.AppImage` | ✅ | ✅ aucune | 3 démos + `diskA` | ✅ 3 |
| `pi400-aarch64.AppImage` | ✅ | ✅ aucune | 3 démos + `diskA` | ✅ 3 |
| `android-arm64-debug.apk` | ✅ | ✅ aucune | `diskA` seul (voulu) | ✅ 3 |
| `web-wasm.zip` | ✅ | ✅ aucune | 3 démos + `diskA` | ❌ **AUCUNE** → corrigé |

**Méthode.** `shasum -a 256 -c SHA256SUMS.txt` pour le pas 1 — **les huit passent**.
Pour le pas 2 : `unzip -l` sur les archives ; pour le bundle web, le manifeste
`files:[…]` d'`index.js`, qui est la liste FAISANT FOI de ce qu'`index.data` embarque
(17 entrées, EmuTOS seul) ; pour les quatre AppImage, `unsquashfs` n'existant pas sur ce
poste, `tools/appimage_ls.py` décompresse directement la table des répertoires
squashfs (`superbloc → directory_table_start`, blocs de métadonnées gzip) et en sort les
noms. ⚠ Sa sortie est une liste de NOMS, pas un inventaire exact : quelques entrées
portent un caractère parasite (`etos256fr.imgX`). Sans effet sur la question posée — on
cherche la présence de `tos*.img` et l'absence de `GPL-3.0.txt` — mais à savoir.

**Ce que ces deux pas ont établi** — la promesse « paquets 100 % libres » n'était
vérifiée que sur le *script* qui fabrique. Elle l'est désormais sur **les huit artefacts
réellement servis** : aucune ROM Atari nulle part, y compris dans le bundle web dont
c'était le troisième canal (fermé le 2026-08-30, ici re-vérifié sur le paquet publié).

🐞 **Et ils ont trouvé une non-conformité GPL encore vivante** : le paquet **web** ne
contenait **aucun texte de licence** — quatre fichiers, `index.*`, rien d'autre — et sa
page ne mentionne ni GPL ni licence. Or il est distribué **deux fois** : le `.zip` de la
release ET le site GitHub Pages. C'est exactement la non-conformité corrigée le
2026-08-19 pour les paquets de bureau ; le job `wasm` avait été oublié, et il était l'un
des **deux** jobs de paquet sans garde de licence. Corrigé le 2026-09-01 : le job pose
les trois fichiers dans `wasm/licenses/`, les vérifie sur le patron des six autres, et
les zippe — le site Pages les reçoit aussi, l'artefact Pages étant le dossier `wasm/`.
**Après ce correctif, huit jobs distincts gardent les licences** — ce que
`docs/RELEASE.md` affirmait déjà alors qu'ils n'étaient que sept.
L'autre job non gardé, `android`, s'est révélé sain : `stage_assets.sh` pose les trois
licences sous `set -euo pipefail`, donc un `cp` qui échoue casse le build. Garantie par
construction, aucune garde redondante ajoutée.
⚠ Ce correctif est de la **CI** : il ne sera exercé qu'à la prochaine construction de
release. Vérifié ici autant que ce poste le permet — YAML valide, et la logique de
l'étape rejouée à la main produit bien un zip portant `licenses/`.

📌 Reste ouvert (UX, pas conformité) : la page web ne **renvoie** vers aucune licence.
Livrer les fichiers satisfait la GPL ; un lien en pied de page serait mieux.

---

## macOS arm64 — ◐ passe faite le 2026-09-01, le VISUEL reste

**Cible** : MacBook Air M1, 8 cœurs, 8 GiO, Darwin 24.6.0 (macOS 15.6), charge 1 min
≈ 10,8 pendant la mesure (indexation Spotlight — les chiffres tombent malgré tout à
2 % de la référence du 2026-08-26, l'estimateur « meilleur de 3 » fait son office).
**Paquet** : `NeoST-0.6-macOS-universal2.dmg`, 19 605 735 o, publié 04:07:42 UTC.

| Pas | Résultat |
|---|---|
| 1. Intégrité | ✅ `90301eca…b8351` == `SHA256SUMS.txt`. Le `.dmg` a été **remplacé après** la publication du fichier de sommes (04:07 vs 04:05) — vérifié : les sommes sont bien celles de l'asset servi. |
| 2. Contenu | ✅ `roms/` = `etos192fr/us` + `etos256fr/us` seulement ; `disks/` = `nocooper`, `closure`, `cuddly_demos`, `diskA.st` ; `licenses/` = `GPL-2.0`, `GPL-3.0`, `THIRD-PARTY`. **La promesse « 100 % libre » est vérifiée sur le paquet réellement publié**, pas seulement sur le script qui le fabrique. |
| 3. Confiance | ◐ `.app` : `adhoc`, `Sealed Resources version=2 rules=13 files=27`, `Info.plist entries=9`, *valid on disk* + *satisfies its Designated Requirement*. **Le palier 0 tient sur l'artefact publié.** `syspolicy_check distribution` ne relève qu'**un seul** défaut fatal : *Notary Ticket Missing* — la cause du cul-de-sac « NeoST est endommagé » a bien disparu, il ne reste que la notarisation. Le `.dmg` lui-même, en revanche, **n'est pas signé du tout**. |
| 4. Exécution | ◐ Les deux binaires sont vraiment `universal2` (`x86_64 arm64`). `--version` dit `0.6`. Le headless livré boote EmuTOS et monte `diskA.st` depuis le paquet monté. **Le rendu de l'interface n'a PAS pu être observé** (cf. ci-dessous). |
| 5. Débit | ✅ binaire **livré** : boot ×34,56 / blitter ×31,06 / MFP ×26,88 → pire ×26,88, **confort**. Build de dev natif sur la même machine : ×26,44. **Le paquet universal2 ne coûte rien de mesurable** face au build natif. |

**Ce que cette passe a trouvé — et corrigé le jour même**

- 🐞 **Le paquet ne pouvait JAMAIS enregistrer sa configuration, chez tout premier
  utilisateur.** Sur le `.dmg` monté, `Contents/` est en lecture seule : la règle A36
  retombe donc, correctement, sur la config utilisateur `~/.config/neost/neost.cfg` —
  mais **rien ne créait ce dossier**, l'ouverture échouait, le repli sur le répertoire
  courant échouait aussi (cwd = `/` au lancement Finder), et le paquet annonçait
  `[cfg] cannot write … configuration NOT saved` à chaque lancement. Tous les réglages
  perdus à chaque fermeture, en silence côté interface.
  **Prouvé par expérience, pas par lecture** : (A) sans le dossier → message ;
  (B) dossier créé à la main, *rien d'autre changé* → config écrite. Correctif :
  `writeConfigAtomic` crée le dossier parent du chemin retenu (`src/gui/AppConfig.cpp`),
  et le message d'erreur nomme désormais le chemin VOULU — c'est son écrasement par le
  repli qui rendait le défaut illisible dans le journal. Revérifié dans les conditions
  exactes du défaut (bundle chmod `a-w`, `~/.config/neost` supprimé) : plus de message,
  `neost.cfg` écrit.
- ✅ **Le `.dmg` ne contenait pas de lien vers `/Applications`** — pas d'installation par
  glisser-déposer, il fallait copier le `.app` à la main. **Corrigé le 2026-09-01** :
  `package_macos.sh` monte un dossier de présentation (le `.app` + le lien) au lieu de
  passer le seul bundle à `hdiutil`. ⚠ `ditto` et non `cp -R` — lui seul préserve la
  signature — et le sceau est vérifié APRÈS la copie, sinon on rejoue le « NeoST est
  endommagé » que le palier 0 vient d'éteindre. Disposition et sceau vérifiés sur un
  `.dmg` réellement fabriqué et monté ici.
- 📌 Pour la **notarisation** (palier 1, 99 $/an) : `syspolicy_check notary-submission`
  bute d'abord sur `Contents/MacOS/neost-headless`. La recette de `docs/RELEASE.md`
  prévoit déjà de signer le binaire secondaire en premier — elle vise juste.

**Ce qui reste sur cette cible** : le pas 4 visuel. Lancé depuis une session
d'automatisation, le `.app` prend la barre de menus mais **aucune fenêtre n'apparaît**.
Le **build de dev se comporte à l'identique** sur la même machine — le contrôle est
donc négatif : rien n'incrimine le paquet, c'est le contexte d'exécution qui ne donne
pas de fenêtre. À reprendre **à la main, dans une session graphique**, en une minute :
ouvrir le `.app`, voir le bureau EmuTOS, lire le compteur `fps` de la barre d'état.

## Ce qui se vérifie STATIQUEMENT, sans la cible (2026-09-01)

Les pas 3 à 5 demandent la machine. Deux classes de défaut de lancement se tranchent
pourtant depuis n'importe quel poste, sur l'artefact publié — et ce sont précisément
celles qui font échouer un premier démarrage.

**Windows — dépendances DLL : ✅ AUCUNE manquante.** La table d'importation PE des deux
exécutables ne cite que des DLL **système** — `KERNEL32`, `USER32`, `GDI32`, `OPENGL32`,
`WINMM`, `WS2_32`, `SHELL32`, `msvcrt` — et le `.zip` n'embarque aucune DLL parce qu'il
n'en a pas besoin : le build MinGW est statique. Le classique « il manque
`libstdc++-6.dll` » est donc **exclu**, sans machine Windows. Architecture confirmée
`x86-64`, PE32+.

**Raspberry Pi — plancher glibc : déjà gardé en CI, vérifié.** Le job `raspberry`
extrait le symbole `GLIBC_x.y` le plus haut (`objdump -T`) et **échoue** au-delà de
`GLIBC_2.36` (bookworm / Pi OS). Rien à ajouter — le contrôle existe et il est au bon
endroit.

**Et le constat qui va avec** : la CI en fait plus que ce qu'A12 laissait entendre.
**Six jobs** (`linux-bionic`, `linux-arm64`, `raspberry`, `pi400`, `macos`, `windows`)
lancent `tools/smoke_package.sh` sur le paquet — version, boot EmuTOS, disquette
embarquée, HD GEMDOS. Ce qui manque n'est donc pas « le paquet ne démarre jamais » mais
trois choses précises : **l'interface** (aucun affichage en CI), **le matériel réel**
(thermique, GPU, budget temps réel d'un Pi, un vrai téléphone) et **le chemin
d'installation d'un utilisateur** (Gatekeeper, SmartScreen, glisser-déposer).

### ⚠ Le trou structurel que cette passe a mis au jour

**Les six smoke-tests tournent sur un dossier EXTRAIT, donc INSCRIPTIBLE** —
`squashfs-root/usr`, `dist/NeoST.app/Contents`, `_check/*/`. Le **support de livraison
réel** — image montée en lecture seule — n'est jamais exercé. C'est exactement ce qui a
laissé passer le défaut de configuration : dans un dossier extrait, le chemin portable
est inscriptible et la règle A36 n'atteint jamais la branche « config utilisateur ».

**Portée réelle de ce défaut : 5 paquets sur 8, pas seulement macOS.** Aucun paquet ne
livre de `neost.cfg` portable (vérifié sur les huit), donc tout support monté en lecture
seule bascule sur `~/.config/neost/` — c'est le cas du `.dmg` **et des quatre AppImage**,
dont le contenu est monté en lecture seule par construction. Le `.zip` Windows y échappe
(l'utilisateur l'extrait dans un dossier inscriptible), l'APK a sa propre logique de
stockage, le bundle web n'est pas concerné.
⚠ Pour le `.dmg`, c'est **mesuré** ; pour les AppImage, c'est **déduit** de trois faits
vérifiés (aucun `neost.cfg` livré, montage en lecture seule, règle A36) — pas exécuté,
faute de machine Linux. La distinction est maintenue exprès.

**La garde SYSTÈME est posée** (2026-09-01, après coup) : `smoke_package.sh` reçoit une
**5ᵉ phase** qui retire le droit d'écriture sur le paquet, relance le binaire livré
depuis un répertoire EXTÉRIEUR et vérifie qu'il démarre en rendant une image **sans rien
déposer dans son paquet**. Elle ne prouve pas le réglage lui-même — l'écriture de
`neost.cfg` est le fait du binaire GUI, qu'aucune CI ne peut lancer faute d'affichage —
mais la PROPRIÉTÉ dont ce défaut n'était qu'un cas. Mutation : une écriture délibérée
dans le paquet est détectée ; les permissions sont rendues même en cas d'échec.
⚠ `chmod` et non un vrai montage : celui-ci dépend de la plateforme (hdiutil, FUSE,
root) alors que le retrait du droit d'écriture capture la même propriété partout. Sous
MSYS2 il est largement inopérant — la phase y est faible, et c'est écrit dans le script
plutôt que sous-entendu.
📌 Trouvé en la posant : `neost-headless` résout `disks/diskA.st` par rapport au
RÉPERTOIRE COURANT, là où le frontend GUI résout par rapport au binaire. C'est toujours
vrai (revérifié le 2026-11-12, disquette par défaut rétablie le même jour) :
- **sans `--disk`**, la disquette par défaut `disks/diskA.st` est cherchée dans le
  répertoire courant ; lancé d'ailleurs que du paquet, le headless ne la trouve pas et
  démarre **lecteur vide, sans échec** (`drive A: empty (default disks/diskA.st not
  found)`). Trouvée, elle est montée et ses écritures restent en RAM : le fichier
  livré n'est jamais modifié ;
- **`--disk` relatif** se résout lui aussi par rapport au répertoire courant, mais un
  média NOMMÉ et introuvable fait **échouer** le run (code 1).

Sans conséquence pour l'utilisateur (le headless est l'outil de débogage, lancé depuis
le dépôt) ; la phase 5 passe donc ROM et disquette en chemins **absolus**.

**La garde UNITAIRE est posée et vérifiée par mutation.** `tests/selftest_logic.cpp` teste
désormais l'ÉCRITURE et non plus seulement la règle : écrire une config dans un dossier
qui n'existe pas encore doit réussir et créer le dossier. `src/gui/AppConfig.cpp` entre
dans `neost-selftest` pour cela (il ne dépend pas d'ImGui). Mutation : correctif retiré
→ **2 FAIL**. Le test voisin d'A36 était PUR — il prouvait quel chemin la règle retient,
jamais qu'on sache y écrire ; le défaut s'est logé dans l'intervalle exact entre les deux.

## Windows x64 — ⭘ jamais lancé hors CI

`NeoST-0.6-windows-x86_64.zip`. Aucune machine Windows ici. Le `.zip` n'est pas signé
(décision assumée : token FIPS/HSM obligatoire depuis 2023, et SmartScreen n'est qu'un
clic, pas un cul-de-sac — cf. `docs/RELEASE.md`). Le protocole ci-dessus s'applique tel
quel ; `run_perfbench.py --budget` gère déjà le suffixe `.exe`.

## Android arm64 — ◐ émulateur arm64 API 36, jamais posé sur un appareil

`NeoST-0.6-android-arm64-debug.apk`. Le cœur seul a été validé sous `qemu-aarch64`
(2026-08-11, image et son identiques au bit près, cf. `packaging/android/README.md`) ;
l'APK **entier** a tourné sur l'**émulateur Android arm64 API 36** le 2026-11-12. Ce
n'est **pas** un appareil réel : la case « appareil » reste vide — rien ici ne dit le
GPU (l'émulateur rend en SwiftShader), le tactile, l'audio ni la thermique d'un vrai
téléphone.

**Config** : MacBook Air M1, 8 cœurs, 8 GiO, macOS 15.6 ; émulateur 35.4.9, AVD
`Medium_Phone_API_36` (image `google_apis_playstore/arm64-v8a`, 1080×2400, rendu
`swangle_indirect`), lancé `-no-window -no-audio -no-snapshot -cores 4 -memory 3072`.
**APK** : artefact CI `NeoST-dev-ede89f6-android-arm64-debug.apk` (run « Artefacts
(release) » 43198943692), puis un APK debug construit sur ce poste avec la page ROM.

| Pas | Résultat |
|---|---|
| 1. Intégrité | — artefact de CI, pas un asset publié : pas de `SHA256SUMS.txt` à confronter. |
| 2. Contenu | (cf. tableau des pas 1-2 ci-dessus, release 0.6.) |
| 3. Confiance | ✅ `adb install -r` d'un APK debug : *Success*, sans avertissement. |
| 4. Exécution | ✅ boot EmuTOS (logo puis bureau GEM) depuis les assets déballés ; souris au doigt ; **clavier** : `Ctrl+R`, `Ctrl+O`, `Ctrl+N`, saisie, Échap, Entrée reçus par le ST (captures, recette au README) ; **ROM** : bascule `etos192fr` (50 Hz, saisie AZERTY par la table FR), reprise au relancement ; `etos256fr` en STE. Défaut trouvé et corrigé : « REWIND: OFF » débordait de son bouton dans l'en-tête du menu. |
| 5. Débit | — non mesuré : `--budget` vise le headless, et un émulateur sur Mac ne dirait rien d'un téléphone. |

⚠ **Piège de l'AVD par défaut** (1 cœur, 2 Go) : la mémoire de l'invité swappe,
SystemUI part en ANR, et l'application ne reçoit plus rien — on croit à un défaut du
frontend. `-cores 4 -memory 3072` le lève.

## Raspberry Pi (borne) — ⭘ aucun budget temps réel mesuré

`NeoST-0.6-raspberry-aarch64.AppImage` / `-pi400-`. **C'est la cible qui motive A12** :
le mode borne est un objectif déclaré du projet, et personne ne sait si un Pi tient le
temps réel. `--budget` a été écrit pour ça, et `_cpu_name()` interroge
`/proc/device-tree/model` en premier — c'est là que le Pi met son identité. Une passe
prend cinq minutes le jour où un Pi est branché.

## Linux x86_64 — ⭘ non consigné

`NeoST-0.6-x86_64.AppImage` / `-aarch64`. La plateforme est un poste de dev du projet
(CachyOS), mais aucune passe sur le PAQUET n'est consignée.
