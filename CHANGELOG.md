# Changelog — NeoST

(c) 2026 VERHILLE Arnaud. Format chronologique par grandes étapes (le projet
n'a pas encore de versions taguées ; tout est en 0.1.x « en cours »).

## Cœur & boot

- Architecture de base : `Bus` (memory map ST), wrapper `Cpu68k` autour de
  Musashi, `Shifter` (vidéo), squelettes `YM2149` / `Glue`.
- Bibliothèque cœur `neost_core` sans dépendance GUI ; deux frontends
  (`neost` fenêtré, `neost-headless` déterministe).
- Boot 68000 : overlay ROM aux adresses `$0-$7` (SSP/PC), refermé après reset.
- ROM TOS auto-détectée : 192 Ko → `$FC0000`, sinon `$E00000`.

## Vidéo

- Shifter : décodage planaire basse (320×200/16c), moyenne (640×200/4c),
  haute (640×400 mono). Texture OpenGL, conversion couleur `$0RGB` → ARGB.
- Haute résolution forcée en **blanc/noir** (moniteur mono), indépendamment de
  la palette couleur (corrige un fond rouge sous TOS 1.02).
- Détection moniteur via **GPIP bit7** : couleur (basse rés) / mono (haute rés).
- Affichage GUI adaptatif : l'écran ST s'ajuste à la fenêtre ImGui (ratio 640×400).

## Interruptions (MFP 68901)

- Contrôleur d'interruptions complet : IER/IPR/IMR/ISR + registre vecteur,
  modes auto et software-EOI.
- **Activation de `M68K_EMULATE_INT_ACK`** dans Musashi — sans ça toutes les IRQ
  étaient auto-vectorisées et les vecteurs MFP (clavier, timers) inutilisés.
- **Timer C 200 Hz** : tic système qui débloque l'accueil EmuTOS et fait vivre
  le bureau.
- **Timer B event-count** (compte le Display Enable, lignes visibles) : la
  synchro vidéo de TOS 1.x ; sans elle TOS 1.02 reste en écran noir.
- Backing store des registres timer/USART (TOS les écrit puis relit pour
  détecter le MFP).
- VBL niveau 4 auto-vectorisé, une fois par trame.

## Clavier & souris (ACIA 6850 / IKBD)

- ACIA clavier + file de scancodes ; mapping clavier GLFW → scancodes ST.
- **Ligne GPIP4** câblée sur l'état RDRF de l'ACIA : `_int_acia` d'EmuTOS la lit
  pour vider l'ACIA puis effacer l'ISR — sans elle le canal 6 restait bloqué dès
  le boot (ni clavier ni souris).
- Souris : paquets relatifs IKBD (en-tête `$F8`|boutons + Δx/Δy), boutons
  événementiels (capte le double-clic rapide), accumulation sous-pixel.
- Capture souris par clic dans l'écran ST, libération par Échap ; fenêtre ST
  `NoMove` pour ne pas la déplacer en cliquant-glissant.

## Disquette (FDC WD1772 + DMA)

- Accès indirect via le DMA (`$FF8600`) : registres FDC/sector-count, adresse DMA.
- Commandes Restore/Seek/Step/Read/Write/ReadAddress ; modèle « DMA instantané ».
- Sélection face/lecteur via le port A du YM2149 (actif bas).
- INTRQ → **GPIP5** (poll `timeout_gpip` d'EmuTOS).
- Image FAT12 720 Ko fabriquée à la main (`disks/diskA.st`).

## Bus errors

- Bus error sur la région blitter `$FF8A00` (absent sur ST de base) : EmuTOS la
  sonde pour conclure « pas de blitter ». Sans ça, **barre de menu et curseur
  GEM disparaissaient** (tracés VDI routés vers un blitter fantôme).

## Audio

- YM2149 : synthèse 3 voies carrées + bruit. Backend **miniaudio** (CoreAudio).
- **Enveloppe YM2149** (registres 11-13) : générateur de volume 0..15, formes
  dent de scie / triangle / hold pilotées par Continue/Attack/Alternate/Hold ;
  une voie suit l'enveloppe quand le bit 4 de son registre de volume est posé.
  Écrire R13 réarme l'enveloppe (drapeau consommé côté thread audio).
- **Son DMA STE** (`DmaSound`, $FF8900-$FF8925) : nouveau composant `neost_core`
  lisant des échantillons 8 bits signés en RAM (6.25/12.5/25/50 kHz, mono/stéréo,
  play/repeat, compteur d'adresse). Routé par le `Bus`, mixé au YM2149 par `Audio`
  (GUI) et `neost_audio_render` (WASM). Donne le son numérique des jeux/démos STE.
- **LMC1992 / Microwire** ($FF8922/24) : décodage de la commande série (mot 11
  bits), volume maître + gauche/droite appliqués en gain linéaire au mix complet,
  et **basses/aigus** ±12 dB via deux filtres en plateau (RBJ shelving) sur le mix
  YM2149 + DMA. 0 dB partout par défaut (bypass total, aucun coût).
- **Interruption de fin de trame du son DMA STE** : datée sur l'ordonnanceur
  (thread émulation, `Scheduler::DMASND`), elle pulse l'entrée TAI du MFP
  (`Mfp::timerA_eventCount`) ; en mode event-count (TACR=0x08) Timer A lève
  l'IRQ canal 13 → permet le double-buffering audio streamé des jeux/démos STE.
- **Bruits mécaniques du lecteur de disquette** : le cœur émet des événements
  `FdcSound` (moteur on/off, pas, seek, index) depuis `Fdc` via un sink, sans
  dépendance audio. Frontends : `DriveSound` (miniaudio `ma_engine`) côté GUI,
  Web Audio côté WASM. Échantillons WAV de STeem SSE (freeware, échantillonnés
  par Stefan jL) embarqués dans `rom/drivesound/`. Bascule on/off des deux côtés.
- **Moteur & index modélisés dans le cœur** : impulsion d'index datée 1/tour
  (~200 ms à 300 tr/min, `Scheduler::FDC_INDEX`) ; le WD1772 coupe le moteur
  après ~10 tours d'inactivité (`MotorOff`). Plus de minuterie côté frontend.
- **Mixage YM2149 + lecteur** (GUI) : un seul périphérique miniaudio
  (`Audio::render`) somme le PSG et la sortie « sans périphérique » de
  `DriveSound` — active aussi la sortie son du PSG dans le frontend fenêtré.
- **Bit INDEX du WD1772** : reflété sur les lectures de statut type I (trou
  d'index ~1.46 ms, 1/tour) — phase dérivée de l'horloge de l'ordonnanceur.
- **Son du PSG en WASM** : export `neost_audio_render` tiré par un
  `ScriptProcessorNode` (Web Audio) partageant l'AudioContext des bruits de
  lecteur → le navigateur mixe YM2149 + lecteur. Le frontend navigateur a enfin
  du son (bips TOS, musiques).

## Types de machine

- **Profil machine** (`MachineType` : ST / Mega ST / STE / Mega STE) choisi avant
  le boot, comme le cœur CPU. Porté par le `Bus` ; gating du matériel optionnel :
  le **son DMA STE** ($FF8900) ne répond que sur STE/Mega STE et fait **bus error**
  sur ST/Mega ST (fidèle, c'est ainsi qu'EmuTOS détecte le modèle). Défaut : STE.
- Sélection : menu « Machine ▸ Modèle » (GUI, `neost.cfg`), `?machine=` (WASM),
  `--machine st|megast|ste|megaste` (headless). Prépare le gating du blitter (Mega).
- **Taille de ST-RAM** configurable (256 Ko / 512 Ko / 1 / 2 / 4 Mo) avant le boot :
  menu « Machine ▸ Mémoire » (GUI), `?mem=` (WASM), `--mem` (headless). EmuTOS
  détecte la `phystop` exacte par sondage ; `$FF8001` posé en cohérence.

## Bus error & cartouches de diagnostic

- **Modèle de bus error réécrit comme un port fidèle de Hatari** (`ioMem.c` +
  `ioMemTabST/STE.c` + `cpu/memory.c`) : tout `$FF8000-$FFFFFF` faute par défaut,
  on whiteliste les registres câblés selon le modèle (`Bus::buildIoFault`, carte
  octet par octet), + zones « void », + miroir PSG, + fixups ST/MegaST/MegaSTE.
  Hors IO : `$400000-$F9FFFF` et `$FF0000-$FF7FFF` fautent (vrais trous). Règle
  word/long : faute seulement si TOUS les octets fautent (`busFaultN`) → `move.w
  $FF8204` marche, `move.b $FF8204` faute. Les DEUX cœurs (Musashi + Moira) la suivent.
- **Double bus fault → halt CPU** : un code parti en vrille fautait en boucle
  pendant l'empilement d'exception → segfault hôte. On halte désormais le CPU
  comme un vrai 68000 (Musashi `m68k_pulse_halt`, Moira `flags|=HALTED`), ce qui
  laisse `neost-headless` vider trace + port série au lieu de crasher.
- **Trame de bus error 68000 dans Musashi** (`extern/Musashi/m68kcpu.h`) : Musashi
  empilait la trame format-8 du 68010 (58 octets) au lieu de la trame 68000 de 14
  octets (`m68ki_stack_frame_buserr`). Les handlers de bus error des ROMs/diags qui
  font `adda #8 ; rte` (sondage matériel) revenaient sur une PC corrompue → vrille.
- **Base écran relisible** (`$FF8201/03`, + octet bas STE `$FF820D`) dans le Shifter
  (Hatari IoMem_ReadWithoutInterception) : les diagnostics RÉCUPÈRENT la base écran
  en relisant ces registres pour situer leur framebuffer (sans ça → base 0 → ils
  dessinent sur la table des vecteurs).
- **Réponse de reset IKBD différée** (`$F1` ~502000 cycles après `$80,$01`, via
  `Scheduler::IKBD`, cf. Hatari) : répondre instantanément levait l'IRQ ACIA avant
  son armement par le logiciel.
- **Registre de synchro `$FF820A` relisible** (Shifter, défaut $02 = 50 Hz PAL,
  cohérent avec la trame 313 lignes) : un logiciel qui lit la fréquence vidéo
  (diagnostics) ne la croit plus 60 Hz.
- **Injection clavier headless** (`--keys "..."`) : pilote les menus des diagnostics
  (scancodes ST pour A-Z, 0-9, Entrée).
- **Cartouches de diagnostic** (`carts/`, magic `$FA52235F`) via `--cart` : rapport
  à l'écran + port série. Grâce aux corrections ci-dessus, **`ST_Diagnostic` boote
  jusqu'à son menu interactif** (self-tests bus error + clavier OK) sur les DEUX
  cœurs, et **sa batterie de tests internes passe 7/8 avec un vrai TOS** (RAM, ROM,
  Color, Keyboard, Audio, RTC, BLiT ; seul « T0 MFP timer » échoue — cycle-accuracy).
  Non régressif : EmuTOS boote, jeux chargent (Musashi + Moira).

## Frontend & confort

- Écran ST dans une fenêtre ImGui dédiée ; visualiseur hexa mémoire + registres
  68000 ; bouton Reset ; barre de boutons résolution (couleur/mono + hard reset).
- Bridage **50 fps réels** (le compteur 200 Hz colle au temps réel → double-clic
  correct).
- Résolution de chemins robuste (rom/, disks/ trouvés depuis la racine ou build/).
- **Persistance** (`neost.cfg`) : dernier ROM chargé + type de moniteur.

## Outillage

- `neost-headless` : trace d'instructions façon MAME, registres, interruptions,
  capture PPM, injection souris, choix moniteur. C'est l'outil de débogage
  principal (a permis de localiser tous les bugs ci-dessus).

## Validé

- EmuTOS (FR/US) : green desktop, fichiers disquette, double-clic, fenêtres.
- TOS 1.02 Mega ST FR : boot complet, green desktop basse rés.
- **Arkanoid** (Imagine 1987) : se lance via l'AUTO de la disquette, écran-titre.
