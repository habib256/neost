// [D] Hatari sound.c:439-440, 1046 @ f0736b24 : filtre C10 du STF à 7,7 et 2,1 kHz, bruit R6=0 mesuré égal à R6=1.
#include "core/YM2149.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
void setReg(YM2149& psg, int reg, int value) {
    psg.write8(0xff8800, uint8_t(reg));
    psg.write8(0xff8802, uint8_t(value));
}

std::vector<float> render(int r6, bool noise, bool stfLowPass, int tonePeriod) {
    YM2149 psg;
    psg.reset();
    psg.setHpfBypass(true);
    psg.setStfLowPass(stfLowPass);
    setReg(psg, 0, tonePeriod);
    setReg(psg, 1, 0);
    setReg(psg, 6, r6);
    setReg(psg, 7, noise ? 0x37 : 0x3E);   // voie A : bruit seul, ou ton seul
    setReg(psg, 8, 15);
    std::vector<float> out(8192);
    psg.synthesize(out.data(), 8192, 44100);
    return out;
}

float swing(const std::vector<float>& s) {
    const auto first = s.begin() + 4096;   // le filtre et le rééchantillonneur sont établis
    const auto [lo, hi] = std::minmax_element(first, s.end());
    return *hi - *lo;
}
}

bool testYmC10Noise() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) { ++failures; std::fprintf(stderr, "[ym-c10-noise] FAIL %s\n", name); }
    };
    check(render(0, true, false, 1) == render(1, true, false, 1), "noise: R6=0 renders like R6=1");
    check(render(0, true, false, 1) != render(2, true, false, 1), "noise: R6=2 differs from R6=1");
    // Ton à 10,4 kHz, au-dessus des deux coupures : C10 l'écrase. Avec les coefficients 44,1 kHz appliqués
    // à 250 kHz (43 et 11 kHz), il passait presque entier.
    const float stf = swing(render(0, false, true, 12));
    const float ste = swing(render(0, false, false, 12));
    std::fprintf(stderr, "[ym-c10-noise] swing STF %.4f, sans C10 %.4f\n", stf, ste);
    check(stf < 0.6f * ste, "C10: a 10.4 kHz tone is attenuated");
    return failures == 0;
}
