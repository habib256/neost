# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire — uniquement l'OUVERT.**

- Ce qui est fait, par puce → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)
- Titres déjà diagnostiqués (corrigés **ou** jugés fidèles) → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md)
- Chronologie (tout ce qui a été retiré d'ici) → [`CHANGELOG.md`](CHANGELOG.md)
- Écarts NeoST↔Hatari, `fichier:ligne` des deux côtés → [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md)
- Écarts Moira↔fx68k, cycle bus par cycle bus → [`docs/FX68K_BENCH.md`](docs/FX68K_BENCH.md)
- Précision cycle (modèle, acquis, restant) → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)
- Logiciels étalons par sous-système → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md)
- Contre quoi chaque verdict est vrai → [`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)
- Échange du cœur : porte et contrat → [`docs/CPU_SWAP_GATE.md`](docs/CPU_SWAP_GATE.md)

## Le cap

**Le meilleur émulateur d'Atari ST, STE et Mega STE — puis le TT et le Falcon.** Le prochain
jalon est l'échange du cœur 68000 par le Moira de POM68K. Ce fichier est rangé par rapport
à lui, en trois temps :

1. **Avant l'échange** (§ 1 à 8) — tout ce que le cœur actuel n'empêche pas de fermer :
   les puces autres que le CPU, la logithèque, le harnais, la livraison. Et d'abord **le
   filet** (§ 2) : plus NeoST est mesuré finement avant l'échange, plus l'échange se juge
   vite et sans débat.
2. **L'échange** (§ 9) — ce qui touche l'intérieur du cœur est **gelé** : un patch de plus
   dans `extern/moira` est un patch de plus à retrouver dans POM68K.
3. **Le TT et le Falcon** (§ 10) — rien ne s'intègre avant que l'auteur déclare le cœur
   prêt ; d'ici là, de la préparation seulement.

Quatre exigences ne changent pas d'un temps à l'autre : **le silicium pour référence**
(chaque affirmation porte son grade, [`CLAUDE.md`](CLAUDE.md) ; Hatari et MAME sont des
témoins, jamais la cible), **la logithèque qui tourne comme sur la machine**, **un harnais
dont chaque verdict dit son échelon**, **des paquets livrables sans surprise**.

**État au 2026-11-10** : contrat logiciel Mega STE tenu et gardé (suite Q du diagnostic Field
Service 12/12, grade C) ; les écarts que les fiches MFP, WD1772, ACIA et SCC suffisaient à
trancher sont corrigés ; **banc fx68k : 34 variantes sur 43 en accord** cycle à cycle (§ 9) ;
blitter, GLUE et lecture du faisceau confrontés aux RTL sur STE (§ 5) ; 21 parcours de jeu
contre Hatari, et la collection entière inchangée en A/B après les corrections du jour.
**Pas d'accès à un vrai ST** : les ⏸ matériel passent par les relevés de tiers, les
documents primaires, les ROM et le RTL épinglé.

### Règles de tenue de ce fichier

- Une entrée = **titre · priorité · statut**, puis 1 à 3 lignes et un lien. Le récit
  (mesures, pièges, preuves) va au `CHANGELOG.md` ou dans la doc du sous-système.
- **Ce fichier est la seule liste de l'ouvert.** Une doc de sous-système décrit un modèle,
  un banc ou un cas ; son « reste à faire » tient en un renvoi ici.
- **Priorité** : **P1** prochain chantier · **P2** utile, à planifier · **P3** à rendement
  faible ou « si un jour ». Elle se lit à l'aune du cap : gain de grade, titres touchés,
  verdicts montés d'un échelon, échange rendu plus sûr.
- **Statut** : ⭘ ouvert · ◐ entamé · ⏸ attend une ressource externe (matériel, média,
  schéma, décision, cœur). Dans chaque section, **ce qui est faisable vient d'abord, les ⏸
  ensuite**.
- **Une question sur le silicium vit au § 3, sous sa puce**, avec son grade actuel et ce
  qui la tranche ; ce qui relève de l'intérieur du 68000 vit au § 9. **aligné Hatari**
  marque une décision dont la seule justification était l'égalité avec Hatari : le code ne
  bouge pas avant une source de rang supérieur.
- Un item clos est **supprimé dans le commit qui l'ajoute au `CHANGELOG.md`**, jamais coché
  ni barré sur place.
- Les identifiants `A`*n* (dette d'architecture) sont stables ; un trou de numérotation
  est du travail fait. Les autres docs citent un item **par son titre**, pas par son
  numéro de section.
- *(proposé)* marque un item ajouté lors d'une réorganisation et pas encore validé par
  l'auteur : à garder, reformuler ou supprimer.

---

## 1. 🎯 Plan avant l'échange

Cinq lots, dans l'ordre. Un lot n'attend pas la fin du précédent, mais c'est l'ordre des
arbitrages : le filet passe avant la fidélité, parce qu'il sert aussi à juger chaque
correction de fidélité.

| Lot | But | Items (titre → §) | Prio |
|-----|-----|-------------------|------|
| **0. Ligne de base** | Une version publiée sur le cœur actuel, à laquelle tout se compare ensuite | Fait : **0.6.3** publiée le 2026-11-10 (`355b6c0`). Une dernière version juste avant l'échange reste possible. | — |
| **1. Filet** | Que l'échange se juge en une commande, sur plus que 56 étalons | Porte élargie, banc fx68k étendu, Mega STE 16 MHz au banc, sort des `.state` → § 2 | P1 |
| **2. Silicium hors cœur** | Fermer ce que les sources déjà en main tranchent | DMA disque sur le bus, restes vidéo au cycle, restes MFP de la fiche, délai de bus error, WS3 → § 3 ; étalons `rtl` en cours de ligne, relevés de tiers, ST-STE Hardware Test, documents primaires → § 5 | P2 |
| **3. Logithèque** | Plus aucun plantage que le vrai matériel n'aurait pas | TOS 2.06 UK double faute, parcours étendus (son, STE, parties longues) → § 4 | P2 |
| **4. Livrable** | Ce qui sert l'utilisateur sans toucher à l'émulation | Disquettes de menus nommées, retour arrière, pilotes EtherNEC, EWeb par le NAT → § 6-7 | P2 |
| **5. Terrain du TT/Falcon** | Préparer sans rien intégrer | A32, inventaire des hypothèses ST dans le code, oracle et fiches TT/Falcon → § 8 et § 10 | P2 |

**À ne pas entreprendre avant l'échange** : tout ce qui est au § 9 (prise d'IPL, IACK,
amont Moira, `g_cur`, points chauds du cœur). Le reste est P3, ou attend une ressource
externe (⏸).

---

## 2. 🥅 Le filet de l'échange

Ce qui doit exister **avant** le commit de l'échange pour qu'il se juge sur pièces.
Procédure et contrat → [`docs/CPU_SWAP_GATE.md`](docs/CPU_SWAP_GATE.md).

- **Porte élargie aux parcours et à la collection** · P1 ⭘ *(proposé)* —
  `tools/cpu_swap_gate.py` ne rejoue que les 56 étalons pixel. Les 21 parcours de jeu et la
  collection (94 images, ST et STE) exercent bien plus de code, mais se comparent
  aujourd'hui à Hatari, donc sans égalité à l'octet. Leur faire relever la même empreinte
  `--arch-hash-every`, NeoST contre NeoST, sans Hatari ni Pillow.
- **Banc fx68k, étendre** · P1 ◐ — tous les modes d'adressage, MOVE, ALU, MOVEM, sauts, RESET,
  RTR, traps et ligne A/F en utilisateur concordent. Restent BERR et double faute, et
  l'autovecteur dans le verdict : ce sont les chemins où deux cœurs divergent le plus
  volontiers. → `docs/FX68K_BENCH.md` § *Ce qui manque*
- **Mega STE à 16 MHz dans le filet** · P2 ⭘ — la porte ne le voit que par ses étalons, alors
  que l'horloge du cœur y vaut deux fois l'horloge bus (`cpuMul`, `cpuBias`). Banc du
  blitter à 16 MHz, disputes de bus avec le DMA, blit sur ses propres registres : 78
  programmes en accord à 8 MHz ; `blitter_timer` et deux sondes s'écartent d'Hatari,
  consigné. → `docs/STBLITTER_BENCH.md`
- **Sort des `.state` à l'échange** · P2 ⏸ décision *(proposé)* — la section CPU du save-state
  change de forme avec le cœur. Refus explicite des anciens états, comme à chaque
  changement de version, ou reprise par `Cpu68k::archState` ? À décider avant, pour que
  `tools/run_determinism.py` ait un attendu.
- **Enregistrer la référence** · P1 ⏸ cœur — `cpu_swap_gate.py --record` sur le **dernier**
  commit avant l'échange, palier `full` vert. La référence ne se versionne pas.

---

## 3. 🔬 Fidélité au silicium, hors cœur — par puce

Sources citées à leurs épingles : `tools/setup_silicon_refs.sh`. Une fiche ou un schéma se lit
avant le RTL, et un `rtl` ne tranche qu'à titre provisoire (règles →
[`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)). Les écarts de sévérité basse déjà
consignés face à Hatari restent dans [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md) ;
ici ne figure que ce qui demande une décision ou une source. Le 68000 lui-même → § 9.

### 3.1 GLUE, MMU, Shifter — vidéo et bus

- **DMA disque sur le bus** · P2 ⭘ — il passe par BR/BG et ASO/DSO, un mot par 4 cycles ([B] ST-4081 feuille 8) ; les
  32 cycles de NeoST pour 8 mots concordent, par lecture. L'arbitrage autour de la rafale n'est ni simulé ni relevé.
  ST et Mega STE : aucune netlist, le STE fait référence. → `docs/CYCLE_ACCURACY.md` § 5
- **Restes vidéo au cycle** · P2 ⭘ — `VIDEO_ENDLINE` distinct du HBL, VBL au cycle exact
  (vraie fin de trame + offset 64/68), phase exacte du tic Timer C, lecture du compteur à
  cheval sur deux lignes. Étalons : démos plein écran. → `docs/CYCLE_ACCURACY.md` § 4
- **Délai de bus error sur ST, Mega ST et Mega STE** · P2 ⭘ · [F] — porté sur STE seulement,
  faute de netlist du GLUE. Chercher le schéma du GLUE sur chzsoft.de.
- **Wakestate WS3 par défaut** · P2 ⭘ · aligné Hatari — le modèle des quatre états est confirmé par le détecteur de Troed (`tools/run_wsdetect.py`, 162, 54, 206, 204 octets, et 180 sur STE). Le choix de WS3 comme défaut reste sans source : sur le fer, l'état se tire à la mise sous tension. Tranche : une statistique de relevés de tiers, ou un tirage au reset en option.
- **Basse résolution PAL issue du reset** · P2 ⭘ · [F] — le modèle GSTMCU garde une phase de
  plans décalée jusqu'à un passage par 60 Hz. Artefact du modèle ou effet réel ? Relevé de tiers.
- **Offsets de datation dérivés d'Hatari** · P2 ◐ — la lecture du compteur est confirmée sur STE (47 999 lectures sur 48 000, `docs/STCPU_BENCH.md`). `kVideoCounterReadOffsetCyc`,
  `kSyncWriteOffsetCyc`, `kSpec512AlignCyc` : fidèles à `Video_CalculateAddress`, donc [D].
  `$FF820A` : le modèle réagit à AS + 5, NeoST et Hatari datent à AS + 8. Avancer casse de vraies démos STE, donc non retenu,
  et la palette, contredite de même par 13 images réelles, va dans ce sens : le modèle prend les écritures trop tôt (`docs/REFERENCE_LADDER.md`). ⚠ read et write bougent par paire.
- **Bord de fin STOP_MIDDLE et signaux HSYNC/VDE** · P3 ⭘ · [F] — la fin de DE à 164 et les 54 octets concordent avec le modèle (`docs/GLUE_SWEEP.md`) ; Hatari rend trois pixels de plus, et le HSYNC parasite vers le cycle 28 n'est pas modélisé. Sondes `blankline_stop_st/ste` en snapshot.
- **STE, impulsions restantes face au modèle** · P3 ⭘ · [D] — DE précoce à 44, 14 impulsions sur 2560 en écart à 50 Hz, 42 sur 2560 à 60 Hz, toutes en haute résolution, et les
  largeurs au-delà de 32 qui enjambent HSYNC. Hors logiciel connu. → `docs/GLUE_SWEEP.md`
- **Écritures en plein DE sur STE** · P3 ⭘ · [D] — `$FF8207` et `$FF8265` agissent aussitôt sur le modèle, à la ligne suivante chez NeoST et Hatari. Aucun logiciel connu n'en dépend. Porter `$FF8207` demande un rendu en deux segments par ligne.
- **Hardscroll 4 px de Paulo Simoes** · P3 ⭘ · [D] — seul reste des tricks par changement de
  résolution (`video.c`, bloc V2) ; aucun titre de la collection ne l'exerce.
- **Étalons sensibles à la phase E-clock** · P3 ⭘ — `blankline_*`, `spec512_*` capturent une
  trame dont la phase tourne à chaque changement de timing du boot ; les ancrer dans leur
  programme (compter les VBL, ou phase fixée par `stop` + attente). **À faire avant
  l'échange si le nouveau cœur déplace la phase du boot.**
- **Couleur pendant BLANK** · P2 ⏸ source · [F] palette 0 — `gstshifter.v:263-265 @ 4a6db7cc` force le
  noir hors mono, et le banc le confirme (étalons `rtl_low_*` : seul écart, 2 lignes en PAL,
  13 en NTSC). Hatari l'admet (`video.c:4008-4009 @ f0736b24`). Code inchangé tant qu'une source
  plus haute manque ; passer au noir déplacerait les références `hatari` à bordure visible.
  ⚠ Le schéma 4118 est le shifter de la Panther ; aucun schéma du Shifter ST n'est disponible.
- **Vidéo — mesures sur Atari** · P2 ⏸ matériel · [F] — comportement électrique de
  NO_SYNC/SYNC_HIGH et confirmation au fer de tout ce § 3.1.
- **Bits 3, 7 et 11 de la palette relue sur ST** · P3 ⏸ matériel · [F] — NeoST les rend à 0. Hatari les tire au hasard (`video.c:5507 @ f0736b24`, « not defined », selon la dernière activité du bus), sauf si le PC est hors RAM. Pirates [cr Delight] relit ainsi sa palette, sans effet à l'image. Tranche : relire ces bits sur le fer, ou modéliser le dernier mot du bus.
- **Octet `$FF8261` relu** · P3 ⏸ source · [F] — `gstshifter.v:121 @ 4a6db7cc` rend $00 en octet bas ; NeoST garde l'alias de
  `$FF8260`, comme Hatari. Bloc « moyen » : le RTL ne l'emporte pas. Tranche : relevé sur STE.
- **Shifter arrêté (res 3 / `$FF8261`)** · P3 ⏸ matériel · [F] — porté sans logiciel réel qui
  l'exerce : noir franc ou couleur 0, comportement sur STE, persistance du décalage.
- **Décodage Mega STE** · P3 ⏸ schéma · [D] — le STE suit la netlist 4081 ; le Mega STE reste au
  modèle Hatari faute de netlist.

### 3.2 MFP 68901

- **Fiche contre code, restes de l'audit** · P2 ⭘ · [B] contre [D] — marqués `⚠` dans `src/io/Mfp.cpp`. TxDR relu à l'accès, et non au dernier front de DS (§ 5.2.1) ; effacement de UE en un bit au lieu d'un cycle de TC ; prescaler changé en marche (§ 5.1.1).
- **Jitter des périodes Timer B** · P2 ⏸ cœur · [F] — sonde Super Hang-On : NeoST {512, 516, 508,
  520}, Hatari {512, 520, 504}. Dépend de la prise d'IPL (§ 9) : à rejouer après l'échange.
- **Lecture du dernier cycle fractionnaire d'un timer** · P3 ⭘ · [F] — la conversion entière peut rendre 0 juste avant l'échéance interne ; forcer 1 change `mfp_poll` de 16 pixels. Lecture conservée, datation DS/horloge à isoler par une sonde ; distinct de l'arrêt du compteur corrigé selon la fiche le 2026-11-10. → `docs/HATARI_DIVERGENCES.md`
- **USART** · P3 ⭘ — modèle synchrone, réacquisition après réactivation au milieu d'un
  caractère, phase du premier front TC, forme d'onde BREAK physique.
- **Citations MAME sans révision** · P3 ⭘ · [D] — dans le MFP, l'ACIA, le YM et le FDC. Aucune copie de MAME dans `extern` : à épingler ou à retirer.

### 3.3 Blitter

Accord au cycle avec `stBlitter.sv` et fx68k sur 78 programmes, ST à 8 MHz → `docs/STBLITTER_BENCH.md`.
Extension du banc à 16 MHz → § 2.

- **DMA externe prioritaire sur le blitter en HOG** · P3 ⭘ · [F] — netlist 4082, DMARQ,
  confiance basse.
- **Résidu blitter/IRQ Mega STE 16 MHz** · P2 ⏸ IACK — comparaison temporelle exacte ; deux
  défauts Hatari isolés, 12-24 cycles résiduels après corrections expérimentales. Attend la
  latence IACK (§ 9). → `docs/HATARI_MEGASTE_AUDIT.md`

### 3.4 Son — YM2149, DMA STE, Microwire

- **IRQ de fin de trame du son DMA, datée au HBL** · P3 ⭘ · [D] — le fetch suit désormais la fin de DE à la lecture du compteur (`docs/STCPU_BENCH.md`), mais la fin de trame n'a pas son événement propre : au plus 136 cycles de retard. Profondeur réelle de la FIFO : 8 octets d'Hatari contre 7 mots du modèle, choix d'auteur. Tranche : relevé sur STE.
- **Tenue du dernier échantillon DMA** · P3 ⭘ · [D] — Hatari tient le niveau continu du dernier
  échantillon et coupe le YM si le mixage l'exclut, après une trame sans boucle
  (`dmaSnd.c`) ; NeoST a fait un autre choix, noté dans `src/core/DmaSound.hpp`. À trancher
  avant toute comparaison de WAV bit à bit.
- **Fréquences exactes centralisées** · P3 ⭘ — CPU 8 021 248 Hz PAL, VBL ≈ 50,05 Hz
  (`clocks_timings.c`) : seul impact réel, la synchro audio long terme.
- **Période d'enveloppe YM à l'écriture de R13** · P3 ⏸ matériel · [F] — `YM2149_199209.pdf` §§ 5-6 lus le 2026-11-04 : ils définissent la période et les formes, et ne disent rien de la phase du compteur à l'écriture. Tranche : une capture sur un YM réel.

### 3.5 Disquette — WD1772, DMA, ACSI

- **Horloge du WD1772 sur STE = SCLK 8,010613 MHz** · P3 ⭘ · [D] — NeoST cadence le FDC sur
  l'horloge CPU (exact sur STF, −0,13 % sur STE). Un accumulateur fractionnaire décalerait les
  rafales de ~5 cycles ; Hatari tronque par octet (`rint`, TODO chez lui). Source :
  `clocks_timings.c` (OSC U303), schéma STE.
- **Préchargement DMA en écriture** · P3 ⭘ · [F] — `dma.v:277 @ a00deb5b`, modèle comportemental :
  simple témoin. Tranche : netlist du DMA.
- **WRITE TRACK / READ TRACK sur STX** · P3 ⭘ · [D] D1/D2 — la piste réécrite est
  réinterprétée en secteurs (CRC régénéré, statut neutralisé), READ TRACK rend cette
  reconstruction : seule divergence de sévérité moyenne assumée, sans promesse de flux brut.
  Tranche : relecture sur un vrai lecteur d'une piste écrite par un jeu (sauvegarde), ou IPF.
- **ACSI : essai Idris OS** · P3 ⭘ — délai de fin de DMA repris d'Hatari pour lui ; jamais tenté.
- **WD1772, restes de l'audit** · P3 ⭘ · [B] contre [D] — marqués `⚠ [B]` dans `src/io/Fdc.cpp` : INTRQ de $D8 effaçable par toute commande, gardé car la fiche à la lettre fait diverger Super Hang-On ; $Dx annulé si une commande suit à moins de 16 µs. Deux écarts voulus protègent de vrais programmes : TR/SR écrits pendant BUSY (Delirious Demo IV) et commande remplacée pendant BUSY (Overdrive Demos).
- **Face absente d’un fichier ST** · P3 ⭘ · [F] — aucun contenu de la face omise : vierge ou formatée à l’origine ? Les cinq index avant RNF sont vérifiés sur une piste réellement sans ID (WRITE TRACK de $4E). La synthèse d’ID sur une face absente reste une convention héritée d’Hatari, à trancher par un dump ou une convention de format. → `docs/HATARI_DIVERGENCES.md`

### 3.6 Clavier — IKBD HD6301, ACIA 6850, MIDI

- **Liaison série physique** · P3 ⭘ — formats et diviseurs autres que /64 8N1, phase SCI
  sortante exacte ; ligne BREAK et erreurs de trame hors modèle.
- **Firmware IKBD, restes face à la ROM** · P3 ⭘ — `$19` scruté toutes les 1 ms (la ROM le fait à
  chaque tour) ; l'état hôte ne change qu'aux événements de l'hôte ; code 6301 custom
  (`Execute`). Les délais de `$18` sont comptés sur la ROM, pas mesurés sur un IKBD.
- **ACIA MIDI** · P3 ⭘ — source RX hôte limitée au câble 31 250 bauds 8N1 ; BREAK non simulé.

### 3.7 Mega STE — SCC Z85C30, RTC RP5C15, cache

- **SCC, restes** · P3 ⭘ — autres fonctions de WR7', FIFO SDLC, DPLL, série hôte ; phase de
  bit, erreurs RX de BREAK, X21 ; Hunt bit à bit ; parité/framing des formats discordants.
- **RTC, restes** · P3 ⭘ — sorties CLKOUT/ALARM, modes TEST ; lecture TEST/RESET à zéro selon
  MAME, sans mesure du bus flottant.
- **Matrice DD/HD × cache** · P3 ⭘ — balayage systématique si un titre l'exige.

### 3.8 TOS, GEMDOS, cartouche

- **GEMDOS : écarts communs à NeoST et Hatari face au vrai TOS** · P3 ⭘ · [D] — Fdelete/Fopen sur
  un dossier, Frename entre systèmes de fichiers, Fwrite sur un fichier ouvert en lecture,
  Dsetpath sur un masque. Échelon `rom` : rejouer sous TOS réel exécuté par NeoST.
- **Port cartouche générique** `$FA0000-$FBFFFF` · P3 ⭘ — au-delà du système GEMDOS ; `cart.c`.

---

## 4. 🕹️ Logithèque — ce qui doit tourner comme sur la machine

TOS 1.02fr sauf mention ; chemins sous `disks/st/` ou `disks/stx/`. Avant de déclarer un bug :
**vérifier la RAM, puis la ROM** ; les cas tranchés (Captain Blood, Enchanted Land, Cuddly, Rick
Dangerous II, Stardust, Spectrum 512 STE, Blood Money, Arkanoid, Wings of Death…) sont dans
[`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md) — ne pas les rouvrir.

### Bugs ouverts

| Titre | Prio | Symptôme | Piste |
|-------|------|----------|-------|
| **TOS 2.06 UK**, double faute au démarrage avec SPACE précoce | P2 ⭘ | Une VBL écrase le contexte GEMDOS sur la pile temporaire, avant le changement de pile (STE 1 Mo, graine 4). Hatari : aucun halt sur 256 graines. | Résidu localisé à la sortie d'attente 200 Hz (+64 cycles) puis au balayage mémoire (+60) ; même prise de VBL que Vroom. `tools/trace_tos206_boot.py`. Seul plantage ouvert qui ne dépende d'aucune ressource externe. |
| **Lemmings** `[cr BBC][a]` | P3 ◐ | Menu et premier niveau gardés (`lemmings_first_level`). | Attribuer des compétences et terminer un niveau. |
| **Vroom** `[cr Elite]`, intro sous TOS 2.06 | P2 ⏸ matériel | Trame rayée transitoire, 20/1 000 trames, reproduite aussi par Hatari ; la sonde GSTMCU retrouve 230/184 octets. | Mesurer latence VBL et fréquence de la séquence sur STE réel avant tout recalage CPU/GLUE (`tools/probe_vroom_intro_glue.py`). |
| **Switchblade** `[cr Replicants][t +2]` | P3 ⏸ relevé | Graine 8 débloquée par la cadence FDC/DMA ; seule la datation émulée change. | Relever la probabilité de collision sur un vrai ST. |
| **Shadow Warriors** (2Hot2Handle) | P3 ⏸ média | Version Ocean jouée sans écart (`shadow_warrior_st`) ; le disque 2Hot2Handle manque. | S'il revient, essayer la touche 1. |

Récit, traces et recettes de chacun : [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md).

### Campagnes à mener

- **Logithèque jouée** · P2 ◐ — `tools/run_game_replay.py` garde 21 parcours de compatibilité
  contre Hatari (médias originaux, changements de disquette, souris et glisser-déposer) ; les
  diagnostics Vroom et Magic Pockets sont exclus par défaut. À étendre : parties plus longues,
  musique et son, disques STE. Un script joystick tient chaque commande plus que la période
  d'interrogation du jeu. Chaque parcours ajouté élargit aussi le filet (§ 2).
  → `docs/CASE_STUDIES.md`, `docs/OPENDST.md`
- **Passe de la collection** · P3 ⭘ — 94 images en ST et en STE : A/B du 2026-11-10 identique à
  l'octet sur 185 cas sur 188 ; passe d'angle du 2026-11-04 (5 graines), seul Vroom change de
  sort. À refaire après tout changement de timing. Super Off Road attend son disque de données
  et Chessmaster plante sous TOS 1.62, chez les deux émulateurs.

---

## 5. 🪜 Harnais et preuve

40 étalons `hatari`, 13 `snapshot`, 3 `rtl` ; les 8 verdicts `buserror-*` sont les seuls au
contact du matériel, par des relevés de tiers. Le banc fx68k ajoute 43 variantes `rtl` pour
le cœur. → [`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)

### Monter l'échelle

- **Étendre les étalons `rtl`** · P2 ◐ — `rtl_low_pal`, `rtl_low_ntsc` et `rtl_med_pal` posés, ce dernier à 0 pixel. Reste ce qui bouge en cours de ligne (palette, fréquence), là où NeoST et le
  modèle peuvent vraiment diverger. Confronter aussi `ST4081S.PDF`.
- **Relevés de tiers** · P2 ⭘ — ST-STE Hardware Test (Troed), notes de Steven Tattersall,
  résultats de la suite Hatari. Un relevé entre en `hardware` s'il nomme machine et protocole.
- **Étalon Troed** · P2 ◐ — WSDETECT est en place (`tools/run_wsdetect.py`). Reste ST-STE Hardware Test, à rapatrier et calibrer.
- **Documents primaires manquants** · P2 ⭘ — HD6301, schémas GLUE/MMU ST et Mega STE,
  schéma du Shifter ST.
- **A43 — dette de preuve** · P2 ⏸ — tout commentaire cite ses références (règle : `CLAUDE.md`) ;
  les 312 citations d'Hatari portent grade et révision. MFP, YM2149, WD1772, ACIA et SCC sont
  confrontés à leurs fiches : ce qu'elles tranchaient seules est corrigé (2026-11-10), les `⚠ [B]`
  restants attendent un relevé sur le fer, ou sont gardés parce qu'un jeu contredit la fiche.
  Restent les GLUE/MMU sans schéma, et le SCC en synchrone, SDLC et DMA.
- **`union_demo` à monter en `hatari`** · P3 ⭘ — référence `snapshot`. `compare_titles` en 1 Mo sans `--fastfdc` concorde, 0,04 % au plus à 3000, 3900 et 6000. L'oracle de l'étalon, en 512 Ko, ne trouve aucune trame exacte dans ±90, avec ou sans `--fastfdc`. Tenter 1 Mo ; une égalité exacte manque peut-être à cause de 46 pixels résiduels, situés sur le scroller du bas : sur certaines lignes, quelques pixels prennent la couleur d'un cran voisin, 43 à la trame 3900. Cause non cherchée.
- **Auto-tests sans source** · P3 ◐ — `tools/classify_selftests.py` lit l'échelon que chaque test de logique déclare : hardware 0 · primary 70 · rom 6 · rtl 1 · hatari 44 · aucun 14 (total 135). Ce qui ne cite rien reste à sourcer. C'est une déclaration, pas une relecture des attendus.
- **Monter les 13 snapshots** · P3 ⭘ — `blankline_*` et `freq_switch` visent `rtl`, et leur machine GLUE est déjà
  confrontée au modèle (`docs/GLUE_SWEEP.md`), pas leurs pixels ; `closure` et
  `overscan_top` s'expliquent déjà face à Hatari (A41, définitif).

### Instruments

- **Suite de tests d'Hatari : restes** · P2 ◐ — `run_hatari_suite.py` rejoue cycles, blitter,
  bus errors, `cpu`, `gemdos` et `serial`. Reste `screen` (référence PNG au format Hatari) ; `xbios`/`mem_end` hors périmètre.
- **Banc temporel Hatari** · P2 ◐ — 20 sondes, dont écriture DMA disquette, lecture STX et
  DIVU/DIVS ; le résidu blitter/IRQ Mega STE attend la capture IACK. → `docs/HATARI_MEGASTE_AUDIT.md`
- **`screen/flixfull.prg`** · P3 ⭘ · [F] — NeoST est 4 px à gauche d'Hatari sur toute la ligne, ST comme STE. Écritures et décisions du GLUE identiques (LEFT_OFF + RIGHT_OFF, DE 5 à 463, 230 octets) :
  l'écart est dans la recopie de la ligne. Aucun témoin réel ne dit qui a raison, et le Shifter du modèle n'est pas fiable à ce grain. Tranche : une capture sur machine.
- **Raccourcir `nocooper_greetings`** · P3 ⭘ — il borne le mur du palier pixel (~45 s) ; savoir
  QUAND la démo relit le clavier avant de raccourcir.
- **A10 — étalons adossés à une ROM Atari** · P3 ◐ — **44 étalons pixel sur 56** tournent
  sans TOS Atari. Les trois `spectrum512_diapo*` restent des SKIP recensés (migration EmuTOS
  réfutée, ne pas retenter) ; leur motif est rendu par `spec512_view`. `union_demo` → ci-dessus.
- **Comparaison MAME ↔ NeoST** · P3 ⭘ — memory map, bus errors, FIFO FDC/MMU, blitter, SCC.
- **Traces Hatari de référence pour `trace_diff`** · P3 ⭘ — Arkanoid et autres.
- **Étalon spec512 en overscan** · P3 ⭘ — si une démo libre à bordures ouvertes est rapatriée.

> **Les références se bâtissent** : `extern/hatari`, `extern/fx68k`, `extern/mistery` et
> `extern/datasheets` sont gitignorés, absents d'une machine fraîche. `tools/setup_hatari.sh` et
> `tools/setup_silicon_refs.sh` les rapatrient à leur épingle.
> → [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md)

---

## 6. 🔌 Extensions — matériel réel, OFF par défaut

→ [`docs/EXTENSIONS.md`](docs/EXTENSIONS.md). Tout a existé sur ST ; rien n'agit sur les étalons.

### Réseau, stockage, ports
- **EWeb ST derrière AERA-Gateway** · P2 ⭘ — navigateur GEM libre (GPL v2) : la passerelle en
  Docker sur l'hôte, EWeb dans NeoST (STE 4 Mo, STinG + `ENEC.STX`) par le NAT, port 8280.
  Premier parcours web réel de bout en bout ; en faire un test s'il passe.
  → <https://yaary.music/software/atari-st/eweb/>
- **EtherNEC : livrer les pilotes libres GPL dans les paquets** · P2 ⭘ — STinG + `ENEC.STX`.
- **NetUSBee : validation MiNT et matériel** · P3 ⭘ — USB4TOS vérifié (HID + disque) ;
  restent FreeMiNT, capture du latch des différentes cartes, timing électrique et ITL/isochrone.
  → [`docs/NETUSBEE.md`](docs/NETUSBEE.md)
- **STinG, premier run en échec DNS** · P3 ⭘ · [F] — `err -17` au tout premier `--slirp` du 2026-11-04, puis 4 succès d'affilée, alors que le `--slirp-selftest` en ligne passait juste après. Le filtre de l'hôte reste la piste la plus simple, sans preuve. À regarder si l'échec revient, avec `NEOST_SLIRP_TRACE=1`.
- **Modem / STinG** · P3 ⭘ — documenter l'installation dans `docs/TEST_SOFTWARE.md` ; banc SLIP
  de bout en bout.
- **Dongles dans les frontends WASM/Android** · P3 ⭘ — pas de page Dongles.
- **Liste blanche de domaines** · P3 ⭘ — optionnelle, pour les backends sortants.
- **Passerelle intégrée au NAT** · P3 ⏸ décision — relais TLS côté hôte pour HTTP, IMAP, SMTP et
  FTP, aux ports des clients AERA ; les services lourds (images, scripts, PDF) restent à une
  AERA externe. Lire d'abord le protocole d'EWeb sur le fil, avec la vraie passerelle.

### Station MIDI
Cas visé : le ST mène la séance (Cubase, Notator), un Mac porte les AU/VST. La sortie est au
niveau (gigue σ 0,4-1,7 ms, `run_midi_sequencer.py`).
- **Interfaces multi-ports** · P3 ⭘ — C-Lab Unitor, Steinberg Midex (32/64 canaux, SMPTE) ; la
  clé de l'Unitor-N est émulée, pas le boîtier.
- **Splitter clavier** · P3 ⭘ — une source = un canal ; pas de zones.
- **Synchro SMPTE/MTC** · P3 ⭘ — l'horloge MIDI traverse, le code temporel de l'Unitor non.
- **Port virtuel sous Windows** · P3 ⭘ — aucune API winmm/WinRT 1.0 ; loopMIDI en contournement.
- **Appareils homonymes sous ALSA** · P3 ⭘ — indiscernables quand un seul est branché.
- **Anneau MIDI** · P3 ◐ — réception headless cadencée livrée ; restent la saisie du pair dans
  le GUI et un test à deux nœuds (un seul processus, bloqué par A33, § 9).

---

## 7. 📦 Livraison et interface

- **Identification des disquettes de menus** · P2 ⭘ — nom de fichier → équipe, numéro et jeux
  (Automation, D-Bug…) pour la ludothèque et la borne. Catalogue de 4 515 disques sous licence
  MIT dans `data/compdisks/` ; à lire par le code, puis à embarquer avec sa licence.
- **Retour arrière (rewind)** · P2 ⭘ — un `.state` par seconde en anneau, disquettes copiées si
  modifiées ; jamais pendant une commande STX minutée ; désactivé avec un disque dur. Dépend
  de l'empreinte machine (§ 5) et du sort des `.state` (§ 2). Modèle : ASE `TimeWarp.cs`.
- **Explorateur de bitmaps** · P3 ⭘ — la RAM en plans ST avec la palette courante, dans le
  débogueur. Modèle : ASE `DebugWindow`.
- **Kiosk & CRT, cosmétique** · P3 ⭘ — membres `srcW_`/`srcH_` morts dans `CrtEffectStack` ;
  tenir gauche/droite bloque la répétition haut/bas en kiosk. Limitations CRT v1 assumées.
- **A12 — validation des paquets sur machines hôtes** · P1 ⏸ matériel — macOS arm64 passé (sauf
  le pas visuel en session graphique). Restent les pas 3 à 5 pour **Raspberry Pi** (aucun
  budget temps réel mesuré), **Windows** (jamais lancé hors CI), **APK** (QEMU seul) et
  **Linux**. Ne dit rien de la fidélité. Le budget temps réel sera à remesurer après
  l'échange (§ 9). → [`docs/HW_VALIDATION.md`](docs/HW_VALIDATION.md)
- **Passe d'usage interactif du GUI** · P2 ⏸ session humaine — le GUI n'est couvert que par une
  capture au boot et trois assertions d'arguments. Même famille qu'A12.
- **Notarisation macOS** · P2 ⏸ décision (99 $/an) — `.app` scellé ad hoc ; manque Developer ID.
  Signature du `.zip` Windows écartée (token HSM, SmartScreen = un clic). ⚠ Le JIT de POM68K
  touchera au hardened runtime (`allow-jit`). → `docs/RELEASE.md`
- **Hygiène FujiNet** · P3 ⏸ décision — code retiré le 2026-08-22 ; deux mentions historiques
  (commentaire de version save-state dans `src/core/Machine.cpp`, `CHANGELOG.md`). Reformuler
  ou assumer.

---

## 8. 🧱 Dette d'architecture et de documentation

Issue de l'audit du 2026-08-27 et des revues suivantes ; le soldé est au `CHANGELOG.md`.
Ce qui touche `src/core/Cpu68k.cpp` attend l'échange → § 9.

- **A32 — `Shifter` en trois classes** · P2 ◐ — fichiers et noms faits. L'état du compteur vidéo est un membre, `VideoCounterState vc_`, sans changement de l'empreinte d'état. Prochain pas : y déplacer les méthodes, ce qui demande une interface vers la Glue et le faisceau, puis seulement la Glue. Le VIDEL du Falcon et la vidéo du TT se brancheront sur cette interface : la finir avant eux.
- **Inventaire des hypothèses ST dans le code** · P2 ⭘ *(proposé)* — ce qui suppose un bus
  24 bits, un 68000 ou une seule carte MMIO, hors du cœur : 19 masques `0xFFFFFF` dans 10
  fichiers de `src` (blitter, Shifter, son DMA, GEMDOS, FPU…), tables de bus error, types
  de machine. Une liste `fichier:ligne`, sans rien changer : elle chiffre le § 10.
- **A9 — `appLoop` découpée** · P3 ◐ — `main()` tient en 10 lignes ; les 28 phases sont des
  fonctions statiques `(App&, Frame&)`. Plus grosse pièce restante :
  `runKioskMenu` — 380 lignes d'un seul tenant. Prochain pas éventuel : sortir ses pages,
  puis `consumeConfigRequests` (219 lignes). Déplacements mécaniques uniquement.
- **A38 — tenue du registre** · P3 ⭘ — `tools/fast_guards.json` inscrit les étapes du palier
  `fast` ; consigner chaque vrai déclenchement dans le commit qui corrige le défaut.
- **A39 — modules jamais audités** · P3 ◐ — pile réseau `src/net` (comportement seulement) ; un
  test par COUCHE du bac à sable GEMDOS, à décider (couple test et implémentation).
- **`HATARI_DIVERGENCES.md` rangé par passe d'audit** · P3 ⭘ *(proposé)* — 2 675 lignes : le détail
  par sous-système, puis onze passes datées et des sections du jour en queue. Le même
  écart se lit à plusieurs endroits. Fondre les passes dans le détail par sous-système,
  les récits au `CHANGELOG.md`. Outil de garde : `tools/check_doc_anchors.py`.
- **Audit de cartographie du 2026-07-08, rangées non revues** · P3 ⭘ *(proposé)* — les tables
  « Docs à corriger » et « Prochains travaux » de `HATARI_MAPPING.md` sont archivées
  (`docs/archive/HATARI_MAPPING_AUDIT_2026-07.md`). Les candidats de code sont clos ou
  repris ici ; les corrections de doc demandées n'ont pas été recontrôlées une à une.

### Garde-fous (à NE PAS faire)
- **Croire que la liste se vide** : auditer coûte une journée, réparer des semaines. Arbitrer
  explicitement entre « ouvrir » et « fermer » à chaque session.
- **Combiner A9 + A32 (ou tout chantier structurel) en un « grand refactor »** : un à la fois,
  filet de test posé AVANT.
- **Refactorer `Cpu68k.cpp` ou patcher `extern/moira` avant l'échange** : c'est le fichier que
  l'échange réécrit et le code qu'il remplace.
- **Supprimer une branche d'exécution sur une intuition** : précédent A34, tranché par la mesure.
- **Compenser pour égaler un oracle** : ni les délais du blitter pour absorber le résidu IACK,
  ni les bords STOP_MIDDLE, ni `closure` passé en `hatari` (A41), **ni Moira ajusté pour égaler
  fx68k** : un écart du banc se tranche à une source plus haute ou reste écrit.
- **Garder un contrôle par un autre contrôle** : A38 ne se traite pas avec un 18ᵉ outil.

---

## 9. 🔁 L'échange du cœur — gelé jusque-là

Le Moira de POM68K (68000/030/040, JIT déterministe, FPU et MMU intégrés, cycle-exact en
68000) remplace `extern/moira`. **Rien ne s'intègre avant que l'auteur le déclare prêt.**
Tout ce qui suit touche l'intérieur du cœur ou son enveloppe `NeostMoira` : y travailler
maintenant, c'est travailler deux fois. Exception : un défaut qui casse un titre se corrige,
et **sa ligne s'ajoute à la table des patches** de `docs/CPU_SWAP_GATE.md`.

### Le jour de l'échange

1. Filet du § 2 en place, référence enregistrée sur le dernier commit d'avant.
2. `diff` de `extern/moira/Moira` contre le cœur POM68K en mode 68000 : chaque patch de
   `extern/moira/NEOST_VENDOR.md` s'y retrouve, ou son absence est expliquée.
3. `cpu_swap_gate.py --check`, palier `full`, `run_moira_runner.sh`, parcours de jeu,
   `run_determinism.py`. Un écart se localise par trace et se tranche au banc fx68k : plus
   proche de fx68k, c'est un progrès consigné ; sinon une régression du cœur.
4. Remesurer : budget temps réel par cible (`run_perfbench.py --budget`), profil callgrind.

### Questions du 68000, à requalifier sur le nouveau cœur

Écarts du **cœur Moira nu** contre fx68k @ `0602ee46`, avec Hatari et le microcode pour autres
témoins : tous grade D, Moira non modifié. La chaîne MFP→IPL de NeoST ajoute ses propres délais :
requalifier dans NeoST avant de conclure sur une IRQ réelle. Détail → [`docs/FX68K_BENCH.md`](docs/FX68K_BENCH.md).

- **Les 9 variantes hors accord au banc fx68k** · P1 ⏸ cœur — à rejouer telles quelles sur POM68K :
  le verdict de chacune dit si l'échange est un progrès.
- **Prise d'IPL sur ST et Mega STE** · P2 ⏸ source · [F] — le STE suit fx68k + GSTMCU pour la HBL et pour la VBL pendant un STOP, depuis le 2026-11-04 (`docs/STCPU_BENCH.md`). Hors STOP, sa VBL se prend au dispatch, car l'intro de Vroom `[cr Elite]` se fige sinon ; ST et Mega STE gardent le commit
  d'Hatari, faute de modèle du GLUE. Le 68000 est le même : à trancher par un modèle du GLUE ST, ou un relevé de tiers.
- **Latence IACK MFP** · P1 ⏸ matériel · [D] 12 cycles — capturer CLK CPU/MFP, IACK et les
  DTACK puce/CPU sur un vrai ST et un vrai Mega STE, aux trois régimes CPU/cache. Sonde prête
  (`tools/make_mfp_iack_probe.py`, contrôles verts). Le bloc IACK = 12 cycles bus (valeur
  d'Hatari, « not measured ») ; le 16 du 2026-08-06 compensait le défaut STOP+4 de Moira. Le
  banc Mega STE (`docs/HATARI_MEGASTE_AUDIT.md`) est à refaire avec 12 ; le résidu
  blitter/IRQ 16 MHz en dépend. → [protocole](docs/MFP_IACK_MEASUREMENT.md)
- **Erreur d'adresse : cycle amorcé** · P3 ⭘ · [D] — seul fx68k pose AS sans strobe à l'adresse fautive.
  Un seul témoin, sans effet connu sur un ST.
- **FPU MC68881 : silicium, et qui calcule** · P3 ⏸ cœur · [D] — bit à bit avec Hatari, transcendantes
  FPSP comprises, mais le 68881 réel calcule en CORDIC : dernier bit non garanti, « zéro »
  non normalisé après underflow à vérifier. `k` dynamique fixé à 17 (le dialogue
  périphérique ne transmet pas Dn). POM68K apporte son FPU : décider si `src/io/Fpu.cpp`
  garde son arithmétique pour le 68881 en périphérique du Mega STE, ou la lui délègue.

### Dette qui attend le nouveau cœur

- **Suivre l'amont Moira** · P3 ⏸ cœur — 10 commits non portés depuis le pin (68020/68030, vAmiga,
  empreinte mémoire) ; rien pour le 68000 hormis DIVU/DIVS, porté. Sans objet si POM68K les
  a déjà : à vérifier au `diff`. Rejouer `tools/run_moira_runner.sh` et le banc fx68k à
  chaque patch du cœur.
- **A33 — vrai parallélisme CPU** · P3 ⏸ cœur — retirer les accès à `g_cur` (≈ 215 dans
  `src/core/Cpu68k.cpp`). Rien ne le réclame, sauf l'anneau MIDI en un seul processus. À
  faire dans la réécriture de l'enveloppe, pas avant.
- **Points chauds du cœur** · P3 ⏸ cœur — `NeostMoira::sync`, le dispatch de l'ordonnanceur et
  les lectures bus pèsent 39 % du profil ; le JIT change la donne. Refaire le profil après
  l'échange avant d'y toucher. → `docs/PERFORMANCE.md` § 6

---

## 10. 🚀 Le TT et le Falcon

Après l'échange. D'ici là : des inventaires, des fiches et des oracles, aucun code de machine.

- **Puces du TT et du Falcon** · P2 ⏸ cœur — DSP56001, VIDEL, Crossbar, second MFP du TT,
  SCSI NCR5380, IDE, NVRAM MC146818 ; bus 32 bits et cartes MMIO par machine. Fiches à
  rapatrier par `tools/setup_silicon_refs.sh` ; témoins : `extern/hatari/src/falcon`.
- **Fiches et oracle, dès maintenant** · P2 ⭘ *(proposé)* — ajouter les fiches des puces
  ci-dessus à `tools/setup_silicon_refs.sh`, avec leurs empreintes ; vérifier que l'oracle
  Hatari headless démarre en TT et en Falcon avec un EmuTOS 512 Ko, et noter la recette dans
  `docs/HATARI_AUTOMATION.md`. Aucun code NeoST.
- **Terrain dans le code** → A32 et l'inventaire des hypothèses ST, § 8.
- **Suite AERA de yaary.music** · P2 ⏸ Falcon — <https://yaary.music/software/atari-st/>.
  Dix programmes GEM en C, sous GPL v2 : EWeb, EMail, ECal, EContact, EFTP, EPDF, EPlay,
  EHypView, EUpdate, ClaudeST. Sur Falcon, EWeb fait son TLS 1.2 et ses JPEG **sur le DSP**,
  et EPlay y décode le MP3 : de vrais juges du DSP56001. Sur ST, ils passent par AERA-Gateway.
