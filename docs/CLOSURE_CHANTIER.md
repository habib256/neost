# Chantier CLOSURE (Sync) — état de l'enquête

**Symptôme** : `disks/etalons/closure.msa` — écran NOIR permanent chez NeoST
(ST + tos102uk + 1 Mo + fastfdc), boote chez Hatari (même config exacte).

> **Statut : RÉSOLU.** Closure boote depuis le Cycle 5 (2026-08-12) et atteint la
> parité visuelle au Cycle 6 ; c'est aujourd'hui l'étalon `closure` de
> `tools/etalons.json`. Ce document est le **journal d'enquête** : les `fichier:ligne`
> et les questions « à trancher » des cycles 1-3 datent de l'époque.

## Chaîne déductive — ce qui est PROUVÉ (2026-08-11)

1. **MSA/FDC/chargement : innocents.** Conversion MSA→ST à la main : Hatari boote
   le `.st`, NeoST non. Dump RAM $10000-$40000 à la trame 150 des deux côtés :
   **PRG chargé IDENTIQUE au bit près** (seules divergent des tables générées).
2. **Détection machine : correcte.** Le loader sonde `$FF8901`/`$FF8E21` par bus
   errors rattrapés — NeoST faute et rattrape comme Hatari.
3. **Le crash** : le PRG exécute l'opcode `$19C0` à `$205BC` → ILLEGAL → bombes
   TOS → Pterm → `etv_term` ($C6100, posé par la démo, encore VIDE) → zéros →
   re-crash en boucle. Le `$19C0` n'est PAS sur le disque ni voulu : c'est du
   **code automodifiant** — `move.w (A2,D4.w),(A4)` recopie une instruction
   depuis une table indexée par une MESURE ; mesure hors barème → index à côté
   → mot poison. Disquette-témoin forgée : `$19C0` = ILLEGAL **chez Hatari
   aussi** (bordure bleue des deux côtés) → Hatari ne passe jamais par là.
4. **La mesure verticale (wakeup state par paire 60/50) MARCHE déjà** :
   la démo balaie la phase d'une paire 60→50 Hz autour de la frontière
   ligne 33/34 (`f00@500/f02@504` etc., ±4 cyc par trame). Règle des deux côtés :
   ouvre si 60 Hz à ligne 33 cyc ≤ `RemoveTopBorder_Pos` (WS3 : 502+1=503),
   TIENT si le retour 50 arrive > borne. NeoST ouvre à la MÊME unique phase
   (500/504) et rend les MÊMES valeurs $8209 : **$8C @ X=336, $92 @ X=348 —
   identiques à l'oracle à l'octet près** (traces `NEOST_VC_TRACE` vs
   `--trace video_addr`, mêmes 1811 lectures).
5. **La phase qui tue est POSTÉRIEURE** : un balayage PER-LINE (une ligne
   sondée par trame, lignes ~30→188) avec la séquence par ligne :
   `hi@510(ligne N−1) / med@6 / lo@14 / 50Hz@22 / 60Hz@374` (PC $2495C-$24A10,
   code auto-généré). **AUCUNE lecture $8209 dans cette phase** : la mesure est
   TEMPORELLE (le crash initial arrête Timer A juste avant le $19C0 ; STOP
   #$2100 ×2 + handler `st $267d8/rte` = sémaphore d'IRQ). La démo chronomètre
   l'effet des tricks per-line — longueurs de ligne (224 en hi au comparateur
   HBL ? 508/512) et/ou Timer B (événements DE). Le WDIAG s'arrête à la ligne
   sondée ~188-189 (dernière écriture `freq $82 @ ligne 189 cyc 2, pc=24a4e` =
   nouveau PC, transition de phase) puis mort.

## PROCHAINE ÉTAPE (précise)

Comparer les **longueurs de ligne réelles** pendant le balayage per-line :
- Hatari : `--trace video_hbl` (HBL avec cycle) sur la fenêtre du balayage —
  quelles lignes font 224/508/512 cycles avec la séquence hi@510/med@6/lo@14 ?
- NeoST : canal `NEOST_LINELEN` (ON par défaut) — ajouter au diag `[GLUP]`
  l'affichage de `glueHblPos_`/`glueCyclesLine_` après chaque écriture, et/ou
  tracer les callbacks `lineGeom_` (Machine reprogramme l'IRQ HBL par ligne).
- Diff : la première ligne dont la durée diverge = le mécanisme à porter
  (candidats : `Hbl_Pos_Hi=224` quand la Glue est en hi au comparateur HBL ;
  les blocs « TEMP for closure » video.c:1791-1819 « remove left with no med
  stab » ws2/STE ; video.c:3954-3964 plane shift).

### Découverte complémentaire : le chronomètre = Timer B

`--trace video_hbl` chez Hatari pendant Closure : les événements `EndLine TB`
prennent un SPECTRE de positions — 400 (×29k), **488** (×22k = lignes RIGHT_OFF
fullscreen, DE→460+), 404/408 (normales ±WS/60Hz), 402/406/412/416/424/432…
La démo compte/chronomètre les événements Timer B (fins de DE par ligne) : la
position TB de la ligne SONDÉE encode la réponse de la Glue à la séquence
per-line. Côté NeoST : vérifier comment le Timer B suit les DE par-ligne de la
Glue live (`LineTimerBPos` d'Hatari = `Video_TimerB_GetPosFromDE(DE_start,
DE_end)`, reprogrammé À CHAQUE écriture qui change le DE — video.c:2880-2891).
Comparaison à faire : distribution des positions TB par ligne des deux côtés
pendant le balayage, première ligne divergente = mécanisme à porter.

## Cycle Timer B (2026-08-11 soir) — portage posé, activation à débugger

**Mesure fondatrice** (croisement ligne-sondée ↔ tic TB, scripts ci-dessous) :
- Hatari : tics des lignes sondées à **488 dans 98,6 %** des cas (DE_end 462+24+2).
- NeoST AVANT : 400 (défaut global) ×27 %, 488 ×23 %, 472/508 (dérive de grille).

**Porté** (étalons full VERTS, SHO inclus — comportement inchangé hors trames à
écritures freq/res) :
1. `Shifter::timerBPosForLine(line, startOfLine)` — port de
   `Video_TimerB_GetPosFromDE` sur `glueLines_[line]` (DE réel, tricks compris).
2. `Machine::onTimerB` : re-vérification au callback (≙ reprogrammation à
   l'écriture d'Hatari, video.c:2880-2891) — cible plus loin → replanifie sans
   tirer ; + planification de la ligne suivante par la position par-ligne.
3. `Shifter::timerBFrameCycleForLine` — cible sur la grille RÉELLE
   (`glueLineStart_`) ; renvoie −1 si indispo → repli nominal.

**PROBLÈME OUVERT** : la distribution des tics sondés est STRICTEMENT inchangée
(mêmes comptes 357/311/269/269), même avec `NEOST_LINELEN=1`. Une maille de la
chaîne ne s'active pas — hypothèses à trancher au fprintf dans
`timerBFrameCycleForLine` : (a) `liveGlueLine_ < line` au moment de la
planification (catch-up pas encore là) ; (b) la grille réelle est EN AVANCE sur
la nominale (lignes 508) → le re-check trouve `target ≤ now` et tire à
l'identique ; (c) le `[TB] pos` du diag est calculé dans le référentiel NOMINAL
et masque un vrai déplacement (recalculer la pos affichée sur glueLineStart_).

**DÉCOUVERTE COLLATÉRALE À INSTRUIRE** : le canal `NEOST_LINELEN` est HYBRIDE —
défaut **ON côté Machine** (`lineLenOn_`, Machine.cpp:173) mais **OFF aux 4
sites du Shifter** (`envFlag(..., false)`, Shifter.cpp:445/491/1669/2554 :
attribution des écritures, lecture $8209, timerBFrameCycleForLine). La mémoire
du dépôt (« ON par défaut depuis 2026-07-08 ») ne vaut que pour Machine. À
trancher : aligner les défauts (risque : re-tester Cuddly menu robot, dont le
mapping grille réelle était « décisif »).
*(Tranché depuis : les deux moitiés lisent `Shifter::lineLenEnv()`, défaut ON, et
l'attribution des écritures a son propre verrou `NEOST_LINELEN_ATTR`, défaut ON —
cf. `src/core/ShifterInternal.hpp`.)*

Scripts de croisement (ligne sondée → tic TB) : parse `[WDIAG] freq val=00
line=N cyc=37x` puis `[TB] line=N pos=P` côté NeoST ; côté Hatari
`--trace video_hbl,video_sync` puis `sync=0x00 …video_hbl_w=N` / `EndLine TB N
…line_cyc=P`.

### Cycle Timer B, suite (débogage) — état précis

Le diag [TB] comptait AUSSI les callbacks qui replanifient (émis avant le
re-check) — déplacé après : les TIRS réels sont 580/763 À LA CIBLE GLUE 487 ✓
(dont 269 affichés « 508 » par le seul référentiel nominal du diag). RESTENT
125 tirs où LA GLUE MÊME dit 400 : sur ces trames, l'attribution grille-réelle
(liveGlueCatchUp, LINELEN) place l'écriture 60 Hz à lc=374+21=395 — HORS de la
fenêtre right-off (373..377] — parce que la grille réelle NeoST est en RETARD
de ~21 cyc sur la nominale À CES trames. Chez Hatari (grille unique) l'écriture
est toujours à 376. RÉVISION après mesure des longueurs de ligne : les 529 lignes 508 d'Hatari
sont AU BOOT (registre $FF820A=0 ⇒ 60 Hz avant l'init TOS), PAS dans le
balayage — pendant la mesure les DEUX grilles sont nominales et cohérentes
(NeoST : 1 seule ligne 508). L'hypothèse « longueurs divergentes » est ÉCARTÉE.

ÉTAT FINAL DU CYCLE : tirs réels = 580/763 À LA CIBLE GLUE 487 ✓ (269 affichés
« 508 » par le seul référentiel du diag). RESTENT ~125 tirs (posLine=400) où la
GLUE MÊME n'a pas le right-off — piste : lignes sondées 30-62 (hors fenêtre
verticale quand le haut n'est pas ouvert) ? À CONFRONTER : pour les MÊMES lignes
sondées, Hatari rend-il 488 (⇒ règle à porter) ou 400 aussi (⇒ résidu normal) ?
Et Closure reste NOIRE : le protocole exige plus que le tic TB — après ce
verrou, refaire le run long et chercher la PHASE suivante qui diverge.
⚠ tbScheduledAt_ (échéance planifiée) ajouté à Machine — le re-check compare à
l'échéance, pas à l'heure de service (STOP quantifié). Diag [TB] désormais émis
APRÈS le re-check (les entrées replanifiées ne sont plus comptées comme tirs) ;
[LLD] (NEOST_LLEN_DUMP) dump des longueurs de ligne réelles.

## Cycle 3 (2026-08-11 nuit) — GPIP convergé, le mécanisme de la file élucidé

**Point 1 TRANCHÉ** : les tics « défaut » ne touchaient que les lignes sondées 65
et 188 — 65 = pareil chez Hatari (une autre phase y vit), 188 = artefact de
parseur (tics post-mortem avec `probed` périmé). **Verrou Timer B CLOS :
NeoST == oracle sur tout le protocole du balayage.** Mais la mort est
INCHANGÉE ($19C0 à $205BC, même ligne de trace) : le tic TB ne pilotait pas la
mesure fatale.

**LE MÉCANISME RÉEL (élucidé)** : `$24B70` masque les IRQ, pose le vecteur
Timer A → `$2059E`, arme TADR=$63/TACR=4, puis `move.l (A0)+ , $205BC` avec
A0 = pointeur de FILE en `$170` (file circulaire `$140-$16F`, wrap à $170).
Le Timer A DÉCLENCHE périodiquement la séquence `$2059E` (fond noir, stop ×2,
SR=2700, éteint Timer A, EXÉCUTE l'instruction patchée). La file = une SUITE
D'ÉPREUVES : instructions-test dont l'exécution/exception EST la mesure
(Hatari y a `$8080` = or.l D0,D0 VALIDE ; NeoST `$19C0` ILLEGAL → bombes).
Ni TADR ni TBDR ne sont lus par la démo pendant la mesure (1 seule lecture
d'inventaire chacun) — les compteurs ne sont PAS le chronomètre.

**CONVERGÉ ce cycle (2 correctifs de fidélité, étalons full verts)** :
- `Mfp.cpp` reset : `gpip = 0x00` (≙ Hatari mfp.c:523 ; ancien 0xFF).
- `gpipInput()` : repos BAS des bits 6 (RI) et 3 (GPU) — Hatari ne les pose
  jamais dans MFP_GPIP_ReadByte_Main. → l'octet GPIP de la table d'identité
  ($2E22F) converge ($B1 == oracle).

**RESTE DIVERGENT à la trame 490** :
- Table d'identité : `$2E240-41` = TBDR/TCDR de l'inventaire ($32/$00 vs
  $6B/$01) — snapshot des compteurs à l'instant du dump (instant identique ?
  compte différent ? À trancher, mais lecture unique → peut être bénin).
- **LA FILE `$140-$16F` : 9 octets** (NeoST : nops + $19C0 $1414 ; Hatari :
  $8080 ×N). PROCHAINE ÉTAPE : trouver le REMPLISSEUR de la file (écritures
  vers $140-$16F — dump-at à 100/200/300/400 pour dater le remplissage, puis
  trace de la fenêtre) et remonter à la mesure qui alimente ces valeurs.
  Piste : les lectures TBDR « séries de 38 » divergent de ±1-2 décréments
  (NeoST 238/236/234/232/227 vs Hatari 239/236/93/91/89) — le ±1 tic peut
  suffire à classer autrement. Comparer ces lectures PAR PC (--trace mfp_read
  côté Hatari ; NEOST_MFPRD_DIAG côté NeoST, ajouter le PC au diag).

## Cycle 4 (2026-08-12) — LA CHAÎNE CAUSALE COMPLÈTE, au cycle près

Remontée intégrale, chaque maillon MESURÉ des deux côtés :

1. `$19C0` exécuté ← copié depuis la FILE D'ÉPREUVES `$15C-$16F` (vocabulaire
   `$8080`/`$4E71` ; nos `$19C0 $1414` = offsets de table recopiés hors barème).
2. La file ← patchée par `$20038+` selon le VERDICT `$20036` : NeoST **0**,
   Hatari **1** (copié en `$158`).
3. Le verdict ← classificateur `$1FFD2-$1FFFC` : delta de deux échantillons du
   compteur vidéo à travers une séquence de tricks beam-syncée (spin sur front
   `$8209`). Barème : $36/$CE/$CC (WS valides) ; défaut → 0. **NeoST mesure
   $A2 = $CE − 44** : les 44 octets du RIGHT_OFF manquant.
4. Le right-off de la ligne 65 ← REFUSÉ : la paire 60→50 (pc=1FFC0/C2) est
   datée par la Glue replay à **380** ∉ (373..377] (Hatari : 376 ✓). Buffer
   d'échantillons primitifs `$30988` : IDENTIQUE (0/1792) — tout le reste
   converge.
5. Le 380 ← grille réelle décalée de −4 : la LIGNE 64 reste à 508 chez NeoST
   (512 chez Hatari) parce que le Freq_match du RETOUR 50 Hz (pc=1FF92) est
   REFUSÉ : GLUP le date **56 > Line_Set_Pal(55)** ; Hatari le date **54 ≤ 55** ✓.
6. **RACINE : la datation d'écriture du replay** — `fc_push = fcRaw + 2`
   CONSTANT ; or cette écriture a `into=4` (WDIAG) : l'accès bus réel est plus
   tôt que fcRaw+2. Hatari (`Video_GetPosition_OnWriteAccess`) date l'accès
   réel par instruction : 60Hz→40 (== nous), 50Hz→54 (nous : 56). Le +2
   constant sur-date CERTAINES instructions de +2.

**PROCHAINE ÉTAPE (unique, et périlleuse)** : réviser la datation write du
replay vers « accès réel » (fcInstrStart + position d'accès, cf. `into`) — ⚠
c'est LA calibration « write +2 » validée jadis ENSEMBLE avec read −6
(mémoire : « ne bouger que par paire », datation par-instruction RÉFUTÉE dans
docs/MOIRA_WINUAE_CONVERGENCE.md §7 — MAIS le contexte a changé : WS3 complet,
LINELEN, grille réelle). Banc OBLIGATOIRE : --tier full + Cuddly menu robot +
LX + EL in-game + SHO + nocooper + CE cas (GLUP ligne 64 : le 50 Hz doit dater
54-55). Alternative si la paire résiste : dater les SYNC à l'accès réel et
garder +2 pour les RES (Hatari a déjà un ajustement res-only « GLUE latch res
1 cycle later », video.c:2221-2228 — précédent d'asymétrie).

## Recettes de reproduction (30 s)

```sh
# NeoST — traces mesure/écritures/Glue (diags enrichis pendant l'enquête) :
NEOST_VC_TRACE=1 NEOST_WRITE_DIAG=1 NEOST_GLUE_DIAG=1 \
  ./build/neost-headless roms/tos102uk.img --machine st --mem 1m \
  --disk /chemin/closure.st --frames 400 --fastfdc 2> neost.log
# Hatari oracle (mêmes trames) :
extern/hatari/build/src/hatari --machine st --tos roms/tos102uk.img \
  --memsize 1 --disk-a closure.st --fast-forward on --fastfdc on \
  --run-vbls 400 --trace video_addr,video_sync,video_res 2> hatari.log
# Point de comparaison validé : lectures pc=$2053A, ligne 34, X=336/348 →
# $8C/$92 sur la phase paire 500/504, $00 ailleurs — IDENTIQUE des deux côtés.
```

Le PRG charge via TOS (26 secteurs), s'exécute en `$1F0E2+` (générateur roxl/roxr
à 10 bits d'après table `$1F23C`), génère le code de mesure en `$205xx`/`$24xxx`.
`$2495C+` = boucle per-line générée ; `$24A5C` = table des lignes.

## Diags AJOUTÉS pendant l'enquête (commités ou à committer)

- `[VC]` enrichi : dispStart/disp2/la/sw/startHBL + dump du tampon syncWrites_.
- `[GLUP]` enrichi : startHBL/endHBL/vo/refresh après chaque écriture rejouée.
(`Shifter.cpp`, gated `NEOST_VC_TRACE`/`NEOST_GLUE_DIAG` — zéro coût sinon.)

## Cycle 5 (2026-08-12) — LE FIX : datation par parité d'`into` ; la démo VIT

**La racine du Cycle 4 est corrigée.** `Shifter::recordSyncWrite` date désormais
les écritures freq/res par la PARITÉ de la position de l'accès dans l'instruction :
`fc += (cyclesIntoInstr() & 2) ? +2 : 0` (défaut ; `NEOST_SYNC_MODE=0` restaure
le `fcRaw + 2` constant pour l'A/B). Transposition de la loi Hatari CE
(`Cycles_GetInternalCycleOnWriteAccess` : position WinUAE de l'accès + 4) au
placement Moira : into ≡ 2 (mod 4) = la classe historique (move Dn,(An)/abs,
tout le parc calibré) → +2 inchangé ; into ≡ 0 (mod 4) = move An,(An) du
classificateur Closure, que Moira place 2 cyc après WinUAE → +0.
⚠ Le premier essai « début d'instruction + 4 » uniforme (équivalent pour les
deux écritures du témoin) CASSAIT nocooper (19361 px : les move vers abs.w,
into=6, exigent start+8 = fcRaw+2, pas start+4). La parité réconcilie tout :
banc --tier full 39/39 VERT (nocooper + 3 diapos spec512 oracle compris),
menu Cuddly trame 3400 pixel-identique à l'avant-changement, et A/B
NEOST_SYNC_MODE=0/1 pixel-identique sur Enchanted Land (SPACE, 2600 tr),
Super Hang-On (2600 tr) et Lethal Xcess (STX A+B, 2600 tr) — la classe
into≡0 mod 4 n'apparaît pas dans leurs écritures sync décisives.

Chaîne re-mesurée après fix : GLUP ligne 64 `cyc=54 sync freq=50` (≤ 55, accepté)
→ ligne 64 rendue à 512 → ligne 65 `cyc=376 sync freq=60 → mask=010` (right-off,
204 octets) → delta classificateur $CE → **verdict $20036 = 0001 = l'oracle** →
file d'épreuves valide → plus d'opcode $19C0 → **Closure boote et tourne**
(3000 trames : logo animé 15 couleurs trames ~1400-2300, écran plein ~2400-2600,
même chronologie que l'oracle à ±100 trames).

### Reste ouvert : hachis du logo (effet 2) — dossier de mesures

L'effet « logo Closure » (plein écran, ~1400-2300) est HACHÉ chez NeoST (sauts
horizontaux +11-12 px toutes les ~9-10 lignes ; oracle lisse à +1 px/ligne).
TOUT le reste est prouvé identique à l'oracle sur la trame 1500 :

- **Plan Glue par ligne** : masks/DE/octets identiques (lignes 39-307 : mask
  0x200110, DE 5..463, 230 o/ligne des deux côtés).
- **Bases vidéo** : séquence par VBL byte-identique sur 18 trames apériodiques
  (e0600→e4d00) — canaux base/bitmap SYNCHRONES.
- **RAM bitmap** : identique (résidu 438 o = bande en cours de redessin entre
  deux instants de dump non strictement égaux).
- **Écritures palette** : ordre/idx/pc identiques, datation NeoST = Hatari − 2
  UNIFORME (42 writes/ligne appariés, ligne 165) — absorbé par kSpec512AlignCyc.
- **Adresses de rendu** : +230/ligne régulier, snap présent partout,
  NEOST_NO_SNAP=1 ne change RIEN à l'image (nouveau kill-switch, Shifter.cpp).
- **« Horloge » de l'animation** (liste d'offsets $6A03E+, lue par
  `move.w (a6)+,d2` — générateur $224xx : `movem.l (a2,d2.w) → $ffff8240+`,
  `lea $24C(a2),a2` par ligne) : synchrone à ±1 pas (échantillons $6a060 :
  0→28→7a→be→c6→90 aux mêmes VBL des deux côtés).

**LA divergence** : les VALEURS écrites dans la palette diffèrent — les couleurs
NeoST de la trame N = celles de l'oracle à N+2/N+3 (match 22/22, ligne 165,
neo1500 == hat1502-1503). Cause immédiate : les listes d'offsets par ligne sont
DOUBLE-BUFFERÉES ($69E33-62 et $6A053-82, seules zones divergentes de tout
$60000-$70000 : hat 00C8×23, neo 0092×23) et re-remplies par « vagues » EN COURS
de trame par le code de fond (self-paced, main loop) pendant que le générateur
beam-locké les consomme. Au même instant (±130 cyc, VBL 1501), Hatari a sa vague
ÉCRITE, NeoST pas encore → le générateur lit la vague voisine → couleurs d'une
autre phase sur le damier de la phase courante → interférence = hachis (les
diagonales lisses de l'oracle émergent de la cohérence bitmap×palette ; le
bitmap RAM est un damier à marches, pré-compensé — reconstruction pitch 230
depuis la RAM : marches +11 px/~9 lignes, IDENTIQUES à notre écran).

**Prochaine étape** : ordonnancement CPU intra-trame pendant l'effet — pourquoi
le remplisseur NeoST atteint la publication de sa vague plus tard qu'Hatari dans
la trame. Suspects mesurés : les ~125-180 tirs Timer B au fallback (hors cible
Glue, cf. Cycle 2) pendant ces trames overscan ; latences d'IRQ ; e-clock.
Micro-divergences résiduelles fixées AVANT rupture (singletons vbl ~1478/1481/
1486 : entrées '0' vs '27' = vague pas encore écrite au passage du faisceau).

### Diags ajoutés au Cycle 5

- `[COL]` (NEOST_COL_DIAG) : datation des écritures palette avec base de trame,
  into, idx, col — format appariable au `--trace video_color` d'Hatari (attention
  aux écritures transitoires octet-haut : filtrer au dernier write par cycle).
- `NEOST_NO_SNAP=1` : neutralise lineSnap_ (repli relecture RAM fin de trame).
- `[render]` enrichi : snap/snapLen/gbytes par ligne (NEOST_RENDER_TRACE).

### Recettes oracle du Cycle 5

```sh
# AVI oracle par trame (1 image PNG = 1 VBL, fiable pour aligner) :
hatari ... --run-vbls 3000 --avirecord on --avi-file o.avi --avi-vcodec png \
  --statusbar off --crop on     # ⚠ --avirecord veut un bool ; --crop, pas --screen-crop
# savebin au breakpoint : adresses/longueurs EN $HEX sinon lues en décimal :
#   b VBL = 1501 :once :file dump.cmd   ; dump.cmd : savebin f.bin $e0000 $c000 + c
# ⚠ le run --parse reste au prompt debugger après le breakpoint : lancer avec
#   timeout et récupérer les fichiers — le savebin, lui, est bien écrit.
# Couplage base↔couleurs par trame : --trace video_color,video_hbl et segmenter
#   sur « restart video counter 0x... » (= base de la trame SUIVANTE).
```

## Cycle 6 (2026-08-12) — le hachis cerné : motifs de transitoires hors répertoire

**Acquis préalables du cycle** (tous mesurés, méthode et pièges inclus) :

1. **512 Ko = FIDÈLE** : la démo affiche « AT STNICCC IN 2015 » (996 px oracle,
   même message chez NeoST) et refuse de démarrer — Hatari pareil. Closure
   exige 1 Mo. Le « ne démarre pas » GUI en 512k n'était pas un bug.
2. **⚠ MÉTHODE : Hatari est NON-DÉTERMINISTE run-à-run sur cette démo**
   (probe : liste d'offsets à VBL 1500 = $C8 run A, $92 run B). L'ancrage de
   boot varie → TOUTE comparaison par VBL absolu des canaux d'animation est
   INVALIDE. Comparer par grandeurs INVARIANTES (deltas par ligne, séries de
   motifs, cohérence interne). Deux runs Hatari d'ancrages différents sont
   TOUS DEUX lisses (var1/var2_f.png) — la démo est auto-cohérente par phase.
3. **Chaîne du rendu exonérée pièce à pièce** (trame 1500) : écritures palette
   NeoST = Hatari −2 UNIFORME sur 11306/11314 writes de la trame ENTIÈRE
   (les 8 restants = lignes 1/32, VBL handler) ; accumWait spec512 = 0 (tout
   ≡2 mod 4) ; pyrender2 (re-rendu python aux adresses du renderer + couleurs
   datées) == notre PPM → le renderer applique fidèlement ses données ; la
   démo est un runner 100 % compté (AUCUNE IRQ MFP pendant l'effet — trace
   mfp_exception vide) ; le remplisseur de listes (pc=$24F2E/30, période
   1792 cyc = structure de boucle, pas un timer) est SYNCHRONE à instant égal
   (piège de convention : --dump-at N NeoST == b VBL=N+1 Hatari).
4. **Oracle INSTRUMENTÉ** : `[LADDR]` dans Video_CopyScreenLineColor
   (extern/hatari/src/video.c, gated HATARI_LINE_ADDR=1, marqué « NEOST TEMP
   for closure ») : adresse du raster à CHAQUE ligne → octets consommés par
   ligne. Les deltas sont invariants à l'ancrage.

**LA MESURE DÉCISIVE** : les octets consommés par les lignes de transition
34-38 (le haut animé du logo, lignes BLANK où la démo bascule 60/50) forment
un MOTIF par trame qui change chaque trame. Séries mesurées :

- Hatari vbl 1498→1501 : [160,160,160,158,…] [160,162,0,186,…]
  [160,184,158,158,…] [160,184,0,186,…]
- NeoST vbl 1493→1496 : LES MÊMES QUATRE, dans le même ordre (ancrage +5) ✓
- NeoST vbl 1497→1500 : [160,160,204,158] [160,162,230,0] [204,184,158,158]
  [160,230,184,0] — **valeurs 204/230 à des positions JAMAIS produites par
  Hatari** (le 230 = mask 1011 : un RIGHT_OFF détecté sur ligne blank là où
  l'oracle voit RIGHT_MINUS_2/rien — frontière ~373-377, MÊME famille que le
  Freq_match du crash résolu au Cycle 5).

**Conséquence** : dès qu'un motif étranger apparaît, le cumul d'octets des
transitoires diverge de ce que la démo (auto-calibrée) attend → tout le
bitmap de la trame est décalé de ±44/±46 octets → le damier pré-compensé
sort haché (sauts +11 px / ~9 lignes, période = les marches du dessin).

**PROCHAINE ÉTAPE** : prolonger les deux séries ([LADDR] 1490-1515 vs
[render] NEOST_RENDER_ALL), aligner (offset ~5), isoler LA première trame au
motif étranger, extraire les écritures 60/50 de ses lignes 34-38
(NEOST_WRITE_DIAG/[GLUP] vs --trace video_sync) et comparer la datation de
la bascule qui fait le RIGHT_OFF fantôme — vraisemblablement une classe
d'opcode dont la datation par parité (Cycle 5) reste fausse, ou la fenêtre
RIGHT_OFF sur ligne BLANK. Corriger, puis : banc complet + les deux AVI.

Diags neufs : NEOST_WATCH=hex (watch d'écriture bus daté, Bus.hpp/Machine.cpp)
— non commité, comme le [LADDR] Hatari.

### Cycle 6 — RÉSOLUTION : le port manquant du scroll hardware STF (X-DISTING)

⚠ La piste « motifs de transitoires hors répertoire » ci-dessus est RÉFUTÉE : en
prolongeant la série Hatari à VBL 1515, nos motifs « étrangers » apparaissent
TOUS chez lui (vbl 1504-1507, même ordre — huit motifs consécutifs identiques,
ancrage +7 sur ce run-là ; chaque run son ancrage). Transitoires FIDÈLES.

**La preuve décisive** : le re-rendu python nourri des données d'HATARI
(sa RAM + ses adresses [LADDR] + ses couleurs video_color datées, un seul run)
produit le DAMIER HACHÉ — alors que son image réelle est lisse. L'ingrédient
absent du modèle (et de notre renderer) était le différenciateur :
**video.c:3946-3990 — le scroll hardware 4 px STF / stab med** (« ST Cnx » et,
nominativement, « 'Closure' demo Troed/Sync »). Pour les lignes
LEFT_OFF/LEFT_OFF_MED avec displayPixelShift ∈ {13,9,5,1,0}, Hatari applique
PAR LIGNE : un OFFSET SOURCE en octets (VideoOffset {+2,0,−2,−4}, −4 pour le
stab 0 — « planes are shifted » : l'origine octet PERMUTE les plans) ET
« STF_PixelScroll −= 8 ». Notre renderer utilisait le shift BRUT sans offset
source : chaque ligne X-DISTING sortait fausse d'un montant dépendant de SON
scroll → les dents (+11 px / ~9 lignes = la période du pattern de la démo).

**Le port** (renderGlueFrame) : table (srcOff, shEff) relative à notre repère
calibré (LEFT_OFF standard shift −4 ↔ Hatari VideoOffset −2, nocooper 0 px) :
13→(+4,+5), 9→(+2,+1), 5→(0,−3), 1→(−2,−7), 0 (LEFT_OFF_MED seul)→(−2,−8).
srcOff en OCTETS sur la source (permutation de plans naturelle par le
décodage) ; offset négatif → repli RAM (les slots lineSnap_ ne sont pas
contigus). Résultat trame 1500 : **24 sauts → 5** (résidu lignes 66/74/114/
234/242), logo lisse, famille visuelle de l'oracle.

**Validation** : --tier full VERT (nocooper/greetings/diapos ×3/scrolls/
overscan_top/trace_odd/etos), Cuddly 3400 pixel-identique, EL/SHO/LX
pixel-identiques à l'avant-port (aucun titre du parc n'a ces lignes).

**Résidu : NUL.** Les « 5 sauts » restants (lignes 66/74/114/234/242) sont
mesurés À L'IDENTIQUE sur l'oracle (cal_f1500 : 69/77/237/245/253 ; var2 :
70/78/238/246/254 — mêmes familles ~67-78 et ~234-254, glissant avec la phase
d'ancrage) : ce sont les discontinuités LÉGITIMES du contenu (bords des
lettres du logo), pas des défauts. **Closure est à parité visuelle.**

### Cycle 6 — confirmations finales (couleurs décalées)

Les trois écrans signalés « couleurs décalées » (petit logo SYNC, cartons texte
« FOUR/BITPLANE/… », photo de la fée) sont VÉRIFIÉS PROPRES avec le binaire
post-port : cartons texte trame ~4750 (headless ET GUI ST/tos102uk/1M —
dégradés métalliques nets), photo trame ~10500 (153 couleurs, détails parfaits),
logo trame 1500 lisse. Les captures striées de l'utilisateur dataient d'avant
les correctifs du matin (datation par parité + port X-DISTING). Le shift des
lignes de la trame 1500 est 0 partout DES DEUX CÔTÉS (le X-DISTING de cette
phase n'utilise que le cas stab) — la table complète 13/9/5/1 servira aux
phases de morphing rapide.

⚠ Config : Closure se joue en **ST + tos102uk + 1 Mo**. La ROM tos162fr
(STE-only) bascule la machine en STE : le chemin STE de Closure n'est PAS
porté (Hatari lui-même le marque « FIXME : should be measured on real STE »,
video.c:3966-3970 : VideoOffset −6 / scroll −10) — écrans divergents attendus
sur STE, chantier séparé si besoin.

Carte de la démo (headless, trames) : 0-1000 chargement/intro noir ; ~800-1000
petit logo SYNC ; 1250-2300 grand logo X-DISTING ; 2500 flash blanc ; 2750-3750
écrans 4-5 couleurs ; 4250-5250 cartons texte métal ; 5750-6000 lettres
arc-en-ciel ; 7000-8500 effets 64-129 couleurs ; 10500 photo fée (153 coul.) ;
13500 écran 130 couleurs. `--shot-every 250/500` pour re-cartographier.

### Cycle 6 — addendum : logo d'intro (kSnapLead)

Le premier port (garde « srcOff négatif → repli RAM ») TUAIT le logo d'intro
SYNC (trames ~300-1200, pendant le chargement) : cet écran single-buffer
dessine/efface son logo EN COURSE avec le faisceau — la RAM de fin de trame est
déjà effacée, le repli la lisait → écran noir. C'est PRÉCISÉMENT l'artefact que
lineSnap_ prévient. Fix : **kSnapLead = 8 octets de garde en TÊTE de chaque
slot de capture** (les octets [base−8, base) capturés au faisceau aussi) → un
offset source négatif (≥ −8) reste dans le slot, plus aucun repli RAM pour ces
lignes. Vérifié : logo intro net trame 700 (2756 px ≈ oracle 11204/4), grand
logo intact trame 1400, banc full 39/39 VERT, Cuddly/EL/SHO/LX
pixel-identiques (la capture change pour TOUTES les lignes → banc obligatoire,
passé).
