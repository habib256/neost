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
//  Côté ÉMISSION, l'ACIA MIDI suit le même modèle que l'ACIA clavier (port de
//  acia.c ACIA_Write_CR/ACIA_UpdateIRQ) : CR bits 5-6 = 01 arme l'IRQ d'émission
//  (TIE) et l'écriture d'une donnée vide TDRE, re-rempli ~1 octet MIDI plus tard
//  (10 bits à 31250 bauds = 2560 cycles, Scheduler::MIDI_TX) — c'est l'IRQ
//  « transmetteur prêt » dont les séquenceurs MIDI cadencent leur sortie.
//  Hors TIE, TDRE reste câblé à 1 (modèle simplifié, comme l'ACIA clavier).
//
//  Hors diagnostic, aucun logiciel ST courant ne dépend de l'absence de bouclage
//  MIDI ; on garde le câble toujours « branché » par simplicité.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <deque>

#include "core/Scheduler.hpp"

class Mfp;

class MidiAcia {
public:
    explicit MidiAcia(Mfp& mfp) : mfp_(mfp) {}

    // Ordonnanceur : date le re-remplissage de TDRE sous TIE (cf. onTxEmpty).
    void setScheduler(Scheduler* s) { sched_ = s; }

    uint8_t read8(uint32_t addr);            // $FFFC04 statut / $FFFC06 données
    void    write8(uint32_t addr, uint8_t v);

    // Échéance MIDI_TX : le registre d'émission s'est vidé (~1 octet MIDI après
    // une écriture $FFFC06 sous TIE) → TDRE repasse à 1 et ré-arme l'IRQ TX.
    void    onTxEmpty();

private:
    void raiseIfReady();                     // lève le canal 6 du MFP si cause RX ou TX

    Mfp&    mfp_;
    Scheduler* sched_ = nullptr;
    std::deque<uint8_t> rx_;                 // file MIDI IN (alimentée par le bouclage)
    uint8_t control_ = 0;                    // registre contrôle ACIA (bit7 = RX int enable)
    bool    txEnableInt_ = false;            // IRQ d'émission armée : CR bits5-6 = 01
    bool    tdre_ = true;                    // Transmit Data Register Empty (0 en émission sous TIE)
};
