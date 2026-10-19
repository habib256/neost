// MC68HC901 §4.1.3 : VR bits2..0 inutilisés, lecture zéro ; MAME REGISTER_VR.
#include "io/Mfp.hpp"
#include <cstdio>

int testMfpVectorRegister() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, int64_t got, int64_t expected) {
        ++checks;
        if (got == expected) return;
        ++failures;
        if (failures < 10)
            std::printf("  FAIL MFP VR %s: %lld attendu %lld\n", label,
                        static_cast<long long>(got), static_cast<long long>(expected));
    };
    for (int value = 0; value < 256; ++value) {
        Mfp mfp;
        mfp.write8(0xFFFA17, uint8_t(value));
        equal("bits réservés lisent zéro", mfp.read8(0xFFFA17), value & 0xF8);
    }
    {
        Mfp mfp;
        mfp.write8(0xFFFA17, 0x48);
        mfp.isra = 0x20;
        mfp.isrb = 2;
        mfp.write8(0xFFFA17, 0x4F);
        equal("bits réservés ne changent pas SEI", mfp.isra, 0x20);
        equal("bits réservés préservent ISRB", mfp.isrb, 2);
        mfp.write8(0xFFFA17, 0x47);
        equal("AEI efface ISRA", mfp.isra, 0);
        equal("AEI efface ISRB", mfp.isrb, 0);
    }
    {
        Mfp mfp;
        mfp.vr = 0xFF; // ancien registre brut public ou sauvegarde antérieure
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        mfp.serialize(save);
        mfp.vr = 0;
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        mfp.serialize(load);
        equal("ancien VR brut restauré", load.ok(), true);
        equal("lecture masque ancien VR brut", mfp.read8(0xFFFA17), 0xF8);
    }
    for (bool softwareEoi : {false, true}) {
        Mfp mfp;
        mfp.write8(0xFFFA17, softwareEoi ? 0xEF : 0xE7);
        mfp.write8(0xFFFA07, 0x20);
        mfp.write8(0xFFFA13, 0x20);
        mfp.raise(Mfp::SRC_TIMERA);
        mfp.flushIrqUpdate();
        equal("IRQ Timer A inchangée", mfp.irqPending(), true);
        equal("vecteur IACK utilise source, pas bits réservés", mfp.iack(), 0xED);
        equal("mode EOI conservé", mfp.isra, softwareEoi ? 0x20 : 0);
    }
    std::printf("[mfp-vector-register] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_MFP_VECTOR_REGISTER_TEST_MAIN
int main() { return testMfpVectorRegister() ? 1 : 0; }
#endif
