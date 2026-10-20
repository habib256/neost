# Extensions NeoST — matériel d'époque que l'ST n'avait pas d'origine

> **Statut commun.** Toutes **inactives par défaut**, sans effet sur les étalons (aucune
> n'ouvre de socket depuis `tools/run_all.py`), et consignées comme divergences délibérées
> dans `docs/HATARI_DIVERGENCES.md` § Extensions — Hatari n'a aucun équivalent. Le cœur
> (`neost_core`) ne fait que *signaler* ; l'I/O réelle vit dans `neost_net` (frontends).
>
> Toutes reproduisent du matériel que les Atariens ont réellement branché :
>
> | Extension | Nature | Réalité matérielle |
> |-----------|--------|--------------------|
> | [UltraSatan](#ultrasatan--interface-sdmmc-sur-le-bus-acsi---ultrasatan-gui-hard-disks) | stockage SD sur ACSI | **matériel réel** (Jookie) |
> | [NetUSBee](#netusbee--ne2000--hôte-usb-isp1160-sur-le-port-cartouche---netusbee-gui-network) | Ethernet + USB, port cartouche | **matériel réel** |
> | [EtherNEC](#ethernec--ne2000-sur-le-port-cartouche---ethernec-gui-network) | NE2000, port cartouche | **matériel réel** (montage T. Redelberger) |
> | [Modem Hayes](#modem-hayes-sur-rs-232---modem-gui-network) | pont RS-232 → TCP | équivalent des modems WiFi ESP8266 vendus pour ST |
> | [Appareils MIDI hôtes](#appareils-midi-de-lhôte--le-câble-din-du-st-vers-du-vrai-matériel) | expandeur/clavier branché sur la machine | **c'est le câble DIN du ST** |
> | [Anneau MIDI](#anneau-midi-réseau--midimaze-en-ligne---midi-net-hpl) | MIDIMaze sur UDP | transpose un câblage MIDI réel |
> | [Clé Steinberg](#clé-steinberg--dongle-cubase-sur-le-port-cartouche---dongle-gui-dongles) | PAL16R8 / EPLD sur /ROM3 | **matériel réel** (clés noire et rouge de Cubase) |
> | [Périphériques des ports](#périphériques-des-ports--un-par-port--clés-joystick--série-dac-pro-sound-boutons---plug-gui-dongles) | clés joystick/RS-232, DAC parallèle, boutons Multiface/URC | **matériel réel** (11 adaptateurs, inventaire Steem SSE) |

# Les extensions, une par une

Philosophie commune : backend hôte hors du cœur, OFF par défaut, `NEOST_WITH_NET` pour
celles qui ouvrent des sockets. **UltraSatan** pour le stockage, **NetUSBee**/**EtherNEC**
pour le réseau, **modem Hayes** et **anneau MIDI** pour le reste.

## Modem Hayes sur RS-232 (`--modem`, GUI Network)

Un modem émulé sur l'USART MFP : les commandes `AT` ouvrent de vraies connexions TCP
(`ATDT hôte:port` → `CONNECT`, pont transparent octets ↔ socket ; `+++`/`ATH` pour
raccrocher ; `DCD` suit la porteuse). C'est le grand débloqueur du logiciel d'époque —
terminaux, BBS, et les piles TCP/IP historiques **STiK/STinG en SLIP/PPP**. Repose sur
`Mfp::receiveByte` (injection RX **cadencée** au débit série via `Scheduler::SERIAL_RX`,
IRQ RxFull par octet — un pilote qui compte sur le rythme du fil ne perd aucun octet).
Vérifié : `MODMTEST.TOS` ↔ serveur TCP local (`CONNECT 9600`, bannière reçue), et
**bout-en-bout sur Internet** (2026-08-22) — `tools/make_net_test.py` génère une disquette
dont l'`AUTO\NETTEST.PRG` compose `ATDT theoldnet.com:80`, envoie une requête HTTP à la
main et affiche la réponse : en-têtes (`Content-Length`, `X-Powered-By: Express`) puis le
HTML. C'est le chemin qui marche **sans pile TCP/IP côté ST** (le modem est un pont
transparent octets ↔ socket) ; interactivement, `UNITERM.PRG` fait la même chose.

```sh
python3 tools/make_net_test.py /tmp/nettest.st theoldnet.com /
./build/neost-headless roms/etos192us.img --machine megast --mem 1m --mono \
    --modem --disk /tmp/nettest.st --frames 2500 --screenshot out.ppm
```

Deux pièges appris en l'écrivant, consignés dans le générateur : le compteur 200 Hz du TOS
(`$4BA`) vit en mémoire BASSE, **protégée** — y toucher depuis un PRG (mode utilisateur)
lève un bus error, d'où `Super(0)` ; et la console ST **n'enroule pas** les lignes longues,
elle écrase la 80ᵉ colonne (une ligne HTML n'y laissait voir que son dernier caractère,
une colonne de « > »), d'où le repli à 78 colonnes et le CR ajouté sur les LF seuls.
`src/net/HayesModem.cpp`.

Le NetUSBee/EtherNEC **sort réellement sur Internet** : `--slirp` en headless, la case
« Real Internet for the NE2000 » de la page **Network** dans le GUI (clé `slirp=` de
`neost.cfg`, rejouée par `--from-cfg` comme `netusbee=`). La bascule boucle locale ↔
Internet est à chaud — la carte vue par le pilote ST ne change pas. En détail : `--slirp` branche
`SlirpBackend` (`src/net/SlirpBackend.cpp`, libslirp, NAT mode utilisateur — ST en 10.0.2.15, passerelle 10.0.2.2,
DNS relayé 10.0.2.3), `--slirp-restricted` la variante bac à sable sans sortie. Build :
option CMake `NEOST_WITH_SLIRP` (AUTO via pkg-config `slirp`). Validation :
`--slirp-selftest` pilote la NE2000 au niveau fil et vérifie **4 points déterministes et
hors ligne** (réponse ARP de la passerelle, OFFER DHCP, compteurs, et une **boucle retour
UDP loopback à travers le NAT** — un répondeur local éphémère prouve le chemin complet
socket hôte → SLIRP → ARP → anneau RX sans toucher au réseau). `NEOST_SLIRP_ONLINE=1`
ajoute le 5ᵉ point, opt-in : une vraie requête DNS sortante (`NEOST_SLIRP_DNS=a.b.c.d[:port]`
pour viser un résolveur précis ; `NEOST_SLIRP_TRACE=1` trace trames et sockets).
⚠ **Un pare-feu applicatif (Little Snitch…) peut faire échouer ce 5ᵉ point seul** : il
jette silencieusement l'UDP externe des binaires non signés (`sendto` OK, réponse jamais
vue) et une alerte en attente peut même **geler `sendto` dans le noyau** jusqu'au verdict —
autoriser `build/neost-headless` en sortie (règle à refaire après un rebuild, binaire non
signé). Le point 4, insensible au filtre, tranche : si lui passe, NeoST est correct.
Côté ST, le banc bout-en-bout est **`tools/make_sting_test.py`** : il génère une
disquette de boot STinG 1.26 + `ENEC.STX` (URLs des archives freeware/GPL dans son
en-tête) dont l'`AUTO` embarque un PRG maison qui configure le port « EtherNet » par
l'API STinG (cookie `STiK` — le rôle que joue `STNGPORT.CPX` en GEM), résout un nom et
fait un GET HTTP, verdict recopié sur RS-232 :

```sh
python3 tools/make_sting_test.py /tmp/sting.st <dossier sting126> <dossier ENEC.STX>
./build/neost-headless roms/etos256us.img --ethernec --slirp --disk /tmp/sting.st \
    --frames 3500 --serial-dump /tmp/ser.txt     # attendu : DNS=a.b.c.d puis HTTP/1.x
```

C'est ce banc qui a démasqué l'inversion des fenêtres ROM3/ROM4 (cf. EtherNEC ci-dessous). Restent le
navigateur (CAB) en GUI et la consignation dans `docs/CASE_STUDIES.md` — cf. `TODO.md`.

## EtherNEC — NE2000 sur le port cartouche (`--ethernec`, GUI Network)

Émulation de la carte réseau NE2000 vue par le port ROM (montage
[EtherNEC](https://github.com/EmmanuelKasper/ethernec) de Dr. Thomas Redelberger), pour
faire tourner les pilotes libres **STinG (`.STX`), MiNTnet (`.XIF`) et MagiCNet SANS
modification**. Le port cartouche étant en lecture seule et sans ligne A0, tout est
encodé dans l'adresse :

* **lire** le registre `n` : lecture à `$FA0000 + n*512` (/ROM4) ;
* **écrire** `d` dans `n` : *fausse lecture* à `$FB0000 + n*512 + d*2` (/ROM3).

⚠ Ces deux fenêtres ont été **inversées** dans NeoST jusqu'au 2026-08-27 : les selftests
passaient (mêmes constantes des deux côtés du test), mais le vrai `ENEC.STX` ne pouvait
ni lire ni écrire la carte — découvert à la première session STinG réelle, tranché sur
`BUSENEC.I` du pilote de l'auteur du montage (`getBUS` lit via `rom4=$FA0000`, `putBUS`
écrit via `rom3=$FB0000`).

Décodé dans `Bus::read8Slow` (`src/io/Ne2000.cpp`). Modèle DP8390 classique (pages 0/1,
anneau de réception avec en-tête 4 octets, Remote DMA, filtrage MAC/broadcast). Backend
physique = interface `NetBackend` (boucle locale, ou SLIRP avec `--slirp` quand libslirp
est présente ; pcap = point d'extension). **Exclusif d'une cartouche montée** (conflit de
fenêtre $FA0000 — le montage est refusé avec message). Auto-test : `--enec-selftest`
(palier `fast`, qui vérifie aussi la PROM et sa lecture seule).

**PROM d'adresse station** (depuis le 2026-10-20) : 32 octets visibles à RSAR=0 par Remote
DMA. La MAC y est doublée (octets 0-11) et la signature `WW` ($57) occupe les octets
28-31, celle que teste `ne.c`. Une PROM 8 bits câblée sur les deux moitiés du bus donne
cette disposition, la même que QEMU. Elle est en lecture seule : une écriture Remote DMA
n'y change rien. Comme sur une vraie NE2000, la puce ne charge pas les PAR depuis la PROM :
c'est le pilote qui la lit et recopie la MAC. **MAC par défaut `02:4E:53:54:00:01`** :
adresse localement administrée (bit 1 du premier octet, « NST » en ASCII), fixe pour que
les runs restent déterministes. Aucun OUI de fabricant n'est documenté pour l'EtherNEC, qui
accepte n'importe quelle carte ISA NE2000, ni pour le NetUSBee ; une adresse inventée ne
doit pas usurper un OUI réel.

## Appareils MIDI de l'hôte — le câble DIN du ST vers du vrai matériel

Le port virtuel « NeoST MIDI OUT » (CoreMIDI / séquenceur ALSA) est une **source
passive** : un FluidSynth ou un DAW s'y abonne, mais un expandeur, une groovebox ou un
clavier maître ne s'abonne à rien. Il fallait donc un patchbay tiers pour relier NeoST à
du matériel. NeoST **choisit désormais lui-même** ses deux extrémités :

Trois backends, un seul comportement visible — `src/audio/MidiWinmm.hpp` porte les
particularités de Windows :

| | macOS | Linux | Windows |
|---|---|---|---|
| Backend | CoreMIDI | séquenceur ALSA | **winmm** |
| Destinations matérielles | ✅ | ✅ | ✅ |
| Sources matérielles | ✅ | ✅ | ✅ |
| Identifiant unique | `kMIDIPropertyUniqueID` | ❌ (nom seul) | **chemin d'interface USB** |
| Port virtuel « NeoST MIDI OUT » | ✅ | ✅ | ❌ — **pilote tiers** (loopMIDI) |
| Ouverture concurrente d'un port | multiplexée | multiplexée | **EXCLUSIVE** |

Deux différences de Windows méritent d'être connues avant de câbler un studio :

- **Aucun port virtuel n'est possible en natif.** Ni winmm ni WinRT MIDI 1.0 ne savent
  créer un port que les autres applications voient — il y faut un **pilote** (loopMIDI,
  LoopBe1). Une fois installé, son port apparaît dans les listes ci-dessous comme
  n'importe quel appareil, **en destination et en source** : le câblage vers un DAW est
  alors complet, sans que NeoST ait à s'en mêler. (Windows MIDI Services, la pile MIDI
  2.0 de Windows 11, sait le faire nativement mais s'installe à part et exigerait une
  pile WinRT/COM que le zip autonome ne peut pas emporter.)
- **Un port MIDI est exclusif.** Là où ALSA et CoreMIDI multiplexent, un DAW ouvert sur
  le clavier empêche NeoST de l'ouvrir : `MMSYSERR_ALLOCATED`, journalisé en clair
  (`device busy - another application holds it`).

L'**identifiant unique**, lui, est meilleur que sous Linux : `DRV_QUERYDEVICEINTERFACE`
rend le chemin d'interface du périphérique
(`\\?\usb#vid_1235&pid_0139&mi_00#6&33d600c&0&0000#{...}`), dont la partie médiane est
le chemin **physique** — hub et prise. Deux appareils du même modèle branchés ensemble
sont donc discernables, comme sous CoreMIDI. ⚠ Corollaire : **changer de prise USB change
l'identifiant** ; le repli par nom rattrape le cas, avec son avertissement au journal.
Les synthés logiciels du système (« Microsoft GS Wavetable Synth ») ne sont pas des
périphériques et rendent une chaîne vide : identifiant vide, repli par nom — le régime
dans lequel ALSA vit tout entier.

| Réglage | `neost.cfg` (UNE ligne, comme toute autre clé) | Effet |
|---|---|---|
| Destinations | `midi_out_devices=nom\|1,2,10-12;autre\|3` | chaque appareil reçoit les canaux nommés |
| Sources | `midi_in_devices=nom\|1;autre\|2` | chaque appareil entre dans le MIDI IN, forcé sur ce canal (0 = tel quel) |

Enregistrements séparés par `;`, champs par `|`, et ces caractères sont échappés par
`\` dans les noms. ⚠ Le format **répétable** de la première version (`midi_out_device=`
une fois par appareil) est encore LU, jamais écrit : `parseConfigLine` est partagé avec
les **profils nommés**, qui s'appliquent *par-dessus* la config courante — là où une clé
scalaire remplace, une clé répétable AJOUTAIT, si bien que charger un profil dupliquait
chaque appareil. Une clé, une ligne, une affectation.

**La sortie est un AIGUILLAGE, pas un Thru box.** Chaque destination porte le masque des
canaux qu'elle reçoit : « instrument 1 de Cubase vers le piano logiciel, instrument 2
vers la groovebox » se règle chez NeoST, sans toucher au réglage des appareils. Un même
canal peut partir vers plusieurs destinations (superposition). ⚠ Les messages **système**
(horloge, start/stop, SysEx) n'ont pas de canal et vont à **toutes** les destinations :
les filtrer désynchroniserait le studio.

**L'entrée est un boîtier de FUSION.** Le ST n'a qu'une prise MIDI IN ; plusieurs
appareils y sont réunis. Un tel boîtier ne mélange pas des octets, il entrelace des
**messages** : deux claviers joués ensemble émettent `90 3C 40` et `90 40 40` au même
instant, et entrelacés octet par octet ils donneraient `90 90 3C 40 40 40`, du charabia.
D'où un décodeur par source (`MidiMessageParser`, partagé avec la sortie). Le statut
n'est ré-émis dans le flux fusionné que s'il a **changé** : une source seule garde son
running status, deux sources qui alternent le voient correctement réinséré.

**Canalisation — sans elle, pas d'enregistrement multipiste.** Deux claviers émettent
tous deux sur le canal 1 par défaut : un séquenceur ne peut alors pas les séparer et
tout atterrit sur la même piste. Forcés sur 1 et 2, ils deviennent enregistrables
simultanément sur deux pistes. Ce que le séquenceur ST en fait le regarde — les Cubase
complets et Notator savent enregistrer plusieurs canaux sur plusieurs pistes, Cubase
Lite non.

GUI : Configuration → **MIDI**. La sortie est une **matrice** appareils × 16 canaux (un
clic par affectation, `all` / `none` par ligne) ; l'entrée, une liste d'appareils avec le
canal forcé de chacun. Headless : `--midi-list` énumère les entrées,
`--midi-in-device NAME` (**répétable**, fusionnées) et `--midi-in-channel N` (s'applique
à l'appareil précédent). Il n'y a pas de `--midi-out-device` côté headless :
`--midi-dump` capture déjà ce que le ST émet, sans dépendre du matériel branché — c'est
ce qu'un test veut.

Quatre choix de conception, chacun payé par un piège réel :

- **Désignation par NOM + IDENTIFIANT UNIQUE, jamais par index.** Débrancher un
  périphérique renumérote tous les autres : une config mémorisée en index se serait mise
  à piloter le mauvais appareil au branchement suivant. Mais le nom seul ne suffit pas
  non plus — deux machines du **même modèle** portent exactement le même nom. La config
  mémorise donc `kMIDIPropertyUniqueID` (CoreMIDI) à côté du nom, et l'appariement fait
  deux passes : identifiant d'abord, nom ensuite, en n'attribuant **jamais deux fois le
  même point de terminaison**. Deux claviers identiques s'ouvrent chacun sur le sien,
  y compris sous ALSA (pas d'identifiant stable, mais la non-réattribution suffit quand
  les deux sont branchés). L'identifiant est **appris** à la première ouverture, donc
  une config qui ne connaît qu'un nom devient sûre d'elle-même. Interface : suffixe
  « #1 / #2 » sur les homonymes. Logique d'appariement : `src/audio/MidiEndpoint.hpp`,
  fonction PURE — c'est ce qui la rend testable sans deux appareils identiques sous la
  main.
- **L'absence n'efface pas le réglage.** Un appareil débranché n'est pas une erreur de
  configuration : le nom reste dans `neost.cfg`, la page affiche *(not connected)*, et
  la boucle **re-tente à 1 Hz** — rebrancher le câble USB suffit, sans rouvrir la
  configuration. (C'est la leçon du `midi_out_port` retombé à 0 sans un mot.)
- **Panique avant de couper.** Un synthé ne relâche JAMAIS une note tout seul : fermer
  la destination en plein accord la laisserait tenue indéfiniment dans l'appareil.
  `closeDestination()` envoie donc CC 120/121/123 sur les 16 canaux **avant** de fermer.
- **C'est l'ACIA qui tire, à 31 250 bauds.** CoreMIDI livre ses paquets sur son propre
  thread quand ça lui chante ; `MidiInHost` accumule, et l'ACIA vient prendre un octet
  toutes les 2 560 cycles (`Scheduler::MIDI_RX`, le pendant de `MIDI_TX` et le même
  patron qu'`IKBD_RX` pour l'ACIA clavier). La première version poussait les octets une
  fois par TRAME, ce qui plafonnait l'entrée à 2 octets/trame — mesuré 1,76, soit
  ~143 o/s contre 3 125 o/s sur un câble : un accord de dix notes mettait 0,2 s à entrer.
  Après correctif, **40,4 octets/trame mesurés en temps réel (~2 885 o/s, 92 % du câble)**.
  Le débordement redevient celui du **matériel** : si le ST ne lit pas assez vite, le
  6850 perd l'octet **neuf** (garder l'ancien préserve le début des messages entamés).
  Éprouvé par `neost-selftest` sur le chemin complet ACIA + Scheduler, sans appareil.
  ⚠ Cette mesure n'est possible que dans le **GUI** : le headless émule ~19 fois plus
  vite que le temps réel, donc une source MIDI réelle y est toujours le facteur limitant.

**Profils d'appareil.** Cocher seize cases pour une machine dont le plan de canaux est
public est du travail perdu : `src/audio/MidiDeviceProfiles.hpp` pose le masque d'un clic
pour les appareils connus. Première entrée, le **Novation Circuit Tracks** (*Programmer's
Reference Guide* v3) :

| Piste | Canal | Notes |
|---|---|---|
| Synth 1 / Synth 2 | 1 / 2 | |
| MIDI 1 / MIDI 2 | 3 / 4 | pistes MIDI externes |
| **Drums 1-4** | **10** (les quatre) | 60 = Drum 1, 62 = Drum 2, 64 = Drum 3, 65 = Drum 4 |
| — | 16 | réservé au Project Control |

Les quatre percussions partagent donc **un seul canal** et se distinguent par la **note** :
pour les séquencer depuis le ST, une piste sur le canal 10 avec ces quatre hauteurs.
⚠ Défauts d'usine, réassignables dans le Setup View de la machine — la table du dépôt
n'accepte qu'une source constructeur **citée**, jamais une supposition.

**Avance de livraison** (`midi_lead_ms=`, curseur 0-100 ms). Chaque octet est reposé à
l'heure hôte correspondant à son cycle 68000, plus cette avance : c'est elle qui tue la
gigue de trame (σ 0,4-1,7 ms mesurée sur l'étalon Cubase), et c'est elle qu'on paie en
latence quand on joue au clavier à travers le ST. Mesuré : 190 ms d'écart de livraison
pour 200 ms commandés. ⚠ Elle ne protège **pas** du cas courant — l'émulation d'une trame
prend moins de temps réel qu'une trame, donc à 0 ms aucun octet n'est en retard sur un run
sain — mais du **rattrapage** quand la boucle GUI décroche (drag de fenêtre, rafale
disque). Le compteur d'octets « sent late » de la page MIDI est le témoin : on baisse
jusqu'à ce qu'il bouge.

Sous Linux, les destinations matérielles sont **adressées explicitement**
(`snd_seq_ev_set_dest`) et NON abonnées comme le ferait `aconnect` : un abonné recevrait
le flux entier, ce qui interdirait le filtrage par canal — c'est-à-dire tout l'intérêt de
l'aiguillage. Choisir une destination fait tout de même exister le port « NeoST MIDI OUT »
même si sa case est décochée : sans port source, il n'y aurait rien d'où émettre.

Aiguillage et fusion vérifiés le 2026-08-29 (macOS, deux destinations virtuelles) : la
panique de fermeture diffuse des CC sur les 16 canaux, et chaque destination n'a reçu que
les siens (`B0 78 00…` pour celle du canal 1, `B1 78 00…` pour celle du canal 2, 9 octets
chacune sur 48 messages), tandis que le SysEx de démarrage de MROS est arrivé aux **deux**.

Vérifié le 2026-08-29 (macOS, Novation Circuit Tracks + appareils virtuels de test) :
255 octets entrés dans l'ACIA depuis une source hôte (0 perdu, contre 2 octets pour un
appareil qu'on ne touche pas), et le SysEx de démarrage de MROS reçu par une destination
matérielle **port virtuel coupé** — donc portée par la destination seule. Le backend
ALSA est écrit mais n'a **pas** été exécuté (pas de machine Linux dans la boucle).
`src/audio/MidiInHost.cpp`, `src/audio/MidiOutHost.cpp`.

Vérifié le 2026-08-31 (**Windows 11, matériel réel, Novation Circuit Tracks en USB**) :
`neost-headless --midi-list` énumère l'appareil avec son identifiant d'interface ;
`--midi-in-device "Circuit Tracks"` fait entrer **35 octets d'horloge MIDI dans l'ACIA
en 1,3 s** (0 perdu) — l'appareil émet son horloge en continu, ce qui donne une source
de test déterministe, sans les mains ; le GUI ouvre les deux extrémités d'un coup
(destination + source canalisée), **apprend l'identifiant** et livre **0 octet en
retard**. Gamme, SysEx, running status et panique éprouvés sur l'appareil réel par un
harnais pilotant `MidiOutHost` directement.

**Résolution du minuteur système** (`timeBeginPeriod(1)`, posée tant qu'une sortie est
ouverte) : sans elle Windows réveille le thread de livraison par tranches de ~15,6 ms, et
tout le mécanisme d'horodatage (ancre + `midi_lead_ms`) serait arrondi à cette tranche.
Mesuré A/B sur 40 échéances de 50 ms vers le Circuit Tracks :

| | intervalle moyen | σ | écart max |
|---|---|---|---|
| minuteur 1 ms **posé** | 50,00 ms | **0,30 ms** | 0,90 ms |
| minuteur 1 ms absent | 50,09 ms | 5,34 ms | 13,10 ms |

## Anneau MIDI réseau — MIDIMaze en ligne (`--midi-net H:P[:L]`)

MIDI Maze relie les ST en **anneau** par câbles MIDI (OUT de l’un → IN du suivant).
`MidiAcia::setMidiSink` transmet les octets achevés vers le pair aval. Les datagrammes
amont sont collectés une fois par trame dans le tampon de gigue de `MidiRing` ;
`tryPop` alimente ensuite la source série à **un octet toutes les 2560 cycles**.
Il n’y a pas d’appel UDP par octet. Le 6850 n’a qu’un seul RDR : si le CPU ne le lit
pas, le caractère suivant est perdu et l’overrun matériel est signalé. Le tampon
réseau ne constitue pas une extension du RDR.

En headless, un anneau ouvert prend la place des appareils MIDI IN hôtes sur cette
entrée DIN ; si les deux ont été demandés, le choix est signalé dans le journal.
Le contrôle local `tests/midi_ring_logic.cpp` reçoit 20 octets dans l’ordre en
51200 cycles après une seule collecte UDP (127.0.0.1 uniquement). Le test historique
`MIDITEST.TOS` (`dev/netdemo/`) avait aussi validé le round-trip OUT→UDP→IN→ACIA.
Implémentation : `src/net/MidiRing.cpp`.

## UltraSatan — interface SD/MMC sur le bus ACSI (`--ultrasatan`, GUI Hard disks)

C'est **la** réponse historique de l'écosystème ST au stockage moderne (Jookie, 2009) :
un boîtier ACSI à **deux slots SD/MMC** — chaque slot est une cible ACSI (IDs *n* et
*n+1*, usine 0-1) — et une **horloge temps réel** sauvegardée par pile. Les pilotes
d'époque (HDDRIVER, ICD PRO, AHDI, **EmuTOS sans pilote**) le voient comme deux disques
SCSI ordinaires ; l'outil `US_CONF.TOS` lui parle par des **paquets ICD propres** :

```
octet 0 : (ID << 5) | $1F         marqueur ICD
octet 1 : $20                     groupe 1 (10 octets) — jamais un opcode SCSI utile
octets 2-3 : 'U','S'              signature UltraSatan
octets 4-7 : code ASCII           CurntFW · RdCl/WrCl · RdINQRN/WrINQRN · RdSt/WrSt · RdLog · RdFW/WrFW
octets 8-10 : 3 paramètres        magie 'RTC' (WrCl), $83 $03 $17 (WrSt)
puis UN secteur (512 octets) par DMA, dans le sens de la commande
```

| Élément | NeoST (`src/io/UltraSatan.*`, `Acsi`) | Source de vérité |
|---------|----------------------------------------|------------------|
| INQUIRY | `JOOKIE  ` + nom (10 car., défaut `UltraSatan`) + n° de slot `'1'`/`'2'` + `1.20` + date, bit **RMB** | `scsi6.c SCSI_Inquiry` |
| Slot sans carte | TEST UNIT READY / READ CAPACITY / READ / WRITE → CHECK CONDITION, clé **NOT READY**, ASC `$3A` (medium not present) ; INQUIRY répond quand même | `ReturnStatusAccordingToIsInit` |
| `USCurntFW` | secteur = `UltraSatan v1.20 (NeoST emulation)`, 0-terminé | `special.c` |
| `USRdClRTC` / `USWrClRTC` | `'R','T','C'` + `{année−2000, mois, jour, h, min, s}` binaires ; écriture = magie `RTC` dans le **paquet ET le secteur** | `special.c`, `rtc.c` |
| `USRdINQRN` / `USWrINQRN` | nom INQUIRY (10 octets ; `$00`/`$FF` en tête = nom d'usine) | `special.c` |
| `USRdSt` / `USWrSt` | page de réglages 512 octets (opaque, octet 1 = firmware amorcé) ; écriture **refusée sans la magie** `$83 $03 $17` | `special.c` |
| `USRdLog` | journal de commandes (vide) | `special.c` |
| `USRdFW` / `USWrFW` | **refusés** (CHECK CONDITION) : NeoST n'émule pas la dataflash — impossible de briquer l'appareil | — |
| Horloge | pile propre, **indépendante** du RP5C15 Mega : seedée sur l'hôte, avance avec les **cycles émulés** (comme `Rtc`) | `rtc.c` (année base 2000) |

Sources : firmware v1.20 ([atarijookie/ce-atari `ultrasatan/`](https://github.com/atarijookie/ce-atari/tree/master/ultrasatan)).
Hatari n'a aucun équivalent (disque ACSI générique « Hatari Emulated Harddisk »). Toute
cible qui n'est pas un slot UltraSatan reste **byte-identique** au port de `hdc.c`.

| Où | Comment |
|----|---------|
| Headless | `--ultrasatan` (IDs 0-1), `--ultrasatan-id N`, `--sd1 IMG`, `--sd2 IMG` (`--acsi IMG` = slot 1 quand l'ID est 0) |
| GUI | Configuration → **Hard disks** : case UltraSatan, slot 1 = l'image ACSI, slot 2 à monter ; `neost.cfg` : `ultrasatan=`, `sd2=` (rejoués par `--from-cfg` depuis le 2026-10-20 : cibles 0-1, slot 2 lu seulement si la case est cochée) |
| Code | `machine.enableUltraSatan(firstTarget); machine.fdc.mountAcsi(path, firstTarget + slot);` |

Image de carte SD : n'importe quel dump brut partitionné (Atari ou DOS). `tools/make_usatan_hd.py`
en génère une minimale (16 Mo, une partition GEM FAT16) qu'EmuTOS monte en **C:** sans pilote.

## NetUSBee — NE2000 + hôte USB ISP1160 sur le port cartouche (`--netusbee`, GUI Network)

Le [NetUSBee](https://hardware.atari.org/netusbee/netus.htm) = une **RTL8019AS câblée
exactement comme l'EtherNEC** (pilotes rétro-compatibles — NeoST réutilise `io/Ne2000`
tel quel) + un **ISP1160** (hôte USB 1.1, deux ports) sur le même port cartouche. Le port
étant en lecture seule, 16 bits, sans A0, le pilote FreeMiNT (`sys/usb/src.km/ucd/netusbee/
isp116x.h`) encode tout dans l'adresse :

```
$FA0000 + (b << 1)   LSB_WRITE      : verrouille l'octet b
$FB8000 + (b << 1)   MSB_DATA_WRITE : écrit le MOT (b << 8) | latch dans le port DONNÉES
$FBC000              MSB_CMD_WRITE  : écrit le MOT latch dans le port COMMANDE (index | $80 = écriture)
$FA8000              DATA_READ      : lit un mot 16 bits du port DONNÉES (registres 32 bits : bas puis haut)
```

`src/io/Isp1160.*` modélise le contrôleur : `HcChipID` = `$6120` (masque `$FF00` = `$6100`),
`HcSoftwareReset` (`$F6`) et `HcCommandStatus.HCR` auto-effacé, registres OHCI (`HcControl`,
`HcFmInterval`, `HcRhDescriptorA` avec **NDP = 2 figé**, `HcRhPortStatus1/2`…), registres ISP
(`HcHardwareConfiguration`, `HcuPInterrupt`/`Enable`, `HcScratch`, `HcITL/ATLBufferLength`,
`HcBufferStatus`), **FIFO ATL** : les PTD écrits sont « exécutés » à chaque trame et relus avec
**CC = 5 (DeviceNotResponding)**, `Active = 0` — c'est un **hub racine vide** : les pilotes
(FreeMiNT `netusbee.ucd`, NetUSBee TOS) s'initialisent, n'énumèrent rien. Ligne IRQ modélisée
(`irqAsserted()`), non câblée (les pilotes ST tournent en polling).

⚠ **Divergence possible, consignée** : la fenêtre `LSB_WRITE` (`$FA0000-$FA01FF`) est AUSSI
celle des écritures du registre 0 (CR) de la NE2000. Sans schéma du NetUSBee, NeoST applique
ce que les adresses publiées impliquent : **les deux puces voient l'accès**. Si le vrai
matériel gate l'une des deux, ce point est à corriger ici (`Bus::read8Slow`).

| Où | Comment |
|----|---------|
| Headless | `--netusbee` (backend boucle locale ; exclusif de `--cart`) |
| GUI | Configuration → **Network** : case NetUSBee (exclusive d'EtherNEC) ; `neost.cfg` : `netusbee=` |
| Code | `machine.enableNetUsbee()` (= `enableEtherNec()` + ISP1160) |

Point d'extension suivant : brancher un périphérique USB hôte (clavier/souris/stockage) —
l'ATL/ITL et le hub racine sont là, il manque un « device » derrière `HcRhPortStatus`.

## Tests UltraSatan + NetUSBee

* `neost-headless --usatan-selftest` — 15 vérifications au niveau **fil** ($FF8604/06, séquence
  **LongRW** de `US_CONF` : A1 bas/haut, bascule R/W + compteur de secteurs AVANT le dernier
  octet, statut après transfert) : INQUIRY des deux slots, slot vide (NOT READY), slot avec
  carte (TEST UNIT READY, READ CAPACITY, READ(6)), `CurntFW`, `RdCl`/`WrCl` (horloge figée),
  `RdINQRN`/`WrINQRN`, `RdSt`/`WrSt`, refus flash/inconnu, **verrouillage** du paquet `'US'`
  aux cibles UltraSatan. Palier `fast` (`tools/etalons.json`, type `usatan_selftest`).
* `neost-headless --netusbee-selftest` — 11 vérifications : les primitives **exactes** du pilote
  FreeMiNT (`raw_read/write_data16/32`, lectures MOT), ID de puce, scratch, reset logiciel,
  registres 32 bits, hub racine vide, ATL → CC = 5, IRQ, NE2000 toujours décodée, save-state.
  Palier `fast` (type `netusbee_selftest`).
* `tools/selftests.json` → **`usatan_netusbee`** (palier `fast`, verdict série) : une carte SD
  générée (`tools/make_usatan_hd.py`, 16 Mo, partition GEM FAT16) qu'EmuTOS monte en **C:** sans
  pilote et dont il lance **`AUTO\USTEST.PRG`** — le programme de test 68000 RELOGEABLE de
  `tools/make_usatan_test.py` (table de relocation TOS générée, `Super()` puis `Pterm0`), qui parle
  à l'UltraSatan **comme `US_CONF`** (séquence LongRW, attente IRQ GPIP5) et au NetUSBee **comme le
  pilote FreeMiNT** (lectures MOT, primitives raw), et vérifie `_drvbits` bit 2. Verdicts `usfw
  usinq usrtc uscdrv nubid nubscr nubnic`. Trois règles EmuTOS apprises en construisant ce test
  (`bios/blkdev.c`, `bios/disk.c`) : dès qu'un disque dur existe, la **disquette n'est plus
  amorcée** ; un **secteur racine exécutable n'est lancé que sur un disque SANS partition
  reconnue** (`disk_try_dmaboot`) ; et le type de FAT suit la **règle Microsoft** (≤ 4084 clusters
  = FAT12 — une partition de 2 Mo y était lue en FAT12, d'où 16 Mo), là où Atari TOS suppose un
  FAT16 sur tout disque dur. `make_usatan_test.py OUT.st` produit la **disquette** équivalente
  (`A:\AUTO`) pour tester SANS carte SD : `uscdrv` échoue alors, c'est attendu. **Contrôle
  négatif** : la même carte sous Hatari (`--acsi`, `--rs232-out`) donne `uscdrv PASS` et les six
  verdicts matériels **FAIL** — le test ne passe pas par construction.
* Save-states : **v12** (UltraSatan + ISP1160 sérialisés) ; drapeaux d'en-tête (v13) bit2 = UltraSatan,
  bit3 = NetUSBee — les configs save/load doivent concorder).

## Clé Steinberg — dongle Cubase sur le port cartouche (`--dongle`, GUI Dongles)

La protection de Cubase ST : une clé dans le port cartouche, lue en `$FB0000-$FBFFFF`
(/ROM3 — le TOS ne sonde que /ROM4 `$FA0000`, la clé lui est invisible et cohabite avec
le HD GEMDOS). Pas d'écriture possible sur ce port : le **défi** voyage sur les lignes
d'adresse (A1-A8) d'une lecture « fantôme », la **réponse** revient sur D8-D15.

| Clé | Puce | Logiciels | Horloge | Émulation |
|-----|------|-----------|---------|-----------|
| **rouge** (`cubase3`) | EPLD Intel 5C060, 16 bascules T, 1 entrée (A8), 1 sortie (D8) | Cubase 3.10, Cubase Score 2.0x, Cubase Audio Falcon | front montant de **/ROM3** : seuls les accès `$FBxxxx` la font avancer | fidèle (équations du JED décapsulé, décompilé par troed) |
| **noire** (`cubase2`) | PAL16R8, 8 bascules D, A1-A8 → D8-D15 | Cubase 2.01 | **chaque** front montant de /UDS, où que lise le CPU — fetchs compris | « au mieux » : exige le motif bus d'un vrai 68000 (Moira modélise le prefetch) ; crochet `udsDone` dans `NeostMoira` |
| `auto` | — | — | — | heuristique MiSTery : premier accès `$FBxxxx` avec A7..A1 = 0 → rouge, sinon noire |
| **C-Lab** (`notator`) | EP600 (= 5C060), 8 bascules D actives bas + 1 bascule d'armement | Notator, Creator (clé seule ou intégrée à l'Unitor-N) | **armement** : `FEEDB1 := STER` sur /ROM4 (STER = A8..A1 = `$75`, soit `$FA00EA`) ; **données** : désarmée → UDS de chaque cycle (comme la noire), armée → **descente** de /ROM3 (le CPU lit l'état *après* le coup d'horloge) ; resets asynchrones D9 (A4·A2) et D8 (A3·A1) pendant l'accès | fidèle aux équations TPH (JED EP600 décapsulé par Zippy, relevé Unnamed Villain, publié 10/2025 ; transcription C du firmware SidecarTridge `md-notator`) ; tout terme contient STER → l'armement remet les 8 bascules à 0, le motif bus exact ne compte donc pas |

### Oracle : trace de référence et rejeu (`--key-log`, `--key-replay`)

Aucun logiciel à clé n'est dans le dépôt : « fidèle aux équations » est une promesse
tant que rien ne la mesure. L'oracle est une **trace de référence** — une ligne par
signal du port cartouche, telle qu'un analyseur logique ou un SidecarTridge (`md-notator`
voit /ROM3, /ROM4 et A1-A8) peut la produire sur une vraie machine :

```
R3 <A8..A1 hex> <octet D15..D8 lu>   lecture sous /ROM3 ($FBxxxx)
R4 <A8..A1 hex>                      lecture sous /ROM4 ($FAxxxx) — armement Notator
U  <A8..A1 hex>                      cycle /UDS hors fenêtre cartouche (clés cadencées par UDS)
#  commentaire
```

`neost-headless --dongle MODEL --key-log FILE` écrit cette trace pendant une session ;
`neost-headless --dongle MODEL --key-replay FILE` rejoue n'importe quelle trace (capture
matérielle ou journal NeoST) contre la machine d'état **sans machine** et sort 0 si tous
les `R3` concordent, 1 avec la première ligne en écart. C'est le test qui tranchera les
incertitudes de chronologie (front de /ROM4, ordre UDS↔/ROM4 à l'armement) le jour où une
capture existe — et il vaut pour les trois clés. ⚠ Une clé cadencée par UDS (noire,
Notator) journalise **chaque** cycle bus : ~30 000 lignes par trame. Un auto-test rejoue
une trace écrite par NeoST (0 écart) et une trace altérée (1 écart localisé). La page
Dongles affiche en direct le nombre de sondages, le dernier octet rendu, l'état et
l'armement.

### Recette de capture matérielle (à faire quand une clé et un logiciel sont réunis)

1. Sonder A1-A8, D8-D15, /ROM3, /ROM4, /UDS sur le port cartouche (analyseur logique ≥ 50 MHz,
   ou firmware SidecarTridge dérivé de `md-notator` qui journalise au lieu de répondre).
2. Échantillonner au front montant de /ROM3 (données valides) → `R3`, au front montant de
   /ROM4 → `R4`, au front montant de /UDS hors $FA/$FB → `U`.
3. `neost-headless roms/<tos>.img --dongle notator --key-replay capture.trace`.

Pour la clé C-Lab, les accès `$FAxxxx` du **GEMDOS HD** de NeoST (qui vit sur /ROM4)
**désarment** la clé (`FEEDB1 := 0`) — Notator le tolère s'il réarme à chaque contrôle,
comme le ferait n'importe quel accès cartouche du TOS sur une vraie machine.

Source des équations : cœur FPGA **MiSTery** (`atarist/cubase2_dongle.v`,
`cubase3_dongle.v`, gyurco) — clé noire relevée par force brute sur une clé réelle
(MasterOfGizmo, 2022), clé rouge depuis le JED de la puce décapsulée. Transcription en
C++ dans `src/io/CartridgeKey.cpp` (classe `CartridgeKey`, abonnée au port cartouche via
`core/CartDevice.hpp`), épinglée par `neost-selftest` (propriétés : registres
à 0 au reset, motif de reset logiciel `%11011000` → 0 sur la noire, /UDS sans effet sur
la rouge). Une lecture renvoie l'état **courant** (les bascules basculent en fin de
cycle) ; octet faible `$FF`. Sérialisé dans le save-state (v14, drapeau bit 4).

⚠ **Non validée sur logiciel** : aucun Cubase à clé n'est dans le dépôt (Cubase Lite n'en
a pas besoin). Recette quand on en aura un : `neost-headless roms/tos104fr.img --dongle
cubase3 --disk cubase310.st --frames 3000 --screenshot s.ppm` ; si bombes ou « dongle
not found », tracer `--trace` autour des lectures `$FB0000` (la rouge lit avec A7..A1 = 0,
A8 = bit de défi).

---

## Périphériques des ports — un par port : clés joystick / série, DAC Pro Sound, boutons (`--plug`, GUI Dongles)

Tout ce qui se branchait sur un port **autre que la cartouche** pour qu'un logiciel le
sonde. Classe `PortDevices` (`src/io/PortDevices.{hpp,cpp}`) : le modèle est **physique**,
un périphérique optionnel par port — joystick 0, joystick 1, RS-232, imprimante, bouton de
cartouche — et ils **coexistent** (Leader Board dans le joystick 1 + DAC Pro Sound sur
l'imprimante + clé Cubase dans la cartouche, comme sur une vraie machine). On peut aussi se
tromper de port, comme avec l'objet : une clé joystick entre dans les deux DE-9, le jeu
n'en sonde qu'un (le GUI signale « wrong port for this game »). `--plug PORT=DEVICE`
(répétable) ou `--adapter DEVICE` (port par défaut) en headless ; `joy0=`, `joy1=`,
`rs232=`, `printer=`, `cartbutton=` dans `neost.cfg` (l'ancien `adapter=` est relu) ; page
**Dongles**. `--from-cfg` rejoue ces cinq clés ainsi que `dongle=` et `auto_dongle=` par
les mêmes chemins que `--plug` et `--dongle` (depuis le 2026-10-20). Un identifiant inconnu
est ignoré avec un avertissement, comme le GUI l'oublie. **OFF par défaut**, sans effet sur les étalons. **Sérialisé** dans le
save-state (v16) : l'oscillateur de Cricket et la date de Music Master font partie du
déterminisme, et les périphériques branchés sont restaurés avec l'état.

**Branchement automatique** : `disks/dongles.txt` (créé avec les titres connus s'il manque,
éditable) associe un motif du nom d'image à un branchement (`leader board = joy1:leaderboard`,
`notator = cart:notator`). Au montage d'une disquette (GUI, borne, `--disk`), NeoST remplit
les emplacements **vides** seulement — un réglage explicite prime — et l'annonce. `auto_dongle=0`
ou `--no-auto-dongle` pour désactiver. Logique pure dans `io/DongleTable`, auto-testée.

| `DEVICE` | Logiciel | Port sondé | Ce que fait la clé | Source |
|----------|----------|------|--------------------|--------|
| `leaderboard`, `10thframe` | Leader Board, 10th Frame (Access) | joystick 1 | cavalier HAUT+BAS : le jeu lit les deux bits à 1, impossible avec un vrai joystick | Steem `ikbd.cpp`, WinUAE `JOY1DAT == 0x0101` |
| `cricket`, `soccer` | Cricket Captain, Multi Player Soccer Manager (D&H) | joystick 0 | oscillateur : nibble direction `%1100` ↔ `%1101` à chaque sonde IKBD (« must continuously change state ») | Steem, WinUAE |
| `rugby` | Rugby Coach (D&H) | joystick 1 | idem | Steem |
| `bat2` | B.A.T. II (Ubi Soft) | RS-232 | CTS (GPIP2) lu à 0 en permanence (sur ST ; l'Amiga exige une impulsion DTR). ⚠ NeoST et Hatari lisent déjà CTS à 0 au repos : la clé est redondante, gardée pour la cohérence avec Steem | Steem `ior.cpp` |
| `musicmaster` | Music Master (Computer's Dream) | RS-232 | DTR recopié sur DCD (GPIP1) avec **~200 cycles** de retard : la première lecture après le basculement rend encore l'ancienne valeur | Steem « inspired by WinUAE » |
| `jeannedarc` | Jeanne d'Arc (Chip) | RS-232 | DCD assertée quand le mot (RTS\|DTR) **décroît sans s'annuler** : `DCD = !(new && new < old)` | Steem `iow.cpp` |
| `prosound` | Wings of Death, Lethal Xcess (Thalion) | parallèle | **pas une clé** : DAC 8 bits R-2R Pro Sound Designer (Eidersoft) sur le port imprimante — samples sur STF sans son DMA. R15 horodaté et rejoué par le YM2149 avec son propre bloc DC (amorcé au branchement : pas de clic) et son **fader** page Sound (`mix_dac=`) | Steem `SSE_DONGLE_PROSOUND` |
| `multiface` | Multiface ST (Romantic Robot) | cartouche + câble moniteur | bouton **freeze** : GPIP7 (détection moniteur) tiré à 0 le temps de l'appui → IRQ niveau 7 prise par la ROM (`--cart`) ; relâché à la VBL suivante. Bouton : page Dongles, `--button-at N` | Steem `run.cpp`/`options.cpp` |
| `urc` | Ultimate Ripper (Gotcha) | cartouche + port série | même idée sur la ligne RI (GPIP6) : l'appui ASSERTE RI (bit 6 → 0, repos à 1 depuis le 2026-10-20), relâchée à la VBL suivante — réaction attendue du logiciel non vérifiée | Steem |

Câblage NeoST : joystick → `Ikbd::setJoystickProbe` (par-dessus l'état hôte) ; série →
abonné port A du PSG (`Machine`) + crochet de lecture `$FFFA01` (`Mfp::setGpipReadHook`,
B.A.T. II / Music Master sont **sondés**, pas armés en IRQ) ou ligne MFP avec front AER
(Jeanne d'Arc, boutons) ; parallèle → `YM2149::setPortBDac`. `Machine::portsApply` remet
les lignes au repos quand on débranche (DCD repos actif, RI, GPIP7). Auto-tests de logique
pure dans `neost-selftest` (un par protocole, plus connecteurs et coexistence).

⚠ **Validation** : les protocoles sont transcrits de Steem/WinUAE, aucun des jeux à clé
n'est dans le dépôt. Wings of Death (présent, `disks/stx`) prouve seulement que le DAC
atteint le mixeur (`--sound-dump` diffère d'un échelon continu filtré) — l'option « Pro
Sound » du menu du jeu reste à exercer.

### Inventaire des dongles ST connus (recherche 2026-08-23) et pourquoi le reste n'est pas émulé

| Logiciel | Port | Matériel | Statut |
|----------|------|----------|--------|
| Cubase 2.01, Avalon 2.1, Synthworks Wavestation | cartouche | **clé noire** PAL/GAL16V8 (routine « A » du forum exxos) | Cubase 2 émulé (équations MiSTery) ; Avalon/Synthworks : même famille, **équations distinctes non relevées** |
| Cubase 3.10, Score 2.0x, Audio Falcon | cartouche | **clé rouge** EPLD 5C060 | émulé |
| Notator / Creator (C-Lab), Unitor-N | cartouche | EP600 ; JED extrait par décapsulation (Zippy), équations publiées par TPH en octobre 2025 (atari-forum t=43078, 1er message), transcrites en C dans le firmware SidecarTridge [`md-notator`](https://github.com/MrYoIt/md-notator) (2026) | **émulé** (`--dongle notator`, voir plus haut) |
| Log 3 (Emagic : Notator Logic) | cartouche | EP600 + EP330 (2 puces) ; seule la partie EP600 est publiée | non émulé (EP330 non relevée) |
| Pro-24 / Twenty Four, Proscore | cartouche | GAL16V8 | aucun relevé public (Steem : « failed like an old dog ») ; Pro 24 v2.1 tourne sans clé |
| Music Master (Computer's Dream) | série | — | émulé |
| Zero-X, Virtuoso, SY77 SWS, X-Analyzer, SoundPool (clé + fichier licence) | cartouche | inconnu | aucun relevé |
| Zodiac (astrologie) | joystick 1 | LED clignotante | protocole inconnu |
| Dames Grand-Maître, Italy '90, Logistix, Scala, Striker Manager, Football Director 2 | joystick (Amiga) | résistances/condensateurs sur POTX/POTY | **Amiga seulement** (ports analogiques) — WinUAE |
| Robocop 3 | — | — | sur ST : protection disque + codes du manuel, pas de clé |
| NeoN Grafix 3D | LAN Falcon | boucle TX→RX dans un boîtier de starter | hors périmètre (Falcon) |
| DynaBlaster, Jeanne d'Arc, B.A.T. II, Leader Board, 10th Frame, Cricket Captain, Rugby Coach | joystick / série | voir ci-dessus | émulés sauf DynaBlaster (protocole inconnu) |

Sources : Steem SSE (`steem/headers/stports.h`, `ior.cpp`, `iow.cpp`, `ikbd.cpp`, `run.cpp`,
miroir github.com/mattiasgustavsson/steem-crt), WinUAE `dongle.cpp`, AtariForumWiki
« Dongle protections », atari-forum « Dongle Protections » (t=14437), « Notator Dongle Dump »
(t=43078, équations Notator dans le 1er message), « Cartridge keys and emulation »
(exxosforum t=2781), MiSTery `cubase*_dongle.v`, firmware `md-notator` (`rp/src/notator_dongle.c`).
