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
    static constexpr int SRC_TIMERB = 8;   // Timer B (synchro vidéo / event-count)
    static constexpr int SRC_TIMERC = 5;   // tic système 200 Hz
    static constexpr int SRC_ACIA   = 6;   // clavier/MIDI (GPIP4)
    static constexpr int SRC_FDC    = 7;   // FDC/DMA disquette (GPIP5)

    // Événement HBLANK (une fois par ligne) : fait décompter Timer B en mode
    // event-count. TOS 1.x s'en sert pour se synchroniser à l'écran au boot.
    void hblank();

    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Ligne d'interruption de l'ACIA clavier/MIDI, câblée sur GPIP4 (active BAS).
    // Le handler _int_acia d'EmuTOS lit GPIP bit4 pour savoir quand cesser de
    // vider l'ACIA, AVANT d'effacer son bit in-service. Sans elle, le canal 6
    // reste bloqué après la première interruption.
    void setAciaLine(bool active) { aciaLine_ = active; }

    // Ligne d'interruption du FDC sur GPIP5 (active BAS). EmuTOS attend la fin
    // d'une commande disque en pollant GPIP bit5 (timeout_gpip).
    void setFdcLine(bool active) { fdcLine_ = active; }

    // Type de moniteur lu sur GPIP bit7 : couleur (basse rés) ou mono (haute rés).
    // À changer AVANT un reset pour que TOS détecte la bonne résolution au boot.
    void setColorMonitor(bool c) { colorMonitor_ = c; }
    bool colorMonitor() const { return colorMonitor_; }

    // Déclenche une source : positionne le bit IPR si le canal est activé (IER).
    void raise(int source);

    // Une interruption MFP doit-elle être présentée au CPU (IPL 6) ?
    bool irqPending() const;

    // Acquittement (cycle IACK) : renvoie le vecteur de la source la plus
    // prioritaire, met à jour IPR/ISR. -1 si plus rien en attente.
    int iack();

    // Registres exposés au débogueur.
    // GPIP : bit7 = détection moniteur (cf. EmuTOS shifter_get_monitor_type) :
    // bit7=1 → moniteur COULEUR (basse résolution, bureau couleur), bit7=0 → mono.
    uint8_t gpip = 0xFF, aer = 0, ddr = 0;
    uint8_t iera = 0, ierb = 0;   // enable
    uint8_t ipra = 0, iprb = 0;   // pending
    uint8_t imra = 0, imrb = 0;   // mask
    uint8_t isra = 0, isrb = 0;   // in-service
    uint8_t vr   = 0;             // vector register (bit3 = software EOI)
    bool    aciaLine_ = false;    // ligne ACIA (true = données dispo → GPIP4 bas)
    bool    fdcLine_  = false;    // ligne FDC  (true = commande finie → GPIP5 bas)
    bool    colorMonitor_ = true; // GPIP bit7 : true = couleur (basse rés)

    // Timer B (event-count sur HBLANK). tbCounter_ = compteur courant (lu en
    // $FFFA21), tbReload_ = valeur rechargée à 0, tbcr_ = mode ($FFFA1B).
    uint8_t tbcr_ = 0, tbReload_ = 0, tbCounter_ = 0;

    // Backing store des autres registres timer/USART : TOS les écrit puis relit
    // pour vérifier la présence du MFP, donc ils doivent renvoyer ce qu'on y a mis.
    uint8_t timer_[0x40] = {};

private:
    int highestPending() const;     // n° de source prête la plus prioritaire, -1 sinon
    int highestInService() const;   // n° de source en cours de service, -1 sinon
};
