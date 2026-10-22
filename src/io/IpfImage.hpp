// =============================================================================
//  IpfImage.hpp — images IPF (SPS / CAPS, disquettes protégées) → modèle STX.
//
//  Une IPF ne stocke ni secteurs ni octets de piste : elle DÉCRIT chaque piste
//  comme une suite de blocs (données + gap), à partir desquels on régénère le
//  FLUX DE CELLULES MFM tel qu'il est gravé sur le support — synchros $4489,
//  octets de gap, « weak bits » (cellules sans transition stable) et cellules
//  de durée variable (pistes à densité variable des protections Copylock,
//  Speedlock…).
//
//  Le module travaille en deux étages, chacun indépendant et testable :
//
//   1. decode() : conteneur IPF (blocs CAPS/INFO/IMGE/DATA, CRC32 vérifiés) →
//      flux de cellules par piste (niveau 0/1/weak + durée en ns). Adapté du
//      décodeur IPF INDÉPENDANT de MAME (src/lib/formats/ipf_dsk.cpp, Olivier
//      Galibert, licence BSD-3-Clause) — PAS de libcapsimage (licence SPS non
//      libre). Cf. l'en-tête d'IpfImage.cpp pour l'attribution.
//
//   2. toStx() : un « lecteur WD1772 » parcourt ce flux comme le ferait le
//      séparateur de données du contrôleur (règles de synchro de MAME
//      wd_fdc.cpp : 3×$A1 avant l'IDAM, fenêtre DAM 28..43 octets après le CRC
//      de l'ID, pas de resynchro pendant les données) et en tire ce qu'un
//      programme ST peut OBSERVER : champs ID réels (positions angulaires
//      exactes, CRC bon/mauvais), champs données, marques F8/FB, bits fuzzy,
//      durée de chaque bloc de 16 octets, et le dump READ TRACK. Le résultat
//      est sérialisé en conteneur STX (Pasti rév. 2) en mémoire, que le chemin
//      _STX du FDC — mûr et testé — exploite tel quel.
//
//  CE QUI EST APPROXIMÉ (documenté dans docs/IMPLEMENTED.md) : il n'y a pas de
//  DPLL cellule par cellule pendant l'émulation. Les comportements que seul un
//  DPLL rend (glissement de phase sur des cellules hors tolérance, IDs trouvés
//  différemment d'un tour à l'autre, bits faibles qui changent la SYNCHRO et
//  pas seulement la valeur d'un octet) sont figés à la conversion.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST (GPL 3).
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ipf {

// Niveau d'une cellule MFM (1 = transition de flux).
enum : uint8_t { CELL_0 = 0, CELL_1 = 1, CELL_WEAK = 2 };

// Piste régénérée : cellule 0 = impulsion d'index.
struct CellTrack {
    int  cylinder = 0, head = 0;
    bool noise = false;                 // piste « non formatée » (type 1) : bruit
    std::vector<uint8_t>  cells;        // CELL_0 / CELL_1 / CELL_WEAK
    std::vector<uint16_t> timeNs;       // durée de chaque cellule (2000 ns nominal en DD)
    uint64_t totalNs() const;
};

struct Info {
    uint32_t encoder = 0;               // 1 = CAPS, 2 = SPS
    uint32_t release = 0, revision = 0;
    uint32_t minCylinder = 0, maxCylinder = 0, minHead = 0, maxHead = 0;
};

// Signature : bloc « CAPS » de 12 octets avec son CRC fixe (cf. MAME identify).
bool isIpf(const uint8_t* p, std::size_t n);

// Étage 1 : conteneur → flux de cellules. Renvoie false (et un message en
// anglais dans *err) si le conteneur est invalide ou utilise une variante non
// prise en charge. Les pistes « type 1 » de longueur nulle sont omises.
bool decode(const std::vector<uint8_t>& raw, std::vector<CellTrack>& out,
            Info* info = nullptr, std::string* err = nullptr);

// Statistiques de conversion (journal de montage, autotests).
struct StxStats {
    int tracks = 0, sectors = 0;
    int idCrcErrors = 0, dataCrcErrors = 0, noData = 0, deleted = 0;
    int fuzzySectors = 0, timedSectors = 0;
};

// Étage 2 : flux de cellules → conteneur STX rév. 2 (octets « RSY\0 … »).
// revBytes[i] reçoit, pour chaque piste ÉMISE (dans l'ordre des pistes du
// conteneur), la durée RÉELLE d'un tour exprimée en octets MFM DD (32 µs) :
// le FDC s'en sert pour cadencer la rotation au lieu de la taille du dump.
struct TrackRev { int cylinder = 0, head = 0; uint32_t bytes = 0; };
bool toStx(const std::vector<CellTrack>& tracks, std::vector<uint8_t>& stx,
           std::vector<TrackRev>* revs = nullptr, StxStats* stats = nullptr);

// Outils exposés pour les autotests (encodeur de piste synthétique).
uint32_t crc32(const uint8_t* p, std::size_t n);                 // CRC32 réfléchi (IPF)
uint16_t crc16Ccitt(const uint8_t* p, std::size_t n, uint16_t crc = 0xFFFF);

}  // namespace ipf
