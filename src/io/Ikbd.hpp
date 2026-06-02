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
#include <cstdint>
#include <deque>

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

    // Événement clavier venant de l'hôte (scancode ST déjà traduit).
    void keyEvent(uint8_t scancode, bool pressed);

    // Mouvement/boutons souris → paquet relatif IKBD de 3 octets. dx>0 = droite,
    // dy>0 = bas (cf. signe choisi côté frontend). left/right = boutons enfoncés.
    void mouseEvent(int dx, int dy, bool left, bool right);

private:
    void pushRx(uint8_t b);                  // empile un octet IKBD → CPU
    void raiseIfReady();                     // tire GPIP4 si octet dispo et RIE actif

    Mfp& mfp_;
    Scheduler* sched_ = nullptr;             // pour différer la réponse de reset
    std::deque<uint8_t> rx_;                 // file IKBD → CPU
    uint8_t control_ = 0;                    // registre contrôle ACIA (bit7 = RX int enable)
    uint8_t cmd0_ = 0;                       // dernier octet de commande (détection reset 0x80,0x01)
};
