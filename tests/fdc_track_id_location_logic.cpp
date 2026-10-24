// WRITE TRACK ST : les champs ID ne doivent pas adresser une autre piste/face.
// Références : fdc.c:FDC_WriteTrack_ST ; représentation standard ST uniquement.
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[fdc-track-id-location] FAIL %s: got %lld, expected %lld\n",
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
        fdc.setHostWriteBack(false);
        // Même connexion que Machine::Machine ; aucun accès FDC dans select().
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
        mfp.write8(0xFFFA09, 0x80); // capturer le front GPIP5 dans IPRB, même après clear IRQ
    }
    void select(uint8_t value) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, value); }
    void mode(uint16_t value) {
        fdc.write8(0xFF8606, uint8_t(value >> 8)); fdc.write8(0xFF8607, uint8_t(value));
    }
    void data(uint8_t value) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, value); }
    uint8_t reg(uint16_t addressBits) { mode(0x80 | addressBits); return fdc.read8(0xFF8605); }
    void reg(uint16_t addressBits, uint8_t value) { mode(0x80 | addressBits); data(value); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    void advance(int64_t at) { live = at; sched.runTo(at); }
    bool next() {
        const int64_t due = sched.nextDue();
        if (due < 0) return false;
        advance(due); return true;
    }
    bool finish() {
        // Ne pas lire STR : cela masquerait une sélection seulement PULL.
        for (unsigned n = 0; n < 100000 && !irq(); ++n) if (!next()) return false;
        return irq();
    }
    void read(uint8_t sector = 2, uint8_t command = 0x88, uint8_t count = 1) {
        mode(0x90); data(count);
        fdc.write8(0xFF860D, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF8609, 0);
        reg(4, sector); reg(0, command);
    }
    uint32_t address() {
        return (uint32_t(fdc.read8(0xFF8609)) << 16)
             | (uint32_t(fdc.read8(0xFF860B)) << 8) | fdc.read8(0xFF860D);
    }

};
}

std::vector<uint8_t> trackImage(uint8_t idTrack, uint8_t idSide,
                                const std::vector<uint8_t>& disk, bool duplicate, bool mixedTrack) {
    std::vector<uint8_t> track(60, 0x4E);
    for (uint8_t sector = 1; sector <= 2; ++sector) {
        track.insert(track.end(), 12, 0);
        track.insert(track.end(), {0xA1, 0xA1, 0xA1, 0xFE, uint8_t(mixedTrack && sector == 2 ? 1 : idTrack), idSide,
                                 uint8_t(duplicate ? 1 : sector), 2, 0, 0});
        track.insert(track.end(), 22, 0x4E);
        track.insert(track.end(), 12, 0);
        track.insert(track.end(), {0xA1, 0xA1, 0xA1, 0xFB});
        // Garder un BPB valide pour que la relecture publique utilise la même géométrie.
        const auto begin = disk.begin() + (sector - 1) * 512;
        const auto data = track.size();
        track.insert(track.end(), begin, begin + 512);
        track[data + 100] = 0xCC;
        track.insert(track.end(), 2, 0);
        track.insert(track.end(), 40, 0x4E);
    }
    track.resize(6268, 0x4E);
    return track;
}

int testFdcTrackIdLocation() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-track-id-location-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    const auto path = work / "target.st";
    std::vector<uint8_t> image(4096);
    for (unsigned sector = 0; sector < 8; ++sector)
        std::fill_n(image.begin() + sector * 512, 512, uint8_t(0x30 + sector));
    image[0x13] = 8; image[0x14] = 0;
    image[0x18] = 2; image[0x19] = 0;
    image[0x1A] = 2; image[0x1B] = 0;

    for (int scenario = 0; scenario < 5; ++scenario) {
        FILE* file = std::fopen(path.string().c_str(), "wb");
        if (!file) return 1;
        const bool saved = std::fwrite(image.data(), 1, image.size(), file) == image.size();
        std::fclose(file);
        if (!saved) return 1;
        Rig r;
        c.equal("montage image ST", r.fdc.loadImage(path.string()), true);
        r.select(5); // tête0, face0 : aucune commande STEP/SEEK pendant le formatage.
        const auto track = trackImage(scenario == 1 ? 1 : 0, scenario == 2 ? 1 : 0, image, scenario == 3, scenario == 4);
        std::copy(track.begin(), track.end(), r.bus.ram.begin() + 0x2000);
        r.mode(0x190); r.data(13);
        r.fdc.write8(0xFF860D, 0);
        r.fdc.write8(0xFF860B, 0x20);
        r.fdc.write8(0xFF8609, 0);
        r.mode(0x180); r.data(0xF8);
        c.equal("WRITE TRACK termine", r.finish(), true);
        // Un WD1772 formate sans erreur quels que soient les IDs : LOST DATA ne
        // signale qu'une DRQ non servie. Une piste que le .st ne sait pas représenter
        // est gardée en mémoire et relue selon SES IDs (le WD1772 ne compare pas la
        // face ; un ID de piste ≠ TR ou un numéro absent → RNF).
        c.equal("WRITE TRACK sans erreur", r.reg(0) & 0x5D, 0);
        auto expected = image;
        if (!scenario) { expected[100] = 0xCC; expected[612] = 0xCC; }
        // Piste 0 face 0 reformatée hors géométrie : secteur lisible (données écrites)
        // ou RNF, selon les IDs du flux.
        auto overrideReadable = [&](uint8_t sector) {
            switch (scenario) {
                case 1: return false;                 // IDs « piste 1 » sur la piste 0
                case 2: return true;                  // face d'ID ignorée par le WD1772
                case 3: return sector == 1;           // deux IDs « secteur 1 »
                case 4: return sector == 1;           // secteur 2 annoncé piste 1
                default: return true;
            }
        };
        // Relire toutes les pistes/faces via SEEK et READ SECTOR : aucune inspection
        // de l'image interne ni dépendance aux offsets privés du formateur.
        for (uint8_t cylinder = 0; cylinder < 2; ++cylinder) {
            r.reg(6, cylinder); r.reg(0, 0x18);
            c.equal("SEEK termine", r.finish(), true);
            c.equal("SEEK registre piste", r.reg(2), cylinder);
            for (uint8_t side = 0; side < 2; ++side) {
                r.select(side ? 4 : 5);
                for (uint8_t sector = 1; sector <= 2; ++sector) {
                    r.read(sector);
                    c.equal("relecture sentinelle", r.finish(), true);
                    const unsigned offset = ((cylinder * 2 + side) * 2 + sector - 1) * 512;
                    if (scenario && cylinder == 0 && side == 0) {
                        if (!overrideReadable(sector)) {
                            c.equal("ID absent de la piste reformatée : RNF", r.reg(0) & 0x10, 0x10);
                            continue;
                        }
                        c.equal("statut relecture piste reformatée", r.reg(0) & 0x5D, 0);
                        // Deux IDs « secteur 1 » (scénario 3) : celui qui passe le premier
                        // sous la tête, donc l'une OU l'autre des deux données du flux.
                        auto matches = [&](unsigned payload) {
                            auto written = std::vector<uint8_t>(image.begin() + payload * 512,
                                                                image.begin() + (payload + 1) * 512);
                            written[100] = 0xCC;      // secteur du flux (marqueur posé à +100)
                            return std::equal(r.bus.ram.begin() + 0x2000, r.bus.ram.begin() + 0x2200,
                                              written.begin());
                        };
                        c.equal("données de la piste reformatée",
                                matches(sector - 1) || (scenario == 3 && matches(1)), true);
                        continue;
                    }
                    c.equal("statut relecture", r.reg(0) & 0x5D, 0);
                    c.equal("secteur entier attendu", std::equal(r.bus.ram.begin() + 0x2000,
                            r.bus.ram.begin() + 0x2200, expected.begin() + offset), true);
                }
            }
        }
        if (scenario == 3) {
            // IDs dupliqués : deux lectures enchaînées rencontrent les deux champs ID tour
            // à tour — la seconde ne doit pas resservir les données du premier.
            r.reg(6, 0); r.reg(0, 0x18);
            c.equal("SEEK piste 0", r.finish(), true);
            r.select(5);
            uint8_t got[2];
            for (int k = 0; k < 2; ++k) {
                r.read(1);
                c.equal("lecture ID dupliqué", r.finish(), true);
                c.equal("statut ID dupliqué", r.reg(0) & 0x5D, 0);
                got[k] = r.bus.ram[0x2000];
            }
            c.equal("IDs dupliqués : données du champ sous la tête", got[0] != got[1], true);
        }
    }
    {
        // READ ADDRESS sur une piste reformatée : l'octet de taille est rendu BRUT
        // ($06 : longueur 512 d'après ses bits 0-1, mais pas un ID standard).
        FILE* file = std::fopen(path.string().c_str(), "wb");
        if (!file) return 1;
        const bool saved = std::fwrite(image.data(), 1, image.size(), file) == image.size();
        std::fclose(file);
        if (!saved) return 1;
        Rig r;
        c.equal("montage image ST (RA)", r.fdc.loadImage(path.string()), true);
        r.select(5);
        std::vector<uint8_t> track(60, 0x4E);
        track.insert(track.end(), 12, 0);
        track.insert(track.end(), {0xF5, 0xF5, 0xF5, 0xFE, 0, 0, 0x42, 0x06, 0xF7});
        track.insert(track.end(), 22, 0x4E);
        track.insert(track.end(), 12, 0);
        track.insert(track.end(), {0xF5, 0xF5, 0xF5, 0xFB});
        track.insert(track.end(), 512, 0x5A);
        track.push_back(0xF7);
        track.resize(6268, 0x4E);
        std::copy(track.begin(), track.end(), r.bus.ram.begin() + 0x2000);
        r.mode(0x190); r.data(13);
        r.fdc.write8(0xFF860D, 0); r.fdc.write8(0xFF860B, 0x20); r.fdc.write8(0xFF8609, 0);
        r.mode(0x180); r.data(0xF8);
        c.equal("WRITE TRACK (RA) termine", r.finish(), true);
        std::fill_n(r.bus.ram.begin() + 0x2000, 16, 0xEE);
        // Le FIFO DMA ne vide qu'à 16 octets : trois READ ADDRESS (3 × 6) sans reset DMA.
        r.read(1, 0xC0);
        c.equal("READ ADDRESS termine", r.finish(), true);
        c.equal("READ ADDRESS sans RNF", r.reg(0) & 0x10, 0);
        for (int k = 0; k < 2; ++k) { r.reg(0, 0xC0); c.equal("READ ADDRESS suivant", r.finish(), true); }
        c.equal("READ ADDRESS : secteur", r.bus.ram[0x2002], 0x42);
        c.equal("READ ADDRESS : taille brute", r.bus.ram[0x2003], 0x06);
    }
    std::fprintf(stderr, "[fdc-track-id-location] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
#ifdef NEOST_FDC_TRACK_ID_LOCATION_TEST_MAIN
int main() { return testFdcTrackIdLocation() ? 1 : 0; }
#endif
