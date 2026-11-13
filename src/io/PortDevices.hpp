// =============================================================================
//  PortDevices — un périphérique optionnel par port : joystick 0/1, RS-232,
//  imprimante, bouton de cartouche. Clés de protection et adaptateurs des jeux.
// =============================================================================
//
//  Les clés du port cartouche (machines d'état PAL/EPLD) vivent dans CartridgeKey.
//  Ici : tout ce qui se branchait sur les AUTRES ports et qu'un logiciel allait
//  sonder. Le modèle est PHYSIQUE : chaque port reçoit au plus un périphérique, et
//  ils coexistent (une clé Leader Board dans le joystick 1, un DAC Pro Sound sur
//  l'imprimante, une clé Cubase dans la cartouche — comme sur une vraie machine).
//  On peut aussi se tromper de port, comme avec le vrai objet : Leader Board
//  branchée dans le port souris ne sera pas vue par le jeu.
//
//  Inventaire repris de Steem SSE 3.9+ (stports.h TDongle, ior.cpp/iow.cpp/ikbd.cpp/
//  run.cpp, SSE_DONGLE_*) et, pour les protocoles d'origine Amiga, de WinUAE
//  (dongle.cpp, Toni Wilen 2009). Hatari n'émule aucun de ces adaptateurs.
//
//  Port joystick (lu par l'IKBD — les bits sont ceux des rapports $FE/$FF) :
//   · Leader Board, 10th Frame (Access) — un cavalier relie HAUT et BAS : une
//     combinaison qu'un vrai joystick ne peut pas produire. Le jeu attend la clé
//     dans le port 1 (WinUAE : JOY1DAT == 0x0101).
//   · Cricket Captain, Multi Player Soccer Manager (port 0), Rugby Coach (port 1)
//     (D&H Games) — un oscillateur : le nibble direction alterne entre %1100 et
//     %1101 à chaque lecture (WinUAE : « must continuously change state »).
//
//  Port série (RS-232 : sorties RTS/DTR du PSG port A → entrées CTS/DCD du MFP) :
//   · B.A.T. II (Ubi Soft) — CTS (GPIP2) lu à 0 en permanence (Steem). ⚠ Sur NeoST
//     comme sur Hatari, CTS est DÉJÀ à 0 au repos : la clé est redondante, gardée
//     pour la cohérence avec Steem.
//   · Music Master (Computer's Dream) — DTR recopié sur DCD (GPIP1) avec un RETARD :
//     ~200 cycles après l'écriture on lit encore l'ancienne valeur.
//   · Jeanne d'Arc (Chip) — DCD assertée quand (RTS|DTR) décroît sans s'annuler.
//
//  Port parallèle (PSG port B = données Centronics) :
//   · Pro Sound Designer (Eidersoft) — DAC 8 bits R-2R sur le port imprimante ;
//     Wings of Death et Lethal Xcess (Thalion) le proposent pour jouer leurs samples
//     sur un STF. Pas une protection : un PÉRIPHÉRIQUE AUDIO, mixé par le YM2149 à
//     partir des écritures horodatées de R15 (cf. YM2149::setPortBDac), avec son
//     propre fader (page Sound).
//
//  Boutons de cartouche :
//   · Multiface ST (Romantic Robot) — le bouton « freeze » tire la ligne moniteur
//     (GPIP7) à 0 le temps de l'appui (câble qui intercepte la prise moniteur) ; la
//     ROM (--cart) prend l'IRQ niveau 7.
//   · Ultimate Ripper (Gotcha) — même idée, sur la ligne RI (GPIP6) du port série.
//     L'appui est relâché à la VBL suivante (comme Steem).
//
//  Non émulés faute de relevé public : Log 3 (EP330), Pro-24 (GAL16V8), Avalon /
//  Synthworks (clé noire, équations ≠ Cubase 2), Zodiac (LED sur joystick 1),
//  DynaBlaster. Les clés à potentiomètres (Dames Grand-Maître, Italy '90, Scala…)
//  sont Amiga seulement. Cf. docs/EXTENSIONS.md.
//
//  OFF par défaut ; aucun effet sur les étalons. Sérialisé dans le save-state (v16) :
//  l'oscillateur de Cricket et la date de Music Master font partie du déterminisme.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstddef>

class Mfp;
class StateArchive;

class PortDevices {
public:
    enum class Port : uint8_t { Joy0 = 0, Joy1, Rs232, Printer, CartButton, Count };
    enum class Device : uint8_t {
        None = 0,
        LeaderBoard, TenthFrame, Cricket, Rugby, Soccer,     // joystick
        Bat2, MusicMaster, JeanneDArc,                       // RS-232
        ProSound,                                            // imprimante
        Multiface, Urc,                                      // bouton de cartouche
        Count
    };

    // Identifiants texte (neost.cfg `joy0=`…, headless `--plug PORT=DEVICE`) et libellés.
    static const char* portId(Port p);
    static Port        portFromId(const char* s, bool* ok = nullptr);
    static const char* id(Device d);
    static const char* label(Device d);
    static Device      fromId(const char* s);          // None si inconnu
    static Port        defaultPort(Device d);          // le port que le logiciel sonde
    static bool        fits(Port p, Device d);         // compatibilité physique (connecteur)
    // Proposé dans les menus (GUI, web, Android) : seulement sur le port que sonde
    // son logiciel. Les deux ports joystick ont le même connecteur, mais une clé
    // joystick sur l'autre port ne sert à rien — l'auteur ne veut la voir que dans le
    // bon (2026-11-13). `fits` reste la règle du BRANCHEMENT (--plug, dongles.txt).
    static bool        offered(Port p, Device d) { return d == Device::None || defaultPort(d) == p; }

    // Branche `d` sur `p` (None = débranche). Faux si le connecteur ne convient pas.
    bool   plug(Port p, Device d);
    Device at(Port p) const { return dev_[size_t(p)]; }
    void   unplugAll();
    bool   any() const;
    bool   usesPortBDac() const { return at(Port::Printer) == Device::ProSound; }
    bool   hasButton() const { return at(Port::CartButton) != Device::None; }
    bool   hasSerial() const { return at(Port::Rs232) != Device::None; }

    void reset();   // état volatil seulement ; les périphériques restent branchés

    // --- Port série ---------------------------------------------------------
    // Écriture du port A du PSG (R14) : bits RTS (3) / DTR (4). `now` = cycle CPU.
    void onPortA(uint8_t a, int64_t now, Mfp& mfp);
    // Lecture GPIP ($FFFA01) : recouvre les bits pilotés (B.A.T. II, Music Master).
    void gpipRead(uint8_t& v, int64_t now) const;

    // --- Ports joystick -----------------------------------------------------
    // Sonde IKBD : recouvre les nibbles de direction (bits 0-3) des deux ports.
    void onJoystick(uint8_t& joy0, uint8_t& joy1);

    // --- Bouton de cartouche ------------------------------------------------
    void pressButton(Mfp& mfp);
    void onVbl(Mfp& mfp);
    bool buttonPressed() const { return pressed_; }

    void serialize(StateArchive& ar);

private:
    void joyOverlay(Device d, uint8_t& joy, uint8_t& osc);

    Device   dev_[size_t(Port::Count)] = {};
    uint16_t serial_  = 0;     // Music Master : bit0 = DTR courant, bit1 = précédent ; Jeanne d'Arc : dernier (RTS|DTR)
    int64_t  timing_  = 0;     // Music Master : cycle de la dernière écriture DTR
    uint8_t  osc_[2]  = {0, 0};// oscillateurs Cricket & co, un par port joystick
    bool     pressed_ = false; // bouton enfoncé jusqu'à la VBL
};
