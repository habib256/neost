// Joystick 0 en touches ($19) selon la ROM 6301 (MiSTery ikbd/rom/IKBD.ASM) :
// anti-rebond $F681/$F186, prédiviseur 100 ms par axe chargé à l'appui ($F7E3) et
// décompté par l'interruption 1 ms ($FE7D), compteurs RX/TX/VX ($F7B0-$F86C).
// Puce nue + Scheduler : les commandes entrent à la sortie du récepteur SCI.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[ikbd-keycode] FAIL %s: got %lld, expected %lld\n",
                     label, (long long)got, (long long)expected);
    }
};
struct Rig {
    Scheduler sched;
    Mfp mfp;
    Ikbd ikbd{mfp};
    int64_t live = 0;
    std::vector<std::pair<int64_t, int>> got;   // (date de livraison RDR, octet)
    Rig() {
        ikbd.setScheduler(&sched);
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        sched.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        sched.setCallback(Scheduler::IKBD, [this] { ikbd.onResetResponse(); });
        sched.setCallback(Scheduler::IKBD_MONITOR, [this] { ikbd.onJoystickMonitor(); });
        ikbd.write8(0xFFFC00, 0x96);
        for (int i = 0; i < 21; ++i) ikbd.onVbl(20000);
    }
    void command(std::initializer_list<uint8_t> bytes) { for (uint8_t b : bytes) ikbd.receiveSerialByte(b); }
    // Avance jusqu'à `end` en lisant chaque octet livré par le SCI.
    void runTo(int64_t end) {
        // Scheduler::runTo ne sert chaque source qu'une fois par appel : avancer
        // d'échéance en échéance, en lisant l'ACIA à chaque livraison.
        for (;;) {
            int64_t at = INT64_MAX;
            for (Scheduler::Source src : {Scheduler::IKBD_RX, Scheduler::IKBD_TX, Scheduler::IKBD, Scheduler::IKBD_MONITOR}) {
                const int64_t d = sched.rawCyclesUntil(src);
                if (d != INT64_MIN && live + d < at) at = live + d;
            }
            if (at > end) break;
            if (at < live) at = live;
            const bool rx = sched.rawCyclesUntil(Scheduler::IKBD_RX) != INT64_MIN &&
                            live + sched.rawCyclesUntil(Scheduler::IKBD_RX) <= at;
            live = at; sched.runTo(at);
            if (rx && (ikbd.read8(0xFFFC00) & 1)) got.push_back({at, ikbd.read8(0xFFFC02)});
        }
        live = end; sched.runTo(end);
    }
    int count(int byte) const { int n = 0; for (auto& g : got) n += g.second == byte; return n; }
    int64_t first(int byte) const { for (auto& g : got) if (g.second == byte) return g.first; return -1; }
};
// Grille de l'interruption 1 ms (origine 0 dans ce banc) et indice du dernier tic <= t.
int64_t tick(int64_t k) { return (k * neost::pacing::kCpuHzInt + 500) / 1000; }
int64_t index(int64_t t) { int64_t k = t * 1000 / neost::pacing::kCpuHzInt; while (tick(k + 1) <= t) ++k; while (tick(k) > t) --k; return k; }
constexpr int64_t kByte = 10240;   // une trame SCI : livraison au RDR 10 bits après le départ
}

int testIkbdKeycode() {
    Checks c;
    {
        // Appui à droite à t0 : anti-rebond ($9E, seuil 5) → accepté au 4ᵉ tic, la paire
        // make/break part aussitôt. RX = 2, TX = 1, VX = 3 dixièmes sur X.
        Rig r;
        r.command({0x19, 2, 0, 1, 0, 3, 0});
        const int64_t t0 = 50000;
        r.runTo(t0);
        r.ikbd.setJoystick(0x08, 0);
        const int64_t press = tick(index(t0) + 4);
        r.runTo(press + 3 * kByte);
        c.equal("make after 4 ms debounce", r.first(0x4D), press + kByte);
        c.equal("break follows make", r.first(0xCD), press + 2 * kByte);
        // Dixièmes de 101 tics à partir du premier tic suivant l'appui ($A2 = 100).
        const int64_t k0 = index(press) + 1;
        const int64_t tenth1 = tick(k0 + 100), tenth2 = tick(k0 + 201), tenth5 = tick(k0 + 100 + 4 * 101);
        r.runTo(tenth1 - 1);
        c.equal("no repeat before first tenth", r.count(0x4D), 1);
        r.runTo(tenth1 + kByte);
        c.equal("TX repeat at the 101st tick", r.got.size() >= 3 ? r.got[2].first : -1, tenth1 + kByte);
        r.runTo(tenth2 + kByte);
        // RX échu en même temps que TX : la paire part et la phase VX commence ($F835).
        c.equal("RX expiry emits and enters VX", r.got.size() >= 5 ? r.got[4].first : -1, tenth2 + kByte);
        r.runTo(tenth5 - 1);
        c.equal("VX waits three tenths", r.count(0x4D), 3);
        r.runTo(tenth5 + kByte);
        c.equal("VX repeat after three tenths", r.count(0x4D), 4);
        // Relâché : rien n'est émis ($F7BC), compteurs effacés.
        r.ikbd.setJoystick(0, 0);
        r.runTo(tenth5 + 20 * kByte);
        c.equal("release sends nothing", r.count(0x4D) + r.count(0xCD), 8);
    }
    {
        // TX = 0 : à l'échéance de RX, bascule en VX SANS émettre ($F81F).
        Rig r;
        r.command({0x19, 1, 0, 0, 0, 1, 0});
        r.runTo(1000);
        r.ikbd.setJoystick(0x04, 0);
        const int64_t press = tick(index(1000) + 4);
        const int64_t k0 = index(press) + 1;
        r.runTo(tick(k0 + 100) + 2 * kByte);
        c.equal("left make only at press", r.count(0x4B), 1);
        r.runTo(tick(k0 + 201) + kByte);
        c.equal("first VX repeat one tenth after the switch", r.count(0x4B), 2);
    }
    {
        // Directions opposées : rien ne change ($F7CD) ; la touche tenue continue.
        Rig r;
        r.command({0x19, 0, 0, 0, 0, 1, 0});
        r.runTo(1000);
        r.ikbd.setJoystick(0x04, 0);
        r.runTo(60000);
        r.ikbd.setJoystick(0x0C, 0);
        r.runTo(120000);
        r.ikbd.setJoystick(0x04, 0);
        r.runTo(200000);
        c.equal("opposite directions keep the held key", r.count(0x4B), 1);
        c.equal("no right key from both pressed", r.count(0x4D), 0);
    }
    {
        // Feu : anti-rebond $F186 ($9D, seuil 10) → $74 au 9ᵉ tic, $F4 de même.
        Rig r;
        r.command({0x19, 0, 0, 0, 0, 0, 0});
        r.runTo(1000);
        r.ikbd.setJoystick(0x80, 0);
        const int64_t down = tick(index(1000) + 9);
        r.runTo(down + 2 * kByte);
        c.equal("fire make after 9 ms", r.first(0x74), down + kByte);
        r.ikbd.setJoystick(0x00, 0);
        const int64_t up = tick(index(down + 2 * kByte) + 9);
        r.runTo(up + 2 * kByte);
        c.equal("fire break after 9 ms", r.first(0xF4), up + kByte);
    }
    {
        // Un rebond plus court que la fenêtre n'émet rien.
        Rig r;
        r.command({0x19, 0, 0, 0, 0, 0, 0});
        r.runTo(1000);
        r.ikbd.setJoystick(0x08, 0);
        r.runTo(1000 + 8021);
        r.ikbd.setJoystick(0x00, 0);
        r.runTo(200000);
        c.equal("2 ms bounce is filtered", r.count(0x4D), 0);
    }
    if (c.failures) std::fprintf(stderr, "[ikbd-keycode] %d checks, %d FAIL\n", c.count, c.failures);
    else std::printf("[ikbd-keycode] %d checks, 0 FAIL\n", c.count);
    return c.failures;
}
