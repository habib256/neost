// =============================================================================
//  Rtc.cpp — RP5C15 (Mega ST / Mega STE). Port de Hatari rtc.c, modèle paresseux
//  déterministe (cf. Rtc.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Rtc.hpp"
#include <ctime>

Rtc::Rtc() {
    initFromHostTime();
}

Rtc::DateTime Rtc::getDateTime() {
    catchUp();
    auto get = [&](int u, int t) { return d_[u] + d_[t] * 10; };
    return { get(0, 1), get(2, 3), hour24(), int(d_[6]), get(7, 8), get(9, 10), get(11, 12) };
}

void Rtc::setDateTime(const DateTime& dt) {
    auto set = [&](int u, int t, int v) {
        if (v < 0) v = 0;
        d_[u] = uint8_t(v % 10);
        d_[t] = uint8_t((v / 10) % 10);
    };
    set(0, 1, dt.sec);
    set(2, 3, dt.min);
    setHour24(dt.hour);
    d_[6] = uint8_t(dt.wday & 0x0F);
    set(7, 8, dt.day);
    set(9, 10, dt.month);
    set(11, 12, dt.year);
    timerCarry_ = false;
    bank1_[11] = uint8_t(dt.year & 3);
    primed_ = false;                             // recale la phase au prochain accès MMIO
}

void Rtc::advanceSeconds(int64_t n) {
    if (n <= 0) return;
    if (n > 90000) n = 90000;                    // même borne que catchUp (>1 jour ignoré)
    while (n-- > 0) tickOneSecond();
}

void Rtc::initFromHostTime() {
    const std::time_t now = std::time(nullptr);
    const std::tm* tm = std::localtime(&now);
    if (!tm) return;                                      // garde la base de secours

    auto set = [&](int u, int t, int v) {
        if (v < 0) v = 0;
        d_[u] = uint8_t(v % 10);
        d_[t] = uint8_t((v / 10) % 10);
    };

    set(0, 1, tm->tm_sec);
    set(2, 3, tm->tm_min);
    setHour24(tm->tm_hour);
    d_[6] = uint8_t(tm->tm_wday & 0x0F);                 // Hatari expose tm_wday (0=dimanche)
    set(7, 8, tm->tm_mday);
    set(9, 10, tm->tm_mon + 1);
    bank1_[11] = uint8_t((tm->tm_year - 80) & 3);
    set(11, 12, (tm->tm_year - 80) % 100);               // GEMDOS : années depuis 1980
}

// Rattrape les secondes entières écoulées depuis le dernier top (baseCycle_), en
// avançant l'horloge BCD. Appelé à chaque accès → le temps lu reflète l'horloge
// émulée à l'instant exact de l'accès (granularité : le cycle CPU).
void Rtc::catchUp() {
    if (!now_) return;
    const int64_t t = now_();
    if (!primed_) { baseCycle_ = t; primed_ = true; return; }  // 1er accès : on cale la phase
    int64_t secs = (t - baseCycle_) / secondCycles_;
    if (secs <= 0) return;
    baseCycle_ += secs * secondCycles_;                 // avance la phase d'un multiple entier de 1 s
    // Ricoh EK-086-9908 p7 : les étages sous-seconde continuent ; leur
    // retenue est mémorisée tant que TIMER EN=0, puis délivrée au redémarrage.
    // Un seul carry est retenu, sans accumuler toutes les secondes arrêtées.
    if (!(mode_ & 0x08)) { timerCarry_ = true; return; }
    if (secs > 90000) secs = 90000;              // borne le travail (>1 jour) : la date exacte
                                                 // au-delà n'intéresse aucun diagnostic
    while (secs-- > 0) tickOneSecond();
}

// addr → index de registre : ($FFFC21,23,...,3F) = 16 registres aux adresses impaires.
//   0..12 = chiffres BCD ; 13 = mode ; 14 = test ; 15 = reset.
uint8_t Rtc::read8(uint32_t addr) {
    catchUp();
    const int i = static_cast<int>((addr - 0xFFFC21) >> 1) & 0x0F;
    if (mode_ & 1) {
        // Ricoh EK-086-9908 p6, MAME rp5c15 : 2..8 sont les alarmes,
        // jamais des alias des compteurs. Hauts F conservés pour la sonde TOS.
        if (i >= 2 && i <= 8) return uint8_t(bank1_[i] | (i <= 3 ? 0xF0 : 0));
        if (i < 13) return bank1_[i];
    }
    if (i < 13) return uint8_t(d_[i] & 0x0F);           // chiffre BCD (4 bits utiles)
    if (i == 13) return uint8_t((mode_ & 0x0F) | 0xF0); // mode : bits hauts à 1 (cf. Hatari)
    if (i == 14) return test_;
    return reset_;
}

void Rtc::write8(uint32_t addr, uint8_t v) {
    catchUp();                                          // fige le temps courant AVANT d'écrire
    const int i = static_cast<int>((addr - 0xFFFC21) >> 1) & 0x0F;
    if ((mode_ & 1) && i >= 2 && i <= 8) {
        static constexpr uint8_t masks[7] = {15,7,15,3,7,15,3};
        bank1_[i] = v & masks[i-2];
    } else if ((mode_ & 1) && i < 13) {
        // Ricoh EK-086-9908 p6-7 : la banque de contrôle n'écrit jamais
        // les chiffres du calendrier. CLKOUT mémorisé, sans sortie synthétique.
        static constexpr uint8_t masks[13] = {7,1,15,7,15,3,7,15,3,0,1,3,0};
        bank1_[i] = v & masks[i];
        if (i == 1 && (v & 1)) {
            // ADJ arrondit à la minute la plus proche et remet le diviseur à zéro.
            // Déduction du reset des étages inférieurs : leur carry retenu est
            // aussi annulé (pas une mesure sur matériel).
            timerCarry_ = false;
            const int seconds = d_[0] + 10*d_[1];
            if (seconds >= 30) { d_[0] = 9; d_[1] = 5; tickOneSecond(); }
            else d_[0] = d_[1] = 0;
            if (now_) { baseCycle_ = now_(); primed_ = true; }
            else primed_ = false;
        }
    }
    else if (i < 13) {
        static constexpr uint8_t masks[13] = {15,7,15,7,15,3,7,15,3,15,1,15,15};
        d_[i] = v & masks[i];
    }        // réglage de l'heure (chiffre BCD)
    else if (i == 13) {
        const bool restarting = !(mode_ & 8) && (v & 8);
        mode_ = v & 0x0D;
        if (restarting && timerCarry_) { timerCarry_ = false; tickOneSecond(); }
    }
    else if (i == 14) test_  = v;
    else {            reset_ = v;
        // RESET bit0 efface seulement les sept registres d'alarme, quelle que
        // soit la banque sélectionnée. ALARM n'est pas câblé sur les Mega ST.
        if (v & 1) for (int alarm = 2; alarm <= 8; ++alarm) bank1_[alarm] = 0;
        // Registre RESET du RP5C15 : bit1 = reset du diviseur sous-seconde. Remet la
        // phase du 1 Hz à zéro → le prochain top tombe EXACTEMENT 1 s plus tard. Le
        // test d'incrément Mega STE s'en sert pour mesurer un débordement propre.
        if (v & 0x02) {
            // Même déduction que pour ADJ : remettre les étages à zéro annule
            // leur carry en attente, quelle que soit la valeur de TIMER EN.
            timerCarry_ = false;
            if (now_) { baseCycle_ = now_(); primed_ = true; }
            else primed_ = false;
        }
    }
}

// Les deux chiffres d'heure restent bruts : deux écritures BCD successives
// peuvent traverser une valeur intermédiaire non valide, sans perdre un chiffre.
int Rtc::hour24() const {
    if (bank1_[10] & 1) return d_[4] + 10*d_[5];
    return (d_[4] + 10*(d_[5] & 1)) % 12 + ((d_[5] & 2) ? 12 : 0);
}

void Rtc::setHour24(int hour) {
    if (hour < 0) hour = 0;
    const int value = (bank1_[10] & 1) ? hour : hour % 12;
    d_[4] = uint8_t(value % 10);
    d_[5] = uint8_t(value / 10 + (!(bank1_[10] & 1) && hour >= 12 ? 2 : 0));
}

// Incrémente l'heure d'une seconde, avec retenue calendaire BCD COMPLÈTE :
// secondes → minutes → heures → jour (longueur du mois) → mois → année.
// Nécessaire pour le test d'incrément (23:59:59 31/12/99 → 00:00:00 01/01/00).
void Rtc::tickOneSecond() {
    auto get = [&](int u, int t) { return d_[u] + d_[t] * 10; };
    auto set = [&](int u, int t, int v) { d_[u] = uint8_t(v % 10); d_[t] = uint8_t(v / 10); };

    int s = get(0, 1) + 1;
    if (s < 60) { set(0, 1, s); return; }
    set(0, 1, 0);
    int mi = get(2, 3) + 1;
    if (mi < 60) { set(2, 3, mi); return; }
    set(2, 3, 0);
    int h = hour24() + 1;
    if (h < 24) { setHour24(h); return; }
    setHour24(0);
    d_[6] = uint8_t((d_[6] + 1) % 7);                   // jour de la semaine 0-6 (Hatari/tm_wday)

    static const int mlen[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int yr  = get(11, 12);
    const int mon = get(9, 10);
    const int feb = (bank1_[11] == 0) ? 29 : 28;            // compteur bissextile indépendant, cycle de quatre ans
    const int dmax = (mon >= 1 && mon <= 12) ? (mon == 2 ? feb : mlen[mon]) : 31;
    int day = get(7, 8) + 1;
    if (day <= dmax) { set(7, 8, day); return; }
    set(7, 8, 1);
    int nm = mon + 1;
    if (nm <= 12) { set(9, 10, nm); return; }
    set(9, 10, 1);
    bank1_[11] = uint8_t((bank1_[11] + 1) & 3);
    set(11, 12, (yr + 1) % 100);                        // année (2 chiffres, débord à 00)
}
