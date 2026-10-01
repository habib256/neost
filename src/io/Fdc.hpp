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
#include <memory>
#include <string>
#include <vector>

#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"
#include "io/StxImage.hpp"
#include "io/Acsi.hpp"

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

    // Lecteur BRANCHÉ ou non (port de Hatari FDC_Drive_Set_Enable, option
    // `--drive-b off`). Un lecteur débranché se comporte comme « aucun lecteur
    // sélectionné » : pas d'index, TR00/INDEX/WPRT éteints, Restore qui n'atteint
    // jamais la piste 0. C'est ce que sonde le TOS au boot pour compter ses lecteurs :
    // B débranché → _nflops ($4A6) = 1, et GEMDOS demande « Insert disk B: » dans A.
    void setDriveEnabled(int drive, bool on) { driveEnabled_[drive & 1] = on; refreshDriveSide(); }
    bool driveEnabled(int drive) const { return driveEnabled_[drive & 1]; }

    // Longueur de la trame COURANTE en cycles bus (posée à chaque VBL par Machine) :
    // elle date la fenêtre de changement de média en VBL, comme Hatari.
    void setFrameCycles(int64_t c) { if (c > 0) frameCycles_ = c; }

    // A14 — écriture HÔTE des images disquette (write-through). ON par défaut :
    // NeoST persiste chaque secteur écrit au fil de l'eau, ce qui est le bon
    // comportement pour un utilisateur (une coupure ne perd pas la sauvegarde du
    // jeu). C'est en revanche un PIÈGE pour les campagnes de test : deux images
    // SUIVIES PAR GIT ont été modifiées dans l'arbre par des runs (Eliminator le
    // 2026-08-25, disks/diskA.st par le test F du diagnostic le 2026-08-27).
    // Coupé (--disk-ro), la machine invitée ne voit AUCUNE différence — les
    // écritures vont toujours dans dk.image, donc les relectures les voient — et
    // seul le fichier hôte est épargné. Ce n'est PAS une protection en écriture
    // (dk.writeProtect), qui elle changerait ce que le programme observe.
    void setHostWriteBack(bool on) { hostWriteBack_ = on; }
    bool hostWriteBack() const { return hostWriteBack_; }
    bool fastFdc() const { return fastFloppy_; }

    // Monte/éjecte une image (.st ou .msa) dans le lecteur `drive` (0 = A, 1 = B).
    bool loadImage(const std::string& path, int drive = 0);
    void eject(int drive = 0);

    // Auto-test déterministe du couple encodeMsa/decodeMsa (aucun disque requis) :
    // le ré-encodage .MSA réécrit le fichier de l'utilisateur, un aller-retour non
    // byte-exact détruirait sa disquette. Appelé par neost-headless --msa-selftest.
    bool msaSelfTest();

    // Monte une image de disque dur ACSI (dump de secteurs brut) sur la cible
    // `target` (0-7, généralement 0). Le TOS/EmuTOS détecte le périphérique au boot,
    // lit la table de partitions et monte les partitions FAT (C:, D:…). Cf. io/Acsi.hpp.
    bool mountAcsi(const std::string& path, int target = 0) { return acsi_.mount(target, path); }
    void unmountAcsi() { acsi_.unmountAll(); }   // le TOS ne le verra qu'au prochain boot
    bool acsiActive() const { return acsi_.anyEnabled(); }
    const std::string& acsiMountedPath(int target = 0) const { return acsi_.mountedPath(target); }
    int  acsiPartitionCount() const { return acsi_.partitionCount(); }
    // UltraSatan (extension NeoST, cf. io/UltraSatan.hpp) : 2 slots SD sur 2 cibles.
    void attachUltraSatan(UltraSatan* dev, int firstTarget) { acsi_.attachUltraSatan(firstTarget, dev); }
    void detachUltraSatan() { acsi_.detachUltraSatan(); }
    int  usatanFirstTarget() const { return acsi_.usatanFirstTarget(); }
    bool inserted(int drive = 0) const { return drive_[drive & 1].present(); }
    const std::string& mountedPath(int drive = 0) const { return drive_[drive & 1].path; }

    // MMIO $FF8600-$FF860F (accès octets ; le 68000 y fait des mots big-endian).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Échéance de la machine à états (Scheduler::FDC) : avance la commande en
    // cours d'une ou plusieurs phases et reprogramme la prochaine échéance.
    void    onFdcEvent();

    // Reset matériel (bouton reset / power-cycle) — port de Hatari FDC_Reset :
    // registres WD1772, machine à états, DMA/FIFO et INTRQ au repos ; à froid,
    // TR/DR et le mot $FF8604 rémanent sont aussi effacés. Les images montées
    // et la position physique des têtes survivent.
    void    reset(bool cold);

    // Sauvegarde/restauration de l'état RUNTIME du contrôleur (WD1772 + DMA).
    // Méthode SYMÉTRIQUE (même code save/load, cf. StateArchive). Inline pour
    // l'accès aux membres privés. Ne sérialise PAS le CONTENU des images disque
    // (drive_[].image, ->stx, path, géométrie, wd1772Path) : ils sont rechargés
    // depuis le fichier .st/.stx, pas depuis la save-state. On sérialise en
    // revanche la POSITION physique de la tête et l'état de transition (Mediach)
    // de chaque lecteur, plus tous les registres/état DMA/FIFO/rotation. Le
    // contrôleur ACSI (acsi_) et les liaisons (bus_/psg_/mfp_/sched_/soundSink_)
    // sont hors périmètre.
    void serialize(StateArchive& ar) {
        // --- Position physique / transition par lecteur (PAS le contenu image) ---
        for (int d = 0; d < 2; ++d) {
            ar(drive_[d].headTrack);        // position PHYSIQUE de la tête
            // Borné comme stxNextSector_/fifoSize_ : hors [0, 90] (= MAX_TRACK,
            // Fdc.cpp), les clamps par ÉGALITÉ d'updateSeek/updateStep (ht == 0 /
            // == MAX_TRACK) ne retiennent plus la tête — elle marche arbitrairement
            // loin (RNF partout jusqu'au remontage).
            ar.check(drive_[d].headTrack >= 0 && drive_[d].headTrack <= 90,
                     "Fdc::headTrack hors [0, 90]");
            ar(drive_[d].transitionPhase);  // phase Mediach (éjection/insertion)
            ar(drive_[d].transitionDeadline);
        }

        // --- Registres internes WD1772 ---
        ar(cr_);
        ar(tr_);
        ar(sr_);
        ar(dr_);
        ar(str_);
        ar(stepDir_);
        ar(side_);
        ar(driveSel_);
        ar(irqSignal_);
        ar(densityMode_);

        // --- Machine à états des commandes ---
        ar(command_);
        ar(commandState_);
        ar(commandType_);
        ar(replaceCommandPossible_);
        ar(fastFloppy_);
        ar(delayIndexPaced_);
        ar(statusTypeI_);
        ar(statusTemp_);
        ar(indexCounter_);
        ar(interruptCond_);

        // --- Champ ID du prochain secteur ---
        ar(nextID_TR_);
        ar(nextID_SR_);
        ar(nextID_LEN_);
        ar(nextID_CRCOK_);
        ar(stxNextSector_);
        // ⚠ Borne BASSE indispensable : les trois sites STX (readSectorStx,
        // writeSectorStx, readAddressStx) ne testent que `>= sectorsCountView()`.
        // Négatif, l'index indexait AVANT le vecteur — lecture hors bornes, et
        // ÉCRITURE pour writeSectorStx (sec.saveIndex). Chez Hatari le champ
        // homologue est un uint8_t, donc structurellement positif ; NeoST l'a
        // élargi en int sans reposer la borne.
        ar.check(stxNextSector_ >= 0 && stxNextSector_ < 256);

        // --- Modèle rotationnel (impulsion d'index + PRNG de phase) ---
        ar(indexTime_);
        ar(rng_);

        // --- Contrôleur DMA ---
        ar(dmaMode_);
        ar(dmaAddr_);
        ar(dmaSectorCount_);
        ar(dmaBytesInSector_);
        ar(dmaBytesToTransfer_);
        ar.arr(fifo_);              // uint8_t[16]
        ar(fifoSize_);
        ar.check(fifoSize_ >= 0 && fifoSize_ < 16);    // fifoPush écrit fifo_[fifoSize_] : 0..15
        ar(dmaError_);
        ar(ff8604recent_);
        ar(ctrlHi_);
        ar(dataHi_);

        // --- Tampon de transfert FDC↔DMA ---
        ar.vec(buf_);              // std::vector<uint8_t>
        ar.podVec(bufTiming_);     // std::vector<uint16_t>
        ar(bufPos_);
        // Invariants : buf_/bufTiming_ grandissent toujours ensemble (bufferAddTiming)
        // et bufPos_ les indexe — des valeurs restaurées incohérentes liraient hors tas.
        ar.check(buf_.size() == bufTiming_.size());
        ar.check(bufPos_ >= 0 && static_cast<std::size_t>(bufPos_) <= buf_.size());
        ar.check(driveSel_ >= -1 && driveSel_ <= 1);   // indexe drive_[2] (−1 = aucun)

        // --- Contrôleur ACSI (état de commande ; config/images hors-snapshot) ---
        acsi_.serialize(ar);

        // La densité par lecteur n'est PAS sérialisée (dérivée du média + piste) :
        // on la recalcule depuis le headTrack restauré (sinon elle resterait celle
        // de la piste courante d'AVANT le load — écart sur STX à densité mixte).
        if (ar.loading()) { updateFloppyDensity(0); updateFloppyDensity(1); }
    }

    // Relit lecteur/face depuis le port A du PSG ; au CHANGEMENT de lecteur, réinitialise
    // la référence d'index du modèle rotationnel (port de FDC_SetDriveSide, fdc.c).
    // PUBLIC (et non plus privé) parce que le PSG doit POUSSER : Hatari appelle
    // FDC_SetDriveSide depuis l'écriture du port A (psg.c:419-420), et NeoST fait de même
    // via Machine (setPortASink). Sans cette poussée, un programme qui écrit sa commande
    // FDC AVANT de sélectionner le lecteur reste bloqué avec driveSel_ = -1 — cf. D-PSG.
    // Idempotent : ne ré-ancre l'index que si le lecteur a effectivement changé.
    void     billDmaCycles(int n);           // stall CPU du DMA FDC (cf. .cpp, D3)
    void     noteFifoFlush();                // sonde NEOST_FDC_FLUSH_DIAG (cf. .cpp)
    // Cycles de stall DMA facturés depuis la dernière échéance FDC servie, à reporter
    // sur le réarmement (cf. billDmaCycles). Transitoire : remis à 0 à chaque
    // réarmement, donc toujours nul à une frontière de trame → non sérialisé.
    int64_t  dmaStallPending_ = 0;
    void     refreshDriveSide();

private:
    // Une disquette montée (lecteur A ou B).
    struct FloppyDisk {
        std::vector<uint8_t> image;             // contenu (.st brut, .msa décompressé)
        std::string          path;              // chemin monté ("" = vide)
        int  spt = 9, sides = 2;                // géométrie (BPB)
        int  density = 1;                       // densité du média : 1=DD, 2=HD, 4=ED (cf. Hatari FloppyDensity)
        int  headTrack = 0;                     // position PHYSIQUE de la tête (≠ registre TR)
        bool writeProtect = false;              // protégé en écriture
        // `raw` = le fichier hôte peut être RÉ-ÉCRIT depuis `image`. Vrai pour les trois
        // conteneurs du modèle .ST (brut, .msa, .dim, cf. imgFormat) ; faux seulement pour
        // une image qu'on sait ne pas savoir ré-encoder (STX, ou en-tête .msa/.dim reconnu
        // mais indécodable) — là, réécrire détruirait le fichier de l'utilisateur.
        bool raw = true;
        // Conteneur du fichier hôte, qui décide COMMENT writeBack le met à jour :
        // brut = écriture partielle à l'offset ; .dim = idem décalé de l'en-tête 32 o ;
        // .msa = ré-encodage RLE du fichier ENTIER (port de MSA_WriteDisk).
        enum ImgFormat { FMT_ST = 0, FMT_MSA = 1, FMT_DIM = 2 };
        int  imgFormat = FMT_ST;

        // Image STX (Pasti) : si présente, le FDC dispatche vers le chemin _STX
        // (champs ID réels, statut par secteur, fuzzy/timing) au lieu du modèle .ST.
        enum ImgType { IMG_ST = 0, IMG_STX = 1 };
        int  imgType = IMG_ST;
        std::unique_ptr<StxImage> stx;          // non nul ⇔ imgType == IMG_STX
        std::string wd1772Path;                 // fichier compagnon des écritures STX ("" = aucun)

        // Un disque est présent si on a des octets .ST OU une image STX montée.
        bool present() const { return !image.empty() || stx != nullptr; }

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
    int      sidesPerDisk(int drive)    const;
    int      tracksPerDisk(int drive)   const;
    int      bytesPerTrack()            const;  // piste .ST : 6268 o × facteur de densité (DD/HD/ED)
    int      bytesPerTrackStx(int track, int side) const;       // longueur réelle de la piste STX
    int64_t  cyclesPerRev() const;              // période d'un tour (constante .ST, par piste STX)

    // --- Densité du média (cf. Hatari FDC_ComputeFloppyDensity & co) -----------
    // La densité (DD=1, HD=2, ED=4) est DÉDUITE de la géométrie : 18 spt → HD,
    // 36 spt → ED (.ST), ou de la longueur réelle de piste (STX). Le débit MFM
    // est multiplié d'autant (256 cyc/octet en DD, 128 en HD) ; la rotation
    // (300 tr/min) ne change pas. Sur Mega STE, $FF860E (bits 0-1) doit être
    // accordé à la densité du média, sinon RNF/LOST_DATA (CanMachineHandleDensity).
    int      computeFloppyDensity(const FloppyDisk& dk, int track, int side) const;
    void     updateFloppyDensity(int drive);    // rafraîchit FloppyDisk::density (sélection, seek/step)
    int      densityFactor() const;             // facteur du lecteur sélectionné (1 si aucun)
    bool     canHandleDensity() const;          // porte $FF860E (Mega STE seulement)
    int      transferDelay(int nbBytes) const;  // n octets MFM en cycles FDC (cf. FDC_TransferByte_FdcCycles)

    // --- Modèle rotationnel : impulsions d'index (cf. Hatari FDC_IndexPulse_*) --
    void     indexInit();                       // ancre l'index à une position « passée » aléatoire (déterministe)
    void     indexCheckUpdate();                // incrémente le compteur si un tour s'est écoulé
    void     indexIncrease(int64_t ipTime);     // valide une impulsion (compteur + son + force-int)
    int      indexCurrentPosBytes() const;      // position tête depuis l'index, en octets (−1 si pas de média)
    int      indexCurrentPosCycles() const;     // position tête depuis l'index, en cycles FDC (STX)
    bool     indexState() const;                // signal d'index actif (≈ 3,71 ms/tour)
    int64_t  nextIndexCycles() const;           // cycles FDC avant la prochaine impulsion (−1 si pas de média)

    // --- Machine à états des commandes (cf. Hatari FDC_Update*Cmd) -------------
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

    // --- Accès « bas niveau » à l'image .ST (cf. Hatari FDC_*_ST). Chacune dispatche
    //     vers sa variante _STX en tête si l'image montée est une STX. -----------
    uint8_t  readSectorST(uint8_t track, uint8_t sector, uint8_t side, int* pSize);
    uint8_t  writeSectorST(uint8_t track, uint8_t sector, uint8_t side, int size);
    uint8_t  readAddressST(uint8_t track, uint8_t sector, uint8_t side);
    uint8_t  readTrackST(uint8_t track, uint8_t side);
    uint8_t  writeTrackBuffer();                // WRITE TRACK : extrait les secteurs du flux écrit
    void     writeBack(FloppyDisk& dk, uint64_t off, uint64_t len);  // recopie dans le .st

    // --- Chemin STX (cf. Hatari FDC_*_STX) : champs ID réels, statut par secteur,
    //     bits fuzzy, timing variable. Utilise stxNextSector_ posé par nextSectorIDStx.
    int      nextSectorIDStx(int* pFdcCycles);
    uint8_t  readSectorStx(int* pSize);
    uint8_t  writeSectorStx(int size);
    uint8_t  readAddressStx();
    uint8_t  readTrackStx(int track, int side);
    uint8_t  writeTrackStx();                   // WRITE TRACK sur STX (flux brut conservé)
    void     stxPersist(FloppyDisk& dk);        // recopie les overlays dans le .wd1772

    // --- Tampon de transfert FDC↔DMA (cf. Hatari FDC_Buffer_*) ----------------
    // Chaque octet porte un TIMING (cycles FDC) : 256/densité pour .ST (bufferAdd),
    // ou variable pour STX (bufferAddTiming, secteurs « variable bit width »).
    void     bufferReset() { buf_.clear(); bufTiming_.clear(); bufPos_ = 0; }
    void     bufferAdd(uint8_t b);              // timing = transferDelay(1)
    void     bufferAddTiming(uint8_t b, uint16_t t) { buf_.push_back(b); bufTiming_.push_back(t); }
    // Les trois lectures sont BORNÉES : bufPos_ est restauré d'un save-state (où
    // bufPos_ == buf_.size() est un état LÉGITIME — tampon entièrement consommé,
    // ou vidé par bufferReset), et un état forgé peut entrer directement dans un
    // TRANSFER_LOOP sans passer par le TRANSFER_START qui teste bufferSize() == 0.
    // Hors tampon on rend l'octet d'une piste non formatée ($ff) : la machine à
    // états enchaîne alors sur son état CRC/COMPLETE comme en fin de transfert.
    uint8_t  bufferReadByte() {
        if (bufPos_ < 0 || static_cast<std::size_t>(bufPos_) >= buf_.size()) { ++bufPos_; return 0xff; }
        return buf_[bufPos_++];
    }
    uint16_t bufferReadTiming() const {
        if (bufPos_ < 0 || static_cast<std::size_t>(bufPos_) >= bufTiming_.size()) return 0;
        return bufTiming_[bufPos_];
    }
    uint8_t  bufferReadBytePos(int i) const {
        if (i < 0 || static_cast<std::size_t>(i) >= buf_.size()) return 0xff;
        return buf_[i];
    }
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
    // Le DMA/FDC route les accès $FF8604/06 vers le contrôleur ACSI `acsi_` (port de
    // hdc.c) quand le bit DMA_CSACSI est posé. Réception octet par octet (broche A1 =
    // DMA_A0), puis transfert DMA RAM↔image piloté ICI (on possède dmaAddr_/dmaMode_
    // et le plan mémoire). Après chaque octet accepté, IRQ HDC (= INTRQ/GPIP5) levée
    // si la cible est peuplée → le pilote/TOS poursuit ; cible vide → « pas de disque ».
    Acsi     acsi_;
    void     writeAcsi(uint32_t addr, uint8_t v);   // un octet de commande ACSI
    void     acsiDmaTransfer();                      // transfert DMA RAM↔image (Acsi_DmaTransfer)

public:
    // Boîtier de test DMA du kit Field Service (test « D DMA Port » des diagnostics
    // Atari) : cible ACSI 0, protocole à UN octet de commande — $10 = le boîtier
    // AVALE count×512 octets (RAM→port), $08 = il les REND (port→RAM). Le transfert
    // est immédiat, le compteur de secteurs décompte à ZÉRO, l'adresse DMA avance,
    // IRQ GPIP5 de fin (le test vérifie les trois : D0/D1/D3). Matériel de banc
    // d'atelier, pas de la machine : OFF par défaut, non sérialisé (cf. Mfp::loopback_).
    void     setDmaFixture(bool plugged) { dmaFixture_ = plugged; }
private:
    void     dmaFixtureTransfer(bool toFixture);     // exécute $10/$08 du boîtier
    bool     dmaFixture_ = false;
    std::vector<uint8_t> dmaFixtureBuf_;             // mémoire du boîtier (dernier bloc avalé)

    int      currentSide() const;               // face d'après le port A du PSG
    int      selectedDrive() const;             // 0 = A, 1 = B, -1 = aucun (PSG port A)

    // Renvoie true tant que la phase d'éjection (force WPRT=1) du lecteur `drive`
    // est active ; expire la transition quand l'échéance est dépassée. Calqué sur
    // Hatari Floppy_DriveTransitionUpdateState (Force=1 pendant l'éjection).
    bool     transitionForceWprt(int drive);
    int64_t  transitionWindow() const;          // durée d'une phase Mediach (18 VBL)
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
    uint16_t densityMode_ = 0; // $FF860E : bits 0-1 = mode FDC (0x00 DD, 0x03 HD) — porte Mega STE

    // État de la machine à états.
    int      command_ = 0;            // commande en cours (CMD_*) ; 0 = inactif
    int      commandState_ = 0;       // sous-état (RUN_*)
    uint8_t  commandType_ = 1;        // 1/2/3/4
    bool     replaceCommandPossible_ = false; // remplaçable pendant prepare+spinup
    int64_t  frameCycles_ = 160256;             // trame courante (cf. setFrameCycles), PAL au boot
    bool     driveEnabled_[2] = {true, true};   // cf. setDriveEnabled (config, pas d'état)
    bool     fastFloppy_ = false;     // « FDC rapide » (cf. setFastFdc) : délais /N
    bool     hostWriteBack_ = true;   // écrire les secteurs dans le FICHIER (cf. setHostWriteBack)
    bool     delayIndexPaced_ = false;// le délai courant est cadencé sur la rotation (non accéléré)
    bool     statusTypeI_ = true;     // le STR rapporte un statut type I
    uint8_t  statusTemp_ = 0;         // statut intermédiaire (lecture secteur)
    int      indexCounter_ = 0;       // tours comptés (spin-up, motor-off, timeout)
    uint8_t  interruptCond_ = 0;      // condition d'un Force Interrupt (type IV)

    // Champ ID du prochain secteur (rempli par nextSectorID()).
    uint8_t  nextID_TR_ = 0, nextID_SR_ = 1, nextID_LEN_ = 2, nextID_CRCOK_ = 1;
    int      stxNextSector_ = 0;     // index du secteur STX trouvé (cf. nextSectorIDStx)

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
    std::vector<uint16_t> bufTiming_;   // cycles FDC/octet : 256 pour .ST, variable pour STX
    int      bufPos_ = 0;
};
