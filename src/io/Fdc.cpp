// =============================================================================
//  Fdc.cpp — WD1772 + DMA disquette : modèle ROTATIONNEL daté (port Hatari).
//
//  Machine à états par commande (cf. extern/hatari/src/fdc.c, chemin « _ST »).
//  Chaque phase renvoie un nombre de cycles FDC (≈ cycles CPU à ~8 MHz sur ST) ;
//  l'ordonnanceur (Scheduler::FDC) rappelle onFdcEvent() à l'échéance pour avancer
//  la commande. On modélise : impulsions d'index (300 tr/min), spin-up (6 tours),
//  chargement de tête (15 ms), latence rotationnelle jusqu'au champ ID du secteur,
//  transfert DMA octet par octet (FIFO 16 o), arrêt moteur (9 tours), INTRQ datée.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Fdc.hpp"
#include "core/Cpu68k.hpp"
#include "core/Bus.hpp"
#include "core/YM2149.hpp"
#include "io/Mfp.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
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

// Masque d'adresse DMA (port de Hatari m68000.c:DMA_MaskAddressHigh + fdc.c:FDC_WriteDMAAddress).
// Octet haut limité selon la RAM (≤4 Mo → 0x3f, ≤8 Mo → 0x7f, >8 Mo → 0xff) ; bit0 du
// bas forcé à 0 (alignement mot). move.b #$ff,$ff8609 → relu $3f ; move.b #$ff,$ff860d → $fe.
namespace {
uint32_t dmaMaskAddressHigh(std::size_t ramBytes) {
    const std::size_t kb = ramBytes / 1024;
    if (kb > 8u * 1024u) return 0xffu;
    if (kb > 4u * 1024u) return 0x7fu;
    return 0x3fu;
}
uint32_t dmaAddressMask(std::size_t ramBytes) {
    return 0xff00fffeu | (dmaMaskAddressHigh(ramBytes) << 16);
}
} // namespace

// --- Bits du registre de statut WD1772 (cf. Hatari fdc.h) -------------------
//   type I  : INDEX(2) TR00(4) CRC(8) SEEKERR/RNF(10) SPINUP(20) WPRT(40) MOTOR(80)
//   type II/III : DRQ(2) LOSTDATA(4) CRC(8) RNF(10) RECTYPE(20) WPRT(40) MOTOR(80)
enum : uint8_t {
    STR_BUSY    = 0x01,
    STR_INDEX   = 0x02,   // type I
    STR_DRQ     = 0x02,   // type II/III
    STR_TR00    = 0x04,   // type I
    STR_LOST    = 0x04,   // type II/III
    STR_CRC     = 0x08,
    STR_RNF     = 0x10,
    STR_SPINUP  = 0x20,   // type I
    STR_RECTYPE = 0x20,   // type II/III
    STR_WPRT    = 0x40,
    STR_MOTOR   = 0x80,
};

// --- Bits optionnels du registre de commande --------------------------------
enum : uint8_t {
    CMD_BIT_VERIFY      = 0x04,   // type I : vérif piste après seek
    CMD_BIT_HEADLOAD    = 0x04,   // type II/III : délai de chargement de tête
    CMD_BIT_SPINUP      = 0x08,   // 1 = désactive le spin-up
    CMD_BIT_UPDATETRACK = 0x10,   // type I STEP : met à jour TR
    CMD_BIT_MULTI       = 0x10,   // type II : lecture/écriture multi-secteurs
};

// --- Condition d'un Force Interrupt (type IV) -------------------------------
enum : uint8_t { INT_COND_IP = 0x04, INT_COND_IMMEDIATE = 0x08 };

// --- Sources d'IRQ (cf. Hatari FDC_IRQ_SOURCE_*) ----------------------------
enum : uint8_t {
    IRQ_COMPLETE = 1, IRQ_INDEX = 2, IRQ_FORCED = 4, IRQ_HDC = 8, IRQ_OTHER = 16,
};

// --- Codes de retour de la recherche de secteur -----------------------------
enum { RET_OK = 0, RET_NO_DRIVE = -1 };

// --- Identifiants de commande (FDC.Command) ---------------------------------
enum {
    CMD_NULL = 0, CMD_RESTORE, CMD_SEEK, CMD_STEP,
    CMD_READSECTORS, CMD_WRITESECTORS,
    CMD_READADDRESS, CMD_READTRACK, CMD_WRITETRACK, CMD_MOTOR_STOP,
};

// --- Sous-états de la machine à états (FDC.CommandState) ---------------------
enum {
    RUN_NULL = 0,
    // RESTORE
    RUN_RE_SEEK0, RUN_RE_SEEK0_SPINUP, RUN_RE_SEEK0_MOTORON, RUN_RE_SEEK0_LOOP,
    RUN_RE_VERIFY, RUN_RE_VERIFY_HEAD, RUN_RE_VERIFY_NEXT, RUN_RE_VERIFY_CHECK, RUN_RE_COMPLETE,
    // SEEK
    RUN_SE_TOTRACK, RUN_SE_TOTRACK_SPINUP, RUN_SE_TOTRACK_MOTORON,
    RUN_SE_VERIFY, RUN_SE_VERIFY_HEAD, RUN_SE_VERIFY_NEXT, RUN_SE_VERIFY_CHECK, RUN_SE_COMPLETE,
    // STEP
    RUN_ST_ONCE, RUN_ST_ONCE_SPINUP, RUN_ST_ONCE_MOTORON,
    RUN_ST_VERIFY, RUN_ST_VERIFY_HEAD, RUN_ST_VERIFY_NEXT, RUN_ST_VERIFY_CHECK, RUN_ST_COMPLETE,
    // READ SECTOR
    RUN_RS_READDATA, RUN_RS_SPINUP, RUN_RS_HEADLOAD, RUN_RS_MOTORON,
    RUN_RS_NEXT, RUN_RS_CHECK, RUN_RS_TRANSFER_START, RUN_RS_TRANSFER_LOOP,
    RUN_RS_CRC, RUN_RS_MULTI, RUN_RS_RNF, RUN_RS_COMPLETE,
    // WRITE SECTOR
    RUN_WS_WRITEDATA, RUN_WS_SPINUP, RUN_WS_HEADLOAD, RUN_WS_MOTORON,
    RUN_WS_NEXT, RUN_WS_CHECK, RUN_WS_TRANSFER_START, RUN_WS_TRANSFER_LOOP,
    RUN_WS_CRC, RUN_WS_MULTI, RUN_WS_RNF, RUN_WS_COMPLETE,
    // READ ADDRESS
    RUN_RA_READADDRESS, RUN_RA_SPINUP, RUN_RA_HEADLOAD, RUN_RA_MOTORON,
    RUN_RA_NEXT, RUN_RA_TRANSFER_START, RUN_RA_TRANSFER_LOOP, RUN_RA_RNF, RUN_RA_COMPLETE,
    // READ TRACK
    RUN_RT_READTRACK, RUN_RT_SPINUP, RUN_RT_HEADLOAD, RUN_RT_MOTORON,
    RUN_RT_INDEX, RUN_RT_TRANSFER_LOOP, RUN_RT_COMPLETE,
    // WRITE TRACK
    RUN_WT_WRITETRACK, RUN_WT_SPINUP, RUN_WT_HEADLOAD, RUN_WT_MOTORON,
    RUN_WT_INDEX, RUN_WT_TRANSFER_LOOP, RUN_WT_COMPLETE,
    // MOTOR STOP
    RUN_MOTOR_STOP, RUN_MOTOR_STOP_WAIT, RUN_MOTOR_STOP_COMPLETE,
};

// --- Constantes de temps (cycles FDC = cycles CPU ≈ 8,021 MHz sur ST) -------
static constexpr int64_t MFM_BYTE         = 4 * 8 * 8;     // 256 cyc : 4µs/bit × 8 bits × 8 MHz
static constexpr int64_t CYCLES_PER_REV   = 1604249;       // 300 tr/min @ 8,021247 MHz → ~200 ms
static constexpr int64_t INDEX_PULSE_LEN  = 29758;         // 3,71 ms : durée du signal d'index
static constexpr int     IP_SPIN_UP       = 6;             // tours pour atteindre la vitesse
static constexpr int     IP_MOTOR_OFF     = 9;             // tours d'inactivité → moteur off
static constexpr int     IP_ADDRESS_ID    = 5;             // tours max pour trouver un champ ID
static constexpr int     MAX_TRACK        = 90;            // butée physique de la tête
static constexpr int64_t HEAD_LOAD        = 8 * 15000;     // 15 ms (réf. 8 MHz : 8 cyc/µs)
static constexpr int     STEP_RATE_MS[4]  = {6, 12, 2, 3};
static constexpr int     PREPARE_TYPE_I   = 90 * 8;        // ≥ 0,09 ms
static constexpr int     PREPARE_TYPE_II  = 1 * 8;
static constexpr int     PREPARE_TYPE_III = 1 * 8;
static constexpr int     PREPARE_TYPE_IV  = 100 * 8;
static constexpr int     CMD_COMPLETE     = 1 * 8;
static constexpr int     CMD_IMMEDIATE    = 0;
static constexpr int     WAIT_NO_DRIVE    = 50000;         // attente d'un lecteur/disque valide
static constexpr int     REFRESH_INDEX    = 500;           // pas de mise à jour de l'index
static constexpr int     FDC_FAST_FACTOR  = 10;            // « FDC rapide » : délais ÷ ce facteur (cf. Hatari)

// --- Disposition standard d'une piste (gaps), cf. Hatari fdc.h --------------
static constexpr int GAP1  = 60;   // pré-gap piste (0x4e)
static constexpr int GAP2  = 12;   // pré-gap ID secteur (0x00)
static constexpr int GAP3a = 22;   // post-gap ID (0x4e)
static constexpr int GAP3b = 12;   // pré-gap données (0x00)
static constexpr int GAP4  = 40;   // post-gap données (0x4e)
// Secteur brut 512 o (ID + données + gaps) : 614 octets.
static constexpr int RAW_SECTOR_512 = GAP2 + 3 + 1 + 6 + GAP3a + GAP3b + 3 + 1 + 512 + 2 + GAP4;
static constexpr int BYTES_PER_TRACK = 6268;  // piste DD standard
static constexpr uint8_t SECTOR_SIZE_512 = 2; // code « taille » 512 o dans le champ ID

// CRC16 CCITT (poly 0x1021, init 0xFFFF) du WD1772.
static uint16_t crc16(const uint8_t* buf, int n) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; ++i) {
        crc ^= uint16_t(buf[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

// Type de commande d'après les bits hauts du registre CR.
static uint8_t cmdType(uint8_t cr) {
    if (!(cr & 0x80)) return 1;             // type I  : restore/seek/step
    if (!(cr & 0x40)) return 2;             // type II : read/write sector
    if ((cr & 0xf0) != 0xd0) return 3;      // type III: read addr/track, write track
    return 4;                               // type IV : force interrupt
}

// Numéro de secteur logique → offset image (.st : piste, puis face, puis secteur).
static inline uint32_t lsnOffset(int track, int side, int sector, int spt, int sides) {
    const int lsn = (track * sides + side) * spt + (sector - 1);
    return uint32_t(lsn) * 512u;
}

// -----------------------------------------------------------------------------
//  Détection de géométrie (port fidèle de Hatari floppy.c) — le BPB du secteur
//  de boot est souvent FAUX sur les disquettes de jeux/cracks (Xenon 2 : faces=1
//  au lieu de 2 ; Epic : BPB entièrement bidon ; Super Hang-On : 9 spt au lieu
//  de 10). Hatari recoupe le BPB avec la TAILLE RÉELLE de l'image et recalcule
//  spt/faces en cas d'incohérence — sinon les chargements multi-secteurs lisent
//  les mauvais octets (bombes / retry infini).
// -----------------------------------------------------------------------------

// Cf. Hatari Floppy_DoubleCheckFormat (floppy.c:765) : devine faces et spt
// depuis la taille de l'image quand le BPB ne colle pas.
static void doubleCheckFormat(long diskSize, int& sides, int& spt) {
    const int sidesFixed = (diskSize < 500 * 1024) ? 1 : 2;   // >500 Ko → 2 faces
    const long totalSectors = diskSize / 512;

    int sptFixed = -1;
    for (int s = 9; s <= 12 && sptFixed < 0; ++s)             // formats courants :
        for (int t = 80; t <= 84; ++t)                        // 80..84 pistes × 9..12 spt
            if (totalSectors == long(t) * s * sidesFixed) { sptFixed = s; break; }
    if (sptFixed < 0) {
        if (spt >= 5 && spt <= 48)
            sptFixed = spt;                                   // disquettes ED : BPB crédible
        else
            sptFixed = int(totalSectors / 80 / sidesFixed);   // BPB irrécupérable : 80 pistes
    }
    sides = sidesFixed;
    spt   = sptFixed;
}

// Cf. Hatari Floppy_FindDiskDetails (floppy.c:839) : lit le BPB et ne lui fait
// confiance que s'il est cohérent avec la taille de l'image.
static void findDiskDetails(const std::vector<uint8_t>& image, int& spt, int& sides) {
    if (image.size() < 0x1C) return;
    int bpbSpt    = image[0x18] | (image[0x19] << 8);          // secteurs/piste
    int bpbSides  = image[0x1A] | (image[0x1B] << 8);          // faces
    const int bpbTotal = image[0x13] | (image[0x14] << 8);     // secteurs totaux

    if (bpbTotal != int(image.size() / 512) || bpbSides == 0 || bpbSides > 2 ||
        bpbSpt == 0 || bpbSpt > 48)
        doubleCheckFormat(long(image.size()), bpbSides, bpbSpt);

    spt   = bpbSpt;
    sides = bpbSides;
}

// =============================================================================
//  Décodage des formats d'image (.msa, .dim) — inchangé.
// =============================================================================

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

// Détecte/décharge une image .DIM : 32 octets d'en-tête suivis du contenu disque
// BRUT, identique à une .st (cf. Hatari floppies/dim.c DIM_ReadDisk). On valide
// l'en-tête comme Hatari — ID 'BB', offset 0x03 = 0 (non compressée) et offset
// 0x0A = 0 (piste de début 0) — puis on retire les 32 octets.
static bool decodeDim(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out) {
    if (raw.size() < 32 + 512) return false;                  // en-tête + ≥ 1 secteur
    if (raw[0x00] != 0x42 || raw[0x01] != 0x42) return false; // ID 'BB'
    if (raw[0x03] != 0 || raw[0x0A] != 0) return false;       // toutes pistes, début piste 0
    out.assign(raw.begin() + 32, raw.end());
    return true;
}

bool Fdc::loadImage(const std::string& path, int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    const bool wasPresent = dk.present();   // disque déjà monté → échange à chaud
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "[FDC] image introuvable : %s\n", path.c_str()); return false; }
    const std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> raw(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(raw.data()), n);

    // Format STX (Pasti, en-tête « RSY\0 ») : image disque BAS NIVEAU (pistes/secteurs
    // bruts, IDs réels, CRC, bits fuzzy, timing) qui préserve les PROTECTIONS. On la
    // PARSE (StxImage) et le FDC dispatche vers le chemin _STX (champ ID véritable,
    // statut par secteur, fuzzy/timing) au lieu du modèle .ST « secteur = offset ».
    if (raw.size() > 4 && raw[0] == 'R' && raw[1] == 'S' && raw[2] == 'Y' && raw[3] == 0) {
        auto stx = std::make_unique<StxImage>();
        if (!stx->parse(std::move(raw))) {
            std::fprintf(stderr, "[FDC] image STX illisible : %s — non montée.\n", path.c_str());
            return false;
        }
        dk.image.clear();
        dk.imgType = FloppyDisk::IMG_STX;
        dk.sides   = stx->sides();
        const int tps = stx->tracksPerSide();
        dk.stx     = std::move(stx);
        dk.raw     = false;
        dk.path    = path;
        if (sched_) {                            // changement de média à chaud (cf. plus bas)
            dk.transitionPhase    = wasPresent ? FloppyDisk::TRANS_EJECT : FloppyDisk::TRANS_INSERT;
            dk.transitionDeadline = sched_->now() + 4 * 160256;
        }
        dk.writeProtect = false;                 // écritures en overlay mémoire (write sector)
        std::fprintf(stderr, "[FDC] lecteur %c : %s (STX, %d pistes, %d face(s))\n",
                     drive & 1 ? 'B' : 'A', path.c_str(), tps, dk.sides);
        return true;
    }

    // Image .ST/.msa/.dim → on (re)bascule en modèle logique (annule un éventuel STX).
    dk.imgType = FloppyDisk::IMG_ST;
    dk.stx.reset();

    // .msa (compressé) ou .dim (en-tête 32 o) → conversion en .st brut ; sinon image
    // .st telle quelle. .msa/.dim sont marquées NON raw (pas de recopie d'écritures).
    std::vector<uint8_t> conv;
    if (decodeMsa(raw, conv)) {
        dk.image = std::move(conv);
        dk.raw = false;                          // .msa : pas de recopie d'écritures
        std::fprintf(stderr, "[FDC] image .msa décompressée : %s\n", path.c_str());
    } else if (decodeDim(raw, conv)) {
        dk.image = std::move(conv);
        dk.raw = false;                          // .dim : en-tête 32 o à préserver
        std::fprintf(stderr, "[FDC] image .dim (en-tête 32 o retiré) : %s\n", path.c_str());
    } else {
        dk.image = std::move(raw);               // .st brut
        dk.raw = true;
    }

    // Géométrie : BPB recoupé avec la taille réelle de l'image (cf. Hatari
    // Floppy_FindDiskDetails) — le BPB seul est souvent faux sur les cracks.
    {
        int spt = dk.spt, sides = dk.sides;
        findDiskDetails(dk.image, spt, sides);
        if (spt   >= 1 && spt   <= 48) dk.spt   = spt;
        if (sides >= 1 && sides <= 2)  dk.sides = sides;
    }
    dk.path = path;

    // Changement de média à chaud (cf. Hatari Floppy_DriveTransitionSetState) :
    //  - échange à chaud → phase d'ÉJECTION (force WPRT le temps de la fenêtre) ;
    //  - premier montage (boot) → simple INSERTION, qui ne force PAS WPRT.
    if (sched_) {
        dk.transitionPhase    = wasPresent ? FloppyDisk::TRANS_EJECT : FloppyDisk::TRANS_INSERT;
        dk.transitionDeadline = sched_->now() + 4 * 160256;   // ~4 trames PAL
    }

    // Write-protect auto-détecté d'après les permissions du fichier (cf. Hatari
    // floppy.c:Floppy_IsWriteProtected, mode « automatic »). Une .msa/.dim est
    // TOUJOURS protégée (writeBack ne sait pas réencoder le format → dk.raw == false).
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
    const bool wasPresent = dk.present();
    dk.image.clear();
    dk.stx.reset();
    dk.imgType = FloppyDisk::IMG_ST;
    dk.path.clear();
    // Éjection à chaud : on force WPRT pendant la fenêtre de transition (cf. Hatari
    // Floppy_DriveTransitionSetState, STATE_EJECT). Une éjection « à vide » n'arme rien.
    if (wasPresent && sched_) {
        dk.transitionPhase    = FloppyDisk::TRANS_EJECT;
        dk.transitionDeadline = sched_->now() + 4 * 160256;
    }
    std::fprintf(stderr, "[FDC] lecteur %c éjecté\n", drive & 1 ? 'B' : 'A');
}

// =============================================================================
//  Sélection lecteur/face (port A du PSG) et géométrie.
// =============================================================================
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

int Fdc::sidesPerDisk(int drive) const {
    const FloppyDisk& dk = drive_[drive];
    if (dk.imgType == FloppyDisk::IMG_STX && dk.stx) return dk.stx->sides();
    return dk.sides;
}
int Fdc::tracksPerDisk(int drive) const {
    const FloppyDisk& dk = drive_[drive];
    if (dk.imgType == FloppyDisk::IMG_STX && dk.stx) return dk.stx->tracksPerSide();
    if (dk.image.empty() || dk.spt < 1 || dk.sides < 1) return 0;
    return int((dk.image.size() / 512u) / unsigned(dk.spt) / unsigned(dk.sides));
}
int Fdc::bytesPerTrack() const { return BYTES_PER_TRACK; }

// Longueur RÉELLE d'une piste STX (octets MFM) — cf. FDC_GetBytesPerTrack_STX. La
// rotation (cyclesPerRev) en dépend → des protections mesurent la durée du tour.
int Fdc::bytesPerTrackStx(int track, int side) const {
    const FloppyDisk& dk = drive_[driveSel_ < 0 ? 0 : driveSel_];
    if (!dk.stx) return BYTES_PER_TRACK;
    StxImage::Track* t = dk.stx->findTrack(track, side);
    if (!t) return BYTES_PER_TRACK;
    if (t->pTrackImage) return t->trackImageSize;
    if ((t->flags & StxImage::TRACK_FLAG_SECTOR_BLOCK) == 0) return t->mfmSize / 8;  // MFMSize en bits
    return t->mfmSize;
}

// Période d'un tour : constante pour .ST, dérivée de la longueur de piste pour STX.
int64_t Fdc::cyclesPerRev() const {
    if (driveSel_ >= 0 && drive_[driveSel_].imgType == FloppyDisk::IMG_STX) {
        const int sz = bytesPerTrackStx(drive_[driveSel_].headTrack, side_);
        return int64_t(sz) * MFM_BYTE;                 // densité DD (facteur 1)
    }
    return CYCLES_PER_REV;
}

// Relit lecteur/face du PSG ; au changement de lecteur, réinitialise la référence
// d'index (cf. Hatari FDC_SetDriveSide).
void Fdc::refreshDriveSide() {
    const int nd = selectedDrive();
    const uint8_t ns = uint8_t(currentSide());
    if (nd != driveSel_) {
        indexTime_ = 0;                          // arrête le comptage d'index courant
        driveSel_ = nd;
        if (nd >= 0 && drive_[nd].present() && (str_ & STR_MOTOR)) indexInit();
    }
    side_ = ns;
}

// =============================================================================
//  Modèle rotationnel : impulsions d'index.
// =============================================================================
void Fdc::indexInit() {
    // Position initiale « dans le passé » (< 1 tour) déterministe mais variable
    // d'un démarrage à l'autre : reproductible pour le headless byte-exact, comme
    // Hatari_rand() côté Hatari (FDC_IndexPulse_Init).
    const int64_t off = int64_t(rngNext() % uint32_t(cyclesPerRev()));
    int64_t t = nowCyc() - off;
    if (t <= 0) t = 1;
    indexTime_ = t;
}

void Fdc::indexCheckUpdate() {
    if (!(str_ & STR_MOTOR)) return;                       // moteur arrêté
    if (driveSel_ < 0 || !drive_[driveSel_].present()) return;   // pas de lecteur/disque
    if (indexTime_ == 0) indexInit();
    const int64_t rev = cyclesPerRev();
    while (nowCyc() - indexTime_ >= rev)
        indexIncrease(indexTime_ + rev);
}

void Fdc::indexIncrease(int64_t ipTime) {
    indexCounter_++;
    indexTime_ = ipTime;
    emitSound(FdcSound::Index);
    if (interruptCond_ & INT_COND_IP) fdcSetIrq(IRQ_INDEX);  // Force int on Index Pulse
}

int Fdc::indexCurrentPosBytes() const {
    if (driveSel_ < 0 || indexTime_ == 0) return -1;
    const int64_t since = nowCyc() - indexTime_;
    return int(since / MFM_BYTE);                          // densité DD (facteur 1)
}

// Position tête depuis l'index en CYCLES FDC (STX : BitPosition est en bits/cycles).
int Fdc::indexCurrentPosCycles() const {
    if (driveSel_ < 0 || indexTime_ == 0) return -1;
    return int(nowCyc() - indexTime_);
}

bool Fdc::indexState() const {
    if (driveSel_ < 0 || indexTime_ == 0) return false;
    const int64_t since = nowCyc() - indexTime_;
    return since >= 0 && since < INDEX_PULSE_LEN;
}

int64_t Fdc::nextIndexCycles() const {
    if (driveSel_ < 0 || indexTime_ == 0) return -1;
    const int64_t rev = cyclesPerRev();
    int64_t res = rev - (nowCyc() - indexTime_);
    if (res <= 1) res = rev;
    return res;
}

// « FDC rapide » (cf. Hatari FDC_StartTimer_FdcCycles) : divise le délai de
// COMMANDE/TRANSFERT par FDC_FAST_FACTOR (sauf les délais < ce facteur, pour ne pas
// les annuler). N'est JAMAIS appliqué aux délais cadencés sur la rotation (spin-up,
// arrêt moteur, attente d'index) : ceux-là gardent leur durée réelle, comme Hatari.
int Fdc::applyFastFdc(int fdcCycles) const {
    if (fastFloppy_ && fdcCycles > FDC_FAST_FACTOR) return fdcCycles / FDC_FAST_FACTOR;
    return fdcCycles;
}

// Attente de la prochaine impulsion d'index (spin-up, arrêt moteur). Si un média est
// présent, on saute directement à l'impulsion (délai cadencé sur la rotation, NON
// accéléré par le FDC rapide) ; sinon on sonde périodiquement (l'index n'arrivera que
// quand un disque sera là — ce délai-là, lui, est accéléré).
int Fdc::spinWaitDelay() {
    const int64_t ni = nextIndexCycles();
    if (ni > 0) { delayIndexPaced_ = true; return int(ni); }
    return REFRESH_INDEX;
}

// =============================================================================
//  IRQ / INTRQ (câblée sur GPIP5 + canal 7 du MFP).
// =============================================================================
void Fdc::setIntrqLine(bool on) {
    mfp_.setFdcLine(on);                 // ligne GPIP5 (polling, ex. EmuTOS)
    if (on) mfp_.raise(Mfp::SRC_FDC);    // ET interruption canal 7 (jeux qui l'utilisent)
}

void Fdc::fdcSetIrq(uint8_t source) {
    const bool was = irqSignal_ != 0;
    if (source == IRQ_HDC)        irqSignal_ = IRQ_HDC;
    else if (source == IRQ_OTHER) irqSignal_ = IRQ_OTHER;
    else { irqSignal_ &= ~(IRQ_HDC | IRQ_OTHER); irqSignal_ |= source; }
    if (!was) setIntrqLine(true);
}

void Fdc::fdcClearIrq() {
    if (!(irqSignal_ & IRQ_FORCED)) {     // pas d'IRQ forcée → on efface
        irqSignal_ = 0;
        setIntrqLine(false);
    } else {
        irqSignal_ &= IRQ_FORCED;         // IRQ forcée : reste haute
    }
}

// =============================================================================
//  Tampon FDC↔DMA et FIFO 16 octets.
// =============================================================================
void Fdc::fifoPush(uint8_t b) {
    ff8604recent_ = uint16_t((ff8604recent_ & 0xff00) | b);
    if (dmaSectorCount_ == 0) { dmaError_ = true; return; }   // DMA off → octet perdu
    dmaError_ = false;
    fifo_[fifoSize_++] = b;
    if (fifoSize_ < 16) return;                                // FIFO pas encore pleine
    bus_.megaSteCacheFlushIfEnabled();   // DMA via BGACK → cache Mega STE invalidé
    for (int j = 0; j < 16; ++j)                               // flush 16 o → RAM
        bus_.dmaWrite8(dmaAddr_ + uint32_t(j), fifo_[j]);      // via le plan mémoire (MMU)
    dmaAddr_ = (dmaAddr_ + 16) & dmaAddressMask(bus_.ram.size());
    fifoSize_ = 0;
    ff8604recent_ = uint16_t((fifo_[14] << 8) | fifo_[15]);
    dmaBytesInSector_ -= 16;
    if (dmaBytesInSector_ <= 0) { dmaSectorCount_--; dmaBytesInSector_ = 512; }
}

uint8_t Fdc::fifoPull() {
    if (dmaSectorCount_ == 0) { dmaError_ = true; return 0; }  // DMA off → '0'
    dmaError_ = false;
    uint8_t b;
    if (fifoSize_ > 0) {
        b = fifo_[16 - (fifoSize_--)];                        // octet en position 0,1,..,15
    } else {
        bus_.megaSteCacheFlushIfEnabled();   // DMA via BGACK → cache Mega STE invalidé
        for (int j = 0; j < 16; ++j)                          // recharge 16 o ← RAM
            fifo_[j] = bus_.dmaRead8(dmaAddr_ + uint32_t(j)); // via le plan mémoire (MMU)
        dmaAddr_ = (dmaAddr_ + 16) & dmaAddressMask(bus_.ram.size());
        fifoSize_ = 15;
        ff8604recent_ = uint16_t((fifo_[14] << 8) | fifo_[15]);
        dmaBytesInSector_ -= 16;
        if (dmaBytesInSector_ < 0) { dmaSectorCount_--; dmaBytesInSector_ = 512; }
        b = fifo_[0];
    }
    ff8604recent_ = uint16_t((ff8604recent_ & 0xff00) | b);
    return b;
}

void Fdc::dmaResetFifo() {
    fifoSize_ = 0;
    dmaBytesInSector_ = 512;
    dmaSectorCount_ = 0;            // après reset, compteur = 0 (vérifié sur STF réel)
    dmaError_ = false;
    acsiByteCount_ = 0;            // réinitialise aussi l'état de commande ACSI
}

uint16_t Fdc::dmaStatusWord() const {
    uint16_t s = 0;
    if (!dmaError_)            s |= 0x1;   // bit0 = pas d'erreur
    if (dmaSectorCount_ != 0) s |= 0x2;   // bit1 = compteur de secteurs ≠ 0
    // Bits 3-15 = dernier accès $FF8604 (vérifié sur STF réel, cf. Hatari FDC_DmaStatus_ReadWord).
    return uint16_t(s | (ff8604recent_ & 0xfff8));
}

// =============================================================================
//  Accès « bas niveau » à l'image .ST (cf. Hatari FDC_*_ST).
// =============================================================================
uint8_t Fdc::readSectorST(uint8_t track, uint8_t sector, uint8_t side, int* pSize) {
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return readSectorStx(pSize);
    FloppyDisk& dk = drive_[driveSel_];
    const uint32_t off = lsnOffset(track, side, sector, dk.spt, dk.sides);
    if (off + 512u <= dk.image.size()) {
        for (int i = 0; i < 512; ++i) bufferAdd(dk.image[off + i]);
        *pSize = 512;
        return 0;
    }
    return STR_RNF;
}

uint8_t Fdc::writeSectorST(uint8_t track, uint8_t sector, uint8_t side, int size) {
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return writeSectorStx(size);
    FloppyDisk& dk = drive_[driveSel_];
    const uint32_t off = lsnOffset(track, side, sector, dk.spt, dk.sides);
    if (off + uint32_t(size) <= dk.image.size()) {
        for (int i = 0; i < size; ++i) dk.image[off + i] = bufferReadBytePos(i);
        writeBack(dk, off, uint32_t(size));   // Flopwr : recopie dans le .st
        return 0;
    }
    return STR_RNF;
}

// Champ ID synthétisé (les .ST n'en ont pas) : 3×A1, FE, TR, SIDE, SR, SIZE, CRC.
// On ajoute au tampon les 6 octets utiles [TR..CRC2] (cf. FDC_ReadAddress_ST).
uint8_t Fdc::readAddressST(uint8_t track, uint8_t sector, uint8_t side) {
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return readAddressStx();
    if (track >= tracksPerDisk(driveSel_)) return STR_RNF;
    uint8_t id[10] = { 0xa1, 0xa1, 0xa1, 0xfe, track, side, sector, SECTOR_SIZE_512, 0, 0 };
    const uint16_t crc = crc16(id, 8);
    id[8] = uint8_t(crc >> 8); id[9] = uint8_t(crc);
    for (int i = 4; i < 10; ++i) bufferAdd(id[i]);
    return 0;
}

// Piste complète synthétisée (gaps, sync, IDAM, données, CRC) — cf. FDC_ReadTrack_ST.
uint8_t Fdc::readTrackST(uint8_t track, uint8_t side) {
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return readTrackStx(track, side);
    FloppyDisk& dk = drive_[driveSel_];
    if (track >= tracksPerDisk(driveSel_)) {            // piste inexistante → bruit
        for (int i = 0; i < bytesPerTrack(); ++i) bufferAdd(uint8_t(rngNext()));
        return 0;
    }
    for (int i = 0; i < GAP1; ++i) bufferAdd(0x4e);     // GAP1
    for (int sec = 1; sec <= dk.spt; ++sec) {
        for (int i = 0; i < GAP2; ++i) bufferAdd(0x00); // GAP2
        uint8_t id[10] = { 0xa1, 0xa1, 0xa1, 0xfe, track, side, uint8_t(sec), SECTOR_SIZE_512, 0, 0 };
        uint16_t crc = crc16(id, 8);
        id[8] = uint8_t(crc >> 8); id[9] = uint8_t(crc);
        for (int i = 0; i < 10; ++i) bufferAdd(id[i]);  // champ ID
        for (int i = 0; i < GAP3a; ++i) bufferAdd(0x4e);
        for (int i = 0; i < GAP3b; ++i) bufferAdd(0x00);
        uint8_t dam[4] = { 0xa1, 0xa1, 0xa1, 0xfb };    // DAM (3×A1 + FB)
        for (int i = 0; i < 4; ++i) bufferAdd(dam[i]);
        uint8_t crcbuf[4 + 512];
        std::memcpy(crcbuf, dam, 4);
        const uint32_t off = lsnOffset(track, side, sec, dk.spt, dk.sides);
        for (int i = 0; i < 512; ++i) {
            const uint8_t v = (off + uint32_t(i) < dk.image.size()) ? dk.image[off + i] : 0;
            bufferAdd(v);
            crcbuf[4 + i] = v;
        }
        crc = crc16(crcbuf, 4 + 512);
        bufferAdd(uint8_t(crc >> 8)); bufferAdd(uint8_t(crc));
        for (int i = 0; i < GAP4; ++i) bufferAdd(0x4e);
    }
    while (bufferSize() < bytesPerTrack()) bufferAdd(0x4e); // GAP5
    return 0;
}

// WRITE TRACK : la DMA a rempli le tampon avec l'image MFM brute d'une piste. On la
// PARCOURT pour extraire les secteurs (IDAM $FE → piste/face/secteur, puis DAM $FB/$F8
// → 512 o) et on les écrit dans l'image .ST. (Hatari renvoie « lost data » sur .ST ;
// on fait mieux, en best-effort.)
uint8_t Fdc::writeTrackBuffer() {
    if (driveSel_ < 0 || !drive_[driveSel_].present()) return STR_LOST;
    FloppyDisk& dk = drive_[driveSel_];
    if (dk.imgType == FloppyDisk::IMG_STX) return 0;       // WRITE TRACK sur STX : non géré (rare)
    const int n = bufferSize();
    auto rb = [&](int k) -> uint8_t { return (k >= 0 && k < n) ? buf_[k] : 0; };
    for (int i = 0; i + 6 < n; ) {
        if (rb(i) == 0xFE) {                                   // IDAM : champ d'adresse
            const uint8_t tr = rb(i + 1), sd = rb(i + 2), sec = rb(i + 3);
            int k = i + 5;                                     // cherche la marque de données
            while (k < n && rb(k) != 0xFB && rb(k) != 0xF8) ++k;
            if (k < n && k + 1 + 512 <= n) {
                const uint32_t off = lsnOffset(tr, sd, sec, dk.spt, dk.sides);
                if (off + 512u <= dk.image.size()) {
                    for (int j = 0; j < 512; ++j) dk.image[off + j] = rb(k + 1 + j);
                    writeBack(dk, off, 512u);
                }
                i = k + 1 + 512;
                continue;
            }
        }
        ++i;
    }
    return 0;
}

// Recopie une zone modifiée de l'image en mémoire vers le fichier .st monté.
void Fdc::writeBack(FloppyDisk& dk, uint32_t off, uint32_t len) {
    if (!dk.raw || dk.path.empty() || off + len > dk.image.size()) return;  // .msa : pas de recopie
    std::fstream f(dk.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) return;                  // image en lecture seule / FS virtuel non inscriptible
    f.seekp(off);
    f.write(reinterpret_cast<const char*>(dk.image.data() + off), len);
}

// =============================================================================
//  Chemin STX (Pasti) — port d'extern/hatari/src/floppies/stx.c (FDC_*_STX).
//  Champs ID RÉELS, statut FDC par secteur, bits fuzzy, timing variable. La
//  position angulaire vient de BitPosition (en BITS, 1 bit = 32 cycles FDC).
// =============================================================================
static constexpr int MFM_BIT = 32;   // 4 µs/bit × 8 MHz = 32 cycles FDC (FDC_DELAY_CYCLE_MFM_BIT)

// Latence jusqu'au prochain champ ID + champs ID du secteur trouvé (stxNextSector_).
// Cf. FDC_NextSectorID_FdcCycles_STX.
int Fdc::nextSectorIDStx(int* pFdcCycles) {
    const int curPos = indexCurrentPosCycles();
    if (curPos < 0) return RET_NO_DRIVE;
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t || t->sectorsCount == 0) return RET_NO_DRIVE;

    int i;
    for (i = 0; i < t->sectorsCount; ++i)
        if (curPos < int(t->sectors[i].bitPosition) * MFM_BIT - 4 * int(MFM_BYTE)) break;

    int delay;
    if (i == t->sectorsCount) {                 // après le dernier ID → 1er secteur du tour suivant
        const int trackSize = bytesPerTrackStx(dk.headTrack, side_);
        delay = trackSize * int(MFM_BYTE) - curPos + int(t->sectors[0].bitPosition) * MFM_BIT;
        stxNextSector_ = 0;
    } else {
        delay = int(t->sectors[i].bitPosition) * MFM_BIT - curPos;
        stxNextSector_ = i;
    }

    const StxImage::Sector& sec = t->sectors[stxNextSector_];
    nextID_TR_  = sec.idTrack;
    nextID_SR_  = sec.idSector;
    nextID_LEN_ = sec.idSize;
    // RNF + CRC tous deux posés ⇒ champ ID à CRC erroné.
    nextID_CRCOK_ = ((sec.fdcStatus & StxImage::FLAG_RNF) && (sec.fdcStatus & StxImage::FLAG_CRC)) ? 0 : 1;

    delay -= 4 * int(MFM_BYTE);                  // BitPosition pointe après l'IDAM → reculer aux 3×$A1
    *pFdcCycles = delay;
    return RET_OK;
}

// Lit le secteur stxNextSector_ dans le tampon, octet par octet, avec bits FUZZY
// (aléatoire à chaque lecture) et TIMING variable. Cf. FDC_ReadSector_STX.
uint8_t Fdc::readSectorStx(int* pSize) {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t || stxNextSector_ >= t->sectorsCount) return STR_RNF;
    StxImage::Sector& sec = t->sectors[stxNextSector_];
    if (sec.fdcStatus & StxImage::FLAG_RNF) return STR_RNF;

    *pSize = sec.sectorSize;
    uint32_t readTime = sec.readTime;

    // Secteur réécrit par un 'write sector' → données de l'overlay, timing standard.
    const uint8_t* writeData = nullptr;
    if (sec.saveIndex >= 0 && sec.saveIndex < int(dk.stx->saveSectors.size())) {
        writeData = dk.stx->saveSectors[sec.saveIndex].data.data();
        readTime = 0;
    }
    if (readTime == 0) readTime = 32u * sec.sectorSize;   // µs (valeur standard)
    readTime *= 8;                                        // µs → cycles FDC à 8 MHz

    double totalPrev = 0;
    for (int i = 0; i < sec.sectorSize; ++i) {
        uint8_t byte;
        if (!writeData) {
            byte = sec.pData ? sec.pData[i] : 0;
            if (sec.pFuzzy)                              // bits fuzzy : aléatoire hors masque
                byte = uint8_t((byte & sec.pFuzzy[i]) | (uint8_t(rngNext()) & ~sec.pFuzzy[i]));
        } else {
            byte = writeData[i];
        }

        uint16_t timing;
        if (sec.pTiming && !writeData) {                // timing spécifique par bloc de 16 o
            uint16_t tv = uint16_t((sec.pTiming[(i >> 4) * 2] << 8) + sec.pTiming[(i >> 4) * 2 + 1]);
            tv = uint16_t(tv * 32 + 28);                // 1 unité = 32 cyc + 28 cyc/bloc (Pasti.prg)
            if (i % 16 == 0) totalPrev = 0;
            const double totalCur = (double(tv) * ((i % 16) + 1)) / 16.0;
            timing = uint16_t(std::lround(totalCur - totalPrev));
            totalPrev += timing;
        } else {                                        // timing uniforme sur le secteur
            const double totalCur = (double(readTime) * (i + 1)) / sec.sectorSize;
            timing = uint16_t(std::lround(totalCur - totalPrev));
            totalPrev += timing;
        }
        bufferAddTiming(byte, timing);
    }
    // On ne remonte que les bits CRC (3) et RECORD_TYPE (5) dans le statut.
    return sec.fdcStatus & (StxImage::FLAG_CRC | StxImage::FLAG_RECORD_TYPE);
}

// 'write sector' sur STX : stocke les données dans un overlay EN MÉMOIRE (relues
// ensuite à la place de l'original). Cf. FDC_WriteSector_STX. Perdu à la fermeture.
uint8_t Fdc::writeSectorStx(int size) {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t || stxNextSector_ >= t->sectorsCount) return STR_RNF;
    StxImage::Sector& sec = t->sectors[stxNextSector_];
    if (sec.fdcStatus & StxImage::FLAG_RNF) return STR_RNF;
    if (sec.fdcStatus & StxImage::FLAG_CRC) return STR_CRC;

    if (sec.saveIndex < 0) {
        StxImage::SaveSector ss;
        ss.track = uint8_t(dk.headTrack); ss.side = side_; ss.bitPos = sec.bitPosition;
        ss.data.resize(size, 0);
        dk.stx->saveSectors.push_back(std::move(ss));
        sec.saveIndex = int(dk.stx->saveSectors.size()) - 1;
    }
    auto& data = dk.stx->saveSectors[sec.saveIndex].data;
    if (int(data.size()) < size) data.resize(size, 0);
    for (int i = 0; i < size; ++i) data[i] = bufferReadBytePos(i);
    return 0;
}

// READ ADDRESS sur STX : renvoie le VRAI champ ID du secteur. Cf. FDC_ReadAddress_STX.
uint8_t Fdc::readAddressStx() {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t || stxNextSector_ >= t->sectorsCount) return STR_RNF;
    const StxImage::Sector& sec = t->sectors[stxNextSector_];
    bufferAdd(sec.idTrack);
    bufferAdd(sec.idHead);
    bufferAdd(sec.idSector);
    bufferAdd(sec.idSize);
    bufferAdd(uint8_t(sec.idCrc >> 8));
    bufferAdd(uint8_t(sec.idCrc));
    if ((sec.fdcStatus & StxImage::FLAG_RNF) && (sec.fdcStatus & StxImage::FLAG_CRC)) return STR_CRC;
    return 0;
}

// READ TRACK sur STX : image MFM brute de la piste si présente, sinon piste standard
// reconstruite à partir des secteurs. Cf. FDC_ReadTrack_STX.
uint8_t Fdc::readTrackStx(int track, int side) {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(track, side);
    if (!t) {                                            // piste absente → bruit
        for (int i = 0; i < bytesPerTrackStx(track, side); ++i) bufferAdd(uint8_t(rngNext()));
        return 0;
    }
    if (t->pTrackImage) {                                // dump MFM complet de la piste
        const double readTime = 8000000.0 / 5.0;        // 1 tour à 300 tr/min
        double totalPrev = 0;
        for (int i = 0; i < t->trackImageSize; ++i) {
            const double totalCur = (readTime * (i + 1)) / t->trackImageSize;
            const uint16_t timing = uint16_t(std::lround(totalCur - totalPrev));
            totalPrev += timing;
            bufferAddTiming(t->pTrackImage[i], timing);
        }
        return 0;
    }
    // Pas d'image → reconstruire une piste standard à partir des secteurs.
    int trackSize = t->mfmSize;
    if ((t->flags & StxImage::TRACK_FLAG_SECTOR_BLOCK) == 0) trackSize /= 8;
    if (t->sectorsCount == 0) {
        for (int i = 0; i < trackSize; ++i) bufferAdd(uint8_t(rngNext()));
        return 0;
    }
    auto crcAdd = [](uint16_t& c, uint8_t b) {
        c ^= uint16_t(b) << 8;
        for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
    };
    for (int i = 0; i < GAP1; ++i) bufferAdd(0x4e);
    for (int s = 0; s < t->sectorsCount; ++s) {
        const StxImage::Sector& sec = t->sectors[s];
        const int ssz = sec.sectorSize;
        if (bufferSize() + ssz + GAP2 + 10 + GAP3a + GAP3b + 4 + 2 + GAP4 >= trackSize) break;
        for (int i = 0; i < GAP2; ++i) bufferAdd(0x00);
        bufferAdd(0xa1); bufferAdd(0xa1); bufferAdd(0xa1); bufferAdd(0xfe);
        bufferAdd(sec.idTrack); bufferAdd(sec.idHead); bufferAdd(sec.idSector); bufferAdd(sec.idSize);
        bufferAdd(uint8_t(sec.idCrc >> 8)); bufferAdd(uint8_t(sec.idCrc));
        for (int i = 0; i < GAP3a; ++i) bufferAdd(0x4e);
        for (int i = 0; i < GAP3b; ++i) bufferAdd(0x00);
        uint16_t crc = 0xFFFF;
        bufferAdd(0xa1); crcAdd(crc, 0xa1); bufferAdd(0xa1); crcAdd(crc, 0xa1); bufferAdd(0xa1); crcAdd(crc, 0xa1);
        bufferAdd(0xfb); crcAdd(crc, 0xfb);
        const uint8_t* pData = sec.pData;
        if (sec.saveIndex >= 0 && sec.saveIndex < int(dk.stx->saveSectors.size()))
            pData = dk.stx->saveSectors[sec.saveIndex].data.data();
        for (int i = 0; i < ssz; ++i) { const uint8_t b = pData ? pData[i] : 0; bufferAdd(b); crcAdd(crc, b); }
        bufferAdd(uint8_t(crc >> 8)); bufferAdd(uint8_t(crc));
        for (int i = 0; i < GAP4; ++i) bufferAdd(0x4e);
    }
    while (bufferSize() < trackSize) bufferAdd(0x4e);
    return 0;
}

// =============================================================================
//  Recherche du prochain champ ID (latence rotationnelle), cf. FDC_NextSectorID_ST.
// =============================================================================
int Fdc::nextSectorID(int* pFdcCycles) {
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return nextSectorIDStx(pFdcCycles);
    const int curPos = indexCurrentPosBytes();
    if (curPos < 0) return RET_NO_DRIVE;                         // pas de lecteur/disque
    FloppyDisk& dk = drive_[driveSel_];
    if (side_ == 1 && dk.sides == 1) return RET_NO_DRIVE;        // face 1 sur disque simple face
    if (dk.headTrack >= tracksPerDisk(driveSel_)) return RET_NO_DRIVE;  // piste inexistante

    const int maxSector = dk.spt;
    int trackPos = GAP1 + GAP2;                                  // position du 1er champ ID
    int i;
    for (i = 0; i < maxSector; ++i) {
        if (curPos < trackPos) break;                           // prochain secteur trouvé
        trackPos += RAW_SECTOR_512;
    }
    int nbBytes, nextSector;
    if (i == maxSector) {                                        // après le dernier ID → tour suivant
        nbBytes = bytesPerTrack() - curPos + GAP1 + GAP2;
        nextSector = 1;
    } else {
        nbBytes = trackPos - curPos;
        nextSector = i + 1;
    }
    nextID_TR_    = uint8_t(dk.headTrack);
    nextID_SR_    = uint8_t(nextSector);
    nextID_LEN_   = SECTOR_SIZE_512;
    nextID_CRCOK_ = 1;                                           // CRC toujours bon pour .ST
    *pFdcCycles = int(int64_t(nbBytes) * MFM_BYTE);
    return RET_OK;
}

// =============================================================================
//  Moteur, fin de commande, vérification de piste.
// =============================================================================
bool Fdc::setMotorOn(uint8_t cr) {
    bool spinUp;
    if (!(cr & CMD_BIT_SPINUP) && !(str_ & STR_MOTOR)) {        // spin-up demandé, moteur arrêté
        updateStr(STR_SPINUP, 0);                              // efface le bit spin-up
        indexCounter_ = 0;                                     // compteur de la séquence de spin-up
        spinUp = true;
    } else {
        spinUp = false;
    }
    const bool wasOff = !(str_ & STR_MOTOR);
    updateStr(0, STR_MOTOR);                                    // démarre le moteur
    if (wasOff) emitSound(FdcSound::MotorOn);
    if (driveSel_ >= 0 && drive_[driveSel_].present()) {
        if (indexTime_ == 0) indexInit();                      // position d'index aléatoire au démarrage
    }
    return spinUp;
}

int Fdc::cmdComplete(bool doInt) {
    updateStr(STR_BUSY, 0);                                     // BUSY tombe
    if (doInt) fdcSetIrq(IRQ_COMPLETE);
    command_ = CMD_MOTOR_STOP;                                  // fausse commande : arrêt du moteur
    commandState_ = RUN_MOTOR_STOP;
    return CMD_IMMEDIATE;
}

// Vérif piste type I : pour .ST la piste est toujours correcte, sauf face absente.
bool Fdc::verifyTrack() {
    if (driveSel_ < 0 || !drive_[driveSel_].present()) return false;
    if (nextID_TR_ != tr_ || nextID_CRCOK_ == 0) return false;
    if (side_ == 1 && drive_[driveSel_].sides != 2) return false;
    return true;
}

int Fdc::updateMotorStop() {
    int fdcCycles = 0;
    switch (commandState_) {
     case RUN_MOTOR_STOP:
        indexCounter_ = 0;
        commandState_ = RUN_MOTOR_STOP_WAIT;
        [[fallthrough]];
     case RUN_MOTOR_STOP_WAIT:
        if (indexCounter_ < IP_MOTOR_OFF) {                    // attend 9 tours d'inactivité
            fdcCycles = spinWaitDelay();
            break;
        }
        [[fallthrough]];
     case RUN_MOTOR_STOP_COMPLETE:
        indexCounter_ = 0;
        indexTime_ = 0;                                        // arrête le comptage d'index
        updateStr(STR_MOTOR, 0);                               // coupe le moteur (garde le bit spin-up)
        emitSound(FdcSound::MotorOff);
        command_ = CMD_NULL;                                   // dernier état : FDC inactif
        fdcCycles = 0;
        break;
    }
    return fdcCycles;
}

// =============================================================================
//  Machine à états : commandes type I (RESTORE / SEEK / STEP).
// =============================================================================
int Fdc::updateRestore() {
    int fdcCycles = 0;
    switch (commandState_) {
     case RUN_RE_SEEK0:
        if (setMotorOn(cr_)) { commandState_ = RUN_RE_SEEK0_SPINUP; fdcCycles = REFRESH_INDEX; }
        else                 { commandState_ = RUN_RE_SEEK0_MOTORON; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_RE_SEEK0_SPINUP:
        if (indexCounter_ < IP_SPIN_UP) { fdcCycles = spinWaitDelay(); break; }
        [[fallthrough]];
     case RUN_RE_SEEK0_MOTORON:
        updateStr(0, STR_SPINUP);
        replaceCommandPossible_ = false;
        tr_ = 0xff;                                            // 255 tentatives max vers la piste 0
        commandState_ = RUN_RE_SEEK0_LOOP;
        [[fallthrough]];
     case RUN_RE_SEEK0_LOOP:
        if (tr_ == 0) {                                        // piste 0 non atteinte après 255 essais
            updateStr(0, STR_RNF);
            updateStr(STR_TR00, 0);
            fdcCycles = cmdComplete(true);
            break;
        }
        if (driveSel_ < 0 || !drive_[driveSel_].present() || drive_[driveSel_].headTrack != 0) {
            updateStr(STR_TR00, 0);
            tr_--;
            if (driveSel_ >= 0 && drive_[driveSel_].present())
                drive_[driveSel_].headTrack--;                // déplace la tête physique
            fdcCycles = STEP_RATE_MS[cr_ & 3] * 1000 * 8;
        } else {                                              // tête sur la piste 0
            updateStr(0, STR_TR00);
            tr_ = 0;
            commandState_ = RUN_RE_VERIFY;
            fdcCycles = CMD_IMMEDIATE;
        }
        break;
     case RUN_RE_VERIFY:
        if (cr_ & CMD_BIT_VERIFY) { commandState_ = RUN_RE_VERIFY_HEAD; fdcCycles = int(HEAD_LOAD); }
        else                      { commandState_ = RUN_RE_COMPLETE;   fdcCycles = CMD_COMPLETE; }
        break;
     case RUN_RE_VERIFY_HEAD:
        indexCounter_ = 0;
        [[fallthrough]];
     case RUN_RE_VERIFY_NEXT:
        if (indexCounter_ >= IP_ADDRESS_ID) {                 // pas de bon champ ID après 5 tours
            updateStr(0, STR_RNF);
            commandState_ = RUN_RE_COMPLETE;
            fdcCycles = CMD_COMPLETE;
            break;
        }
        {
            int c = 0;
            const int res = (driveSel_ < 0) ? RET_NO_DRIVE : nextSectorID(&c);
            if (res == RET_OK) { fdcCycles = c + int(10 * MFM_BYTE); commandState_ = RUN_RE_VERIFY_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
            commandState_ = RUN_RE_VERIFY_NEXT;
        }
        break;
     case RUN_RE_VERIFY_CHECK:
        if (verifyTrack()) { updateStr(STR_RNF, 0); commandState_ = RUN_RE_COMPLETE; fdcCycles = CMD_COMPLETE; }
        else               { commandState_ = RUN_RE_VERIFY_NEXT; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_RE_COMPLETE:
        fdcCycles = cmdComplete(true);
        break;
    }
    return fdcCycles;
}

int Fdc::updateSeek() {
    int fdcCycles = 0;
    switch (commandState_) {
     case RUN_SE_TOTRACK:
        if (setMotorOn(cr_)) { commandState_ = RUN_SE_TOTRACK_SPINUP; fdcCycles = REFRESH_INDEX; }
        else                 { commandState_ = RUN_SE_TOTRACK_MOTORON; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_SE_TOTRACK_SPINUP:
        if (indexCounter_ < IP_SPIN_UP) { fdcCycles = spinWaitDelay(); break; }
        [[fallthrough]];
     case RUN_SE_TOTRACK_MOTORON:
        updateStr(0, STR_SPINUP);
        replaceCommandPossible_ = false;
        if (tr_ == dr_) { commandState_ = RUN_SE_VERIFY; fdcCycles = CMD_IMMEDIATE; }
        else {
            stepDir_ = (dr_ < tr_) ? -1 : 1;
            tr_ = uint8_t(tr_ + stepDir_);
            fdcCycles = STEP_RATE_MS[cr_ & 3] * 1000 * 8;
            updateStr(STR_TR00, 0);
            if (driveSel_ >= 0 && drive_[driveSel_].present()) {
                int& ht = drive_[driveSel_].headTrack;
                if (ht == MAX_TRACK && stepDir_ == 1) {       // au-delà de la piste max
                    commandState_ = RUN_SE_VERIFY; fdcCycles = CMD_IMMEDIATE;
                } else if (ht == 0 && stepDir_ == -1) {       // butée piste 0
                    tr_ = 0; commandState_ = RUN_SE_VERIFY; fdcCycles = CMD_IMMEDIATE;
                } else {
                    ht += stepDir_;                           // déplace la tête physique
                }
                if (ht == 0) updateStr(0, STR_TR00);
            }
        }
        break;
     case RUN_SE_VERIFY:
        if (cr_ & CMD_BIT_VERIFY) { commandState_ = RUN_SE_VERIFY_HEAD; fdcCycles = int(HEAD_LOAD); }
        else                      { commandState_ = RUN_SE_COMPLETE;   fdcCycles = CMD_COMPLETE; }
        break;
     case RUN_SE_VERIFY_HEAD:
        indexCounter_ = 0;
        [[fallthrough]];
     case RUN_SE_VERIFY_NEXT:
        if (indexCounter_ >= IP_ADDRESS_ID) {
            updateStr(0, STR_RNF);
            commandState_ = RUN_SE_COMPLETE;
            fdcCycles = CMD_COMPLETE;
            break;
        }
        {
            int c = 0;
            const int res = (driveSel_ < 0) ? RET_NO_DRIVE : nextSectorID(&c);
            if (res == RET_OK) { fdcCycles = c + int(10 * MFM_BYTE); commandState_ = RUN_SE_VERIFY_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
            commandState_ = RUN_SE_VERIFY_NEXT;
        }
        break;
     case RUN_SE_VERIFY_CHECK:
        if (verifyTrack()) { updateStr(STR_RNF, 0); commandState_ = RUN_SE_COMPLETE; fdcCycles = CMD_COMPLETE; }
        else               { commandState_ = RUN_SE_VERIFY_NEXT; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_SE_COMPLETE:
        fdcCycles = cmdComplete(true);
        break;
    }
    return fdcCycles;
}

int Fdc::updateStep() {
    int fdcCycles = 0;
    switch (commandState_) {
     case RUN_ST_ONCE:
        if (setMotorOn(cr_)) { commandState_ = RUN_ST_ONCE_SPINUP; fdcCycles = REFRESH_INDEX; }
        else                 { commandState_ = RUN_ST_ONCE_MOTORON; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_ST_ONCE_SPINUP:
        if (indexCounter_ < IP_SPIN_UP) { fdcCycles = spinWaitDelay(); break; }
        [[fallthrough]];
     case RUN_ST_ONCE_MOTORON:
        updateStr(0, STR_SPINUP);
        replaceCommandPossible_ = false;
        if (cr_ & CMD_BIT_UPDATETRACK) tr_ = uint8_t(tr_ + stepDir_);
        fdcCycles = STEP_RATE_MS[cr_ & 3] * 1000 * 8;
        updateStr(STR_TR00, 0);
        if (driveSel_ >= 0 && drive_[driveSel_].present()) {
            int& ht = drive_[driveSel_].headTrack;
            if (ht == MAX_TRACK && stepDir_ == 1)       fdcCycles = CMD_IMMEDIATE;
            else if (ht == 0 && stepDir_ == -1)         fdcCycles = CMD_IMMEDIATE;
            else                                        ht += stepDir_;
            if (ht == 0) updateStr(0, STR_TR00);
        }
        commandState_ = RUN_ST_VERIFY;
        break;
     case RUN_ST_VERIFY:
        if (cr_ & CMD_BIT_VERIFY) { commandState_ = RUN_ST_VERIFY_HEAD; fdcCycles = int(HEAD_LOAD); }
        else                      { commandState_ = RUN_ST_COMPLETE;   fdcCycles = CMD_COMPLETE; }
        break;
     case RUN_ST_VERIFY_HEAD:
        indexCounter_ = 0;
        [[fallthrough]];
     case RUN_ST_VERIFY_NEXT:
        if (indexCounter_ >= IP_ADDRESS_ID) {
            updateStr(0, STR_RNF);
            commandState_ = RUN_ST_COMPLETE;
            fdcCycles = CMD_COMPLETE;
            break;
        }
        {
            int c = 0;
            const int res = (driveSel_ < 0) ? RET_NO_DRIVE : nextSectorID(&c);
            if (res == RET_OK) { fdcCycles = c + int(10 * MFM_BYTE); commandState_ = RUN_ST_VERIFY_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
            commandState_ = RUN_ST_VERIFY_NEXT;
        }
        break;
     case RUN_ST_VERIFY_CHECK:
        if (verifyTrack()) { updateStr(STR_RNF, 0); commandState_ = RUN_ST_COMPLETE; fdcCycles = CMD_COMPLETE; }
        else               { commandState_ = RUN_ST_VERIFY_NEXT; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_ST_COMPLETE:
        fdcCycles = cmdComplete(true);
        break;
    }
    return fdcCycles;
}

// =============================================================================
//  Machine à états : commandes type II (READ/WRITE SECTOR).
// =============================================================================
int Fdc::updateReadSectors() {
    int fdcCycles = 0;
    switch (commandState_) {
     case RUN_RS_READDATA:
        if (setMotorOn(cr_)) { commandState_ = RUN_RS_SPINUP; fdcCycles = REFRESH_INDEX; }
        else                 { commandState_ = RUN_RS_HEADLOAD; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_RS_SPINUP:
        if (indexCounter_ < IP_SPIN_UP) { fdcCycles = spinWaitDelay(); break; }
        [[fallthrough]];
     case RUN_RS_HEADLOAD:
        if (cr_ & CMD_BIT_HEADLOAD) { commandState_ = RUN_RS_MOTORON; fdcCycles = int(HEAD_LOAD); break; }
        [[fallthrough]];
     case RUN_RS_MOTORON:
        replaceCommandPossible_ = false;
        indexCounter_ = 0;
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        commandState_ = RUN_RS_NEXT;
        fdcCycles = CMD_IMMEDIATE;
        break;
     case RUN_RS_NEXT:
        if (indexCounter_ >= IP_ADDRESS_ID) { commandState_ = RUN_RS_RNF; fdcCycles = CMD_IMMEDIATE; break; }
        {
            int c = 0;
            const int res = (driveSel_ < 0) ? RET_NO_DRIVE : nextSectorID(&c);
            if (res == RET_OK) { fdcCycles = c + int(10 * MFM_BYTE); commandState_ = RUN_RS_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
        }
        break;
     case RUN_RS_CHECK:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        if (nextID_TR_ == tr_ && nextID_SR_ == sr_ && nextID_CRCOK_) {
            commandState_ = RUN_RS_TRANSFER_START;
            fdcCycles = int(int64_t(GAP3a + GAP3b + 3 + 1) * MFM_BYTE);  // jusqu'aux données
        } else {
            commandState_ = RUN_RS_NEXT; fdcCycles = CMD_IMMEDIATE;
        }
        break;
     case RUN_RS_TRANSFER_START:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        {
            bufferReset();
            int size = 0;
            statusTemp_ = readSectorST(uint8_t(drive_[driveSel_].headTrack), sr_, side_, &size);
            if (statusTemp_ & STR_RNF) { commandState_ = RUN_RS_RNF; fdcCycles = CMD_IMMEDIATE; }
            else {
                // Type d'enregistrement (« deleted data ») depuis le statut du secteur
                // (toujours 0 pour .ST, possiblement posé pour STX).
                if (statusTemp_ & STR_RECTYPE) updateStr(0, STR_RECTYPE);
                else                            updateStr(STR_RECTYPE, 0);
                commandState_ = RUN_RS_TRANSFER_LOOP;
                fdcCycles = int(bufferReadTiming());          // délai du 1er octet (timing STX variable)
            }
        }
        break;
     case RUN_RS_TRANSFER_LOOP:
        fifoPush(bufferReadByte());                           // 1 octet → FIFO DMA
        if (bufPos_ < bufferSize()) fdcCycles = int(bufferReadTiming());
        else { commandState_ = RUN_RS_CRC; fdcCycles = int(2 * MFM_BYTE); }  // 2 octets de CRC
        break;
     case RUN_RS_CRC:
        // CRC toujours bon pour .ST ; pour STX, une ERREUR CRC volontaire (protection)
        // est remontée dans le statut et termine la commande (cf. Hatari READSECTORS_CRC).
        if (statusTemp_ & STR_CRC) { updateStr(0, STR_CRC); fdcCycles = cmdComplete(true); }
        else                       { commandState_ = RUN_RS_MULTI; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_RS_MULTI:
        if (cr_ & CMD_BIT_MULTI) {                            // multi-secteurs : secteur suivant
            sr_++; indexCounter_ = 0;
            commandState_ = RUN_RS_NEXT; fdcCycles = CMD_IMMEDIATE;
        } else {
            commandState_ = RUN_RS_COMPLETE; fdcCycles = CMD_COMPLETE;
        }
        break;
     case RUN_RS_RNF:
        updateStr(0, STR_RNF); fdcCycles = cmdComplete(true); break;
     case RUN_RS_COMPLETE:
        fdcCycles = cmdComplete(true); break;
    }
    return fdcCycles;
}

int Fdc::updateWriteSectors() {
    int fdcCycles = 0;
    // Disquette protégée → on s'arrête tout de suite (cf. Hatari, contrôle en tête).
    if (driveSel_ >= 0 && drive_[driveSel_].present() && drive_[driveSel_].writeProtect) {
        updateStr(0, STR_WPRT);
        fdcCycles = cmdComplete(true);
    } else {
        updateStr(STR_WPRT, 0);
    }
    switch (commandState_) {
     case RUN_WS_WRITEDATA:
        if (setMotorOn(cr_)) { commandState_ = RUN_WS_SPINUP; fdcCycles = REFRESH_INDEX; }
        else                 { commandState_ = RUN_WS_HEADLOAD; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_WS_SPINUP:
        if (indexCounter_ < IP_SPIN_UP) { fdcCycles = spinWaitDelay(); break; }
        [[fallthrough]];
     case RUN_WS_HEADLOAD:
        if (cr_ & CMD_BIT_HEADLOAD) { commandState_ = RUN_WS_MOTORON; fdcCycles = int(HEAD_LOAD); break; }
        [[fallthrough]];
     case RUN_WS_MOTORON:
        replaceCommandPossible_ = false;
        indexCounter_ = 0;
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        commandState_ = RUN_WS_NEXT;
        fdcCycles = CMD_IMMEDIATE;
        break;
     case RUN_WS_NEXT:
        if (indexCounter_ >= IP_ADDRESS_ID) { commandState_ = RUN_WS_RNF; fdcCycles = CMD_IMMEDIATE; break; }
        {
            int c = 0;
            const int res = (driveSel_ < 0) ? RET_NO_DRIVE : nextSectorID(&c);
            if (res == RET_OK) { fdcCycles = c + int(10 * MFM_BYTE); commandState_ = RUN_WS_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
        }
        break;
     case RUN_WS_CHECK:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        if (nextID_TR_ == tr_ && nextID_SR_ == sr_ && nextID_CRCOK_) {
            commandState_ = RUN_WS_TRANSFER_START;
            fdcCycles = int(int64_t(GAP3a + GAP3b + 3 + 1) * MFM_BYTE);
        } else {
            commandState_ = RUN_WS_NEXT; fdcCycles = CMD_IMMEDIATE;
        }
        break;
     case RUN_WS_TRANSFER_START:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        bufferReset();
        dmaBytesToTransfer_ = 128 << (nextID_LEN_ & 3);       // 512 o pour .ST
        commandState_ = RUN_WS_TRANSFER_LOOP;
        fdcCycles = CMD_IMMEDIATE;
        break;
     case RUN_WS_TRANSFER_LOOP:
        if (dmaBytesToTransfer_-- > 0) {
            bufferAdd(fifoPull());                            // 1 octet ← FIFO DMA
            fdcCycles = int(MFM_BYTE);
        } else {
            commandState_ = RUN_WS_CRC; fdcCycles = int(2 * MFM_BYTE);
        }
        break;
     case RUN_WS_CRC:
        {
            const uint8_t st = writeSectorST(uint8_t(drive_[driveSel_].headTrack), sr_, side_, bufferSize());
            if (st & STR_RNF) { commandState_ = RUN_WS_RNF; fdcCycles = CMD_IMMEDIATE; }
            else              { commandState_ = RUN_WS_MULTI; fdcCycles = CMD_IMMEDIATE; }
        }
        break;
     case RUN_WS_MULTI:
        if (cr_ & CMD_BIT_MULTI) {
            sr_++;
            commandState_ = RUN_WS_MOTORON; fdcCycles = CMD_IMMEDIATE;
        } else {
            commandState_ = RUN_WS_COMPLETE; fdcCycles = CMD_COMPLETE;
        }
        break;
     case RUN_WS_RNF:
        updateStr(0, STR_RNF); fdcCycles = cmdComplete(true); break;
     case RUN_WS_COMPLETE:
        fdcCycles = cmdComplete(true); break;
    }
    return fdcCycles;
}

// =============================================================================
//  Machine à états : commandes type III (READ ADDRESS / READ TRACK / WRITE TRACK).
// =============================================================================
int Fdc::updateReadAddress() {
    int fdcCycles = 0;
    switch (commandState_) {
     case RUN_RA_READADDRESS:
        if (setMotorOn(cr_)) { commandState_ = RUN_RA_SPINUP; fdcCycles = REFRESH_INDEX; }
        else                 { commandState_ = RUN_RA_HEADLOAD; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_RA_SPINUP:
        if (indexCounter_ < IP_SPIN_UP) { fdcCycles = spinWaitDelay(); break; }
        [[fallthrough]];
     case RUN_RA_HEADLOAD:
        replaceCommandPossible_ = false;
        if (cr_ & CMD_BIT_HEADLOAD) { commandState_ = RUN_RA_MOTORON; fdcCycles = int(HEAD_LOAD); break; }
        [[fallthrough]];
     case RUN_RA_MOTORON:
        replaceCommandPossible_ = false;
        indexCounter_ = 0;
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        commandState_ = RUN_RA_NEXT;
        fdcCycles = CMD_IMMEDIATE;
        break;
     case RUN_RA_NEXT:
        if (indexCounter_ >= IP_ADDRESS_ID) { commandState_ = RUN_RA_RNF; fdcCycles = CMD_IMMEDIATE; break; }
        {
            int c = 0;
            const int res = (driveSel_ < 0) ? RET_NO_DRIVE : nextSectorID(&c);
            if (res == RET_OK) { fdcCycles = c + int(4 * MFM_BYTE); commandState_ = RUN_RA_TRANSFER_START; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
        }
        break;
     case RUN_RA_TRANSFER_START:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        bufferReset();
        statusTemp_ = readAddressST(uint8_t(drive_[driveSel_].headTrack), nextID_SR_, side_);
        sr_ = bufferReadBytePos(0);                           // 1er octet du champ ID → registre secteur
        commandState_ = RUN_RA_TRANSFER_LOOP;
        fdcCycles = int(bufferReadTiming());
        break;
     case RUN_RA_TRANSFER_LOOP:
        fifoPush(bufferReadByte());
        if (bufPos_ < bufferSize()) fdcCycles = int(bufferReadTiming());
        else { commandState_ = RUN_RA_COMPLETE; fdcCycles = CMD_COMPLETE; }
        break;
     case RUN_RA_RNF:
        updateStr(0, STR_RNF); fdcCycles = cmdComplete(true); break;
     case RUN_RA_COMPLETE:
        fdcCycles = cmdComplete(true); break;
    }
    return fdcCycles;
}

int Fdc::updateReadTrack() {
    int fdcCycles = 0;
    switch (commandState_) {
     case RUN_RT_READTRACK:
        if (setMotorOn(cr_)) { commandState_ = RUN_RT_SPINUP; fdcCycles = REFRESH_INDEX; }
        else                 { commandState_ = RUN_RT_HEADLOAD; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_RT_SPINUP:
        if (indexCounter_ < IP_SPIN_UP) { fdcCycles = spinWaitDelay(); break; }
        [[fallthrough]];
     case RUN_RT_HEADLOAD:
        replaceCommandPossible_ = false;
        if (cr_ & CMD_BIT_HEADLOAD) { commandState_ = RUN_RT_MOTORON; fdcCycles = int(HEAD_LOAD); break; }
        [[fallthrough]];
     case RUN_RT_MOTORON:
        {
            const int64_t ni = nextIndexCycles();             // attend la prochaine impulsion d'index
            if (ni < 0) { fdcCycles = WAIT_NO_DRIVE; }
            else {
                if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
                commandState_ = RUN_RT_INDEX; fdcCycles = int(ni); delayIndexPaced_ = true;
            }
        }
        break;
     case RUN_RT_INDEX:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        bufferReset();
        if (side_ == 1 && drive_[driveSel_].sides != 2) {     // face inexistante → bruit
            for (int i = 0; i < bytesPerTrack(); ++i) bufferAdd(uint8_t(rngNext()));
        } else {
            statusTemp_ = readTrackST(uint8_t(drive_[driveSel_].headTrack), side_);
        }
        commandState_ = RUN_RT_TRANSFER_LOOP;
        fdcCycles = int(bufferReadTiming());
        break;
     case RUN_RT_TRANSFER_LOOP:
        fifoPush(bufferReadByte());
        if (bufPos_ < bufferSize()) fdcCycles = int(bufferReadTiming());
        else { commandState_ = RUN_RT_COMPLETE; fdcCycles = CMD_COMPLETE; }
        break;
     case RUN_RT_COMPLETE:
        fdcCycles = cmdComplete(true); break;
    }
    return fdcCycles;
}

int Fdc::updateWriteTrack() {
    int fdcCycles = 0;
    switch (commandState_) {
     case RUN_WT_WRITETRACK:
        if (setMotorOn(cr_)) { commandState_ = RUN_WT_SPINUP; fdcCycles = REFRESH_INDEX; }
        else                 { commandState_ = RUN_WT_HEADLOAD; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_WT_SPINUP:
        if (indexCounter_ < IP_SPIN_UP) { fdcCycles = spinWaitDelay(); break; }
        [[fallthrough]];
     case RUN_WT_HEADLOAD:
        replaceCommandPossible_ = false;
        if (cr_ & CMD_BIT_HEADLOAD) { commandState_ = RUN_WT_MOTORON; fdcCycles = int(HEAD_LOAD); break; }
        [[fallthrough]];
     case RUN_WT_MOTORON:
        {
            const int64_t ni = nextIndexCycles();
            if (ni < 0) { fdcCycles = WAIT_NO_DRIVE; }
            else        { commandState_ = RUN_WT_INDEX; fdcCycles = int(ni); delayIndexPaced_ = true; }
        }
        break;
     case RUN_WT_INDEX:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        if (drive_[driveSel_].writeProtect) {
            updateStr(0, STR_WPRT);
            fdcCycles = cmdComplete(true);
            break;
        }
        updateStr(STR_WPRT, 0);
        bufferReset();
        dmaBytesToTransfer_ = bytesPerTrack();
        commandState_ = RUN_WT_TRANSFER_LOOP;
        fdcCycles = CMD_IMMEDIATE;
        break;
     case RUN_WT_TRANSFER_LOOP:
        if (dmaBytesToTransfer_-- > 0) {
            bufferAdd(fifoPull());
            fdcCycles = int(MFM_BYTE);
        } else {
            commandState_ = RUN_WT_COMPLETE; fdcCycles = CMD_IMMEDIATE;
        }
        break;
     case RUN_WT_COMPLETE:
        writeTrackBuffer();                                   // extrait les secteurs du flux écrit
        fdcCycles = cmdComplete(true);
        break;
    }
    return fdcCycles;
}

// =============================================================================
//  Décodage des commandes (écriture du registre CR) et amorçage.
// =============================================================================
void Fdc::executeCommand(uint8_t cmd) {
    refreshDriveSide();
    cr_ = cmd;
    const uint8_t type = cmdType(cmd);
    // Trace FDC optionnelle (NEOST_FDC_DEBUG=1) : commande + piste/secteur/horloge.
    static const bool fdcDebug = getenv("NEOST_FDC_DEBUG") != nullptr;
    if (fdcDebug)
        std::fprintf(stderr, "[fdc] @%lldms cmd=%02x type=%d tr=%d sr=%d side=%d head=%d dmaCnt=%d motor=%d\n",
            (long long)(nowCyc() / 8021), cmd, type, tr_, sr_, side_,
            driveSel_ >= 0 ? drive_[driveSel_].headTrack : -1, dmaSectorCount_, (str_ & STR_MOTOR) ? 1 : 0);

    // Nouvelle commande : on efface l'IRQ du FDC (sauf « force interrupt immediate »).
    if ((irqSignal_ & IRQ_FORCED) && !(interruptCond_ & INT_COND_IMMEDIATE))
        irqSignal_ &= ~IRQ_FORCED;
    if (type != 4) fdcClearIrq();
    interruptCond_ = 0;

    int fdcCycles = 0;
    switch (type) {
     case 1: {                                                // RESTORE / SEEK / STEP[-IN/-OUT]
        commandType_ = 1; statusTypeI_ = true;
        switch (cr_ & 0xf0) {
         case 0x00: command_ = CMD_RESTORE; commandState_ = RUN_RE_SEEK0;    emitSound(FdcSound::Seek); break;
         case 0x10: command_ = CMD_SEEK;    commandState_ = RUN_SE_TOTRACK;
                    emitSound((dr_ > tr_ ? dr_ - tr_ : tr_ - dr_) > 1 ? FdcSound::Seek : FdcSound::Step); break;
         case 0x20: case 0x30: command_ = CMD_STEP; commandState_ = RUN_ST_ONCE; emitSound(FdcSound::Step); break;
         case 0x40: case 0x50: command_ = CMD_STEP; commandState_ = RUN_ST_ONCE; stepDir_ = 1;  emitSound(FdcSound::Step); break;
         case 0x60: case 0x70: command_ = CMD_STEP; commandState_ = RUN_ST_ONCE; stepDir_ = -1; emitSound(FdcSound::Step); break;
        }
        updateStr(STR_INDEX | STR_CRC | STR_RNF, STR_BUSY);
        fdcCycles = PREPARE_TYPE_I;
        break;
     }
     case 2: {                                                // READ / WRITE SECTOR
        commandType_ = 2; statusTypeI_ = false;
        if ((cr_ & 0xf0) <= 0x90) {                           // 0x80/0x90 : read sector(s)
            command_ = CMD_READSECTORS; commandState_ = RUN_RS_READDATA;
            updateStr(STR_DRQ | STR_LOST | STR_CRC | STR_RNF | STR_RECTYPE | STR_WPRT, STR_BUSY);
        } else {                                              // 0xA0/0xB0 : write sector(s)
            command_ = CMD_WRITESECTORS; commandState_ = RUN_WS_WRITEDATA;
            updateStr(STR_DRQ | STR_LOST | STR_CRC | STR_RNF | STR_RECTYPE, STR_BUSY);
        }
        fdcCycles = PREPARE_TYPE_II;
        break;
     }
     case 3: {                                                // READ ADDRESS / READ TRACK / WRITE TRACK
        commandType_ = 3; statusTypeI_ = false;
        switch (cr_ & 0xf0) {
         case 0xc0: command_ = CMD_READADDRESS; commandState_ = RUN_RA_READADDRESS; break;
         case 0xe0: command_ = CMD_READTRACK;   commandState_ = RUN_RT_READTRACK;   break;
         case 0xf0: command_ = CMD_WRITETRACK;  commandState_ = RUN_WT_WRITETRACK;  break;
        }
        updateStr(STR_DRQ | STR_LOST | STR_CRC | STR_RNF | STR_RECTYPE | STR_WPRT, STR_BUSY);
        fdcCycles = PREPARE_TYPE_III;
        break;
     }
     default: {                                               // FORCE INTERRUPT (type IV)
        commandType_ = 4;
        if (!(str_ & STR_BUSY)) {                             // FDC inactif → statut type I, moteur ON
            statusTypeI_ = true;
            updateStr(STR_SPINUP, STR_MOTOR);
        }
        interruptCond_ = cr_ & 0x0f;
        if (interruptCond_ & INT_COND_IMMEDIATE) fdcSetIrq(IRQ_FORCED);
        else                                     fdcClearIrq();
        fdcCycles = PREPARE_TYPE_IV + cmdComplete(false);    // BUSY tombe, moteur s'arrêtera
        break;
     }
    }

    replaceCommandPossible_ = true;     // remplaçable pendant prepare+spinup
    // Le délai de préparation (type I/II/III/IV) est une commande, donc accéléré en
    // mode « FDC rapide » (jamais cadencé sur la rotation).
    if (sched_) sched_->schedule(Scheduler::FDC, nowCyc() + applyFastFdc(fdcCycles));
}

// Échéance de la machine à états : avance d'une ou plusieurs phases (les phases
// « immédiates » s'enchaînent), puis reprogramme la prochaine échéance.
void Fdc::onFdcEvent() {
    int fdcCycles = 0;
    int guard = 0;                                            // garde-fou anti-boucle
    do {
        delayIndexPaced_ = false;                            // remis par les états cadencés sur l'index
        indexCheckUpdate();
        if (command_ != CMD_NULL) {
            switch (command_) {
             case CMD_RESTORE:      fdcCycles = updateRestore();      break;
             case CMD_SEEK:         fdcCycles = updateSeek();         break;
             case CMD_STEP:         fdcCycles = updateStep();         break;
             case CMD_READSECTORS:  fdcCycles = updateReadSectors();  break;
             case CMD_WRITESECTORS: fdcCycles = updateWriteSectors(); break;
             case CMD_READADDRESS:  fdcCycles = updateReadAddress();  break;
             case CMD_READTRACK:    fdcCycles = updateReadTrack();    break;
             case CMD_WRITETRACK:   fdcCycles = updateWriteTrack();   break;
             case CMD_MOTOR_STOP:   fdcCycles = updateMotorStop();    break;
            }
        }
    } while (command_ != CMD_NULL && fdcCycles == 0 && ++guard < 100000);

    if (command_ != CMD_NULL && sched_) {
        // Délai de commande/transfert → accéléré en mode « FDC rapide » ; délai cadencé
        // sur la rotation (spin-up, arrêt moteur, attente d'index) → durée réelle.
        const int delay = delayIndexPaced_ ? fdcCycles : applyFastFdc(fdcCycles);
        sched_->schedule(Scheduler::FDC, nowCyc() + delay);
    }
}

// =============================================================================
//  Transition de média (cf. Hatari Floppy_DriveTransitionUpdateState).
// =============================================================================
bool Fdc::transitionForceWprt(int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    if (dk.transitionPhase == FloppyDisk::TRANS_NONE || !sched_) return false;
    if (sched_->now() >= dk.transitionDeadline) {   // fenêtre écoulée → transition finie
        dk.transitionPhase = FloppyDisk::TRANS_NONE;
        return false;
    }
    return dk.transitionPhase == FloppyDisk::TRANS_EJECT;  // éjection → force WPRT
}

// =============================================================================
//  Accès mémoire MMIO $FF8600-$FF860F.
// =============================================================================
uint8_t Fdc::read8(uint32_t addr) {
    // $FF8604/06 : accès octet → bus error sur ST (registres mot-seulement, cf. fdc.c).
    if (bus_.ioAccessWidth() == 1) {
        const unsigned off = addr & 0xF;
        if (off >= 4 && off <= 7 && bus_.cpu) {
            if (bus_.cpu->triggerBusError(addr, false)) return 0;
        }
    }
    auto noteFf8604 = [&](uint8_t b) {
        if (!(dmaMode_ & DMA_SCREG))
            ff8604recent_ = uint16_t((ff8604recent_ & 0xff00) | b);
    };
    switch (addr & 0xF) {
        case 0x4:                                    // data, octet haut
            if (dmaMode_ & DMA_SCREG) return uint8_t(ff8604recent_ >> 8);
            return 0;
        case 0x5:                                    // data, octet bas
            if (dmaMode_ & DMA_SCREG) return uint8_t(ff8604recent_);
            if (dmaMode_ & DMA_CSACSI) { const uint8_t v = acsiStatus_; noteFf8604(v); return v; }
            switch (dmaMode_ & (DMA_A1 | DMA_A0)) {
                case 0: {             // FDC_CS : registre de statut
                    refreshDriveSide();
                    indexCheckUpdate();
                    // Pour un statut type I, certains bits sont mis à jour en temps réel
                    // d'après les signaux (TR00, INDEX, WPRT). Sinon, on renvoie STR tel quel.
                    if (statusTypeI_) {
                        if (driveSel_ < 0) {
                            updateStr(STR_TR00 | STR_INDEX | STR_WPRT, 0);
                        } else {
                            const FloppyDisk& dk = drive_[driveSel_];
                            if (dk.headTrack == 0) updateStr(0, STR_TR00); else updateStr(STR_TR00, 0);
                            if (indexState())      updateStr(0, STR_INDEX); else updateStr(STR_INDEX, 0);
                            updateStr(STR_CRC, 0);
                            if (!dk.present() || dk.writeProtect) updateStr(0, STR_WPRT);
                            else                                     updateStr(STR_WPRT, 0);
                            // Créneau de transition média (éjection) → force WPRT, ce que TOS
                            // lit après un Restore pour détecter le changement de disquette.
                            if (transitionForceWprt(driveSel_)) updateStr(0, STR_WPRT);
                        }
                    }
                    // La lecture du statut efface l'IRQ (sauf « force interrupt immediate »).
                    if ((irqSignal_ & IRQ_FORCED) && !(interruptCond_ & INT_COND_IMMEDIATE))
                        irqSignal_ &= ~IRQ_FORCED;
                    fdcClearIrq();
                    noteFf8604(str_);
                    return str_;
                }
                case DMA_A0: noteFf8604(tr_); return tr_;               // FDC_TR
                case DMA_A1: noteFf8604(sr_); return sr_;               // FDC_SR
                default:     noteFf8604(dr_); return dr_;               // FDC_DR
            }
        case 0x6: return 0;                          // status, octet haut
        case 0x7: return uint8_t(dmaStatusWord());   // status, octet bas
        // Adresse DMA ($FF8609/0B/0D) : RELISIBLE — le compteur incrémente pendant le
        // transfert (cf. Hatari FDC_GetDMAAddress). Les diagnostics la relisent pour
        // vérifier le nombre d'octets transférés (sinon « DMA count error »).
        case 0x9: return uint8_t(dmaAddr_ >> 16);
        case 0xB: return uint8_t(dmaAddr_ >> 8);
        case 0xD: return uint8_t(dmaAddr_);
        case 0xE: case 0xF: return uint8_t(densityMode_);    // $FF860E : densité DD/HD
        default:  return 0xFF;
    }
}

void Fdc::write8(uint32_t addr, uint8_t v) {
    if (bus_.ioAccessWidth() == 1) {
        const unsigned off = addr & 0xF;
        if (off >= 4 && off <= 7 && bus_.cpu) {
            if (bus_.cpu->triggerBusError(addr, true)) return;
        }
    }
    switch (addr & 0xF) {
        case 0x4: dataHi_ = v; return;               // data, octet haut (latch)
        case 0x5:                                    // data, octet bas → action
            ff8604recent_ = uint16_t((ff8604recent_ & 0xff00) | v);
            if (dmaMode_ & DMA_SCREG)  { dmaSectorCount_ = uint16_t(((dataHi_ << 8) | v) & 0xff); return; }
            if (dmaMode_ & DMA_CSACSI) { writeAcsi(addr, v); return; }   // disque dur ACSI
            switch (dmaMode_ & (DMA_A1 | DMA_A0)) {
                case 0: {                            // FDC_CS : commande
                    refreshDriveSide();
                    if (str_ & STR_BUSY) {           // FDC occupé : seuls force-int / remplacement
                        const uint8_t tn = cmdType(v);
                        const bool ok = (tn == 4)
                            || (replaceCommandPossible_
                                && ((tn == 1 && commandType_ == 1) || (tn == 2 && commandType_ == 2)));
                        if (!ok) return;             // commande ignorée
                    }
                    executeCommand(v);
                    return;
                }
                case DMA_A0: tr_ = v; return;        // FDC_TR
                case DMA_A1: sr_ = v; return;        // FDC_SR
                default:     dr_ = v; return;        // FDC_DR
            }
        case 0x6: ctrlHi_ = v; return;               // control, octet haut (latch)
        case 0x7: {                                  // control, octet bas
            const uint16_t prev = dmaMode_;
            dmaMode_ = uint16_t((ctrlHi_ << 8) | v);
            if ((prev ^ dmaMode_) & 0x0100) dmaResetFifo();  // bascule du bit 8 → reset DMA
            return;
        }
        case 0x9:
            dmaAddr_ = (dmaAddr_ & 0x00FFFFu) | (uint32_t(v) << 16);
            dmaAddr_ &= dmaAddressMask(bus_.ram.size());   // FDC_WriteDMAAddress
            return;
        case 0xB:
            dmaAddr_ = (dmaAddr_ & 0xFF00FFu) | (uint32_t(v) << 8);
            dmaAddr_ &= dmaAddressMask(bus_.ram.size());
            return;
        case 0xD:
            dmaAddr_ = (dmaAddr_ & 0xFFFF00u) | uint32_t(v);
            dmaAddr_ &= dmaAddressMask(bus_.ram.size());
            return;
        case 0xE: case 0xF: densityMode_ = v; return;    // $FF860E : densité DD/HD
        default:  return;
    }
}

// =============================================================================
//  Contrôleur ACSI (disque dur). Port minimal de Hatari hdc.c : commande de 6
//  octets (classe 0) reçue octet par octet via la DMA, transfert DMA, statut.
//  Un disque virtuel EN MÉMOIRE (cible 0) suffit au « Hard Disk DMA Exerciser ».
// =============================================================================
static constexpr uint32_t ACSI_DISK_CAP = 64u * 1024u * 1024u;   // plafond du disque virtuel

void Fdc::writeAcsi(uint32_t /*addr*/, uint8_t v) {
    setIntrqLine(false);                   // efface l'IRQ (réarmée si l'octet est accepté)
    // Le « pin A1 » de l'ACSI est câblé sur le bit de contrôle DMA_A0 (0x02) : 0 pour le
    // 1er octet du paquet (sélection cible + opcode), 1 pour les octets suivants.
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
    if (acsiTarget_ == 0) setIntrqLine(true);
}

void Fdc::executeAcsi() {
    bus_.megaSteCacheFlushIfEnabled();   // DMA disque dur via BGACK → cache invalidé
    const uint8_t op = acsiCmd_[0];
    const uint32_t lba = (uint32_t(acsiCmd_[1] & 0x1F) << 16) | (uint32_t(acsiCmd_[2]) << 8) | acsiCmd_[3];
    const int      cnt = acsiCmd_[4] ? acsiCmd_[4] : 256;
    acsiStatus_ = 0;                                      // OK par défaut
    const uint64_t need = uint64_t(lba + uint32_t(cnt)) * 512u;
    if (need <= ACSI_DISK_CAP && hd_.size() < need) hd_.resize(size_t(need), 0);
    auto toRam = [&](const uint8_t* src, uint32_t n) {
        for (uint32_t j = 0; j < n; ++j)
            bus_.dmaWrite8(dmaAddr_ + j, src[j]);        // via le plan mémoire (MMU)
        dmaAddr_ = (dmaAddr_ + n) & dmaAddressMask(bus_.ram.size());
    };
    switch (op) {
        case 0x08: {                                     // READ(6) : disque → RAM
            uint32_t off = lba * 512u;
            for (int i = 0; i < cnt && off + 512u <= hd_.size(); ++i, off += 512u) toRam(&hd_[off], 512);
            dmaSectorCount_ = 0; break;
        }
        case 0x0A: {                                     // WRITE(6) : RAM → disque
            uint32_t off = lba * 512u, src = dmaAddr_;
            for (int i = 0; i < cnt && off + 512u <= hd_.size(); ++i, off += 512u)
                for (uint32_t j = 0; j < 512u; ++j)
                    hd_[off + j] = bus_.dmaRead8(src + uint32_t(i) * 512u + j);   // via MMU
            dmaAddr_ = (dmaAddr_ + uint32_t(cnt) * 512u) & dmaAddressMask(bus_.ram.size());
            dmaSectorCount_ = 0; break;
        }
        case 0x12: {                                     // INQUIRY : identité du périphérique
            uint8_t inq[36] = {0};
            inq[0] = 0x00;          // type : disque à accès direct
            inq[1] = 0x00;          // non amovible
            inq[2] = 0x02;          // version SCSI-2
            inq[4] = 31;            // longueur additionnelle
            std::memcpy(inq + 8, "NeoST   NeoST Hard Disk  1.0 ", 28);
            toRam(inq, uint32_t(acsiCmd_[4] ? acsiCmd_[4] : 36)); dmaSectorCount_ = 0; break;
        }
        case 0x25: {                                     // READ CAPACITY (classe 1, inoffensif ici)
            const uint32_t last = ACSI_DISK_CAP / 512u - 1;
            uint8_t cap[8] = { uint8_t(last >> 24), uint8_t(last >> 16), uint8_t(last >> 8), uint8_t(last),
                               0, 0, 2, 0 };             // taille de bloc = 512
            toRam(cap, 8); dmaSectorCount_ = 0; break;
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
