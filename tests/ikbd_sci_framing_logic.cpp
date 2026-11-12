// Liaison ACIA clavier → SCI du 6301 hors 8N1 : formats du 6850 émis bit à bit,
// erreur de trame et BREAK vus par la ROM (gestionnaire ORFE $FF40, tic $FE29).
// [B] MC6850 § Word Select Bits p. 8 ; HD6301 Handbook p. 152 (WU) et p. 155 (trame).
// [C] ROM 6301 (MiSTery ikbd/rom/IKBD.ASM) $FF40-$FF6D, $FE29-$FE4C, $F00B.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
#include <utility>
#include <vector>

namespace {
struct Rig {
    Scheduler s; Mfp m; Ikbd k{m}; int64_t live = 0;
    std::vector<std::pair<int64_t, int>> got;
    Rig() {
        k.setScheduler(&s); s.setLiveClock([this] { return live; });
        s.setCallback(Scheduler::IKBD_TX, [this] { k.onTxEmpty(); });
        s.setCallback(Scheduler::IKBD_RX, [this] { k.onRxDeliver(); });
        s.setCallback(Scheduler::IKBD, [this] { k.onResetResponse(); });
        k.write8(0, 0x96);
        for (int i = 0; i < 21; ++i) k.onVbl(20000);
    }
    // Avance sans lire le statut ACIA entre deux livraisons : le récepteur n'est
    // rattrapé que par les échéances IKBD_TX, comme sous une boucle CPU muette.
    void runTo(int64_t end) {
        // Scheduler::runTo ne sert chaque source qu'une fois par appel : avancer
        // d'échéance en échéance, en lisant l'ACIA à chaque livraison.
        for (;;) {
            int64_t at = INT64_MAX;
            for (Scheduler::Source src : {Scheduler::IKBD_RX, Scheduler::IKBD_TX, Scheduler::IKBD, Scheduler::IKBD_MONITOR}) {
                const int64_t d = s.rawCyclesUntil(src);
                if (d != INT64_MIN && live + d < at) at = live + d;
            }
            if (at > end) break;
            if (at < live) at = live;
            const bool rx = s.rawCyclesUntil(Scheduler::IKBD_RX) != INT64_MIN &&
                            live + s.rawCyclesUntil(Scheduler::IKBD_RX) <= at;
            live = at; s.runTo(at);
            if (rx && (k.read8(0) & 1)) got.push_back({at, k.read8(2)});
        }
        live = end; s.runTo(end);
    }
    void cr(uint8_t v) { k.write8(0, v); }
    void send(uint8_t v) { k.write8(2, v); }
    int count(int byte) const { int n = 0; for (auto& g : got) n += g.second == byte; return n; }
};
int64_t tick(int64_t k) { return (k * neost::pacing::kCpuHzInt + 500) / 1000; }
int64_t index(int64_t t) { int64_t k = t * 1000 / neost::pacing::kCpuHzInt; while (tick(k + 1) <= t) ++k; while (tick(k) > t) --k; return k; }
}

int testIkbdSciFraming() {
    int failures = 0, checks = 0;
    auto eq = [&](const char* label, int64_t got, int64_t want) {
        ++checks;
        if (got != want) { ++failures; std::fprintf(stderr, "[ikbd-sci-framing] FAIL %s: %lld != %lld\n", label, (long long)got, (long long)want); }
    };
    {
        // 7 bits + parité paire, 2 stops : le SCI (8N1) reçoit la parité en bit 7.
        // $16 = 0010110 (3 « 1 ») → parité 1 → $96 : le 6301 lit $96, pas $16.
        Rig r;
        r.cr(0x80 | (0 << 2) | 2);          // 7E2 /64
        r.send(0x16);
        r.runTo(400000);
        eq("7E2 parity lands in bit 7: no $16 response", r.count(0xFD), 0);
        // 7 bits + parité impaire, 1 stop : $16 → parité 0 → $16 reçu intact.
        Rig q;
        q.cr(0x80 | (3 << 2) | 2);          // 7O1 /64
        q.send(0x16);
        q.runTo(400000);
        eq("7O1 with zero parity bit reads as $16", q.count(0xFD), 1);
    }
    {
        // 8 bits + parité paire : le dixième bit vu par le SCI est la parité. $03 → parité 0
        // → erreur de trame en plein $0B : le caractère compte comme premier paramètre,
        // le suivant est jeté ($FF69, $FF33), la commande est perdue.
        Rig r;
        r.send(0x0B);                       // SET MOUSE THRESHOLD, 2 paramètres
        r.runTo(10240);                     // stop du premier caractère
        r.cr(0x80 | (6 << 2) | 2);          // 8E1 /64
        r.send(0x03);                       // start 11264, parité (10ᵉ bit) 20480
        r.runTo(21000);
        r.cr(0x96);                         // retour en 8N1 ; stop 21504, puis repos
        // WU s'efface au dixième « 1 » consécutif : stop 21504 + 9 bits = 30720.
        const int64_t resume = tick(index(30720) + 1);
        r.k.keyEvent(0x10, true);           // $CB bit 4 : envoi jeté avant la reprise
        r.runTo(resume - 1);
        eq("key dropped while discarding", r.count(0x10), 0);
        r.send(0x16);                       // paramètre restant de $0B : jeté
        r.runTo(resume + 20000);
        r.send(0x16);                       // vraie commande
        r.runTo(resume + 400000);
        eq("aborted command: only the second $16 answers", r.count(0xFD), 1);
        eq("dropped key never sent", r.count(0x10), 0);
        r.k.keyEvent(0x11, true);
        r.runTo(resume + 450000);
        eq("output back after resume", r.count(0x11), 1);
    }
    {
        // Pendant WU, un caractère qui suit sans repos de dix bits n'est pas reçu.
        Rig r;
        r.cr(0x80 | (6 << 2) | 2);          // 8E1
        r.send(0x03);                       // erreur de trame au 10ᵉ bit
        r.runTo(10240 + 2048);
        r.cr(0x96);
        r.send(0x16);                       // départ au bit suivant : la ligne n'a pas reposé
        r.runTo(600000);
        eq("byte without idle gap is lost under WU", r.count(0xFD), 0);
    }
    {
        // BREAK court (50 ms) : une erreur de trame, sortie retenue jusqu'au repos, pas de reset.
        Rig r;
        r.runTo(1000);
        r.cr(0xF6);                         // BREAK, /64 8N1 : dix zéros, erreur de trame à 10240
        r.runTo(20000);
        r.k.keyEvent(0x20, true);           // pas de commande en cours : envoi gardé, mais retenu
        r.runTo(1000 + 401062);
        eq("output held during BREAK", r.count(0x20), 0);
        r.cr(0x96);
        r.runTo(1000 + 401062 + 200000);
        eq("held key sent after the line idles", r.count(0x20), 1);
        eq("short BREAK does not reset", r.count(0xF1), 0);
    }
    {
        // BREAK de 250 ms : $D4 atteint $CD, la ROM attend la ligne au repos puis se
        // réinitialise ($F00B → $F010) et répond $F1.
        Rig r;
        r.runTo(1000);
        r.cr(0xF6);
        r.runTo(1000 + 2005312);
        eq("no reset while BREAK holds", r.count(0xF1), 0);
        r.cr(0x96);
        r.runTo(1000 + 2005312 + 8021248);
        eq("long BREAK resets the IKBD", r.count(0xF1), 1);
    }
    {
        // Sauvegarde au milieu du réveil : la reprise reste datée pareil.
        Rig r;
        r.cr(0x80 | (6 << 2) | 2); r.send(0x03); r.runTo(10240 + 2048); r.cr(0x96);
        std::vector<uint8_t> bytes;
        { auto a = StateArchive::saver(bytes); r.k.serialize(a); r.s.serialize(a); }
        Rig q;
        { auto a = StateArchive::loader(bytes.data(), bytes.size()); q.k.serialize(a); q.s.serialize(a); q.live = q.s.now(); eq("load SCI wake state", a.ok(), 1); }
        q.k.keyEvent(0x21, true); r.k.keyEvent(0x21, true);
        q.runTo(200000); r.runTo(200000);
        eq("restored resume matches", q.got.empty() ? -1 : q.got[0].first, r.got.empty() ? -2 : r.got[0].first);
    }
    if (failures) std::fprintf(stderr, "[ikbd-sci-framing] %d checks, %d FAIL\n", checks, failures);
    else std::printf("[ikbd-sci-framing] %d checks, 0 FAIL\n", checks);
    return failures;
}
