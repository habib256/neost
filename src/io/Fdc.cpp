// =============================================================================
//  Fdc.cpp — WD1772 + DMA disquette : modèle ROTATIONNEL daté (port Hatari).
//
//  Machine à états par commande (cf. extern/hatari/src/fdc.c, chemin « _ST »).
//  Chaque phase renvoie un nombre de cycles FDC (≈ cycles CPU à ~8 MHz sur ST) ;
//  l'ordonnanceur (Scheduler::FDC) rappelle onFdcEvent() à l'échéance pour avancer
//  la commande. On modélise : impulsions d'index (300 tr/min, [B] p. 1-7), spin-up
//  (6 tours, [B] p. 1-7), chargement de tête (15 ms, [B] p. 1-6), latence rotationnelle
//  jusqu'au champ ID du secteur, transfert DMA octet par octet (FIFO 16 o), arrêt moteur
//  (9 tours, [B] p. 1-7), INTRQ datée.
//
//  RÉFÉRENCES DE CE FICHIER :
//  [B] « WD177X-00 Floppy Disk Formatter/Controller » (Western Digital),
//      extern/datasheets/WD177X-00.pdf,
//      sha256 a2b267094ec9af10c0f4d68fbc5c6ea94ec873897ad9b129a2ff607ad7fe8388.
//      Pagination imprimée : « p. 1-7 » = page 7 du PDF. Elle couvre les 1770/1772/1773 :
//      l'état du 1772 est p. 1-18, le tableau p. 1-19 ne vaut que pour le 1773.
//  [D] Hatari = extern/hatari @ f0736b24, chemins relatifs à src/ (floppy.h dans src/includes/).
//      Ses notes « [NP] tested on STF » sont des relevés d'un tiers, rapportés sans
//      protocole : ils restent [D] ici. La fiche prime sur Hatari.
//  [F] La fiche se contredit elle-même sur quatre points, tranchés seulement par une mesure
//      sur WD1772 : pas 2/3/8/12 ms (p. 1-1) contre 6/12/2/3 ms (p. 1-6) ; recherche d'ID sur
//      4 tours (p. 1-9), 5 (p. 1-7, 1-9, 1-10) ou 6 (organigramme p. 1-8) ; polarité de h,
//      h=0 → spin-up (p. 1-6) contre h=1 (organigrammes p. 1-9, 1-14) ; date du type
//      d'enregistrement, fin de lecture (p. 1-9) contre marque de données (p. 1-11).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Fdc.hpp"
#include "io/IpfImage.hpp"
#include "io/DiskImageCodec.hpp"
#include "core/Cpu68k.hpp"
#include "core/Bus.hpp"
#include "core/YM2149.hpp"
#include "io/Mfp.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <functional>

#include <filesystem>

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

// --- Bits du registre de statut WD1772 ---------------------------------------
// [B] WD177X-00 p. 1-18 (état 1770/1772) : S7 Motor On, S6 Write Protect, S5 Record Type/
// Spin-Up, S4 RNF, S3 CRC, S2 Lost Data/TR00, S1 DRQ/Index, S0 Busy.
// [D] Hatari fdc.c:250-260 @ f0736b24 : mêmes valeurs.
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
// [B] WD177X-00 p. 1-6 (Flag Summary) : V, E, h, u, m. Le test du bit 3 suit le tableau
// (h=0 active le spin-up), pas les organigrammes p. 1-9 et 1-14 qui impriment h=1.
enum : uint8_t {
    CMD_BIT_VERIFY      = 0x04,   // type I : vérif piste après seek
    CMD_BIT_HEADLOAD    = 0x04,   // type II/III : délai de chargement de tête
    CMD_BIT_SPINUP      = 0x08,   // 1 = désactive le spin-up
    CMD_BIT_UPDATETRACK = 0x10,   // type I STEP : met à jour TR
    CMD_BIT_MULTI       = 0x10,   // type II : lecture/écriture multi-secteurs
};

// --- Condition d'un Force Interrupt (type IV) -------------------------------
// [B] WD177X-00 p. 1-6, p. 1-16 : I2 = à chaque impulsion d'index, I3 = immédiate ; I0 et I1
// sont sans usage sur 1770/1772.
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

// --- Constantes de temps en cycles FDC (conversion BUS à l'ordonnancement) --
// [B] WD177X-00 p. 1-5 : horloge de 8 MHz, base des délais de la fiche. [D] fdc.c:385 @ f0736b24 :
// 8,021247 MHz sur ST, hors de la tolérance de 0,1 % de la p. 1-2.
static constexpr int64_t MFM_BYTE         = 4 * 8 * 8;     // 256 cyc : 4µs/bit × 8 bits × 8 MHz ([B] p. 1-1 : 250 kbit/s en MFM)
static constexpr int64_t CYCLES_PER_REV   = 1604249;       // 300 tr/min ([B] p. 1-7) @ 8,021247 MHz ([D] fdc.c:385) → ~200 ms
// [D] fdc.c:405, 422-423 @ f0736b24 : 3710 µs × 8 = 29680, à l'horloge STANDARD 8 MHz d'Hatari, PAS
// au 8,021 MHz machine. Relevé d'un tiers sur STF rapporté par Hatari, 1,5 à 4 ms selon le lecteur.
// [B] p. 1-23 n'exige que tIP ≥ 20 µs : la durée vient du lecteur, la fiche ne la fixe pas.
static constexpr int64_t INDEX_PULSE_LEN  = 29680;         // 3,71 ms : durée du signal d'index
static constexpr int     IP_SPIN_UP       = 6;             // tours pour atteindre la vitesse ([B] p. 1-7)
static constexpr int     IP_MOTOR_OFF     = 9;             // tours d'inactivité → moteur off ([B] p. 1-7)
// [B] p. 1-7 (type I), p. 1-9 (exemple) et p. 1-10 (organigramme type II) disent 5 tours ; mais p. 1-9
// dit aussi « four revolutions » et l'organigramme type I p. 1-8 « 6 index holes ». [D] fdc.c:416 : 5.
// [F] Contradiction de la fiche, tranchée par la durée d'un RNF ou d'un verify raté sur un vrai 1772.
static constexpr int     IP_ADDRESS_ID    = 5;             // tours max pour trouver un champ ID
static constexpr int     MAX_TRACK        = 90;            // butée physique de la tête ([D] fdc.c:449 ; fiche muette, c'est le lecteur)
// [B] p. 1-6 : « 1772 Add 15ms Delay » pour E=1. Le settling du verify type I y est écrit « *30 msec »,
// astérisque sans note dans ce scan. [D] fdc.c:420, 2694 : 15 ms aussi pour le verify.
static constexpr int64_t HEAD_LOAD        = 8 * 15000;     // 15 ms (réf. 8 MHz : 8 cyc/µs)
// [B] p. 1-6 (colonne WD1772-00, r1 r0 = 00, 01, 10, 11). La p. 1-1 écrit « 2, 3, 8 et 12 msec »,
// contredite par ce tableau qui prime ici. [D] fdc.c:454 : mêmes valeurs.
static constexpr int     STEP_RATE_MS[4]  = {6, 12, 2, 3};
// Délais de préparation : la fiche ne les donne pas.
static constexpr int     PREPARE_TYPE_I   = 90 * 8;        // ≥ 0,09 ms ([D] fdc.c:427-428 : relevé d'un tiers sur 520 STF rapporté par Hatari)
static constexpr int     PREPARE_TYPE_II  = 1 * 8;         // [D] fdc.c:430
static constexpr int     PREPARE_TYPE_III = 1 * 8;         // [D] fdc.c:431
// [F] Inconnu, Hatari aussi ([D] fdc.c:432 @ f0736b24 : « FIXME [NP] : this was not measured »).
// Tranché par une mesure de BUSY/INTRQ après $D0 sur un vrai ST.
static constexpr int     PREPARE_TYPE_IV  = 100 * 8;
static constexpr int     CMD_COMPLETE     = 1 * 8;         // [D] fdc.c:433
static constexpr int     CMD_IMMEDIATE    = 0;
static constexpr int     WAIT_NO_DRIVE    = 50000;         // attente d'un lecteur/disque valide
static constexpr int     REFRESH_INDEX    = 500;           // pas de mise à jour de l'index
static constexpr int     FDC_FAST_FACTOR  = 10;            // « FDC rapide » : délais ÷ ce facteur (cf. Hatari)

// --- Disposition standard d'une piste (gaps), cf. Hatari fdc.h --------------
// [B] WD177X-00 p. 1-17 (Double Density Format) : 60×$4E, 12×$00, 3×$A1, $FE ; après l'ID, 22×$4E,
// 12×$00, 3×$A1, marque. [D] fdc.h:26-30 @ f0736b24 : mêmes valeurs, et GAP4.
static constexpr int GAP1  = 60;   // pré-gap piste (0x4e)
static constexpr int GAP2  = 12;   // pré-gap ID secteur (0x00)
static constexpr int GAP3a = 22;   // post-gap ID (0x4e)
static constexpr int GAP3b = 12;   // pré-gap données (0x00)
static constexpr int GAP4  = 40;   // post-gap données (0x4e) ; [D] fdc.h:30, [B] p. 1-18 : 24 au format 256 o, minimum 16
// Secteur brut 512 o (ID + données + gaps) : 614 octets.
static constexpr int RAW_SECTOR_512 = GAP2 + 3 + 1 + 6 + GAP3a + GAP3b + 3 + 1 + 512 + 2 + GAP4;
static constexpr int BYTES_PER_TRACK = 6268;  // piste DD standard ([D] fdc.c:394, 409-410 ; 6250 d'après [B] p. 1-1 et 1-7 à 8 MHz)
static constexpr uint8_t SECTOR_SIZE_512 = 2; // code « taille » 512 o dans le champ ID ([B] p. 1-5)

// CRC16 CCITT (poly 0x1021, init 0xFFFF) du WD1772.
// [B] WD177X-00 p. 1-3 : G(x) = x^16 + x^12 + x^5 + 1, préchargé à un, depuis la marque d'adresse.
static uint16_t crc16(const uint8_t* buf, int n) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; ++i) {
        crc ^= uint16_t(buf[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

// Type de commande d'après les bits hauts du registre CR ([B] WD177X-00 p. 1-6, Command Summary).
static uint8_t cmdType(uint8_t cr) {
    if (!(cr & 0x80)) return 1;             // type I  : restore/seek/step
    if (!(cr & 0x40)) return 2;             // type II : read/write sector
    if ((cr & 0xf0) != 0xd0) return 3;      // type III: read addr/track, write track
    return 4;                               // type IV : force interrupt
}

// Numéro de secteur logique → offset image (.st : piste, puis face, puis secteur).
// ⚠ Renvoie un offset 64 bits : en uint32 le produit REBOUCLAIT. Un secteur 0
// (registre SR_ restauré d'un save-state forgé, ou géométrie absurde) donne
// lsn = −1 → off = $FFFFFE00, et la garde « off + 512 <= image.size() » se
// calculait ALORS en uint32 : $FFFFFE00 + 512 = 0, donc la garde PASSAIT et
// l'accès indexait l'image ~4 Go plus loin. Le 64 bits rend les gardes exactes.
// Sentinelle « pas d'offset » : assez grande pour être refusée par toutes les
// gardes, assez petite pour qu'un « off + longueur » ne reboucle pas à son tour.
static constexpr uint64_t kNoLsn = uint64_t(1) << 62;

static inline uint64_t lsnOffset(int track, int side, int sector, int spt, int sides) {
    const int64_t lsn = (int64_t(track) * sides + side) * spt + (sector - 1);
    if (lsn < 0) return kNoLsn;                          // secteur 0 / géométrie absurde
    return uint64_t(lsn) * 512u;
}

// -----------------------------------------------------------------------------
//  Détection de géométrie (port fidèle de Hatari floppy.c) — le BPB du secteur
//  de boot est souvent FAUX sur les disquettes de jeux/cracks (Xenon 2 : faces=1
//  au lieu de 2 ; Epic : BPB entièrement bidon ; Super Hang-On : 9 spt au lieu
//  de 10). Hatari recoupe le BPB avec la TAILLE RÉELLE de l'image et recalcule
//  spt/faces en cas d'incohérence — sinon les chargements multi-secteurs lisent
//  les mauvais octets (bombes / retry infini).
// -----------------------------------------------------------------------------

// Cf. Hatari Floppy_DoubleCheckFormat ([D] floppy.c:766 @ f0736b24) : devine faces et spt
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

// Cf. Hatari Floppy_FindDiskDetails ([D] floppy.c:840 @ f0736b24) : lit le BPB et ne lui fait
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
// Le fichier se PRÉSENTE-t-il comme une .msa ? Mêmes contrôles d'en-tête que
// decodeMsa (le magic seul, 2 octets, ferait des faux positifs sur des .st
// légitimes). Sert au repli de loadImage quand la décompression échoue.
static bool looksLikeMsaHeader(const std::vector<uint8_t>& raw) {
    if (raw.size() < 10 || raw[0] != 0x0E || raw[1] != 0x0F) return false;
    const int spt   = (raw[2] << 8) | raw[3];
    const int sides = ((raw[4] << 8) | raw[5]) + 1;
    const int t0    = (raw[6] << 8) | raw[7];
    const int t1    = (raw[8] << 8) | raw[9];
    return spt >= 1 && spt <= 56 && sides >= 1 && sides <= 2 && t1 >= t0 && t1 <= 86;
}

static bool decodeMsa(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out) {
    if (raw.size() < 10 || raw[0] != 0x0E || raw[1] != 0x0F) return false;
    // Avertissement RLE émis UNE FOIS par décodage (A30) : une image douteuse a
    // typiquement toutes ses pistes atteintes, et le message sortait par piste —
    // 86 lignes identiques pour un seul fichier. C'est le harnais de fuzzing qui
    // l'a rendu visible : 130 lignes par run, noyant tout le reste.
    // NEOST_QUIET_PARSERS=1 le coupe complètement : le harnais décode des dizaines
    // de milliers d'images DÉLIBÉRÉMENT corrompues, l'avertissement n'y apprend
    // rien. Un interrupteur explicite plutôt que rediriger stderr côté harnais —
    // on ne met JAMAIS en sourdine le flux où un sanitizer écrit son rapport.
    static const bool quiet = std::getenv("NEOST_QUIET_PARSERS") != nullptr;
    bool rleWarned = quiet;
    const int spt   = (raw[2] << 8) | raw[3];
    const int sides = ((raw[4] << 8) | raw[5]) + 1;
    const int t0    = (raw[6] << 8) | raw[7];
    const int t1    = (raw[8] << 8) | raw[9];
    // Bornes Hatari (floppies/msa.c:137-140) : spt ≤ 56 (accepte HD/ED étendues,
    // l'ancien 30 rejetait une .msa ED 36 spt valide), pistes ≤ 86.
    if (spt < 1 || spt > 56 || sides < 1 || sides > 2 || t1 < t0 || t1 > 86) return false;
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
                        std::size_t cnt = static_cast<std::size_t>((raw[p + 1] << 8) | raw[p + 2]); p += 3;
                        // Plafond sur ce qui reste de la piste ([D] msa.c:205-210 @ f0736b24 : « Limit
                        // length to size of track, incorrect images may overflow »).
                        // Sans lui, out dépasse target et TOUTE l'image est rejetée →
                        // repli en .st brut INSCRIPTIBLE, qui écrase le fichier source.
                        if (out.size() + cnt > target) {
                            if (!rleWarned) {
                                rleWarned = true;
                                std::fprintf(stderr, "[FDC] .msa: RLE run too long → truncated "
                                                     "(dubious image ; message émis une seule fois)\n");
                            }
                            cnt = target - out.size();
                        }
                        out.insert(out.end(), cnt, val);
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
    // Hatari se contente de ces 4 octets — mais il ne les teste QUE sur un fichier
    // dont l'EXTENSION est .dim (DIM_FileNameIsDIM, floppy.c). NeoST, lui, détecte par
    // CONTENU : la même signature devient alors un piège, car $4242 est aussi
    // l'encodage de « clr.w d2 », début plausible d'un secteur de boot exécutable, et
    // les deux autres octets tombent au milieu du nom OEM / du numéro de série. Une
    // vraie .ST était ainsi montée amputée de 32 octets — donc décalée, de géométrie
    // fausse et forcée en lecture seule. On exige donc en plus que les champs de
    // géométrie soient plausibles ET qu'ils rendent compte de la taille du fichier
    // (dim.c § « .DIM FILE FORMAT » : 0x06 faces−1, 0x08 spt, 0x0C piste de fin).
    const int sides = raw[0x06] + 1, spt = raw[0x08], endTrack = raw[0x0C];
    if (sides > 2 || spt < 1 || spt > 48 || endTrack > 85) return false;
    const std::size_t expect = std::size_t(endTrack - raw[0x0A] + 1) * spt * sides * 512u;
    if (raw.size() - 32 != expect) return false;
    out.assign(raw.begin() + 32, raw.end());
    return true;
}

// -----------------------------------------------------------------------------
//  Ré-encodage .MSA — port de MSA_WriteDisk / MSA_FindRunOfBytes
//  (extern/hatari/src/floppies/msa.c:275-420). Symétrique de decodeMsa ci-dessus.
// -----------------------------------------------------------------------------

// Longueur du run d'octets identiques en tête de [p, p+n). 0 = « pas de run » →
// l'appelant émet l'octet tel quel. Port littéral de MSA_FindRunOfBytes : un run de
// moins de 4 ne vaut pas ses 4 octets d'encodage et est donc refusé… SAUF si l'octet
// est le marqueur $E5, qui doit être échappé même isolé (sinon la relecture le prendrait
// pour un début de run).
static int msaFindRun(const uint8_t* p, int n) {
    const bool marker = (*p == 0xE5);
    if (n < 2) return (n == 1 && marker) ? 1 : 0;
    int run = 1;
    const uint8_t b = *p++;
    for (int i = 1; i < n; ++i) { if (*p++ != b) break; ++run; }
    if (run < 4 && !marker) run = 0;
    return run;
}

// Comprime `image` (secteurs bruts) au format .MSA complet, en-tête 10 o inclus.
// Renvoie false si la géométrie ne rend pas compte de la taille de l'image.
// Façade TESTABLE des deux décodeurs (cf. io/DiskImageCodec.hpp) : même ordre
// d'essai que Fdc::loadImage. Existe pour que le harnais de fuzzing d'A30 puisse
// atteindre des fonctions qui, sans elle, resteraient `static` — et donc hors de
// portée du seul outil capable de prouver leur bornage.
namespace diskimg {
bool decodeContainer(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out) {
    // PAS de out.clear() entre les deux essais : Fdc::loadImage n'en fait pas non
    // plus (`if (decodeMsa(raw, conv)) … else if (decodeDim(raw, conv))`), et une
    // façade qui « nettoie » ce que l'appelant réel ne nettoie pas cache justement
    // la classe de bugs qu'on veut attraper — un décodeur qui refuse en laissant
    // des octets derrière lui. Constaté en écrivant le harnais : avec un clear(),
    // la mutation « plafond RLE retiré » passait inaperçue.
    if (decodeMsa(raw, out)) return true;
    return decodeDim(raw, out);
}
}  // namespace diskimg

static bool encodeMsa(const std::vector<uint8_t>& image, int spt, int sides,
                      std::vector<uint8_t>& out) {
    if (spt < 1 || spt > 56 || sides < 1 || sides > 2) return false;
    const std::size_t trackBytes = static_cast<std::size_t>(spt) * 512u;
    if (trackBytes == 0 || image.empty()) return false;
    const std::size_t tracks = image.size() / (trackBytes * static_cast<std::size_t>(sides));
    // L'image doit être un nombre ENTIER de pistes : sinon la dernière serait tronquée
    // et le fichier réécrit ne se relirait pas. La borne est SYMÉTRIQUE de decodeMsa
    // (piste de fin ≤ 86, index 0-based → jusqu'à 87 pistes) : la mettre à 86 rejetait
    // une .msa de 87 pistes que decodeMsa monte pourtant inscriptible → toute
    // sauvegarde y était acceptée en RAM puis JAMAIS persistée (perdue au remontage).
    if (tracks == 0 || tracks > 87 ||
        tracks * trackBytes * static_cast<std::size_t>(sides) != image.size()) return false;

    out.clear();
    out.reserve(image.size() + image.size() / 8 + 16);
    auto put16 = [&out](std::size_t at, unsigned v) {         // big-endian, en place
        out[at]     = static_cast<uint8_t>(v >> 8);
        out[at + 1] = static_cast<uint8_t>(v);
    };
    auto push16 = [&out](unsigned v) {
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v));
    };
    push16(0x0E0F);                                           // ID
    push16(static_cast<unsigned>(spt));
    push16(static_cast<unsigned>(sides - 1));
    push16(0);                                                // piste de début
    push16(static_cast<unsigned>(tracks - 1));                // piste de fin

    for (std::size_t track = 0; track < tracks; ++track)
        for (int side = 0; side < sides; ++side) {
            const uint8_t* src = image.data()
                               + trackBytes * static_cast<std::size_t>(side)
                               + trackBytes * static_cast<std::size_t>(sides) * track;
            const std::size_t lenAt = out.size();             // longueur remplie après coup
            push16(0);
            const std::size_t dataAt = out.size();
            int toGo = static_cast<int>(trackBytes);
            const uint8_t* q = src;
            while (toGo > 0) {
                int run = msaFindRun(q, toGo);
                if (run == 0) { out.push_back(*q++); run = 1; }
                else {
                    out.push_back(0xE5);                      // marqueur
                    out.push_back(*q);                        // octet répété
                    push16(static_cast<unsigned>(run));       // compteur 16 bits
                    q += run;
                }
                toGo -= run;
            }
            // Comprimée plus grosse que l'originale ? On stocke la piste TELLE QUELLE
            // ([D] msa.c:388-402 @ f0736b24) — c'est la longueur == trackBytes qui le signale au lecteur.
            if (out.size() - dataAt >= trackBytes) {
                out.resize(dataAt);
                out.insert(out.end(), src, src + trackBytes);
                put16(lenAt, static_cast<unsigned>(trackBytes));
            } else {
                put16(lenAt, static_cast<unsigned>(out.size() - dataAt));
            }
        }
    return true;
}

// Auto-test DÉTERMINISTE du couple encodeMsa/decodeMsa (aucun disque ni oracle requis).
// Le ré-encodage .MSA écrit PAR-DESSUS le fichier de l'utilisateur : si l'aller-retour
// n'est pas byte-exact, on détruit sa disquette en silence. On vérifie donc, sur des
// motifs qui couvrent chaque branche de l'encodeur, que decodeMsa(encodeMsa(x)) == x.
bool Fdc::msaSelfTest() {
    int ok = 0, fail = 0;
    // (spt, sides, tracks, nom, générateur d'octet) — les motifs visent : incompressible
    // (pire cas, force la branche « piste stockée telle quelle »), runs longs, marqueur
    // $E5 isolé (doit être échappé même seul), $E5 en run, alternance qui interdit tout
    // run, et run à cheval sur la fin de piste.
    struct Motif { const char* nom; std::function<uint8_t(std::size_t)> f; };
    const Motif motifs[] = {
        { "zeros",           [](std::size_t)   -> uint8_t { return 0x00; } },
        { "0xE5 partout",    [](std::size_t)   -> uint8_t { return 0xE5; } },
        { "0xE5 isolés",     [](std::size_t i) -> uint8_t { return (i % 7 == 0) ? 0xE5 : uint8_t(i); } },
        { "alternance",      [](std::size_t i) -> uint8_t { return (i & 1) ? 0xAA : 0x55; } },
        { "runs courts",     [](std::size_t i) -> uint8_t { return uint8_t((i / 3) & 0xFF); } },
        { "runs longs",      [](std::size_t i) -> uint8_t { return uint8_t((i / 977) & 0xFF); } },
        // Pseudo-aléatoire déterministe : incompressible, la piste doit ressortir BRUTE.
        { "incompressible",  [](std::size_t i) -> uint8_t {
              uint32_t x = uint32_t(i) * 2654435761u; x ^= x >> 13; x *= 1274126177u;
              return uint8_t(x >> 24); } },
    };
    const struct { int spt, sides, tracks; } geos[] = {
        { 9, 2, 80 },     // 720 Ko standard
        { 9, 1, 80 },     // simple face
        { 10, 2, 82 },    // 820 Ko (démos)
        { 18, 2, 80 },    // HD 1,44 Mo
        { 36, 2, 80 },    // ED
        { 9, 2, 1 },      // cas limite : une seule piste
        { 9, 2, 87 },     // cas limite HAUT : 87 pistes = max que decodeMsa accepte
                          // (piste de fin 86, 0-based) — encodeMsa doit le persister
                          // aussi (régression « 87 pistes montées mais non sauvables »).
    };
    for (const auto& g : geos)
        for (const auto& m : motifs) {
            const std::size_t n = std::size_t(g.spt) * 512u * std::size_t(g.sides)
                                * std::size_t(g.tracks);
            std::vector<uint8_t> src(n);
            for (std::size_t i = 0; i < n; ++i) src[i] = m.f(i);
            std::vector<uint8_t> enc, dec;
            if (!encodeMsa(src, g.spt, g.sides, enc)) {
                std::fprintf(stderr, "[msa-selftest] FAIL encode %d spt/%d sides/%d tracks \"%s\"\n",
                             g.spt, g.sides, g.tracks, m.nom); ++fail; continue;
            }
            if (!decodeMsa(enc, dec)) {
                std::fprintf(stderr, "[msa-selftest] FAIL decode %d spt/%d sides/%d tracks \"%s\" "
                                     "(%zu B encoded)\n", g.spt, g.sides, g.tracks, m.nom, enc.size());
                ++fail; continue;
            }
            if (dec != src) {
                std::size_t at = 0; while (at < dec.size() && at < src.size() && dec[at] == src[at]) ++at;
                std::fprintf(stderr, "[msa-selftest] FAIL round-trip %d spt/%d sides/%d tracks "
                                     "\"%s\": %zu B != %zu B, first mismatch at %zu\n",
                             g.spt, g.sides, g.tracks, m.nom, dec.size(), src.size(), at);
                ++fail; continue;
            }
            ++ok;
        }
    // --- Phase 2 : le VRAI chemin fichier (montage → écriture → remontage) ---------
    // La phase 1 ne teste que le codec en mémoire. Ici on vérifie ce qui casse pour de
    // bon : qu'une .msa/.dim se monte INSCRIPTIBLE (le bit WPRT ne doit plus dépendre du
    // format), que writeBack met le fichier à jour dans son conteneur, et qu'un
    // remontage relit bien l'octet écrit.
    {
        const char* td = std::getenv("TMPDIR");
        const std::string dir = (td && *td) ? std::string(td) : std::string("/tmp");
        const int spt = 9, sides = 2, tracks = 80;
        std::vector<uint8_t> src(std::size_t(spt) * 512u * sides * tracks);
        for (std::size_t i = 0; i < src.size(); ++i) src[i] = uint8_t((i / 512) ^ (i & 0xFF));

        struct Cas { const char* nom; int format; };
        const Cas cas[] = { { ".msa", FloppyDisk::FMT_MSA }, { ".dim", FloppyDisk::FMT_DIM } };
        for (const Cas& c : cas) {
            const std::string path = dir + "/neost_selftest" + c.nom;
            std::vector<uint8_t> file;
            if (c.format == FloppyDisk::FMT_MSA) {
                if (!encodeMsa(src, spt, sides, file)) {
                    std::fprintf(stderr, "[msa-selftest] FAIL file %s: encoding\n", c.nom);
                    ++fail; continue;
                }
            } else {                                   // .dim : en-tête 32 o + secteurs bruts
                file.assign(32, 0);
                file[0x00] = file[0x01] = 0x42; file[0x03] = 0; file[0x06] = uint8_t(sides - 1);
                file[0x08] = uint8_t(spt); file[0x0A] = 0; file[0x0C] = uint8_t(tracks - 1);
                file.insert(file.end(), src.begin(), src.end());
            }
            { std::ofstream o(path, std::ios::binary | std::ios::trunc);
              o.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size())); }

            // Sur le lecteur B, pour ne pas déranger un éventuel disque en A.
            if (!loadImage(path, 1)) {
                std::fprintf(stderr, "[msa-selftest] FAIL %s: mount refused\n", c.nom);
                ++fail; std::remove(path.c_str()); continue;
            }
            FloppyDisk& dk = drive_[1];
            if (dk.writeProtect) {                     // LE bug d'origine
                std::fprintf(stderr, "[msa-selftest] FAIL %s: mounted WRITE-PROTECTED "
                                     "(the format must no longer force WPRT)\n", c.nom);
                ++fail;
            } else if (dk.image != src) {
                std::fprintf(stderr, "[msa-selftest] FAIL %s: mounted content != source\n", c.nom);
                ++fail;
            } else {
                // Écriture d'un secteur au milieu de l'image, puis recopie hôte.
                const uint64_t off = 40ull * spt * sides * 512ull + 3ull * 512ull;
                for (int i = 0; i < 512; ++i) dk.image[off + i] = uint8_t(0xA0 + (i & 0x0F));
                writeBack(dk, off, 512u);
                std::vector<uint8_t> attendu = dk.image;
                eject(1);
                if (!loadImage(path, 1)) {
                    std::fprintf(stderr, "[msa-selftest] FAIL %s: remount refused\n", c.nom);
                    ++fail;
                } else if (drive_[1].image != attendu) {
                    std::size_t at = 0; const auto& g = drive_[1].image;
                    while (at < g.size() && at < attendu.size() && g[at] == attendu[at]) ++at;
                    std::fprintf(stderr, "[msa-selftest] FAIL %s: write not persisted "
                                         "(first mismatch at %zu, expected %02x got %02x)\n", c.nom, at,
                                 at < attendu.size() ? attendu[at] : 0,
                                 at < g.size() ? g[at] : 0);
                    ++fail;
                } else {
                    std::fprintf(stderr, "[msa-selftest] file %s: writable mount + "
                                         "persisted write OK\n", c.nom);
                    ++ok;
                }
            }
            eject(1);
            std::remove(path.c_str());
        }
    }

    std::fprintf(stderr, "[msa-selftest] %d OK, %d FAIL\n", ok, fail);
    return fail == 0;
}

bool Fdc::loadImage(const std::string& path, int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    const bool wasPresent = dk.present();   // disque déjà monté → échange à chaud
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "[FDC] image not found: %s\n", path.c_str()); return false; }
    const std::streamsize n = f.tellg();
    // tellg() peut renvoyer -1 (taille indéterminable) OU 2^63-1 (répertoire sous
    // Linux, qui N'EST PAS -1) → allocation géante. Borne haute large : les plus
    // grosses images légitimes (STX multi-révolutions) font ~3 Mo.
    constexpr std::streamsize kMaxImage = 8 * 1024 * 1024;
    if (n <= 0 || n > kMaxImage) {
        std::fprintf(stderr, "[FDC] invalid image (%lld B, max %lld B): %s\n",
                     static_cast<long long>(n), static_cast<long long>(kMaxImage), path.c_str());
        return false;
    }
    f.seekg(0);
    std::vector<uint8_t> raw(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(raw.data()), n);
    // Lecture COURTE contrôlée (E/S défaillante, fichier tronqué entre le tellg et le
    // read, support amovible retiré) : sans ce test l'image était complétée de zéros en
    // silence et montée comme valide — le joueur voyait une disquette illisible sans
    // savoir pourquoi. StxImage::loadWd1772 fait déjà ce contrôle.
    if (f.gcount() != n) {
        std::fprintf(stderr, "[FDC] incomplete read (%lld/%lld B): %s\n",
                     static_cast<long long>(f.gcount()), static_cast<long long>(n), path.c_str());
        return false;
    }

    // Format IPF (SPS/CAPS, bloc « CAPS » en tête) : disquette décrite au niveau du
    // FLUX DE CELLULES MFM (protections : pistes longues, densité variable, weak
    // bits). On régénère ce flux (ipf::decode), on le LIT comme le ferait le
    // WD1772 (ipf::toStx : IDs, champs données, CRC, fuzzy, timing, dump READ
    // TRACK) et on monte le résultat sur le chemin _STX. Cf. io/IpfImage.hpp.
    const char* fmt = nullptr;
    std::vector<ipf::TrackRev> ipfRevs;
    if (ipf::isIpf(raw.data(), raw.size())) {
        std::vector<ipf::CellTrack> cells;
        ipf::Info info;
        std::string err;
        if (!ipf::decode(raw, cells, &info, &err)) {
            std::fprintf(stderr, "[FDC] unreadable IPF image (%s): %s — not mounted.\n",
                         err.c_str(), path.c_str());
            return false;
        }
        std::vector<uint8_t> blob;
        ipf::StxStats st;
        if (!ipf::toStx(cells, blob, &ipfRevs, &st)) {
            std::fprintf(stderr, "[FDC] IPF image has no usable track: %s — not mounted.\n",
                         path.c_str());
            return false;
        }
        std::fprintf(stderr, "[FDC] IPF (%s rel. %u rev. %u): %d tracks, %d sector IDs"
                             " (%d bad ID CRC, %d no data, %d bad data CRC, %d deleted,"
                             " %d weak, %d timed)\n",
                     info.encoder == 1 ? "CAPS" : "SPS", unsigned(info.release),
                     unsigned(info.revision), st.tracks, st.sectors, st.idCrcErrors,
                     st.noData, st.dataCrcErrors, st.deleted, st.fuzzySectors, st.timedSectors);
        raw = std::move(blob);
        fmt = "IPF";
    } else if (raw.size() > 4 && raw[0] == 'R' && raw[1] == 'S' && raw[2] == 'Y' && raw[3] == 0) {
        fmt = "STX";
    }

    // Format STX (Pasti, en-tête « RSY\0 ») : image disque BAS NIVEAU (pistes/secteurs
    // bruts, IDs réels, CRC, bits fuzzy, timing) qui préserve les PROTECTIONS. On la
    // PARSE (StxImage) et le FDC dispatche vers le chemin _STX (champ ID véritable,
    // statut par secteur, fuzzy/timing) au lieu du modèle .ST « secteur = offset ».
    // Une IPF arrive ici déjà convertie en STX.
    if (fmt) {
        auto stx = std::make_unique<StxImage>();
        if (!stx->parse(std::move(raw))) {
            std::fprintf(stderr, "[FDC] unreadable %s image: %s — not mounted.\n", fmt, path.c_str());
            return false;
        }
        for (const ipf::TrackRev& r : ipfRevs)      // IPF : durée réelle de chaque tour
            if (StxImage::Track* t = stx->findTrack(r.cylinder, r.head))
                t->revolutionBytes = uint16_t(r.bytes);
        dk.image.clear();
        dk.imgType = FloppyDisk::IMG_STX;
        dk.sides   = stx->sides();
        const int tps = stx->tracksPerSide();
        dk.stx     = std::move(stx);
        dk.raw     = false;
        dk.path    = path;
        if (sched_) {                            // changement de média à chaud (cf. plus bas)
            if (wasPresent) transitionSetState(drive, FloppyDisk::TRANS_EJECT);
            transitionSetState(drive, FloppyDisk::TRANS_INSERT);
        }
        dk.writeProtect = false;                 // écritures en overlay (persisté en .wd1772)
        updateFloppyDensity(drive & 1);
        // Fichier compagnon des écritures (cf. Hatari STX_FileNameToSave) : on
        // remplace l'extension .stx (ou .ipf) par .wd1772 et on recharge les
        // overlays d'une session précédente s'il existe. L'IPF elle-même n'est
        // JAMAIS réécrite.
        dk.wd1772Path = path;
        if (dk.wd1772Path.size() >= 4) {
            std::string ext = dk.wd1772Path.substr(dk.wd1772Path.size() - 4);
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".stx" || ext == ".ipf")
                dk.wd1772Path.resize(dk.wd1772Path.size() - 4);
        }
        dk.wd1772Path += ".wd1772";
        if (dk.stx->loadWd1772(dk.wd1772Path))
            std::fprintf(stderr, "[FDC] writes restored: %s (%zu sector(s), %zu track(s))\n",
                         dk.wd1772Path.c_str(), dk.stx->saveSectors.size(), dk.stx->saveTracks.size());
        std::fprintf(stderr, "[FDC] drive %c: %s (%s, %d tracks, %d side(s))\n",
                     drive & 1 ? 'B' : 'A', path.c_str(), fmt, tps, dk.sides);
        return true;
    }

    // Image .ST/.msa/.dim → on (re)bascule en modèle logique (annule un éventuel STX).
    dk.imgType = FloppyDisk::IMG_ST;
    dk.stx.reset();
    dk.stOverride.clear();
    // Remise à l'état « conteneur .ST inscriptible » : `dk` est RÉUTILISÉ d'un montage
    // à l'autre, et sans ça un précédent STX (raw=false) ou .dim (imgFormat=FMT_DIM)
    // laissait son format à l'image suivante — écriture décalée de 32 o, ou disquette
    // muette en écriture sans raison.
    dk.raw       = true;
    dk.imgFormat = FloppyDisk::FMT_ST;

    // .msa (compressé) ou .dim (en-tête 32 o) → conversion en .st brut ; sinon image
    // .st telle quelle. Les trois conteneurs sont INSCRIPTIBLES : writeBack sait
    // ré-encoder le .msa (MSA_WriteDisk) et décaler l'écriture .dim de l'en-tête.
    std::vector<uint8_t> conv;
    if (decodeMsa(raw, conv)) {
        dk.image = std::move(conv);
        dk.imgFormat = FloppyDisk::FMT_MSA;
        dk.msaSpt = (raw[2] << 8) | raw[3];
        dk.msaSides = ((raw[4] << 8) | raw[5]) + 1;
        std::fprintf(stderr, "[FDC] .msa image decompressed: %s\n", path.c_str());
    } else if (decodeDim(raw, conv)) {
        dk.image = std::move(conv);
        dk.imgFormat = FloppyDisk::FMT_DIM;
        std::fprintf(stderr, "[FDC] .dim image (32 B header stripped): %s\n", path.c_str());
    } else {
        // Un en-tête .msa PLAUSIBLE (mêmes contrôles que decodeMsa) qui ne se décode
        // pas ne doit surtout pas repartir en « .st brut inscriptible » : dk.raw
        // réactiverait writeBack, qui recopierait des secteurs bruts par-dessus le
        // fichier .msa source et le détruirait. dk.raw = false neutralise writeBack
        // ET force dk.writeProtect (plus bas).
        const bool msaLike = looksLikeMsaHeader(raw);
        // MÊME raisonnement pour le .dim : depuis que decodeDim exige une géométrie
        // d'en-tête cohérente, il REJETTE des images qu'Hatari monte sans broncher
        // (padding en queue, champs à 0 — il n'inspecte jamais ces champs). Sans cette
        // garde, une telle image repartait en « .st brut INSCRIPTIBLE » et writeBack
        // recopiait les secteurs invités par-dessus le fichier .dim source, DÉCALÉS de
        // 32 octets : destruction silencieuse du fichier de l'utilisateur.
        // Les QUATRE octets d'identité d'Hatari ([D] dim.c:75 @ f0736b24), pas seulement « BB » : $4242
        // est aussi l'encodage de « clr.w d2 », début plausible d'un secteur de boot —
        // sniffer 2 octets mettait des .ST parfaitement valides en LECTURE SEULE, et les
        // sauvegardes en jeu cessaient de persister sans le moindre message.
        const bool dimLike = raw.size() >= 32u + 512u && raw[0] == 0x42 && raw[1] == 0x42
                          && raw[0x03] == 0 && raw[0x0A] == 0;
        dk.image = std::move(raw);               // .st brut
        dk.raw   = !msaLike && !dimLike;
        if (msaLike)
            std::fprintf(stderr, "[FDC] %s: .msa header but decompression failed — "
                                 "mounted RAW and READ-ONLY (dubious image)\n", path.c_str());
        else if (dimLike)
            std::fprintf(stderr, "[FDC] %s: .dim header but inconsistent geometry — "
                                 "mounted RAW and READ-ONLY (dubious image)\n", path.c_str());
    }

    // Géométrie : BPB recoupé avec la taille réelle de l'image (cf. Hatari
    // Floppy_FindDiskDetails) — le BPB seul est souvent faux sur les cracks.
    refreshImageGeometry(dk);
    dk.path = path;

    // Changement de média à chaud (cf. Hatari Floppy_DriveTransitionSetState) :
    //  - échange à chaud → phase d'ÉJECTION (force WPRT le temps de la fenêtre) ;
    //  - premier montage (boot) → simple INSERTION, qui ne force PAS WPRT.
    if (sched_) {
        if (wasPresent) transitionSetState(drive, FloppyDisk::TRANS_EJECT);
        transitionSetState(drive, FloppyDisk::TRANS_INSERT);
    }

    // Write-protect auto-détecté d'après les permissions du fichier (cf. Hatari
    // floppy.c:Floppy_IsWriteProtected, mode « automatic » : le réglage et stat(),
    // JAMAIS le format d'image). Les .msa/.dim ne sont donc plus protégées du seul
    // fait de leur format : writeBack sait désormais les ré-écrire (MSA_WriteDisk /
    // en-tête .dim préservé). Auparavant ce drapeau ne bloquait pas que la recopie
    // hôte — il pilote le bit WPRT du WD1772 (updateWriteSectors/updateWriteTrack et
    // le statut type I) — si bien que sur toute .msa/.dim les sauvegardes en jeu, les
    // high-scores et les écritures depuis le bureau échouaient « disque protégé »
    // alors que la même disquette en .st fonctionnait.
    // `!dk.raw` subsiste pour le seul cas où l'on ne SAIT PAS ré-encoder : STX, ou
    // en-tête .msa/.dim reconnu mais indécodable. Y écrire détruirait le fichier.
    // Permission d'écriture du PROPRIÉTAIRE, via std::filesystem : <sys/stat.h>
    // manque hors POSIX, et sous Windows l'implémentation reflète l'attribut
    // « lecture seule » dans owner_write — même sémantique qu'Hatari.
    std::error_code stec;
    const std::filesystem::perms pm = std::filesystem::status(path, stec).permissions();
    const bool writable = !stec &&
        (pm & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
    dk.writeProtect = !dk.raw || !writable;

    // Densité déduite de la géométrie (18 spt → HD 1,44 Mo, 36 spt → ED) — sur
    // Mega STE, TOS doit accorder $FF860E à cette densité pour lire le disque.
    updateFloppyDensity(drive & 1);
    // Hatari FDC_InsertFloppy : chaque nouveau média reçoit sa propre phase,
    // même lors d'un remplacement à chaud moteur déjà lancé.
    if ((drive & 1) == driveSel_) {
        indexTime_ = 0;
        if (str_ & STR_MOTOR) indexInit();
    }
    std::fprintf(stderr, "[FDC] drive %c: %s (%zu KB, %d sectors/track, %d sides%s%s)\n",
                 drive & 1 ? 'B' : 'A', path.c_str(), dk.image.size() / 1024, dk.spt, dk.sides,
                 dk.density == 4 ? ", ED" : dk.density == 2 ? ", HD" : "",
                 dk.writeProtect ? ", write-protected" : "");
    return true;
}

void Fdc::eject(int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    const bool wasPresent = dk.present();
    dk.image.clear();
    dk.stx.reset();
    dk.stOverride.clear();
    dk.imgType = FloppyDisk::IMG_ST;
    dk.path.clear();
    // FDC_EjectFloppy coupe immédiatement l'index du lecteur éjecté.
    // Sans cela, STR expose encore le créneau de la dernière impulsion.
    if ((drive & 1) == driveSel_) indexTime_ = 0;
    // Éjection à chaud : on force WPRT pendant la fenêtre de transition (cf. Hatari
    // Floppy_DriveTransitionSetState, STATE_EJECT). Une éjection « à vide » n'arme rien.
    if (wasPresent && sched_) {
        transitionSetState(drive, FloppyDisk::TRANS_EJECT);
    }
    std::fprintf(stderr, "[FDC] drive %c ejected\n", drive & 1 ? 'B' : 'A');
}

// =============================================================================
//  Reset matériel — port de Hatari FDC_Reset ([D] fdc.c:1189 @ f0736b24) : registres WD1772,
//  machine à états, IRQ et DMA/FIFO remis au repos. Les images montées et la
//  position PHYSIQUE des têtes (headTrack) survivent, comme sur le vrai matériel.
//  [B] WD177X-00 p. 1-2 : MR réinitialise la puce et le registre d'état, rien de plus précis ;
//  CR=0, SR=1 et TR/DR gardés à chaud viennent d'Hatari. Tête : mécanique, fiche muette.
// =============================================================================
void Fdc::reset(bool cold) {
    // Un reset machine annule aussi le paquet et la phase de données ACSI en
    // vol. Le simple toggle du bit DMA 8 reste, lui, un resetCommand() limité
    // au statut (voir dmaResetFifo), conformément au comportement matériel.
    acsi_.reset();
    cr_  = 0;
    str_ = 0;
    sr_  = 1;
    statusTypeI_ = false;
    if (cold) {                    // à froid seulement : TR/DR + mot $FF8604 rémanent ([D] fdc.c:1201-1207 @ f0736b24)
        tr_ = 0;
        dr_ = 0;
        ff8604recent_ = 0;
    }
    stepDir_ = 1;

    command_      = 0;             // CMD_NULL : plus de commande en vol
    commandState_ = 0;
    commandType_  = 0;
    replaceCommandPossible_ = false;
    delayIndexPaced_ = false;
    interruptCond_ = 0;
    irqSignal_     = 0;            // efface aussi une éventuelle IRQ « forcée »
    setIntrqLine(false);           // propage vers GPIP5 (FDC_ClearIRQ)

    // Rotation : les positions d'index sont perdues (moteur coupé) et le compteur
    // de tours repart de zéro (spin-up complet à la prochaine commande).
    // [D] fdc.c:1218-1222 @ f0736b24.
    indexCounter_ = 0;
    indexTime_    = 0;

    // DMA : statut « pas d'erreur, compteur 0 », mode 0, FIFO et tampon vidés.
    dmaError_ = false;             // FDC_Reset force Status=1, à froid ET à chaud
    dmaResetFifo();
    dmaMode_ = 0;
    bufferReset();

    // Une commande datée en vol ne doit pas survivre au reset (sinon la machine à
    // états reprendrait PENDANT le boot avec des adresses DMA périmées).
    if (sched_) {
        sched_->cancel(Scheduler::FDC);
        sched_->cancel(Scheduler::FDC_INDEX);
        sched_->cancel(Scheduler::ACSI_IRQ);
    }
}

// =============================================================================
//  Sélection lecteur/face (port A du PSG) et géométrie.
// =============================================================================
int Fdc::currentSide() const {
    // Port A du PSG (registre 14), bit0 actif bas : 0 = face 1, 1 = face 0 ([D] fdc.c:1745 @ f0736b24).
    return (psg_.regs_[14] & 0x01) ? 0 : 1;
}
// Sélection lecteur : port A du PSG bit1 = A (actif bas), bit2 = B (actif bas) ([D] fdc.c:1750-1755 @ f0736b24).
// Un lecteur débranché vaut « aucun » : Hatari teste `DriveSelSignal < 0 ||
// !Enabled` à chaque endroit où le lecteur compte (fdc.c, index, TR00, statut type I).
int Fdc::selectedDrive() const {
    int d = -1;
    if ((psg_.regs_[14] & 0x02) == 0) d = 0;        // A prioritaire si les deux
    else if ((psg_.regs_[14] & 0x04) == 0) d = 1;
    return (d >= 0 && !driveEnabled_[d]) ? -1 : d;
}

void Fdc::refreshImageGeometry(FloppyDisk& dk) {
    if (dk.imgType != FloppyDisk::IMG_ST) return;
    int spt = dk.spt, sides = dk.sides;
    findDiskDetails(dk.image, spt, sides);
    if (spt   >= 1 && spt   <= 48) dk.spt   = spt;
    if (sides >= 1 && sides <= 2)  dk.sides = sides;
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
// Octets bruts d'une piste .ST ([D] fdc.c:410 @ f0736b24) : 6268 × facteur de densité déduit du nombre de
// secteurs/piste (cf. Hatari FDC_GetBytesPerTrack : ≥36 spt → ED ×4, ≥18 → HD ×2).
int Fdc::bytesPerTrack() const {
    const FloppyDisk& dk = drive_[driveSel_ < 0 ? 0 : driveSel_];
    // .stx : longueur RÉELLE de la piste sous la tête (dispatch comme Hatari
    // FDC_GetBytesPerTrack) — dk.spt n'est pas renseigné au montage STX et
    // gardait la valeur du média PRÉCÉDENT (bruit READ TRACK jusqu'à 4× trop
    // long après un hot-swap .st ED → .stx).
    if (dk.imgType == FloppyDisk::IMG_STX && dk.stx)
        return bytesPerTrackStx(dk.headTrack, side_);
    if (dk.spt >= 36) return BYTES_PER_TRACK * 4;
    if (dk.spt >= 18) return BYTES_PER_TRACK * 2;
    return BYTES_PER_TRACK;
}

// =============================================================================
//  Densité du média (cf. Hatari FDC_ComputeFloppyDensity / FDC_UpdateFloppyDensity).
//  Modèle : l'horloge reste comptée à 8 MHz, une piste HD porte 2× plus d'octets et le
//  débit MFM est 2× plus rapide (128 cyc/octet), la rotation (300 tr/min) ne change pas.
//  [D] fdc.c:133 @ f0736b24 : sur Mega STE c'est l'horloge FDC qui passe à 16 MHz ; ce facteur
//  en est l'équivalent, pas le WD1772 « resté à 8 MHz ». [B] WD177X-00 p. 1-1, 1-2 : muette
//  sur le HD (8 MHz, 250 kbit/s au plus).
// =============================================================================
int Fdc::computeFloppyDensity(const FloppyDisk& dk, int track, int side) const {
    int trackSize;
    if (dk.imgType == FloppyDisk::IMG_STX && dk.stx) {
        StxImage::Track* t = dk.stx->findTrack(track, side);
        if (!t)                       trackSize = BYTES_PER_TRACK;
        else if (t->writeReinterpreted) trackSize = t->writeMfmSize;   // piste réécrite (WRITE TRACK)
        else if (t->revolutionBytes)  trackSize = t->revolutionBytes;  // IPF : durée réelle du tour
        else if (t->pTrackImage)      trackSize = t->trackImageSize;
        else if ((t->flags & StxImage::TRACK_FLAG_SECTOR_BLOCK) == 0) trackSize = t->mfmSize / 8;
        else                          trackSize = t->mfmSize;
    } else {
        trackSize = (dk.spt >= 36) ? BYTES_PER_TRACK * 4
                  : (dk.spt >= 18) ? BYTES_PER_TRACK * 2
                  :                  BYTES_PER_TRACK;
    }
    // Marges ×1,5 / ×3 (tolèrent les variations de vitesse/mastering, cf. Hatari).
    if (trackSize > 3 * BYTES_PER_TRACK)     return 4;    // ED
    if (trackSize > BYTES_PER_TRACK * 3 / 2) return 2;    // HD
    return 1;                                             // DD
}

void Fdc::updateFloppyDensity(int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    dk.density = dk.present() ? computeFloppyDensity(dk, dk.headTrack, side_) : 1;
}

// Facteur du lecteur sélectionné ; lecteur désélectionné en cours de transfert →
// débit DD (cf. Hatari FDC_TransferByte_FdcCycles, cas DriveSelSignal < 0).
int Fdc::densityFactor() const {
    return (driveSel_ < 0) ? 1 : drive_[driveSel_].density;
}

// Durée de n octets MFM en cycles FDC : 256/densité par octet (DD 256, HD 128).
int Fdc::transferDelay(int nbBytes) const {
    return int(int64_t(nbBytes) * MFM_BYTE / densityFactor());
}

// Octet ajouté au tampon avec le timing du débit courant (cf. FDC_Buffer_Add).
void Fdc::bufferAdd(uint8_t b) {
    buf_.push_back(b);
    bufTiming_.push_back(uint16_t(transferDelay(1)));
}

// Porte de densité (cf. Hatari FDC_CanMachineHandleDensity) : SEUL le Mega STE
// (contrôleur AJAX + registre $FF860E) sait lire le HD — et encore faut-il que
// TOS ait programmé $FF860E (bits 0-1 : 0x00 = DD, 0x03 = HD) en accord avec le
// média. Toute discordance → secteur introuvable (RNF) ou LOST_DATA, comme sur
// le vrai matériel. Sur ST/STE, on accepte tout (convenance, comme Hatari).
bool Fdc::canHandleDensity() const {
    if (bus_.machine != MachineType::MegaSte) return true;
    if (driveSel_ < 0) return true;
    if (drive_[driveSel_].density == 1) return (densityMode_ & 0x03) == 0x00;
    return (densityMode_ & 0x03) == 0x03;                 // HD ou ED → mode HD requis
}

// Longueur RÉELLE d'une piste STX (octets MFM) — cf. FDC_GetBytesPerTrack_STX. La
// rotation (cyclesPerRev) en dépend → des protections mesurent la durée du tour.
int Fdc::bytesPerTrackStx(int track, int side) const {
    const FloppyDisk& dk = drive_[driveSel_ < 0 ? 0 : driveSel_];
    if (!dk.stx) return BYTES_PER_TRACK;
    StxImage::Track* t = dk.stx->findTrack(track, side);
    if (!t) return BYTES_PER_TRACK;
    int sz;
    if (t->writeReinterpreted) sz = t->writeMfmSize;            // piste réécrite (WRITE TRACK)
    else if (t->revolutionBytes) sz = t->revolutionBytes;       // IPF : durée réelle du tour
    else if (t->pTrackImage) sz = t->trackImageSize;
    else if ((t->flags & StxImage::TRACK_FLAG_SECTOR_BLOCK) == 0) sz = t->mfmSize / 8;  // MFMSize en bits
    else sz = t->mfmSize;
    // Piste vide d'une STX malformée : 0 ferait un modulo par zéro dans indexPulse
    // (rngNext() % cyclesPerRev()) et un livelock d'événements à période nulle.
    return sz > 0 ? sz : BYTES_PER_TRACK;
}

// [B] Atari, STe Schematic C300780-001 rev. A, 03/10/1989, feuille 3/6 :
// U303=8.010613 MHz, SCLK va directement à CLK du WD1772 U301, distinct de CLK8.
// https://info-coach.fr/atari/documents/general/ste/STe_Schematic_10-3-1989.pdf
// SHA-256 408a1394282157945447ede283a78b96050a256316e296e98b583a75fc1fed05.
// [D] Hatari clocks_timings.c:137-146,218-241,270-276 @ f0736b24 : bus PAL
// à 8 021 247 Hz ; même SCLK FDC sur Mega STE. Le Scheduler compte en cycles BUS.
// Arrondi par événement comme FDC_FdcCyclesToCpuCycles (fdc.c:1009-1021).
int64_t Fdc::fdcToBusCycles(int64_t cycles) const {
    if (bus_.machine == MachineType::St || bus_.machine == MachineType::MegaSt) return cycles;
    return (cycles * 8021247 + 8010613 / 2) / 8010613;
}
int64_t Fdc::busToFdcCycles(int64_t cycles) const {
    if (bus_.machine == MachineType::St || bus_.machine == MachineType::MegaSt) return cycles;
    return (cycles * 8010613 + 8021247 / 2) / 8021247;
}

// Période d'un tour : constante pour .ST, dérivée de la longueur de piste pour STX
// (au débit de la densité du média : une piste HD a 2× plus d'octets, 2× plus vite).
int64_t Fdc::cyclesPerRev() const {
    if (driveSel_ >= 0 && drive_[driveSel_].imgType == FloppyDisk::IMG_STX) {
        const int sz = bytesPerTrackStx(drive_[driveSel_].headTrack, side_);
        return fdcToBusCycles(int64_t(sz) * MFM_BYTE / densityFactor());
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
    // Densité du média sous la tête (cf. Hatari FDC_SetDriveSide → UpdateFloppyDensity) :
    // dépend de la piste/face pour les STX, du spt pour les .ST.
    if (driveSel_ >= 0) updateFloppyDensity(driveSel_);
}

// =============================================================================
//  Modèle rotationnel : impulsions d'index.
// =============================================================================
void Fdc::indexInit() {
    // Position initiale « dans le passé » (< 1 tour) déterministe mais variable
    // d'un démarrage à l'autre : reproductible pour le headless byte-exact, comme
    // Hatari_rand() côté Hatari (FDC_IndexPulse_Init, [D] fdc.c:2077 @ f0736b24).
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
    if (rev <= 0) return;
    // Rattrapage BORNÉ. Hatari fait un simple `if` ici (FDC_IndexPulse_CheckUpdate,
    // [D] fdc.c:2033 @ f0736b24) — la fonction est appelée toutes les 200-500 cycles FDC, donc au plus
    // une impulsion par appel, et la boucle y est structurellement impossible. NeoST
    // boucle, et chaque tour émet un son et peut lever une IRQ : un indexTime_ très en
    // retard sur l'horloge (save-state forgé, ou horloge recalée) demandait des milliers
    // de MILLIARDS d'itérations — gel définitif à 100 % de CPU. Au-delà de quelques
    // révolutions de retard, on se recale d'un coup : les impulsions manquées sont de
    // toute façon sans consommateur.
    const int64_t late = nowCyc() - indexTime_;
    if (late >= 8 * rev) { indexTime_ = nowCyc() - (late % rev); return; }
    while (nowCyc() - indexTime_ >= rev)
        indexIncrease(indexTime_ + rev);
}

void Fdc::indexIncrease(int64_t ipTime) {
    indexCounter_++;
    indexTime_ = ipTime;
    emitSound(FdcSound::Index);
    if (interruptCond_ & INT_COND_IP) fdcSetIrq(IRQ_INDEX);  // Force int on Index Pulse ([B] WD177X-00 p. 1-16, I2)
}

int Fdc::indexCurrentPosBytes() const {
    if (driveSel_ < 0 || indexTime_ == 0) return -1;
    const int64_t since = nowCyc() - indexTime_;
    return int(busToFdcCycles(since) * densityFactor() / MFM_BYTE); // HD : 2× plus d'octets/tour
}

// Position tête depuis l'index en CYCLES FDC (STX : BitPosition est en bits/cycles).
int Fdc::indexCurrentPosCycles() const {
    if (driveSel_ < 0 || indexTime_ == 0) return -1;
    return int(busToFdcCycles(nowCyc() - indexTime_));
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
// les annuler). ÉCART ASSUMÉ avec Hatari : les délais cadencés sur la rotation
// (spin-up, arrêt moteur, attente d'index — delayIndexPaced_) gardent leur durée
// réelle, alors qu'Hatari divise TOUT ([D] fdc.c:459 @ f0736b24 « Divide all delays »). Son
// --fastfdc « can break some programs » (ex. l'écran-titre d'Arkanoid gèle) ; le
// nôtre reste inoffensif au prix de chargements moins accélérés (ex. Dragon Ninja
// met ~16 s de plus que Hatari fastfdc à atteindre son titre, 200 ms réels/piste).
int Fdc::applyFastFdc(int fdcCycles) const {
    // ÉCART assumé avec Hatari (anti-piège) : les protections des images STX
    // MESURENT les durées (timing par octet, rotation) — un FDC accéléré les
    // casse (Stunt Car Racer : 11 bombes ; Hatari avertit lui-même que son
    // --fastfdc « can break some programs »). Une image STX montée dans le
    // lecteur sélectionné neutralise donc l'accélération, avec avertissement.
    if (fastFloppy_ && fdcCycles > FDC_FAST_FACTOR) {
        const int dr = driveSel_ < 0 ? 0 : driveSel_;
        if (drive_[dr].imgType == FloppyDisk::IMG_STX) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::fprintf(stderr, "[FDC] STX image (timing-based protections): "
                                     "fast FDC disabled for this drive\n");
            }
            return fdcCycles;
        }
        return fdcCycles / FDC_FAST_FACTOR;
    }
    return fdcCycles;
}

// [B] WD177X-00 p. 1-7 : le spin-up et l'arrêt du moteur se comptent en impulsions d'index, donc sans
// disque aucun décompte n'avance. [D] fdc.c:156-159 @ f0736b24 : attente sans fin, relevé d'un tiers.
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
//  IRQ / INTRQ (câblée sur GPIP5 + canal 7 du MFP, [D] fdc.c:2240-2242 @ f0736b24).
// =============================================================================
void Fdc::setIntrqLine(bool on) {
    static const bool fdcDebug = getenv("NEOST_FDC_DEBUG") != nullptr;
    if (fdcDebug)
        std::fprintf(stderr, "[fdc-irq] @%lldms intrq=%d (sig=%02x)\n",
                     (long long)(nowCyc() / 8021), on ? 1 : 0, irqSignal_);
    // Ligne GPIP5 (polling EmuTOS via timeout_gpip) ; le canal 7 est levé par le
    // détecteur de FRONT du setter (règle AER) — pas de raise() manuel : une INTRQ
    // maintenue (commandes enchaînées) ne regénère pas d'IRQ sans front réel.
    mfp_.setFdcLine(on);
}

void Fdc::fdcSetIrq(uint8_t source) {
    const bool was = irqSignal_ != 0;
    if (source == IRQ_HDC)        irqSignal_ = IRQ_HDC;
    else if (source == IRQ_OTHER) irqSignal_ = IRQ_OTHER;
    else { irqSignal_ &= ~(IRQ_HDC | IRQ_OTHER); irqSignal_ |= source; }
    if (!was) setIntrqLine(true);
}

// [B] WD177X-00 p. 1-16 : une interruption immédiate ($D8) n'est effacée ni par une lecture d'état,
// ni par une écriture de commande, tant qu'un $D0 ne l'a pas permis. [D] fdc.c:2276-2299 @ f0736b24.
void Fdc::fdcClearIrq() {
    if (!(irqSignal_ & IRQ_FORCED)) {     // pas d'IRQ forcée → on efface
        irqSignal_ = 0;
        setIntrqLine(false);
    } else {
        irqSignal_ &= IRQ_FORCED;         // IRQ forcée : reste haute
    }
}

void Fdc::clearHdcIrq() {
    // FDC_ClearHdcIRQ : la ligne GPIP5 est partagée avec le WD1772. Ne pas
    // effacer une IRQ disquette en acquittant seulement le contrôleur ACSI.
    irqSignal_ &= ~IRQ_HDC;
    if (irqSignal_ == 0) setIntrqLine(false);
}

// =============================================================================
//  Tampon FDC↔DMA et FIFO 16 octets.
// =============================================================================
// Coût du transfert FIFO↔RAM : 4 cycles par MOT, 16 octets = 8 mots = 32 cycles
// (port de `4 * FDC_DMA_FIFO_SIZE / 2` chez Hatari).
static constexpr int kDmaFlushCycles = 4 * 16 / 2;

// Facture au CPU les cycles de bus pris par le DMA du FDC — port de
// `M68000_AddCycles_CE ( 4 * FDC_DMA_FIFO_SIZE / 2 )` (fdc.c, FDC_DMA_FIFO_Push et
// _Pull) : le transfert FIFO↔RAM prend 4 cycles par MOT, soit 32 cycles pour les
// 16 octets, et le CPU est STALLÉ pendant ce temps.
//
// ⚠ MÊME INVARIANT QUE BL3, et c'est le piège de ce chantier : ces flushs ont lieu
// depuis `Fdc::onFdcEvent`, callback de l'échéance `Scheduler::FDC` — donc HORS de
// tout `cpu.run()`. Avancer la seule horloge de Moira laisserait ces cycles
// invisibles de l'ordonnanceur, exactement le bug qui plantait Lethal Xcess. On
// crédite donc AUSSI `Scheduler::addStolenCycles`, discriminé par `Cpu68k::inRun`
// (dans le quantum, `ran` capte déjà l'avance et créditer la compterait deux fois).
void Fdc::billDmaCycles(int n) {
    if (n <= 0) return;
    // L'invariant stall CPU + crédit ordonnanceur (discriminant Cpu68k::inRun) vit
    // dans Bus::stealBusCycles (A23) — même primitive que Blitter::billCycles.
    bus_.stealBusCycles(sched_, n);
    // [B] WD177X-00 p. 1-1, 1-5, 1-13 (SHA-256 a2b26709…fe8388) : le flux
    // série et DRQ suivent CLK et le disque, pas le bus mémoire du CPU. Le vol
    // de bus est bien facturé ci-dessus, mais ne doit PAS ralentir ce flux.
}

// SONDE (NEOST_FDC_FLUSH_DIAG=1) : cycles écoulés entre deux flushs FIFO de 16 o.
// Cadence DD attendue : 4096 cycles, indépendamment des 32 cycles de bus CPU.
// L'ancien alignement sur les ~4127 cycles d'Hatari ralentissait aussi le disque.
//
// ⚠ ELLE EST APPELÉE DEPUIS LES SITES DE FLUSH, PAS DEPUIS billDmaCycles. Le
// 2026-08-26 elle y était logée, et comme `billDmaCycles` n'est appelée QUE si le
// stall D3 est en place, la mesure « avant correctif » ne mesurait RIEN : zéro
// ligne de sonde, que j'ai lue comme « aucun flush régulier » et qui m'a fait
// conclure à tort que l'ancrage doublait le débit DMA. Un banc de comparaison doit
// être ACTIF DES DEUX CÔTÉS de ce qu'il compare.
//
// La moyenne est restreinte au régime de RAFALE (intervalle < 8000 cyc) : sur tous
// les flushs elle est polluée par les seeks et les silences entre commandes, et
// donne ~19000 cyc/flush — sans rapport avec un débit de rafale.
void Fdc::noteFifoFlush() {
    static const bool diag = std::getenv("NEOST_FDC_FLUSH_DIAG") != nullptr;
    if (!diag || !sched_) return;
    static int64_t prev = -1, sum = 0; static long cnt = 0;
    const int64_t now = sched_->liveNow();
    if (prev >= 0) {
        const int64_t d = now - prev;
        if (d > 0 && d < 8000) { sum += d; ++cnt;
            if (cnt % 500 == 0)
                std::fprintf(stderr, "[FDCFLUSH] %ld flushs en rafale, moyenne %.1f cyc/flush\n",
                             cnt, double(sum) / double(cnt)); }
    }
    prev = now;
}

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
    noteFifoFlush();
    billDmaCycles(kDmaFlushCycles);   // D3
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
        noteFifoFlush();
        billDmaCycles(kDmaFlushCycles);   // D3
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
    dmaBytesToTransfer_ = 0;        // interrompt aussi la boucle WRITE SECTOR/TRACK
    // FDC_ResetDMA conserve Status : seul un transfert suivant actualise l'erreur,
    // ou un reset matériel FDC_Reset la remet à « pas d'erreur ».
    acsi_.resetCommand();          // purge le statut ACSI (le paquet en vol continue)
}

void Fdc::writeDmaAddressByte(uint32_t addr, uint8_t value) {
    const unsigned shift = (0x0D - (addr & 0x0F)) * 4; // $09/$0B/$0D : 16/8/0
    const uint32_t old = dmaAddr_;
    uint32_t address = (old & ~(0xFFu << shift)) | (uint32_t(value) << shift);
    // FDC_DmaAddress_WriteByte : le compteur STF (ST et Mega ST) propage
    // une retenue si bit7 ou bit15 tombe, même lors d'une écriture CPU.
    // Tester AVANT la retenue et le masque ; le else-if évite de compter deux
    // fois le débordement de l'octet médian provoqué par la retenue du bas.
    if (bus_.machine == MachineType::St || bus_.machine == MachineType::MegaSt) {
        if ((old & 0x80) && !(address & 0x80)) address += 0x100;
        else if ((old & 0x8000) && !(address & 0x8000)) address += 0x10000;
    }
    dmaAddr_ = address & dmaAddressMask(bus_.ram.size());
}

uint16_t Fdc::dmaStatusWord() const {
    uint16_t s = 0;
    if (!dmaError_)            s |= 0x1;   // bit0 = pas d'erreur
    if (dmaSectorCount_ != 0) s |= 0x2;   // bit1 = compteur de secteurs ≠ 0
    // Bits 3-15 = dernier accès $FF8604 (vérifié sur STF réel, cf. Hatari FDC_DmaStatus_ReadWord).
    return uint16_t(s | (ff8604recent_ & 0xfff8));
}

void Fdc::registerWait() {
    // Hatari M68000_WaitState(4) ajoute des cycles CPU, avant l'action du
    // registre. addBusWaitCycles reçoit des cycles BUS : 4 à 8 MHz, 2 à 16 MHz.
    // Cette conversion suit la source, sans prétendre à une mesure Mega STE.
    // Les lectures d'observation hors opcode ne font pas avancer la machine.
    if (bus_.cpu && bus_.cpu->inRun())
        bus_.cpu->addBusWaitCycles(bus_.cpu->megaSte16Mhz() ? 2 : 4);
}

// =============================================================================
//  Accès « bas niveau » à l'image .ST (cf. Hatari FDC_*_ST).
// =============================================================================
uint8_t Fdc::readSectorST(uint8_t track, uint8_t sector, uint8_t side, int* pSize) {
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return readSectorStx(pSize);
    if (auto* ov = stOverrideHere()) {
        // Le champ ID trouvé sous la tête par nextSectorID — pas le premier de même
        // numéro : sur une piste à IDs dupliqués, le WD1772 lit celui qui passe
        // ([B] WD177X-00 p. 1-8, 1-10 : comparaison au prochain ID rencontré).
        const auto* s = stOverrideFound(ov);
        if (!s) return STR_RNF;
        for (uint8_t b : s->data) bufferAdd(b);
        *pSize = int(s->data.size());
        return s->deleted ? STR_RECTYPE : 0;
    }
    FloppyDisk& dk = drive_[driveSel_];
    // Face au-delà de l'image (face 2 d'une image simple face) : les données
    // n'existent pas → RNF (port de Floppy_ReadSectors, [D] floppy.c:908 @ f0736b24 Side >=
    // nSides). Sans cette garde, lsnOffset replierait sur la piste suivante.
    // Le lecteur/SR peut changer entre l'identification et le transfert : un
    // secteur absent ne doit pas devenir un offset dans la face/piste suivante
    // (Hatari Floppy_ReadSectors, garde Sector).
    if (side >= dk.sides || sector == 0 || sector > dk.spt) return STR_RNF;
    const uint64_t off = lsnOffset(track, side, sector, dk.spt, dk.sides);
    if (off + 512u <= dk.image.size()) {
        static const bool fdcDebug = getenv("NEOST_FDC_DEBUG") != nullptr;
        if (fdcDebug)
            std::fprintf(stderr, "[fdc-rd] tr=%d sr=%d side=%d off=%llu data=%02x%02x%02x%02x\n",
                         track, sector, side, (unsigned long long)off, dk.image[off], dk.image[off+1],
                         dk.image[off+2], dk.image[off+3]);
        for (int i = 0; i < 512; ++i) bufferAdd(dk.image[off + i]);
        *pSize = 512;
        return 0;
    }
    return STR_RNF;
}

uint8_t Fdc::writeSectorST(uint8_t track, uint8_t sector, uint8_t side, int size) {
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return writeSectorStx(size);
    if (auto* ov = stOverrideHere()) {
        auto* s = stOverrideFound(ov);
        if (!s) return STR_RNF;
        for (int i = 0; i < size && i < int(s->data.size()); ++i) s->data[i] = bufferReadBytePos(i);
        s->deleted = cr_ & 1;                 // bit 0 : marque $F8 ([B] WD177X-00 p. 1-10, a0)
        return 0;
    }
    FloppyDisk& dk = drive_[driveSel_];
    // Face au-delà de l'image → RNF (même garde que readSectorST, Floppy_WriteSectors).
    // Même validation CHS que Floppy_WriteSectors, même si l'offset linéaire
    // calculé resterait dans l'image après un changement de lecteur en DMA.
    if (side >= dk.sides || sector == 0 || sector > dk.spt) return STR_RNF;
    const uint64_t off = lsnOffset(track, side, sector, dk.spt, dk.sides);
    // Le transfert peut avoir engagé un ID STX de 1024 octets avant une
    // sélection live de ce lecteur ST. FDC_WriteSector_ST/Floppy_WriteSectors
    // écrivent alors un seul secteur logique (512 octets), pas le voisin.
    // Conserver le comportement borné existant pour les transferts plus courts.
    size = std::min(size, 512);
    if (size >= 0 && off + uint64_t(size) <= dk.image.size()) {
        for (int i = 0; i < size; ++i) dk.image[off + i] = bufferReadBytePos(i);
        // Hatari Floppy_Read/WriteSectors et FDC_Get* relisent le BPB à chaque
        // accès. Le cache NeoST doit donc être invalidé dès que le boot change,
        // avant le prochain secteur (y compris au sein d'une commande multiple).
        // L'offset de l'écriture courante vient encore de l'ANCIENNE géométrie.
        if (off == 0) refreshImageGeometry(dk);
        writeBack(dk, off, uint64_t(size));   // Flopwr : recopie dans le .st
        return 0;
    }
    return STR_RNF;
}

// Champ ID synthétisé (les .ST n'en ont pas) : 3×A1, FE, TR, SIDE, SR, SIZE, CRC.
// On ajoute au tampon les 6 octets utiles [TR..CRC2] (cf. FDC_ReadAddress_ST).
// [B] WD177X-00 p. 1-13 (Read Address : six octets, CRC compris), p. 1-17 (format double densité).
uint8_t Fdc::readAddressST(uint8_t track, uint8_t sector, uint8_t side) {
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return readAddressStx();
    if (auto* ov = stOverrideHere()) {
        // Octet de taille BRUT (nextID_LEN_ n'en garde que les bits 0-1).
        const auto* s = stOverrideFound(ov);
        if (!s) return STR_RNF;
        uint8_t id[10] = { 0xa1, 0xa1, 0xa1, 0xfe, s->tr, s->hd, s->sr, s->sz, 0, 0 };
        const uint16_t crc = crc16(id, 8);
        id[8] = uint8_t(crc >> 8); id[9] = uint8_t(crc);
        for (int i = 4; i < 10; ++i) bufferAdd(id[i]);
        return 0;
    }
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
    if (auto* ov = stOverrideHere()) {
        // Relecture du flux écrit, comme le WD1772 l'a enregistré : $F5 → $A1 (CRC
        // préchargé au premier), $F6 → $C2, $F7 → les deux octets de CRC calculés.
        // [B] WD177X-00 p. 1-13 (table), p. 1-15 (qui imprime FF pour F7, coquille face à la table).
        // ⚠ [B] p. 1-13, 1-15 : CHAQUE $F5 initialise le CRC, pas le seul premier d'une suite. Écart
        // non corrigé, sans effet sur les 3 $F5 exacts qu'exige la p. 1-18. Hors de ce format, ce que
        // vaut le CRC est [F] : tranché par un format à 1 ou 2 $F5 relu sur un vrai 1772.
        uint16_t crc = 0xFFFF;
        bool prevF5 = false;
        auto upd = [&](uint8_t b) {
            crc ^= uint16_t(b) << 8;
            for (int k = 0; k < 8; ++k)
                crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
        };
        for (uint8_t b : ov->raw) {
            if (bufferSize() >= bytesPerTrack()) break;
            if (b == 0xF5) { if (!prevF5) crc = 0xFFFF; upd(0xA1); bufferAdd(0xA1); prevF5 = true; continue; }
            prevF5 = false;
            if (b == 0xF7) { bufferAdd(uint8_t(crc >> 8)); bufferAdd(uint8_t(crc)); continue; }
            const uint8_t v = (b == 0xF6) ? 0xC2 : b;
            upd(v); bufferAdd(v);
        }
        while (bufferSize() < bytesPerTrack()) bufferAdd(0x4e);
        return 0;
    }
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
        const uint64_t off = lsnOffset(track, side, sec, dk.spt, dk.sides);
        for (int i = 0; i < 512; ++i) {
            const uint8_t v = (off + uint64_t(i) < dk.image.size()) ? dk.image[off + i] : 0;
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
// PARCOURT pour extraire les secteurs (IDAM $FE → piste/face/secteur/taille, puis DAM
// $FB/$F8 → données) et on les écrit dans l'image .ST — c'est l'approche que le TODO
// de Hatari décrit ([D] fdc.c:5521-5539 @ f0736b24, FDC_WriteTrack_ST, qui renvoie toujours « lost data »).
// Une .ST ne sait représenter qu'une géométrie STANDARD. Un format qui ne colle pas à
// celle de l'image montée (taille ≠ 512 o, secteurs hors 1..spt, IDs étrangers, nombre
// de secteurs ≠ spt) n'est PAS une erreur pour le WD1772 : LOST DATA ne signale qu'une
// DRQ non servie ([B] WD177X-00 p. 1-18, S2 ; p. 1-14, 1-15). NeoST garde alors la piste en mémoire (stOverride) et la relit
// comme le matériel ; Hatari renvoie LOST DATA pour tout WRITE TRACK .ST (défaut).
uint8_t Fdc::writeTrackBuffer() {
    if (driveSel_ < 0 || !drive_[driveSel_].present()) return STR_LOST;
    FloppyDisk& dk = drive_[driveSel_];
    if (dk.imgType == FloppyDisk::IMG_STX) return writeTrackStx();
    const int n = bufferSize();
    // Piste non représentable dans le .st → piste volatile en mémoire.
    auto nonStandard = [&]() -> uint8_t {
        return stOverrideFromBuffer(dk) ? uint8_t(0) : uint8_t(STR_LOST);
    };

    // 1re passe : extraire tous les secteurs du flux, sans rien écrire.
    struct Found { int dataPos; uint64_t offset; };
    std::vector<Found> found;
    std::vector<bool> seen(dk.spt + 1, false);
    for (int i = 0; i + 6 < n; ) {
        if (buf_[i] != 0xFE) { ++i; continue; }                // IDAM : champ d'adresse
        const uint8_t tr = buf_[i + 1], sd = buf_[i + 2], sec = buf_[i + 3], len = buf_[i + 4];
        int k = i + 5;                                         // cherche la marque de données
        while (k < n && buf_[k] != 0xFB && buf_[k] != 0xF8) ++k;
        if (k >= n || k + 1 + 512 > n) return nonStandard();   // données incomplètes
        if ((len & 3) != SECTOR_SIZE_512) return nonStandard();// seule la taille 512 o est standard
        if (sec < 1 || sec > dk.spt)      return nonStandard();// secteur hors géométrie
        if (sd >= dk.sides || tr >= tracksPerDisk(driveSel_)) return nonStandard();
        // Les IDs n'actionnent ni STEP ni la sélection de face : WRITE TRACK
        // n'écrit que sous la tête actuelle ([B] WD177X-00 p. 1-13). Des C/H étrangers ou des numéros
        // dupliqués exigent une image à flux ; les accepter en ST écraserait
        // une autre piste/face ou masquerait un secteur manquant.
        if (tr != dk.headTrack || sd != side_ || seen[sec]) return nonStandard();
        seen[sec] = true;
        found.push_back({k + 1, lsnOffset(tr, sd, sec, dk.spt, dk.sides)});
        i = k + 1 + 512;
    }
    if (int(found.size()) != dk.spt) return nonStandard();     // piste incomplète / surnuméraire

    // 2de passe : géométrie standard confirmée → écriture des secteurs.
    for (const Found& f : found) {
        const uint64_t off = f.offset;
        if (off + 512u > dk.image.size()) return STR_LOST;     // garde-fou (ne devrait pas arriver)
        for (int j = 0; j < 512; ++j) dk.image[off + j] = buf_[f.dataPos + j];
    }
    // WRITE TRACK est un transfert unique : ses offsets restent ceux validés
    // avant l'écriture. Les commandes suivantes voient ensuite le nouveau BPB.
    for (const Found& f : found)
        if (f.offset == 0) { refreshImageGeometry(dk); break; }
    for (const Found& f : found) writeBack(dk, f.offset, 512u);
    dk.stOverride.erase((dk.headTrack << 1) | side_);   // la piste redevient celle du .st
    return 0;
}

// Analyse le flux WRITE TRACK comme le relira le WD1772 (IDAM $FE → champ ID, marque
// $FB/$F8 → données de la taille annoncée, [B] WD177X-00 p. 1-5, 1-13), même règle que StxImage::reinterpretSaveTrack.
bool Fdc::stOverrideFromBuffer(FloppyDisk& dk) {
    if (dk.headTrack < 0 || dk.headTrack > 255) return false;
    FloppyDisk::StOverrideTrack t;
    const int n = bufferSize();
    t.raw.assign(buf_.begin(), buf_.begin() + n);
    // Position sur la piste RELUE : chaque $F7 y devient deux octets de CRC ([B] WD177X-00 p. 1-13).
    std::vector<int> pos(n + 1, 0);
    for (int i = 0; i < n; ++i) pos[i + 1] = pos[i] + (t.raw[i] == 0xF7 ? 2 : 1);
    for (int i = 0; i + 5 < n; ) {
        if (t.raw[i] != 0xFE) { ++i; continue; }
        FloppyDisk::StOverrideSector s;
        s.tr = t.raw[i + 1]; s.hd = t.raw[i + 2]; s.sr = t.raw[i + 3]; s.sz = t.raw[i + 4];
        s.idPos = pos[i];
        const int len = 128 << (s.sz & 3);
        int k = i + 5;
        while (k < n && t.raw[k] != 0xFB && t.raw[k] != 0xF8) ++k;
        if (k >= n || k + 1 + len > n) { ++i; continue; }
        s.deleted = t.raw[k] == 0xF8;
        s.data.assign(t.raw.begin() + k + 1, t.raw.begin() + k + 1 + len);
        t.secs.push_back(std::move(s));
        i = k + 1 + len;
    }
    static bool warned = false;
    if (!warned) {
        warned = true;
        std::fprintf(stderr, "[FDC] WRITE TRACK: non-standard track %d side %d kept in memory "
                     "(a .st image cannot store it)\n", dk.headTrack, side_);
    }
    dk.stOverride[(dk.headTrack << 1) | side_] = std::move(t);
    return true;
}

Fdc::FloppyDisk::StOverrideSector* Fdc::stOverrideFound(FloppyDisk::StOverrideTrack* ov) {
    if (ovSecIdx_ < 0 || ovSecIdx_ >= int(ov->secs.size())) return nullptr;
    return &ov->secs[ovSecIdx_];
}

Fdc::FloppyDisk::StOverrideTrack* Fdc::stOverrideHere() {
    if (driveSel_ < 0) return nullptr;
    FloppyDisk& dk = drive_[driveSel_];
    if (dk.imgType != FloppyDisk::IMG_ST || dk.stOverride.empty()) return nullptr;
    auto it = dk.stOverride.find((dk.headTrack << 1) | side_);
    return it == dk.stOverride.end() ? nullptr : &it->second;
}

// Recopie une zone modifiée de l'image en mémoire vers le fichier monté, selon son
// conteneur (cf. FloppyDisk::imgFormat) : écriture partielle pour les secteurs bruts,
// ré-encodage complet pour le .msa.
void Fdc::writeBack(FloppyDisk& dk, uint64_t off, uint64_t len) {
    if (!dk.raw || dk.path.empty() || off + len > dk.image.size()) return;
    if (!hostWriteBack_) return;    // A14 (--disk-ro) : RAM seulement, le fichier est intact

    if (dk.imgFormat == FloppyDisk::FMT_MSA) {
        // .MSA : format COMPRIMÉ par piste — aucune correspondance entre un offset de
        // secteur et une position dans le fichier. Il faut donc tout ré-encoder
        // (port de MSA_WriteDisk). Coût : une passe RLE sur ~720 Ko, négligeable
        // devant la rareté des écritures secteur, et ça préserve la sémantique
        // « write-through » que NeoST applique déjà aux .st (Hatari, lui, ne sauve
        // qu'à l'éjection — une coupure y perd la partie sauvegardée).
        std::vector<uint8_t> enc;
        // Préférer la géométrie BPB courante, comme MSA_WriteDisk. Si elle n'est
        // pas représentable (ex. 720 Ko, 9 spt, 1 face → 160 pistes), conserver
        // le découpage du conteneur monté : les mêmes octets bruts, BPB compris,
        // sont préservés sans produire une MSA que decodeMsa refuserait ensuite.
        if (!encodeMsa(dk.image, dk.spt, dk.sides, enc)
            && !encodeMsa(dk.image, dk.msaSpt, dk.msaSides, enc)) {
            std::fprintf(stderr, "[FDC] %s: cannot re-encode .msa (geometry "
                                 "%d spt x %d sides incompatible with %zu B) — "
                                 "write NOT persisted\n",
                         dk.path.c_str(), dk.spt, dk.sides, dk.image.size());
            return;
        }
        // Écriture ATOMIQUE (tmp + rename) : réécrire un .msa en place, c'est le
        // reconstruire ENTIÈREMENT. Une coupure à mi-course laisserait un fichier
        // tronqué — donc une disquette définitivement illisible, là où le .st ne
        // perdrait qu'un secteur. Même précaution que l'écriture de neost.cfg.
        const std::string tmp = dk.path + ".tmp";
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            if (!o) {                             // dossier non inscriptible : on renonce, EN LE DISANT
                std::fprintf(stderr, "[FDC] %s: cannot create tmp file — "
                                     "write NOT persisted\n", dk.path.c_str());
                return;
            }
            o.write(reinterpret_cast<const char*>(enc.data()),
                    static_cast<std::streamsize>(enc.size()));
            o.flush();
            if (!o.good()) {
                o.close(); std::remove(tmp.c_str());
                std::fprintf(stderr, "[FDC] %s: tmp write failed (disk full?) — "
                                     "write NOT persisted\n", dk.path.c_str());
                return;
            }
        }
        // ⚠ std::filesystem::rename, PAS std::rename : le rename C de la CRT Windows
        // ÉCHOUE si la destination existe — or elle existe toujours ici. Chaque
        // write-back .msa échouait donc en silence sous Windows (sauvegardes de jeu
        // perdues à l'éjection). fs::rename remplace atomiquement sur les deux OS
        // (même raison que writeConfigAtomic, main.cpp).
        std::error_code rnec;
        std::filesystem::rename(tmp, dk.path, rnec);
        if (rnec) {
            std::remove(tmp.c_str());
            std::fprintf(stderr, "[FDC] %s: cannot replace (%s) — write NOT persisted\n",
                         dk.path.c_str(), rnec.message().c_str());
        }
        return;
    }

    // .ST brut, et .DIM dont seul l'en-tête de 32 o décale les données : écriture
    // partielle in situ. L'en-tête .dim déjà présent dans le fichier est PRÉSERVÉ tel
    // quel — c'est aussi ce que fait Hatari, qui relit l'ancien en-tête pour ne pas
    // perdre ses champs non documentés ([D] dim.c:134-149 @ f0736b24). Cet en-tête du conteneur
    // reste distinct du BPB invité modifiable dans le secteur de boot.
    const uint64_t fileOff = off + (dk.imgFormat == FloppyDisk::FMT_DIM ? 32u : 0u);
    std::fstream f(dk.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) {                        // image en lecture seule / FS virtuel non inscriptible
        std::fprintf(stderr, "[FDC] %s: cannot open for writing — "
                             "sector NOT persisted\n", dk.path.c_str());
        return;
    }
    f.seekp(static_cast<std::streamoff>(fileOff));
    f.write(reinterpret_cast<const char*>(dk.image.data() + off), static_cast<std::streamsize>(len));
    // Écriture in situ (pas d'atomicité possible sans réécrire tout le fichier) : on
    // ne peut plus annuler un secteur déchiré, mais on PRÉVIENT au lieu d'échouer en
    // silence — un disque plein laissait sinon une sauvegarde à moitié écrite sans
    // aucun signe (le .msa, lui, est atomique tmp+rename ci-dessus).
    if (!f.good())
        std::fprintf(stderr, "[FDC] %s: incomplete sector write at offset %llu "
                             "(disk full?) — image possibly corrupt\n",
                     dk.path.c_str(), static_cast<unsigned long long>(fileOff));
}

// =============================================================================
//  Chemin STX (Pasti) — port d'extern/hatari/src/floppies/stx.c (FDC_*_STX).
//  Champs ID RÉELS, statut FDC par secteur, bits fuzzy, timing variable. La
//  position angulaire vient de BitPosition (en BITS, 1 bit = 32 cycles FDC).
// =============================================================================
static constexpr int MFM_BIT = 32;   // 4 µs/bit × 8 MHz = 32 cycles FDC (FDC_DELAY_CYCLE_MFM_BIT), en DD ([B] WD177X-00 p. 1-1)

// Latence jusqu'au prochain champ ID + champs ID du secteur trouvé (stxNextSector_).
// Cf. FDC_NextSectorID_FdcCycles_STX.
//
// DENSITÉ (correction au-delà de Hatari) : BitPosition est exprimée en CELLULES DD
// dans le conteneur STX. Or `indexCurrentPosCycles` et `cyclesPerRev` suivent déjà
// le débit du média (÷ densité). Pour qu'une piste HD/ED (2×/4× plus d'octets, donc
// 2×/4× plus de bits) reste cohérente avec ce tour, on convertit bit→cycles et
// octet→cycles à la densité courante (MFM_BIT/dens, MFM_BYTE/dens). En DD (dens=1)
// les valeurs sont inchangées (32, 256) — aucune régression. Hatari multiplie par la
// constante DD brute, ce qui désaligne ses positions sur les images HD.
int Fdc::nextSectorIDStx(int* pFdcCycles) {
    const int curPos = indexCurrentPosCycles();
    if (curPos < 0) return RET_NO_DRIVE;
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t || t->sectorsCountView() == 0) return RET_NO_DRIVE;
    const std::vector<StxImage::Sector>& secs = t->sectorsView();
    const int nsec = int(secs.size());

    const int dens    = densityFactor();
    const int mfmBit  = MFM_BIT      / dens;     // cellule bit à la densité du média
    const int mfmByte = int(MFM_BYTE) / dens;    // octet MFM à la densité du média

    int i;
    for (i = 0; i < nsec; ++i)
        if (curPos < int(secs[i].bitPosition) * mfmBit - 4 * mfmByte) break;

    int delay;
    if (i == nsec) {                            // après le dernier ID → 1er secteur du tour suivant
        const int trackSize = bytesPerTrackStx(dk.headTrack, side_);
        delay = trackSize * mfmByte - curPos + int(secs[0].bitPosition) * mfmBit;
        stxNextSector_ = 0;
    } else {
        delay = int(secs[i].bitPosition) * mfmBit - curPos;
        stxNextSector_ = i;
    }

    const StxImage::Sector& sec = secs[stxNextSector_];
    nextID_TR_  = sec.idTrack;
    nextID_SR_  = sec.idSector;
    nextID_LEN_ = sec.idSize;
    // RNF + CRC tous deux posés ⇒ champ ID à CRC erroné ([B] WD177X-00 p. 1-18, S3).
    nextID_CRCOK_ = ((sec.fdcStatus & StxImage::FLAG_RNF) && (sec.fdcStatus & StxImage::FLAG_CRC)) ? 0 : 1;

    delay -= 4 * mfmByte;                        // BitPosition pointe après l'IDAM → reculer aux 3×$A1
    *pFdcCycles = delay;
    return RET_OK;
}

// Lit le secteur stxNextSector_ dans le tampon, octet par octet, avec bits FUZZY
// (aléatoire à chaque lecture) et TIMING variable. Cf. FDC_ReadSector_STX.
uint8_t Fdc::readSectorStx(int* pSize) {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t || stxNextSector_ < 0 || stxNextSector_ >= t->sectorsCountView()) return STR_RNF;
    StxImage::Sector& sec = t->sectorsView()[stxNextSector_];
    if (sec.fdcStatus & StxImage::FLAG_RNF) return STR_RNF;

    *pSize = sec.sectorSize;
    uint32_t readTime = sec.readTime;

    // Secteur réécrit par un 'write sector' → données de l'overlay, timing standard.
    const uint8_t* writeData = nullptr;
    uint8_t recordType = sec.fdcStatus & StxImage::FLAG_RECORD_TYPE;
    if (sec.saveIndex >= 0 && sec.saveIndex < int(dk.stx->saveSectors.size())) {
        const auto& save = dk.stx->saveSectors[sec.saveIndex];
        writeData = save.data.data();
        recordType = save.dataMark == 0xF8 ? StxImage::FLAG_RECORD_TYPE : 0;
        readTime = 0;
    }
    // Valeur standard : 32 µs/octet en DD, divisée par la densité du média (16 µs
    // en HD, 8 en ED) — même correction que nextSectorIDStx, sans quoi un secteur
    // HD durait deux secteurs et le suivant d'un multi-secteurs coûtait un tour.
    // Hatari (floppies/stx.c) garde la constante DD.
    if (readTime == 0) readTime = 32u * sec.sectorSize / uint32_t(densityFactor());
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

        // std::rint (demi-vers-pair, mode FE défaut) == le rint d'Hatari
        // (floppies/stx.c:1676/1682/1903) — lround (demi-loin-de-zéro) décalait
        // ±1 cyc la répartition des octets à timing variable (somme identique).
        uint16_t timing;
        if (sec.pTiming && !writeData) {                // timing spécifique par bloc de 16 o
            uint16_t tv = uint16_t((sec.pTiming[(i >> 4) * 2] << 8) + sec.pTiming[(i >> 4) * 2 + 1]);
            tv = uint16_t(tv * 32 + 28);                // 1 unité = 32 cyc + 28 cyc/bloc (Pasti.prg)
            if (i % 16 == 0) totalPrev = 0;
            const double totalCur = (double(tv) * ((i % 16) + 1)) / 16.0;
            timing = uint16_t(std::rint(totalCur - totalPrev));
            totalPrev += timing;
        } else {                                        // timing uniforme sur le secteur
            const double totalCur = (double(readTime) * (i + 1)) / sec.sectorSize;
            timing = uint16_t(std::rint(totalCur - totalPrev));
            totalPrev += timing;
        }
        bufferAddTiming(byte, timing);
    }
    // Un secteur réécrit porte un champ données neuf au CRC juste : l'erreur CRC
    // d'origine (champ données) ne survit pas à l'écriture.
    if (writeData) return recordType;
    return (sec.fdcStatus & StxImage::FLAG_CRC) | recordType;
}

// 'write sector' sur STX : stocke les données dans un overlay (relues ensuite à la
// place de l'original) et le persiste dans le fichier compagnon .wd1772 — au
// prochain montage de la STX, les écritures sont restaurées. Cf. FDC_WriteSector_STX.
uint8_t Fdc::writeSectorStx(int size) {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t || stxNextSector_ < 0 || stxNextSector_ >= t->sectorsCountView()) return STR_RNF;
    StxImage::Sector& sec = t->sectorsView()[stxNextSector_];
    if (sec.fdcStatus & StxImage::FLAG_RNF) return STR_RNF;
    // CRC seul (sans RNF) = erreur CRC du champ DONNÉES ([B] WD177X-00 p. 1-18, S3). Le WD1772 ne
    // contrôle que le CRC du champ ID ([B] p. 1-9, 1-10 ; la mention de MAME wd_fdc.cpp write_sector,
    // sans révision et non relue ici, ne pèse pas) puis écrit un champ données NEUF avec un CRC
    // juste ([B] p. 1-12, « Write CRC ») : l'écriture réussit et la relecture est saine. Hatari
    // (stx.c FDC_WriteSector_STX) refuse ce cas en renvoyant CRC — contre son propre
    // commentaire (« ID field CRC ») — et sa machine à états, qui ne teste que RNF,
    // annonçait alors un SUCCÈS sans rien écrire : écriture perdue en silence.
    // Un CRC d'ID erroné est codé RNF+CRC par Pasti, déjà refusé ci-dessus.

    if (sec.saveIndex < 0) {
        StxImage::SaveSector ss;
        ss.track = uint8_t(dk.headTrack); ss.side = side_; ss.bitPos = sec.bitPosition;
        ss.idTrack = sec.idTrack; ss.idHead = sec.idHead;       // champ ID → bloc SECT
        ss.idSector = sec.idSector; ss.idSize = sec.idSize; ss.idCrc = sec.idCrc;
        // Dimensionné sur sec.sectorSize, PAS sur `size` : `size` vient du nextID_LEN_
        // figé au début du transfert, alors que la cible est re-résolue ici — et la
        // face peut avoir changé entre-temps (refreshDriveSide() relit le PSG à chaque
        // lecture du registre de statut, ce que l'invité fait en boucle). Les relectures
        // bouclent, elles, sur sec.sectorSize : un overlay plus court se lirait hors du
        // vector. Même garde que le chemin de chargement (StxImage::loadWd1772).
        ss.data.resize(sec.sectorSize, 0);
        dk.stx->saveSectors.push_back(std::move(ss));
        sec.saveIndex = int(dk.stx->saveSectors.size()) - 1;
    }
    auto& save = dk.stx->saveSectors[sec.saveIndex];
    save.used = true;
    // L'overlay fait EXACTEMENT sec.sectorSize : c'est sur cette taille que bouclent
    // readSectorStx/readTrackStx (un overlay plus court se lirait hors du vector), et
    // c'est la seule que loadWd1772 accepte au rechargement. `size` vient du
    // nextID_LEN_ figé en début de transfert, alors que la cible est re-résolue ici et
    // que la face a pu changer entre-temps (refreshDriveSide relit le PSG à chaque
    // lecture du statut) : on borne donc la copie au lieu de dimensionner dessus.
    const int need = int(sec.sectorSize);
    if (int(save.data.size()) != need) save.data.assign(need, 0);
    for (int i = 0; i < size && i < need; ++i) save.data[i] = bufferReadBytePos(i);
    // [B] WD177X-00 p. 1-10 : a0=1 marque effacée ($F8, p. 1-13), a0=0 marque normale ($FB). CR reste
    // celui de la commande engagée (remplacements ignorés une fois le DMA parti).
    save.dataMark = (cr_ & 1) ? 0xF8 : 0xFB;
    stxPersist(dk);
    return 0;
}

// WRITE TRACK sur STX (cf. Hatari FDC_WriteTrack_STX) : on CONSERVE le flux brut
// écrit par le programme (timings ignorés) et on le persiste en bloc TRCK du
// .wd1772. Au-delà de Hatari (qui laisse la ré-interprétation en TODO), on PARSE
// aussitôt ce flux en secteurs lisibles (reinterpretSaveTrack) → les LECTURES
// suivantes voient le nouveau contenu. Le write track PRIME sur les 'write sector'
// précédents de la piste, qui sont invalidés.
uint8_t Fdc::writeTrackStx() {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t) return STR_LOST;

    if (t->saveTrackIndex < 0) {
        dk.stx->saveTracks.emplace_back();
        t->saveTrackIndex = int(dk.stx->saveTracks.size()) - 1;
    }
    StxImage::SaveTrack& st = dk.stx->saveTracks[t->saveTrackIndex];
    st.track = uint8_t(dk.headTrack);
    st.side  = side_;
    st.data.assign(buf_.begin(), buf_.end());

    // sectorsView() : sur une piste DÉJÀ réinterprétée, les overlays vivent sur
    // writeSectors — que reinterpretSaveTrack va effacer juste après. Ne parcourir que
    // t->sectors laissait donc des entrées « used » devenues inatteignables, réécrites
    // dans le .wd1772 à chaque sauvegarde (fichier qui grossit sans fin) et rejetées
    // au chargement suivant.
    for (StxImage::Sector& sec : t->sectorsView())   // invalide les 'write sector' précédents
        if (sec.saveIndex >= 0) {
            dk.stx->saveSectors[sec.saveIndex].used = false;
            sec.saveIndex = -1;
        }
    dk.stx->reinterpretSaveTrack(*t);              // flux → secteurs relus à la place de l'original
    stxPersist(dk);
    return 0;
}

// Recopie les overlays d'écriture STX dans le fichier compagnon .wd1772 (écriture
// au fil de l'eau, comme writeBack pour les .st — Hatari, lui, ne sauve qu'à
// l'éjection/sortie ; même format de fichier).
void Fdc::stxPersist(FloppyDisk& dk) {
    if (!dk.stx || dk.wd1772Path.empty()) return;
    if (!hostWriteBack_) return;    // A14 (--disk-ro) : pas de fichier compagnon .wd1772
    if (!dk.stx->saveWd1772(dk.wd1772Path)) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr, "[FDC] STX writes NOT persisted (%s unreachable)\n",
                         dk.wd1772Path.c_str());
        }
    }
}

// READ ADDRESS sur STX : renvoie le VRAI champ ID du secteur. Cf. FDC_ReadAddress_STX.
uint8_t Fdc::readAddressStx() {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(dk.headTrack, side_);
    if (!t || stxNextSector_ < 0 || stxNextSector_ >= t->sectorsCountView()) return STR_RNF;
    const StxImage::Sector& sec = t->sectorsView()[stxNextSector_];
    bufferAdd(sec.idTrack);
    bufferAdd(sec.idHead);
    bufferAdd(sec.idSector);
    bufferAdd(sec.idSize);
    bufferAdd(uint8_t(sec.idCrc >> 8));
    bufferAdd(uint8_t(sec.idCrc));
    if ((sec.fdcStatus & StxImage::FLAG_RNF) && (sec.fdcStatus & StxImage::FLAG_CRC)) return STR_CRC;
    return 0;
}

// Le dump STX contient des octets décodés, pas les cellules MFM. Sans formule
// démontrée pour SyncPos, ne fusionner que les enregistrements dont l'ancre ID
// et les données originales concordent exactement, sur strictement moins d'un
// tour. Les protections/gaps/CRC des autres enregistrements restent tels quels.
static std::vector<uint8_t> stxTrackWithSectorWrites(
        const StxImage::Track& track, const std::vector<StxImage::SaveSector>& writes) {
    std::vector<uint8_t> bytes(track.pTrackImage, track.pTrackImage + track.trackImageSize);
    const size_t length = bytes.size();
    if (track.trackImageSyncPos != 0 || length <= 10) return bytes;
    // Le parse peut écrêter un dump tronqué. Sa fin n'est alors pas l'index :
    // conserver les fusions contiguës, sans boucler sur cette coupure artificielle.
    const size_t declaredLength = size_t(track.pTrackImage[-2])
        | (size_t(track.pTrackImage[-1]) << 8);
    const bool completeDump = declaredLength == length;
    struct Patch {
        size_t mark;
        const StxImage::SaveSector* write;
        bool ambiguous = false;
    };
    std::vector<Patch> patches;
    auto byte = [&](size_t pos) { return bytes[pos % length]; };
    auto sync = [&](size_t pos, uint8_t mark) {
        return byte(pos) == 0xA1 && byte(pos + 1) == 0xA1
            && byte(pos + 2) == 0xA1 && byte(pos + 3) == mark;
    };
    for (const auto& sector : track.sectors) {
        if (sector.saveIndex < 0 || size_t(sector.saveIndex) >= writes.size()
                || sector.pFuzzy || !sector.pData || !sector.sectorSize
                || (sector.bitPosition & 7)) continue;
        const auto& write = writes[sector.saveIndex];
        if (write.data.size() != sector.sectorSize
                || (write.dataMark != 0xFB && write.dataMark != 0xF8)) continue;
        // Ne jamais ramener une ancre hors dump dans le tampon par modulo.
        const size_t id = sector.bitPosition / 8;
        if (id >= length) continue;
        const size_t start = id + length - 4;
        if (!completeDump && start % length + 10 > length) continue;
        if (!sync(start, 0xFE)) continue;
        uint8_t header[8];
        for (size_t i = 0; i < 8; ++i) header[i] = byte(start + i);
        if (header[4] != sector.idTrack || header[5] != sector.idHead
                || header[6] != sector.idSector || header[7] != sector.idSize
                || byte(start + 8) != uint8_t(sector.idCrc >> 8)
                || byte(start + 9) != uint8_t(sector.idCrc)
                || crc16(header, 8) != sector.idCrc) continue;
        size_t limit = completeDump ? start + length
            : start + (length - start % length);
        for (size_t pos = start + 10; pos < limit
                && (completeDump || pos + 4 <= limit); ++pos) {
            if (sync(pos, 0xFE)) { limit = pos; break; }
        }
        size_t dam = 0, matches = 0;
        for (size_t pos = start + 10; pos + 4 <= limit; ++pos) {
            if (sync(pos, 0xFB) || sync(pos, 0xF8)) { dam = pos; ++matches; }
        }
        const size_t size = sector.sectorSize;
        const size_t end = dam + 4 + size + 2;
        if (matches != 1 || end > limit || end - start >= length) continue;
        if (!completeDump && start % length + (end - start) > length) continue;
        std::vector<uint8_t> record(4 + size);
        for (size_t i = 0; i < record.size(); ++i) record[i] = byte(dam + i);
        if (!std::equal(record.begin() + 4, record.end(), sector.pData)) continue;
        const uint16_t stored = (uint16_t(byte(end - 2)) << 8) | byte(end - 1);
        // Secteur à erreur CRC données (CRC sans RNF) : le CRC du dump est faux par
        // construction ; l'égalité des données suffit à valider l'ancre.
        const bool dataCrcError = (sector.fdcStatus & StxImage::FLAG_CRC)
            && !(sector.fdcStatus & StxImage::FLAG_RNF);
        if (!dataCrcError && crc16(record.data(), int(record.size())) != stored) continue;
        patches.push_back({(dam + 3) % length, &write});
    }
    // Occuper aussi toutes les zones d'un candidat déjà ambigu : une collision
    // en chaîne doit invalider chaque participant, même au-delà de l'index.
    std::vector<int> owner(length, -1);
    for (size_t i = 0; i < patches.size(); ++i) {
        const size_t count = patches[i].write->data.size() + 3;
        for (size_t n = 0; n < count; ++n) {
            const size_t pos = (patches[i].mark + n) % length;
            if (owner[pos] >= 0) {
                patches[size_t(owner[pos])].ambiguous = true;
                patches[i].ambiguous = true;
            } else owner[pos] = int(i);
        }
    }
    for (const auto& patch : patches) {
        if (patch.ambiguous) continue;
        std::vector<uint8_t> record = {0xA1, 0xA1, 0xA1, patch.write->dataMark};
        record.insert(record.end(), patch.write->data.begin(), patch.write->data.end());
        const uint16_t crc = crc16(record.data(), int(record.size()));
        record.push_back(uint8_t(crc >> 8));
        record.push_back(uint8_t(crc));
        for (size_t n = 3; n < record.size(); ++n)
            bytes[(patch.mark + n - 3) % length] = record[n];
    }
    return bytes;
}

// READ TRACK sur STX : dump décodé de la piste si présent, sinon piste standard
// reconstruite à partir des secteurs. Cf. FDC_ReadTrack_STX.
uint8_t Fdc::readTrackStx(int track, int side) {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(track, side);
    if (!t) {                                            // piste absente → bruit
        for (int i = 0; i < bytesPerTrackStx(track, side); ++i) bufferAdd(uint8_t(rngNext()));
        return 0;
    }
    if (t->pTrackImage && !t->writeReinterpreted) {     // conserver la disposition originale
        const auto bytes = stxTrackWithSectorWrites(*t, dk.stx->saveSectors);
        const double readTime = 8000000.0 / 5.0;        // 1 tour à 300 tr/min
        double totalPrev = 0;
        for (int i = 0; i < t->trackImageSize; ++i) {
            const double totalCur = (readTime * (i + 1)) / t->trackImageSize;
            // Borner AVANT la conversion : `trackImageSize` vient du FICHIER et n'est
            // borné que par le haut (StxImage::clampImage rend min(taille annoncée,
            // octets restants), sans plancher). Le delta vaut 1 600 000 / taille, donc
            // il déborde uint16_t dès qu'une piste porte moins de 25 octets d'image —
            // et une conversion flottant→entier hors domaine est un comportement
            // INDÉFINI ([conv.fpint]), pas un enroulement : c'est ce que signale
            // -fsanitize=float-cast-overflow, utilisé ailleurs dans ce dépôt.
            const double raw = std::rint(totalCur - totalPrev);
            const uint16_t timing = uint16_t(std::min(raw, 65535.0));
            totalPrev += timing;
            bufferAddTiming(bytes[i], timing);
        }
        return 0;
    }
    // Pas d'image (ou piste réécrite) → reconstruire une piste standard à partir des secteurs.
    int trackSize = t->writeReinterpreted ? t->writeMfmSize : t->mfmSize;
    if (!t->writeReinterpreted && (t->flags & StxImage::TRACK_FLAG_SECTOR_BLOCK) == 0) trackSize /= 8;
    if (t->sectorsCountView() == 0) {
        for (int i = 0; i < trackSize; ++i) bufferAdd(uint8_t(rngNext()));
        return 0;
    }
    auto crcAdd = [](uint16_t& c, uint8_t b) {
        c ^= uint16_t(b) << 8;
        for (int k = 0; k < 8; ++k) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
    };
    for (int i = 0; i < GAP1; ++i) bufferAdd(0x4e);
    const std::vector<StxImage::Sector>& secs = t->sectorsView();
    for (int s = 0; s < int(secs.size()); ++s) {
        const StxImage::Sector& sec = secs[s];
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
        const bool hasWrite = sec.saveIndex >= 0 && sec.saveIndex < int(dk.stx->saveSectors.size());
        const uint8_t mark = hasWrite ? dk.stx->saveSectors[sec.saveIndex].dataMark
            : ((sec.fdcStatus & StxImage::FLAG_RECORD_TYPE) ? 0xF8 : 0xFB);
        bufferAdd(mark); crcAdd(crc, mark);
        const uint8_t* pData = sec.pData;
        if (hasWrite)
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
    // Densité du média ≠ mode $FF860E (Mega STE) → aucun champ ID lisible : la
    // commande tourne à vide et finit en RNF après 5 tours ([B] WD177X-00 p. 1-10 ; cf. Hatari
    // FDC_NextSectorID_FdcCycles_ST, retour NO_DRIVE_FLOPPY, [D] fdc.c:5190-5191 @ f0736b24).
    if (!canHandleDensity()) return RET_NO_DRIVE;
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return nextSectorIDStx(pFdcCycles);
    const int curPos = indexCurrentPosBytes();
    if (curPos < 0) return RET_NO_DRIVE;                         // pas de lecteur/disque
    if (auto* ov = stOverrideHere()) {
        // Prochain champ ID de la piste reformatée, dans l'ordre angulaire du flux.
        if (ov->secs.empty()) return RET_NO_DRIVE;              // piste sans ID : RNF après 5 tours ([B] WD177X-00 p. 1-10)
        const FloppyDisk::StOverrideSector* next = nullptr;
        for (const auto& s : ov->secs) if (s.idPos + 1 > curPos) { next = &s; break; }
        int nbBytes;
        if (next) nbBytes = next->idPos + 1 - curPos;
        else { next = &ov->secs.front(); nbBytes = bytesPerTrack() - curPos + next->idPos + 1; }
        ovSecIdx_ = int(next - ov->secs.data());
        nextID_TR_ = next->tr; nextID_SR_ = next->sr; nextID_LEN_ = uint8_t(next->sz & 3);
        nextID_CRCOK_ = 1;
        *pFdcCycles = transferDelay(std::max(nbBytes, 1));
        return RET_OK;
    }
    FloppyDisk& dk = drive_[driveSel_];
    // PAS de test des faces de l'IMAGE ici : Hatari teste NumberOfHeads du LECTEUR
    // ([D] fdc.c:5184, 1165 @ f0736b24, = 2 par défaut — les lecteurs NeoST sont double face). Les champs
    // ID sont synthétisés depuis le BPB quelle que soit la face (FDC_GetSectorsPerTrack
    // ignore Side, [D] fdc.c:1795 @ f0736b24) ; c'est la lecture des DONNÉES qui échoue sur la face
    // absente ([D] floppy.c:908 @ f0736b24 Side >= nSides → RNF en < 1 tour, cf. readSectorST). Les
    // jeux qui sondent la face 2 (Drakkhen, Bolo) obtiennent le RNF rapide, pas le
    // timeout 5 tours de l'ancien RET_NO_DRIVE (~1 s par sonde).
    // [B] WD177X-00 p. 1-6 : le 1772 n'a pas de comparaison de face (C/S : 1773 seul).
    // ⚠ [B] p. 1-10 : une face sans aucun ID mène au RNF après 5 impulsions d'index, pas avant un
    // tour. Écart non corrigé, hérité d'Hatari : l'image ne dit pas si la face 2 du disque d'origine
    // était vierge ou formatée, et ce RNF arrive ~0,8 s trop tôt si elle était vierge.
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
    *pFdcCycles = transferDelay(nbBytes);
    return RET_OK;
}

// =============================================================================
//  Moteur, fin de commande, vérification de piste.
// =============================================================================
bool Fdc::setMotorOn(uint8_t cr) {
    bool spinUp;
    // [B] WD177X-00 p. 1-7, 1-8 : h=0 et MO bas → MO monte et 6 tours d'attente ; MO haut → aucune
    // attente, quel que soit h. La p. 1-14 (Write Track) monte MO avant de tester h ; les organigrammes
    // p. 1-7 et 1-9 ne le montent que dans la branche spin-up, muets donc pour h=1 moteur arrêté.
    // [D] fdc.c:4052-4070 @ f0736b24 : le moteur démarre dans tous les cas, comme ici.
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
        if (indexTime_ == 0) indexInit();                      // position d'index aléatoire au démarrage ([D] fdc.c:4083 @ f0736b24)
    }
    return spinUp;
}

int Fdc::cmdComplete(bool doInt) {
    updateStr(STR_BUSY, 0);                                     // BUSY tombe ([B] WD177X-00 p. 1-5)
    if (doInt) fdcSetIrq(IRQ_COMPLETE);
    command_ = CMD_MOTOR_STOP;                                  // fausse commande : arrêt du moteur
    commandState_ = RUN_MOTOR_STOP;
    return CMD_IMMEDIATE;
}

// Vérif piste type I : pour .ST la piste est toujours correcte, sauf face absente.
// [B] WD177X-00 p. 1-8 : TR comparé à la piste de l'ID, CRC vérifié. Le test de face vient
// d'Hatari ([D] fdc.c:2499-2558 @ f0736b24) ; sur 1772 seule l'absence d'ID le justifierait (p. 1-6).
bool Fdc::verifyTrack() {
    if (driveSel_ < 0 || !drive_[driveSel_].present()) return false;
    if (nextID_TR_ != tr_) return false;
    // [B] WD177X-00 p. 1-6/7, p. 1-8 : piste juste mais CRC d'ID faux → le bit CRC est posé et l'ID suivant lu ;
    // un bon ID l'efface. Touche les seules images STX.
    if (nextID_CRCOK_ == 0) { updateStr(0, STR_CRC); return false; }
    updateStr(STR_CRC, 0);
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
        if (indexCounter_ < IP_MOTOR_OFF) {                    // attend 9 tours d'inactivité ([B] WD177X-00 p. 1-7)
            fdcCycles = spinWaitDelay();
            break;
        }
        [[fallthrough]];
     case RUN_MOTOR_STOP_COMPLETE:
        indexCounter_ = 0;
        indexTime_ = 0;                                        // arrête le comptage d'index
        updateStr(STR_MOTOR, 0);                               // coupe le moteur (garde le bit spin-up, [D] fdc.c:2604 @ f0736b24)
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
        // [B] WD177X-00 p. 1-7 (organigramme, branche Restore) : $FF → TR, 0 → DR ; p. 1-8 : 255 pas au plus.
        // [D] fdc.c:2658 @ f0736b24 : TR à 255 une fois le spin-up passé, quand le remplacement n'est plus possible.
        // (La flèche SEEK du même organigramme aboutit aussi à « 0 TO DR » : coquille, la p. 1-8 dit que DR porte la piste visée.)
        // [B] p. 1-7 : DSR (0) < TR ($FF) → RESET DIRECTION : un STEP ($20-$3F) après un Restore repart vers la
        // piste 0. [D] fdc.c:1209, 2828-2830, 4185, 4209 @ f0736b24 ne touche ni DR ni le sens : la fiche prime.
        dr_ = 0;
        stepDir_ = -1;
        tr_ = 0xff;                                            // 255 tentatives max vers la piste 0
        commandState_ = RUN_RE_SEEK0_LOOP;
        [[fallthrough]];
     case RUN_RE_SEEK0_LOOP:
        // [B] WD177X-00 p. 1-8 : après 255 pas sans TR00, Seek Error n'est posé que « providing the V flag is
        // set ». [D] fdc.c:2662-2667 @ f0736b24 le pose quel que soit V : la fiche prime.
        if (tr_ == 0) {                                        // piste 0 non atteinte après 255 essais
            if (cr_ & CMD_BIT_VERIFY) updateStr(0, STR_RNF);
            updateStr(STR_TR00, 0);
            fdcCycles = cmdComplete(true);
            break;
        }
        // La tête bouge dès qu'un lecteur ACTIVÉ est sélectionné — disque monté ou
        // pas (Hatari [D] fdc.c:2670-2678 @ f0736b24 teste `Enabled`, jamais l'insertion ; les
        // lecteurs NeoST sont toujours activés). RESTORE lecteur vide atteint donc
        // la piste 0 avec TR00=1 (l'ancien gate present() décomptait 255 essais
        // sans bouger → RNF + TR00=0, ~1,5 s de délai fantôme).
        if (driveSel_ < 0 || drive_[driveSel_].headTrack != 0) {
            updateStr(STR_TR00, 0);
            tr_--;
            if (driveSel_ >= 0) {
                drive_[driveSel_].headTrack--;                // déplace la tête physique
                updateFloppyDensity(driveSel_);               // densité de la nouvelle piste (STX)
            }
            fdcCycles = STEP_RATE_MS[cr_ & 3] * 1000 * 8;
        } else {                                              // tête sur la piste 0 ([B] WD177X-00 p. 1-8 : TR ← 0)
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
     // [B] WD177X-00 p. 1-6, 1-8 : verify après le settling ; bon ID → RAZ de CRC, puis INTRQ.
     case RUN_RE_VERIFY_HEAD:
        indexCounter_ = 0;
        [[fallthrough]];
     case RUN_RE_VERIFY_NEXT:
        if (indexCounter_ >= IP_ADDRESS_ID) {                 // pas de bon champ ID après 5 tours ([B] WD177X-00 p. 1-7 ; cf. IP_ADDRESS_ID)
            updateStr(0, STR_RNF);
            commandState_ = RUN_RE_COMPLETE;
            fdcCycles = CMD_COMPLETE;
            break;
        }
        {
            int c = 0;
            const int res = (driveSel_ < 0) ? RET_NO_DRIVE : nextSectorID(&c);
            if (res == RET_OK) { fdcCycles = c + transferDelay(10); commandState_ = RUN_RE_VERIFY_CHECK; break; }
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
        // [B] WD177X-00 p. 1-7, p. 1-8 : TR comparé à DR, sens selon DSR > TR, un pas par tour de boucle
        // puis délai r1 r0 ; à l'égalité on passe au verify.
        if (tr_ == dr_) { commandState_ = RUN_SE_VERIFY; fdcCycles = CMD_IMMEDIATE; }
        else {
            stepDir_ = (dr_ < tr_) ? -1 : 1;
            tr_ = uint8_t(tr_ + stepDir_);
            fdcCycles = STEP_RATE_MS[cr_ & 3] * 1000 * 8;
            updateStr(STR_TR00, 0);
            // Tête déplacée dès qu'un lecteur activé est sélectionné, disque ou
            // pas (Hatari [D] fdc.c:2838-2862 @ f0736b24 — cf. commentaire RESTORE).
            if (driveSel_ >= 0) {
                int& ht = drive_[driveSel_].headTrack;
                if (ht == MAX_TRACK && stepDir_ == 1) {       // au-delà de la piste max ([D] fdc.c:2841-2845 @ f0736b24)
                    commandState_ = RUN_SE_VERIFY; fdcCycles = CMD_IMMEDIATE;
                } else if (ht == 0 && stepDir_ == -1) {       // butée piste 0 ([B] WD177X-00 p. 1-7 : 0 → TR, sans pas ni délai)
                    tr_ = 0; commandState_ = RUN_SE_VERIFY; fdcCycles = CMD_IMMEDIATE;
                } else {
                    ht += stepDir_;                           // déplace la tête physique
                    updateFloppyDensity(driveSel_);           // densité de la nouvelle piste (STX)
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
            if (res == RET_OK) { fdcCycles = c + transferDelay(10); commandState_ = RUN_SE_VERIFY_CHECK; break; }
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
        // [B] WD177X-00 p. 1-6 (u), p. 1-8 : TR mis à jour si u=1, sens du dernier pas ; p. 1-7 : TR ajusté
        // avant le test de la piste 0.
        if (cr_ & CMD_BIT_UPDATETRACK) tr_ = uint8_t(tr_ + stepDir_);
        fdcCycles = STEP_RATE_MS[cr_ & 3] * 1000 * 8;
        updateStr(STR_TR00, 0);
        // Tête déplacée dès qu'un lecteur activé est sélectionné, disque ou
        // pas (Hatari [D] fdc.c:3004-3021 @ f0736b24 — cf. commentaire RESTORE).
        // [D] fdc.c:3006-3007 : butée de piste max, sans délai (fiche muette, c'est le lecteur).
        // [B] WD177X-00 p. 1-7 : tête en piste 0 et sens sortant → 0 → TR, quel que soit u, sans pas ni délai.
        // [D] fdc.c:2998, 3009-3010 @ f0736b24 garde TR, voire le passe à $FF si u=1 : la fiche prime.
        if (driveSel_ >= 0) {
            int& ht = drive_[driveSel_].headTrack;
            if (ht == MAX_TRACK && stepDir_ == 1)       fdcCycles = CMD_IMMEDIATE;
            else if (ht == 0 && stepDir_ == -1)         { tr_ = 0; fdcCycles = CMD_IMMEDIATE; }
            else { ht += stepDir_; updateFloppyDensity(driveSel_); }  // densité de la nouvelle piste (STX)
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
            if (res == RET_OK) { fdcCycles = c + transferDelay(10); commandState_ = RUN_ST_VERIFY_CHECK; break; }
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
     // [B] WD177X-00 p. 1-6, p. 1-8, p. 1-9 : E=1, délai avant la recherche d'ID (15 ms sur 1772). Vaut pour
     // RS, WS, RA, RT, WT.
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
     // [B] WD177X-00 p. 1-10 : RNF après 5 impulsions d'index ; cf. IP_ADDRESS_ID.
     // [B] p. 1-8, 1-10 et [D] fdc.c:156-159 @ f0736b24 : sans index, pas de RNF, attente sans fin.
     case RUN_RS_NEXT:
        if (indexCounter_ >= IP_ADDRESS_ID) { commandState_ = RUN_RS_RNF; fdcCycles = CMD_IMMEDIATE; break; }
        {
            int c = 0;
            const int res = (driveSel_ < 0) ? RET_NO_DRIVE : nextSectorID(&c);
            if (res == RET_OK) { fdcCycles = c + transferDelay(10); commandState_ = RUN_RS_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
        }
        break;
     // [B] WD177X-00 p. 1-10 : TR, SR, puis CRC de l'ID. Pas de comparaison de face sur 1772 (p. 1-6).
     // [B] p. 1-10, p. 1-18 (S3) : TR et SR justes mais CRC d'ID faux → le bit CRC est posé, et s'il n'y a pas
     // de bon ID le statut final vaut RNF+CRC. [D] fdc.c:3249-3258 @ f0736b24 rend RNF seul : la fiche prime.
     // Idem WS_CHECK. Touche les seules images STX.
     case RUN_RS_CHECK:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        if (nextID_TR_ == tr_ && nextID_SR_ == sr_) updateStr(nextID_CRCOK_ ? STR_CRC : 0, nextID_CRCOK_ ? 0 : STR_CRC);
        if (nextID_TR_ == tr_ && nextID_SR_ == sr_ && nextID_CRCOK_) {
            commandState_ = RUN_RS_TRANSFER_START;
            fdcCycles = transferDelay(GAP3a + GAP3b + 3 + 1);  // jusqu'aux données ([B] WD177X-00 p. 1-9 : marque sous 43 octets en MFM)
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
            // bufferSize()==0 : secteur de taille 0 d'une STX malformée → la boucle
            // de transfert lirait bufTiming_/buf_ vides. Traité comme RNF.
            if ((statusTemp_ & STR_RNF) || bufferSize() == 0) { commandState_ = RUN_RS_RNF; fdcCycles = CMD_IMMEDIATE; }
            else {
                // Type d'enregistrement (« deleted data ») depuis le statut du secteur
                // (toujours 0 pour .ST, possiblement posé pour STX).
                // [B] WD177X-00 p. 1-11 : posé à la marque, avant le 1er octet, comme ici ; la p. 1-9
                // écrit « at the end of the Read operation ». Cf. en-tête.
                if (statusTemp_ & STR_RECTYPE) updateStr(0, STR_RECTYPE);
                else                            updateStr(STR_RECTYPE, 0);
                commandState_ = RUN_RS_TRANSFER_LOOP;
                fdcCycles = int(bufferReadTiming());          // délai du 1er octet (timing STX variable)
            }
        }
        break;
     // [D] fdc.c:145-152 @ f0736b24 : le DMA sert toujours DRQ, donc LOST DATA ne monte jamais sur ST.
     // [B] WD177X-00 p. 1-11 décrit le bit sans parler du DMA : fiche muette sur ce câblage.
     case RUN_RS_TRANSFER_LOOP:
        fifoPush(bufferReadByte());                           // 1 octet → FIFO DMA
        if (bufPos_ < bufferSize()) fdcCycles = int(bufferReadTiming());
        else { commandState_ = RUN_RS_CRC; fdcCycles = transferDelay(2); }  // 2 octets de CRC
        break;
     case RUN_RS_CRC:
        // CRC toujours bon pour .ST ; pour STX, une ERREUR CRC volontaire (protection)
        // est remontée dans le statut et termine la commande, même en multi-secteurs
        // ([B] WD177X-00 p. 1-9, 1-11 ; cf. Hatari READSECTORS_CRC, [D] fdc.c:3313-3319 @ f0736b24).
        if (statusTemp_ & STR_CRC) { updateStr(0, STR_CRC); fdcCycles = cmdComplete(true); }
        else                       { commandState_ = RUN_RS_MULTI; fdcCycles = CMD_IMMEDIATE; }
        break;
     case RUN_RS_MULTI:
        if (cr_ & CMD_BIT_MULTI) {                            // multi-secteurs : secteur suivant ([B] WD177X-00 p. 1-9, 1-11)
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
        // Protection en écriture testée APRÈS la montée en régime (6 index si h=0) et
        // le délai E de 15 ms, avant la recherche d'ID : [B] WD177X-00 p. 1-9, organigramme
        // type II (la p. 1-2 dit WPRT échantillonné à la réception, l'organigramme est plus
        // précis ; la mention de MAME wd_fdc.cpp, sans révision et non relue ici, ne pèse pas).
        // Hatari la teste dès le premier événement, avant le moteur ([D] fdc.c:3381-3382
        // @ f0736b24) : l'erreur tombait sans démarrage.
        // Re-testée à chaque secteur d'un multi-secteurs (retour par MOTORON ; [B] p. 1-11,
        // connecteur 4 de la p. 1-9).
        if (driveSel_ >= 0 && drive_[driveSel_].present() && drive_[driveSel_].writeProtect) {
            updateStr(0, STR_WPRT);
            fdcCycles = cmdComplete(true);
            break;
        }
        updateStr(STR_WPRT, 0);
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
            if (res == RET_OK) { fdcCycles = c + transferDelay(10); commandState_ = RUN_WS_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
        }
        break;
     case RUN_WS_CHECK:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        if (nextID_TR_ == tr_ && nextID_SR_ == sr_) updateStr(nextID_CRCOK_ ? STR_CRC : 0, nextID_CRCOK_ ? 0 : STR_CRC);
        if (nextID_TR_ == tr_ && nextID_SR_ == sr_ && nextID_CRCOK_) {
            commandState_ = RUN_WS_TRANSFER_START;
            fdcCycles = transferDelay(GAP3a + GAP3b + 3 + 1);
        } else {
            commandState_ = RUN_WS_NEXT; fdcCycles = CMD_IMMEDIATE;
        }
        break;
     // [B] WD177X-00 p. 1-9, 1-10, 1-12 : après le CRC de l'ID, 22 octets, 12 zéros, puis la marque : le
     // délai de RUN_WS_CHECK couvre ces 38 octets. p. 1-5 : taille d'après le champ longueur.
     // ⚠ [B] p. 1-10, 1-12 : DRQ non servi à temps → Lost Data, fin de commande avant écriture. Ici jamais : écart non corrigé.
     // Fiche muette sur le DMA du ST : [D] fdc.c:145-149, 5316 @ f0736b24 le dit toujours servi, des
     // zéros partant sur le disque si le compteur DMA vaut 0.
     // [F] Tranché par un Write Sector à compteur DMA nul sur un vrai ST, statut relu.
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
            fdcCycles = transferDelay(1);
        } else {
            commandState_ = RUN_WS_CRC; fdcCycles = transferDelay(2);
        }
        break;
     // [B] WD177X-00 p. 1-12, 1-13 : 2 octets de CRC, 1 octet $FF, puis INTRQ 24 µs (MFM) après le dernier CRC.
     // [D] fdc.c:3549, 433 @ f0736b24 finit ~1 µs après le CRC : la fiche prime.
     case RUN_WS_CRC:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }  // pas de lecteur → on attend (même état)
        {
            const uint8_t st = writeSectorST(uint8_t(drive_[driveSel_].headTrack), sr_, side_, bufferSize());
            if (st & STR_RNF) { commandState_ = RUN_WS_RNF; fdcCycles = CMD_IMMEDIATE; }
            else              { commandState_ = RUN_WS_MULTI; fdcCycles = 24 * 8 - CMD_COMPLETE; }
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
            if (res == RET_OK) { fdcCycles = c + transferDelay(4); commandState_ = RUN_RA_TRANSFER_START; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
        }
        break;
     case RUN_RA_TRANSFER_START:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        bufferReset();
        statusTemp_ = readAddressST(uint8_t(drive_[driveSel_].headTrack), nextID_SR_, side_);
        // RNF sans octet bufferisé (piste hors image, lecteur changé pendant le délai
        // rotationnel — éjection/échange à chaud) : bufferReadBytePos(0) lirait un
        // vector VIDE. Même garde que RUN_RS_TRANSFER_START.
        if ((statusTemp_ & STR_RNF) || bufferSize() == 0) {
            commandState_ = RUN_RA_RNF; fdcCycles = CMD_IMMEDIATE; break;
        }
        sr_ = bufferReadBytePos(0);                           // 1er octet du champ ID → registre secteur ([B] WD177X-00 p. 1-13)
        commandState_ = RUN_RA_TRANSFER_LOOP;
        fdcCycles = int(bufferReadTiming());
        break;
     case RUN_RA_TRANSFER_LOOP:
        fifoPush(bufferReadByte());
        if (bufPos_ < bufferSize()) fdcCycles = int(bufferReadTiming());
        else {
            // [B] WD177X-00 p. 1-13 : les six octets restent transférés malgré un CRC ID
            // invalide, et le bit CRC est posé (la mention de MAME wd_fdc.cpp, sans révision et non
            // relue ici, ne pèse pas). Hatari collecte ce résultat dans Status_Temp mais omet de
            // le rendre visible dans cette commande ([D] fdc.c:3724-3733, 3738-3750 @ f0736b24).
            if (statusTemp_ & STR_CRC) updateStr(0, STR_CRC);
            commandState_ = RUN_RA_COMPLETE;
            fdcCycles = CMD_COMPLETE;
        }
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
            const int64_t ni = nextIndexCycles();             // attend la prochaine impulsion d'index ([B] WD177X-00 p. 1-13)
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
        // Face inexistante OU densité non lisible ($FF860E, Mega STE) → bruit ([D] fdc.c:3855-3863 @ f0736b24).
        if ((side_ == 1 && drive_[driveSel_].sides != 2) || !canHandleDensity()) {
            for (int i = 0; i < bytesPerTrack(); ++i) bufferAdd(uint8_t(rngNext()));
        } else {
            statusTemp_ = readTrackST(uint8_t(drive_[driveSel_].headTrack), side_);
        }
        // Piste reconstruite VIDE (STX malformée, trackSize 0) : la boucle de
        // transfert lirait buf_/bufTiming_ vides → terminer directement.
        if (bufferSize() == 0) { commandState_ = RUN_RT_COMPLETE; fdcCycles = CMD_COMPLETE; break; }
        commandState_ = RUN_RT_TRANSFER_LOOP;
        fdcCycles = int(bufferReadTiming());
        break;
     // [D] fdc.c:150-152 @ f0736b24 : relevé d'un tiers sur STF, un Read Track réussi POSE Lost Data, « maybe
     // it depends on the size of the track ». Ni Hatari (fdc.c:3880-3895 @ f0736b24) ni NeoST ne le posent.
     // [B] WD177X-00 p. 1-13, 1-19 : bit prévu, sans ce cas. [F] Tranché par un Read Track sur un vrai ST.
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
        // Protection en écriture : après le délai E, AVANT l'attente de l'index
        // ([B] WD177X-00 p. 1-14, organigramme type III ; la mention de MAME, sans révision et non
        // relue ici, ne pèse pas). Hatari la teste après l'index ([D] fdc.c:3977 @ f0736b24) :
        // l'erreur arrivait jusqu'à un tour trop tard.
        // ⚠ [B] p. 1-13, 1-14 : ensuite DRQ monte, 3 octets d'attente, et sans service la commande finit
        // en Lost Data, AVANT d'attendre l'index. Ici rien de tout cela : écart non corrigé. Sur ST,
        // [D] fdc.c:145-149, 5515 @ f0736b24 dit DRQ toujours servi ; seule la date du départ change, d'au
        // plus un tour si l'index tombe dans ces 3 octets. Compteur DMA nul : [F], cf. WS.
        if (driveSel_ >= 0 && drive_[driveSel_].present() && drive_[driveSel_].writeProtect) {
            updateStr(0, STR_WPRT);
            fdcCycles = cmdComplete(true);
            break;
        }
        updateStr(STR_WPRT, 0);
        {
            const int64_t ni = nextIndexCycles();
            if (ni < 0) { fdcCycles = WAIT_NO_DRIVE; }
            else        { commandState_ = RUN_WT_INDEX; fdcCycles = int(ni); delayIndexPaced_ = true; }
        }
        break;
     case RUN_WT_INDEX:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        if (!canHandleDensity()) {            // densité ≠ mode $FF860E (Mega STE) ; [D] fdc.c:3966-3974 @ f0736b24
            updateStr(0, STR_LOST);
            fdcCycles = cmdComplete(true);
            break;
        }
        bufferReset();
        // Longueur de piste : réelle pour une STX, standard × densité pour une .ST
        // (cf. Hatari FDC_GetBytesPerTrack qui dispatche selon le type d'image).
        dmaBytesToTransfer_ = (drive_[driveSel_].imgType == FloppyDisk::IMG_STX)
                            ? bytesPerTrackStx(drive_[driveSel_].headTrack, side_)
                            : bytesPerTrack();
        commandState_ = RUN_WT_TRANSFER_LOOP;
        fdcCycles = CMD_IMMEDIATE;
        break;
     // [B] WD177X-00 p. 1-13, 1-15 : d'un index au suivant, une DRQ par octet chargé. Mais $F7 écrit DEUX
     // octets de CRC pour une seule DRQ.
     // [B] p. 1-15 : $F7 écrit deux octets de CRC pour une seule DRQ. [D] fdc.c:3996-4005 @ f0736b24 le compte
     // pour un octet, et l'adresse DMA finale d'un format 9 secteurs y dépasse d'environ 18 octets : la fiche prime.
     case RUN_WT_TRANSFER_LOOP:
        if (dmaBytesToTransfer_-- > 0) {
            const uint8_t b = fifoPull();
            bufferAdd(b);
            fdcCycles = transferDelay(1);
            if (b == 0xF7 && dmaBytesToTransfer_ > 0) { --dmaBytesToTransfer_; fdcCycles = transferDelay(2); }
        } else {
            commandState_ = RUN_WT_COMPLETE; fdcCycles = CMD_IMMEDIATE;
        }
        break;
     case RUN_WT_COMPLETE:
        // Extrait les secteurs du flux écrit ; un format non standard (géométrie
        // différente de l'image) reste en mémoire (stOverride) et ne pose rien, à la
        // différence d'Hatari qui pose LOST_DATA partout ([D] fdc.c:5521-5539 @ f0736b24).
        // LOST_DATA ne sort ici que d'un garde-fou de NeoST, pas du WD1772 ([B] p. 1-18, S2).
        if (writeTrackBuffer() & STR_LOST) updateStr(0, STR_LOST);
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
    // [B] WD177X-00 p. 1-5, p. 1-16 ; [D] fdc.c:4538-4548 @ f0736b24.
    // ⚠ [B] p. 1-16 : « The Hex D0 is the only command » qui rend effaçable l'interruption de $D8.
    // [D] fdc.c:4550-4555 @ f0736b24 efface la condition à toute commande, « not just when the new command is D0 ».
    // Essai du 2026-11-04 : la fiche à la lettre fait diverger Super Hang-On (99 % d'écart à la trame 500,
    // 0,0 % sinon). Écart gardé. Tranché par : un relevé d'INTRQ après $D8 puis une autre commande.
    if ((irqSignal_ & IRQ_FORCED) && !(interruptCond_ & INT_COND_IMMEDIATE))
        irqSignal_ &= ~IRQ_FORCED;
    if (type != 4) fdcClearIrq();
    interruptCond_ = 0;

    int fdcCycles = 0;
    switch (type) {
     // [B] WD177X-00 p. 1-6 (codes et sens), p. 1-7 (organigramme : BUSY ; RAZ CRC, Seek Error, DRQ, INTRQ).
     // [D] fdc.c:427-428 @ f0736b24 pour PREPARE_TYPE_I.
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
     // [B] WD177X-00 p. 1-6, p. 1-9 (BUSY ; RAZ DRQ, Lost Data, RNF, bits 5 et 6, INTRQ) ; CRC : p. 1-16.
     // [B] p. 1-9 : le bit 6 est aussi remis à zéro en Write Sector. [D] fdc.c:4268-4269 @ f0736b24 le garde : la fiche prime.
     case 2: {                                                // READ / WRITE SECTOR
        commandType_ = 2; statusTypeI_ = false;
        if ((cr_ & 0xf0) <= 0x90) {                           // 0x80/0x90 : read sector(s)
            command_ = CMD_READSECTORS; commandState_ = RUN_RS_READDATA;
            updateStr(STR_DRQ | STR_LOST | STR_CRC | STR_RNF | STR_RECTYPE | STR_WPRT, STR_BUSY);
        } else {                                              // 0xA0/0xB0 : write sector(s)
            command_ = CMD_WRITESECTORS; commandState_ = RUN_WS_WRITEDATA;
            updateStr(STR_DRQ | STR_LOST | STR_CRC | STR_RNF | STR_RECTYPE | STR_WPRT, STR_BUSY);
        }
        fdcCycles = PREPARE_TYPE_II;
        break;
     }
     // [B] WD177X-00 p. 1-6, p. 1-14 (Write Track), p. 1-16, p. 1-19 (bit 6 nul en lecture).
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
     // [B] WD177X-00 p. 1-16 : commande en cours → BUSY tombe, le reste de l'état ne bouge pas.
     // ⚠ [B] p. 1-16 : $Dx moins de 16 µs (MFM) avant une nouvelle commande est annulé ; arrêt à la fin
     // d'une micro-instruction. Rien de cela ici, ni chez Hatari : écart non corrigé, visible des seuls
     // chargeurs enchaînant $D0 et commande en quelques instructions.
     default: {                                               // FORCE INTERRUPT (type IV)
        commandType_ = 4;
        // [B] WD177X-00 p. 1-16 : au repos, l'état reflète un type I. Moteur ON et spin-up à 0 : fiche
        // muette (p. 1-7 exclut seulement Force Interrupt du flag h) ; [D] fdc.c:4381-4382 @ f0736b24,
        // « verified on STF », relevé d'un tiers.
        // [B] p. 1-16 : au repos, « the rest of the status bits are updated or cleared ». TR00, Index et WPRT se
        // rafraîchissent à la lecture ; RNF et CRC tombent ici. [D] Hatari garde RNF : la fiche prime.
        if (!(str_ & STR_BUSY)) {                             // FDC inactif → statut type I, moteur ON
            statusTypeI_ = true;
            updateStr(STR_SPINUP | STR_RNF | STR_CRC, STR_MOTOR);
        }
        interruptCond_ = cr_ & 0x0f;
        // [B] WD177X-00 p. 1-16 : I3 → INTRQ immédiate ; I3-I0 à zéro → aucune interruption. L'effacement par
        // $D0 d'une INTRQ en attente : p. 1-5 (toute écriture de commande) ; [D] fdc.c:4389-4392 @ f0736b24.
        if (interruptCond_ & INT_COND_IMMEDIATE) fdcSetIrq(IRQ_FORCED);
        else                                     fdcClearIrq();
        fdcCycles = PREPARE_TYPE_IV + cmdComplete(false);    // BUSY tombe, moteur s'arrêtera ([B] WD177X-00 p. 1-16, p. 1-7)
        break;
     }
    }

    replaceCommandPossible_ = true;     // remplaçable pendant prepare+spinup ([D] fdc.c:4567 @ f0736b24 ; ⚠ cf. write8)
    // Le délai de préparation (type I/II/III/IV) est une commande, donc accéléré en
    // mode « FDC rapide » (jamais cadencé sur la rotation).
    if (sched_) sched_->schedule(Scheduler::FDC, nowCyc() + fdcToBusCycles(applyFastFdc(fdcCycles)));
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
    // Garde-fou atteint (machine à états coincée à délai 0) : reprogrammer à now+0
    // ré-armerait l'événement à chaque dispatch (livelock inter-événements) —
    // on force une période de respiration.
    if (guard >= 100000 && fdcCycles == 0) fdcCycles = REFRESH_INDEX;

    // Trace des TRANSITIONS d'état (NEOST_FDC_DEBUG=1) : une ligne par changement
    // de commande/sous-état — complète la trace des écritures de commande
    // (executeCommand) pour suivre spin-up, latence rotationnelle et transferts.
    static const bool fdcDebug = getenv("NEOST_FDC_DEBUG") != nullptr;
    if (fdcDebug) {
        static int lastCmd = -1, lastState = -1;
        if (command_ != lastCmd || commandState_ != lastState) {
            std::fprintf(stderr, "[fdc-st] @%lldms cmd=%d state=%d drv=%d idxTime=%lld idxCnt=%d str=%02x delay=%d\n",
                (long long)(nowCyc() / 8021), command_, commandState_, driveSel_,
                (long long)indexTime_, indexCounter_, str_, fdcCycles);
            lastCmd = command_; lastState = commandState_;
        }
    }

    if (command_ != CMD_NULL && sched_) {
        // Délai de commande/transfert → accéléré en mode « FDC rapide » ; délai cadencé
        // sur la rotation (spin-up, arrêt moteur, attente d'index) → durée réelle.
        const int delay = delayIndexPaced_ ? fdcCycles : int(fdcToBusCycles(applyFastFdc(fdcCycles)));
        // SONDE (NEOST_FDC_LATE_DIAG=1) : retard de dispatch par rapport à
        // l'échéance, distinct de la cadence du flux et du coût du DMA RAM.
        static const bool lateDiag = std::getenv("NEOST_FDC_LATE_DIAG") != nullptr;
        if (lateDiag) {
            const int64_t due = sched_->firingDue();
            if (due >= 0) {
                static int64_t sum = 0, mx = 0; static long n = 0;
                const int64_t late = nowCyc() - due;
                sum += late; if (late > mx) mx = late; ++n;
                if (n % 2000 == 0)
                    std::fprintf(stderr, "[FDCLATE] %ld dispatches, retard moyen %.1f cyc, "
                                 "max %lld (delai nominal %d)\n",
                                 n, double(sum) / double(n), (long long)mx, delay);
            }
        }
        // [B] WD177X-00 p. 1-1, 1-5, 1-13 : cadence indépendante du DMA RAM.
        // Les 32 cycles de vol de bus restent dans l'horloge CPU/ordonnanceur ;
        // les ajouter ici retardait AUSSI le prochain octet, soit 4128 au lieu
        // de 4096 cycles par bloc de 16 octets MFM. Le disque ne s'arrête pas.
        // [D] Divergence volontaire avec Hatari fdc.c:2332,2388 @ f0736b24 :
        // son réarmement conserve ce stall après compensation du retard.
        const int64_t fdcDue = sched_->firingDue();
        // nextIndexCycles() mesure depuis nowCyc(), contrairement aux périodes
        // de transfert. La réancrer sur firingDue() retranchait le retard de
        // dispatch une seconde fois et réveillait READ TRACK avant l'index.
        const int64_t base = delayIndexPaced_ || fdcDue < 0 ? nowCyc() : fdcDue;
        sched_->schedule(Scheduler::FDC,
                         base + delay);
    }
}

// =============================================================================
//  Transition de média (cf. Hatari Floppy_DriveTransitionUpdateState).
// =============================================================================
// [D] floppy.c:475-501 et floppy.h:20 @ f0736b24 (18 VBL). Fiche WD177X-00 muette : WPRT vient du lecteur.
// Floppy_DriveTransitionUpdateState compare nVBLs : les changements PAL/NTSC
// ou monochrome n'altèrent pas le nombre de VBL restant dans chaque action.
bool Fdc::transitionForceWprt(int drive) {
    FloppyDisk& dk = drive_[drive & 1];
    if (!sched_) return false;
    const int64_t now = vblCounter_;
    if (dk.transitionPhase != FloppyDisk::TRANS_NONE && now >= dk.transitionEndVbl)
        dk.transitionPhase = FloppyDisk::TRANS_NONE;
    if (dk.transitionPhase2 != FloppyDisk::TRANS_NONE && now >= dk.transitionEndVbl2)
        dk.transitionPhase2 = FloppyDisk::TRANS_NONE;
    // State2 commence à la fin de State1 ; les deux gardent leur identité
    // même quand seule State2 reste active (Floppy_DriveTransitionUpdateState).
    if (dk.transitionPhase2 != FloppyDisk::TRANS_NONE && now >= dk.transitionEndVbl)
        return dk.transitionPhase2 == FloppyDisk::TRANS_EJECT;
    return dk.transitionPhase == FloppyDisk::TRANS_EJECT;
}

void Fdc::transitionSetState(int drive, int phase) {
    if (!sched_) return;
    (void)transitionForceWprt(drive); // expirer les deux actions avant insertion
    FloppyDisk& dk = drive_[drive & 1];
    if (dk.transitionPhase == FloppyDisk::TRANS_NONE) {
        dk.transitionPhase = phase;
        dk.transitionEndVbl = vblCounter_ + kMediaChangeVbl;
        dk.transitionPhase2 = FloppyDisk::TRANS_NONE;
    } else if (dk.transitionPhase == phase) {
        dk.transitionPhase2 = FloppyDisk::TRANS_NONE;
    } else {
        dk.transitionPhase2 = phase;
        dk.transitionEndVbl2 = dk.transitionEndVbl + kMediaChangeVbl;
    }
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
            registerWait(); // FDC/HDC seulement ; lecture SCREG sans attente
            if (dmaMode_ & DMA_CSACSI) { const uint8_t v = acsi_.status(); noteFf8604(v); return v; }
            refreshDriveSide();
            // FDC_UpdateAll précède TOUS les registres WD1772, pas seulement STR.
            // L'horloge live peut franchir un index pendant l'instruction CPU,
            // avant que le Scheduler ait dispatché son prochain événement FDC.
            indexCheckUpdate();
            switch (dmaMode_ & (DMA_A1 | DMA_A0)) {
                case 0: {             // FDC_CS : registre de statut
                    // Pour un statut type I, certains bits sont mis à jour en temps réel
                    // d'après les signaux (TR00, INDEX, WPRT). Sinon, on renvoie STR tel quel.
                    // [B] WD177X-00 p. 1-18 : S2 reflète TR00 et S1 le signal IP sur type I.
                    // [D] fdc.c:4851-4853 @ f0736b24 : WPRT aussi, « contrary to the doc » selon
                    // Hatari, qui nomme Procopy ; la p. 1-18 n'en parle pas pour le 1772, la p. 1-19
                    // le dit pour le 1773. Sans lecteur, ces entrées sont au repos : [D] fdc.c:4856-4857.
                    // [B] p. 1-5, p. 1-16 : la lecture du même registre doit attendre 16 µs après
                    // une écriture, celle de BUSY 24 µs et des bits 1-7 32 µs après une commande
                    // (MFM). La fiche ne dit pas ce qu'on lit avant : ici, l'état est immédiat.
                    if (statusTypeI_) {
                        if (driveSel_ < 0) {
                            updateStr(STR_TR00 | STR_INDEX | STR_WPRT, 0);
                        } else {
                            const FloppyDisk& dk = drive_[driveSel_];
                            if (dk.headTrack == 0) updateStr(0, STR_TR00); else updateStr(STR_TR00, 0);
                            if (indexState())      updateStr(0, STR_INDEX); else updateStr(STR_INDEX, 0);
                            // [B] WD177X-00 p. 1-8 : CRC garde le verdict du verify type I, cf. verifyTrack.
                            // [D] fdc.c:4872 @ f0736b24 l'efface à chaque lecture, sans source : la fiche prime.
                            // [D] fdc.c:4875-4880 @ f0736b24 : sans disque, le lecteur se lit protégé.
                            if (!dk.present() || dk.writeProtect) updateStr(0, STR_WPRT);
                            else                                     updateStr(STR_WPRT, 0);
                            // Créneau de transition média (éjection) → force WPRT, ce que TOS
                            // lit après un Restore pour détecter le changement de disquette
                            // ([D] fdc.c:217-225 @ f0736b24, d'après un TOS 1.02 non relu ici).
                            if (transitionForceWprt(driveSel_)) updateStr(0, STR_WPRT);
                        }
                    }
                    // La lecture du statut efface l'IRQ (sauf « force interrupt immediate »).
                    // [B] WD177X-00 p. 1-5, p. 1-16 ; [D] fdc.c:4900-4909 @ f0736b24.
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
        // Statut DMA : les bits 8-15 rejouent le dernier accès $8604 (vérifié STF
        // réel — Hatari FDC_DmaStatus_ReadWord renvoie le MOT complet, [D] fdc.c:5040 @ f0736b24).
        case 0x6: return uint8_t(dmaStatusWord() >> 8);   // status, octet haut
        case 0x7: return uint8_t(dmaStatusWord());        // status, octet bas
        // Adresse DMA ($FF8609/0B/0D) : RELISIBLE — le compteur incrémente pendant le
        // transfert (cf. Hatari FDC_GetDMAAddress). Les diagnostics la relisent pour
        // vérifier le nombre d'octets transférés (sinon « DMA count error »).
        case 0x9: return uint8_t(dmaAddr_ >> 16);
        case 0xB: return uint8_t(dmaAddr_ >> 8);
        case 0xD: return uint8_t(dmaAddr_);
        // $FF860E : mot de densité (Mega STE/TT) — bit1 densité (0=DD, 1=HD),
        // bit0 fréquence FDC. TOS y écrit 0x0003 pour le mode HD.
        case 0xE:
            // Hatari accepte aussi les octets sur la densité (Mega STE câblé
            // par le Bus). Un mot ne facture qu'à son dernier octet $860F.
            if (bus_.ioAccessWidth() == 1) registerWait();
            return uint8_t(densityMode_ >> 8);
        case 0xF: registerWait(); return uint8_t(densityMode_);
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
            registerWait(); // écriture $8604 : SCREG, FDC et HDC attendent tous
            // SCREG AVANT le stockage de ff8604recent_ : Hatari retourne sans le
            // mettre à jour ([D] fdc.c:4741-4748 @ f0736b24, store à :4748 seulement après) — le
            // compteur de secteurs reste NON relisible via les bits rémanents.
            if (dmaMode_ & DMA_SCREG)  { dmaSectorCount_ = uint16_t(((dataHi_ << 8) | v) & 0xff); return; }
            ff8604recent_ = uint16_t((ff8604recent_ & 0xff00) | v);
            if (dmaMode_ & DMA_CSACSI) { writeAcsi(addr, v); return; }   // disque dur ACSI
            refreshDriveSide();
            // Même ordre que FDC_DiskController_WriteWord : émettre d'abord un
            // index échu, puis accepter/ignorer la commande ou modifier TR/SR/DR.
            // Une nouvelle commande peut effacer INTRQ, mais son front a déjà
            // été capturé par le MFP (IRQ index de la commande précédente).
            indexCheckUpdate();
            switch (dmaMode_ & (DMA_A1 | DMA_A0)) {
                case 0: {                            // FDC_CS : commande
                    // [B] WD177X-00 p. 1-5 : BUSY levé, seul un Force Interrupt est chargé.
                    // ⚠ [B] p. 1-3, p. 1-5 : aucun remplacement prévu. [D] fdc.c:161-168, 4608-4632
                    // @ f0736b24 : Hatari l'admet pendant prepare+spinup d'après un logiciel réel
                    // (Overdrive Demos), type II non testé selon lui. Écart non corrigé, voulu.
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
                // ⚠ [B] WD177X-00 p. 1-3 : TR et SR ne se chargent pas quand la puce est occupée. Ici
                // ils se chargent toujours : écart non corrigé, voulu. [D] fdc.c:4654-4656, 4681-4683
                // @ f0736b24 : « contrary to the doc », et le chargeur de Delirious Demo IV écrit SR
                // après sa commande. Tranché par une relecture de TR/SR pendant BUSY sur un vrai ST.
                case DMA_A0: tr_ = v; return;        // FDC_TR
                case DMA_A1: sr_ = v; return;        // FDC_SR
                default:     dr_ = v; return;        // FDC_DR
            }
        case 0x6: ctrlHi_ = v; return;               // control, octet haut (latch)
        case 0x7: {                                  // control, octet bas
            registerWait(); // écriture $8606 seulement ; lecture statut sans attente
            const uint16_t prev = dmaMode_;
            dmaMode_ = uint16_t((ctrlHi_ << 8) | v);
            if ((prev ^ dmaMode_) & 0x0100) dmaResetFifo();  // bascule du bit 8 → reset DMA
            // Fin de la phase « compteur de secteurs » → déclenche le transfert DMA
            // ACSI en attente (port de HDC_DmaTransfer, fdc.c:FDC_DmaModeControl).
            if ((prev & 0xC0) != 0 && (dmaMode_ & 0xC0) == 0 && acsi_.anyEnabled())
                acsiDmaTransfer();
            return;
        }
        case 0x9: case 0xB: case 0xD: writeDmaAddressByte(addr, v); return;
        // $FF860E : mot de densité (cf. read8) — seuls les bits 0-1 comptent
        // pour la porte canHandleDensity().
        case 0xE:
            if (bus_.ioAccessWidth() == 1) registerWait();
            densityMode_ = uint16_t((densityMode_ & 0x00FF) | (uint32_t(v) << 8)); return;
        case 0xF:
            registerWait();
            densityMode_ = uint16_t((densityMode_ & 0xFF00) | v); return;
        default:  return;
    }
}

// =============================================================================
//  Contrôleur ACSI (disque dur) — délégation au port de hdc.c (cf. io/Acsi.cpp).
//  Le DMA/FDC reçoit les octets de commande et orchestre le transfert RAM↔image ;
//  toute la logique « disque » (commandes SCSI, accès image) est dans Acsi.
// =============================================================================

// Réception d'un octet de commande ACSI (port de Acsi_WriteCommandByte).
void Fdc::writeAcsi(uint32_t /*addr*/, uint8_t v) {
    // Port de FDC_ClearHdcIRQ (fdc.c) : n'efface QUE la source HDC — la ligne
    // INTRQ est partagée avec le FDC, la rabaisser inconditionnellement perdrait
    // une IRQ disquette encore pendante (elle est réarmée si l'octet est accepté).
    clearHdcIrq();
    // La broche A1 de l'ACSI est câblée sur le bit de contrôle DMA_A0 (0x02) : 0 pour le
    // 1er octet du paquet (sélection cible + opcode), 1 pour les octets suivants. On
    // ignore A1 pour le 2e octet (byteCount==1), comme le vrai matériel (pilotes bogués).
    const bool a1 = (dmaMode_ & DMA_A0) != 0;
    // Boîtier de test DMA branché : il court-circuite le protocole paquet — sur le
    // PREMIER octet de commande cible 0, $10/$08 déclenchent le transfert immédiat
    // (cf. dmaFixtureTransfer). Les autres octets suivent le chemin ACSI normal.
    // L'octet de commande du boîtier encode le compte dans les bits 7-6 :
    // ((count-1)<<6) | opcode, opcode $10 = avaler / $08 = rendre (observé : $10,
    // $08 pour count=1 puis $D0 = 3<<6|$10 pour count=4). Le transfert lui-même
    // suit dmaSectorCount_, déjà programmé via $8604 en mode SCREG.
    if (dmaFixture_ && !a1 && acsi_.byteCount() != 1
        && ((v & 0x3F) == 0x10 || (v & 0x3F) == 0x08)) {
        dmaFixtureTransfer((v & 0x3F) == 0x10);
        return;
    }
    if (!a1 && acsi_.byteCount() != 1) {
        acsi_.selectTarget(uint8_t((v >> 5) & 7));        // cible = bits 7-5
        if ((v & 0x1F) != 0x1F)                           // octet ordinaire (pas marqueur ICD)
            acsi_.feedByte(uint8_t(v & 0x1F));            // opcode (1er octet ne termine jamais)
        else
            acsi_.setIcdOk();                             // marqueur ICD étendu : statut OK
    } else {
        if (acsi_.feedByte(v) && acsi_.status() == 0 && acsi_.dataLen())
            acsiDmaTransfer();                            // commande complète → transfert immédiat
    }
    // Acquittement : IRQ HDC (INTRQ/GPIP5) + statut DMA si la cible est peuplée → le
    // pilote envoie l'octet suivant ; cible vide → pas d'IRQ → « pas de disque ».
    if (acsi_.targetEnabled()) {
        // FDC_SetDMAStatus(AcsiBus.bDmaError) : dmaError_ (« erreur ») = l'erreur
        // ACSI TELLE QUELLE — le `!` inversé (jumeau du bug corrigé dans
        // acsiDmaTransfer) rapportait « erreur » au bit0 de $FF8606 après chaque
        // octet de commande accepté, y compris juste APRÈS un transfert réussi.
        dmaError_ = acsi_.dmaError();
        fdcSetIrq(IRQ_HDC);                // FDC_SetIRQ(FDC_IRQ_SOURCE_HDC) : source datée
    }
}

// Boîtier de test DMA (kit Field Service) : exécution d'une commande $10 (avale
// count×512 octets de la RAM) / $08 (les rend). Protocole décodé du test « D DMA
// Port » du MegaSTE_Diagnostic v1.5 : après l'octet de commande, le test attend
// GPIP5, vérifie que l'ADRESSE DMA a avancé de count×512 (sinon « D1 count
// error »), que le statut $8606 vaut bit0=1/bit1=0/bit2=0 — donc compteur décompté
// à ZÉRO et pas d'erreur (sinon « D3 not responding ») — puis compare les données
// rendues (sinon « D2 data mismatch »).
void Fdc::dmaFixtureTransfer(bool toFixture) {
    const bool modeWrite = (dmaMode_ & DMA_WRBIT) != 0;
    if (toFixture != modeWrite) return;              // sens DMA ≠ commande : rien ne part
    const int len = int(dmaSectorCount_) * 512;
    bus_.megaSteCacheFlushIfEnabled();               // DMA via BGACK → cache Mega STE invalidé
    const bool rangeOk = uint64_t(dmaAddr_) + uint64_t(len) <= bus_.ram.size();
    if (rangeOk && len > 0) {
        if (toFixture) {                             // RAM → boîtier
            dmaFixtureBuf_.resize(size_t(len));
            for (int i = 0; i < len; ++i) dmaFixtureBuf_[size_t(i)] = bus_.dmaRead8(dmaAddr_ + uint32_t(i));
        } else {                                     // boîtier → RAM (blocs au-delà du stock : 0)
            for (int i = 0; i < len; ++i)
                bus_.dmaWrite8(dmaAddr_ + uint32_t(i),
                               size_t(i) < dmaFixtureBuf_.size() ? dmaFixtureBuf_[size_t(i)] : uint8_t(0));
        }
    }
    dmaAddr_ = (dmaAddr_ + uint32_t(len)) & dmaAddressMask(bus_.ram.size());
    dmaSectorCount_ = 0;                             // décompté à zéro ($8606 bit1 = 0)
    dmaError_ = !rangeOk;                            // bit0 = 1 quand PAS d'erreur
    fdcSetIrq(IRQ_HDC);                              // IRQ GPIP5 de fin de transfert
}

// Port de HDC_ACSI_InterruptHandler_Update : le Scheduler consomme l'échéance
// avant ce callback. Une nouvelle commande n'annule pas cette fin différée.
void Fdc::onAcsiIrq() {
    fdcSetIrq(IRQ_HDC);
}

void Fdc::onCpuSpeedChange(bool was16Mhz, bool is16Mhz) {
    if (!sched_ || bus_.machine != MachineType::MegaSte || was16Mhz == is16Mhz) return;
    const int64_t remaining = sched_->rawCyclesUntil(Scheduler::ACSI_IRQ);
    // Ni source inactive ni échéance déjà échue : ne jamais repousser une IRQ
    // qui attend seulement le prochain dispatch. Hatari conserve les ticks CPU
    // restants lors de la bascule ; seuls les cycles bus correspondants changent.
    if (remaining <= 0) return;
    // Une demi-période à 16 MHz est arrondie au cycle bus suivant.
    const int64_t delay = is16Mhz ? (remaining + 1) / 2 : remaining * 2;
    sched_->schedule(Scheduler::ACSI_IRQ, nowCyc() + delay);
}

// Transfert DMA RAM↔image (port de Acsi_DmaTransfer). dmaMode_/dmaAddr_ sont à nous.
void Fdc::acsiDmaTransfer() {
    if ((dmaMode_ & 0xC0) != 0x00 || acsi_.dataLen() == 0) return;   // pas un DMA ACSI / rien à faire
    const bool modeWrite = (dmaMode_ & DMA_WRBIT) != 0;
    if (acsi_.isWrite() != modeWrite) return;                        // sens DMA ≠ commande
    // Compteur de secteurs DMA : la même puce que pour la disquette (fifoPush/
    // fifoPull) n'acquitte plus aucun octet à compte nul, et décompte par bloc de
    // 512 o. Compte nul → rien ne part : les données restent en attente côté
    // cible (un compte programmé ensuite puis le retour en mode DMA les débloque).
    // Hatari (hdc.c Acsi_DmaTransfer) ignore ce compteur ; le diagnostic Field
    // Service vérifie $8606 bit1 = 0 après transfert (cf. dmaFixtureTransfer).
    if (dmaSectorCount_ == 0) { dmaError_ = true; return; }
    bus_.megaSteCacheFlushIfEnabled();   // DMA disque dur via BGACK → cache Mega STE invalidé
    // Au plus count×512 octets : au-delà, le DMA cesse d'acquitter.
    const int len = std::min(acsi_.dataLen(), int(dmaSectorCount_) * 512);
    // Plage RAM invalide → erreur DMA, transfert sauté ; l'adresse avance quand
    // même et l'IRQ part (port Acsi_DmaTransfer, [D] hdc.c:1125-1160 @ f0736b24 :
    // STMemory_CheckAreaType/SafeCopy en échec → bDmaError = true).
    const bool rangeOk = uint64_t(dmaAddr_) + uint64_t(len) <= bus_.ram.size();
    if (!rangeOk) {
        acsi_.markDmaRangeError();
        std::fprintf(stderr, "[ACSI] DMA outside RAM: $%06x+%d — transfer skipped\n", dmaAddr_, len);
    } else if (acsi_.isWrite()) {                                    // RAM → image
        std::vector<uint8_t> tmp(len);
        for (int i = 0; i < len; ++i) tmp[i] = bus_.dmaRead8(dmaAddr_ + uint32_t(i));
        acsi_.writeToDisk(tmp.data(), len);
    } else {                                                         // image → RAM
        const uint8_t* src = acsi_.readBuffer();
        for (int i = 0; i < len; ++i) bus_.dmaWrite8(dmaAddr_ + uint32_t(i), src[i]);
    }
    dmaAddr_ = (dmaAddr_ + uint32_t(len)) & dmaAddressMask(bus_.ram.size());
    // Décompte par bloc de 512 o, comme fifoPush : un transfert complet ramène le
    // compteur à zéro ($8606 bit1 = 0) ; une queue < 512 o (INQUIRY…) le laisse.
    for (int left = len; left > 0 && dmaSectorCount_ > 0; ) {
        if (dmaBytesInSector_ <= 0) dmaBytesInSector_ = 512;
        const int chunk = std::min(left, int(dmaBytesInSector_));
        left -= chunk;
        dmaBytesInSector_ = int16_t(dmaBytesInSector_ - chunk);
        if (dmaBytesInSector_ <= 0) { dmaSectorCount_--; dmaBytesInSector_ = 512; }
    }
    acsi_.clearData();
    // FDC_SetDMAStatus(bDmaError) : bit0 de $8606 = 1 quand PAS d'erreur —
    // dmaError_ (« erreur ») = l'erreur ACSI telle quelle. L'ancien `!` inversé
    // rapportait une erreur DMA après chaque transfert RÉUSSI.
    dmaError_ = acsi_.dmaError() || !rangeOk;
    // Hatari hdc.c:ACSI_TRANSFER_MIN_CYCLES : la copie reste immédiate, mais
    // Idris OS attend un délai avant l'IRQ de fin. 1000 cycles CPU est
    // l'approximation fixe d'Hatari, pas une durée physique mesurée du bus ACSI.
    // Le Scheduler compte des cycles BUS à 8 MHz : 1000 cycles CPU correspondent
    // à 500 cycles bus sur Mega STE à 16 MHz. fastFloppy_ ne change pas ce délai.
    clearHdcIrq();
    const bool fastCpu = bus_.machine == MachineType::MegaSte && (bus_.megaSteCacheCtrl & 2);
    if (sched_) sched_->schedule(Scheduler::ACSI_IRQ, nowCyc() + (fastCpu ? 500 : 1000));
    else onAcsiIrq();  // contrôleur isolé sans horloge : conserver la fin synchrone
}
