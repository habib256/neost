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
#include <functional>
#include <string>
#include <vector>

#include "core/Scheduler.hpp"

class Bus;
class YM2149;
class Mfp;

// Événements sonores « mécaniques » du lecteur (purement cosmétiques). Le cœur
// ne fait que les SIGNALER ; c'est le frontend qui joue les échantillons WAV
// (miniaudio côté GUI, Web Audio côté WASM) — cf. rom/drivesound/. Ainsi
// neost_core reste sans aucune dépendance audio.
enum class FdcSound {
    MotorOn,   // accès disque → moteur énergisé (boucle ronron + spin-up si arrêté)
    Step,      // un pas de tête (STEP) → « clic »
    Seek,      // déplacement multi-pistes (RESTORE/SEEK) → bruit de seek
    MotorOff,  // moteur coupé (après N tours sans commande) → arrêt de la boucle
    Index,     // impulsion d'index (1/tour) → léger « tic » périodique moteur tournant
};

class Fdc {
public:
    Fdc(Bus& bus, YM2149& psg, Mfp& mfp) : bus_(bus), psg_(psg), mfp_(mfp) {}

    // Branche l'ordonnanceur : une commande pose BUSY puis l'INTRQ tombe APRÈS un
    // délai daté (seek selon step-rate, transfert selon le débit) — cf. Phase 4.
    void setScheduler(Scheduler* s) { sched_ = s; }

    // Branche le « puits » de sons mécaniques (cf. FdcSound). Optionnel : sans
    // sink, le FDC reste silencieux. Posé par le frontend (thread émulation).
    void setSoundSink(std::function<void(FdcSound)> fn) { soundSink_ = std::move(fn); }

    // Monte/éjecte une image (.st ou .msa) dans le lecteur `drive` (0 = A, 1 = B).
    bool loadImage(const std::string& path, int drive = 0);
    void eject(int drive = 0);
    bool inserted(int drive = 0) const { return !drive_[drive & 1].image.empty(); }
    const std::string& mountedPath(int drive = 0) const { return drive_[drive & 1].path; }

    // MMIO $FF8600-$FF860F (accès octets ; le 68000 y fait des mots big-endian).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Échéance de fin de commande : applique le statut final, BUSY tombe, INTRQ.
    void    onCommandComplete();

    // Impulsion d'index (datée par l'ordonnanceur, 1/tour tant que le moteur
    // tourne) : émet le « tic » et coupe le moteur après assez de tours d'inactivité.
    void    onIndexPulse();

private:
    // Une disquette montée (lecteur A ou B).
    struct FloppyDisk {
        std::vector<uint8_t> image;             // contenu (.st brut, .msa décompressé)
        std::string          path;              // chemin monté ("" = vide)
        int  spt = 9, sides = 2;                // géométrie (BPB)
        bool writeProtect = false;              // protégé en écriture
        bool raw = true;                        // .st brut (writeBack possible) vs .msa
    };

    void     executeCommand(uint8_t cmd);
    int64_t  commandDelayCycles(uint8_t cmd);   // durée réaliste avant fin de commande
    void     writeBack(FloppyDisk& dk, uint32_t off, uint32_t len);  // recopie dans le .st
    uint8_t  readSectors(uint8_t cmd);          // renvoie le statut FDC
    uint8_t  writeSectors(uint8_t cmd);
    uint8_t  readAddress();
    int      currentSide() const;               // face d'après le port A du PSG
    int      selectedDrive() const;             // 0 = A, 1 = B, -1 = aucun (PSG port A)
    uint8_t  dmaStatus() const;
    void     setIntrq(bool on);                 // pilote GPIP5
    void     emitSound(FdcSound e) { if (soundSink_) soundSink_(e); }

    std::function<void(FdcSound)> soundSink_;    // bruits mécaniques (cosmétique)

    Bus&     bus_;
    YM2149&  psg_;
    Mfp&     mfp_;

    FloppyDisk drive_[2];                        // lecteurs A et B
    uint8_t    density_ = 0;                      // $FF860E : densité DD/HD

    // Registres WD1772.
    uint8_t  status_ = 0, track_ = 0, sector_ = 1, data_ = 0;
    bool     intrq_ = false;

    // Timing (Phase 4) : pendant l'exécution d'une commande, BUSY est posé et le
    // statut final est mémorisé ; l'INTRQ est levée à l'échéance Scheduler::FDC.
    Scheduler* sched_ = nullptr;
    uint8_t    pendingStatus_ = 0;   // statut à appliquer à la fin de la commande
    bool       busy_ = false;

    // Moteur & impulsion d'index (modèle matériel, pilote aussi le son). Le moteur
    // s'énergise à la première commande et tourne encore quelques tours après la
    // dernière (le WD1772 le coupe au bout de ~10 tours d'inactivité).
    void     motorOn();                          // énergise le moteur (+ arme l'index)
    void     motorOff();                         // coupe le moteur (+ désarme l'index)
    bool     motorRunning_ = false;
    int      idleRevs_ = 0;                       // tours écoulés depuis la dernière commande
    int64_t  indexRef_ = 0;                       // cycle de référence de la phase d'index
    bool     lastCmdTypeI_ = false;               // dernière commande de type I (bit INDEX valide)

    // Contrôleur DMA.
    uint16_t dmaMode_  = 0;                      // dernier $FF8606 écrit
    uint32_t dmaAddr_  = 0;                      // adresse RAM du transfert
    uint16_t dmaCount_ = 0;                      // compteur de secteurs
    uint8_t  ctrlHi_ = 0, dataHi_ = 0;           // octets hauts latchés (accès mot)
};
