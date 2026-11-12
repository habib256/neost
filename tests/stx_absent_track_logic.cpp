// WRITE TRACK sur une piste ABSENTE d'une STX : la piste est créée, relue, persistée.
// [B] WD177X-00 p. 1-13 : formater = placer la tête et lancer Write Track ; LOST DATA
// ne signale qu'un registre de données non servi. Hatari rend LOST DATA ([D] stx.c
// FDC_WriteTrack_STX @ f0736b24) : ce test échoue sur ce comportement.
// Registres WD1772 publics + Scheduler, sans accès aux internals du contrôleur.
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include "io/StxImage.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[stx-absent-track] FAIL %s: got %lld, expected %lld\n",
                     label, static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct Rig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    Rig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(true);
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
        mfp.write8(0xFFFA09, 0x80);
    }
    // Lecteur A ; face 0 (5) ou face 1 (4) — bit 0 du port A du YM, actif bas.
    void select(int side) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, side ? 4 : 5); }
    void mode(uint16_t value) {
        fdc.write8(0xFF8606, uint8_t(value >> 8)); fdc.write8(0xFF8607, uint8_t(value));
    }
    void data(uint8_t value) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, value); }
    uint8_t reg(uint16_t addressBits) { mode(0x80 | addressBits); return fdc.read8(0xFF8605); }
    void reg(uint16_t addressBits, uint8_t value) { mode(0x80 | addressBits); data(value); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    bool next() {
        const int64_t due = sched.nextDue();
        if (due < 0) return false;
        live = due; sched.runTo(due); return true;
    }
    bool finish() {
        for (unsigned n = 0; n < 200000 && !irq(); ++n) if (!next()) return false;
        return irq();
    }
    void dma(bool write, uint8_t count) {
        mode(write ? 0x190 : 0x90); data(count);
        fdc.write8(0xFF860D, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF8609, 0);
    }
    // SEEK sans vérification (type I) : TR suit la tête.
    bool seek(uint8_t track) { reg(6, track); reg(0, 0x10); return finish(); }
    bool seekVerify(uint8_t track) { reg(6, track); reg(0, 0x14); return finish(); }
    bool writeTrack(const std::vector<uint8_t>& flux) {
        std::copy(flux.begin(), flux.end(), bus.ram.begin() + 0x2000);
        dma(true, 13); mode(0x180); data(0xF8); return finish();
    }
    bool readSector(uint8_t sector) {
        std::fill_n(bus.ram.begin() + 0x2000, 512, 0xCC);
        dma(false, 1); reg(4, sector); reg(0, 0x88); return finish();
    }
    bool writeSector(uint8_t sector, uint8_t fill) {
        std::fill_n(bus.ram.begin() + 0x2000, 512, fill);
        reg(4, sector); dma(true, 1); mode(0x180); data(0xA8); return finish();
    }
};

// Jamais $F5-$FE : dans un flux WRITE TRACK ce sont des marques ou un CRC ([B] p. 1-13).
uint8_t pattern(uint8_t track, uint8_t side, uint8_t sector, int i) {
    return uint8_t((track * 7 + side * 0x40 + sector * 0x11 + i) % 0xF0);
}

// Flux qu'un formateur envoie au WD1772 : $F5 = A1 de synchro, $F7 = CRC.
std::vector<uint8_t> formatFlux(uint8_t track, uint8_t side, int sectors) {
    std::vector<uint8_t> f(60, 0x4E);
    for (int s = 1; s <= sectors; ++s) {
        f.insert(f.end(), 12, 0x00);
        f.insert(f.end(), {0xF5, 0xF5, 0xF5, 0xFE, track, side, uint8_t(s), 2, 0xF7});
        f.insert(f.end(), 22, 0x4E);
        f.insert(f.end(), 12, 0x00);
        f.insert(f.end(), {0xF5, 0xF5, 0xF5, 0xFB});
        for (int i = 0; i < 512; ++i) f.push_back(pattern(track, side, uint8_t(s), i));
        f.push_back(0xF7);
        f.insert(f.end(), 40, 0x4E);
    }
    f.resize(6268, 0x4E);
    return f;
}

bool sectorIs(Rig& r, uint8_t track, uint8_t side, uint8_t sector) {
    for (int i = 0; i < 512; ++i)
        if (r.bus.ram[0x2000 + i] != pattern(track, side, sector, i)) return false;
    return true;
}

// Lit le .wd1772 comme STX_LoadSaveFile d'Hatari @ f0736b24 (stx.c:548-702) :
// compteurs d'en-tête = nombre de blocs SECT/TRCK, chaque SECT/TRCK sur une piste de
// l'image d'origine, sinon Hatari rejette le fichier entier. Renvoie le nombre de
// blocs NTRK + NSCT (sautés par Hatari comme blocs inconnus), -1 si incompatible.
int hatariCompatibleExtraBlocks(const std::vector<uint8_t>& in,
                                const std::vector<std::pair<int, int>>& original) {
    if (in.size() < 16 || std::memcmp(in.data(), "WD1772", 6) != 0) return -1;
    auto be32 = [&](size_t p) {
        return (uint32_t(in[p]) << 24) | (uint32_t(in[p + 1]) << 16) | (uint32_t(in[p + 2]) << 8) | in[p + 3];
    };
    const uint32_t nbSect = be32(8), nbTrck = be32(12);
    uint32_t sect = 0, trck = 0;
    int extra = 0;
    for (size_t p = 16; p + 8 <= in.size();) {
        const uint32_t len = be32(p + 4);
        if (p + 4 + len > in.size() || len < 6) return -1;
        const int track = in[p + 8], side = in[p + 9];
        const bool known = std::find(original.begin(), original.end(),
                                     std::make_pair(track, side)) != original.end();
        if (!std::memcmp(&in[p], "SECT", 4)) { ++sect; if (!known) return -1; }
        else if (!std::memcmp(&in[p], "TRCK", 4)) { ++trck; if (!known) return -1; }
        else if (!std::memcmp(&in[p], "NSCT", 4) || !std::memcmp(&in[p], "NTRK", 4)) ++extra;
        p += 4 + len;
    }
    if (sect != nbSect || trck != nbTrck) return -1;
    return extra;
}
} // namespace

int testStxAbsentTrack() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-stx-absent-track-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};

    // STX simple face d'UNE piste (0, face 0), un secteur de 512 o : tout le reste est absent.
    const auto path = work / "single.stx";
    std::vector<uint8_t> image = {'R','S','Y',0,3,0,0,0,0,0,1,0,0,0,0,0};
    auto le16 = [&](unsigned v) { image.push_back(uint8_t(v)); image.push_back(uint8_t(v >> 8)); };
    auto le32 = [&](unsigned v) { le16(v); le16(v >> 16); };
    le32(16 + 512); le32(0); le16(1); le16(0); le16(6268 * 8);
    image.push_back(0); image.push_back(0);
    image.insert(image.end(), 512, 0xA5);
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
        if (!f) return 1;
    }

    {
        Rig r;
        c.equal("montage", r.fdc.loadImage(path.string()), true);
        r.select(0);

        // 1) Au-delà de la dernière piste de l'image (piste 1), puis formatage 80-82.
        for (uint8_t track : {uint8_t(1), uint8_t(82)}) {
            c.equal("SEEK", r.seek(track), true);
            const uint64_t writes = r.fdc.stxWrites();
            c.equal("WRITE TRACK piste absente terminé", r.writeTrack(formatFlux(track, 0, 3)), true);
            // Bits 2 (LOST DATA), 4 (RNF), 6 (WPRT) : l'écriture a eu lieu.
            c.equal("WRITE TRACK piste absente sans LOST DATA", r.reg(0) & 0x54, 0);
            // Le retour arrière ne franchit pas une écriture STX : la règle tient.
            c.equal("écriture STX comptée", r.fdc.stxWrites() > writes, true);
            c.equal("READ SECTOR piste créée", r.readSector(2), true);
            c.equal("READ SECTOR sans erreur", r.reg(0) & 0x1C, 0);
            c.equal("données relues", sectorIs(r, track, 0, 2), true);
            // READ ADDRESS : la piste du champ ID va dans le registre secteur (p. 1-13).
            r.dma(false, 1); r.reg(0, 0xC0);
            c.equal("READ ADDRESS terminé", r.finish(), true);
            c.equal("READ ADDRESS sans erreur", r.reg(0) & 0x18, 0);
            c.equal("READ ADDRESS piste", r.reg(4), track);
            // READ TRACK : le champ ID écrit est sur la piste relue.
            std::fill_n(r.bus.ram.begin() + 0x2000, 6656, 0);
            r.dma(false, 13); r.reg(0, 0xE8);
            c.equal("READ TRACK terminé", r.finish(), true);
            const std::vector<uint8_t> id = {0xA1, 0xA1, 0xA1, 0xFE, track, 0, 3, 2};
            const auto begin = r.bus.ram.begin() + 0x2000, end = begin + 6256;
            c.equal("READ TRACK porte l'ID", std::search(begin, end, id.begin(), id.end()) != end, true);
        }

        // 2) Face 1 d'une STX simple face : SEEK avec vérification refusé avant, accepté après.
        c.equal("SEEK piste 0", r.seek(0), true);
        r.select(1);
        c.equal("vérif face 1 absente terminée", r.seekVerify(0), true);
        c.equal("vérif face 1 absente : RNF", r.reg(0) & 0x10, 0x10);
        c.equal("WRITE TRACK face 1", r.writeTrack(formatFlux(0, 1, 2)), true);
        c.equal("WRITE TRACK face 1 sans LOST DATA", r.reg(0) & 0x54, 0);
        c.equal("vérif face 1 formatée terminée", r.seekVerify(0), true);
        c.equal("vérif face 1 formatée", r.reg(0) & 0x18, 0);
        // WRITE SECTOR sur la piste créée → bloc NSCT.
        c.equal("WRITE SECTOR face 1", r.writeSector(1, 0x3C), true);
        c.equal("WRITE SECTOR sans erreur", r.reg(0) & 0x5C, 0);
        c.equal("relecture face 1", r.readSector(1), true);
        c.equal("secteur réécrit", r.bus.ram[0x2000 + 100], 0x3C);
        c.equal("secteur 2 face 1", r.readSector(2), true);
        c.equal("données face 1", sectorIs(r, 0, 1, 2), true);

        // 3) Face 0 piste 0 (présente) reformatée : bloc TRCK standard, compté.
        r.select(0);
        c.equal("WRITE TRACK piste présente", r.writeTrack(formatFlux(0, 0, 1)), true);

        // 4) Save-state : le média est hors instantané, la piste créée reste lisible.
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        r.sched.serialize(save); r.fdc.serialize(save);
        const int64_t savedLive = r.live;
        c.equal("SEEK piste 82 après instantané", r.seek(82), true);
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        r.sched.serialize(load); r.fdc.serialize(load);
        r.live = savedLive;
        c.equal("instantané relu", load.ok(), true);
        c.equal("SEEK piste 82 après restauration", r.seek(82), true);
        c.equal("relecture après restauration", r.readSector(3), true);
        c.equal("données après restauration", sectorIs(r, 82, 0, 3), true);
    }

    // 5) Le .wd1772 reste lisible par Hatari : SECT/TRCK sur des pistes d'origine,
    //    compteurs exacts ; les pistes créées sont en NTRK/NSCT (trois NTRK, un NSCT).
    {
        std::ifstream f(work / "single.wd1772", std::ios::binary);
        const std::vector<uint8_t> wd((std::istreambuf_iterator<char>(f)), {});
        c.equal("format .wd1772 compatible Hatari",
                hatariCompatibleExtraBlocks(wd, {{0, 0}}), 4);
    }

    // 6) Rechargement : les pistes créées reviennent, face 1 comprise.
    {
        Rig r;
        c.equal("remontage", r.fdc.loadImage(path.string()), true);
        r.select(0);
        for (uint8_t track : {uint8_t(1), uint8_t(82)}) {
            c.equal("SEEK remontage", r.seek(track), true);
            c.equal("READ SECTOR remontage", r.readSector(1), true);
            c.equal("READ SECTOR remontage sans erreur", r.reg(0) & 0x1C, 0);
            c.equal("données remontage", sectorIs(r, track, 0, 1), true);
        }
        c.equal("SEEK piste 0", r.seek(0), true);
        c.equal("piste 0 reformatée", r.readSector(1), true);
        c.equal("données piste 0 reformatée", sectorIs(r, 0, 0, 1), true);
        r.select(1);
        c.equal("vérif face 1 rechargée", r.seekVerify(0), true);
        c.equal("vérif face 1 rechargée sans RNF", r.reg(0) & 0x18, 0);
        c.equal("WRITE SECTOR rechargé", r.readSector(1), true);
        c.equal("WRITE SECTOR rechargé (NSCT)", r.bus.ram[0x2000 + 100], 0x3C);
        // Piste jamais formatée : toujours absente, toujours RNF.
        r.select(0);
        c.equal("SEEK piste 40", r.seek(40), true);
        c.equal("piste 40 jamais formatée", r.readSector(1), true);
        c.equal("piste 40 RNF", r.reg(0) & 0x10, 0x10);
    }

    // 7) Bloc hors format STX (piste > 127, face > 1) : ignoré, sans repli sur une
    //    autre piste ni sur l'autre face.
    {
        StxImage stx;
        c.equal("parse STX", stx.parse(image), true);
        const std::string wpath = (work / "forged.wd1772").string();
        std::vector<uint8_t> wd = {'W','D','1','7','7','2',1,0, 0,0,0,0, 0,0,0,0};
        const std::vector<uint8_t> flux = formatFlux(0, 0, 1);
        for (const auto& ts : {std::make_pair(128, 0), std::make_pair(0, 2)}) {
            wd.insert(wd.end(), {'N','T','R','K'});
            const uint32_t len = uint32_t(8 + flux.size());
            wd.insert(wd.end(), {uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)});
            wd.push_back(uint8_t(ts.first)); wd.push_back(uint8_t(ts.second));
            wd.push_back(uint8_t(flux.size() >> 8)); wd.push_back(uint8_t(flux.size()));
            wd.insert(wd.end(), flux.begin(), flux.end());
        }
        std::ofstream(wpath, std::ios::binary).write(reinterpret_cast<const char*>(wd.data()),
                                                      std::streamsize(wd.size()));
        c.equal("blocs hors format ignorés", stx.loadWd1772(wpath), false);
        c.equal("piste 0 intacte", stx.findTrack(0, 0) && !stx.findTrack(0, 0)->writeReinterpreted, true);
        c.equal("aucune piste ajoutée", stx.tracksCount(), 1);
    }

    std::fprintf(stderr, "[stx-absent-track] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
#ifdef NEOST_STX_ABSENT_TRACK_TEST_MAIN
int main() { return testStxAbsentTrack() ? 1 : 0; }
#endif
