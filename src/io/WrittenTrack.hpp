// =============================================================================
//  WrittenTrack.hpp — Relecture d'un flux WRITE TRACK comme le WD1772 le relira.
//
//  Commun aux images .ST (piste gardée en mémoire, Fdc::parseWrittenTrack) et STX
//  (StxImage::reinterpretSaveTrack, piste de l'overlay .wd1772). Sans dépendance au
//  reste de l'émulateur : neost-stx-test ne lie que StxImage.cpp et ce module.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <vector>

namespace wdtrack {

// Un champ ID trouvé sur la piste relue, avec son champ données s'il en a un.
struct Sector {
    uint8_t tr = 0, hd = 0, sr = 0, sz = 2;
    int     idPos = 0;                  // position de l'IDAM ($FE) sur la piste relue
    uint16_t idCrc = 0;                 // deux octets de CRC relus après l'ID
    bool    idCrcOk = true;             // CRC d'ID juste
    bool    hasData = true;             // marque de données trouvée dans la fenêtre
    bool    dataCrcOk = true;           // CRC du champ données juste
    bool    deleted = false;            // marque $F8
    std::vector<uint8_t> data;          // 128 << (sz & 3) octets, même sans marque
};

// CRC16 CCITT du WD1772 (poly 0x1021, préchargé à 0xFFFF).
// [B] WD177X-00 p. 1-3 : G(x) = x^16 + x^12 + x^5 + 1, préchargé à un.
uint16_t crc16(const uint8_t* buf, int n);

// Flux WRITE TRACK brut (`raw`, `n` octets tels qu'écrits par le programme) →
// secteurs dans l'ordre angulaire. `trackLen` borne la piste relue (octets MFM d'un
// tour) : ce qui dépasse n'a pas trouvé de place avant l'index.
std::vector<Sector> parse(const uint8_t* raw, int n, int trackLen);

} // namespace wdtrack
