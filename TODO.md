# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire — uniquement l'OUVERT.**

- Ce qui est fait, par puce → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)
- Titres déjà diagnostiqués (corrigés **ou** jugés fidèles) → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md)
- Chronologie (tout ce qui a été retiré d'ici) → [`CHANGELOG.md`](CHANGELOG.md)
- Écarts NeoST↔Hatari, `fichier:ligne` des deux côtés → [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md)
- Précision cycle (modèle, acquis, restant) → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)
- Logiciels étalons par sous-système → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md)

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06, STE
vidéo/son/joypads, blitter, RTC, SCC, SCU, ACSI, DD/HD) avec un timing assez fidèle pour jeux,
démos et utilitaires. **Atteint et GARDÉ** depuis le 2026-08-27 : `tools/run_megaste_diag.py`
rejoue la suite Q du diagnostic Field Service (12/12) à chaque palier `full`. Ce qui suit
affine ; rien ne conditionne plus l'objectif.

**Cible : le matériel réel.** Hatari (`extern/hatari/src`) et MAME sont des références
comparatives ; une erreur confirmée de l'un d'eux ne se reproduit pas (cf. [`CLAUDE.md`](CLAUDE.md)).
Aucune divergence de sévérité haute n'est ouverte.

### Règles de tenue de ce fichier

- Une entrée = **titre · priorité · statut**, puis 1 à 3 lignes et un lien. Le récit
  (mesures, pièges, preuves) va au `CHANGELOG.md` ou dans la doc du sous-système.
- **Priorité** : **P1** prochain chantier · **P2** utile, à planifier · **P3** à rendement
  faible ou « si un jour ».
- **Statut** : ⭘ ouvert · ◐ entamé · ⏸ attend une ressource externe (matériel, média,
  décision) — ceux-là vivent au § 2.
- Un item clos est **supprimé dans le commit qui l'ajoute au `CHANGELOG.md`**, jamais coché
  ni barré sur place.
- Les identifiants `A`*n* (dette d'architecture) sont stables ; un trou de numérotation
  est du travail fait.

---

## 1. 🎯 Prochaines actions

Premier tri proposé ; le détail est dans la section indiquée.

| # | Action | Prio | § |
|---|--------|------|---|
| 1 | A32 : `VideoCounter` en objet membre (A9 livré, un chantier à la fois) | P2 | 7 |
| 2 | Suite Hatari `screen` : NeoST = référence décalée de 4 px sur `flixfull` | P2 | 8 |
| 3 | Suite Hatari : `screen` (référence PNG au format Hatari) et `serial` | P2 | 8 |
| 4 | EtherNEC : livrer les pilotes libres GPL dans les paquets | P2 | 3 |

Aucun P1 faisable sans ressource externe : les P1 restants sont au § 2.

---

## 2. ⏸ En attente d'une ressource externe

- **Latence IACK MFP** · P1 ⏸ matériel — capturer CLK CPU/MFP, IACK et les DTACK
  puce/CPU sur un vrai ST et un vrai MegaSTE, aux trois régimes CPU/cache. Sonde prête
  (`tools/make_mfp_iack_probe.py`, contrôles `tools/run_mfp_iack_probe.py` verts). État au
  2026-10-22 : séquence d'exception identique à fx68k ; bloc IACK = 12 cycles bus (valeur
  d'Hatari, « not measured ») — le 16 posé le 2026-08-06 compensait le défaut STOP+4 de
  Moira, corrigé. La valeur matérielle (4 + attente DTACK du 68901, phase du 4 MHz) n'est
  connue d'aucune source ; le banc Mega STE (`docs/HATARI_MEGASTE_AUDIT.md`) est à refaire
  avec 12. → [protocole](docs/MFP_IACK_MEASUREMENT.md)
- **A12 — validation sur matériel réel** · P1 ⏸ matériel — macOS arm64 passé (sauf le pas
  visuel en session graphique). Restent les pas 3 à 5 pour **Raspberry Pi** (aucun budget
  temps réel mesuré), **Windows** (jamais lancé hors CI), **APK** (QEMU seul) et **Linux**.
  → [`docs/HW_VALIDATION.md`](docs/HW_VALIDATION.md)
- **Shadow Warriors** · P1 ⏸ média — après SPACE, titre et musique OK, mais le bouton
  joystick ne lance pas le jeu. Disque absent, y compris des médias privés restaurés le
  2026-10-20. Recette : diff contre l'oracle piloté au joystick
  (A5 → `docs/HATARI_AUTOMATION.md`), même durée d'appui (`--key-hold`).
- **Passe d'usage interactif du GUI** · P2 ⏸ session humaine — le GUI n'est couvert que par
  une capture au boot et trois assertions d'arguments. Une passe réelle, consignée (même
  famille qu'A12).
- **Notarisation macOS** · P2 ⏸ décision (99 $/an) — le `.app` est scellé ad hoc depuis
  le 2026-09-01 ; manque Developer ID + notarisation. Recette → [`docs/RELEASE.md`](docs/RELEASE.md).
  Signature du `.zip` Windows écartée (token HSM obligatoire, SmartScreen = un clic).
- **Vidéo — mesures sur Atari** · P2 ⏸ matériel — signaux HSYNC/VDE et couleur physique du
  blanking (palette 0 conservée en attendant), bord exact de la fin STOP_MIDDLE (Hatari
  rend trois pixels de plus ; sondes `blankline_stop_st/ste` en snapshot), tricks
  NO_SYNC/SYNC_HIGH. → `docs/CYCLE_ACCURACY.md` §4
- **Shifter arrêté (res 3 / `$FF8261`)** · P3 ⏸ matériel — porté le 2026-10-20 sans
  logiciel réel qui l'exerce. À mesurer : noir franc ou couleur 0 pendant l'arrêt, ce que
  relit un accès OCTET à `$FF8261`, comportement du trick sur STE, persistance du décalage
  aux trames suivantes. → `docs/HATARI_DIVERGENCES.md` § *Nouveaux — BASSES*
- **GPIP3 au repos avec blitter** · P3 ⏸ matériel — étayé par le RTL de Cwik
  (`stBlitter.sv:190`, INT = BUSY) mais pas mesuré — `$FFFA01` bit 3 supposé 0 au repos
  sur Mega ST/STE/Mega STE (INT = copie de BUSY) ; seule la lecture STFM sans blitter
  ($E9) est publiée. Lire `$FFFA01` au repos sur l'une de ces machines.
  → `docs/HATARI_DIVERGENCES.md` § *Cas où NeoST améliore Hatari*
- **Logiciels à clé / dongles** · P3 ⏸ média — `PortDevices` (Leader Board, 10th Frame,
  B.A.T. II, Music Master, « Pro Sound » de Wings of Death / Lethal Xcess) ; clé **Notator**
  (deux incertitudes : front de /ROM4 cadençant FEEDB1, ordre UDS↔/ROM4 à l'armement) ;
  clé **Steinberg** (Cubase 3.10 / Score / 2.01 originaux). Sans relevé public : Log 3,
  Pro-24, Avalon / Synthworks, Zodiac, DynaBlaster. Outil : capture `R3`/`R4`/`U` +
  `--key-replay`. → [`docs/EXTENSIONS.md`](docs/EXTENSIONS.md)
- **Notator n'a jamais tourné** · P2 ⏸ média — cité comme séquenceur cible au même titre
  que Cubase, sans étalon ni démarrage constaté.
- **NetUSBee — fenêtre LSB partagée** · P3 ⏸ schéma — `$FA0000-$FA01FF` = latch ISP1160 ET
  registre CR NE2000 ; les deux puces voient l'accès. Trancher sur le schéma
  (hardware.atari.org), puis ajuster `Bus::read8Slow`.
- **`union_demo`** · P3 ⏸ média — disque absent (planetemu manuel) ; appliquer la recette
  de migration A10 à son retour.
- **EtherNEC sous TOS 1.04** · P2 ⏸ média — valider STinG + `ENEC.STX` (DHCP, ping, GET),
  consigner dans `docs/CASE_STUDIES.md`. TOS 1.04 est présent (`roms/tos104*.img`) ; manquent
  les archives STinG 1.26 + `ENEC.STX` (URLs en tête de `tools/make_sting_test.py`).
- **GEMDOS : écarts communs à NeoST et Hatari face au vrai TOS** · P3 ⏸ mesure — Fdelete et
  Fopen sur un dossier (EACCDN/ERROR, TOS rend probablement EFILNF), Frename entre
  systèmes de fichiers hôtes (ERROR, TOS : ENSAME), Fwrite sur un fichier ouvert en
  lecture (ERROR à l'écriture, TOS refuse à l'ouverture), Dsetpath sur un chemin terminé
  par un masque. À relever sur TOS réel avant de diverger d'Hatari.
- **Stardust STE** · P3 ⏸ média — l'oracle affiche « INSERT DISK 2 IN ANY DRIVE » là où
  NeoST fond au noir puis scrute le lecteur B ; disquettes 2/3 absentes.
- **Exceptions vidéo E605 / Tekila** · P3 ⏸ programme — hacks par opcodes (FIXME) chez
  Hatari ; port différé jusqu'à reproduction sur les programmes, aucun défaut matériel
  démontré.
- **Hygiène FujiNet** · P3 ⏸ décision — code retiré le 2026-08-22 ; restent deux mentions
  historiques (commentaire de version save-state dans `src/core/Machine.cpp`, entrées
  `CHANGELOG.md`). Reformuler ou assumer.

---

## 3. Release & distribution

- **A10 — étalons adossés à une ROM Atari** · P3 ◐ — **39 étalons pixel sur 43** tournent
  sans TOS Atari (A3). Les trois `spectrum512_diapo*` restent des SKIP recensés
  (**migration EmuTOS réfutée, ne pas retenter**) ; leur motif est désormais rendu par
  `spec512_view`, étalon généré (oracle Hatari à 0 px). `union_demo` → § 2. Recette de
  migration : balayer les trames **au pas de 1** et retenir celle à 0 px, jamais la moins
  mauvaise (`rom_note` d'`etalons.json`).
- **Raccourcir `nocooper_greetings`** · P3 ⭘ — il borne à lui seul le mur du palier pixel
  (~45 s). Trois calendriers d'espaces essayés en vain : il faut d'abord savoir QUAND la
  démo relit le clavier.
- **EtherNEC : livrer les pilotes libres GPL dans les paquets** · P2 ⭘ — STinG + `ENEC.STX` ;
  la validation sous TOS 1.04 attend son média (§ 2).

---

## 4. Fidélité matérielle — restes par puce

Ne figure ici que l'ouvert ; l'acquis de chaque puce est dans `docs/IMPLEMENTED.md`, l'écart
détaillé dans `docs/HATARI_DIVERGENCES.md`.

### 4.0 CPU 68000 (Moira)
- **STOP : synchronisation IPL à deux étages** · P3 ⭘ — fx68k échantillonne la broche IPL
  deux fois (enPhi2) avant de poser `intPend` : ~2 cycles de latence de reconnaissance,
  aussi hors STOP. Moira voit la broche aussitôt. Du ressort de l'option `NEOST_IPLFETCH`
  (désactivée) ; à qualifier sur les bancs poll avant de l'activer.
- **Jitter des périodes Timer B face à Hatari** · P3 ⭘ — sonde Super Hang-On : NeoST rend
  {512, 516, 508, 520}, Hatari {512, 520, 504} (quantification de la prise d'IRQ dans la
  boucle `nop`/`bra.s` de la sonde, déplacée de 4 cycles par le STOP+8). Les deux sont des
  modèles ; trancher avec la synchronisation IPL à deux étages (item précédent) ou sur
  machine.

### 4.1 Vidéo / Shifter
- **STE : écriture 60 Hz à 32-36 cycles, image à une ligne de décalage** · P2 ⭘ —
  `blankline_blank_ste` aux phases 32/36 : 10 714 px qu'aucune trame Hatari ne produit
  (`run_etalons.py --oracle-check --only blankline_blank_ste` après avoir mis `frames` sur
  une phase 32/36). Comparer `VideoGlue.cpp` (branches 60 Hz `<= Preload_Start_Low_60`,
  NO_DE (36,40]) à `video.c` et à la netlist 4081P29 ; trancher pour le matériel.
  → `docs/HATARI_DIVERGENCES.md` § Vidéo.
- **Étalons sensibles à la phase E-clock** · P3 ⭘ — `blankline_*`, `spec512_*` capturent
  une trame dont la phase (cycle de 5 trames du réveil VBL) tourne à chaque changement de
  timing du boot ; les ancrer dans leur programme (compter les VBL depuis le démarrage, ou
  phase fixée par `stop` + attente) rendrait les références stables.
- **Latch couleur de la bordure gauche** · P3 ⭘ — 16 px (colonnes 45-60) : Hatari applique
  l'écriture palette ~16 px après le début nominal de l'aire active (latence pipeline),
  NeoST pile à `activeX_`. Invisible aux étalons.
- Mesures matérielles → § 2.

- **STE, impulsion hi-res : bords à ±4** · P3 ⏸ matériel — la netlist place le bord de
  rechargement HSC (longueur de ligne) et le décodage HDE au cycle ~56 ±4 ; une impulsion
  retirée dans (40,44] donnerait un DE précoce à ~48 (prédiction basse, non portée). À
  mesurer sur STE réel avant d'affiner `VideoGlue.cpp` (branche 71 Hz).

### 4.2 MFP 68901 & interruptions
- Latence IACK MegaSTE → § 2.
- ***Seven Gates of Jambala*** · P2 ⭘ — valider le recalage Timer B après écriture AER3
  (porté le 2026-10-19). Média désormais présent (`disks/seven_gates_of_jambala_{a,b}.stx`,
  non versionné).

### 4.3 ACIA 6850 / IKBD HD6301
- **Liaison série physique** · P3 ⭘ — formats et diviseurs autres que /64 8N1 (clavier),
  phase SCI sortante exacte ; ligne BREAK et erreurs de trame qui en découlent hors modèle.
- **Firmware IKBD, restes face à la ROM** · P3 ⭘ — le firmware est calé sur la ROM 6301
  désassemblée depuis le 2026-10-22. Restent approximés : `$19` est scruté toutes les 1 ms
  (la ROM le fait à chaque tour de boucle) ; l'état hôte (souris, manettes) ne change qu'aux
  événements de l'hôte ; code 6301 custom (`Execute`). Les délais de `$18` (tours de boucle
  et interruptions 1 ms 128/215 µs) sont comptés sur la ROM, pas encore mesurés sur un IKBD.
- **ACIA MIDI** · P3 ⭘ — source RX hôte limitée au câble 31 250 bauds 8N1 ; forme d'onde
  BREAK non simulée.

### 4.4 USART du MFP
- **Restes** · P3 ⭘ — modèle synchrone, réacquisition après réactivation au milieu d'un
  caractère, phase du premier front TC, forme d'onde BREAK physique.

### 4.5 SCC Z85C30 (MegaSTE)
- **Restes** · P3 ⭘ — autres fonctions de WR7', FIFO SDLC, DPLL, série hôte ; phase de bit,
  erreurs RX de BREAK, X21 ; Hunt avec un vrai synchroniseur bit à bit ; décodage
  parité/framing des formats TX/RX discordants. Phase initiale de l'oscillateur abstraite.

### 4.6 FDC WD1772 / DMA / ACSI
- **ACSI : essai Idris OS** · P3 ⭘ — jamais tenté.

- **Horloge du WD1772 sur STE = SCLK 8,010613 MHz** · P3 ⭘ — NeoST cadence le FDC sur
  l'horloge CPU (exact sur STF, −0,13 % sur STE : `Fdc.cpp` MFM_BYTE=256, `transferDelay`).
  Un accumulateur fractionnaire décalerait les rafales de ~5 cycles chacune ; Hatari
  tronque par octet (`rint`, incohérent, TODO chez lui). À valider sur matériel réel avant
  de diverger. Source : `clocks_timings.c` (OSC U303), schéma STE.

### 4.7 RTC RP5C15
- **Restes** · P3 ⭘ — sorties CLKOUT/ALARM, modes d'inspection TEST ; lecture TEST/RESET
  à zéro selon MAME, sans mesure du bus flottant réel.

### 4.8 FPU MC68881
- **k dynamique** · P3 ⭘ — fixé à 17 : le dialogue périphérique ne transmet pas Dn.
- **Silicium** · P3 ⏸ matériel — tout est bit à bit avec Hatari, transcendantes FPSP
  comprises, mais le 68881 réel calcule par microcode CORDIC : dernier bit non garanti,
  et « zéro » non normalisé après underflow simple/double à vérifier (probable vrai ±0).

### 4.9 GEMDOS (disque hôte)
- Codes d'erreur confrontés à `gemdos.c` le 2026-10-20 (portage 1:1, écarts consignés dans
  `docs/HATARI_DIVERGENCES.md`) ; écarts communs avec le vrai TOS → § 2.

### 4.10 Cartouche & profils machine
- **Port cartouche générique** `$FA0000-$FBFFFF` · P3 ⭘ — au-delà du système GEMDOS ;
  réf. `cart.c`.

- **Décodage Mega STE** · P3 ⏸ schéma — le décodage STE suit la netlist du 4081 depuis
  le 2026-10-22 ; le Mega STE reste au modèle Hatari faute de netlist.

### Hors périmètre
- **SCSI / NCR5380** — TT/Falcon uniquement, le MegaSTE n'en a pas. Réf. `ncr5380.c`.

---

## 5. Catalogue logiciels — bugs ouverts

TOS 1.02fr sauf mention ; chemins sous `disks/st/` ou `disks/stx/`.

| Jeu | Prio | Symptôme | Piste |
|-----|------|----------|-------|
| **Shadow Warriors** (2Hot2Handle) | P1 ⏸ | Le bouton joystick ne lance pas le jeu (Castle Warrior fonctionne). | Disque absent → § 2. |

> ⚠ **Avant de déclarer un bug : vérifier la RAM, puis la ROM.** Les cas déjà tranchés
> (Captain Blood, Enchanted Land, Cuddly, Rick Dangerous II, Stardust, Spectrum 512 STE,
> Blood Money, Arkanoid, Wings of Death…) sont dans
> [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md) — ne pas les rouvrir.

---

## 6. Extensions NeoST

Matériel ayant réellement existé sur ST, OFF par défaut → [`docs/EXTENSIONS.md`](docs/EXTENSIONS.md).

### Station MIDI
Cas visé : le ST mène la séance (Cubase, Notator), un Mac porte les AU/VST. La sortie est au
niveau (gigue σ 0,4-1,7 ms, `run_midi_sequencer.py`).
- **Interfaces multi-ports** · P3 ⭘ — C-Lab Unitor, Steinberg Midex (32/64 canaux, SMPTE) ;
  la clé de l'Unitor-N est déjà émulée, pas le boîtier.
- **Splitter clavier** · P3 ⭘ — une source = un canal ; pas de zones.
- **Synchro SMPTE/MTC** · P3 ⭘ — l'horloge MIDI traverse, le code temporel de l'Unitor non.
- **Port virtuel sous Windows** · P3 ⭘ — aucune API winmm/WinRT 1.0 ; loopMIDI comme
  contournement, Windows MIDI Services incompatible avec le zip autonome.
- **Appareils homonymes sous ALSA** · P3 ⭘ — indiscernables quand un seul est branché.

### Réseau, stockage, ports
- **EtherNEC** : validation sous TOS 1.04 → § 2 ; pilotes GPL dans les paquets → § 3.
- **UltraSatan : `US_CONF.TOS` réel** · P3 ⭘ — compiler l'outil de Jookie (Pure C) et le
  passer sur NeoST ; idem HDDRIVER / ICD PRO sur une image à deux slots.
- **NetUSBee : périphériques USB** · P3 ⭘ — l'ISP1160 est un hub racine vide ; brancher HID
  puis stockage de masse derrière `HcRhPortStatus` (banc : FreeMiNT `netusbee.ucd` + `usb.km`).
- **Modem / STinG** · P3 ⭘ — documenter l'installation (noyau + `sting.inf` dans `AUTO`,
  modules dans `C:\STING`) dans `docs/TEST_SOFTWARE.md` ; banc SLIP de bout en bout.
- **Anneau MIDI** · P3 ◐ — réception headless cadencée livrée ; restent la saisie du pair
  dans le GUI et un test à deux nœuds (en un seul processus, bloqué par A33).
- **Dongles dans les frontends WASM/Android** · P3 ⭘ — pas de page Dongles.
- **Liste blanche de domaines** · P3 ⭘ — optionnelle, pour les backends sortants.

---

## 7. Dette d'architecture

Issue de l'audit du 2026-08-27 et des revues suivantes ; le soldé est au `CHANGELOG.md`.

- **Build sans ImGui** · P3 ⭘ — `-DNEOST_WITH_IMGUI=OFF` ne compile plus (constaté
  le 2026-10-20, antérieur à A9) : des fichiers GUI incluent `imgui.h` sans condition et
  `AppLoop.cpp` manque de déclarations (`keyboardWindowReleaseAll`, `MidiInHost`…).
  Réparer ou retirer l'option.
- **A9 — `appLoop` découpée** · P3 ◐ — `main()` tient en 10 lignes, et `appLoop`
  (`src/gui/AppLoop.cpp`) n'est plus qu'une suite d'appels : les variables de trame vivent
  dans une `struct Frame`, et les 28 phases (entrées / trames dues / dessin / requêtes /
  présentation) sont des fonctions statiques `(App&, Frame&)` déplacées mécaniquement.
  Plus grosse pièce restante : `runKioskMenu` — 380 lignes d'un seul tenant (ses pages
  partagent fronts, directions et lambdas de navigation). Prochain pas éventuel : sortir
  ses pages, puis `consumeConfigRequests` (219 lignes). Déplacements mécaniques uniquement.
- **A32 — `Shifter` en trois classes** · P2 ◐ — fichiers et noms faits ; les rôles restent
  des méthodes qui partagent l'état par ligne. Prochain pas : `VideoCounter` en objet membre
  (champs `vc*`), mesurer ce que ça casse, puis seulement regarder la Glue.
- **Suivre l'amont Moira** · P3 ⭘ — 10 commits non portés depuis le pin (68020/68030,
  vAmiga, empreinte mémoire) ; rien pour le 68000 hormis DIVU/DIVS, porté. Rejouer
  `tools/run_moira_runner.sh` à chaque patch du cœur.
- **A38 — tenue du registre** · P3 ⭘ — `tools/fast_guards.json` inscrit les 21 étapes du
  palier `fast` (refus d'une étape non inscrite) et `run_all.py` affiche le mur et les
  contrôles sans déclenchement réel depuis 180 jours. Reste à y consigner chaque vrai
  déclenchement, dans le commit qui corrige le défaut.
- **A39 — modules jamais audités** · P3 ◐ — pile réseau `src/net` (couverte par le
  comportement seulement) ; un test par COUCHE du bac à sable GEMDOS (le test actuel ne
  rougit qu'en retirant les deux défenses), à décider car il couple test et implémentation.
- **A33 — vrai parallélisme CPU** · P3 ◐ — l'état est par instance ; deux CPU dans deux
  threads demanderaient de retirer les accès à `g_cur` (≈ 215 dans `src/core/Cpu68k.cpp`,
  recomptés le 2026-10-20). Aucune fonctionnalité ne le
  réclame, sauf l'anneau MIDI en un seul processus.
- **A15 — DSL d'injection** · P3 ⭘ — pas de token « bouton tenu pendant un mouvement »
  (pas de DRAG GEM).

### Garde-fous (à NE PAS faire)
- **Croire que la liste se vide** : auditer coûte une journée, réparer des semaines.
  Arbitrer explicitement entre « ouvrir » et « fermer » à chaque session.
- **Combiner A9 + A32 (ou tout autre chantier structurel) en un « grand refactor »** :
  un à la fois, filet de test posé AVANT.
- **Supprimer une branche d'exécution sur une intuition** : précédent A34, tranché par
  la mesure.
- **Compenser pour égaler l'oracle** : ni les délais du blitter pour absorber le résidu
  IACK, ni les bords STOP_MIDDLE, ni `closure` passé en `oracle` (il reste `snapshot`
  définitivement, cf. A41 au `CHANGELOG.md`).
- **Garder un contrôle par un autre contrôle** : A38 ne se traite pas avec un 18ᵉ outil.

---

## 8. Tests & outillage

- **Suite de tests d'Hatari : restes** · P2 ◐ — `tools/run_hatari_suite.py` (palier `full`)
  rejoue cycles, blitter, bus errors, `cpu` et `gemdos` : 12/12. Restent `screen`
  (référence PNG au format Hatari) et `serial` ; `xbios`/`mem_end` sont hors périmètre
  (extensions propres à Hatari, cf. `docs/HATARI_AUTOMATION.md`).
- **`screen/flixfull.prg` (plein écran)** · P2 ⭘ — la capture NeoST est identique à la
  référence d'Hatari (reproduite par l'Hatari épinglé) décalée de 4 px horizontalement.
  Trancher entre cadrage de capture (`--screenshot-dir` en `--tos none`) et fenêtre gauche
  en overscan (cf. A40) en repassant le programme par `hatari_oracle.sh`.
- **Étalon Troed** · P3 ⭘ — à rapatrier et calibrer ; Union → § 2.
- Banc temporel Hatari : 20 sondes, dont écriture DMA disquette, lecture STX et DIVU/DIVS ;
  le résidu blitter/IRQ MegaSTE 16 MHz attend la capture IACK (§ 2).
  → [`docs/HATARI_MEGASTE_AUDIT.md`](docs/HATARI_MEGASTE_AUDIT.md)
- **Étalon spec512 en overscan** · P3 ⭘ — si une démo libre à bordures ouvertes est
  rapatriée (l'auto_diapo est sans bordure).
- **Empreinte machine + relecture d'entrées** · P2 ⭘ — hacher l'état complet (CPU, RAM,
  puces, via `StateArchive`) toutes les N trames et rejouer un `.state` + entrées par trame :
  la première empreinte divergente date une perte de déterminisme. Socle du retour arrière
  et d'un jeu en ligne en lockstep. Modèle : ASE `Netplay/MachineHash.cs`, `InputRecording.cs`.
- **RTL MiSTer comme référence matérielle** · P3 ⭘ — `gstmcu.v` (schéma ST4081S),
  `stBlitter.sv` (Cwik) : plus proches du silicium qu'Hatari pour arbitrer ; à citer dans
  `docs/HATARI_MAPPING.md`.
- **Pistes MiSTery/netlist non portées** · P2 ⭘ — preuve moyenne ou travail risqué
  (2026-10-22) : couleur pendant BLANK forcée au noir (`gstshifter.v:264`, schéma 4118) au
  lieu de la palette 0 ; octet `$FF8261` relu $00 (`gstshifter.v:121`) ; mode pulse des
  timers A/B du MFP (comptage conditionné par TAI/TBI + IRQ GPIP4/3 sur le front opposé,
  fiche Motorola) ; préchargement DMA en écriture (`dma.v:277`) ; période d'enveloppe YM à
  l'écriture de R13 ; reset STE : `$FF8901` gardé (PORB seul) et base vidéo effacée par
  /RESET (netlist 4081, lecture d'agent) ; DMA externe prioritaire sur le blitter en HOG
  (netlist 4082, DMARQ, confiance basse) ; délai de bus error sur ST, Mega ST et Mega STE
  (porté sur STE seulement, faute de netlist du GLUE).
- **Comparaison MAME ↔ NeoST** · P3 ⭘ — memory map, bus errors, FIFO FDC/MMU, blitter, SCC.
- **Traces Hatari de référence pour `trace_diff`** · P3 ⭘ — Arkanoid et autres.
- **Matrice MegaSTE DD/HD × cache** · P3 ⭘ — balayage systématique si un titre l'exige.

> **L'oracle se bâtit** : `extern/hatari` est gitignoré, absent d'une machine fraîche.
> `tools/setup_hatari.sh` clone au pin et bâtit → [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

---

## 9. Interface

- **Retour arrière (rewind)** · P2 ⭘ — un `.state` par seconde en anneau, disquettes copiées
  seulement si modifiées ; jamais pendant une commande STX minutée ; désactivé avec un
  disque dur. Modèle : ASE `TimeWarp.cs`. Dépend de l'empreinte machine (§ 8).
- **Explorateur de bitmaps** · P3 ⭘ — afficher la RAM en plans ST avec la palette courante
  (largeur, plans, adresse réglables) dans le débogueur. Modèle : ASE `DebugWindow`.
- **Identification des disquettes de menus** · P3 ⭘ — nom de fichier → jeu (Automation,
  D-Bug…) pour la ludothèque kiosk ; catalogue ASE `GameMenus.json` (licence à vérifier).

- **Kiosk & CRT, cosmétique** · P3 ⭘ — membres `srcW_`/`srcH_` morts dans
  `CrtEffectStack` ; tenir gauche/droite bloque la répétition haut/bas en kiosk.
  Limitations CRT v1 assumées (baril/vignette sur le buffer entier en kiosk ;
  GL 2.1 → passthrough).
