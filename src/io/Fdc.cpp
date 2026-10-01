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
#include "io/DiskImageCodec.hpp"
#include "core/Cpu68k.hpp"
#include "core/Bus.hpp"
#include "core/YM2149.hpp"
#include "io/Mfp.hpp"

#include <algorithm>
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
static constexpr int64_t INDEX_PULSE_LEN  = 29680;         // 3,71 ms : durée du signal d'index —
                                                           // à l'horloge STANDARD 8 MHz du datasheet
                                                           // WD1772 (Hatari FDC_CLOCK_STANDARD,
                                                           // fdc.c:405/423 : 3710 µs × 8 = 29680),
                                                           // PAS au 8,021 MHz machine
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
                        // Plafond sur ce qui reste de la piste (msa.c:205-210 : « Limit
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
            // (msa.c:388-402) — c'est la longueur == trackBytes qui le signale au lecteur.
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

    // Format STX (Pasti, en-tête « RSY\0 ») : image disque BAS NIVEAU (pistes/secteurs
    // bruts, IDs réels, CRC, bits fuzzy, timing) qui préserve les PROTECTIONS. On la
    // PARSE (StxImage) et le FDC dispatche vers le chemin _STX (champ ID véritable,
    // statut par secteur, fuzzy/timing) au lieu du modèle .ST « secteur = offset ».
    if (raw.size() > 4 && raw[0] == 'R' && raw[1] == 'S' && raw[2] == 'Y' && raw[3] == 0) {
        auto stx = std::make_unique<StxImage>();
        if (!stx->parse(std::move(raw))) {
            std::fprintf(stderr, "[FDC] unreadable STX image: %s — not mounted.\n", path.c_str());
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
            dk.transitionDeadline = sched_->now() + transitionWindow();
        }
        dk.writeProtect = false;                 // écritures en overlay (persisté en .wd1772)
        updateFloppyDensity(drive & 1);
        // Fichier compagnon des écritures (cf. Hatari STX_FileNameToSave) : on
        // remplace l'extension .stx par .wd1772 et on recharge les overlays
        // d'une session précédente s'il existe.
        dk.wd1772Path = path;
        if (dk.wd1772Path.size() >= 4) {
            const std::string ext = dk.wd1772Path.substr(dk.wd1772Path.size() - 4);
            if (ext == ".stx" || ext == ".STX" || ext == ".Stx")
                dk.wd1772Path.resize(dk.wd1772Path.size() - 4);
        }
        dk.wd1772Path += ".wd1772";
        if (dk.stx->loadWd1772(dk.wd1772Path))
            std::fprintf(stderr, "[FDC] writes restored: %s (%zu sector(s), %zu track(s))\n",
                         dk.wd1772Path.c_str(), dk.stx->saveSectors.size(), dk.stx->saveTracks.size());
        std::fprintf(stderr, "[FDC] drive %c: %s (STX, %d tracks, %d side(s))\n",
                     drive & 1 ? 'B' : 'A', path.c_str(), tps, dk.sides);
        return true;
    }

    // Image .ST/.msa/.dim → on (re)bascule en modèle logique (annule un éventuel STX).
    dk.imgType = FloppyDisk::IMG_ST;
    dk.stx.reset();
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
        // Les QUATRE octets d'identité d'Hatari (dim.c:75), pas seulement « BB » : $4242
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
        dk.transitionDeadline = sched_->now() + transitionWindow();
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
    dk.imgType = FloppyDisk::IMG_ST;
    dk.path.clear();
    // Éjection à chaud : on force WPRT pendant la fenêtre de transition (cf. Hatari
    // Floppy_DriveTransitionSetState, STATE_EJECT). Une éjection « à vide » n'arme rien.
    if (wasPresent && sched_) {
        dk.transitionPhase    = FloppyDisk::TRANS_EJECT;
        dk.transitionDeadline = sched_->now() + transitionWindow();
    }
    std::fprintf(stderr, "[FDC] drive %c ejected\n", drive & 1 ? 'B' : 'A');
}

// =============================================================================
//  Reset matériel — port de Hatari FDC_Reset (fdc.c:1172) : registres WD1772,
//  machine à états, IRQ et DMA/FIFO remis au repos. Les images montées et la
//  position PHYSIQUE des têtes (headTrack) survivent, comme sur le vrai matériel.
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
    if (cold) {                    // à froid seulement : TR/DR + mot $FF8604 rémanent
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
    indexCounter_ = 0;
    indexTime_    = 0;

    // DMA : statut « pas d'erreur, compteur 0 », mode 0, FIFO et tampon vidés.
    dmaResetFifo();
    dmaMode_ = 0;
    dmaBytesToTransfer_ = 0;
    bufferReset();

    // Une commande datée en vol ne doit pas survivre au reset (sinon la machine à
    // états reprendrait PENDANT le boot avec des adresses DMA périmées).
    if (sched_) {
        sched_->cancel(Scheduler::FDC);
        sched_->cancel(Scheduler::FDC_INDEX);
    }
}

// =============================================================================
//  Sélection lecteur/face (port A du PSG) et géométrie.
// =============================================================================
int Fdc::currentSide() const {
    // Port A du PSG (registre 14), bit0 actif bas : 0 = face 1, 1 = face 0.
    return (psg_.regs_[14] & 0x01) ? 0 : 1;
}
// Sélection lecteur : port A du PSG bit1 = A (actif bas), bit2 = B (actif bas).
// Un lecteur débranché vaut « aucun » : Hatari teste `DriveSelSignal < 0 ||
// !Enabled` à chaque endroit où le lecteur compte (fdc.c, index, TR00, statut type I).
int Fdc::selectedDrive() const {
    int d = -1;
    if ((psg_.regs_[14] & 0x02) == 0) d = 0;        // A prioritaire si les deux
    else if ((psg_.regs_[14] & 0x04) == 0) d = 1;
    return (d >= 0 && !driveEnabled_[d]) ? -1 : d;
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
// Octets bruts d'une piste .ST : 6268 × facteur de densité déduit du nombre de
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
//  Le WD1772 reste à 8 MHz : une piste HD porte 2× plus d'octets et le débit MFM
//  est 2× plus rapide (128 cyc/octet), la rotation (300 tr/min) ne change pas.
// =============================================================================
int Fdc::computeFloppyDensity(const FloppyDisk& dk, int track, int side) const {
    int trackSize;
    if (dk.imgType == FloppyDisk::IMG_STX && dk.stx) {
        StxImage::Track* t = dk.stx->findTrack(track, side);
        if (!t)                       trackSize = BYTES_PER_TRACK;
        else if (t->writeReinterpreted) trackSize = t->writeMfmSize;   // piste réécrite (WRITE TRACK)
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
    else if (t->pTrackImage) sz = t->trackImageSize;
    else if ((t->flags & StxImage::TRACK_FLAG_SECTOR_BLOCK) == 0) sz = t->mfmSize / 8;  // MFMSize en bits
    else sz = t->mfmSize;
    // Piste vide d'une STX malformée : 0 ferait un modulo par zéro dans indexPulse
    // (rngNext() % cyclesPerRev()) et un livelock d'événements à période nulle.
    return sz > 0 ? sz : BYTES_PER_TRACK;
}

// Période d'un tour : constante pour .ST, dérivée de la longueur de piste pour STX
// (au débit de la densité du média : une piste HD a 2× plus d'octets, 2× plus vite).
int64_t Fdc::cyclesPerRev() const {
    if (driveSel_ >= 0 && drive_[driveSel_].imgType == FloppyDisk::IMG_STX) {
        const int sz = bytesPerTrackStx(drive_[driveSel_].headTrack, side_);
        return int64_t(sz) * MFM_BYTE / densityFactor();
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
    if (rev <= 0) return;
    // Rattrapage BORNÉ. Hatari fait un simple `if` ici (FDC_IndexPulse_CheckUpdate,
    // fdc.c:2016) — la fonction est appelée toutes les 200-500 cycles FDC, donc au plus
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
    if (interruptCond_ & INT_COND_IP) fdcSetIrq(IRQ_INDEX);  // Force int on Index Pulse
}

int Fdc::indexCurrentPosBytes() const {
    if (driveSel_ < 0 || indexTime_ == 0) return -1;
    const int64_t since = nowCyc() - indexTime_;
    return int(since * densityFactor() / MFM_BYTE);        // HD : 2× plus d'octets/tour
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
// les annuler). ÉCART ASSUMÉ avec Hatari : les délais cadencés sur la rotation
// (spin-up, arrêt moteur, attente d'index — delayIndexPaced_) gardent leur durée
// réelle, alors qu'Hatari divise TOUT (fdc.c:459 « Divide ALL delays »). Son
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
    // Le stall doit AUSSI décaler la prochaine échéance du FDC. Chez Hatari c'est
    // automatique : `M68000_AddCycles_CE` avance le compteur global, qui EST la base
    // de temps des échéances CycInt. Ici les deux sont distincts, donc on mémorise le
    // stall et `onFdcEvent` le reporte sur le réarmement. Sans ce report, l'ancrage
    // anti-dérive ABSORBE les 32 cycles et la cadence retombe au modèle sans stall
    // (mesuré : 4096 cyc/flush au lieu des 4127 d'Hatari).
    dmaStallPending_ += n;
}

// SONDE (NEOST_FDC_FLUSH_DIAG=1) : cycles écoulés entre deux flushs FIFO de 16 o.
// C'est LA grandeur qui mesure D3 — l'audit de 2026-08-25 l'a chiffrée à 4173
// cyc/flush pour NeoST contre 4127 pour Hatari, le modèle sans stall valant 4096.
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
    dmaError_ = false;
    acsi_.resetCommand();          // purge le statut ACSI (le paquet en vol continue)
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
    // Face au-delà de l'image (face 2 d'une image simple face) : les données
    // n'existent pas → RNF (port de Floppy_ReadSectors, floppy.c:907 Side >=
    // nSides). Sans cette garde, lsnOffset replierait sur la piste suivante.
    if (side >= dk.sides) return STR_RNF;
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
    FloppyDisk& dk = drive_[driveSel_];
    // Face au-delà de l'image → RNF (même garde que readSectorST, Floppy_WriteSectors).
    if (side >= dk.sides) return STR_RNF;
    const uint64_t off = lsnOffset(track, side, sector, dk.spt, dk.sides);
    if (size >= 0 && off + uint64_t(size) <= dk.image.size()) {
        for (int i = 0; i < size; ++i) dk.image[off + i] = bufferReadBytePos(i);
        writeBack(dk, off, uint64_t(size));   // Flopwr : recopie dans le .st
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
// de Hatari décrit (fdc.c FDC_WriteTrack_ST, qui renvoie toujours « lost data »).
// LIMITE assumée (philosophie Hatari) : une .ST ne sait représenter qu'une géométrie
// STANDARD ; un format qui ne colle pas à celle de l'image montée (taille ≠ 512 o,
// secteurs hors 1..spt, piste/face hors image, nombre de secteurs ≠ spt) est REJETÉ
// en bloc avec LOST_DATA — rien n'est écrit. Le vrai reformatage non standard
// demande une image à flux (STX/SCP).
uint8_t Fdc::writeTrackBuffer() {
    if (driveSel_ < 0 || !drive_[driveSel_].present()) return STR_LOST;
    FloppyDisk& dk = drive_[driveSel_];
    if (dk.imgType == FloppyDisk::IMG_STX) return writeTrackStx();
    const int n = bufferSize();

    // 1re passe : extraire tous les secteurs du flux, sans rien écrire.
    struct Found { uint8_t tr, sd, sec; int dataPos; };
    std::vector<Found> found;
    for (int i = 0; i + 6 < n; ) {
        if (buf_[i] != 0xFE) { ++i; continue; }                // IDAM : champ d'adresse
        const uint8_t tr = buf_[i + 1], sd = buf_[i + 2], sec = buf_[i + 3], len = buf_[i + 4];
        int k = i + 5;                                         // cherche la marque de données
        while (k < n && buf_[k] != 0xFB && buf_[k] != 0xF8) ++k;
        if (k >= n || k + 1 + 512 > n) return STR_LOST;        // données incomplètes
        if ((len & 3) != SECTOR_SIZE_512) return STR_LOST;     // seule la taille 512 o est standard
        if (sec < 1 || sec > dk.spt)      return STR_LOST;     // secteur hors géométrie
        if (sd >= dk.sides || tr >= tracksPerDisk(driveSel_)) return STR_LOST;
        found.push_back({tr, sd, sec, k + 1});
        i = k + 1 + 512;
    }
    if (int(found.size()) != dk.spt) return STR_LOST;          // piste incomplète / surnuméraire

    // 2de passe : géométrie standard confirmée → écriture des secteurs.
    for (const Found& f : found) {
        const uint64_t off = lsnOffset(f.tr, f.sd, f.sec, dk.spt, dk.sides);
        if (off + 512u > dk.image.size()) return STR_LOST;     // garde-fou (ne devrait pas arriver)
        for (int j = 0; j < 512; ++j) dk.image[off + j] = buf_[f.dataPos + j];
        writeBack(dk, off, 512u);
    }
    return 0;
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
        if (!encodeMsa(dk.image, dk.spt, dk.sides, enc)) {
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
    // perdre ses champs non documentés (dim.c:134-149) ; la géométrie, elle, ne change
    // pas en cours de session.
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
static constexpr int MFM_BIT = 32;   // 4 µs/bit × 8 MHz = 32 cycles FDC (FDC_DELAY_CYCLE_MFM_BIT), en DD

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
    // RNF + CRC tous deux posés ⇒ champ ID à CRC erroné.
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
    // On ne remonte que les bits CRC (3) et RECORD_TYPE (5) dans le statut.
    return sec.fdcStatus & (StxImage::FLAG_CRC | StxImage::FLAG_RECORD_TYPE);
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
    if (sec.fdcStatus & StxImage::FLAG_CRC) return STR_CRC;

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

// READ TRACK sur STX : image MFM brute de la piste si présente, sinon piste standard
// reconstruite à partir des secteurs. Cf. FDC_ReadTrack_STX.
uint8_t Fdc::readTrackStx(int track, int side) {
    FloppyDisk& dk = drive_[driveSel_];
    StxImage::Track* t = dk.stx->findTrack(track, side);
    if (!t) {                                            // piste absente → bruit
        for (int i = 0; i < bytesPerTrackStx(track, side); ++i) bufferAdd(uint8_t(rngNext()));
        return 0;
    }
    if (t->pTrackImage && !t->writeReinterpreted) {     // dump MFM complet de la piste d'origine
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
            bufferAddTiming(t->pTrackImage[i], timing);
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
    // Densité du média ≠ mode $FF860E (Mega STE) → aucun champ ID lisible : la
    // commande tourne à vide et finit en RNF après 5 tours (cf. Hatari
    // FDC_NextSectorID_FdcCycles_ST, retour NO_DRIVE_FLOPPY).
    if (!canHandleDensity()) return RET_NO_DRIVE;
    if (drive_[driveSel_].imgType == FloppyDisk::IMG_STX) return nextSectorIDStx(pFdcCycles);
    const int curPos = indexCurrentPosBytes();
    if (curPos < 0) return RET_NO_DRIVE;                         // pas de lecteur/disque
    FloppyDisk& dk = drive_[driveSel_];
    // PAS de test des faces de l'IMAGE ici : Hatari teste NumberOfHeads du LECTEUR
    // (fdc.c:5140, = 2 par défaut — les lecteurs NeoST sont double face). Les champs
    // ID sont synthétisés depuis le BPB quelle que soit la face (FDC_GetSectorsPerTrack
    // ignore Side, fdc.c:1778) ; c'est la lecture des DONNÉES qui échoue sur la face
    // absente (floppy.c:907 Side >= nSides → RNF en < 1 tour, cf. readSectorST). Les
    // jeux qui sondent la face 2 (Drakkhen, Bolo) obtiennent le RNF rapide, pas le
    // timeout 5 tours de l'ancien RET_NO_DRIVE (~1 s par sonde).
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
        // La tête bouge dès qu'un lecteur ACTIVÉ est sélectionné — disque monté ou
        // pas (Hatari fdc.c:2616-2625 teste `Enabled`, jamais l'insertion ; les
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
        if (tr_ == dr_) { commandState_ = RUN_SE_VERIFY; fdcCycles = CMD_IMMEDIATE; }
        else {
            stepDir_ = (dr_ < tr_) ? -1 : 1;
            tr_ = uint8_t(tr_ + stepDir_);
            fdcCycles = STEP_RATE_MS[cr_ & 3] * 1000 * 8;
            updateStr(STR_TR00, 0);
            // Tête déplacée dès qu'un lecteur activé est sélectionné, disque ou
            // pas (Hatari fdc.c:2785-2808 — cf. commentaire RESTORE).
            if (driveSel_ >= 0) {
                int& ht = drive_[driveSel_].headTrack;
                if (ht == MAX_TRACK && stepDir_ == 1) {       // au-delà de la piste max
                    commandState_ = RUN_SE_VERIFY; fdcCycles = CMD_IMMEDIATE;
                } else if (ht == 0 && stepDir_ == -1) {       // butée piste 0
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
        if (cr_ & CMD_BIT_UPDATETRACK) tr_ = uint8_t(tr_ + stepDir_);
        fdcCycles = STEP_RATE_MS[cr_ & 3] * 1000 * 8;
        updateStr(STR_TR00, 0);
        // Tête déplacée dès qu'un lecteur activé est sélectionné, disque ou
        // pas (Hatari fdc.c:2950-2966 — cf. commentaire RESTORE).
        if (driveSel_ >= 0) {
            int& ht = drive_[driveSel_].headTrack;
            if (ht == MAX_TRACK && stepDir_ == 1)       fdcCycles = CMD_IMMEDIATE;
            else if (ht == 0 && stepDir_ == -1)         fdcCycles = CMD_IMMEDIATE;
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
            if (res == RET_OK) { fdcCycles = c + transferDelay(10); commandState_ = RUN_RS_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
        }
        break;
     case RUN_RS_CHECK:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        if (nextID_TR_ == tr_ && nextID_SR_ == sr_ && nextID_CRCOK_) {
            commandState_ = RUN_RS_TRANSFER_START;
            fdcCycles = transferDelay(GAP3a + GAP3b + 3 + 1);  // jusqu'aux données
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
        else { commandState_ = RUN_RS_CRC; fdcCycles = transferDelay(2); }  // 2 octets de CRC
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
            if (res == RET_OK) { fdcCycles = c + transferDelay(10); commandState_ = RUN_WS_CHECK; break; }
            if (res == RET_NO_DRIVE) fdcCycles = WAIT_NO_DRIVE;
        }
        break;
     case RUN_WS_CHECK:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }
        if (nextID_TR_ == tr_ && nextID_SR_ == sr_ && nextID_CRCOK_) {
            commandState_ = RUN_WS_TRANSFER_START;
            fdcCycles = transferDelay(GAP3a + GAP3b + 3 + 1);
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
            fdcCycles = transferDelay(1);
        } else {
            commandState_ = RUN_WS_CRC; fdcCycles = transferDelay(2);
        }
        break;
     case RUN_WS_CRC:
        if (driveSel_ < 0) { fdcCycles = WAIT_NO_DRIVE; break; }  // pas de lecteur → on attend (même état)
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
        // Face inexistante OU densité non lisible ($FF860E, Mega STE) → bruit.
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
        if (!canHandleDensity()) {            // densité ≠ mode $FF860E (Mega STE)
            updateStr(0, STR_LOST);
            fdcCycles = cmdComplete(true);
            break;
        }
        if (drive_[driveSel_].writeProtect) {
            updateStr(0, STR_WPRT);
            fdcCycles = cmdComplete(true);
            break;
        }
        updateStr(STR_WPRT, 0);
        bufferReset();
        // Longueur de piste : réelle pour une STX, standard × densité pour une .ST
        // (cf. Hatari FDC_GetBytesPerTrack qui dispatche selon le type d'image).
        dmaBytesToTransfer_ = (drive_[driveSel_].imgType == FloppyDisk::IMG_STX)
                            ? bytesPerTrackStx(drive_[driveSel_].headTrack, side_)
                            : bytesPerTrack();
        commandState_ = RUN_WT_TRANSFER_LOOP;
        fdcCycles = CMD_IMMEDIATE;
        break;
     case RUN_WT_TRANSFER_LOOP:
        if (dmaBytesToTransfer_-- > 0) {
            bufferAdd(fifoPull());
            fdcCycles = transferDelay(1);
        } else {
            commandState_ = RUN_WT_COMPLETE; fdcCycles = CMD_IMMEDIATE;
        }
        break;
     case RUN_WT_COMPLETE:
        // Extrait les secteurs du flux écrit ; un format non standard (géométrie
        // différente de l'image) est REJETÉ avec LOST_DATA, comme Hatari.
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

    // ⚠ Remis à zéro à CHAQUE événement, y compris ceux qui ne réarment PAS : sinon le
    // report s'accumulait d'un événement à l'autre au lieu de valoir UN stall — mesuré,
    // c'est ce qui donnait 4235 cyc/flush au lieu de 4128.
    struct StallReset { int64_t& v; ~StallReset() { v = 0; } } stallReset{dmaStallPending_};
    if (command_ != CMD_NULL && sched_) {
        // Délai de commande/transfert → accéléré en mode « FDC rapide » ; délai cadencé
        // sur la rotation (spin-up, arrêt moteur, attente d'index) → durée réelle.
        const int delay = delayIndexPaced_ ? fdcCycles : applyFastFdc(fdcCycles);
        // SONDE (NEOST_FDC_LATE_DIAG=1) : de COMBIEN l'échéance FDC est-elle servie en
        // retard ? C'est la grandeur qui décide du chantier D3 : chez Hatari,
        // `-PendingCyclesOver` ne compense QUE ce dépassement-là, qui est petit
        // (dispatch cycle-exact). Si NeoST dispatche avec un retard BIEN plus grand
        // (granularité du bloc CPU), alors réancrer sur `firingDue()` ne « porte » pas
        // le mécanisme d'Hatari : ça retranche un retard qu'Hatari n'a jamais eu — ce
        // qui expliquerait le doublement du débit DMA mesuré le 2026-08-26.
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
        // FORMULATION COMPLÈTE du couple D3 (la seule jamais mesurée jusqu'ici) :
        //   échéance suivante = due + stall + delay
        // C'est exactement l'arithmétique d'Hatari, où l'ORDRE fait tout :
        // PendingCyclesOver est capturé EN TÊTE de handler (fdc.c:2332), PUIS le stall
        // M68000_AddCycles_CE avance le compteur global, PUIS le réarmement se fait
        // « maintenant − overshoot + delay » (fdc.c:2388) = due + stall + delay.
        // → l'ancrage retire le retard de DISPATCH (mesuré : 6,7 cyc/échéance, ~107
        //   par cycle de 16 octets — l'écart 4203→4096), mais PAS le stall, qui décale
        //   réellement la suite. Cadence attendue : 16×256 + 32 = 4128 ≈ 4127 (Hatari).
        // Les deux variantes partielles sont FAUSSES et ont été mesurées comme telles :
        //   due + delay            → 4096 (stall absorbé, modèle sans stall)
        //   nowCyc() + stall+delay → 4235 (ancrage neutralisé)
        // dmaStallPending_ est remis à zéro par le StallReset en tête d'événement.
        const int64_t fdcDue = sched_->firingDue();
        sched_->schedule(Scheduler::FDC,
                         (fdcDue >= 0 ? fdcDue : nowCyc()) + dmaStallPending_ + delay);
    }
}

// =============================================================================
//  Transition de média (cf. Hatari Floppy_DriveTransitionUpdateState).
// =============================================================================
// Durée d'une phase de changement de média : FLOPPY_DRIVE_TRANSITION_DELAY_VBL = 18
// VBL chez Hatari (floppy.h, « min of 16 VBLs »). Le TOS n'échantillonne WPRT qu'à
// une VBL sur 8, en alternant les lecteurs quand il en a deux (EmuTOS flopvbl, comme
// Atari TOS) : le lecteur A n'est lu que toutes les 16 VBL. L'ancienne fenêtre de
// 4 trames passait le plus souvent entre deux lectures — la disquette changée à chaud
// n'était pas vue (mesuré : TOSFC relistait l'ancien contenu après « insert A »).
int64_t Fdc::transitionWindow() const {
    return 18 * frameCycles_;
}

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
            if (dmaMode_ & DMA_CSACSI) { const uint8_t v = acsi_.status(); noteFf8604(v); return v; }
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
        // Statut DMA : les bits 8-15 rejouent le dernier accès $8604 (vérifié STF
        // réel — Hatari FDC_DmaStatus_ReadWord renvoie le MOT complet, fdc.c:4996).
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
        case 0xE: return uint8_t(densityMode_ >> 8);
        case 0xF: return uint8_t(densityMode_);
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
            // SCREG AVANT le stockage de ff8604recent_ : Hatari retourne sans le
            // mettre à jour (fdc.c:4695-4703, store à :4702 seulement après) — le
            // compteur de secteurs reste NON relisible via les bits rémanents.
            if (dmaMode_ & DMA_SCREG)  { dmaSectorCount_ = uint16_t(((dataHi_ << 8) | v) & 0xff); return; }
            ff8604recent_ = uint16_t((ff8604recent_ & 0xff00) | v);
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
            // Fin de la phase « compteur de secteurs » → déclenche le transfert DMA
            // ACSI en attente (port de HDC_DmaTransfer, fdc.c:FDC_DmaModeControl).
            if ((prev & 0xC0) != 0 && (dmaMode_ & 0xC0) == 0 && acsi_.anyEnabled())
                acsiDmaTransfer();
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
        // $FF860E : mot de densité (cf. read8) — seuls les bits 0-1 comptent
        // pour la porte canHandleDensity().
        case 0xE: densityMode_ = uint16_t((densityMode_ & 0x00FF) | (uint32_t(v) << 8)); return;
        case 0xF: densityMode_ = uint16_t((densityMode_ & 0xFF00) | v); return;
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
    irqSignal_ &= ~IRQ_HDC;
    if (irqSignal_ == 0) setIntrqLine(false);
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

// Transfert DMA RAM↔image (port de Acsi_DmaTransfer). dmaMode_/dmaAddr_ sont à nous.
void Fdc::acsiDmaTransfer() {
    if ((dmaMode_ & 0xC0) != 0x00 || acsi_.dataLen() == 0) return;   // pas un DMA ACSI / rien à faire
    const bool modeWrite = (dmaMode_ & DMA_WRBIT) != 0;
    if (acsi_.isWrite() != modeWrite) return;                        // sens DMA ≠ commande
    bus_.megaSteCacheFlushIfEnabled();   // DMA disque dur via BGACK → cache Mega STE invalidé
    const int len = acsi_.dataLen();
    // Plage RAM invalide → erreur DMA, transfert sauté ; l'adresse avance quand
    // même et l'IRQ part (port Acsi_DmaTransfer, hdc.c:1125-1160 :
    // STMemory_CheckAreaType/SafeCopy en échec → bDmaError = true).
    const bool rangeOk = uint64_t(dmaAddr_) + uint64_t(len) <= bus_.ram.size();
    if (!rangeOk) {
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
    acsi_.clearData();
    // FDC_SetDMAStatus(bDmaError) : bit0 de $8606 = 1 quand PAS d'erreur —
    // dmaError_ (« erreur ») = l'erreur ACSI telle quelle. L'ancien `!` inversé
    // rapportait une erreur DMA après chaque transfert RÉUSSI.
    dmaError_ = acsi_.dmaError() || !rangeOk;
    fdcSetIrq(IRQ_HDC);                  // IRQ HDC de fin de transfert (FDC_SetIRQ)
}
