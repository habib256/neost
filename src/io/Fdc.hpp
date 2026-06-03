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
//  Modèle ROTATIONNEL fidèle Hatari (fdc.c, chemin « _ST ») : machine à états
//  par commande, datée au cycle FDC (≈ cycle CPU à ~8 MHz). On modélise la
//  rotation du disque (impulsions d'index, position tête / secteur), le spin-up
//  (6 tours), le chargement de tête (15 ms), la latence rotationnelle par
//  secteur, le transfert DMA octet par octet (FIFO 16 o) et l'INTRQ datée. Le
//  débit MFM réel (256 cyc/octet) et le spin-up débloquent les jeux à
//  track-loader maison (Arkanoid…). Vérité matérielle : extern/hatari/src/fdc.c.
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
// (miniaudio côté GUI, Web Audio côté WASM) — cf. roms/drivesound/. Ainsi
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

    // Branche l'ordonnanceur : la machine à états du FDC est datée via la source
    // Scheduler::FDC (chaque phase reprogramme l'événement au cycle voulu).
    void setScheduler(Scheduler* s) { sched_ = s; }

    // Branche le « puits » de sons mécaniques (cf. FdcSound). Optionnel : sans
    // sink, le FDC reste silencieux. Posé par le frontend (thread émulation).
    void setSoundSink(std::function<void(FdcSound)> fn) { soundSink_ = std::move(fn); }

    // « FDC rapide » (équivalent de `hatari --fastfdc`) : divise les délais de
    // COMMANDE et de TRANSFERT par un facteur fixe pour accélérer les accès disque
    // (chargements 10× plus courts). La ROTATION du disque (impulsions d'index,
    // spin-up, arrêt moteur) reste au rythme réel, comme Hatari. ⚠ Peut casser les
    // programmes au track-loader maison qui dépendent du débit physique du WD1772.
    void setFastFdc(bool on) { fastFloppy_ = on; }
    bool fastFdc() const { return fastFloppy_; }

    // Monte/éjecte une image (.st ou .msa) dans le lecteur `drive` (0 = A, 1 = B).
    bool loadImage(const std::string& path, int drive = 0);
    void eject(int drive = 0);
    bool inserted(int drive = 0) const { return !drive_[drive & 1].image.empty(); }
    const std::string& mountedPath(int drive = 0) const { return drive_[drive & 1].path; }

    // MMIO $FF8600-$FF860F (accès octets ; le 68000 y fait des mots big-endian).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Échéance de la machine à états (Scheduler::FDC) : avance la commande en
    // cours d'une ou plusieurs phases et reprogramme la prochaine échéance.
    void    onFdcEvent();

private:
    // Une disquette montée (lecteur A ou B).
    struct FloppyDisk {
        std::vector<uint8_t> image;             // contenu (.st brut, .msa décompressé)
        std::string          path;              // chemin monté ("" = vide)
        int  spt = 9, sides = 2;                // géométrie (BPB)
        int  headTrack = 0;                     // position PHYSIQUE de la tête (≠ registre TR)
        bool writeProtect = false;              // protégé en écriture
        bool raw = true;                        // .st brut (writeBack possible) vs .msa

        // Détection de changement de média (Mediach) à chaud (cf. Hatari
        // floppy.c:Floppy_DriveTransition*) : une éjection/insertion à chaud force
        // brièvement WPRT, ce que TOS surveille pour relire le répertoire. On modélise
        // ce créneau par une phase datée (en cycles CPU de l'ordonnanceur, horloge
        // continue). 0 = aucune ; 1 = éjection (force WPRT) ; 2 = insertion (ne force rien).
        enum TransitionPhase { TRANS_NONE = 0, TRANS_EJECT = 1, TRANS_INSERT = 2 };
        int     transitionPhase    = TRANS_NONE;
        int64_t transitionDeadline = 0;         // cycle CPU de fin de la phase courante
    };

    // --- Géométrie / capacités du média (cf. Hatari FDC_Get*PerTrack/Disk) -----
    int      sectorsPerTrack(int drive) const { return drive_[drive].spt; }
    int      sidesPerDisk(int drive)    const { return drive_[drive].sides; }
    int      tracksPerDisk(int drive)   const;
    int      bytesPerTrack()            const;  // piste DD standard (≈ 6268 o)

    // --- Modèle rotationnel : impulsions d'index (cf. Hatari FDC_IndexPulse_*) --
    void     indexInit();                       // ancre l'index à une position « passée » aléatoire (déterministe)
    void     indexCheckUpdate();                // incrémente le compteur si un tour s'est écoulé
    void     indexIncrease(int64_t ipTime);     // valide une impulsion (compteur + son + force-int)
    int      indexCurrentPosBytes() const;      // position tête depuis l'index, en octets (−1 si pas de média)
    bool     indexState() const;                // signal d'index actif (≈ 3,71 ms/tour)
    int64_t  nextIndexCycles() const;           // cycles FDC avant la prochaine impulsion (−1 si pas de média)

    // --- Machine à états des commandes (cf. Hatari FDC_Update*Cmd) -------------
    void     refreshDriveSide();                // relit lecteur/face du PSG (réinit l'index au changement)
    void     executeCommand(uint8_t cmd);       // décode CR, lance la commande, programme le 1er événement
    void     updateStr(uint8_t dis, uint8_t en) { str_ = uint8_t((str_ & ~dis) | en); }
    bool     setMotorOn(uint8_t cr);            // démarre le moteur ; renvoie true si spin-up nécessaire
    int      cmdComplete(bool doInt);           // fin de commande : BUSY tombe, INTRQ, puis MOTOR_STOP
    bool     verifyTrack();                     // vérif piste type I (toujours OK pour .ST sauf bord)
    int      updateMotorStop();
    int      updateRestore();
    int      updateSeek();
    int      updateStep();
    int      updateReadSectors();
    int      updateWriteSectors();
    int      updateReadAddress();
    int      updateReadTrack();
    int      updateWriteTrack();

    int      typeIPrepare(int cmdId, int runState);   // amorce une commande type I
    int      nextSectorID(int* pFdcCycles);     // latence jusqu'au prochain champ ID (cf. NextSectorID_FdcCycles_ST)
    int      applyFastFdc(int fdcCycles) const; // divise le délai en mode « FDC rapide » (sauf délais cadencés sur l'index)
    int      spinWaitDelay();                   // attente d'une impulsion d'index (spin-up / arrêt moteur)

    // --- Accès « bas niveau » à l'image .ST (cf. Hatari FDC_*_ST) --------------
    uint8_t  readSectorST(uint8_t track, uint8_t sector, uint8_t side, int* pSize);
    uint8_t  writeSectorST(uint8_t track, uint8_t sector, uint8_t side, int size);
    uint8_t  readAddressST(uint8_t track, uint8_t sector, uint8_t side);
    uint8_t  readTrackST(uint8_t track, uint8_t side);
    uint8_t  writeTrackBuffer();                // WRITE TRACK : extrait les secteurs du flux écrit
    void     writeBack(FloppyDisk& dk, uint32_t off, uint32_t len);  // recopie dans le .st

    // --- Tampon de transfert FDC↔DMA (cf. Hatari FDC_Buffer_*) ----------------
    // Pour les images .ST chaque octet a un timing fixe (256 cycles FDC), on ne
    // stocke donc que les octets ; le timing est constant.
    void     bufferReset() { buf_.clear(); bufPos_ = 0; }
    void     bufferAdd(uint8_t b) { buf_.push_back(b); }
    uint8_t  bufferReadByte() { return buf_[bufPos_++]; }
    uint8_t  bufferReadBytePos(int i) const { return buf_[i]; }
    int      bufferSize() const { return int(buf_.size()); }

    // --- DMA FIFO 16 octets (cf. Hatari FDC_DMA_FIFO_Push/Pull) ----------------
    void     fifoPush(uint8_t b);               // octet lu du FDC → FIFO → RAM (par blocs de 16)
    uint8_t  fifoPull();                         // octet RAM → FIFO → FDC (par blocs de 16)
    void     dmaResetFifo();                     // bascule du bit 8 de $FF8606 : reset DMA
    uint16_t dmaStatusWord() const;

    // --- IRQ / INTRQ (câblée sur GPIP5 + canal 7 du MFP) ----------------------
    void     fdcSetIrq(uint8_t source);
    void     fdcClearIrq();
    void     setIntrqLine(bool on);             // pilote la ligne GPIP5 (+ canal 7 sur front)

    // --- Contrôleur ACSI (disque dur, $FF8606 bit DMA_CSACSI) -----------------
    // Variante Atari de SCSI : commande de 6 octets envoyée octet par octet via la
    // DMA ($FF8604) ; après chaque octet accepté, le contrôleur lève l'IRQ HDC
    // (= INTRQ/GPIP5) pour que le CPU envoie le suivant (cf. Hatari Acsi_WriteCommandByte).
    // Sans disque sur la cible → pas d'IRQ → le pilote conclut « pas de disque ».
    void     writeAcsi(uint32_t addr, uint8_t v);
    void     executeAcsi();                     // exécute la commande complète (DMA + statut)
    std::vector<uint8_t> hd_;                    // disque dur virtuel (cible 0), alloué à la demande
    uint8_t  acsiCmd_[6] = {0};
    int      acsiByteCount_ = 0;
    uint8_t  acsiTarget_ = 0;
    uint8_t  acsiStatus_ = 0;                    // statut renvoyé (0 = OK)

    int      currentSide() const;               // face d'après le port A du PSG
    int      selectedDrive() const;             // 0 = A, 1 = B, -1 = aucun (PSG port A)

    // Renvoie true tant que la phase d'éjection (force WPRT=1) du lecteur `drive`
    // est active ; expire la transition quand l'échéance est dépassée. Calqué sur
    // Hatari Floppy_DriveTransitionUpdateState (Force=1 pendant l'éjection).
    bool     transitionForceWprt(int drive);
    void     emitSound(FdcSound e) { if (soundSink_) soundSink_(e); }

    // Horloge FDC = horloge CPU « live » (sous-instruction si dispo), absolue et
    // continue. La conversion cycles-FDC ↔ cycles-CPU est l'identité sur ST (le
    // WD1772 tourne à ~8,021 MHz, comme le CPU).
    int64_t  nowCyc() const { return sched_ ? sched_->liveNow() : 0; }

    std::function<void(FdcSound)> soundSink_;    // bruits mécaniques (cosmétique)

    Bus&     bus_;
    YM2149&  psg_;
    Mfp&     mfp_;

    FloppyDisk drive_[2];                         // lecteurs A et B
    Scheduler* sched_ = nullptr;

    // --- Registres internes WD1772 (cf. FDC_STRUCT) ---------------------------
    uint8_t  cr_ = 0;        // Command Register
    uint8_t  tr_ = 0;        // Track Register
    uint8_t  sr_ = 1;        // Sector Register
    uint8_t  dr_ = 0;        // Data Register (destination des SEEK)
    uint8_t  str_ = 0;       // Status Register
    int      stepDir_ = 1;   // sens du dernier pas (+1 / −1)
    uint8_t  side_ = 0;      // face sélectionnée (0/1)
    int      driveSel_ = -1; // lecteur sélectionné (0/1) ou −1
    uint8_t  irqSignal_ = 0; // sources d'IRQ actives (cf. IRQ_SOURCE_*)
    uint16_t densityMode_ = 0; // $FF860E (bits densité DD/HD)

    // État de la machine à états.
    int      command_ = 0;            // commande en cours (CMD_*) ; 0 = inactif
    int      commandState_ = 0;       // sous-état (RUN_*)
    uint8_t  commandType_ = 1;        // 1/2/3/4
    bool     replaceCommandPossible_ = false; // remplaçable pendant prepare+spinup
    bool     fastFloppy_ = false;     // « FDC rapide » (cf. setFastFdc) : délais /N
    bool     delayIndexPaced_ = false;// le délai courant est cadencé sur la rotation (non accéléré)
    bool     statusTypeI_ = true;     // le STR rapporte un statut type I
    uint8_t  statusTemp_ = 0;         // statut intermédiaire (lecture secteur)
    int      indexCounter_ = 0;       // tours comptés (spin-up, motor-off, timeout)
    uint8_t  interruptCond_ = 0;      // condition d'un Force Interrupt (type IV)

    // Champ ID du prochain secteur (rempli par nextSectorID()).
    uint8_t  nextID_TR_ = 0, nextID_SR_ = 1, nextID_LEN_ = 2, nextID_CRCOK_ = 1;

    // Position rotationnelle : cycle CPU de la dernière impulsion d'index (0 =
    // inconnue) et PRNG déterministe pour la phase initiale (reproductible →
    // headless byte-exact, mais variable d'un démarrage moteur à l'autre).
    int64_t  indexTime_ = 0;
    uint32_t rng_ = 0x2545F491u;
    uint32_t rngNext() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return rng_; }

    // Contrôleur DMA.
    uint16_t dmaMode_  = 0;          // dernier $FF8606 écrit
    uint32_t dmaAddr_  = 0;          // adresse RAM du transfert
    uint16_t dmaSectorCount_ = 0;    // compteur de secteurs ($FF8604 en mode SCREG)
    int16_t  dmaBytesInSector_ = 512;// octets restants dans le secteur DMA courant
    int      dmaBytesToTransfer_ = 0;// octets restants (write sector/track)
    uint8_t  fifo_[16] = {0};
    int      fifoSize_ = 0;
    bool     dmaError_ = false;      // bit 0 de $FF8606 (0 = erreur)
    uint16_t ff8604recent_ = 0;      // dernier mot lu/écrit en $FF8604 (bits inutilisés)
    uint8_t  ctrlHi_ = 0, dataHi_ = 0; // octets hauts latchés (accès mot)

    // Tampon de transfert FDC↔DMA.
    std::vector<uint8_t> buf_;
    int      bufPos_ = 0;
};
