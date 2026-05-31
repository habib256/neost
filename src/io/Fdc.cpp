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
#include <fstream>

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
// --- Bits statut FDC ---
enum : uint8_t { FDC_TRACK0 = 0x04, FDC_RNF = 0x10, FDC_WRPRO = 0x40 };

bool Fdc::loadImage(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "[FDC] image introuvable : %s\n", path.c_str()); return false; }
    const std::streamsize n = f.tellg();
    f.seekg(0);
    image_.resize(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(image_.data()), n);

    // Géométrie depuis le BPB (offsets 0x18 = secteurs/piste, 0x1A = faces).
    if (image_.size() >= 0x1C) {
        const int spt   = image_[0x18] | (image_[0x19] << 8);
        const int sides = image_[0x1A] | (image_[0x1B] << 8);
        if (spt   >= 1 && spt   <= 30) spt_   = spt;
        if (sides >= 1 && sides <= 2)  sides_ = sides;
    }
    path_ = path;
    std::fprintf(stderr, "[FDC] disquette montée : %s (%zu Ko, %d secteurs/piste, %d faces)\n",
                 path.c_str(), image_.size() / 1024, spt_, sides_);
    return true;
}

void Fdc::eject() {
    image_.clear();
    path_.clear();
    std::fprintf(stderr, "[FDC] disquette éjectée\n");
}

int Fdc::currentSide() const {
    // Port A du PSG (registre 14), bit0 actif bas : 0 = face 1, 1 = face 0.
    return (psg_.regs_[14] & 0x01) ? 0 : 1;
}
bool Fdc::driveASelected() const { return (psg_.regs_[14] & 0x02) == 0; }

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
            switch (dmaMode_ & (DMA_A1 | DMA_A0)) {
                case 0:               setIntrq(false); return status_;  // FDC_CS : lire le statut efface l'INTRQ
                case DMA_A0:          return track_;                    // FDC_TR
                case DMA_A1:          return sector_;                   // FDC_SR
                default:              return data_;                    // FDC_DR
            }
        case 0x6: return 0;                          // status, octet haut
        case 0x7: return uint8_t(dmaStatus());       // status, octet bas
        default:  return 0xFF;
    }
}

void Fdc::write8(uint32_t addr, uint8_t v) {
    switch (addr & 0xF) {
        case 0x4: dataHi_ = v; return;               // data, octet haut (latch)
        case 0x5:                                    // data, octet bas → action
            if (dmaMode_ & DMA_SCREG) { dmaCount_ = uint16_t((dataHi_ << 8) | v); return; }
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
        default:  return;
    }
}

void Fdc::executeCommand(uint8_t cmd) {
    switch (cmd & 0xF0) {
        case 0x00: track_ = 0;     status_ = FDC_TRACK0;                  break; // RESTORE
        case 0x10: track_ = data_; status_ = track_ ? 0 : FDC_TRACK0;    break; // SEEK
        case 0x20: case 0x40: case 0x60:                                        // STEP/IN/OUT
                   status_ = track_ ? 0 : FDC_TRACK0;                    break;
        case 0x80: status_ = readSectors(cmd);                           break; // READ SECTOR
        case 0xA0: status_ = writeSectors(cmd);                          break; // WRITE SECTOR
        case 0xC0: status_ = readAddress();                              break; // READ ADDRESS
        case 0xD0: /* FORCE INTERRUPT */                                 break;
        default:   status_ = 0;                                          break;
    }
    setIntrq(true);   // commande terminée → GPIP5 bas
}

// Numéro de secteur logique → offset image (.st : piste, puis face, puis secteur).
static inline uint32_t lsnOffset(int track, int side, int sector, int spt, int sides) {
    const int lsn = (track * sides + side) * spt + (sector - 1);
    return uint32_t(lsn) * 512u;
}

uint8_t Fdc::readSectors(uint8_t /*cmd*/) {
    if (image_.empty() || !driveASelected()) return FDC_RNF;
    const int side = currentSide();
    int count = dmaCount_ ? dmaCount_ : 1;
    uint32_t off = lsnOffset(track_, side, sector_, spt_, sides_);

    for (int i = 0; i < count; ++i) {
        if (off + 512u > image_.size()) return FDC_RNF;        // secteur hors disque
        for (uint32_t j = 0; j < 512u; ++j) {                  // DMA → RAM
            const uint32_t a = (dmaAddr_ + j) & 0x00FFFFFF;
            if (a < bus_.ram.size()) bus_.ram[a] = image_[off + j];
        }
        off += 512u; dmaAddr_ += 512u;
    }
    dmaCount_ = 0;
    return 0;   // succès
}

uint8_t Fdc::writeSectors(uint8_t /*cmd*/) {
    if (image_.empty() || !driveASelected()) return FDC_RNF;
    const int side = currentSide();
    int count = dmaCount_ ? dmaCount_ : 1;
    uint32_t off = lsnOffset(track_, side, sector_, spt_, sides_);

    for (int i = 0; i < count; ++i) {
        if (off + 512u > image_.size()) return FDC_RNF;
        for (uint32_t j = 0; j < 512u; ++j) {                  // RAM → image (en mémoire)
            const uint32_t a = (dmaAddr_ + j) & 0x00FFFFFF;
            image_[off + j] = (a < bus_.ram.size()) ? bus_.ram[a] : 0;
        }
        off += 512u; dmaAddr_ += 512u;
    }
    dmaCount_ = 0;
    return 0;
}

uint8_t Fdc::readAddress() {
    if (image_.empty() || !driveASelected()) return FDC_RNF;
    // Champ ID renvoyé par DMA : piste, face, secteur, taille(2=512), CRC.
    const uint8_t id[6] = { track_, uint8_t(currentSide()), sector_, 0x02, 0, 0 };
    for (uint32_t j = 0; j < 6; ++j) {
        const uint32_t a = (dmaAddr_ + j) & 0x00FFFFFF;
        if (a < bus_.ram.size()) bus_.ram[a] = id[j];
    }
    dmaAddr_ += 6;
    return 0;
}
