# Changelog — NeoST

(c) 2026 VERHILLE Arnaud. **La chronologie** : releases, puis les chantiers datés dans
l'ordre inverse. Version courante : **0.6.1**.

- « NeoST gère-t-il X ? » → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md) (inventaire par puce)
- « Que reste-t-il ? » → [`TODO.md`](TODO.md)

## Pilotage externe : menu, spécification du protocole, recherche d'adresses (2026-09-05)

Pour qu'un planner externe s'y mette vite : **`tools/opendst.py`**, un verbe et le reste de la
ligne (`server`, `memdiff`, `explore`, `oracle`, `compile`, `equiv`, `hatari`, `doc`) ;
`docs/OPENDST.md` § 0 « démarrer en cinq commandes » et § 5 **spécification complète du
protocole serveur** (transport, forme des réponses, format des champs, bornes, sémantique du
compteur de trames et du joystick tenu, garanties, coûts) — de quoi écrire un client dans
n'importe quel langage sans lire le C++ ; « paralléliser » = N serveurs, cellules par fichiers.

**`tools/opendst_memdiff.py`** : le premier travail sur tout nouveau jeu — trouver en RAM la
position, les munitions, le numéro d'écran — par diff d'états pilotés par le serveur.
Candidats = (change avec l'entrée) − (change avec le temps), exact grâce au déterminisme,
512 Ko en 0,4 s, lecture `peek` sans effet de bord. Auto-test EmuTOS : compteurs TOS exclus,
trace joystick trouvée en 6 octets.

## Pilotage externe déterministe de NeoST (2026-09-04)

**But** : rendre `neost-headless` conduisible par un programme tiers — planner, fuzzer,
explorateur d'états façon Go-Explore — et comparable à Hatari sur un même script
d'entrées. Tout est documenté dans [`docs/OPENDST.md`](docs/OPENDST.md).

**Entrées.** `--joy-script` ne posait **qu'un bit à la fois** : ni tir en mouvement, ni
dynamite de Rick Dangerous (feu+bas). La grammaire devient un sur-ensemble
rétro-compatible — combinaisons `[UF]`, masque brut `[$88]` (préfixe obligatoire : sans
lui « DF » est ambigu), répétition `TOKEN*N`, total borné à 10 M de trames, commentaires
`#` — plus `--joy-script-file` pour les rollouts qui ne tiennent plus sur `argv`. Un
script fautif est **refusé avant le boot** au lieu d'être traduit en « neutre ». La
grammaire est de la logique pure (`src/util/JoyScript.hpp`), couverte à la valeur près par
`neost-selftest`.

**Observation.** `--probe NOM=ADR:LEN`, `--probe-every N`, `--hash-ram ADR:LEN` émettent
une ligne par échantillon sur **stdout** (journaux sur stderr). Lecture par `Bus::peek8` :
**sans effet de bord**. Une première version passait par `read8` pour la MMIO — un
`--probe FF9200:2` sur STE **terminait le processus** (bus error d'un registre whitelisté
en accès octet, levée hors du `try/catch` de Moira : le piège que `Bus::dmaRead8`
documente déjà). D'où le choix assumé : l'espace I/O n'est pas sondable.

**Mode serveur** (`--server`) : boucle de commandes stdin/stdout avec des emplacements
d'état **en mémoire**. Un rollout entier = un aller-retour. L'équivalence avec la boucle
`--frames` est un **verdict du palier `fast`** (`tools/run_server_equiv.py`) — et il est
MUTATION-TESTÉ : une inversion posant l'entrée après la trame le fait rougir sur la bonne
ligne.

**Save-states plus rapides.** Le mode serveur devait supprimer un coût de lancement de
processus estimé à ~20 ms ; la mesure a démenti : le lancement pèse ~3 ms, et le coût
dominant était **`loadState` lui-même à 21,6 ms** — CRC-32 calculé **bit à bit**, payé
deux fois, plus une sérialisation complète pour le filet de sécurité. Table de CRC (mêmes
valeurs, format inchangé, états existants toujours lisibles) et filet sérialisé sans CRC :
reprise **21,6 → 3,9 ms**, sauvegarde **10,2 → 2,6 ms**. Le GUI (F7/F8) en profite autant.

**Oracle différentiel** `tools/opendst_oracle.py` : *pour tout script d'entrées, NeoST ≡
Hatari*. Deux obstacles levés côté oracle, dans un patch enfin **versionné**
(`tools/hatari_neost_oracle.patch`, `extern/hatari` étant gitignoré) : un script joystick
**daté par VBL** (`--cmd-fifo` ne connaît que des touches et tourne en temps réel) et une
**graine figeable** (`HATARI_SEED`). Alignement en deux passes, parce qu'ancrer sur une
scène d'attente ne suffit pas. ⚠ Limite mesurée : le décalage NeoST↔Hatari **saute à
chaque chargement disque** (−7 / −11 / −110 / −200 trames sur Super Sprint), donc une
comparaison n'est fiable que si l'ancre et la cible n'en sont pas séparées. Contrôle
négatif fait : sans le script côté Hatari, aucune trame identique sur 241 (la plus proche
à 2 280 px) ; avec, **0 px**.

**Client d'exemple** `tools/opendst_explore.py` : boucle Go-Explore minimale sur le mode
serveur. 60 itérations en 13,5 s sur Rick Dangerous depuis un état en jeu, 56 cellules
relisibles par `--load-state`.

**Deux chasses aux bugs** sur l'ensemble (16 défauts, dont 4 de ma conception : un verdict
aveugle, une explication fausse écrite dans le code, quatre correctifs perdus par une
restauration de sauvegarde, une couverture retirée sans raison).

## Numéros de version sautés

Cette section existe pour qu'un trou dans la numérotation ne soit jamais SILENCIEUX —
`tools/check_release.py` échoue tant qu'un numéro sauté n'y figure pas.

- **0.5.3 — jamais utilisée.** Le bump du 2026-08-23 (`dec5929`, « Version 0.5.4 ») est
  passé directement de 0.5.2 à 0.5.4 : aucun tag, aucune entrée de CHANGELOG, aucun
  artefact publié sous ce numéro. **La raison du saut n'est pas consignée** et elle
  n'est pas reconstituable depuis l'historique — on l'écrit tel quel plutôt que
  d'inventer une explication.

## Le PSG grésillait dans Super Hang-On : un digidrum à 45 kHz décimé de 61 % (2026-09-02)

Rapport utilisateur : « le PSG grésille dans Super Hang-On ». C'était un vrai défaut, et
l'inventaire des divergences le portait depuis longtemps — mais sous-évalué.

**Ce qui a été écarté d'abord.** Pas une sous-alimentation audio : mesuré à **×35 le temps
réel** sur ce titre, la boucle d'émulation a 35 fois la marge nécessaire. Pas non plus une
saturation : aucun échantillon près de la butée sur 40 s.

**La cause.** `YM2149::synthesizeFrame` datait chaque écriture de registre en
**échantillons HÔTE** (`e.cycle * frames / frameCycles`), soit une grille de 20,8 µs à
48 kHz, au lieu de la grille interne du modèle YM (250 kHz, 4 µs).

Super Hang-On joue un **digidrum 3 voies** : son mixeur pose `R8`, `R9`, `R10` — les trois
registres de volume — pour chaque instant d'échantillon. Mesuré (`NEOST_YMEV_DIAG=1`) :
897 écritures par trame, en **groupes de 3** espacés de 524 cycles, soit **15,3 kHz**
d'instants sonores. L'histogramme des écarts le prouve sans ambiguïté — **66,7 % sous
32 cycles, 33,3 % au-delà de 64, presque rien entre les deux** — et la trace nomme les
registres : `R8@134518 R9@134526 R10@134546`, puis le groupe suivant 524 cycles plus loin.

Le défaut n'est donc PAS une perte d'échantillons : sur la grille hôte, 350 offsets
distincts suffisaient pour 299 groupes, aucun groupe n'était perdu. C'est un **JITTER DE
DATATION**. Une période de 65,3 µs quantifiée sur une grille de 20,8 µs sort en 3 ou 4 pas
— 62,5 ou 83,3 µs — soit **±30 % de gigue sur la période** d'un flux à 15,3 kHz. Cette
modulation de période est exactement ce qui fabrique du bruit de bande large : le
grésillement entendu. Sur la grille 250 kHz la même période sort en 16 ou 17 pas (64 ou
68 µs), soit ±3 %.

**Le correctif** pose les écritures sur la grille **250 kHz**, la cadence interne du modèle
YM, au lieu de la grille de sortie. Le rééchantillonneur pondéré qui existait déjà
(`nextResampleWeightedN`) fait alors sa moyenne sur ~5,6 pas internes par écriture : le flux
est **filtré** au lieu d'être décimé. C'est ce que fait Hatari, qui appelle `Sound_Update`
avant chaque `Sound_WriteReg` (psg.c:346). En pratique : `synthBlock` est scindé en
`applyRegs` + `renderHost`, et `synthesizeFrame` génère le 250 kHz par segments délimités
par les événements.

**Vérifié à l'oracle**, spectre sur la même fenêtre de 4 s (audio extrait de l'AVI d'Hatari) :

| | <1k | 1-3k | 3-6k | 6-10k | 10-16k |
|---|---|---|---|---|---|
| NeoST **avant** | 92,5 % | 3,1 % | 2,3 % | 1,4 % | **0,5 %** |
| NeoST **après** | 93,5 % | 2,8 % | 2,0 % | 1,2 % | **0,3 %** |
| Hatari | 93,8 % | 2,5 % | 2,0 % | 1,2 % | **0,3 %** |

NeoST rejoint l'oracle exactement dans les bandes où tombait le repliement, et l'énergie
revient au fondamental.

⚠ **Ce qui reste « non résolu » ne doit PAS l'être.** 37,5 % des écritures tombent
encore dans un pas 250 kHz déjà occupé — ce sont précisément `R9` et `R10`, qui
appartiennent au MÊME instant sonore que le `R8` qui les précède de 8 et 28 cycles. Les
séparer synthétiserait les états transitoires de 1 à 3,5 µs où une voie est à jour et pas
les deux autres : ce n'est pas du signal, c'est l'ordre d'écriture du 68000, et le
passe-bas C10 du STF (~7,6 kHz) l'efface de toute façon. Les résoudre AJOUTERAIT du bruit.
Hatari ne les sépare pas davantage.

📌 **Leçon, et une correction que je me fais à moi-même.** La ligne existait dans
`docs/HATARI_DIVERGENCES.md`, classée « basse », avec la mention « jitter ≤ 21 µs
(sync-buzzer) ». Le mécanisme était le bon — c'est bien un jitter — mais la borne « ≤ 21 µs »
était présentée comme négligeable, or elle ne l'est que rapportée à une période longue. Sur
un digidrum à 15,3 kHz (période 65 µs), 21 µs valent ±30 % : le même chiffre devient
audible. **Ma première rédaction de cette entrée disait « 61 % du flux perdu » — c'était
faux**, et c'est l'histogramme des écarts, mesuré ensuite, qui l'a montré : les 61 %
étaient les trois registres d'un même instant, dont la fusion est correcte. Le correctif et
sa validation à l'oracle ne changent pas ; l'explication, si.

Instrument conservé : `NEOST_YMEV_DIAG=1` imprime, par trame, le nombre d'écritures, le
nombre d'instants sonores distincts, et combien chacune des deux grilles en résout.

## Un réglage de sensibilité pour la souris émulée (2026-09-02)

Demandé à l'usage : une souris hôte à très haute résolution rend le pointeur du ST
inutilisable.

**Le problème.** La souris du ST est mécanique — environ 200 points par pouce — et le
TOS n'offre aucune accélération réglable digne de ce nom. NeoST transmettait le delta
de la souris hôte **tel quel** à l'IKBD. Une souris moderne à 1600, 3200 ou 8000 dpi
envoie donc jusqu'à 40 fois plus de pas pour le même geste : le pointeur GEM traverse
l'écran au moindre mouvement, et viser une icône devient un exercice de patience.

**Le réglage** : un curseur *Emulated mouse speed* sur la page **Input** de la fenêtre
Configuration, de 0,05× à 4,00×, avec un bouton *Reset*. Il divise (ou multiplie) le
delta hôte avant l'IKBD. **1,00× est le défaut et reproduit exactement le comportement
d'avant** — une configuration existante, qui n'a pas la clé, se comporte à l'identique.
Persisté en `mousespeed=` dans `neost.cfg`, appliqué au bureau **comme en borne** (c'est
le seul chemin de MOUVEMENT vers le ST : les autres appels à `Ikbd::mouseEvent` ne
portent que les boutons).

**Le point délicat n'est pas la multiplication, c'est le RESTE.** Sous 1,0, la plupart
des deltas mis à l'échelle valent moins d'un pas entier. Les tronquer sans les reporter
ferait perdre **tous** les petits mouvements : la souris ST ne bougerait plus du tout
sur un déplacement lent — c'est-à-dire exactement au moment où le réglage sert. La règle
vit donc dans `src/util/MouseScale.hpp`, isolée pour être exerçable : `selftest_logic`
vérifie qu'à 0,25× le premier pas tombe bien au 4ᵉ appel, et surtout la CONSERVATION
(3000 px hôte à 0,3× → 900 pas ST à ±1 près), le signe des deltas négatifs, et le
bornage défensif.

⚠ **NaN est traité, et ce n'est pas de la bureaucratie** : `int(NaN)` est un
comportement indéfini, et un reste devenu NaN figerait la souris ST pour toute la
session. Un `mousespeed=nan` venu d'un fichier corrompu retombe donc sur 1,0 — la même
leçon que celle payée sur `volume=`/`mix_*` (cf. `mixGain`, AppConfig.cpp), appliquée
cette fois du premier coup. Le plancher n'est pas 0 non plus : à 0 la souris serait
figée sans que rien ne le dise.

**Périmètre** : bureau et borne. Les frontends **web et Android ne sont pas touchés** —
ils ont leur propre chemin souris et leurs propres réglages, et je n'ai pas de moyen de
les vérifier ici.

## A42 soldé : l'élection de l'IRQ MFP voit enfin tout le lot de dispatch (2026-09-03)

Suite et fin du dossier ouvert la veille. Le port de `MFP_UpdateNeeded` était PARTIEL :
l'élection ne voyait qu'une entrée à la fois parce que `Machine` fait suivre chaque
callback d'ordonnanceur d'un `cpu.updateIpl()` — 18 sites — qui la déclenchait aussitôt.

**L'exhibiteur d'abord**, comme le TODO l'exigeait. Mais en **table de vérité** plutôt
qu'en étalon pixel : la règle se teste sur la puce nue en quatre lignes, là où un
programme ST aurait demandé deux handlers, un journal et un rendu pour une observation
moins directe. Ajouté à `mfp-selftest` (31 → 35 contrôles) :

- Timer D (priorité BASSE) arrive avant Timer A (HAUTE) → **c'est D qui est servi** ;
- l'ordre des `raiseAt` est indifférent, c'est la DATE qui tranche ;
- à date ÉGALE, la priorité reprend ses droits ;
- plus ancienne ET prioritaire → pas de conflit.

Vérifié par mutation : `NEOST_MFP_BATCH=0` fait échouer le premier cas — `got=13`
(Timer A, plus récent mais plus prioritaire) au lieu de `want=4`. C'est exactement le
défaut que corrige Hatari (2013/04/21, « fix Fuzion CD Menus 77, 78, 84 »).

**La refonte.** `Scheduler::setDispatchHooks` borne le LOT — garde RAII et compteur de
profondeur, parce que `runTo` est ré-entrant (`addStolenCycles` du blitter). `Machine` y
suspend l'élection MFP pendant le lot et la tranche UNE FOIS à la fin, avant de relire
l'IPL : l'ordre d'Hatari (`CycInt_Process`, puis `MFP_UpdateIRQ_All(0)`, puis IPL).
Les `cpu.updateIpl()` des callbacks sont **laissés en place** : le CPU est arrêté pendant
le lot et Moira ne relit sa broche qu'à une frontière d'instruction, donc les recalculs
intermédiaires n'ont aucun effet observable — les retirer aurait été une seconde refonte
pour rien. Verrou d'A/B : `NEOST_IPL_BATCH=0`.

**Ce que ça change, mesuré** : le groupement passe de **0** à **123** entrées groupées sur
Super Hang-On (sur 1 000 123) et **1 240** sur le bureau EmuTOS (sur 17,6 M) — environ
1 entrée sur 10 000. « Rarissime », comme l'inventaire l'annonçait, mais non nul, et
désormais servi dans le bon ordre.

⚠ **Effet de bord assumé, et il fallait le regarder avant de le classer.** `dmasnd_poll`
se décale de 288 px : cet étalon arme la VBL (`sr=$2300`), donc l'instant où l'exception
est prise bouge d'un cheveu et la phase du poll avec. L'écart est **structurellement nul**
— mêmes 61 valeurs distinctes, même histogramme de deltas `{6:47, 0:39, 8:12, 4:1}`, une
seule position à 2 octets près (`0038` → `003A`). La propriété que l'étalon contraint — le
compteur n'avance qu'au HBL — est intacte. Référence re-posée. Sa note dit ce qui la
justifie **et ce qui ne la justifie pas** : c'est un snapshot sans oracle (Hatari ne se
reproduit pas dessus), il ne peut donc pas arbitrer laquelle des deux phases est juste ;
ce qui la fait re-poser est que le nouvel ordre est celui d'Hatari, pas une mesure pixel.

## CI : les deux téléchargements du job Android n'avaient aucun filet (2026-09-02)

Le workflow *Artefacts* a échoué sur le push du 2026-09-02, sur le seul job `android` :

```
An error occurred while preparing SDK package NDK (Side by side) 27.2.12479018:
Error on ZipFile unknown archive.
```

Archive tronquée au téléchargement du NDK. **Aléa confirmé** : le même job, sans aucune
modification côté Android, est repassé au push suivant. Mais `publish` dépend des huit
paquets — donc **un aléa réseau suffit à bloquer une release**, et c'est exactement ce qui
avait empêché la 0.6.1 de sortir (l'`apt-get install` sans `update` de `linux-bionic`).

Audit du job : **deux** étapes réseau sans reprise, et aucune des deux n'en avait.
- `sdkmanager --install` (SDK/NDK/CMake) : 3 tentatives, en **purgeant le NDK
  partiellement installé** entre deux — sans quoi une extraction à moitié faite peut être
  reprise telle quelle et échouer à l'identique.
- `packaging/android/fetch_sdl.sh` (clone SDL2) : même motif, avec purge de `extern/SDL2`
  entre deux tentatives. C'est nécessaire pour une raison propre à ce script : son garde
  d'entrée teste `-d extern/SDL2/.git`, donc un clone interrompu laisserait un arbre
  PARTIEL que le tour suivant prendrait pour une installation valide.

Les deux échouent explicitement après 3 tentatives (« ce n'est plus un aléa ») plutôt que
de passer en silence. Logique vérifiée dans les deux sens sous `set -eu` : succès après
échecs transitoires, et code 1 après trois échecs consécutifs.

## L'élection de l'exception MFP : la règle chronologique existait mais ne pouvait jamais s'appliquer (2026-09-02)

Suite du dossier MFP. En comparant `MFP_InputOnChannel` à `Mfp::raiseAt` — fonction
entière, `else` compris — les deux sont identiques ligne pour ligne. L'écart était
ailleurs, dans QUAND l'élection a lieu.

**La règle d'Hatari.** Une ENTRÉE d'interruption ne déclenche pas l'élection : elle pose
son bit pending et sa date, puis marque « à faire » (`MFP_UpdateNeeded = true`). L'élection
vient plus tard, une fois toutes les entrées reçues. Son propre commentaire
(mfp.c:1084-1087) l'explique : « *As we can have several inputs during one CPU instruction,
not necessarily sorted by Interrupt_Delayed_Cycles, we must call MFP_UpdateIRQ() only later
in the main CPU loop, when all inputs were received, to choose the oldest input's event
time* ». C'est ce qui rend opérant le test `Pending_Time[Int] <= Pending_Time_Min` de
`MFP_InterruptRequest` (2013/04/21, « fix Fuzion CD Menus 77, 78, 84 »).

**Le défaut.** NeoST avait bien `pendingTime_[]`, `pendingTimeMin_` et le test
chronologique dans `checkPendingInterrupts` — mais il élisait IMMÉDIATEMENT à chaque
`raiseAt`, et `updateIrq` remet `pendingTimeMin_` à l'infini en sortant. Le minimum était
donc consommé avant l'arrivée de l'entrée suivante : la règle avait **toujours un seul
candidat** et ne pouvait rien départager. Une entrée plus récente mais plus prioritaire
l'emportait, là où le 68901 sert la plus ancienne.

**Ce qui est porté.** `raiseAt` n'arme plus qu'un drapeau ; l'élection est faite par
`flushIrqUpdate()`, appelé au calcul d'IPL (`neostUpdateIpl`, l'équivalent NeoST de la
boucle CPU) et en fin de `Mfp::updateTimers` (≙ mfp.c:689, avec l'horloge courante comme
date de front, comme Hatari). Les paires d'entrées d'une même fonction — `TXERR`→`TXEMPTY`,
`RXERR`→`RXFULL` — sont désormais départagées correctement. Verrou : `NEOST_MFP_BATCH=0`.

⚠ **CE QUI RESTE, mesuré et dit plutôt qu'habillé.** Chez Hatari le flush est PAR
INSTRUCTION ; chez NeoST il est par CALLBACK, parce que `Machine` fait suivre chaque
callback d'ordonnanceur d'un `cpu.updateIpl()` — 18 sites. Deux timers MFP échus dans le
même `runTo` sont donc encore élus séparément. Compté sur Super Hang-On : **0 groupement
sur 1 000 000 d'entrées**, la fenêtre se refermant aussitôt. Conséquence honnête : ce port
**ne change aucune image** — A/B à 0 px sur Super Hang-On, `mfp_poll`, `blitter_timer` et
`trace_odd`. Il rend la règle opérante là où elle peut l'être, sans prétendre couvrir le
cas inter-callbacks. Le fermer demande de sortir `updateIpl()` des callbacks pour un unique
appel en fin de dispatch : une refonte du pilotage de l'IPL, à faire séparément et avec son
propre filet — pas en fin de session.

## Les trois autres pistes « Super Hang-On » d'Hatari, instruites (2026-09-02)

Le nom du jeu apparaît **quatre fois** dans les sources d'Hatari. Après `MFP_UpdateTimers`,
les trois restantes ont été lues et confrontées à NeoST — deux étaient déjà couvertes, la
troisième manquait.

**Déjà porté — délai de 4 cycles sur la montée d'IRQ** (mfp.c:107, 2013/03/01) :
`kIrqDelayToCpu`, `irqTime_`, source `Scheduler::MFP_IRQ` armée à `irqTime_ + 4`, et le cas
« délai déjà écoulé » qui prend l'exception à la frontière courante plutôt qu'une
instruction plus tard.

**Déjà correct par construction — bit pending posé deux fois avant l'IACK**
(video.c:320, 2013/05/03) : NeoST modélise HBL et VBL par des BOOLÉENS effacés à
l'acquittement et lus comme un NIVEAU pour l'IPL. Poser le bit deux fois s'y confond
naturellement en une seule interruption, là où Hatari a dû ajouter du code pour l'obtenir.
Le commentaire de `Cpu68k.cpp:51-52` disait déjà « ≙ pendingInterrupts = 0 ».

**Manquait — cycles d'écriture dans `MFP_IRQ_Time`** (mfp.c:113, 2013/03/14, « properly fix
Super Hang On ») : Hatari date une écriture MMIO à `currcycle + 4`,
« *the number of cycles when the write will be completed* ». NeoST datait ses IRQ nées
d'une écriture registre MFP à `liveNow()` — or sa propre convention veut que la fin d'accès
soit à **+2** (Moira facture déjà le SYNC de tête ; c'est écrit dans
`Cpu68k::cyclesIntoInstr`, et le Shifter l'applique à ses écritures palette). Les IRQ du MFP
étaient donc antidatées de 2 cycles, et comme la visibilité CPU court depuis cet instant,
l'exception partait 2 cycles trop tôt. Corrigé sur les 9 sites (`Mfp::writeEventTime`).

⚠ **Ce qui n'est PAS démontré, et je le dis plutôt que de l'habiller** : à la valeur juste,
ce port ne change aucune image mesurée — sur Super Hang-On en jeu, `+0` et `+2` rendent la
même trame au pixel près. Ce qui est démontré, c'est que le chemin pilote réellement
l'émulation : `NEOST_MFP_WRITE_END=40` déplace 24 773 px sur cette même trame. C'est donc un
port de FIDÉLITÉ adopté sur la lettre d'Hatari, pas sur un pixel. Et `mfp_poll` ne peut pas
l'arbitrer : son programme masque les IRQ (`SR=$2700`), il reste à 0 px même à 40 cycles de
décalage — utile à savoir avant de croire cet étalon omniscient.

## Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)

Suite du rapport « des bandes pleine largeur, à n'importe quelle hauteur, de temps en
temps, souvent noires ou blanches ». Deux correctifs d'affichage plus tard, l'utilisateur
voyait toujours le défaut et a demandé de chercher du côté des autres émulateurs. C'était
la bonne idée : **Hatari nomme ce jeu et ce symptôme dans son propre journal.**

`mfp.c:135`, entrée du 2022/01/27 :

> *Call MFP_UpdateTimers / CycInt_Process before accessing any MFP registers, to ensure
> MFP timers are updated in chronological order (**fix the game Super Hang On**, where
> `bclr #0,$fffffa0f` to clear Timer B ISR sometimes happens at the same time that Timer C
> expires, which used the wrong ISR value and gave **flickering raster colors**)*

« Flickering raster colors » : des lignes rendues avec la palette de leur voisine, donc des
bandes pleine largeur, à hauteur arbitraire, par intermittence. Le symptôme, mot pour mot.

⚠ **Et j'avais porté ce correctif le matin même, avant de le retirer.** L'entrée écrite
alors — « correctif prescrit RÉFUTÉ À LA MESURE, ne pas re-tenter » — reposait sur un seul
chiffre : le port faisait passer l'étalon `mfp_poll` de 80 à 88 px contre l'oracle. **Ce
chiffre était un artefact**, mesuré AVANT la correction de l'échéance arrondie de
`readTimerData` — le vrai défaut de cet étalon, corrigé quelques heures plus tard le même
jour. Sur une base saine, le port est **neutre sur `mfp_poll` : 0 px contre l'oracle**. Il
n'y avait jamais eu de raison de le rejeter, et le rejet avait été inscrit dans
l'inventaire avec une consigne de non-répétition — exactement le genre de verdict faux qui
coûte plus cher qu'une case vide. `docs/HATARI_DIVERGENCES.md` porte la correction.

**Ce qui est en place** : `Scheduler::runMfpTimersTo` sert les timers délai
(TIMER_A/B_DELAY/C/D) échus avant chaque `Mfp::read8`/`write8`, à l'horloge live. Le
dispatch reste CIBLÉ sur ces quatre sources — un `syncTo` nu réactiverait le modèle
sync-driven réfuté (deadlock Enchanted Land) — et `now_` n'est pas avancé.

**Portée mesurée** sur une partie de Super Hang-On : **125 890 accès registre sur 1,4 M
(≈ 9 %)** trouvent un timer échu à servir. Le chemin travaille donc en permanence ; seule
sa conséquence visible — la course entre l'acquittement d'ISR et une expiration
concurrente — est occasionnelle, ce que le « *sometimes* » de Hatari dit déjà.
Sur une fenêtre de 60 trames en course, les images avec et sans le correctif sont
d'ailleurs **identiques** : la course ne s'y produit pas. Verrou d'A/B :
`NEOST_MFP_UPDTIMERS=0`.

📌 **Leçon de méthode, et elle est à mes dépens.** `CLAUDE.md` prescrit de comparer à
`extern/hatari/src` AVANT d'investiguer. Sur ce dossier j'ai fait l'inverse : trois passes
d'instrumentation sur l'affichage — cadrage, rééchantillonnage, capture de fenêtre — avant
de lire le journal d'Hatari, où le nom du jeu apparaît quatre fois. Un `grep -i 'hang.on'`
dans `extern/hatari/src` aurait donné la réponse en dix secondes.

## Les bandes de Super Hang-On : UNE trame de transitoire faisait sauter le cadre pendant 0,6 s (2026-09-02)

Rapport utilisateur : « des bandes complètes sur toute la largeur de l'écran, à n'importe
quelle hauteur, de temps en temps, souvent noires ou blanches », en jeu, dans l'interface.

**Ce qui a été éliminé, dans l'ordre.** Pas la passe CRT (l'utilisateur a vérifié : les
bandes apparaissent CRT éteint). Pas le framebuffer émulé : un détecteur de bande
intrusive (`NEOST_BAND_DIAG=1`) en trouve **0 sur 18 000 trames** en jeu. Pas une
déchirure : `runFrame()` puis `screen.update()` s'enchaînent dans la même itération.

**Il manquait l'instrument, et c'est la vraie leçon.** `--shot` ne rend que le
framebuffer ÉMULÉ : tout ce qui se passe entre lui et l'écran — cadrage, échelle,
filtrage, CRT — était **invisible au harnais**. D'où deux ajouts : `--shot-window P N C`
capture la FENÊTRE réellement composée (glReadPixels avant le swap), et
`NEOST_WBAND_DIAG=1` traque les bandes dans l'image affichée en ne relisant qu'une bande
verticale de 16 px — une bande pleine largeur la traverse forcément, et le coût reste
négligeable là où relire 2880×1800 coûterait 15 Mo par trame.

**La cause.** `stContentRegion` armait son verrou de 30 trames dès que la Glue signalait
une bordure ouverte — **sur une seule trame**. L'hystérésis ne protégeait donc que le
RETOUR : elle empêchait de rebasculer trop vite, mais rien n'empêchait de basculer sur un
transitoire isolé. Mesuré (`NEOST_FRAMING_DIAG=1`) sur Super Hang-On : à la trame 2, la
Glue rend `live=58+197` contre une zone active `29+200` — un transitoire de mise en route
vidéo, d'UNE trame — et le cadre passait de `top=29 h=200 w=320` à `top=29 h=226 w=416`
pour les **29 trames suivantes**. L'image saute, se redimensionne, et les BORDURES NOIRES
entrent dans le cadre. Sur le run de 12 000 trames : 11 968 au cadre normal, **29 au cadre
élargi**.

Et les deux mécanismes se composent : le cadre qui change fait varier l'échelle, donc —
en échantillonnage au plus proche voisin — la façon dont les lignes source retombent sur
les lignes écran. D'où des bandes à des hauteurs ARBITRAIRES pendant 0,6 s, puis un retour
à la normale. C'est exactement le symptôme décrit.

**Le correctif** exige que la bordure reste ouverte **3 trames consécutives** avant
d'élargir le cadre, en gardant l'hystérésis de relâchement. Une vraie démo overscan ouvre
ses bordures à CHAQUE trame : elle ne perd que 60 ms, imperceptibles. Un transitoire isolé,
lui, ne déclenche plus rien.

**Mesuré des deux côtés** :

| titre | avant | après |
|---|---|---|
| Super Hang-On (12 000 trames) | 11 968 normal + **29 élargi** | **12 000 / 12 000 normal** |
| Enchanted Land en jeu | `top=0 h=229 w=320` | inchangé |
| étalon `overscan_top` | `top=0 h=229 w=320` | inchangé |
| `closure` (overscan complet) | `top=0 h=276 w=416` | inchangé |
| bureau EmuTOS | `top=29 h=200 w=320` | inchangé |

📌 **Leçon.** Le commentaire d'origine annonçait « hystérésis pour ne pas basculer sur un
retrait d'une trame ». L'intention était juste ; le code ne la réalisait qu'à moitié —
il protégeait la sortie et pas l'entrée. Un garde-fou à moitié posé se lit comme un
garde-fou posé.

## Le zoom auto cadrait Enchanted Land sur 416 px de large pour une image de 320 (2026-09-02)

Signalé à l'usage : « l'autozoom devrait être meilleur avec Enchanted Land ». Il l'est, et
le défaut était double.

**Le cadre était trop LARGE.** `stContentRegion` ne connaissait que deux cas — zone active,
ou buffer entier — et décidait des deux sur un signal unique, `Shifter::bordersOpen()`. Or ce
signal est vrai dès qu'une bordure **haute, basse OU latérale** bouge. Enchanted Land n'ouvre
que le haut : son cadre passait quand même à 416 px pour une image qui n'en occupe que 320
(boîte de contenu mesurée : x 48..367, exactement la zone active), soit un zoom **1,3× trop
petit** avec deux bandes noires. Le pire est que le signal latéral existait dans la Glue mais
était **inatteignable** : son balayage par ligne était gardé par `if (!bordersTrick_)`, donc
sauté dès qu'un trick vertical avait déjà mordu.

**Le cadre ROGNAIT en hauteur.** La branche « bordure haute seule » remontait le cadre de
2 lignes en gardant la hauteur active : sur Enchanted Land elle coupait les 2 dernières
lignes de l'image, et sur l'étalon `overscan_top` les **29 lignes du haut**, que ce programme
dessine réellement (sa boîte de contenu commence à y=0).

**Ce qui tranche, et c'est mesuré.** La Glue compte désormais les lignes affichées dont le DE
déborde des cycles nominaux, au lieu de poser un booléen. Relevé le 2026-09-02 :

| titre | lignes élargies | contenu mesuré |
|---|---|---|
| Closure | **272-275 / 276** (99 %) | x 8..407 — déborde vraiment des deux côtés |
| Enchanted Land | **4 / 229** (2 %) | x 48..367 — pile la zone active |
| `overscan_top` | **5 / 229** (2 %) | x 44..367 |

Deux ordres de grandeur séparent les régimes ; le seuil est posé à un quart. Contrôle de
cohérence : sur `overscan_top`, les lignes qui portent des pixels à gauche de x=48 sont
**exactement 5** — ce sont les 5 lignes élargies, c'est-à-dire l'artefact de synchro et non
l'image. La règle les écarte à juste titre.

**Nouvelle règle** : verticalement, l'étendue que la Glue AFFICHE (plus de rognage) ;
horizontalement, les bordures latérales seulement si elles débordent sur une part
significative des lignes. L'hystérésis d'origine est conservée et généralisée — on retient
l'UNION des étendues vues pendant le latch, donc le cadre ne rétrécit jamais en cours de
scène et rien ne « respire ».

**Gain mesuré** sur Enchanted Land (contenu 320×197), hauteur d'image à l'écran :

| fenêtre | avant | après |
|---|---|---|
| 1920×1080 (16:9) | 909 px | 929 px (+2 %) |
| 1280×800 (16:10) | 606 px | 688 px (+13 %) |
| 1024×768 (4:3) | 485 px | 630 px (**+30 %**) |

Le gain est modeste en 16:9, où c'est la hauteur qui borne — c'est dit ainsi plutôt
qu'annoncé plus large. Sur les fenêtres moins larges il est très net.

⚠ **Effet de bord assumé** : `cW` est un PLANCHER (le frontend dessine tout le buffer quand
il tient, et ne rogne jamais en deçà), donc le passage de 416 à 320 ne cache rien — il
autorise seulement un zoom plus grand. Seul cas où il retire quelque chose : une fenêtre plus
étroite que ~1,41:1, où `overscan_top` perd jusqu'à 2 px de chaque côté — sur les 5 lignes
d'artefact ci-dessus, jamais sur l'image. En échange, le même étalon cesse de perdre 29 lignes.

**Instrument conservé** : `NEOST_FRAMING_DIAG=1` sur `neost-headless` imprime, par trame, la
zone active, l'étendue live, les deux moitiés du signal de bordure et le cadre décidé. Sans
lui, juger le cadrage demandait de regarder une fenêtre à l'œil — donc de ne rien pouvoir
mesurer. Le refactor du signal Glue est vérifié neutre : `bordersTrick_` garde exactement la
même valeur (OU des deux mêmes conditions), et les 25 étalons pixel sont inchangés.

## Un infini en décimal empaqueté sortait en chiffres arbitraires (2026-09-02)

Le dernier item « faisable sans oracle » de l'inventaire. Deux de ses trois points sont
faits ; le troisième ne l'est PAS, et c'est une décision, pas un abandon.

**±inf / NaN.** Le 68881 n'émet pas de BCD pour un infini ou un NaN : il recopie le motif
étendu, dont les 12 bits bas du premier mot valent alors `$FFF` — l'exposant « spécial » du
format P (`fp_from_pack`, fpp_softfloat.c:702). NeoST, lui, tombait dans son
`snprintf("%+.*e")` : la libc rend « +inf », et le parseur BCD tirait des chiffres
**arbitraires** des lettres de « inf ». Corrigé ; le payload d'un NaN traverse désormais
intact, vérifié sur une valeur témoin.

**k-factor > 17 → OPERR.** Le format P ne porte que 17 chiffres significatifs ; au-delà, le
68881 arme OPERR (softfloat_decimal.c:412-414). NeoST écrêtait à 17 **en silence**. Le test
ne vise que le k positif : un k négatif sélectionne le style point fixe, qui n'est pas
concerné.

Les deux sont gardés par les tests 13 et 14 du banc (`make_fpu_testrom.py`, 12 → 14 tests,
exécuté au palier `fast`), chacun vérifié par mutation.

⏸ **INEX1 n'est PAS posé, délibérément.** Chez Hatari le drapeau est `float_flag_decimal` →
`FPSR_INEX1` (fpp_softfloat.c:100), et il est armé sur la direction **décimal → étendu**
(`floatdecimal_to_floatx80`, softfloat_decimal.c:369) — pas sur la sortie, contrairement à ce
que la formulation de `TODO.md` laissait croire. Or NeoST approxime cette direction par
`std::strtod`, soit 53 bits pour 17 chiffres décimaux : **on ne sait pas** quand la conversion
a été exacte. Poser le drapeau à l'estime serait pire que ne pas l'avoir — un programme qui
teste INEX1 se fierait à une information parfois fausse. Le débloquer demande le vrai
`floatdecimal_to_floatx80`, c'est-à-dire le port de `softfloat_decimal.c` (492 lignes), qui
livrerait aussi la génération de chiffres bit-exacte et le style point fixe k ≤ 0. C'est ce
qui reste écrit dans `TODO.md`.

📌 Ce chantier illustre une limite de l'inventaire lui-même : sa puce disait « INEX1 sur
conversion inexacte » sans dire DE QUELLE DIRECTION, et il a fallu remonter au mapping
d'Hatari pour voir que le drapeau n'appartient pas au chemin qu'on était en train de corriger.

## Sur macOS, un fichier accentué du lecteur GEMDOS était introuvable (2026-09-02)

Un des deux items « faisables sans oracle » de l'inventaire des divergences. Il y dormait
depuis la 3ᵉ passe d'audit, classé MOYENNE et différé comme « spécifique plateforme ».

**Le défaut.** macOS rend les noms de fichiers en forme DÉCOMPOSÉE (NFD) : « café » y est
stocké « cafe » suivi de U+0301, l'accent aigu combinant — six caractères, pas cinq. NeoST
comparait ces octets tels quels au nom demandé par le programme Atari. Résultat : **un
fichier accentué du lecteur GEMDOS était introuvable depuis le TOS**, et le même dossier
n'exposait pas les mêmes noms sur macOS et sur Linux.

**Le correctif** suit la méthode imposée : `Str_DecomposedToPrecomposedUtf8`
(`hatari/src/str.c:726`) est porté en `neost::hostpath::precomposeUtf8`, avec sa table des
53 couples (lettre, marque combinante) → point de code précomposé reprise TELLE QUELLE — le
sous-ensemble qui existe dans le jeu de caractères Atari, pas au-delà. Branché sur les deux
mêmes sites que Hatari : `matchHostDirEntry` et le listing Fsfirst. `determineMaxPartitions`
n'en a pas besoin, Hatari n'y convertit pas non plus (seule la première lettre y est lue).

**Le bug était réel, et c'est mesuré plutôt que supposé** : en retirant la recomposition, un
fichier créé avec les octets NFD est bel et bien introuvable sous sa forme précomposée —
`gemdos-selftest` passe de 14 OK à 13 OK / 1 FAIL. APFS préserve les octets tels qu'écrits,
donc le cas se reproduit sur cette machine.

⚠ **Ce que le port apporte, exactement.** Ni NeoST ni Hatari ne convertissent le JEU DE
CARACTÈRES sur ce chemin (comparaison octet à octet, « conversion charset off » des deux
côtés). Le gain n'est donc pas un affichage correct des accents côté TOS — un « é » restera
rendu selon le jeu Atari — mais la COHÉRENCE macOS ↔ Linux : le même dossier hôte rend
désormais les mêmes noms, de la même longueur, donc la même troncature 8.3 et le même
aller-retour listage → ouverture. C'est dit ainsi plutôt que promis plus large.

**Gardé à deux niveaux**, et c'est délibéré :
- `selftest_logic` exerce la FONCTION PURE (13 cas : le cas « café » qui motive le port, une
  chaîne déjà précomposée qui doit rester STRICTEMENT inchangée, l'ASCII, les quatre familles
  d'accents, deux accents dans un nom, un accent en FIN de chaîne, une combinaison absente de
  la table, et une marque combinante TRONQUÉE). Elle est pure et sans `#ifdef`, donc exerçable
  depuis n'importe quelle machine — la même discipline que `Style` pour les chemins Windows,
  et pour la même raison : un défaut spécifique à une plateforme n'est gardé que s'il est
  exerçable ailleurs.
- `gemdos-selftest` exerce le CÂBLAGE de bout en bout, avec un vrai fichier écrit sur disque
  en NFD. Ce test ne peut pas être vert par accident : si le volume normalise de lui-même et
  rend déjà du NFC, la recomposition est un no-op et la recherche réussit pareil.

## Le FPU arrondissait deux fois — et son banc de test ne voyait pas les tests qu'on lui ajoutait (2026-09-02)

Chantier n°4 de la liste des divergences, après les n°3 et n°2 le même jour. Celui-ci se
termine là où il visait, pour changer.

**Le défaut.** `Fpu::encodeFmt` convertissait la valeur étendue en `double` hôte (`extToD`)
AVANT toute conversion sortante. Or l'étendu porte 64 bits de mantisse et le `double` 53 :
arrondir deux fois de suite ne vaut pas arrondir une fois. L'étendu immédiatement supérieur à
0,5 — `$3FFE 80000000_00000001` — retombait sur 0,5 EXACTEMENT en double, puis la règle du pair
le plus proche l'envoyait sur **0**, là où le 68881 rend **1** (la valeur est strictement
au-dessus de 0,5, il n'y a aucune égalité à départager). Dans la même famille : **INEX2 n'était
jamais levé** sur une conversion inexacte, le **mode d'arrondi du FPCR était ignoré** en sortie
(le `float(double)` appliquait celui de l'hôte, toujours « au plus près »), **UNFL** était
absent, l'**OVFL silencieux en D**, et un NaN rendait 0 au lieu de son payload.

**Le correctif** suit la méthode imposée : porter Hatari plutôt que réinventer.
`floatx80_to_int32/16/8` et `floatx80_to_float32/64`, avec leurs `roundAndPackInt32/16/8` et
`roundAndPackFloat32/64`, sont portés depuis `extern/hatari/src/cpu/softfloat/softfloat.c` dans
`src/io/SoftFloatX80.hpp` (`sf::toInt`, `sf::toFloat32/64`). `encodeFmt` ne traverse plus de
`double` pour L/W/B/S/D — il reste sur `double` pour les transcendantes et le décimal empaqueté,
et le bandeau du fichier le dit maintenant (il annonçait encore « calculs en double hôte », faux
depuis le portage softfloat).

**Trois cas ajoutés au banc** (`tools/make_fpu_testrom.py`, 9 → 12 tests, exécuté au palier
`fast` par l'auto-test série `fpu_cir`), chacun vérifié par mutation :
- **test 10** — FMOVE.L de l'étendu juste au-dessus de 0,5 doit rendre 1. Aucun des tests 1-9 ne
  pouvait le voir : le test 8 sort en FMOVE.X, qui recopie la mantisse sans conversion, et tous
  les autres tiennent en 53 bits.
- **test 11** — INEX2 (et l'accumulé INEX) armés par FMOVE.L de 1,5.
- **test 12** — FPCR en RZ : FMOVE.S de 1/3 doit TRONQUER ($3EAAAAAA), pas arrondir au plus
  près ($3EAAAAAB).

⚠ **Deux pièges d'outillage débusqués, et le premier est sérieux.**
`disks/etalons/fpu_testrom.img` est **commise**, et `ensure_rom_asset` ne la régénère que si
elle est **absente** : les tests 10 et 11 ajoutés au générateur sont restés **invisibles du
palier `fast`** jusqu'à ce qu'on re-commette l'image. Un banc de test peut donc être vert en
exerçant une version périmée de lui-même. La note de l'entrée `fpu_cir` le dit désormais.
Second piège, corrigé dans `tools/run_selftests.py` : une ROM `rom_generate` manquante était
classée « TOS Atari absent → SKIP » — un verdict vert qui n'exécute rien — au lieu d'être
régénérée. La garde exclut maintenant les ROM générées de ce test ; vérifié en effaçant l'image
(elle se régénère au lieu de sauter).

**Reste ouvert sur cette puce** : FSGLMUL/FSGLDIV (plage d'exposant étendue avec mantisse
24 bits, `roundSigAndPackFloatx80`), le décimal empaqueté, FMOVECR et FMOD — non touchés, et
dits tels quels dans `TODO.md`.

## Le fetch du son DMA se mesure enfin — et c'est l'oracle qui ne se reproduit pas (2026-09-02)

Chantier n°2 de la liste des divergences ouvertes, dans la foulée du n°3. Il est **CLOS**, et
comme le précédent il ne se termine pas où il visait.

**Ce qui manquait.** L'item disait : « quantification HBL du refill FIFO à confronter à l'oracle
sur un poll serré de `$FF8909/0B/0D` ». Le modèle FIFO (divergence S2) était porté depuis le
2026-07-07 et validé au WAV — mais **le WAV mesure ce que le DAC CONSOMME, pas la date à laquelle
le DMA FETCHE**. Or c'est le fetch que le compteur de trame expose, et c'est lui que les
programmes lisent pour se synchroniser. Ce chemin n'avait aucun étalon : `make_dmasnd_test.py`
module le tampon et s'écoute, il ne lit jamais le compteur.

**L'exhibiteur** : `tools/make_dmasnd_poll_test.py` → étalon **`dmasnd_poll`** (généré, ROM libre,
STE 1 Mo). 100 tours d'une boucle qui lit `$FF890B` puis `$FF890D` et écrit le mot en RAM vidéo,
pendant que le DMA joue à 50066 Hz stéréo. Ce qu'il contraint : le compteur ne doit **avancer
qu'au HBL** — deux polls tombant dans la même ligne doivent lire la même valeur. Mesuré : 39
deltas nuls sur 99, puis des sauts de 6 (×47) et de 8 (×12).

**Verdict : NeoST est le plus fidèle des deux.** L'arithmétique tranche sans arbitre : 100 132 o/s
÷ 15 650 lignes/s = **6,398 octets par ligne**, ce qui impose 19,9 % de sauts de 8 parmi des sauts
de 6. NeoST en rend **20,3 %**. Hatari, lui, jitte sur 4/6/8/12 autour du même débit moyen, parce
que sa consommation DAC passe par le rééchantillonnage vers le **taux hôte** : le découpage y
hérite d'une granularité d'échantillon hôte, étrangère au matériel. Et le débit moyen, lui, est
identique des deux côtés — sur la fenêtre des 100 polls, les deux émulateurs avancent le compteur
de **382 octets exactement**. C'est donc bien la granularité qui diffère, pas la cadence.

⚠ **L'oracle ne se reproduit pas lui-même sur ce chemin.** Deux runs Hatari de la même ligne de
commande donnent **664 px d'écart entre eux** ; 1160 après ancrage VBL, 1432 avec `--sound 50066`
au lieu de `--sound off`. Cause : l'accumulateur fractionnaire du resampler court depuis le
DÉMARRAGE de l'émulateur, donc dépend de la durée du boot — que le RNG d'Hatari tire au sort.
L'ancrage VBL (recette éprouvée de `spec512_bands`) a été appliqué et **n'y change rien** : il fixe
la phase du PROGRAMME, pas celle du resampler. `dmasnd_poll` est donc `ref_kind: snapshot`, et le
**premier étalon du corpus refusé à l'oracle pour non-reproductibilité d'HATARI** — pas pour une
raison de modèle. La distinction est écrite dans son `ref_note` pour qu'on ne retente pas la pose.

**Garde vérifiée par mutation**, et sa signature est sans ambiguïté : réintroduire le
`fifoRefill()` que `DmaSound::liveCounter` avait de trop (défaut corrigé le 2026-08-13) rend
1592 px — et surtout transforme l'image en **rampe continue** (100 valeurs distinctes, deltas 2
et 4, plus aucun delta nul) au lieu du palier-saut. L'étalon ne compte pas des pixels, il exhibe
une forme.

📌 **Leçon**, jumelle de celle du n°3 le même jour : un étalon qui encode des VALEURS mérite un
décodeur. C'est en lisant les octets écrits à l'écran — et non le nombre de pixels différents —
qu'on voit que le débit est exact et que seul le découpage diffère. Le compteur de pixels, seul,
aurait rendu « 896 px, divergence son » et envoyé chercher un bug qui n'existe pas.

## Le poll de timer MFP passe à 0 px de l'oracle — et la divergence qu'on croyait tenir n'existait plus (2026-09-02)

Chantier n°3 de la liste des divergences ouvertes (`TODO.md` § *Divergences Hatari & précision
cycle*). Il est **CLOS**, mais aucune des deux moitiés du résultat n'était celle attendue.

**Ce qu'on croyait corriger.** L'inventaire portait depuis le 2026-08-25 : « pas de
`MFP_UpdateTimers` avant lecture IPR/ISR/TBDR en mode bloc — un timer qui expire PENDANT
l'instruction qui polle est vu en retard, jusqu'à 157 cycles ». Le correctif était même
prescrit (un `runTo` CIBLÉ sur les sources `TIMER_*`, surtout pas un `syncTo` nu qui
réactiverait le modèle sync-driven réfuté), et la condition de reprise était remplie : l'oracle
Hatari headless tourne depuis A11.

**Ce que la mesure a dit.** L'étalon `mfp_poll` existe depuis le 2026-08-26 justement pour
exhiber cet écart ; sa note annonçait 120 px, « 18 lignes où IPRA diffère contre 3 pour TADR ».
En décodant les octets de l'image plutôt qu'en comptant les pixels : **IPRA est identique à
Hatari sur les 100 lignes**. L'écart visé n'existe plus — le modèle BLOC préempte déjà le
timeslice CPU à chaque échéance de timer, et BL4/D3 ont fermé le reste. La note décrivait un
état dépassé, et le décompte en pixels l'avait masqué : 80 px, ça ressemblait au même défaut.

**Le port a quand même été écrit, puis retiré.** `MFP_UpdateTimers` (dispatch ciblé des quatre
timers délai en tête de `Mfp::read8`/`write8`, à l'horloge live) : il ne ferme rien et **dégrade
l'étalon de 80 à 88 px** — il fait recharger le timer avant la lecture de TADR et ajoute une
ligne divergente. Balayage de l'instant de dispatch sur ±12 cycles : aucun offset n'atteint 0 px,
et le meilleur (−12) ne fait que reproduire l'image non corrigée. C'est écrit dans
`docs/HATARI_DIVERGENCES.md` pour que personne ne le re-tente.

**La vraie cause, qui n'était écrite nulle part.** Les 80 px étaient **entièrement sur TADR** :
6 lignes sur 100 (période 19), **toujours NeoST = Hatari + 1**. `readTimerData` reconstruisait le
compteur vivant depuis l'échéance vue par le `Scheduler` — or celle-ci n'est que le **plafond
entier** de l'échéance réelle (`scheduleTimerAt` : `next = (nextSub + 255) >> 8`). Le reste était
donc surestimé de presque un cycle CPU, et comme le compte est un `ceil` (≙ `MFP_CYCLE_TO_REG`),
TADR sortait d'un cran trop haut chaque fois que le reste tombait pile sur un multiple du
prescaler. Hatari ne peut pas avoir ce défaut : son `InterruptHandlers[].Cycles` EST la valeur
fractionnaire (unités internes CPU<<8, `CYCINT_SHIFT`).

**Correctif** : partir de l'échéance SOUS-CYCLIQUE que NeoST tenait déjà (`Mfp::timerDueSub_`,
8 bits de fraction, au save-state depuis la v11) et ne lâcher la fraction qu'à la conversion
CPU→MFP. Trois lignes de calcul. Résultat : **0 px contre l'oracle**, les 100 octets IPRA **et**
les 100 octets TADR identiques. Garde vérifiée par mutation : revenir au plafond entier rend
80 px. `storeStoppedCounter` reçoit le même changement par cohérence (chez Hatari les deux
chemins sont le seul `MFP_ReadTimer_AB/CD`) — mais **aucun test ne le couvre**, le muter seul
laisse l'étalon vert : c'est un port raisonné, pas une correction mesurée, et c'est dit tel quel.

**Effet de bord utile** : `mfp_poll` passe de `ref_kind: snapshot` à **`oracle`**, donc le corpus
que la CI re-dérive chaque lundi (A11) passe de 7 à 8 étalons. Il en est le plus robuste : son
programme finit sur `bra.s *`, l'image est figée dès la fin des 100 tours et ne dépend donc pas
de la durée de boot — deux runs Hatari indépendants rendent 0 px, sans `oracle_scan`.

📌 **Leçon de méthode.** La borne « 157 cycles » qui a justifié ce chantier pendant huit jours
est une métrique (`Scheduler::timerMaxLate`, un maximum sur toute la trace, boot compris), pas un
écart de rendu. Et le décompte en pixels d'un étalon ne dit pas CE QUI diffère : ici il fallait
décoder les octets que le programme écrit à l'écran pour voir que la colonne IPRA était verte
depuis longtemps et que tout le reliquat était dans la colonne TADR. Un étalon qui encode des
VALEURS mérite un décodeur, pas seulement un compteur de pixels.

## La 0.6.1 ne se publiait pas : un `apt-get install` sans `update` (2026-09-02)

Le tag `0.6.1` a été posé, mais la release **n'est pas sortie** : le job `linux-bionic` a
échoué, et `publish` dépendant des huit paquets, il a été `skipped`. Deux fois de suite —
donc pas un aléa.

**Cause.** L'index apt de l'image runner est figé à sa date de construction. Quand Ubuntu
publie une révision d'un paquet, l'ancienne SORT du pool et l'index périmé la demande
encore : `libssh-gcrypt-4 0.10.6-2ubuntu0.4`, dépendance transitive de ffmpeg, était
ignorée par les trois miroirs puis rendait 404. L'étape échouait avant d'avoir rien
construit.

**Ce qui rend le diagnostic sûr** : c'était le **SEUL** `apt-get install` du dépôt sans
`apt-get update` devant lui. Les six autres — `tests.yml` ×4, `oracle.yml`, et le job
`linux-arm64` situé quinze lignes plus bas dans le même fichier — l'ont toujours eu. Une
omission isolée, pas une pratique.

📌 **Le tag a été REPOSITIONNÉ** sur le commit portant le correctif. C'est assumé et écrit
ici plutôt que fait en silence : `gh run rerun` rejoue le workflow tel qu'il existe AU TAG,
donc relancer sans déplacer le tag aurait rejoué le même échec indéfiniment. Aucune release
n'avait été publiée depuis ce tag et aucun asset n'avait été diffusé — le numéro n'est donc
pas brûlé et rien n'a changé sous les pieds d'un utilisateur.

## 0.6.1 — vos réglages restent en place (2026-09-02)

Une version de correction. Aucune fonctionnalité nouvelle : **23 défauts** trouvés et
corrigés en cinq rounds de chasse multi-agents, plus la passe de validation matérielle
A12. Ce que l'utilisateur y gagne :

- **Les réglages sont enfin conservés.** Sur macOS et sur les quatre AppImage, NeoST est
  lancé depuis un support en LECTURE SEULE : il range donc sa configuration dans le
  dossier utilisateur — mais il ne créait jamais ce dossier, et **tout réglage était perdu
  en silence à la fermeture**. Cinq paquets sur huit étaient concernés.
- **Le `.dmg` macOS s'installe par glisser-déposer** : il ne contenait que l'application,
  sans le raccourci vers `/Applications`.
- **Le paquet web porte ses licences**, comme les sept autres — et le site les sert déjà.
- **Un save-state ne peut plus être rechargé sous une autre ROM.** L'appariement ne tenait
  qu'aux 2 octets de version TOS, que cinq localisations partagent : reprendre un état sous
  la mauvaise ROM figeait le CPU sans un mot. Le format passe en v19 ; **les états v18 et
  antérieurs sont refusés explicitement**.
- **Un `.PRG` malformé ne peut plus faire lire NeoST hors de sa mémoire** (paquet Windows).
- **Android** : le déballage des données se répare de lui-même au lieu de rester
  définitivement incomplet ; le modèle tactile est refait — un tap produit un vrai clic, et
  le clic droit à deux doigts fonctionne.
- **Borne** : l'image n'est plus amputée sur un écran moins large que 16:10, et une touche
  ne reste plus collée quand on bascule bureau ⇄ borne pendant un appui.
- **Web** : le profil STE démarrait sur une ROM ST ; il démarre désormais sur EmuTOS 256 Ko.
- **UltraSatan** : un paquet resté en vol ne détourne plus l'écriture du secteur suivant,
  et une carte éjectée est réellement remontée.
- **MIDI** : la case MT-32 de la page de configuration répondait dans le vide ; couper tous
  les canaux d'un appareil le rouvrait en grand au redémarrage ; une course entre deux
  threads pouvait corrompre le décodeur.

Les paquets 0.5.2 et 0.5.4 ont été retirés le 2026-09-02 : leurs archives contenaient des
ROM Atari. Leurs tags git restent en place.

## 0.6 — le MegaSTE au banc, le studio MIDI, un paquet 100 % libre (2026-09-01)

Depuis la 0.5.4 :

- **MegaSTE validé au banc Field Service** — suite Q **12/12**, rejouée à chaque palier
  `full` : l'objectif de tête du projet est atteint et GARDÉ.
- **Station MIDI** — l'entrée tire à 31 250 bauds réels (**×20**, 92 % d'un vrai câble),
  **fusion** de plusieurs claviers avec canalisation par source, **aiguillage par canal**
  en sortie, profils d'appareil, appareils homonymes enfin distingués, avance de sortie
  réglable, **synthé GM intégré** ; les appareils matériels marchent sur les **trois**
  plateformes (CoreMIDI, ALSA, winmm).
- **Réseau** — theoldnet.com s'affiche dans CAB depuis le GUI ; EtherNEC (fenêtres
  ROM3/ROM4 corrigées) et Slirp 5/5.
- **Fidélité** — cycles volés du blitter portés à l'horloge des timers, stall du flush
  FIFO FDC, halt du 68000 annoncé, retrait gauche med corrigé ; Closure et un étalon
  Spectrum 512 **généré** rejoignent le palier pixel, trois démos étalons quittent les
  ROM Atari.
- **Interface** — page Input par port, clavier ST cliquable, mixeur audio par source,
  bascule bureau ⇄ borne au clavier.
- **Paquets 100 % libres par défaut** — l'historique public ne distribue plus rien de
  propriétaire (pack **165 → 12 Mio**), EmuTOS seul, trois démos embarquées, chaque
  composant livré nommé avec sa licence.

⚠ Le `.dmg` macOS n'est ni signé ni notarisé et le `.zip` Windows n'est pas signé :
Gatekeeper affiche « NeoST est endommagé ». En attendant la notarisation,
`xattr -dr com.apple.quarantine /Applications/NeoST.app` lève le blocage.

## 0.5.4 — dongles, ports, MIDI vérifié (2026-08-23)

Depuis la 0.5.2 : **clés de protection** (Cubase rouge/noire, Notator/Creator, Leader
Board, Cricket Captain, B.A.T. II, Music Master, Jeanne d'Arc, Multiface, Ultimate
Ripper, DAC Pro Sound) avec page Dongles, `disks/dongles.txt` et oracle de rejeu ;
**page Input par port** (souris / manette / clavier sur chaque DE-9) ; **Cubase Lite
vérifié note à note** en headless, corpus MIDI piano/blues ; port MIDI ALSA sous Linux ;
save-state v16. Détail dans les chantiers datés ci-dessous.

## Chasse round 5 : le save-state acceptait une AUTRE ROM, et le harnais annonçait des écritures tronquées comme réussies (2026-09-02)

**Le tri se resserre : 7 soumises, 3 retenues, 4 ÉCARTÉES.** Le taux de rejet passe à
57 %, et surtout **la lentille de régression n'a rien trouvé** contre les correctifs du
round 4 : la seule trouvaille visant la réécriture tactile et le bornage du zoom borne a
été réfutée (prémisse fausse, comportement documenté). Le modèle tactile repris d'un bloc
a donc tenu son premier passage adverse — c'est la première fois de la journée qu'un lot
de correctifs survit intact.

- 🐞 **`Machine.cpp` — un save-state repris sous une AUTRE ROM était accepté, et figeait
  le CPU.** `loadState` affirme refuser tout état pris dans une autre config, et compare
  effectivement le type de machine, la taille RAM, les périphériques et un CRC32 COMPLET de
  la cartouche — mais, pour la ROM, les seuls **2 octets de version TOS**. Or `tos104us`,
  `uk`, `fr`, `de` et `es` portent toutes `$0104` ; `tos106*` toutes `$0106` ; `etos192us`
  et `etos192fr` également `$0104`. La ROM étant le composant HORS-snapshot par excellence,
  rien ne garantissait donc qu'on la recharge. Un état repris sous une localisation
  différente passait tous les contrôles, et la RAM restaurée — vecteurs d'exception, piles,
  adresses de retour, pointeurs système — désignait des adresses d'une AUTRE image : CPU
  figé, sans un mot. L'en-tête porte désormais un **CRC32 de l'image ROM** (v18 → v19).
  Vérifié : même ROM acceptée, `etos192fr` sur un état pris avec `etos192us` — même version
  `$0104` — rejeté avec un message qui nomme la cause.
- 🐞 **`main_headless.cpp` + `Tracer.cpp` — cinq écrivains annonçaient « réussi » sur une
  écriture tronquée.** Seul `writePpm` contrôlait `fwrite` ET `fclose`, et son commentaire
  disait pourquoi : « un disque plein peut n'échouer qu'au flush final — une capture
  tronquée qui "réussit" finit diffée comme si c'était l'image ». `--dump-at`, la trace, le
  WAV de `--sound-dump`, `--midi-dump` et `--serial-dump` jetaient tous leurs retours,
  contre l'invariant que le harnais s'est lui-même donné (« une SORTIE fichier a échoué →
  exit ≠ 0, jamais silencieux »). Sur le même disque plein, `--screenshot` rendait 1 en
  criant et `--dump-at` rendait 0 en annonçant la réussite.
  **Reproduit sur un disque RAM de 2 Mo** : avant, dump de 4 Mo → `exit 0` et ligne de
  succès pour 1 949 696 octets réellement écrits ; après, `exit 1` et
  « FAILED RAM dump — 1953792/4000000 bytes written (disk full?) ». Idem pour la trace :
  `Tracer::close()` rend maintenant un verdict, et le harnais annonce « it is TRUNCATED ».
  ⚠ Le contradicteur a RÉTROGRADÉ la gravité annoncée, à juste titre : aucun runner du
  dépôt n'appelle `--dump-at`, et les consommateurs réels de la trace et du série échouent
  du bon côté (une troncature ampute la fin, donc le verdict disparaît et le test rougit).
  L'impact retenu est un artefact partiel indistinguable d'un artefact complet en
  diagnostic manuel — plus le temps perdu à chasser une « divergence » qui commence pile à
  l'octet de troncature.
- 🐞 **`InputCallbacks.cpp` — touche collée si le mode borne bascule pendant un appui.**
  `onKey` est bâti sur un invariant écrit : le BREAK d'une touche dont le MAKE est parti au
  ST doit TOUJOURS partir, sinon la touche reste collée et le clavier semble en panne. Les
  trois filtres à condition volatile (ImGui, joystick clavier, overlay disquette) sont donc
  placés APRÈS le bloc de relâchement. Le filtre borne F9/F10 était le seul placé AVANT — et
  `A.kiosk` est volatile, la bascule bureau ⇄ borne se faisant au clavier. F9 et F10 ont un
  scancode ST (`$43`/`$44`) : leur BREAK pouvait donc être avalé. Filtre déplacé ; F12, qui
  n'a pas de scancode ST, reste en amont sans risque.

**Ce qui a été écarté, et pourquoi c'est utile** : un « rétrécissement de 15 % » du zoom
borne (prémisse fausse) ; un `--loopback` hors fenêtre (mécanisme réel, impact nul sur les
trois chemins invoqués) ; un aliasing de registres RTC (une garde amont ferme le cas — le
harnais de la trouvaille testait `Rtc.cpp` compilé SEUL, un chemin qui n'existe pas) ; un
`std::terminate` du navigateur borne (site immunisé, prouvé). Quatre mécanismes exacts sur
le papier, quatre impacts qui ne tiennent pas.

## Chasse round 4 : le tactile Android est REPRIS D'UN BLOC, et le web public démarrait le STE sur une ROM ST (2026-09-01)

**Scheduler et Bus rendent zéro.** Deux sous-systèmes lourds, jamais balayés jusque-là —
l'ordonnanceur qui porte toutes les puces, et le décodage MMIO avec sa whitelist de bus
errors — n'ont produit aucune trouvaille. Deux autres ont été ÉCARTÉES par le
contradicteur, toutes deux de l'axe « outillage de test » : leur mécanisme était exact
mais leur IMPACT ne tenait pas (une garde amont fermait le cas). C'est le bon tri.

**Deux défauts hors Android, dans des zones jamais regardées**

- 🐞 **`main_web.cpp` — le bundle web PUBLIC démarrait le profil STE sur une ROM ST.**
  Le défaut du STE était resté `tos162uk.img`, posé le 2026-08-02 et jamais revu — or la
  purge du 2026-08-30 a sorti cette ROM du bundle. L'enchaînement était silencieux :
  `adjustMachineForTos` ne pouvant pas OUVRIR le fichier rendait la machine demandée
  telle quelle, la Machine était donc construite en STE, `loadTos` échouait, et le repli
  chargeait EmuTOS 192 Ko — une ROM ST-only — **sans rejouer la garde**. Le défaut pointe
  désormais `etos256us.img`, réellement préchargée (CMakeLists:497), ce qui referme le cas
  à la source avant même que le repli n'entre en jeu.
- 🐞 **`StScreenView.cpp` — le zoom de la borne AMPUTAIT l'image sous 16:10.** L'échelle
  ne se calculait que sur la HAUTEUR et rien ne vérifiait que le contenu tenait en
  largeur : sur une dalle 4:3 ou 5:4, ou une fenêtre borne redimensionnée, l'image était
  coupée des deux côtés. Le chemin bureau posait pourtant la règle en toutes lettres — « on
  préfère une bande haut/bas à une image amputée » — et supposait le cas absent en borne,
  « son écran étant plus large que haut » : vrai en 16:9, faux dès qu'on descend. La
  fonction n'avait même pas de paramètre de largeur ; elle reçoit `cW` et applique la même
  borne.

🔁 **Le tactile Android est REPRIS D'UN BLOC, après trois rustines successives.** Les
rounds 2, 3 et 4 ont trouvé chacun un défaut DIFFÉRENT au même endroit — compteur de
doigts lu après décrément ; mouvement d'un second doigt diffé contre la position du
premier, donc l'écart ENTRE LES DOIGTS ; puis un id « primaire » jamais réarmé, qui figeait
la souris dès que ce doigt se levait le premier et ressuscitait le bug précédent via le
recyclage d'id d'Android. Trois symptômes, une seule cause : **un état global pour
plusieurs doigts**.
Le modèle porte désormais **un état PAR DOIGT**. Un mouvement se diffe contre la position
du MÊME doigt — l'écart entre deux doigts n'est plus représentable. Quand le doigt qui
pilote se lève, un autre prend le relais AVEC SA PROPRE position, donc sans saut. Un id
recyclé arrive par `FINGERDOWN` et sème sa position : rien ne survit d'un geste à l'autre.
**Vérifié sur neuf scénarios**, harnais dont le corps est EXTRAIT du fichier par script :
tap simple → clic gauche ; glissé → 179 px ; deux doigts (sans mouvement, avec mouvement
du second, avec `ACTION_MOVE` sur les deux) → clic droit ; **pilote levé en premier → la
souris reste vivante** (le bug du round 4) ; **id recyclé → aucun saut** (594 px avant) ;
mouvement d'un doigt inconnu → ignoré ; douze doigts → pas de débordement.

- 🐞 **Le déballage Android purge aussi les RELIQUES.** Le sous-dossier `data/` posé au
  round 3 déplaçait la destination, mais c'est le nom SOURCE qui est relatif : tant qu'un
  fichier du même nom traînait à la racine du stockage interne — déballé là par une version
  antérieure — SDL le trouvait AVANT l'AssetManager, et « l'asset » restait ce fichier
  périmé. Une relique de 0 octet suffisait à rendre l'application définitivement sans TOS,
  en silence : `want` valant 0, la branche entière était sautée, message d'erreur compris.
  Les reliques sont maintenant supprimées au démarrage — ce sont des fichiers que NeoST a
  écrits lui-même, jamais des fichiers utilisateur.

📌 **Leçon.** Quatre rounds : les deux premiers ont trouvé des défauts dans le code
existant, les deux derniers presque uniquement dans le travail du jour. Et sur le tactile,
patcher symptôme par symptôme du code qu'on ne peut ni compiler ni exécuter a produit trois
correctifs dont deux étaient faux. Le harnais ne rate que ce à quoi on n'a pas pensé — et
c'est justement ce qu'on cherche. Reprendre le modèle valait mieux qu'une quatrième rustine.

## Chasse round 3 : trois axes rendent ZÉRO, et la lentille de régression démolit deux de mes correctifs (2026-09-01)

**Le résultat le plus utile est un silence.** Trois des cinq axes — **Blitter + FDC/DMA
disquette**, **GEMDOS (handles, Pexec, codes d'erreur, Fsfirst/DTA)** et **couverture du
save-state** (chaque puce sérialise-t-elle tout son état mutable ?) — ont rendu **zéro
trouvaille**. La consigne « zéro est une excellente réponse » a donc été prise au mot, et
c'est un signal en soi : ces sous-systèmes ne cèdent pas à cette méthode. L'axe MFP a
produit une trouvaille, correctement ÉCARTÉE (divergence de fidélité Hatari, catégorie
exclue — et son impact annoncé était démenti par la mesure du contradicteur).

**Les deux seules trouvailles sont des RÉGRESSIONS SUR MES PROPRES CORRECTIFS du round 2,
posés moins d'une heure plus tôt.** Toutes deux prouvées par exécution, toutes deux
majeures, toutes deux dans le frontend Android — le fichier que le build par défaut ne
compile pas et que je n'avais vérifié qu'au `-fsyntax-only`.

- 🐞 **Le clic droit à deux doigts était TOUJOURS inatteignable.** Mon correctif avait levé
  un verrou (le compteur de doigts lu après décrément) ; il y en avait un SECOND, ailleurs
  et indépendant : `SDL_FINGERMOTION` ne filtre pas `fingerId` et diffe la position du
  doigt qui bouge contre un `lastX/lastY` UNIQUE. Le premier mouvement d'un second doigt
  calculait donc **l'écart ENTRE LES DOIGTS** : mesuré, 0,40 de « travel » pour un seuil de
  tap à 0,02, soit 20×, et 360 px de déplacement souris parasites. Le tap GAUCHE tombait
  pareillement dès qu'une paume touchait la dalle. Seul le doigt PRIMAIRE pilote désormais
  le mouvement ; les autres ne servent qu'à compter.
- 🐞 **La réparation du déballage était un NO-OP.** Sous Android, `SDL_RWFromFile` avec un
  nom RELATIF cherche D'ABORD dans le stockage interne et ne retombe sur l'AssetManager
  qu'en cas d'échec (SDL 2.30.9, le tag qu'épingle `fetch_sdl.sh` — le contradicteur est
  allé le lire). Comme je déballais à la RACINE du stockage interne, « lire l'asset »
  revenait à relire le fichier DÉJÀ DÉBALLÉ : la garde comparait le fichier à lui-même et
  valait toujours vrai. Pire pour le cas que mon propre commit nommait — un fichier de
  0 octet — : `want` valant 0, la condition `want > 0` était fausse et la branche entière
  était sautée **en silence**, rendant inatteignable jusqu'au message d'erreur que j'avais
  ajouté. Le déballage vit désormais dans un **sous-dossier** `data/`, ce qui rend au nom
  relatif son sens : celui de l'asset.

**Vérification, cette fois par l'exécution et non par la lecture.** Après deux correctifs
Android faux d'affilée, le troisième a été éprouvé sur un harnais dont le corps de
`handleTouch` est **EXTRAIT du fichier par script**, pas retapé : six scénarios (deux
doigts sans mouvement, avec un mouvement, avec `ACTION_MOVE` sur les deux, paume posée,
glissé à un doigt, tap simple). Le clic droit sort maintenant dans les quatre cas à deux
doigts — « travel » 0,0004 au lieu de 0,400 — le glissé déplace toujours la souris
(179 px) et le tap simple rend toujours un clic gauche.
⚠ Ce qui n'est PAS vérifié : le correctif du déballage repose sur la sémantique de
résolution de chemin de SDL sous Android, qu'aucune machine ici ne peut exécuter. Il est
établi par lecture du SDL épinglé et par construction — le nom relatif ne peut plus
désigner la destination — mais pas par une exécution. Et une installation existante verra
ses fichiers re-déballés dans le sous-dossier : sans conséquence, l'APK n'ayant jamais
tourné sur un appareil.

**Leçon de méthode** : les deux rounds précédents avaient trouvé des défauts dans le code
d'autrui ; celui-ci n'a trouvé que les miens. Une lentille de régression braquée sur le
travail de la même journée vaut, à ce stade, plus qu'un axe neuf.

## Chasse round 2 : 6 défauts de plus, dont un trou dans le correctif du matin (2026-09-01)

**Ce qui a changé dans la méthode.** Cinq axes NEUFS (le frontend GUI, les frontends
non-desktop, la vidéo, l'IKBD, et une **lentille de régression braquée sur les correctifs
du jour même**), et une réfutation à barre haute : les contradicteurs devaient ESSAYER DE
REPRODUIRE, renseigner ce qu'ils avaient réellement exécuté, et réfuter dans le doute.
Résultat : **8 soumises → 7 prouvées PAR EXÉCUTION, 0 par raisonnement seul, 1 écartée**.
Le round 1 avait laissé passer 9 sur 9 ; le tri fonctionne maintenant. L'écartée l'a été
pour la bonne raison — elle tombait dans la catégorie « fidélité Hatari » exclue d'office.
Deux axes ayant trouvé le même bug Android, cela fait **6 défauts distincts**.

🐞 **Le plus important : la lentille de régression a trouvé un trou dans le correctif
posé le matin même.** `fixBools()` avait été appliqué aux **trois sites nommés** par le
round 1 (`Scc::Chn`, `Fpu`, `StePads`) au lieu d'auditer — et il en manquait un, le plus
lourd : `ar(reg)` (`Cpu68k.cpp`) copie en bloc `moira::Registers`, qui porte
`StatusRegister sr` avec **neuf booléens**, dont le bit **superviseur**. L'audit est
désormais fait **par le COMPILATEUR** et non au grep : un
`static_assert(!std::is_class_v<T>)` temporaire fait énumérer toutes les instanciations,
ce qui donne les **7 agrégats non-tableaux** du dépôt — 4 portent des booléens (tous
traités), 3 n'en portent pas (`Bus::MegaSteCache`, `Scu`, `moira::PrefetchQueue`). La
liste complète est écrite dans `StateArchive.hpp`, avec l'avertissement qu'un agrégat
AJOUTÉ plus tard ne sera signalé par rien.

**Les cinq autres**

- 🐞 **`ConfigWindow.cpp` / `AppLoop.cpp` — la case MT-32 de la page MIDI était un
  CONTRÔLE MORT.** Elle posait `reqMidiOutMt32`, que rien ne consommait : la ligne
  manquait entre ses deux voisines dans le bloc de déversement de la boucle. La case se
  recochait seule à la trame suivante (elle relit `cfg` à chaque passage), et le réglage
  n'était atteignable que par le menu Machine — hors de la page dont c'est le sujet. Le
  contradicteur l'a prouvé **par compilation** : en supprimant le membre d'un en-tête
  recopié hors dépôt, `AppLoop.cpp` compile encore, donc aucun lecteur ne peut exister.
  Il a aussi RÉTROGRADÉ lui-même la gravité annoncée — ni plantage, ni UB, contournement
  existant. Honnêteté à porter à son crédit.
- 🐞 **`AppConfig.cpp` — couper tous les canaux d'un appareil MIDI le rouvrait EN GRAND
  au redémarrage.** `formatChannelMask(0)` écrivait « 1-16 » et `parseChannelMask("")`
  rendait 0xFFFF, au nom d'un invariant écrit — « un masque vide serait une destination
  muette, ce qu'on n'écrit jamais » — que le bouton `none` de la page MIDI a rendu faux.
  Le réglage se retournait donc exactement à l'envers. Un jeton explicite « none » sépare
  désormais le masque vide VOULU de la ligne ILLISIBLE, qui vaut toujours « tous les
  canaux » comme prévu à l'origine. 8 assertions neuves.
- 🐞 **`main_android.cpp` — le déballage des assets ne vérifiait aucune écriture.** Ouvrir
  en `"wb"` crée le fichier à 0 octet, `SDL_RWwrite` n'était pas contrôlé, et la fonction
  rendait `true` inconditionnellement ; `fileExists()` ne testant que l'ouverture, un TOS
  tronqué (stockage plein, processus tué) était réputé « déjà déballé » **POUR TOUJOURS**.
  Lectures et écritures sont vérifiées, un fichier partiel est SUPPRIMÉ plutôt que laissé
  en place, et la garde compare désormais la TAILLE à celle de l'asset.
  ⚠ **La phrase « le déballage se répare tout seul au lancement suivant », écrite ici
  d'abord, était FAUSSE** : le round 3 a montré que la garde se comparait à elle-même.
  Corrigé le jour même — cf. l'entrée du round 3 ci-dessus. La phrase est rectifiée là où
  elle a été écrite plutôt qu'effacée.
- 🐞 **`main_android.cpp` — un tap n'envoyait AUCUN clic au ST.** `Ikbd::mouseEvent`
  n'émet rien, il écrase l'état ; le paquet souris n'est construit qu'à la VBL, et l'appui
  suivi du relâchement dans la MÊME trame ne laissait aucun changement à voir. Le remède
  existait déjà deux fonctions plus haut — `g_injectHold`, le clic MAINTENU 4 trames de la
  page clavier, dont le commentaire décrit précisément ce piège — il n'était simplement
  pas câblé au tactile. Il l'est.
- 🐞 **`main_android.cpp` — le clic droit à deux doigts était structurellement
  inatteignable.** Le compteur de doigts était capturé avant décrément, mais la garde qui
  suit ne laisse passer que le dernier doigt levé : la valeur lue valait donc toujours 1,
  et `right = fingers >= 2` était toujours faux. Un maximum atteint pendant le geste
  (`peakFingers`) remplace le compteur instantané.

📌 **Le contrôle syntaxique a payé, et il a attrapé une erreur DE MOI.** Le frontend
Android n'est pas construit par le build par défaut : j'ai bouchonné SDL2 (Homebrew) et
GLES2 (33 fonctions et 20 constantes générées depuis le fichier lui-même) pour obtenir un
`-fsyntax-only`. Il a immédiatement signalé que mon `std::vector<uint8_t> buf(size_t(sz));`
est le **« most vexing parse »** — déclaré comme une FONCTION, pas une variable. L'écriture
d'origine y échappait par accident, son argument étant un ternaire. Sans ce contrôle, du
code qui ne compile pas serait parti sur la branche : rien dans le palier `full` ne touche
ce fichier.

Palier `full` vert, poste au repos. Le dépôt n'a pas été modifié par les agents.

## Chasse aux bugs multi-agents : 9 défauts trouvés et corrigés, dont un hors bornes qui ne visait que Windows (2026-09-01)

**La méthode.** Cinq axes de recherche en parallèle (chemins hôte et bac à sable ;
parseurs d'entrées non fiables ; pile réseau et extensions du port cartouche ; audio
temps réel et concurrence ; bornes et débordements du cœur), deux trouvailles maximum
par axe, puis **un contradicteur par trouvaille** dont la mission était de la RÉFUTER,
pas de la confirmer, avec consigne de reproduire plutôt que d'argumenter. 14 agents,
34 min. Périmètre EXCLU d'emblée et écrit dans chaque prompt : une divergence de
fidélité avec Hatari n'est pas un bug (c'est `docs/HATARI_DIVERGENCES.md`), sauf si elle
produit un plantage ou de l'UB.
⚠ **Zéro trouvaille sur neuf n'a été écartée.** Un étage adversarial qui ne rejette
jamais rien n'a pas fait son travail d'étage adversarial — deux trouvailles ont donc été
re-vérifiées À LA MAIN avant d'être crues (`HostPath` et `Fdc`, toutes deux confirmées).

**Les cinq majeurs**

- 🐞 **`GemdosHd.cpp` — lecture hors bornes à ~2 Gio sous le tampon, sur le paquet
  WINDOWS uniquement.** Le champ `slen` de l'en-tête d'un `.PRG` (taille de la table de
  symboles) n'était validé NULLE PART : ni `gemPexec`, qui ne teste que tlen/dlen/blen,
  ni la borne de `loadAndReloc`, qui ne couvre que texte+données. Avec `long` sur
  32 bits — MinGW-w64, donc le paquet livré depuis la 0.5.1 — un `slen ≥ 0x80000000`
  rendait `(long)nSym` négatif : la garde passait (débordement signé, déjà de l'UB),
  puis `relIdx += nSym` rendait l'index NÉGATIF, et la table de relocation ainsi « lue »
  était appliquée à la RAM invitée. Sur LP64 la branche n'était simplement jamais prise :
  **le défaut ne se voyait que sur la plateforme qu'on ne lance jamais à la main** — le
  lien avec A12 est direct. Le contradicteur a prouvé l'atteignabilité sur le binaire
  réel (PRG forgé lancé depuis le bureau TOS via `DESKTOP.INF`) et rejoué l'arithmétique
  LLP64 dans un programme séparé. Correctif : arithmétique en `int64_t`, où
  `(int64_t)nSym` est positif pour tout `uint32_t`.
- 🐞 **`Acsi.cpp` — un paquet UltraSatan en vol détournait l'écriture suivante.** Le
  drapeau `usatanPending_` n'était effacé que par `writeToDisk()`, `executeUltraSatan()`
  et `reset()`. Si le transfert DMA du paquet n'avait pas lieu, il survivait, et le
  prochain `WRITE(6)` partait dans `usatan_->writeData()` au lieu de l'image : le secteur
  destiné à la carte SD écrasait les réglages de l'appareil — et `writeData` rendant
  ST_OK, **l'écriture perdue était annoncée RÉUSSIE au pilote**. Désarmé désormais par
  `emulateCommand()` et par `clearData()`. Vérifié que le chemin NORMAL n'est pas touché :
  `clearData()` est appelé APRÈS `writeToDisk`, et `emulateCommand()` n'est jamais pris
  pour le paquet UltraSatan.
- 🐞 **`Acsi.cpp` — chemin périmé après démontage.** `unmountAll()` fermait le
  descripteur mais gardait `path`, et `mountedPath()` le rend dès que `enabled` est vrai
  (ce qui reste le cas des slots UltraSatan). `App::usatanApply()` s'en sert comme unique
  test « faut-il remonter ? » : le chemin périmé étant égal à celui voulu, le remontage
  était sauté et le slot restait « carte absente » (NOT READY / ASC $3A) jusqu'à la fin
  de la session. Le chemin part maintenant avec le descripteur, montage raté compris.
- 🐞 **`Machine.cpp` — save-state forgé, gel définitif.** Les gardes d'horloge ne
  contraignaient que l'ÉCART entre `frameStart_` et `frameEnd_` : un couple décalé DE
  FAÇON COHÉRENTE les passait toutes, et `runFrame` bouclait ensuite sur 2^58 cycles.
  `frameStart_ >= 0` n'avait aucune borne HAUTE, et le commentaire d'en tête annonçait
  pourtant fermer exactement ce trou. La fenêtre est désormais **ancrée sur l'horloge
  maître restaurée** — contrôle impossible plus haut, `sched` n'étant restauré qu'après.
  **Prouvé par forge** : offsets retrouvés en diffant deux états (frameStart_ à 23,
  frameEnd_ à 32), CRC32 du payload recalculé, décalage de 2^58 appliqué aux deux →
  l'état est REJETÉ par nom, alors qu'un état légitime charge toujours.
- 🐞 **`MidiOutHost.cpp` — course de données sur le décodeur MIDI.** `panic()` faisait
  `parser_.reset()` depuis le thread principal pendant que le thread de livraison était
  dans `parser_.byte()` — `std::vector` de SysEx écrit des deux côtés, sans verrou
  (`outMtx_` ne couvre que `emit`). Et le chemin est AUTOMATIQUE : `setDestinations` →
  `closeDestinations` → `panic`, déclenché à 1 Hz par la boucle GUI. `parser_`
  n'appartient plus qu'au thread de livraison ; `panic()` pose une demande atomique que
  le worker honore entre deux octets — l'endroit exact où une remise à zéro a un sens.
  Pas de verrou ajouté : en prendre un ferait attendre le thread d'émulation sur un
  pilote MIDI.

**Les quatre mineurs**

- **`HostPath.cpp`** — le clamp de « .. » était le corps purement Unix d'Hatari : sous
  Windows, remonter au-dessus de la racine JETAIT la lettre de lecteur et rendait un
  chemin relatif au lecteur COURANT du processus (et « //X » pour une racine UNC). La
  remontée est bornée à la racine réelle du lecteur ; 5 cas ajoutés à l'auto-test, dont
  deux Posix qui prouvent que rien n'a bougé de ce côté.
- **`StateArchive.hpp`** — la normalisation des booléens ne couvrait que les `bool`
  passés SEULS, alors que son commentaire affirmait couvrir « TOUS les booléens du projet
  d'un coup ». Un bool membre d'un agrégat copié en bloc (`Scc::Chn`, `Fpu`, `StePads`)
  recevait son octet brut — UB, et `Chn` est relu dans les DEUX polarités. `fixBools()`
  est le rattrapage, appelé sur les trois sites ; la portée réelle est écrite là où le
  commentaire mentait. C++17 n'a pas de réflexion : il n'y a pas de correctif générique.
- **`DriveSound.cpp`** — `init()` est appelé deux fois (48 kHz, puis la fréquence
  négociée) et écrasait ses trois poignées sans rien libérer : un `ma_engine`, son
  resource manager, son THREAD de travail et deux `ma_sound` aux tampons PCM décodés
  restaient vivants jusqu'à la fin du processus. `init()` commence par `shutdown()`,
  qui est idempotent.
- **`Fdc.cpp`** — `readTrackStx` convertissait en `uint16_t` un délai valant
  1 600 000 / `trackImageSize` ; `trackImageSize` vient du FICHIER et n'est borné que par
  le haut, donc toute piste STX de moins de 25 octets d'image produisait une conversion
  flottant→entier **hors domaine**, comportement indéfini ([conv.fpint]) et non un
  enroulement. Borné avant conversion.

**Gardes posées** : 10 assertions neuves au palier `fast` (5 pour le clamp de racine,
5 pour `fixBools`), plus la garde d'ancrage d'horloge prouvée par forge. Palier `full`
vert. Le dépôt n'a PAS été modifié par les agents (lecture seule imposée, `git status`
vérifié vide à la fin de la chasse).

## A12 — la première cible de livraison est validée sur du vrai matériel, et le paquet macOS n'enregistrait rien (2026-09-01)

**Le point de départ.** Huit paquets livrés, **aucun** jamais lancé sur la machine
visée : la CI construit, elle n'exécute pas. Et l'outil qui aurait pu répondre ne le
pouvait pas — `run_perfbench.py` garde des RATIOS, machine-indépendants *par
construction*, ce qui en fait une bonne barrière de CI et une réponse impossible à
« cette machine tient-elle le temps réel ? ».

**L'outil.** `run_perfbench.py --budget` (mode ajouté à l'outil existant, pas un 18ᵉ
outil — garde-fou A38) rend le **facteur temps réel absolu** : trames/s ÷ balayage
annoncé par la machine émulée. Le balayage est **lu sur la sortie** (`video: … @ NN Hz`)
et jamais supposé — il dépend de la ROM. `NEOST_HEADLESS=…` pointe le banc sur le
binaire LIVRÉ plutôt que sur le build de l'arbre. La **charge** de la machine entre dans
la config relevée : la leçon du 2026-08-25 s'appliquait aussi à l'outil qui mesure.
⚠ Ce mode ne doit jamais entrer dans un palier — un seuil absolu sur un runner de CI est
exactement le piège décrit dans l'en-tête du fichier.

**Le registre.** [`docs/HW_VALIDATION.md`](docs/HW_VALIDATION.md) : une ligne par cible,
avec la config de la machine, et un protocole en cinq pas (intégrité, contenu, chaîne de
confiance, exécution, débit). Il n'accepte que du mesuré — une case vide vaut mieux
qu'une case remplie de bonne foi.

**La passe macOS arm64** (MacBook Air M1, 8 Gio, macOS 15.6), sur le `.dmg` 0.6 **publié** :

- somme conforme à `SHA256SUMS.txt` — l'asset ayant été remplacé APRÈS la publication du
  fichier de sommes, ce n'était pas acquis ;
- contenu **100 % libre vérifié sur le paquet servi** (EmuTOS seul, trois démos, les
  trois licences) — jusque-là la promesse ne l'était que sur le script qui fabrique ;
- **le palier 0 de signature tient sur l'artefact publié** : bundle scellé
  (`Sealed Resources version=2`, `Info.plist entries=9`), *valid on disk*, et
  `syspolicy_check distribution` ne relève plus qu'**un** défaut fatal, *Notary Ticket
  Missing*. La cause du cul-de-sac « NeoST est endommagé » a donc bien disparu. Le
  `.dmg` lui-même, lui, n'est toujours pas signé ;
- **débit du binaire livré : ×26,9 temps réel au pire**, contre ×26,4 pour le build
  natif de l'arbre — le paquet universal2 ne coûte **rien de mesurable**.

🐞 **Ce que la passe a trouvé, et qu'aucun test ne pouvait voir : le paquet macOS ne
pouvait JAMAIS enregistrer sa configuration.** Sur le `.dmg` monté, `Contents/` est en
lecture seule ; la règle A36 retombe donc — correctement — sur `~/.config/neost/`. Mais
**rien ne créait ce dossier** : l'ouverture du temporaire échouait, le repli sur le
répertoire courant échouait aussi (cwd = `/` au lancement Finder), et le paquet annonçait
`[cfg] cannot write … configuration NOT saved` à chaque lancement. Tout réglage perdu à
chaque fermeture, chez **tout premier utilisateur**.

**Prouvé par expérience, pas par lecture** : (A) sans le dossier → le message ;
(B) le dossier créé à la main, *rien d'autre changé* → configuration écrite. Correctif
dans `writeConfigAtomic` (`src/gui/AppConfig.cpp`) : créer le dossier parent du chemin
retenu — et **pas** dans `resolve()`, qui est une fonction pure et le reste (c'est ce qui
la rend testable). Le message d'erreur nomme désormais le chemin VOULU : son écrasement
par le repli est ce qui rendait le défaut illisible dans le journal. Revérifié dans les
conditions exactes du défaut (bundle `chmod a-w`, `~/.config/neost` supprimé) : plus de
message, `neost.cfg` écrit. Palier `fast` vert.

**Ce qui n'est PAS acquis, et pourquoi c'est écrit** : le pas visuel de macOS. Lancé
depuis une session d'automatisation, le `.app` prend la barre de menus mais n'affiche
aucune fenêtre — **le build de dev se comporte à l'identique sur la même machine**, donc
le contrôle est négatif et rien n'incrimine le paquet : c'est le contexte d'exécution
qui ne donne pas de fenêtre. À reprendre à la main dans une vraie session graphique.
Windows, l'APK sur appareil, le Raspberry Pi et Linux restent entiers — un émulateur ne
soldera pas la case Android, c'est encore QEMU.

📌 Autre relevé de la passe, **corrigé le même jour** : le `.dmg` ne contenait **pas de
lien vers `/Applications`** — pas d'installation par glisser-déposer, il fallait copier
le `.app` à la main. `package_macos.sh` monte désormais un dossier de présentation (le
`.app` + le lien) au lieu de passer le seul bundle à `hdiutil` ; `ditto` et non `cp -R`,
parce que lui seul préserve la signature, et le sceau est vérifié APRÈS la copie — un
`.dmg` dont le `.app` est descellé rejouerait le « NeoST est endommagé » que le palier 0
vient d'éteindre. Disposition et sceau vérifiés sur un `.dmg` fabriqué et monté ici.

**Suite du même jour — les pas 1 et 2 sont soldés sur les HUIT paquets.** Ils ne
demandent aucun matériel : ils se font sur les assets publiés, depuis n'importe quelle
machine. `shasum -c SHA256SUMS.txt` → **8/8**. Contenu : `unzip -l` pour les archives,
le manifeste `files:[…]` d'`index.js` pour le bundle web (la liste faisant foi de ce
qu'`index.data` embarque — 17 entrées), et `tools/appimage_ls.py` pour les quatre
AppImage, `unsquashfs` n'existant pas sur macOS : il lit le superbloc squashfs et
décompresse la table des répertoires. **Aucune ROM Atari nulle part**, bundle web
compris — le troisième canal de la purge, ici re-vérifié sur le paquet publié. La
promesse « 100 % libre » n'était vérifiée que sur le script qui fabrique ; elle l'est
maintenant sur les artefacts servis.

🐞 **Non-conformité GPL trouvée et corrigée : le paquet web ne portait aucune licence.**
Quatre fichiers, `index.*`, rien d'autre — et la page ne mentionne ni GPL ni licence.
Il est pourtant distribué DEUX fois : le `.zip` de la release et le site GitHub Pages.
C'est la non-conformité même que le 2026-08-19 avait corrigée pour les paquets de
bureau ; le job `wasm` était l'un des **deux** jobs de paquet sans garde de licence.
Le job pose désormais les trois textes dans `wasm/licenses/`, les vérifie sur le patron
des six autres et les zippe — Pages les reçoit aussi, son artefact étant le dossier
`wasm/`. **Huit jobs distincts gardent maintenant les licences**, ce que
`docs/RELEASE.md` affirmait déjà alors qu'ils n'étaient que **sept** : l'affirmation
comptait des occurrences, pas des jobs (`borne` en porte deux). Elle devient vraie.
L'autre job non gardé, `android`, s'est révélé SAIN et n'a rien reçu :
`stage_assets.sh` pose les licences sous `set -euo pipefail`, un `cp` qui échoue casse
le build — garantie par construction, une garde de plus n'aurait été que de la cérémonie.
⚠ Correctif de CI : il ne sera exercé qu'à la prochaine construction de release. Vérifié
ici autant que ce poste le permet (YAML valide, logique de l'étape rejouée à la main →
zip portant bien `licenses/`).

**Troisième temps — ce qui se tranche SANS la cible, et un trou structurel.**
Deux classes de défaut de premier lancement se vérifient sur l'artefact depuis
n'importe quel poste. **Windows** : la table d'importation PE des deux exécutables ne
cite que des DLL système (`KERNEL32`, `USER32`, `GDI32`, `OPENGL32`, `WINMM`, `WS2_32`,
`SHELL32`, `msvcrt`) — le build MinGW est statique, donc le classique « il manque
`libstdc++-6.dll` » est **exclu** sans machine Windows. **Raspberry Pi** : le plancher
glibc était **déjà gardé** (le job `raspberry` échoue au-delà de `GLIBC_2.36`) — rien
ajouté, le contrôle est au bon endroit.

Au passage, la CI en fait plus qu'A12 ne le laissait entendre : **six jobs** lancent
`tools/smoke_package.sh` sur le paquet (version, boot EmuTOS, disquette embarquée, HD
GEMDOS). Ce qui manque n'est donc pas « le paquet ne démarre jamais », mais trois choses
précises : l'INTERFACE (pas d'affichage en CI), le MATÉRIEL réel, et le CHEMIN
D'INSTALLATION d'un utilisateur.

⚠ **Le trou structurel, et il explique le bug du matin** : les six smoke-tests tournent
sur un dossier **EXTRAIT**, donc inscriptible (`squashfs-root/usr`,
`dist/NeoST.app/Contents`, `_check/*/`). Le support de livraison réel — image montée en
lecture seule — n'est jamais exercé. Dans un dossier extrait, le chemin portable est
inscriptible et la règle A36 n'atteint jamais sa branche « config utilisateur » : le
défaut ne pouvait pas apparaître.

**Le défaut de configuration touche donc 5 paquets sur 8, pas seulement macOS.** Aucun
paquet ne livre de `neost.cfg` portable (vérifié sur les huit) : tout support monté en
lecture seule bascule sur `~/.config/neost/`, ce qui vise le `.dmg` **et les quatre
AppImage**. Le `.zip` Windows y échappe (extrait dans un dossier inscriptible), l'APK a
sa propre logique, le web n'est pas concerné. ⚠ Pour le `.dmg` c'est MESURÉ ; pour les
AppImage c'est DÉDUIT de trois faits vérifiés (aucun `neost.cfg` livré, montage en
lecture seule, règle A36) — pas exécuté, faute de machine Linux, et la distinction est
maintenue exprès.

**Garde posée, vérifiée par mutation.** `tests/selftest_logic.cpp` teste maintenant
l'ÉCRITURE et non plus seulement la règle : écrire une config dans un dossier qui
n'existe pas doit réussir ET créer le dossier. `src/gui/AppConfig.cpp` entre dans
`neost-selftest` pour cela (aucune dépendance ImGui). Correctif retiré → **2 FAIL**,
correctif remis → 337 OK. Le test d'A36 était PUR : il prouvait quel chemin la règle
retient, jamais qu'on sache y écrire — le défaut s'est logé dans l'intervalle exact
entre les deux, et c'est la leçon à retenir de tout ce chantier.
📌 Piège d'outillage rencontré en chemin : restaurer le fichier muté avec `cp` dans la
MÊME SECONDE que la compilation de son `.o` laisse `make` croire l'objet à jour — la
suite rougissait avec le correctif pourtant en place. `touch` puis reconstruction.

**Quatrième temps — la conformité est éteinte EN LIGNE, et le jour J est préparé.**
Le job `pages` se déclenche au push sur `main`, et son artefact est le dossier `wasm/` :
pousser le correctif a donc suffi à redéployer le site. **Vérifié en production** —
`GPL-3.0.txt`, `GPL-2.0.txt` et `THIRD-PARTY.txt` répondent HTTP 200 sur
`habib256.github.io/neost`. La non-conformité GPL est close sans qu'aucun tag soit posé.
La garde ajoutée au job `wasm` est passée verte à sa première exécution.

`docs/RELEASE.md` reçoit un § **JOUR J** : le numéro dû (`0.6.1` — PATCH, parce que la
0.6 publiée ne conserve aucun réglage sur 5 paquets sur 8), ce que la release emporte,
les trois pas qu'on saute (bump des trois endroits, le cache `NEOST_VERSION_STR`, le
palier `full`), les **notes de release EN ANGLAIS déjà rédigées** — le job `publish` les
génère sinon à partir de titres de commits français — et l'action d'après-publication qui
solde le dernier blocage (supprimer 0.5.2/0.5.4). Rien n'est bumpé d'avance **à dessein** :
`check_release.py` exige que les trois numéros soient égaux, et une version bumpée sans
tag ferait mentir le dépôt aussi sûrement que l'inverse.

**Cinquième temps — le trou structurel est fermé, pas seulement constaté.** Les quatre
phases de `smoke_package.sh` ÉCRIVENT dans le paquet (captures, journaux, dossier
GEMDOS) : elles exigent donc un dossier EXTRAIT, et c'est ce qui les rendait
structurellement incapables d'exercer le support que l'utilisateur lance vraiment — un
`.dmg` monté ou un AppImage, tous deux en LECTURE SEULE. Le défaut de configuration est
passé exactement par là.

**5ᵉ phase** : retirer le droit d'écriture sur le paquet, relancer le binaire livré
depuis un répertoire EXTÉRIEUR, vérifier qu'il démarre en rendant une image et qu'il n'a
**rien déposé** dans son paquet. Elle ne prouve pas le réglage (l'écriture de `neost.cfg`
est le fait du binaire GUI, qu'aucune CI ne peut lancer faute d'affichage — c'est la
garde unitaire de `neost-selftest`) mais la PROPRIÉTÉ dont ce défaut n'était qu'un cas.
Mutation : une écriture délibérée est détectée ; les permissions sont rendues même sur
échec (le `trap` les rétablit avant de nettoyer, sinon un arbre non supprimable resterait).
⚠ `chmod` et non un vrai montage — celui-ci dépend de la plateforme (hdiutil, FUSE, root)
quand le retrait du droit d'écriture capture la même propriété partout ; sous MSYS2 il est
largement inopérant, la phase y est faible et le script le DIT.

Éprouvée sur le paquet **fabriqué par la CI** (artefact `NeoST-macOS-universal2-dmg` du
run de `a500bd6`, monté puis recopié) : les cinq phases passent.
📌 Trouvé en la posant : `neost-headless` résout `disks/diskA.st` par rapport au
RÉPERTOIRE COURANT, là où le frontend GUI résout par rapport au binaire — lancé
d'ailleurs, le headless perd la disquette embarquée et la capture sort uniforme. Sans
conséquence (c'est l'outil de débogage, lancé depuis le dépôt), mais la phase passe ses
chemins en absolu, et le piège est écrit là où quelqu'un le relira.

## Plus aucune référence oracle non re-dérivable : `spec512_bands` s'ancre sur la VBL par `stop` (2026-09-01)

Dernière astérisque d'A11. Hatari ne se reproduisait pas lui-même sur ce disque (deux runs →
deux jeux de phases disjoints, 2 460 px de la référence au mieux) : son RNG de boot décale le
démarrage du programme de quelques cycles, et une resynchro par scrutation de `$FF8207` ne se
recale qu'à ~20 cycles près — sur l'étalon dont TOUT l'objet est de rendre un cycle CPU
visible. `oracle_scan` n'y pouvait rien : le décalage est sous-trame.

Le remède est dans le **programme**, pas dans l'outillage. La séquence vit dans le handler de
VBL et le programme attend en **`stop #$2300`** : une interruption prise depuis STOP a une
latence FIXE, là où un `bra.s` d'attente (l'ancrage de `freq_switch`, suffisant là-bas) la
prend à une frontière d'instruction, jusqu'à 10 cycles de jitter. Mesuré : 2 runs Hatari →
**mêmes 3 phases** ; NeoST rend **ces mêmes 3 images** (matrice 3×3 des phases, zéro sur la
diagonale). L'image cycle sur 5 trames — le handler démarre 4 cycles plus tard à chaque trame
(première écriture à cyc 138, 142, 146…) — **identiquement chez Hatari** : c'est le programme,
et `oracle_scan` retient la trame identique, qui existe dans toute fenêtre de 5.

Référence régénérée (trame nominale, décalage 0, une image comparée) ; run frais identique.
L'exclusion `oracle_check: false` est levée, le mécanisme reste. Le job hebdomadaire confronte
désormais **7 étalons** — les 7 oracles sur ROM libre, sans exception. Règle retenue pour tout
étalon généré dont la mesure est sous-ligne : ancrer sur la VBL par `stop`, jamais sur le
compteur vidéo.

## V3 est CLOS : l'attribution de ligne à la grille réelle devient le défaut — et le verrou ne faisait rien (2026-09-01)

Dernier reste du front « précision cycle » à valeur élevée. Le verrou `NEOST_LINELEN_ATTR`
(attribution des écritures freq/res à la grille RÉELLE des débuts de ligne, contre la grille
fixe `frameCycle/512`) était OFF depuis toujours, faute d'étalon qui bascule la fréquence en
cours de trame — Closure avait été essayée et réfutée le 2026-08-30. « Le palier `full` est
vert avec le verrou armé » se lisait comme une non-régression ; c'était pire que ça.

**L'exhibiteur.** `tools/make_freqswitch_test.py` → étalon **`freq_switch`** (généré, ROM
libre). Le levier est l'accumulation : une ligne 60 Hz fait 508 cycles, pas 512, donc la
grille réelle dérive de 4 cycles par ligne — et il faut **plus d'une ligne entière** de
dérive (≥ 128 lignes 60 Hz) pour que les deux modèles désignent des lignes différentes. C'est
exactement pourquoi aucune démo du dépôt ne l'exhibait : leurs bascules sont ponctuelles.
Structure : plage de 140 lignes 60 Hz (560 cycles de dérive) puis 8 bascules 50/60
alternées, écran rempli d'index 1 pour que la largeur affichée de chaque ligne se lise.
⚠ **Ancré sur la VBL**, et c'est la leçon de la première version : se resynchroniser sur le
compteur vidéo — le bon choix de `make_spec512_test.py` — est faux ici, le compteur dépendant
de la fréquence, donc de ce qu'on mesure ; dès 2 lignes de 60 Hz l'image changeait d'une
trame à l'autre. Dans le handler de VBL (MFP coupé, IPL 3), 12 trames NeoST et 17 trames
Hatari sortent md5-identiques.

**Ce que l'exhibiteur a montré d'abord : le verrou n'avait aucun effet.** Image bit-identique
dans les deux positions. L'instrumentation `[varline]` annonçait pourtant 17 écritures sur 18
mal attribuées, jusqu'à 2 lignes d'écart. Un second diagnostic (`[attr]`, gardé) a dit lequel
des deux modèles tournait vraiment : sous le verrou, seul le **cycle** bougeait (+4), jamais
la **ligne**. Cause : dans le replay la longueur de ligne retombait à `cpl` à CHAQUE ligne et
n'était corrigée que par un `Freq_match` tombant SUR la ligne ; 140 lignes de 60 Hz sans
écriture restaient à 512, la grille réelle ne dérivait jamais. Le verrou ne pouvait pas
produire l'effet pour lequel il existe.

**Le correctif**, une ligne de principe : la longueur de base de chaque ligne est celle
qu'implique l'état freq/res au début de ligne (`glueLineLenFor`, ≙ `Video_StartHBL` →
`nCyclesPerLine` chez Hatari), les `Freq_match` la raffinent ensuite comme avant — dans le
replay comme sur le chemin live.

**La preuve, au niveau de la Glue.** Trace `--trace video_sync` d'Hatari contre `[attr]` :
même cycle-trame des deux côtés (`video_cyc_w` = `fc` exactement), et avec le correctif
**les 18 écritures de la trame sont attribuées à la même ligne ET au même cycle** —
`169/336 174/268 179/224 184/156 189/112 194/44 199/4 203/444 208/400 213/332 218/288
223/220 228/176 233/108 238/64 242/508 247/468`. La grille fixe en manque 17 sur 18, d'une
à deux lignes. Les frontières de blocs visibles suivent (`135,136,140,141,145,146,150,151,
155,156`, identiques).

⚠ **Le pixel n'était pas juge.** 56 % d'écart image contre l'oracle — parce qu'Hatari rend
les lignes 60 Hz d'une trame 50 Hz avec l'artefact de recopie « left+2 » tranché en A40 :
toute la ligne décode à l'index 8 ($333), décalée. Mesuré ligne à ligne : y=50 (plage 60 Hz)
NeoST `44..363` index 1, Hatari `32..351` index 8 ; y=137 (ligne 50 Hz) **identiques**. D'où
`ref_kind: snapshot`, la preuve oracle consignée au `ref_note`.

**Promotion.** Défaut ON ; le palier pixel entier reste à **0 px** avec le canal armé — rien
d'existant ne bouge, seul l'exhibiteur le voit, c'est ce qu'il fallait. `NEOST_LINELEN_ATTR=0`
fait rougir `freq_switch` à **16 408 px** (garde vérifiée par mutation) ; `glue_selftest_attr`
garde désormais la position DÉSARMÉE pour que l'A/B reste exécutable. `env_locks.json`,
bandeaux de `Shifter.cpp`/`.hpp`, `docs/HATARI_DIVERGENCES.md`, `docs/CYCLE_ACCURACY.md` à jour.

## L'oracle se pilote au clavier à la VBL près, perd sa LED, et ffmpeg cesse de renuméroter ses trames (2026-09-01)

Trois verrous d'outillage réglés à la source, après la première exécution d'A11 qui avait
laissé `nocooper` « non re-dérivable » faute de pouvoir tenir la touche espace.

- **Appui touche, précis à la VBL.** `HATARI_ORACLE_KEYS="down:up:scancode …"` (manifeste :
  `oracle_keys: [[900, 960, 57]]`) : `--parse` pose un point d'arrêt `b VBL = N :once` par
  événement, Hatari s'y GÈLE sur son prompt (stdin = une fifo tenue par le script), on pousse
  `hatari-event keydown|keyup` dans la fifo de contrôle puis `c` — appliqué à la VBL N+1,
  déterministe, sans aucune attente horloge. Tout prompt reçoit un `c`, même inattendu.
  **Deux affirmations de la doc étaient fausses** et coûtaient cher : `--cmd-fifo` ne
  désactive PAS le fast-forward (562,9 VBL/s avec, 565,0 sans) — la chorégraphie « temps réel
  + sleep 30 s » n'avait pas lieu d'être — et la fifo ne bloque pas à l'ouverture
  (`O_RDONLY|O_NONBLOCK`, `control.c:553`) ; en revanche un writer laissé connecté fait
  rendre `EAGAIN` à chaque trame, que Hatari journalise : le writer n'est ouvert que le temps
  d'un message. Vérifié sur No Cooper : trame 6929 retenue (+139), **identique à la capture
  NeoST et à la référence commise** — l'oracle posé à la main est re-dérivable.
- **La LED disquette n'est pas une fatalité** : `--drive-led off`. Zone vérifiée entièrement
  noire. Le masque `buffer_noled` reste tant que des références commises la portent.
- **ffmpeg renumérotait les trames.** Hatari encode chaque image PNG en `pal8` dès qu'elle
  tient en 256 couleurs, en `rgb24` sinon — le format change sans arrêt (nocooper : 0-2 pal8,
  3 rgb24, 4-408 pal8, 409 rgb24…). À chaque changement ffmpeg reconstruit son graphe et le
  `n` de `select` **repart de zéro** : « frame 1000 hors de l'AVI » alors que `ffprobe` en
  compte 1 100 — et une fenêtre de scan silencieusement décalée. `-reinit_filter 0`
  (+ `-pix_fmt rgb24`) sur les deux extractions de `hatari_oracle.sh`.
- Bonus : `HATARI_ORACLE_KEEP=1` conserve AVI et journal d'un run (c'est ce qui a permis de
  voir les deux points précédents), et le script est écrit pour le bash 3.2 de macOS (pas de
  `mapfile`), celui que `run_etalons.py` invoque.

Périmètre du job hebdomadaire : **6 étalons** confrontés en CI (nocooper compris) ; seul
`spec512_bands` reste déclaré non re-dérivable, pour sa raison propre.

## A11 — l'oracle Hatari entre en CI, et sa première exécution trouve deux références non re-dérivables (2026-09-01)

Dernière boucle de validation entièrement manuelle du projet. `tests.yml` compare depuis
août la capture NeoST aux références commises à chaque push — une non-régression : « NeoST
rend encore ce qu'il rendait ». Il ne disait rien de la question qui FONDE ces références :
Hatari, la source de vérité matérielle, rend-il toujours la même chose ? Cette moitié-là ne
tournait que sous les doigts du mainteneur.

**Le mode manquait, pas seulement le job.** `run_etalons.py --oracle` **écrase** les
références : il sert à en POSER une, jamais à en contrôler une — il efface la preuve qu'il
devrait comparer. D'où `--oracle-check`, non destructif : il rejoue Hatari au pin, retient
(via `oracle_scan`) l'image identique à la capture NeoST du jour, puis la CONFRONTE à la
référence commise sans jamais écrire dans `tests/reference/`. Quand il passe, trois choses
sont vraies d'un coup :

```
NeoST  ==  référence commise  ==  Hatari (aujourd'hui, au pin)
```

Il attrape ce qu'aucun autre palier ne voit : une référence régénérée contre un oracle non
épinglé, un pin déplacé sans repose des références, un ffmpeg dont le décodage bouge.
**Garde vérifiée par mutation** — 1 pixel modifié dans une référence commise suffit à la
faire échouer (le contraire d'un garde-fou qui ne mord jamais, cf. A38).

`.github/workflows/oracle.yml` : **hebdomadaire** (lundi 04:00 UTC) plus
`workflow_dispatch`, jamais au push — bâtir Hatari et balayer les fenêtres coûte des
minutes, et ce qui dérive ici dérive en semaines. Le pin est **lu** dans
`tools/setup_hatari.sh` pour former la clé de cache, plutôt que recopié : une clé qui
répète une constante finit par mentir sur ce qu'elle met en cache, et un cache survivant à
un changement de pin ferait tourner l'ANCIEN Hatari en prétendant valider le nouveau.
Mesuré : **5 min 31** pour les 8 étalons re-dérivables sur un poste de dev au repos, et
**13 min 43** pour la première exécution réelle du job (runner ubuntu-24.04, cache froid —
build complet d'Hatari inclus), verte : 5 étalons confrontés, les 3 `spectrum512_diapo*`
sautés faute de ROM Atari et recensés comme tels. Le job n'est donc pas seulement écrit,
il a tourné.

### Ce que la première exécution a trouvé

Deux des dix références `ref_kind: oracle` ne sont **pas re-dérivables**, chacune pour une
raison propre — et ce n'était écrit nulle part.

- **`nocooper`** : son oracle exige une touche **TENUE** (espace, vbl ~900) que
  `hatari_oracle.sh` ne sait pas injecter — il faut `--cmd-fifo` + `keydown`/`keyup`. La
  référence avait donc été posée à la main. Second obstacle du même ordre, resté invisible
  jusqu'ici : le run NeoST utilise `--fastfdc` alors qu'`oracle_fastfdc` est absent de
  l'entrée, donc les deux timelines ne sont même pas alignées.
- **`spec512_bands`** : **Hatari ne se reproduit pas lui-même** dessus. Deux runs de la
  MÊME ligne de commande donnent deux jeux de phases **entièrement disjoints** (md5 des
  trames de la fenêtre : aucun commun). C'est la rançon exacte de ce que cet étalon
  mesure — la position horizontale des bascules de palette dépend du cycle de l'écriture,
  c'est le seul endroit du rendu où un cycle de CPU se voit à l'œil — et le tirage RNG de
  la position angulaire de la disquette décale le démarrage du programme.
  **`oracle_scan` n'y peut rien** : il corrige une renumérotation de TRAMES, or le décalage
  est **sous-trame**. Les 4 phases présentes dans une fenêtre de 181 trames diffèrent
  toutes de la référence — la moins pire à **2 460 px**, avec des couleurs permutées
  circulairement sur 4 px au bord de chaque bande, signature d'un décalage de 4 cycles.

⚠ **Aucune des deux n'est un bug de NeoST, et aucune n'est une mauvaise référence.** Celle
de `spec512_bands` est un authentique oracle Hatari — elle porte sa LED disquette — et
prouve ce qu'elle prouve : un jour, sur un run donné, NeoST a rendu cette image AU PIXEL
comme Hatari. Elle n'est simplement pas RE-DÉRIVABLE. Une référence peut donc être **vraie
et non reproductible**, cas qui n'était pas prévu par l'outillage.

Le manifeste le déclare désormais : `oracle_check: false` + `oracle_check_note`, où **la
raison est OBLIGATOIRE** — sans elle `--oracle-check` refuse de tourner plutôt que
d'exclure en silence. Le journal les nomme à chaque exécution, comme les SKIP de ROM
propriétaire. Périmètre réel en CI : **5 étalons** (blitter_hog, cuddly_demos, scroll_8264,
scroll_8265, trace_odd) — les 3 `spectrum512_diapo*` dépendent d'une ROM Atari que le dépôt
ne porte plus depuis la purge. Le décompte est imprimé : un « TOUS OK » sur deux étalons au
lieu de cinq doit se voir.

## A41 — les 27 px de Closure sont à Hatari : il colore avec des écritures pas encore faites (2026-09-01)

Dernier écart oracle inexpliqué du dépôt, résidu d'A40 : l'étalon `closure` tombait à
**27 px / 114816** contre l'oracle Hatari, **tous sur la ligne 0** du buffer — la première
ligne affichée (`sl=34`), celle qu'ouvre le retrait de bordure haute — chaque pixel fautif
portant une couleur *voisine d'un cran* ($210 côté NeoST là où Hatari donne $310).

**Le verdict : NeoST est fidèle, Hatari anticipe.** Et il est MESURÉ, pas déduit. En
instrumentant `Spec512_StartFrame` dans l'arbre `extern/hatari` (gitignoré ; sonde révoquée
et binaire rebâti au pin `f0736b2` juste après), la palette d'**amorce** qu'Hatari emploie
pour cette ligne sort telle quelle :

```
[probe] vover=3 nStartHBL=34 STScreenStartHorizLine=0 OVERSCAN_TOP=29 nScanLine=29 skip=5
        seed=000 100 200 210 310 310 320 420 430 531 442 541 552 652 652 763
```

Or ces seize valeurs sont **exactement**, registre par registre, le bloc que la démo écrit
aux **cycles 438-508 de cette même ligne 34** — relevé indépendamment côté NeoST par
`NEOST_SPEC512_TRACE`. Hatari colore donc les pixels des cycles 56-144 avec des écritures
qui n'auront lieu que 380 cycles plus tard, sur la même ligne. Aucun faisceau ne fait ça.

**La cause, dans le code d'Hatari** : `Spec512_StartFrame` (`spec512.c:233`) fait
`nScanLine += OVERSCAN_TOP` dès que `V_OVERSCAN_NO_TOP` est armé — les `CyclePalettes` des
scanlines **0 à 28 ne sont jamais rejouées**. Le bloc d'initialisation de la démo vit
lignes 1-2, en plein dedans : il est perdu, et l'amorce retombe sur `pHBLPalettes[]`, que le
`pHBLPalettes -= OVERSCAN_TOP` de `video.c:3429` — commenté « FIXME useless ? » par Hatari
lui-même — a garni d'écritures postérieures.

**Conséquence actée** : `closure` reste `ref_kind: snapshot` **définitivement**, comme
`overscan_top` (A40). Le passer en `oracle` installerait l'anticipation d'Hatari comme
référence ; la self-capture, elle, garde la non-régression AU PIXEL. L'entrée est écrite au
`ref_note` de l'étalon, aux verdicts (`docs/CASE_STUDIES.md`) et à l'inventaire maître
(`docs/HATARI_DIVERGENCES.md` § *Cas où NeoST améliore Hatari*).

**Le minimum a été CHERCHÉ avant de conclure**, parce qu'un écart de 27 px sur une démo qui
anime sa palette ressemble d'abord à une capture mal alignée : 70 trames NeoST voisines
(10471-10540) comparées à l'oracle commis donnent **27 ou 43 px, jamais 0**. Ce n'est donc
pas un défaut d'alignement, et l'`oracle_scan` de l'étalon n'y peut rien.

📌 **Deux pièges d'outillage payés en route**, à ne pas repayer :
- la palette d'init de cette démo **change à chaque trame** ; une trace `--trace video_color`
  d'Hatari prise sur une AUTRE trame que la capture comparée mène droit à une fausse piste
  (vécu : la première lecture accusait un décalage de ligne inexistant). Armer la trace sur
  la bonne trame : `--parse f` avec `b VBL = N :once :file arm.txt`, `arm.txt` contenant
  `trace video_color` puis `cont` ;
- l'égalité de breakcond s'écrit **`=`**, pas `==` : le parseur scinde `==` en `= =` et
  refuse la condition (« Unrecognized number prefix in '=' »).

Itération ramenée de 20 s à 1 s par `--save-state` à la trame 10499 puis `--load-state
… --frames 1` : capture **identique au pixel** à la référence, donc utilisable comme banc.

## Le MIDI Windows rejoint macOS et Linux — backend winmm (2026-08-31)

Suite directe du chantier ci-dessous, sur la dernière plateforme en retard. L'inventaire
d'entrée était net : sous Windows, `MidiOutHost::destinations()` et
`MidiInHost::sources()` rendaient des listes **vides**, `available()` disait faux, et la
page MIDI cachait sa moitié « appareils matériels » — gardée, à tort, par
`portAvailable()` (le port *virtuel*) plutôt que par la disponibilité des
**destinations**. Un Windows avec un clavier maître branché voyait donc une page vide.

**Livré** : backend **winmm** dans les deux ponts existants (une troisième branche `#if`,
pas un fichier parallèle — la logique partagée reste partagée) et
`audio/MidiWinmm.hpp` pour ce qui est propre à Windows.

- **Destinations** : `midiOutOpen` par appareil, `midiOutShortMsg` pour les messages
  courts, `midiOutLongMsg` pour le SysEx avec récolte différée des tampons (un dump de
  4 Ko met plus d'une seconde à sortir à 31 250 bauds : ni attendre, ni libérer).
- **Sources** : `midiInOpen` en **`CALLBACK_THREAD`**, pas `CALLBACK_FUNCTION`. La
  documentation winmm interdit d'appeler autre chose qu'une courte liste blanche depuis
  un callback, et `midiInAddBuffer` — indispensable pour ré-armer les tampons SysEx —
  n'en fait pas partie. Une file de messages sur notre thread rend l'opération légale et
  réutilise le thread déjà présent pour ALSA.
- **Identifiant unique, que Windows était censé ne pas avoir** : `MIDIOUTCAPS` ne donne
  qu'un nom tronqué à 31 caractères et, pour le Circuit Tracks testé, `mid=65535
  pid=65535` — rien. `midiOutMessage(DRV_QUERYDEVICEINTERFACE)` rend en revanche le
  chemin d'interface (`\\?\usb#vid_1235&pid_0139&mi_00#6&33d600c&0&0000#{...}`), dont la
  partie médiane est le chemin **physique** : deux appareils du même modèle sont donc
  discernables. Windows rejoint CoreMIDI ; ALSA reste la seule plateforme sans
  identifiant stable. Corollaire assumé : changer de prise USB change l'identifiant, et
  le repli par nom le signale au journal.
- **`timeBeginPeriod(1)` tant qu'une sortie est ouverte.** Sans lui, Windows réveille le
  thread de livraison par tranches de ~15,6 ms et tout l'horodatage (ancre + `lead`)
  serait arrondi à cette tranche. Mesuré A/B, 40 échéances de 50 ms vers le Circuit
  Tracks : **σ 0,30 ms / écart max 0,90 ms** avec, contre **σ 5,34 ms / 13,10 ms** sans.
- **`shortMessageLength()`** (`MidiMessageParser.hpp`) : `MM_MIM_DATA` empaquette un
  message dans un mot sans dire combien d'octets comptent. Pousser les trois aveuglément
  ajouterait une donnée fantôme après un Program Change, que le running status avale en
  silence. Fonction PURE, donc 14 cas dans `neost-selftest` — exécutés aussi sur macOS et
  Linux, où aucun clavier n'est branché en CI.
- **La banque GM entre dans les paquets.** `packaging/stage_free_data.sh` n'embarquait
  pas `roms/gm/` : la case « Built-in General MIDI synth » du chantier précédent était
  donc morte dans **tous** les binaires livrés (TinySoundFont ne joue rien sans banque,
  et aucun Windows ne fournit de `.sf2`). `packaging/licenses/THIRD-PARTY.txt` annonçait
  pourtant le fichier — l'annonce est redevenue vraie.

**Ce qui reste hors de portée** : le port virtuel « NeoST MIDI OUT ». Windows n'a aucune
API pour créer un port que les autres applications voient (ni winmm, ni WinRT MIDI 1.0) ;
il y faut un pilote tiers (loopMIDI), dont le port apparaît ensuite comme un appareil
ordinaire, en destination **et** en source. La page MIDI le dit au lieu de laisser une
case grisée sans explication. Windows MIDI Services (MIDI 2.0, Win11) le ferait
nativement, mais s'installe à part et exigerait une pile WinRT/COM que le zip autonome
ne peut pas emporter.

**Vérifié sur matériel réel** (Windows 11, Novation Circuit Tracks en USB, llvm-mingw) :
`--midi-list` énumère l'appareil avec son identifiant ; `--midi-in-device` fait entrer
35 octets d'horloge dans l'ACIA en 1,3 s (0 perdu) ; le GUI ouvre destination + source
canalisée d'un coup, apprend l'identifiant et livre 0 octet en retard ; gamme, SysEx,
running status et panique éprouvés sur l'appareil. Paliers `fast` **et `full` verts sous
Windows** — une première, qui a demandé quatre correctifs d'outillage sans rapport avec
le MIDI (voir ci-dessous).

### Quatre bugs d'outillage que Windows a mis au jour

Aucun n'est lié au MIDI ; tous empêchaient de valider quoi que ce soit sur cette
plateforme, ce qui explique qu'ils aient survécu — la suite de tests n'y avait jamais
tourné, et personne ne pouvait le voir depuis un Mac ou une CI Linux.

- **`tools/compare_screenshot.py`** : `mkstemp` rend un descripteur **ouvert**, laissé
  tel quel avant de faire écrire ffmpeg dans le fichier. Sous POSIX, simple fuite de
  descripteur (une par comparaison) ; sous Windows le fichier est verrouillé, ffmpeg
  échoue — et le `unlink` du `finally` échouait à son tour, **masquant** le vrai
  message (« ffmpeg est requis »). Toute référence PNG (les captures d'oracle Hatari)
  était incomparable.
- **`tools/check_doc_anchors.py`** : le motif `[A-Za-z_][A-Za-z0-9_]*` était passé à
  `grep` en argument. Sous git-bash/MSYS, `grep.exe` re-découpe la ligne de commande et
  prend ce motif pour un **glob** qu'il expanse. Résultat : « 197 ancres mortes » sur des
  symboles bien vivants (`Machine::runFrame`…). Le scan des lexèmes se fait désormais en
  Python — plus de dépendance à grep, et 0 ancre morte.
- **Le suffixe `.exe`** : les huit outils qui lancent un binaire le cherchaient sous son
  nom POSIX, donc la suite se déclarait « non bâtie » alors que tout était compilé. Le
  contournement évident (copier `neost.exe` en `neost`) est un PIÈGE — la copie ne suit
  pas les rebuilds et on teste alors un binaire périmé, sans rien qui le dise.
- **`gui_available()`** sautait le boot GUI dès que `DISPLAY`/`WAYLAND_DISPLAY`
  manquaient — ce qui ne veut rien dire hors X11/Wayland. macOS était déjà excepté,
  Windows non : la cible `neost` n'était donc lancée par la suite sur **aucune** des
  deux plateformes non-Linux.

Reste connu, non corrigé (hors sujet de ce chantier) : les outils lisent leurs fichiers
sans `encoding="utf-8"`, or le cp1252 de Windows échoue sur `etalons.json` — d'où le
`PYTHONUTF8=1` nécessaire devant `run_all.py` sur cette plateforme.

## Le synthé GM intégré cesse d'être un privilège macOS (2026-08-30)

Objectif : **le support MIDI doit être aussi complet sous Linux que sous macOS.**
L'inventaire donnait un seul manque : la case « Built-in General MIDI synth » était
morte hors macOS — le commentaire de `MidiOutHost.hpp` affirmait « aucun équivalent
gratuit et embarquable sous Linux/Windows » et renvoyait l'utilisateur vers un
FluidSynth à installer et câbler lui-même. Tout le reste (port virtuel ALSA,
destinations et sources matérielles par nom, MT-32/Munt, profils, panique, avance
réglable) était déjà à parité.

**Livré** : `audio/GmSynth` — TinySoundFont (**vendorisé** dans `extern/tsf`, MIT, un
seul header, même politique que Moira/Munt) + banque **TimGM6mb livrée** dans
`roms/gm/` (5,7 Mo, GPL-2, archive Debian `timgm6mb-soundfont` — l'équivalent de la
banque GS qu'Apple embarque dans l'OS). `gm_soundfont=` accepte un `.sf2` ou un
dossier, avec repli sur `/usr/share/soundfonts` et `/usr/share/sounds/sf2`.

Le rendu suit le schéma Mt32Synth, PAS le chemin temps réel du DLSMusicDevice :
octets datés du cycle 68000 → messages (parseur partagé `MidiMessageParser`) →
découpe du bloc de la trame aux dates des messages → mix dans `Audio::produceFrame`.
Précision à l'échantillon, zéro gigue d'hôte — sur ce point Linux sort MIEUX loti que
macOS. Fader « Built-in GM synth » sur la page Sound (`mix_gm=`), statut de banque sur
la page MIDI, panique câblée, fermeture/réouverture sur changement de fréquence du
périphérique et chargement de profil.

Au passage : le libellé du menu Machine disait « CoreMIDI port » même sous Linux —
il suit désormais `portKindName()` (ALSA). Et Windows, sans rien lui écrire de
spécifique, gagne le même synthé (TSF est portable ; winmm reste le manque pour le
port virtuel et les appareils, cf. `TODO.md`).

**Garde** (`neost-selftest`, palier `fast`) : ouverture de la banque LIVRÉE, « la note
posée à mi-trame ne sonne que la seconde moitié » (datation à l'échantillon), canal 10
percussif, silence après note-off + release, close idempotent. Vérifié sur machine
réelle : le Circuit Tracks branché est énuméré (`--midi-list`) et visible en
destination comme en source.

## La note de la page MIDI manquait : la police de TEXTE lui volait son codepoint (2026-08-30)

Rapport : « l'icône dans la fenêtre config du MIDI devrait être une note, elle n'apparaît
pas ». Elle était pourtant bien déclarée (`ICON_FA_MUSIC`, U+F001) et bien présente dans
`fonts/fa-solid-900.ttf` — vérifié en lisant la table `cmap` du fichier.

**La cause est une collision de codepoints.** `fonts/DejaVuSans.ttf`, notre police de
TEXTE, occupe une partie de la zone à usage privé : **U+F000-F003** (ses ligatures
ff/fi/fl/ffi héritées) et U+F400+ — exactement là où vit Font Awesome. Or ImGui parcourt
ses sources **dans l'ordre** et retient la **première** qui sait fournir le codepoint
(`ImFontBaked_BuildLoadGlyph`, imgui_draw.cpp:4590-4610) : DejaVu, chargée en premier,
gagnait. La page MIDI affichait donc une ligature « fi » de 15 px là où on attendait une
note — indiscernable d'une icône absente.

Une SEULE des 35 macros est concernée (comptées : `ICON_FA_MUSIC` est la seule dont le
codepoint tombe dans le domaine PUA de DejaVu), ce qui explique pourquoi rien d'autre ne
manquait et pourquoi le bug a tenu si longtemps.

**Correctif** : `GlyphExcludeRanges = { 0xF000, 0xF8FF, 0 }` sur la police de TEXTE —
le champ existe pour exactement ce cas (« designed to exclude ranges from a font source,
when merging fonts with overlapping glyphs », imgui.h:3728), et
`ImFontAtlasBuildAcceptCodepointForSource` refuse alors ces codepoints à DejaVu, si bien
que la source Font Awesome est consultée. Sans risque collatéral : aucune étiquette de
l'interface n'utilise la PUA hors des macros d'icônes, et ImGui ne fait pas de
substitution de ligatures.

**Garde posé** (`tools/check_icon_glyphs.py`, palier `fast`) : une icône se perd en
silence de deux façons, et le script ferme les deux — (1) codepoint ABSENT de la police
d'icônes (macro tapée à la main, glyphe qui n'existe qu'en variante Regular/Brands), (2)
codepoint revendiqué AUSSI par la police de texte sans `GlyphExcludeRanges`. Il lit les
tables `cmap` des deux `.ttf` et les macros de `UiCommon.hpp`. Vérifié qu'il ROUGIT : en
retirant l'exclusion, il sort 1 en nommant `ICON_FA_MUSIC (U+F001)`.

⚠ **Non vérifié en image**, comme le bandeau de halt : la capture `--shot` du GUI ne
contient que le framebuffer ST, pas l'interface ImGui. Ce qui est prouvé tient au niveau
des polices et du chemin de sélection de glyphe d'ImGui, lu dans son code.

## A42 : un CPU halté le DIT enfin — et la réserve de No Cooper tombe (2026-08-30)

Deux manques, et c'était le même : NeoST ne journalisait pas l'ADRESSE d'une faute de
bus. Faute de quoi (1) le GUI ne pouvait rien montrer d'un CPU halté, et (2) la
comparaison à l'oracle sur No Cooper s'arrêtait au « même mécanisme », sans pouvoir dire
« même instruction ».

**Le cœur retient et nomme la faute.** `CpuState` garde désormais adresse, PC (`getPC0`),
sens et validité de la dernière faute de groupe 0 — l'équivalent des
`last_*_for_exception_3` d'Hatari (`newcpu.c:3086`) —, `Cpu68k::lastFault()` les expose,
et le message de halt les affiche. Journal complet de la CHAÎNE de fautes en opt-in
(`NEOST_FAULT_TRACE`, classé « trace » dans `tools/env_locks.json` — le palier `fast`
rougit sinon) : silencieux par
défaut, parce que la détection de machine du TOS fait fauter des dizaines d'accès par
boot — Hatari doit d'ailleurs les filtrer par une liste blanche d'adresses
(`M68000_IsVerboseBusError`, m68000.c:572-621), qu'on préfère ne pas avoir à maintenir.

**La réserve d'hier tombe, et elle tombe à l'instruction près.** Sur No Cooper en
Mega ST :

```
NeoST  : reading at address $00FF820F, PC=$000056
Hatari : Bus Error reading at address $ffff820f, PC=$58 addr_e3=58 op_e3=4228
```

Même adresse (NeoST la masque en 24 bits, Hatari montre le miroir 32 bits), même sens.
Le PC ne diffère que par la convention de prefetch : la mémoire en `$56` porte
`$4228 $FA0F` = `clr.b $FA0F(a0)` (dump `--dump-at`), donc NeoST nomme le DÉBUT de
l'instruction et Hatari son mot d'extension — son `op_e3=4228` le dit lui-même.

**Et la cause remonte au chipset.** `$FF820F` est l'une des deux adresses que le **Ricoh**
du ST simple laisse « void » et que l'**IMP** du Mega ST fait FAUTER (Hatari
`IoMem_FixVoidAccessForST`/`ForMegaST`, ioMem.c:150-195 ; NeoST `Bus.cpp:533-541`, déjà
porté). La démo lit un registre qui répond sur un ST et faute sur un Mega ST : le verdict
« FIDÈLE » n'est donc plus une observation, c'est une chaîne complète.

**Côté interface** (A42, soldé) : la fenêtre CPU affiche « CPU HALTED — double bus/address
error » avec l'adresse fautive et le vecteur, et un **bandeau permanent** au-dessus de
l'écran dit l'état, la faute et la seule sortie (reset) — en borne aussi, où un visiteur
devant un écran figé mérite de savoir que la machine est morte et non lente. Les
registres affichés sous le bandeau sont ceux du GEL, figés.

⚠ **Ce qui n'a PAS été vérifié en image** : le bandeau lui-même. Le pousser jusqu'au halt
demande d'écrire dans le `neost.cfg` de l'utilisateur (le GUI lit sa config à côté de
l'exécutable, pas dans le cwd) — on ne touche pas à un fichier d'état vivant pour faire
une capture. Ce qui est prouvé : l'état halté est atteint et correctement renseigné
(mesuré en headless), et les deux affichages sont gardés par ce même `cpu.halted()`.
Contrôlé au passage, puisque le doute existait : le harnais `--run-frames` du GUI ne
réécrit PAS `neost.cfg` (md5 identique avant/après), conformément à ce que promet A9a.

## No Cooper « plante » en Mega ST : halt CPU, et Hatari halte pareil (2026-08-30)

Rapport GUI : « No Cooper plante après la page 1, puis la page 2 avec la musique, puis
Return Return ». La méthode a payé dans l'ordre exact où elle est écrite — lire
`neost.cfg` AVANT de soupçonner une régression : `machine=megast`.

Ce n'est pas une image fausse, c'est un **halt** : dès que la démo prend la touche qui
fait avancer la partie, le 68000 part en **double faute de bus** (exception vecteur 2) et
la machine gèle — musique comprise. Le profil machine seul est en cause, pas la ROM :
matrice `{megast, st}` × `{tos104fr, etos192fr, tos102uk}`, halt sur les trois `megast`,
aucun sur les trois `st`. Sans appui touche, `megast` tient 9 000 trames.

**Oracle** : Hatari en `--machine megast`, même ROM, même disquette, espace injecté par
`--cmd-fifo` — `Bus Error reading at address $ffff820f, PC=$58` puis
`Detected double bus/address error => CPU halted!`. **FIDÈLE**, donc : No Cooper ne tourne
pas sur un Mega ST, chez l'oracle comme chez nous. ⚠ Ce qui est apparié : le MÉCANISME et
la dépendance à la configuration, PAS l'instruction fautive.

⚠ **Le piège qui a failli produire un faux verdict** : la démo n'accepte l'espace qu'à
partir de son écran « PRESS SPACE TO GO ON » (~VBL 2800 sous `tos104fr`). La première
injection, calée sur la trame 900 de l'étalon, tombait pendant le chargement — Hatari
restait sagement sur son titre et « ne plantait pas ». Conclure là aurait donné
« divergence NeoST » à l'envers. Recaler l'injection APRÈS l'écran d'attente, puis
conclure.

Vérifié au passage que le correctif A40 du même jour n'y est pour rien : le binaire
reconstruit depuis `4c38cc0` halte à l'identique (et le correctif n'écrit que dans le
tampon de pixels). La note du 2026-08-01 de `docs/TEST_SOFTWARE.md` — « No Cooper,
Cuddly Demos, Enchanted Land et Lethal Xcess "en panne" tournaient en `machine=megast` »
— passe donc de constat à **mécanisme prouvé et confronté à l'oracle**.

⚠ **Manque d'ergonomie repéré, NON corrigé** : le GUI ne dit RIEN quand le CPU halte. Le
drapeau vit dans `Cpu68k.cpp` et aucun code d'interface ne le lit — l'utilisateur voit un
gel, là où le headless écrit « 68000 halted: double bus/address error ». C'est ce qui
transforme un diagnostic d'une minute en « ça plante ».

## A40 est FERMÉ : les 4 px de Closure étaient à nous, les 144 px d'`overscan_top` sont à Hatari (2026-08-30)

Le couple de diagnostic annoncé la veille (« Closure décalée / Cuddly bit-exacte, même
machine, même ROM ») a été ouvert — et **il a d'abord démoli sa propre prémisse**. Le
masque de bordure PAR LIGNE des deux étalons, relevé à la trame de référence
(`NEOST_RENDER_TRACE`/`NEOST_RENDER_ALL` sur un `--load-state` d'une trame) :

| étalon | lignes de la trame comparée |
|---|---|
| `cuddly_demos` | **200 lignes `bm=000`** — aucune bordure retirée, écran 320×200 standard |
| `overscan_top` | 224 normales + **4 `bm=00a`** (LEFT_PLUS_2\|RIGHT_MINUS_2) + 1 `bm=002` |
| `closure` | **274 lignes `bm=200110`** = LEFT_OFF_MED \| RIGHT_OFF, `sh=0`, 458 px |

`cuddly_demos` **n'ouvre aucune bordure sur l'image mesurée** : elle ne calibrait donc
rien du chemin de retrait gauche, et « calibré à 0 px contre No Cooper et Cuddly »
était infondé une seconde fois, pour un autre étalon que la veille. Le vrai levier
n'était pas le couple, c'était le masque : Closure passe par
`BORDERMASK_LEFT_OFF_MED`, le « remove left + med stab » qu'Hatari nomme
explicitement d'après cette démo (`video.c:3974-3995`).

**La cause, par le calcul PUIS par la mesure.** Hatari ne rend pas ces lignes au
faisceau : il RECOPIE des octets dans un tampon de `SCREENBYTES_LINE` = 208 o
(24 + 160 + 24 = 416 px) en partant de `raster + 2 + VideoOffset` (`video.c:4014`),
PUIS décale tout le tampon de `STF_PixelScroll` (`video.c:4273`). Dans le repère du
buffer NeoST cela donne `s_H(x) = x + 4 + 2·VideoOffset − scrollFinal`, contre
`s_N(x) = x + 4 − shEff + 2·medSrcBytes` chez nous : avec la convention déjà posée
(`medSrcBytes = VideoOffset + 2`), les deux coïncident **si et seulement si
`shEff = 4 + scrollFinal`**. Le stab med vaut donc **−4**, pas −8 : le −8 recopiait le
`STF_PixelScroll` d'Hatari en oubliant que son ancrage de recopie est déjà 4 px à
droite du faisceau. Balayage de contrôle ±6 px × ±3 o autour de l'optimum : **aucun
autre couple ne descend sous 53 %**, l'optimum est isolé.

**Et une seconde maille**, qui restait après le décalage : 8 px par ligne (2 083 px).
Le décalage à gauche fait « entrer » par la droite des pixels sans source, qu'Hatari
laisse à l'index couleur 0 (`video.c:4295`, « entering pixels to the extreme right
should be set to color 0 »). En repère faisceau, cela revient à laisser **les
|scrollFinal| DERNIÈRES COLONNES du buffer** à l'index 0 (`blankTailFrom = W − 8`) —
règle exprimée en colonnes, donc valable que les bordures soient rendues ou non.

**Résultat mesuré, étalon `closure` contre l'oracle Hatari** : **64,08 % → 1,81 %
(shEff) → 0,02 %**, soit **27 px sur 114 816**, et **tous sur la ligne 0** — vérifié
sur trois trames voisines (27 / 43 / 27 px, toutes ligne 0 seule). **Tous les autres
étalons du palier `full` restent à 0 px** — aucune régression —, et la référence de
`closure` est reposée.

**Les 144 px d'`overscan_top` ne sont PAS le même bug — ni un bug de NeoST.** Trace
`--trace video_border_h` d'Hatari sur ce disque : il détecte `left+2 / right-2 60Hz
53<->373` sur ces lignes, **exactement le masque de NeoST**. L'écart est entièrement
dans la RECOPIE d'Hatari : ses 2 octets « left+2 » vont dans les 2 derniers octets de
la bordure gauche du tampon, le reste étant mis à 0 — or 2 octets en basse résolution
ne sont pas 4 pixels, c'est **un mot de plan sur 16 pixels**. Le groupe de 16 px
à cheval sur la limite sort donc avec des plans mixtes. Prédiction faite avant de
regarder : tête = plans 0-2 à zéro + plan 3 à $FFFF → **index 8**, queue (memset
`right-2`) = plans 0-2 à $FFFF + plan 3 à zéro → **index 7**. Palette relevée sur le
run (`--dump-at 380 FF8240 32`) : `palette[8] = $333`, `palette[7] = $555` — et
l'oracle montre bien 16 px de $333 en x=32..47 et 16 px de $555 en x=352..367. Le
compte tombe juste au pixel près : 4 lignes `bm=00a` × 32 px (tête + queue) + 1 ligne
`bm=002` × 16 px (tête seule, pas de `right-2`) = **144**. NeoST, lui, rend les
160 octets décalés de 4 px — ce que fait une ligne dont le DE part 4 cycles plus tôt.
`overscan_top` garde donc `ref_kind: snapshot` **pour de bon** : l'écart est expliqué
à l'index de palette près, il ne se refermera pas, et le figer sur l'oracle
installerait l'artefact d'Hatari comme référence.

⚠ **Ce qui n'a PAS été touché, faute d'exhibiteur mesuré.** La même algèbre dit les
cas de scroll « hardware » 13/9/5/1 décalés de 4 px, et le −4 du retrait gauche
standard décalé de 8. Balayage des masques sur les **10 500 trames** de Closure :
`sh` ne vaut jamais que 0 ou −4, et les masques `LEFT_OFF` n'y apparaissent que
combinés à `BLANK` (lignes rendues à l'index 0, donc aveugles au décalage). Aucun
étalon ne les exerce : on les laisse en l'état, avec l'algèbre écrite dans le code.

## Closure devient un étalon, et A40 change de taille (2026-08-30)

`closure` entre dans `tools/etalons.json` (Sync, écran 153 couleurs, ST 1 Mo,
`etos192fr`, `--frames 10500`). Motif : `docs/HATARI_DIVERGENCES.md` § V3 la donne
comme **seul exhibiteur connu** de l'attribution de ligne, le verrou
`NEOST_LINELEN_ATTR` que le TODO disait impromouvable faute d'étalon l'exerçant.
Déterminisme vérifié AVANT la pose (deux runs à 10500, captures md5-identiques).

**Elle a rapporté quelque chose dès le premier run — mais pas ce qu'on cherchait.**
L'oracle Hatari diverge de **64,15 %**. Trois mesures ont écarté les fausses pistes :
(1) les **palettes sont identiques** — les 153 couleurs de NeoST sont TOUTES chez
Hatari, les 4 exclusives de l'oracle étant sa LED disquette ; (2) un décalage
horizontal de **+4 px ST** ramène l'écart à **3,20 %**, et ±1 px autour le fait
remonter à ~29 % — le décalage vaut donc EXACTEMENT 4 ; (3) refait en **pleine
résolution** (832×552, dx=+8, mêmes 3,20 %) pour écarter tout artefact de
rééchantillonnage. C'est `glue::LEFT_OFF`, le `default: −4` de la table `shEff` —
la signature d'**A40**.

**Ce que ça change pour A40** : ce n'est pas une singularité d'`overscan_top`.
Closure n'a ni retrait de bordure haute ni ligne de transition, et l'écart y est
UNIFORME — aucune des 276 lignes n'est épargnée, là où `overscan_top` ne montrait que
5 lignes. ⚠ Il n'est pour autant PAS universel : **10 étalons restent à 0 px contre
l'oracle**, dont `cuddly_demos` et `nocooper`, deux démos qui ouvrent aussi les bordures
sur la même machine. Le décalage dépend donc de ce que Closure fait et que Cuddly ne
fait pas — et ce couple (même machine, même ROM, l'un décalé de 4 px, l'autre bit-exact
contre Hatari) est le meilleur levier de diagnostic qu'A40 ait jamais eu. Au passage,
le TODO affirmait ce chemin « calibré à 0 px contre No Cooper, **Closure** et Cuddly » :
c'était infondé pour Closure, qui n'étant pas un étalon n'avait jamais été comparée à
l'oracle. Une affirmation de calibration ne vaut que pour ce qui est MESURÉ.

La référence commise est donc une **self-capture** (`ref_kind: snapshot`), pas
l'oracle : y installer l'image d'Hatari aurait figé l'écart au lieu de le signaler.
L'étalon garde la non-régression, la mesure vit dans son `ref_note`, et il repassera
en `oracle` le jour où A40 sera compris.

⚠ **Et un résultat NÉGATIF qu'il serait coûteux de perdre : Closure n'exerce pas V3.**
C'était pourtant sa raison d'être — `HATARI_DIVERGENCES.md` § V3 la donnait comme
« seul exhibiteur connu » de l'attribution de ligne. Mesuré le jour même : le même run
avec `NEOST_LINELEN_ATTR=1` rend une image **bit-identique** (même md5, 0 px). L'écran
153 couleurs ne bascule donc pas la fréquence en cours de trame ; la phrase du § V3
datait du chantier CLOSURE (l'écran noir au boot), pas d'une mesure du verrou. Elle est
corrigée là-bas, et **V3 reste sans exhibiteur mesuré** — il faudra un autre écran de la
démo, ou l'étalon généré. L'étalon garde sa valeur : il a rapporté A40 à la place.

⚠ Deux pièges de méthode évités, tous deux déjà consignés dans ce projet. Le scan
oracle ne contenait que **4 images distinctes sur 801** — signature de l'« AVI figé »
qui avait produit deux faux verdicts en août. Vérification faite : l'AVI est bel et
bien VALIDE, l'écran « photo » est simplement statique pendant des centaines de trames.
Conclure « AVI figé » aurait été un faux verdict symétrique du premier. Et le coût :
cet étalon prend **~20 s**, ce qui en fait le 2ᵉ mur du palier pixel derrière
`nocooper_greetings` (~45 s) — mesuré et dit, comme A38 le demande.

## Le paquet se montre : trois démos embarquées, et le frontend web reprend la main (2026-08-30)

**Les paquets embarquent enfin de quoi se montrer.** `cuddly_demos.msa`,
`nocooper.msa` et `closure.msa` — les trois productions demoscene que la purge a
délibérément gardées — partent désormais dans les paquets bureau
(`stage_free_data.sh`) ET dans le bundle web, +1,9 Mo (bundle à 10,6 Mo, plafond CI
40 Mo). Trois pièges traités au passage : elles sont montées **à plat dans `/disks`**
et non sous `/disks/etalons/`, parce que le sélecteur du shell fait un
`FS.readdir('/disks')` NON RÉCURSIF — dans un sous-dossier elles auraient été
embarquées mais invisibles ; les **6 gardes** « rien d'autre que `diskA.st` » de
`release.yml` les autorisent NOMMÉMENT (jamais un glob : le dépôt contient encore des
images de test générées) ; et `THIRD-PARTY.txt` les DÉCLARE — auteurs, année, statut
d'œuvre librement diffusée, plus une procédure de retrait à la demande des ayants
droit. Rien de tout cela n'était en place : le paquet aurait distribué trois œuvres
sans les nommer.

⚠ **Closure re-vérifiée sous EmuTOS avant de l'embarquer** : son chantier la donne en
« ST + tos102uk + 1 Mo », et ce TOS est purgé. Contrôlée trame par trame sous
`etos192fr` : logo SYNC à 700, grand logo X-DISTING à 1400, 4 couleurs en 640×200 à
3000, 87 couleurs à 7200, **153 couleurs à 10500** — la « photo fée » de la carte du
chantier, au bon endroit. Elle se déroule intégralement.

**Le frontend web reprend la main.** Cinq points, dont deux étaient des bugs :

- 🐞 **Une manette USB TUAIT l'émulateur.** `readStickRaw` appelait
  `glfwGetJoystickHats` sans garde, or le port GLFW d'Emscripten la définit en
  `abort('glfwGetJoystickHats is not implemented')` — un abort du RUNTIME, pas un code
  d'erreur. Le navigateur n'expose une manette qu'après la première pression de
  bouton : le web tournait donc parfaitement jusqu'à ce qu'on TOUCHE une manette.
  Gardé `#ifndef __EMSCRIPTEN__`, et aucun hat n'est perdu — la Gamepad API n'en
  rapporte pas, elle donne le D-pad en boutons 12-15 du « standard mapping », déjà lus
  juste en dessous.
- 🐞 **Le bouton « Reset » ne faisait pas ce que son infobulle annonçait** : elle disait
  « Cold reset of the machine », il appelait `neost_reset` (à chaud, RAM conservée) —
  le TOS gardait son `memvalid` et sautait le boot complet. `Machine::hardReset()`
  existait et était utilisé par le GUI bureau et par Android ; **le web était le seul
  frontend à ne pas l'exposer**. Nouveau `neost_hard_reset()`. Le montage en A: passe
  aussi au boot à froid, comme le montage à chaud du bureau (`reqHardReset`) : une démo
  chargée par-dessus les restes d'une autre pouvait démarrer de travers.
- **Réglages joystick** : le port du joystick clavier était **codé en dur à 1** dans le
  JS alors que le cœur accepte 0, et `neost_set_joy_deadzone` était **exportée mais
  jamais appelée** — 0.30 était en pratique la seule zone morte possible sur le web.
  La page a maintenant son sélecteur de port et son curseur de zone morte, calqués sur
  la page Input du GUI bureau (même plage 0–0.95).
- **Zoom adaptatif ACTIF PAR DÉFAUT.** `g_fullscreen` portait DEUX rôles — le cadrage
  sur la zone active, et « qui possède la taille du canvas » (en plein écran, c'est
  Emscripten). Séparés : `g_autoZoom` (défaut ON, avec son bouton) fait le cadrage,
  `g_fullscreen` garde la question du canvas.
- **« Open a file… » retiré** : le glisser-déposer sur l'écran fait la même chose, et
  `mountLocalFile` reste le chemin commun.

## Deux rouges de CI : le bundle WASM rattrape la purge, un pin qui ne pouvait que casser (2026-08-30)

**Le bundle web embarquait encore les TOS Atari.** Premier run « Artefacts (release) »
mené à son terme après la purge : le job `wasm` meurt sur `file_packager: error:
roms/tos102uk.img does not exist`. Le pas 4 avait inversé le défaut dans
`stage_free_data.sh` et dans les 8 gardes de `release.yml`/`pi-borne.yml` — mais la
liste des `--preload-file` du bundle web vit dans le **CMakeLists**, pas dans le YAML,
et personne ne l'a suivie jusque-là. Elle applique désormais la même règle, sous le
même nom et la même polarité que les paquets bureau : `NEOST_PACKAGE_NO_ATARI_TOS`,
défaut ON, `=0` ré-embarque des copies LOCALES. Testé dans les deux sens ; le bundle
produit ne contient plus que les 4 EmuTOS + `drivesound` + `diskA.st` (8,6 Mo, plafond
CI 40 Mo), vérifié sur le manifeste d'`index.js` et pas seulement sur la taille.
Au passage, une donnée manquante se dit maintenant **au configure** en nommant le
fichier : laissée au `file_packager`, elle sortait au LIEN, noyée dans la ligne `em++`
complète — c'est ce qui rendait l'échec illisible.

**Un SHA256 épinglé contre un tag mouvant casse tout seul.** « Build bionic builder
image » échouait sur `computed checksum did NOT match` pour `linuxdeploy` **sans
qu'une ligne du dépôt ait changé** : le `Dockerfile.bionic` épinglait par SHA256 un
binaire téléchargé depuis le tag `continuous`, que l'amont a republié le 2026-08-01.
Le pin ne protégeait donc de rien — il ne faisait que transformer une mise à jour
amont silencieuse en panne. `linuxdeploy` passe sur le tag **immuable**
`1-alpha-20251107-1` (SHA recalculé). `appimagetool` reste sur `continuous` faute de
mieux, et c'est écrit dans le Dockerfile : AppImageKit est en fin de vie et a RENOMMÉ
les assets de ses releases numérotées en `obsolete-appimagetool-*` ; son `continuous`
ne bouge plus depuis le 2025-07-26 et son SHA a été re-vérifié inchangé.
⚠ `packaging/linux/make_appimage.sh` a le même défaut **en pire** (tag mouvant, aucune
somme de contrôle côté arm64) — cf. § Conformité annexe du `TODO.md`.

Ce qui n'a JAMAIS été touché : le job `linux-bionic` de `release.yml`, qui consomme
l'image déjà poussée, épinglée par digest — d'où un « Build bionic builder image »
rouge pendant que les AppImage sortaient normalement.

**Et un TROISIÈME canal distribuait du propriétaire : GitHub Pages.** Découvert en
vérifiant le site après coup, pas par une garde. `habib256.github.io/neost` servait le
bundle web, et ce bundle embarquait `tos102uk.img` + `tos162uk.img` depuis le
2026-08-03 (`fab274e`) — avant cela, en `build_type=legacy`, Pages publiait carrément
le dépôt ENTIER, ROM comprises. La purge de 08:54 ne l'a pas fermé toute seule : elle
a fait ÉCHOUER le job `wasm`, si bien que le site a continué de servir le dernier
bundle réussi, celui de 07:54, TOS dedans. C'est le déploiement de **10:03**, celui du
correctif ci-dessus, qui est le premier bundle Pages réellement libre — vérifié sur le
site lui-même (l'`index.js` en ligne ne précharge que les 4 EmuTOS + `drivesound` +
`diskA.st`, `index.data` à l'octet près celui du build local). Le § BLOQUANT du
`TODO.md`, qui ne recensait que les assets des releases 0.5.2 / 0.5.4, était donc
incomplet ce matin.

## La purge : l'historique public ne distribue plus rien de propriétaire (2026-08-30)

Le pas 3 du § BLOQUANT est exécuté — la **réécriture d'historique** (`git filter-repo`,
69 motifs) qui fait passer le pack de **165 à 12 Mio** (−93 %) et sort du dépôt ET de
tous ses commits : 38 TOS Atari, 68 jeux commerciaux majoritairement crackés,
5 cartouches Field Service, Cubase Lite, Spectrum 512, `dev/agt` et
`dev/reservoir-gods` (vendorisés sans licence), `gemdos/etalon` (binaires dérivés de
GODLIB). L'essai à blanc préalable sur clone jetable a rapporté plus que le plan : le
tableau du § BLOQUANT ne voyait que HEAD, et l'historique cachait **~60 Mio de plus**
du même genre — `wasm/index.data` (73 Mo dépaquetés : l'image Emscripten d'avant le
23/08 embarquait TOS + disquettes), les 35 jeux à leur emplacement d'avant le
déménagement vers `disks/st/`, l'ancien dossier `rom/`, `build/` commité par erreur en
juin, `disks/utils`, `disks/midi/MIDI_ST1`. Avec la seule liste du TODO, le pack ne
descendait qu'à 87 Mio.

Ce que la purge ne coûte PAS : les chemins purgés sont gitignorés et chaque machine
les garde localement — **prouvé sur le clone purgé** : sans les fichiers, palier
`fast` vert avec SKIP recensés ; fichiers restaurés (`tools/private_assets.sh
unpack`), palier `fast` entièrement vert **sans amputation**, séquenceur MIDI
compris, et git n'y voit rien (`check_doc_claims` compte via `git ls-files`).
`tools/setup_devkits.sh` re-clone GODLIB×4 + AGT aux pins de leurs notes de
vendorisation (reprises avant leur départ). Le fil-piège `check_doc_claims` a mordu
pendant l'essai exactement comme conçu (les compteurs du tableau § BLOQUANT) — les
six entrées partent avec leur tableau, remplacées par un garde **négatif** : le
« 12 Mio » du TODO est recompté comme 12 + le nombre de fichiers purgés encore
suivis, donc toute fuite d'un chemin purgé dans l'index refait mentir le chiffre.
CI : le job « démos réelles sous sanitizers » passe de `tos102uk` à `etos192fr`
(les trois démos bootent sous EmuTOS, vérifié). Décision assumée : les œuvres
demoscene (Cuddly, No Cooper, Closure, CURLY, le corpus MIDI CC BY-NC-SA) restent.

Hors dépôt : archives privées du mainteneur (miroir git complet d'avant purge +
tarball des fichiers de travail + la liste des 69 motifs), régénérables par
`tools/private_assets.sh pack`. Reste après la purge (→ TODO) : pas 4
(`NEOST_PACKAGE_NO_ATARI_TOS=1` par défaut), les **assets des releases 0.5.2/0.5.4**
(leurs paquets bureau contiennent `tos102uk.img` + `tos162uk.img` — vérifié en
ouvrant le zip Windows ; le web-wasm 0.5.4 est propre, EmuTOS seul), et A37
(signature), enfin sensée maintenant qu'un paquet peut être 100 % libre.

## Les trois « laissé tel quel » du bug hunt ne le sont plus (2026-08-30, même jour)

Le bug hunt ci-dessous s'était arrêté à quatre correctifs et avait consigné trois
trouvailles « telles quelles ». Décision revue le jour même : les trois sont traitées —
deux correctifs, et un silence levé.

**1. Le refCon CoreMIDI ne peut plus pointer sur un Device libéré.** Rien ne documente
que `MIDIPortDisconnectSource` attende la fin d'un callback EN VOL sur le thread du
MIDIServer, or le `srcConnRefCon` de chaque connexion pointe sur NOTRE `Device`. Le
callback ne déréférence plus qu'après avoir vérifié, sous un verrou dédié (`devMtx_`),
que ce Device appartient encore à `devices_` — et il GARDE le verrou pendant la
livraison du paquet ; `close()` prend le même verrou avant de libérer. Un callback
commencé se termine donc AVANT la libération, un callback tardif échoue au test
d'appartenance et repart sans toucher à rien. Corollaire d'ordre : le Device entre dans
la liste AVANT `MIDIPortConnectSource` (un premier paquet peut arriver dès la
connexion ; il aurait échoué au test et été perdu). Ordre des verrous documenté :
`devMtx_` puis `mtx_`, jamais l'inverse. Pas de test possible sans un MIDIServer réel —
correction par construction, invariant écrit à la déclaration du verrou.

**2. Le SysEx tronqué respecte enfin sa propre borne — et le mécanisme consigné la
veille était FAUX.** L'entrée du bug hunt disait « dépasse d'un octet le paquet CoreMIDI
de sortie » : recompté, c'est inexact — 4 097 octets TIENNENT dans le tampon CoreMIDI
(4 160 − ~14 d'en-tête). La vraie victime est l'**encodeur ALSA de sortie**,
dimensionné à 4 096 exactement (`snd_midi_event_new(4096)`) : le message d'un octet de
trop échouait à l'encodage et tombait en silence — sous Linux, pas sous macOS. Le
correctif pose l'invariant à la SOURCE : la troncature du Parser vaut pour le message
ENTIER, `$F0` et `$F7` compris (le contenu était borné AVANT l'ajout du `$F7` final).
Trois assertions au palier `fast` (309 → **312**) : jamais plus de `kMaxSysex`, le
tronqué reste un SysEx bien formé, un dump court passe intact — vérifiées par MUTATION
(la borne fautive réintroduite fait rougir la première).

**3. Le repli par nom cesse d'être silencieux quand l'identité diffère.** Le
comportement reste (choix assumé : un clavier remplacé par le même modèle doit marcher
sans toucher la config), mais quand l'identifiant unique de l'appareil ouvert diffère
de celui que la config désigne, l'ouverture le DIT désormais — entrée comme sortie :
`opened by NAME — its unique id differs (configured X, found Y): same-model
replacement?`. La config n'est PAS réécrite : réapprendre l'identifiant d'office
recréerait le bug des homonymes que le hunt venait de fermer.

`--tier full` vert, exit 0 ; extinction GUI propre vérifiée.

## Bug hunt sur les travaux du 27-30 août : quatre bugs, quatre correctifs, quatre tests (2026-08-30)

Chasse ciblée sur ce que les trois derniers jours ont produit. Le tri du périmètre :
les chantiers du 28 (Shifter, CpuState, Pacing, dispatch MMIO) sont gardés par les
étalons et leurs propres selftests ; A9 (le matin même) a été vérifié par construction.
Le moins couvert était la **pile MIDI hôte** du 29 — du code qui parle à du matériel
que la CI n'a pas — et les **points de suture** d'A9. C'est là que tout a été trouvé.
Quatre bugs, aucun ne pouvait être vu par un étalon : trois ne se manifestent qu'avec
des appareils MIDI branchés, le quatrième qu'à l'extinction du processus.

**1. La reconnexion à 1 Hz paniquait le studio tant qu'un appareil manquait.**
La garde de la boucle comparait le nombre d'appareils OUVERTS au nombre CONFIGURÉS —
or un appareil configuré durablement absent est un état NORMAL et revendiqué (« on
garde le nom, on re-tente, la page affiche (not connected) »). La garde était donc
vraie à chaque seconde, et une re-tentative commence par tout fermer :
`closeDestinations()` **panique** (All Sound Off + Reset Controllers + All Notes Off
sur 16 canaux) les appareils encore branchés, puis détruit et recrée les ports. Les
notes tenues du synthé restant tombaient **une fois par seconde** ; côté entrée, la
purge du tampon jetait les octets en attente au même rythme. La garde compare
désormais à ce qu'une re-tentative **ouvrirait** (`neost::midi::countMatchable`,
fonction pure) : un absent durable coûte zéro, un retour ou un débranchement
re-déclenche exactement une fois.

**2. Deux homonymes apprenaient le même identifiant — le bug dans le correctif de
l'avant-veille.** `midiLearnUids()` (2026-08-29) devait rendre sûre une config sans
identifiants ; pour DEUX claviers du même modèle — le cas d'usage même de la
fonctionnalité — les deux lignes apprenaient l'identifiant du PREMIER point ouvert
(les deux boucles s'arrêtaient au premier nom correspondant). Le repli par nom
masquait l'erreur tant que les deux claviers restaient branchés ; on débranchait le
premier, et le masque de canaux de la seconde ligne pilotait le mauvais appareil.
La logique est extraite en fonction pure (`neost::midi::learnUids`) avec la règle
« jamais deux fois le même identifiant » ; une ligne dont l'homonyme ouvert est déjà
réclamé reste VIDE — ne rien apprendre vaut mieux qu'apprendre faux.

**3. Un save-state d'avant le MIDI tuait l'entrée MIDI au chargement.** L'horloge de
réception (`Scheduler::MIDI_RX`, l'ACIA tire un octet toutes les 2 560 cycles) est
une échéance du Scheduler, et le Scheduler restaure SES échéances — celles de l'état
sauvé. Un état sauvé sans appareil MIDI IN, rechargé (F7) pendant qu'un clavier est
branché : MIDI_RX restauré éteint, la source hôte toujours là, plus rien ne tire —
**entrée morte en silence**, et la reconnexion à 1 Hz ne la ranime pas (l'appareil
est toujours « ouvert » côté hôte). `MidiAcia::serialize` réarme au chargement si une
source existe et que l'échéance ne s'est pas restaurée ; restaurée, elle garde sa phase.

**4. A9 avait inversé l'ordre de destruction du thread audio.** Dans l'ancien
`main()`, `Audio` était déclaré APRÈS `DriveSound` et `Mt32Synth`, donc détruit
AVANT eux — son thread, qui les mixe par pointeur brut et lit `machine.psg/dmasnd`,
mourait avant ses sources. La `struct App` du matin les avait rangés par thème :
`audio` en deuxième position, donc détruit APRÈS `drive`, `mt32` et presque tout —
fenêtre d'usage-après-destruction à chaque extinction. L'ordre des membres est
rétabli et documenté comme un CONTRAT, pas une présentation.

**Vérification** : chaque correctif de logique porte son test dans
`tests/selftest_logic.cpp` (301 → **309 assertions**, palier `fast`) — apprentissage
des homonymes (3 scénarios : les deux branchés, uid déjà réservé, un seul présent),
garde de reconnexion (absent durable / retour), et le save-state croisé sur le
harnais `Rig` complet (MidiInHost + MidiAcia + Scheduler, sérialisé dans l'ordre de
`Machine`). Les trois sont vérifiés par MUTATION — réintroduire chaque bug fait
rougir son test, et le sien seulement. L'ordre de destruction n'a pas de test (il
faudrait instrumenter des destructeurs) : il a un commentaire-contrat et un run
d'extinction propre. `--tier full` vert, exit 0.

**Trouvé et laissé tel quel, consigné ici pour ne pas le re-trouver :**
- `MIDIPortDisconnectSource` ne garantit pas qu'un callback CoreMIDI en vol soit
  terminé quand `close()` libère les `Device*` passés en refCon — fenêtre théorique
  de quelques microsecondes à la fermeture, pattern standard des clients CoreMIDI,
  aucun crash observé. À revoir si un crash à la fermeture apparaît un jour.
- L'appariement retombe sur le NOM quand l'identifiant voulu est absent, même si
  l'homonyme présent porte un identifiant DIFFÉRENT. C'est un choix, pas un oubli :
  un clavier remplacé par le même modèle doit continuer de marcher sans toucher la
  config. Le cas limite (deux homonymes, un seul branché, le mauvais) est déjà au
  TODO.
- Un SysEx tronqué à la borne (4 096 octets + $F7 = 4 097) dépasse d'un octet le
  paquet CoreMIDI de sortie et tombe en silence — atteignable uniquement par un dump
  déjà tronqué, donc déjà corrompu.

## A9 — `main.cpp` passe de 5 100 lignes à 28 (2026-08-30)

Le fichier le plus gros du dépôt était aussi le seul où rien ne pouvait sortir. Il
portait **84 variables `g_*`** à liaison interne et un `main()` de **2 505 lignes**, dont
une boucle de 1 600. Chacun des 84 globaux se justifiait pris isolément — un callback
GLFW n'a pas de paramètre où passer un contexte, une requête posée par un menu se
consomme en fin de trame — mais leur SOMME verrouillait le fichier : aucune fonction ne
pouvait déménager sans emporter la moitié du tas avec elle, donc toute nouvelle page
d'interface atterrissait là. Mesure du coût de l'attente : **+286 lignes en deux jours**
(4 814 le 27, 5 013 le 29, 5 100 le 30), l'essentiel étant la page MIDI.

**L'état a un propriétaire.** `src/gui/App.hpp` — une `struct App` qui contient les 84
ex-globaux (mêmes valeurs initiales, mêmes commentaires, regroupés par thème), puis la
SESSION que `main()` tenait en variables locales : `Machine`, `Audio`, `DriveSound`,
`MidiOutHost`/`MidiInHost`/`Mt32Synth`, les deux backends réseau, le modem, l'écran GL,
les chemins de données et la config de travail. Possédés par `unique_ptr` sur types
incomplets : un fichier d'interface inclut `App.hpp` sans traîner tout le cœur derrière.
`app()` rend l'instance unique — et c'est réservé aux **callbacks GLFW et aux
gestionnaires de réglages ImGui**, dont la signature est imposée ; tout le reste reçoit
`App&` en paramètre.

**Les treize lambdas de `main()` sont devenues des méthodes** : `applyConfig`,
`midiOutApply`, `midiInApply`, `midiLearnUids`, `usatanApply`, `slirpApply`,
`etherApply`, `netUsbeeApply`, `modemApply`, `neBackend`, `resolvePath`,
`loadDongleTable`, `switchKioskMode`. Elles capturaient `[&]` la moitié de `main()` ;
elles agissent maintenant sur une session nommée.

**Onze modules dans `src/gui/`**, chacun prenant `App&` : `ConfigWindow` (la fenêtre de
14 pages, 810 l.), `KioskMenu` (le menu borne, 421 l.), `DebugWindows` (hexa, CPU,
joystick, débogueur, 344 l.), `StScreenView` (`GlScreen` + passe CRT + les deux
cadrages, 197 l.), `InputCallbacks`, `DockLayout`, `CrtUi`, `JoyMap`, `GlHeaders`,
`AppInit` (l'avant-première-trame, 435 l. + un `parseCommandLine` séparé de 105 l.),
`AppLoop` (la boucle + l'arrêt). La discipline de requêtes de `MediaPages` est
généralisée : **une page ne fait rien**, elle lit un état et pose une requête que la
boucle consomme à une frontière de trame — c'est précisément ce qui permet à 810 lignes
de fenêtre de vivre ailleurs que dans la boucle qui les exécute.

`main()` tient en **10 lignes** : `appInit` → `appLoop` → `appShutdown`.

**Ce qui N'A PAS été fait, et pourquoi.** `appLoop` fait toujours **1 758 lignes d'un
seul tenant**. Elle a été DÉPLACÉE, pas découpée. Deux raisons, l'une de méthode et
l'autre technique : le garde-fou du plan interdit de combiner deux refontes (« ne pas
refondre la boucle en même temps qu'autre chose ») ; et la boucle a ses propres verrous
— une vingtaine de variables de trame partagées entre les phases (`fbw/fbh`,
`cTop/cH/cW`, `menuH`, `reqMount*`, `cfgUi`…) et un corps qui traverse des blocs
`#if defined(NEOST_WITH_IMGUI)` de plusieurs centaines de lignes, `#else` compris. Le
prochain pas est écrit au TODO : nommer ces variables dans une `struct Frame` — le même
geste qu'`App`, à l'échelle du tour — PUIS couper aux frontières déjà commentées.

**Ce qui a rendu le chantier sûr sans filet de test neuf : aucun corps n'a été
réécrit.** Deux conventions rendent chaque déplacement textuellement nul :
1. la fonction extraite **s'appelle son paramètre `A`**, comme s'appelait la référence
   au niveau fichier ;
2. elle **ouvre sur des alias** — `Machine& machine = *A.machine;`, `Config& cfg =
   A.cfg;` — si bien que le corps déplacé est identique au caractère près.
Le compilateur a donc attrapé ce qu'un test n'aurait pas vu (une lambda sans capture qui
lisait un global, un `const` de signature, une inclusion manquante), et les paliers ont
gardé le reste : **`--tier full` est vert, doc-claims compris** — tous les étalons pixel,
le diagnostic MegaSTE 12/12, le boot GUI, à l'octet près.

Deux effets de bord réparés au passage :
- **les modules extraits gardent la sévérité de `main.cpp`.** Sans une ligne ajoutée à
  `set_source_files_properties`, sortir du code de `main.cpp` aurait silencieusement
  désarmé `-Wall -Wextra -Wpedantic` sur les 2 400 lignes déplacées. Vérifié : zéro
  avertissement, strict armé.
- **l'inclusion GL a un seul endroit** (`gui/GlHeaders.hpp`) : la danse
  macOS/`glext.h`-hors-macOS était recopiée, elle l'aurait été une fois de plus.

Garde-fou déplacé : `tools/check_doc_claims.py` ne surveille plus la taille de
`main.cpp` (28 lignes, plus rien à surveiller) mais celle d'`AppLoop.cpp` — le RESTE.
Fil-piège vérifié en le déclenchant.

Références de fichier corrigées dans la foulée : `DEV.md` (arborescence `src/`,
invariants du mode borne), `docs/HATARI_MAPPING.md` (`main.cpp:531-547` → `keymap` vit
dans `gui/InputCallbacks.cpp` + `gui/StKeys` ; `main.cpp:1063` → `core/Pacing.hpp:27`).

### Et une affirmation fausse trouvée en corrigeant ces renvois

`core/Pacing.hpp` écrivait : « C'est le SEUL littéral 8021248 de l'arbre — tout le reste
pointe ici. » **C'était faux**, et depuis assez longtemps pour que personne ne le
recompte : `io/Mfp.cpp:119` calculait la durée d'un octet série sur un
`int64_t(8021248)` recopié, et `headless/main_headless.cpp` écrivait `cpu_hz=8021248`
**en dur dans l'en-tête du dump MIDI** — le fichier même dont `tools/midi_compare.py`
se sert pour convertir des cycles en secondes. Les deux pointent maintenant sur
`neost::pacing::kCpuHzInt` (substitution à valeur ET type identiques : `int64_t`,
8021248 — l'en-tête du dump ressort octet pour octet).

Et `midi_compare.py` **lit désormais `cpu_hz=` dans l'en-tête** au lieu de porter sa
propre copie de la constante : le producteur date ses octets en cycles, c'est donc son
horloge qui doit convertir. Vérifié sur trois dumps forgés — en-tête présent (1,0 s au
cycle 8021248), en-tête absent (repli sur la constante, même résultat), en-tête
annonçant la moitié de l'horloge (le temps double, l'outil SUIT le producteur). Sans
ça, changer `kCpuHzInt` d'un côté décalait silencieusement toutes les mesures de tempo
de l'autre — l'outil aurait mesuré une dérive qui n'existait pas.

Le commentaire de `Pacing.hpp` ne se contente plus d'affirmer : il énonce la règle,
**dit qu'elle a déjà cédé une fois**, et nomme la seule exception assumée (le script
Python, qui ne peut pas inclure un `.hpp`).

### Et les trois avertissements que personne ne voyait plus

En recompilant TOUT (`touch` sur chaque `.cpp`, pas seulement les cibles reconstruites
au fil du chantier), il restait **trois avertissements permanents**, tous de la même
famille et tous nés du travail sur les identifiants MIDI du 2026-08-29 :
`missing field 'uid' initializer` — `gui/AppConfig.cpp` ×2 et `headless/main_headless.cpp`.
Anodins pris un par un ; ensemble, c'est ce qui apprend à ne plus lire les
avertissements, et `-Wall -Wextra -Wpedantic` cesse alors d'être un garde-fou. **Zéro
avertissement sur une reconstruction complète, toutes cibles.**

Le correctif ne se contente pas de faire taire : les trois sites construisent
maintenant l'entrée champ par champ, avec la raison écrite — `uid` vide n'est pas un
oubli, c'est l'état exact d'un format qui est ANTÉRIEUR aux identifiants (et que
`App::midiLearnUids()` renseignera à la première ouverture réussie). Bénéfice de
forme : un champ ajouté demain gardera son défaut au lieu d'être recopié à la main.

**Et ce chemin-là n'était couvert nulle part.** Le format hérité (`midi_out_device=`,
`midi_in_device=`, clés répétables) n'existe que pour qu'un `neost.cfg` d'avant ne perde
pas son studio en silence — son seul symptôme de panne serait une liste d'appareils vide
au démarrage suivant, sans message. Toucher à un tel chemin sans le border n'était pas
tenable : `tests/selftest_logic.cpp` gagne **4 assertions** (nom relu des deux côtés,
masque de canaux `1,2,10` → `$0203`, canal d'entrée, et l'invariant « uid vide »),
palier `fast`. 297 → **301 assertions**. Vérifiées par MUTATION : un `substr(16)` changé
en `substr(17)` fait rougir la première — le test mord.

La ligne d'inventaire correspondante de `docs/HATARI_MAPPING.md` a été **recomptée** :
son verdict « 8021248 en dur à 5-6 endroits, non centralisé » était périmé depuis A28,
et ses cinq `fichier:ligne` désignaient tous du code sans rapport (`Audio.cpp:74`,
`DmaSound.cpp:27`, `Rtc.hpp:52`, `Mfp.cpp:226` — non revérifiés depuis). Ce qui reste
vrai de cette divergence, et seul : **une seule fréquence pour toutes les machines**,
pas de variantes NTSC/MegaSTE. Leçon, la même qu'A38 : un chiffre posé dans un document
et jamais recompté finit par mentir, et un `fichier:ligne` non revérifié ment plus vite
encore que le chiffre.

## MIDI — deux claviers du même modèle cessent d'être le même clavier (2026-08-29)

Limite inscrite au TODO la veille au soir, levée. La config désignait les appareils par
leur nom d'affichage ; deux machines du MÊME MODÈLE branchées ensemble — deux claviers
identiques, le cas d'un studio — portent exactement le même nom. On ouvrait donc deux
fois le premier et le second restait muet, sans que rien ne le dise.

L'index n'était pas la solution : il se renumérote dès qu'on débranche un voisin, et une
config mémorisée en index se met à piloter la mauvaise machine — c'est le piège inverse,
et c'est pour l'éviter qu'on était parti du nom.

**La réponse : (nom, identifiant unique).** `kMIDIPropertyUniqueID` sous CoreMIDI, stable
d'un branchement à l'autre ; ALSA n'a pas d'équivalent (client:port change à chaque fois)
et laisse l'identifiant vide. L'appariement (`src/audio/MidiEndpoint.hpp`) fait deux
passes — identifiant d'abord, nom ensuite — et n'attribue **jamais deux fois le même
point de terminaison**. C'est cette dernière règle qui fait tenir le cas ALSA : deux
homonymes branchés ensemble reçoivent chacun le sien. L'ordre des passes compte : sans
lui, une entrée désignée par son nom raflerait le point qu'une autre réclamait par son
identifiant.

**Apprentissage.** Une config qui ne connaît qu'un nom ne deviendrait jamais sûre toute
seule : l'identifiant du point réellement ouvert y est noté à la première ouverture
(trace `learned unique id …`), et persisté. Les configs existantes se réparent donc
d'elles-mêmes au premier lancement.

**Interface.** Étiquettes suffixées « #1 / #2 » sur les seuls homonymes — sans quoi deux
lignes seraient rigoureusement identiques à l'écran. Le suffixe n'apparaît jamais sur un
nom unique.

L'appariement est une fonction PURE, et c'est délibéré : le développeur n'a qu'un seul
appareil branché, le cas ne pouvait donc être éprouvé QUE hors matériel. `neost-selftest`
couvre les deux entrées homonymes qui doivent tomber sur deux points distincts, la
priorité de l'identifiant sur le nom, la survie à une renumérotation, l'absent qui ne
vole rien, et les étiquettes. Deux mutations vérifiées : supprimer la non-réattribution,
et ignorer l'identifiant. 297 assertions.

⚠ Reste hors de portée : deux homonymes dont un SEUL est branché, sous ALSA — rien ne
permet alors de savoir lequel. Inscrit au TODO.

## Interface — « Memory (hex) » et « CPU 68000 » se ferment enfin (2026-08-29)

Rapporté par l'utilisateur. Ces deux fenêtres d'inspection appelaient `ImGui::Begin`
SANS son paramètre `p_open` : ImGui ne dessine alors aucune croix, et le seul moyen de
les faire disparaître était le menu Windows — qu'il faut savoir chercher. Elles étaient
les DEUX SEULES du projet dans ce cas (Joystick, CRT Effects, Debugger, Floppies,
Configuration et Keyboard passent toutes leur drapeau), donc une incohérence pure, pas
un choix.

L'état était déjà persisté (`showHex=` / `showCpu=` dans `neost.cfg`, vérifié : la valeur
fait l'aller-retour config → global → config) : la croix se comporte exactement comme
l'entrée de menu, fenêtre fermée = fenêtre encore fermée au lancement suivant.

Restent volontairement non fermables : l'écran ST (fenêtre de base) et les incrustations
du mode borne.

## MIDI — le studio en double : une clé répétable n'est pas une clé (2026-08-29)

Bug rapporté : « Circuit Tracks est en double dans la fenêtre MIDI, ce qui crée un bug ».
Les deux moitiés étaient vraies et distinctes.

**La cause.** Les listes d'appareils utilisaient des clés RÉPÉTABLES (`midi_out_device=`
une fois par appareil, `midi_out_channels=` s'appliquant à la précédente). Or
`parseConfigLine` est partagé avec les **profils nommés**, et un profil s'applique
PAR-DESSUS la config courante (`Config p = cfg;` puis application des lignes). Là où
toutes les autres clés remplacent, un `push_back` AJOUTE : charger un profil dupliquait
chaque appareil. Le format était en désaccord avec la sémantique du reste du fichier, et
c'est le partage avec les profils — que je n'avais pas vu — qui a transformé ce désaccord
en bug.

**Le symptôme.** Deux lignes de même nom partageaient le même `ImGui::PushID(name)` :
leurs cases se pilotaient l'une l'autre. D'où « ça crée un bug » et pas seulement « c'est
affiché deux fois ».

**Correction.** Une clé, une ligne, une affectation — comme tout le reste du format :
`midi_out_devices=nom|1-4,10;autre|3`, enregistrements séparés par `;`, champs par `|`,
ces caractères échappés par `\` dans les noms. L'objection qui avait fait écarter un
séparateur au départ (« un nom d'appareil contient n'importe quoi ») est levée par
l'échappement, pas contournée par la répétition. Les listes sont écrites **même vides**,
sans quoi un profil ne pourrait pas effacer le studio. L'ancien format reste LU pour
qu'un `neost.cfg` existant ne perde pas son studio en silence ; la première sauvegarde le
convertit.

Défense en profondeur : l'identifiant ImGui d'une ligne est désormais son INDEX. Deux
appareils du même MODÈLE branchés ensemble portent le même nom d'affichage et
produiraient la même collision — cas réel, inscrit au TODO (il demande un identifiant
unique mémorisé à côté du nom).

`neost-selftest` : aller-retour d'écriture/relecture avec un nom contenant les trois
caractères de l'encodage, et surtout le SCÉNARIO DU BUG — rejouer les mêmes lignes
par-dessus une config déjà remplie, ce que fait `loadProfileInto`, doit REMPLACER.
Mutation vérifiée : rétablir la sémantique d'ajout fait tomber trois assertions. 287 au
total.

## MIDI — profil Circuit Tracks et avance de sortie réglable (2026-08-29)

Deux points de la section « Station MIDI » du TODO.

**Profils d'appareil** (`src/audio/MidiDeviceProfiles.hpp`). Cocher seize cases pour une
machine dont le plan de canaux est public est du travail perdu. Première entrée, le
**Novation Circuit Tracks**, d'après son *Programmer's Reference Guide* v3 : Synth 1
canal 1, Synth 2 canal 2, pistes MIDI 1-2 canaux 3-4, **Drums 1-4 tous sur le canal 10**
— ils se distinguent par la NOTE (60, 62, 64, 65), pas par le canal — et canal 16 réservé
au Project Control. Le bouton pose le masque `$020F`, l'infobulle donne le plan complet,
qui est justement ce qui manque au moment de séquencer les percussions et non au moment
de câbler. ⚠ Ce sont des défauts d'usine, réassignables en Setup View : la table n'accepte
qu'une source constructeur **citée**. Un plan deviné enverrait chercher une panne qui
n'existe pas.

**Avance de livraison réglable** (`midi_lead_ms=`, curseur 0-100 ms). Elle était figée à
30 ms — la moitié de la latence de jeu, et un arbitrage qui appartient à l'utilisateur :
plus courte, le clavier répond plus direct ; plus longue, elle absorbe un à-coup de la
boucle GUI. Vérifié : **190 ms d'écart de livraison mesurés pour 200 ms commandés**.

Résultat NÉGATIF, consigné parce qu'il contredit l'intuition qui a motivé le chantier :
à 0 ms d'avance, **aucun octet n'est en retard** sur un run sain. L'émulation d'une trame
prend moins de temps réel qu'une trame, donc l'octet daté au milieu de la trame est
encore dans le futur au moment d'être programmé. L'avance ne protège pas du cas courant
mais du RATTRAPAGE, quand la boucle décroche et enchaîne jusqu'à 6 trames. D'où le témoin
`lateBytes` (compté quand l'échéance est déjà passée) exposé dans la page MIDI et dans le
bilan `--run-frames` : on baisse le réglage jusqu'à ce qu'il bouge, au lieu de deviner.

Deux instruments se sont révélés inaptes en route, ce qui vaut d'être noté : le sink
horodaté est noyé dans la gigue de démarrage des processus (663 ms d'écart relevé pour
200 ms attendus, sur un tirage), et `NEOST_MIDIOUT_TRACE` pose son `t0` au PREMIER octet
livré — un décalage uniforme s'y annule par construction. Seul un protocole à démarrage
contrôlé, comparant deux runs, a donné les 190 ms.

Corrigé au passage dans `docs/EXTENSIONS.md` : la phrase « sous Linux la destination est
un abonnement » était devenue fausse au chantier précédent (les destinations sont
adressées explicitement depuis l'aiguillage par canal, un abonné recevant tout).

## MIDI — un studio entier : fusion en entrée, aiguillage par canal en sortie (2026-08-29)

NeoST ne tenait qu'UN appareil de chaque côté. Un studio en a plusieurs — une groovebox
et deux claviers d'un côté, un piano logiciel et un expandeur de l'autre.

**Sortie : un AIGUILLAGE, pas un Thru box.** Chaque destination porte le masque des
canaux qu'elle reçoit (`midi_out_device=` + `midi_out_channels=`, clés répétables) :
« instrument 1 de Cubase vers le piano logiciel, instrument 2 vers la groovebox » se
règle chez NeoST, sans toucher au réglage des appareils. Un même canal peut partir vers
plusieurs destinations (superposition). Les messages **système** (horloge, start/stop,
SysEx) n'ont pas de canal et vont à **toutes** : les filtrer désynchroniserait le studio.
Sous ALSA, les destinations sont adressées explicitement (`snd_seq_ev_set_dest`) et non
abonnées — un abonné recevrait tout, ce qui interdirait le filtrage.

**Entrée : un boîtier de FUSION.** Le ST n'a qu'une prise MIDI IN. Un tel boîtier ne
mélange pas des octets, il entrelace des MESSAGES : deux claviers joués ensemble émettent
`90 3C 40` et `90 40 40` au même instant, et entrelacés octet par octet ils donneraient
`90 90 3C 40 40 40` — du charabia. D'où un décodeur PAR SOURCE. Le statut n'est ré-émis
dans le flux fusionné que s'il a CHANGÉ : une source seule garde son running status, deux
sources qui alternent le voient réinséré.

**Canalisation — sans elle, pas d'enregistrement multipiste.** Deux claviers émettent tous
deux sur le canal 1 par défaut : le séquenceur ne peut pas les séparer et tout finit sur
la même piste. `midi_in_channel=N` réécrit le quartet de canal des messages de voie (pas
des messages système, qui n'en ont pas). Ce que le séquenceur ST en fait le regarde — les
Cubase complets et Notator enregistrent plusieurs canaux sur plusieurs pistes, Lite non.

`MidiMessageParser.hpp` : le décodeur octets→messages, jusque-là privé de MidiOutHost,
devient partagé — la fusion en avait besoin, et deux copies auraient divergé. Extraction
vérifiée SANS régression par l'étalon Cubase (200 notes, pente 1,00097, gigue σ 0,45 ms,
identique).

GUI : matrice appareils × 16 canaux en sortie (un clic par affectation, `all`/`none` par
ligne), liste avec canal forcé en entrée. Headless : `--midi-in-device` devient RÉPÉTABLE
(fusion) et `--midi-in-channel N` s'applique à l'appareil précédent — un séparateur dans
la valeur aurait buté sur les noms ALSA, qui contiennent déjà « : ».

Vérifié bout en bout (macOS, deux destinations virtuelles) : la panique de fermeture
diffuse des CC sur les 16 canaux, et chaque destination n'a reçu QUE les siens — `B0 78
00…` pour celle du canal 1, `B1 78 00…` pour celle du canal 2, 9 octets chacune sur 48
messages — tandis que le SysEx de démarrage de MROS est arrivé aux DEUX. `neost-selftest`
couvre la fusion (messages intacts et ordonnés quand deux sources s'entrelacent), la
canalisation (deux sources → deux canaux distincts) et le temps réel (l'horloge $F8
traverse sans casser le running status) ; mutation vérifiée : un décodeur unique partagé
par les sources fait tomber le test de fusion.

## MIDI IN — l'ACIA tire à 31 250 bauds au lieu d'une rafale par trame (2026-08-29)

Défaut de la livraison du jour même, trouvé en mesurant plutôt qu'en supposant. L'entrée
hôte poussait ses octets dans l'ACIA **une fois par trame**, et le 6850 n'en accepte que
2 : plafond de **2 octets/trame**, mesuré 1,76 sous flux saturé (10 564 octets en 6 000
trames) — soit ~143 o/s en mono, ~100 o/s en PAL, contre **3 125 o/s** sur un vrai câble
MIDI, 4,5 %. Un accord de dix notes mettait 0,2 s à entrer ; un balayage de molette de
hauteur (~300 o/s) creusait un retard qui grandissait.

C'est désormais l'**ACIA qui tire** : `MidiAcia::setRxSource` + échéance
`Scheduler::MIDI_RX`, un octet toutes les 2 560 cycles (10 bits à 31 250 bauds). Le
pendant exact de `MIDI_TX` en émission, et le même patron qu'`IKBD_RX` pour l'ACIA
clavier. Conséquence de fidélité : le débordement redevient celui du **matériel** — si
le ST ne lit pas assez vite, c'est le 6850 qui perd l'octet **neuf** — au lieu d'être
masqué par une rétention côté hôte.

**Mesuré en temps réel (GUI, mono 71 Hz, source hôte à 3 000 o/s) : 76 800 octets en
1 900 trames = 40,4 o/trame, ~2 885 o/s, 92 % d'un câble.** Facteur 20, et hors
d'atteinte de l'ancien plafond. ⚠ La mesure n'est possible **que** dans le GUI : le
headless émule ~19 fois plus vite que le temps réel, donc une source MIDI réelle y reste
toujours le facteur limitant — le bilan `MIDI IN: N bytes` y est ajouté aussi, mais il
mesure l'hôte, pas la puce.

`neost-selftest` éprouve le chemin complet (MidiInHost + MidiAcia + Scheduler) sans le
moindre appareil : cadence bornée des deux côtés (ni plus vite ni plus lentement que le
câble), 200 octets en ~200 périodes série, overrun matériel, tampon hôte saturé. Un
piège rencontré et consigné dans le test : une source ne tire qu'**une fois par appel à
`runTo`** (masque `fired` du Scheduler, modèle Hatari) — un test qui avance d'un gros
bloc ne mesure que lui-même.

**Save-state v18** : `SRC_COUNT` passe de 20 à 21, le tableau `due_` sérialisé change de
taille. Les états v17 sont refusés.

## MIDI — NeoST choisit son appareil, dans les deux sens (2026-08-29)

Le MIDI OUT de NeoST ne savait sortir que vers un **port virtuel** (source CoreMIDI /
port ALSA « NeoST MIDI OUT »), et le MIDI IN n'avait aucune entrée hôte du tout. Or une
source virtuelle est **passive** : un FluidSynth s'y abonne, un expandeur ou une
groovebox ne s'abonne à rien. Piloter du vrai matériel demandait donc un patchbay tiers
entre les deux — constaté sur un Novation Circuit Tracks, qui n'entendait NeoST qu'à
travers un pont écrit pour l'occasion.

- **Destination matérielle** (`midi_out_device=`, GUI Configuration → MIDI) : le MIDI OUT
  du ST entre directement dans l'appareil. `MidiOutHost::destinations()` énumère,
  `openDestination()` ouvre par NOM.
- **Source matérielle** (`midi_in_device=`, même page) : un clavier maître ou un
  séquenceur entre dans le MIDI IN du ST. Classe neuve `MidiInHost`, câblée sur
  `MidiAcia::receiveExternal` — le chemin que l'anneau MIDI réseau utilisait déjà.
- Headless : `--midi-list` (énumère) et `--midi-in-device NAME`. Pas de
  `--midi-out-device` : `--midi-dump` capture déjà la sortie sans dépendre du matériel.

Quatre décisions, chacune contre un piège concret : désignation **par nom** (un index se
serait mis à pointer le mauvais appareil au débranchement suivant) ; un appareil absent
**n'efface pas** le réglage et la boucle re-tente à 1 Hz (branchement à chaud) ; panique
CC 120/121/123 **avant** de fermer une destination (un synthé ne relâche jamais une note
tout seul) ; **tampon de gigue** en entrée qui ne livre que ce que `rxCanAccept()`
autorise, en perdant les octets NEUFS en saturation comme un vrai 6850 en overrun.

Vérifié (macOS, Circuit Tracks + appareils virtuels de test) : 255 octets entrés dans
l'ACIA depuis une source hôte, 0 perdu — contre 2 octets pour un appareil qu'on ne
touche pas ; SysEx de démarrage de MROS reçu par une destination matérielle **port
virtuel coupé**, donc portée par la destination seule ; panique observée à la fermeture.
`neost-selftest` couvre le tampon de gigue sans aucun appareil branché (3 propriétés,
2 mutations vérifiées : jeter l'ancien, ignorer le refus du 6850). ⚠ Le backend **ALSA**
est écrit mais **n'a pas été exécuté** — aucune machine Linux dans la boucle.

## A11 — les deux écarts oracle « inexpliqués » sont tranchés : un n'existait pas (2026-08-29)

Deux étalons dormaient en `ref_kind: snapshot` depuis le 2026-08-19 avec un résidu oracle
mesuré et jamais instruit — `overscan_top` 194 px, `trace_odd` 22 px. La note du corpus les
rangeait dans la « même famille » (premières lignes de trame). Elle avait tort sur les deux.

**`trace_odd` : 22 px qui n'étaient pas du rendu.** Hatari incruste une LED disquette dans
ses captures ; `compare_screenshot.py` la masque depuis toujours, sur `(403, 3, 10, 5)` — sa
taille À L'ŒIL. Mais l'oracle capture en 2× puis sous-échantillonne : il reste un liseré d'un
pixel tout autour. Sur fond noir, du noir mêlé de noir ne se voit pas ; sur le fond VERT de
cet étalon, si. Masque corrigé en `(402, 2, 12, 6)` → **0 px**, et l'étalon est promu
`ref_kind: oracle` (10 des 16 étalons machine sont maintenant adossés à Hatari, 9 avant).

Ce qui a permis de trancher SANS dépendre du masque, et qui est la partie réutilisable : les
72 pixels en cause portent des teintes que le Shifter **ne peut pas produire**.
`stColorToArgb` construit chaque octet par `v |= v << 4` — nibbles égaux — et sur ST le bit 3
du nibble n'existe pas : les seuls octets atteignables sont `00 22 44 66 88 AA CC EE`. Or on
lisait `#00B200`, `#007700`, `#E00000`. Aucun réglage de palette ne les produit. Le même test
appliqué à `overscan_top` donne l'inverse : ses teintes (`$333`, `$555`) sont légales.
**Une couleur impossible est une preuve ; une couleur plausible n'en est pas une.**

**`overscan_top` : l'écart est réel, mais il valait 144 et non 194.** Les 50 px de différence
sont la LED — le chiffre de 2026-08-19 avait été relevé au crop `buffer`, sans masque. Un
écart annoncé sans dire ce que la mesure inclut n'est pas une mesure ; c'est la deuxième fois
de la semaine que ça mord.

Les 144 px restants sont **stables sur les 61 trames** de la fenêtre (structurel, aucune
dépendance de phase) et tiennent sur les **5 premières lignes** — celles qu'ouvre le retrait
de bordure haute. Au-delà, les deux images sont identiques au pixel : **le retrait de bordure
haute lui-même est conforme**, ce n'est pas lui qui est en cause. Ce qui diverge est la
bordure GAUCHE sur ces lignes de transition — NeoST garde une fenêtre de 320 px décalée de
−4, Hatari en rend une de 336. Localisé, nommé, non corrigé : ce chemin est calibré à 0 px
contre No Cooper, Closure et Cuddly, et rien ne sera réglé sur la foi d'un seul étalon.
L'item reste ouvert au TODO (**A40**) avec sa mesure et sa piste.

**Ce que le masque élargi coûte** : 22 pixels de plus exclus de chaque comparaison. Vérifié
plutôt que supposé — sur les 15 captures du corpus, ces 22 pixels sont **uniformément la
couleur de bordure**. Le masque ne cache aucun signal.

Palier `full` vert. `tests/reference/trace_odd.ppm` retirée : une entrée `oracle` ne la lit
jamais, la garder aurait été un fichier mort qui a l'air d'une référence.

## A39 — l'IKBD passe son audit : le protocole est pincé, une étiquette était fausse (2026-08-29)

Deuxième module de la liste des jamais-audités. `io/Ikbd.cpp` fait 1 189 lignes et
n'avait qu'un test : le TDRE de son ACIA. Le protocole du 6301 lui-même — accumulation
des commandes multi-octets, longueur attendue de chacune, forme des paquets de réponse
— n'était couvert QUE par des jeux réels : « ça marche ou ça ne marche pas », rien
entre les deux. Or c'est une machine à états pure : des octets entrent, des octets
sortent.

**L'audit, contre `extern/hatari/src/ikbd.c`** :

- **la table des longueurs, opcode par opcode** face à `KeyboardCommands[]`
  (ikbd.c:222-266). Les 39 longueurs sont JUSTES. Une seule anomalie, d'étiquette :
  `$19` était commenté « SetJoystickFireDuration » — le nom que Hatari donne à `$18` —
  alors que c'est le mode **keycode manette** (`SetCursorForJoystick`, 6 paramètres
  RX/RY/TX/TY/VX/VY). Aucune conséquence à l'exécution ; le lecteur, lui, était trompé ;
- **le traitement d'un opcode inconnu.** Hatari vide le tampon (« IKBD assumes a
  NOP ») ; NeoST le dispatche comme mono-octet et remet la longueur à zéro —
  équivalent, vérifié ;
- **PAUSE OUTPUT (`$13`).** Le détail fidèle est que **toute** commande valide lève la
  pause, pas seulement `$11`. NeoST le fait déjà (`Ikbd.cpp`, « toute commande VALIDE
  complète lève la pause de sortie »). C'est le genre de règle qu'une réécriture perd
  sans bruit : elle est désormais gardée.

**La table de vérité** (10 assertions, palier `fast`) : interrogation manette `$16` →
`$FD` + 2 octets ; une commande incomplète n'agit pas ET son octet suivant reste un
paramètre ; `$0D` → `$F7` + 5 octets ; opcode inconnu = NOP qui ne désynchronise pas ;
pause puis reprise par une commande quelconque ; et les 39 longueurs comparées une à
une.

**Vérifiée par mutation**, et c'est là que l'étage intermédiaire paie :

| mutation | ce qui rougit |
|---|---|
| longueur de `$09` : 5 → 3 | **trois** assertions — la table NOMME l'opcode, et les deux tests de flux montrent la désynchronisation (3 octets au lieu de 0, puis 14 au lieu de 6) |
| « toute commande valide lève la pause » retiré | l'assertion dédiée, seule |

Une longueur fausse ne se voit PAS à l'exécution normale : elle décale le flux de
commandes du jeu qui l'utilise, et de lui seul — donc elle se manifeste des milliers de
cycles plus loin, dans un titre, sous la forme « la manette ne répond plus ». C'est
exactement la classe de bug qu'un étalon pixel ne localise jamais.

`neost-selftest` : 257 → **267 assertions**. Palier `full` vert.

## A39 — le disque dur GEMDOS passe son premier audit, et reçoit son premier test (2026-08-28)

L'évaluation d'architecture du jour nommait `io/GemdosHd.cpp` « la surface la plus
exposée aux fichiers de l'utilisateur », jamais auditée. C'est la seule faille de ce
projet qui puisse abîmer autre chose que l'émulation : un programme invité qui sort du
dossier monté lit et **écrit** sur l'hôte avec les droits de l'utilisateur.

**Ce que l'audit a regardé** (1 704 lignes, méthode de celui du 2026-08-27) :

- **la provenance de CHAQUE chemin hôte.** Les huit points où le module touche vraiment
  le système de fichiers — `mkdir`, deux `fopen`, `opendir` ×2, `rename`, `access`,
  le `fopen` de Pexec — ont été remontés jusqu'à leur source. **Tous** passent par
  `createHostFileName`, donc par `clampToSandbox`. Le seul chemin dérivé
  (`dirPath` de `Fsfirst`) est retaillé sans jamais pouvoir remonter au-dessus de
  `rootLen` ;
- **les contrôles mémoire à taille variable.** `checkArea(addr, size)` délègue à
  `Bus::hostRamPtr`, qui borne à 4 Mo. Un `addr + size` peut théoriquement déborder en
  `uint32`, mais les deux appelants à taille invitée sont couverts : Fread écrête
  `size` à la taille du fichier AVANT le contrôle, et Fwrite refuse un `size` négatif
  (donc ≤ 2 Gio, sans repli). **Le débordement est rattrapé par le test de contiguïté
  de `hostRamPtr`, pas par une garde explicite** — c'est solide aujourd'hui, ça tient à
  un raisonnement plutôt qu'à une ligne de code ;
- **le bac à sable lui-même.** Il a été durci plusieurs fois, et chaque durcissement
  porte le récit de l'évasion qu'il ferme : la normalisation « / » → « \ » à l'entrée
  (« / » est un caractère INVALIDE côté GEMDOS mais LE séparateur côté hôte), et les
  deux étapes `makeAbsoluteName` puis `physicalCanon` de `clampToSandbox` (un préfixe
  existant suivi d'un suffixe inexistant ressortait avec ses « .. » intacts).

**Verdict : le module est plus solide que sa réputation.** Ce qui manquait n'était pas
la robustesse, c'était la GARDE — rien ne rejouait ces cas.

**`--gemdos-selftest`** (13 assertions, palier `fast`) monte un dossier temporaire et y
jette des noms hostiles : remontées simples et profondes, séparateurs UNIX, lettre de
lecteur, préfixe existant + suffixe inexistant, jokers, composant vide — plus deux cas
LÉGITIMES qui ne doivent pas être mutilés. Il vit dans `GemdosHd.cpp` (comme
`glueSelfTest` vit dans le Shifter) pour atteindre `createHostFileName` sans ouvrir
l'API. Rendu possible par **A33** du matin : il construit sa propre machine minimale,
et il aurait fallu jeter le `throw` mono-instance pour ça.

**Ce que la mutation a appris, et qui vaut plus que le test lui-même.** Trois essais :

| mutation | verdict |
|---|---|
| normalisation « / » → « \ » retirée | **13 OK** — `clampToSandbox` rattrape |
| `clampToSandbox` neutralisé | **13 OK** — la normalisation rattrape |
| **les deux** retirées | **3 FAIL** — `/etc/passwd` hors du bac à sable |

Donc : la défense en profondeur est **réelle et vérifiée** — deux mécanismes
indépendants suffisent chacun pour la famille « séparateurs UNIX ». Et le test garantit
la **propriété** (rien ne sort), pas les **couches** : il ne rougira pas si UNE seule
défense disparaît. C'est la bonne sémantique pour une garde de sécurité, mais il faut
l'écrire — sans quoi un lecteur croira que chaque couche est épinglée. Un test par
couche demanderait d'observer des états intermédiaires, donc de coupler le test à
l'implémentation : porté au TODO comme décision, pas comme oubli.

Restent non audités : `io/Ikbd.cpp` (1 189 l.) et la pile réseau (~1 030 l.).

## Évaluation d'architecture — regard extérieur après une journée dans le code (2026-08-28)

Consignée ici comme l'audit du 2026-08-27, et pour la même raison : un jugement qui ne
vit que dans une conversation ne sert à personne dans six mois. Appuyée sur une session
de travail réelle — neuf chantiers menés dans le `Bus`, le `Shifter`, `Cpu68k`, le
harnais, la CI et le packaging — donc sur ce qui casse et ce qui tient, pas sur une
lecture.

**Verdict.** Un cœur d'émulation d'excellente facture, entouré d'un projet qui ne s'est
pas encore décidé à être un produit. La fidélité matérielle est objectivement au-dessus
de ce qu'on voit dans un émulateur amateur ; le frontend, la gouvernance et le cycle de
release sont très en dessous du cœur. L'auto-note de 6,5/10 de la veille est **juste,
peut-être un peu sévère sur les tests, trop indulgente sur les frontends**.

### Ce qui est vraiment fort

- **La méthode.** « Hatari est la source de vérité : porter d'abord, enquêter ensuite »
  est appliqué, et produit du vérifiable. Mesuré dans la journée : NeoST et Hatari
  s'accordent **au pixel** sur trois démos migrées ET sur `spec512_bands`, un motif créé
  le jour même et entièrement déterminé par le modèle de cycle. Ça ne s'obtient pas par
  tâtonnement.
- **La documentation est PORTEUSE.** Chaque décision non triviale porte son *pourquoi*
  avec le `fichier:ligne` d'amont. C'est ce qui rend une session productive dans 40 000
  lignes inconnues. Et `check_doc_anchors` / `check_doc_claims` rendent la prose
  **exécutable** — garder ses propres chiffres de documentation contre une machine est
  rare au point d'être singulier.
- **La culture de la réfutation.** Le journal enregistre ce qui a été *démenti* (le
  paradoxe de signe BL5, le sync-driven, la migration spec512). Consigner une hypothèse
  morte vaut plus que consigner un succès : ça l'empêche de ressusciter.

### Ce qui ne va pas, par ordre de danger réel

1. **Le risque juridique est le vrai bloquant, et il n'est pas technique.** Dépôt public
   GPL-3 suivant 37 ROM Atari, 68 jeux commerciaux majoritairement crackés, 5 cartouches
   Field Service, Cubase Lite ; Pages sert la racine. Le correctif exige une réécriture
   d'historique — décision de mainteneur. Tout le travail technique tourne autour de ce
   nœud sans pouvoir le trancher.
2. **La pyramide de test est inversée.** Avant cette journée : **159 assertions de
   logique pure pour ~40 000 lignes**, tout le reste validé par comparaison d'images de
   bout en bout. Un diff pixel dit « 3 400 px faux en (112,57) » — il ne localise rien.
   Les tables de vérité d'A29 l'ont prouvé le jour même : couper le budget de tranche du
   blitter de 64 à 63 fait rougir UNE ligne nommée. 257 assertions aujourd'hui ; c'est
   mieux, ça reste mince.
3. **Le frontend est la moitié faible, et c'est là que vivent les utilisateurs.**
   `main.cpp` : **4 814 lignes, dont 2 430 pour `main()`, une boucle de ~1 500 lignes,
   82 globaux**. Couverture : une capture au boot et trois assertions d'arguments. C'est
   le plus gros passif structurel (item A9), et le seul module qu'on ne refactorise pas
   sans poser un filet d'abord.
4. **L'état global comme habitude de conception.** `Cpu68k` en avait 48, `main.cpp` en a
   82, le `Shifter` ~90 champs. Ce motif a produit le plafond mono-instance, le verrou
   hybride A16 (deux moitiés d'un même réglage avec deux défauts contradictoires, des
   semaines durant) et l'intestabilité. Un tiers corrigé le 2026-08-28 (A33) ; le reste
   est devant.
5. **La configuration par variables d'environnement a débordé du laboratoire.** 83
   verrous `NEOST_*` dans le cœur, dont **33 changent l'émulation**, dans aucun fichier
   de configuration. Inventoriés et gardés par A34 ; **pas réduits**.
6. **Trois cibles livrées, une seule validée.** Windows jamais lancé hors CI, APK jamais
   posé sur un appareil, et le Raspberry Pi — cible déclarée du mode borne — n'a **aucun
   budget temps réel mesuré**. Le banc de débit ne garde que des ratios sur le poste de
   dev.

### Deux remarques de fond

- **Le backlog croît plus vite qu'il ne se vide.** L'audit du 27 a produit 22 items ; la
  journée du 28 en a soldé 4 entièrement et avancé 5. La méthode VOIT mieux qu'elle ne
  RÉPARE — ce n'est pas un défaut, mais il faut le savoir avant de croire qu'on approche
  de la fin.
- **Risque de cérémonie.** Le palier `fast` lance désormais **17 outils**, dont quatre
  ajoutés le 2026-08-28. Chacun se justifie et chacun a attrapé quelque chose de réel ;
  la trajectoire mérite néanmoins surveillance. Un garde-fou qui ne mord jamais devient
  du bruit, puis on désarme le lot.

### Ordre recommandé

1. **Trancher le § BLOQUANT** — rien d'autre ne compte tant qu'un dépôt public distribue
   des ROM Atari (pas 3 et 4 du séquencement).
2. **Taguer** — 129 commits non tagués, dont le MegaSTE 12/12 : du travail invisible.
3. **A9, le frontend**, filet posé AVANT.
4. **Mesurer sur Pi, une fois.** Un émulateur temps réel qui n'a jamais mesuré son temps
   réel sur sa cible est un pari.

### Ce qui de tout ceci est OUVERT vit au TODO, pas ici

Cette entrée est datée : elle fige un jugement. Les points qui appellent une action ont
été versés au `TODO.md`, où ils seront relus — **A38** (garde-fous qui ne mordent plus,
17 outils au palier `fast`), **A39** (trois modules jamais audités : `GemdosHd` 1 704 l.,
`Ikbd` 1 189 l., le réseau ~1 030 l., plus le GUI jamais exercé interactivement) et un
garde-fou de plan sur le backlog qui croît plus vite qu'il ne se vide. Le journal
raconte, le TODO réclame ; confondre les deux, c'est écrire une observation qui ne sera
jamais relue.

### Limites de cette évaluation, pour qu'elle soit lisible pour ce qu'elle est

Une session, une plateforme (macOS ARM), et **le GUI n'a jamais été lancé de façon
interactive** — il est jugé sur sa forme, pas sur son usage. `GemdosHd` (1 704 lignes),
`Ikbd` et le réseau n'ont pas été audités. Et l'évaluateur a contribué à ce qu'il
évalue : quatre des chantiers du 2026-08-28 sont PARTIELS, et leurs résidus sont sa
dette autant que celle du projet.

## A10 — la couverture Spectrum 512 est RENDUE, sans une ligne de ROM Atari (2026-08-28)

Le matin, la migration EmuTOS des trois étalons `spectrum512_diapo*` était réfutée à
l'oracle : leur diapo vient d'un dossier `AUTO` qu'EmuTOS n'exécute pas jusqu'au bout.
Le jour de la purge, ils deviennent des SKIP recensés — et **toute** la couverture
« palette changée en cours de ligne » partait avec eux. Le TODO ne laissait qu'une voie :
l'étalon **généré**. La voici.

`tools/make_spec512_test.py` produit une disquette dont le **secteur de boot est
autonome** : il pose la résolution, la fréquence, la base écran, remplit la RAM écran de
l'index 1 **partout** (plan 0 à `$FFFF`, les trois autres à 0), puis martèle
`palette[1]` (`$FF8242`) avec rouge / vert / bleu séparés d'un délai fixe, en se
resynchronisant sur le compteur vidéo à chaque trame.

**Ce que ça mesure, et que rien d'autre ne mesurait sans ROM Atari** : l'écran entier
suit `palette[1]`, donc la position **horizontale** de chaque bascule de couleur dépend
du cycle exact auquel l'écriture prend effet. C'est le seul endroit du rendu où un cycle
de CPU se voit à l'œil. L'étalon exerce `recordColorWrite` (datation), `spec512Active_`
(bascule en re-rendu par ligne) et l'alignement bus des écritures palette.

**Le résultat qui compte : 0 px / 114816 contre l'oracle Hatari**, sur ROM libre
(`etos192fr`). NeoST et Hatari s'accordent au pixel sur un motif pourtant entièrement
déterminé par le modèle de cycle.

Deux choses apprises en le posant, écrites dans le générateur et dans l'entrée du
manifeste plutôt que redécouvertes :

- **La taille de la boucle compte.** Premier essai à 2 600 itérations : ~4 trames par
  passe, donc la resynchronisation ne voyait qu'une trame sur quatre et l'écran sortait
  UNIFORME (la dernière couleur écrite). La trace `NEOST_PAL_TRACE` l'a dit tout de
  suite — 98 écritures, toutes lignes 0 à 19. À ~300 cycles l'itération, 500 couvrent
  une trame PAL en laissant de quoi se resynchroniser.
- **L'image n'est PAS statique, et ce n'est pas grave.** La boucle et la trame ne sont
  pas commensurables : le motif a une période de **4 trames** (mesuré). NeoST étant
  déterministe, la trame 399 est toujours la même ; et l'oracle a sa fenêtre de scan,
  qui trouve la trame identique — une sur quatre l'est. `oracle_scan` est à 90 et non
  40 parce que le premier essai de pose a MANQUÉ la fenêtre (Hatari tire au hasard la
  position angulaire initiale de la disquette).

**Bilan** : **12 étalons pixel sur 16** survivent désormais au retrait des TOS Atari —
8 ce matin. Les trois `spectrum512_diapo*` resteront des SKIP recensés, mais plus rien
d'essentiel ne part avec eux.

## A33 — deux CPU peuvent enfin vivre dans le même processus (2026-08-28)

`Cpu68k` jetait sur une seconde instance :

    throw std::logic_error("Cpu68k supports only one live instance")

C'était le plafond que le TODO nommait : pas de test unitaire d'une `Machine`, pas
d'A/B en un processus, pas d'anneau MIDI à deux nœuds. Il n'y est plus.

**Ce qui bloquait n'était pas une difficulté technique, c'était une CONFUSION.** Le
fichier portait 48 globaux `g_*` et rien ne les distinguait. Les classer a été le
vrai travail :

- **25 sont de l'ÉTAT** — le bus, l'ordonnanceur, le cœur Moira, les broches IRQ en
  attente, les compteurs d'IPL, les breakpoints, le multiplicateur 8/16 MHz… Une
  machine émulée en a un jeu. Ils forment désormais une `struct CpuState` **possédée
  par `Cpu68k`** ;
- **23 sont de la CONFIGURATION DE PROCESSUS** lue une fois dans l'environnement (les
  verrous d'IACK, d'E-Clock, d'IPL, de créneau RAM, toutes les traces). Les rendre
  par-instance aurait été **faux**. Ils restent globaux, et `tools/env_locks.json`
  (posé par A34 le matin même) dit lesquels.

**Le détail qui fait la différence, et que seul le test a révélé.** Après le
regroupement, les méthodes de `Cpu68k` lisaient encore l'état via le pointeur
d'instance ACTIVE. Avec un seul CPU c'est le même objet — invisible. Avec deux,
`cpuA.pc()` rendait le PC de B. Les **135 accès internes** passent maintenant par
`state_`, l'état de l'objet ; ne restent en `g_cur` que les 94 accès des callbacks
Moira et des fonctions libres, qui n'ont pas de `this`.

**Prouvé, pas supposé.** `selftest_logic.cpp` construit deux `Cpu68k` sur deux `Bus`,
leur donne deux vecteurs de reset différents, et vérifie que chacun prend le sien,
qu'en faire tourner un avance SON horloge et laisse celle de l'autre intacte, et que
le PC du premier ne bouge pas pendant que le second travaille. Ce test est la raison
d'être du chantier : sans lui, A33 n'aurait rien changé d'observable.

**Ce qui reste, et c'est écrit au TODO** : le vrai parallélisme. Le modèle est « à
tour de rôle » — `g_cur` désigne l'instance active, posée à l'entrée de `run()` et de
`reset()`. Deux CPU dans DEUX THREADS demanderaient de supprimer les 94 accès
restants (contexte passé aux callbacks, ou `thread_local`). Ce n'est pas ce qu'A33
promettait, et rien ne le réclame aujourd'hui.

Méthode, parce qu'elle a compté : **pas de `sed` sur ce fichier**. Une première
tentative de renommage de masse par motif avait pris `return g_cpuMul == 1 ? …` pour
une déclaration et supprimé la ligne (détecté avant application). Le chantier a été
fait en trois passes compilées et testées séparément — regrouper, posséder, séparer
`state_` de `g_cur` — chacune avec le palier `full` vert derrière.

Débit inchangé : blitter/boot +0,4 %, mfp/boot +0,4 % — dans le bruit.
`neost-selftest` : 250 → **257 assertions**. Palier `full` vert.

## A37 — la discipline de release s'écrit, et une machine la garde (2026-08-28)

L'audit relevait trois symptômes : trois tags le même jour (0.5 → 0.5.2, le
2026-08-10), une **0.5.3 sautée sans une ligne pour le dire**, et le travail majeur
depuis le 2026-08-23 non tagué. Une seule cause : la procédure n'était écrite nulle
part, donc rien ne pouvait la vérifier.

- **[`docs/RELEASE.md`](docs/RELEASE.md)** l'écrit en sept pas — dont le troisième, celui
  qu'on saute : `NEOST_VERSION_STR` est une variable de **cache** CMake, et sans un
  `-DNEOST_VERSION_STR=x.y.z` après le bump, `--version` **ment**. Le fichier dit aussi
  ce qui BLOQUE encore une release publique (le § BLOQUANT, la signature/notarisation) et
  récapitule ce que les machines vérifient déjà.
- **`tools/check_release.py`** (palier `fast`) exige que les TROIS numéros disent la même
  chose — `CMakeLists`, « Version courante » du CHANGELOG, dernière en-tête de release —
  et refuse un numéro sauté en silence. Fil-piège vérifié en le déclenchant : bumper le
  `CMakeLists` seul donne « CMakeLists dit 0.5.5, le CHANGELOG dit Version courante 0.5.4 ».
- **Le saut de 0.5.3 est consigné**, avec ce qu'on sait et rien de plus : elle n'a jamais
  existé (le bump `dec5929` du 2026-08-23 passe de 0.5.2 à 0.5.4 — aucun tag, aucune
  entrée, aucun artefact), et **la raison n'est pas reconstituable depuis l'historique**.
  On l'écrit tel quel plutôt que d'inventer une explication plausible.

**Ce que je n'ai PAS fait, et pourquoi.** Poser le tag. Il y a 128 commits depuis la
0.5.4 et de quoi faire une belle version — mais choisir un numéro et publier est une
décision de mainteneur sur un dépôt public, pas une conséquence mécanique de ce
chantier. La procédure est prête ; la décision reste. Idem pour la signature du `.dmg`
et du `.zip`, qui attend la purge.

## A36 — `neost.cfg` sait où il habite, y compris installé dans `/usr` (2026-08-28)

`cfgPath()` valait `exeDir + "/../neost.cfg"`. Correct pour `build/neost`, pour
l'AppImage et pour le `.zip` Windows — tous **portables**, binaire et config voyagent
ensemble. Faux dès qu'on installe pour de bon : `/usr/bin/neost` cherchait sa config
dans `/usr/`, où l'utilisateur n'écrit pas. L'écriture échouait en silence — ou, pire,
réussissait pour root seulement.

La règle, dans l'ordre, et **elle ne surprend jamais l'installation portable** :

1. si `<exeDir>/../neost.cfg` **existe**, c'est lui. Arbre de dev, AppImage, zip,
   borne : rien ne change, pas de migration, pas de réglages qui « disparaissent »
   après une mise à jour ;
2. sinon, la config utilisateur si elle existe (`$XDG_CONFIG_HOME/neost/neost.cfg`,
   défaut `~/.config/neost/` ; `%APPDATA%\neost\neost.cfg` sous Windows) ;
3. sinon, on choisit où **écrire** : à côté du binaire si ce dossier est inscriptible
   (portable neuf), sinon dans la config utilisateur (installation système) ;
4. ni l'un ni l'autre (démon sans environnement) : chemin historique, et l'écriture
   échouera **en le disant** plutôt qu'en silence.

Les **profils nommés suivent la config retenue** — sinon on écrirait des profils que
la config ne retrouverait pas. Un dossier `profiles` déjà utilisé à côté du binaire
garde la priorité : on ne déplace pas les profils de quelqu'un.

**« Inscriptible » est testé en écrivant.** Regarder les bits de permission mentirait :
montage en lecture seule, ACL, conteneur, sandbox macOS. Le seul test qui ne ment pas
est l'essai, et il coûte un fichier temporaire une fois au démarrage.

**La règle est PURE et testée** (`util/ConfigPath.hpp`) : elle prend ses sondes —
existe ? inscriptible ? environnement ? — en paramètre. Le test les fournit en mémoire,
la production les branche sur le disque. C'est ce que fait déjà `hostpath::Style`, et
c'est ce qui avait permis d'attraper l'issue #37 depuis un Mac. Onze assertions, dont
les deux cas Windows et la règle XDG « une valeur RELATIVE s'ignore » (sans quoi la
config partirait dans le répertoire courant du lancement).

Au passage, une attente fausse corrigée : mon premier test exigeait des `\` sous
Windows. Il a rougi — et le code avait raison : `hostpath` normalise en `/`, « accepté
par Win32 aussi », c'est la convention interne du projet. Le test dit maintenant
pourquoi.

Palier `full` vert, et le `neost.cfg` du dépôt est intact (cas 1).

## A34 — un seul modèle d'exécution, et les 83 verrous du cœur sont enfin classés (2026-08-28)

**La branche morte-vivante est morte.** `Machine::runFrame` et `stepInstruction`
portaient DEUX modèles d'exécution : le BLOC (défaut — bloc CPU borné au prochain
événement, dispatch à la frontière) et le « piloté par l'horloge » (`do_cycles` de
WinUAE, où `sync()` dispatchait au fil de l'instruction), sous `NEOST_SYNC_DISPATCH`.
Le second n'a jamais été validé par un seul étalon.

**Mesure re-prise sur l'arbre du jour**, comme le garde-fou du plan l'exigeait :
`NEOST_SYNC_DISPATCH=1` rend le palier `fast` **ROUGE** — `blitter_timer` diverge de
**245 px** là où le modèle BLOC est à 0. À quoi s'ajoute le deadlock d'Enchanted Land
déjà consigné (boucle beam-sync jamais servie), et le fait qu'il ne corrigeait pas le
jitter qu'il promettait. Verdict sans ambiguïté : supprimé — le `else` de `runFrame`,
celui de `stepInstruction`, le `syncTo` de `NeostMoira::sync`, les deux
`g_blockDispatch` et les trois commentaires qui expliquaient « quand cette branche est
dormante ». Une branche morte-vivante dans la boucle d'exécution coûte plus qu'elle ne
garde : elle double le raisonnement de chaque lecteur et rien ne l'exerce.

**La seconde moitié de l'item : les verrous `NEOST_*`.** Le cœur en lit **83**. Les uns
changent ce que la machine ÉMULE — donc ce que valent les étalons —, les autres
n'ajoutent que de la trace. Rien ne les distinguait : ni le nom, ni un fichier, ni un
test. Avant une release, personne ne pouvait répondre à « qu'est-ce qui peut encore
changer l'émulation sans qu'aucun fichier de configuration ne le dise ? » autrement
qu'en relisant le cœur.

`tools/env_locks.json` les classe : **33 de COMPORTEMENT** (wakestate, `LINELEN`,
`RAM_SLOT`, la famille `IACK`, les décalages de calibration `VC_OFF`/`SYNC_OFF`…) et
**50 de trace**, plus une famille « retirée » qui garde la trace de
`NEOST_SYNC_DISPATCH` — un vieux script qui la pose doit savoir qu'elle ne fait plus
rien. `tools/check_env_locks.py` compare ce manifeste à ce que le code lit VRAIMENT, au
palier `fast` : une variable lue mais non classée fait rougir, une variable classée que
plus rien ne lit aussi. Fil-piège vérifié en ajoutant un `getenv` bidon dans
`Machine.cpp` — attrapé, nommé.

Et le rappel que le contrôle imprime à chaque passage, parce qu'il vaut mieux l'écrire
que le supposer : **les étalons sont mesurés avec les DÉFAUTS. Armer un verrou de
comportement invalide toute comparaison au corpus.**

Retombée directe : la priorité MFP n°3 des divergences (« `UpdateTimers` avant lecture
IPR/ISR/TBDR, 157 cycles de retard ») disait « attendre A34 ». Il n'y a plus rien à
attendre — le modèle qu'elle espérait est supprimé, et toute correction devra se faire
DANS le modèle BLOC. Le TODO le dit désormais.

Palier `full` vert.

**Bévue de la session, notée pour ne pas la refaire** : pour annuler une sonde
temporaire dans `Machine.cpp`, j'ai fait `git checkout src/core/Machine.cpp` — sur un
fichier qui portait des modifications NON COMMISES. Elles ont été effacées, et il a
fallu refaire la moitié du chantier. La bonne manière, appliquée partout ailleurs
aujourd'hui : copier le fichier dans le scratchpad AVANT la sonde, et restaurer au `cp`.

## A32 — le Shifter perd 1 586 lignes, le GLUE vidéo retrouve son nom (2026-08-28)

`Shifter.cpp` faisait **2 917 lignes** et portait six rôles. Le plus gênant n'était
même pas la taille : c'était le NOM. Un lecteur qui cherchait la machine à états des
bordures ouvrait `Glue.hpp` — et n'y trouvait qu'un **stub de 31 lignes** portant la
config mémoire du MMU, pendant que le vrai GLUE vivait anonymement au milieu du
Shifter, dans un `namespace glue` local à l'unité de compilation.

| | avant | après |
|---|---|---|
| `Shifter.cpp` (registres, palette, rasterisation) | 2 917 | **1 331** |
| `VideoGlue.cpp` (DE/HBL, bordures, Timer B) | — | 1 032 |
| `VideoCounter.cpp` (`$FF8205/07/09`, faisceau) | — | 441 |
| `VideoGlue.hpp` (masques, table de timings, wakestate) | — | 168 |
| `ShifterInternal.hpp` (outillage des trois unités) | — | 63 |

- **Le vrai GLUE a son en-tête** : `VideoGlue.hpp`. Sa table de timings ne prend plus
  un `Bus&` mais un `MachineType` — c'est cette dépendance-là qui l'empêchait de
  sortir du `.cpp`, et le GLUE vidéo n'a que faire du bus.
- **Le stub MMU a son vrai nom** : `MmuGlue.hpp` / `class MmuGlue`, avec un ⚠ en tête
  qui renvoie vers `VideoGlue.hpp`. Le nom « Glue » ne trompe plus personne.
- **Les quatre `const_cast` ont disparu.** `videoCounter()` se déclarait `const` et
  appelait `liveGlueCatchUp` — qui AVANCE la machine Glue — à travers quatre
  `const_cast<Shifter*>`. Elle n'est plus `const` : son seul appelant, `Shifter::read8`,
  ne l'est pas non plus. Un `const` qui ment coûte plus cher qu'il ne rapporte.
- Les verrous d'environnement partagés (`envFlag`, `lineLenAttrEnv`, les décalages de
  calibration, la table d'éclatement des bitplanes) passent de `static` à `inline`
  dans `ShifterInternal.hpp` : **une** définition pour trois unités, pas trois copies
  — un verrou lu deux fois avec deux résultats serait exactement le genre d'hybride
  qu'A16 a mis des semaines à débusquer.

**Ce qui N'EST PAS fait, et pourquoi ce n'est pas une paresse.** Les trois rôles
restent des MÉTHODES de `Shifter` : ils partagent son état par-ligne (`glueLines_`,
`glueLineStart_`, `liveGlue*`, `vc*`). En faire trois CLASSES demande de trancher qui
possède cet état ; une réponse bâclée y ajouterait des accesseurs croisés — le même
couplage, avec plus de cérémonie. L'item reste ouvert au TODO avec son prochain pas
écrit : extraire `VideoCounter` en objet membre (ses champs `vc*` sont les moins
partagés), mesurer, et regarder la Glue ensuite.

Aucun changement de comportement : pur déplacement de code. Palier `full` vert —
étalons pixel compris, c'est-à-dire tout ce que ce fichier décide.

## A31 — le dispatch MMIO devient une table, et sa cohérence est PROUVÉE (2026-08-28)

Ajouter une puce demandait de toucher six endroits, dont **deux chaînes de `if` de
~110 lignes** (`Bus::mmioRead8` / `Bus::mmioWrite8`) à ordre sémantique implicite :
rien ne disait où était « le bon endroit », ni ce qui dépendait de la position.

Les deux fonctions font désormais **5 et 4 lignes**. Chaque branche est devenue un
handler nommé (`rdMfp`/`wrMfp`, `rdStePads`/`wrStePads`…) dont le corps est déplacé
TEL QUEL — mêmes wait states, mêmes `updateIpl()`, mêmes octets « void », même ordre
d'effets — et une **table de 14 plages** les relie : `{lo, hi, porte, read, write,
nom}`. La porte (`claims`) porte la puce câblée, le modèle de machine, et la parité
quand elle compte : le RTC RP5C15 ne décode que les adresses IMPAIRES, une adresse
paire de sa plage doit retomber sur la glue — c'est dans la table, plus dans un `if`.

**Et surtout, l'ordre n'est plus signifiant : il est PROUVÉ qu'il ne l'est pas.**
`Bus::mmioTableDisjoint()` vérifie que deux plages ne se recouvrent jamais, et
`selftest_logic.cpp` l'appelle à chaque palier `fast` — sans machine ni ROM. Une
ligne ajoutée qui chevaucherait une autre fait rougir le palier **en nommant les deux
coupables** (vérifié en le déclenchant : élargir le SCU sur `$FF8E21` donne
« chevauchement : scu / mste-cache »). C'est le cœur du sujet : une table dont les
plages se recouvrent ne vaut pas mieux qu'une chaîne de `if`, elle cache juste
l'ordre ailleurs.

**Ce que la table N'A PAS absorbé, et pourquoi.** Le port cartouche
(`$FA0000-$FBFFFF`, dans `read8Slow`) est justement l'exemple que le TODO citait —
« l'ISP1160 doit précéder la NE2000 ». Or ce n'est PAS un premier-match : les deux
puces voient le MÊME accès (la fenêtre LSB est aussi le registre CR de la NE2000), et
faute de schéma on ne suppose aucun décodage exclusif. Le forcer dans une table de
premier-match l'aurait décrit **faux**. Il reste donc une chaîne, avec sa raison
écrite au-dessus.

**Coût mesuré** (la table est parcourue linéairement, comme la chaîne l'était) :
boot 1690 → 1676 tr/s, blitter 1480 → 1499, poll IPRA du MFP 1256 → 1296. ±3 % dans
les deux sens, donc dans le bruit — et la charge qui martèle le plus le MMIO (le poll
IPRA) est la plus rapide des deux mesures.

Palier `full` vert : étalons pixel, MegaSTE 12/12, verdicts série, cycle-bench.

## A28 — le servo audio et la cadence rentrent dans le cœur (2026-08-28)

Le filtre proportionnel d'asservissement audio — même constante `/256`, même clamp
±8, même rampe anti-clic — existait en **trois copies** : GUI natif
(`audio/Audio.cpp`), web (`main_web.cpp`), Android (`main_android.cpp`). La boucle de
rattrapage de cadence en **deux** (web et Android, identiques au caractère près). Et
`kCpuHz` était déclarée **quatre** fois — cinq en comptant `Mt32Synth` et
`MidiOutHost`, plus trois `CPU_HZ` entiers dans les puces.

Le précédent est au journal : la chaîne de mixage vivait elle aussi en clair dans le
GUI, recopiée ailleurs, et **la copie web avait dérivé** sur l'ancienne API mono non
horodatée — des échantillons inaudibles dans le navigateur, sans que personne ne le
voie. `AudioMix` a réglé ce cas-là ; `core/Pacing.hpp` règle les deux qui restaient.

- **`neost::pacing::AudioPacer`** : report fractionnaire, servo proportionnel borné,
  rampe anti-clic du volume maître, clamp de sortie. Les trois frontends l'appellent.
- **`neost::pacing::FramePacer`** : la boucle de rattrapage bornée à 4 trames, avec
  `resync()` pour les pauses (menu borne, onglet en arrière-plan). Web et Android.
- **UNE seule définition de l'horloge** : `kCpuHz` (double) et `kCpuHzInt` (entier,
  pour les puces qui comptent des cycles émulés). Le littéral `8021248` n'apparaît
  plus qu'**une fois** dans tout l'arbre.

**Et pour la première fois, ce code est TESTÉ.** Ni le headless ni les étalons pixel
ne le traversent : c'est lui qui décide combien d'échantillons sortent par trame, et
une erreur y donne un son qui dérive ou qui hache — jamais un pixel de différence.
La table de vérité (`selftest_logic.cpp`, dans la lignée d'A29) pose : la cadence suit
la GÉOMÉTRIE (19,98 / 16,66 / 13,99 ms pour PAL / NTSC / mono, pas un 20 ms figé) ;
le report fractionnaire tient 1 000 trames **à ±1 échantillon** là où une troncature
en perdrait ~989 ; le servo est du bon SIGNE (file vide ⇒ produire plus), borné à ±8,
et vaut +1 pour 256 trames d'écart ; la rampe de volume ARRIVE à la cible ; le
rattrapage plafonne à 4 trames et **abandonne** le retard au lieu de le traîner.

Les trois mutations essayées font rougir la bonne ligne : clamp 8 → 16 (3 assertions),
signe du servo inversé (4), report fractionnaire retiré (1 — exactement celle qui le
mesure).

Vérification des frontends que le poste de dev ne bâtit pas d'habitude : le **web est
compilé** (emcc 6.0.8) pour valider les modifications. L'**Android** ne l'est pas — le
NDK n'est pas installé ici ; ses éditions sont symétriques de celles du web (mêmes
appels, mêmes noms) et c'est la CI qui les compile.

Palier `full` vert.

## A30 — les parseurs d'images disquette passent au fuzzer, et la CI ne bâtissait pas ce qu'elle lance (2026-08-28)

`decodeMsa`, `decodeDim` et `StxImage::parse` sont les seules fonctions du projet qui
digèrent un fichier venu de l'EXTÉRIEUR — téléchargé, tronqué, corrompu, ou forgé. Leur
bornage manuel est excellent (il corrige même une lecture hors bornes présente dans
Hatari) — mais rien ne le PROUVAIT ni ne le GARDAIT.

**Pas libFuzzer, et c'est mesuré, pas supposé** : le clang d'Apple ne livre pas
`libclang_rt.fuzzer_osx.a`, `-fsanitize=fuzzer` **ne lie pas** sur la plateforme de
développement du projet. Un harnais libFuzzer y serait du code que personne n'exécute.
`tests/fuzz_diskimage.cpp` est donc un driver **déterministe** : PRNG xorshift semé,
corpus de départ construit en mémoire (.msa/.dim/.st valides, en-tête STX, et les cas
dégénérés), sept mutations orientées TAILLES et CHAMPS DE LONGUEUR — c'est là que les
parseurs sortent des bornes, pas dans les données. `--seed` rejoue un cas à l'identique.
La cible `fuzzOne` est écrite sans état ni sortie : brancher libFuzzer dessus, le jour où
la machine l'a, tient en trois lignes (recette dans l'en-tête du fichier).

**Résultat : 500 000 itérations sur 4 graines, plus 50 000 sous ASan+UBSan — zéro
violation.** Le bornage tient. C'était le but : le prouver.

**Ce que le harnais vaut vraiment, mesuré par mutation.** En build NORMAL il ne voit
presque rien : retirer le plafond RLE, puis retirer `if (p + len > raw.size()) return
false;`, 20 000 itérations chacune — **aucune des deux n'est attrapée**. Sous ASan, la
seconde donne un `heap-buffer-overflow` en 2 s. C'est écrit tel quel dans `DEV.md` : au
palier `fast` ce harnais est un test de FUMÉE ; c'est le job `sanitizers` de la CI (qui
lance `--tier fast`) qui le rend mordant. Un garde-fou dont on surestime la portée est un
garde-fou qu'on croit avoir.

**Deux trouvailles au passage, dont une qui n'a rien à voir avec le fuzzing.**

1. *Une façade qui « nettoie » cache ce qu'on cherche.* Ma première version de
   `diskimg::decodeContainer` faisait `out.clear()` entre l'essai MSA et l'essai DIM. Or
   `Fdc::loadImage` n'en fait pas. Avec le clear, la mutation « plafond RLE retiré »
   passait inaperçue ; sans lui, le harnais a immédiatement montré que **le contrat
   « refus ⇒ tampon vide » est FAUX** — `decodeMsa` peut refuser en laissant des pistes
   décodées derrière lui (270 violations sur 5 000 itérations quand on l'exige). Sans
   conséquence aujourd'hui (l'appelant repart de `raw` sur ce chemin), mais c'est une
   dépendance IMPLICITE : elle est désormais écrite, dans le harnais et dans la façade.
2. *La CI ne bâtissait pas les binaires qu'elle lance.* Les quatre jobs qui appellent
   `run_all.py` compilaient `--target neost-headless neost-selftest`. Or A20 (la veille) a
   ajouté `neost-stx-test` au palier `fast` ET à la liste des binaires requis : sans lui,
   `run_all.py` sort en **2 « Build requis » avant le moindre test**. Vérifié en local en
   retirant le binaire. Les quatre `--target` sont corrigés — et surtout la liste des
   binaires requis n'est plus écrite à la main : `run_all.py` la **déduit des commandes
   qu'il est sur le point de lancer**, donc elle ne peut plus diverger.

Au passage, l'avertissement « .msa: RLE run too long » n'est plus émis qu'**une fois par
décodage** (il sortait par piste : 86 lignes pour un fichier), et `NEOST_QUIET_PARSERS=1`
le coupe — c'est ce que pose le harnais, plutôt que de rediriger `stderr` : on ne met
jamais en sourdine le flux où un sanitizer écrit son rapport.

Palier `full` vert.

## A29 — le blitter, le son DMA et le FDC ont enfin une table de vérité (2026-08-28)

Le plan le disait depuis l'audit : avant tout chantier structurel, **poser le filet**. Il
manquait un étage. `selftest_logic.cpp` savait déjà instancier une puce NUE avec un
`Scheduler` (YM2149, MFP+ACIA, RTC) ; au-dessus, il n'y avait plus que le pixel. Résultat :
une régression du blitter se présentait comme « 3 400 px divergents à (112,57) » et
l'enquête commençait.

Trois tables ajoutées, **~50 assertions**, sans machine ni ROM — juste un `Bus` de 512 Ko,
un `Scheduler`, et la puce :

- **Blitter** : copie et incréments, masques de fin (em1 au premier mot, em3 au dernier),
  les quatre coins de la table HOP/LOP (uns, zéro, S, S XOR D, S AND D), le multi-lignes,
  et surtout **la tranche non-hog** — 64 accès bus, donc 32 mots avec masques pleins :
  après la première tranche le mot 31 est copié, le 32 ne l'est pas, BUSY tient encore.
- **Son DMA STE** : masques d'adresse (22 bits utiles, adresse paire), `$FF8900` qui est un
  registre MOT (l'octet pair relit 0, pas $FF), masque du contrôle et du mode, et le
  compteur VIVANT `$FF8909/0B/0D` — celui-là même que la divergence Hatari encore ouverte
  sur la quantification HBL du refill FIFO fait poller, et qui n'était couvert par RIEN
  (un étalon pixel ne voit pas le son, un dump WAV ne dit pas où le pointeur en est).
- **FDC / DMA disquette** : adresse DMA relisible, paire, bornée par la taille RAM ; le
  compteur de secteurs écrit via `$FF8604` sous SCREG **sans** devenir rémanent ; les bits
  3-7 du statut DMA qui rejouent le dernier `$FF8604` ; et la polarité des trois entrées du
  WD1772 quand **aucun lecteur n'est sélectionné** (TR00/INDEX/WPRT sont EFFACÉS, pas
  forcés).

**Chaque table a été vérifiée par MUTATION**, parce qu'un test qui n'a jamais échoué ne
prouve rien : `kNonHogBusBlitter` 64 → 63 fait rougir « mot 31 copié (32ᵉ mot) » ; retirer
le masque `$3F` du pointeur son fait rougir « octet haut masqué à $3F » ; inverser
`updateStr(TR00|INDEX|WPRT, 0)` fait rougir les trois lignes de polarité. C'est ce que
donne l'étage manquant : un `fichier:ligne` au lieu d'une enquête.

**Deux pièges rencontrés, consignés dans `DEV.md`.** (1) Écrire une de ces tables demande
de câbler ce que `Machine` câble — le callback `Scheduler::BLITTER`/`FDC`. Sans lui
l'échéance est POSÉE mais jamais servie : le blit non-hog ne démarre pas et le test mesure
du vide. C'est l'erreur que j'ai commise en premier, et elle passait pour un « 0 FAIL ».
(2) Première version du test FDC : trois assertions VERTES qui n'atteignaient pas la
branche qu'elles prétendaient couvrir (le statut valait 0 parce que `statusTypeI_` était
faux). La mutation de polarité ne les faisait PAS rougir — c'est elle qui l'a révélé. Un
test vert qui n'exerce rien est pire qu'un test absent : il rassure.

`neost-selftest` passe de 159 à **209 assertions**, palier `fast`, coût inchangé à la
milliseconde près.

Palier `full` vert.

## A14 — `--disk-ro` : une campagne de test ne modifie plus les images du dépôt (2026-08-28)

Le symptôme était consigné depuis des semaines : deux images **suivies par git** modifiées
dans l'arbre de travail par des runs — Eliminator le 2026-08-25, `disks/diskA.st` par le
test F du diagnostic le 2026-08-27, restaurées à la main les deux fois. NeoST persiste
chaque secteur écrit **au fil de l'eau** (write-through), ce qui est le bon comportement
pour un utilisateur — une coupure ne perd pas la sauvegarde du jeu — et un piège pour une
campagne de test. Et `disks/etalons/` compte **13 images suivies** : une écriture invitée
sur l'une d'elles ferait dériver la donnée d'entrée d'un étalon sans qu'une seule ligne de
code ait bougé.

**`--disk-ro` protège le FICHIER, pas la disquette.** Les écritures continuent d'aller dans
l'image en RAM — le programme invité relit ce qu'il a écrit, rien ne change pour lui — et
seul le write-through est coupé (`Fdc::writeBack` pour `.st`/`.msa`/`.dim`, `Fdc::stxPersist`
pour l'overlay `.wd1772` d'une STX). Une protection en écriture (`dk.writeProtect`), elle,
changerait ce que le programme OBSERVE, donc la mesure : c'est exactement ce qu'il ne
fallait pas faire.

**La preuve, sur le seul programme de la pyramide qui formate vraiment une disquette.** Le
test F du diagnostic MegaSTE écrit sur A *et* B. Sans l'option, les deux fichiers changent
de md5. Avec, ils sont **intacts** — et le dump série est **byte-identique** à celui du run
qui écrivait : 11 Pass, 0 Fail, « Q Tests Completed », « No VME board ». La machine invitée
n'a rien vu. Contre-épreuve à l'échelle du corpus : les 23 étalons passent avec l'option,
tous à 0 px.

**Deux garde-fous, et le vrai est dans `full`.** `run_megaste_diag.py` passe désormais
`--disk-ro` et **échoue** si une image sacrificielle a bougé d'un octet (vérifié en le
déclenchant : option retirée → « ÉCHEC A14 : sacA.st a été MODIFIÉ », code 1). Les copies
sacrificielles restent : ceinture ET bretelles, elles couvrent les chemins d'écriture hôte
que `--disk-ro` ne couvre pas. `run_etalons.py` passe l'option sur toutes ses captures. Au
palier `fast`, `check_headless_options.py` ne vérifie que l'existence et l'annonce de
l'option — et le dit : aucun programme invité de ce palier n'écrit sur disquette, la preuve
de bout en bout ne peut pas y vivre.

⚠ Hors périmètre, assumé et écrit : les images **ACSI** (`--acsi`, `--sd1/2`) et le disque
**GEMDOS** (`--gemdos`) écrivent toujours sur l'hôte. Les protéger demanderait un overlay
copie-sur-écriture, pas un interrupteur.

Palier `full` vert.

## A16b — le segfault du chantier V3 : un invariant rompu par `replayGlue`, pas un défaut du selftest (2026-08-28)

`NEOST_LINELEN_ATTR=1` faisait SEGFAULTER `--glue-selftest` — sans une ligne de sortie,
code 139. Le TODO portait l'hypothèse : « probable : `glueLineStart_` vide/désynchronisé
dans le selftest ». Elle était juste, et le sanitizer la confirme **à la ligne près** :

    runtime error: reference binding to null pointer of type 'long long'
    AddressSanitizer: SEGV on unknown address 0x000000000000 (READ)
      #0 Shifter::liveGlueCatchUp(int)   Shifter.cpp:537
      #1 Shifter::liveLineDisplayed(int) Shifter.cpp:1259
      #2 Shifter::glueSelfTest()         Shifter.cpp:2219

**Mais le défaut n'était pas dans le selftest.** `glueLineStart_` — l'échelle des débuts
de ligne réels, le cœur du canal V3 — doit TOUJOURS avoir la taille de `glueLines_` :
`liveGlueCatchUp` l'indexe sans garde, en s'appuyant dessus, et `Shifter::serialize`
revalide déjà l'invariant au chargement d'un save-state. Or **seul `beginFrame`** le
tenait. `replayGlue`, qui redimensionne `glueLines_` aussi, le rompait :

- le glue-selftest appelle `replayGlue()` SANS `beginFrame()` — `glueLineStart_` restait
  **vide**, `data()` valait `nullptr`, et le premier `liveLineDisplayed()` du test 4bis
  déréférençait zéro. Crash garanti ;
- et **en production**, silencieusement : le commentaire de `serialize` admet lui-même que
  `replayGlue()` peut redimensionner `glueLines_` en cours de trame (lpf changé) — dans ce
  cas `glueLineStart_` restait plus COURT et les lectures live sortaient du tas. Un bug de
  mémoire, pas une bizarrerie d'auto-test.

Correctif d'une ligne utile dans `replayGlue` : `resize` (pas `assign` — les débuts de
ligne calculés par le passage LIVE de la trame en cours sont conservés, seules les lignes
nouvelles sont mises à zéro). Vérifié sous ASan+UBSan : plus une seule erreur, ni sur
l'auto-test ni sur 300 trames de No Cooper et Cuddly Demos avec le verrou armé.

**Le garde-fou : `glue_selftest_attr`.** Un chemin opt-in que personne n'exécute pourrit —
celui-ci segfautait depuis des semaines sans qu'aucun palier ne PUISSE le voir, puisque
tous tournent avec le verrou à OFF. Le manifeste accepte désormais un champ `env` pour
rejouer un auto-test sous un verrou d'émulation ; la nouvelle entrée rejoue les 39
assertions de la machine Glue avec `NEOST_LINELEN_ATTR=1`, pour ~0,1 s. Fil-piège vérifié
en le déclenchant : correctif retiré, rebuild, le runner rend
« ÉCHEC glue_selftest_attr (exit -11) » et sort 1.

**Ce que ça ne prouve PAS.** Le palier `full` est vert *avec le verrou armé* — 23 étalons,
tous à 0 px. C'est une **non-régression du canal**, pas un feu vert : **aucun étalon
n'exerce la géométrie mi-trame 50↔60 Hz** que V3 vise (priorité n°1 des divergences).
Promouvoir le verrou sur cette base serait exactement le pari que le § « Garde-fous du
plan » interdit. Le prochain pas réel est un étalon qui bascule la fréquence EN COURS DE
TRAME — généré ou calé à l'oracle.

Palier `full` vert, avec et sans le verrou.

**Trouvé en validant ce correctif — le banc de débit criait au loup.** Un `--tier full`
de contrôle a rendu `blitter/boot = 0,604 (réf 0,888, −32,0 %)` → ÉCHEC ; deux passages
isolés du MÊME binaire, aussitôt après, donnaient **−2,4 %** et **−6,8 %**, et le palier
relancé au repos **−2,6 %**. La machine bâtissait un oracle Hatari en parallèle. Le banc
mesure ses charges SÉQUENTIELLEMENT : une bouffée de charge qui couvre une charge mais pas
l'étalon de vitesse fausse le ratio, et le « meilleur de REPS » n'y peut rien si la
bouffée dure plus longtemps que les REPS. Le message d'échec disait déjà « relancer avant
de conclure » — il le disait à l'humain, et la CI, elle, rougissait. `run_perfbench.py`
fait désormais une **seconde passe complète avant de rougir** et n'échoue que sur les
ratios hors tolérance **aux deux passages** ; le surcoût n'est payé que dans le cas qui
allait échouer, et un vrai surcoût de chemin franchit les deux. C'est la leçon du
2026-08-25 sur les grandeurs dépendantes de la charge, appliquée à l'outil qui la mesure —
un garde-fou qui crie au loup finit désarmé.

## Tout ce qu'on livre est nommé, avec sa licence — et GLFW ne l'était nulle part (purge pas 5, 2026-08-28)

Cinquième pas du séquencement de la purge (§ BLOQUANT du `TODO.md`). L'item demandait de
compléter le tableau des composants tiers du README avec libmt32emu, stb_image, libslirp,
SDL2 et les TOS Atari. En le faisant, il en manquait un **sixième que personne n'avait
listé** : **GLFW**. Il est compilé en statique dans le binaire macOS (tag 3.4, bâti par
`packaging/macos/package_macos.sh`) et Windows (MinGW-w64), et embarqué en `.so` dans
l'AppImage — donc présent dans TOUS les paquets de bureau — et il n'apparaissait ni au
README, ni dans le `THIRD-PARTY.txt` qui accompagne chaque paquet.

**Les deux documents sont désormais complets et cohérents** : Moira, Dear ImGui,
miniaudio, **GLFW**, **libmt32emu 2.8.3** (LGPL 2.1+, *lié statiquement*), **stb_image
v2.30**, **SDL2 2.30.9** (paquet Android), EmuTOS, DejaVu / Font Awesome, plus la mention
⚠ des TOS Atari que les paquets de bureau redistribuent encore par défaut. La note LGPL
est explicite sur ce qu'elle impose ici : le lien statique est permis tant que le
destinataire peut recompiler contre une version modifiée — la GPL 3 du dépôt, qui publie
cette copie de Munt inchangée, y suffit. Au passage, le `THIRD-PARTY.txt` livré perd ses
deux appendices FRANÇAIS ajoutés après coup à un document anglais : ils rentrent dans le
corps du texte.

**Une licence corrigée** : le `CMakeLists.txt` annonçait « libslirp (LGPL 2.1+) ». Le
fichier `LICENSE` de la bibliothèque installée dit **BSD-3-Clause** (Danny Gasparovski,
1995-96). Corrigé — et le README précise ce que la CI disait déjà en commentaire :
libslirp est une dépendance de compilation **optionnelle et NON LIVRÉE**, les builds de
release sont faits sans elle.

**Le verrou : `tools/check_licenses.py`, au palier `fast`.** Huit jobs de CI vérifiaient
déjà que les *fichiers* de licence accompagnent chaque paquet ; aucun ne lisait leur
*contenu*, et c'est exactement là que l'omission se loge. Le contrôle dresse la liste des
composants livrés depuis les fichiers de build (`extern/<nom>` cité par le CMakeLists ou
par le paquet Android — `extern/hatari`, ni compilé ni redistribué, est explicitement
hors liste) et exige que chacun soit nommé, **avec une licence**, dans le README ET dans
le `THIRD-PARTY.txt`. Il cherche la licence sur TOUTES les lignes qui nomment le
composant, pas sur la première : la première version n'en regardait qu'une et criait à
tort sur Moira et GLFW, cités plus haut en prose — un contrôle qui crie à tort finit
désarmé. Fil-piège vérifié en le déclenchant (ligne GLFW retirée du README → sortie 1 avec
le motif exact). Ce qu'il ne sait pas voir, et qui reste à relire à la main : les
bibliothèques que `linuxdeploy` embarque toute seule dans l'AppImage.

Palier `full` vert. Il reste au séquencement les pas **3** (la purge et la réécriture
d'historique) et **4** (basculer le défaut des paquets sur `NEOST_PACKAGE_NO_ATARI_TOS=1`)
— les deux sont des décisions de mainteneur, pas du travail technique en attente.

## Le palier `fast` ne dépend plus d'aucun fichier propriétaire, et un SKIP ne peut plus se cacher dans le vert (purge pas 2, 2026-08-28)

Deuxième pas du séquencement de la purge (§ BLOQUANT du `TODO.md`), dans la foulée
d'A10. Trois sous-suites du palier `fast` seraient tombées en **rouge dur** le jour où
les TOS Atari quittent le dépôt — la politique « ROM absente = SKIP recensé » de
`run_etalons.py` ne les couvrait pas. C'est réglé de deux façons, selon ce que chacune
peut faire.

**Deux migrent, et ça ne change rien à ce qu'elles mesurent.** `run_selftests.py`
(`diag_cart`) et `run_cyclebench.py` passent de `tos102uk` à `etos192fr`. Leurs deux
programmes prennent la main **avant le TOS** — cartouche diagnostic $FA52235F pour l'un,
cartouche bench pour l'autre — donc la ROM ne sert qu'à construire la machine. Vérifié
plutôt que supposé : le dump série de `diag_cart` est **identique octet pour octet** sous
`tos102uk` (50 Hz), `etos192fr` (50 Hz) et `etos192us` (60 Hz) — 4 verdicts PASS, mêmes
lignes ; et le golden `tests/reference/cyclebench.json`, posé sous `tos102uk`, **passe tel
quel** sous EmuTOS avec sa tolérance de 0 cycle (il n'a PAS été régénéré — c'est ce qui
fait la preuve). `etos192fr` a été retenu pour avoir la même fréquence de balayage que la
ROM qu'il remplace : la bascule ne change rien, même hors des chemins exercés.

**Une ne peut pas, et on dit pourquoi.** `run_midi_sequencer.py` dépend de DEUX fichiers
non redistribuables : le TOS 1.04 FR et **Cubase Lite** (Steinberg, 33 fichiers suivis par
git — désormais listés dans le tableau du § BLOQUANT, chiffre gardé par
`check_doc_claims.py`). Migrer la ROM ne suffirait pas, et ne marcherait pas : le scénario
repose sur l'auto-lancement `#Z` de `DESKTOP.INF` (« Install Application » du TOS 1.04),
qu'EmuTOS n'honore pas — il lit `EMUDESK.INF`. Mesuré : sous `etos192fr`, C: est bien
monté, mais Cubase ne démarre pas et **0 octet MIDI** sort. Elle applique donc la
politique de SKIP recensé.

**Convention posée : le code de sortie 77 = « sauté, recensé ».** Ni 0 ni 1.
`run_selftests.py`, `run_midi_sequencer.py` et `run_megaste_diag.py` le rendent quand il
leur manque une donnée non redistribuable, et `run_all.py` le distingue : le bilan liste
les étapes qui n'ont RIEN vérifié et se termine par « TOUS LES PALIERS OK — **COUVERTURE
AMPUTÉE** ». Avant, ces suites sortaient 0 : le SKIP du diagnostic MegaSTE, par exemple,
s'imprimait au milieu de centaines de lignes puis disparaissait sous un « TOUS LES PALIERS
OK » plein. C'est le « vert creux » de l'audit, appliqué à l'orchestrateur lui-même. Les
deux chemins de SKIP ont été exercés pour de vrai (ROM pointée sur un fichier inexistant),
seuls puis ensemble : sortie 77, bilan amputé, code de retour global 0.

**Et un verrou pour que ça ne revienne pas.** `run_all.py` relit, au démarrage, le CODE
des outils que le palier `fast` lance (commentaires ignorés — ils racontent l'histoire,
ils n'ouvrent pas de fichier) et les ROM passées en argument : un `roms/tos*.img` codé en
dur fait sortir 2 avec le `fichier:ligne`. Un outil garde le droit de nommer un fichier
propriétaire s'il porte la constante `EXIT_SKIPPED` — c'est-à-dire s'il sait s'en passer.
Fil-piège vérifié en le déclenchant : remettre `tos102uk` dans `run_cyclebench.py` est
attrapé à la ligne près. Ce couplage codé en dur est exactement ce qui rendait le nettoyage
juridique « impossible sans casser la CI » ; il ne peut plus se reformer en silence.

Palier `full` vert (1 min 40).

## Trois démos étalons quittent les ROM Atari — A10 à moitié soldé, l'autre moitié réfutée (2026-08-28)

Le § BLOQUANT du `TODO.md` fait d'**A10** le premier pas de la purge : tant que le filet
pixel dépend de ROM propriétaires, purger ampute le filet. Sept étalons étaient concernés.

**Trois migrés sur `etos192fr` (EmuTOS 192 Ko, PAL), référence commise INCHANGÉE :**
`cuddly_demos` (`--frames` 3500 → 3655), `nocooper` (6802 → 6932), `nocooper_greetings`
(29500 → 29700). Contrairement aux 4 étalons à disque généré migrés le 2026-08-19, ces
démos ne bootent PAS d'un secteur autonome (celui de `nocooper.msa`/`cuddly_demos.msa` est
chargé par le TOS) — mais l'image n'en dépend pas davantage : **seule la durée du boot
change**, donc la numérotation des trames. Ce n'est pas supposé, c'est mesuré : la capture
NeoST sous EmuTOS est **byte-identique** (0 px / 114816, crop `buffer_noled`) à la
référence oracle Hatari posée sous TOS 1.02. Contre-épreuve à l'oracle sur `cuddly_demos` :
Hatari + EmuTOS rend la même image (trame Hatari 3720 ↔ trame NeoST 3654 de ce run) —
NeoST et Hatari restent byte-exacts sur cette démo, ROM libre comprise. Et
`nocooper_greetings` retombe aussi à 0 px de l'oracle ARCHIVÉ (`nocooper_greetings_oracle.png`),
pas seulement de sa self-capture.

**Méthode du recalage** (à réutiliser, elle a coûté la moitié du chantier) : balayer les
trames à **pas 1** et retenir CELLE qui est à 0 px, **jamais la moins pire**. Sur ces
écrans animés le voisinage n'est pas une pente douce : la trame suivante est déjà à
7 548 px (`cuddly_demos`) ou 19 069 px (`nocooper`). Un balayage à pas 20 — le premier
essai — ne voyait qu'un plateau à ~15 % et concluait à tort « la démo ne rend pas pareil ».
Corollaire : `--frames N --screenshot` capture la trame **N-1** de `--shot-every`
(convention déjà présente dans le manifeste : `frames: 1200, frame: 1199`) ; le vérifier
d'abord évite de chercher un décalage qui n'existe pas.

**Les 3 étalons Spectrum 512 ne migreront PAS — réfuté à l'oracle, ne pas retenter.** La
cause est enfin nommée : `spectrum_512_auto_diapo.st` **n'a pas de secteur de boot
exécutable** (somme des 256 mots = $FB35, pas $1234) ; la diapo est lancée par le dossier
`AUTO` (`SYNC.PRG` + `SPSLIDE8.PRG`). Sous EmuTOS le programme AUTO démarre (écran noir,
glyphes rouges illisibles vers la trame 350) puis abandonne, et le bureau GEM apparaît —
écran figé de la trame 600 à la fin. Réflexe « vérifier la ROM avant de déclarer un bug »
appliqué : **Hatari + `etos192fr` rend le même bureau** (22 px d'écart, tous dans la bande
de la LED disquette d'Hatari — donc le bureau EmuTOS de NeoST est lui-même byte-exact vs
l'oracle, mesure incidente). `etos256fr` échoue identiquement. `docs/TEST_SOFTWARE.md`
l'affirmait déjà depuis le 2026-08-19 sans la preuve ni la cause ; le `TODO.md`, lui,
comptait encore ces trois-là parmi les migrables.

**Bilan chiffré** (recompté, gardé par `check_doc_claims.py`) : **11 étalons pixel sur 15**
survivent au retrait des TOS Atari, contre 8 la veille. Reste `union_demo` (disquette
absente du dépôt, donc non testable) et les 3 spec512, qui deviendront des SKIP recensés
le jour de la purge — à assumer, ou à racheter par un étalon spec512 **généré** (secteur de
boot autonome écrivant la palette en cours de ligne, esprit `make_overscan_test.py`).

**LE PRIX, mesuré et assumé** — « justesse validée, coût ignoré » est déjà au journal
comme erreur de méthode : le palier pixel passe de **46 s à 50 s** de mur d'horloge
(`run_etalons.py` complet, 2 runs de chaque côté, poste au repos, A/B sur le même arbre :
46,2 / 46,5 s → 50,0 / 50,3 s). Le mur reste `nocooper_greetings`, qui passe de **41,3 à
45,1 s** — dont 0,7 % viennent des 200 trames ajoutées, le reste (~8 %) du fait que NeoST
émule cette démo un peu plus lentement sous EmuTOS que sous TOS 1.02, sans qu'un pixel
change. 4 secondes pour un filet qui survit à la purge : c'est acheté.

⚠ **Et la mesure a d'abord menti** — la leçon du 2026-08-25 (« un seuil absolu sur une
grandeur dépendante de la CHARGE ») s'est re-jouée ici en direct : les premiers relevés,
pris pendant que la machine bâtissait un oracle Hatari et convertissait 500 PNG, donnaient
« 46 s → 90 s » et « 43 s → 63 s », soit un surcoût **onze fois** trop gros. Le chiffre
n'est devenu vrai qu'une fois les deux côtés mesurés au repos, en alternance, deux fois
chacun. Une mesure de durée sans description de la charge n'est pas une mesure.

**Bonus NON acquis, et c'est écrit** : raccourcir `nocooper_greetings` (il borne à lui seul
le mur du palier pixel). Trois tentatives mesurées : espaces resserrés à 2 000 puis à 600
trames d'intervalle → l'écran greetings n'est **jamais** atteint (au mieux 24 508 px) ;
aucun espace du tout → pas davantage (la démo plafonne sur un autre écran). Et décaler les
5 espaces de +141 trames ne change RIEN à l'arrivée (greetings toujours à 29 610) : la
durée de la dernière partie ne dépend pas d'eux. La démo joue ses parties à son rythme
(l'écran change sans touche aux trames 2 000, 2 800, 3 700…) et un espace anticipé n'est
pas pris. Trancher demande de savoir QUAND la démo relit le clavier ; re-tirer un
calendrier au hasard ne tranchera rien.

## Second ménage du TODO : le journal de la journée part d'ici, le TODO redevient une liste d'OUVERT (2026-08-27)

La journée d'audit + P1 + P2 avait redéposé dans le TODO ce qu'elle venait d'y
enlever : blocs « ✅ SOLDÉ » (P1/A16-A24, A25-A27), récit du filet GUI dans A9,
narratifs de clôture beam-sync dans deux sections. Tout le clos est ICI (entrées
datées ci-dessous) ; le TODO repasse de 411 à 363 lignes et ne porte plus que :

- le § BLOQUANT RELEASE (décision de mainteneur, séquencement en 5 pas) ;
- les items ouverts **A3, A9-A15, A16b, A28-A37** — la numérotation A*n* reste
  continue, LES TROUS SONT DU TRAVAIL FAIT (le TODO le dit désormais en une ligne
  au lieu de re-raconter chaque clôture) ;
- le catalogue (1 bug + 2 suivis), les divergences restantes (§ Divergences et
  § Précision cycle FUSIONNÉS — les deux ne faisaient plus que pointer l'un vers
  l'autre autour du même restant), et la roadmap par sous-système élaguée
  (dates de contexte devenues inutiles retirées, « la pyramide est en place »
  remplacé par un renvoi CLAUDE.md/DEV.md pour ne pas dupliquer des chiffres
  qui bougent).

## La boucle rapide voit des pixels, le palier pixel tourne en parallèle (A27, 2026-08-27)

- **`run_etalons.py --jobs`** (défaut auto = min(4, cpus)) : les étalons machine sont
  indépendants — chacun tourne dans son processus, sortie bufferisée et rejouée dans
  l'ordre du manifeste, listes de SKIP re-fusionnées (le parallélisme ne doit pas
  avaler un recensement). Séquentiel forcé avec `--oracle`/`--update-ref` ; les
  générateurs de disques sont appelés AVANT le pool. Mesuré : palier pixel **66 s →
  46 s**, borné par le seul `nocooper_greetings` (~46 s) — le prochain gain est le
  raccourcissement de CET étalon, pas davantage de parallélisme.
- **Le palier `fast` n'est plus aveugle au rendu** : 4 étalons pixel courts
  (`overscan_top`, `trace_odd`, `scroll_8264`, `blitter_timer` — bordures Glue,
  exceptions CPU, scroll STE, blitter/ordonnanceur), tous sur ROM EmuTOS libre
  (prêt-à-purge). `fast` complet : **~12 s** (boot GUI, STX et pixels inclus) —
  chiffre mis à jour partout (`CLAUDE.md`, `TODO.md`, `TEST_SOFTWARE.md`), et
  `CLAUDE.md` précise que ces 4 pixels sont un garde-fou, PAS une couverture :
  « avant de conclure sur le rendu, `--tier full` » reste la règle.

## L'OBJECTIF est gardé par une machine + les chiffres de la doc se recomptent (A25 + A26, 2026-08-27)

**A25 — la suite Q du diagnostic MegaSTE est rejouée à chaque palier `full`.** Le 12/12
qui a déclaré l'objectif atteint était une validation MANUELLE, non gardée — une
régression MegaSTE (SCU, SCC, DMA, RTC…) serait restée invisible de toute la pyramide.
`tools/run_megaste_diag.py` (~14 s) rejoue la recette — redécouverte au banc, la session
d'origine ne l'avait pas consignée : menu du diag à l'écran avant la trame 300, « Q » +
Return à 320 (scancodes `10,90,1c,9c`), `--loopback-at 330` (APRÈS l'injection datée,
leçon OUTIL-1), `--dma-fixture`, `--fastfdc`, fin de suite mesurée entre les trames
5000 et 6500 → 8000 de marge, disquettes A ET B sacrificielles (le test F formate,
piège A14). Verdict série STRICT : exactement 11 « Pass » (R,O,M,T,D,I,L,P,F×2,Y) +
« No VME board » (fidèle) + « Q Tests Completed » + zéro « Fail » — le leurre « No
loopback connector » documenté dans l'en-tête. Dépendances Atari (TOS 2.06 + cartouche)
absentes → **SKIP recensé**, jamais un faux vert ; c'est pourquoi l'étape vit dans
`full`, jamais dans `fast` (prêt-à-purge). Au passage, mesuré : sur **EmuTOS** la suite
rend 11/12 — le test O échoue LÉGITIMEMENT (CRC EmuTOS hors table Atari), d'où le choix
du TOS d'époque.

**A26 — `check_doc_claims.py` : les ancres gardaient les symboles, ceci garde les
CHIFFRES.** Neuf affirmations vérifiées contre leur source de vérité recalculée
(git ls-files, etalons.json, wc -l) ; motif introuvable = échec aussi (une reformulation
ne désarme pas le contrôle en silence). Au palier `fast`. **Il a attrapé une dérive à
son premier run** : `main.cpp` annoncé 5 017 lignes, recompté 4 814 (l'extraction A21
du matin même). Passe de péremption faite dans la foulée : le `_comment` d'`etalons.json`
recompté (15 étalons, plus 12 ; le MegaSTE n'est plus « couvert par rien ») ;
`CYCLE_ACCURACY.md` §4 réécrit (le beam-sync y était encore « le chantier ouvert » avec
le texte du 2026-06-18 — section remplacée par le récit de clôture) ; la borne MFP
« ≤ 1 instruction » corrigée en « 157 cycles mesurés » dans le résumé de
`HATARI_DIVERGENCES.md` (le détail, plus bas dans le même fichier, était déjà juste) ;
et `MOIRA_WINUAE_CONVERGENCE.md` reçoit un bloc **« ÉTAT COURANT »** daté en tête —
chantier CLOS, valeurs en vigueur (read −6 / write +2, PAR PAIRE), plan §8 exécuté —
pour que l'état ne s'obtienne plus qu'en lisant 700 lignes de journal dans l'ordre.

## Le palier P1 de l'audit est soldé : A16-A24, neuf corrections en une passe (2026-08-27)

Chaque item validé au palier `full` (15 étalons pixel + cycle-bench à tolérance zéro),
TOUS LES PALIERS OK en fin de passe.

- **A16 — `NEOST_LINELEN` tranché, et le tranchage a instruit un bug.** L'hybride
  Machine-ON/Shifter-OFF n'était pas un choix : le basculement du défaut à ON
  (tranchage WS3 du 2026-07-08) avait oublié les quatre sites Shifter. Tenté :
  unifier à ON → le glue-selftest **SEGFAULTE** — et il segfaultait déjà sur le code
  d'avant via la recette d'A/B documentée `NEOST_LINELEN=1`. Verdict : ce sont DEUX
  fonctionnalités — le canal Machine validé garde `NEOST_LINELEN` (ON, =0 pour
  l'A/B, lecteur unique `Shifter::lineLenEnv`), l'attribution expérimentale V3 passe
  sur `NEOST_LINELEN_ATTR` (OFF) avec son segfault consigné (**A16b** au TODO).
  Poser `NEOST_LINELEN=1` pour un A/B n'arme plus un chemin non validé.
- **A17** — version de save-state en constante unique (`kStateVersion`) ; le message
  de rejet annonçait « writes v16 » pour un build qui écrit v17.
- **A18** — `run_timed()` (verdict 124, façon timeout(1)) sur les ~15 appels à
  l'émulateur des 5 runners : un 68000 qui boucle ne consommera plus les 45 min du
  job CI sans diagnostic. Budgets par nature d'étape (selftest 300 s, pixel 900 s,
  oracle 1800 s).
- **A19** — les selftests de `run_all.py` sont sélectionnés **par type** depuis
  `etalons.json` (fin de la liste d'IDs en dur) : l'orphelin `serloop_selftest` est
  branché (il passe), et un selftest ajouté au manifeste est exécuté d'office.
- **A20** — `stx_writetrack_test` sort d'`EXCLUDE_FROM_ALL` et entre au palier
  `fast`, sur une image STX **FORGÉE** en mémoire (l'ancien défaut — Rick Dangerous
  cracké — n'existait même plus dans le dépôt : le test était doublement mort).
  ⚠ piège évité : ses `assert()` seraient vidés par le -DNDEBUG du profil Release —
  `-UNDEBUG` forcé sur la cible.
- **A21** — le clavier vit dans `gui/StKeys`, module PARTAGÉ bureau/web (même
  recette qu'`AudioMix`) : le navigateur récupère le pavé numérique, Undo, Help et
  le keymap international (AZERTY sous TOS FR tapait en QWERTY), et le pays TOS est
  armé au boot ET au changement de ROM côté web.
- **A22** — `--from-cfg` appelle `parseConfigLine` (le lecteur du GUI) : plus de
  liste de clés à tenir à jour, la classe de bug qui a récidivé deux fois est fermée.
  Sentinelles sur machine/mem/cpu (défaut headless Ste ≠ défaut Config st).
- **A23** — `Bus::stealBusCycles` : la primitive unique du vol de cycles bus
  (l'invariant « MÊME QUE BL3 » vivait recopié dans `Blitter::billCycles` et
  `Fdc::billDmaCycles`). Cycle-bench inchangé au cycle près.
- **A24** — les 14 clés `crt_*` de `neost.cfg` sont bornées NaN+clamp aux plages de
  `CrtParams.h` (la leçon `volume=nan` appliquée au bloc voisin), `crt_mask=` borné
  0..3.
- **Boot GUI dans run_all (A9a)** : 400 trames EmuTOS + capture non uniforme à
  chaque palier quand un affichage existe (sauté ET DIT sinon) — `build/neost`
  n'était couvert localement par rien. `--run-frames` est devenu un vrai mode
  harnais : gel CENTRAL de `saveConfig` (6 appelants, dont la résolution MIDI qui
  réécrivait `midi_out_port=0` depuis un contexte sandboxé) + `imgui.ini` ni lu ni
  écrit — constaté avant la garde : le boot GUI de test écrasait `rom=`/`rtc=` du
  poste de dev.

Note de véracité : l'audit annonçait le palier fast « périmé d'un facteur ~5 »
(15-20 s) — la mesure donne **4,8 s**. C'est le chiffre mesuré qui fait foi partout.

## Audit d'architecture quatre dimensions + grand ménage du TODO (2026-08-27)

Quatre audits indépendants (cœur, frontends, tests/CI, fidélité/docs/gouvernance) menés sur
l'arbre au commit `ee617e4`, croisés avec des vérifications directes (build propre, palier
`fast` vert en 4,8 s, `git ls-files` recompté). Bilan d'ensemble : **~6,5/10** — fidélité
d'émulation 8,5 (zéro divergence Hatari de sévérité haute ouverte, vérifiée entrée par
entrée), sécurité des parseurs 8,5, headless 8, modèle temporel 7,5, save-state 7,
documentation 6,5, tests/CI 6, cœur 6, frontends GUI 4, **gouvernance juridique 3** (le
§ BLOQUANT du TODO). Le fil conducteur, écrit dans `gui/AppConfig.hpp` par le projet
lui-même : *ce qui est testable est bon, ce qui ne l'est pas dérive.*

**Trouvailles neuves versées au TODO en items A16-A37** (plan de correction P1/P2/P3),
dont les plus notables :
- `NEOST_LINELEN` a **deux défauts contradictoires** (`Machine.cpp` : `true` ;
  4 sites `Shifter.cpp` : `false`) — l'hybride que le commentaire d'en-tête de
  `Shifter.cpp` dénonce existe en production, dans l'autre sens (A16) ;
- `serloop_selftest` est **orphelin** : la liste d'IDs codée en dur de `run_all.py` ne
  l'exécute dans aucun palier, et rien ne peut le signaler (A19) ;
- `tests/stx_writetrack_test.cpp` est du code de test **mort** (`EXCLUDE_FROM_ALL`,
  jamais compilé) alors que le parseur STX n'a aucun autre test (A20) ;
- le clavier du frontend **web** est amputé (pavé numérique, Undo, Help, clavier
  international absents — copie partielle du keymap de `main.cpp`) (A21) ;
- le message de rejet des save-states annonce « writes v16 » alors que le build écrit
  v17 — la constante vit en double (A17) ;
- la validation MegaSTE 12/12 est **manuelle et non gardée** — aucun étalon ne couvre
  MegaST/MegaSTE/TOS 2.06, `etalons.json` l'avoue lui-même (A25) ;
- le servo audio et la boucle de cadence existent en **trois copies** (GUI/web/android),
  `kCpuHz` en quatre (A28) ; `--from-cfg` relit `neost.cfg` par une copie qui a déjà
  divergé deux fois (A22) ;
- le pin upstream de Moira n'est écrit **nulle part** dans `extern/moira/NEOST_VENDOR.md`
  et le `Cputester/` a été élagué : le fork n'est ni rebasable ni re-validable (A35).

**Grand ménage du TODO** (482 → 444 lignes : ~160 lignes de clos/périmé retirées, le plan
A16-A37 ajouté ; ne reste que l'OUVERT) :
- **Retiré car clos et déjà consigné ici** : les chroniques Slirp/Little Snitch, fenêtres
  EtherNEC ROM3/ROM4 et CAB/theoldnet (entrées du 2026-08-27 ci-dessous, recettes
  incluses) ; les items barrés SCSI-NCR5380/NVRAM/ROM TOS MegaSTE (entrée « MegaSTE au
  banc ») ; la matrice MegaSTE validée (seul le reste DD/HD × cache demeure).
- **Affirmations périmées corrigées** (constat de l'audit : la doc dérive là où
  `check_doc_anchors.py` ne regarde pas — les chiffres) : « beam-sync casse EL/Cuddly/SHO »
  retiré de la priorité n°1 (chantier **clos** depuis les passes du 2026-07-09 et du
  2026-08-06 — EL 12402/12402, Cuddly 250/250, SHO résolu ; le TODO l'affichait encore
  comme front actif sept semaines plus tard) ; « 44 ROM » → **37** ; « 6 étalons sur 13 »
  → **8 sur 15** ; `wasm/index.data` retiré du § BLOQUANT (plus suivi depuis le
  2026-08-23) ; `main.cpp` « 4 980 » → **5 017** lignes ; borne MFP « ≤ 1 instruction »
  → **157 cycles mesurés** ; palier fast « ~3 s (2026-08-19) » → **4,8 s (mesuré
  2026-08-27)** — également corrigé dans `docs/TEST_SOFTWARE.md`. Restent
  `docs/CYCLE_ACCURACY.md` §4 et `docs/MOIRA_WINUAE_CONVERGENCE.md` (journal par
  accrétion) : c'est l'item **A26**, avec un `check_doc_claims.py` à créer sur le modèle
  des ancres.
- **Archivé ici — les deux erreurs de méthode du 2026-08-25** (retirées du TODO, à ne pas
  recommettre) : (1) *un seuil absolu sur une grandeur dépendante de la charge* — `timer
  IRQ max lateness` avait été inscrit comme sonde « doit rester à 132 » ; faux (147, 156,
  157, 163 relevés ailleurs) — cette métrique se compare à charge identique, jamais à un
  seuil : un faux garde-fou coûte plus cher qu'aucun garde-fou ; (2) *justesse validée,
  coût ignoré* — BL4 validé au pixel sans aucune mesure de débit alors qu'il multiplie
  les appels au dispatch (coût mesuré nul a posteriori par le perfbench).

## LE MEGASTE AU BANC FIELD SERVICE — suite Q 12/12, l'objectif de tête du TODO est ATTEINT (2026-08-27)

L'objectif déclaré en tête du `TODO.md` (« émuler proprement un MegaSTE ») listait trois
manquants : SCSI/NCR5380, TOS 2.05/2.06, NVRAM. Bilan du chantier : **les trois étaient
soit déjà faits, soit des idées fausses** — et la validation a débusqué deux vrais bugs
(un d'outillage, un d'émulation SCC) qui font passer la suite Q du diagnostic Atari.

**Les trois « manquants » du TODO, tranchés :**
- **SCSI/NCR5380 : le MegaSTE n'en a PAS.** Le 5380 est du TT/Falcon ; le disque interne
  du MegaSTE passe par un **pont ACSI-SCSI** (carte type Megafile) sur le bus ACSI — que
  NeoST émule déjà. Concordance triple : Hatari ne câble le 5380 que sur TT
  (`ioMemTabTT.c`) et Falcon (`hdc.c`), EmuTOS (`bios/scsi.c`) ne connaît que TT/Falcon,
  et l'Atari Wiki l'écrit noir sur blanc (« the Mega STE has no full-featured SCSI bus —
  only ACSI/DMA »). L'item du TODO reclassé TT-only, hors périmètre.
- **TOS 2.05 et 2.06 bootent au bureau** sur `--machine megaste` (US 60 Hz et FR 50 Hz,
  1/2/4 Mo), EmuTOS 256 aussi, et une image ACSI se monte en C:. ⚠ Recette : TOS 2.x
  attend une TOUCHE après son test mémoire (`--scancode-at N "39,b9"`), déjà consigné.
- **NVRAM : n'existe pas sur MegaSTE** (c'est le TT/Falcon, MC146818). TOS 2.06 boote
  sans — l'item se clôt par vérification.

**Suite Q du `MegaSTE_Diagnostic_v1.5` (TOS 2.06, toutes fixtures) : 12/12.**
R RAM, O ROM, M MIDI, S RS232, T MFP/Glue/Video, **D DMA Port**, I SCC, L RTC, F
disquettes A+B (DS, format/écriture/lecture), P Printer/Joy, Y blitter long : **Pass** ;
V : « No VME board » (fidèle — Hatari n'émule pas le VME). L'exerciseur **J (Hard Disk
W/R) tourne propre** sur une image ≥ ~21 Mo (il lit la LBA 40732 en supposant le disque
interne 48 Mo d'époque : sur une image de 16 Mo, son « Command error » est un CHECK
CONDITION légitime, pas un bug).

**Le boîtier de test DMA du kit Field Service est émulé** (`Fdc::setDmaFixture`,
`--dma-fixture`, OFF par défaut — Hatari ne l'a pas : sans lui, D échoue pareil ici et
chez l'oracle). Protocole décodé du test D lui-même : c'est une cible ACSI de banc à
**UN octet de commande** — `((count-1)<<6) | opcode`, opcode **$10** = le boîtier AVALE
count×512 octets (RAM→port), **$08** = il les REND — transfert immédiat, compteur de
secteurs décompté à ZÉRO, adresse DMA avancée, IRQ GPIP5 (le test vérifie ces trois
points : D0 time-out / D1 count / D3 status, puis compare les données, D2). Piège
d'implémentation payé : les bits 7-6 de l'octet ne sont PAS une cible ACSI — filtrer
sur `(v>>5)==0` rejetait `$D0` (count=4) et rendait un D0 time-out en phase 3.

**Vrai bug d'outillage (classe OUTIL-1) : `--loopback` était IGNORÉ avec les injections
datées.** Le branchement des connecteurs ne vivait que dans le chemin `--keys` ; avec
`--keys-at`/`--scancode-at`, les tests S/M/P concluaient « No loopback connector » à tort.
Corrigé (branchement après la DERNIÈRE injection datée) + nouvelle option **`--loopback-at
N`** pour les recettes où le test démarre sitôt le Return avalé.

**Le « No loopback connector » du dump série est un LEURRE** — et il avait déjà coûté une
enquête (commit du 2026-08-25 : « la détection exige encore autre chose que je n'ai pas
identifié »). Réponse : la routine série du diag ÉMET LE MESSAGE D'ERREUR COMME DONNÉES DE
SONDE (chaque caractère transmis doit revenir par le bouclage) ; la chaîne apparaît donc
dans `--serial-dump` même quand le test PASSE. Le verdict fiable est à l'ÉCRAN (« Pass »)
ou l'absence de « Fail at cycle » — jamais cette ligne-là.

**Vrai bug d'émulation : le SCC n'avait pas de prises de bouclage.** Le test I détecte
chaque prise (Port A, Port B, LAN) en émettant un **BREAK (WR5 bit4)** et en guettant
**RR0 bit7 (Break/Abort)** au retour, puis vérifie TxD→RxD, RTS→CTS, DTR→DCD et DTR→DSR
(/SYNC), et les IRQ ext-status. Implémenté dans `io/Scc` (`setLoopback`, fronts via la
machinerie `updateRR0` existante), branché par `--loopback`/`--loopback-at`, OFF par
défaut — étalons intacts. `Testing SCC : Pass`, les trois prises détectées.

**Piège A14 re-payé une fois de plus** : le test F du diag FORMATE la disquette A — un run
Q a modifié `disks/diskA.st` dans l'arbre git (restauré ; les recettes passent désormais
par des copies sacrificielles `--disk`/`--diskb`).

## « Impossible d'ouvrir un dossier sur C: » : c'était l'EMUDESK.INF, pas la souris ni l'ACSI (2026-08-27)

Symptôme (GUI, image ACSI `cab_hd.img` du chantier CAB) : les icônes de lecteur
s'ouvrent (double-clic, Return, File→Open), mais **aucun sous-dossier** ne s'ouvre —
l'icône se sélectionne et rien ne suit. Le TODO l'attribuait au double-clic trackpad
(souris relative) ; l'utilisateur a écarté cette piste : le même geste marche sur le
lecteur GEMDOS de Cubase. Reproduit en **headless** (`--mouse-at`, double-clic scripté)
→ ni souris GUI, ni ACSI : sur une image fraîche au contenu identique, tout s'ouvre.

**Bisection sur l'image elle-même** (résidents AUTO supprimés → pareil ; INF supprimé →
guéri) : le coupable est l'`EMUDESK.INF` **minimal** écrit à la main pendant la session
CAB — `#Z`/`#M`/`#T` sans les lignes **`#W`** (fenêtres du bureau). Sans `#W`, EmuDesk
ouvre les lecteurs dans une fenêtre dégénérée plein écran et **refuse toute navigation**
dans un sous-dossier. Correctif : INF **écrit par EmuTOS lui-même** (Options → Save
desktop, scripté à la souris en headless), ligne `#Z 01 C:\CAB\CAB.APP@` réinsérée avant
le bloc `#W`. Vérifié : dossiers ouvrables (C:\CAB, 45 items) ET autostart CAB intact.

Recettes payées : un `EMUDESK.INF` se termine en **CRLF** (un patch en `\n` fait
disparaître des icônes) ; « Save desktop » se confirme par **Return** (le clic scripté
sur OK n'a pas pris) ; le `DIRTEST.PRG` de la session précédente plantait de lui-même
(routine `puts` placée en tête du binaire → TOS démarre dedans) et n'avait donc jamais
testé l'ACSI. Au passage l'image quitte le scratchpad volatil pour `disks/cab_hd.img`
(gitignorée à l'unité comme la carte UltraSatan), `neost.cfg` mis à jour, sauvegarde
`cab_hd.img.bak` conservée à l'ancien emplacement.

## THEOLDNET.COM S'AFFICHE DANS CAB — un Atari ST émulé surfe en GUI (2026-08-27)

« Welcome To The Old Internet Again! » rendu dans **CAB 1.5** (freeware d'Alexander
Clauss) sur NeoST : TOS → STinG 1.26 → `ENEC.STX` → NE2000 émulée → NAT SLIRP →
Internet réel. Toute la session est scriptée (`--mouse-at`/`--scancode-at` GUI) :
boot ACSI (image `make_hd_image.py` avec AUTO STinG config-only + CAB + `CAB.OVL`
1.4401), autostart EmuDesk (`#Z`), dialogue Paths au sélecteur, clic du lien, rendu.

Le pare-feu applicatif du poste bloquant les sockets du binaire GUI non signé, la démo
passe par un chemin **100 % loopback côté hôte** : `tools/net_localdns.py` (DNS lié à
`0.0.0.0:53` — l'exemption macOS INADDR_ANY permet les ports bas sans root — répond
`10.0.2.2` pour tout nom existant) + `tools/net_gateway80.py` (passerelle transparente
`0.0.0.0:80`, routée par l'en-tête Host, pompe bidirectionnelle keep-alive). Côté ST le
DNS et le HTTP sont authentiques ; la sortie hôte part d'un python autorisé. Le chemin
DIRECT (sans aides) marche dès qu'une règle « any version » couvre le binaire.

Recettes payées pendant la traque (à ne pas repayer) :
- **Un clic en dernier token de `--mouse-at` n'est jamais relâché** (la relâche vient du
  token suivant) — les boutons GEM veulent un appui TENU : `1111` puis `.` ; un Return
  vaut le bouton par défaut ; les « champs » du dialogue Paths de CAB sont des
  boutons-sélecteurs, pas des champs texte ; le homing souris doit contourner la barre
  de menus (survol = menu ouvert).
- **Le cache de CAB persiste dans l'image ACSI entre les runs** : une entrée vide d'un
  échec précédent se re-sert en silence (« page blanche chargée ») — régénérer l'image.
- Le motif « 61 dup-ACKs » du headless n'était PAS une perte de trames (loopback :
  80 livraisons = 80 reçues, trace `rx-macdrop` muette) : le RTO TCP de STinG (1,5 s
  ÉMULÉES) expire avant le vrai réseau quand le headless court plus vite que le mur.
  En GUI temps réel, le problème n'existe pas.
- Chemins de l'OVL à éviter : `HTTP_PROXY` (ignoré ici), URL numérique + proxy
  (panic Line-F dans CAB).

## EtherNEC : les fenêtres ROM3/ROM4 étaient INVERSÉES — trouvé par la 1re session STinG réelle (2026-08-27)

Premier banc bout-en-bout « un ST émulé surfe » : disquette STinG 1.26 + `ENEC.STX`
générée par **`tools/make_sting_test.py`** — un PRG 68000 assemblé à la main (assembleur
maison de `make_usatan_test.py`) y joue le rôle du CPX : cookie `STiK` → `get_dftab` →
TPL/STX, IP/masque pokés dans le `PORT` « EtherNet », `on_port`, `load_routing_table`,
puis `resolve()` et GET HTTP en TCP, verdict recopié sur RS-232 (`--serial-dump`).

Ce banc a démasqué un **vrai bug NeoST** que tous les selftests rataient : les fenêtres
du protocole fil EtherNEC étaient **inversées** — NeoST lisait en `$FB0000` et écrivait
en `$FA0000`, le vrai pilote fait l'inverse (`BUSENEC.I` de l'auteur du montage :
`getBUS` lit via **/ROM4 = `$FA0000` + reg×512**, `putBUS` écrit par fausse lecture via
**/ROM3 = `$FB0000` + reg×512 + data×2**). Les selftests passaient car ils parlaient la
même convention inversée que l'émulation — un test qui partage ses constantes avec le
code testé ne teste pas le CONTRAT ; seul le logiciel d'époque le teste. Diagnostic en
escalier, consigné dans le banc : port up mais 0 trame → `ACTIVATE`/routes OK (TRUE, 1) →
file d'émission pleine avec « 180 octets émis » côté pilote (3 ARP de 60) et rien côté
carte → protocole fil. Au passage, le client STinG doit tourner en mode **user**
(les entrées d'API jouent avec le vecteur Privilege Violation).

Corrigé (swap des constantes `READ_BASE`/`WRITE_BASE` de `Ne2000.hpp`, noms /ROM3-/ROM4
redressés partout, `nubnic` de `make_usatan_test.py` mis au protocole réel + image SD
de test régénérée). Verdict final du banc, série à l'appui : **`DNS=138.197.157.224`,
`TCP connected`, `HTTP/1.1 200 OK` + `<title>The Old Net</title>` reçus DANS le ST** —
un Atari ST émulé, sous STinG et le pilote d'époque, a chargé une vraie page web à
travers le NAT SLIRP. L'objectif « un ST émulé surfe » est atteint en headless ;
restent CAB en GUI et la borne (cf. `TODO.md` § Réseau).

## Slirp : le « dernier pas » est clos — 5/5, le coupable était Little Snitch (2026-08-27)

Le FAIL « SORTIE REELLE : DNS » qui bloquait `NetBackendSlirp` depuis le 2026-08-22 n'était
**pas un bug NeoST**. Diagnostic par interposition DYLD (`sendto`/`recvfrom`/`poll` espionnés) :
le datagramme DNS **sort** (`sendto` = 31 octets vers 8.8.8.8:53, rc OK) mais `poll()` ne voit
jamais la réponse — et un **témoin de 30 lignes hors NeoST** (socket/sendto/poll nus) échoue à
l'identique pendant que `dig` (signé Apple) résout. Cause : **Little Snitch** jette
silencieusement l'UDP externe des binaires non signés ; une alerte en attente **gèle même le
`sendto` dans le noyau** (~2 min) jusqu'au verdict. Autoriser `build/neost-headless` → **5/5**.

Durci au passage :

- **4ᵉ vérification déterministe et HORS LIGNE** dans `--slirp-selftest` : boucle retour UDP
  loopback à travers le NAT (répondeur local éphémère, `10.0.2.2:port` → `127.0.0.1:port`).
  Contrairement aux points 1-3 (servis par SLIRP en interne), la trame traverse une **vraie
  socket hôte aller-retour** : chemin complet socket → SLIRP → ARP → anneau RX prouvé sans
  Internet, insensible aux filtres applicatifs (le loopback n'est pas filtré) — c'est elle qui
  tranche « NeoST correct » vs « environnement ». CI-compatible.
- La sortie réelle devient le **5ᵉ point** (opt-in `NEOST_SLIRP_ONLINE=1` inchangé) ;
  `NEOST_SLIRP_DNS` accepte `a.b.c.d[:port]` ; les réponses des points 4 et 5 portent des
  xid distincts (la réponse loopback RESTE dans l'anneau — BNRY n'avance pas — sans
  discriminant le point 5 rendrait un faux vert). Doc → `docs/EXTENSIONS.md` § NetUSBee.
- **Le GUI est câblé** (même jour) : case « Real Internet for the NE2000 » page Network,
  clé `slirp=` de `neost.cfg` (aller-retour couvert par `neost-selftest`), applicateur
  unique `neBackend()` — SLIRP ouvert → SLIRP, sinon boucle locale — et bascule À CHAUD
  (la carte vue par le pilote ne change pas, pas de reset). Vérifié : boot GUI scripté
  (`--run-frames`) avec `slirp=1` → `[slirp] NAT user-mode démarré`. Au passage,
  `--from-cfg` rejoue désormais `netusbee=` et `slirp=` (le premier manquait à l'appel
  alors que son commentaire promettait la parité avec le lecteur GUI).

## La revue d'architecture A1-A8 est soldée : l'outillage se teste lui-même (2026-08-26)

Clôture de la revue du 2026-08-25 — huit propriétés manquantes du **système de développement**,
pas du système émulé. Les chantiers qui n'avaient pas encore leur entrée ici (A2/BL5, D3 et les
briques GUI d'A8 ont les leurs plus bas) :

- **A1 — le palier PIXEL garde le commit** : job `pixel` dans `.github/workflows/tests.yml` —
  `run_all.py --tier full` à chaque push et pull_request (+ `--verify-refs`, captures d'écart
  déposées en artefact à l'échec). Mesuré le jour du constat : `NEOST_SYNC_DISPATCH=1` cassait
  `nocooper_greetings` à 98,97 % sans que le `fast` ne bronche — c'est ce trou qui est fermé.
- **A4 — l'instrument est testé** : `tools/check_headless_options.py` (palier fast, en boîte
  noire) couvre le parsing du headless ET du GUI (`--help`, `--version`, rejet d'une option
  inconnue — une faute de frappe comme `--kisok` partait sans un mot). Chaque test porte le bug
  qu'il empêche de revenir : répétabilité DISCRIMINANTE de `--joy-at`, première occurrence
  appliquée, garde « un run nominal ÉMULE » contre le piège zsh. Deux manques comblés au
  passage : `--key-hold N` (durée d'appui — indispensable pour égaliser une A/B avec le
  `--cmd-fifo` d'Hatari qui tient ~600 ms : un verdict « confirmé à l'oracle » avait déjà été
  rendu FAUX par cet écart) et `--scancode-at` (scancodes ST bruts, rouvre le pavé numérique).
- **A5 — l'oracle est épinglé et sa mise en place scriptée** : Hatari pin **`f0736b2`**
  (v2.6.1-devel), `tools/setup_hatari.sh` (clone au SHA + options macOS obligatoires),
  avertissement de `tools/hatari_oracle.sh` quand l'arbre présent diverge du pin, options CPU
  **explicites** (`--cpu-exact on --compatible on` — les forcer à off déplace la comparaison de
  69 px), et recette de pilotage au joystick versée dans `docs/HATARI_AUTOMATION.md`. Résidu :
  le RNG d'Hatari semé sur l'heure (numérotation de trames variable), contourné par
  `oracle_scan`.
- **A6 — barrière de débit** : `tools/run_perfbench.py` (palier full) garde des **RATIOS**
  entre charges du même run (indépendants de la vitesse du runner), tolérance ±25 % — fait pour
  attraper un chemin devenu deux fois plus cher, pas 5 %. Première réponse rendue : le coût de
  BL3/BL4 est **nul** (±1,5 % mesuré) — le dispatch est O(1) quand aucune échéance n'est due.
- **A7 — les ancres de la doc sont vérifiées en CI** : `tools/check_doc_anchors.py` (palier
  fast), 242 ancres de code contrôlées dans les docs vivants (erreur) et le CHANGELOG
  (avertissement seulement — un registre daté a le droit de citer un symbole renommé depuis),
  ALLOWLIST motivée, contrôle négatif fait.
- **A8 — le GUI tourne en CI** : job `xvfb` (Linux, GL logiciel) — boot réel de la cible
  `neost`, capture `--shot`, comparaison **bit-exacte** contre le headless. Avec
  `--run-frames`, l'injection d'entrées et la souris scriptée (entrées dédiées ci-dessous), A8
  est clos ; seul reliquat, une limite de DSL : pas de token « mouvement bouton tenu » (drag).
- Au même mouvement, le **MFP en mode bloc** a reçu son étalon (`mfp_poll`).

Reste ouvert de la revue : **A3** (corpus de régression non livrable) et la revue
complémentaire du 2026-08-26 (A9-A12) — voir `TODO.md`.

## BL5 : chaque blit démarrait 4 cycles trop tôt (2026-08-26)

**Trouvé par l'étalon `blitter_timer` posé le jour même**, puis instruit jusqu'à la cause.
Hatari pose `Blitter_CyclesBeforeStart = 4 + 4` au **démarrage** d'un blit — son propre
commentaire : « 4 cycles to complete current bus write to ctrl reg + 4 cycles before blitter
request the bus » — mais `= 4` seulement à la **reprise** d'une tranche non-hog. NeoST utilisait
`kPreStartCycles = 4` dans les **deux** cas. La fenêtre PRE_START est désormais paramétrée
(8 au démarrage, 4 à la reprise, `kStartDelayCycles`).

**Mesuré** : dérive de datation **86 → 20 cycles par blit**, écart à l'oracle **397 → 299 px**.
Le mode HOG reste à **0 px** (`blitter_hog`, référence oracle) et **aucun autre étalon pixel ne
bouge** — le changement est confiné au chemin de démarrage du blitter.

⚠ **Trois hypothèses formulées puis réfutées** avant d'arriver là, consignées dans BL5 pour ne
pas les rouvrir : l'arbitration par blit vs par tranche (faux — `Blitter_Start` est ré-appelé à
chaque tranche) ; l'oracle tournant le chemin non-CE et son forfait de 256 cyc (faux — le défaut
d'Hatari ici EST le cycle-exact) ; et l'overlap CPU parallèle, qui serait un effet **par
tranche** (faux — doubler la taille du blit ne double pas la dérive : elle est **par blit**, et
c'est cette mesure qui a mené à la vraie cause).

**Résidu instruit le lendemain, cause NON établie — et rien n'a été modifié sur une
supposition.** Décomposition mesurée en variant le nombre de tranches par blit (1 / 2 / 4) :
**10 cyc/blit à une tranche, 12 à deux, 20 à quatre** — ~10 cyc fixes plus ~2-3 par tranche
supplémentaire. Le résidu suit les **restitutions de bus**, pas le volume transféré.

Deux réfutations de plus, consignées dans BL5. **L'overlap CPU parallèle
(`Blitter_Check_Simultaneous_CPU`) est réfuté par le SIGNE** : l'expérience du port 4+4 est
causale — *ajouter* 4 cyc/blit à NeoST a *réduit* l'écart, donc NeoST facture **moins** de temps
par blit qu'Hatari, alors que l'overlap est un remboursement côté Hatari, qui le rendrait plus
rapide encore. Et **l'absence de délai de démarrage sur le chemin HOG est CORRECTE** : l'ajouter
casse la conformité de `blitter_hog` (0 → 122 px). En HOG, `start()` tourne dans l'instruction
d'écriture de `$FF8A3C`, dont les cycles restants sont déjà facturés par Moira ; en non-hog on
planifie un événement depuis ce même point, et le délai doit être explicite.

Aucun candidat Hatari n'explique les ~10 cycles restants (arbitration, PRE_START de reprise et
comptage des accès CPU sont identiques des deux côtés). Ajouter une constante sans ligne de
source pour la justifier serait une rustine — le projet en a déjà retiré. **Le résidu reste
ouvert et documenté, pas masqué.**

## GUI : souris scriptée (`--mouse-at`) — le chemin « double-clic GEM » est franchi, Beyond the Ice Palace CLOS (2026-08-26)

**La dernière marche d'injection d'A8.** Le GUI accepte `--mouse-at N "SCRIPT"`, même DSL que le
headless (L/R/U/D = ±8 px, 1/2/3 = clics, `.` = idle, un token par trame émulée). Avec elle, le
chemin que le TODO déclarait hors de portée — « double-clic bureau GEM (Pexec sous AES) » — a été
piloté de bout en bout : bureau → double-clic icône A → fenêtre → double-clic `ICEPALAC.PRG` →
cracktro D-Bug → trainer (`--scancode-at`) → menu du jeu → **EN JEU, PROPRE, 16 couleurs**.

**Verdict Beyond the Ice Palace** (rapport « écran scramblé en jeu ») : **non reproduit après
instruction complète**. En couleur, le chemin GEM exact du rapport joue proprement. En moniteur
**mono**, le PRG **quitte en ~10 s** (retour bureau, pointeur re-centré — la signature d'un Pexec
terminé) : on ne peut même pas être « en jeu » en mono, le scramble ne peut donc pas venir de
cette config. Dossier clos comme Wings of Death : à rouvrir sur nouvelle repro avec `neost.cfg`
et version. Le catalogue des bugs ouverts ne contient plus que **Shadow Warriors** (disque absent
du dépôt).

**Pilotage GEM au script souris — les pièges, payés puis consignés :**
- le **double-clic** doit suivre le pattern d'Hatari (`ikbd.c:DoubleClickPattern`) : 4 trames
  down, 4 up, 4 down, 4 up — et FINIR PAR UNE RELÂCHE, GEM compte au release ;
- les clics d'UNE trame (14 ms) sont trop brefs pour certains widgets ;
- les menus TOS se déroulent au **SURVOL** : un trajet de saturation par le coin HAUT les
  accroche, et le clic suivant tombe dans un item (vécu : dialogue « Informations bureau »
  ouvert à la place de l'icône). Approcher par le coin BAS ;
- la **saturation** d'un positionnement absolu doit couvrir la PLUS GRANDE résolution :
  ≥ 80 tokens en X (640 px), ≥ 50 en Y (400 px). 40 tokens ne saturent qu'en basse résolution —
  bug de calage vécu, invisible tant que tous les runs étaient en couleur ;
- limite de DSL connue : pas de token « mouvement avec bouton tenu », donc pas de DRAG
  (rubber band GEM impossible).

⚠ Le GUI RÉÉCRIT `neost.cfg` à chaque sortie (rom/disk/mono suivent la session) : un harnais qui
enchaîne les runs fait DÉRIVER la config utilisateur — vécu, restauré à la main. À traiter un
jour (`--no-save-config` ?).

## GUI : injection d'entrées — et le dossier Wings of Death est CLOS (2026-08-26)

**La marche suivante d'A8, posée et immédiatement rentabilisée.** Le GUI accepte désormais
`--scancode-at N HEX[,…]`, `--key-hold N` et `--joy-at N VAL` (mêmes noms que le headless).
⚠ Nuance de sémantique, documentée dans le code : `--joy-at` est **TENU** — l'état est re-posé à
chaque trame ≥ N, parce que le GUI écrase le port avec l'état des manettes réelles à chaque
tour ; une pose unique serait perdue au tour suivant, et « tenu » est de toute façon ce qu'un
harnais veut.

**Application immédiate : Wings of Death « après le bouton », la réserve restante du dossier.**
Feu injecté et tenu dès la trame 3000, config utilisateur réelle (`neost.cfg`, `drivesound`
actif), capture à 5500 — après la rafale FDC de t=72-74 s identifiée comme la fenêtre du
symptôme : **titre au dragon 154 couleurs PROPRE, zéro `ring underrun`** sur tout le run (mesure
faite machine au repos, sans aucune charge concurrente). Après trois passes — cœur émulé
byte-identique à l'oracle, GUI en chargement, GUI après le bouton — **le rapport n'est reproduit
nulle part** : dossier clos dans `docs/CASE_STUDIES.md`, à rouvrir uniquement sur une nouvelle
repro accompagnée du `neost.cfg` et de la version (le rapport d'origine prédate BL3/BL4/D-PSG/D3).

## GUI : sortie automatique après N trames + capture (`--run-frames`, `--shot`) (2026-08-26)

La brique qui manquait au chantier A8 : le GUI ne pouvait ni s'arrêter seul ni être observé par
un harnais. `--run-frames N` quitte proprement après N trames **émulées** (décompte au site
d'émulation nominal — le pas-à-pas du débogueur ne compte pas, c'est voulu : l'option sert un
harnais, pas une session de débogage) ; `--shot PATH` écrit le framebuffer ST en PPM juste
avant, avec le `fclose` vérifié (un disque plein n'échoue qu'au flush). Vérifié en réel : boot
EmuTOS, capture, sortie seule en ~5 s, code 0. Ouvre la voie au job CI `xvfb` (recensé dans A8).

## D3 est corrigé : le flush FIFO du FDC stalle le CPU, à la cadence exacte d'Hatari (2026-08-26)

**Troisième tentative, la bonne — et les deux verdicts d'échec précédents étaient des artefacts
d'outillage, corrigés dans le même mouvement.** La formulation est `due + stall + delay` : chez
Hatari, `PendingCyclesOver` est capturé EN TÊTE de handler, PUIS le stall avance le compteur
global, PUIS le réarmement retranche l'overshoot — l'ancrage retire donc le retard de dispatch
(mesuré : 6,7 cyc/échéance, ~107 par cycle de 16 octets) mais laisse le stall décaler la suite.
Les quatre formulations mesurées : 4203 (ancien code, +76 vs Hatari), 4096 (ancrage pur, stall
absorbé), 4235 (ancrage neutralisé), **4127,8 (couple complet — cible Hatari 4127)**.

**Validé** : 14/14 étalons pixel verts, `nocooper` re-posé à **0 px contre l'oracle**, canaris
Lethal Xcess et Stardust OK. `dmaStallPending_` est transitoire (RAII par événement, nul à toute
frontière de trame) — rien à sérialiser.

**La chasse aux artefacts, consignée pour ne pas la refaire.** (1) La sonde de mesure était logée
DANS le correctif → inactive côté témoin → « l'ancrage double le débit DMA », faux. (2) L'AVI
oracle d'un run `--cmd-fifo` contenait 7297 fois LA MÊME frame → diffs strictement constantes
lues comme « le titre occupe toute la fenêtre » puis « l'oracle est figé », faux aussi. Contrôle
désormais obligatoire : **md5 de frames éloignées avant tout scan**. (3) La démo No Cooper attend
un ESPACE que `hatari_oracle.sh` n'injecte pas — la référence d'origine disait « touche espace
tenue vbl ~900 » dans sa note, jamais relue. Protocole complet dans la note de l'étalon
(`--cmd-fifo` + `hatari-event keydown 57` ; le nom SPACE est refusé ; fast-forward inopérant).

✅ Le suivi `lateness` 132 → 252 a été instrumenté et CLOS le jour même : **deux pics isolés de
Timer D** (161 et 252 cyc) sur ~300 000 échéances, pas un régime — un timer échu pendant une
instruction que le stall DMA intra-quantum allonge attend la fin de l'instruction, comme pour
tout wait-state. Fidèle au matériel.

## Le blitter a enfin un étalon — et il trouve une divergence (A1+A2+A3) (2026-08-26)

**A1 — le palier PIXEL garde désormais la barrière.** `tests.yml`, le job qui tourne à chaque
push, ne lançait que `--tier fast`, **qui ne compare aucun pixel**.
`--tier full` n'existait que dans `release.yml`, donc une régression de rendu partait sur `main`
et n'était vue qu'à la publication suivante. Nouveau job **`pixel`** : `--tier full` à chaque
push et pull_request, plus `--verify-refs`, plus le dépôt des captures d'écart en artefact quand
il échoue. Coût ~1-2 min. Deux cas mesurés que le `fast` ne voyait pas :
`NEOST_SYNC_DISPATCH=1` casse `nocooper_greetings` à 98,97 %, et le chantier BL3/BL4 (base de
temps du blitter) serait passé au vert.

**A2 — `tools/make_blitter_test.py` + étalon `blitter_timer`.** Le trou mesuré la veille :
`NEOST_BLIT_TRACE=1` rendait **0 blit** sur l'intégralité du corpus pixel, entièrement en
`machine=st`. Le nouvel étalon (EmuTOS 256 Ko, STE, 512 Ko) contraint **deux** propriétés dans une
seule image : la **datation** — lignes 0-99, l'octet TADR relu après chaque blit non-hog, Timer A
en mode délai prescaler /200, donc 1 tic = 200 cyc : insensible au jitter sous-tic, sensible à la
classe BL3 (1088 cyc ≈ 5 tics) — et les **données** — lignes 120-127, destination de 100 blits
16×8 mots depuis un motif à pas `$3B27` qui balaie les 4 plans. ~400 tranches non-hog par run.

**Dents vérifiées, pas supposées.** L'image diffère de **406 px** entre le commit `6bc2ce3`
(AVANT BL3/BL4) et l'état corrigé — **toutes** dans la zone TADR, **zéro** dans la zone de
données, soit la signature exacte d'un écart de datation sans écart de données. Cet étalon aurait
donc attrapé BL3/BL4. Il détecte aussi `NEOST_RAM_SLOT=0` (269 px) et `NEOST_SYNC_DISPATCH=1`
(198 px), et ignore `NEOST_IACK_SYNC=0` (0 px) — ce qui est **correct** depuis BL4, la dette
étant soldée par accès et non plus au point d'IACK.

**Et il a trouvé une divergence dès sa première exécution — BL5.** NeoST vs oracle Hatari :
**397 px / 114816**. Le chemin de **données est byte-identique** (0 px sur la destination) ;
l'écart est **entièrement de datation**, 99 lignes TADR sur 100, avec une dérive qui
**s'accumule** de +1 à +43 tics sur 100 blits, soit ≈ **86 cycles par blit non-hog**. L'oracle est
stable (3 runs Hatari = 0 px entre eux), ce n'est donc pas un artefact de boot. **BL3/BL4 avaient
rapproché** NeoST de l'oracle sans fermer l'écart : 463 px avant → 397 px après. Piste inscrite
en BL5 : NeoST facture l'arbitration une fois par **blit**, Hatari une fois par **tranche**.

**A3 — le corpus livrable progresse.** `blitter_timer` est créé d'emblée sur ROM **libre** : le
corpus qui survivrait au retrait des TOS Atari propriétaires passe de **5 à 6 étalons**, et la
**seule** couverture du blitter est du côté libre.

**Le chemin HOG aussi — et il localise BL5.** Étalon jumeau **`blitter_hog`** (même programme,
`ctrl=$C0`, donc `Blitter::start` au lieu de `Blitter::onSlice`) : ce chemin n'était exercé par
aucun test ni aucun titre. Contrôle de non-trivialité : 380 px d'écart avec `blitter_timer`, dont
**0 px en zone de données**. Et il passe **`ref_kind: oracle` à 0 px** — conformité, pas seulement
non-régression. Conséquence directe : **BL5 est spécifique au NON-HOG**, donc à la fenêtre CPU
entre deux tranches, et non à la facturation du blit (identique dans les deux modes).

⚠ **Deux hypothèses posées puis réfutées le jour même** sur BL5, consignées pour ne pas les
rouvrir : (1) « NeoST facture l'arbitration par blit, Hatari par tranche » — faux,
`Blitter_Start` est ré-appelé à chaque tranche ; (2) « l'oracle tourne le chemin non-CE et son
forfait de 256 cyc » — faux aussi, le défaut d'Hatari ici EST le cycle-exact (mesuré : forcer
`--cpu-exact off --compatible off` change l'image, 397 → 328 px). ⚠ Ces deux options déplacent la
comparaison de 69 px et `hatari_oracle.sh` n'en passe aucune : toute reprise doit les épingler.

⚠ Restent non couverts : le MFP en mode bloc et le stall FIFO du FDC (D3).

## La sélection de lecteur du PSG atteint enfin le FDC (`D-PSG`) (2026-08-25)

**Ce qui n'allait pas.** Le FDC ne **relisait** le port A du PSG (R14 : lecteur A/B, face) que
depuis ses propres accès registre — `Fdc::refreshDriveSide()` n'avait que trois appelants, tous
des accès CPU aux registres FDC — et le sink port A du YM2149 n'avait qu'un abonné, qui ne
faisait que le bouclage RS-232. **Le chemin PSG→FDC n'existait pas.** Hatari, lui, **pousse** :
`psg.c:419-420` appelle `FDC_SetDriveSide` à *chaque* écriture du port A. Conséquence : tout
programme qui écrit sa commande FDC **avant** de sélectionner le lecteur restait bloqué avec
`driveSel_ = -1` — commande partie sans lecteur, `indexTime_` à 0, INTRQ/GPIP5 jamais levé.

**La mesure.** *Stardust* sur STE : écran noir définitif après le menu trainer, le CPU tournant
dans `tst.b D5 / bne $261e` en attente d'une IRQ FDC qui ne venait plus. Trace :
`[fdc-st] cmd=1 state=2 drv=-1 idxTime=0 str=c5` puis **silence FDC total** sur 90 s de temps ST
(4714 lignes en tout).

**Ce qui a changé.** `Machine::setPortASink` appelle désormais `Fdc::refreshDriveSide()`
(passée publique) à chaque écriture du port A, avant le bouclage RS-232 — port direct du
comportement d'Hatari. La fonction est idempotente : elle ne ré-ancre l'index que si le lecteur
a effectivement changé.

**Résultat mesuré.** `drv=0` au lieu de `drv=-1`, INTRQ levé, **374 294** lignes FDC au lieu de
4714, et Stardust STE joue son **intro défilante** (14 couleurs) puis fond au noir et va chercher
la disquette 2 dans le **lecteur B** (`drv=1`, `idxTime=0`, lecteur vide) — le comportement de
l'oracle. `--tier full` : tous les paliers OK ; Lethal Xcess `megast` inchangé (132 cyc).
⚠ Les disquettes 2 et 3 sont **absentes du dépôt** : le titre n'est pas jouable pour autant.
C'est une correction de **fidélité**. ◑ Résidu : l'oracle affichait « INSERT DISK 2 IN ANY
DRIVE » là où NeoST fond au noir puis poll le lecteur B.

## Headless : `--joy-at`, `--joy-script` et `--mouse-at` deviennent répétables (`OUTIL-1`) (2026-08-25)

Ces trois options étaient des **scalaires** là où `--keys-at` / `--key-down` / `--key-up` sont
des vecteurs : la dernière occurrence de la ligne de commande écrasait les précédentes **sans le
moindre avertissement**. Ce n'est pas un détail — le balayage du catalogue du même jour a montré
que ce seul piège fabriquait des « bugs » qui n'existaient pas : *Xenon 2*, *Flood* et *Dynamite
Dux* étaient tous rapportés bloqués, tous jouables une fois la repro corrigée. Les trois passent
en `std::vector`, l'aide porte « (repeatable) », et les sites d'application bouclent sur les
listes (chevauchement : le dernier de la ligne gagne sur les trames communes ; des scripts
disjoints jouent tous). Vérifié : `--joy-at 10 0x80 --joy-at 30 0x08` produit désormais **deux**
lignes « joystick applied », une seule avant.

⚠ Piège de mise en œuvre, consigné : `--joy-at` consomme **deux** arguments, donc un
`emplace_back(next(a), next(a))` aurait un ordre d'évaluation **non spécifié** et inverserait la
trame et la valeur au gré du compilateur — les temporaires nommés sont obligatoires.

Reste ouvert (cf. `TODO.md`) : `--keys-at` ne tient la touche que **2 trames ≈ 40 ms**, ce qui
fausse toute A/B contre un `--cmd-fifo` d'Hatari (~600 ms) — un verdict « confirmé à l'oracle » a
déjà été rendu FAUX par cet écart ; et `stScancode()` ne mappe pas le pavé numérique.

## Bug hunt : les 67 disques du dépôt passés au headless (2026-08-25)

**Couverture intégrale, catalogue sain.** Les 67 images de `disks/st` et `disks/stx` ont été
bootées en headless. **66 sans divergence démontrée.** Le brut donnait 8 anomalies ; **7 tombent**
en réfutation adversariale, dont 5 à l'oracle Hatari ou au réflexe RAM. Les sept sont versées
dans `docs/CASE_STUDIES.md` — New Zealand Story (manque de RAM : `$FC0BD8` est la routine
d'affichage des **bombes** du TOS, pas une boucle mystérieuse), Xenon 2, Flood, Dynamite Dux,
disquettes STX de données seules en A:, Great Giana Sisters, Pipe Dream. *Arkanoid*, tranché
FIDÈLE lors de la passe précédente mais jamais versé, y est également fermé et retiré du
catalogue des bugs ouverts.

**Un seul bug d'émulation survit — `D-PSG`** (fiche complète dans `TODO.md`) : la sélection de
lecteur/face écrite dans le **port A du PSG** n'est jamais poussée vers le FDC. Hatari pousse à
chaque écriture (`psg.c:419-420` → `FDC_SetDriveSide`) ; côté NeoST `Fdc::refreshDriveSide()`
n'est appelé que depuis des accès CPU aux registres FDC — **le chemin PSG→FDC n'existe pas**.
Tout programme qui écrit sa commande FDC *avant* de sélectionner le lecteur reste bloqué avec
`drv=-1`. Symptôme : Stardust sur STE, écran noir définitif après le menu trainer. Oracle :
Hatari atteint « INSERT DISK 2 IN ANY DRIVE ».

**Aucune régression** du chantier blitter du même jour : le seul échec STE-only du catalogue est
`D-PSG`, dont la trace montre un FDC arrêté et non un décalage de base de temps ; Lethal Xcess,
Wings of Death et `faster_atari_ste` passent sur machine à blitter.

**Le principal fabricant de faux positifs est un défaut d'OUTILLAGE**, pas d'émulation :
`--joy-at`, `--joy-script` et `--mouse-at` ne sont **pas répétables** (scalaires, dernière
occurrence gagnante, sans avertissement) — à eux seuls ils ont produit **trois** des huit
« bloquants ». Et `--keys-at` ne tient la touche que **2 trames ≈ 40 ms**, ce qui rend toute A/B
contre un `--cmd-fifo` d'Hatari (~600 ms) **faussée** : un verdict « confirmé à l'oracle » a été
rendu FAUX par cet écart. Recensé `OUTIL-1` dans `TODO.md`.

**Corrections d'inventaire.** *V3* : « CyclesPerVBL ±4 » **retiré, faux positif prouvé** — chez
Hatari ce compteur n'est vivant qu'en mode VDI, la VBL normale vient de la chaîne
`ShifterLines[].StartCycle`, que NeoST porte déjà. *D3* : **confirmé et chiffré** (Hatari 4127
cyc/flush, NeoST 4173, modèle sans stall 4096) avec un avertissement neuf — **à ne pas appliquer
seul**, D3 isolé porterait l'erreur de +45 à +77 cyc, et sous `--fastfdc` l'écart actuel est de
**+14 %**, pas 1,1 %. *MFP* : écart confirmé et exhibé, mais le patch proposé est **rejeté** (il
réactiverait le dispatch sync-driven déjà réfuté, ~1590 fois par trame) ; la borne annoncée
« ≤ 1 instruction » est corrigée en **157 cycles** mesurés.

⚠ **Leçon d'outillage** : un balayage de masse laisse des images de disque **modifiées dans
l'arbre git** (le jeu écrit sur sa disquette). Une image a dû être restaurée. Il manque un
`--disk-ro`.

## Les cycles volés par le blitter avancent enfin l'horloge des timers (2026-08-25)

**Ce qui n'allait pas.** NeoST avait DEUX bases de temps dès que le blitter volait le bus
en dehors d'un quantum CPU. `Blitter::billCycles` → `Cpu68k::addBusWaitCycles` n'avance que
l'horloge du cœur Moira ; or `Blitter::onSlice` (tranche non-hog) est le **callback de
l'échéance `Scheduler::BLITTER`** : il tourne dans `Scheduler::runTo`, donc **entre** deux
`cpu.run()`. Ces cycles n'étaient donc ni dans `ran` (retour de `Cpu68k::run`, mesuré
depuis un `quantumStartBus_` réancré sur l'horloge CPU à chaque entrée) ni dans
`sched.now()` : **perdus** pour l'ordonnanceur. Chez Hatari le problème n'existe pas —
`Blitter_AddCycles` (`blitter.c:351-352`) écrit dans `nCyclesMainCounter` /
`CyclesGlobalClockCounter`, les compteurs mêmes que le CPU incrémente et que `CycInt` lit
pour dater ses échéances (`cycInt.h`, `CycInt_Process`).

**La mesure.** Sonde `NEOST_QDELTA_DIAG` (delta = horloge CPU − `sched.now()` à l'entrée du
quantum) sur *Lethal Xcess* en `megast`, 6000 trames : delta plat à **40** (décalage de
reset, sans rapport) pendant ~7 millions d'entrées de `run()`, puis **escalier de 136 en
136 sur les 21 dernières** — 8 tranches non-hog × 136 cyc = **1088 cycles bus** de dette,
résorbée d'un seul coup par le `syncTo` du hook d'IACK juste avant le handler Timer A. Ce
saut mangeait les 2 tics de prescaler de marge : `Mfp::readTimerData` rendait TADR = `$3C`
au lieu de `$3D`/`$3E`, et la garde `$14C2E` du jeu tombait dans son `ILLEGAL`. Le
symptôme FDC noté au TODO le 2026-08-19 (`cmd=d0` puis silence) était un **leurre** :
identique en `machine=st`, où le jeu démarre.

**Ce qui a changé.** `Blitter::billCycles` crédite désormais l'ordonnanceur
(`Scheduler::addStolenCycles`) **en plus** de l'horloge Moira, mais **uniquement** hors
quantum (discriminant `Cpu68k::inRun` — dans le quantum, le mode HOG est déjà capté par
`ran` puis reversé par le `runTo(now + ran)` de `Machine::runFrame` ; créditer les deux
double-compterait). La facturation est **par accès**, pas en lot : `Blitter::billCycles`
(port de `Blitter_AddCycles` + `Blitter_FlushCycles`) est appelé par `readWord`/`writeWord`
pour leurs 4 cycles et à chaque **arbitration**, à sa position réelle — prise du bus AVANT
le transfert, restitution APRÈS. `addStolenCycles` **dispatche** (`syncTo`) au lieu
d'avancer `now_` en silence, si bien qu'une échéance tombant au milieu d'une tranche est
servie **au cycle où elle tombe**. C'est le modèle d'Hatari, dont `CycInt_Process`
(`cycInt.h:85-88`) est ré-entré depuis le handler `INTERRUPT_BLITTER` lui-même. La
ré-entrance de `Scheduler::runTo` est acquise : `fired`/`minAll` étaient déjà des locales,
seul `firingDue_` — l'ancre anti-dérive de l'événement en cours — manquait, il est
désormais sauvegardé/restauré par un garde RAII. **Aucun état persistant ajouté, format
de save-state inchangé.**

**Résultat.** *Lethal Xcess* : plus aucun `BREAK $14C2E`, et le jeu va **en jeu** sur les
**trois** machines à blitter — `megast` (trame 5552 avant), `ste` et `megaste` (trame 5523
avant, écran noir à 1 couleur ; désormais 26-29 couleurs, écran de jeu). `machine=st` :
capture 26000 trames **bit-identique** et métriques au cycle près (le patch y est inerte,
pas de blitter). Boot EmuTOS `megast` nu : capture bit-identique et `timer IRQ max
lateness` **3334 → 161 cyc**. `--tier full` : tous les paliers OK.

**Sur la métrique `timer IRQ max lateness`.** Une étape intermédiaire de ce chantier
créditait l'ordonnanceur en **fin de tranche** : elle réparait le jeu mais faisait monter ce
compteur de 132 à ~265 cyc, un retard intra-tranche borné (136 cyc, 264 sur tranche pleine)
qui était le prix du crédit groupé. Le passage au dispatch **par accès** (ci-dessus) l'a
supprimé : le compteur **redescend à 132** sur `megast`, `ste` et `megaste` — soit la valeur
ST, mais cette fois sans plus rien masquer, là où AVANT tout le chantier il cachait un saut
cumulatif de 1088. Divergences recensées **BL3** et **BL4**, toutes deux ✅, dans
`docs/HATARI_DIVERGENCES.md`.

**Validation de la ré-entrance.** Build à assertions ACTIVES (`-UNDEBUG` — attention,
`RelWithDebInfo` réinjecte `-DNDEBUG` APRÈS `CMAKE_CXX_FLAGS`, il faut passer par
`CMAKE_CXX_FLAGS_RELWITHDEBINFO`) : *Lethal Xcess* `megast` 26000 trames, ~1,6 M
préemptions, des milliers de tranches à 64 dispatches imbriqués chacune — **aucune**
assertion de `runTo` déclenchée (`armedInvariant`, `nextDue_ == scanNextDue()`). Idem
`ste`/`megaste`.

## Le halt du 68000 s'annonce (double faute de bus/adresse) (2026-08-25)

Un `cpuDidHalt()` était disponible côté Moira mais non surchargé : NeoST **arrêtait le
CPU en silence** sur une double faute. Écran figé, aucune explication — c'est ce qui avait
fait ouvrir le faux bug « Stardust sur ST : NeoST reste noir là où Hatari halte ».
**Vérification (2026-08-25) : NeoST HALTE déjà, sur la MÊME instruction que Hatari**
(`$FC5082`, `A7=$4E7340E7` impair, après la bus error `$FFFF8900` d'un jeu STE lancé sur
ST) ; 0 instruction exécutée après la trame ~1826, écran noir des deux côtés. Ajouté :
le message `[cpu] 68000 halted: double bus/address error while taking exception vector N
(SSP=$…)` (vecteur latché par les délégués d'exception de groupe 0 — `reg.pc0` a déjà
été avancé par le prefetch, il ne désigne PAS la faute, donc aucun PC n'est affiché,
comme Hatari `gui-sdl/dlgHalt.c:66-71`), le cas distinct « reset vector fetch failed »
(`Moira::reset`), l'accesseur `Cpu68k::halted()` et une ligne de bilan headless.
**Aucun changement de comportement émulé** ; suivis TODO/CASE_STUDIES clos.

## Page Input : ce qu'on branche dans les deux ports joystick (2026-08-23)

Vue **par port** au lieu d'une liste de manettes : « Port 0 (mouse port) » = souris
(défaut) / auto (2ᵉ manette) / clavier / telle manette ; « Port 1 » = auto (1ʳᵉ manette) /
clavier / telle manette. S'exprime sur le mécanisme existant (rôles par GUID `joymap=`,
émulation clavier, nouveau `port0=mouse|auto`) : la fenêtre Joystick et le menu borne
restent cohérents. **Changement de défaut** : une 2ᵉ manette ne prend plus le port souris
toute seule (`port0=mouse`) — on l'y branche explicitement, ou `port0=auto` pour
l'ancien comportement. Un joystick qui occupe le port 0 **débranche la souris hôte**
(`g_port0Joystick`), comme sur un vrai ST ; les clés de protection du port (page Dongles)
s'affichent à côté.

## Dongles, 2e passe : oracle de rejeu, un périphérique par port, port cartouche abstrait (2026-08-23)

Revue d'architecture du chantier dongles, six manques corrigés :
1. **Oracle** — format de trace de référence (`R3`/`R4`/`U`, `docs/EXTENSIONS.md`),
   `--key-log FILE` l'écrit, `--key-replay FILE` rejoue une capture (matérielle ou NeoST)
   contre la machine d'état sans machine et sort 0/1 ; recette de capture documentée.
2. **Un périphérique par port** — `PortDevices` remplace l'enum unique « à la Steem » :
   `joy0=`/`joy1=`/`rs232=`/`printer=`/`cartbutton=`, `--plug PORT=DEVICE`, connecteurs
   vérifiés (une clé joystick entre dans les deux DE-9, le GUI signale le mauvais port),
   coexistence. `disks/dongles.txt` branche la clé d'un jeu au montage (emplacements vides
   seulement), GUI, borne et headless.
3. **Port cartouche abstrait** — `core/CartDevice.hpp` : les périphériques s'abonnent aux
   signaux /ROM3, /ROM4, /UDS ; le `Bus` boucle sur la liste (plusieurs clés possibles),
   plus de `dongleUds` spécial dans le wrapper CPU. `CubaseDongle` → `CartridgeKey`.
4. **Observabilité** — page Dongles : sondages, dernier octet, état, armement.
5. **Save-state v16** — `PortDevices` sérialisé (oscillateur, date, périphériques branchés).
6. **DAC Pro Sound** — bloc DC propre amorcé au branchement (plus de clic), fader `mix_dac=`
   page Sound.
Non couvert : les frontends WASM/Android n'exposent toujours pas les dongles.

## Page « Dongles » et adaptateurs de port : `PortDongle` (2026-08-23)

Après les clés Steinberg, **les autres** : recherche sur les dongles ST (Steem SSE, WinUAE,
MiSTery, forums — inventaire dans `docs/EXTENSIONS.md`), puis transcription des onze
adaptateurs que Steem émule. `--adapter NAME`, `adapter=`, nouvelle page **Dongles** de la
configuration (la clé Steinberg y déménage depuis la page MIDI). Joystick : **Leader Board /
10th Frame** (haut+bas), **Cricket Captain / Rugby Coach / Multi Player Soccer Manager**
(oscillateur `%1100`/`%1101`). RS-232 : **B.A.T. II** (CTS à 0), **Music Master** (DTR → DCD
retardé de 200 cycles), **Jeanne d'Arc** (DCD sur décroissance de RTS|DTR). Parallèle :
**Pro Sound Designer**, DAC 8 bits sur le port imprimante (Wings of Death, Lethal Xcess sur
STF) — R15 horodaté et rejoué par le YM avant son HPF. Boutons **Multiface ST** (GPIP7) et
**Ultimate Ripper** (RI) : page Dongles ou `--button-at N`, relâchés à la VBL. Crochets :
`Mfp::setGpipReadHook`/`setMonitorButton`, `YM2149::setPortBDac`, sonde joystick IKBD, abonné
port A. **OFF par défaut**, étalons inchangés (`--tier full`), auto-tests par protocole.
**Clé C-Lab Notator / Creator** (`--dongle notator`) : les équations de l'EP600, publiées
par TPH en octobre 2025 et transcrites en C dans le firmware SidecarTridge `md-notator`,
sont portées dans `CubaseDongle` — bascule d'armement `FEEDB1` sur /ROM4 (`$FA00EA`),
8 bascules D cadencées par UDS (désarmée) ou par la **descente** de /ROM3 (armée), resets
asynchrones D8/D9 ; nouveau crochet `rom4Read` dans le `Bus`, save-state **v15**.
Non émulés faute de relevé public : Log 3 (EP330), Pro-24, Avalon, Zodiac.

## Cubase Lite joue un SMF en headless, et on le vérifie note à note (2026-08-23)

La chaîne « classiques du piano → Cubase Lite → Pianoteq » tenait déjà (sortie CoreMIDI
horodatée du 21/08), mais rien ne la **prouvait** sans oreille. Désormais :
`neost-headless --midi-dump FILE` journalise chaque octet MIDI OUT daté de son cycle
68000 ; `tools/midi_compare.py` le confronte au SMF donné au séquenceur (notes, ordre,
vélocités, durées, pédale CC64, pente de tempo, gigue) ou le convertit en SMF (`--to-smf`) ;
`tools/run_midi_sequencer.py` rejoue tout le scénario — boot TOS 1.04 sur un lecteur GEMDOS
temporaire, **auto-lancement de Cubase** par la ligne `#Z` de `DESKTOP.INF`, *File → Import…*
à la souris relative (`--mouse-at`), nom de fichier tapé au clavier **AZERTY** (nouveau
`--azerty` : sur TOS FR, `M` tombait en virgule), Enter du pavé (`|`) = Play — et tranche :
**PASS**, 200 notes, pente 1,001, gigue σ 0,4 ms. Étalon du palier `fast` (≈3 s).
Corpus **piano classique**, un dossier 8.3 par compositeur (`disks/midi/BACH`, `MOZART`,
`BEETHOVE`, `CHOPIN`, `HAYDN`, `SCARLATT` — 159 pièces : inventions, sinfonies et Clavier bien
tempéré I de Bach, 17 sonates de Mozart, sonates de Beethoven/Haydn, préludes et mazurkas de
Chopin ; partitions Humdrum de C. S. Sapp, CC BY-NC-SA, rendues par music21, sources et `.krn`
sous `disks/midi/sources/`) ; le blues déménage en `BLUES/`. Un canal, une piste de notes par
fichier — Pianoteq ne voit plus de note-on en double.
Trois quirks **de Cubase Lite** trouvés par l'étalon et absorbés par `midi_simplify.py` :
une armure (méta 0x59) en cours de morceau lui fait **jeter la piste de notes** à l'import
(gardée au tick 0 seulement) ; doublure à l'unisson ou note répétée sans trou → note coupée à
2 ms (`--detach` : fusion, troncature, tick de séparation) ; note-off un tick interne en
avance (tolérance de durée à part). `disks/midi/README.md` : provenance, recette Pianoteq.

## Clé Steinberg sur le port cartouche : `CubaseDongle` (2026-08-23)

`--dongle cubase3|cubase2|auto`, page MIDI, `dongle=` — **OFF par défaut**. Clé **rouge**
(Cubase 3.10 / Score / Audio : EPLD 5C060, 16 bascules T, A8 → D8, cadencée par /ROM3) et clé
**noire** (Cubase 2.01 : PAL16R8, A1-A8 → D8-D15, cadencée par **chaque** front /UDS du CPU —
crochet `udsDone` dans `NeostMoira`, un test de bool par accès). Banque `/ROM3 $FB0000-$FBFFFF`
(pas `$FA0000` : le TOS ne sonde que /ROM4, la clé cohabite avec le HD GEMDOS). Équations
transcrites de MiSTery (`cubase2_dongle.v`, `cubase3_dongle.v`) ; Hatari n'émule aucune clé.
Save-state **v14** (état de la clé + drapeau). ⚠ Fidèle aux équations, **pas encore confrontée
à un Cubase à clé** (aucun dans le dépôt) — cf. `docs/EXTENSIONS.md`, `TODO.md`.

## Warnings de build éliminés (2026-08-23)

`Ne2000.cpp` (constantes CR inutilisées), `SlirpBackend.cpp` (`register_poll_fd` dépréciés
sous libslirp ≥ 4.8 : pragma + `SlirpCb` remplie en place, le constructeur de déplacement
implicite déclenchait l'avertissement hors de portée du pragma), `CMakeLists.txt`
(`neost_core` lié deux fois via `neost_net` PUBLIC → `ld: duplicate libraries`).

## Pages servi par la CI : `wasm/` sort du dépôt (2026-08-23)

**Le bundle WebAssembly n'est plus commité.** Le job `wasm` de `release.yml` le dépose
en **artefact Pages** et un job `pages` le publie (`build_type=workflow`). Une seule
construction Emscripten par push — le job existant sert les deux usages, l'archive de
release et la démo en ligne. `wasm/` est gitignoré ; `cmake --build build-web --target
neost-web` le régénère pour qui veut la démo hors ligne.

**Ce que ça règle.** En `build_type=legacy`, Pages servait la branche : le dépôt était
publié **en entier** sur habib256.github.io/neost/, ROM Atari et jeux compris — une
contrepartie qui avait été assumée faute de mieux (cf. `TODO.md` § contenu propriétaire).
Le déploiement par artefact ne publie QUE le bundle. Et la CI ne pose plus de commit de
bot sur `main` à chaque push, ce que faisait la recommission du bundle.

**L'URL change** : la démo passe de `/neost/wasm/` à la **racine** `/neost/`. Le lien du
README suit.

Troisième et dernier état d'un aller-retour assumé : garde de fraîcheur
(`tools/wasm_stamp.sh`, rouge sans réparer) → recommission par la CI (réparait, mais
commit de bot systématique) → déploiement par artefact.

## Cartes SD UltraSatan amorçables : `tools/make_hd_image.py` (2026-08-22)

Un dossier hôte devient une carte SD que le **vrai TOS** monte en C:, pilote compris.
`make_hd_image.py SRC OUT.img` écrit la table de partitions Atari AHDI à la main
(aucun outil hôte ne la connaît : ce n'est pas un MBR DOS), délègue le FAT16 à mtools,
et **greffe automatiquement le pilote** depuis un disque donneur détecté dans `hd/`.
Aucun pilote n'est embarqué dans le dépôt — HDDRIVER est commercial ; on reprend celui
que l'utilisateur possède déjà. Vérifié `drvbits=$00000007` (A B C) sous **TOS 1.04,
TOS 2.06 et EmuTOS**.

**L'amorçage HDDRIVER est en deux étages, chacun gardé par une somme `$1234`** — et
c'est là que tout se joue. L'étage 1 (secteur racine, que le TOS n'exécute que si sa
somme vaut `$1234`) choisit la partition dont le drapeau masqué par `$F8` vaut `$80`,
lit son premier secteur, **refait la même somme dessus** (`cmp.w #$1234,d0` / `bne` →
abandon) et seulement alors saute dedans. Deux conséquences, toutes deux constatées en
échec avant d'être comprises au désassembleur :

- `mformat` écrit un secteur DOS de somme quelconque ⇒ l'étage 1 refuse de l'exécuter,
  et la trace ACSI s'arrête pile après les secteurs 0 et 2. On rétablit `$1234` par un
  mot d'ajustement en `$1FE`, là où DOS met `$55AA`.
- le code de l'étage 2 commence à **`$34`**, cible du `BRA.S` de tête, donc il empiète
  sur les 2 derniers octets du champ « nom de volume ». Le copier depuis `$36` laisse
  ces 2 octets s'exécuter et fait dérailler l'étage 2 **en silence**.

Le BPB de mformat (FAT16, 512 o/secteur) convient tel quel : l'étage 2 sait le lire,
il n'y a pas à reproduire la géométrie du donneur.

**Piège de méthode consigné** : `--keys` tape APRÈS le boot, or TOS 2.06 attend une
touche sur son écran mémoire — utiliser `--keys-at 700 " "`, faute de quoi aucun disque
dur ne se monte et l'on croit à une divergence d'émulation. Et juger sur `_drvbits`
(`$4C2`) et les vecteurs `hdv_*` (`$472`), jamais sur les icônes du bureau : elles
viennent des lignes `#M` de `NEWDESK.INF` et s'affichent sans lecteur monté.

## libmt32emu vendorisé — le MT-32 cesse d'être une option de machine (2026-08-22)

**Munt (`libmt32emu` 2.8.3) passe de dépendance système à copie vendorisée**, dans
`extern/mt32emu`, liée en **statique**. Le `find_package(MT32Emu CONFIG QUIET)` exigeait
`brew install mt32emu` / `libmt32emu-dev` : aucun runner de CI ni aucune image de release
ne l'installait, donc le Roland MT-32/CM-32L n'existait **dans aucun binaire livré** — et
son absence ne se signalait que par une ligne de configuration. Le TODO du 2026-08-21
(« embarquer le `.dylib` ou compiler Munt en statique ») est fermé par cette seconde voie.

Code d'amont **intact** ; seul le `CMakeLists.txt` du dossier est de NeoST — il remplace
les 578 lignes d'amont (installation d'en-têtes, export de paquet CMake, `.pc`) par ce que
NeoST utilise : API C++ seule, statique, rééchantillonneur interne, aucune dépendance
externe. Les en-têtes sont recopiés dans `build/generated/mt32emu/mt32emu/` — les sources
d'amont s'incluent à plat (`"Synth.h"`) alors que NeoST écrit `<mt32emu/mt32emu.h>` —
même schéma que Moira. `add_subdirectory` est placé **après** le garde-fou GLFW : seul le
GUI s'en sert, inutile de le compiler pour les cibles headless, Android et WASM.

Licence : LGPL 2.1+, compatible avec la GPL 3 de NeoST (§ 3 de la LGPL), et l'édition de
liens statique est couverte puisque NeoST publie ses sources, cette copie comprise.
Détails et marche à suivre pour une mise à jour : `extern/mt32emu/NEOST_VENDOR.md`.
⚠ Les ROM Roland ne sont toujours pas incluses (`roms/mt32/`, à fournir).

## FujiNet retiré : NeoST n'émule plus que du matériel qui a existé (2026-08-22)

**Le FujiNet virtuel est supprimé du projet.** Il n'a jamais existé de FujiNet pour
l'Atari ST : c'était un *binding de référence* inventé ici, greffé sur l'opcode vendeur
ACSI `$60`. Un émulateur d'Atari ST n'a pas à porter du matériel imaginaire — et ce
sous-système était par ailleurs le moins tenu du dépôt (`FujiHostLive` : **0 %** de
couverture sous la suite complète).

Retiré : `io/FujiDevice.{cpp,hpp}`, `net/FujiHost.hpp`, `net/FujiHostLive.{cpp,hpp}`,
`net/FujiHostReplay.{cpp,hpp}`, `net/MiniJson.{cpp,hpp}`, `net/HttpClient.{cpp,hpp}`,
les fixtures `tests/fixtures/fuji/`, `dev/fujinet/`, `gemdos/DEMOS/NWGET.TOS`, les
drapeaux `--fujinet*` / `--fuji-selftest`, les clés `fujinet*=` de `neost.cfg`, l'étalon
`fuji_selftest` et la section FujiNet de `docs/EXTENSIONS.md`.

**Ce qui reste et ne bouge pas.** Le modem Hayes, l'anneau MIDI, EtherNEC, NetUSBee et
l'UltraSatan sont intacts — tous correspondent à du matériel réel. La couche socket
partagée qui vivait dans `HttpClient` (namespace `neonet` : `tcpConnect`, `sockSend`,
`sockRecv`…) est extraite dans **`net/Socket.{hpp,cpp}`**, dont c'est désormais le seul
rôle ; le modem et l'anneau MIDI y sont repointés. `NEOST_FUJI_TRACE` devient
`NEOST_NET_TRACE`. Les programmes ST `MIDITEST.C` / `MODMTEST.C`, qui testent le MIDI
ring et le modem, migrent de `dev/fujinet/` vers **`dev/netdemo/`** avec leur `build.sh`.

**Effet de bord bienvenu sur l'ACSI** : l'opcode `$60` était routé vers le périphérique
virtuel sur une cible ; il retrouve le **rejet strict sur toutes les cibles**, exactement
comme le port de `hdc.c`. NeoST est donc *plus* fidèle qu'avant sur ce point.

**Save-states : v12 → v13.** `FujiDevice` et `Acsi::fujiPending_` quittent le flux, et
les bits de drapeau d'en-tête se décalent (bit1 = EtherNEC, bit2 = UltraSatan, bit3 =
NetUSBee). Les `.state` antérieurs sont refusés avec un message explicite.

**Vérification** : `--tier full` vert — 19 étalons, dont les 11 comparaisons pixel, **0
pixel de différence**. Le retrait est neutre sur tout ce que la suite couvre.

## `docs/FUJINET.md` renommé, et un backend Internet réel pour la NE2000 (2026-08-22)

**Renommage.** `docs/FUJINET.md` devient **`docs/EXTENSIONS.md`** : le fichier documentait
depuis longtemps *toutes* les extensions NeoST — UltraSatan (stockage), NetUSBee, EtherNEC,
modem Hayes, anneau MIDI — alors que son nom n'annonçait que le FujiNet, **qui n'a jamais
existé sur Atari ST** (c'est le seul binding « de référence » du lot, sans matériel
correspondant). Un tableau en tête distingue désormais ce qui a réellement existé de ce qui
est une invention NeoST. `git mv` (l'historique suit), 17 fichiers de références mis à jour.

**`NetBackendSlirp` (en cours).** Le TODO « EtherNEC — backend réel » est attaqué :
`src/net/SlirpBackend.{hpp,cpp}` donne à la NE2000 émulée un accès Internet par **NAT en
espace utilisateur** (libslirp, le routeur de QEMU) — aucun privilège, pas de pcap ni de TAP.
L'ST reçoit 10.0.2.15/24, passerelle 10.0.2.2, DNS 10.0.2.3, DHCP servi par SLIRP. Option
CMake `NEOST_WITH_SLIRP` (pkg-config), drapeaux `--slirp` / `--slirp-restricted`, auto-test
`--slirp-selftest`. **3 vérifications sur 4 passent** (ARP, DHCP, compteurs) ; la sortie
réelle (DNS, opt-in `NEOST_SLIRP_ONLINE=1`) reste à finir — état détaillé, pièges déjà
résolus et pistes dans `TODO.md`, entrée en tête de liste.

Trois pièges de libslirp valent d'être retenus : ses callbacks `register_poll_fd` sont
*deprecated* mais appelés **sans test de nullité** (SIGSEGV à la première socket sortante) ;
`clock_get_ns` doit partir de zéro, sinon toute socket UDP naît « expirée » et meurt au
premier tour ; et SLIRP **ARPe l'invité** avant de lui livrer un paquet — sur un vrai ST
c'est la pile TCP/IP qui répond.

## La CI reconstruit ET recommite le bundle `wasm/` — plus de garde, plus de rouge (2026-08-22)

La démo en ligne est le dossier `wasm/` **commité** (Pages sert la branche). Conséquence :
toute modification de `src/**` le périmait, et la garde `tools/wasm_stamp.sh --check`
rendait la CI **rouge** jusqu'à une reconstruction manuelle — le 2026-08-19, puis le
2026-08-21 (chantier UltraSatan/NetUSBee). La garde faisait son travail (sans elle, la
démo restait périmée en silence) mais elle **signalait** au lieu de **réparer**.

Désormais, sur push de `main`, le job `wasm` de `release.yml` recommite le bundle qu'il
vient de construire : `[skip ci]` pour ne pas boucler, rien de commité s'il est identique
(emcc est déterministe à version d'emsdk égale), et en cas de push concurrent il repart du
nouveau sommet en y redéposant le bundle. Le dossier reste donc dans l'arbre de travail
(`git pull` après le run) et la démo suit les sources sans intervention.
`tools/wasm_stamp.sh` est supprimé.

Essai intermédiaire écarté : déployer Pages depuis l'**artefact** de CI (`build_type=workflow`,
`upload-pages-artifact` → `deploy-pages`). Ça marchait — démo à jour par construction, et le
dépôt n'était plus publié — mais le bundle disparaissait de l'arbre de travail, ce que le
mainteneur ne veut pas. Pages reste donc en `build_type=legacy` (`main/(root)`), avec sa
contrepartie assumée : le dépôt est publié en entier, ROM comprises (cf. `TODO.md`).

## Page Sound : mixeur par source + choix MT-32 / CM-32L (2026-08-21)

Configuration → Sound : **faders** YM2149, son DMA (STE), bruits du lecteur, MT-32 (0-200 %,
100 % = matériel), appliqués à chaud et persistés (`mix_ym=`, `mix_dma=`, `mix_drive=`,
`mix_mt32=`). YM et DMA sont dosés **en amont du LMC1992** (`neost::mixEmulatedFrame` reçoit
deux gains optionnels ; headless et web restent neutres, l'image sonore de référence ne
bouge pas — la voie DMA est rendue seule, dosée, puis le YM rejoint le mix). Même page :
sorties MIDI (GM, CoreMIDI, MT-32) et **modèle Roland Auto / MT-32 / CM-32L** (`mt32_model=`,
réouverture de Munt à chaud ; le chargeur ne retient que les ROM complètes et préfère
CM-32L 1.02, jamais la variante CM-32LN).

## Sorties MIDI hôte : synthé GM, port CoreMIDI, Roland MT-32/CM-32L (Munt) — livraison sans gigue (2026-08-21)

Le MIDI OUT de l'ACIA a enfin des oreilles côté hôte (`src/audio/MidiOutMac.*`, macOS) :
**synthé General MIDI intégré** (DLSMusicDevice) et **port CoreMIDI virtuel « NeoST MIDI OUT »**
(GarageBand, Logic, synthés logiciels). Mesure sur Cubase Lite : tempo exact (pente hôte/ST
1,0007) mais livraison au fil de l'exécution = **gigue ±60 ms** (σ 28 ms), le « bizarre » —
l'émulation avance par rafales de trames. Correctif : **livraison horodatée** — chaque octet est
daté de son cycle 68000 (`MidiAcia::setMidiSinkTimed`), ancré sur l'heure réelle de sa trame
(`emuNext`) + 30 ms d'avance fixe, et délivré par un thread à l'heure dite.
Et l'expandeur que visaient les morceaux d'époque : **Roland MT-32 / CM-32L** via
**libmt32emu (Munt, LGPL 2.1)**, optionnel au configure (`brew install mt32emu`), ROM Roland à
déposer dans `roms/mt32/`. Rendu DANS la sortie audio de NeoST, événements datés à
l'échantillon (`Synth::playMsg(msg, timestamp)`) : l'horloge est l'audio, gigue nulle par
construction (`src/audio/Mt32Synth.*`, `Audio::produceFrame(frameCycles, frameEndCycle)`).
Menu *Machine → MIDI OUT → …* ; `neost.cfg` : `midi_out_gm=`, `midi_out_port=`, `midi_out_mt32=`,
`mt32_roms=`. Traces : `NEOST_MIDI_TRACE` (cycle ST par octet), `NEOST_MIDIOUT_TRACE` (heure hôte).

## Câble de bouclage MIDI débranché par défaut — Cubase Lite charge ses morceaux (2026-08-21)

L'ACIA MIDI rebouclait en permanence OUT→IN (câble de test du diagnostic Atari). Avec le
MIDI Thru de Cubase/MROS, chaque octet émis revenait et repartait : larsen infini, « gel » au
chargement de `JAMMER.ALL` (73 IRQ RX pour 73 TX dans le save-state du gel). La fiche devient
**optionnelle et débranchée par défaut** (`MidiAcia::setLoopback`, `--loopback` branche RS-232
ET MIDI, menu *Machine → MIDI loopback cable*, `midi_loopback=`). Cas détaillé dans
`docs/CASE_STUDIES.md`. Le test « M MIDI » du diagnostic exige donc `--loopback`.

## Fenêtre « Keyboard » : le clavier ST en photo, toutes ses touches cliquables (2026-08-21)

Menu *Windows → Keyboard* (`showKbd=` dans `neost.cfg`) : la photo `pic/Black_Keyboard_AtariST.jpeg`
(décodée par `stb_image`, nouveau `extern/stb`, domaine public) avec une zone par touche →
scancode IKBD (`Ikbd::keyEvent`) : appui tenu tant que la souris l'est, Shift/Control/
Alternate/CapsLock collants (armés d'un clic, relâchés après la touche suivante), surbrillance
et info-bulle `nom ($scancode)`. Les libellés fantaisistes de la photo sont mappés à leur
position sur un vrai ST (grande touche à gauche de Z = Shift gauche, « Splift » = Shift droit,
« Ne » = Insert, « Rel » = Clr/Home, « PgDn » = Delete, vierge sous Tab = `~, pavé
« Num Lock / × ÷ » = ( ) / *). `src/gui/KeyboardWindow.cpp` ; `pic/` est livré par
`stage_free_data.sh`.

## Ctrl+Alt+F : bascule bureau ⇄ borne au clavier (2026-08-21)

Même action que `F8`, sous forme de chord hôte (discipline de `Ctrl+Alt+G` : `F` seul va
toujours à l'IKBD, le relâchement du chord est absorbé). `onKey` dans `src/main.cpp`.

## UltraSatan + NetUSBee : le vrai couple stockage/réseau du ST (2026-08-21)

Le FujiNet virtuel est un binding de référence sans matériel ; l'écosystème ST, lui, a tranché
autrement : **UltraSatan** (SD sur ACSI) et **NetUSBee** (Ethernet + USB sur le port cartouche).
Les deux sont maintenant émulés, OFF par défaut, sans effet sur les étalons.

- **UltraSatan** (`src/io/UltraSatan.*`, hooks dans `Acsi`) : 2 slots = cibles ACSI 0-1 (ID
  réglable en headless), INQUIRY `JOOKIE  UltraSatan` (RMB, n° de slot, v1.20), slot vide =
  NOT READY / medium not present, horloge propre sur les cycles émulés, et les **paquets ICD
  `$20 'US…'`** portés du firmware v1.20 (atarijookie/ce-atari) : `CurntFW`, `RdCl`/`WrCl`
  (magie `RTC`), `RdINQRN`/`WrINQRN`, `RdSt`/`WrSt` (magie `$83 $03 $17`), `RdLog` ; `RdFW`/`WrFW`
  refusés (pas de dataflash émulée). Garde-fou : les paquets `'US'` ne sont routés que sur les
  slots — toute autre cible reste byte-identique à `hdc.c`. **EmuTOS monte C:** depuis une image
  SD générée (`tools/make_usatan_hd.py`, partition GEM FAT16) sans un octet de pilote.
- **NetUSBee** (`src/io/Isp1160.*` + `io/Ne2000` inchangé) : contrôleur hôte USB ISP1160 décodé
  aux adresses du pilote FreeMiNT (`isp116x.h`) — latch `$FA0000`, lecture `$FA8000`, données
  `$FB8000`, commande `$FBC000`, accès MOT avec effets de bord une fois par accès CPU. ID `$6120`,
  reset logiciel, registres OHCI/ISP, hub racine **vide** (2 ports), ATL achevée en
  `DeviceNotResponding`. ⚠ Fenêtre LSB partagée avec le CR de la NE2000, consigné.
- **Tests** : `--usatan-selftest` (15 checks, séquence **LongRW** exacte de `US_CONF` — la première
  version lisait le statut APRÈS la bascule R/W, que le matériel efface : le harnais avait tort,
  pas le cœur) et `--netusbee-selftest` (11 checks, primitives **raw** du pilote — idem : le
  harnais utilisait la variante swappée que le pilote n'emploie pas pour les registres), tous
  deux au palier `fast`. Verdict série **`usatan_netusbee`** : une carte SD générée (16 Mo, GEM
  FAT16) qu'EmuTOS monte en C: et dont il lance `AUTO\USTEST.PRG` — programme 68000 relogeable
  qui pilote les deux cartes comme les logiciels d'époque. Trois règles EmuTOS apprises en chemin
  (`bios/blkdev.c`, `bios/disk.c`) : disque dur présent ⇒ la disquette n'est plus amorcée ; un
  secteur racine exécutable n'est lancé que sur un disque SANS partition reconnue ; FAT12/16 par
  le nombre de clusters (> 4084 ⇒ FAT16) — une partition de 2 Mo était lue en FAT12 et la chaîne
  du PRG cassait. (Et Rwabs ne revient jamais depuis un secteur de boot : Floprd.) Save-states **v12**.
- GUI : case UltraSatan + slot 2 (page Hard Disks), case NetUSBee exclusive d'EtherNEC (page
  Network) ; `neost.cfg` : `ultrasatan=`, `sd2=`, `netusbee=`. Headless : `--ultrasatan`,
  `--ultrasatan-id N`, `--sd1/--sd2 IMG`, `--netusbee`.

## Cuddly Demos ré-activé : l'oracle Hatari n'est pas déterministe (2026-08-19)

L'étalon `cuddly_demos` était **désactivé depuis le 2026-08-01** au motif que « l'animation
est à une PHASE différente » (au mieux 16023 px d'écart, jamais 0). Ce verdict est **réfuté**,
et le coupable n'était pas NeoST.

- **NeoST rend cette démo BYTE-IDENTIQUEMENT à Hatari** : 0 px / 114766 sur **220 trames
  consécutives** (3300-3519 côté NeoST, 3361-3580 côté oracle).
- **Ce qui manquait était la bonne trame oracle.** Hatari fait `Hatari_srand(time(NULL))`
  (`sdl/main_sdl.c`) et tire avec ce RNG la **position angulaire initiale de la disquette**
  (`fdc.c`) : la durée du boot — donc la numérotation des trames de l'AVI — **change d'un run
  à l'autre**. Mesuré : la même trame NeoST tombait sur la trame Hatari **n+61** dans un run
  et **n−2** dans un autre ; deux runs lancés à quelques secondes d'intervalle, eux, sont
  identiques. Un balayage à `frame:` figé ne pouvait pas converger — il cherchait au mauvais
  endroit, pas trop court.
- **Outillage : `oracle_scan: N`.** `hatari_oracle.sh` accepte `HATARI_ORACLE_SCAN` (extrait
  la fenêtre au lieu d'une image) et `run_etalons.py --oracle` retient la trame **identique**
  à la capture NeoST — jamais la « moins pire » : installer une image simplement proche
  figerait un écart au lieu de le signaler. Aucune correspondance ⇒ échec bruyant, et c'est
  alors une vraie divergence. Les **7 étalons qui bootent un disque** en sont équipés.
  La capture NeoST passe donc AVANT l'oracle dans `run_one` (elle sert de clé de recherche).
- Résultat : `cuddly_demos` repasse **actif, `ref_kind: oracle`, `max_diff_px: 0`** — la
  trame retenue par le scan est à **+60** de la nominale et à **0 px**. Plus aucun étalon
  désactivé dans la suite ; `--tier full` vert.

## Découplage juridique de la suite d'étalons + licences dans les paquets (2026-08-19)

Le seul point 🚨 du `TODO.md` — 79 Mo de contenu sous copyright suivi par un dépôt public —
était bloqué par un **couplage technique** : 12 étalons dépendaient de deux ROM Atari, donc
les retirer « cassait mécaniquement la CI ». Ce couplage est levé, sans perdre un pixel de
couverture. Paliers `fast` et `full` verts avant comme après.

- **4 étalons migrés sur EmuTOS, à 0 px près.** `overscan_top`, `trace_odd`, `scroll_8264`
  et `scroll_8265` tournent sur `etos192fr`/`etos256us` au lieu de `tos102uk`/`tos162us`.
  Leur programme est un **secteur de boot autonome** (il pose lui-même résolution, palette,
  base écran) : le TOS ne fait que le charger. Vérifié plutôt que supposé — capture EmuTOS
  vs capture TOS propriétaire = **0 px / 114816**, références `tests/reference/` **inchangées**,
  et l'**oracle Hatari lui-même est byte-identique entre les deux ROM**.
- **ROM absente : deux cas, plus un seul.** `run_etalons.py` sépare ROM libre et ROM
  propriétaire (`rom_is_free()`) : une `etos*` manquante reste un ÉCHEC (dépôt cassé), une
  `tos*` manquante SAUTE l'étalon et le **recense** dans un bloc dédié. Vérifié bout-en-bout
  en retirant les deux ROM : suite **verte** avec 6 étalons déclarés non exécutés, au lieu de
  8 échecs. Reste alors 7 auto-tests + 5 étalons machine (ST ×2, STE ×3).
- **Ce que la confrontation à l'oracle a révélé au passage** : `scroll_8264`/`scroll_8265`
  sont à **0 px** de Hatari → leurs références sont promues `ref_kind: oracle` (PNG 832×552
  commis, `.ppm` supprimés). Mais `overscan_top` est à **194 px (0,17 %)** et `trace_odd` à
  **22 px (0,02 %)** de l'oracle, **sur les premières lignes de trame** — écarts que leurs
  références en self-capture ne pouvaient pas voir. Indépendants de la ROM (mesurés
  identiques sous les deux). Consignés en 11ᵉ passe de `docs/HATARI_DIVERGENCES.md` et dans
  `etalons.json` ; références laissées en `snapshot`, car ni les promouvoir ni les
  re-baseliner ne serait honnête tant que l'écart n'est pas expliqué.
- **Les paquets partaient sans aucune licence.** `stage_free_data.sh` copie désormais
  `licenses/GPL-3.0.txt` (NeoST), `licenses/GPL-2.0.txt` (EmuTOS) et un `THIRD-PARTY.txt`
  qui nomme chaque composant, sa licence et **l'offre de source** ; idem dans l'APK. Les
  **8 vérifications de paquet** de `release.yml`/`pi-borne.yml` échouent maintenant si une
  licence manque — c'est l'absence de cette garde qui rendait la non-conformité invisible.
- **`NEOST_PACKAGE_NO_ATARI_TOS=1`** construit un paquet 100 % libre (EmuTOS seul), gardes
  CI comprises. Le défaut est INCHANGÉ : les paquets bureau continuent d'embarquer
  `tos102uk`/`tos162uk`. Le `TODO.md` prétendait le contraire (« les paquets bureau étaient
  déjà propres ») — corrigé, la décision revient au mainteneur.
- **L'oracle Hatari n'était nulle part.** `extern/hatari` est gitignoré, n'est pas un
  sous-module, et aucun script ne le rapatrie : sur cette machine il était **absent**, alors
  que `CLAUDE.md` et `HATARI_AUTOMATION.md` le décrivaient comme « bâti dans le dépôt ».
  Cloné, bâti (v2.6.1-devel) et la recette écrite — avec les deux options macOS sans
  lesquelles le build échoue : `-DCMAKE_OSX_ARCHITECTURES=arm64` (sinon édition de liens
  x86_64 contre des bibliothèques arm64) et `-DENABLE_OSX_BUNDLE=0` (sinon `ibtool` part en
  `Abort trap: 6` et make **supprime le binaire déjà lié**).

## Passe de cohérence doc ↔ code, 3ᵉ tour (2026-08-19)

Dernier tour, sur les docs restées hors périmètre (CLOSURE_CHANTIER, MOIRA_WINUAE_CONVERGENCE,
HATARI_AUTOMATION, empaquetage Raspberry) et sur les affirmations chiffrées, qui vieillissent
sans prévenir. Palier `fast` vert.

- **La feuille de route disait encore d'attendre l'oracle.** `TODO.md` gardait une section
  « à reprendre une fois l'oracle Hatari bâti » : l'oracle EST bâti, et **cinq de ses six items
  sont faits** grâce à lui (V1, V2, S2, S3, M1). La section dit maintenant ce qui a été traité et
  ce qui reste — beam-sync par-ligne, résidus V3, D3, `UpdateTimers`, arrondi FPU.
- **Le contrôle négatif mort traînait à un deuxième endroit** : `TODO.md` affirmait aussi
  « Détection prouvée : `NEOST_ALIGN_OFF=1` → exit 1 ». Corrigé comme dans `TEST_SOFTWARE.md`.
- **Chiffres re-mesurés plutôt que recopiés** : le palier `fast` était annoncé à ~0,1 s / ~0,3 s
  dans deux docs — il fait **~3 s** aujourd'hui (il a absorbé le round-trip save-state, le
  contrôle de la disquette livrée et quatre auto-tests de plus). La liste des auto-tests P0 et
  celle des clés relues par `--from-cfg` ont été alignées sur le code au même endroit.
- **Deux sondes d'instrumentation citées comme utilisables n'existent plus** : `NEOST_EXC_DIAG`
  (NeoST) et `NEOST_HAT_IPLDIAG` (Hatari patché). `MOIRA_WINUAE_CONVERGENCE.md` le dit désormais
  en tête — les mesures restent valables, les commandes non. Les 27 autres `NEOST_*` du doc ont
  été vérifiées vivantes une par une.
- **Dernières ancres ré-ancrées** : `updateGlueState` (Shifter, +600 lignes de dérive),
  `recordSyncWrite`, `restartVideoCounter`, `dacTable`, les quatre sites `envFlag` de
  `CLOSURE_CHANTIER.md`, `lineLenOn_`, le latch de bordure gauche dans `TODO.md`. Après quoi le
  balayage automatique ne remonte plus que des faux positifs (symboles Hatari, homonymes).

## Passe de cohérence doc ↔ code, 2ᵉ tour (2026-08-19)

Suite de la passe du jour, sur ce qu'elle avait explicitement laissé de côté : le reste des
ancres `fichier:ligne`, et les docs qui n'avaient pas encore été confrontées au code (KIOSK,
FUJINET, TEST_SOFTWARE, TODO, empaquetage). Palier `fast` vert avant comme après.

- **Cinq divergences CORRIGÉES étaient encore décrites comme des défauts ouverts** dans
  l'inventaire maître — dont une **HAUTE** et deux **ÉLEVÉES**. `HATARI_DIVERGENCES.md`
  annonçait leur correction dans le chapeau de la 3ᵉ passe, mais les entrées détaillées, elles,
  n'avaient jamais été retouchées : SCU jamais réinitialisé (`Scu::reset(bool cold)` existe et
  est appelé), propagation des NaN FPU (l'opérande réel est renvoyé quiété), SNaN→SNAN
  (`flag_signaling` distinct → `EXC_SNAN`), INQUIRY ACSI `buf[4]` (valeur fixe 31), masques
  FPCR/FPSR (`&0xFFF0` / `&0x0FFFFFF8`). Idem plus bas pour **S3** (gain LMC ×2) et **S4**
  (table DAC mesurée), que le tableau des priorités donnait pourtant ✅. Toutes marquées, avec
  le *avant* conservé et l'ancre du correctif. Un inventaire qui liste comme à faire ce qui est
  fait coûte une deuxième fois le travail.
- **Le compteur `$FF8909/0B/0D` « au cycle » était porté depuis le 2026-08-06** (`liveCounter`
  ≙ `DmaSnd_GetFrameCount`) mais restait en « Reste » dans `CYCLE_ACCURACY.md` et en TODO. Seule
  la confrontation à l'oracle de la quantification HBL du refill reste ouverte — c'est ce qui est
  écrit maintenant.
- **Un contrôle négatif qui ne contrôlait plus rien** : `TEST_SOFTWARE.md` donnait
  `NEOST_ALIGN_OFF=1 … --spec512-selftest doit échouer`. La variable a disparu du code —
  vérifié : le test sort **0**. Le vrai garde-fou est la première vérification du test, qui
  ÉPINGLE `kSpec512AlignCyc` à −25 ; c'est elle qui est documentée.
- **Inventaire des étalons refait** : la doc en listait une poignée et disait Union Demo « à
  rapatrier » alors que `etalons.json` en compte **19** (7 auto-tests + 12 étalons machine,
  `union_demo` présent mais `optional`). La liste des auto-tests P0 ignorait `--msa-selftest`,
  `--fuji-selftest`, `--enec-selftest` et `neost-selftest`.
- **`--fuji-selftest` fait 17 vérifications, pas 11** (`EXTENSIONS.md`) — les cas-limites ACSI
  (`count=0` ⇒ 256, plafond MODE SENSE, reset) ont été ajoutés sans que le compte suive.
- **L'APK Android a une interface depuis le 2026-08-11** (menu décalqué de la borne,
  `src/android/AndroidMenu.cpp`) ; `CLAUDE.md` disait encore « pas d'interface ». Ce qui reste
  vrai, et qui est conservé : il n'a jamais tourné sur un appareil réel.
- **Frontends recomptés** : l'en-tête de `CMakeLists.txt` parlait de « deux frontends » et
  `IMPLEMENTED.md` de `neost` + `neost-headless` — il y en a quatre (plus `neost_net` et
  `neost-selftest`). Le sélecteur `--cpu musashi|moira` y était encore annoncé comme un choix,
  dix lignes sous le paragraphe qui explique le retrait de Musashi.
- **Reste des ancres `fichier:ligne` ré-ancrées** (Scc, Shifter, StxImage, GemdosHd, SoftFloatX80,
  MidiAcia, DmaSound, Fpu, Acsi…) : plusieurs pointaient à des centaines de lignes de leur sujet.
- Commentaire du menu borne dans `main.cpp` : « index action 0..4 » pour six actions bouclées
  modulo 6, et une liste d'actions qui s'était arrêtée à trois.

## Passe de cohérence doc ↔ code (2026-08-19)

Relecture croisée de la documentation et du code, sans changement de comportement
d'émulation. Méthode : extraction mécanique de ce que la doc AFFIRME (chemins, cibles,
options, symboles, variables d'environnement, `fichier:ligne`) puis confrontation à
l'arbre. Le palier `fast` est vert avant comme après.

- **La release livre 8 paquets, la doc en annonçait 7.** `release.yml` construit et
  attache l'APK Android depuis le 2026-08-11 (son job `publish` COMPTE 8 et échoue
  sinon), mais `README.md` et `DEV.md` étaient restés à 7 — le tableau public des paquets
  ne mentionnait même pas l'APK. Ligne ajoutée, avec sa réserve (« pas d'interface »).
- **L'arborescence `src/` de `DEV.md` datait d'avant le découpage de `main.cpp`.** Elle
  ignorait `gui/`, `util/`, `net/`, `android/`, `tests/`, et une dizaine de composants
  `io/` (Acsi, Fpu, GemdosHd, Ne2000, Scc, StxImage, MediaScan) — c'est-à-dire la carte
  qu'on donne à lire à quelqu'un qui arrive. Refaite sur les listes de sources réelles,
  `neost_net` incluse.
- **`neost-selftest` n'était listé nulle part** alors que `run_all.py --tier fast` en
  fait son PREMIER pas : la ligne de build de `CLAUDE.md` et le « pas de tests unitaires »
  de `DEV.md` le rendaient invisible.
- **Commentaires de code qui contredisaient le code.** Les cinq signalés par l'audit
  `HATARI_MAPPING` du 2026-07-08 — jamais traités depuis — sont corrigés : « préemption
  DORMANTE » (fausse dans le modèle BLOC, qui est le DÉFAUT ; dormante seulement sous
  `NEOST_SYNC_DISPATCH`), en-tête `Scheduler.hpp` « Phase 1 : 3 sources, quantum ligne »
  (une vingtaine de sources, quantum à l'événement), « le blitter est ABSENT : NeoST ne
  l'émule pas » dans `Bus.cpp` (il l'émule, la plage est dé-fautée selon
  `machineHasBlitter`), l'intro `Mfp.hpp` « strict nécessaire : Timer C » (le MFP est
  quasi 1:1 avec `mfp.c`), et le `regs_[7]=0xFF` de `YM2149.hpp` qui laissait croire
  qu'Hatari fait pareil (micro-écart désormais écrit noir sur blanc). Un commentaire faux
  coûte plus cher qu'une absence de commentaire : il fait chercher au mauvais endroit.
- **Huit options du headless étaient analysées mais absentes du `--help`** — `--mono`,
  `--shot-every`, `--shot-from`, `--keys-at`, `--joy-at`, `--joy-script`, `--mouse-at`,
  `--version` : documentées dans `DEV.md`, utilisées par l'outillage, invisibles pour qui
  lance le binaire. Ajoutées (et le commentaire de `--from-cfg`, qui listait les clés
  relues, remis d'accord avec la boucle qui les lit).
- **Renvois morts.** `disks/utils/` (c'est `disks/etalons/`), `docs/SOUND_HATARI_DIFF.md`
  cité comme existant alors qu'il a été supprimé, `NEOST_ALIGN_OFF` retiré du code,
  `DmaSound::onFrameEnd` et `kioskAreSiblings` qui n'existent pas (`fifoRefill` et
  `neost::areSiblingImages`), `gemdos/BUILD`, un lien Markdown cassé.
- **`fichier:ligne` ré-ancrés** dans `CYCLE_ACCURACY.md` et `HATARI_DIVERGENCES.md` là où
  ils tombaient sur du code sans rapport (Blitter, Bus, Cpu68k, Fdc, Mfp, YM2149,
  Machine). Ces ancres dérivent à chaque édition : `HATARI_DIVERGENCES.md` le dit
  maintenant en tête — **c'est le symbole cité qui fait foi**, pas le numéro.

## Chasse aux bugs : auto-test EtherNEC dépendant du compilateur, `--from-cfg` amnésique (2026-08-17)

Campagne de recherche de défauts. Deux angles : **ASan+UBSan** sur toute la suite
(auto-tests, étalons, **51 jeux du corpus × 900 trames**) et **fuzzing** des parseurs
d'entrée non fiable (4000 mutations de `.stx`, 120 de `.st`/`.msa`/`.dim`). Résultat côté
mémoire : **aucun défaut** — le cœur d'émulation et les parseurs durcis tiennent. Les
trois défauts trouvés sont ailleurs, tous du même genre : une règle écrite **deux fois**,
ou une expression dont le résultat dépend du compilateur.

- **Auto-test EtherNEC : verdict décidé par le compilateur.** La lecture de l'en-tête de
  paquet tenait en `rd(0x10) | (rd(0x10) << 8)` — or les opérandes de `|` ne sont **pas
  séquencés** en C++17 et chaque `rd(0x10)` fait AVANCER le Remote DMA. Les deux octets
  de longueur pouvaient donc être pris à l'envers : l'auto-test lisait `0x2400` au lieu
  de 36. Vert en Release, **ROUGE** sur la même source compilée avec sanitizers — c'est
  ainsi qu'il est sorti. Un gate dont le verdict dépend du choix du compilateur ne garde
  rien : il serait passé rouge sur le premier portage ou la première montée de toolchain.
  Une lecture par instruction.
- **`--from-cfg` perdait le lecteur B, le modem et l'EtherNEC.** Le headless a son PROPRE
  lecteur de `neost.cfg` (dette déjà notée le même jour, ci-dessous) et il ignorait
  `diskb=`, `modem=` et `ethernec=` — trois clés que le GUI écrit, les deux dernières
  juste à côté des `fujinet_*` toutes relues, elles. Rejouer une config dans le headless
  démarrait donc **sans** disquette B ni réseau, sans un mot, alors que `--diskb`,
  `--modem` et `--ethernec` existent et fonctionnent.
- **Le rognage de ligne avait divergé.** Le lecteur GUI retire `\r` **et** les blancs de
  queue (correctif CRLF de la 0.5.2) ; la recopie côté headless ne retirait que le `\r`.
  Un `machine=st ` suivi d'une espace y retombait donc en silence sur la machine par
  défaut — exactement le défaut que le rognage GUI corrige, resté vivant dans le second
  lecteur. La règle devient `appconfig::trimConfigLine`, **une** définition pour deux
  appelants, verrouillée par 8 cas dans `neost-selftest` (**84 → 92**).

Reste, inchangé : le headless garde son propre *aiguillage* de clés (il résout en plus
les chemins relatifs au dossier du cfg). Seule la règle de rognage est désormais partagée.

## Chemins hôte unifiés, logique pure testable, `main.cpp` dégrossi (2026-08-17)

Chantier d'architecture né du constat des issues #37/#38 : la discipline du projet
s'arrête à la frontière de l'émulation. Tout ce qui est *autour* — couche hôte, données
livrées, frontend — n'avait ni module, ni test, ni gate.

- **`src/util/HostPath` : UNE définition de « chemin absolu ».** Il y en avait quatre,
  toutes écrites à la main sur la règle Unix « ça commence par `/` » : `GemdosHd`
  (corrigé la veille), les deux résolveurs de `main.cpp`, et le lambda `resolve` de
  `--from-cfg` côté headless — où le défaut de #37 était **toujours vivant**, clé
  `gemdos=` comprise. Le style de chemin (`Posix` / `Windows`) est un **paramètre
  d'exécution**, pas un `#ifdef` : c'est ce qui rend la sémantique Windows exerçable
  depuis un Mac ou une CI Linux. Personne ne pouvait le faire, donc personne ne l'a fait,
  et le lecteur C: est resté mort sur tous les paquets Windows depuis la 0.5.1.
- **Nouvelle cible `neost-selftest`** (`tests/selftest_logic.cpp`, **84 cas**) pour la
  logique qui n'a besoin ni de machine ni de ROM, et que le headless ne peut donc pas
  couvrir. Câblée au palier `fast` — un test qu'aucun script n'exécute pourrit.
- **`main.cpp` : 4981 → ~4250 lignes**, en trois unités qui n'avaient rien à faire là.
  `gui/AppConfig` (structure `Config`, analyse, écriture atomique, profils) entre dans
  `neost_core` : sans dépendance GUI, il devient **testable**, d'où les nouveaux cas
  d'aller-retour `parseConfigLine` ↔ `writeConfigKeys` (une clé écrite mais jamais relue
  est un réglage muet, invisible au démarrage), de bornage des valeurs hostiles et de
  neutralisation des noms de profils. `gui/UiCommon` (pictogrammes + `IconButton`) et
  `gui/MediaPages` (pages Disquettes / Cartouche / Disque dur / Réseau) sortent côté GUI ;
  elles s'y prêtaient sans réécriture, ayant déjà la discipline « une page ne fait rien,
  elle pose des requêtes ».

Reste identifié, non fait : le headless garde **son propre parseur** de `neost.cfg`
(`--from-cfg`), deuxième lecture du même format — à faire converger sur `gui/AppConfig`.

## La CI teste enfin le PAQUET, pas seulement son binaire (2026-08-17)

Les deux seuls défauts jamais signalés sur un paquet publié (issues #37 et #38, la veille)
avaient **la même racine** : le smoke-launch de la release amorçait le binaire — `--version`,
500 trames d'EmuTOS, capture non uniforme — et s'arrêtait là. Il n'ouvrait ni la **disquette
livrée** ni le **disque dur GEMDOS**, c'est-à-dire précisément les deux données que le paquet
embarque. Un lecteur C: mort sur tout Windows et une `diskA.st` sans système de fichiers ont
donc traversé plusieurs releases sans qu'une seule ligne rouge apparaisse.

Nouveau `tools/smoke_package.sh`, appelé par les **6 jobs** qui exécutent le paquet
(`linux-bionic`, `linux-arm64`, `raspberry`, `pi400`, `macos`, `windows`) — les 15 lignes
dupliquées dans chacun disparaissent au passage. Quatre phases :

1. version annoncée = version du paquet (contrôle existant) ;
2. boot EmuTOS 500 trames + capture non uniforme (existant, via
   `tools/check_ppm_nonuniform.py`) ;
3. **disquette livrée** : structure FAT12 de la copie DU PAQUET
   (`check_disk_assets.py --image`, nouvelle option) **et** montage réel par l'émulateur
   du paquet (le FDC doit journaliser la géométrie) ;
4. **HD GEMDOS sur chemin ABSOLU au format natif de l'hôte** (`cygpath -w` sous MSYS2) :
   le montage doit aboutir en C: **et** aucun accès ne doit être refusé par le bac à sable.
   C'est exactement la forme `D:\a\…` qui échouait sous Windows.

Vérifié sur les deux régressions : avec l'ancienne `diskA.st` écrasée le smoke sort en
échec à la phase 3, avec un montage GEMDOS qui rate il sort en échec à la phase 4.


## Retours 0.5.2 : HD GEMDOS sous Windows, disquette livrée (2026-08-16)

Deux défauts signalés par Christian Zietz (EmuTOS) sur la **0.5.2 Windows** —
issues [#37](https://github.com/habib256/neost/issues/37) et
[#38](https://github.com/habib256/neost/issues/38).

- **HD GEMDOS : tout chemin Windows était traité comme RELATIF.** `makeAbsoluteName`
  (port de `File_MakeAbsoluteName`) ne connaissait que la règle Unix « absolu = commence
  par `/` ». Sous Windows un chemin absolu commence par une **lettre de lecteur**
  (`C:\Temp\atari`) ou une racine **UNC** (`\\serveur\partage`) : le dossier glissé sur
  la fenêtre était donc préfixé du répertoire courant → `C:\…\NeoST-0.5.2\C:\Temp\atari`,
  « GEMDOS folder not found » sur un dossier pourtant valide. Ajout de
  `isAbsoluteHostPath` (lettre de lecteur + UNC) et normalisation `\` → `/` des chemins
  hôte à l'entrée (`makeAbsoluteName`, et le retour de `getcwd`, lui aussi en `\`), le
  reste du fichier comparant déjà en `/` (comme `physicalCanon`). `cleanFileName` ne
  rabote plus « `C:/` » en « `C:` » (racine du lecteur ≠ dossier courant du lecteur).
  Aucun effet sur Unix/macOS (montages absolus, relatifs et `../` revérifiés).
- **`disks/diskA.st` livrée n'était plus une disquette.** Écrasée par un test d'écriture
  secteur (commit `828bc87`, juin 2026), elle partait dans **tous** les paquets avec un
  motif binaire à la place du système de fichiers : BPB absurde, aucun secteur de boot,
  inutilisable sous TOS. Regénérée par `tools/make_floppy.py` (FAT12 720 Ko, 9x2,
  `LISEZMOI.TXT` + `NEOST.TXT` + dossier `PROGS`), avec un **numéro de série** non nul
  (détection de changement de disquette par le TOS). Vérifiée de bout en bout : sur une
  copie de travail munie d'un `EMUDESK.INF` qui ouvre `A:\*.*` au boot, le bureau EmuTOS
  liste bien le contenu (« 497 bytes used in 4 items »).
  Nouveau gate `tools/check_disk_assets.py` au palier **fast** (BPB + chaînes FAT +
  égalité bit à bit avec le générateur) : l'image ne peut plus être écrasée en silence.

## Capture souris sans bouton central (2026-08-15)

Le clic molette reste la bascule principale, avec **Ctrl+Alt+G** comme raccourci de
secours dans les frontends bureau et Web pour les trackpads et souris à deux boutons.
Seul le chord **Ctrl+Alt+G** est réservé à l'hôte ; `G` seul reste transmis normalement
à l'IKBD.

## Super Hang-On : phase MFP sans dérive (2026-08-14)

Les timers MFP A/B/C/D conservent désormais leur échéance en unités de **1/256 de
cycle CPU**, comme `CYCINT_SHIFT` dans Hatari. Auparavant chaque recharge tronquait
séparément la conversion MFP→CPU (par exemple 40106 au lieu de 40106,238 cycles pour
le Timer C 200 Hz) : la phase dérivait progressivement par rapport au faisceau et aux
écritures YM, terrain commun aux rares lignes raster transitoires et aux événements
musicaux manqués de *Super Hang-On*. L'auto-test MFP couvre l'accumulation sur 25
périodes. La phase fractionnaire est sérialisée ; save-state **v11**.

Deux défauts de présentation amplifiaient les symptômes : le bureau désactivait la
**VSync**, ce qui pouvait couper une image à une ligne aléatoire (tearing hôte), et un
seul underrun audio imposait ensuite **85 ms de silence** avant de reprendre. La VSync
est réactivée avec la boucle de rattrapage à échéance absolue (elle maintient le temps
émulé même si un swap bloque), et la ré-amorce après underrun descend à environ 20 ms,
au moins la taille d'un bloc CoreAudio. Le premier amorçage conserve la latence choisie.

## Réseau : FujiNet + modem Hayes + EtherNEC + anneau MIDI (2026-08-12)

Première ouverture de la machine émulée sur le réseau — **quatre extensions NeoST**,
toutes **OFF par défaut**, sans équivalent Hatari (`docs/HATARI_DIVERGENCES.md` §
Extensions), **sans effet sur les étalons** (réseau jamais ouvert par `run_all.py`).
Réf. complète : [`docs/EXTENSIONS.md`](docs/EXTENSIONS.md). Principe : `neost_core` reste sans
socket ni thread ; une nouvelle lib **`neost_net`** (frontends) fait l'I/O (option CMake
`NEOST_WITH_NET`, forcée OFF sur WASM/Android).

- **FujiNet virtuel** sur le bus ACSI (opcode vendeur $60, devices Fuji $70 + N1:-N8:).
  Déport de protocole HTTP/TCP/JSON et **montage d'images distantes** (démarrer une
  disquette HTTP sans un octet de code ST — validé **0 px** vs montage local). Backends
  live (sockets) / rejeu (déterministe) / hors ligne. Lib 68000 + `NWGET.TOS`
  (`dev/fujinet/`). Panneau GUI **Network**, clés `neost.cfg`. Save-state **v10**.
- **Modem Hayes** RS-232 (`--modem`) → pont TCP réel pour STiK/STinG, terminaux, BBS ;
  a nécessité `Mfp::receiveByte` (injection RX cadencée, `Scheduler::SERIAL_RX`).
- **EtherNEC** : NE2000 émulée sur le port cartouche (`--ethernec`), pour les pilotes
  STinG/MiNTnet/MagiCNet historiques ; exclusive d'une cartouche montée.
- **Anneau MIDI réseau** (`--midi-net`) : MIDIMaze jouable en ligne.

Auto-tests fil déterministes ajoutés au palier `fast` : `--fuji-selftest` (11/11),
`--enec-selftest` (5/5). Tier **full vert** (pixels inchangés). Save-state v10 :
`FujiDevice` + `Ne2000` + file RX MFP sérialisés, flags d'en-tête GEMDOS/FujiNet/EtherNEC.

## 0.5.2 — la 0.5.1, mais réellement livrée (2026-08-10)

**Aucun changement fonctionnel par rapport à la 0.5.1** : mêmes 7 paquets, même cœur
d'émulation. La 0.5.1 n'a jamais reçu ses binaires — son job `wasm` échouait, donc
`publish` (qui attend les 7 paquets) restait `skipped` et le tag pointait sur une
Release vide. Cette version reprend le même contenu sur une CI verte.

Ce qui a été corrigé pour y arriver est décrit au chantier du 2026-08-10 ci-dessous :
bundle WebAssembly reconstruit (il servait encore l'ancien défaut Mega STE + TOS Atari)
et empreinte de fraîcheur rendue reproductible d'une machine à l'autre.

## 0.5.1 — Windows, et le vrai paquet Pi 400 (2026-08-10)

Deux paquets s'ajoutent aux cinq de la 0.5 ; le cœur d'émulation est inchangé.

**Windows x64.** `NeoST-<ver>-windows-x86_64.zip` : on déballe, on lance `neost.exe`,
il n'y a rien à installer. Bâti en **MinGW-w64** (MSYS2/MINGW64) et non MSVC — le code
est écrit pour GCC/Clang et Moira exige C++20, donc la chaîne se réutilise telle quelle
au lieu de porter les options et les dépendances vers vcpkg. **Tout est lié en
statique** (libgcc, libstdc++, winpthread, GLFW) : `packaging/windows/build_mingw.sh`
inspecte les imports du binaire et REFUSE le paquet s'il dépend d'une DLL non système,
parce qu'une DLL manquante est invisible en CI (MSYS2 les a dans son `PATH`) et fatale
chez l'utilisateur. Le paquet n'est **pas signé** : SmartScreen prévient au premier
lancement. Le job CI tourne sur `windows-latest` et **exécute réellement** le binaire
produit (`--version`, boot EmuTOS 500 trames, capture non uniforme).

Portage nécessaire, quatre points — aucun changement de comportement sur POSIX :
`main.cpp` résout le dossier de l'exécutable par `GetModuleFileNameW` (la variante W :
un chemin accentué ressort en mojibake avec la version ANSI, et plus rien n'est trouvé) ;
`Acsi.cpp` et `Fdc.cpp` abandonnent `<sys/stat.h>` pour `std::filesystem` (la
write-protection reste la permission d'écriture du propriétaire, ce que Windows dérive
de son attribut « lecture seule » — même sémantique qu'Hatari) ; `GemdosHd.cpp` remplace
`realpath()` par `std::filesystem::canonical` et `statvfs()` par `GetDiskFreeSpaceExW`.
⚠ La canonicalisation Windows renormalise les séparateurs en `/` : tout le fichier
compare avec `PATHSEP`, et un retour en `\` aurait fait échouer le test de préfixe du
bac à sable GEMDOS — donc rabattu chaque accès sur la racine, en silence.

**Paquet Pi 4 / Pi 400.** `NeoST-<ver>-pi400-aarch64.AppImage`, compilé
`-mcpu=cortex-a72` (~10-20 % sur Moira). Il existait déjà, mais seulement dans
`pi-borne.yml`, un workflow manuel dont les artefacts n'étaient attachés à aucune
Release. `raspberry-aarch64` reste ce qu'il a toujours été et ce qu'il doit être :
aarch64 **générique**, du Pi 3 au Pi 5. En cas de doute, c'est le générique.

La release passe donc de 5 à **7 paquets** ; le job `publish` compte 7 et échoue sinon.

## 0.5 « Newborn » — première release taguée (2026-08-10)

Premier tag du projet. « Newborn » parce que c'est la première fois que NeoST sort du
dépôt sous forme de paquets : la machine est complète et se tient debout toute seule,
mais elle vient de naître. Ce qui suit récapitule l'état livré ; le détail par
sous-système est dans les sections datées ci-dessous.

**Interface et journaux en anglais.** Toute l'UI (barre de menus, fenêtre
`Configuration` et ses onze pages, débogueur, joystick, effets CRT, mode borne, barre
d'état, infobulles, messages transitoires) et **tous les messages de journal** —
`neost-headless --help` inclus — sont désormais en anglais. Les commentaires du code et
la documentation restent en français. Les étiquettes de journal (`[FDC]`, `[headless]`,
`[Bus]`…), les noms d'options et les champs de trace sont inchangés : rien de ce qui
analyse cette sortie ne bouge. Unités normalisées `Ko/Mo` → `KB/MB`.

**Le matériel émulé.** Quatre profils — ST, Mega ST, STE, Mega STE — de 256 Ko à 4 Mo de
ST-RAM, avec le matériel optionnel présent ou absent selon le modèle (68000 8/16 MHz +
cache et socket **FPU MC68881** sur Mega STE). Cœur 68000 **Moira** cycle-exact.
Shifter + Glue (bordures, overscan, tricks de résolution, **Spectrum 512**, scroll fin
STE), MFP 68901, ACIA 6850 + IKBD, FDC WD1772 (`.st`/`.msa`/`.dim` inscriptibles, `.stx`
Pasti), DMA/ACSI, blitter, YM2149 + son DMA STE + Microwire/LMC1992, RTC.

**Ce qu'on peut en faire.** Disque dur **GEMDOS** (dossier hôte monté en C:) et image
**ACSI**, **save-states** complets (v9, empreinte de config vérifiée), **débogueur**
(breakpoints, watchpoints, symboles, pas-à-pas instruction, désassemblage, hexa,
registres), **mode borne** plein écran pilotable à la manette, **effets CRT** opt-in, et
un `neost-headless` déterministe (traces façon MAME, captures PPM, dumps RAM/audio/série)
qui sert d'outil de diagnostic et de banc de non-régression.

**Validation.** `tools/run_all.py --tier full` passe intégralement : auto-tests logique
(glue 36, spec512 15, bus 12, MFP 16, MSA 51 — 0 échec), verdicts série de la cartouche
de diagnostic, cycle-bench, provenance des références, et les étalons pixel comparés à
**0 pixel d'écart** (dont `nocooper` / `nocooper_greetings` en overscan med-res et les
diaporamas Spectrum 512, référencés sur l'**oracle Hatari**).

Deux étalons ne gardent rien, ce qui ne dit RIEN du logiciel qu'ils visent :
`union_demo` est ignoré faute de disque optionnel, et `cuddly_demos` est désactivé faute
de **référence** — il n'y a aucune image dans `tests/reference/` pour la trame 3400
(animation centrale), donc rien à comparer. **The Cuddly Demos tourne bien** en
512 Ko ST + TOS 1.02 UK (`--machine st --mem 512k roms/tos102uk.img`) : écran-titre
complet et conforme. C'est l'outillage de non-régression qui manque, pas l'émulation.

**Paquets.** Cinq artefacts à la 0.5 (sept depuis la 0.5.1), avec leurs sommes SHA-256 : AppImage Linux
x86_64 (plancher glibc 2.27), AppImage Linux aarch64, **deux** paquets Raspberry —
`raspberry-aarch64` GÉNÉRIQUE (Pi 3 → Pi 5, aucun `-mcpu`, PGO+LTO) et `pi400-aarch64`
taillé pour le Cortex-A72 du Pi 4/400 (~10-20 % sur Moira, mais il ne démarre pas sur un
cœur plus ancien) —, `.dmg` macOS Universal 2, bundle WebAssembly. Pas de paquet Windows :
les cibles sont macOS Silicon et Linux. Contenu embarqué : liste explicite tenue
par `packaging/stage_free_data.sh` (EmuTOS, `tos102uk`/`tos162uk` des profils 520 ST /
1040 STE, `diskA.st`, polices, échantillons de lecteur) — un garde-fou refuse toute
autre ROM. ⚠ `tos102uk`/`tos162uk` sont des **ROM Atari sous copyright** : leur
redistribution est un choix assumé du projet, pas une donnée libre.

## Closure : l'image rejoint l'oracle — scroll hardware STF 4 px / stab med (2026-08-12)

Suite (et fin du volet visuel) du chantier Closure : après le crash au boot
(chantier précédent), la démo tournait mais **hachée** — logo en damier à
marches, cartons texte et photo aux couleurs striées. L'enquête (dossier
[`docs/CLOSURE_CHANTIER.md`](docs/CLOSURE_CHANTIER.md) § Cycle 6) a exonéré une
à une toutes les données (bitmap RAM, adresses par ligne, écritures palette —
11 306/11 314 identiques à l'oracle à une constante près) grâce à deux outils
neufs : un **oracle instrumenté** (`[LADDR]` dans le Hatari du dépôt : adresse
du raster à chaque ligne, deltas invariants à l'ancrage) et le **re-rendu
modèle en boucle fermée** — un pipeline Python nourri des données d'Hatari
lui-même, qui produisait le damier là où l'image d'Hatari est lisse : preuve
que l'ingrédient manquant du modèle était aussi celui du renderer.

Cet ingrédient : **le scroll hardware 4 px du STF** (`video.c:3946-3990`, qui
cite nommément « 'Closure' demo Troed/Sync »). Le retrait de bordure gauche
par bascule hi→med→lo déplace chaque ligne de 1 à 13 px selon le cycle de la
bascule retour — le « X-DISTING » de la démo — et le matériel réalise ce
déplacement en **désalignant les plans** : l'offset d'origine en octets fait
charger les bitplanes dans les mauvais registres. Port dans `renderGlueFrame` :
table par ligne (offset source, shift effectif) relative au repère calibré —
13→(+4,+5), 9→(+2,+1), 5→(0,−3), 1→(−2,−7), stab 0→(−2,−8) — l'offset
s'applique en octets à la source (la permutation de plans émerge du décodage,
comme le chemin med l'avait établi).

Deuxième pièce : **kSnapLead** — 8 octets de garde en tête des captures
par-ligne (`lineSnap_`), pour que les offsets sources négatifs restent dans le
slot. Le premier essai (repli RAM) tuait le logo d'intro SYNC : cet écran
single-buffer dessine et efface son logo en course avec le faisceau, la RAM de
fin de trame est déjà vide — précisément l'artefact que la capture au faisceau
prévient.

Résultat : logo d'intro net, grand logo lisse (les 5 discontinuités restantes
sont mesurées à l'identique sur l'oracle : bords de lettres, résidu NUL),
cartons texte et photo impeccables — **parité visuelle**. Leçons de méthode au
dossier, dont une majeure : **Hatari est non-déterministe run-à-run sur cette
démo** (ancrage de boot) — les canaux d'animation ne se comparent JAMAIS par
VBL absolu, seulement par invariants (deltas par ligne, séries de motifs,
cohérence interne). Diag `NEOST_WATCH=hex` ajouté (watch d'écriture bus daté,
coût nul désarmé). Validation : `--tier full` 39/39 vert, menu Cuddly,
Enchanted Land, Super Hang-On et Lethal Xcess pixel-identiques. ⚠ Closure se
joue en ST + tos102uk + 1 Mo (512 Ko : refus fidèle ; chemin STE non porté —
FIXME assumé chez Hatari aussi).

## Closure (Sync) boote : datation des écritures freq/res par parité d'accès (2026-08-12)

La démo **Closure** de Sync (`disks/etalons/closure.msa`, ST 1 Mo + TOS 1.02 UK)
crashait au boot (opcode illégal `$19C0` auto-généré) là où Hatari l'exécute. Un
travail d'oracle en cinq cycles (dossier complet :
[`docs/CLOSURE_CHANTIER.md`](docs/CLOSURE_CHANTIER.md)) a remonté la chaîne causale
au cycle près : la démo classe le *wakestate* de la machine en mesurant le compteur
vidéo à travers des bascules 60/50 Hz beam-racées, et NeoST datait l'écriture du
retour 50 Hz de la ligne 64 à 56 (> `Line_Set_Pal` 55, Freq_match refusé) là où
Hatari mesure 54 — ligne restée à 508 cycles, grille −4, right-off de la ligne 65
manqué, 44 octets perdus, delta `$A2` au lieu de `$CE`, verdict 0, file d'épreuves
corrompue, crash.

**Le correctif** (`Shifter::recordSyncWrite`) : la datation `fcRaw + 2` constante
devient une datation par **parité de la position de l'accès dans l'instruction** —
`+2` quand `cyclesIntoInstr() ≡ 2 (mod 4)` (la classe historique `move Dn,(An)`/abs,
tout le parc calibré inchangé), `+0` quand `≡ 0` (la classe `move An,(An)` du
classificateur de Closure, que Moira place 2 cycles après WinUAE). C'est la
transposition de la loi Hatari CE (`Cycles_GetInternalCycleOnWriteAccess` :
position de l'accès + 4). ⚠ Un premier essai « début d'instruction + 4 » uniforme
cassait nocooper de 19 361 px (les `move` vers abs.w exigent start+8) — la parité
réconcilie tout. `NEOST_SYNC_MODE=0` restaure l'ancienne datation pour l'A/B.
Validation : `--tier full` 39/39 vert (nocooper oracle et les 3 diapos spec512
compris), menu Cuddly trame 3400 pixel-identique, A/B pixel-identique sur
Enchanted Land, Super Hang-On et Lethal Xcess (2600 trames chacun).

S'ajoutent, issus de la même enquête (fidélité Hatari, étalons verts à chaque pas) :

- **Timer B positionné par ligne réelle** (`Machine::onTimerB`,
  `Shifter::timerBPosForLine`/`timerBFrameCycleForLine`) : la position du tir suit
  le DE réel de la ligne (Glue live, `(DE_start|DE_end) + 24` comme
  `Video_TimerB_GetPosFromDE`) au lieu d'une position fixe ; re-check du callback
  sur l'ÉCHÉANCE planifiée (`tbScheduledAt_`, robuste au service quantifié par
  STOP). Mesuré : 580/763 tirs à la cible Glue sur le balayage per-line de Closure.
- **MFP fidèle au reset** (`Mfp.cpp`) : GPIP initialisé à `0x00` (Hatari
  `mfp.c:523`) et bits 6 (RI) / 3 (GPU idle) au repos BAS dans `gpipInput()` —
  la table d'identité machine de Closure (`$2E22F`) converge à l'octet près.
- **Diags d'enquête** (zéro coût hors env) : `NEOST_COL_DIAG` (datation des
  écritures palette, appariable au `--trace video_color` d'Hatari),
  `NEOST_NO_SNAP` (neutralise la capture par-ligne), `[GLUP]`/`[VC]`/`[render]`
  enrichis, `NEOST_WRITE_DIAG`/`NEOST_TB_TRACE`.

Reste ouvert (consigné au dossier § Cycle 5) : le logo animé de l'effet 2 est
haché chez NeoST (interférence bitmap×palette : le remplisseur de listes de
couleurs de la démo publie sa vague plus tard dans la trame que chez Hatari —
chantier d'ordonnancement CPU intra-trame, suspects Timer B fallback / latences
IRQ / e-clock, toutes les mesures archivées).

## CI verte : bundle web reconstruit, et une empreinte qui ne dépend plus de la machine (2026-08-10)

Le job `wasm` de `release.yml` bloquait la 0.5.1 : sa garde de fraîcheur refusait le
dossier `wasm/` commité, et le job `publish` (qui attend les 7 paquets) restait donc
`skipped` — **aucune Release n'était attachée au tag**.

**La garde avait raison.** Le commit « démo par défaut en ST 1 Mo / EmuTOS » avait changé
`src/web/main_web.cpp` sans reconstruire le bundle : `wasm/index.wasm` contenait encore
l'ancien défaut Mega STE + TOS Atari. Le bundle est reconstruit
(`-DNEOST_WEB_FREE_ONLY=ON`, 9,0 Mo au total) et vérifié dans un Chromium headless — la
démo boote bien sur `Atari ST · 1 MB` / `etos192us.img`, disquette A montée, sans erreur
de page.

**Mais l'empreinte, elle, avait tort aussi.** Elle ne retombait sur la même valeur qu'à
machine identique, ce qui aurait fait rougir la CI *sans* qu'aucune source bouge :
`find | sort` classe selon la **locale** (le poste macOS en `en_US.UTF-8`, le runner en
`C`, et un `/` ou un `_` suffit à départager deux chemins dans l'ordre inverse), `find`
compte les fichiers **non suivis** traînant dans `src/`, et `sha256sum` n'existe pas sur
un macOS sans coreutils. `tools/wasm_stamp.sh` liste désormais par `git ls-files` (tri
par octets, fichiers suivis seulement), recompose lui-même chaque ligne
« empreinte + chemin » et accepte `sha256sum`, `shasum` ou `openssl` — vérifié
identique entre les trois.

**Et l'artefact part avant la garde** (`if: always()`) : quand elle se déclenche, le zip
que le job vient de construire EST le bundle à recommiter, donc on le récupère depuis la
CI sans installer emsdk. Marche à suivre dans `DEV.md` § *Builds spécialisés*, qui
pointait encore le `deploy-web.yml` supprimé.

## CI : 8ᵉ paquet — l'APK Android entre dans release.yml (2026-08-11)

Nouveau job `android` dans `release.yml`, sur le modèle des sept autres : chaque
push/PR vérifie que l'APK se construit, un tag l'attache à la Release
(`NeoST-<ver>-android-arm64-debug.apk` — signé clé de DEBUG, installable tel quel ;
le projet n'a pas de clé de store et n'en aura pas dans le dépôt). Le job `publish`
compte désormais **8** paquets et ramasse aussi les `.apk`.

Le job rejoue exactement la recette locale : JDK 17 **complet** via setup-java (un
JRE n'a pas `jlink`, dont AGP a besoin — piège documenté), composants SDK épinglés
aux versions du README (NDK 27, CMake 3.22, API 34) sur le SDK préinstallé du
runner, `fetch_sdl.sh`, `build_apk.sh debug` (garde-fous bibliothèques/assets
intégrés), puis vérification STATIQUE du manifeste (`aapt dump badging` : paquet,
activité lançable, arm64-v8a) — pas d'appareil ni de KVM sur les runners ; le cœur
arm64 est validé par ailleurs (bit-exact sous qemu, cf. packaging/android/README.md).

Avant de pousser, le chemin CI a été **répété dans un clone frais** du dépôt
(checkout vierge → sous-module imgui → fetch_sdl → build_apk) : c'est ce test qui
prouve que le dépôt commité contient tout — le poste de dev, lui, avait déjà tout
sous la main.

Au passage, le job `wasm` du push précédent était tombé au rouge sur sa garde de
fraîcheur — à raison : `SOURCE_STAMP` avait été écrit AVANT le `git add` complet,
et l'empreinte (assise sur `git ls-files src/**`) a changé quand les nouveaux
fichiers Android sont entrés dans l'index. Le bundle, lui, était bon : reconstruit
pour vérifier, il ressort **identique au bit près** au commité (même md5) — emcc
est reproductible à version fixe sur la même machine. Empreinte réécrite ; leçon :
`--write` toujours APRÈS le stage complet.

## Plein écran WASM : zoom adaptatif (2026-08-11)

Le plein écran de la démo web montrait le CADRE COMPLET, bordures comprises — l'image
utile flottait, petite, au milieu des bandes. Il applique désormais le **zoom
adaptatif** du mode borne : cadré sur la ZONE ACTIVE (rectangle matériel, jamais au
pixel → zéro saccade), et **buffer entier dès qu'une démo ouvre les bordures**
(hystérésis ~0,6 s). En fenêtré, rien ne change : le « moniteur » de la page montre
les bordures, c'est son charme.

- **Calcul partagé, pas recopié** : `stContentRegion` (latches d'hystérésis compris)
  quitte `main.cpp` pour `core/Framing.cpp` — bureau, kiosk et WASM appellent la même
  fonction. C'était la prochaine copie divergente en puissance, après `AudioMix` (son)
  et `MediaScan` (ludothèque) cette même semaine.
- Le shell signale `fullscreenchange` au cœur (`neost_set_fullscreen`) — l'écouteur
  couvre aussi la sortie par Échap, que le bouton ne voit pas.
- **Piège mesuré, pas supposé** : en plein écran, le port GLFW d'Emscripten
  redimensionne LUI-MÊME le canvas à la taille de l'écran (640×400 demandés, 800×600
  imposés) — compter sur la taille intrinsèque du canvas pour le ratio aurait ÉTIRÉ
  l'image. Le letterbox est donc fait au viewport GL (recette de `drawStKiosk` et du
  frontend Android), et le canvas est rendu à Emscripten pendant le plein écran ;
  `syncCanvasSize` compare à la taille RÉELLE du canvas pour reposer le ratio fenêtré
  à la sortie.

Vérifié dans Chrome headless (Puppeteer, clic de confiance sur le vrai bouton) :
l'image plein écran mesure un ratio de **1,605** (zone active = 1,600 attendu ; le
cadre complet ferait 1,507, l'étirement écran 1,333), et la sortie de plein écran
restaure le canvas 832×552. Le repli overscan n'a pas été rejoué en navigateur : sa
logique est le code du bureau, déplacé tel quel.

## Paquet Android : premier APK qui tourne (2026-08-11)

Quatrième plateforme. `packaging/android/build_apk.sh` produit un **APK arm64-v8a**
(Android 5.0+) : la machine démarre sur EmuTOS, l'image et le son sont là, la souris se
pilote au doigt et une manette physique tient le port joystick 1.

**Le portage n'est pas celui du GUI, c'est celui du WEB** — et ce n'est pas un choix
esthétique : `src/main.cpp` rend en OpenGL mode immédiat (`glBegin`/`GL_QUADS`) et
pilote son interface avec `imgui_impl_opengl2`, deux choses qui n'existent pas sur
Android. Le frontend web, lui, rendait déjà en **GLES 2** et produisait son son au
modèle « push ». `src/android/main_android.cpp` en est la transposition : même shader,
même chaîne audio partagée (`core/AudioMix.cpp`, extraite la veille), même cadence sur
le **temps émulé** — un tour de boucle exécute 0, 1 ou 2 trames selon ce que le temps
réel réclame, jamais « une trame par image écran ».

Le cœur est repris **tel quel** : aucune ligne de `neost_core` n'a bougé. C'est le
dividende du découplage « le cœur ne dépend pas du GUI ».

- **SDL2** fournit ce que GLFW ne sait pas faire ici (fenêtre, contexte GLES, cycle de
  vie, tactile, manettes, audio). Vendorisé **non commité** dans `extern/SDL2` comme
  Hatari — `packaging/android/fetch_sdl.sh` le récupère.
- **Branche `if(ANDROID)` du CMakeLists racine**, sur le modèle exact de `if(EMSCRIPTEN)` :
  on ne construit que le cœur et le frontend de la plateforme. Gradle appelle ce
  CMakeLists — pas de définition dupliquée de `neost_core`.
- **Données embarquées : EmuTOS + `diskA.st` seulement** (~1 Mo), avec un garde-fou qui
  refuse tout autre fichier. Aucun TOS Atari, aucun jeu : le Play Store est plus strict
  que nos paquets de bureau. Déballage dans le stockage interne au 1er lancement.
- **Entrées v1** : glissé = souris relative (le bureau GEM se pilote ainsi), appui bref
  = clic gauche, deux doigts = clic droit, manette SDL → port 1.

**Validation sans appareil.** Ni `/dev/kvm` ni téléphone ici, donc l'APK est vérifié
statiquement (bibliothèques natives, assets, manifeste, classes SDL dans le dex,
`SDL_main` exporté) — et le **cœur** est validé sur l'architecture cible autrement :
compilé pour ARM64 Linux et lancé sous `qemu-aarch64`, il rend une image et un son
**identiques au bit près** au x86-64 (`cmp` sur PPM et WAV). Perf : 1000 trames ST en
7,1 s sous QEMU, qui coûte lui-même un facteur 5 à 10.

Trois pièges consignés dans `packaging/android/README.md` : `jlink` absent des JRE
headless (le plugin Android en a besoin, et le message ne le dit pas), Gradle 8.1 du
gabarit SDL2 qui refuse le JDK 21 (d'où Gradle 8.9 + AGP 8.5.2), et
`-DNEOST_ANDROID_APP=OFF` pour bâtir le cœur seul sans SDL.

**Interface : le menu borne, décalqué (même jour).** Plutôt qu'inventer une interface
mobile, on reprend la grammaire du **menu borne** — elle a été pensée pour être lue à
distance et pilotée sans clavier, ce qui est exactement la contrainte d'un téléphone :
voile sombre et machine EN PAUSE, ludothèque en rangées énormes avec le disque inséré
en vert et les **suites** du jeu en cours teintées et remontées en tête, **insérer ne
redémarre pas** (seul `RESTART` relance), et une page **clavier** ancrée en bas où la
machine continue de TOURNER — c'est ce qui permet de répondre à un « PRESS SPACE ».

Le tri de la ludothèque n'est pas recopié : `kioskScanDisks` est extrait en
**`io/MediaScan`**, partagé par la borne et Android (scan borné, détection des suites
par préfixe/suffixe communs, ordre de proximité). Vérifié : monté sur un *Blood Money*,
l'autre version du même jeu ressort en 2ᵉ position.

Deux écarts assumés avec la borne, dictés par le support : les rangées sont **tapables**
(pas seulement navigables au curseur), et les actions passent sur une **rangée
horizontale** — empilées comme sur un téléviseur, elles ne laissaient que deux jeux
visibles sur un écran de téléphone en paysage.

**`neost-menu-preview`** : le menu ne dépendant que d'ImGui et de `io/MediaScan`, une
cible de bureau (`EXCLUDE_FROM_ALL`) le dessine dans une fenêtre au format d'un
téléphone. C'est elle qui a rattrapé l'erreur du premier jet — des tailles en pixels
multipliées par l'échelle alors que la police l'était déjà : rangées deux fois trop
hautes, deux jeux visibles, actions hors cadre. Sans appareil sous la main, dessiner
une interface à l'aveugle n'est pas une option.

**Chasse aux bugs (même jour), 8 corrections** — presque toutes trouvées en comparant
mon code à ce que la borne fait DÉJÀ, la recette exacte étant à trois écrans de la
table de touches que j'avais recopiée :

- **injection touche/clic** : la borne MAINTIENT 4 trames puis relâche, et refuse
  toute nouvelle injection pendant le maintien. Mon premier jet faisait le clic
  down+up dans la même trame (ratable par un jeu qui scrute chaque VBL) et laissait
  un 2ᵉ tap rapide écraser le relâchement en attente — touche « collée » côté ST,
  le bug même que le commentaire kiosk décrit ;
- **clic fantôme** : taper le bouton MENU envoyait aussi un clic gauche au ST
  (gate `WantCaptureMouse`), et un FINGERUP avalé par l'interface désynchronisait
  le compteur de doigts (le tap suivant passait pour un clic droit à deux doigts) ;
- **boucle libre** : depuis que le menu est redessiné à chaque itération, swap
  immédiat + `Delay(1)` ≈ 50 rendus par trame émulée — vsync ON (repli si refusé),
  la cadence d'émulation restant sur le temps émulé ;
- **data race** : `g_primed` (thread audio) était écrit par le thread principal au
  retour de veille — retiré, l'anneau se gère seul ; underruns passés en atomique
  et JOURNALISÉS (~1 msg/5 s, comme le natif) ;
- **contexte EGL perdu en veille** : texture/programme/VBO recréés au retour au
  premier plan (écran noir muet sinon, sur les appareils qui ne préservent pas le
  contexte) ; troncation UTF-8 du pied de page calée sur un bord de point de code.

**Ce n'est pas fini** : pas de stick virtuel, pas d'import de disquettes (SAF), pas de
sélecteur de ROM ni de réglages, pas de sauvegarde d'état, pas d'effets CRT — et rien
n'a tourné sur un appareil réel.

## Son de la démo WASM : les samples redeviennent audibles (2026-08-11)

Symptôme rapporté : dans le navigateur, « les samples ne s'entendent presque pas ».
La mélodie passait, la batterie non.

**Cause.** La chaîne de mixage vivait en TROIS copies : `Audio::produceFrame` (GUI),
le dump `--sound-dump` du headless — dont le commentaire disait déjà « même chaîne
que » — et le frontend web. Cette troisième copie était restée sur l'ANCIENNE API :
la page TIRAIT des échantillons quand son `ScriptProcessorNode` réclamait un bloc
(~43 ms) et le cœur synthétisait alors en lisant les registres du YM **en direct**.

Or tout ce qui fait un sample sur ST module le son SOUS la trame : un digidrum écrit
le registre de volume à plusieurs kHz, le sync-buzzer réarme l'enveloppe en rafale,
les bruitages DMA durent quelques millisecondes. Échantillonner ça une fois par bloc,
c'est n'en garder qu'un point sur mille : la modulation disparaît, et il ne reste que
ce qui varie lentement — la mélodie. Le son n'était pas « trop faible », il était
**aplati**.

**Correctif.** La chaîne devient une unité du cœur, `core/AudioMix.cpp`, appelée par
les trois frontends : YM horodaté (`synthesizeFrame`), DMA STE horodaté (`mixStereo`),
HPF, gains et tonalité LMC1992, dans cet ordre — celui qui a été calé contre les WAV
oracles. Le web produit désormais le son **par trame émulée**, juste après `runFrame`,
comme le natif ; la page ne fait plus que mettre en file et sortir.

Détails qui comptent :

- **`setCycleClock` armé côté web** (PSG et son DMA). C'est la ligne sans laquelle
  rien ne marche : `synthesizeFrame` rend le jeu de registres « audio », que SEULS les
  événements horodatés mettent à jour — sans horloge, la machine est muette. Vérifié
  en le manquant : la sortie tombait à zéro absolu.
- **Sortie stéréo** (elle était mono) : le DMA STE est stéréo et le LMC1992 panoramique.
- **AudioWorklet** quand le navigateur le sait, repli automatique sur
  `ScriptProcessorNode` (déprécié) sinon. Le mixage vit alors sur le thread audio : un
  à-coup du thread principal — qui porte l'émulation ET le rendu — ne coupe plus le son.
- **File d'attente avec coussin de 90 ms**, amorçage, et **asservissement de débit** :
  la page renvoie sa profondeur de file, le cœur ajuste de ±8 échantillons par trame
  (≤ 0,8 % de hauteur, inaudible) pour absorber la dérive entre l'horloge de
  l'AudioContext et celle de la machine. Garde-fou des deux côtés : une sortie qui ne
  consomme pas (contexte suspendu avant le geste utilisateur) ne fait plus enfler la
  file de ~380 Ko/s.
- **Curseur de volume** dans la page, appliqué par le cœur en rampe anti-clic.

**Nouvel étalon : `tools/make_digidrum_test.py`.** Une disquette bootable qui joue un
digidrum — mixeur YM à $3F (ni tonalité ni bruit, seul le DAC de volume sort) et Timer A
à 7 979 Hz écrivant une table de 8 points → carré de ~997 Hz. C'est le test qui
DISCRIMINE : une synthèse « en direct » ne peut pas le rendre. Le disque `make_dmasnd_test`,
lui, joue un flux continu et sortait au même niveau AVANT comme APRÈS — il mesure le
niveau, pas la fidélité temporelle.

Mesures (Chrome headless, analyse spectrale de la sortie réelle du navigateur) :

| digidrum ~997 Hz | avant | après | référence native |
|---|---|---|---|
| raie dominante | 211 Hz (fantôme) | **1008 Hz** (1 case de FFT) | 996 Hz |
| saillance | ×6 | **×28** | ×104 |
| niveau | −19,8 dBFS | **−13,7 dBFS** (volume 80 %) | −12,7 dBFS |

Non-régression du natif prouvée au bit près : `--sound-dump` avant/après l'extraction
donne des WAV **identiques** (`cmp`), sur le test DMA STE et sur une démo ST.

## Profils de réglages nommés (2026-08-10)

`neost.cfg` **était** déjà écrit tout seul à chaque changement — mais il n'y a qu'UNE
configuration courante, et l'émulateur sert des attelages incompatibles : une démo
Spectrum 512 veut 512 Ko + TOS européen (50 Hz), un crack veut 1 Mo, une image `.stx`
veut son lecteur B. Refaire la manœuvre à chaque fois, c'est exactement ce que la barre
d'état du 2026-08-07 a montré comme source n°1 de faux rapports de bug.

**Page `Profiles`** (fenêtre Configuration, ou `Machine → Settings profiles…`) :
on nomme la configuration EN VIGUEUR, on la retrouve d'un clic. Un fichier
`profiles/<nom>.cfg` par profil, à côté de `neost.cfg` et **au même format** —
lisible, éditable, copiable d'une machine à l'autre. `Load` / `Overwrite` / `Delete`
(en deux temps : le bouton devient `Delete?` + `Cancel`).

Un profil enregistre les réglages, pas l'état de la machine : modèle, RAM, FPU, ROM,
supports montés (A, B, cartouche, GEMDOS, ACSI), moniteur, CRT, son, entrées. Il laisse
volontairement dehors l'**horloge** (`rtc=`, état machine), les **dossiers ROM de la
borne** (`kiosk_romdir=`, propre à l'installation) et la **disposition de l'interface**
(`dock=`, `showXxx=`, `uiVersion=` — cousins d'`imgui.ini`) : rappeler un profil ne doit
pas déplacer les fenêtres de l'utilisateur.

Trois points de mise en œuvre, tous conséquences de choix déjà faits dans le fichier :

- **Un seul format, deux lecteurs.** `parseConfigLine` / `writeConfigKeys` /
  `writeConfigAtomic` sont extraits de `loadConfig`/`saveConfig`. Charger un profil, c'est
  partir de la config courante et lui appliquer les lignes du fichier : ce qu'un profil
  **ne dit pas** ne change pas, sans liste de recopie champ par champ à tenir à jour.
  L'écriture atomique (tmp + rename, échec = ancien fichier intact) profite aux profils.
- **Application par les requêtes existantes.** Charger pose `reqRebuild` (`applyConfig`
  refait déjà modèle/RAM/FPU/ROM/cartouche/HD/moniteur/FDC en une reconstruction) et,
  pour les lecteurs — qu'`applyConfig` conserve délibérément —, les requêtes normales de
  montage, qui valident l'image avant d'écrire quoi que ce soit.
- **Nom de fichier assaini.** Le champ est libre : séparateurs de chemin, caractères de
  contrôle et réservés Windows sont retirés, points et espaces de bord rognés, nom vide
  refusé (`../../evil` → `evil.cfg`, dans `profiles/`). Les accents, eux, passent.

**En borne, rien ne s'écrit** : les profils restent consultables et chargeables, mais
`Save`/`Overwrite`/`Delete` sont grisés et doublés d'une garde côté boucle — l'invariant
« la borne repart identique » vaut aussi pour ce dossier.

**Au passage : le son du lecteur était un réglage sans mémoire.** La case « Floppy drive
sound » se cochait, se décochait… et repartait à ON au lancement suivant : `drivesound=`
n'existait pas. Clé ajoutée. Le câblage (brancher `DriveSound` sur l'`Audio`, armer le
sink `FdcSound`) suit désormais la **disponibilité** des échantillons et non le réglage,
sinon démarrer son coupé rendait la case sans effet pour toute la session ; et la case est
grisée si `roms/drivesound/` manque, au lieu de mentir.

La rangée de préréglages matériels en haut de la fenêtre s'appelle maintenant `Presets:`
(520 ST / 1040 STE / Mega STE) — elle ne garnit que les champs « en attente », là où un
profil est une configuration complète de l'utilisateur.

## Interface : une fenêtre « Configuration » unique + barre d'état (2026-08-07)

Réorganisation de la GUI. Le diagnostic tenait en trois points : **trois idiomes pour la
même action** (une cartouche se montait par le menu *et* par une fenêtre, un disque dur en
tapant un chemin *ou* par une fenêtre, une disquette par une fenêtre seulement), un menu
`Machine` **fourre-tout** (actions + configuration matérielle + réglage d'émulation +
Quitter), et surtout **aucun affichage de l'état courant** — or les deux « bugs » signalés
ce jour-là (démo déchirée, jeu qui plante) étaient des faits de configuration invisibles :
ROM `us` en 60 Hz NTSC, et 512 Ko là où le crack veut 1 Mo.

**Fenêtre `Configuration`** (⚙ dans la barre d'outils, `Machine → Configuration…`),
ancrable et non modale : colonne de navigation à gauche (Machine · Mémoire · ROM/TOS ·
Disquettes · Disques durs · Cartouche · Écran · Son · Entrées · Émulation · Borne), page à
droite, rangée de profils en haut (520 ST / 1040 STE / Mega STE). Elle **absorbe** six
sous-menus et les **trois fenêtres-bibliothèques** (Disk/Cart/Hard Disks, supprimées) : il
n'y a désormais qu'**une** façon de monter un support. Elle ne fait rien elle-même — tout
sort en requêtes consommées en fin de trame, la discipline des anciennes bibliothèques.
La page ROM affiche **50 Hz PAL / 60 Hz NTSC** (en orange) à côté de chaque image, d'après
le suffixe pays.

**« Appliquer et redémarrer »** : modèle, RAM, FPU et ROM ne relancent plus la machine à
chaque clic. Ils sont mis **en attente**, le pied de page les compte (« 3 réglages
matériels en attente ») et un seul bouton reconstruit une fois — avec le rattrapage TOS
≥ 2.06 du Mega STE (`pickTosForMachine`) au passage. Les **montages** restent immédiats :
monter est une action, pas un réglage.

**Barre d'état permanente** : `Mega STE | 4 Mo | tos206fr | 50 Hz PAL | A: … | B: … |
C: gemdos/ | 50,1 fps`. Chaque segment est cliquable et ouvre SA page. C'est le remède
direct aux faux rapports de bug ci-dessus.

**Lecteur B en GUI.** Le cœur le gérait depuis toujours (`Fdc::loadImage(path,1)`,
`--diskb` du headless) ; seule l'interface l'ignorait — alors que Lethal Xcess ne DÉMARRE
qu'avec son disque 2 monté. Chaque ligne de la ludothèque a maintenant deux boutons
`[A] [B]` ; mémorisé (`diskb=`).

**Glisser-déposer** sur la fenêtre : un DOSSIER se monte en C: (GEMDOS), une image
`.st/.msa/.dim/.stx` va dans le lecteur A, une image de disque dur en ACSI, un TOS devient
la ROM. `.img` étant ambigu (roms/, carts/ et hd/ en sont tous pleins), l'arbitrage se fait
sur la **taille et l'en-tête** (BRA.S `$602E` + ≤ 512 Ko = TOS ; ≤ 128 Ko = cartouche ;
au-delà = disque dur), pas sur l'extension. Ignoré en borne (config figée).

**Barre de menus ramenée à quatre entrées** — Machine (actions + états + borne + Quitter),
Affichage (moniteur, zoom, CRT, ancrage), Fenêtres (**inspection seulement** : hex, CPU,
joystick, débogueur), Aide (**liste des raccourcis**, jusqu'ici nulle part). La barre
d'outils ne porte plus que des verbes (⚙ ⟳ ⏻ ◐ + volume) : ses bascules de fenêtres
faisaient doublon avec le menu.

**Dossier `hd/`** (+ `hd/README.md`, contenu gitignoré) : un **dossier** dedans = un lecteur
GEMDOS, un **fichier** image = un disque ACSI. Le scan des images n'est pas récursif, sinon
les `.img` rangés dans un lecteur GEMDOS seraient proposés comme disques durs.

Migration : `neost.cfg` gagne `diskb=`, `showCfg=`, `uiVersion=` (les `showDisk=/showCart=/
showHd=` d'avant sont ignorés) ; `uiVersion` resème **une fois** la disposition ancrée,
sans quoi un `imgui.ini` existant garderait des nœuds pour des fenêtres disparues et
laisserait la fenêtre Configuration flotter au-dessus de l'écran ST.

Validé en GUI (captures à l'appui) : montage GEMDOS → barre d'état `C: gemdos/` ; profil
Mega STE → « 3 réglages en attente » → Appliquer → `[Bus] TOS chargé : tos206fr` et barre
d'état `Mega STE | 4 Mo | tos206fr` ; lecteur B → `B: diskA.st` ; page ROM avec ses badges
50/60 Hz ; **mode borne intact** (`--kiosk` : plein écran, aucun chrome). ⚠ Non faits, et
c'est ce que la colonne de gauche est faite pour accueillir : pause/avance rapide,
capture d'écran, protection en écriture, imprimante/RS-232, plein écran hors borne.

## Effets CRT : version GLSL choisie à l'exécution (débloque le Raspberry Pi) (2026-08-07)

Sur la borne Pi, activer les effets CRT échouait avec « **shader indisponible : GLSL 1.50
is not supported. Supported versions are: 1.10, 1.20, 1.30, 1.40, 1.00 ES, 3.00 ES** » : le
préambule `#version 150` était **codé en dur** dans `OpenGLShader.cpp` alors que le V3D des
Raspberry Pi (Mesa) plafonne à **GLSL 1.40**. Le corps des shaders CRT, lui, n'utilise que
des constructions **GLSL 1.30** (`in`/`out`, `texture()`, `fwidth()`) — il n'y avait rien à
réécrire, seulement à cesser d'exiger 1.50.

Le dialecte est maintenant déduit de `GL_SHADING_LANGUAGE_VERSION` puis essayé **en cascade
150 → 140 → 130** (`#version 300 es` si le contexte est GLES natif — Pi en KMS/Wayland,
Emscripten). La cascade est un filet et pas une coquetterie : un pilote peut annoncer une
version et la refuser dans *ce* contexte, seule la compilation réelle tranche. Les échecs
des tentatives intermédiaires sont silencieux et `errorOut` est vidé en cas de succès —
sinon le panneau afficherait « shader indisponible » alors que la pile est prête.

Diagnostic : une ligne au démarrage dit ce qui a été retenu **et** ce que le pilote annonce
— `[CRT] GLSL 140 (pilote : 1.40)`.

Validé bout en bout sous Mesa llvmpipe avec la version forcée (`MESA_GL_VERSION_OVERRIDE` /
`MESA_GLSL_VERSION_OVERRIDE` — ⚠ le pilote propriétaire NVIDIA les ignore, il faut
`LIBGL_ALWAYS_SOFTWARE=1 __GLX_VENDOR_LIBRARY_NAME=mesa`) : pilote 4.60 → 150, **1.40 → 140
(cas Pi)**, 1.30 → 130, chaque fois « pile d'effets CRT prête » et, capture de fenêtre à
l'appui en 1.30, l'image bien rendue à travers la pile. ⚠ **Pas encore rejoué sur le Pi
lui-même** ; la ligne `[CRT] GLSL …` le confirmera. Une pile limitée à GLSL 1.20/ES 1.00
échouerait encore (il faudrait repasser en `attribute`/`varying`/`texture2D`/
`gl_FragColor`) — ce n'est pas le cas du Pi 4. Le reste de la pile (FBO `GL_RGBA8`, VAO)
passe sans retouche sur V3D.

## IACK MFP +4 cyc (raster Super Hang-On verrouillé à l'oracle) + chaîne son STE fidèle (2026-08-06/07)

**IACK MFP vectorisé : 12 → 16 cycles** (`Cpu68k.cpp`, `g_iackMfp`). Mesuré à l'oracle
Hatari **instrumenté** sur Super Hang-On EN JEU (banc souris déterministe + `--trace
video_color` + diag `[HEXC]` étendu à la position vidéo) : la chaîne fixe « exception
Timer B → handler → `stop #$2100` → HBL pendante prise au stop » fait 144 cycles chez
Hatari, 140 chez NeoST — le `CPU_IACK_CYCLES_MFP_CE=12` d'Hatari (« not measured ») ne
compte pas le cycle bus d'IACK lui-même. Après correction, l'histogramme des écritures
palette du raster in-game est **verrouillé à ±1 point** sur ~2 500 trames (1re écriture
de paire {104..128}, réveil STOP 40/40/20 inchangé et déjà exact). C'était la cause des
« lignes transitoires » de SHO — aucun des candidats de la 5ᵉ passe. Nouveaux outils :
`NEOST_PAL_TRACE_ALL` (trace palette cumulative par trame), `NEOST_RAISE_DIAG`/
`NEOST_RAISE_WINDOW` (fenêtre de différé ipl_fetch, opt-in — mesuré : les frontières de
prise d'IRQ d'Hatari CE correspondent au commit SANS différé), événements fifo
`leftdown`/`leftup` ajoutés à l'oracle. Étalon `nocooper` recalé d'une trame (méthode de
sa note, 0 px bit-identique). Détail complet → `docs/HATARI_DIVERGENCES.md` § 10ᵉ passe.

**Son STE — chaîne de sortie alignée sur Hatari** : signe DMA **×−1** (le LMC1992 inverse
le canal DMA — phase relative YM↔DMA) ; **HPF sous-sonique déplacé sur le MIX YM+DMA** en
STE (le YM entre brut dans le mix, comme sound.c/dmaSnd.c — DC du DMA filtré), GUI +
`--sound-dump` + WASM ; correcteur LMC1992 en **plateaux 1er ordre Savinkoff**
(118.2763/8438.756 Hz, port exact — remplace le RBJ 2e ordre 200/8000) ; horloge YM
**250 663 Hz** réels (MCLK/128 — l'ancien 250 000 jouait ~4,6 cents bas). Validation :
selftests + tier full verts, étalon `make_dmasnd_test` (fetch au faisceau) inchangé.

**Événements échus dispatchés au point d'IACK** (`NEOST_IACK_SYNC`, défaut ON) — port du
`CycInt_Process()` que Hatari appelle juste avant la séquence d'IACK (newcpu.c:2938-2946) :
un timer expirant dans la fenêtre « frontière d'instruction → IACK » n'avait pas posé son
bit IPR quand le vecteur MFP était élu. Mesuré : un événement est échu à **5,7-7 % des
IACK**. Le dispatch **rebase le quantum** au préalable (`Cpu68k::rebaseQuantumAndSync`),
comme le saut STOP : sans ce rebase le temps est compté deux fois et tout le raster glisse
de ~16 cycles — régression attrapée par le banc SHO avant commit.

**Bug hunt (workflow 6 chasseurs + vérification adversariale) — 12 correctifs**, dont :
niveau DMA STE **−6 dB** (le ÷4 d'Hatari pré-compense AUSSI le ×2 des gains LMC —
`kDmaGain` −0.1875) ; `reconfigure` à chaud qui perdait le placement du HPF (STE↔ST) ;
chaîne LMC non gatée et `adjustMachineForTos` absent côté **WASM** (YM +6 dB sur ST,
TOS incompatible = écran figé) ; **scroll fin STE par ligne** dans le re-rendu fenêtré
(`renderGlueFrame` appliquait la valeur de fin de trame à tout l'écran → save-state
**v9**, capture `lineScrollSnap_`) ; bit `LOOPING` forgeable dans un save-state
(pointeur-membre nul en Release) ; $FFFA31-3F void (0xFF, sans wait-state) au lieu de
RAM cachée ; $FF8900/8920 fidèles ; `--load-state` qui écrasait `--joy` ; défauts de
config annoncés. Détail → `docs/HATARI_DIVERGENCES.md` § 10ᵉ passe (bug hunt).

## Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)

Campagne menée **au callgrind**, sur un profil de boot TOS et un profil en jeu (les deux
ont la même forme : les points chauds du cœur ne dépendent pas du logiciel émulé).
Méthode, mesures ligne à ligne, fausses pistes et pièges → **[`docs/PERFORMANCE.md`](docs/PERFORMANCE.md)**.
**Aucune valeur émulée ne change** : les 15 étalons pixel-exacts sont restés à 0 pixel
d'écart après chaque étape, et les variantes de compilation ont été vérifiées
octet-identiques sur des captures de 6801 et 29500 trames.

**Bus — le décodage MMU était refait à chaque octet.** `mmuTranslate()` relisait la
config `$FF8001`, retraversait deux `switch` de taille de banque, divisait pour obtenir
la taille de RAM posée, puis rejouait le remappage RAS/CAS : 12,8 M d'appels pour
300 trames de boot, ~39 instructions pièce, ~20 % du programme avec ses appelants. Le
résultat ne dépend pourtant que de deux entrées (l'octet de config, la taille de `ram[]`)
— il est désormais mémorisé et *revalidé par comparaison de ces deux entrées*, ce qui
rend impossible l'oubli d'un site d'invalidation. Le cache ne retient qu'une chose : la
longueur du préfixe où la traduction est l'**identité** (le cas dès qu'une banque est
annoncée à sa taille réelle, ce que fait tout TOS après son sizing) — démonstration en
commentaire dans `Bus::rebuildMmuCache`, plus un contrôle en debug contre le décodage
complet. `read8`/`read16`/`write8` passent dans l'en-tête et s'inlinent chez l'appelant.
⚠ La première version ne couvrait que la RAM et ne rendait que −4,4 % : **le code du TOS
s'exécute depuis la ROM**, donc chaque mot d'opcode repassait par le chemin lent. La
fenêtre ROM ajoutée au chemin rapide a porté le gain à −20 %.

**Ordonnanceur — le balayage chaud n'était pas celui qu'on croyait.** Le profil
l'attribuait à `Machine::runFrame` : l'appelant est `sched.nextDue()`, une fois par bloc
CPU. Plutôt que d'accélérer le balayage, on l'a supprimé — `runTo` calcule le minimum des
échéances *pendant* sa passe de dispatch (qui parcourt déjà les mêmes sources), et
`schedule()` tient `nextDue_` **exact** au lieu de simplement minorant, si bien que
`nextDue()` répond en O(1). Un `assert` compare le cache au balayage complet en debug.
Une variante intermédiaire — minimum sans branche sur tableau plein — a été **essayée
puis retirée** : elle coûtait +1,4 % d'instructions (parcourir 19 entrées coûte plus que
d'en sauter 5).

**Shifter — deux tables au lieu de deux boucles.** Le dé-entrelacement des bitplanes se
faisait bit à bit (4 décalages + 4 masques par pixel) : remplacé par une table de 256
entrées qui éclate un octet de plan en 8 octets, les quatre plans se composant en **une
opération 64 bits**, 8 pixels d'un coup (indépendant du boutisme : chaque octet valant 0
ou 1, les décalages de 1-3 restent confinés). Et `stColorToArgb` était appelée pour
*chacun* des 320 à 640 pixels d'une ligne alors que la palette ne peut pas changer
pendant l'émission — 16 conversions par ligne désormais.

**Deux appels gratuits sur le chemin chaud** : `busFaultN` court-circuite la RAM ordinaire
et la lecture en ROM, qui ne fautent jamais ; et `busDiag`, diagnostic éteint, franchissait
la garde d'un statique local **et** évaluait `getClock()` à chaque accès bus.

**Compilation guidée par profil (PGO) + LTO — le plus gros gain unitaire, sans toucher au
code.** La boucle chaude est l'interpréteur Moira : un branchement indirect sur l'opcode
puis beaucoup de branches rarement prises. Avec le profil, GCC range les blocs pour que le
cas fréquent tombe en séquence — ce qui compte double sur un Cortex-A72 (32 Ko de L1i).
`build_native_pi.sh --pgo`, `pgo_train.sh` (parcours volontairement large : ST et STE,
50 et 60 Hz, mono, un jeu, une démo à retraits de bordure, les auto-tests — un profil
étroit fait déclarer « froid » du code qui ne l'est pas) et la CI `pi-borne.yml`, qui
entraîne sur le runner ARM64 pour que le Pi n'en paie rien. ⚠ **Piège qui coûte tout le
gain en silence** : GCC nomme les `.gcda` d'après le chemin absolu de l'objet, donc
instrumenter dans un répertoire et relire depuis un autre ne trouve **aucun** profil — et
`-Wno-missing-profile`, indispensable pour les objets du GUI non entraînés, rend l'échec
totalement muet. Une première mesure annonçait ainsi « PGO = −4 % », qui n'était que du
bruit. Les deux passes partagent désormais le même répertoire, et les scripts **échouent**
si aucun profil n'a été collecté pour `Cpu68k`, `Bus`, `Shifter` et `Moira`.

## Borne Raspberry Pi : démarrage direct + latence audio réglable (2026-08-02)

**`--audio-latency MS`** (persisté `audio_latency_ms=` dans `neost.cfg`) : le coussin
d'amorçage de l'anneau audio était figé à 85 ms dans `Audio::start`. Il est maintenant
réglable et borné à `[20, 250]` ms par `Audio::setLatencyMs` — au-delà, le coussin
s'approcherait de la capacité de `SampleRing{32768}` (341 ms à 48 kHz stéréo) et le
producteur jetterait des échantillons à chaque trame. Vérifié bout en bout : `--audio-latency
130` → `coussin 6240 frames` = 48000 × 130/1000, et `latence ~130 ms` au démarrage.

**`packaging/raspberry/`** — déploiement d'une borne qui démarre *directement* sur
l'émulateur, sans bureau. `install_kiosk.sh` (idempotent, `--uninstall`) monte un X **nu**
(ni gestionnaire de fenêtres ni compositeur) sur le VT 1 via une unité systemd modèle,
purge les serveurs de son (miniaudio → ALSA en direct), passe le gouverneur en
`performance`, épingle les IRQ sur le cœur 0, coupe Wi-Fi/BT/swap et le boot bavard.
`build_native_pi.sh` compile avec le `-mcpu` du cœur réel (l'AppImage livrée est aarch64
générique), et `pi-borne.yml` fait le même travail en CI sur runner ARM64 natif dans un
conteneur bookworm (plancher glibc ≤ 2.36 vérifié) pour éviter les 20-40 min de
compilation sur le Pi.

**Son : HDMI ou Bluetooth — le Pi 400 n'a pas de jack.** Par défaut, aucun serveur de
son et détection de la sortie HDMI *réellement branchée* via l'ELD (le Pi 400 a deux
ports). `--bluetooth-audio` installe PipeWire, seul chemin vers l'A2DP : miniaudio ne
sait pas parler Bluetooth. Ce qui rend le mode possible **sans toucher au code**, c'est
que miniaudio classe PulseAudio AVANT ALSA (`ma_backend` est ordonné par priorité) —
NeoST se branche donc sur `pipewire-pulse`, qui déplace le flux vers l'enceinte quand
elle se connecte, même en pleine partie ; sans cela `Audio::start` ouvre UN périphérique
au démarrage et n'en change jamais. Réglé pour ne pas coûter cher : 48 kHz verrouillé
(NeoST sort déjà en 48 kHz → aucun rééchantillonnage), quantum 1024, et profils HSP/HFP
coupés (une enceinte qui bascule en HSP passe en 8 kHz mono avec le micro ouvert —
la panne Bluetooth la plus fréquente). `neost-bt.sh` + un timer de 30 s rattrapent
l'enceinte allumée APRÈS la borne. ⚠ l'A2DP ajoute 150-250 ms irréductibles : pour
jouer, l'HDMI reste très supérieur.

Deux pièges refermés dans le script plutôt que dans un ticket : miniaudio
demande `SCHED_FIFO` pour son thread ALSA et **échoue silencieusement** sans
`LimitRTPRIO=` (le thread audio reste préemptible → underruns), et le `libglfw3` de
bookworm est **X11 uniquement**, ce qui exclut un kiosk Wayland (`cage`) sans recompiler
GLFW. ⚠ Scripts **non encore exécutés sur un Pi réel** — cf. leur README.

## Relecture adversariale pré-release (2026-08-01)

Cinq audits parallèles (zone chaude des 8 derniers commits, sécurité des entrées non
fiables, mémoire/UB, fidélité vs Hatari, préparation de release), chaque constat
re-vérifié dans le code avant correction. Suite `--tier full` verte avant ET après
(14 étalons, étalons pixel byte-identiques) ; 51 images disque corrompues et
**240 save-states forgés à CRC valide** rejoués sous ASan/UBSan.

**Save-states — 5 corruptions mémoire refermées.** Le chargement d'un `.state` forgé
restait une frontière de confiance trouée :
- `Shifter::liveGlueLine_` n'était pas borné et sert d'index à `startHBL` : le
  `borderMask |= …` devenait un read-modify-write à un offset **négatif arbitraire** du
  tas, répété sur toute la plage rattrapée. Garde de bornes dans `startHBL` + invariant.
- `Shifter::colorWrites_[].index` (registre palette) alimente `pal[index] = …` sur un
  `array<uint16_t,16>` **de pile** : jusqu'à 510 octets écrits au-delà, offset ET valeur
  choisis. Borné à la relecture comme `YM2149::RegEvent::reg`, et re-testé à l'écriture.
- `Fdc::lsnOffset` calculait l'offset image en **uint32 qui rebouclait** : un secteur 0
  donnait `$FFFFFE00`, et la garde « `off + 512 <= image.size()` » se calculant elle aussi
  en uint32 valait `0 <= size` — elle PASSAIT, et l'accès indexait ~4 Go plus loin (en
  lecture *et* en écriture, `writeBack` allant jusqu'à `seekp` dans le `.st` de
  l'utilisateur). Passé en 64 bits de bout en bout.
- `Fdc::bufferReadByte/Timing/BytePos` déréférençaient `buf_[bufPos_]` sans borne ; un
  état forgé entre directement dans un `TRANSFER_LOOP` sans passer par le `TRANSFER_START`
  qui teste le tampon vide (`buf_` vide → déréférencement nul).
- Gels à 100 % de CPU : `vcLineY_` et `renderLine_/tbLine_/hblLine_` n'étaient bornés
  qu'en bas (ou pas du tout) alors qu'ils pilotent des boucles de rattrapage.

**Comportements indéfinis (trouvés par fuzzing sous UBSan).** Un `bool` restauré à 63 et
une énumération `MouseMode` à 173 : dans les deux cas le chargement lui-même est un UB (un
`bool` non-0/1 peut rendre `if (b)` et `if (!b)` vrais tous les deux). Les booléens sont
désormais **normalisés dans `StateArchive`** — donc pour tous les composants d'un coup — et
`mouseMode_` transite par son type sous-jacent. Format de fichier **inchangé**.
`StateArchive::check()` prend en outre une étiquette : un état refusé dit maintenant PAR
QUOI (sans elle, une garde trop stricte est indiscernable d'un fichier corrompu — c'est ce
qui a permis de rattraper une des gardes de cette passe, qui refusait l'overscan légitime).

**GUI.** La bascule F8 vers le mode borne persistait les préférences de la séance, puis
n'importe quel `saveConfig(force)` ultérieur de la borne les écrasait avec la config du
LANCEMENT (`g_cfgPristine` figé au démarrage) — y compris sur simple auto-purge d'un
dossier ROM disparu. `drawCartLibrary` a reçu les deux durcissements de son jumeau
`drawDiskLibrary` : retour anticipé sur `Begin()` faux, et itération manuelle du dossier
(le range-for lève `filesystem_error` non rattrapée → `std::terminate`).

**CI de release — le chemin de publication ne pouvait pas aboutir.** Le job `linux-arm64`
compilait sans `-DNEOST_VERSION_STR` (les trois autres le posent), donc son binaire
annonçait le `project(VERSION)` figé `0.1.0` tandis que la garde exigeait la version du
paquet : job rouge à tous les coups, et `publish` en dépendant, **aucune release n'aurait
pu sortir**. `ffmpeg`, dépendance non déclarée de `compare_screenshot.py`, est désormais
installé par les deux jobs Linux (sans lui les 5 étalons à référence PNG échouent).

**Le garde-fou de vacuité des références échouait « en mode ça passe »** : sans ffmpeg il
imprimait un simple ⚠ et sortait 0, laissant 6 références non contrôlées — dont les trois
oracles spec512, précisément l'étalon dont la référence a été noire deux fois. Un contrôle
non concluant est maintenant un ÉCHEC.

**Diffusion.** Le workflow GitHub Pages construisait le bundle WASM avec
`NEOST_WEB_FREE_ONLY=OFF`, c'est-à-dire tout `roms/` et `disks/` embarqués — sous une
condition écrite dans ce même fichier (« dépôt à garder privé ») qui **n'est pas remplie**.
Basculé sur `ON` (EmuTOS + `diskA.st`). ⚠ Le dépôt lui-même suit toujours ce contenu :
cf. `TODO.md`.

**`.MSA`/`.DIM` inscriptibles — port de `MSA_WriteDisk`/`DIM_WriteDisk`.** Ces images
étaient montées en lecture seule, et le drapeau ne bloquait pas que la recopie hôte : il
pilotait le **bit WPRT du WD1772** vu par le programme. Sauvegardes en jeu, high-scores,
écritures depuis le bureau TOS et protections « écrit puis relit » échouaient donc
« disque protégé » sur toute `.msa`/`.dim`, alors que la même disquette en `.st`
fonctionnait. Hatari, lui, ne dérive WPRT que du réglage et de `stat()` — jamais du
format (`floppy.c:205-225`). `writeProtect` ne vient plus que de `stat()` ; `writeBack`
dispatche désormais sur le conteneur (`FloppyDisk::imgFormat`) : écriture partielle in
situ pour le `.ST`, idem décalée de 32 o pour le `.DIM` (en-tête préservé, comme
`dim.c:134-149`), et ré-encodage RLE complet **atomique** (tmp + rename) pour le `.MSA` —
reconstruire tout le fichier, une coupure en cours laisserait sinon une disquette
illisible. Le refus d'écrire ne subsiste que là où l'on ne SAIT PAS ré-encoder (STX, ou
en-tête `.msa`/`.dim` reconnu mais indécodable) : y écrire détruirait le fichier.
Nouvel auto-test `neost-headless --msa-selftest` (étalon `msa_selftest`, palier *fast*) :
44 cas — aller-retour byte-exact sur 6 géométries × 7 motifs (dont `$E5` isolé, qui doit
être échappé même seul, et un motif incompressible qui force la branche « piste stockée
brute »), plus deux cas de bout en bout montage → écriture → remontage sur fichiers `.msa`
et `.dim` réels. Vérifié aussi qu'aucun disque d'étalon suivi par git n'est modifié par
un run complet, et que les `.msa` tronquées restent en lecture seule.

**Documentation.** `HATARI_DIVERGENCES.md` affirmait que `.MSA`/`.DIM` étaient « conformes
(vérifiés ligne à ligne) » : c'est faux et cela masquait un écart réel (montage en lecture
seule + bit WPRT présenté au programme, là où Hatari ne dérive WPRT que de `stat()`) —
consigné en D0. L'écart `$FFFA01` GPIP bits 3/6 vs Hatari est consigné, attendu, et à
connaître avant toute chasse différentielle.

## Bug hunt passes 1-3 + CI de release (2026-07-31)

**Sécurité / crashs.** Le pont GEMDOS laissait s'ÉCHAPPER du dossier monté : `/` n'était
pas reconnu comme séparateur Atari (seul `\` l'était), donc un `.TOS` hostile lisait,
écrivait et listait hors du bac à sable avec les droits de l'utilisateur (Hatari a le même
trou — durcissement assumé). Le blitter pouvait faire PLANTER l'émulateur : un blit visant
son propre registre de contrôle se relançait à l'infini (SIGSEGV). Les deux sont prouvés
par repro et re-testés fermés.

**Le filet de test lui-même était troué** — `run_selftests.py` rendait VERT un émulateur
qui segfaute (code de retour ignoré + dump série périmé relu) ; `--only <ID inconnu>`
exécutait zéro test en annonçant « TOUS OK » ; une référence absente comptait comme une
réussite. La CI de release ne lançait AUCUN palier de validation : elle lance désormais
`run_all.py --tier fast`.

**Émulation.** FPU : `normalizeSubnormal` portait la branche x87 au lieu de la branche
68881 → tout opérande dénormal ressortait ×2 ; FSGLMUL/FSGLDIV rabattaient à tort la plage
d'exposant et tronquaient avant les cas spéciaux (fuzz différentiel contre `softfloat.c` :
0 écart). L'instruction 68000 **RESET** ne réinitialisait aucune puce (port de
`customreset()` : IKBD, Glue, PSG, FDC + IRQ latchées ; `MFP_Reset_All` reste à faire).
`Machine::liveNow()` comptait le temps DEUX FOIS pendant le dispatch du saut STOP (1208
dispatches sur 15780, δ ≤ 112 cyc → 0). Save-states **v6 → v7** (empreinte GEMDOS +
cartouche, `bus.cart` sérialisée). STX : les écritures faites après un formatage de piste
étaient perdues au remontage.

**Paquets.** CI de release : 5 artefacts (AppImage x86_64 glibc 2.27, AppImage aarch64,
AppImage Raspberry Pi, `.dmg` macOS Universal 2, bundle WebAssembly) + sommes SHA-256.

## Bug hunt multi-agents (2026-07-29)

Chasse à 6 lentilles parallèles (diff non commité, cœur CPU/Bus/état, vidéo, I/O disque,
périphériques, audio & concurrence), chaque lot passé à un sceptique mandaté pour RÉFUTER :
26 findings bruts → **22 confirmés, 4 réfutés**, tous corrigés. Étalons `--tier full` verts
avant/après (Cuddly & Enchanted Land 0 px), build ASan/UBSan sans rapport.

- **Sécurité mémoire — 3 critiques, toutes reproduites puis refermées :**
  - *Save-state / framebuffer* : `curW_`/`curH_` étaient restaurés sans invariant les liant à
    la taille de `frame_`, et le court-circuit « même w/h » de `resizeFor()` empêchait la
    réallocation → écriture hors du tas dès le 1ᵉʳ `renderLine` (repro : *stack smashing* +
    core dump). Invariants `ar.check` posés APRÈS `podVec(frame_)` (géométrie, mode, aires
    actives) + taille du tampon intégrée au test de `resizeFor`. `Shifter.hpp/.cpp`.
  - *Parseur STX* : `TrackImageSize` (16 bits venus du fichier) n'était jamais confronté à la
    fin du tampon → `readTrackStx` lisait jusqu'à ~64 Ko hors du tas sur une `.stx` tronquée
    (mesuré : 65535 annoncés pour 6 octets disponibles). Plafonné sur le reste réel, dans
    l'esprit du clamp de `msa.c:205`. `StxImage.cpp`.
  - *Save-state PSG* : `RegEvent::reg` relu brut (0..255) indexait `audioRegs_[16]` → écrasait
    les pointeurs de `events_` et les `std::function` voisines. `ar.check(reg < 14)` (borne de
    `write8`) + masque par événement. `YM2149.hpp/.cpp`.
- **Autres invariants de save-state** (même classe, tous « fichier forgé passé le CRC ») :
  `envPos_ < 96` (table d'enveloppes), `fifoPos_ < 8 && fifoNb_ <= 8` + masque dans `fifoPull`
  (FIFO DMA son), `mwSteps_ ∈ [0,16]` (compteur de décalage Microwire), et la borne FDC
  `fifoSize_ <= 16` corrigée en `< 16` — `fifoPush` écrit `fifo_[fifoSize_]`, donc 16 était la
  seule valeur toxique que l'invariant laissait passer.
- **`.msa` illisible ne détruit plus l'image source** : une longueur de run RLE non plafonnée
  (cas nommément prévu par `msa.c:205-210`) faisait échouer tout le décodage, et le repli
  montait les octets COMPRESSÉS comme `.st` **brut et inscriptible** — le premier `write sector`
  de l'invité écrasait le `.msa` de l'utilisateur. Clamp porté + repli en LECTURE SEULE dès que
  l'en-tête ressemble vraiment à une `.msa` (`looksLikeMsaHeader`). `Fdc.cpp`.
- **Vidéo — trois consommateurs manquants, alignés sur `video.c` :**
  - `V_OVERSCAN_NO_DE` était détecté fidèlement mais consommé NULLE PART : une trame dont le DE
    vertical n'est jamais activé s'affichait normalement au lieu de sortir à l'index couleur 0,
    et le compteur vidéo avançait à tort. Branché sur ses trois consommateurs Hatari — rendu
    (`video.c:3988`), stride du compteur (raster non avancé) et Timer B en event-count
    (`video.c:3649`) — avec un cas d'auto-test Glue dédié (détection + contre-épreuve).
  - `videoCounter()` ignorait `BORDERMASK_LEFT_OFF_2_STE`/`_MED` (+20 o, `video.c:1514-1517`) :
    la ligne valait 180 o pour l'accumulation inter-lignes mais 160 pour l'offset intra-ligne,
    et le gel de fin de ligne tombait 40 cycles trop tôt.
  - Fenêtre verticale plus COURTE que `curAH_` (`VO_BOTTOM_SHORT_50`, −29 lignes) : le compteur
    avançait sur les lignes non affichées. Borné dans `glueLineBytes` plutôt que dans la boucle
    de commit, pour ne pas toucher à la cadence de capture `lineSnap_`.
- **Verrou `NEOST_LINELEN` à sémantique inversée** : `Machine` lit la VALEUR (défaut ON, `0` =
  OFF), les 3 sites `Shifter` testaient la seule PRÉSENCE de la variable — `NEOST_LINELEN=0`
  désactivait donc une moitié et ACTIVAIT l'autre, et l'A/B documenté mesurait un hybride
  jamais validé. Lecture unifiée (`envFlag`), défaut de chaque site INCHANGÉ (c'est l'hybride
  validé au pixel), docs corrigées.
- **FPU (68881)** : infini GÉNÉRÉ empaqueté avec la mantisse `$8000…` au lieu de la forme
  canonique 0 (`floatx80_default_infinity_low`) → deux motifs binaires différents pour +∞ selon
  qu'il est chargé ou calculé (`INF_SIG` séparé d'`INF_LOW`) ; `FGETEXP` d'un NaN rendait
  l'opérande BRUT, sans quiéter le SNaN ni lever `FPSR.SNAN` (délégué à `propagateNaN1`, comme
  `FGETMAN`) ; `FMOD` empruntait le court-circuit de `FREM` sur `expDiff < 0`, sautant arrondi
  de précision et `UNFL` (`softfloat.c:3048` vs `2941`).
- **Blitter** : les accès bus CPU étaient datés dans l'horloge du CŒUR alors que les fenêtres
  du blitter sont armées dans celle de l'ORDONNANCEUR — 40 cycles d'écart (les cycles de
  `Moira::reset()` avant l'ancrage de la 1ʳᵉ trame), donc fenêtre PRE_START ratée et tranche
  reprogrammée trop tard. Datation unifiée sur `Scheduler::liveNow()`.
- **Kiosk (travail en cours de `main.cpp`)** : le gel de la configuration était définitivement
  rompu par un aller-retour F8 (la borne réécrivait `neost.cfg` avec la session du visiteur) —
  `g_kioskLaunched` distingue désormais l'invariant de DÉPLOIEMENT de l'état courant ;
  l'émulation joystick-clavier restait armée en revenant au bureau depuis une session lancée en
  `--kiosk` (capture de lambda évaluée après `g_kbdJoy = g_kiosk`), avalant flèches et Ctrl
  droit sans rien afficher ; la fenêtre était replacée en (0,0) faute de géométrie fenêtrée
  jamais observée (drapeau `g_winGeomValid` + centrage sur la zone de travail du moniteur).
- **MIDI** : la file de bouclage OUT→IN croissait sans borne (~11 Mo/heure pour un séquenceur
  qui n'a aucune raison de lire MIDI IN, recopiée dans chaque save-state, `RDRF` collé donc IRQ
  ACIA permanente sous RIE). Bornée à la profondeur physique d'un 6850 (RDR + registre à
  décalage), ce qui modélise en prime l'overrun.
- **UB** : `mwData_ << 16` débordait un `int` signé dès que le bit 15 de `$FF8922` était posé
  (atteignable par du code invité, sans save-state) → arithmétique non signée.

---

## Inventaire par sous-système

Le détail de **ce qui est implémenté, puce par puce** — Cœur & boot, machines & mémoire,
Vidéo/Shifter, MFP, IKBD/ACIA, FDC & DMA, GEMDOS HD, Audio, Bus error & cartouches de
diagnostic, Frontend & outillage — a été déplacé dans
**[`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)**. Ce fichier-ci reste la chronologie ;
celui-là répond à « NeoST gère-t-il X ? ».
