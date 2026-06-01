// =============================================================================
//  Fdc.hpp — WD1772 (contrôleur disquette) + contrôleur DMA de l'Atari ST.
//
//  Le CPU n'accède jamais directement au WD1772 : tout passe par le DMA
//  ($FF8600). $FF8606 (control) sélectionne quel registre FDC ou le compteur de
//  secteurs est visible dans $FF8604 (data) ; le DMA transfère les octets entre
//  le FDC et la RAM à l'adresse $FF8609/0B/0D. La face/lecteur vient du port A
//  du YM2149 (actif bas). La fin de commande est signalée par l'INTRQ du FDC,
//  câblé sur GPIP5 du MFP (actif bas), qu'EmuTOS poll via timeout_gpip.
//
//  Modèle "DMA instantané" (comme Hatari en mode rapide) : on exécute la
//  commande et on copie les secteurs d'un coup. Suffisant pour lire/écrire des
//  disquettes FAT12 et naviguer dans GEM. Vérité matérielle : source EmuTOS.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/Scheduler.hpp"

class Bus;
class YM2149;
class Mfp;

class Fdc {
public:
    Fdc(Bus& bus, YM2149& psg, Mfp& mfp) : bus_(bus), psg_(psg), mfp_(mfp) {}

    // Branche l'ordonnanceur : une commande pose BUSY puis l'INTRQ tombe APRÈS un
    // délai daté (seek selon step-rate, transfert selon le débit) — cf. Phase 4.
    void setScheduler(Scheduler* s) { sched_ = s; }

    bool loadImage(const std::string& path);   // monte une image .st dans le lecteur A
    void eject();                               // retire la disquette
    bool inserted() const { return !image_.empty(); }
    const std::string& mountedPath() const { return path_; }

    // MMIO $FF8600-$FF860F (accès octets ; le 68000 y fait des mots big-endian).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Échéance de fin de commande : applique le statut final, BUSY tombe, INTRQ.
    void    onCommandComplete();

private:
    void     executeCommand(uint8_t cmd);
    int64_t  commandDelayCycles(uint8_t cmd);   // durée réaliste avant fin de commande
    void     writeBack(uint32_t off, uint32_t len);   // recopie les écritures dans le .st
    uint8_t  readSectors(uint8_t cmd);          // renvoie le statut FDC
    uint8_t  writeSectors(uint8_t cmd);
    uint8_t  readAddress();
    int      currentSide() const;               // face d'après le port A du PSG
    bool     driveASelected() const;
    uint8_t  dmaStatus() const;
    void     setIntrq(bool on);                 // pilote GPIP5

    Bus&     bus_;
    YM2149&  psg_;
    Mfp&     mfp_;

    std::vector<uint8_t> image_;                // contenu de la disquette
    std::string path_;                          // chemin de l'image montée ("" = vide)
    int      spt_ = 9, sides_ = 2;              // géométrie (lue dans le BPB)
    bool     writeProtect_ = false;             // disquette protégée en écriture

    // Registres WD1772.
    uint8_t  status_ = 0, track_ = 0, sector_ = 1, data_ = 0;
    bool     intrq_ = false;

    // Timing (Phase 4) : pendant l'exécution d'une commande, BUSY est posé et le
    // statut final est mémorisé ; l'INTRQ est levée à l'échéance Scheduler::FDC.
    Scheduler* sched_ = nullptr;
    uint8_t    pendingStatus_ = 0;   // statut à appliquer à la fin de la commande
    bool       busy_ = false;

    // Contrôleur DMA.
    uint16_t dmaMode_  = 0;                      // dernier $FF8606 écrit
    uint32_t dmaAddr_  = 0;                      // adresse RAM du transfert
    uint16_t dmaCount_ = 0;                      // compteur de secteurs
    uint8_t  ctrlHi_ = 0, dataHi_ = 0;           // octets hauts latchés (accès mot)
};
