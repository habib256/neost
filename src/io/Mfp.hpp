// =============================================================================
//  Mfp.hpp — MC68901 (Multi-Function Peripheral) de l'Atari ST.
//
//  Le MFP est le contrôleur d'interruptions vectorisées du ST (4 timers, GPIP,
//  USART). Toutes les IRQ "intelligentes" passent par lui et ressortent en
//  IPL 6 vers le 68000. NeoST en modélise le strict nécessaire :
//    - Timer C  (canal 5) : tic système 200 Hz → débloque l'accueil EmuTOS,
//                           fait avancer l'horloge et le curseur.
//    - ACIA clavier (canal 6, GPIP4) : réception d'octets IKBD.
//  Logique d'interruption complète : IER/IPR/IMR/ISR + registre vecteur (VR),
//  modes auto / "software end-of-interrupt".
//
//  Numéro de canal = numéro de source MFP (0..15). Vecteur = (VR & 0xF0) | canal.
//  Registre A = sources 8..15 (bits 0..7), registre B = sources 0..7 (bits 0..7).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

class Mfp {
public:
    static constexpr int SRC_TIMERC = 5;   // tic système 200 Hz
    static constexpr int SRC_ACIA   = 6;   // clavier/MIDI (GPIP4)

    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Ligne d'interruption de l'ACIA clavier/MIDI, câblée sur GPIP4 (active BAS).
    // Le handler _int_acia d'EmuTOS lit GPIP bit4 pour savoir quand cesser de
    // vider l'ACIA, AVANT d'effacer son bit in-service. Sans elle, le canal 6
    // reste bloqué après la première interruption.
    void setAciaLine(bool active) { aciaLine_ = active; }

    // Déclenche une source : positionne le bit IPR si le canal est activé (IER).
    void raise(int source);

    // Une interruption MFP doit-elle être présentée au CPU (IPL 6) ?
    bool irqPending() const;

    // Acquittement (cycle IACK) : renvoie le vecteur de la source la plus
    // prioritaire, met à jour IPR/ISR. -1 si plus rien en attente.
    int iack();

    // Registres exposés au débogueur.
    uint8_t gpip = 0xFF, aer = 0, ddr = 0;
    uint8_t iera = 0, ierb = 0;   // enable
    uint8_t ipra = 0, iprb = 0;   // pending
    uint8_t imra = 0, imrb = 0;   // mask
    uint8_t isra = 0, isrb = 0;   // in-service
    uint8_t vr   = 0;             // vector register (bit3 = software EOI)
    bool    aciaLine_ = false;    // ligne ACIA (true = données dispo → GPIP4 bas)

private:
    int highestPending() const;     // n° de source prête la plus prioritaire, -1 sinon
    int highestInService() const;   // n° de source en cours de service, -1 sinon
};
