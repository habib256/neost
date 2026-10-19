// FF8920 : octet sans interception STE, sauvegardé comme l'IoMem d'Hatari.
#include "core/Bus.hpp"
#include "core/DmaSound.hpp"
#include <cstdio>
#include <vector>
int testDmaSoundMmioState() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* label, unsigned got, unsigned want) {
        ++checks;
        if (got == want) return;
        ++failures;
        std::fprintf(stderr, "[dmasound-mmio-state] FAIL %s: %u != %u\n", label, got, want);
    };

    for (auto machine : {MachineType::Ste, MachineType::MegaSte}) {
        Bus bus;
        DmaSound snd(bus);
        bus.machine = machine;
        bus.dmasnd = &snd;
        eq("scratch initial",bus.read8(0xFF8920),0);
        bus.write16(0xFF8920,0xA583);
        eq("écriture mot scratch",bus.read8(0xFF8920),0xA5);
        eq("mode compagnon",bus.read8(0xFF8921),0x83);
        std::vector<uint8_t> state;
        {
            auto ar = StateArchive::saver(state);
            snd.serialize(ar);
        }
        bus.write16(0xFF8920,0x5A00);
        {
            auto ar = StateArchive::loader(state.data(), state.size());
            snd.serialize(ar);
            eq("archive valide", ar.ok(), true);
        }
        eq("scratch restauré même instance",bus.read8(0xFF8920),0xA5);
        eq("mode restauré",bus.read8(0xFF8921),0x83);
        Bus freshBus;
        DmaSound fresh(freshBus);
        freshBus.machine = machine;
        freshBus.dmasnd = &fresh;
        {
            auto ar = StateArchive::loader(state.data(), state.size());
            fresh.serialize(ar);
            eq("archive instance neuve", ar.ok(), true);
        }
        eq("scratch restauré instance neuve",freshBus.read8(0xFF8920),0xA5);
        bus.write8(0xFF8920,0xA5);
        snd.reset(false);
        eq("reset chaud garde scratch",bus.read8(0xFF8920),0xA5);
        snd.reset(true);
        eq("reset froid composant garde scratch",bus.read8(0xFF8920),0xA5);
        bus.write8(0xFF8903,0xFF);
        bus.write8(0xFF890F,0xFF);
        eq("masque début",bus.read8(0xFF8903),0x3F);
        eq("masque fin",bus.read8(0xFF890F),0x3F);
        eq("contrôle pair zéro",bus.read8(0xFF8900),0);
    }
    std::fprintf(stderr,"[dmasound-mmio-state] %d checks, %d FAIL\n",checks, failures);
    return failures;
}
#ifdef NEOST_DMASOUND_MMIO_STATE_TEST_MAIN
int main() { return testDmaSoundMmioState() ? 1 : 0; }
#endif
