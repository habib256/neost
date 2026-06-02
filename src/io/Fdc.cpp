// =============================================================================
//  Fdc.cpp — Implémentation WD1772 + DMA (modèle instantané).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Fdc.hpp"
#include "core/Bus.hpp"
#include "core/YM2149.hpp"
#include "io/Mfp.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

#include <sys/stat.h>

// --- Bits DMA control ($FF8606), cf. EmuTOS bios/dma.h -----------------------
enum : uint16_t {
    DMA_A0     = 0x0002, DMA_A1   = 0x0004,
    DMA_CSACSI = 0x0008,        // 1 = ACSI, 0 = disquette
    DMA_SCREG  = 0x0010,        // accès au compteur de secteurs
    DMA_FLOPPY = 0x0080,        // gate DRQ disquette
    DMA_WRBIT  = 0x0100,        // sens : écriture vers la disquette
};
// --- Bits statut DMA ($FF8606 en lecture) ---
enum : uint16_t { DMA_OK = 0x0001, DMA_SCNOT0 = 0x0002 };
// --- Bits statut WD1772 (cf. Hatari fdc.c) ---
//   type I  : INDEX(2) TR00(4) CRC(8) SEEKERR(10) SPINUP(20) WPRT(40) MOTOR(80)
//   type II/III : DRQ(2) LOSTDATA(4) CRC(8) RNF(10) RECTYPE(20) WPRT(40) MOTOR(80)
enum : uint8_t {
    FDC_INDEX   = 0x02,   // type I : tête au-dessus du trou d'index (1/tour)
    FDC_TRACK0  = 0x04,   // type I : tête sur piste 0
    FDC_RNF     = 0x10,   // type II/III : secteur introuvable (Record Not Found)
    FDC_SPINUP  = 0x20,   // type I : séquence spin-up terminée
    FDC_WRPRO   = 0x40,   // disquette protégée en écriture
    FDC_MOTOR_ON = 0x80,  // moteur en rotation
};

// Rotation du lecteur : 300 tr/min → 1 tour = 200 ms, et une impulsion d'index
// par tour. (C'est le « tic » périodique du lecteur ; il sert aussi à compter
// l'inactivité pour couper le moteur, comme le fait le WD1772.) Le moteur tourne
// encore ~9 tours (≈ 1,8 s) après la dernière commande avant de s'arrêter.
static constexpr int64_t INDEX_PERIOD_CYCLES = 200 * 8021;   // ~200 ms @ ~8 MHz
static constexpr int64_t INDEX_PULSE_CYCLES  = 29758;        // 3,71 ms : trou d'index visible (Hatari FDC_DELAY_US_INDEX_PULSE_LENGTH)
static constexpr int     MOTOR_OFF_REVS      = 9;            // tours d'inactivité → moteur off (Hatari FDC_DELAY_IP_MOTOR_OFF)

// Durée du créneau de transition média (éjection/insertion à chaud) pendant lequel
// WPRT est forcé, calqué sur Hatari (FLOPPY_DRIVE_TRANSITION_DELAY_VBL ≈ 18 VBL ;
// on prend ici quelques trames PAL, ce qui suffit à ce que TOS voie le flip WPRT).
// Une trame = 313 lignes × 512 cycles = 160256 cycles ; ~4 trames de fenêtre.
static constexpr int64_t FRAME_CYCLES               = 313 * 512;   // 160256 (cf. Machine)
static constexpr int64_t TRANSITION_WINDOW_CYCLES   = 4 * FRAME_CYCLES;

// Décompresse une image .msa (Magic Shadow Archiver) en image .st brute.
// En-tête (mots big-endian) : 0E0F, secteurs/piste, faces-1, piste début, fin.
// Chaque piste : un mot de longueur ; si != spt*512, flux RLE (marqueur 0xE5
// suivi de valeur + compteur mot). Cf. Hatari src/msa.c.
static bool decodeMsa(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out) {
    if (raw.size() < 10 || raw[0] != 0x0E || raw[1] != 0x0F) return false;
    const int spt   = (raw[2] << 8) | raw[3];
    const int sides = ((raw[4] << 8) | raw[5]) + 1;
    const int t0    = (raw[6] << 8) | raw[7];
    const int t1    = (raw[8] << 8) | raw[9];
    if (spt < 1 || spt > 30 || sides < 1 || sides > 2 || t1 < t0) return false;
    const std::size_t trackBytes = static_cast<std::size_t>(spt) * 512u;
    out.clear();
    std::size_t p = 10;
    for (int track = t0; track <= t1; ++track)
        for (int s = 0; s < sides; ++s) {
            if (p + 2 > raw.size()) return false;
            const int len = (raw[p] << 8) | raw[p + 1]; p += 2;
            if (p + len > raw.size()) return false;
            if (static_cast<std::size_t>(len) == trackBytes) {        // piste non compressée
                out.insert(out.end(), raw.begin() + p, raw.begin() + p + len);
                p += len;
            } else {                                                   // piste RLE
                const std::size_t target = out.size() + trackBytes;
                const std::size_t end = p + len;
                while (p < end && out.size() < target) {
                    const uint8_t b = raw[p++];
                    if (b == 0xE5 && p + 3 <= end) {                   // run-length
                        const uint8_t val = raw[p];
                        const int cnt = (raw[p + 1] << 8) | raw[p + 2]; p += 3;
                        out.insert(out.end(), static_cast<std::size_t>(cnt), val);
                    } else {
                        out.push_back(b);
                    }
                }
                if (out.size() != target) return false;                // piste mal décodée
            }
        }
    return true;
}

bool Fdc::loadImage(const std::string& path, int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    const bool wasPresent = !dk.image.empty();   // disque déjà monté → échange à chaud
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "[FDC] image introuvable : %s\n", path.c_str()); return false; }
    const std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> raw(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(raw.data()), n);

    // .msa (compressé) → décompression en .st brut ; sinon image .st telle quelle.
    std::vector<uint8_t> msa;
    if (decodeMsa(raw, msa)) {
        dk.image = std::move(msa);
        dk.raw = false;                          // .msa : pas de recopie d'écritures
        std::fprintf(stderr, "[FDC] image .msa décompressée : %s\n", path.c_str());
    } else {
        dk.image = std::move(raw);               // .st brut
        dk.raw = true;
    }

    // Géométrie depuis le BPB (offsets 0x18 = secteurs/piste, 0x1A = faces).
    if (dk.image.size() >= 0x1C) {
        const int spt   = dk.image[0x18] | (dk.image[0x19] << 8);
        const int sides = dk.image[0x1A] | (dk.image[0x1B] << 8);
        if (spt   >= 1 && spt   <= 30) dk.spt   = spt;
        if (sides >= 1 && sides <= 2)  dk.sides = sides;
    }
    dk.path = path;

    // Changement de média à chaud (cf. Hatari Floppy_DriveTransitionSetState) :
    //  - échange à chaud (un disque était déjà présent) → on arme la phase
    //    d'ÉJECTION (force WPRT le temps de la fenêtre) ; l'insertion qui suit ne
    //    force rien, donc l'éjection seule suffit à provoquer le flip WPRT que TOS
    //    surveille pour relire le répertoire de la nouvelle disquette ;
    //  - premier montage (boot, aucun disque présent) → simple INSERTION, qui ne
    //    force PAS WPRT : le chemin de boot reste donc rigoureusement inchangé.
    if (sched_) {
        dk.transitionPhase    = wasPresent ? FloppyDisk::TRANS_EJECT : FloppyDisk::TRANS_INSERT;
        dk.transitionDeadline = sched_->now() + TRANSITION_WINDOW_CYCLES;
    }

    // Write-protect auto-détecté d'après les permissions du fichier (cf. Hatari
    // floppy.c:Floppy_IsWriteProtected, mode « automatic ») : on stat() l'image et
    // on regarde le bit propriétaire S_IWUSR — si le fichier n'est pas inscriptible,
    // la disquette est protégée. Une image .msa est TOUJOURS protégée car writeBack
    // ne sait pas réencoder le format compressé (dk.raw == false).
    struct stat st;
    const bool writable = (::stat(path.c_str(), &st) == 0) && (st.st_mode & S_IWUSR);
    dk.writeProtect = !dk.raw || !writable;

    std::fprintf(stderr, "[FDC] lecteur %c : %s (%zu Ko, %d secteurs/piste, %d faces%s)\n",
                 drive & 1 ? 'B' : 'A', path.c_str(), dk.image.size() / 1024, dk.spt, dk.sides,
                 dk.writeProtect ? ", protégé en écriture" : "");
    return true;
}

void Fdc::eject(int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    const bool wasPresent = !dk.image.empty();
    dk.image.clear();
    dk.path.clear();
    // Éjection à chaud : on force WPRT pendant la fenêtre de transition (cf. Hatari
    // Floppy_DriveTransitionSetState, STATE_EJECT). Une éjection « à vide » (aucun
    // disque présent) n'arme rien.
    if (wasPresent && sched_) {
        dk.transitionPhase    = FloppyDisk::TRANS_EJECT;
        dk.transitionDeadline = sched_->now() + TRANSITION_WINDOW_CYCLES;
    }
    std::fprintf(stderr, "[FDC] lecteur %c éjecté\n", drive & 1 ? 'B' : 'A');
}

int Fdc::currentSide() const {
    // Port A du PSG (registre 14), bit0 actif bas : 0 = face 1, 1 = face 0.
    return (psg_.regs_[14] & 0x01) ? 0 : 1;
}
// Sélection lecteur : port A du PSG bit1 = A (actif bas), bit2 = B (actif bas).
int Fdc::selectedDrive() const {
    if ((psg_.regs_[14] & 0x02) == 0) return 0;   // A prioritaire si les deux
    if ((psg_.regs_[14] & 0x04) == 0) return 1;
    return -1;
}

// Transition de média (cf. Hatari Floppy_DriveTransitionUpdateState) : tant que la
// phase d'ÉJECTION est active (échéance non dépassée), WPRT est forcé à 1 ; la phase
// d'INSERTION ne force rien (Force=0). On expire la transition une fois l'échéance
// franchie. Sans ordonnanceur (now() indispo), aucune transition n'est active.
bool Fdc::transitionForceWprt(int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    if (dk.transitionPhase == FloppyDisk::TRANS_NONE || !sched_) return false;
    if (sched_->now() >= dk.transitionDeadline) {   // fenêtre écoulée → transition finie
        dk.transitionPhase = FloppyDisk::TRANS_NONE;
        return false;
    }
    return dk.transitionPhase == FloppyDisk::TRANS_EJECT;  // éjection → force WPRT
}

void Fdc::setIntrq(bool on) {
    intrq_ = on;
    mfp_.setFdcLine(on);                 // ligne GPIP5 (polling, ex. EmuTOS)
    if (on) mfp_.raise(Mfp::SRC_FDC);    // ET interruption canal 7 (jeux qui l'utilisent)
}

uint8_t Fdc::dmaStatus() const {
    return uint8_t(DMA_OK | (dmaCount_ ? DMA_SCNOT0 : 0));   // pas d'erreur DMA
}

// -----------------------------------------------------------------------------
//  Accès mémoire. $FF8604 = data (mot), $FF8606 = control/status (mot),
//  $FF8609/0B/0D = adresse DMA. Le 68000 fait des mots → octet haut puis bas.
// -----------------------------------------------------------------------------
uint8_t Fdc::read8(uint32_t addr) {
    switch (addr & 0xF) {
        case 0x4: return 0;                         // data, octet haut
        case 0x5:                                    // data, octet bas
            if (dmaMode_ & DMA_SCREG) return uint8_t(dmaCount_);
            if (dmaMode_ & DMA_CSACSI) return acsiStatus_;   // statut du contrôleur ACSI
            switch (dmaMode_ & (DMA_A1 | DMA_A0)) {
                case 0: {             // FDC_CS : lire le statut efface l'INTRQ
                    setIntrq(false);
                    uint8_t s = status_;
                    // Bit INDEX (type I) : actif au passage du trou d'index, 1/tour.
                    if (motorRunning_ && lastCmdTypeI_ && sched_) {
                        const int64_t phase = (sched_->now() - indexRef_) % INDEX_PERIOD_CYCLES;
                        if (phase >= 0 && phase < INDEX_PULSE_CYCLES) s |= FDC_INDEX;
                    }
                    // Changement de média à chaud : pendant la fenêtre d'éjection on
                    // force WPRT (cf. Hatari ForceWPRT == 1), ce que TOS lit après un
                    // Restore pour détecter le changement et relire le répertoire.
                    const int d = selectedDrive();
                    if (d >= 0 && transitionForceWprt(d)) s |= FDC_WRPRO;
                    return s;
                }
                case DMA_A0:          return track_;                    // FDC_TR
                case DMA_A1:          return sector_;                   // FDC_SR
                default:              return data_;                    // FDC_DR
            }
        case 0x6: return 0;                          // status, octet haut
        case 0x7: return uint8_t(dmaStatus());       // status, octet bas
        // Adresse DMA ($FF8609/0B/0D) : RELISIBLE — le compteur incrémente pendant
        // le transfert (cf. Hatari FDC_GetDMAAddress). Les diagnostics la relisent
        // après une commande pour vérifier que la DMA a transféré le bon nombre
        // d'octets (sinon « DMA count error »).
        case 0x9: return uint8_t(dmaAddr_ >> 16);
        case 0xB: return uint8_t(dmaAddr_ >> 8);
        case 0xD: return uint8_t(dmaAddr_);
        case 0xE: case 0xF: return density_;         // $FF860E : densité DD/HD
        default:  return 0xFF;
    }
}

void Fdc::write8(uint32_t addr, uint8_t v) {
    switch (addr & 0xF) {
        case 0x4: dataHi_ = v; return;               // data, octet haut (latch)
        case 0x5:                                    // data, octet bas → action
            if (dmaMode_ & DMA_SCREG) { dmaCount_ = uint16_t((dataHi_ << 8) | v); return; }
            if (dmaMode_ & DMA_CSACSI) { writeAcsi(addr, v); return; }   // disque dur ACSI
            switch (dmaMode_ & (DMA_A1 | DMA_A0)) {
                case 0:               executeCommand(v); return;   // FDC_CS : commande
                case DMA_A0:          track_  = v;       return;   // FDC_TR
                case DMA_A1:          sector_ = v;       return;   // FDC_SR
                default:              data_   = v;       return;   // FDC_DR
            }
        case 0x6: ctrlHi_ = v; return;               // control, octet haut (latch)
        case 0x7: dmaMode_ = uint16_t((ctrlHi_ << 8) | v); return;  // control, octet bas
        case 0x9: dmaAddr_ = (dmaAddr_ & 0x00FFFF) | (uint32_t(v) << 16); return;
        case 0xB: dmaAddr_ = (dmaAddr_ & 0xFF00FF) | (uint32_t(v) << 8);  return;
        case 0xD: dmaAddr_ = (dmaAddr_ & 0xFFFF00) |  uint32_t(v);        return;
        case 0xE: case 0xF: density_ = v; return;    // $FF860E : densité DD/HD
        default:  return;
    }
}

// WD1772 : bit 0 du statut = BUSY (commande en cours).
enum : uint8_t { FDC_BUSY = 0x01 };

// Durée réaliste d'une commande avant la fin (INTRQ). Type I : step-rate × pas ;
// type II/III : ~temps de transfert par secteur. (Le débit réel est ~16 ms/secteur ;
// on le modélise accéléré — l'essentiel est que la commande ne soit plus instantanée.)
int64_t Fdc::commandDelayCycles(uint8_t cmd) {
    constexpr int64_t MS = 8021;                       // cycles CPU par ms (~8 MHz)
    if (!(cmd & 0x80)) {                               // type I : seek / restore / step
        static constexpr int stepMs[4] = {6, 12, 2, 3};   // taux WD1772 (bits 0-1)
        const int sr = stepMs[cmd & 0x03];
        int steps = 1;
        if      ((cmd & 0xF0) == 0x00) steps = track_;                 // RESTORE → TR00
        else if ((cmd & 0xF0) == 0x10) {                               // SEEK
            steps = int(data_) - int(track_);
            if (steps < 0) steps = -steps;
        }
        if (steps < 1) steps = 1;
        return MS * sr * steps;
    }
    const int count = dmaCount_ ? dmaCount_ : 1;       // type II/III : par secteur
    return MS * count;                                 // ~1 ms/secteur (accéléré)
}

void Fdc::executeCommand(uint8_t cmd) {
    // FORCE INTERRUPT ($Dx) : termine immédiatement la commande en cours.
    if ((cmd & 0xF0) == 0xD0) {
        if (sched_) sched_->cancel(Scheduler::FDC);
        busy_ = false;
        status_ &= ~FDC_BUSY;
        setIntrq(true);
        return;
    }

    const int64_t delay = commandDelayCycles(cmd);     // AVANT de bouger track_

    // Moteur (matériel + son) : toute commande l'énergise et relance le compte
    //  d'inactivité ; le WD1772 le coupera après MOTOR_OFF_REVS tours (cf.
    //  onIndexPulse). Les commandes type I (seek/restore/step) déplacent la tête →
    //  « clic » d'un pas, ou bruit de seek si plusieurs pistes sont franchies.
    motorOn();
    lastCmdTypeI_ = !(cmd & 0x80);                     // le bit INDEX n'a de sens qu'en type I
    if (!(cmd & 0x80)) {                               // type I : seek / restore / step
        int steps = 1;
        if      ((cmd & 0xF0) == 0x00) steps = track_;            // RESTORE → piste 0
        else if ((cmd & 0xF0) == 0x10) {                          // SEEK → piste cible
            steps = int(data_) - int(track_);
            if (steps < 0) steps = -steps;
        }
        emitSound(steps > 1 ? FdcSound::Seek : FdcSound::Step);
    }

    uint8_t result;
    switch (cmd & 0xF0) {
        case 0x00: track_ = 0;     result = FDC_TRACK0;                  break; // RESTORE
        case 0x10: track_ = data_; result = track_ ? 0 : FDC_TRACK0;    break; // SEEK
        case 0x20: case 0x40: case 0x60:                                        // STEP/IN/OUT
                   result = track_ ? 0 : FDC_TRACK0;                    break;
        case 0x80: result = readSectors(cmd);                           break; // READ SECTOR
        case 0xA0: result = writeSectors(cmd);                          break; // WRITE SECTOR
        case 0xC0: result = readAddress();                              break; // READ ADDRESS
        case 0xE0: result = readTrack();                                break; // READ TRACK
        case 0xF0: result = writeTrack();                               break; // WRITE TRACK
        default:   result = 0;                                          break;
    }

    // Statut WD1772 fidèle (cf. Hatari) : le moteur tourne pendant/après toute
    // commande ; les commandes type I (seek/restore/step) signalent aussi le
    // spin-up terminé et l'état write-protect de la disquette.
    result |= FDC_MOTOR_ON;
    if (!(cmd & 0x80)) {                               // type I
        result |= FDC_SPINUP;
        const int d = selectedDrive();
        if (d >= 0 && drive_[d].writeProtect) result |= FDC_WRPRO;
    }

    // Le transfert DMA est déjà fait (données en RAM) ; on POSE BUSY et on diffère
    // l'INTRQ : pendant ce temps, le statut lu garde BUSY et l'INTRQ reste haut.
    pendingStatus_ = result;
    busy_   = true;
    status_ = uint8_t(result | FDC_BUSY);
    intrq_  = false;
    mfp_.setFdcLine(false);
    if (sched_) sched_->schedule(Scheduler::FDC, sched_->now() + delay);
    else        onCommandComplete();        // sans ordonnanceur : repli instantané
}

void Fdc::onCommandComplete() {
    busy_   = false;
    status_ = pendingStatus_;               // BUSY tombe, statut final
    setIntrq(true);                         // fin de commande → GPIP5 bas + canal 7
}

// Énergise le moteur (à la première commande) et arme l'impulsion d'index. Toute
// commande remet le compte d'inactivité à zéro pour garder le moteur en rotation.
void Fdc::motorOn() {
    idleRevs_ = 0;
    if (motorRunning_) return;
    motorRunning_ = true;
    if (sched_) indexRef_ = sched_->now();        // origine de la phase d'index
    emitSound(FdcSound::MotorOn);
    if (sched_) sched_->schedule(Scheduler::FDC_INDEX, sched_->now() + INDEX_PERIOD_CYCLES);
}

// Coupe le moteur : le bit MOTEUR retombe dans le statut, l'index se désarme.
void Fdc::motorOff() {
    if (!motorRunning_) return;
    motorRunning_ = false;
    status_ &= ~FDC_MOTOR_ON;
    if (sched_) sched_->cancel(Scheduler::FDC_INDEX);
    emitSound(FdcSound::MotorOff);
}

// Impulsion d'index (datée 1/tour par l'ordonnanceur tant que le moteur tourne) :
// émet le « tic » et compte les tours d'inactivité ; au-delà du seuil, coupe le
// moteur (comme le WD1772). Une commande en cours (BUSY) garde le moteur vif.
void Fdc::onIndexPulse() {
    if (!motorRunning_) return;
    emitSound(FdcSound::Index);
    if (!busy_ && ++idleRevs_ >= MOTOR_OFF_REVS) { motorOff(); return; }
    if (sched_) sched_->schedule(Scheduler::FDC_INDEX, sched_->now() + INDEX_PERIOD_CYCLES);
}

// Numéro de secteur logique → offset image (.st : piste, puis face, puis secteur).
static inline uint32_t lsnOffset(int track, int side, int sector, int spt, int sides) {
    const int lsn = (track * sides + side) * spt + (sector - 1);
    return uint32_t(lsn) * 512u;
}

// -----------------------------------------------------------------------------
//  Contrôleur ACSI (disque dur). Port minimal de Hatari hdc.c : commande de 6
//  octets (classe 0) reçue octet par octet via la DMA, transfert DMA, statut.
//  Un disque virtuel EN MÉMOIRE (cible 0) suffit au « Hard Disk DMA Exerciser »
//  du diagnostic (écrit puis relit et vérifie — l'aller-retour doit être fidèle).
// -----------------------------------------------------------------------------
static constexpr uint32_t ACSI_DISK_CAP = 64u * 1024u * 1024u;   // plafond du disque virtuel

void Fdc::writeAcsi(uint32_t /*addr*/, uint8_t v) {
    setIntrq(false);                       // efface l'IRQ (réarmée si l'octet est accepté)
    // Le « pin A1 » de l'ACSI est câblé sur le bit de contrôle DMA_A0 (0x02) : 0 pour le
    // 1er octet du paquet (sélection cible + opcode), 1 pour les octets suivants. On
    // ignore ce pin pour le 2e octet (byteCount==1), comme le vrai matériel (cf. Hatari).
    const bool a1 = (dmaMode_ & DMA_A0) != 0;
    if (!a1 && acsiByteCount_ != 1) {
        // 1er octet du paquet (A1=0) : bits 7-5 = cible, bits 4-0 = opcode.
        acsiTarget_ = uint8_t((v >> 5) & 7);
        acsiCmd_[0] = uint8_t(v & 0x1F);
        acsiByteCount_ = 1;
    } else {
        if (acsiByteCount_ < 6) acsiCmd_[acsiByteCount_++] = v;
        if (acsiByteCount_ == 6) { executeAcsi(); acsiByteCount_ = 0; }
    }
    // Seule la cible 0 porte un disque : on acquitte (IRQ HDC = INTRQ/GPIP5) pour
    // que le CPU poursuive. Toute autre cible reste muette → « pas de disque ».
    if (acsiTarget_ == 0) setIntrq(true);
}

void Fdc::executeAcsi() {
    const uint8_t op = acsiCmd_[0];
    const uint32_t lba = (uint32_t(acsiCmd_[1] & 0x1F) << 16) | (uint32_t(acsiCmd_[2]) << 8) | acsiCmd_[3];
    const int      cnt = acsiCmd_[4] ? acsiCmd_[4] : 256;
    acsiStatus_ = 0;                                      // OK par défaut
    // Disque virtuel agrandi à la demande (l'exerciser teste des LBA élevés, ~20 Mo)
    // jusqu'à un plafond ; au-delà → erreur (cible inexistante).
    const uint64_t need = uint64_t(lba + uint32_t(cnt)) * 512u;
    if (need <= ACSI_DISK_CAP && hd_.size() < need) hd_.resize(size_t(need), 0);
    auto toRam = [&](const uint8_t* src, uint32_t n) {
        for (uint32_t j = 0; j < n; ++j) { const uint32_t a = (dmaAddr_ + j) & 0x00FFFFFF;
            if (a < bus_.ram.size()) bus_.ram[a] = src[j]; }
        dmaAddr_ += n;
    };
    switch (op) {
        case 0x08: {                                     // READ(6) : disque → RAM
            uint32_t off = lba * 512u;
            for (int i = 0; i < cnt && off + 512u <= hd_.size(); ++i, off += 512u) toRam(&hd_[off], 512);
            dmaCount_ = 0; break;
        }
        case 0x0A: {                                     // WRITE(6) : RAM → disque
            uint32_t off = lba * 512u, src = dmaAddr_;
            for (int i = 0; i < cnt && off + 512u <= hd_.size(); ++i, off += 512u)
                for (uint32_t j = 0; j < 512u; ++j) { const uint32_t a = (src + uint32_t(i)*512u + j) & 0x00FFFFFF;
                    if (a < bus_.ram.size()) hd_[off + j] = bus_.ram[a]; }
            dmaAddr_ += uint32_t(cnt) * 512u; dmaCount_ = 0; break;
        }
        case 0x12: {                                     // INQUIRY : identité du périphérique
            uint8_t inq[36] = {0};
            inq[0] = 0x00;          // type : disque à accès direct
            inq[1] = 0x00;          // non amovible
            inq[2] = 0x02;          // version SCSI-2
            inq[4] = 31;            // longueur additionnelle
            std::memcpy(inq + 8, "NeoST   NeoST Hard Disk  1.0 ", 28);
            toRam(inq, uint32_t(acsiCmd_[4] ? acsiCmd_[4] : 36)); dmaCount_ = 0; break;
        }
        case 0x25: {                                     // READ CAPACITY (classe 1, mais inoffensif ici)
            const uint32_t last = ACSI_DISK_CAP / 512u - 1;
            uint8_t cap[8] = { uint8_t(last >> 24), uint8_t(last >> 16), uint8_t(last >> 8), uint8_t(last),
                               0, 0, 2, 0 };             // taille de bloc = 512
            toRam(cap, 8); dmaCount_ = 0; break;
        }
        case 0x00:                                       // TEST UNIT READY
        case 0x03:                                       // REQUEST SENSE
        case 0x04:                                       // FORMAT UNIT
        case 0x0B:                                       // SEEK(6)
        case 0x15:                                       // MODE SELECT
        case 0x1A:                                       // MODE SENSE
        default:
            acsiStatus_ = 0; break;                      // accepté (pas d'erreur)
    }
}

uint8_t Fdc::readSectors(uint8_t /*cmd*/) {
    const int d = selectedDrive();
    if (d < 0 || drive_[d].image.empty()) return FDC_RNF;
    FloppyDisk& dk = drive_[d];
    const int side = currentSide();
    int count = dmaCount_ ? dmaCount_ : 1;
    uint32_t off = lsnOffset(track_, side, sector_, dk.spt, dk.sides);

    for (int i = 0; i < count; ++i) {
        if (off + 512u > dk.image.size()) return FDC_RNF;      // secteur hors disque
        for (uint32_t j = 0; j < 512u; ++j) {                  // DMA → RAM
            const uint32_t a = (dmaAddr_ + j) & 0x00FFFFFF;
            if (a < bus_.ram.size()) bus_.ram[a] = dk.image[off + j];
        }
        off += 512u; dmaAddr_ += 512u;
    }
    dmaCount_ = 0;
    return 0;   // succès
}

uint8_t Fdc::writeSectors(uint8_t /*cmd*/) {
    const int d = selectedDrive();
    if (d < 0 || drive_[d].image.empty()) return FDC_RNF;
    FloppyDisk& dk = drive_[d];
    if (dk.writeProtect) return FDC_RNF | FDC_WRPRO;  // disquette protégée → refus
    const int side = currentSide();
    int count = dmaCount_ ? dmaCount_ : 1;
    const uint32_t off0 = lsnOffset(track_, side, sector_, dk.spt, dk.sides);
    uint32_t off = off0;

    for (int i = 0; i < count; ++i) {
        if (off + 512u > dk.image.size()) return FDC_RNF;
        for (uint32_t j = 0; j < 512u; ++j) {                  // RAM → image (en mémoire)
            const uint32_t a = (dmaAddr_ + j) & 0x00FFFFFF;
            dk.image[off + j] = (a < bus_.ram.size()) ? bus_.ram[a] : 0;
        }
        off += 512u; dmaAddr_ += 512u;
    }
    dmaCount_ = 0;
    writeBack(dk, off0, off - off0);     // Flopwr : recopie les secteurs dans le .st
    return 0;
}

// Recopie une zone modifiée de l'image en mémoire vers le fichier .st monté
// (persistance des écritures — cf. TODO « Flopwr complet → recopie dans le .st »).
void Fdc::writeBack(FloppyDisk& dk, uint32_t off, uint32_t len) {
    if (!dk.raw || dk.path.empty() || off + len > dk.image.size()) return;  // .msa : pas de recopie
    std::fstream f(dk.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) return;                  // image en lecture seule / FS virtuel non inscriptible
    f.seekp(off);
    f.write(reinterpret_cast<const char*>(dk.image.data() + off), len);
}

uint8_t Fdc::readAddress() {
    const int d = selectedDrive();
    if (d < 0 || drive_[d].image.empty()) return FDC_RNF;
    // Champ ID renvoyé par DMA : piste, face, secteur, taille(2=512), CRC.
    const uint8_t id[6] = { track_, uint8_t(currentSide()), sector_, 0x02, 0, 0 };
    for (uint32_t j = 0; j < 6; ++j) {
        const uint32_t a = (dmaAddr_ + j) & 0x00FFFFFF;
        if (a < bus_.ram.size()) bus_.ram[a] = id[j];
    }
    dmaAddr_ += 6;
    return 0;
}

// WRITE TRACK ($F0) = formatage. La DMA fournit l'image MFM brute d'une piste
// (gaps, sync, marques d'adresse, données). On la PARCOURT pour extraire les
// secteurs (IDAM $FE → piste/face/secteur/taille, puis DAM $FB/$F8 → 512 octets de
// données) et on les écrit dans l'image .ST là où la géométrie le permet. NB :
// Hatari ne supporte PAS du tout WRITE TRACK sur .ST (renvoie « lost data ») ; on
// fait mieux (best-effort) MAIS un reformatage à géométrie non standard (ex. 18
// secteurs/piste sur une image 9 spt) ne tient pas — il faudrait une image flux/HD.
// L'essentiel ici : CONSOMMER la DMA (sinon « DMA count error » côté diagnostic).
uint8_t Fdc::writeTrack() {
    const int d = selectedDrive();
    if (d < 0 || drive_[d].image.empty()) return FDC_RNF;
    FloppyDisk& dk = drive_[d];
    if (dk.writeProtect) return FDC_WRPRO;
    const uint32_t bytes = uint32_t(dmaCount_ ? dmaCount_ : 1) * 512u;
    const uint32_t base  = dmaAddr_;
    auto rb = [&](uint32_t k) -> uint8_t {
        const uint32_t a = (base + k) & 0x00FFFFFF;
        return a < bus_.ram.size() ? bus_.ram[a] : 0;
    };
    for (uint32_t i = 0; i + 6 < bytes; ) {
        if (rb(i) == 0xFE) {                                   // IDAM : champ d'adresse
            const uint8_t tr = rb(i + 1), sd = rb(i + 2), sec = rb(i + 3);
            uint32_t k = i + 5;                                // cherche la marque de données
            while (k < bytes && rb(k) != 0xFB && rb(k) != 0xF8) ++k;
            if (k < bytes && k + 1 + 512 <= bytes) {
                const uint32_t off = lsnOffset(tr, sd, sec, dk.spt, dk.sides);
                if (off + 512u <= dk.image.size()) {
                    for (uint32_t j = 0; j < 512u; ++j) dk.image[off + j] = rb(k + 1 + j);
                    writeBack(dk, off, 512u);
                }
                i = k + 1 + 512;
                continue;
            }
        }
        ++i;
    }
    dmaAddr_ += bytes;        // la DMA a bien transféré toute la piste
    dmaCount_ = 0;
    return 0;
}

// READ TRACK ($E0) : lit la piste brute via DMA. On ne synthétise pas le format MFM
// complet (gaps/marques) ; on CONSOMME la DMA pour que le compteur retombe à 0
// (évite « DMA count error »). Les octets transférés sont au mieux approximatifs.
uint8_t Fdc::readTrack() {
    const uint32_t bytes = uint32_t(dmaCount_ ? dmaCount_ : 1) * 512u;
    for (uint32_t j = 0; j < bytes; ++j) {
        const uint32_t a = (dmaAddr_ + j) & 0x00FFFFFF;
        if (a < bus_.ram.size()) bus_.ram[a] = 0;
    }
    dmaAddr_ += bytes;
    dmaCount_ = 0;
    return 0;
}
