// =============================================================================
//  stx_writetrack_test.cpp — vérifie la ré-interprétation en LECTURE d'une piste
//  réécrite par WRITE TRACK (StxImage::reinterpretSaveTrack) + le round-trip via
//  le fichier compagnon .wd1772. Compilé/lancé hors CMake :
//      g++ -std=c++17 -I src tests/stx_writetrack_test.cpp src/io/StxImage.cpp -o /tmp/wt
//      /tmp/wt "disks/stx/Rick Dangerous - Firebird (UK) (France).stx"
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/StxImage.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

static void add(std::vector<uint8_t>& v, uint8_t b) { v.push_back(b); }
static uint8_t expect(int s, int i) { return uint8_t((s + 1) * 0x10 + (i & 0x0f)); }

// Forge un flux WRITE TRACK standard : GAP1, puis pour chaque secteur GAP2 + sync +
// IDAM + champ ID + GAP3 + sync + DAM + données + GAP4. C'est ce qu'un formateur
// envoie au WD1772 ; reinterpretSaveTrack doit en ressortir les secteurs.
static std::vector<uint8_t> forgeFlux(uint8_t track, int nSectors) {
    std::vector<uint8_t> f;
    for (int i = 0; i < 60; ++i) add(f, 0x4e);                     // GAP1
    for (int s = 1; s <= nSectors; ++s) {
        for (int i = 0; i < 12; ++i) add(f, 0x00);                 // GAP2
        add(f, 0xf5); add(f, 0xf5); add(f, 0xf5); add(f, 0xfe);    // sync ($F5 → A1) + IDAM
        add(f, track); add(f, 0); add(f, uint8_t(s)); add(f, 2);   // ID : tr/hd/sr/size(=512)
        add(f, 0xf7);                                              // CRC d'ID ($F7 → deux octets)
        for (int i = 0; i < 22; ++i) add(f, 0x4e);                 // GAP3a
        for (int i = 0; i < 12; ++i) add(f, 0x00);                 // GAP3b
        add(f, 0xf5); add(f, 0xf5); add(f, 0xf5); add(f, 0xfb);    // sync + DAM
        for (int i = 0; i < 512; ++i) add(f, expect(s - 1, i));    // données distinctes/secteur
        add(f, 0xf7);                                              // CRC des données
        for (int i = 0; i < 40; ++i) add(f, 0x4e);                 // GAP4
    }
    return f;
}

// Forge une image STX SYNTHÉTIQUE minimale : en-tête « RSY\0 » + une piste au
// format « simple » (sectorsCount secteurs de 512 o, sans bloc secteur — la branche
// buildSectorsSimple du parseur). A20 (2026-08-27) : le défaut historique du test
// était un jeu commercial cracké (`disks/stx/Rick Dangerous…`) — inutilisable dans
// un palier destiné à survivre à la purge du § BLOQUANT RELEASE. Un chemin en
// argument reste accepté pour rejouer le test sur une vraie image.
static std::vector<uint8_t> forgeStx(uint8_t track, int nSectors) {
    std::vector<uint8_t> v;
    // En-tête fichier (16 o) : magic, version, outil, réservé, nb pistes, révision.
    const uint8_t hdr[16] = { 'R','S','Y','\0', 3,0, 0,0, 0,0, /*tracks=*/1, /*rev=*/0, 0,0,0,0 };
    v.insert(v.end(), hdr, hdr + 16);
    // Bloc piste : blockSize(4) fuzzySize(4) sectorsCount(2) flags(2) mfmSize(2)
    // trackNumber(1) recordType(1), puis nSectors×512 o de données.
    const uint32_t blockSize = 16u + uint32_t(nSectors) * 512u;
    auto le32 = [&](uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); };
    auto le16 = [&](uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); };
    le32(blockSize); le32(0); le16(uint16_t(nSectors)); le16(0 /* pas de SECTOR_BLOCK */);
    le16(6250); v.push_back(track); v.push_back(0);
    for (int s = 0; s < nSectors; ++s)
        for (int i = 0; i < 512; ++i) v.push_back(uint8_t(0xD0 + s));   // contenu quelconque
    return v;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : nullptr;

    std::vector<uint8_t> raw;
    if (path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) { std::fprintf(stderr, "FAIL: impossible d'ouvrir %s\n", path); return 2; }
        const std::streamsize n = f.tellg(); f.seekg(0);
        raw.resize(size_t(n)); f.read(reinterpret_cast<char*>(raw.data()), n);
    } else {
        raw = forgeStx(/*track=*/5, /*nSectors=*/10);   // même géométrie que l'ex-défaut
    }

    StxImage stx;
    if (!stx.parse(std::move(raw))) { std::fprintf(stderr, "FAIL: parse STX\n"); return 2; }

    const int TR = 5, NS = 3;
    StxImage::Track* t = stx.findTrack(TR, 0);
    assert(t && "piste 5 absente");

    // Avant : la piste a ses secteurs d'origine (10 pour Rick Dangerous).
    const int origCount = t->sectorsCountView();
    assert(!t->writeReinterpreted);

    // --- Simule un WRITE TRACK : pousse la SaveTrack et réinterprète. ---
    StxImage::SaveTrack st; st.track = TR; st.side = 0; st.data = forgeFlux(TR, NS);
    stx.saveTracks.push_back(std::move(st));
    t->saveTrackIndex = int(stx.saveTracks.size()) - 1;
    stx.reinterpretSaveTrack(*t);

    assert(t->writeReinterpreted && "ré-interprétation non armée");
    assert(t->sectorsCountView() == NS && "mauvais nombre de secteurs relus");
    assert(t->sectorsCountView() != origCount && "la vue n'a pas changé");

    const std::vector<StxImage::Sector>& secs = t->sectorsView();
    for (int s = 0; s < NS; ++s) {
        assert(secs[s].idTrack == TR);
        assert(secs[s].idHead == 0);
        assert(secs[s].idSector == s + 1);
        assert(secs[s].idSize == 2 && secs[s].sectorSize == 512);
        assert(secs[s].pData != nullptr);
        for (int i = 0; i < 512; ++i) assert(secs[s].pData[i] == expect(s, i));
    }
    assert(secs[0].bitPosition < secs[1].bitPosition);
    assert(secs[1].bitPosition < secs[2].bitPosition);

    // --- Round-trip .wd1772 : sauve, recharge dans une image neuve, re-vérifie. ---
    const char* tmp = "/tmp/neost_wt_test.wd1772";
    assert(stx.saveWd1772(tmp) && "saveWd1772");

    std::vector<uint8_t> raw2;
    if (path) {
        std::ifstream f2(path, std::ios::binary | std::ios::ate);
        const std::streamsize n2 = f2.tellg(); f2.seekg(0);
        raw2.resize(size_t(n2)); f2.read(reinterpret_cast<char*>(raw2.data()), n2);
    } else {
        raw2 = forgeStx(5, 10);
    }
    StxImage stx2;
    assert(stx2.parse(std::move(raw2)) && "parse #2");
    assert(stx2.loadWd1772(tmp) && "loadWd1772");

    StxImage::Track* t2 = stx2.findTrack(TR, 0);
    assert(t2 && t2->writeReinterpreted && t2->sectorsCountView() == NS && "rechargement TRCK");
    const std::vector<StxImage::Sector>& secs2 = t2->sectorsView();
    for (int s = 0; s < NS; ++s)
        for (int i = 0; i < 512; ++i) assert(secs2[s].pData[i] == expect(s, i));

    // --- Relecture comme la puce ([B] WD177X-00 p. 1-9, 1-13, 1-18) -------------------
    // Secteur 1 : A1 écrits en clair (pas de $F5) → aucune marque, invisible.
    // Secteur 2 : CRC d'ID écrit en clair et faux → RNF + CRC (erreur de CRC d'ID).
    // Secteur 3 : ID sans champ données (pas de synchro dans les 43 octets) → RNF.
    // Secteur 4 : CRC de données en clair et faux → CRC, données présentes.
    // Secteur 5 : sain, marque $F8 → RECORD TYPE.
    {
        std::vector<uint8_t> f;
        for (int i = 0; i < 60; ++i) add(f, 0x4e);
        auto gap = [&](int n, uint8_t b) { for (int i = 0; i < n; ++i) add(f, b); };
        auto id = [&](uint8_t sr, bool sync, bool goodCrc) {
            gap(12, 0x00);
            const uint8_t m = sync ? 0xf5 : 0xa1;
            add(f, m); add(f, m); add(f, m); add(f, 0xfe);
            add(f, TR); add(f, 0); add(f, sr); add(f, 2);
            if (goodCrc) add(f, 0xf7); else { add(f, 0x12); add(f, 0x34); }
        };
        auto data = [&](uint8_t mark, bool goodCrc) {
            gap(22, 0x4e); gap(12, 0x00);
            add(f, 0xf5); add(f, 0xf5); add(f, 0xf5); add(f, mark);
            for (int i = 0; i < 512; ++i) add(f, 0x5a);
            if (goodCrc) add(f, 0xf7); else { add(f, 0x56); add(f, 0x78); }
            gap(40, 0x4e);
        };
        id(1, false, true); data(0xfb, true);
        id(2, true, false); data(0xfb, true);
        id(3, true, true);  gap(600, 0x4e);
        id(4, true, true);  data(0xfb, false);
        id(5, true, true);  data(0xf8, true);
        StxImage::SaveTrack st3; st3.track = TR; st3.side = 0; st3.data = f;
        stx.saveTracks[size_t(t->saveTrackIndex)] = std::move(st3);
        stx.reinterpretSaveTrack(*t);
        const std::vector<StxImage::Sector>& v = t->sectorsView();
        assert(t->sectorsCountView() == 4 && "A1 en clair : pas une marque");
        assert(v[0].idSector == 2 && v[0].fdcStatus == (StxImage::FLAG_RNF | StxImage::FLAG_CRC)
               && v[0].pData == nullptr && "CRC d'ID faux");
        assert(v[1].idSector == 3 && v[1].fdcStatus == StxImage::FLAG_RNF && v[1].pData == nullptr
               && "ID sans données");
        assert(v[2].idSector == 4 && v[2].fdcStatus == StxImage::FLAG_CRC && v[2].pData != nullptr
               && v[2].pData[0] == 0x5a && "CRC de données faux");
        assert(v[3].idSector == 5 && v[3].fdcStatus == StxImage::FLAG_RECORD_TYPE && "marque $F8");
    }

    std::printf("OK: WRITE TRACK ré-interprété (%d secteurs, vue %d→%d) + round-trip .wd1772"
                " + marques et CRC relus comme la puce\n",
                NS, origCount, NS);
    std::remove(tmp);
    return 0;
}
