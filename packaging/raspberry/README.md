# Borne NeoST sur Raspberry Pi — démarrage direct sur l'émulateur

Transforme un Raspberry Pi en **borne d'arcade Atari ST** : mise sous tension →
écran ST, sans bureau, sans gestionnaire de connexion, sans serveur de son.
Cible validée sur le papier pour **Pi 4 / Pi 400 (64 bits)** ; les scripts
détectent aussi le Pi 5 et le Pi 3.

> ⚠ Les scripts de ce dossier **n'ont pas encore été exécutés sur une machine
> réelle** — ils sont écrits à partir des recettes standard Debian/Pi OS et de ce
> que `src/gui/`/`Audio.cpp` attendent réellement. Prévoir un clavier et une
> carte SD de secours pour le premier essai (cf. § *Rattrapage*).

---

## 0. Avant tout : mesurer d'où viennent les coupures son

NeoST **dit déjà** pourquoi le son saute. `Audio::produceFrame` (`src/audio/Audio.cpp`)
imprime sur `stderr`, au plus une fois toutes les ~5 s :

```
[Audio] ring underrun (total N) — emulation loop: XX.X real frames/s (nominal 50 PAL / 60 NTSC / 71 mono), ring NNN
```

| Lecture                              | Diagnostic                                                              | Remède                                            |
|--------------------------------------|-------------------------------------------------------------------------|---------------------------------------------------|
| **trames/s < 50**                    | le Pi ne tient pas la cadence : c'est du CPU                             | build natif, couper le CRT, § 3                    |
| **trames/s ≈ 50** mais underruns     | gigue d'ordonnancement : un thread se fait préempter                     | la borne (§ 1-2) + `--audio-latency`               |
| aucun message, son quand même haché  | ce n'est pas l'anneau : c'est la couche de sortie (PipeWire, HDMI)       | § 2 (ALSA en direct)                               |

Deux commandes qui règlent souvent le problème avant toute réinstallation :

```sh
vcgencmd get_throttled     # ≠ 0x0 → sous-tension ou throttling thermique :
                           #   c'est l'alimentation / le dissipateur, pas le logiciel
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_governor   # `ondemand` → à passer en `performance`
```

### Le binaire compte autant que le système

Avant de dégraisser le système, prendre le bon binaire : **l'AppImage publiée est
aarch64 générique**, elle doit tourner du Pi 3 au Pi 5. Un binaire taillé pour le
cœur du Pi 400 (`-mcpu=cortex-a72`) **et compilé en deux passes guidées par profil
+ LTO** va nettement plus vite sur exactement le même code — mesuré **−34 %** de
temps d'émulation pour la seule recette de compilation, en plus de ~−37 % obtenus
par l'optimisation du cœur lui-même.

```sh
packaging/raspberry/build_native_pi.sh --pgo --install   # sur le Pi, ~1 h
```

…ou, sans rien compiler sur le Pi, récupérer l'artefact de la CI (déjà PGO+LTO,
cf. § 2.a). Méthode, mesures et pièges : **[`docs/PERFORMANCE.md`](../../docs/PERFORMANCE.md)**.

---

## 1. Ce que la borne enlève, et ce que ça rapporte

Debian lui-même ne coûte presque rien une fois démarré. Les postes qui coûtent,
par ordre décroissant :

| Poste supprimé                | Pourquoi c'est cher sur un Pi 4                                                                 |
|-------------------------------|--------------------------------------------------------------------------------------------------|
| **Le compositeur du bureau**  | recopie la fenêtre plein écran à chaque trame — plusieurs ms/trame, exactement la marge qui manque |
| **PipeWire / PulseAudio**     | resampling + graphe entre miniaudio et la carte ; 1ʳᵉ cause de micro-coupures                      |
| **Gouverneur `ondemand`**     | les rampes de fréquence produisent le hachage périodique caractéristique                           |
| **IRQ sur tous les cœurs**    | l'USB et l'Ethernet préemptent la boucle d'émulation → `irqaffinity=0`                             |
| **Wi-Fi / Bluetooth**         | les interruptions `brcmfmac` sont une source de gigue documentée                                   |
| **Le binaire générique**      | l'AppImage est aarch64 générique ; `-mcpu=cortex-a72` vaut ~10-20 % sur Moira                      |

Le boot direct ne fait **pas** gagner de puissance en soi — il supprime des
concurrents. Le gain « puissance » vient du build natif (§ 3).

Ce qui reste : un noyau Debian, `systemd`, un X nu, NeoST. Environ 120 Mo de RAM
et ~7 s de la mise sous tension à l'écran ST.

**Pourquoi X et pas Wayland ?** `cage` sur DRM/KMS serait plus élégant, mais
GLFW doit alors être compilé avec le backend Wayland : le `libglfw3` de Debian
bookworm (et donc celui qu'embarque l'AppImage Raspberry) est **X11 uniquement**.
Un X **sans gestionnaire de fenêtres ni compositeur** page-flippe directement via
KMS : le surcoût réel est de l'ordre du bruit.

---

## 2. Installation

Partir d'une carte **Raspberry Pi OS Lite 64 bits (bookworm)** fraîche —
*Lite*, pas *Desktop* : c'est ce qui garantit qu'aucun serveur de son n'est
installé.

### 2.a — Voie automatique : tout préparer sur la carte SD (recommandé)

Raspberry Pi Imager ≥ 2.0 écrit ses personnalisations en **cloud-init**
(`user-data`, `network-config` sur la partition `bootfs`). On s'en sert pour que
le **premier démarrage fasse tout seul** l'installation : plus rien à taper.

Depuis le poste de développement, carte SD montée :

```sh
# 1. Archive : sources + ROMs + disquettes (~44 Mo compressés)
tar -czf neost-payload.tar.gz --transform 's,^,neost/,' \
    CMakeLists.txt LICENSE README.md DEV.md CHANGELOG.md \
    src extern/moira extern/imgui extern/miniaudio packaging tools \
    tests/stx_writetrack_test.cpp \
    roms fonts gemdos carts disks/diskA.st disks/st disks/stx

# 2. Déposer l'archive + le script sur la partition de démarrage
cp neost-payload.tar.gz            /media/$USER/bootfs/
cp packaging/raspberry/provision.sh /media/$USER/bootfs/neost-provision.sh

# 3. Ajouter au `user-data` cloud-init (sous `packages:` et sous `runcmd:`) :
#      packages: … git, cmake, g++, make, libglfw3-dev, libgl1-mesa-dev
#      runcmd:   - ['systemd-run', '--unit=neost-provision',
#                   '/bin/bash', '/boot/firmware/neost-provision.sh']
```

⚠ `tests/stx_writetrack_test.cpp` n'est pas facultatif : `CMakeLists.txt`
déclare la cible `neost-stx-test` (même en `EXCLUDE_FROM_ALL`), et **la
configuration CMake échoue entièrement** si le fichier manque.

**Binaires pré-compilés (recommandé)** — compiler sur un Pi 4 prend 20 à 40
minutes. Le workflow `.github/workflows/pi-borne.yml` fait le même travail sur
un runner ARM64 natif, dans un conteneur `debian:bookworm`, avec
`-mcpu=cortex-a72`, et vérifie que le plancher glibc reste ≤ 2.36. Pousser la
branche `borne-raspberry` le déclenche ; ensuite :

```sh
gh run download <run-id> -n neost-pi400-aarch64
# Deux paquets, MÊME build : l'AppImage (Pi OS avec bureau, un fichier
# cliquable) et le tar.gz (borne sans bureau, à déballer dans /opt/neost).
cp neost-pi400-aarch64.tar.gz /media/$USER/bootfs/
```

`provision.sh` **préfère ce paquet** s'il le trouve sur la partition de
démarrage (et vérifie qu'il s'exécute avant d'aller plus loin) ; sinon il
compile sur place. La borne est alors prête en quelques secondes au lieu d'une
demi-heure.

Au premier démarrage, le Pi installe les paquets, déballe l'archive dans
`/usr/local/src/neost`, met les binaires en place, installe la borne et la
démarre. Suivre :

```sh
ssh <utilisateur>@<hôte>.local
journalctl -u neost-provision -f      # ou : tail -f /var/log/neost-provision.log
```

Le provisionnement est **idempotent** : en cas d'échec (réseau, place disque),
`sudo /boot/firmware/neost-provision.sh` reprend tout.

### 2.b — Voie manuelle

Sur le Pi :

```sh
sudo apt install -y git cmake g++ libglfw3-dev libgl1-mesa-dev
git clone <dépôt> neost && cd neost
git submodule update --init --recursive

# 1. Compilation native + installation dans /opt/neost (binaire + roms/ + disks/…)
sudo packaging/raspberry/build_native_pi.sh --install

# 2. Le système : X nu, ALSA direct, gouverneur, boot, service
sudo packaging/raspberry/install_kiosk.sh --user pi

# 3. Choisir ROM / disquette / latence audio
sudo nano /etc/neost-kiosk.conf

sudo reboot
```

### Disposition installée

Elle **n'est pas arbitraire** : elle découle de `resolveData()` (`src/gui/App.cpp`) et de
`cfgPath()` (`src/gui/AppConfig.cpp` → `src/util/ConfigPath.cpp`), qui cherchent les
données et la config relativement à `exeDir/..`.

```
/opt/neost/bin/neost              ← exeDir
/opt/neost/bin/neost-kiosk.sh     ← ExecStart du service (lance X nu)
/opt/neost/bin/neost-session.sh   ← lancé PAR startx, devient NeoST
/opt/neost/roms/  disks/  fonts/  gemdos/  carts/
/opt/neost/neost.cfg              ← cfgPath() : exeDir + "/../neost.cfg" s'il existe ou si
                                    le dossier est inscriptible, sinon ~/.config/neost/
/etc/neost-kiosk.conf             ← réglages de la borne (ROM, latence, CRT)
```

### Fichiers de ce dossier

| Fichier                  | Rôle                                                                    |
|--------------------------|--------------------------------------------------------------------------|
| `provision.sh`           | premier démarrage clé en main (déballe, installe, lance)                  |
| `install_kiosk.sh`       | tout le système (idempotent, `--uninstall` pour revenir en arrière)       |
| `build_native_pi.sh`     | compilation SUR le Pi, `-mcpu=<cœur réel>`, `--install` vers `/opt/neost` |
| `build_in_bookworm_pi.sh`| compilation en conteneur bookworm arm64 (utilisé par la CI)               |
| `neost-bt.sh`            | enceinte Bluetooth : `scan` / `pair` / `connect` / `status`               |
| `neost-bt-connect.{service,timer}` | reconnexion de l'enceinte toutes les 30 s                       |
| `neost-kiosk@.service`   | unité systemd **modèle** (`neost-kiosk@pi.service`)                       |
| `neost-kiosk.sh`         | démarre X nu sur le VT 1                                                  |
| `neost-session.sh`       | dans X : coupe DPMS/économiseur, fond noir, `exec neost --kiosk`          |

---

## 3. Réglages de la borne — `/etc/neost-kiosk.conf`

```sh
NEOST_ROM=roms/tos162uk.img     # ⚠ suffixe us → 60 Hz NTSC ; uk/fr/de/es → 50 Hz PAL
NEOST_DISK=disks/Jeu.stx
NEOST_AUDIO_LATENCY=120         # coussin audio en ms
NEOST_CRT_PRESET=               # off | leger | arcade | phosphor
NEOST_EXTRA_ARGS=               # ex. --kiosk-monitor 1
```

Après modification : `sudo systemctl restart neost-kiosk@pi`.

**`NEOST_AUDIO_LATENCY`** correspond à l'option `--audio-latency` de NeoST
(défaut 85 ms, borné `[20, 250]` dans `Audio::setLatencyMs`). C'est le coussin
que le thread d'émulation doit maintenir dans l'anneau. Un underrun coûte un
**trou audible** le temps de ré-amorcer ; une latence un peu plus haute ne coûte
qu'un décalage image/son imperceptible en dessous de ~150 ms. Sur Pi 4 :
commencer à **120**, monter à **150** si le journal montre encore des underruns.
Ce n'est pas un remède à une boucle d'émulation trop lente — seulement à la gigue.

**Le preset CRT est le premier poste à couper** si les trames/s ne tiennent pas :
c'est un shader plein écran, et sur Pi 4 il se paie comptant.

⚠ **`NEOST_BIN` vers une AppImage antérieure à `--audio-latency`** : ces anciens
binaires ignoraient les options `--` inconnues mais gardaient leurs valeurs comme arguments (les actuels les refusent, code 2)
**positionnels** — `--audio-latency 120` y deviendrait donc « ROM = 120 » et la
borne ne démarrerait pas. Échappatoire : mettre `NEOST_AUDIO_LATENCY=""`
(vide ⇒ `neost-session.sh` n'émet pas l'option du tout).

---

## 3bis. La sortie son — HDMI ou Bluetooth

**Le Pi 400 n'a pas de prise jack.** Le son sort donc par HDMI ou par Bluetooth,
et ces deux voies ne demandent pas la même pile logicielle.

### HDMI (défaut, `install_kiosk.sh` sans option)

Aucun serveur de son : miniaudio ouvre ALSA directement. C'est la voie la plus
sobre et la seule sans latence ajoutée. Le Pi 400 ayant **deux ports HDMI**, le
script choisit la carte `vc4hdmi*` dont l'**ELD** est valide — c'est-à-dire
celle où un écran répond réellement — et l'écrit dans `/etc/asound.conf`.
Écran branché après coup ? Relancer le script, ou forcer : `--alsa-card N`
(`aplay -l` pour les numéros).

### Bluetooth (`install_kiosk.sh --bluetooth-audio`)

L'A2DP **n'existe pas en ALSA nu** : il faut un serveur de son. Le mode installe
donc PipeWire + WirePlumber + BlueZ — c'est-à-dire qu'il réintroduit
délibérément la couche que le § 1 avait retirée. Ce qui rend l'opération
acceptable, et ce qui la rend possible sans toucher au code de NeoST :

- **miniaudio classe PulseAudio avant ALSA** (`ma_backend` est ordonné par
  priorité). NeoST se branche donc sur `pipewire-pulse` — et comme PipeWire
  déplace les flux vers le nouveau puits par défaut, l'enceinte qui se connecte
  **en pleine partie** récupère le son. Sans cela, rien ne serait possible :
  `Audio::start` ouvre UN périphérique au démarrage et n'en change jamais.
- **48 kHz verrouillé** (`default.clock.allowed-rates`) : NeoST synthétise déjà
  en 48 kHz, donc zéro rééchantillonnage. C'est là qu'un serveur de son coûte
  d'habitude cher.
- **Quantum large** (1024) : sur un Pi 400 à pleine charge, mieux vaut peu de
  gros réveils que beaucoup de petits.
- **Profils HSP/HFP coupés** (`bluez5.roles = [ a2dp_sink ]`) : une enceinte qui
  bascule en profil casque passe en 8-16 kHz mono avec le micro ouvert — le son
  devient un talkie-walkie. C'est la panne Bluetooth la plus fréquente.

L'HDMI reste disponible dans ce mode : WirePlumber bascule sur l'enceinte quand
elle arrive et revient à l'HDMI quand elle s'éteint.

```sh
sudo /opt/neost/bin/neost-bt.sh scan              # 20 s de découverte
sudo /opt/neost/bin/neost-bt.sh pair AA:BB:…      # appaire + fait confiance + mémorise
sudo /opt/neost/bin/neost-bt.sh status
```

`pair` mémorise l'adresse dans `/etc/neost-kiosk.conf` (`NEOST_BT_MAC`) et arme
`neost-bt-connect.timer`, qui rappelle `connect` toutes les 30 s : **l'enceinte
allumée après la borne est rattrapée toute seule** — le cas normal en
exposition. `trust` est posé avant `connect` : sans confiance, BlueZ redemande
une autorisation à chaque reconnexion, et sur une borne personne ne répondra.

⚠ **L'A2DP ajoute 150 à 250 ms de retard**, inhérents au Bluetooth et
irréductibles. Avec `NEOST_AUDIO_LATENCY=120` on approche les 300 ms entre
l'image et le son : jouable pour de la musique de démo, gênant pour un jeu
d'action. Pour une borne où l'on joue, **l'HDMI reste très supérieur** ; le
Bluetooth se justifie quand l'écran n'a pas de haut-parleurs. En Bluetooth, on
peut descendre `NEOST_AUDIO_LATENCY` vers 85 pour ne pas empiler — en
surveillant le compteur d'underruns.

## 4. Exploitation

```sh
journalctl -u neost-kiosk@pi -f          # journal, dont les diagnostics [Audio]
sudo systemctl restart neost-kiosk@pi    # relancer la borne
sudo systemctl stop neost-kiosk@pi       # rendre la main (le VT 1 revient au getty)
sudo packaging/raspberry/install_kiosk.sh --uninstall --user pi
```

`Restart=always` : un plantage de l'émulateur relance la borne en 2 s au lieu de
laisser un écran noir devant le public.

### Verrouillage pour une expo publique

L'installation **laisse volontairement** `Ctrl+Alt+F2` → console (les getty 2-6
restent actifs) comme porte de service. Si le public a un clavier complet, les
couper :

```sh
sudo systemctl mask getty@tty2 getty@tty3 getty@tty4 getty@tty5 getty@tty6
```

`Alt+F4` ferme NeoST, mais `Restart=always` le relance : il n'y a pas de sortie
vers un bureau. Voir aussi `DEV.md` § *Mode kiosk* : en `--kiosk`, la
configuration est **figée** (`saveConfig`, `src/gui/App.cpp`, ne réécrit que les dossiers
ROM et l'affectation des manettes, par-dessus l'image lue au démarrage), une session
publique ne peut donc pas corrompre le réglage de la borne.

### Survivre aux coupures de courant

Une borne se débranche à l'arraché. La carte SD n'aime pas :

```sh
sudo raspi-config nonint enable_overlayfs     # rootfs en lecture seule (overlayfs)
```

À faire **en dernier**, une fois la borne réglée : tant que l'overlay est actif,
toute modification est perdue au redémarrage (le désactiver pour intervenir).

---

## 5. Rattrapage — si la borne ne démarre plus

Le service ne peut pas empêcher le Pi de démarrer, mais un `config.txt` fautif
si. Deux portes de sortie :

1. **Depuis le Pi** : `Ctrl+Alt+F2` → connexion → `sudo systemctl stop neost-kiosk@pi`.
2. **Depuis un autre ordinateur** : monter la partition de démarrage de la carte SD.
   `install_kiosk.sh` sauvegarde les originaux à côté :
   `config.txt.neost-orig` et `cmdline.txt.neost-orig` — les remettre en place.
   Ajouter `systemd.unit=multi-user.target systemd.mask=neost-kiosk@pi.service`
   à `cmdline.txt` démarre sans la borne.

Symptômes fréquents au premier essai :

| Symptôme (dans `journalctl`)                       | Cause                                                                 |
|----------------------------------------------------|------------------------------------------------------------------------|
| `Only console users are allowed to run the X server`| `/etc/X11/Xwrapper.config` absent ou écrasé par une mise à jour         |
| `Cannot open virtual console 1`                    | `getty@tty1` réactivé (le `Conflicts=` de l'unité doit le neutraliser)  |
| `no screens found`                                 | l'utilisateur n'est pas dans le groupe `video`/`render` (se déconnecter/reconnecter) |
| aucun son, aucun message `[Audio]`                 | mauvaise carte ALSA : `aplay -l` puis `install_kiosk.sh --alsa-card N`  |
