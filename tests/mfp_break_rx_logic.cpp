// BREAK reçu par l'USART du MFP en bouclage (H/L = 11) : mot nul + B, fin de BREAK.
// [B] MC68901, janvier 1984 (MC68901_Jan84.pdf), Fig. 6-3 p. 6-5 (F/S or B) : « A break is
//     detected when an all zero data word with no stop bit is received » ; B retombe quand un
//     bit non nul arrive ET que le RSR a été lu depuis le début du BREAK, avec une interruption
//     de fin de BREAK. § 6.2.3 p. 6-5 : buffer plein, pas d'overrun, B seul à la lecture de l'UDR.
//     § 6.2.1 p. 6-4 : canal d'erreur s'il est validé, sinon canal de buffer plein.
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>

int testMfpBreakRx() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* name, int64_t got, int64_t want) {
        ++checks;
        if (got != want) { ++failures; std::printf("  FAIL MFP BREAK RX %s: %lld attendu %lld\n", name, (long long)got, (long long)want); }
    };
    struct Rig {
        Scheduler s; Mfp m;
        explicit Rig(uint8_t iera) {
            m.setScheduler(&s);
            s.setCallback(Scheduler::SERIAL_TX, [this] { m.onSerialTxEvent(); });
            m.write8(0xFFFA29, 0x88);                  // /16, 8 bits, asynchrone 1 stop
            m.write8(0xFFFA07, iera);
            m.write8(0xFFFA2B, 0x01);                  // RE
        }
        void step() { const int64_t d = s.nextDue(); if (d >= 0) s.runTo(d); }
        int rsr() { return m.read8(0xFFFA2B); }
    };
    {
        Rig r(0x08 | 0x10);                            // RXERR et RXFULL validés
        r.m.write8(0xFFFA2D, 0x0F);                    // TE, bouclage interne, BREAK
        r.step();                                      // premier intervalle : mot nul sans stop
        eq("break word fills the buffer", r.m.read8(0xFFFA2B) & 0x88, 0x88);
        eq("break on error channel", r.m.ipra & 0x18, 0x08);
        eq("break word is zero", r.m.read8(0xFFFA2F), 0);
        r.m.write8(0xFFFA0B, 0xE7);                    // IPRA effacé
        r.step();                                      // le BREAK continue : pas de nouveau mot
        eq("continuing break: no second word", r.rsr() & 0x80, 0);
        eq("B stays while line low", r.rsr() & 0x08, 0x08);
        r.m.write8(0xFFFA0B, 0xE7);
        r.m.write8(0xFFFA2D, 0x07);                    // fin du BREAK : stop de fin, ligne haute
        eq("end of break after RSR read clears B", r.rsr() & 0x08, 0);
        eq("end of break interrupt", r.m.ipra & 0x08, 0x08);
    }
    {
        // RSR non lu pendant le BREAK : B tient jusqu'à la lecture, puis retombe.
        Rig r(0x08);
        r.m.write8(0xFFFA2D, 0x0F);
        r.step();
        (void)r.m.read8(0xFFFA2F);
        r.m.write8(0xFFFA0B, 0xE7);
        r.m.write8(0xFFFA2D, 0x07);
        eq("no end interrupt before RSR read", r.m.ipra & 0x08, 0);
        eq("first RSR read still shows B", r.rsr() & 0x08, 0x08);
        eq("B cleared after the read", r.rsr() & 0x08, 0);
        eq("end interrupt at the read", r.m.ipra & 0x08, 0x08);
    }
    {
        // Canal d'erreur désactivé : le BREAK passe par le canal de buffer plein.
        Rig r(0x10);
        r.m.write8(0xFFFA2D, 0x0F);
        r.step();
        eq("break on buffer-full channel", r.m.ipra & 0x18, 0x10);
    }
    {
        // Buffer plein : pas d'overrun, B seul à la lecture de l'UDR (§ 6.2.3 cas 1).
        Rig r(0x08 | 0x10);
        r.m.write8(0xFFFA2D, 0x07);
        r.m.write8(0xFFFA2F, 0x5A);
        r.step(); r.step();                            // caractère bouclé
        eq("looped byte received", r.m.read8(0xFFFA2B) & 0x80, 0x80);
        r.m.write8(0xFFFA0B, 0xE7);
        r.m.write8(0xFFFA2D, 0x0F);
        for (int i = 0; i < 3 && !(r.m.ipra & 0x08); ++i) r.step();
        eq("break behind full buffer: no immediate flag", r.rsr() & 0x48, 0);
        eq("full buffer keeps the data", r.m.read8(0xFFFA2F), 0x5A);
        eq("B appears after the UDR read, without OE", r.rsr() & 0x48, 0x08);
        eq("break interrupt at the UDR read", r.m.ipra & 0x08, 0x08);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes); r.m.serialize(save); r.s.serialize(save);
        Rig q(0);
        auto load = StateArchive::loader(bytes.data(), bytes.size()); q.m.serialize(load); q.s.serialize(load);
        eq("archive BREAK RX", load.ok(), 1);
        q.m.write8(0xFFFA2D, 0x07);
        eq("restored break ends on the stop", q.rsr() & 0x08, 0);
    }
    {
        // Sans bouclage, le BREAK émis n'atteint pas le récepteur.
        Rig r(0x08 | 0x10);
        r.m.write8(0xFFFA2D, 0x09);
        r.step();
        eq("no loopback: nothing received", r.rsr() & 0x88, 0);
    }
    std::printf("[mfp-break-rx] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
