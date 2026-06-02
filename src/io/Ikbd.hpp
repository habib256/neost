// =============================================================================
//  Ikbd.hpp — ACIA 6850 clavier + contrôleur IKBD (HD6301) de l'Atari ST.
//
//  Le clavier ST est un micro-contrôleur intelligent (IKBD) relié au 68000 par
//  une liaison série à travers une ACIA 6850 ($FFFC00 contrôle/statut,
//  $FFFC02 données). L'IKBD envoie des scancodes : "make" à l'appui, make|0x80
//  au relâchement. Quand un octet est reçu, l'ACIA tire la ligne GPIP4 du MFP
//  (canal 6) → interruption niveau 6.
//
//  NeoST modélise : la file de réception, les bits de statut ACIA, et juste ce
//  qu'il faut de l'IKBD (réponse 0xF1 au reset) pour qu'EmuTOS soit content.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>

#include "core/Scheduler.hpp"

class Mfp;

class Ikbd {
public:
    explicit Ikbd(Mfp& mfp) : mfp_(mfp) {}

    // Ordonnanceur : l'IKBD y date sa réponse de reset (l'IRQ ACIA doit arriver
    // APRÈS coup, pas pendant l'instruction qui envoie la commande).
    void setScheduler(Scheduler* s) { sched_ = s; }

    uint8_t read8(uint32_t addr);            // $FFFC00 statut / $FFFC02 données
    void    write8(uint32_t addr, uint8_t v);

    // Échéance : l'IKBD a fini son auto-test → envoie $F1 (réponse de reset).
    void    onResetResponse() { pushRx(0xF1); }

    // Sonde joystick : remplit (joy0, joy1) à l'interrogation `$16`. Par défaut
    // neutre ($00). Le diagnostic « Printer/Joystick » branche un fixture de
    // bouclage parallèle→joystick : Machine y connecte le port B du PSG (cf. le
    // mapping du fixture). true = au moins une manette présente.
    void    setJoystickProbe(std::function<void(uint8_t&, uint8_t&)> fn) { joyProbe_ = std::move(fn); }

    // Événement clavier venant de l'hôte (scancode ST déjà traduit).
    void keyEvent(uint8_t scancode, bool pressed);

    // Mouvement/boutons souris → paquet relatif IKBD de 3 octets. dx>0 = droite,
    // dy>0 = bas (cf. signe choisi côté frontend). left/right = boutons enfoncés.
    void mouseEvent(int dx, int dy, bool left, bool right);

private:
    void pushRx(uint8_t b);                  // empile un octet IKBD → CPU
    void raiseIfReady();                     // tire GPIP4 si octet dispo et RIE actif

    // Renvoie le nombre total d'octets (commande incluse) attendu pour `opcode`,
    // d'après la table KeyboardCommands[] de Hatari (ikbd.c). 0 = opcode inconnu
    // (traité comme une commande mono-octet ignorée).
    static int cmdLength(uint8_t opcode);

    // Exécute la commande IKBD complète accumulée dans inBuf_ (inBuf_[0] = opcode).
    void dispatchCommand();

    Mfp& mfp_;
    Scheduler* sched_ = nullptr;             // pour différer la réponse de reset
    std::deque<uint8_t> rx_;                 // file IKBD → CPU
    uint8_t control_ = 0;                    // registre contrôle ACIA (bit7 = RX int enable)
    std::array<uint8_t, 8> inBuf_{};         // accumulation des octets d'une commande multi-octets
    int inBufLen_ = 0;                       // octets déjà reçus pour la commande en cours
    int cmdExpected_ = 0;                    // octets attendus au total (0 = aucune commande en cours)
    std::function<void(uint8_t&, uint8_t&)> joyProbe_;   // état manettes (fixture de bouclage)
};
