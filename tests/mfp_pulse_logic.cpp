// Modes pulse des timers A/B (TxCR 9-15) : table de vérité tirée de la fiche.
// [B] MC68901 Advance Information (Motorola, janv. 1984) § 2.1.6.2 et § 5.1.2, p. 5-2 et 5-3,
//     extern/datasheets/MC68901_Jan84.pdf (sha256 5441a092).
// [B] MK68901 (SGS-Thomson) p. 9 et 12, extern/datasheets/MK68901.pdf (sha256 8a032fa1).
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kAer = 0x03, kIera = 0x07, kIerb = 0x09, kIpra = 0x0B, kIprb = 0x0D,
                   kTacr = 0x19, kTbcr = 0x1B, kTadr = 0x1F, kTbdr = 0x21;
constexpr uint8_t kTimerAIpra = 0x20, kTimerBIpra = 0x01, kI4Iprb = 0x40, kI3Iprb = 0x08;
// /4 sur 100 comptes : 400 cycles MFP, soit 1305,5 cycles CPU à 8 021 248 Hz.
constexpr int64_t kPeriod = 1306;

struct Rig {
    Scheduler sched;
    Mfp mfp;
    int expiredA = 0, expiredB = 0;
    Rig() {
        mfp.setScheduler(&sched);
        sched.setCallback(Scheduler::TIMER_A, [this] { mfp.onTimerExpire(0); ++expiredA; });
        sched.setCallback(Scheduler::TIMER_B_DELAY, [this] { mfp.onTimerExpire(1); ++expiredB; });
        mfp.write8(kIera, 0x21);       // canaux Timer A et Timer B
        mfp.write8(kIerb, 0x48);       // canaux I4 et I3
    }
    void advance(int64_t cycles) { sched.runTo(sched.now() + cycles); }
};

}  // namespace

int testMfpPulse() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[mfp-pulse] FAIL %s\n", label); }
    };

    {   // Le comptage suit l'entrée, active au niveau de son bit d'AER.
        Rig r;
        r.mfp.write8(kAer, 0x10);      // TAI actif haut
        r.mfp.write8(kTadr, 100);
        r.mfp.write8(kTacr, 0x09);     // pulse, /4
        r.advance(kPeriod * 3);
        check(r.expiredA == 0, "TAI inactive: Timer A does not run");
        check(r.mfp.read8(kTadr) == 100, "TAI inactive: counter stays at its value");
        r.mfp.timerA_setLineInput(true);
        r.advance(kPeriod / 2);
        const int mid = r.mfp.read8(kTadr);
        check(mid < 100 && mid > 0, "TAI active: counter runs");
        r.mfp.timerA_setLineInput(false);
        const int frozen = r.mfp.read8(kTadr);
        r.advance(kPeriod * 3);
        check(r.mfp.read8(kTadr) == frozen, "TAI inactive again: counter is frozen");
        check(r.expiredA == 0, "frozen timer does not expire");
        r.mfp.timerA_setLineInput(true);
        r.advance(kPeriod);
        check(r.expiredA == 1, "resumed timer reaches its time out");
        check(r.mfp.read8(kIpra) & kTimerAIpra, "time out still raises the timer channel");
    }
    {   // Niveau actif bas, défini par l'AER : le timer tourne dès le mode posé.
        Rig r;
        r.mfp.write8(kAer, 0x00);
        r.mfp.write8(kTadr, 100);
        r.mfp.write8(kTacr, 0x09);
        r.advance(kPeriod + 1);
        check(r.expiredA == 1, "edge bit 0: timer runs while TAI is low");
        r.mfp.write8(kAer, 0x10);
        const int frozen = r.mfp.read8(kTadr);
        r.advance(kPeriod * 2);
        check(r.expiredA == 1 && r.mfp.read8(kTadr) == frozen, "writing AER stops the timer");
    }
    {   // L'IRQ de I4 répond à TAI, sur la transition OPPOSÉE à l'AER.
        Rig r;
        r.mfp.write8(kAer, 0x10);
        r.mfp.write8(kTadr, 100);
        r.mfp.write8(kTacr, 0x09);
        r.mfp.write8(kIprb, 0x00);
        r.mfp.timerA_setLineInput(true);
        check(!(r.mfp.read8(kIprb) & kI4Iprb), "edge bit 1: no I4 interrupt on the rising TAI");
        r.mfp.timerA_setLineInput(false);
        check(r.mfp.read8(kIprb) & kI4Iprb, "edge bit 1: I4 interrupt on the falling TAI");
        r.mfp.write8(kIprb, 0x00);
        r.mfp.setAciaLineKbd(true);
        check(!(r.mfp.read8(kIprb) & kI4Iprb), "real I4 line has no interrupt capability");
        r.mfp.setAciaLineKbd(false);
        check(!(r.mfp.read8(kIprb) & kI4Iprb), "real I4 line has no interrupt capability (other edge)");
        check((r.mfp.read8(0x01) & 0x10) != 0, "I4 can still be read as I/O");
    }
    {   // Sans mode pulse, TAI ne touche pas au canal de I4 (état antérieur conservé).
        Rig r;
        r.mfp.write8(kAer, 0x10);
        r.mfp.write8(kIprb, 0x00);
        r.mfp.timerA_setLineInput(true);
        r.mfp.timerA_setLineInput(false);
        check(!(r.mfp.read8(kIprb) & kI4Iprb), "delay or stopped timer: TAI leaves I4 alone");
    }
    {   // Entrer dans le mode ou changer le bit d'AER peut lever le canal.
        Rig r;
        r.mfp.write8(kAer, 0x00);
        r.mfp.write8(kIprb, 0x00);
        r.mfp.write8(kTacr, 0x09);     // I4 repos haut, vue pulse = !TAI = haut : pas de front
        check(!(r.mfp.read8(kIprb) & kI4Iprb), "entering pulse mode with equal input: no interrupt");
        r.mfp.timerA_setLineInput(true);   // vue passe à 0, égale à l'AER 0 : front actif
        check(r.mfp.read8(kIprb) & kI4Iprb, "edge bit 0: I4 interrupt on the rising TAI");
        r.mfp.write8(kIprb, 0x00);
        r.mfp.write8(kTacr, 0x00);     // retour à I4 : la vue remonte, sans front actif ici
        check(!(r.mfp.read8(kIprb) & kI4Iprb), "leaving pulse mode towards an inactive level");
    }
    {   // TxDR charge le compteur hors impulsion, pas pendant.
        Rig r;
        r.mfp.write8(kAer, 0x10);
        r.mfp.write8(kTadr, 100);
        r.mfp.write8(kTacr, 0x09);
        r.mfp.write8(kTadr, 50);
        check(r.mfp.read8(kTadr) == 50, "write while inactive reloads the main counter");
        r.mfp.timerA_setLineInput(true);
        r.advance(kPeriod / 4);
        const int before = r.mfp.read8(kTadr);
        r.mfp.write8(kTadr, 200);
        check(r.mfp.read8(kTadr) == before, "write while active leaves the main counter");
        r.advance(kPeriod / 2 + 1);
        check(r.expiredA == 1, "time out from the previous contents");
        check(r.mfp.read8(kTadr) > 150, "new value loaded when counting through 01");
    }
    {   // Mode pulse → arrêt : le compteur figé survit. Arrêt pendant l'impulsion : figé aussi.
        Rig r;
        r.mfp.write8(kAer, 0x10);
        r.mfp.write8(kTadr, 100);
        r.mfp.write8(kTacr, 0x09);
        r.mfp.timerA_setLineInput(true);
        r.advance(kPeriod / 2);
        r.mfp.timerA_setLineInput(false);
        const int frozen = r.mfp.read8(kTadr);
        r.mfp.write8(kTacr, 0x00);
        check(r.mfp.read8(kTadr) == frozen, "stopping a gated timer keeps its counter");
        r.mfp.write8(kTacr, 0x09);
        r.mfp.timerA_setLineInput(true);
        r.advance(kPeriod / 8);
        const int running = r.mfp.read8(kTadr);
        r.mfp.write8(kTacr, 0x00);
        check(r.mfp.read8(kTadr) == running, "stopping a running pulse timer keeps its counter");
    }
    {   // Le Timer B suit TBI et le canal de I3.
        Rig r;
        r.mfp.write8(kAer, 0x08);
        r.mfp.write8(kTbdr, 100);
        r.mfp.write8(kTbcr, 0x09);
        r.advance(kPeriod * 2);
        check(r.expiredB == 0, "TBI inactive: Timer B does not run");
        r.mfp.setTbiLine(true, r.sched.liveNow());
        r.advance(kPeriod + 1);
        check(r.expiredB == 1, "TBI active: Timer B reaches its time out");
        check(r.mfp.read8(kIpra) & kTimerBIpra, "Timer B channel raised");
        r.mfp.write8(kIprb, 0x00);
        r.mfp.setTbiLine(false, r.sched.liveNow());
        check(r.mfp.read8(kIprb) & kI3Iprb, "I3 interrupt on the falling TBI");
        r.mfp.write8(kIprb, 0x00);
        r.mfp.setBlitterLine(false);
        r.mfp.setBlitterLine(true);
        check(!(r.mfp.read8(kIprb) & kI3Iprb), "blitter line has no interrupt capability");
    }
    {   // [B] MC68HC901 § 6.1.3 p. 6-3, MK68901 p. 13 : en event-count, I4 reste une entrée d'interruption.
        Rig r;
        r.mfp.write8(kAer, 0x10);
        r.mfp.write8(kTadr, 10);
        r.mfp.write8(kTacr, 0x08);
        r.mfp.write8(kIprb, 0x00);
        r.mfp.timerA_setLineInput(true);
        check(!(r.mfp.read8(kIprb) & kI4Iprb), "event count: TAI leaves I4 alone");
        check(r.mfp.read8(kTadr) == 9, "event count: the active edge still counts");
        r.mfp.setAciaLineKbd(true);
        r.mfp.setAciaLineKbd(false);                   // la ligne remonte : le front actif de cet AER
        check(r.mfp.read8(kIprb) & kI4Iprb, "event count: real I4 line keeps its interrupt");
    }
    {   // Timer B en event-count : TBI n'est suivi qu'en mode pulse.
        Rig r;
        r.mfp.write8(kIerb, 0x08);
        r.mfp.write8(kTbcr, 0x08);
        check(!r.mfp.tbiTracked(), "event count: TBI is not tracked");
        r.mfp.write8(kTbcr, 0x09);
        check(r.mfp.tbiTracked(), "pulse: TBI is tracked");
        r.mfp.write8(kTbcr, 0x00);
        check(!r.mfp.tbiTracked(), "stopped: TBI is not tracked");
    }
    {   // Les modes délai et event-count ne changent pas.
        Rig r;
        r.mfp.write8(kTadr, 100);
        r.mfp.write8(kTacr, 0x01);
        r.advance(kPeriod + 1);
        check(r.expiredA == 1, "delay mode ignores TAI");
        check(!r.mfp.timerPulseMode(0), "delay is not a pulse mode");
        r.mfp.write8(kTacr, 0x08);
        check(!r.mfp.timerPulseMode(0), "event count is not a pulse mode");
        r.mfp.write8(kTacr, 0x0F);
        check(r.mfp.timerPulseMode(0), "control 15 is a pulse mode");
    }
    // [B] MC68901 Jan. 1984 § 5.1.1 et Fig. 5-3 : le compteur reste à 1
    // jusqu'à l'impulsion suivante ; l'arrêt ne change pas ce compteur.
    for (int timer = 0; timer < 4; ++timer) {
        for (int stopAt : {250, 251, 255, 257}) {
            Rig r;
            const uint32_t data = 0x1F + 2 * timer;
            const uint32_t control = timer < 2 ? 0x19 + 2 * timer : 0x1D;
            const uint8_t start = timer == 2 ? 0x10 : 0x01;
            r.mfp.write8(data, 20); // /4 : expiration après 261 cycles CPU
            r.mfp.write8(control, start);
            r.advance(stopAt);
            check(r.mfp.read8(data) == 1, "last prescaler interval reads one");
            r.mfp.write8(control, 0);
            check(r.mfp.read8(data) == 1, "stop preserves one, not reload");
            r.advance(100);
            check(r.mfp.read8(data) == 1, "stopped counter remains one");
            r.mfp.write8(control, start);
            r.advance(5);
            check(r.mfp.read8(data) == 1, "restart uses saved one");
        }
    }
    // [B] MC68901 janv. 1984 § 5.1.3 p. 5-4, § 4.2.2 note p. 4-2 : en event-count, changer
    // le bit de front compte une impulsion quand l'entrée vaut déjà le nouveau bit (XOR).
    {
        Rig r;
        r.mfp.write8(kAer, 0x00);
        r.mfp.write8(kTadr, 10);
        r.mfp.write8(kTacr, 0x08);                 // event-count, TAI au repos (0)
        r.mfp.write8(kAer, 0x10);                  // TAI=0, nouveau bit 1 : pas d'impulsion
        check(r.mfp.read8(kTadr) == 10, "AER4 0->1 with TAI low: no count");
        r.mfp.write8(kAer, 0x00);                  // TAI=0, nouveau bit 0 : impulsion
        check(r.mfp.read8(kTadr) == 9, "AER4 1->0 with TAI low: one count");
        r.mfp.timerA_setLineInput(true);           // front montant, non sélectionné
        check(r.mfp.read8(kTadr) == 9, "TAI rising edge with AER4=0: no count");
        r.mfp.write8(kAer, 0x10);                  // TAI=1, nouveau bit 1 : impulsion
        check(r.mfp.read8(kTadr) == 8, "AER4 0->1 with TAI high: one count");
        r.mfp.write8(kTacr, 0x01);                 // délai : le changement d'AER ne compte plus
        r.mfp.write8(kTacr, 0x00);
        const int stopped = r.mfp.read8(kTadr);
        r.mfp.write8(kAer, 0x00);
        r.mfp.write8(kAer, 0x10);
        check(r.mfp.read8(kTadr) == stopped, "AER4 change outside event-count: no count");
    }
    std::fprintf(stderr, "[mfp-pulse] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
