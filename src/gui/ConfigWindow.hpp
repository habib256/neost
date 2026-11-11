// =============================================================================
//  ConfigWindow.hpp — la fenêtre « Configuration » et celle des disquettes.
//
//  Elle ne fait RIEN elle-même : tout sort en requêtes (ConfigUi::req*) que la
//  boucle consomme en fin de trame, seul endroit qui sait enchaîner un reset et
//  persister neost.cfg. C'est la discipline de MediaPages, à l'échelle d'une
//  fenêtre entière — et c'est ce qui permet à ces 800 lignes de vivre ici.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "io/CompDisks.hpp"
#include "audio/MidiEndpoint.hpp"
#include "audio/MidiOutHost.hpp"
#include "gui/AppConfig.hpp"
#include "core/MachineType.hpp"

struct App;
class Machine;

using neost::appconfig::Config;

// ─────────────────────────────────────────────────────────────────────────────
// Contenu des fenêtres de supports et pages de « Configuration ». Ce sont des
// FRAGMENTS (pas de Begin/End). Aucun ne monte ni ne démonte quoi que ce soit —
// elles remplissent des requêtes consommées en fin de trame, seul endroit qui sait
// enchaîner un reset et persister neost.cfg.
// ─────────────────────────────────────────────────────────────────────────────

// Page « Disquettes » : lecteurs A et B + la ludothèque de disks/.
// mountedA/mountedB = chemins montés (vides si lecteur vide).
// ─────────────────────────────────────────────────────────────────────────────
// Fenêtre « Configuration » — réglages de la machine.
//
// Elle remplace six sous-menus (Modèle, Mémoire, ROM, Cartouche, Disque dur,
// Résolution/Joystick/Son). Les disquettes, que l'on change couramment en jouant,
// vivent volontairement dans leur propre fenêtre indépendante.
//
// Deux principes qui expliquent la forme :
//  · Elle ne fait RIEN elle-même. Tout sort en requêtes (`ConfigUi::req*`) consommées
//    en fin de trame, seul endroit qui sait enchaîner reset + persistance. C'est la
//    discipline des anciennes bibliothèques, conservée.
//  · Les quatre réglages qui EXIGENT de reconstruire la machine (modèle, RAM, FPU,
//    ROM) sont mis EN ATTENTE au lieu de relancer à chaque clic : on les empile, le
//    pied de page les compte, « Appliquer et redémarrer » relance une seule fois.
//    Les montages, eux, restent immédiats — monter est une action, pas un réglage.
// ─────────────────────────────────────────────────────────────────────────────
struct ConfigUi {
    // Dossiers scannés (résolus une fois au démarrage).
    std::string disksDir, cartsDir, romsDir, hdDir, gemdosDir;
    const neost::CompDiskCatalog* compDisks = nullptr;   // menus de la scène (App::compDisks)
    std::string profDir;          // dossier des profils nommés (profiles/)
    // Lecture de l'état courant.
    const Config* cfg = nullptr;
    Machine* machine  = nullptr;
    float volume = 1.0f;          // volume maître courant (0..1)
    bool  color  = true;          // moniteur courant : couleur (vs mono)
    bool  driveSound = false;     // son du lecteur de disquettes
    bool  driveSoundAvail = false;    // échantillons roms/drivesound/ chargés (sinon : réglage sans effet)
    std::string curGemdos, curAcsi;   // chemins RÉSOLUS des disques durs montés
    std::string curSd2;               // image du slot 2 de l'UltraSatan (résolue)

    // Réglages matériels EN ATTENTE (cf. « Appliquer et redémarrer »).
    std::string pendMachine, pendMem, pendRom;
    bool pendFpu  = false;
    bool pendInit = false;        // faux tant que les champs ci-dessus n'ont pas été semés

    // Requêtes sortantes (toutes consommées puis remises à zéro par l'appelant).
    std::string reqMountA, reqMountB, reqMountCart, reqMountGemdos, reqMountAcsi;
    bool reqEjectA = false, reqEjectB = false, reqEjectCart = false;
    bool reqEjectGemdos = false, reqEjectAcsi = false;
    int  reqModem = -1;                   // modem Hayes RS-232 (0/1)
    int  reqEther = -1;                   // EtherNEC NE2000 (0/1)
    // Sorties MIDI (page Sound) : synthé GM, port CoreMIDI, MT-32 (0/1) ; modèle 0 auto/1 MT-32/2 CM-32L.
    int  reqMidiOutGm = -1, reqMidiOutPort = -1, reqMidiOutMt32 = -1, reqMt32Model = -1;
    bool reqMidiPanic = false;            // bouton « All notes off » de la page MIDI
    int  reqMidiLoopback = -1;            // case OUT->IN de la page MIDI
    // Appareils MIDI hôtes (page MIDI). Les listes sont RAFRAÎCHIES PAR LA BOUCLE, pas
    // par la page : énumérer CoreMIDI 60 fois par seconde pour dessiner deux menus
    // déroulants serait absurde, et un appareil branché à chaud n'a pas besoin
    // d'apparaître à la milliseconde (cf. le rafraîchissement 1 Hz de la boucle).
    std::vector<neost::midi::Endpoint> midiOutDevs, midiInDevs;   // lecture : ce qui est BRANCHÉ
    std::vector<MidiOutHost::Dest> midiOutOpen;         // lecture : destinations OUVERTES
    std::vector<std::string> midiInOpen;                // lecture : sources OUVERTES
    // Requête : l'ensemble voulu, appliqué et persisté d'un bloc (un clic dans la
    // matrice change une case, pas un appareil entier — on renvoie donc tout l'état).
    bool midiDevsDirty = false;
    std::vector<Config::MidiOutDev> reqMidiOut;
    std::vector<Config::MidiInDev>  reqMidiIn;
    uint64_t midiInBytes = 0;             // lecture : octets entrés dans le ST (preuve de vie)
    uint64_t midiLateBytes = 0;           // lecture : octets sortis en retard (avance trop courte)
    int  reqMidiLead = -1;                // requête : nouvelle avance en ms
    int  reqDongle = -1;                  // clé cartouche : 0 none, 1 cubase2, 2 cubase3, 3 auto, 4 notator
    int  reqPlugPort = -1, reqPlugDev = -1; // page Dongles : brancher reqPlugDev sur reqPlugPort
    bool reqPortButton = false;           // bouton Multiface / Ultimate Ripper (page Dongles)
    std::string mt32Status;               // lecture : modèle chargé ou erreur
    std::string gmStatus;                 // lecture : SoundFont chargée ou erreur (synthé TSF)
    // Mixeur (page Sound) : édité EN PLACE par la page ; mixDirty = appliquer, mixDone = persister.
    float mixYm = 1.0f, mixDma = 1.0f, mixDrive = 1.0f, mixMt32 = 1.0f, mixDac = 1.0f, mixGm = 1.0f;
    bool  mixInit = false, mixDirty = false, mixDone = false;
    int  reqNetUsbee = -1;                // NetUSBee NE2000 + ISP1160 (0/1)
    int reqUsbPort = -1;
    std::string reqUsbSpec;
    char usbImage[2][1024] = {};
    bool usbReadOnly[2] = {};
    bool natInit = false, reqNatOptions = false;
    bool natRestricted = false, natAllowLoopback = false;
    char natForwards[2048] = {};
    int  reqNat = -1;                   // NE2000 : Internet réel via NAT intégré (0/1)
    int  reqUltraSatan = -1;              // UltraSatan sur les IDs ACSI 0-1 (0/1)
    std::string reqMountSd2;              // image SD du slot 2 (ID 1)
    bool reqEjectSd2 = false;
    bool reqApply  = false;       // appliquer les réglages matériels en attente
    bool reqKiosk  = false;
    int  reqMonitor = -1;         // 1 = couleur, 0 = mono
    float reqVolume = -1.0f;      // >= 0 → nouveau volume maître
    bool  volumeDone = false;     // fin de glissé → persister
    int   reqFastFdc = -1;        // 0/1 → nouvelle valeur du FDC rapide
    int   reqStePads = -1;        // 0/1 → ports DB15 du STE branchés sur les joysticks
    bool  cfgDirty = false;       // un réglage à effet immédiat a changé → saveConfig
    bool  reqSaveState = false, reqLoadState = false;
    bool  rewind = false;             // retour arrière actif (rewind=)
    // Profils nommés (page « Profiles ») : le nom demandé, consommé par la boucle.
    std::string reqSaveProfile, reqLoadProfile, reqDeleteProfile;
};

// Page ouverte (App::cfgPage) : on revient là où on était en rouvrant la fenêtre.
enum ConfigPage {
    kCfgMachine = 0, kCfgMem, kCfgRom, kCfgHd, kCfgCart, kCfgNet, kCfgDongle,
    kCfgScreen, kCfgSound, kCfgMidi, kCfgInput, kCfgEmul, kCfgProfiles, kCfgKiosk, kCfgCount
};

// Suffixe pays d'une ROM → fréquence de balayage (« us » = 60 Hz NTSC).
bool romIsNtsc(const std::string& filename);
// Modèle en clair (« ST », « Mega ST », « STE », « Mega STE »).
const char* machineLabel(MachineType t);
// Le modèle RÉEL (Machine::machineType(), après adjustMachineForTos) diffère-t-il du
// modèle DEMANDÉ (cfg.machine) ? `why` = raison courte, `advice` = quoi changer
// (en anglais, pour l'interface). La barre d'état, la page Machine et le bandeau
// de halt affichaient le modèle demandé : avec etos192us et « STE » choisi, la
// machine tournait en ST, la barre disait STE, la page « up to date », et le bandeau
// conseillait… de choisir STE — une boucle sans issue.
bool machineForcedByTos(const std::string& requested, MachineType real,
                        std::string& why, std::string& advice);
void drawFloppyWindow(App& A, ConfigUi& ui);
void drawConfigWindow(App& A, ConfigUi& ui);
