// Hatari mfp.c:1180-1219 : recomposer toute la ligne GPIP7 avant le front.
// Le bouton Multiface force déjà GPIP7 bas dans le modèle de port NeoST.
#include "io/Mfp.hpp"
#include <cstdio>

int testMfpGpipComposite() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, unsigned actual, unsigned expected) {
        ++checks;
        if (actual == expected) return;
        ++failures;
        std::fprintf(stderr, "[mfp-gpip-composite] FAIL %s: %u != %u\n",
                     label, actual, expected);
    };
    for (bool color : {false, true}) {
        for (bool rising : {false, true}) {
            for (bool output : {false, true}) {
                Mfp mfp;
                mfp.reset();
                mfp.setColorMonitor(color);
                mfp.setHasDmaSound(true);
                mfp.setMonitorButton(true);
                mfp.write8(0xFFFA03, rising ? 0x80 : 0);
                mfp.write8(0xFFFA05, output ? 0x80 : 0);
                mfp.write8(0xFFFA07, 0x80);
                for (bool xsint : {true, true, false}) {
                    mfp.setXsintLine(xsint);
                    equal("pin maintenue basse", mfp.read8(0xFFFA01) & 0x80, 0);
                    equal("aucun front sous bouton", mfp.ipra & 0x80, 0);
                    mfp.write8(0xFFFA0B, 0x7F);
                }
                mfp.setMonitorButton(false);
                equal("libération bouton selon AER et DDR", mfp.ipra & 0x80,
                      color && rising && !output ? 0x80 : 0);
                equal("lecture libération bouton", mfp.read8(0xFFFA01) & 0x80,
                      color && !output ? 0x80 : 0);
                mfp.write8(0xFFFA0B, 0x7F);
                mfp.setXsintLine(true);
                const bool pin = !color;
                equal("XSINT après libération", mfp.ipra & 0x80,
                      pin == rising && !output ? 0x80 : 0);
                mfp.write8(0xFFFA0B, 0x7F);
                mfp.setXsintLine(true);
                equal("XSINT identique sans front", mfp.ipra & 0x80, 0);
            }
        }
    }
    // Sans son DMA, XSINT ne participe pas au niveau GPIP7.
    {
        Mfp mfp;
        mfp.reset();
        mfp.write8(0xFFFA07, 0x80);
        mfp.setXsintLine(true);
        equal("ST sans XOR son", mfp.read8(0xFFFA01) & 0x80, 0x80);
        equal("ST sans IRQ XSINT", mfp.ipra & 0x80, 0);
    }
    std::fprintf(stderr, "[mfp-gpip-composite] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_MFP_GPIP_COMPOSITE_TEST_MAIN
int main() { return testMfpGpipComposite() ? 1 : 0; }
#endif
