// Changement de lecteur pendant READ/WRITE SECTOR : registres publics + Scheduler.
// Références : fdc.c états TRANSFER_START/CRC, floppy.c validation CHS.
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
        std::fprintf(stderr, "[fdc-live-media] FAIL %s: got %lld, expected %lld\n",
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
    int pulses = 0;
    Rig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(false);
        fdc.setSoundSink([this](FdcSound sound) { if (sound == FdcSound::Index) ++pulses; });
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
    bool pending() { return mfp.read8(0xFFFA0D) & 0x80; }
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
    int64_t armIndex() {
        select(5); reg(0, 0xD4); next();
        // Le premier handler initialise la phase puis attend précisément l'index.
        return sched.nextDue();
    }
};
}

int testFdcLiveMedia() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-live-media-" + std::to_string(n));
        std::error_code ec; made = std::filesystem::create_directory(work, ec); if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{work};
    std::filesystem::path paths[] = {work / "a.st", work / "b.st"};
    for (int d = 0; d < 2; ++d) {
        std::vector<uint8_t> image(4096, uint8_t(0xA0 + d));
        image[0x13] = 8; image[0x14] = 0;
        image[0x18] = uint8_t(d ? 1 : 2); image[0x19] = 0;
        image[0x1A] = 2; image[0x1B] = 0;
        FILE* f = std::fopen(paths[d].string().c_str(), "wb");
        if (!f) return 1;
        const bool ok = std::fwrite(image.data(), 1, image.size(), f) == image.size();
        std::fclose(f); if (!ok) return 1;
    }
    for (bool write : {false, true}) {
        Rig r;
        c.equal("montage A 2 spt", r.fdc.loadImage(paths[0].string(), 0), true);
        c.equal("montage B 1 spt", r.fdc.loadImage(paths[1].string(), 1), true);
        r.select(5);
        std::fill(r.bus.ram.begin() + 0x2000, r.bus.ram.begin() + 0x2200, 0xCC);
        r.reg(4, 2);
        r.mode(write ? 0x190 : 0x90); r.data(1);
        r.fdc.write8(0xFF860D, 0); r.fdc.write8(0xFF860B, 0x20); r.fdc.write8(0xFF8609, 0);
        r.mode(write ? 0x180 : 0x80); r.data(write ? 0xA8 : 0x88);
        unsigned n = 0;
        while (++n < 10000 && r.next()) {
            if (write ? r.address() >= 0x2010 : r.sched.nextDue() - r.live == 38 * 256) break;
        }
        c.equal("point de changement atteint", n < 10000, true);
        c.equal("commande encore busy", r.fdc.read8(0xFF8605) & 1, 1);
        r.select(3); // B ne possède pas le secteur2 pourtant identifié sur A.
        c.equal("commande termine", r.finish(), true);
        c.equal("secteur absent RNF", r.reg(0) & 0x58, 0x10);
        c.equal("DMA borné", r.address(), write ? 0x2200 : 0x2000);
        // Relire la face suivante : elle ne doit pas être l'alias du secteur2.
        r.select(2); r.read(1); c.equal("relecture B face1", r.finish(), true);
        c.equal("face suivante B préservée", r.bus.ram[0x2000], 0xA1);
        r.select(5); r.read(2); c.equal("relecture A", r.finish(), true);
        c.equal("source A préservée", r.bus.ram[0x2000], 0xA0);
    }
    std::fprintf(stderr, "[fdc-live-media] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
#ifdef NEOST_FDC_LIVE_MEDIA_TEST_MAIN
int main() { return testFdcLiveMedia() ? 1 : 0; }
#endif
