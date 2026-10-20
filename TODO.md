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
| 1 | GEMDOS : codes d'erreur face à `gemdos.c` | P2 | 4.9 |
| 2 | A9 : `struct Frame` puis découpe d'`appLoop` | P2 | 7 |
| 3 | A38 : trace du dernier déclenchement réel des contrôles `fast` | P2 | 7 |
| 4 | Étalon spec512 généré (remplace les 3 `spectrum512_diapo*`) | P2 | 3 |
| 5 | Comparaison temporelle Hatari : sonde STX | P2 | 8 |
| 6 | FPU : transcendantes encore en double hôte | P2 | 4.8 |
| 7 | Lethal Xcess : re-vérifier en GUI | P2 | 5 |
| 8 | Cadrer l'écart d'angle de rotation disquette (~9 500 cycles) avec Hatari | P2 | 4.6 |

Aucun P1 faisable sans ressource externe : les P1 restants sont au § 2.

---

## 2. ⏸ En attente d'une ressource externe

- **Latence IACK MegaSTE** · P1 ⏸ matériel — capturer CLK CPU/MFP, IACK et les DTACK
  puce/CPU sur un vrai MegaSTE, aux trois régimes CPU/cache. Sonde prête
  (`tools/make_mfp_iack_probe.py`, contrôles `tools/run_mfp_iack_probe.py` verts) ;
  résidu IACK→handler de +3/+4 cycles à 16 MHz à expliquer. → [protocole](docs/MFP_IACK_MEASUREMENT.md)
- **A12 — validation sur matériel réel** · P1 ⏸ matériel — macOS arm64 passé (sauf le pas
  visuel en session graphique). Restent les pas 3 à 5 pour **Raspberry Pi** (aucun budget
  temps réel mesuré), **Windows** (jamais lancé hors CI), **APK** (QEMU seul) et **Linux**.
  → [`docs/HW_VALIDATION.md`](docs/HW_VALIDATION.md)
- **Shadow Warriors** · P1 ⏸ média — après SPACE, titre et musique OK, mais le bouton
  joystick ne lance pas le jeu. Disque absent de ce poste (`disks/st/` non restauré, archives
  privées introuvables le 2026-10-20). Recette : diff contre l'oracle piloté au joystick
  (A5 → `docs/HATARI_AUTOMATION.md`), même durée d'appui (`--key-hold`).
- **Passe d'usage interactif du GUI** · P2 ⏸ session humaine — le GUI n'est couvert que par
  une capture au boot et trois assertions d'arguments. Une passe réelle, consignée (même
  famille qu'A12).
- **Test F de la cartouche STE_Test v1.9** · P2 ⏸ média — « Cannot write drive A/B », lecteurs
  vus simple face, alors que le test F du diagnostic MegaSTE passe. Cartouche absente de
  l'arbre : trace FDC + oracle Hatari sur la même cartouche dès son retour.
- **Notarisation macOS** · P2 ⏸ décision (99 $/an) — le `.app` est scellé ad hoc depuis
  le 2026-09-01 ; manque Developer ID + notarisation. Recette → [`docs/RELEASE.md`](docs/RELEASE.md).
  Signature du `.zip` Windows écartée (token HSM obligatoire, SmartScreen = un clic).
- **Vidéo — mesures sur Atari** · P2 ⏸ matériel — signaux HSYNC/VDE et couleur physique du
  blanking (palette 0 conservée en attendant), bord exact de la fin STOP_MIDDLE (Hatari
  rend trois pixels de plus ; sondes `blankline_stop_st/ste` en snapshot), tricks
  NO_SYNC/SYNC_HIGH. → `docs/CYCLE_ACCURACY.md` §4
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

- **A10 — 3 étalons adossés à une ROM Atari** · P2 ◐ — depuis la purge, le corpus est local
  par construction : **38 étalons pixel sur 42** tournent sans TOS Atari (A3). Les quatre restants :
  - `spectrum512_diapo`, `_diapo2`, `_diapo_ste` : **migration EmuTOS réfutée, ne pas
    retenter** (lancement par `AUTO`, l'oracle Hatari+EmuTOS échoue pareil). Seule voie :
    un **étalon généré** dans l'esprit de `tools/make_overscan_test.py`. La couverture
    « palette en cours de ligne » est déjà rendue par `spec512_bands`.
  - `union_demo` → § 2.
  - Recette de migration : balayer les trames **au pas de 1** et retenir celle à 0 px,
    jamais la moins mauvaise (détail dans le `rom_note` d'`etalons.json`).
- **Raccourcir `nocooper_greetings`** · P3 ⭘ — il borne à lui seul le mur du palier pixel
  (~45 s). Trois calendriers d'espaces essayés en vain : il faut d'abord savoir QUAND la
  démo relit le clavier.
- **EtherNEC : livrer les pilotes libres GPL dans les paquets** · P2 ⭘ — avec la validation
  TOS 1.04 du § 6.

---

## 4. Fidélité matérielle — restes par puce

Ne figure ici que l'ouvert ; l'acquis de chaque puce est dans `docs/IMPLEMENTED.md`, l'écart
détaillé dans `docs/HATARI_DIVERGENCES.md`.

### 4.1 Vidéo / Shifter
- **WS3 sous-pixel, interfoliage blitter** · P3 ⭘ → `docs/CYCLE_ACCURACY.md` §4.
- **Latch couleur de la bordure gauche** · P3 ⭘ — 16 px (colonnes 45-60) : Hatari applique
  l'écriture palette ~16 px après le début nominal de l'aire active (latence pipeline),
  NeoST pile à `activeX_`. Invisible aux étalons.
- Mesures matérielles → § 2.

### 4.2 MFP 68901 & interruptions
- Latence IACK MegaSTE → § 2.
- **Timer B après écriture AER3** · P2 ⭘ — recalage porté le 2026-10-19 ; valider sur les
  données de *Seven Gates of Jambala*.

### 4.3 ACIA 6850 / IKBD HD6301
- **Liaison série physique** · P3 ⭘ — formats et diviseurs autres que /64 8N1 (clavier),
  phase SCI sortante exacte ; ligne BREAK et erreurs de trame qui en découlent hors modèle.
- **Firmware IKBD** · P3 ⭘ — modes souris absolus sous mise à l'échelle, horloge, code
  6301 custom (`Execute`) ; modes `$18`/`$19` reconnus mais non émulés (comme Hatari).
- **Démo Froggies complète** · P2 ⭘ — jamais validée de bout en bout.
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
- Test F de STE_Test → § 2.
- **Angle de rotation à l'armement** · P2 ⭘ — sur la sonde `fdc_write`, NeoST rencontre le
  secteur ~9 500 cycles plus tard qu'Hatari (même image, même programme). Intervalles et
  fin identiques ; l'attente initiale est exclue des deux sondes FDC. Confronter la phase
  d'index au boot (`fdc.c`) avant d'en faire une divergence.
- **ACSI : essai Idris OS** · P3 ⭘ — jamais tenté.

### 4.7 RTC RP5C15
- **Restes** · P3 ⭘ — sorties CLKOUT/ALARM, modes d'inspection TEST ; lecture TEST/RESET
  à zéro selon MAME, sans mesure du bus flottant réel.

### 4.8 FPU MC68881
- **Transcendantes en double hôte** · P2 ⭘ — seul calcul encore non bit-exact.
- **k dynamique** · P3 ⭘ — fixé à 17 : le dialogue périphérique ne transmet pas Dn.
- Les bancs comparent au moteur Hatari, pas à un 68881 physique.

### 4.9 GEMDOS (disque hôte)
- **Codes d'erreur face à `gemdos.c`** · P2 ◐ — handles, Pexec, reprise et Fsfirst/DTA
  couverts ; le reste des chemins d'erreur à confronter.

### 4.10 Cartouche & profils machine
- **Port cartouche générique** `$FA0000-$FBFFFF` · P3 ⭘ — au-delà du système GEMDOS ;
  réf. `cart.c`.

### Hors périmètre
- **SCSI / NCR5380** — TT/Falcon uniquement, le MegaSTE n'en a pas. Réf. `ncr5380.c`.

---

## 5. Catalogue logiciels — bugs ouverts

TOS 1.02fr sauf mention ; chemins sous `disks/st/` ou `disks/stx/`.

| Jeu | Prio | Symptôme | Piste |
|-----|------|----------|-------|
| **Shadow Warriors** (2Hot2Handle) | P1 ⏸ | Le bouton joystick ne lance pas le jeu (Castle Warrior fonctionne). | Disque absent → § 2. |
| **Lethal Xcess** | P2 | Titre « buggé à ~8 % » en GUI (2026-07-02). | Probablement la calibration `$8209` déjà réparée en jeu ; re-vérifier en GUI. |

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
- **EtherNEC sous TOS 1.04** · P2 ⭘ — valider STinG + `ENEC.STX` (DHCP, ping, GET),
  consigner dans `docs/CASE_STUDIES.md`.
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

- **A9 — `appLoop` d'un seul tenant** · P2 ◐ — `main()` tient en 10 lignes, mais
  `src/gui/AppLoop.cpp` reste `appLoop` — 1965 lignes d'un seul tenant, déplacées et pas
  découpées. Prochain pas :
  nommer la vingtaine de variables de trame dans une `struct Frame`, PUIS couper aux
  frontières déjà commentées (entrées / trames dues / dessin / requêtes / présentation).
  Déplacements mécaniques uniquement, comme pour `App`.
- **A32 — `Shifter` en trois classes** · P2 ◐ — fichiers et noms faits ; les rôles restent
  des méthodes qui partagent l'état par ligne. Prochain pas : `VideoCounter` en objet membre
  (champs `vc*`), mesurer ce que ça casse, puis seulement regarder la Glue.
- **Suivre l'amont Moira** · P3 ⭘ — 10 commits non portés depuis le pin (68020/68030,
  vAmiga, empreinte mémoire) ; rien pour le 68000 hormis DIVU/DIVS, porté. Rejouer
  `tools/run_moira_runner.sh` à chaque patch du cœur.
- **A38 — les 17 outils du palier `fast`** · P2 ⭘ — tenir pour chacun la trace de son
  dernier déclenchement RÉEL, fixer une règle de retrait, re-mesurer le mur du palier
  à chaque ajout.
- **A39 — modules jamais audités** · P3 ◐ — pile réseau `src/net` (couverte par le
  comportement seulement) ; un test par COUCHE du bac à sable GEMDOS (le test actuel ne
  rougit qu'en retirant les deux défenses), à décider car il couple test et implémentation.
- **A33 — vrai parallélisme CPU** · P3 ◐ — l'état est par instance ; deux CPU dans deux
  threads demanderaient de retirer les 94 accès à `g_cur`. Aucune fonctionnalité ne le
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

- **Comparaison temporelle Hatari : sonde STX** · P2 ⭘ — 19 sondes passent, dont l'écriture
  DMA disquette et DIVU/DIVS en RAM ; manque une lecture depuis une image STX (générateur
  Pasti à écrire). Le résidu global blitter/IRQ MegaSTE 16 MHz est réduit au seul segment
  IACK→handler, qui attend la capture matérielle (§ 2). → [`docs/HATARI_MEGASTE_AUDIT.md`](docs/HATARI_MEGASTE_AUDIT.md)
- **Self-tests puce nue supplémentaires** · P2 ⭘ — autres Timers MFP, ACIA.
- **Étalons à calibrer** · P2 ⭘ — Troed, Hatari Test Suite (frames + références) ; Union → § 2.
- **Étalon spec512 en overscan** · P3 ⭘ — si une démo libre à bordures ouvertes est
  rapatriée (l'auto_diapo est sans bordure).
- **Comparaison MAME ↔ NeoST** · P3 ⭘ — memory map, bus errors, FIFO FDC/MMU, blitter, SCC.
- **Traces Hatari de référence pour `trace_diff`** · P3 ⭘ — Arkanoid et autres.
- **Matrice MegaSTE DD/HD × cache** · P3 ⭘ — balayage systématique si un titre l'exige.

> **L'oracle se bâtit** : `extern/hatari` est gitignoré, absent d'une machine fraîche.
> `tools/setup_hatari.sh` clone au pin et bâtit → [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

---

## 9. Interface

- **Kiosk & CRT, cosmétique** · P3 ⭘ — membres `srcW_`/`srcH_` morts dans
  `CrtEffectStack` ; tenir gauche/droite bloque la répétition haut/bas en kiosk.
  Limitations CRT v1 assumées (baril/vignette sur le buffer entier en kiosk ;
  GL 2.1 → passthrough).
