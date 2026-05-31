# TODO — NeoST

(c) 2026 VERHILLE Arnaud. Feuille de route, par priorité indicative.

## Matériel — précision

- [x] **HBL (interruption niveau 2)** : générée par ligne visible, gatée par le
      masque SR. ✓
- [x] **Interruption FDC** (MFP canal 7, GPIP5) sur fin de commande, en plus de
      la ligne pour le polling. ✓
- [ ] **Arkanoid** : charge et affiche son écran-titre via le FDC (✓), puis se
      fige sur `tst.b $26E7 / bne`. Le flag n'est effacé par aucun code exécuté ;
      le jeu n'utilise ni Xbtimer ni timer MFP, son VBL ne touche pas `$26E7`.
      Mécanisme à identifier (reverse plus poussé / comparaison Hatari).
- [ ] **MFP** : Timers A et D, modes delay (prescaler) complets, interruptions
      RS232 (USART). Actuellement seuls B (event-count) et C (200 Hz) tournent.
- [ ] **Timing cycle-accurate** : le FDC fait un « DMA instantané » et la boucle
      est ligne-par-ligne (512 cycles). Suffisant pour booter/jouer, mais les
      effets raster fins et certaines protections de disquette demanderaient une
      horloge plus fine.
- [ ] **Blitter** (Mega ST / STE) : actuellement bus error (= absent). L'émuler
      permettrait les logiciels qui l'exigent.
- [ ] **GLUE/MMU** : configuration mémoire réelle, vraie détection RAM.
- [ ] **Horloge temps réel / NVRAM** (Mega ST) : date/heure, résolution de boot
      mémorisée côté machine.

## Disquette / stockage

- [ ] **Écriture disquette → fichier** : les écritures vont dans l'image en
      mémoire ; les recopier dans le `.st` (avec confirmation) pour persister.
- [ ] **Lecteur B** : second drive, sélection via PSG port A (déjà décodée).
- [ ] Support `.msa` (compressé) et éjection/insertion à chaud depuis le GUI.
- [ ] **ACSI / disque dur** ($FF8600 en mode ACSI).

## Vidéo

- [ ] **Moyenne résolution comme mode bureau** : forcée au boot, l'image est
      brouillée (le VDI/AES fige la géométrie basse rés ailleurs que `$FF8260`).
      Marche déjà quand un programme l'active (le Shifter suit). Piste : injecter
      proprement un `Setscreen`, ou un `EMUDESK.INF` côté EmuTOS.
- [ ] Bordures / overscan, palette STE (4 bits/canal), hardware scroll STE.
- [ ] Filtrage/scanlines optionnels à l'affichage.

## Audio

- [ ] YM2149 : **enveloppe** (registres 11-13) et accordage fin du bruit.
- [ ] Sortie son **STE** (DMA sound, $FF8900).

## Entrées

- [ ] Vérifier le **clic-glissé** en conditions réelles (sélection élastique,
      déplacement d'icônes) — validé en headless, à confirmer en GUI.
- [ ] **Souris en absolu** + joystick (port IKBD).
- [ ] Remappage clavier complet (touches mortes FR, pavé numérique).

## Outillage / qualité

- [ ] **Comparaison automatique avec MAME/Hatari** : script qui diff la trace
      `neost-headless` contre une trace MAME du même ROM, pour valider la
      fidélité instruction par instruction.
- [ ] Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).
- [ ] CMake : `FetchContent` pour les sous-modules, build CI Linux + macOS.

## Confort GUI

- [ ] Chargeur de ROM/disquette **dans l'appli** (au lieu d'arguments CLI).
- [ ] Désassembleur live + points d'arrêt dans la fenêtre ImGui.
- [ ] Plein écran, zoom réglable, capture d'écran depuis le GUI.
