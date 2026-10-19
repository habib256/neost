// =============================================================================
//  AppConfig.hpp — neost.cfg : structure, analyse, écriture, profils nommés.
//
//  Extrait de main.cpp, qui frôlait les 5000 lignes en mélangeant la persistance,
//  une douzaine de panneaux ImGui et la boucle principale. Ce découpage n'est pas
//  cosmétique : le FORMAT de configuration est de la logique pure (des parseurs,
//  une écriture atomique) et, tant qu'il vivait dans main.cpp, RIEN ne pouvait
//  l'exercer — main.cpp n'est lié à aucune cible de test. Ici, la TU ne dépend ni
//  de GLFW ni d'ImGui : elle entre dans neost_core, donc dans neost-selftest
//  (cf. tests/selftest_logic.cpp, aller-retour parse↔écriture).
//
//  Ce qui RESTE dans main.cpp : tout ce qui touche la Machine (loadRtcFromConfig,
//  snapshotRtc) et la politique de gel de la borne (saveConfig).
//
//  ⚠ writeConfigKeys et parseConfigLine se répondent clé pour clé : ne toucher
//  qu'ensemble, sinon un réglage s'écrit sans jamais se relire (ou l'inverse).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstdint>

#include <ctime>
#include <iosfwd>
#include <string>
#include <vector>

#include "gui/CrtParams.h"
#include "io/Rtc.hpp"

namespace neost::appconfig {

// --- Persistance des préférences (dernier ROM, type de moniteur) -------------
// Fichier neost.cfg à la racine du projet (à côté de build/).
// cpu = cœur 68000 (toujours "moira", cycle-exact ; clé conservée pour rétro-compat —
// une ancienne valeur "musashi" est tolérée puis ramenée à "moira" avec un avertissement).
// Défaut machine : 512 Ko ST + cœur Moira.
// joyport = port ST visé par l'émulation joystick clavier (0 ou 1, défaut 1 =
// port « jeux »). L'activation de cette émulation N'EST PAS persistée : elle
// démarre toujours OFF (elle avale flèches + Ctrl droit, ce qui « casse » le
// clavier des jeux — Cuddly, Captain Blood…) ; F11/menu l'activent à la session.
struct Config { std::string rom; std::string disk; std::string diskb; std::string cart; bool mono = false;
                std::string gemdos;   // HD GEMDOS : dossier hôte monté en C: (vide = off)
                std::string acsi;     // image disque dur ACSI cible 0 (vide = off)
                bool modem = false;   // modem Hayes sur l'USART (pont AT → TCP)
                bool midiLoopback = false; // fiche de bouclage MIDI OUT→IN (diagnostics ; OFF = vrai ST)
                bool midiOutGm = false;    // MIDI OUT → synthé GM intégré (DLSMusicDevice macOS / TinySoundFont ailleurs)
                // Banque du synthé GM hors macOS : fichier .sf2 ou dossier à fouiller ;
                // repli sur les banques système. NeoST livre TimGM6mb dans roms/gm/.
                std::string gmSoundFont = "roms/gm";
                bool midiOutPort = false;  // MIDI OUT → port virtuel « NeoST MIDI OUT » (CoreMIDI/ALSA)
                // Appareils MIDI hôtes, désignés par leur NOM (un index se serait mis à
                // pointer le mauvais appareil dès qu'on en débranche un autre ; le nom
                // survit au débranchement ET permet la reconnexion à chaud).
                //
                // SORTIE : un AIGUILLAGE, pas un Thru box. Chaque destination porte le
                // masque des canaux qu'elle reçoit — « instrument 1 de Cubase vers le
                // piano logiciel, instrument 2 vers la groovebox ». Les messages système
                // (horloge, SysEx) vont à toutes, les filtrer casserait la synchro.
                // ENTRÉE : une FUSION. Chaque source peut être forcée sur son canal,
                // sans quoi deux claviers émettant tous deux sur le canal 1 seraient
                // inséparables et finiraient sur la même piste du séquenceur.
                // `uid` : identifiant unique de l'hôte (CoreMIDI), vide si indisponible
                // (ALSA) ou si la config précède son introduction. Sans lui, deux
                // appareils du MÊME MODÈLE sont indistinguables — cf. MidiEndpoint.hpp.
                struct MidiOutDev { std::string name; uint16_t channels = 0xFFFF; std::string uid; };
                struct MidiInDev  { std::string name; int channel = 0; std::string uid; };
                std::vector<MidiOutDev> midiOutDevices;
                std::vector<MidiInDev>  midiInDevices;
                // Avance de livraison du MIDI OUT, en ms (0-200). Arbitrage
                // gigue/latence : cf. MidiOutHost::setLeadMs et son témoin lateBytes.
                int midiLeadMs = 30;
                // ACTIF PAR DÉFAUT depuis que Munt est vendorisé (extern/mt32emu) : la
                // bibliothèque est toujours là, il n'y a plus de raison d'attendre que
                // l'utilisateur découvre l'option. Sans ROM Roland dans mt32Roms,
                // l'ouverture échoue proprement et main.cpp remet le drapeau à false
                // en affichant l'erreur — aucun silence, aucune casse.
                bool midiOutMt32 = true;   // MIDI OUT → Roland MT-32/CM-32L émulé (Munt), mixé dans la sortie
                std::string mt32Roms = "roms/mt32"; // dossier des ROM Roland (MT32_*/CM32L_*)
                std::string mt32Model = "auto";     // auto | mt32 | cm32l (page Sound)
                // Mixeur (page Sound) : gains par source, 0..2, 1 = neutre.
                float mixYm = 1.0f, mixDma = 1.0f, mixDrive = 1.0f, mixMt32 = 1.0f, mixGm = 1.0f;
                // Clé Steinberg sur /ROM3 ($FB0000) : "" (aucune), cubase3 (rouge :
                // Cubase 3.10/Score/Audio), cubase2 (noire : Cubase 2.01), auto.
                std::string dongle;
                // Périphériques des ports (page Dongles) : un identifiant PortDevices::id
                // par port, "" = rien. `adapter=` (ancien, un seul) est relu et rangé
                // sur le port par défaut du périphérique.
                std::string joy0, joy1, rs232, printer, cartbutton;
                float mixDac = 1.0f;   // fader du DAC Pro Sound (page Sound)
                bool autoDongle = true; // disks/dongles.txt : brancher la clé d'un jeu au montage
                bool ethernec = false; // NE2000/EtherNEC sur le port cartouche
                bool netusbee = false; // NetUSBee : NE2000 + ISP1160 USB sur le port cartouche (exclusif d'ethernec)
                bool slirp = false;    // NE2000 : sortie Internet réelle (NAT libslirp) au lieu de la boucle locale
                bool ultrasatan = false; // UltraSatan sur les cibles ACSI 0-1 (slot 1 = acsi, slot 2 = sd2)
                std::string sd2;       // image SD du slot 2 de l'UltraSatan (cible 1 ; vide = slot vide)
                std::string cpu = "moira"; std::string machine = "st";
                std::string mem = "512k"; bool fpu = false;   // MC68881 Mega STE (cf. Fpu.hpp)
                int joyport = 1;
                // port0 = ce qui occupe le port 0 (port souris) : "mouse" (défaut — une
                // manette n'y va que si on l'y ÉPINGLE par joymap) ou "auto" (historique :
                // la 2e manette présente y va toute seule). Page Input.
                std::string port0 = "mouse";
                // Affectation des manettes HÔTE aux ports ST, persistée par GUID :
                // "guid:rôle,guid:rôle" avec rôle ∈ {0, 1, x} (port 0, port 1, off).
                // Une manette absente de la liste est AUTO (cf. stjoy::resolveAssign).
                std::string joymap;
                float joydeadzone = 0.30f; bool fastfdc = false;
                float volume = 1.0f;   // volume maître de la sortie audio (0..1, barre de menu)
                int audioLatencyMs = 85; // coussin audio visé (cf. Audio::setLatencyMs, --audio-latency)
                bool driveSound = true;  // bruits mécaniques du lecteur (roms/drivesound/, cf. DriveSound)
                bool showHex = true, showCpu = true;
                bool showJoy = false;
                bool showKbd = false;          // fenêtre clavier virtuel (photo pic/, touches cliquables)
                bool showCfg = true;           // fenêtre des réglages matériels
                bool showFloppy = true;        // fenêtre indépendante des disquettes
                // Version de la DISPOSITION d'interface. Quand elle change, on resème la
                // disposition ancrée une fois : sinon un imgui.ini écrit par une version
                // précédente garde des nœuds pour des fenêtres disparues et la nouvelle
                // fenêtre Configuration flotte au-dessus de l'écran ST.
                int uiVersion = 0;
                bool dock = true;              // mode ancré (dockspace ImGui) — cf. renderDockSpace
                bool autoZoom = true;          // zoom adaptatif de l'écran ST — cf. g_autoZoom
                float mouseSpeed = 1.0f;       // sensibilité de la souris émulée — cf. App::mouseSpeed
                bool desktopMouse = false;    // souris absolue GEM, sans capture
                bool crt = false;              // effets CRT actifs (façade moniteur)
                neost::CrtParams crtParams;    // réglages CRT (cf. gui/CrtParams.h)
                std::vector<std::string> romDirs;   // kiosk : dossiers ROM/disques additionnels (en plus de disks/)
                std::string rtc; std::time_t rtcSaved = 0; };

// Chemin du neost.cfg (à côté de build/, cf. exeDir).
std::string cfgPath(const std::string& exeDir);

// « sec,min,hour,wday,day,month,year » → Rtc::DateTime (false si la ligne ment).
bool parseRtcConfig(const std::string& s, Rtc::DateTime& dt);

// Hygiène d'UNE ligne de neost.cfg, avant toute comparaison de valeur : retire la
// fin de ligne CRLF et les blancs de queue. Exposée parce que le headless a son
// PROPRE lecteur du même fichier (--from-cfg) : tant que la règle était recopiée
// là-bas, les deux pouvaient diverger — et elles divergeaient (le headless ne
// rognait que le '\r', si bien qu'un « machine=st » suivi d'une espace retombait
// en silence sur la machine par défaut). Une seule définition, deux appelants.
void trimConfigLine(std::string& line);

// Applique UNE ligne « clé=valeur » à `c` (partagé par neost.cfg et les profils).
void parseConfigLine(Config& c, std::string line);
Config loadConfig(const std::string& exeDir);

// Sérialisation. full=true : neost.cfg complet ; false : profil nommé.
void writeConfigKeys(std::ostream& f, const Config& w, bool full);
// Masque de canaux MIDI ↔ texte. « 1-16 », « 2 », « 1,3,10-12 », et « none » pour le
// masque VIDE — jeton nécessaire, sans quoi « aucun canal » ne se distingue pas d'une
// ligne illisible et l'appareil coupé revient grand ouvert au redémarrage. Déclarés
// ici pour que l'auto-test puisse les exercer : c'est de la logique pure.
uint16_t    parseChannelMask(const std::string& s);
std::string formatChannelMask(uint16_t m);

bool writeConfigAtomic(const std::string& finalPath, const Config& w, bool full,
                       bool cwdFallback, std::string& err);

// Profils nommés (profiles/*.cfg, même format que neost.cfg).
std::string profilesDir(const std::string& exeDir);
std::string profileFileName(const std::string& in);
std::vector<std::string> listProfiles(const std::string& dir);
bool saveProfile(const std::string& dir, const std::string& name,
                 const Config& c, std::string& err);
bool loadProfileInto(const std::string& dir, const std::string& name, Config& c);
bool deleteProfile(const std::string& dir, const std::string& name);

}  // namespace neost::appconfig
