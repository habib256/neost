# Sons du lecteur de disquette (DriveSound)

Échantillons WAV reproduisant les bruits **mécaniques** d'un lecteur de
disquette 3"½ d'Atari ST (ronron du moteur, « clac » des pas de tête, etc.).
Ni Hatari ni MAME n'émulent ces bruits : ils sont rejoués à partir de samples,
déclenchés par les événements du contrôleur WD1772 (cf. `TODO.md`, section Audio).

## Provenance

Récupérés depuis **STeem SSE** (émulateur Atari ST *freeware*), dossier
`DriveSound/`. Les sons « Epson SMD-480L » ont été **échantillonnés par Stefan jL**
à partir d'un vrai lecteur (merci à lui). STeem est freeware ; ces échantillons
sont fournis tels quels — conserver le crédit ci-dessus en cas de redistribution.

- Site STeem SSE : https://sourceforge.net/projects/steemsse/

## Contenu

Deux jeux de 4 échantillons (mono) ; choisir l'un ou l'autre selon le rendu voulu :

| Fichier            | Rôle                                              |
|--------------------|---------------------------------------------------|
| `drive_spin.wav`   | boucle moteur (joué en boucle tant que MOTOR_ON)  |
| `drive_startup.wav`| démarrage moteur / mise en route                  |
| `drive_seek.wav`   | déplacement de tête (SEEK / rafale de pas)        |
| `drive_click.wav`  | un pas de tête (STEP) / tic                        |

- `basic/` : sons synthétiques génériques (44.1 kHz, sauf seek en 11 kHz/8 bit).
- `epson_smd480l/` : enregistrements d'un vrai Epson SMD-480L (44.1 kHz/16 bit),
  seek bien marqué.

## Branchement (à implémenter)

Voir `TODO.md` → *Audio* → « Bruits du lecteur de disquette » : hooks dans
`src/io/Fdc.cpp` (type I → `drive_click`/`drive_seek` ; bascule `MOTOR_ON` →
`drive_spin`/`drive_startup`), mixés par le frontend (`src/audio/Audio.cpp`
miniaudio pour le GUI ; Web Audio pour le WASM, préchargés via `--preload-file`).
