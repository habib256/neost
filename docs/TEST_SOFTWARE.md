# Logiciels étalons — cas limites d'émulation

> (c) 2026 VERHILLE Arnaud — catalogue des jeux/démos/suites qui poussent un émulateur ST
> dans ses retranchements, classés par **sous-système** matériel.
>
> Complète la **méthode imposée** (`CLAUDE.md`) : comparer les implémentations
> (`extern/hatari/src`), vérifier leur accord avec le matériel, puis valider
> avec du logiciel réel. Ne pas reproduire un défaut confirmé d’Hatari. Chaque cible ci-dessous
> exerce un effet non documenté (affichage détourné, timing au cycle près, astuce de
> synchro HW) qui ne sort que si l'émulation est fidèle. Les chantiers correspondants
> sont dans [`TODO.md`](../TODO.md) ; le rendu attendu se valide à l'oracle Hatari
> ([`HATARI_AUTOMATION.md`](HATARI_AUTOMATION.md)) et l'instant d'IRQ au cycle via
> [`CYCLE_ACCURACY.md`](CYCLE_ACCURACY.md).

⚠ **Redistribution** : seules les démos freeware (Cuddly Demos, Union Demo, suites de test
HW) sont rapatriables via `tools/fetch_disk.py` (domaine public / freeware). Les jeux
commerciaux (Dungeon Master, Xenon 2, Arkanoid, Enchanted Land, Turrican) ne le sont PAS —
tester avec ses propres images.

## ⚠ Configuration : PAL/NTSC et couverture de la suite

**C'est la ROM TOS qui fixe la fréquence de balayage au boot**, et cela change l'image
affichée par les démos :

| ROM | Fréquence |
|-----|-----------|
| suffixes **`uk` / `fr` / `de` / `es`** (`tos102uk`, `tos162uk`, `tos206fr`, `etos192fr`, `etos256fr`…) | **50 Hz — PAL** |
| suffixe **`us`** (`tos102us`, `tos162us`, `tos206us`, `etos192us`, `etos256us`) | **60 Hz — NTSC** |

Vérification en une commande — `./build/neost-headless roms/<rom>.img --frames 120`
affiche `[headless] video: 416x276 @ NN Hz`.

**Conséquence mesurée (2026-08-01)** : les images **Spectrum 512** de
`spectrum_512_auto_diapo.st` sont calculées pour le 50 Hz. Sous une ROM NTSC, les
commutations de palette par ligne ne tombent plus au bon endroit : l'image sort
**déchirée/tramée** au lieu du dégradé 512 couleurs — **sur ST comme sur STE**, et
**à l'identique sous Hatari (0 px des deux côtés)**. C'est donc FIDÈLE, pas un bug ;
le remède est une ROM européenne. Repères d'aire non-noire pour la diapo « coucher de
soleil » : **0,555 = rendu correct (PAL)**, **0,475 = déchiré (NTSC)**.

**Couverture de la suite** (recomptée 2026-10-20 : 43 étalons « machine », hors les
11 auto-tests) : `st` ×23 et `ste` ×20 (dans le manifeste) ; `tos102uk` ×3 (50 Hz) + `tos162us` ×1 (60 Hz)
— **ROM Atari propriétaires** —, `etos192fr` ×28 (50 Hz) + `etos256us` ×11 (60 Hz)
— **ROM libres** ; 512 Ko ×39 et 1 Mo ×4 ; `fastfdc` ×35. Donc **MegaST, TOS
1.00/1.04/1.06 ne sont couverts par AUCUN étalon** (le MegaSTE et TOS 2.06, eux, le sont
depuis le 2026-08-27 par `tools/run_megaste_diag.py`, hors de ce manifeste). Une suite verte ne prouve rien hors de ces
configurations : devant un rapport « ça plante dans le GUI », commencer par lire
`neost.cfg` et rejouer la scène avec `--from-cfg neost.cfg --disk <image>`
(+ `--shot-every N PRÉFIXE` pour voir *où* ça casse) avant de soupçonner une
régression. Vérifié le 2026-08-01 : No Cooper, Cuddly Demos, Enchanted Land et
Lethal Xcess « en panne » tournaient en `machine=megast` — tous corrects en `st`.

⚠ **Ce que « en panne » veut dire, mesuré le 2026-08-30 sur No Cooper** : ce n'est pas
une image fausse, c'est un **halt CPU**. En `machine=megast`, dès que la démo prend la
touche qui fait avancer la partie, le 68000 part en **double faute de bus** (exception
vecteur 2) et la machine gèle. **Hatari HALTE PAREIL** sur la même configuration
(`Bus Error reading at address $ffff820f, PC=$58` → `Detected double bus/address error
=> CPU halted!`) : c'est **FIDÈLE**, pas un bug NeoST. **La cause est matérielle** : l'adresse fautive `$FF820F` est l'une des deux que le
chipset **Ricoh** du ST simple laisse « void » et que le chipset **IMP** du Mega ST fait
FAUTER (Hatari `IoMem_FixVoidAccessForST`/`ForMegaST`, ioMem.c:156-196 ; NeoST
`Bus.cpp:535-542`). Le profil machine seul est donc en cause, pas la ROM — matrice
`{megast, st}` × `{tos104fr, etos192fr, tos102uk}` : halt sur les trois `megast`, aucun
sur les trois `st`. Détail et recette d'injection clavier :
[`CASE_STUDIES.md`](CASE_STUDIES.md).

### Lignes masquées ST/STE (2026-10-19)

`tools/make_blankline_test.py` génère huit secteurs de boot autonomes libres,
sur EmuTOS PAL : `blankline_{blank,node,node_late,stop}_{st,ste}`.
Le motif RAM contient des longs successifs : un compteur vidéo qui avance
sur une ligne NO_DE ou se fige sur une ligne BLANK décale les pixels suivants.
Le fond vert distingue la palette 0 du noir. Aucune écriture palette dans
le handler : les sondes NO_DE et STOP_MIDDLE exercent le déclenchement du
rendu final sans extension de bordure, absent avant correction.

L'impulsion tardive atteint la fenêtre NO_DE du STF mais pas celle du STE
(préchargement déjà commencé). STOP_MIDDLE raccourcit DE et consomme 54 octets.
Les huit variantes `_ste` tournent sous `etos256fr` (EmuTOS 256 Ko PAL, TOS 2.06)
depuis le 2026-10-20 : sous `etos192fr` (TOS 1.04), `Machine::adjustMachineForTos`
les rétrogradait en ST et leurs références copiaient celles des `_st`. Revalidées
contre Hatari (épingle f0736b24, `--machine ste`) : `node`, `nosync(_cancel)` et
`sync_high(_cancel)` à 0 px et identiques aux `_st` (timings communs, attendu) ;
`node_late` à 0 px et différent du `_st` (10 714 px : préchargement STE) ; `blank`
capture la trame 402 (phase NO_DE, 0 px) — sur STE aucune phase ne donne le BLANK
du STF, et les phases `left+2` (32/36 cycles) divergent de 204 px sur une ligne,
artefact de recopie A40 d'Hatari ; `stop` reste un snapshot inchangé (Hatari STE :
3 px documentés).
La VBL depuis STOP laisse plusieurs phases de quatre cycles : `oracle_scan`
cherche uniquement une image identique, sans tolérance pixel, pour les six
sondes BLANK/NO_DE. Elles sont des oracles Hatari. Les deux STOP_MIDDLE sont
des snapshots NeoST : Hatari rend trois pixels de plus au dernier groupe
de plans (même résultat sur 61 captures par machine). Aucune référence
Hatari n'est admise avec une tolérance pour absorber ce résidu. Le masquage
par palette 0 est une convention comparative ; sa couleur physique n'est
pas validée par l'accord avec Hatari. NO_SYNC/SYNC_HIGH ne sont pas couverts
par ces huit premières sondes.

Complément : `blankline_{nosync,nosync_cancel,sync_high,sync_high_cancel}_{st,ste}`.
Une impulsion hi-res avant HSYNC ou pendant celui-ci inhibe DE sur la ligne
suivante ; le retour assez tôt annule l'effet. L'image contraint le masquage,
l'adresse accumulée et la ligne ajoutée au bas de l'affichage. Les mêmes
séquences dans `glueSelfTest` vérifient aussi la fenêtre de comptage Timer B,
les 32 000 octets consommés au total et l'identité des décisions live/replay.
Ce sont des comparaisons au modèle d'Hatari, pas des mesures électriques.
Les témoins annulés réécrivent la même couleur après l'impulsion pour forcer
le rendu final : une extension basse oubliée doit rester visible même si le
masque de la ligne suivante a été correctement annulé. Cette réécriture ne
change pas l'image correcte.

### ROM libres vs ROM propriétaires (depuis le 2026-08-19)

Un étalon dont la ROM est **absente** ne se comporte plus de la même façon selon la ROM
(`rom_is_free()`, `tools/run_etalons.py`) :

| ROM | Absente ⇒ | Pourquoi |
|-----|-----------|----------|
| `roms/etos*.img` (EmuTOS, libre) | **ÉCHEC** | elle est livrée avec le dépôt : son absence est une casse. |
| toute autre (`tos*.img`, Atari) | **SKIP recensé** | non redistribuable : le dépôt ne peut pas garantir sa présence. |

Le SKIP n'est jamais silencieux — la suite imprime un bloc
« ⚠ NON EXÉCUTÉS — ROM propriétaire absente (N) : … » et le code de sortie reste 0.

**Depuis le 2026-08-28, la règle vaut pour TOUT le palier `fast`** (purge § BLOQUANT,
pas 2), et elle remonte jusqu'au bilan :

| Sous-suite | Dépendance propriétaire | Traitement |
|-----------|-------------------------|-----------|
| `run_selftests.py` (`diag_cart`) | *(plus aucune)* — passée sur `etos192fr` | — |
| `run_cyclebench.py` | *(plus aucune)* — passée sur `etos192fr` | — |
| `run_midi_sequencer.py` | TOS 1.04 FR **et** Cubase Lite | **SKIP recensé**, sortie **77** |
| `run_megaste_diag.py` | TOS 2.06 + cartouche Field Service | **SKIP recensé**, sortie **77** |
| `run_etalons.py` | 4 étalons sur `tos102uk`/`tos162us` | SKIP recensé par étalon (`rom_is_free`) |

**Code de sortie 77 = « sauté, recensé »** : ni succès ni échec. `run_all.py` le distingue
et termine par « TOUS LES PALIERS OK — **COUVERTURE AMPUTÉE** », en listant les étapes
qui n'ont rien vérifié. Avant, ces sous-suites sortaient 0 : leur SKIP était imprimé au
milieu de centaines de lignes puis englouti par un « TOUS LES PALIERS OK » plein — le
« vert creux » que ce dossier combat.

Pourquoi `diag_cart` et le cycle-bench ont pu migrer sans rien changer : **leurs deux
programmes prennent la main AVANT le TOS** (cartouche $FA52235F pour l'un, cartouche bench
pour l'autre), la ROM ne sert qu'à construire la machine. Vérifié plutôt que supposé —
dump série identique octet pour octet sous `tos102uk` (50 Hz), `etos192fr` (50 Hz) et
`etos192us` (60 Hz) ; golden `tests/reference/cyclebench.json`, posé sous `tos102uk`,
passe tel quel sous EmuTOS avec sa tolérance de **0 cycle** (il n'a pas été régénéré).

Pourquoi le séquenceur MIDI, lui, ne peut PAS migrer : son scénario repose sur
l'auto-lancement `#Z` de `DESKTOP.INF` (« Install Application » du TOS 1.04), qu'EmuTOS
n'honore pas — il lit `EMUDESK.INF`. Mesuré le 2026-08-28 : sous `etos192fr`, C: est bien
monté mais Cubase ne démarre pas, **0 octet MIDI émis**, on reste sur le bureau. Et même
s'il démarrait, Cubase Lite resterait un logiciel commercial : changer la ROM ne
retirerait pas la dépendance non redistribuable.
Sans les deux ROM Atari, il reste **11 auto-tests + 39 étalons machine** (ST ×20, STE ×19 au manifeste, recompté 2026-10-20) — c'était **5** avant les deux passes d'A10 du 2026-08-28 : migration de trois démos sur EmuTOS, puis l'étalon spec512 GÉNÉRÉ.

Les **4 étalons à disque généré** (`overscan_top`, `trace_odd`, `scroll_8264`,
`scroll_8265`) ont été migrés sur EmuTOS le 2026-08-19 : leur programme est un secteur de
boot autonome (il pose lui-même résolution, palette et base écran), donc le TOS ne fait que
le charger. Vérifié plutôt que supposé — capture EmuTOS vs capture TOS propriétaire = 0 px,
oracle Hatari EmuTOS vs oracle Hatari TOS = 0 px, références inchangées.

Trois **démos** de plus ont été migrées le 2026-08-28 (chantier A10) : `cuddly_demos`,
`nocooper`, `nocooper_greetings` passent de `tos102uk` à `etos192fr`. Elles ne bootent pas
d'un secteur autonome — c'est le TOS qui les charge — mais l'image, elle, n'en dépend pas :
**seule la durée du boot change**, donc la numérotation des trames. Preuve : la capture
NeoST sous EmuTOS est **byte-identique** (0 px / 114816, crop `buffer_noled`) à la
référence oracle Hatari posée sous TOS 1.02, et les références commises sont INCHANGÉES ;
contre-épreuve à l'oracle sur `cuddly_demos` (Hatari + EmuTOS rend la même image, trame
3720 ↔ trame NeoST 3654). Recalage : `--frames` 3500 → 3655, 6802 → 6932, 29500 → 29700.
**Méthode** : balayer les trames à **pas 1** et retenir CELLE qui est à 0 px, jamais la
moins pire — sur ces écrans animés la trame voisine est déjà à 7 548 px (`cuddly_demos`)
ou 19 069 px (`nocooper`).

⚠ Les **3 étalons Spectrum 512 ne sont PAS migrables** — RÉFUTÉ À L'ORACLE le 2026-08-28,
ne pas retenter. Le disque `spectrum_512_auto_diapo.st` n'a **pas de secteur de boot
exécutable** (somme des 256 mots = $FB35, pas $1234) : la diapo est lancée par le dossier
`AUTO` (`SYNC.PRG` + `SPSLIDE8.PRG`). Sous EmuTOS le programme AUTO démarre (écran noir,
glyphes rouges illisibles vers la trame 350) puis abandonne, et le bureau GEM apparaît —
écran figé de la trame 600 à la fin. Ce n'est **pas** un bug NeoST : **Hatari + `etos192fr`
rend le même bureau** (22 px d'écart, tous dans la bande de la LED disquette d'Hatari) ;
`etos256fr` échoue identiquement. Ils restent donc sur `tos102uk` / `tos162us`, et se
sautent proprement sans ces ROM. **Leur couverture est RENDUE** depuis le 2026-08-28 par
`spec512_bands`, étalon **généré** (`tools/make_spec512_test.py`) : secteur de boot
autonome, écran entièrement à l'index 1, `palette[1]` martelée trois fois par ~100
cycles. La position horizontale de chaque bascule dépend du cycle exact de l'écriture —
**0 px contre l'oracle Hatari**, sur ROM libre. ⚠ Son image n'est pas statique (période
de 4 trames : la boucle et la trame ne sont pas commensurables) ; c'est sans conséquence,
NeoST est déterministe et l'oracle a sa fenêtre de scan. Depuis le 2026-10-20,
`spec512_view` (`tools/make_spec512_view.py`) rend aussi le motif d'un VRAI
visualiseur : 48 couleurs par ligne, les 16 registres rechargés trois fois par ligne
par rafales `movem.l`, `palette[0]` compris. **0 px contre l'oracle**, re-dérivé deux
fois ; `NEOST_RAM_SLOT=0` le fait rougir de 82 152 px.

### Les images disquette ne sont plus modifiées par les runs (A14, 2026-08-28)

`neost-headless --disk-ro` : les écritures secteur de la machine invitée restent dans
l'image **en RAM** ; le fichier hôte (`.st`, `.msa`, `.dim`, et l'overlay `.wd1772` d'une
STX) n'est jamais réécrit. Ce n'est **pas** une protection en écriture — le programme
relit ce qu'il a écrit, donc l'émulation est inchangée ; c'est le *write-through* vers le
fichier qui est coupé.

Pourquoi : deux images **suivies par git** avaient été modifiées dans l'arbre par des runs
(Eliminator le 2026-08-25, `disks/diskA.st` par le test F du diagnostic le 2026-08-27), et
`disks/etalons/` en compte 13 — une écriture invitée y ferait dériver la donnée d'entrée
d'un étalon en silence.

Preuve, sur le seul programme de la pyramide qui formate vraiment une disquette (test F du
diagnostic MegaSTE, qui écrit sur A **et** B) : avec et sans l'option, le dump série est
**byte-identique** (11 Pass, 0 Fail, « Q Tests Completed », « No VME board ») ; sans
l'option les deux fichiers changent de md5, avec l'option ils sont intacts.

Qui l'utilise : `run_etalons.py` sur **toutes** ses captures, et `run_megaste_diag.py`, qui
**échoue** si ses images sacrificielles ont bougé d'un octet — c'est le garde-fou de bout
en bout d'A14 (palier `full`). Au palier `fast`, `check_headless_options.py` ne vérifie que
l'existence et l'annonce de l'option : aucun programme invité n'y écrit sur disquette.

⚠ Hors périmètre : les images **ACSI** (`--acsi`, `--sd1/2`) et le disque **GEMDOS**
(`--gemdos`) écrivent toujours sur l'hôte. Une image ACSI se protège en la copiant.

## Ordre de débogage affichage conseillé

Du plus simple au plus violent, chaque étape suppose la précédente acquise :

1. **Spectrum 512** — ✅ **RÉSOLU** (100 % pixel-identique à Hatari, sans flicker) : stabilité
   de la palette par ligne (synchro CPU↔faisceau de base).
2. **The Cuddly Demos** — suppression des 4 bordures (timings HSYNC/VSYNC).
3. **Enchanted Land** — sync-scroll horizontal (bascule 50/60 Hz au cycle exact).

---

## 1. Shifter / synchro CPU-vidéo

Le ST n'a ni scrolling matériel, ni split-screen, ni copper : tout passe par la
réécriture des registres Shifter (fréquence 50/60/71 Hz, résolution, palette) à des
cycles précis du 68000 PENDANT le balayage. → réf. NeoST : `Shifter`, `Machine::runFrame` ;
Hatari : `video.c`, `spec512.c`.

| Étalon | Éditeur | Mécanisme | Cas limite testé | TODO NeoST |
|--------|---------|-----------|------------------|------------|
| **Spectrum 512** | Inshape | Réécrit la palette plusieurs fois par ligne → >512 couleurs affichées | Synchro cycle-près `MOVE.W` ↔ position du faisceau. Défaut : couleurs décalées verticalement, bandes/flicker | ✅ **RÉSOLU** — diaporama étalon **0 px vs Hatari** (4 images), flicker éliminé. Reste : scroll fin mi-ligne |
| **Enchanted Land** | Thalion | Scrolling horizontal pixel-près SANS Blitter : bascule 50↔60 Hz en fin de ligne pour tromper le compteur d'adresse interne du Shifter et décaler l'adresse de base | 1 cycle d'erreur → écran qui saute/déchire ou plante. Exige la géométrie variable EN COURS de trame | « Suppression de bordures » (géométrie mi-trame) |
| **No Cooper** | 1984, 1989 | Écran **greetings** : page med-res 4 bordures ouvertes (chaque ligne : hi@0/lo@12/**MED@20** + 60/50@376/384 + stab@444/456) ; écran principal : nappe raster bord à bord + scrolltext 2 px/trame | Cas d'école `Video_WriteToGlueRes` — **✅ V2 VALIDÉ 0 px vs oracle (2026-07-08)** | Étalons `nocooper` (écran principal, ~6800) et `nocooper_greetings` (`ref_kind: snapshot`, oracle archivé `nocooper_greetings_oracle.png` à 0 px, 5 espaces datés, ~29700) — fetch fujiology auto ; les deux sur **EmuTOS** depuis le 2026-08-28 (A10) |
| **The Cuddly Demos** | The Carebears (TCB), 1989 | 1ʳᵉ démo à ouvrir les **4 bordures** (haut/bas/gauche/droite) simultanément : boucles de NOP calibrées qui commutent la fréquence au moment où le canon atteint les limites de l'affichage standard. Le robot du menu est dessiné puis **effacé en course avec le faisceau** (un seul buffer) : seul un rendu échantillonné PAR LIGNE le voit (cf. `lineSnap_`, commit 08b58e1) | Précision des timings de génération HSYNC/VSYNC + tampons internes Shifter | « Suppression de bordures » (BORDERMASK_*) |
| **Closure** | Sync | Démo moderne à **rasters fins** : la palette est réécrite en cours de ligne sur des écrans denses (64→129 couleurs, puis **153 couleurs** sur la « photo fée » vers la trame 10500). Se joue en **ST + 1 Mo** ; le chemin **STE n'est PAS porté** (Hatari lui-même : « FIXME: should be measured on real STE », `video.c:3976-3982`) — ne pas la lancer sous une ROM STE-only. | **A40 : SOLDÉ le 2026-08-30 — le retrait gauche MED (`LEFT_OFF_MED`, « remove left + med stab » qu'Hatari nomme d'après cette démo, `video.c:3974-3997`).** Posée en étalon le 2026-08-30, elle a immédiatement montré un écart oracle de 64 % dont la cause était GÉOMÉTRIQUE, pas chromatique : palettes identiques, et un décalage de +4 px ST ramenait l'écart à 3,2 %. Cause établie par le calcul puis mesurée : Hatari RECOPIE la ligne dans un tampon de 208 o à partir de `raster + 2 + VideoOffset` PUIS applique `STF_PixelScroll` — son ancrage est donc déjà 4 px à droite du faisceau, d'où `shEff = 4 + scrollFinal` = **−4** et non −8. Avec la règle qui va avec (les 8 dernières colonnes du buffer, « entrées » sans source par le décalage, restent à l'index 0), l'écart tombe à **0,02 % (27 px, tous sur la ligne 0)** — le reste est suivi en A41 | Étalon `closure` (écran 153 couleurs, `--frames 10501`, ST 1 Mo, `etos192fr`) — **`ref_kind: snapshot`** tant qu'A41 n'est pas fermé, mais l'oracle est désormais COMMIS à côté (`tests/reference/closure_oracle.png`) : la self-capture garde la non-régression au pixel, l'oracle permet de re-mesurer l'écart sans relancer Hatari. Le disque est dans le dépôt (`disks/etalons/closure.msa`), pas de fetch. ~20 s, 2ᵉ mur du palier pixel. Journal d'enquête : `docs/CLOSURE_CHANTIER.md` |

### Quirk connu — PAS un bug d'émulation

- **Spectrum 512 sous ROM NTSC** : images déchirées/tramées au lieu du dégradé 512
  couleurs, parce que la ROM `…us` démarre la machine en 60 Hz (cf. § *Configuration :
  PAL/NTSC* en tête de document). Identique sous Hatari (0 px), sur ST **et** sur STE.
  Lancer ces disques avec `tos102uk`/`tos102fr`/`tos162uk`…

- **`overscan_top` : 144 px d'écart oracle qui ne se refermeront PAS** (A40, tranché le
  2026-08-30). Sur les 5 lignes de transition 60 Hz (`left+2 / right-2`), les deux
  émulateurs sont d'accord sur l'état Glue — la trace `--trace video_border_h` d'Hatari
  dit `detect left+2 / right-2 60Hz 53<->373`, exactement le masque de NeoST. L'écart
  vient de la RECOPIE d'Hatari, qui range les 2 octets « left+2 » dans les 2 derniers
  octets de la bordure gauche de son tampon : en basse résolution 2 octets ne sont pas
  4 pixels mais **un mot de plan sur 16 pixels**, d'où 16 px de plans mixtes à chaque
  bout (index 8 = $333 en tête, index 7 = $555 en queue). NeoST rend le décalage
  physique de 4 px. Détail et arithmétique : `docs/HATARI_DIVERGENCES.md` §
  *Cas où NeoST améliore Hatari*. **Cet étalon reste donc `ref_kind: snapshot`.**

- **Captain Blood** (ERE, crack 42-Crew anglais) : au chargement, le jeu scanne la ROM
  TOS à la recherche de la chaîne `AZER` (table clavier AZERTY, code en RAM `$1d69a`).
  TOS **français** détecté → affiche « KEYBOARD PROBLEM » (Cconws VT52) puis attend une
  touche et **reboote**. Comportement IDENTIQUE sous Hatari (oracle vérifié trame à
  trame) : ce n'est pas l'IKBD/ACIA. **Lancer ce crack avec un TOS US/UK** (`tos102uk`)
  → le jeu démarre (écran planète) et se joue normalement.

## 2. MFP 68901 / interruptions

Timers et IRQ poussés à fréquences extrêmes (rasters, musique, digidrums). → réf. NeoST :
`Mfp`, `Scheduler` ; Hatari : `mfp.c`, `cycInt.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **The Union Demo** (menu) | Timer B = IRQ par ligne de balayage (rasters/lignes de couleur) **+** Timer A = musique en fond | Priorité des IRQ, latence de prise en compte de l'exception 68000, **réentrance** (IRQ pendant IRQ). Défaut : blocage CPU (Line F / Bus Error) | Timer B/A faits ; reste latence IRQ au cycle (cf. `cycle-accuracy`) |

## 3. YM2149 PSG / digidrums (PCM)

Le YM2149 ne sait pas jouer d'échantillons : les jeux émulent une voix numérique en
modulant le **volume** des canaux à très haute fréquence, cadencé par le **Timer A** du
MFP (souvent >8 kHz). → réf. NeoST : `YM2149`, `Mfp::timerA_*` ; Hatari : `psg.c`, `sound.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **Xenon 2: Megablast**, **Turrican** (musiques J. Hippel / D. Whittaker) | Volume des canaux YM réécrit à chaque IRQ Timer A → voix/percussions PCM | Synchro MFP↔YM2149 parfaite. Défaut : aliasing sévère, craquements, dérive de la hauteur tonale | Timer A event-count fait ; reste « décodage son sur l'horloge d'émulation » (précision cycle) |

## 4. Contrôleur disquette WD1772 / protections

Les protections (souvent Rob Northen) exploitent des caractéristiques PHYSIQUES non
standards du flux magnétique, qu'une émulation WD1772 « haut niveau » (logique) ne
reproduit pas. → réf. NeoST : `Fdc` ; Hatari : `fdc.c`, `floppies/stx.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **Dungeon Master** | FTL | « **Fuzzy bits** » : flux affaibli volontairement → le WD1772 lit alternativement 0 ou 1 à chaque passage. + secteurs de tailles exotiques (8192 o) | Fidélité au flux physique (format `.STX`/`.IPF`) : timing de rotation exact + registres d'erreur du contrôleur. Une émulation logique échoue à lancer le jeu | « Support STX (Pasti) » + « Timing réel » (FDC cycle-exact) |

## 4 bis. Séquenceurs MIDI — Cubase Lite joue un SMF (2026-08-23)

Le séquenceur exerce d'un coup l'ACIA 6850 (`$FFFC04/06`, TDRE/TIE à 31 250 bauds),
le Timer A du MFP (horloge MROS), le GEMDOS HD (`Pexec`, `Fopen/Fread`) et le
convertisseur `tools/midi_simplify.py`. → réf. NeoST : `MidiAcia`, `Mfp`, `GemdosHd` ;
Hatari : `acia.c`, `midi.c`, `gemdos.c`.

| Étalon | Configuration | Ce qui est vérifié | Recette |
|--------|---------------|--------------------|---------|
| **Cubase Lite** (Steinberg 1996, MROS, sans clé) + `disks/midi/BLUES/ALBERTAM.MID` | **TOS 1.04 FR**, Mega ST, 1 Mo, mono, `--gemdos disks/midi` (EmuTOS : MROS panique, incompatibilité connue) | 200 notes : hauteur, ordre, **vélocité**, durée (±12 ms + 0,2 %), **pédale CC64**, **pente de tempo** (1,001, tolérance ±0,5 %), **gigue σ < 5 ms** (mesuré 0,4-1,7 ms) | `python3 tools/run_midi_sequencer.py` (palier `fast`, ≈3 s ; **SKIP recensé + sortie 77** si le TOS 1.04 FR ou Cubase Lite manquent) ; `--song disks/midi/CHOPIN/RAINDROP.MID` pour une autre pièce ; `--keep` garde `midi.log` + la capture |

Mécanique : `DESKTOP.INF` auto-lance `CB_LITE.PRG` (ligne `#Z`, TOS 1.04) ; souris
relative `--mouse-at` vers *File → Import…* ; `--azerty` pour taper `SONG.MID` dans le
sélecteur (TOS FR = AZERTY, sinon le M se perd) ; `|` = Enter du pavé = Play ;
`--midi-dump` journalise chaque octet MIDI OUT daté du cycle 68000 ;
`tools/midi_compare.py` compare (ou convertit le journal en SMF : `--to-smf`).

Quirks **de Cubase Lite** mesurés ainsi (pas des bugs NeoST, cf. `disks/midi/README.md`) :
note-off émis un tick interne en avance (−1,6 ms à 150 bpm, −5 ms à 55 bpm) ;
doublure à l'unisson ou note répétée sans trou → note coupée à 2 ms (`--detach` du
convertisseur) ; **armure (méta 0x59) en cours de morceau → piste de notes jetée à
l'import** (le convertisseur ne garde que celle du tick 0).

## Suite headless NeoST (`tools/run_etalons.py`)

Infra de non-régression par captures PPM (déterministe, sans GUI) :

```sh
# 1. Rapatrier les disques freeware listés dans tools/etalons.json
python3 tools/fetch_etalons.py

# 2. Générer les captures de référence (1ʳᵉ fois ou après correctif validé)
python3 tools/run_etalons.py --update-ref

# 3. Régression (compare NeoST vs tests/reference/*.ppm)
python3 tools/run_etalons.py

# Sous-ensemble + oracle Hatari pour une nouvelle référence externe
python3 tools/run_etalons.py --only spectrum512_diapo --oracle
python3 tools/compare_screenshot.py tests/out/foo_neost.ppm tests/reference/foo.png --crop active
```

Étalons intégrés aujourd'hui : **glue_selftest** (+ `glue_selftest_attr`), **spec512_selftest**, **bus_selftest**,
**mfp_selftest**, **serloop_selftest**, **msa_selftest**, **enec_selftest**, **gemdos_selftest**,
**usatan_selftest**, **netusbee_selftest** (P0, logique pure),
**EmuTOS STE boot**, **spectrum512_diapo** + **spectrum512_diapo2** (ST) +
**spectrum512_diapo_ste** (STE, scramble FIDÈLE == oracle Hatari STE), **overscan_top**,
**trace_odd**, **scroll_8264** / **scroll_8265** (scroll fin STE), **nocooper** et
**nocooper_greetings** (V2, réf. oracle archivée), **closure**, et les sondes générées
(`spec512_bands`/`spec512_view`, `freq_switch`, `scroll_336*`, `blitter_timer`/`blitter_hog`,
`mfp_poll`, `dmasnd_poll`, `vblank_on`/`off`, 16 `blankline_*`) ; fetch auto : **Cuddly Demos**
(`disks/etalons/cuddly_demos.msa`), **No Cooper** (`disks/etalons/nocooper.msa`),
**union_demo** (`optional` : SKIP tant que la disquette n'est pas rapatriée).
Soit **54 entrées** dans `tools/etalons.json` (2026-10-20) — 11 auto-tests + **43 étalons machine**, dont
le `_comment` du fichier rappelle la couverture réelle (ni MegaST, ni TOS 1.00/1.04/1.06).
L'auto-test `glue_selftest_attr` (A16b, 2026-08-28) : le MÊME auto-test Glue
rejoué avec le verrou `NEOST_LINELEN_ATTR=0` (grille fixe, l'A/B depuis que V3 est le défaut, 2026-09-01). Un chemin que
personne n'exécute pourrit — celui-là segfautait depuis des semaines sans qu'aucun palier
ne puisse le voir. Un champ `env` du manifeste sert à ça : rejouer un auto-test sous un
verrou d'émulation, pour ~0,1 s.

### Auto-tests logique pure (P0 — ms, sans boot ni oracle)

```sh
./build/neost-selftest                                        # logique PURE : chemins hôte, neost.cfg
./build/neost-headless roms/etos256us.img --glue-selftest      # machine Glue (bordures)
./build/neost-headless roms/etos256us.img --spec512-selftest   # re-rendu Spectrum 512 (borderless + bordé)
./build/neost-headless roms/etos256us.img --bus-selftest       # whitelist bus error (par octet)
./build/neost-headless roms/etos256us.img --mfp-selftest       # GPIP forcé / fronts AER-DDR / Timer B
./build/neost-headless roms/etos256us.img --msa-selftest       # ré-encodage .msa (aller-retour)
./build/neost-headless roms/etos256us.img --enec-selftest      # NE2000/EtherNEC (bouclage)
./build/neost-headless roms/etos256us.img --usatan-selftest    # UltraSatan (fil ACSI : INQUIRY, paquets 'US', RTC)
./build/neost-headless roms/etos256us.img --netusbee-selftest  # NetUSBee (ISP1160 + NE2000 en coexistence)
python3 tools/run_cyclebench.py [--update]                    # golden du modèle de cycle 68000
```

`--spec512-selftest` construit une RAM vidéo synthétique (tous pixels = index 1), injecte des
écritures palette datées et vérifie **octet-exact** la couleur de chaque pixel contre le modèle
`f(kSpec512AlignCyc, géométrie)`. Toute dérive d'alignement palette↔pixel (la cause du « scramble
spec512 ») décale les frontières → exit 1. La constante elle-même est ÉPINGLÉE par la première
vérification du test (`chk("kSpec512AlignCyc", …, -25)`) : la changer suffit à faire tomber le
test — c'est le contrôle négatif. (L'ancien `NEOST_ALIGN_OFF` n'existe plus : le poser ne fait
plus rien, le test reste vert.) Intégré à la suite via `run_etalons.py` (type `spec512_selftest`).

### Auto-tests à verdict série (P1 — s, déterministe, sans oracle)

Convention : une ROM écrit sur le port série RS-232 (UDR `$FFFA2F`, capturé par
`--serial-dump FILE`) une ligne par test :

```
NEOST-TEST: <nom> PASS
NEOST-TEST: <nom> FAIL <détail>
```

```sh
python3 tools/make_selftest_cart.py disks/etalons/selftest_cart.bin   # cartouche diagnostic $FA52235F
python3 tools/run_selftests.py                                        # génère si absent, lance, scanne, exit 0/1
python3 tools/run_selftests.py --list
```

`tools/make_selftest_cart.py` produit une **cartouche diagnostic** (magic `$FA52235F` → le TOS saute
à `$FA0004` au reset, pré-TOS, sans disque) qui teste :
- **`cpu`** — invariants arithmétiques (garde-fou cœur 68000) ;
- **`timing`** — sentinelle liveness : le compteur vidéo `$FF8209` n'est pas figé (anti-clock-morte) ;
- **`frame`** — cycle-exact : installe les vecteurs HBL/VBL (`$68`/`$70`) et **compte les HBL par trame**
  par interruptions (262 pré-TOS, déterministe) → flague une dérive grossière (50 Hz→313, 71 Hz→501) ;
- **`ipl`** — latence d'exception : le handler HBL de la ligne 100 fait un délai puis lit `$FF8209`
  (position faisceau = phase d'entrée IACK+prologue, 224±4 déterministe).

`--break cpu|timing|frame|ipl` force le FAIL correspondant (valide que le runner l'attrape). Le verdict
**`fpu`** vient de `make_fpu_testrom.py` (Mega STE + `--fpu`, 54 tests MC68881), qui émet désormais
`NEOST-TEST: fpu PASS|FAIL` sur le série (en plus de `D7` pour la trace). Le runner
`tools/run_selftests.py` (manifeste `tools/selftests.json`) lance le headless avec `--cart`/rom +
`--serial-dump`, scanne les verdicts et sort 0/1.

- **`usatan_netusbee`** (2026-08-21) — carte SD `disks/etalons/usatan_sd.img` (générée par
  `tools/make_usatan_hd.py` : 16 Mo, partition GEM FAT16 portant `AUTO\USTEST.PRG`), EmuTOS
  192 Ko, `--ultrasatan --sd1 … --netusbee`. EmuTOS amorce sur la carte, monte C: et lance le
  PRG (`tools/make_usatan_test.py`, relogeable, `Super()`), qui parle à l'UltraSatan **comme
  `US_CONF.TOS`** (séquence LongRW, attente IRQ GPIP5) et au NetUSBee **comme le pilote FreeMiNT**
  (lectures MOT, primitives raw). ⚠ Règles EmuTOS apprises ici : disque dur présent ⇒ disquette
  non amorcée ; secteur racine exécuté seulement sans partition reconnue ; FAT12/16 par le
  nombre de clusters (> 4084 ⇒ FAT16), d'où 16 Mo. Variante disquette (`A:\AUTO`, même PRG) :
  `tools/make_usatan_test.py OUT.st`, à utiliser SANS carte SD.
  **Contrôle négatif Hatari** (vérifié 2026-08-21) : la même carte sous l'oracle —
  `hatari --machine st --tos roms/etos192us.img --acsi disks/etalons/usatan_sd.img --rs232-in
  /dev/null --rs232-out OUT --run-vbls 900` — monte C: et lance le PRG (`uscdrv PASS`) mais rend
  **FAIL** sur les six verdicts matériels : Hatari n'a ni UltraSatan ni NetUSBee. Le test
  discrimine donc bien l'émulation des cartes, il ne passe pas « par construction ».

### Orchestration par paliers + hook pre-push

```sh
python3 tools/run_all.py --tier fast      # P0 (logique pure + tous les *_selftest) + P1 (verdicts série)
                                          # + cycle-bench + round-trip save-state + disquette
                                          # livrée + boot GUI + 4 étalons pixel rapides
                                          # — ~12 s (mesuré 2026-08-27)
python3 tools/run_all.py --tier full      # fast + P2 (étalons pixel + --verify-refs)
python3 tools/run_all.py --install-hook    # hook git pre-push (opt-in) → lance --tier fast
python3 tools/run_all.py --uninstall-hook
```

### Étalons qui BOOTENT un disque : `oracle_scan` (2026-08-19)

Hatari sème son RNG sur `time(NULL)` et s'en sert pour la position angulaire initiale de
la disquette : **la numérotation des trames de son AVI change d'un run à l'autre**. Un
`frame:` figé ne peut donc pas servir de référence oracle pour un étalon qui boote un
disque. Les entrées concernées (32 au 2026-10-20) portent `oracle_scan: N` : `--oracle` extrait la fenêtre
`[frame−N, frame+N]` et retient la trame **identique** à la capture NeoST (jamais la moins
pire ; aucune correspondance ⇒ échec bruyant = vraie divergence). Détail et mesures →
`docs/HATARI_AUTOMATION.md`.

### Provenance des références & diff par ligne (P2)

Chaque étalon pixel déclare `ref_kind` dans `etalons.json` :
- **`oracle`** → comparé à l'**oracle Hatari** `tests/reference/<id>.png` (jamais une self-capture).
- **`snapshot`** → comparé à la self-capture NeoST `<id>.ppm` (non-régression ; repli `.png`).

```sh
python3 tools/run_etalons.py --verify-refs      # contrôle la provenance (oracle = .png ≥832px)
python3 tools/compare_screenshot.py A.ppm B.png --crop active --report   # diff PAR SCANLINE
```

`--report` affiche le 1ᵉʳ pixel divergent (x/y) et les pires scanlines — un décalage vertical spec512
apparaît en bande contiguë, un décalage horizontal en petit compte réparti sur beaucoup de lignes.
`run_etalons.py` passe `--report` automatiquement (diagnostic en cas d'échec).

### Pont config GUI↔headless (P3)

Les bugs « seulement en GUI » viennent d'une config ≠ headless. `--from-cfg neost.cfg` rejoue la config
EXACTE du GUI (machine/TOS/mem/cpu/disque/cart/mono/fastfdc/fpu/gemdos/acsi) ; les chemins `./../` du
GUI sont résolus vers la racine. Les options CLI placées **après** surchargent :

```sh
# Reproduire headless ce que le GUI a lancé, capturer, diff Hatari
./build/neost-headless --from-cfg neost.cfg --frames 1651 --screenshot s.ppm
python3 tools/compare_screenshot.py s.ppm tests/reference/<oracle>.png --crop active --report
```

---

## 5. Suites de test automatisées

Micro-tests formalisés par la communauté (Hatari, Steem), à exécuter au headless plutôt
que jeu par jeu.

| Suite | Cible de validation | Réf. |
|-------|---------------------|------|
| **Hatari Test Suite** | Micro-tests 68000 (instructions/exceptions non documentées) + Shifter | dépôt Hatari (`tests/`) |
| **ST-STE Hardware Test** (Troed / Sync) | Timings fins du Shifter, détection des modes de rémanence, variations de cycles mémoire (accès RAM asynchrones CPU/vidéo) | scène Atari |
| **Anatool / Discovery Cartridge** | Lignes de statut WD1772 lors du formatage / lecture de pistes corrompues | utilitaire bas niveau |
