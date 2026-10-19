// Sélection PSG et index avant accès WD1772 : registres publics + Scheduler.
// Références Hatari : psg.c:FDC_SetDriveSide, fdc.c:FDC_UpdateAll et accès $8604.
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
        std::fprintf(stderr, "[fdc-force-interrupt] FAIL %s: got %lld, expected %lld\n",
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

int testFdcForceInterrupt() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-force-interrupt-" + std::to_string(n));
        std::error_code ec; made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{work};
    const auto path = work / "test.st";
    std::vector<uint8_t> image(2048, 0);
    image[0x13] = 4; image[0x18] = 2; image[0x1A] = 2;
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) return 1;
    const bool written = std::fwrite(image.data(), 1, image.size(), file) == image.size();
    std::fclose(file);
    if (!written) return 1;
    for (uint8_t cmd : {0xD0, 0xD1, 0xD2, 0xD3}) {
        Rig r; r.select(5); r.reg(0, cmd);
        c.equal("Dx ready ne produit pas IRQ", r.irq(), false);
        c.equal("Dx idle moteur et statut I", r.reg(0) & 0xA1, 0x80);
        r.fdc.loadImage(path.string()); r.fdc.eject(); r.select(3);
        c.equal("changement média/sélection sans IRQ ready", r.irq(), false);
    }
    {
        Rig r; r.select(5); r.reg(0, 0xD8);
        c.equal("D8 immédiat", r.irq(), true);
        (void)r.reg(0); c.equal("D8 résiste lecture STR", r.irq(), true);
        r.reg(0, 0xD0); c.equal("D0 désarme mais garde IRQ", r.irq(), true);
        (void)r.reg(0); c.equal("STR acquitte après D0", r.irq(), false);
    }
    for (uint8_t cmd : {0x08, 0x88, 0xC8}) {
        Rig r; r.select(5); r.reg(0, cmd);
        const uint8_t before = r.reg(0);
        c.equal("commande occupée avant Dx", before & 1, 1);
        r.reg(0, 0xD0);
        c.equal("Dx occupé conserve statut hors BUSY", r.reg(0), before & ~1);
        c.equal("abandon sans IRQ", r.irq(), false);
    }
    {
        Rig r; c.equal("montage", r.fdc.loadImage(path.string()), true);
        const int64_t due = r.armIndex(); r.advance(due);
        c.equal("IRQ index", r.irq(), true);
        c.equal("INDEX avant éjection", r.reg(0) & 2, 2);
        r.fdc.eject(1); c.equal("éjection lecteur non sélectionné préserve INDEX", r.reg(0) & 2, 2);
        r.fdc.eject(); c.equal("éjection retire INDEX immédiatement", r.reg(0) & 2, 0);
        c.equal("réinsertion moteur actif", r.fdc.loadImage(path.string()), true);
        // La nouvelle phase déterministe n'est pas le créneau actif précédent.
        c.equal("réinsertion ne conserve pas ancien INDEX", r.reg(0) & 2, 0);
        r.fdc.eject();
        const int pulses = r.pulses;
        for (int n = 0; n < 10; ++n) r.next();
        c.equal("disque absent sans impulsion", r.pulses, pulses);
        c.equal("disque absent sans IRQ index", r.irq(), false);
        c.equal("DMA adresse intacte", r.address(), 0);
    }
    std::fprintf(stderr, "[fdc-force-interrupt] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
#ifdef NEOST_FDC_FORCE_INTERRUPT_TEST_MAIN
int main() { return testFdcForceInterrupt() ? 1 : 0; }
#endif
