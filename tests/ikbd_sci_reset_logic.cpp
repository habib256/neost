// Oracle indépendant : tables de bits 8N1 et automate SCI de Hatari ikbd.c755-818.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>
namespace {
struct Oracle {
    unsigned state = 0, value = 0, left = 0;
    void bit(bool high) {
        switch (state) {
        case 0: if (!high) { state = 1; value = 0; left = 8; } break;
        case 1:
            value = (value >> 1) | (high ? 128 : 0);
            if (!--left) state = 2;
            break;
        default: state = 0; break; // stop0 abandonne, stop1 publie ; RSR reste intact
        }
    }
    // L'algorithme oracle décale après CHAQUE bit, production avant le suivant.
    unsigned raw() const { return state == 1 ? value >> 1 : value; }
};
struct Rig {
    Mfp mfp;
    Scheduler scheduler;
    Ikbd ikbd{mfp};
    int64_t live = 0;
    Rig(int phase = 0) {
        ikbd.setScheduler(&scheduler);
        scheduler.setLiveClock([this] { return live; });
        scheduler.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        scheduler.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        scheduler.setCallback(Scheduler::IKBD, [this] { ikbd.onResetResponse(); });
        live = phase; scheduler.runTo(live); ikbd.write8(0, 0x96);
    }
    void at(int64_t cycle) {
        live = cycle;
        do { scheduler.runTo(cycle); }
        while (scheduler.nextDue() >= 0 && scheduler.nextDue() <= cycle);
    }
    int64_t bootDue() {
        auto remaining = scheduler.rawCyclesUntil(Scheduler::IKBD);
        return remaining == INT64_MIN ? -1 : live + remaining;
    }
};
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t actual, int64_t expected) {
        ++count;
        if (actual == expected) return;
        ++failures;
        if (failures < 12) std::fprintf(stderr, "[ikbd-sci-reset] FAIL %s: %lld != %lld\n",
                                       label, (long long)actual, (long long)expected);
    }
};
#ifndef NEOST_SCI_RESET_BASELINE
void compareSci(Checks& checks, Rig& rig, const Oracle& oracle) {
    // serialize est l'API publique d'observation de l'état, sans hook produit.
    std::vector<uint8_t> bytes;
    auto save = StateArchive::saver(bytes); rig.ikbd.serialize(save);
    auto read = StateArchive::loader(bytes.data(), bytes.size());
    uint32_t queued; read(queued);
    uint8_t u; bool b;
    for (uint32_t i = 0; i < queued; ++i) read(u);
    read(u); read(b); read(b); read(u); read(b); read(b); read(b); // RDR/états RX
    read(u); read(b); read(b); read(b); read(u); read(u); read(b); // CR/init/TIE/TDRE/TDR/TSR/busy
    int64_t clock; read(clock); read(clock); read(u); // phase, next bit, index TX
    uint8_t state, value, left; read(state); read(value); read(left);
    checks.equal("état SCI contre oracle", state, oracle.state);
    checks.equal("bits SCI restants contre oracle", left, oracle.left);
    checks.equal("assemblage SCI contre oracle", value, oracle.raw());
}
#endif
}
int testIkbdSciReset() {
    Checks checks;
    // 00 tronqué devient 80 au SCI. Reçu SEUL pendant l'init de la ROM (sei $F010 …
    // cli $F139), il attend dans le RDR et s'exécute après : le 01 envoyé après la
    // fenêtre complète la commande et relance réellement le boot.
    for (int phase : {0, 37}) for (int offset : {0, 173}) for (bool late : {false, true}) {
        Rig rig(phase);
        rig.ikbd.write8(2, 0);
        const int64_t start = phase + 1024;
        rig.at(start + offset); rig.ikbd.resetHw();
        const int64_t reset = rig.live;
        const int64_t assembled = reset + 10*1024;
        rig.at(assembled); // le SCI reçoit 80 au stop MARK après le TSR
        const int64_t after = assembled + 50*10240;   // au-delà de la fenêtre, sur la grille TX
        rig.at(reset + 502000); rig.at(after);
        rig.ikbd.write8(2, 1);
        const int64_t commandStop = after + 10*1024;
        if (late) {
            rig.live = commandStop + 3197;
            rig.ikbd.write8(0, 0x96); // rattrapage MMIO, Scheduler encore au passé
        } else rig.at(commandStop);
        checks.equal("80 retenu pendant l'init puis 01 relance le boot", rig.bootDue(), commandStop + 502000);
    }
    {
        // Contre-épreuve : 80 puis 01 tous deux pendant l'init — le second déborde, la
        // routine $FF40 jette tout, pas de reboot.
        Rig rig; rig.ikbd.write8(2, 0); rig.at(1024); rig.ikbd.resetHw();
        const int64_t reset = rig.live;
        rig.at(reset + 10*1024); rig.ikbd.write8(2, 1); rig.at(reset + 20*1024);
        checks.equal("80 puis 01 pendant l'init : perdus, boot inchangé", rig.bootDue(), reset + 502000);
    }
    {
        Rig rig; rig.ikbd.write8(2, 0xFF); rig.at(1123); rig.ikbd.resetHw();
        const int64_t reset = rig.live;
        rig.at(reset + 10*1024); rig.ikbd.write8(2, 1); rig.at(reset + 20*1024);
        checks.equal("FF sans zéro ne redétecte aucun start", rig.bootDue(), reset + 502000);
    }
    // Contamination sur le TDR suivant : 00 tronqué après D2 + 1C produit
    // 10 (commande Y-up), au lieu des commandes intactes 00 puis 1C.
    {
        Rig rig; rig.ikbd.write8(2, 0); rig.at(4096 + 173); rig.ikbd.resetHw();
        const int64_t reset = rig.live;
        rig.ikbd.receiveSerialByte(0x0F); // état témoin Y-down, après le boot ROM
        rig.ikbd.write8(2, 0x1C);
        rig.at(reset + 30*1024);
        rig.at(reset + 502000); rig.at(reset + 512240);
        checks.equal("réponse premier boot", rig.ikbd.read8(2), 0xF1);
        for (int i = 0; i < 21; ++i) rig.ikbd.onVbl(20000);
        rig.ikbd.mouseEvent(0, 3, false, false); rig.ikbd.onAutoSend();
        for (unsigned expected : {0xF8u, 0u, 3u}) {
            rig.at(rig.live + 10240);
            checks.equal("commande10 reconstruite change réellement axe Y", rig.ikbd.read8(2), expected);
        }
    }
#ifndef NEOST_SCI_RESET_BASELINE
    for (int saveTick : {2, 9}) {
        Rig rig; rig.ikbd.write8(2, 0); rig.at(1197); rig.ikbd.resetHw();
        const int64_t reset = rig.live;
        rig.at(reset + saveTick*1024); rig.ikbd.write8(0, 0x96);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        rig.ikbd.serialize(save); rig.scheduler.serialize(save);
        Rig restored; restored.live = rig.live;
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        restored.ikbd.serialize(load); restored.scheduler.serialize(load);
        checks.equal("archive SCI DATA ou STOP valide", load.ok(), true);
        restored.at(reset + 10*1024);                 // 80 assemblé, retenu pendant l'init
        const int64_t after = reset + 10*1024 + 50*10240;
        restored.at(reset + 502000); restored.at(after);
        restored.ikbd.write8(2, 1); restored.at(after + 10*1024);
        checks.equal("archive conserve assemblage80 et parsing01", restored.bootDue(), after + 10*1024 + 502000);
    }
    // Deux resets successifs abandonnent aussi le premier assemblage incomplet.
    {
        Rig rig; rig.ikbd.write8(2, 0); rig.at(1197); rig.ikbd.resetHw();
        rig.at(1197 + 2*1024 + 91); rig.ikbd.resetHw();
        const int64_t reset = rig.live;
        Oracle oracle;
        for (unsigned i = 0; i < 20; ++i) {
            const bool bit = i >= 6; // D2..D7 restent nuls, puis STOP et MARK
            rig.at(reset + (i+1)*1024); rig.ikbd.write8(0, 0x96);
            oracle.bit(bit); compareSci(checks, rig, oracle);
        }
    }
#endif
#ifndef NEOST_SCI_RESET_BASELINE
    for (unsigned original : {0u, 0xFFu, 0x16u, 0x55u, 0x80u})
    for (unsigned cut = 0; cut <= 8; ++cut)
    for (bool follower : {false, true}) {
        Rig rig(37); rig.ikbd.write8(2, uint8_t(original));
        const int64_t reset = 37 + 1024 + cut*1024 + 173;
        rig.at(reset); rig.ikbd.resetHw();
        if (follower) rig.ikbd.write8(2, 0x1C);
        std::vector<bool> stream;
        for (unsigned i = cut; i < 8; ++i) stream.push_back((original >> i) & 1);
        stream.push_back(true); // stop TSR initial
        if (follower) {
            stream.push_back(false);
            for (unsigned i = 0; i < 8; ++i) stream.push_back((0x1C >> i) & 1);
            stream.push_back(true);
        }
        for (int i = 0; i < 12; ++i) stream.push_back(true);
        Oracle oracle;
        for (size_t tick = 0; tick < stream.size(); ++tick) {
            const int64_t time = reset + int64_t(tick+1)*1024;
            rig.at(time); rig.ikbd.write8(0, 0x96); // synchronisation publique sans changement CR
            oracle.bit(stream[tick]); compareSci(checks, rig, oracle);
        }
    }
#endif
    std::fprintf(stderr, "[ikbd-sci-reset] %d checks, %d FAIL\n", checks.count, checks.failures);
    return checks.failures;
}
#ifdef NEOST_IKBD_SCI_RESET_TEST_MAIN
int main() { return testIkbdSciReset() ? 1 : 0; }
#endif
