# Cas tranchés — diagnostics de terrain

(c) 2026 VERHILLE Arnaud. Les titres dont le comportement est **expliqué**, avec la
recette pour le reproduire. Deux familles s'y mêlent, et la distinction est le principal
intérêt du document :

- ✅ **corrigé** — c'était un bug NeoST, il ne l'est plus ;
- ✅ **FIDÈLE** — ce n'était pas un bug : Hatari et/ou le vrai matériel font pareil. Ces
  cas-là sont les plus coûteux à re-diagnostiquer, d'où leur place ici plutôt qu'à la
  corbeille.

Ce qui reste OUVERT est dans [`../TODO.md`](../TODO.md).

| Jeu | Verdict | Piste / renvoi |
|-----|---------|----------------|
| **Systematic Error (Imagina, 1993)**, STE : masque Microwire écrit pendant un décalage (2026-11-11) | ✅ **Contrainte établie, règle non tranchée.** La boucle d'init `$F1AC` écrit `$FF8924` 28 cycles après la donnée précédente, en plein décalage, puis attend que le masque relise `$7FF`. La démo tourne sur le fer : une puce qui prendrait le masque sans arrêter ni relancer le transfert la bloquerait (masque tourné de 13 crans). Ignorer (Hatari, NeoST) et arrêter (MiSTery) la laissent tourner tous deux ; seule la commande interrompue `$048C` (aigus +12 dB) diffère : appliquée chez Hatari et NeoST, perdue chez MiSTery. | [Recette et contre-épreuve](#systematic-error--le-masque-microwire-écrit-en-plein-décalage-2026-11-11). |
| **Vroom**, écran bruité sous TOS 2.06 (2026-11-04) | **Cause isolée : la VBL du jeu désélectionne le lecteur pendant le chargement.** Erreur ignorée, tampon périmé copié sur l’écran. Un octet neutralisant l’arrêt automatique fait passer le même état. Cœur inchangé, fréquence réelle inconnue. | [Recette et contre-épreuve](#vroom-sous-tos-206--la-vbl-désélectionne-le-lecteur-pendant-le-chargement-2026-11-04). |
| **TOS 2.06 UK**, double faute au démarrage (2026-11-04, jugé le 2026-11-10) | **Course réelle de la ROM ; fréquence due au tirage discret (2026-11-10).** NeoST 3/256, Hatari 0/1024 : une seule trajectoire de boot touche une fenêtre de 16 cycles, le réseau des trajectoires étant figé par le rapport exact des horloges MFP/CPU. Mesuré le 2026-11-12 : la phase MFP tirée au reset n'y change rien (3/256 sur 7 phases), un autre quartz si (0/256 à 32,0424 et 32,215905 MHz). **Cause démontrée dans NeoST : collision de pile et de contexte GEMDOS.** Reproduite avec `diskA.st`, sans Vroom ; masquer la fenêtre de changement de pile débloque le même état. Cœur inchangé, datation matérielle non établie. | [Trace et recette publique](#tos-206--la-vbl-écrase-le-contexte-sur-la-pile-temporaire-2026-11-04). |
| **TOS 2.05 FR**, écran uniforme avec le profil ST/Mega ST (2026-11-04) | **Corrigé par le choix du profil compatible, sans patch TOS.** `$E00096` accède au son DMA STE avant l'installation des vecteurs. Le profil ST partait ensuite en RAM vide. NeoST sélectionne désormais STE ; STE/Mega STE déjà choisis sont conservés. Hatari contourne cette incompatibilité par les patchs `TP_ANTI_STE`. | `tos205fr.img`, ST demandé, 1 Mo, `diskA.st`, SPACE 100/300 tenu 8 trames, capture 2000 : bureau, 0 pixel d'écart avec STE explicite et Hatari STE sans patch. Audit détaillé dans `HATARI_AUTOMATION.md`, section « Patchs TOS ». |
| **Switchblade** `[cr Replicants][t +2]`, écran noir rare (2026-11-04) | **Graine 8 débloquée par la correction de cadence FDC/DMA.** Le flux disque ne ralentit plus pendant le DMA RAM. Le chargeur laisse toujours `flock` nul ; aucun patch logiciel. Fréquence réelle des collisions inconnue. | [Trace, contre-épreuve et recette](#switchblade--une-vbl-du-tos-acquitte-le-read-track-du-chargeur-2026-11-04). |
| **Barbarian (Psygnosis, 1987)** — souris ET joystick actifs ensemble (2026-10-22) | ✅ **FIDÈLE, par la ROM 6301 et non plus par un hack.** Le jeu envoie `$80 $01 $08 $0B 03 03 $14` d'un seul trait : tout arrive pendant l'initialisation de la ROM (sei `$F010` … cli `$F139`). `$08` reste dans le RDR, l'octet suivant déborde et la routine `$FF40` jette tout. Il reste les défauts : souris relative (seuil 1) + joystick 1 en événement. Hatari obtenait le même résultat par un quirk « `$08+$14` pendant le reset », qui laissait en plus passer le joystick 0. Vérifié : joystick 1 → le barbare avance, souris → le pointeur bouge, depuis le même état. | `--server` : `run 2800`, `insert A …(Disk 2).STX`, `run 1500`, `key make 39`/`break 39`, `run 800`, `save 1`, puis `joy 08` ou `mouse 6 3 0` ×30. Trace : `NEOST_KBD_TRACE=1` (`reset-window … held/overrun`). |
| **International Karate + (IK+)** sur STE / TOS 256 Ko — bus error à `$FC000C` (2026-10-22, signalé par l'émulateur ASE) | ✅ **FIDÈLE, rien à corriger.** Le générateur aléatoire du jeu indexe la ROM ST : `lea $FC0000,a0` puis `move.w (a0,d0.w),d0` à `$36D2`. Avec un TOS en $E00000, rien ne décode $FC0000 : le GST MCU du STE (schéma ST4081S, `gstmcu.v` : /ROM2 ne couvre $FC0000-$FEFFFF qu'avec la sangle `tos192k` ajoutée par MiSTer) ne rend pas DTACK, d'où une bus error. Le jeu a détourné les vecteurs 2-11 vers du code vivant ($14FA) et finit par exécuter de la RAM vide. ASE fait répondre la fenêtre (« inconclusive » selon son auteur) ; Hatari (`memory.c:1784`) et NeoST la font fauter, comme le matériel. IK+ est un titre STF : sur STE, utiliser un TOS 1.x, ou un correctif STE. | Garde : `neost-headless roms/etos256us.img --machine ste --bus-selftest` (« STE $FC000C faute »). Rejoué sur `disks/ik_plus.STX` : ST + `tos102uk` = démo normale, image identique à l'oracle à la trame 1800 ; STE + `tos206uk` : `--break 36D2` s'arrête trame 1727 (`D0=$C`), puis double faute, vecteur 2, SSP=$6. L'oracle Hatari STE échoue aussi (« BUM COPY..... » dans la démo à la trame 1800, puis sortie avant la trame 2400). STE + `tos162uk` : pas de halt en 6000 trames (chemin non exercé dans ce délai). |
| **Froggies Over The Fence** (3 disquettes, `disks/etalons/froggies_over_the_fence/`) (2026-10-20) | **Deux causes, une par couche.** (1) ✅ **FIDÈLE sous EmuTOS : la démo exige un TOS Atari.** ST 1 Mo, EmuTOS 192 FR : panique « Line F Emulator » à `pc=$8000C` en noir sur noir ; le chargeur appelle `Rwabs(0, $30000, 8, 1, A:)`, qui rend −14 `E_CHNG`, et saute sur de la RAM vide. Hatari + EmuTOS : même `D0=$FFFFFFF2`. (2) ✅ **CORRIGÉ : menu inerte sous TOS 1.02.** Le pointeur ne bougeait ni à la souris ni aux flèches, alors qu'il bouge chez Hatari. Le programme 6301 de la démo n'était pas reconnu : NeoST calculait un vrai CRC32, alors que l'empreinte d'Hatari (`utils.c`) teste `(c & 0x80) ^ (crc & 0x80000000)`, en pratique un OU. Boot-stub : `0xed260822` au lieu de `0x2efb11b1`. Après correction, A/B flèche droite tenue 200 trames : même position du pointeur que chez Hatari ; les trois disquettes tournent (Overlanders « Jess's Greatest Hits », ST Connexion « Art Gallery », Legacy), changements de disquette compris. | TOS 1.02 FR, ST 1 Mo. Pilotage par `--server` : flèches (`key make 4D`…), clic par le 0 du pavé (`$70`), `insert A` à l'invite « INSERT DISK n ». ⚠ `--key-down N C` prend un CARACTÈRE, pas un scancode. |
| **Cuddly — The Fullscreen Demo (TCB)**, double faute avec faux diagnostic blitter (2026-10-19) | **CORRIGÉ** : le snapshot utilisateur montre `PC=$C782`, A4 et A7 impairs. Le rendu utilise A7 comme table ; une VBL anticipée l'interrompait et corrompait ses entrées. Cause : l'impulsion hi-res de bordure traversant X=0 faisait verrouiller 501×224 cycles au lieu de 313×512, soit 48 032 cycles trop tôt. Relire la géométrie au signal VBL (X=64/68), réarmer les événements futurs et adapter la fin de trame. Le diagnostic `$FF8A3C, PC=$FC0638` était une ancienne sonde blitter EmuTOS : les erreurs d'adresse et imbriquées sont désormais relevées. | Reproduction indépendante avec `TCB_FULL.PRG` dans `AUTO/` d'un dossier GEMDOS, obtenu depuis [l'archive TCB](https://fujiology.org/ST/T/TCB/TCB_FULL.ZIP). ST, 1 Mo, `etos192fr`, disquette formatée : ancien binaire en halt vers la trame 457 ; corrigé : 1 800 trames sans halt. Six contrôles synthétiques PAL↔mono échouent avant et passent après. Ce contrôle ne prétend pas à une égalité pixel du plein écran ; l'étalon d'intro existant ne couvre pas cette partie. Un état déjà gelé conserve la mémoire corrompue : relancer depuis un reset. |
| **Closure / No Cooper — bureau à la place du démarrage, noms incohérents sur A:** (2026-10-19) | **EXPLIQUÉ et démarrage vérifié** avec la configuration utilisateur : ST, EmuTOS 192 FR, 1 Mo, FDC normal et dossier GEMDOS sur C:. Le disque dur est choisi au démarrage. Les images des deux démos contiennent leur chargeur et leurs données, sans répertoire GEMDOS exploitable : No Cooper a un BPB mais des données binaires à l'emplacement du répertoire ; Closure n'a pas de BPB valide. Le même lecteur affiche correctement `PROGS`, `LISEZMOI.TXT` et `NEOST.TXT` sur la disquette formatée. | **Machine → Hard Reset**, puis **Alt pendant l'accueil EmuTOS** pour démarrer A: en conservant C:. Vérifié sur les deux images, en FDC normal. Recette headless : `--from-cfg … --scancode-at 50 38 --key-hold 500 --frames 1500`. Comportement documenté dans la [séquence de démarrage EmuTOS](https://github.com/emutos/emutos/blob/master/doc/startup.txt). |
| **Captain Blood (1988)** (ERE) | ✅ **JOUABLE en TOS US. Cas TOS FR TRANCHÉ (2026-07-09) = FIDÈLE, pas un bug.** « KEYBOARD PROBLEM » en TOS FR : oracle Hatari TOS FR montre EXACTEMENT le même écran (diff visuel identique). Le jeu n'envoie AUCUNE commande IKBD spéciale (flux IKBD US==FR : $08/$0F/$12/$80 init standard) → détection purement logicielle (lecture table clavier TOS) ; NeoST fait tourner la même ROM FR que Hatari → même résultat. La version crackée `[cr 42-Crew]` rejette la table AZERTY. Rien à corriger. | Vérifié à l'oracle (cmd-fifo+SPACE, tos102fr) : Hatari FR == NeoST FR. |
| **Shadow Warrior (1990)** (Ocean) | ✅ **PAS UN BUG (2026-11-04).** Le titre attend la touche 1 (« PUSH 1P OR 2P BUTTON »), pas le tir, sous NeoST comme sous Hatari. Séquence : ESPACE sur l'écran de crack, 1 sur le titre, ESPACE au disque 1, disque 2 puis ESPACE. Identique à Hatari jusqu'au début du combat, où ennemis et coups divergent. | `tests/replays/replays.json`, `shadow_warrior_st`. |
| **Eliminator-Nebulus** (A-Ha, menu) | ✅ **Pas un défaut établi (2026-11-04).** Le dégradé change à chaque trame. `compare_titles` donne de 0,16 à 69,9 % sur la même machine selon la seule liste de trames demandées, et chaque commande se reproduit à l'identique. Le texte et le menu concordent. Ne pas juger ce titre à la trame. | Passes `--joy` du 2026-11-04. |
| **Pirates (1989)** (MicroProse) `[cr Delight]`, écran d'intro | ✅ **PAS UN BUG — tirage au hasard (2026-11-04).** Le « bruit » d'Hatari à ~184 couleurs, noté à la passe du 2026-11-03, ne se reproduit pas : TOS 1.04 FR, `--auto A:\PIRATES.PRG`, `compare_titles`, 73 couleurs des deux côtés et 0,0 % jusqu'à la trame 2255 en ST et en STE. Les conditions exactes de la passe d'origine n'ont pas été gardées, sa cause reste donc inconnue. L'écart de 6 à 16 % qui suit, et les 0,63 % sur la couleur du sous-titre, jaune contre cyan, en 4 Mo et en 60 Hz, viennent du programme : il appelle deux fois XBIOS `Random` en `$36C9C`, et [C] TOS 1.04 `$FC1C76` sème cette suite une seule fois, sur `_hz200` copié en moitiés haute et basse. Relevé à ce point : `$00F8CDE2` puis `$00722A6B` sous NeoST, `$00303A7D` puis `$0045F713` sous Hatari, avec 6 tics de `_hz200` d'écart depuis le boot. Le lien entre ces tirages et l'ondulation du logo se lit dans le code d'appel, la forme exacte n'a pas été recalculée. | Ne pas rouvrir sur un écart du logo ou du sous-titre. Pour comparer ces écrans, il faudrait partir de la même valeur de `_hz200`. |
| **Vroom (1991)** `[cr Elite]`, intro plein écran sur STE | ✅ **CORRIGÉ (2026-11-04), le jeu l'emporte sur le modèle RTL.** La VBL `$C006` tient presque toute la trame. Si elle entre quelques cycles trop tard, `clr.b $820A` manque la bordure haute, le gestionnaire déborde sur la VBL suivante, et le programme principal, qui lit le clavier en `$D740`, ne tourne plus : SPACE reste sans effet. Deux corrections : l'attente E-clock de l'ACIA se date au début du cycle bus, 2 cycles plus tôt, [D] `m68000.c:810-822 @ f0736b24` ; hors STOP, la VBL du STE se prend au dispatch et non à la broche. Après correction : histogramme d'entrée de 268 à 308 comme Hatari, titre à 0,0 %, et bancs RTL et sondes de timing inchangés. | Partie `vroom_elite_ste`. Aucun relevé réel : le modèle fx68k + GSTMCU reste contredit sur la VBL hors STOP. |
| **Vroom (1991)** (Lankhor) | ⚠ **Cause trouvée sur cette image, réparable ; contradiction avec le fer non levée (2026-11-04).** `AUTO\VROOM.PRG` fait 2442 octets : l'en-tête, 2412 octets de texte et 2 octets de relocation au lieu de 4. Sur 102 images `.st`, seule une autre, Switchblade, porte un fichier de ce genre. **TOS 1.62, grade C** : `$E08A34-$E08A50`, `Pexec` lit 4 octets en `-$1C(A6)`, ignore le résultat (`D0=2`) et teste le long entier. Le mot bas reste celui de la pile : `$00E0`, haut de l'adresse de retour du `jsr $E09102` en `$E08BAA`. TOS ajoute la base `$AA9A` au long en base+`$E0`, `$2118` devient `$CBB2`, et le chargeur exécute `move.w #$1A8,$CBB2.w`, soit une erreur de bus. Si une VBL tombe avant le `link` de `Pexec`, sa sauvegarde des registres efface ce mot. TOS 1.04 et 1.06 lisent de même, ce résidu mène chez eux au retour au bureau, chemin lu sans le détailler. **Contre-épreuve** : la taille seule portée à 2444, les octets suivants valant déjà zéro. 1 Mo, SPACE, 24 graines : TOS 1.62, 17 échecs à 0 ; TOS 1.06, 16 à 0 ; TOS 1.04, 14 à 0. Hatari, TOS 1.62, 8 sur 8 après réparation contre 3 sur 8 avant. **Le fer** : le mainteneur se souvient d'un Vroom cracké, sur un STE de 1 Mo, jamais planté au démarrage, même après un reset. La version de ce crack n'est pas connue. Il n'a plus cette machine ; son TOS et son disque sont inconnus. À ces taux, une dizaine de démarrages sans échec serait très improbable avec ce fichier sous TOS 1.04, 1.06 ou 1.62, donc l'hypothèse la plus simple est un autre fichier ou un autre TOS. C'est une déduction, pas une mesure. | Copie réparée, non versionnée : `tools/fix_short_reloc.py IMAGE SORTIE`. Un tiers équipé d'un STE peut relever la taille du fichier et lancer `tools/make_reloc_probe.py`. Si la sonde reste intacte sous TOS 1.62, NeoST et Hatari ont tort tous les deux. Le bruit sous TOS 2.06 vient de la désélection du lecteur par la VBL du jeu, détaillée ci-dessous. |
| **Chessmaster 2000** (STX, Electronic Arts France), TOS 1.62 | ✅ **PLANTE AUSSI SOUS HATARI (2026-11-04).** Sous TOS 1.04 sur ST, `--auto A:\CMASTER.PRG` et le disque 2 en B mènent à l'échiquier, à 0,0 %. Sous TOS 1.62 sur STE, la même erreur de bus part chez les deux, en `$E1FC98`, `cmpi.w #8,($22,A0)` avec `A0=$455288D0`. La suite dépend de la graine : 2 ou 4 bombes sous NeoST selon `--fdc-seed`, 4 bombes ou d'autres fautes sous Hatari selon `HATARI_SEED`. Après le plantage, TOS efface une zone où vit encore la routine VBL du jeu, ce qui explique ces variations sans les démontrer en détail. | Lancer sous TOS 1.04. Aucun relevé réel sous TOS 1.62. |
| **Xenon 2 (1989)** (Image Works) | ✅ **PAS UN BUG — la durée dépend de l'angle de la disquette (2026-11-04).** Écran « LOADING LEVEL 1 », STF, TOS 1.02 UK, 1 Mo. Mêmes 136 lectures, sur les mêmes secteurs, chez les deux émulateurs. De la première à la dernière : 348 à 692 trames sous Hatari sur 6 valeurs de `HATARI_SEED`, 332 à 684 sous NeoST sur 24 valeurs de `--fdc-seed`. L'écart de 660 contre 930 trames relevé d'abord venait donc de deux tirages. La cause probable reste la collision entre un secteur de 1,02 VBL et le gestionnaire de VBL, non démontrée. | Comparer une durée de chargement sur plusieurs graines, jamais sur un seul tirage. Aucun relevé réel de cette durée. |
| **Enchanted Land (1990)** (Thalion) | ✅✅✅ **RÉSOLU (5ᵉ passe 2026-07-02) : moteur fullscreen verrouillé 100 %** (12402/12402 écritures freq à la position Hatari-exacte). Le dernier verrou n'était PAS la reconnaissance IPL mais un **double comptage du saut STOP** dans la comptabilité de quantum → `sched.now()`/datation vidéo en avance de δ∈{4..26} sur l'horloge CPU (quand δ≡2 mod 4, le calibrateur $8209 déverrouillait). Micro-sauts éliminés. **Son Thalion OK** (confirmé GUI 2026-07-02 — réparé par la même passe). Jeu complet. | Fix = rebase `quantumStartBus_` au saut STOP (`Cpu68k::run`) → `docs/MOIRA_WINUAE_CONVERGENCE.md` (5ᵉ passe). Les 3 mystères (lock, commit-VBL↔loader, IPLFETCH↔loader) avaient cette même cause. Sur STE, plusieurs SPACE pendant l'intro peuvent mener à un écran noir tenu par le moteur plein écran, chez NeoST comme chez Hatari (2026-11-04) : `compare_titles` avec SPACE à 900, 1200, 1600 et 2000 donne 0,0 % de 2600 à 5600. Ce n'est pas un défaut propre à NeoST. |
| **Lethal Xcess** (`.STX`, TOS 1.62) | ✅ titre stable + **in-game RÉPARÉ (2026-07-12)** : la régression « commit scanline au HBL » (617d030) faisait committer les 29 lignes de bordure haute sur l'ancre sticky `liveStartHBL_=34` (paire 60/50 de calibration ligne 32, qui n'ouvre PAS le haut) → poll $8209 fin de trame faux (+29×160 octets) → écran en jeu déchiré + chargements en échec. Fix `commitAnchor()` (fenêtre Glue LIVE, latchée) → pixel-identique au pré-régression, flux $8209 byte-identique (~3 M lectures). ✅ Titre « buggé à ~8 % » GUI (2026-07-02) : **non reproduit le 2026-10-20**, écran titre (60 couleurs) à 0 px de l'oracle Hatari aux trames 2000 et 2800 (STE, TOS 1.62 UK, 512 Ko, disques 1 et 2). | Recette in-game headless : `--disk Disk_1.STX --diskb Disk_2.STX --keys-at 3000 " " --joy-script 16500 "FFFF" --joy-at 21000 0x80` → jeu ≈ trame 21900 (⚠ le jeu NE DÉMARRE qu'avec le disque 2 monté : titre→SPACE→music select→FEU→briefing→FEU tenu). Oracle Hatari : mêmes disques, Xvfb+xdotool, feu=Control_R tenu pendant chargement+briefing. |
| **The Cuddly Demos** (TCB) | ✅ **étalon trame 3400 RÉ-ACTIVÉ (2026-08-19)** : désactivé le 2026-08-01 pour « phase d'animation différente » (16023 px au mieux), il est en fait **byte-identique à l'oracle** — 0 px / 114766 sur **220 trames consécutives**. Ce qui manquait était la bonne trame Hatari : l'oracle sème son RNG sur `time(NULL)` et tire la position angulaire initiale de la disquette, donc la durée du boot ET la numérotation des trames changent d'un run à l'autre (mesuré : n+61 dans un run, n−2 dans un autre). Le balayage à `frame:` figé ne pouvait pas converger. Remède outillage : `oracle_scan` (fenêtre + choix de la trame IDENTIQUE). ✅ **menu robot RÉSOLU (2026-07-03, commit `125388b`)** : clignotement vertical 10-47 % → **0** (250/250 trames verrouillées, mur régulier = Hatari). 1ʳᵉ page OK. | Cause : datations lecture (`−14`) / écriture (`−6`) du compteur vidéo co-calibrées autour d'une « origine −8 » devenue artefact après le fix STOP — ramenées ENSEMBLE aux valeurs fidèles §8 (**read −6, write +2**). Le synchroniseur (pc=f264, sortie sur octet bas `$8209` > `$40` SIGNÉ) lisait 4-6 octets de moins qu'Hatari au même instant → sortie L34→L36 → paire 60/50 une ligne trop tard. ⚠ Ne bouger ces offsets que PAR PAIRE (read seul casse EL). Repro : `--fastfdc --keys-at 3000 " "`, ≈ trame 6000. → `docs/MOIRA_WINUAE_CONVERGENCE.md`. |
| **Rick Dangerous II (1989)** (Core) | ✅ **JOUABLE** (résolu ; l'ancien plantage « 4 bombes » ne se reproduit plus). | — |
| **Stardust (1994)** (Daze) | ✅ **TRANCHÉ (2026-07-09) : jeu STE-ONLY, « crash ST » = FIDÈLE.** Sur `--machine st` le jeu lit `$FFFF8900` (son DMA **STE**) à PC=$38800 → bus error : **Hatari HALTE aussi** (« double bus/address error => CPU halted ») → pas un bug NeoST. Sur `--machine ste` (tos162fr, 1 Mo) : intro crack HARDCORE + « présente STARDUST » rendus. ⚠ Après l'intro → noir (multi-disque « Disk 1 of 3 » / touche — non creusé). ✅ **Double-fault ST : suivi CLOS (2026-08-25), NON REPRODUIT.** NeoST **halte** lui aussi, sur la MÊME instruction que Hatari : `$FC5082 move.l A3,-(A7)` avec `A7=$4E7340E7` **impair** (le handler bus error du TOS, entré en `$02FC0B50`, recharge un A7 corrompu) → erreur d'adresse + SSP impair = double faute (porte Hatari `cpu/newcpu.c:3076-3079`, porte Moira `MoiraExceptions_cpp.h:281-282`). Mesures : `--frames 1827/2000/2500` → **0 instruction** après la trame ~1826, préemptions figées à 3053, capture 100 % noire ; oracle Hatari → `cpu exception 3 currpc fc5086 buspc fc5082 … fault_e3 4e7340e5` puis « Detected double bus/address error => CPU halted! ». Cause de l'observation d'origine **indéterminée** (le chemin de halt existait déjà au 2026-07-09). Seul écart réel, corrigé : NeoST haltait en **silence** → `[cpu] 68000 halted: double bus/address error while taking exception vector 3 (SSP=$…)` (`src/core/Cpu68k.cpp`, `cpuDidHalt`) + ligne de bilan `[headless] CPU halted …`. | Lancer sur **STE**. Le halt ST est **fidèle** et désormais journalisé. |
| **Spectrum 512 — palettes « foirées » sur STE** (2026-07-09) | ✅ **PAS UN BUG — comportement FIDÈLE** : l'auto-diapo scramblait les palettes sur STE/Mega STE (parfait sur ST). Diag : le viewer spec512 est calibré timing **STF** → sur STE il se désynchronise **sur vrai matériel aussi**. NeoST STE == **oracle Hatari STE byte-exact (0 px, plage f1645-1660)** → NeoST reproduit fidèlement, ce n'est pas sa faute. La vraie lacune était l'**absence de détection** (rien ne disait « fidèle » vs « bug »). | ✅ Étalon `spectrum512_diapo_ste` ajouté (épinglé à l'oracle Hatari STE `tests/reference/spectrum512_diapo_ste.png`). ⚠ `9f0d2bc` (res-tricks) écarté : ne touche que MED_OFFSET ; ST reste 0 px vs oracle. Leçon → « Système de régression » ci-dessous. |
| **Blood Money (1989)** (Psygnosis) | ✅ **PAS UN BUG — manque de RAM (2026-08-07)** : « plante après le cracktro » = écran noir définitif en **512 Ko**, sur les DEUX cracks. **Oracle Hatari : même écran noir** (même ROM/disque/SPACE, jusqu'à 7000 VBL) → fidèle. Trace : recopie emballée à `$E26` avec `D2=$FFCC0484` (compteur négatif ≈ 4,3 G d'itérations) et `A0` déjà à `$34CA84` alors que `phystop` (D5) = `$00080000` — le dépaqueteur du crack calcule une taille bidon faute de RAM ; Hatari logge le même emballement (`Bus Error writing at address $400000, PC=$e28`). En **`--mem 1m`** : ça charge et ça joue. | ✅ Jouable en **1 Mo** avec `[cr Delight][m Superior][t]` (version « file », mono-disquette) : jeu ≈ trame 4800. `[cr Replicants][t]` démarre aussi mais réclame la **disquette 2** (non présente dans `disks/st/`). Recette : `--mem 1m --fastfdc --key-down 900 " " --key-up 906 " " --shot-every 600`. |
| **HotPot (2002)** (Reservoir Gods, notre build `build.sh --game`) | ✅ **RÉSOLU (2026-07-09)** : front-end JOUABLE (menu « DOUBLE JUGGLE », INFO/OPTIONS/PLAY/EXIT) sur STE 1 Mo/tos162fr et tos106uk 4m. Le « noir après l'intro » N'était **PAS** une divergence d'émulation (identique dans Hatari) mais un **bug toolchain** : `build.sh` ignorait les `-D` du `.PRJ`, donc `-DdGODLIB_FADE` jamais défini → `Fade_Init()` (dans `#ifdef dGODLIB_FADE` de PLATFORM.C) compilé hors → callback VBL `Fade_Vbl` jamais installé → **fade-in de palette jamais armé** (palette figée noire alors que le front-end était bien dessiné). | Fix : `build.sh` collecte les `-D` actifs du `.PRJ` (`GAME_DEFS`) → toutes les compilations `vc`. Diag : symboles DRI du `.TOS` + `--dump-at` (`$FFFF8240`, `gFade`, `mfCalls`) + oracle Hatari `--harddrive/--avirecord`. Renvoi mémoire `hotpot-jouable-divergence`. |
| **Wings of Death** (`.stx`, Thalion) | ✅ **NON REPRODUIT après instruction COMPLÈTE (2026-08-26) — dossier CLOS.** Rapport GUI : « après le bouton : titre corrompu + son ralenti ». Trois passes : (1) **cœur émulé** disculpé au bug hunt du 2026-08-25 — vidéo **byte-identique** à l'oracle Hatari, cadence YM à ±0,1 s sur 26 s, silence exact des deux côtés ; (2) **GUI, phase de chargement** : 100 s, zéro `ring underrun` ; (3) **GUI, APRÈS LE BOUTON** (la réserve restante) : feu joystick **injecté et tenu** dès la trame 3000 (`--joy-at 3000 0x80`, brique A8), config utilisateur réelle (`neost.cfg`, `drivesound` actif), capture à 5500 — **titre au dragon 154 couleurs PROPRE, zéro underrun** sur tout le run, rafale FDC t=72-74 s comprise. | Rouvrir UNIQUEMENT sur nouvelle repro utilisateur accompagnée du `neost.cfg` et de la version (`--version`) : le rapport d'origine prédate les chantiers BL3/BL4/D-PSG/D3. Recette de la passe (3) : `./build/neost --run-frames 5500 --shot x.ppm --joy-at 3000 0x80 roms/tos102fr.img Wings_Of_Death_Disk_1.stx`, sans sandbox. |
| **Beyond the Ice Palace** (D-BUG) | ✅ **NON REPRODUIT après instruction COMPLÈTE (2026-08-26) — dossier CLOS, y compris le chemin « double-clic GEM ».** Rapport GUI : « écran scramblé en jeu ». Trois chemins instruits : (1) **headless, boot AUTO** (2026-08-25) : gameplay PROPRE ; (2) **GUI, chemin GEM EXACT du rapport** — bureau → double-clic icône A → fenêtre → double-clic `ICEPALAC.PRG` (`Pexec` sous AES), entièrement piloté au script souris (`--mouse-at`, brique A8) → cracktro D-Bug → réponses au trainer (`--scancode-at`, `--key-hold 30`) → menu du jeu → **EN JEU, PROPRE, 16 couleurs** (héros, échelles, HUD nets) ; (3) **GUI, moniteur MONO** (`mono=1`) : le PRG **QUITTE en ~10 s** (retour bureau, pointeur re-centré = signature d'un `Pexec` terminé) — on ne peut même pas être « en jeu » en mono, le scramble ne peut donc PAS venir de cette config. | Rouvrir UNIQUEMENT sur nouvelle repro avec `neost.cfg` + version. Recette GEM couleur : saturation coin (⚠ ≥ 80 tokens X / 50 tokens Y pour couvrir 640×400 — 40 ne sature qu'en basse résolution, bug de calage vécu), double-clic au pattern Hatari `1111....1111....` (4 down/4 up/4 down/4 up, `ikbd.c:DoubleClickPattern`), approche par le coin BAS (les menus TOS se déroulent au SURVOL et capturent les clics d'un trajet par le haut). |
| **Arkanoid (1987)** (Imagine) | ✅ **TRANCHÉ (2026-08-25) : FIDÈLE — dépendance du jeu à la version de TOS.** Sous **TOS 1.02** le jeu boucle à l'infini sur `$31736 tst.b $26e7 / bne` : NeoST **et Hatari** bouclent identiquement, état machine **byte-identique** (`$26E0` = `00 00 00 00 00 00 01 3f …`, D0-A7 identiques, `$3173E` jamais atteint en 20000 VBL ; watchpoint Hatari sur `$26e7` = UNE seule transition 0 → $3F). Sous **TOS 1.00 US/UK**, `$26E7` vaut `$00` des deux côtés, la boucle tombe et le jeu est **JOUABLE** — dans NeoST comme dans Hatari. La boucle est l'attente clavier « 1 ou 2 joueurs » du titre (+ code triche « PAJ ») : les pistes **protection / 2ᵉ chargement / IRQ sont RAYÉES** (aucun vecteur $47 pendant le gel ; seuls $1C et $45 — le poll flopvbl du TOS `$FC1BC6`/`$FC1C0A` continue, mais il n'est pas du jeu). Le chargeur `AUTO/STARTGEM.PRG` liste **deux** dates ROM en `$FC0018` (`$11201985` = TOS 1.00, `$04221987` = TOS 1.02) et applique des patchs **exclusifs** ; toute autre date → « Error! - Can't work with this TOS-version. » (reproduit avec tos104uk et tos100fr). | Lancer sur **tos100us/uk** + touche « 1 » → ROUND 1. ⚠ Le disque `Arkanoid (1987)(Imagine).st` n'est **plus dans le dépôt** (supprimé incidemment par `45f9a65`, qui a aussi effacé `carts/Buggy Boy.img`) ; `Arkanoid II [cr Replicants][t]`, lui, va **jusqu'au jeu** (double-clic `DOH1.TOS` → ROUND 01). Balayage sans effet : tos102fr/uk/us, 512k/1M, `st`/`megast`, MEGA TOS, avec et sans `--fastfdc`. |
| **New Zealand Story** (`.st`, D-Bug/Vapour) | ✅ **PAS UN BUG — manque de RAM (2026-08-25).** « Boucle infinie dans la ROM TOS à `$FC0BD8` » : NON, `$FC0BD8` est la routine d'affichage des **BOMBES** du TOS — le bitmap pointé par `A1=$FC0DFA` est la bombe 16×16, vérifiable par dump direct de la ROM. Le dépaqueteur mono-fichier de D-Bug exige **4 Mo** : en 1 ou 2 Mo il écrit au-delà de la RAM et plante, le TOS bombe, et le compteur `D2=$FFFF32B5` de la boucle de blit est la **conséquence** du plantage, pas sa cause. Oracle : le même `NZEALAND.TOS` lancé depuis un dossier AUTO **plante aussi dans Hatari** (3 bombes) en 1 Mo. | `--mem 4m` → chargeur D-Bug affiché puis **niveau 1 en jeu** (score 000300). Réflexe RAM. |
| **Xenon 2 Megablast (1989)** (Image Works) | ✅ **PAS UN BUG — repro invalide (2026-08-25).** « Figé sur GET READY PLAYER 1 » : `--joy-script` n'était **pas répétable** (scalaire à l'époque ; répétable depuis `4b32a36`, le même jour) ; les quatre scripts de la ligne d'origine se réduisaient au dernier, qui était la *validation du menu*, pas un feu. Preuve indépendante : aucune activité FDC entre les trames 2000 et 26000. | Avec **UN** script joystick continu : niveau 1 joué puis table HIGH SCORE (0000600). Les 7 configurations testées jouent (512k→4M, tos102fr/uk, tos104uk, st et ste). |
| **Flood (1990)** (Bullfrog) `[cr Replicants - ST Amigos][t]` | ✅ **PAS UN BUG — protection de cracker (2026-08-25).** L'« écran drapeaux figé sans texte » est le **sélecteur de langue** du crack, navigable au joystick, suivi d'un **quiz de documentation** lu en clair (« From the poster. / Who is … » + invite `>`). Le clavier est prouvé fonctionnel : taper « abcde » affiche `>ABCDE` (+139 pixels allumés, mesuré). Repro d'origine invalide par surcroît (deux `--joy-script`). | Réponse du quiz inconnue → le jeu n'est pas atteignable, sans que NeoST soit en cause. Face à Hatari, le 2026-11-04 : le titre interroge le joystick toutes les 8 trames sur les deux. Un tir de 4 trames est pris par NeoST à 8 phases sur 16, comme le veut cette période, et jamais par Hatari ; un tir de 12 trames est pris par les deux. Le même menu s'ouvre ensuite. Écart de lecture du tir non expliqué côté Hatari, sans effet sur le jeu. |
| **Dynamite Dux (1989)** (Core) `[cr MCA][t]` | ✅ **PAS UN BUG — le jeu tourne (2026-08-25).** « Aucune entrée ne lance la partie » : deux pièges CUMULÉS. (1) `--joy-script` n'était pas répétable (corrigé par `4b32a36`) ; (2) `--keys-at` ne tient la touche que **2 trames = 40 ms** par défaut (make +0 / break +`keyHold`, réglable par `--key-hold N` depuis le 2026-08-26, N ≥ 1 ; boucle d'injection de `main_headless.cpp`), trop court pour la passation de la cracktro MCA. ⚠ Un premier verdict « confirmé à l'oracle » était FAUX : il comparait 40 ms côté NeoST à ~600 ms côté Hatari. **Toute comparaison via `--cmd-fifo` doit égaliser la DURÉE d'appui.** | `--key-down`/`--key-up` (espace tenue 60 trames) + UN script joystick continu → niveau 1 joué (15 couleurs, HUD « BIN / START / GOAL »). |
| **Disquettes STX de DONNÉES seules en A:** (`GoldenAxe-D2`, `SuperOffRoad-D2`) | ✅ **FIDÈLE (2026-08-25), trois oracles concordants.** L'« écran blanc sans bureau GEM » est correct : ces images ont un secteur de boot **exécutable** (checksum `$1234`) dont le chargeur affiche « Boot file not found! / Please Re-boot with / correct disc in drive! » ou boucle. Hatari donne le même écran, **au message près**. Les 1001 pixels « manquants » du décompte d'origine *étaient* ce texte. | Contre-épreuve des deux côtés : une `.st` NON amorçable (Beyond the Ice Palace) donne bien le bureau GEM vert. Ce n'est pas le décodeur STX. |
| **Great Giana Sisters** (chargeur D-Bug) | ✅ **PAS UN BUG — entrée non trouvée (2026-08-25), dossier CLOS.** Le menu ignore le joystick chez NeoST **ET** chez Hatari (oracle avec vrai tir sur le port ST 1, `[Joystick1] nJoystickMode=2` : même menu, flèche au même pixel). La chaîne IKBD→ACIA de NeoST est prouvée fonctionnelle sur ce titre : `NEOST_DEBUG_ACIA=1` montre 205 en-têtes `$F8`, **22 paquets `$FA` = clic gauche**, 45 `$08` et 202 `$00` de deltas — le programme REÇOIT tout. | Remontée joystick portée à l'identique de `ikbd.c:1447-1473`. |
| **Pipe Dream** (`disks/st/Pipe Dream.st`) | ✅ **PAS UN BUG — outillage headless (2026-08-25).** L'image n'est pas le jeu mais le menu de compilation **Automation « CHAPTER 200 »**, correctement rendu et animé. Aucune touche ne sélectionne un titre, chez NeoST **comme chez Hatari** (oracle `--cmd-fifo`, scancodes 2/3/4 ; livraison de la fifo prouvée par l'ERROR sur un `keypress zz` volontairement invalide). | Cause probable : `stScancode()` (`main_headless.cpp`) ne mappe pas le **PAVÉ NUMÉRIQUE** (seuls `.` 0x71 et Enter 0x72). Piste d'outillage, pas d'émulation ; `--scancode-at` (scancodes bruts) contourne désormais cette table. |
| **Closure (Sync)** — écran 153 couleurs | ✅ **CORRIGÉ (2026-08-30, A40) : décalage de 4 px de TOUTE l'image.** L'écran passe par le **retrait gauche MED** (`BORDERMASK_LEFT_OFF_MED`, le « remove left + med stab » qu'Hatari nomme d'après cette démo, `video.c:3974-3997`) et NeoST y recopiait le `STF_PixelScroll` d'Hatari (−8) en oubliant que l'ancrage de recopie d'Hatari est déjà 4 px à droite du faisceau : la bonne valeur est `shEff = 4 + scrollFinal` = **−4**. Deuxième maille : les pixels qui « entrent » par la droite après le décalage n'ont plus de source et sortent à l'index 0 chez Hatari (`video.c:4295`) → les 8 dernières colonnes du buffer restent à l'index 0. **Oracle 64,08 % → 0,02 %** (27 px, tous ligne 0 → A41). | Recette : `--load-state` d'UNE trame + `NEOST_RENDER_TRACE`/`NEOST_RENDER_ALL` pour lire le masque par ligne ; balayage ±6 px × ±3 o pour isoler l'optimum. `Shifter.cpp` (bloc A40), `CHANGELOG.md` 2026-08-30 |
| **Closure (Sync)** — les 27 px de la ligne 0 | ✅ **FIDÈLE (2026-09-01, A41) : c'est HATARI qui anticipe.** Résidu d'A40 : 27 px, tous sur la ligne 0 du buffer (`sl=34`, la première ligne affichée, celle qu'ouvre le retrait de bordure haute), chacun d'une couleur voisine d'un cran. **Mesuré**, pas déduit : en instrumentant `Spec512_StartFrame` (arbre `extern/hatari` gitignoré, sonde révoquée et binaire rebâti au pin après coup), l'amorce de palette d'Hatari pour cette ligne vaut `000 100 200 210 310 310 320 420 430 531 442 541 552 652 652 763` — **exactement, sur les 16 registres**, le bloc que la démo écrit aux cycles **438-508 de cette même ligne 34**, soit 380 cycles APRÈS les pixels qu'il colore. Le faisceau au cycle 56 ne peut pas afficher une couleur écrite au cycle 446 : NeoST, qui applique chaque écriture à son cycle, est fidèle. **Mécanisme** : `Spec512_StartFrame` (`spec512.c:233`) fait `nScanLine += OVERSCAN_TOP` sous `V_OVERSCAN_NO_TOP` — les `CyclePalettes` des scanlines **0 à 28 ne sont jamais rejouées**, or le bloc d'init de la démo vit lignes 1-2, en plein dedans ; l'amorce retombe alors sur `pHBLPalettes[]`, garni par le `pHBLPalettes -= OVERSCAN_TOP` de `video.c:3429` que Hatari commente lui-même « FIXME useless ? ». Contexte : `vover=3 nStartHBL=34 STScreenStartHorizLine=0 OVERSCAN_TOP=29 nScanLine=29 skip=5`. | Ne PAS corriger vers Hatari. L'étalon `closure` reste `ref_kind: snapshot` **définitivement** : le minimum a été CHERCHÉ (70 trames NeoST voisines balayées contre l'oracle commis — 27 ou 43 px, jamais 0), ce n'est donc pas un défaut d'alignement de capture. Recette de la sonde : `fprintf` de `pHBLPalettes[0..15]` dans `Spec512_StartFrame`, confronté au bloc de fin de ligne 34 de `NEOST_SPEC512_TRACE`. ⚠ Piège rencontré : la trace `--trace video_color` d'Hatari doit être armée sur LA MÊME trame (`--parse` + `b VBL = N :once :file …`, et l'égalité s'écrit `=`, pas `==`) — la palette d'init de cette démo change à chaque trame, comparer deux trames différentes envoie sur une fausse piste. |
| **Closure (Sync)** — trames isolées noires ou brouillées du grand logo | ✅ **CORRIGÉ (2026-11-12) : régression du 2026-10-22, le saut d'attente STOP sortait le 68000 de sa grille de 2 cycles.** Pendant le logo X-DISTING (~1 300-2 300), une trame noire ou brouillée isolée de temps en temps ; Hatari aucune. La démo se cale par `stop #$2100` ×2 sur la HBL puis compense la gigue E-clock de l'IACK par une file de cinq instructions recopiée à chaque VBL (`$15C-$16F` → `$205BC`, une entrée par phase : 160 256 ≡ 6 mod 10). Invariant mesuré chez Hatari : l'écriture `$FF820A` de `pc=$2061A` tombe ligne 33 cycle 468 sur 513 trames sur 513 ; NeoST la sortait à 460-480 sur 86 trames (TOS 1.02 UK). Cause, mesurée au cycle : `Cpu68k::run` posait l'horloge du CPU en STOP sur l'échéance du prochain événement, ici le Timer D (26,08 cycles, échéances souvent impaires) ; le pas de STOP de Moira courait alors sur la grille impaire, la HBL réveillait un cycle trop tard et le réveil quantifié de `2fc53a8` (fusion `fb70af8`) l'arrondissait à +4 → mauvaise attente E-clock. Correctif : le saut avance d'un nombre pair de cycles ([B] M68000UM Rev 8 § 8, [D] fx68k.sv:177-197 @ 0602ee46, [D] Hatari gencpu.c:7018 @ f0736b24). Trames fautives 52/96/85 → 0 (TOS 1.02 UK / EmuTOS / `--fastfdc`). | Recette : `NEOST_SYNC_TRACE=1` et filtrer `pc=02061a` à la ligne 33 (468 attendu) ; `NEOST_STOP_PHASE=0` pour l'A/B. Garde : `tools/run_closure_intro_guard.py` (palier full). Journal : `docs/CLOSURE_CHANTIER.md` § Cycle 7 |
| **`overscan_top`** — 5 lignes de transition 60 Hz | ✅ **FIDÈLE (2026-08-30, A40) : les 144 px sont un artefact de recopie d'HATARI, pas un bug NeoST.** Les deux émulateurs sont d'accord sur l'état Glue (`detect left+2 / right-2 60Hz 53<->373`). Hatari range les 2 octets « left+2 » dans les 2 derniers octets de la bordure gauche de son tampon — or 2 octets en basse résolution valent **un mot de plan sur 16 px**, d'où 16 px de plans mixtes à chaque bout : index 8 ($333) en tête, index 7 ($555) en queue. 4 lignes × 32 px + 1 ligne × 16 px = **144**, tout l'écart. NeoST rend le décalage physique de 4 px. | `--trace video_border_h` côté Hatari, `--dump-at 380 FF8240 32 pal.bin` pour la palette () ; `docs/HATARI_DIVERGENCES.md` § 2, « Lignes 60 Hz isolées (A40) ». L'étalon reste `ref_kind: snapshot` |
| **No Cooper (1984)** en `machine=megast` | ✅ **FIDÈLE (2026-08-30) — HALT CPU, et Hatari halte pareil.** Rapport GUI : « plante après la page 1, puis la page 2 avec la musique, puis Return Return ». Ce n'est pas une image fausse : dès que la démo prend la touche qui fait avancer la partie, le 68000 part en **double faute de bus** (exception vecteur 2) et la machine gèle. Le profil machine seul est en cause, pas la ROM — matrice `{megast, st}` × `{tos104fr, etos192fr, tos102uk}` : halt sur les trois `megast`, aucun sur les trois `st` (la démo déroule ses parties). Sans appui touche, `megast` tient 9 000 trames. **Oracle** : Hatari en `--machine megast`, même ROM, espace injecté par `--cmd-fifo`, donne `Bus Error reading at address $ffff820f, PC=$58` puis `Detected double bus/address error => CPU halted!`. **Apparié à l'INSTRUCTION PRÈS** (2026-08-30, une fois la faute journalisée — A42) : NeoST dit `reading at address $00FF820F, PC=$000056`, Hatari `reading at address $ffff820f, PC=$58` — même adresse (NeoST la masque en 24 bits, Hatari montre le miroir 32 bits), même sens, et le PC ne diffère que de la convention de prefetch : la mémoire en $56 porte `$4228 $FA0F` = `clr.b $FA0F(a0)`, donc NeoST nomme le DÉBUT de l'instruction et Hatari le mot d'extension (son `op_e3=4228` le confirme). **LA CAUSE, matérielle** : l'adresse fautive `$FF820F` est l'une des deux que le chipset **Ricoh** du ST simple laisse « void » (lecture sans faute) et que le chipset **IMP** du Mega ST fait FAUTER — Hatari le modélise explicitement (`IoMem_FixVoidAccessForST` / `ForMegaST`, ioMem.c:156-196) et NeoST le porte (`Bus.cpp:535-542`). La démo lit donc un registre qui répond sur un ST et faute sur un Mega ST ; son gestionnaire part en vrille et double-faute. **Remède : `machine=st`.** | ⚠ Piège d'injection : la démo n'accepte l'espace qu'à partir de l'écran « PRESS SPACE TO GO ON » (~VBL 2800 sous `tos104fr`) — une injection plus tôt ne fait RIEN et donne un faux « Hatari ne plante pas ». `hatari_oracle.sh` ne sait pas taper : `--cmd-fifo` + `hatari-event keydown 57` (cf. la note de l'étalon `nocooper`). Vérifié aussi que le correctif A40 du même jour n'y est pour rien — le binaire de `4c38cc0` halte à l'identique |

## Vroom STE : vérifier la conduite et l'arrêt audio (2026-11-05)

**Périmètre actuel : seule l'image `[cr Elite]` reste dans la ludothèque.**
Le mainteneur a demandé le retrait de l'image sans suffixe après la campagne
ci-dessous. Ses six scénarios sont archivés dans
`tests/replays/archive/vroom_retired.json` et ne font plus partie des suites
actives. Les mesures qui suivent décrivent les médias avant ce retrait ; les
défauts restent documentés, sans être déclarés corrigés. Pour rejouer un cas
archivé, il faut rétablir explicitement l'image privée dont l'empreinte est
consignée et passer `--manifest tests/replays/archive/vroom_retired.json`.

### Elite sous TOS 2.06 : intro qualifiée et course rejouée (2026-11-05)

**Périmètre : STE 1 Mo, TOS 2.06 UK, image Elite intacte, FDC normal.**
Le nouveau parcours `vroom_elite_tos206_ste_race` atteint le menu
**ARCADE / JOYSTICK / AUTO**, la grille puis la conduite. SPACE reste à
2000 et 3008 ; le joystick et la souris sont décalés de 2224 trames par
rapport au parcours 1.62 : Fire à 6040, première sélection à 6860,
démarrage à 7764, accélération à 9024. Sans ce décalage, Fire arrive trop
tôt et la démonstration automatique finit par rouler : ce n'est pas une
partie pilotée. Les graines NeoST et Hatari sont 1, indépendamment.

Les captures inspectées aux trames 7724, 8974, 9324 et 9624 donnent
**0,05 / 0,03 / 0,02 / 0,18 %** face à Hatari `f0736b24`, sans patch TOS,
fast boot ou fast FDC. Fenêtre ±40 et seuil 0,5 % conservés ; les empreintes
`scene_hashes` gardent les étapes du parcours. Le verdict reste de grade D.

Un pilote visuel externe poursuit ensuite la course à partir de la trame
10001, par les seules commandes joystick, et franchit **Lap 1**. Ses
entrées figées sont rejouées **depuis reset sur 18 001 trames**, sans
chargement d'état ; l'image finale est identique à celle du pilotage.
`vroom_elite_tos206_ste_lap`, dans `tests/replays/vroom_laps.json`, garde
ce résultat à l'échelon **snapshot**, avec les empreintes des médias.
Il complète le tour Elite sous 1.62, sans valider toutes les issues du jeu.

**Intro rayée, graine 5 : une trame transitoire, mécanisme localisé.**
La recette initiale (SPACE 2000/3008, Fire 6040 tenu 20 trames) est
rejouée sans autre entrée. Parmi les **61 captures consécutives 8470–8530**,
seule **8500** est rayée ; les 60 autres sont identiques entre elles.
L'image initialement signalée est reproduite à l'octet, puis **8501**
retrouve spontanément l'intro lisible. Aucun halt ni chargement supplémentaire.

Les traces depuis reset et la reprise d'un état à 8495 concordent. Dans
la trame fautive, le basculement 60 Hz en `$E558` se date à la ligne 33,
cycle **500**, contre **480** à 8499 ; le retour 50 Hz est à 34:40.
Après lecture de `$FF8209` par le stabilisateur `$E58A–$E590`, les
écritures de résolution arrivent à **34:504 / 35:8**, contre
**34:508 / 35:12**. Les bascules de fréquence des lignes suivantes se
font à **372/380**, contre **376/384** : le retrait droit est manqué.
Le rendu consomme alors **184 au lieu de 230 octets** par ligne pleine,
ce qui explique les bandes sans corruption permanente de l'image.
À 8501, les dates et les 230 octets reviennent.

**Contre-épreuve longue : Hatari reproduit aussi la trame rayée.** Sous la
même configuration (STE, TOS 2.06 UK, graine 5), sans aucune entrée hôte,
les traces `video_sync,video_addr` de 8000 à 8999 contiennent **20 séquences**
avec le basculement à 33:500, le retour à 34:40 et le compteur `$026A0E`
à 34:80. La première est à 8054. Les captures prises aux VBL 8054, 8055
et 8056 montrent respectivement une intro lisible, rayée, puis lisible.
La capture rayée correspond donc à la trame achevée 8054. Le premier
témoin court avait manqué ces occurrences ; son écart de 62,67 % ne
permettait pas de conclure à une faute propre à NeoST.

Le mécanisme est désormais précis : après le préchargement 60 Hz, le
retour à 50 Hz à 34:40 conserve le départ anticipé de la ligne, à 52 au
lieu de 56. Le compteur expose **14 au lieu de 12** à 34:80. Le programme
soustrait cette valeur de 16 puis attend par `lsl.w D2,D4` ; ces deux
unités en trop raccourcissent l'attente de quatre cycles. Le retrait
droit est ensuite tenté à 372/380 au lieu de 376/384.

**Sonde horizontale indépendante du jeu :**

```sh
python3 tools/probe_vroom_intro_glue.py
```

Elle rejoue les deux séquences, translatées des lignes 33–36 aux lignes
99–102 pour isoler l'horizontal, sans CPU ni données du jeu. Le modèle
GSTMCU `4a6db7cc` et NeoST donnent **160 puis 230 octets** pour la séquence
normale, **162 puis 184** pour la tardive. La fin DE normale reste décalée
de quatre cycles entre modèles (écart déjà décrit dans GLUE_SWEEP), sans
écart d'octets ; la séquence tardive concorde aussi sur les fenêtres DE.
Le témoin reçoit les chutes d'AS, NeoST les dates AS+8 du banc CPU/raster
existant. Ce choix est explicite, sans recalage propre à Vroom.

Le schéma **[B] Atari ST-4081 rév. A, 27 octobre 1988, feuille 29** montre
les décodeurs CPAL/CNTSC et les registres successifs du départ HDE
(PT004/PT010/PT017/PT026). Il soutient le mécanisme à mémoire ; il ne
valide pas à lui seul les dates numériques de ce rejeu. SHA-256 du PDF :
`935c4dd9a09a9af8f738276ccfeb32ee8d8e254a8d0985dad57a23326fcdfc9f`.

Aucun changement CPU/GLUE n'est déduit de ce cas. **L'accord des modèles
ne prouve pas le comportement matériel** : restent à mesurer la latence
d'entrée VBL et la fréquence des trames rayées sur un STE réel. Modifier
la lecture du compteur ou avancer globalement les écritures pour effacer
cette trame masquerait une séquence reproduite par les deux témoins.
Traces, captures et plans : `build/vroom-intro-fix/` (non versionné).

**Game Over à zéro point : défaut du média Elite, reproduit dans Hatari.**
L'ancien pilotage `vroom_elite_ste_lap.joy` transposé de 2224 trames
finit à zéro point dans NeoST, contre 60 dans Hatari. Leurs issues
initialement différentes ne comparaient donc pas le même état du jeu.
Le contre-exemple `vroom_elite_tos206_gameover_zero` conserve les
entrées du menu, puis accélère sans braquer (`F*20 .*2964 U*14000`,
départ 6040) : Hatari affiche lui aussi les bandes et deux bombes à 22000.
Ce témoin ne modifie ni le disque ni la mémoire du jeu.

Les captures RAM et le désassemblage donnent la chaîne causale suivante
(image SHA-256 `4c2f63befd89e4a55b5867dbc744a93bc2210504889ce48f18736de6635461a2`) :

- Le chargeur `$1E670` lit piste 78, face 0, secteur 1 ; `$1E6AA` copie
  108 octets à `$73E0`. Dans les deux émulateurs, ils sont identiques aux
  octets bruts de l'image à l'offset 798720, sans validation.
- `$40D0` lit le score courant à `$74B8`. S'il est nul, `$40D6` saute
  directement à `$4170`, sans remplacer le classement. Avec 60 points,
  Hatari passe par `$4162` et remplace les valeurs ; ce chemin masquait
  le défaut du média dans la première comparaison.
- Avec zéro point, `$4216` lit `$9786EB66` dans la dernière entrée
  (`$7400`). La division non signée par 10000 à `$117CE` déborde ; la
  valeur `$EB66` devient un indice de glyphe. `$13C24` écrit alors à
  `$FFC97A` dans l'ancien parcours (`$FFC976` sans braquage), ce qui
  déclenche l'erreur de bus. Le handler `$12E34`
  exécute `RTE` sans supprimer la cause.

Le cœur CPU/FDC n'est pas modifié pour masquer ce défaut logiciel.
Une expérience sur une **copie jetable** a aussi montré que réinitialiser
les 108 octets directement sur disque casse le chargement. Leur rôle
exact dans ce chargement reste à isoler ; les traiter comme un bloc de
scores indépendant n'est pas une réparation sûre. L'original reste intact ;
aucun correctif de l'image n'est livré. La fréquence matérielle du plantage
reste non mesurée.

Quatre diagnostics **snapshot** séparés conservent la trame rayée, son
rétablissement et les bombes après Game Over (ancien pilotage et accélération
sans braquage). Leur succès signifie
uniquement « défaut/état reproduit », jamais « compatibilité validée ».
Le contre-exemple Hatari montre les mêmes bombes mais pas les mêmes bandes
(44,93 % de pixels différents à 22000, fenêtre ±40) ; aucun accord pixel
n’est revendiqué. La nouvelle garde est donc native, elle aussi :

```sh
python3 tools/run_game_replay.py --only vroom_elite_tos206_ste_race
python3 tools/run_game_replay.py --manifest tests/replays/vroom_laps.json --only vroom_elite_tos206_ste_lap
python3 tools/run_game_replay.py --manifest tests/replays/vroom_elite_tos206_diagnostics.json --only vroom_elite_tos206_intro_raster,vroom_elite_tos206_intro_recovery,vroom_elite_tos206_gameover,vroom_elite_tos206_gameover_zero
```

Pour observer chaque trame autour du défaut, reprendre les arguments du
diagnostic `intro_raster`, porter `--frames` à 8531 et ajouter
`--shot-from 8470 --shot-every 1 /tmp/elite-intro-`.
Commandes exactes, captures, traces et contre-épreuves de cette passe :
`build/vroom-goal/elite206-finish/` (non versionné).
L'enquête Game Over, ses captures RAM et le témoin Hatari à zéro point
sont dans `build/vroom-gameover-fix/` (non versionné).

### Croisement des deux images et des deux TOS (2026-11-05)

**Les échecs de chargement et d'arrêt audio de cette campagne concernent
l'image sans suffixe.** Comparaison native depuis reset : STE 1 Mo, TOS UK
1.62 puis 2.06, FDC normal, graines 1 à 8 pour chaque image et chaque TOS
(32 combinaisons). Entrées communes : SPACE à 2000 et 3008, tenu 8 trames,
Fire à 6040, tenu 20 trames. Captures à 6000 et 7000 ; les menus encore en
cours de dessin sont revérifiés depuis reset jusqu'à 8500, sans nouvelle
entrée. Les captures sont inspectées : un code de sortie zéro ou l'absence
de halt ne suffisent pas à compter un succès.

| Image | TOS 1.62 UK | TOS 2.06 UK |
|---|---|---|
| `Vroom (1991)(Lankhor).st` | 2/8 atteignent le menu ; 6/8 affichent deux bombes avant Fire, graines 3–8 | 7/8 atteignent le menu ; graine 5 : titre correct, puis bruit, bombes et halt après Fire |
| `Vroom (1991)(Lankhor)[cr Elite].st` | 8/8 atteignent le menu | 5/8 atteignent le menu avec les entrées communes ; graines 2, 4 et 5 restent dans l'intro, puis atteignent le menu après les entrées supplémentaires ci-dessous |

Pour les trois intros Elite sous 2.06, rejeu depuis reset avec SPACE aussi à
7008, 8008 et 9008, puis Fire à 10040 : **3/3 au menu à 12000**, sans halt.
Ce résultat écarte un blocage persistant de l'intro dans ces essais, mais ne
compte pas comme un succès avec le script initial ni comme un essai audio à
la même phase. **Contre-épreuve sans nouvelles entrées : Elite/2.06/graine 5
affiche une intro rayée à 8500, sans halt.** Un autre rejeu sans nouvelle
entrée, jusqu'à 12000 pour les trois graines, conserve l'intro intacte aux
captures 8000, 9000, 10000, 11000 et 12000, sans halt. L'aspect rayé à 8500
restait alors à qualifier ; la reprise détaillée ci-dessus l'isole à une
trame et localise les écritures de bordure décalées.
Les appuis supplémentaires ne doivent pas masquer cette observation ; la
cause de l'attente initiale et de l'aspect rayé n'est pas établie par cette
campagne. Le
reproducteur sans suffixe/2.06/graine 5 est également
rejoué avec le seul SPACE à 2000 : captures 6000/7000 identiques à celles du
script commun, même halt. Le second SPACE n'a donc pas créé cet échec.

Le contrôle des fichiers détecte la relocation courte dans
`AUTO\VROOM.PRG` de l'image sans suffixe, aucune sur Elite. Le témoin Hatari
`f0736b24`, patchs TOS/fast boot/fast FDC désactivés, est aussi exécuté sur
les quatre combinaisons à graine 5 : les deux images atteignent le menu sous
les deux TOS à 8500, sans halt observé. La même valeur de graine n'impose pas le
même angle de disque entre les émulateurs ; cette passe n'annule pas les
échecs Hatari de relocation précédemment reproduits avec d'autres graines.

Commandes, scripts de reproduction, captures, journaux, classifications et
empreintes : `build/vroom-goal/cross-version-20261105/`. Les deux disquettes,
les deux ROM et le binaire NeoST ont les mêmes empreintes avant et après.
Aucun patch de compatibilité ajouté. Ces observations portent sur le passage
au menu ; elles ne prouvent ni l'absence de défaut rare sur Elite ni la
fidélité matérielle, et ne remplacent pas les parcours de conduite ci-dessous.

### Parcours de conduite

**Un tour complet rejoué sur chaque image, objectif de compatibilité encore ouvert.**
Les disquettes et ROM d'origine sont inchangées, STE 1 Mo, FDC normal.
Le [manuel Lankhor, pages 4–5](https://www.atarimania.com/st/files/vroom_lankhor_manual.PDF)
décrit les commandes : en mode souris manuel, bouton droit pour monter les
rapports, gauche pour descendre. Le joystick est disponible en arcade.
Rester au point mort après un clic gauche ne démontrait donc aucun défaut.

Le parcours `vroom_elite_ste_race` quitte l'intro puis le titre, choisit
**ARCADE / JOYSTICK / AUTO**, démarre et accélère avec un braquage à droite.
Captures inspectées : menu à 5500, grille à 6750, conduite à 7100 et 7400
(281 km/h). TOS 1.62 UK, graines NeoST/Hatari 1 : écarts respectifs
**0 / 0,48 / 0,02 / 0,34 %**, fenêtre ±40, seuil 0,5 %.
Ce parcours court ne couvre pas un tour complet ; il ne prouve pas la fidélité matérielle.

Le parcours `vroom_tos206_ste_race` utilise l'autre image, TOS 2.06 UK,
graines 5. Fire à 4008, navigation souris puis conduite : 280 km/h à 7400,
CPU vivant des deux côtés. Les écarts sont **0,05 / 0 / 0,10 / 12,35 %**.
La dernière capture reste rouge ; ni fenêtre ni seuil ne sont augmentés.
Le parcours est conservé comme diagnostic de comparaison, à distinguer du
plantage de chargement et de celui décrit ci-dessous.

**Parcours longs natifs** : les deux images atteignent maintenant **Lap 1**,
puis continuent à rouler, avec le cœur courant. Un pilote externe a dirigé le
joystick d'après les captures (virages, tunnel, collisions et reprise de
vitesse). Ses entrées ont été figées, puis rejouées **depuis le reset pendant
15 401 trames**, sans chargement d'état. Les deux images finales sont exactement
celles des parcours pilotés, et les deux CPU restent vivants. Elite : TOS 1.62
UK, graine FDC 1 ; image sans suffixe : TOS 2.06 UK, graine 5. STE 1 Mo, FDC
normal, médias inchangés.

```sh
python3 tools/run_game_replay.py --manifest tests/replays/vroom_laps.json
```

Les deux résultats **SNAP** sont comptés à part des comparaisons Hatari.
Le manifeste conserve les commandes, les empreintes des médias, la taille
et l'empreinte RGB de la scène finale inspectée. Les fichiers `.joy` ne
contiennent que les commandes hôte. Un halt, une capture différente ou
absente, ou une modification des médias invalide ce contrôle. Ce sont des
gardes **snapshot**, pas une preuve matérielle ni une résolution des
collisions de démarrage. Les captures de cette passe sont dans
`build/vroom-goal/race-driving/`.

**Correction du banc** : les essais initiaux pouvaient rendre 0 % alors que
les deux programmes restaient au titre ou au menu. `scene_hashes` exige
désormais, pour ces nouveaux parcours, les captures NeoST inspectées (SHA-256
des pixels RGB, **échelon snapshot**). Cette garde complète l'accord Hatari
de grade D ; elle ne prouve pas le matériel. Une scène différente échoue
même si Hatari affiche exactement la même mauvaise étape.
Le comptage des pixels est corrigé séparément : le maximum des différences
R/G/B remplace la conversion en luminance, qui arrondissait une différence
bleue de 1 à zéro. Les chiffres Vroom ci-dessus restent inchangés ; les
dimensions différentes sont refusées. `check_compare_titles.py` couvre ces
cas et les transitions des boutons du pilote souris.

Un second défaut du banc est reproduit avec le vrai headless sur EmuTOS :
demander les trames **101 et 201** lisait deux fois `n_00200.ppm`. Le pas
était calculé sur les différences entre dates, alors que le headless capture
les multiples absolus du pas. De plus, une capture manquante était remplacée
par la dernière image disponible avant même le contrôle du verdict.
`neost_frames` prend maintenant le PGCD des dates absolues et
`read_neost_frames` exige chaque fichier portant le numéro demandé.
La contre-épreuve lit bien `n_00101.ppm` puis `n_00201.ppm` ; une capture
finale absente échoue. Les dates du manifeste existant étaient divisibles par
l'ancien pas : les cinq rejeux Vroom conservent leurs trois succès et leurs
deux échecs, aux mêmes pourcentages.

`compare_titles.py --mouse TRAME:SCRIPT` transmet `.LRUDlrud123` aux deux témoins.
`--mouse-step 60` permet ici les gestes du menu ; les anciens pas de 8 restent
le défaut. Les scripts qui se chevauchent sont refusés, boutons relâchés en
fin de script. L'oracle ajoute seulement l'entrée relative `mousemove` à
sa FIFO, comme son chemin hôte SDL. Aucun comportement de puce n'est changé.

### Autre échec : une interruption audio relance le timer arrêté

Reproducteur minimal sur l'image sans suffixe, TOS 2.06 UK, STE 1 Mo,
graine 5 : SPACE à **2000**, tenu 8 trames, puis **un seul Fire à 6040**,
tenu 20 trames. À 6000 le titre est correct ; à 6200 apparaissent le bruit
et les bombes, puis le CPU s'arrête. Hatari `f0736b24`, graine 5, atteint
le menu. `vroom_tos206_audio_stop` conserve ce diagnostic **actuellement
rouge**, exclu de la suite normale :

```sh
python3 tools/run_game_replay.py --only vroom_elite_ste_race
python3 tools/run_game_replay.py --manifest tests/replays/archive/vroom_retired.json --only vroom_tos206_audio_stop,vroom_tos206_ste_race
```

La seconde commande doit encore rendre 1. Chaîne causale lue dans le jeu :

1. `$588B2` masque les IRQ avec `ORI #$700,SR`. Le timer A expire pendant
   cette section. La sonde depuis l'état avant Fire donne une échéance à
   +17 cycles ; IPRA passe de `$00` à `$20` pendant l'instruction.
2. `$588BA` arrête TACR. À `$588BE`, TACR vaut 0 mais IPRA vaut toujours
   `$20`. `ANDI #$F3FF,SR` autorise l'IRQ en attente.
3. Le gestionnaire `$58A50` termine le dernier échantillon (`$A2=1→0`),
   passe en `$58976`, choisit l'échantillon suivant et **réarme TACR=1 en
   `$589BC`**. Le programme principal croit la musique arrêtée.
4. La lecture piste 49 / face 0 / secteur 5 recopie 512 octets à `$5888E`,
   écrasant la table audio puis le début du gestionnaire `$58A10`. Une IRQ
   exécute ces données, omet la sauvegarde des registres et dépile un faux
   SR/PC. Les bombes puis la double faute sont des conséquences.

**[B] Motorola MC68901, janvier 1984, §3.3.1–3.3.2, p. 3-3 et 3-5** :
un bit pending s'efface par IACK, écriture IPR ou désactivation du canal
IER. L'arrêt TACR n'est pas un acquittement. Source locale
`extern/datasheets/MC68901_Jan84.pdf`, SHA-256
`5441a092562cc5b55bb167298030647622132e0061c0be8d976e25fb60ba959c`.
Effacer IPRA à l'arrêt du timer serait donc un contournement injustifié.
**[F]** La phase exacte conduisant à cette collision sur le vrai STE reste
à établir. L'absence de plantage chez Hatari à cette graine ne la tranche pas.

### Contre-épreuve des phases audio

Depuis l'état NeoST avant Fire, 81 dates d'appui (6040–6120) atteignent
l'arrêt : 28 ont le bit Timer A pendant, mais seul 6040 termine le dernier
échantillon et réarme le timer. Depuis un état Hatari à 6000, 599 appuis
(6002–6600) atteignent l'arrêt : 177 ont le bit pendant, aucun ne réarme
le timer ; aucun halt n'est observé dans les 200 trames suivantes.
Les plages et les états des deux émulateurs restent distincts : ce ne sont
pas des probabilités de panne sur matériel.

**Audit de la sonde** : les deux relevés de pending sont pris à `$588BE`,
avant `ANDI` qui rouvre les IRQ. Une première sonde Hatari à `$588C2`
voyait déjà l'IRQ acquittée et donnait artificiellement zéro pending.
Le point d'arrêt de relance `$589BC` est également armé avant `ANDI`,
pour ne pas manquer un gestionnaire servi avant le retour au débogueur.

Une exécution depuis le reset confirme le compteur audio au même point
d'entrée `$588B2`, appui 6040 : **1 pour NeoST, 62 pour Hatari**.
Les 19 changements d'échantillon précédents utilisent exactement les mêmes
compteurs, pointeurs de données et positions dans la séquence musicale.
Le décalage des horloges absolues est de **236 032 cycles** au premier,
**235 960** au dernier : seulement −72 cycles de dérive sur 2264 trames
NeoST. L'écart de phase est donc principalement présent dès le démarrage
de la musique ; changer la fréquence du Timer A n'est pas justifié par
ces mesures. L'origine précise de ce décalage initial reste à isoler, avec
les angles FDC et les origines VBL propres aux deux témoins.

La fin de la dernière lecture avant la musique a ensuite été datée :

| Point observé | NeoST | Hatari | Écart Hatari − NeoST |
|---|---:|---:|---:|
| Dernier secteur chargé | 579 247 280 | 579 483 316 | 236 036 |
| Premier `TACR=1`, PC `$5896A` | 579 254 864 | 579 490 912 | 236 048 |

Le traitement après lecture prend donc **7584 / 7596 cycles**, soit 12 cycles
d'écart. Le décalage audio est déjà présent à la fin du chargement ; il ne
naît pas dans l'initialisation du lecteur musical. Les horloges restent
propres à chaque témoin, avec des phases FDC indépendantes.
Calcul NeoST depuis la dernière transition FDC : `indexTime + 1604249 −
delayIndex`, soit `578769990 + 1604249 − 1126959`. Calcul Hatari, même VBL
3616 : `CycleCounter − FrameCycles + video_cyc(fin FDC)`, soit
`579490912 − 86128 + 78532`. Sources des calculs et traces conservées dans
`audio-audit-v2/load-phase.json` et les relevés `load-start-position`.

La variante VBL préarmée hors STOP est reconstruite depuis le cœur courant
et recontrôlée avec **toutes** les entrées du parcours piloté Elite jusqu'à
7400 : elle reste dans l'intro. L'ancien rejet ne dépend donc pas seulement
d'un appui SPACE manqué. Cette variante séparée ne résout pas non plus la
recette d'arrêt audio. Captures : `build/vroom-goal/vbl-recheck/`.

Observations de grade D, cœur inchangé ; scripts, empreintes, relevés et
comparaison des horloges dans `build/vroom-goal/audio-audit-v2/`.

Les trois causes sont distinctes : relocation tronquée au démarrage,
désélection du lecteur pendant une lecture, et relance audio après arrêt.
Les nouvelles recettes ne résolvent pas ces échecs en choisissant simplement
un moment d'appui favorable. Les captures et traces de cette passe restent
dans `build/vroom-goal/` (non versionné).

**Arbitrage matériel encore manquant.** Le reproducteur audio a été relancé
une dernière fois sur le binaire courant : même double faute, médias et
exécutable identiques aux empreintes de l'audit. Les parcours longs réussis
ne permettent donc pas de clôturer l'objectif. Il manque toujours une
mesure des images exactes sur STE avec TOS identifié, puis une chronologie
matérielle pour départager les phases des témoins. La variante VBL qui
bloque Elite et l'effacement injustifié du pending MFP restent exclus.

Le dossier local `build/vroom-goal/hardware-handoff/` contient une nouvelle
génération de `RELOCPRB.ST` par `tools/make_reloc_probe.py`, les empreintes
des deux images et des ROM, et un protocole `README.txt`. La sonde publique
ne contient pas le jeu ; elle mesure uniquement le chargement de la table
de relocation courte. Elle ne tranche pas les collisions audio ou FDC.
Le relevé à faire concerne d'abord un STE 1 Mo sous TOS 1.62, avec la ligne
`INTACT` ou `CHANGED +$…` affichée à chaque essai. Aucun résultat matériel
n'est déduit du fonctionnement de la sonde dans un émulateur.

Validation de cette passe : les contrôles non graphiques de `--tier fast` et
`--tier full` passent, références CPU/GLUE et pixels inchangées. Les deux
orchestrateurs terminent en échec après interruption de leur processus GUI,
bloqué par le bac à sable macOS ; le même boot GUI de 400 trames, relancé hors
bac à sable, termine normalement et sa capture passe `check_ppm_nonuniform.py`.
Les deux anciens rejeux Vroom et le nouveau parcours Elite passent. Les deux
nouveaux diagnostics de l'autre image échouent aux points indiqués ci-dessus.

## Vroom sous TOS 2.06 : la VBL désélectionne le lecteur pendant le chargement (2026-11-04)

**Cause démontrée dans NeoST, cœur inchangé.** L'écran bruité est distinct du
halt de démarrage décrit ci-dessous. La recette initiale a été retrouvée dans
les anciens scripts temporaires `vfix/ste.py`, `vfix/ms.py` et `play4.joy` :
SPACE aux trames 1300, 1600, 2000, 2500, 3200 et 4000, tenu **12 trames** ;
joystick à 1000, capture à 5200. La copie réparée de la relocation reproduit
exactement les taux initiaux, graines 1 à 12 :

- STE 1 Mo : bruit aux graines 2, 5, 7, 9, 11 et 12, soit 6/12 ;
- Mega STE 4 Mo : graines 2, 7, 10 et 12, soit 4/12 ;
- Hatari `f0736b24`, STE 1 Mo, graines 1 à 8 : bruit aux graines 3, 5 et 6.
  Capture NeoST 2 / Hatari 3 à 5200 : **0,0 % d'écart**.

Le titre est correct à 3250, le bruit apparaît avant 3500, sans halt CPU.
La base vidéo reste `$028E00`. La trace du programme explique l'enchaînement :

1. La VBL en `$009DDC` teste le mot `$3A`. Positif, il arme l'arrêt automatique
   du lecteur : le compteur `$9E68` est décrémenté ; à expiration, `$009E04`
   écrit `$07` dans le port A du PSG, désélectionnant les lecteurs.
2. Le chargeur de cette image pose `flock` en `$030C32`, mais laisse `$3A`
   positif. Il ne protège donc pas sa lecture contre sa propre VBL.
   État à 3050 : PC=`$030CE0`, `flock=$FF00`, `$3A=1`, compteur=8.
3. Une lecture du secteur 1, piste 0, lecteur A, est engagée à environ 60987 ms.
   La VBL désélectionne le lecteur pendant l'attente : le chargeur expire,
   essaie B: (absent), puis rend `D0=-2`. `$01B334` poursuit sans tester l'erreur.
4. Le tampon non rechargé à `$05888E` contient encore des données de disque.
   `$01B338` interprète son premier mot `$6000` comme un décalage ; le mot
   `$AB1E` à `$05E88E` devient une longueur. La boucle `$01B344` recopie alors
   `$AB1E` octets vers `$021100`, traversant l'écran en `$028E00`. Les octets
   bruités y correspondent exactement au contenu disque à l'offset `$27900`.

**Contre-épreuve depuis le même état à 3050** : deux reprises de 1500
`runFrame()`, sans nouvelles entrées. Témoin : écran bruité. Variante : une seule
écriture `bus.ram[0x3b] = 0`, qui annule l'indicateur `$3A` sans changer le
compteur, le code ou les données du jeu. Résultat : menu normal. Empreintes écran
respectives `0c23ad2e9ba36c8f` et `2fed95d64b7d5801`.

Le diagnostic a ensuite été reproduit sur **l'image originale non modifiée**,
graines NeoST 5 / Hatari 3 : même bruit, 0,0 % à 5200. Le rejeu utilise désormais
cette image ; aucune préparation ni réparation du média n'est nécessaire :

```sh
python3 tools/run_game_replay.py --manifest tests/replays/archive/vroom_retired.json --only vroom_tos206_loader_collision
```

Le JSON du rejeu contient le script joystick répété 40 fois. La contre-épreuve
historique à 3050 ci-dessus utilisait la copie réparée et `--fdc-seed 2`,
`--key-hold 12`, `--frames 3050 --save-state /tmp/vroom-collision.state`.
Charger cet état dans `Machine`, puis comparer les deux reprises décrites.
`NEOST_WATCH=ff8800` et `NEOST_FDC_DEBUG=1` exposent la désélection pendant la
lecture ; depuis l'état à 3250, une trace avec `--regs --irq` expose le retour
`-2` et la copie. Toutes les exécutions directes utilisent `--disk-ro`.

Le rejeu conserve un **échec connu partagé avec Hatari**, pas une partie réussie.
Marqué `diagnostic`, il est exclu de la suite par défaut ; `--only` le demande
explicitement et son résultat est compté séparément (`DIAG`). Les parcours de
compatibilité utilisent les images originales, sans réparation du TOS ou des
disquettes. La contre-épreuve sur copie décrite ici reste un diagnostic historique.
Le contrôle courant garde le défaut sur l’original ; la copie réparée avait
uniquement servi à isoler la relocation tronquée dans l’enquête précédente.
`compare_titles.py --key-hold` et le champ `key_hold` des rejeux permettent
désormais de conserver la durée d'appui, appliquée aux deux émulateurs et
consignée dans le résumé JSON (défaut inchangé : 8).

**Limite matérielle [F]** : la cause logicielle de cette collision est isolée,
mais sa fréquence sur un vrai STE reste à mesurer. L'égalité avec Hatari ne
prouve pas la chronologie physique ; aucun contournement propre à Vroom n'est
introduit dans le cœur. SHA-256 de la copie réparée :
`94da3e1e16f296dc1d70505ff08a63b33d1348ea1fc0966f412c77ab5f08fcc4`.
ROM TOS 2.06 UK : même empreinte que dans le cas suivant.

## Lemmings : aligner le menu sur son entrée effective (2026-11-04)

**Faux échec de comparaison résolu, aucune correction du jeu ou du rendu.**
Le rejeu `[cr BBC][a]` reste au menu animé ; son script clavier/joystick
n'entre pas dans une partie à la souris. Après la correction FDC, une fenêtre
±30 centrée sur le même numéro de trame donnait 0,75–36,39 % de pixels différents.
Même ±240 ne suffisait pas.

**[D] Traces NeoST et Hatari `f0736b24`, TOS 1.02 UK original, ST 1 Mo**, mêmes
entrées du manifeste et médias inchangés, FDC normal, aucun patch TOS/fast boot :

- Premier passage dans la boucle du menu `$4370` : NeoST trame **2615**,
  Hatari VBL **3159**. Les registres concordent, ainsi que les 56 octets des
  compteurs d'animation à `$14C8A`. Le décalage de chargement vaut **544**.
- La trace NeoST de la trame 4000 et celle d'Hatari à 4544 concordent sur les
  compteurs et le traitement du défilement. Les premières ruptures de flux
  relevées sont des IRQ clavier reçues à des instants différents, les touches
  restant injectées aux dates absolues du manifeste.
- La capture NeoST est prise **après** sa trame indexée à partir de zéro ;
  la capture Hatari est prise à la VBL. À 4000, c'est donc Hatari **4545** qui
  est identique, 0 pixel. Une recherche au pas 2 autour de 4544 ne visite
  que les trames paires et manque cette image.

Le manifeste fixe `offset: 545`, transmis par `run_game_replay.py` au
comparateur. Seuil **0,5 %**, fenêtre **±30**, pas **2** et cinq captures
3000/4000/5000/6000/8000 conservés. Les cinq captures passent à **0 pixel** ; cette validation porte sur
le menu, pas sur un niveau joué. Le choix de l'offset repose sur un même PC
d'entrée et sa convention de capture, pas sur une suppression de pixels gênants.

```sh
python3 tools/run_game_replay.py --only lemmings
```

## TOS 2.06 : la VBL écrase le contexte sur la pile temporaire (2026-11-04)

**Collision finale toujours ouverte ; un écart antérieur de lecture Timer B est corrigé.**
L'enquête Vroom a trouvé un arrêt distinct de l'ancien signalement d'écran bruité :
avec SPACE précoce, le TOS plante avant toute instruction du jeu. La même recette
plante sur la disquette publique `disks/diskA.st`, sans programme Vroom.

Reproduction minimale, STE 1 Mo, TOS 2.06 UK, SPACE tenu huit trames :

```sh
./build/neost-headless roms/tos206uk.img --machine ste --mem 1m \
  --disk disks/diskA.st --disk-ro --fdc-seed 4 --frames 800 \
  --key-hold 8 --scancode-at 100 39 --scancode-at 300 39
```

Résultat : double faute, PC=`$E0FBBC`, SSP=`$E01296`, écriture en `$E012A2`.
Sur les graines 1 à 64, seule la graine 4 halte avec cette recette. Le code de
sortie headless reste zéro : rechercher `CPU halted` dans le journal.

**[C] ROM TOS 2.06 UK**, chemin observé dans les deux disquettes :

1. Le démarrage crée un processus AUTO dont la basepage est `$CCB6`, mais dont
   l'entrée `$E0126A` est en ROM. Après `Super(0)`, `$E0127A` place A7 en
   basepage + `$100`, soit `$CDB6` : la pile temporaire occupe cette basepage.
2. À l'entrée GEMDOS de `Fsetdta`, `$E0FB76` sauvegarde A7=`$CD70` dans
   basepage + `$7C`, soit `$CD32`. `$E0FB7E` doit ensuite charger la pile noyau
   `$31D0`, mais la VBL arrive entre le `lea` en `$E0FB7A` et ce changement.
3. La trame d'exception abaisse A7 à `$CD6A`. En `$E00C16`,
   `movem.l D0-D7/A0-A6,-(A7)` descend jusqu'à `$CD2E` : D1=`$E01272`
   écrase le pointeur à `$CD32`. Sonde mémoire : `$0000CD70 → $00E01272`.
4. Au retour GEMDOS, `$E0FB98` recharge ce pointeur corrompu, interprète des
   octets de ROM comme un contexte CPU et installe A7 en ROM. La première
   écriture de pile déclenche la faute, puis la double faute.

Pour isoler la fenêtre, ajouter à la commande `--break E0FBBC --save-state
/tmp/tos206.state`. Le premier arrêt est normal, trame 344, A7=`$3DD4`, avant
le changement de processus. Reprendre cet état avec `--load-state`, `--frames 30`,
`--trace /tmp/tos206.trace --regs --irq` et `NEOST_TRACE_CYC=1` dans l'environnement.
Sur `diskA.st`, la sauvegarde VBL fautive commence au cycle 55209572.

**Contre-épreuve depuis cet état identique**, dans un pilote C++ lié au cœur :
exécuter 4000 `stepInstruction()`, puis 3000 `runFrame()`, en arrêtant si le CPU
halte. Témoin : double faute. Variante : avant chaque PC=`$E0FB76` pendant ces
4000 instructions, mémoriser les bits IPL du SR et les monter à 7 ; avant
PC=`$E0FB84`, restaurer ces seuls bits, en conservant les autres bits courants du
SR. Quatre fenêtres protégées, aucun halt, retour au bureau avec `diskA.st`.
Aucun octet de ROM, de disque ou de RAM n'est modifié par la sonde. Ce masquage
sert au diagnostic, pas à corriger l'émulation.

**Reprise du 2026-11-04 : le cas reste ouvert.** La correction de cadence DMA,
la conversion d’horloge SCLK sur STE et l’ancrage des attentes d’index ne
suppriment pas le halt à graine 4. Hatari `f0736b24`, graines 1 à 256, même
recette avec `--patch-tos off --fast-boot off` : aucun halt. Ce contrôle exclut
le patch de checksum ROM d’Hatari comme explication suffisante. Le masquage
spécifique à cette adresse TOS reste une contre-épreuve, pas un correctif.

**Traces appariées du 2026-11-04**, reprises depuis un boot neuf du binaire
courant : même ROM, copie identique de `diskA.st`, STE 1 Mo, SPACE 100/300
pendant huit trames, aucun patch TOS ni fast boot/FDC. Graines indépendantes
NeoST/Hatari 4/4 : ce même nombre ne garantit pas le même angle de disquette.
Depuis `$E01240`, les **1190 premières instructions et les registres disponibles
concordent**. Première divergence de flux : après `$E0FB7A`, NeoST entre dans
la VBL `$E00C06` au cycle 55 209 510 ; Hatari exécute `$E0FB7E` au cycle
55 811 124. Le décalage de chronologie existe déjà à `$E01240`
(55 194 832 contre 55 796 504), pas seulement au point de prise d'IRQ.
Les petits écarts de datation internes aux entrées d'exception ne sont pas une
égalité cycle par cycle. Aucun correctif CPU n'est déduit de ce seul alignement.

Le comparateur `tools/trace_diff.py` sait désormais lire le mélange Hatari
`cpu_disasm,cpu_regs,cpu_video_cycles` : le dump de registres décrit le PC
suivant et contient un aperçu désassemblé, qu'il ne faut pas compter comme
une seconde instruction exécutée. Les opérandes `D0` ne sont plus prises pour
la valeur du registre dans la trace NeoST. Quatre contrôles de non-régression
complètent le banc du comparateur. L'audit des patchs ROM/RAM et leur effet sur
la chronologie figurent dans `HATARI_AUTOMATION.md`, section « Patchs TOS ».

**Contre-essais du 2026-11-04, reprise ciblée TOS 2.06.** Aucun des changements
ci-dessous n'est retenu dans le cœur :

| Essai | Résultat | Motif du rejet |
|---|---|---|
| `NEOST_RAISE_COMMIT=1` : VBL sondée par le CPU après son dispatch | La graine 4 démarre ; l'intro Vroom Elite STE reste affichée après SPACE 2000, aux trames 3000 et 4000 | Régression du rejeu `vroom_elite_ste` : 70,98 % de pixels différents du titre Hatari. |
| Broche VBL préarmée hors STOP, retard IPL de 3 cycles | La graine 4 démarre, mais Vroom Elite régresse aussi | Le banc CPU + GSTMCU régresse : en PAL, HBL NOP 176/256, NOPs 178/256, MULU 101/256 lectures identiques au RTL ; STOP 256/256 et VBL 32/32 restent identiques. Un retard uniforme ne convient pas. |
| Conversion des échéances FDC absolues entre SCLK et horloge bus | Même double faute | Le démarrage rejoint encore `$E01240` au cycle 55 194 832. |
| Champs ID `.st` datés depuis l'index, plutôt que depuis le callback retardé et une position arrondie à l'octet | Même double faute et même cycle d'entrée `$E01240` | Cette approximation FDC mérite son propre contrôle, mais ne résout pas ce cas. |

Hatari sans patch, graine 4, a également été essayé avec **129 couples d'appuis** :
SPACE aux trames `k` et `k+200`, pendant huit trames, pour `k=4,8,…,516`.
Aucun halt. Ces essais ne constituent pas 129 phases distinctes : plusieurs
appuis précoces conduisent à la même date d'entrée dans AUTO. Le témoin NeoST
sans aucun appui, graine 4, atteint 2000 trames sans halt ; ce n'est pas un
correctif de la recette avec SPACE précoce.

Le fragment original `$E0FB6A-$E0FB83`, exécuté sur les deux cœurs nus,
produit les mêmes 66 accès sans interruption. Une IRQ 4 balayée autour de la
sauvegarde du contexte provoque l'écrasement de `$CD32` **aussi dans fx68k**
(`0602ee46`), avec un gestionnaire qui sauvegarde puis restaure les quinze
registres comme la VBL. Ce témoin ne reproduit ni la carte mère ni le boot
complet : il confirme le risque architectural du chevauchement, sans valider
la phase retenue par NeoST.

**[F]** La ROM démontre le chevauchement possible des zones mémoire. Elle ne
prouve pas que les dates FDC/VBL/CPU de NeoST permettent cette collision à la
bonne fréquence : un relevé sur un vrai STE reste nécessaire. L'essai Vroom
précoce à graine 4 ne plante pas chez Hatari `f0736b24` ; les graines et les
chronologies des deux émulateurs ne sont pas équivalentes.

**Traces remontées jusqu'au reset (2026-11-04).** Le nouveau pilote
`tools/trace_tos206_boot.py` conserve deux fenêtres appariées, `reset` et `auto`,
avec commandes, configuration Hatari effective et empreintes des sources.
Avant toute commande FDC, le TOS configure Timer B en compteur d'événements
(`TBCR=8`) et lit `$FFFA21` en `$E013EC/$E013F6`. Avant correction, le premier
écart de registres arrive à l'instruction 6669, puis le flux diverge à
l'instruction 15541 : NeoST reste dans le `dbra` en `$E013FA`, Hatari repart
en `$E013F0`, autour du cycle 171 100.

Le défaut est dans `Scheduler::runMfpTimersTo` : les accès MFP anticipaient
les quatre timers délai échus, mais excluaient le front DE de Timer B. Une
lecture au milieu d'une instruction pouvait donc rendre le compteur précédant
un front déjà passé. Le masque inclut maintenant `TIMER_B`, sans dispatcher
HBL, VBL ou le rendu. **[B] MC68901, janvier 1984, § 5.1.3 p. 5-3** : chaque
transition active de TBI décrémente le compteur. **[D] Hatari mfp.c:2536-2590
@ f0736b24** : en mode cycle-exact, ce front est traité avant la lecture de TBDR.
La capture exacte du registre au signal DS reste une question distincte.

Après correction, les **25 491 PC concordent** jusqu'au retour `$E000A8`.
Les registres ne sont pas tous identiques : premier écart de SR à l'instruction
6761 ; un écart de deux cycles existe déjà depuis le premier accès RAM en
`$E00BB2`. Ce résultat ne prétend donc pas à une égalité cycle par cycle.
Le test de lecture autour de DE, sans dispatch global, détecte douze échecs
avec l'ancien cœur et aucun avec le nouveau ; il vérifie aussi la recharge,
l'IRQ et l'absence de double comptage au dispatch suivant.

La capture `auto` fraîche confirme encore les **1190 instructions concordantes**
puis la VBL en `$E00C06` contre `$E0FB7E` chez Hatari. Le correctif Timer B
ne suffit donc pas à fermer la collision de pile à graine 4. Artefacts de cette
reprise : `build/tos206-paired-reset-audit/` et `build/tos206-paired-auto-audit/`.

**Séparer les phases disque des horloges (même reprise).** La trace FDC montre
des index initiaux différents malgré les graines 4/4. Dans un binaire NeoST de
diagnostic seulement, remplacer les trois dates d'index par celles relevées
chez Hatari (`37031232`, `45959357`, `47174628`) ramène l'entrée AUTO à
55 796 436 cycles, contre 55 796 504 chez Hatari : écart de 68 cycles au lieu
de 601 672. Les **1287 instructions et registres disponibles concordent**, et
la collision disparaît. Cette substitution d'état rotationnel sert à isoler
les causes ; elle ne constitue pas une correction de l'émulateur.

L'essai inverse, avec les deux index NeoST (`36296002`, `45351085`) dans une
copie instrumentée d'Hatari, rapproche AUTO à 52 cycles mais laisse la VBL
plus précoce. Les traces vidéo révèlent une anomalie supplémentaire du témoin :
des trames PAL de **160244 cycles au lieu de 160256**, sans écriture de mode.
**[D] Hatari `video.c:4984-4993`, `cycles.c:315-320`, `cycInt.c:385-395`
@ `f0736b24`** : l'échéancier utilise l'horloge immédiate incluant `currcycle`,
mais `Video_InterruptHandler_VBL` recale `VBL_ClockCounter` sur l'horloge globale
qui l'exclut. Exemple de la trame 40 : global=6331350, immédiat=6331362,
dépassement=2, origine=6331280 ; la période perd exactement les 12 cycles
internes. Dès les trames NTSC 1 et 2, le même mécanisme perd quatre cycles.

Contre-épreuve dans la **copie temporaire d'Hatari uniquement** : employer
`Cycles_GetClockCounterImmediate()` pour l'origine et soustraire ces cycles
internes du compteur vidéo historique. Les origines deviennent celles de
NeoST : 133604, 267208, 401832, puis période PAL constante de 160256 ; à la
trame 345, origine=55209384 dans les deux modèles. Aucun changement de ROM,
disque ou RAM invité. Cette dérive d'Hatari n'est pas portée dans NeoST.

Avec cette correction expérimentale du témoin et les index NeoST, AUTO reste
60 cycles plus tard chez Hatari (55194892 contre 55194832). Sa VBL interrompt
après `$E0FB6A`, avant la sauvegarde du pointeur : pas de corruption.
65 décalages du second index de −2048 à +2048 cycles, par pas de 64, donnent
la même entrée AUTO et aucun halt ; ils ne couvrent donc pas 65 phases CPU
distinctes. **La collision finale reste ouverte**, et ces contre-épreuves ne
prouvent pas sa fréquence sur un vrai STE.

**Résidu localisé dans la sortie d'attente 200 Hz (reprise suivante).**
Une capture des trames 340 → entrée AUTO, avec le même témoin expérimental
(index NeoST et correction d'origine VBL), montre que le décalage précède AUTO :

| Point du TOS 2.06 UK | NeoST | Hatari expérimental | Écart Hatari − NeoST |
|---|---:|---:|---:|
| Timer C, entrée `$E03662` qui fait expirer l'attente | 55022618 | 55022696 | +78 |
| Attente expirée, `$E01730` | 55022760 | 55022824 | +64 |
| Premier passage du balayage mémoire `$E0140E`, A0=$100000 | 55023088 | 55023152 | +64 |
| Sortie du balayage `$E01444`, A0=$400 | 55148790 | 55148850 | +60 |
| Entrée AUTO `$E01240` | 55194832 | 55194892 | +60 |

**[C]** `$E0171E` compare D0=$3F3F au compteur `$4BA` ; `$E01722`
prend BCS vers `$E01730`, qui rend −1. Chez Hatari, `$4BA` vaut alors $3F40.
La sortie suit donc l'expiration de l'attente, pas une nouvelle fin de DMA.
Les deux modèles parcourent les 2046 mêmes valeurs d'A0 dans le balayage mémoire.
Les 832 dernières instructions exécutées avant AUTO ont les mêmes PC et un
décalage constant de 60 cycles. Cela ne signifie pas que tout l'état est identique.
La dernière VBL du balayage atteint `$E00C0C` au même cycle 55049292,
mais avec A0=$C9800 contre $C9A00 : égalité de date, état différent.

**[D/F]** La prochaine mesure est l'échéance Timer C depuis son armement,
puis le délai échéance → requête MFP → entrée CPU. Les entrées du gestionnaire
seules ne distinguent pas ces trois causes. Aucun retard artificiel n'est ajouté
au cœur et la collision de pile n'est pas déclarée corrigée.

Capture livrée reproductible (témoin Hatari habituel, sans alignements expérimentaux) :

```sh
python3 tools/trace_tos206_boot.py --phase preauto --out build/tos206-preauto-new
```

`checkpoints.json` conserve chaque occurrence des points ci-dessus avec cycles et
registres disponibles ; les fenêtres commencent à la trame 340 de chaque modèle,
pas au même PC. Une divergence de flux initiale y est donc attendue, et le code
retour 0 signifie uniquement que la capture a réussi. Les captures expérimentales,
leur pilote et leurs empreintes sont dans `build/tos206-residual/` ; la capture
avec le témoin habituel est dans `build/tos206-preauto-capture/`.

**Armement Timer C et contre-épreuve VBL (reprise « corrige »).**
Les sondes distinguent maintenant l'armement de la prise d'IRQ : Timer C démarre
au cycle **5250702** dans NeoST, **5250790** dans le témoin expérimental, soit
88 cycles d'écart avant sa première expiration. Le résidu ne démontre donc pas
un défaut dans le rechargement du timer. Pendant les sondages matériels du TOS
($E00402, $E00418, $E004C0, $E00584), chaque bus error coûte environ 60 cycles
supplémentaires dans NeoST : c'est son délai STE issu du modèle GSTMCU, qui
ne doit pas être supprimé pour aligner le boot sur Hatari. Une contre-épreuve
avec la conversion MFP d'Hatari (8 021 247 Hz au lieu de 8 021 248) conserve
la double faute ; aucune modification d'horloge n'est retenue.

La nouvelle sonde `tools/run_vbl_entry_probe.py` relève le PC empilé par trois
VBL sur STOP puis seize VBL dans une boucle LEA/MOVEA/NOP/CMPI/BLE, sans TOS.
**[D] fx68k @ 0602ee46 + GSTMCU @ 4a6db7cc** : **16/19** PC concordent
avec le cœur livré ; les occurrences zéro-indexées 5, 7 et 18 diffèrent.
Préarmer la VBL hors STOP dans un binaire temporaire donne **19/19**, mais
**la double faute TOS persiste et Vroom Elite reste bloqué** (70,98 % de pixels
différents au titre, trames 3000 et 4000). Cette variante n'est pas retenue.
La sonde retourne volontairement 1 tant que le désaccord subsiste ; elle n'est
pas ajoutée à la suite verte et ne transforme pas cet échec en référence acceptée.

Les preuves supplémentaires sont dans `build/tos206-timerc/`,
`build/tos206-timerc-rate/`, `build/tos206-vbl/` et `build/vbl-entry-probe/`.
Les sources instrumentées sont séparées du cœur livré. **Aucun correctif de la
collision validé à ce stade** ; le désaccord sur le PC interrompu et la compatibilité
Vroom doivent être résolus ensemble, puis la recette TOS vérifiée de nouveau.

Scripts et sources instrumentées conservés dans
`build/tos206-investigation/phase-and-clock/` ; captures dans
`build/tos206-paired-fdc/`, `build/tos206-paired-origins/`,
`build/tos206-hatari-video/0/` (instrumentation seule) et
`build/tos206-hatari-clock/0/` (contre-épreuve d'horloge).
Validation du cœur livré après Timer B : **suite `--tier full` verte,
17 rejeux de compatibilité verts**, dont Vroom Elite et Switchblade ; balayage
TOS 2.06 de 64 graines, seul halt à la graine 4, aucune erreur d'exécution hôte.

Le bruit Vroom est expliqué dans le cas précédent : la recette initiale a été
retrouvée dans les scripts temporaires après cette enquête. Avec la copie réparée,
SPACE à partir de 900,
joystick à 1000 et 8000 trames, aucun bruit sur 12 graines STE 1 Mo et 12 graines
Mega STE 4 Mo. Les rejeux `vroom_tos206_ste` et `vroom_tos206_megaste` utilisent
l'image originale, graines indépendantes 1/1 : menu à 4000, entrée en course
à 6000 et 8000, respectivement 0,0 % et au plus 0,05 % d'écart avec Hatari.
Ils ne valident ni une course complète ni toutes les phases de démarrage.

Empreintes SHA-256 de la reproduction publique :

- ROM : `b776d5ade5ace0d09cb9f13a85ac07f6d492ab69fcac1b56995573f620f74e6c` ;
- `diskA.st` : `aacabef42939d2217afd0482965b7a6ff5789b58fdc296922f6a18b06f390197`.

**Verdict du 2026-11-10 : course réelle de la ROM, fréquence due au tirage discret.**
Même recette (STE 1 Mo, `diskA.st`, ESPACE 100/300 tenu huit trames) :

| Émulateur | Halts | Graines |
|---|---|---|
| NeoST `84c12d1` | 3 (graines 4, 69, 161) | 256 |
| Hatari `f0736b24` sans patch ni démarrage rapide | 0 | 1 024 |

La différence est significative, mais elle ne désigne pas un défaut de NeoST. La fenêtre
exposée va de `$E0FB76` (A7 écrit en basepage+`$7C`) à `$E0FB7E` (passage sur `$31D0`) :
le seul `lea`, une quinzaine de cycles. Le processus AUTO ne la traverse que **deux fois**
par démarrage avec A6 = `$CCB6`, à 8 420 cycles d'écart. Si la phase de la VBL y était
uniforme, le taux serait de l'ordre de 2 × 16 / 160 256, soit 2 × 10⁻⁴.

Elle ne l'est pas : le premier passage suit un tic du Timer C, à 200 Hz. Mesuré sur 72
graines (distance à la dernière VBL prise, `--trace --irq --regs`), il ne prend qu'une
trentaine de valeurs, groupées vers 33 000, 72 000, 112 000 et 152 000 cycles. Une même
valeur revient à l'identique (112 650 pour les graines 13, 15, 20 et 22). Les trois graines
qui haltent partagent **une seule trajectoire** : 151 750, puis 160 170, à 86 cycles de la
VBL suivante. Ses voisines, 151 578 et 151 918, passent à côté.

Pourquoi un réseau discret : chez NeoST, l'horloge du MFP part du front 0 au cycle CPU 0 et
garde un rapport exact au CPU (31 333/9 600, `src/io/Mfp.cpp`, [F] déjà consigné). La phase
du Timer C par rapport à la VBL est donc une fonction déterministe de la durée du boot, que la
graine FDC ne fait varier que par crans. Hatari tire un autre réseau, qui ne contient pas de
trajectoire touchée. Sur le fer, MFP et CPU ont chacun leur oscillateur : la phase n'a aucune
raison d'être figée, et le taux attendu redevient celui de la phase uniforme.

**Conséquence** : le mécanisme est réel (grade C pour la ROM ; fx68k reproduit la collision,
cf. plus haut). Le taux de NeoST mesure son réseau de trajectoires, pas le matériel. Aucun
correctif d'émulation.

**Phase et rapport des horloges, mesurés (2026-11-12).** Deux options de session explorent
désormais ce que le fer explore à chaque allumage (`DEV.md` § Modèle d'horloge) :
`--mfp-phase-seed N` (front 0 de l'horloge MFP à `p/64` de cycle MFP, `p` tiré de la graine)
et `--quartz` (rapport CPU/MFP). Même recette, graines FDC 1 à 256 :

| Horloges | Halts | Graines |
|---|---|---|
| quartz PAL, phase 0 (défaut) | 3 (4, 69, 161) | 256 |
| quartz PAL, phases tirées des graines 1 à 6 (43, 59, 19, 8, 1, 14 sur 64) | 3 (4, 69, 161) dans chaque cas | 6 × 256 |
| quartz 32,0424 MHz, phase 0 | 0 | 256 |
| quartz 32,215905 MHz, phase 0 | 0 | 256 |

La phase seule ne change rien : elle ne décale un départ de timer que d'au plus un cycle MFP
(≈ 3,26 cycles CPU), et la prise d'IRQ sur STOP arrondit à 4 cycles (sonde Timer A : premier
IRQ au cycle 167 798 ou 167 802 selon la phase). L'état de la machine change (empreintes
`--arch-hash-every` distinctes à la trame 340), pas la trajectoire de la course. Le **rapport**
la déplace : Timer C armé vers le cycle 5,25 M, la fenêtre vers 55 M, un écart relatif ε déplace
la phase du Timer C de ε × 50 M cycles, soit ~66 000 cycles à −0,13 % : un autre réseau, qui ne
contient plus la trajectoire touchée, comme celui d'Hatari. Sur le fer, les quartz du CPU et du
MFP s'écartent de quelques dizaines de ppm d'une machine à l'autre (relevés HWTST, 8 020 7xx à
8 021 2xx Hz sur des STE PAL), soit ~50 cycles par ppm sur ce trajet : bien plus que la fenêtre
de 16 cycles. Le taux matériel attendu reste celui de la phase uniforme (~2 × 10⁻⁴). Une graine
de tolérance du quartz MFP (±ppm) serait l'option qui le reproduit ; elle n'est pas posée.

## Switchblade : une VBL du TOS acquitte le READ TRACK du chargeur (2026-11-04)

Image `Switchblade (1989)(Core Design)[cr Replicants][t +2].st`, ST 1 Mo, TOS 1.04 UK.
**Reproduction à graine 8 débloquée par la correction FDC/DMA (2026-11-04).**
Le cœur ajoutait les 32 cycles de vol de bus CPU au délai du prochain octet
FDC. Le flux disque continue désormais pendant le DMA, sans modification de
`flock`, de ROM ou de disquette. Le détail ci-dessous décrit la trace avant
correction ; il reste utile pour comprendre la vulnérabilité du chargeur.

- **[C] ROM TOS 1.04 UK** : la VBL appelle `$FC1360`. En `$FC136A`, elle teste le mot
  `flock` à `$43E` ; s'il est nul, une VBL sur huit lit le statut du lecteur pour sa
  protection en écriture. En `$FC13A0`, `move.w (-$79FC,A5),D0`, avec A5 nul, lit
  `$FFFF8604` et rend `$80` lors de l'échec observé.
- **[B] WD177X-00, p. 1-5** : lire le statut efface INTRQ. NeoST le fait, comme
  `fdc.c:4900-4909 @ f0736b24` d'Hatari ([D]). Conserver l'IRQ malgré cette lecture
  contredirait la fiche.
- Le chargeur attend en `$010096` (`btst #5,$FFFFFA01`) avec SR=`$2300`, donc la VBL
  peut l'interrompre. Il laisse `flock` nul. Graine NeoST 8 : fin du READ TRACK de
  la piste 79 pendant cette VBL, lecture TOS au cycle d'instruction 477324620,
  retour à une boucle dont l'interruption a déjà disparu.
- **Contre-épreuve à état initial identique** : sauvegarde après 2978 trames
  (horloge 477163436, PC=`$01009E`). Reprise sans modification jusqu'à 4000 :
  PC=`$010096`, écran noir. Même reprise en posant seulement `bus.ram[0x43e]=0xff`
  avant le prochain `runFrame()` : titre, PC=`$054F44`. Aucun octet de ROM, de
  disquette, de commande DMA ou de code du chargeur n'est changé. L'empreinte écran
  passe de `b22c5bc0d03d9783` à `fe1fc117383f3eb9`.

Une copie locale préexistante `[flock fix]` remplace les six octets à l'offset disque
`$306E` par `50 F8 04 3E 4E 71` (`st $43e.w; nop`). Elle passe aussi à la graine 8,
à 0,0 % d'Hatari aux trames 3500 et 4000. Cette variante remplace également une
écriture du mode DMA : la contre-épreuve à un seul octet ci-dessus isole mieux la cause.
Aucune image du jeu n'est modifiée par cette enquête ni distribuée dans le dépôt.

La campagne antérieure de `nuit/todo` (commit `73ccc78`, avant la correction
FDC/DMA) complète cette trace : le journal INTRQ relève désormais aussi le PC ;
à la graine 8, la lecture TOS efface l'interruption 55 instructions après sa
montée. Sur 96 graines par TOS, elle rapportait deux écrans noirs sous 1.04
et un sous 1.06 chez NeoST, un sous 1.02 chez Hatari. La copie avec `flock`
posée atteignait le titre sur 96/96 graines sous 1.04. Ces nombres décrivent
les anciens exécutables et ces images, pas une fréquence sur machine réelle
ni le cœur courant ; le correctif de cadence a depuis débloqué la graine 8.

Recette de non-régression de l’ancien échec (SPACE tenu huit trames à chaque date) :

```sh
./build/neost-headless roms/tos104uk.img --machine st --mem 1m \
  --disk 'disks/st/Switchblade (1989)(Core Design)[cr Replicants][t +2].st' \
  --disk-ro --fdc-seed 8 --frames 4000 --key-hold 8 \
  --scancode-at 900 39 --scancode-at 1200 39 --scancode-at 1600 39 \
  --scancode-at 2000 39 --scancode-at 2500 39 --scancode-at 3200 39 \
  --probe flock=43e:2 --probe-every 100 --screenshot /tmp/switchblade.ppm
```

Pour dater la lecture, ajouter `NEOST_TRACE_CYC=1` devant la commande et
`--trace-from 2977 --trace /tmp/switchblade.trace --regs --irq`, en limitant à
`--frames 2982`. Pour la contre-épreuve, arrêter à `--frames 2978` avec
`--save-state /tmp/switchblade.state`, charger cet état dans `Machine`, puis
exécuter 1022 `runFrame()`, avec ou sans la seule écriture RAM indiquée.

Hatari épinglé, graines 1 à 64 avec la même recette : 64 titres, aucun écran noir
à la trame 4000. Cela ne réfute pas la collision : ses phases de rotation et de VBL
diffèrent de celles de NeoST. La contre-épreuve ne dépend pas d'Hatari.

`switchblade_title_st` garde le parcours jusqu'au titre aux graines NeoST 8 / Hatari 1.
Les validations du correctif repartent du reset, avec les médias originaux.
Graines NeoST 1 à 64 : 64 titres à la trame 4000, aucun halt et zéro pixel
d’écart avec le titre Hatari normalisé à 416×276. Le rejeu à graine 8 passe
également aux trames 3500 et 4000. Cette validation ne couvre pas une partie.
Les graines sont indépendantes : même numéro ne veut pas dire même angle ou même
chronologie. `compare_titles.py --fdc-seed 8 --hatari-seed 1` contrôle désormais le correctif.
**[F]** Sans relevé de ce disque sur un vrai ST, ni la probabilité de collision ni
l'exactitude des dates de fin READ TRACK ne sont établies.

Empreintes SHA-256 de la recette :

- disque : `1492a39ac6a8cf9b27461e818ffaffebb109bb064187a8050d715078d87a7a72` ;
- ROM : `c005b0ae8ccd5bf2569add2a0d0a42583b2fb70eec3e1202d82311e96664a88e`.

## Cubase Lite « figé » au chargement d'un morceau : le câble MIDI de bouclage (2026-08-21)

Cubase Lite (CB_LITE.PRG 1996, MROS) démarre et fonctionne sous TOS 1.04 (sous EmuTOS il
panique : MROS s'accroche à des internes non documentés du TOS — incompatibilité EmuTOS
connue, pas un bug NeoST). Mais charger `JAMMER.ALL` **figeait** l'écran.

Recette : save-state au gel (F5), rechargé en headless (`--load-state … --trace --irq`). Le CPU
n'était pas bloqué : **vecteur `$46` (ACIA) toutes les ~300 instructions**, et dans le handler
MROS **73 passages RX (statut `$83`) pour 73 TX** — chaque octet MIDI émis revenait aussitôt en
MIDI IN. Cause : NeoST émulait **en permanence** un câble de bouclage MIDI OUT→IN (pour le test
« M MIDI » du diagnostic Atari) ; avec le **MIDI Thru** de Cubase, tout octet reçu est ré-émis →
larsen infini, l'interface n'a plus de temps CPU. Sur un vrai ST rien n'est branché par défaut.

Correctif : fiche de bouclage **débranchée par défaut**, optionnelle (`MidiAcia::setLoopback`,
`--loopback` en headless — qui branche aussi celle du RS-232 —, menu *Machine → MIDI loopback
cable* et `midi_loopback=` dans le GUI). Vérifié : le même save-state rechargé câble débranché
→ 28 IRQ ACIA en 300 trames (la file TX se vide), 0 réception, MROS et le TOS retombent dans
leurs boucles d'attente. Leçon : un « gel » avec IRQ qui pleuvent = chercher la **source**
externe (ici un câble virtuel), pas le CPU.

## Le réflexe qui tranche le plus de cas : la RAM (puis la ROM)

Beaucoup de titres 1989+ et la plupart des cracks/dépaqueteurs exigent **1 Mo** ; en
512 Ko ils ne râlent pas, ils partent en vrille — compteur de recopie négatif, écran noir
figé, ou l'AUTO simplement skippé par le TOS. Symptôme typique : « ça plante juste après
le cracktro ». Cas déjà tranchés ainsi : **Blood Money**, **Beyond the Ice Palace**
(BSS 384 Ko), **Stardust** (STE-only).

Réflexe : rejouer en `--mem 1m` **avant** de tracer, puis confirmer à l'oracle Hatari
(mêmes ROM/disque/touches). Dans ces trois cas Hatari plantait **à l'identique** : la
configuration était en cause, pas l'émulation. Second réflexe : la ROM fixe 50/60 Hz
(suffixe `us` = NTSC, cf. [`../CLAUDE.md`](../CLAUDE.md)).

## Premiers parcours souris : Bubble Ghost et Lemmings (2026-11-10)

`tests/replays/replays.json` garde désormais `bubble_ghost_st` dans sa première salle :
clic à 3500, déplacement droite/bas et clic à 4500. Sous ST, TOS 1.04 UK, 1 Mo,
les captures à 4000, 5000 et 6000 concordent exactement avec Hatari. Le fantôme se
déplace ; la salle n'est pas terminée. Les empreintes RGB des scènes inspectées
empêchent un retour au titre de passer pour une partie.

`lemmings_first_level`, ST/TOS 1.02 UK/1 Mo, conserve les espaces du chargement,
puis clique sur « 1 player » à 4000 et entre dans le niveau à 6000. Les captures
à 8000 et 10000 montrent les marcheurs dans le premier niveau. Le décalage 545,
la fenêtre ±30 et le seuil 0,5 % sont conservés ; aucun niveau terminé ni
compétence attribuée n'est revendiqué. Le rejeu du menu reste distinct.
Ces verdicts sont `hatari`, assortis de garde-fous `snapshot`, sans mesure matérielle.

**Suite du 2026-11-12 : compétence attribuée et niveau terminé.** `lemmings_level1_complete`
reprend la même entrée, puis choisit le Digger dans le panneau (souris à 6150 : un pas à
gauche, 21 vers le bas, clic tenu 4 trames), remonte de 15 pas sur la corniche et clique toutes
les 6 trames jusqu'à 7153. Un seul creuseur est attribué, vers 6750 ; les dix lemmings
tombent dans le puits et rejoignent la sortie : « You rescued 100% », code du niveau 2
`IJJLDNCCCN` ; un clic à 9100 ouvre la présentation du niveau 2 (« Only floaters can survive
this »). Contre Hatari, 0,0 % aux cinq étapes 6800, 7200, 7800 (dans le niveau, décalage 0 ou
−1), 8700 et 9700 (écrans fixes, trouvés 38 à 40 VBL plus tôt chez Hatari) ; empreintes RGB
posées sur les cinq scènes inspectées. Échelon `hatari`, sans mesure matérielle.

Recette du curseur, utile à d'autres parcours : 8 px par pas, 1 pour 1 ; il part du haut de
l'écran, au milieu ; l'amener à un bord fait **défiler le niveau** (ne pas « caler » le curseur
dans un coin). Un clic ne prend un lemming que si le point chaud tombe sur son corps : un ou
deux pas plus haut (16 ou 17 pas de remontée) ne prennent rien ; même à la bonne hauteur, un
clic toutes les 20 trames n'a rien pris pendant 300 trames ; une variante un pas plus bas, clics toutes les 6 trames, attribue quatre creuseurs
et **diverge** d'Hatari (OUT 6 / IN 40 % contre OUT 8 / IN 20 % à 7300, à une trame de phase
près entre les émulateurs), avant que les deux sauvent tout le monde. Le parcours retenu est
celui qui n'attribue qu'une fois.

Le décalage du premier parcours est corrigé au passage : `lemmings_first_level` gardait le 545
du menu, faux une fois le niveau lancé par le clic de 6000. La meilleure image Hatari tombait
alors à 0,4 % (8000) et 0,47 % (10000), marcheurs ailleurs, sous le seuil par la seule petite
taille des sprites ; à 0, 0,0 % (7999 et 9999). Recette :

```sh
/usr/bin/python3 tools/run_game_replay.py --only lemmings_first_level,lemmings_level1_complete
```

Chessmaster arrive à l'échiquier ; les clics isolés ou prolongés sur e2 puis e4 ne déplacent
aucune pièce : le jeu attend un glisser-déposer. Depuis le 2026-11-10, les jetons `l/r/u/d`
(bouton gauche tenu) le permettent : e2-e4 à 4000, réponse e7-e6, accord Hatari à 5000 et 7000.

## Magic Pockets STX sur STE : retour à gauche sensible à l’alignement (2026-11-10)

STE, TOS 1.62 UK, 1 Mo, disque `Magic Pockets.STX` SHA-256
`87c8807f1499c164d1e72be32a59a3fa200edfafd2ae0bf1e178a0014108fd77`,
Hatari épinglé, graine 1. Aucun changement du cœur ni du média.

`magic_pockets_ste` garde les étapes inspectées à 8550 (droite), 8650
(tir) et 8800 (saut) : 0 pixel d’écart, aux VBL Hatari 8553, 8652 et 8802.
Les commandes de jeu commencent à 8500, après le chargement ; les impulsions
précédentes servent à démarrer. Les empreintes RGB empêchent un écran de titre
de valider ce parcours. Cela reste une comparaison Hatari, pas une preuve matérielle.

À 9000, après le retour à gauche, la meilleure capture dans ±60 trames est à
8952 : 29,63 % d’écart. Le diagnostic `magic_pockets_ste_return_diagnostic`
conserve cet échec et reste exclu de la suite normale. Ne pas élargir son seuil.
Une pause de deux trames **côté Hatari seulement**, insérée juste avant les
commandes de jeu à 8500, ramène les quatre étapes 8550/8650/8800/9000 à 0 %,
la dernière à 8979. C’est un contrôle de sensibilité des entrées, pas la preuve
que cette pause représente la datation matérielle correcte. Le désaccord de
scénario est plausible ; aucun défaut matériel n’est établi par cette expérience.
La passe sans entrée à 4000/6000/8000 concorde aussi, sur les écrans vus seulement.

Reproduction des deux parcours enregistrés :

```sh
python3 tools/run_game_replay.py --only magic_pockets_ste
python3 tools/run_game_replay.py --only magic_pockets_ste_return_diagnostic
# Le second doit actuellement signaler l'écart, avec un code de sortie 1.
```

Pour reproduire le contrôle de sensibilité, enregistrer le script suivant dans
un fichier temporaire et l’exécuter depuis la racine (Pillow requis). Il reprend
le script du diagnostic, compile les mêmes masques, puis ajoute uniquement deux
masques neutres dans le flux Hatari à l’index 3500 (5000 + 3500 = VBL 8500).
Les captures et seuils restent ceux de la comparaison normale.

```python
import copy, json, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path('tools').resolve()))
import compare_titles as c
r = next(r for r in json.loads(Path('tests/replays/replays.json').read_text())['replays']
         if r['id'] == 'magic_pockets_ste_return_diagnostic')
original = c.hatari_shots

def delayed(disk, args, work):
    args = copy.copy(args)
    start, source, compiled = args.joy
    data = compiled.read_bytes()
    shifted = work / 'shifted.bin'
    shifted.write_bytes(data[:3500] + bytes(2) + data[3500:])
    args.joy = (start, source, shifted)
    return original(disk, args, work)

c.hatari_shots = delayed
with tempfile.TemporaryDirectory() as tmp:
    joy = Path(tmp) / 'input.joy'
    joy.write_text(r['joy'])
    sys.argv = ['compare_titles', 'tests/out/magic_alignment', r['disk'],
                '--machine', r['machine'], '--rom', r['rom'], '--mem', '1',
                '--frames', '8550,8650,8800,9000', '--window', '60', '--step', '1',
                '--hatari-seed', '1', '--joy', f'5000:{joy}', '--jobs', '1']
    raise SystemExit(c.main())
```

## Fuzion CD Menus 77, 78, 84 : la priorité fixe du MFP suffit (2026-11-10)

Hatari élit, parmi les requêtes MFP d'une même instruction, la plus ancienne
(`mfp.c:998 @ f0736b24`, release-notes : « Fuzion CD 77/78/84 Menu (MFP, random crash
with digidrums in STF mode) »). La fiche MC68901 (§ 3.1) ne connaît que la priorité fixe.
NeoST avait porté la règle d'Hatari ; `TODO.md` interdisait de la retirer sans ces disques.

Recette : `disks/st/fuzcd0{77,78,84}.msa` (privés, non versionnés), ST, TOS 1.02 UK, 1 Mo.
- Les deux règles s'écartent souvent : 741 à 1 712 élections en 3 000 trames, toujours
  Timer B (canal 8) contre Timer C (canal 5) pendants ensemble.
- Priorité fixe seule : 3 menus × 4 graines × 15 000 trames, aucun halt, menu vivant.
- L'état machine diverge entre les deux règles vers la trame 900 ; à l'image, 0 pixel
  sur 57 captures sur 60, et environ 130 pixels sur les trois autres (hauteur des
  VU-mètres et étoiles : même animation, phase différente).
- Contre Hatari (`compare_titles.py`, fenêtre ±40) : 9 à 12 % d'écart, même menu, phases
  d'animation différentes (cycle du logo, scroller), plus longues que la fenêtre.

Décision : canal élu par priorité fixe ([B]), front IRQ daté de la plus ancienne requête
éligible (`Mfp::earliestEligible`), qui est ce qui fait monter la ligne. Le plantage de
2013 relevait du modèle d'Hatari de l'époque, pas d'un besoin de ces menus sous NeoST.

## Lethal Xcess STE : la bordure haute calibrée sur la gigue du MFP (2026-11-10)

Recette : `Lethal_Xcess_Disk_1.STX` en A, `Disk_2` en B, STE, TOS 1.62 UK, 1 Mo ; SPACE aux trames
900, 1500, 2500, puis un script joystick à partir de 3000. NeoST ouvrait la bordure haute de 3796 à
3820, puis la refermait pour toujours ; Hatari la garde ouverte. Écart antérieur à la session
(binaire de `355b6c0` identique).

Chaîne du jeu : le Timer B déclenche la bordure basse (ligne 262) et réarme le Timer A (/200).
Au tour suivant, son gestionnaire lit TADR en boucle jusqu'à la recharge moins 2, cale la bascule
par des `lsr.l` dont les décalages (`$14CE8`) sont **calibrés au démarrage**, puis écrit `$FF820A`
vers la ligne 33, cycle 404 (NeoST) ou 420 (Hatari). La calibration converge au bord de la fenêtre
quand le délai du timer est parfaitement stable : la bascule retombe dans la ligne 33 et la chaîne
déraille. Hatari le sait et ajoute un aléa réservé à ce jeu (`mfp.c:1386-1392 @ f0736b24`).

Correctif, physique et non spécifique : un timer ne démarre qu'au front suivant de l'horloge des
timers (`docs/HATARI_DIVERGENCES.md` § 1, MFP). Lethal Xcess concorde alors avec Hatari de 4000 à 8000
(0,0 %, 0,01 % à 5000) ; la fermeture vers 5480 est une transition du jeu, chez les deux.
Outils : `NEOST_SYNC_TRACE=1` côté NeoST, `compare_titles.py --hatari-trace video_sync` côté Hatari.
Deux exécutions d'Hatari ont échoué pendant l'enquête, alors qu'une autre instance tournait ;
non reproduit ensuite (trois comparaisons simultanées passent). `--jobs` ne parallélise que les
disques. `compare_titles.py` garde désormais la cause d'un échec (code de sortie, délai, fin du
journal) au lieu du début de la ligne de commande.

## Systematic Error : le masque Microwire écrit en plein décalage (2026-11-11)

Recette : `disks/st/systematic_error.msa` (Imagina, 1993, image fournie par l'auteur, sha256
`7af68f56…`, non versionnée), STE, `tos162fr.img`, 1 Mo :
`NEOST_MW_TRACE=1 neost-headless roms/tos162fr.img --machine ste --mem 1m --disk
disks/st/systematic_error.msa --disk-ro --frames 1000`. Sous EmuTOS 256 la démo reste sur un
écran noir, chez NeoST comme chez Hatari : elle demande un TOS Atari.

Vers la trame 722, la routine `$F19E` coupe le son DMA puis envoie trois commandes LMC1992 :

```
F1AC  move.w #$7ff,$ff8924     ; masque
F1B2  cmpi.w #$7ff,$ff8924     ; attend que le masque relise $7FF
F1B8  bne    F1B2
F1BA  move.w (a0)+,$ff8922     ; donnée : $0001, $048C, $0449
F1BE  dbra   d0,F1AC
```

Le masque de la commande suivante tombe 28 cycles après la donnée, au troisième des seize
décalages. Les règles en présence :

- [D] Hatari `dmaSnd.c:1195-1206, 1238-1244 @ f0736b24` ignore l'écriture ; NeoST fait de même.
  Le masque tourne jusqu'au bout et relit `$7FF` après 124 cycles ; `$048C` (aigus 12) puis
  `$0449` (basses 9) s'appliquent. Trace Hatari `--trace dmasound` : « new trebble=0xc »,
  « new bass=0x9 ».
- [D] MiSTery `gstshifter.v:332-367 @ gstmcu 4a6db7cc` arrête le transfert (commits gstmcu
  `d90a1dca`, qui relançait, puis `05cf1df0`, qui arrête, le 2020-01-21 : « Systematic Error
  does it ») : le masque relit `$7FF` aussitôt, `$048C` est perdu, seules les basses passent.

La démo tourne sur un vrai STE ([A], logiciel réel validé sur matériel). Elle prouve donc que
le masque finit par relire la valeur écrite. Une puce qui prendrait l'écriture sans arrêter ni
relancer le transfert le laisserait tourné de 13 crans, et la boucle ne sortirait plus. Les deux
règles restantes la font tourner ; elle ne les départage pas, et elle n'écrit jamais la donnée
pendant un décalage. **Contre-épreuve** : la règle de MiSTery greffée dans `DmaSound::write8`
laisse sortir la boucle de `tests/dmasound_microwire_timing_logic.cpp` et ne change que les aigus
(6 au lieu de 12).

NeoST garde la règle d'Hatari, marquée [F]. Ce qui tranchera : la musique de la démo enregistrée
sur un vrai STE (aigus relevés de 12 dB ou non), ou la netlist du Shifter STE (l'archive des ASIC
n'a que le GSTMCU).
