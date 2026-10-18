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
        std::fprintf(stderr, "[fdc-selection] FAIL %s: got %lld, expected %lld\n",
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

int testFdcSelection() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-fdc-selection-" + std::to_string(n));
        std::error_code ec; made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    c.equal("répertoire temporaire exclusif", made, true);
    if (!made) return c.failures;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    std::filesystem::path paths[2] = {work / "a.st", work / "b.st"};
    for (int drive = 0; drive < 2; ++drive) {
        // 1 piste, 2 faces, 2 secteurs : la donnée distingue lecteur ET face.
        std::vector<uint8_t> image(2048, uint8_t(0xA0 + 0x10 * drive));
        std::fill(image.begin() + 512, image.begin() + 1024, uint8_t(0xA2 + 0x10 * drive));
        std::fill(image.begin() + 1536, image.end(), uint8_t(0xA3 + 0x10 * drive));
        image[0x13] = 4; image[0x14] = 0;
        image[0x18] = 2; image[0x19] = 0; image[0x1A] = 2; image[0x1B] = 0;
        FILE* file = std::fopen(paths[drive].string().c_str(), "wb");
        c.equal("création image", file != nullptr, true);
        if (!file) return c.failures;
        const bool written = std::fwrite(image.data(), 1, image.size(), file) == image.size();
        const bool closed = std::fclose(file) == 0;
        c.equal("image complète", written && closed, true);
        if (!written || !closed) return c.failures;
    }
    const struct { uint8_t porta, expected; } selections[] = {
        {5, 0xA2}, {4, 0xA3}, {3, 0xB2}, {2, 0xB3}, {1, 0xA2}, {0, 0xA3}
    };
    for (const auto& selection : selections) {
        Rig r;
        c.equal("montage lecteur A", r.fdc.loadImage(paths[0].string(), 0), true);
        c.equal("montage lecteur B", r.fdc.loadImage(paths[1].string(), 1), true);
        r.select(7); r.read(); r.next();
        c.equal("commande sans lecteur encore pendante", r.irq(), false);
        r.select(selection.porta);
        c.equal("sélection tardive sans accès FDC termine", r.finish(), true);
        c.equal("lecteur, face active bas et priorité A", r.bus.ram[0x2000], selection.expected);
    }
    {
        Rig r;
        c.equal("montage lecture multi-face", r.fdc.loadImage(paths[0].string()), true);
        r.select(5); r.read(1, 0x98, 2);
        unsigned events = 0;
        while (r.address() < 0x2200 && ++events < 100000 && r.next()) {}
        c.equal("premier secteur transféré", r.address(), 0x2200);
        r.select(4); // pas d'accès FDC avant la recherche du second secteur
        while (r.address() < 0x2400 && ++events < 100000 && r.next()) {}
        c.equal("second secteur transféré", r.address(), 0x2400);
        c.equal("face changée entre secteurs", r.bus.ram[0x2200], 0xA3);
    }
    // Live franchit une échéance pendant l'instruction, AVANT le dispatch FDC.
    // FDC_UpdateAll doit déjà émettre le front d'index depuis TR/SR/DR.
    for (bool write : {false, true}) for (uint16_t bits : {2, 4, 6}) {
        Rig r;
        c.equal("montage index registres", r.fdc.loadImage(paths[0].string()), true);
        const int64_t due = r.armIndex();
        c.equal("index armé dans le futur", due > r.live, true);
        r.live = due - 1;
        if (write) r.reg(bits, 0x35); else (void)r.reg(bits);
        c.equal("pas de front avant index", r.irq(), false);
        r.live = due;
        if (write) r.reg(bits, 0x36); else (void)r.reg(bits);
        c.equal("registre actualise index au cycle dû", r.irq(), true);
        c.equal("front index capturé par le MFP", r.pending(), true);
        c.equal("une seule impulsion index", r.pulses, 1);
        r.advance(due + 1);
        c.equal("dispatch suivant ne répète pas index", r.pulses, 1);
    }
    {
        Rig r;
        c.equal("montage index commande", r.fdc.loadImage(paths[0].string()), true);
        const int64_t due = r.armIndex(); r.live = due;
        r.reg(0, 0x08); // nouvelle commande efface INTRQ après le front
        c.equal("commande efface ensuite INTRQ", r.irq(), false);
        c.equal("front précédent commande reste dans IPRB", r.pending(), true);
    }
    {
        Rig r;
        c.equal("montage exclusions DMA et ACSI", r.fdc.loadImage(paths[0].string()), true);
        const int64_t due = r.armIndex(); r.live = due;
        r.mode(0x90); (void)r.fdc.read8(0xFF8605); r.data(1);
        r.mode(0x88); (void)r.fdc.read8(0xFF8605);
        r.mode(0x80); (void)r.address(); (void)r.fdc.read8(0xFF8607);
        c.equal("compteur, ACSI et DMA ne simulent pas accès WD", r.irq(), false);
        c.equal("statut WD garde sa mise à jour existante", r.reg(0) & 2, 2);
        c.equal("lecture STR acquitte le front actualisé", r.irq(), false);
        c.equal("lecture STR a bien produit le front", r.pending(), true);
    }
    std::fprintf(stderr, "[fdc-selection] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}

#ifdef NEOST_FDC_SELECTION_TEST_MAIN
int main() { return testFdcSelection() ? 1 : 0; }
#endif
