// WRITE SECTOR : ID STX de 1024 octets puis sélection live d’une image ST.
// Références : fdc.c:FDC_WriteSector_ST, floppy.c:Floppy_WriteSectors Count=1.
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
        std::fprintf(stderr, "[fdc-sector-size-switch] FAIL %s: got %lld, expected %lld\n",
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

// Image STX à un champ ID de 1024 octets. La taille est engagée avant
// la sélection du lecteur B ; le transfert DMA conserve cette longueur.
std::vector<uint8_t> sourceImage(uint8_t sector) {
    std::vector<uint8_t> image = {'R','S','Y',0,3,0,0,0,0,0,1,0,0,0,0,0};
    auto le16 = [&](unsigned v) { image.push_back(uint8_t(v)); image.push_back(uint8_t(v >> 8)); };
    auto le32 = [&](unsigned v) { le16(v); le16(v >> 16); };
    le32(16 + 16 + 1024); le32(0);
    le16(1); le16(1); le16(6268);
    image.push_back(0); image.push_back(0);
    le32(0); le16(800); le16(0);
    image.insert(image.end(), {0, 0, sector, 3, 0, 0, 0, 0});
    image.insert(image.end(), 1024, 0xA5);
    return image;
}

bool saveImage(const std::filesystem::path& path, const std::vector<uint8_t>& image) {
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(image.data(), 1, image.size(), file) == image.size();
    std::fclose(file);
    return ok;
}

int testFdcSectorSizeSwitch() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-sector-size-switch-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};

    // Secteur1 : voisin dans la piste ; secteur2 : fin de piste et d'image.
    for (uint8_t sector : {1, 2}) {
        const auto source = work / "source.stx";
        const auto target = work / "target.st";
        std::vector<uint8_t> image(1024, 0xB6);
        image[0x13] = 2; image[0x14] = 0;
        image[0x18] = 2; image[0x19] = 0;
        image[0x1A] = 1; image[0x1B] = 0;
        if (!saveImage(source, sourceImage(sector)) || !saveImage(target, image)) return 1;

        Rig r;
        c.equal("montage STX 1024", r.fdc.loadImage(source.string(), 0), true);
        c.equal("montage ST 512", r.fdc.loadImage(target.string(), 1), true);
        r.select(5);
        std::fill(r.bus.ram.begin() + 0x2000, r.bus.ram.begin() + 0x2200, 0xCC);
        std::fill(r.bus.ram.begin() + 0x2200, r.bus.ram.begin() + 0x2400, 0xDD);
        r.reg(4, sector);
        r.mode(0x190); r.data(2);
        r.fdc.write8(0xFF8609, 0);
        r.fdc.write8(0xFF860B, 0x20);
        r.fdc.write8(0xFF860D, 0);
        r.mode(0x180); r.data(0xA8);
        unsigned events = 0;
        while (++events < 10000 && r.next()) if (r.address() >= 0x2010) break;
        c.equal("DMA engagé sur ID 1024", events < 10000, true);
        c.equal("BUSY au changement", r.fdc.read8(0xFF8605) & 1, 1);
        r.select(3);
        c.equal("écriture termine", r.finish(), true);
        c.equal("écriture ST sans RNF", r.reg(0) & 0x59, 0);
        c.equal("DMA reste 1024 octets", r.address(), 0x2400);

        r.read(sector);
        c.equal("relecture cible", r.finish(), true);
        c.equal("statut cible", r.reg(0) & 0x59, 0);
        c.equal("premier octet cible", r.bus.ram[0x2000], 0xCC);
        c.equal("dernier octet cible", r.bus.ram[0x21FF], 0xCC);
        c.equal("secteur cible entier", std::all_of(r.bus.ram.begin() + 0x2000,
                r.bus.ram.begin() + 0x2200, [](uint8_t v) { return v == 0xCC; }), true);
        r.read(sector == 1 ? 2 : 1);
        c.equal("relecture voisin", r.finish(), true);
        c.equal("voisin début préservé", r.bus.ram[0x2000], sector == 1 ? 0xB6 : image[0]);
        c.equal("voisin fin préservée", r.bus.ram[0x21FF], 0xB6);
        c.equal("voisin entier préservé", std::equal(r.bus.ram.begin() + 0x2000,
                r.bus.ram.begin() + 0x2200, image.begin() + (sector == 1 ? 512 : 0)), true);
        r.select(5); r.read(sector);
        c.equal("relecture source STX", r.finish(), true);
        c.equal("source préservée", r.bus.ram[0x2000], 0xA5);
    }
    std::fprintf(stderr, "[fdc-sector-size-switch] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
#ifdef NEOST_FDC_SECTOR_SIZE_SWITCH_TEST_MAIN
int main() { return testFdcSectorSizeSwitch() ? 1 : 0; }
#endif
