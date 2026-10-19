// Ricoh EK-086-9908 p7 : Eh/Fh uniquement en écriture.
// MAME rp5c15_device::read renvoie 0 pour REGISTER_TEST/REGISTER_RESET.
#include "io/Rtc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int got, int expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[rtc-write-only] FAIL %s: got %d, expected %d\n", label, got, expected);
    }
};
uint32_t address(int index) { return 0xFFFC21 + 2 * index; }
}

int testRtcWriteOnly() {
    Checks check;
    for (uint8_t bank : {8, 9}) {
        for (uint8_t value : {1, 2, 15, 255}) {
            Rtc rtc;
            rtc.setDateTime({10, 20, 3, 2, 15, 6, 24});
            rtc.write8(address(13), bank);
            rtc.write8(address(14), value);
            rtc.write8(address(15), value);
            check.equal("TEST sans lecture du latch", rtc.read8(address(14)), 0);
            check.equal("RESET sans lecture du latch", rtc.read8(address(15)), 0);
            std::vector<uint8_t> bytes;
            auto save = StateArchive::saver(bytes);
            rtc.serialize(save);
            Rtc restored;
            auto load = StateArchive::loader(bytes.data(), bytes.size());
            restored.serialize(load);
            check.equal("archive compatible", load.ok(), true);
            check.equal("TEST reste non relisible après archive", restored.read8(address(14)), 0);
            check.equal("RESET reste non relisible après archive", restored.read8(address(15)), 0);
        }
    }
    // Les écritures gardent leurs effets matériels ; les lectures répétées
    // ne relancent ni RESET alarme ni remise à zéro du diviseur.
    int64_t clock = 0;
    Rtc rtc;
    rtc.setClock([&] { return clock; });
    rtc.setSecondCycles(100);
    rtc.setDateTime({10, 20, 3, 2, 15, 6, 24});
    rtc.write8(address(13), 9);
    rtc.write8(address(2), 7);
    clock = 25;
    rtc.write8(address(15), 3);
    check.equal("RESET efface alarme", rtc.read8(address(2)) & 15, 0);
    rtc.write8(address(2), 6);
    clock = 75;
    check.equal("lecture RESET retourne zéro", rtc.read8(address(15)), 0);
    check.equal("lecture RESET ne réexécute pas commande", rtc.read8(address(2)) & 15, 6);
    clock = 124;
    check.equal("RESET phase avant seconde", rtc.getDateTime().sec, 10);
    clock = 125;
    check.equal("RESET phase seconde exacte", rtc.getDateTime().sec, 11);
    std::fprintf(stderr, "[rtc-write-only] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_RTC_WRITE_ONLY_TEST_MAIN
int main() { return testRtcWriteOnly() ? 1 : 0; }
#endif
