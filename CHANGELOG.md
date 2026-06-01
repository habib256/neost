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
- **Bruits mécaniques du lecteur de disquette** : le cœur émet des événements
  `FdcSound` (moteur / pas / seek) depuis `Fdc::executeCommand` via un sink, sans
  dépendance audio. Frontends : `DriveSound` (miniaudio `ma_engine`) côté GUI,
  Web Audio côté WASM. Échantillons WAV de STeem SSE (freeware, échantillonnés
  par Stefan jL) embarqués dans `rom/drivesound/`. Bascule on/off des deux côtés.

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
