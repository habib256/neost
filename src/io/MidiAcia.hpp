// =============================================================================
//  MidiAcia.hpp — ACIA 6850 MIDI de l'Atari ST ($FFFC04 contrôle/statut,
//  $FFFC06 données), avec BOUCLAGE interne (connecteur MIDI OUT→IN).
//
//  Le port MIDI ST est une seconde ACIA 6850, distincte du clavier. Le diagnostic
//  « M MIDI » émet un octet sur MIDI OUT, active l'interruption de réception (RIE)
//  et attend l'IRQ ACIA (canal 6 du MFP via GPIP4) prouvant qu'il a RELU l'octet
//  sur MIDI IN — ce qui suppose un câble de bouclage OUT→IN branché. NeoST émule
//  ce câble : tout octet écrit sur la donnée TX est ré-injecté dans la file de
//  réception et, si RIE est armé, lève le canal 6 du MFP (comme l'ACIA clavier).
//
//  Hors diagnostic, aucun logiciel ST courant ne dépend de l'absence de bouclage
//  MIDI ; on garde le câble toujours « branché » par simplicité.
//
//  Modèle ACIA aligné sur l'ACIA clavier (cf. Ikbd.cpp) et midi.c/acia.c d'Hatari :
//  cause d'IRQ TX (TIE + TDRE), RDR persistant (relire à vide renvoie le dernier
//  octet), master reset 6850 qui NE purge PAS la file de réception en transit.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <deque>

class Mfp;

class MidiAcia {
public:
    explicit MidiAcia(Mfp& mfp) : mfp_(mfp) {}

    uint8_t read8(uint32_t addr);            // $FFFC04 statut / $FFFC06 données
    void    write8(uint32_t addr, uint8_t v);

private:
    void raiseIfReady();                     // lève le canal 6 du MFP si une cause d'IRQ est active
    bool irqActive() const;                  // cause d'IRQ : RX (RDRF & RIE) OU TX (TIE & TDRE)

    Mfp&    mfp_;
    std::deque<uint8_t> rx_;                 // file MIDI IN (alimentée par le bouclage)
    uint8_t control_ = 0;                    // registre contrôle ACIA (bit7 = RX int enable)
    uint8_t rdr_ = 0;                        // Receive Data Register : dernier octet livré
                                             // (relire à vide le renvoie, cf. acia.c)
    bool    rdrf_ = false;                   // RDR plein (octet livré non encore lu)
    bool    txEnableInt_ = false;            // IRQ d'émission armée : CR bits5-6 = 01 (TIE)
    bool    tdre_ = true;                    // Transmit Data Register Empty : 1 au repos
};
