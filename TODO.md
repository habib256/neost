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

## Le cap

**Le meilleur émulateur d'Atari ST, STE et Mega STE.** Quatre choses le définissent, et ce
fichier est rangé dans cet ordre :

1. **Le silicium pour référence** (§ 2) — chaque puce se comporte comme la vraie, et chaque
   affirmation du code porte son grade de preuve ([`CLAUDE.md`](CLAUDE.md)). Hatari et MAME
   sont des témoins, jamais la cible ; une décision prise « pour égaler Hatari » est une
   question ouverte, marquée **aligné Hatari** ci-dessous.
2. **La logithèque qui tourne comme sur la machine** (§ 3) — jeux, démos, séquenceurs,
   systèmes, protections ; un titre qui diverge est un bug tant qu'il n'est pas tranché.
3. **Un harnais dont chaque verdict dit son échelon** (§ 4) — un vert `hatari` dit « d'accord
   avec Hatari », un vert `rtl` « d'accord avec un modèle du circuit », un `snapshot` « n'a
   pas bougé ». Monter l'échelle est un chantier en soi.
4. **Livrable partout, sans surprise** (§ 5-6) — extensions réelles, paquets validés,
   interface qui sert.

**État au 2026-11-03** : contrat logiciel Mega STE tenu et gardé (suite Q du diagnostic Field
Service 12/12, grade C) ; aucune divergence haute face à Hatari ; 2 étalons `rtl` ;
**banc fx68k : 34 variantes sur 43 en accord** cycle à cycle (§ 2.1) ; blitter, GLUE et lecture du faisceau confrontés aux RTL sur STE (§ 4).
**Pas d'accès à un vrai ST** : les ⏸ matériel passent par les relevés de tiers, les
documents primaires, les ROM et le RTL épinglé.

### Règles de tenue de ce fichier

- Une entrée = **titre · priorité · statut**, puis 1 à 3 lignes et un lien. Le récit
  (mesures, pièges, preuves) va au `CHANGELOG.md` ou dans la doc du sous-système.
- **Priorité** : **P1** prochain chantier · **P2** utile, à planifier · **P3** à rendement
  faible ou « si un jour ». Elle se lit à l'aune du cap : gain de grade, titres touchés,
  verdicts montés d'un échelon.
- **Statut** : ⭘ ouvert · ◐ entamé · ⏸ attend une ressource externe (matériel, média,
  schéma, décision).
- **Une question sur le silicium vit au § 2, sous sa puce**, avec son grade actuel et ce
  qui la tranche. **aligné Hatari** marque une décision dont la seule justification était
  l'égalité avec Hatari : le code ne bouge pas avant une source de rang supérieur.
- Un item clos est **supprimé dans le commit qui l'ajoute au `CHANGELOG.md`**, jamais coché
  ni barré sur place.
- Les identifiants `A`*n* (dette d'architecture) sont stables ; un trou de numérotation
  est du travail fait.

---

## 1. 🎯 Prochaines actions

| # | Action | Prio | § |
|---|--------|------|---|
| 1 | Logithèque : relire les écarts de `compare_titles.py --joy`, étendre `tests/replays/replays.json` | P1 | 3 |
| 2 | A43 : écarts `⚠ [B]` restants : tous attendent une mesure, une source ou contredisent un jeu (relu le 2026-11-10) | P2 ⏸ | 4 |
| 3 | A12 : valider les paquets sur Raspberry Pi, Windows, Linux, Android | P1 ⏸ | 6 |
| 4 | A32 : méthodes du compteur vidéo dans leur objet (un chantier à la fois) | P2 | 7 |
| 5 | Échange du cœur 68000 par le Moira de POM68K (prélude au TT et au Falcon) | P1 ⏸ cœur | 7 |

---

## 2. 🔬 Fidélité au silicium — par puce

Sources citées à leurs épingles : `tools/setup_silicon_refs.sh`. Une fiche ou un schéma se lit
avant le RTL, et un `rtl` ne tranche qu'à titre provisoire (règles →
[`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)). Les écarts de sévérité basse déjà
consignés face à Hatari restent dans [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md) ;
ici ne figure que ce qui demande une décision ou une source.

### 2.1 CPU 68000 (Moira) — ce que le banc fx68k a localisé

Écarts du **cœur Moira nu** contre fx68k @ `0602ee46`, avec Hatari et le microcode pour autres
témoins : tous grade D, Moira non modifié. La chaîne MFP→IPL de NeoST ajoute ses propres délais :
requalifier dans NeoST avant de conclure sur une IRQ réelle. Détail → [`docs/FX68K_BENCH.md`](docs/FX68K_BENCH.md).

- **Prise d'IPL sur ST et Mega STE** · P2 ⏸ source · [F] — le STE suit fx68k + GSTMCU pour la HBL et pour la VBL pendant un STOP, depuis le 2026-11-04 (`docs/STCPU_BENCH.md`). Hors STOP, sa VBL se prend au dispatch, car l'intro de Vroom `[cr Elite]` se fige sinon ; ST et Mega STE gardent le commit
  d'Hatari, faute de modèle du GLUE. Le 68000 est le même : à trancher par un modèle du GLUE ST, ou un relevé de tiers.
- **Erreur d'adresse : cycle amorcé** · P3 ⭘ · [D] — seul fx68k pose AS sans strobe à l'adresse fautive.
  Un seul témoin, sans effet connu sur un ST.
- **Latence IACK MFP** · P1 ⏸ matériel · [D] 12 cycles — capturer CLK CPU/MFP, IACK et les
  DTACK puce/CPU sur un vrai ST et un vrai Mega STE, aux trois régimes CPU/cache. Sonde prête
  (`tools/make_mfp_iack_probe.py`, contrôles verts). Le bloc IACK = 12 cycles bus (valeur
  d'Hatari, « not measured ») ; le 16 du 2026-08-06 compensait le défaut STOP+4 de Moira. Le
  banc Mega STE (`docs/HATARI_MEGASTE_AUDIT.md`) est à refaire avec 12 ; le résidu
  blitter/IRQ 16 MHz en dépend. → [protocole](docs/MFP_IACK_MEASUREMENT.md)
- **FPU MC68881 : silicium** · P3 ⏸ matériel · [D] — bit à bit avec Hatari, transcendantes
  FPSP comprises, mais le 68881 réel calcule en CORDIC : dernier bit non garanti, « zéro »
  non normalisé après underflow à vérifier. `k` dynamique fixé à 17 (le dialogue
  périphérique ne transmet pas Dn).

### 2.2 GLUE, MMU, Shifter — vidéo et bus

- **DMA disque sur le bus** · P2 ⭘ — il passe par BR/BG et ASO/DSO, un mot par 4 cycles ([B] ST-4081 feuille 8) ; les
  32 cycles de NeoST pour 8 mots concordent, par lecture. L'arbitrage autour de la rafale n'est ni simulé ni relevé.
  ST et Mega STE : aucune netlist, le STE fait référence. → `docs/CYCLE_ACCURACY.md` § 5
- **Couleur pendant BLANK** · P2 ⏸ source · [F] palette 0 — `gstshifter.v:263-265 @ 4a6db7cc` force le
  noir hors mono, et le banc le confirme (étalons `rtl_low_*` : seul écart, 2 lignes en PAL,
  13 en NTSC). Hatari l'admet (`video.c:4008-4009 @ f0736b24`). Code inchangé tant qu'une source
  plus haute manque ; passer au noir déplacerait les références `hatari` à bordure visible.
  ⚠ Le schéma 4118 est le shifter de la Panther ; aucun schéma du Shifter ST n'est disponible.
- **Bits 3, 7 et 11 de la palette relue sur ST** · P3 ⏸ matériel · [F] — NeoST les rend à 0. Hatari les tire au hasard (`video.c:5507 @ f0736b24`, « not defined », selon la dernière activité du bus), sauf si le PC est hors RAM. Pirates [cr Delight] relit ainsi sa palette, sans effet à l'image. Tranche : relire ces bits sur le fer, ou modéliser le dernier mot du bus.
- **Octet `$FF8261` relu** · P3 ⏸ source · [F] — `gstshifter.v:121 @ 4a6db7cc` rend $00 en octet bas ; NeoST garde l'alias de
  `$FF8260`, comme Hatari. Bloc « moyen » : le RTL ne l'emporte pas. Tranche : relevé sur STE.
- **Wakestate WS3 par défaut** · P2 ⭘ · aligné Hatari — le modèle des quatre états est confirmé par le détecteur de Troed (`tools/run_wsdetect.py`, 162, 54, 206, 204 octets, et 180 sur STE). Le choix de WS3 comme défaut reste sans source : sur le fer, l'état se tire à la mise sous tension. Tranche : une statistique de relevés de tiers, ou un tirage au reset en option.
- **Offsets de datation dérivés d'Hatari** · P2 ◐ — la lecture du compteur est confirmée sur STE (47 999 lectures sur 48 000, `docs/STCPU_BENCH.md`). `kVideoCounterReadOffsetCyc`,
  `kSyncWriteOffsetCyc`, `kSpec512AlignCyc` : fidèles à `Video_CalculateAddress`, donc [D].
  `$FF820A` : le modèle réagit à AS + 5, NeoST et Hatari datent à AS + 8. Avancer casse de vraies démos STE, donc non retenu,
  et la palette, contredite de même par 13 images réelles, va dans ce sens : le modèle prend les écritures trop tôt (`docs/REFERENCE_LADDER.md`). ⚠ read et write bougent par paire.
- **Bord de fin STOP_MIDDLE et signaux HSYNC/VDE** · P3 ⭘ · [F] — la fin de DE à 164 et les 54 octets concordent avec le modèle (`docs/GLUE_SWEEP.md`) ; Hatari rend trois pixels de plus, et le HSYNC parasite vers le cycle 28 n'est pas modélisé. Sondes `blankline_stop_st/ste` en snapshot.
- **Restes vidéo au cycle** · P2 ⭘ — `VIDEO_ENDLINE` distinct du HBL, VBL au cycle exact
  (vraie fin de trame + offset 64/68), phase exacte du tic Timer C, lecture du compteur à
  cheval sur deux lignes. Étalons : démos plein écran. → `docs/CYCLE_ACCURACY.md` § 4
- **Délai de bus error sur ST, Mega ST et Mega STE** · P2 ⭘ · [F] — porté sur STE seulement,
  faute de netlist du GLUE. Chercher le schéma du GLUE sur chzsoft.de.
- **Basse résolution PAL issue du reset** · P2 ⭘ · [F] — le modèle GSTMCU garde une phase de
  plans décalée jusqu'à un passage par 60 Hz. Artefact du modèle ou effet réel ? Relevé de tiers.
- **STE, impulsions restantes face au modèle** · P3 ⭘ · [D] — DE précoce à 44, 14 impulsions sur 2560 en écart à 50 Hz, 42 sur 2560 à 60 Hz, toutes en haute résolution, et les
  largeurs au-delà de 32 qui enjambent HSYNC. Hors logiciel connu. → `docs/GLUE_SWEEP.md`
- **Écritures en plein DE sur STE** · P3 ⭘ · [D] — `$FF8207` et `$FF8265` agissent aussitôt sur le modèle, à la ligne suivante chez NeoST et Hatari. Aucun logiciel connu n'en dépend. Porter `$FF8207` demande un rendu en deux segments par ligne.
- **Étalons sensibles à la phase E-clock** · P3 ⭘ — `blankline_*`, `spec512_*` capturent une
  trame dont la phase tourne à chaque changement de timing du boot ; les ancrer dans leur
  programme (compter les VBL, ou phase fixée par `stop` + attente).
- **Vidéo — mesures sur Atari** · P2 ⏸ matériel · [F] — comportement électrique de
  NO_SYNC/SYNC_HIGH et confirmation au fer de tout ce § 2.2.
- **Shifter arrêté (res 3 / `$FF8261`)** · P3 ⏸ matériel · [F] — porté sans logiciel réel qui
  l'exerce : noir franc ou couleur 0, comportement sur STE, persistance du décalage.
- **Décodage Mega STE** · P3 ⏸ schéma · [D] — le STE suit la netlist 4081 ; le Mega STE reste au
  modèle Hatari faute de netlist.

### 2.3 MFP 68901

- **Jitter des périodes Timer B** · P2 ⭘ · [F] — sonde Super Hang-On : NeoST {512, 516, 508,
  520}, Hatari {512, 520, 504}. Dépend de la prise d'IPL (§ 2.1).
- **Lecture du dernier cycle fractionnaire d'un timer** · P3 ⭘ · [F] — la conversion entière peut rendre 0 juste avant l'échéance interne ; forcer 1 change `mfp_poll` de 16 pixels. Lecture conservée, datation DS/horloge à isoler par une sonde ; distinct de l'arrêt du compteur corrigé selon la fiche le 2026-11-10. → `docs/HATARI_DIVERGENCES.md`
- **USART** · P3 ⭘ — modèle synchrone, réacquisition après réactivation au milieu d'un
  caractère, phase du premier front TC, forme d'onde BREAK physique.
- **Fiche contre code, restes de l'audit** · P2 ⭘ · [B] contre [D] — marqués `⚠` dans `src/io/Mfp.cpp`. TxDR relu à l'accès, et non au dernier front de DS (§ 5.2.1) ; effacement de UE en un bit au lieu d'un cycle de TC ; prescaler changé en marche (§ 5.1.1).
- **Citations MAME sans révision** · P3 ⭘ · [D] — dans le MFP, l'ACIA, le YM et le FDC. Aucune copie de MAME dans `extern` : à épingler ou à retirer.

### 2.4 Blitter

Accord au cycle avec `stBlitter.sv` et fx68k sur 78 programmes, ST à 8 MHz → `docs/STBLITTER_BENCH.md`.

- **Résidu blitter/IRQ Mega STE 16 MHz** · P2 ⏸ IACK — comparaison temporelle exacte ; deux
  défauts Hatari isolés, 12-24 cycles résiduels après corrections expérimentales. Attend la
  latence IACK (§ 2.1). → `docs/HATARI_MEGASTE_AUDIT.md`
- **DMA externe prioritaire sur le blitter en HOG** · P3 ⭘ · [F] — netlist 4082, DMARQ,
  confiance basse.

### 2.5 Son — YM2149, DMA STE, Microwire

- **Période d'enveloppe YM à l'écriture de R13** · P3 ⏸ matériel · [F] — `YM2149_199209.pdf` §§ 5-6 lus le 2026-11-04 : ils définissent la période et les formes, et ne disent rien de la phase du compteur à l'écriture. Tranche : une capture sur un YM réel.
- **IRQ de fin de trame du son DMA, datée au HBL** · P3 ⭘ · [D] — le fetch suit désormais la fin de DE à la lecture du compteur (`docs/STCPU_BENCH.md`), mais la fin de trame n'a pas son événement propre : au plus 136 cycles de retard. Profondeur réelle de la FIFO : 8 octets d'Hatari contre 7 mots du modèle, choix d'auteur. Tranche : relevé sur STE.
- **Fréquences exactes centralisées** · P3 ⭘ — CPU 8 021 248 Hz PAL, VBL ≈ 50,05 Hz
  (`clocks_timings.c`) : seul impact réel, la synchro audio long terme.

### 2.6 Disquette — WD1772, DMA, ACSI

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

### 2.7 Clavier — IKBD HD6301, ACIA 6850, MIDI

- **Liaison série physique** · P3 ⭘ — formats et diviseurs autres que /64 8N1, phase SCI
  sortante exacte ; ligne BREAK et erreurs de trame hors modèle.
- **Firmware IKBD, restes face à la ROM** · P3 ⭘ — `$19` scruté toutes les 1 ms (la ROM le fait à
  chaque tour) ; l'état hôte ne change qu'aux événements de l'hôte ; code 6301 custom
  (`Execute`). Les délais de `$18` sont comptés sur la ROM, pas mesurés sur un IKBD.
- **ACIA MIDI** · P3 ⭘ — source RX hôte limitée au câble 31 250 bauds 8N1 ; BREAK non simulé.

### 2.8 Mega STE — SCC Z85C30, RTC RP5C15, cache

- **SCC, restes** · P3 ⭘ — autres fonctions de WR7', FIFO SDLC, DPLL, série hôte ; phase de
  bit, erreurs RX de BREAK, X21 ; Hunt bit à bit ; parité/framing des formats discordants.
- **RTC, restes** · P3 ⭘ — sorties CLKOUT/ALARM, modes TEST ; lecture TEST/RESET à zéro selon
  MAME, sans mesure du bus flottant.
- **Matrice DD/HD × cache** · P3 ⭘ — balayage systématique si un titre l'exige.

### 2.9 TOS, GEMDOS, cartouche

- **GEMDOS : écarts communs à NeoST et Hatari face au vrai TOS** · P3 ⭘ · [D] — Fdelete/Fopen sur
  un dossier, Frename entre systèmes de fichiers, Fwrite sur un fichier ouvert en lecture,
  Dsetpath sur un masque. Échelon `rom` : rejouer sous TOS réel exécuté par NeoST.
- **Port cartouche générique** `$FA0000-$FBFFFF` · P3 ⭘ — au-delà du système GEMDOS ; `cart.c`.

**Hors périmètre** : SCSI / NCR5380 — TT et Falcon seulement.

---

## 3. 🕹️ Logithèque — ce qui doit tourner comme sur la machine

TOS 1.02fr sauf mention ; chemins sous `disks/st/` ou `disks/stx/`. Avant de déclarer un bug :
**vérifier la RAM, puis la ROM** ; les cas tranchés (Captain Blood, Enchanted Land, Cuddly, Rick
Dangerous II, Stardust, Spectrum 512 STE, Blood Money, Arkanoid, Wings of Death…) sont dans
[`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md) — ne pas les rouvrir.

### Bugs ouverts

| Titre | Prio | Symptôme | Piste |
|-------|------|----------|-------|
| **Vroom** `[cr Elite]`, intro sous TOS 2.06 | P2 attente mesure matérielle | Trame rayée transitoire reproduite aussi dans Hatari : 20/1 000 trames sans entrée, retour 50 Hz à 34:40, compteur avancé de deux octets et retrait droit manqué. La sonde GSTMCU et NeoST retrouve 230/184 octets. | Mesurer la latence VBL et la fréquence de cette séquence sur STE réel avant tout recalage CPU/GLUE. `tools/probe_vroom_intro_glue.py`, traces et limites dans `docs/CASE_STUDIES.md`. Le Game Over à zéro point provient des données du média et plante aussi dans Hatari ; aucun correctif de l’image livré. Course courte et tour complet sous 2.06 conservés. |
| **Lemmings** `[cr BBC][a]`, couverture d'une partie | P3 à compléter | Faux échec du menu résolu par traces : entrée `$4370` à 2615 chez NeoST, 3159 chez Hatari ; alignement des captures +545, seuil de 0,5 % et fenêtre ±30 conservés. Le rejeu du menu passe. | Entrée souris et évolution du premier niveau gardées par `lemmings_first_level` (2026-11-10). Reste à attribuer les compétences et terminer le niveau ; le menu animé seul ne valide pas une partie. |
| **TOS 2.06 UK**, double faute au démarrage avec SPACE précoce | P2 ouvert | Pile temporaire dans la basepage : une VBL écrase le contexte GEMDOS avant le changement de pile. STE 1 Mo, `diskA.st`, graine 4, SPACE aux trames 100 et 300. Le défaut persiste après les corrections FDC et la lecture du Timer B événementiel. Les traces depuis le reset ont permis cette dernière correction : 25 491 PC concordants jusqu’à $E000A8, puis collision AUTO toujours reproduite. Hatari sans patch TOS : aucun halt sur 256 graines. Preuve ROM de grade C, datation matérielle encore non établie (`docs/CASE_STUDIES.md`). | Poursuivre la chronologie avant AUTO avec tools/trace_tos206_boot.py et les sondes de CASE_STUDIES.md : phases disque alignées et dérive VBL du témoin neutralisée, le résidu est localisé à la sortie d’attente 200 Hz (+64 cycles), puis au balayage mémoire (+60). Armement Timer C mesuré : NeoST précède le témoin de 88 cycles dès le démarrage du timer. La sonde run_vbl_entry_probe.py révèle 3 PC interrompus différents sur 19 ; le préarmement hors STOP les corrige mais laisse le halt TOS et bloque Vroom Elite. Résoudre les deux cas ensemble. Audit du témoin du 2026-11-05 : artefact de reset asynchrone corrigé dans le harnais, sans effet sur les 3 écarts du témoin courant ; phase E-clock sensible, fonctionnement matériel des jeux prioritaire. Ne pas porter la dérive d’origine VBL d’Hatari. Un relevé STE réel reste nécessaire pour valider la fréquence matérielle de la collision. |
| **Switchblade** `[cr Replicants][t +2]`, fréquence matérielle des collisions | P3 ⏸ relevé de tiers | Graine 8 débloquée sans patch logiciel : correction de cadence FDC/DMA, 4096 cycles par bloc au lieu de 4128. Le chargeur laisse toujours `flock` nul ; seule la datation émulée change. Rejeu de non-régression passé à la graine 8 (`docs/CASE_STUDIES.md`). | Relever ce disque sur un vrai ST : probabilité de collision et dates de fin READ TRACK non confirmées. |
| **Shadow Warriors** (2Hot2Handle) | P3 ⏸ média | Après SPACE, titre et musique OK, le bouton joystick ne lance pas le jeu (Castle Warrior fonctionne). Sur la version Ocean, ce n'est pas un défaut : le titre attend la touche 1 (« PUSH 1P OR 2P BUTTON »), et le tir ne lance rien non plus sous Hatari. Partie jouée, disques 1 et 2, sans écart avant le combat (`shadow_warrior_st`). | Le disque 2Hot2Handle reste absent. S'il revient, essayer d'abord la touche 1. |

### Campagnes à mener

- **Passe logithèque** · P2 ◐ — tous les disques ont démarré sous NeoST et Hatari le 2026-11-03 : aucun
  plantage propre à NeoST ; écrans d'intro ou titres seulement, sans partie jouée. À étendre :
  disquettes STX à lancer, parties pilotées (`docs/OPENDST.md`), musique et son, disques STE.

- **Logithèque STE de référence** · P1 ◐ — `tools/compare_titles.py` compare NeoST à Hatari sur `disks/st/demos`. E605,
  Obsession, Pacemaker, Just Musix 2, More or Less Zero, Double Rez Trouble, Sea of Colour, Drone et Cernit Trandafir
  s'affichent correctement aux écrans vus. `--key`, `--swap` et `--joy` permettent d'entrer dans les titres et d'y jouer ; `tools/run_game_replay.py` garde 21 parcours de compatibilité sur médias originaux, changements de disquette compris ; les trois diagnostics Vroom, eux aussi sur les originaux, sont exclus par défaut. Halt CPU, capture manquante et altération des médias sources invalident le verdict. Passe STX du 2026-11-04 : 11 disques, 10 à 0,0 % aux écrans vus sur ST, disques B compris pour 5 titres. Sur STE, 8 à 0,0 % ; Lethal Xcess et Golden Axe concordent depuis le 2026-11-10 et sont gardés en partie (`lethal_xcess_ste`, `golden_axe_ste` ; départ des timers MFP sur leur horloge, `docs/CASE_STUDIES.md`) ; Magic Pockets au retour à gauche (diagnostic sensible à deux trames d’alignement des entrées, voir `docs/CASE_STUDIES.md`) ; Super Off Road reste à sa demande de disque de données chez les deux émulateurs, Bubble Ghost et Chessmaster se lancent par `--auto A:\GHOST.PRG` et `--auto A:\CMASTER.PRG`, sous TOS 1.04 ou plus, sans toucher à l'image, à 0,0 % sur ST jusqu'aux écrans vus. Ils se jouent à la souris ; `compare_titles` sait désormais rejouer ces entrées, le parcours interactif de Bubble Ghost est maintenant gardé (première salle, non terminée), celui de Chessmaster reste à ajouter. Chessmaster plante sous TOS 1.62 chez les deux émulateurs (`docs/CASE_STUDIES.md`). Un script joystick doit tenir chaque commande plus que la période d'interrogation du jeu. Passe d'angle du 2026-11-04, 89 disques sur ST et sur STE, 5 valeurs de `--fdc-seed` et SPACE répété, jusqu'à la trame 4000 : un seul titre change de sort selon la graine, Vroom, ouvert ci-dessus. Les six arrêts CPU touchent cinq disques STE lancés sur ST et Epic disque 2, à toutes les graines, et l'arrêt d'Epic existe aussi sous Hatari.

## 4. 🪜 Harnais et preuve

40 étalons `hatari`, 13 `snapshot`, 3 `rtl` ; les 8 verdicts `buserror-*` sont les seuls au
contact du matériel, par des relevés de tiers. Le banc fx68k ajoute 43 variantes `rtl` pour
le cœur. → [`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)

### Monter l'échelle

- **A43 — dette de preuve** · P1 ◐ — tout commentaire cite ses références (règle : `CLAUDE.md`). Les 312 citations d'Hatari par numéro de ligne portent grade et révision, relues à l'épingle : `tools/audit_citations.py list` n'en rend plus aucune à relire. Le Blitter nomme sa netlist ou sa ligne où il citait déjà une source. MFP, YM2149, WD1772, ACIA et SCC sont confrontés à leurs fiches, et leurs écarts marqués `⚠ [B]` sur place. Le 2026-11-10, tous ceux que la fiche suffisait à trancher sont corrigés (MFP arrêt, AER, élection ; ACIA TDRE, reset MIDI ; SCC All Sent) ; les restants attendent un relevé sur le fer ou sont gardés parce qu'un jeu contredit la fiche. `audit_citations.py list` signale encore 2 citations, justes, dont la révision suit sur la même phrase. Restent les GLUE/MMU sans schéma. Le SCC n'a pas été relu pour le synchrone, le SDLC et le DMA.
- **`union_demo` à monter en `hatari`** · P3 ⭘ — référence `snapshot`. `compare_titles` en 1 Mo sans `--fastfdc` concorde, 0,04 % au plus à 3000, 3900 et 6000. L'oracle de l'étalon, en 512 Ko, ne trouve aucune trame exacte dans ±90, avec ou sans `--fastfdc`. Tenter 1 Mo ; une égalité exacte manque peut-être à cause de 46 pixels résiduels, situés sur le scroller du bas : sur certaines lignes, quelques pixels prennent la couleur d'un cran voisin, 43 à la trame 3900. Cause non cherchée.
- **Banc fx68k, étendre** · P3 ◐ — tous les modes d'adressage, MOVE, ALU, MOVEM, sauts, RESET, RTR, traps et ligne A/F en utilisateur concordent. Restent BERR et double faute, et l'autovecteur dans le verdict. → `docs/FX68K_BENCH.md` § *Ce qui manque*
- **Banc du blitter, étendre** · P3 ⭘ — Mega STE à 16 MHz, disputes de bus avec le DMA, blit sur ses propres registres.
  78 programmes en accord ; `blitter_timer` et deux sondes s'écartent d'Hatari, consigné. → `docs/STBLITTER_BENCH.md`
- **Étendre les étalons `rtl`** · P2 ◐ — `rtl_low_pal`, `rtl_low_ntsc` et `rtl_med_pal` posés, ce dernier à 0 pixel. Reste ce qui bouge en cours de ligne (palette, fréquence), là où NeoST et le
  modèle peuvent vraiment diverger. Confronter aussi `ST4081S.PDF`.
- **Relevés de tiers** · P2 ⭘ — ST-STE Hardware Test (Troed), notes de Steven Tattersall,
  résultats de la suite Hatari. Un relevé entre en `hardware` s'il nomme machine et protocole.
- **Étalon Troed** · P2 ◐ — WSDETECT est en place (`tools/run_wsdetect.py`). Reste ST-STE Hardware Test, à rapatrier et calibrer.
- **Documents primaires manquants** · P2 ⭘ — HD6301, schémas GLUE/MMU ST et Mega STE,
  schéma du Shifter ST.
- **Auto-tests sans source** · P3 ◐ — `tools/classify_selftests.py` lit l'échelon que chaque test de logique déclare : hardware 0 · primary 70 · rom 6 · rtl 1 · hatari 44 · aucun 14 (total 135). Ce qui ne cite rien reste à sourcer. C'est une déclaration, pas une relecture des attendus.
- **Monter les 13 snapshots** · P3 ⭘ — `blankline_*` et `freq_switch` visent `rtl`, et leur machine GLUE est déjà
  confrontée au modèle (`docs/GLUE_SWEEP.md`), pas leurs pixels ; `closure` et
  `overscan_top` s'expliquent déjà face à Hatari (A41, définitif).

### Instruments

- **Suite de tests d'Hatari : restes** · P2 ◐ — `run_hatari_suite.py` rejoue cycles, blitter,
  bus errors, `cpu`, `gemdos` et `serial`. Reste `screen` (référence PNG au format Hatari) ; `xbios`/`mem_end` hors périmètre.
- **`screen/flixfull.prg`** · P3 ⭘ · [F] — NeoST est 4 px à gauche d'Hatari sur toute la ligne, ST comme STE. Écritures et décisions du GLUE identiques (LEFT_OFF + RIGHT_OFF, DE 5 à 463, 230 octets) :
  l'écart est dans la recopie de la ligne. Aucun témoin réel ne dit qui a raison, et le Shifter du modèle n'est pas fiable à ce grain. Tranche : une capture sur machine.
- **Raccourcir `nocooper_greetings`** · P3 ⭘ — il borne le mur du palier pixel (~45 s) ; savoir
  QUAND la démo relit le clavier avant de raccourcir.
- **A10 — étalons adossés à une ROM Atari** · P3 ◐ — **44 étalons pixel sur 56** tournent
  sans TOS Atari. Les trois `spectrum512_diapo*` restent des SKIP recensés (migration EmuTOS
  réfutée, ne pas retenter) ; leur motif est rendu par `spec512_view`. `union_demo` → § 3.
- **Banc temporel Hatari** · P2 ◐ — 20 sondes, dont écriture DMA disquette, lecture STX et
  DIVU/DIVS ; le résidu blitter/IRQ Mega STE attend la capture IACK. → `docs/HATARI_MEGASTE_AUDIT.md`
- **Comparaison MAME ↔ NeoST** · P3 ⭘ — memory map, bus errors, FIFO FDC/MMU, blitter, SCC.
- **Traces Hatari de référence pour `trace_diff`** · P3 ⭘ — Arkanoid et autres.
- **Étalon spec512 en overscan** · P3 ⭘ — si une démo libre à bordures ouvertes est rapatriée.

> **Les références se bâtissent** : `extern/hatari`, `extern/fx68k`, `extern/mistery` et
> `extern/datasheets` sont gitignorés, absents d'une machine fraîche. `tools/setup_hatari.sh` et
> `tools/setup_silicon_refs.sh` les rapatrient à leur épingle.
> → [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md)

---

## 5. 🔌 Extensions — matériel réel, OFF par défaut

→ [`docs/EXTENSIONS.md`](docs/EXTENSIONS.md). Tout a existé sur ST ; rien n'agit sur les étalons.

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
  le GUI et un test à deux nœuds (un seul processus, bloqué par A33).

### Réseau, stockage, ports
- **EtherNEC : livrer les pilotes libres GPL dans les paquets** · P2 ⭘ — STinG + `ENEC.STX`.
- **NetUSBee : validation MiNT et matériel** · P3 ⭘ — USB4TOS vérifié (HID + disque) ;
  restent FreeMiNT, capture du latch des différentes cartes, timing électrique et ITL/isochrone.
- **STinG, premier run en échec DNS** · P3 ⭘ · [F] — `err -17` au tout premier `--slirp` du 2026-11-04, puis 4 succès d'affilée, alors que le `--slirp-selftest` en ligne passait juste après. Le filtre de l'hôte reste la piste la plus simple, sans preuve. À regarder si l'échec revient, avec `NEOST_SLIRP_TRACE=1`.
- **Modem / STinG** · P3 ⭘ — documenter l'installation dans `docs/TEST_SOFTWARE.md` ; banc SLIP
  de bout en bout.
- **Dongles dans les frontends WASM/Android** · P3 ⭘ — pas de page Dongles.
- **Liste blanche de domaines** · P3 ⭘ — optionnelle, pour les backends sortants.

---

## 6. 📦 Livraison et interface

- **A12 — validation des paquets sur machines hôtes** · P1 ⏸ matériel — macOS arm64 passé (sauf
  le pas visuel en session graphique). Restent les pas 3 à 5 pour **Raspberry Pi** (aucun
  budget temps réel mesuré), **Windows** (jamais lancé hors CI), **APK** (QEMU seul) et
  **Linux**. Ne dit rien de la fidélité. → [`docs/HW_VALIDATION.md`](docs/HW_VALIDATION.md)
- **Passe d'usage interactif du GUI** · P2 ⏸ session humaine — le GUI n'est couvert que par une
  capture au boot et trois assertions d'arguments. Même famille qu'A12.
- **Notarisation macOS** · P2 ⏸ décision (99 $/an) — `.app` scellé ad hoc ; manque Developer ID.
  Signature du `.zip` Windows écartée (token HSM, SmartScreen = un clic). → `docs/RELEASE.md`
- **Retour arrière (rewind)** · P2 ⭘ — un `.state` par seconde en anneau, disquettes copiées si
  modifiées ; jamais pendant une commande STX minutée ; désactivé avec un disque dur. Dépend
  de l'empreinte machine (§ 4). Modèle : ASE `TimeWarp.cs`.
- **Explorateur de bitmaps** · P3 ⭘ — la RAM en plans ST avec la palette courante, dans le
  débogueur. Modèle : ASE `DebugWindow`.
- **Identification des disquettes de menus** · P3 ⭘ — nom de fichier → jeu (Automation,
  D-Bug…) pour la ludothèque kiosk ; catalogue ASE `GameMenus.json` (licence à vérifier).
- **Kiosk & CRT, cosmétique** · P3 ⭘ — membres `srcW_`/`srcH_` morts dans `CrtEffectStack` ;
  tenir gauche/droite bloque la répétition haut/bas en kiosk. Limitations CRT v1 assumées.
- **Hygiène FujiNet** · P3 ⏸ décision — code retiré le 2026-08-22 ; deux mentions historiques
  (commentaire de version save-state dans `src/core/Machine.cpp`, `CHANGELOG.md`). Reformuler
  ou assumer.

---

## 7. 🧱 Dette d'architecture

Issue de l'audit du 2026-08-27 et des revues suivantes ; le soldé est au `CHANGELOG.md`.

- **A32 — `Shifter` en trois classes** · P2 ◐ — fichiers et noms faits. L'état du compteur vidéo est un membre, `VideoCounterState vc_`, sans changement de l'empreinte d'état. Prochain pas : y déplacer les méthodes, ce qui demande une interface vers la Glue et le faisceau, puis seulement la Glue.
- **A9 — `appLoop` découpée** · P3 ◐ — `main()` tient en 10 lignes ; les 28 phases sont des
  fonctions statiques `(App&, Frame&)`. Plus grosse pièce restante :
  `runKioskMenu` — 380 lignes d'un seul tenant. Prochain pas éventuel : sortir ses pages,
  puis `consumeConfigRequests` (219 lignes). Déplacements mécaniques uniquement.
- **Échange du cœur : Moira de POM68K** · P1 ⏸ cœur POM68K — 68000/030/040, JIT déterministe, FPU et MMU intégrés ; cycle-exact en 68000. Porte prête (`tools/cpu_swap_gate.py`) : enregistrer la référence sur le dernier commit avant l'échange. Contrat attendu du cœur → [`docs/CPU_SWAP_GATE.md`](docs/CPU_SWAP_GATE.md)
- **Suivre l'amont Moira** · P3 ⭘ — 10 commits non portés depuis le pin (68020/68030, vAmiga,
  empreinte mémoire) ; rien pour le 68000 hormis DIVU/DIVS, porté. Rejouer
  `tools/run_moira_runner.sh` et le banc fx68k à chaque patch du cœur.
- **A38 — tenue du registre** · P3 ⭘ — `tools/fast_guards.json` inscrit les étapes du palier
  `fast` ; consigner chaque vrai déclenchement dans le commit qui corrige le défaut.
- **A39 — modules jamais audités** · P3 ◐ — pile réseau `src/net` (comportement seulement) ; un
  test par COUCHE du bac à sable GEMDOS, à décider (couple test et implémentation).
- **A33 — vrai parallélisme CPU** · P3 ◐ — retirer les accès à `g_cur` (≈ 215 dans
  `src/core/Cpu68k.cpp`). Rien ne le réclame, sauf l'anneau MIDI en un seul processus.
- **A15 — DSL d'injection** · P3 ⭘ — pas de token « bouton tenu pendant un mouvement ».

### Garde-fous (à NE PAS faire)
- **Croire que la liste se vide** : auditer coûte une journée, réparer des semaines. Arbitrer
  explicitement entre « ouvrir » et « fermer » à chaque session.
- **Combiner A9 + A32 (ou tout chantier structurel) en un « grand refactor »** : un à la fois,
  filet de test posé AVANT.
- **Supprimer une branche d'exécution sur une intuition** : précédent A34, tranché par la mesure.
- **Compenser pour égaler un oracle** : ni les délais du blitter pour absorber le résidu IACK,
  ni les bords STOP_MIDDLE, ni `closure` passé en `hatari` (A41), **ni Moira ajusté pour égaler
  fx68k** : un écart du banc se tranche à une source plus haute ou reste écrit.
- **Garder un contrôle par un autre contrôle** : A38 ne se traite pas avec un 18ᵉ outil.

---

## 8. 📚 Logiciels réservés au TT et au Falcon

- **Suite AERA de yaary.music** · P2 ⏸ Falcon dans NeoST — <https://yaary.music/software/atari-st/>.
  Dix programmes GEM en C, sous GPL v2 (« ou ultérieure » sur la page d'EWeb) : EWeb (navigateur), EMail, ECal, EContact, EFTP, EPDF, EPlay (MP3), EHypView,
  EUpdate, ClaudeST. Sur Falcon et TT, EWeb fait son TLS 1.2 et décode ses JPEG **sur le DSP**,
  EPlay lit le MP3 sur le DSP : d'excellents juges du DSP56001 et de la pile réseau. Sur ST, ils
  passent par AERA-Gateway (<https://yaary.music/software/aera-gateway/>). À tester quand le
  Falcon sera émulé (cœur POM68K, `docs/CPU_SWAP_GATE.md`).
