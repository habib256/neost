// Émission USART à travers Bus/Machine : les échéances doivent être exécutées.
// [B] MC68901, janvier 1984, § 6.3.2 p. 6-7 (MC68901_Jan84.pdf, sha256 5441a092) : UE se pose quand le registre
//     à décalage se vide avant une écriture du tampon, et s'efface à la lecture du TSR.
// [D] MAME mc68901.cpp:530-537 @ ce391499d37d (REGISTER_TSR) : l'effacement n'a lieu qu'après une
//     période complète de l'horloge d'émission. La fiche ne donne pas ce délai.
#include "core/Machine.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
#include <memory>
#include <vector>

int testMachineMfpTx() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[machine-mfp-tx] FAIL %s\n", label); }
    };
    for (auto profile : {MachineType::St, MachineType::MegaSte}) {
        auto m = std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, profile);
        std::vector<uint8_t> output;
        m->mfp.setSerialSink([&](uint8_t b) { output.push_back(b); });
        m->mfp.setLoopback(true);
        m->bus.write8(0xFFFA07, 0x06); // interruptions TXEMPTY et TXERR
        m->bus.write8(0xFFFA29, 0x88); // 8N1 ; débit hôte initial 9600 bauds
        m->bus.write8(0xFFFA2B, 1);    // récepteur actif
        m->bus.write8(0xFFFA2D, 1);    // émetteur actif
        const int64_t start = m->sched.liveNow();
        m->bus.write8(0xFFFA2F, 0x51);
        const int64_t duration = (neost::pacing::kCpuHzInt * 10 + 9599) / 9600;
        check(m->sched.cyclesUntil(Scheduler::SERIAL_TX) == duration,
              "Bus write schedules a full character");
        check(output.empty(), "no output at CPU write");
        check((m->bus.read8(0xFFFA0B) & 6) == 4, "initial TDR transfer raises only TXEMPTY");
        m->bus.write8(0xFFFA2F, 0x52);
        check(!(m->bus.read8(0xFFFA2D) & 0x80), "pending TDR is not empty");
        m->sched.runTo(start + duration - 1);
        check(output.empty() && !(m->bus.read8(0xFFFA2B) & 0x80),
              "sink and loopback wait for character completion");
        m->sched.runTo(start + duration);
        check(output == std::vector<uint8_t>{0x51}, "Machine callback emits first character");
        check(m->bus.read8(0xFFFA2F) == 0x51, "loopback receives completed character");
        check(m->bus.read8(0xFFFA2D) & 0x80, "holding byte transferred to TSR");
        const int64_t next = m->sched.now() + m->sched.cyclesUntil(Scheduler::SERIAL_TX);
        m->sched.runTo(next);
        check(output == std::vector<uint8_t>({0x51, 0x52}), "second character follows on serial grid");
        check(m->bus.read8(0xFFFA2D) & 0x40, "idle transmitter reports underrun");
        // Deux caractères, puis une période bit avant qu’une lecture puisse
        // effacer UE. La lecture éligible restitue encore le bit mémorisé.
        m->sched.runTo(start + (neost::pacing::kCpuHzInt * 21 + 9599) / 9600);
        check(m->bus.read8(0xFFFA2D) & 0x40, "eligible TSR read still returns underrun");
        check(!(m->bus.read8(0xFFFA2D) & 0x40), "TSR read clears underrun status");
        m->bus.write8(0xFFFA2F, 0x53);
        m->reset();
        check(m->sched.cyclesUntil(Scheduler::SERIAL_TX) < 0, "reset cancels in-flight transmission");
        m->sched.runTo(next + duration * 2);
        check(output.size() == 2, "reset does not emit cancelled byte");
        m->mfp.setSerialSink({});
    }
    std::printf("[machine-mfp-tx] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
