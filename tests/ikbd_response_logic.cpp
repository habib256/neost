// Délais firmware des réponses IKBD, sans modifier la cadence série des octets.
// Référence : Hatari ikbd.c Send_Byte_Delay / SCI_Set_Line_TX et Cmd_Read/Report*.
// Les commandes sont injectées à la sortie du récepteur série : ce banc
// mesure le firmware ; le transport CPU est couvert par ikbd_cpu_serial_logic.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[ikbd-responses] FAIL %s: got %lld, expected %lld\n",
                     label, (long long)got, (long long)expected);
    }
};
struct Rig {
    Scheduler sched;
    Mfp mfp;
    Ikbd ikbd{mfp};
    int64_t live = 0;
    Rig() {
        ikbd.setScheduler(&sched);
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        sched.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        sched.setCallback(Scheduler::IKBD, [this] { ikbd.onResetResponse(); });
        ikbd.write8(0xFFFC00, 0x96);
        ikbd.setClock(26, 10, 18, 12, 34, 56);
    }
    void command(std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) ikbd.receiveSerialByte(b);
    }
    void advance(int64_t cycle) { live = cycle; sched.runTo(cycle); }
    int ready() { return ikbd.read8(0xFFFC00) & 1; }
    int data() { return ikbd.read8(0xFFFC02); }
    int64_t due() { return sched.rawCyclesUntil(Scheduler::IKBD_RX) + live; }
    std::vector<uint8_t> save() {
        std::vector<uint8_t> bytes;
        auto ar = StateArchive::saver(bytes); ikbd.serialize(ar); sched.serialize(ar);
        return bytes;
    }
    bool load(const std::vector<uint8_t>& bytes) {
        auto ar = StateArchive::loader(bytes.data(), bytes.size());
        ikbd.serialize(ar); sched.serialize(ar); live = sched.now(); return ar.ok();
    }
    void froggies() {
        // Handler installé directement : le vrai programme 6301 n'est pas versionné
        // et l'empreinte d'Hatari, non linéaire, ne se forge pas (cf. Ikbd.cpp).
        ikbd.armFroggiesForTest();
    }
};
}

int testIkbdResponses() {
    Checks c;
    {
        // Empreinte des programmes 6301 custom : valeurs du vrai utils.c d'Hatari
        // compilé (2026-10-20). Le OU exclusif d'un CRC32 standard rend 0xcbf43926
        // sur « 123456789 » (complément) et ne reconnaissait aucun programme réel.
        const uint8_t digits[] = {'1','2','3','4','5','6','7','8','9'};
        uint8_t zeros[167] = {}, ones[16], ramp[64];
        for (auto& b : ones) b = 0xFF;
        for (int i = 0; i < 64; ++i) ramp[i] = uint8_t(i * 37 + 11);
        c.equal("custom code crc digits", Ikbd::customCodeCrcForTest(digits, 9), 0xf489df43u);
        c.equal("custom code crc zeros", Ikbd::customCodeCrcForTest(zeros, 167), 0x8b948443u);
        c.equal("custom code crc ones", Ikbd::customCodeCrcForTest(ones, 16), 0x03bf0b6du);
        c.equal("custom code crc ramp", Ikbd::customCodeCrcForTest(ramp, 64), 0x13016f2du);
    }
    struct Case { uint8_t opcode, header; int delay, size; };
    const Case cases[] = {
        {0x0D, 0xF7, 10240, 6}, {0x16, 0xFD, 8192, 3}, {0x1C, 0xFC, 7168, 7},
        {0x21, 0xF6, 7168, 8}, {0x87, 0xF6, 7168, 8}, {0x88, 0xF6, 7168, 8},
        {0x89, 0xF6, 7168, 8}, {0x8A, 0xF6, 7168, 8}, {0x8B, 0xF6, 7168, 8},
        {0x8C, 0xF6, 7168, 8}, {0x8F, 0xF6, 7168, 8}, {0x90, 0xF6, 7168, 8},
        {0x92, 0xF6, 7168, 8}, {0x94, 0xF6, 7168, 8}, {0x95, 0xF6, 7168, 8},
        {0x99, 0xF6, 7168, 8}, {0x9A, 0xF6, 7168, 8},
    };
    for (const auto& tc : cases) {
        Rig r;
        r.live = 100; // MMIO au milieu d'un quantum, sched.now() reste à zéro.
        // $0D ne répond qu'en mode absolu (ROM $FB39) : $09 d'abord (sans réponse).
        if (tc.opcode == 0x0D) r.command({0x09, 0x01, 0x40, 0x00, 0xC8});
        if (tc.opcode == 0x21) r.command({tc.opcode, 0, 0});
        else r.command({tc.opcode});
        c.equal("firmware wait uses live clock", r.due(), 100 + tc.delay);
        r.advance(100 + tc.delay);
        c.equal("wait ends before serial byte", r.ready(), 0);
        c.equal("serial delay remains additive", r.due(), 100 + tc.delay + 10240);
        r.advance(100 + tc.delay + 10239);
        c.equal("header not one cycle early", r.ready(), 0);
        r.advance(100 + tc.delay + 10240);
        c.equal("header delivered", r.data(), tc.header);
        for (int i = 1; i < tc.size; ++i) {
            r.advance(100 + tc.delay + (i + 1) * 10240);
            c.equal("payload byte at unchanged cadence", r.ready(), 1);
            const int b = r.data();
            if (tc.opcode == 0x1C) {
                const uint8_t clock[] = {0x26, 0x10, 0x18, 0x12, 0x34, 0x56};
                c.equal("clock snapshot payload", b, clock[i - 1]);
            }
        }
        c.equal("response ends without extra byte", r.sched.cyclesUntil(Scheduler::IKBD_RX), -1);
    }
    {
        // Comportements tirés de la ROM 6301 désassemblée (MiSTery ikbd/rom/IKBD.ASM).
        auto collect = [](Rig& r, int n) {
            std::vector<int> out;
            for (int k = 0; k < 40 && int(out.size()) < n; ++k) {
                const int64_t d = r.sched.rawCyclesUntil(Scheduler::IKBD_RX);
                if (d == INT64_MIN) break;
                r.advance(r.live + d);
                if (r.ready()) out.push_back(r.data());
            }
            return out;
        };
        {   // $8C après boot : échelle 1/1 ($F106).
            Rig r; r.command({0x8C});
            const auto b = collect(r, 4);
            c.equal("$8C default scale X", b.size() > 2 ? b[2] : -1, 1);
            c.equal("$8C default scale Y", b.size() > 3 ? b[3] : -1, 1);
        }
        {   // $0E pose la position ET le mode absolu ($FB5F) : $0D répond.
            Rig r; r.command({0x0E, 0, 0x00, 0x12, 0x00, 0x34}); r.command({0x0D});
            const auto b = collect(r, 6);
            c.equal("$0E then $0D header", b.size() > 0 ? b[0] : -1, 0xF7);
            c.equal("$0E then $0D X", b.size() > 3 ? b[3] : -1, 0x12);
        }
        {   // $0D hors mode absolu : ignoré.
            Rig r; r.command({0x0D});
            c.equal("$0D in relative mode is ignored", r.sched.rawCyclesUntil(Scheduler::IKBD_RX) == INT64_MIN, 1);
        }
        {   // $1A puis $16 : refusé ; $1A puis $94 : le mode événement reste ($14).
            Rig r; r.command({0x1A}); r.command({0x16});
            c.equal("$16 refused when joysticks disabled", r.sched.rawCyclesUntil(Scheduler::IKBD_RX) == INT64_MIN, 1);
            r.command({0x94});
            const auto b = collect(r, 2);
            c.equal("$1A keeps event mode in $94", b.size() > 1 ? b[1] : -1, 0x14);
        }
        {   // $19 : $94 rapporte $19 + les 6 paramètres ($FCAE).
            Rig r; r.command({0x19, 1, 2, 3, 4, 5, 6}); r.command({0x94});
            const auto b = collect(r, 8);
            c.equal("$94 in keycode mode", b.size() > 1 ? b[1] : -1, 0x19);
            c.equal("$94 keycode last parameter", b.size() > 7 ? b[7] : -1, 6);
        }
        {   // Fenêtre de reset : un octet reçu pendant l'init s'exécute après $F1.
            Rig r; r.ikbd.bootRom(); r.command({0x12});
            r.advance(502000); const auto b = collect(r, 1);
            c.equal("$F1 after boot", b.size() > 0 ? b[0] : -1, 0xF1);
            r.command({0x92});
            const auto st = collect(r, 2);
            c.equal("single byte during init executed", st.size() > 1 ? st[1] : -1, 0x12);
        }
        {   // Deux octets pendant l'init : overrun, tout est perdu ($FF40).
            Rig r; r.ikbd.bootRom(); r.command({0x12, 0x1A});
            r.advance(502000); collect(r, 1);
            r.command({0x92});
            const auto st = collect(r, 2);
            c.equal("two bytes during init are lost", st.size() > 1 ? st[1] : -1, 0x00);
            r.command({0x9A});
            const auto sj = collect(r, 2);
            c.equal("lost $1A leaves joysticks enabled", sj.size() > 1 ? sj[1] : -1, 0x00);
        }
        {   // $18 : octets de 8 échantillons du feu joystick 1, sans en-tête.
            Rig r;
            r.sched.setCallback(Scheduler::IKBD_MONITOR, [&r] { r.ikbd.onJoystickMonitor(); });
            r.command({0x18}); r.ikbd.setJoystick(0, 0x80);
            // Une source ne tire qu'une fois par runTo : avancer par petits pas,
            // comme la machine (synchronisation à chaque accès CPU).
            // 8 tours de ~177 µs + les interruptions 1 ms du 6301 (128/215 µs).
            for (int64_t t = 200; t <= 20000; t += 200) r.advance(t);
            const auto b = collect(r, 1);
            c.equal("$18 fire sample byte", b.size() > 0 ? b[0] : -1, 0xFF);
        }
        {   // $18 : échantillons aux instants de la ROM (177 µs enfoncé, 180 µs
            // relâché, ≈ 1420/1444 cycles CPU). 4 enfoncés puis 4 relâchés → $F0.
            Rig r;
            r.sched.setCallback(Scheduler::IKBD_MONITOR, [&r] { r.ikbd.onJoystickMonitor(); });
            r.ikbd.setJoystick(0, 0x80);
            r.command({0x18});
            for (int64_t t = 100; t <= 5000; t += 100) r.advance(t);   // 4 échantillons : 0, 1420, 2840, 4259
            r.ikbd.setJoystick(0, 0x00);
            for (int64_t t = 5100; t <= 12000; t += 100) r.advance(t);
            const auto b = collect(r, 1);
            c.equal("$18 byte mixes pressed and released samples", b.size() > 0 ? b[0] : -1, 0xF0);
        }
        {   // $18 lancé tard : les tics 1 ms du 6301 gardent leur phase. Une seconde
            // vaut 8 021 248 cycles, soit 1 000 tics (parité incluse) : le premier
            // octet doit arriver au même délai à t0, t0 + 2 s et t0 + 60 s. Avec une
            // milliseconde arrondie à 8 021 cycles, la phase dérivait de 0,248 cycle/ms.
            auto firstByte = [](int64_t t0) -> int64_t {
                Rig r;
                r.sched.setCallback(Scheduler::IKBD_MONITOR, [&r] { r.ikbd.onJoystickMonitor(); });
                r.advance(t0);
                r.ikbd.setJoystick(0, 0x80);
                r.command({0x18});
                for (int64_t t = t0 + 20; t <= t0 + 40000; t += 20) {
                    r.advance(t);
                    if (r.ready()) return t - t0;
                }
                return -1;
            };
            const int64_t ref = firstByte(1000);
            c.equal("$18 first byte arrives", ref > 0, 1);
            c.equal("$18 phase kept after 2 s", firstByte(1000 + 2 * 8021248), ref);
            c.equal("$18 phase kept after 60 s", firstByte(1000 + 60 * 8021248), ref);
        }
        {   // Seuil 0 ($F4E5) : flux continu de $F8 0 0, un paquet par ligne libre.
            Rig r;
            for (int i = 0; i < 21; ++i) r.ikbd.onVbl(20000);
            r.command({0x0B, 0, 0});
            const auto b = collect(r, 6);
            c.equal("threshold 0 header", b.size() > 0 ? b[0] : -1, 0xF8);
            c.equal("threshold 0 dx", b.size() > 1 ? b[1] : -1, 0);
            c.equal("threshold 0 stream continues", b.size() > 3 ? b[3] : -1, 0xF8);
        }
        {   // Pas $0A à 0 ($F5D2) : flèches « droite » et « haut » en continu.
            Rig r;
            for (int i = 0; i < 21; ++i) r.ikbd.onVbl(20000);
            r.command({0x0A, 0, 0});
            const auto b = collect(r, 4);
            c.equal("keycode step 0 right make", b.size() > 0 ? b[0] : -1, 77);
            c.equal("keycode step 0 up make", b.size() > 2 ? b[2] : -1, 72);
        }
        {   // Chasse aux bugs : $07 $04 + clic avec mouvement — le $F8 part ET la
            // touche $74 suit dans le même passage (ROM $F59D-$F5A2).
            Rig r;
            for (int i = 0; i < 21; ++i) r.ikbd.onVbl(20000);
            r.command({0x07, 0x04});
            r.ikbd.mouseEvent(5, 0, true, false);
            const auto b = collect(r, 4);
            c.equal("bit 2 packet dx", b.size() > 1 ? b[1] : -1, 5);
            c.equal("bit 2 button key follows packet", b.size() > 3 ? b[3] : -1, 0x74);
        }
        {   // Chasse aux bugs : en $19, le port 0 ne compte pas d'impulsions souris ;
            // rien ne doit jaillir au retour en mode souris.
            Rig r;
            for (int i = 0; i < 21; ++i) r.ikbd.onVbl(20000);
            r.command({0x19, 0, 0, 0, 0, 0, 0});
            r.ikbd.mouseEvent(10000, 0, false, false);
            r.command({0x08});
            c.equal("no burst after keycode joystick", r.sched.rawCyclesUntil(Scheduler::IKBD_RX) == INT64_MIN, 1);
        }
        {   // Chasse aux bugs : mode flèches + $07 $04 — une seule touche $74.
            Rig r;
            for (int i = 0; i < 21; ++i) r.ikbd.onVbl(20000);
            r.command({0x0A, 5, 5}); r.command({0x07, 0x04});
            r.ikbd.mouseEvent(0, 0, true, false); r.ikbd.onAutoSend();
            const auto b = collect(r, 3);
            int keys = 0; for (int v : b) keys += (v == 0x74);
            c.equal("single button key in cursor mode", keys, 1);
        }
        {   // $19 : flèche à l'appui sur le joystick 0, feu = $74 puis $F4.
            Rig r;
            r.sched.setCallback(Scheduler::IKBD_MONITOR, [&r] { r.ikbd.onJoystickMonitor(); });
            for (int i = 0; i < 21; ++i) r.ikbd.onVbl(20000);
            r.command({0x19, 0, 0, 0, 0, 0, 0});
            r.ikbd.setJoystick(0x08 | 0x80, 0); r.advance(8100);
            const auto b = collect(r, 3);
            c.equal("$19 right make", b.size() > 0 ? b[0] : -1, 0x4D);
            c.equal("$19 right break", b.size() > 1 ? b[1] : -1, 0xCD);
            c.equal("$19 fire make", b.size() > 2 ? b[2] : -1, 0x74);
        }
    }
    {
        // $92 rapporte le seul bit 7 de l'octet de mode $C9 (ROM 6301 $FCA2) :
        // $14/$15 rendent le port 0 au joystick (bit 1) sans « désactiver » la
        // souris ; seul $12 la désactive, un mode souris la réactive.
        struct Avail { std::initializer_list<uint8_t> pre; int want; const char* label; };
        const Avail cases[] = {
            {{0x14}, 0x00, "$92 after $14 reports mouse enabled"},
            {{0x15}, 0x00, "$92 after $15 reports mouse enabled"},
            {{0x12}, 0x12, "$92 after $12 reports mouse disabled"},
            {{0x12, 0x14}, 0x12, "$14 does not re-enable a disabled mouse"},
            {{0x12, 0x08}, 0x00, "mouse mode re-enables mouse"},
        };
        for (const auto& tc : cases) {
            Rig r;
            r.command(tc.pre); r.advance(200000); r.command({0x92});
            int header = -1;
            for (int k = 0; k < 4 && header != 0xF6; ++k) {
                if (r.due() < r.live) break;
                r.advance(r.due());
                if (r.ready()) header = r.data();
            }
            c.equal("$92 header", header, 0xF6);
            r.advance(r.due()); c.equal(tc.label, r.data(), tc.want);
        }
    }
    {
        Rig r;
        r.command({0x1C}); r.advance(1000); r.command({0x16});
        c.equal("new response replaces pending wait", r.due(), 1000 + 8192);
        r.advance(9192); r.advance(19432);
        c.equal("global wait preserves queue order", r.data(), 0xFC);
        c.equal("replacement is not per packet", r.due(), 29672);
    }
    {
        Rig r;
        r.ikbd.keyEvent(0x39, true); r.advance(1000); r.command({0x1C});
        c.equal("new response preserves byte in flight", r.due(), 10240);
        r.advance(10240); c.equal("old byte arrives first", r.data(), 0x39);
        c.equal("wait starts at next idle", r.due(), 10240 + 7168);
        r.advance(17408); r.advance(27648);
        c.equal("reply follows global wait and serial byte", r.data(), 0xFC);
    }
    {
        Rig r;
        // ROM $F9AF : $13 mémorise l'index d'écriture ; la réponse déjà en file
        // AVANT la pause sort quand même, un octet arrivé après attend $11.
        r.command({0x1C}); r.advance(1000); r.command({0x13});
        r.advance(7168);
        c.equal("bytes queued before pause still leave", r.due(), 7168 + 10240);
        r.advance(17408); c.equal("pre-pause header delivered", r.data(), 0xFC);
        for (int i = 1; i <= 6; ++i) { r.advance(17408 + i * 10240); r.data(); }
        r.ikbd.keyEvent(0x39, true);
        c.equal("byte queued during pause waits", r.sched.cyclesUntil(Scheduler::IKBD_RX), -1);
        r.advance(100000); r.command({0x11});
        c.equal("resume releases held byte", r.due(), 100000 + 10240);
        r.advance(110240); c.equal("held byte after resume", r.data(), 0x39);
    }
    {
        Rig r;
        r.command({0x1C}); r.advance(1000); r.command({0x13});
        r.advance(2000); r.command({0x16});
        c.equal("reply resumes paused queue after new wait", r.due(), 2000 + 8192);
        r.advance(10192); r.advance(20432);
        c.equal("resumed old response keeps priority", r.data(), 0xFC);
    }
    {
        Rig r;
        r.command({0x1C}); r.advance(1000); r.ikbd.bootRom();
        r.advance(7168);
        c.equal("warm boot drains wait despite empty queue", r.sched.cyclesUntil(Scheduler::IKBD_RX), -1);
        r.advance(503000);
        c.equal("reset reply has no leftover response wait", r.due(), 513240);
        r.advance(513240); c.equal("warm reset response", r.data(), 0xF1);
    }
    {
        Rig r;
        r.command({0x1C}); r.advance(1000); r.ikbd.resetHw();
        c.equal("hardware reset cancels wait", r.sched.cyclesUntil(Scheduler::IKBD_RX), -1);
        r.advance(503000);
        c.equal("hardware reset reply starts normal serial", r.due(), 513240);
    }
    {
        Rig r;
        r.command({0x1C}); r.advance(1000);
        const auto state = r.save();
        Rig restored;
        c.equal("waiting response state loads", restored.load(state), true);
        c.equal("load preserves wait deadline", restored.due(), 7168);
        restored.advance(7168); restored.advance(17408);
        c.equal("load preserves waiting response", restored.data(), 0xFC);
        const auto serialState = restored.save();
        Rig serial;
        c.equal("serial phase state loads", serial.load(serialState), true);
        serial.advance(27648); c.equal("serial phase resumes payload", serial.data(), 0x26);
    }
    {
        Rig r;
        r.command({0x1C}); r.advance(8000);
        c.equal("late wait dispatch anchors serial to due", r.due(), 7168 + 10240);
    }
    {
        Rig r;
        // Tampon exactement plein avant la réponse : le rejet ne pose aucun délai.
        for (int i = 0; i < 1024; ++i) r.ikbd.keyEvent(0x39, true);
        r.command({0x1C});
        c.equal("full queue keeps current byte deadline", r.due(), 10240);
        r.advance(10240); r.data();
        c.equal("dropped reply does not delay old queue", r.due(), 20480);
    }
    {
        Rig r;
        r.froggies(); r.command({4});
        c.equal("Froggies four byte reply waits six bits", r.due(), 6144);
        r.advance(6144); r.advance(16384);
        c.equal("Froggies RAM83 header", r.data(), 0xFC);
        for (int i = 1; i < 4; ++i) {
            r.advance(16384 + i * 10240);
            c.equal("Froggies remaining payload", r.data(), 0);
        }
        const int64_t now = r.live;
        r.command({1});
        c.equal("Froggies one byte reply has no firmware wait", r.due(), now + 10240);
    }
    {
        Rig r;
        r.froggies();
        for (int i = 0; i < 1024; ++i) r.command({1});
        r.command({4});
        c.equal("full custom queue preserves byte in flight", r.due(), 10240);
        r.advance(10240); r.data();
        c.equal("full custom reply still sets global wait", r.due(), 10240 + 6144);
        r.advance(16384);
        c.equal("full custom queue resumes serial after wait", r.due(), 26624);
    }
    {
        Rig r;
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state); r.ikbd.serialize(save);
        // Les nouveaux champs terminent l'état IKBD : int puis bool.
        const auto at = state.size() - sizeof(int) - sizeof(bool);
        for (int bad : {-1, 11}) {
            auto forged = state; std::memcpy(forged.data() + at, &bad, sizeof(bad));
            auto load = StateArchive::loader(forged.data(), forged.size()); r.ikbd.serialize(load);
            c.equal("forged response delay rejected", load.ok(), false);
        }
        state.back() = 1;
        auto load = StateArchive::loader(state.data(), state.size()); r.ikbd.serialize(load);
        c.equal("forged wait phase rejected", load.ok(), false);
    }
    std::printf("[ikbd-responses] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}

#ifdef NEOST_IKBD_RESPONSE_TEST_MAIN
int main() { return testIkbdResponses() ? 1 : 0; }
#endif
