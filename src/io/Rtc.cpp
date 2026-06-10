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
    return { get(0, 1), get(2, 3), get(4, 5), int(d_[6]), get(7, 8), get(9, 10), get(11, 12) };
}

void Rtc::setDateTime(const DateTime& dt) {
    auto set = [&](int u, int t, int v) {
        if (v < 0) v = 0;
        d_[u] = uint8_t(v % 10);
        d_[t] = uint8_t((v / 10) % 10);
    };
    set(0, 1, dt.sec);
    set(2, 3, dt.min);
    set(4, 5, dt.hour);
    d_[6] = uint8_t(dt.wday & 0x0F);
    set(7, 8, dt.day);
    set(9, 10, dt.month);
    set(11, 12, dt.year);
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
    set(4, 5, tm->tm_hour);
    d_[6] = uint8_t(tm->tm_wday & 0x0F);                 // Hatari expose tm_wday (0=dimanche)
    set(7, 8, tm->tm_mday);
    set(9, 10, tm->tm_mon + 1);
    set(11, 12, (tm->tm_year - 80) % 100);               // GEMDOS : années depuis 1980
}

// Rattrape les secondes entières écoulées depuis le dernier top (baseCycle_), en
// avançant l'horloge BCD. Appelé à chaque accès → le temps lu reflète l'horloge
// émulée à l'instant exact de l'accès (granularité : le cycle CPU).
void Rtc::catchUp() {
    if (!now_) return;
    const int64_t t = now_();
    if (!primed_) { baseCycle_ = t; primed_ = true; return; }  // 1er accès : on cale la phase
    int64_t secs = (t - baseCycle_) / CPU_HZ;
    if (secs <= 0) return;
    baseCycle_ += secs * CPU_HZ;                 // avance la phase d'un multiple entier de 1 s
    if (secs > 90000) secs = 90000;              // borne le travail (>1 jour) : la date exacte
                                                 // au-delà n'intéresse aucun diagnostic
    while (secs-- > 0) tickOneSecond();
}

// addr → index de registre : ($FFFC21,23,...,3F) = 16 registres aux adresses impaires.
//   0..12 = chiffres BCD ; 13 = mode ; 14 = test ; 15 = reset.
uint8_t Rtc::read8(uint32_t addr) {
    catchUp();
    const int i = static_cast<int>((addr - 0xFFFC21) >> 1) & 0x0F;
    if ((mode_ & 0x01) && i == 2) return fakeAm_;          // BANK=1 : alias AM/PM TOS 1.0x
    if ((mode_ & 0x01) && i == 3) return fakeAmz_;
    if (i < 13) return uint8_t(d_[i] & 0x0F);           // chiffre BCD (4 bits utiles)
    if (i == 13) return uint8_t((mode_ & 0x0F) | 0xF0); // mode : bits hauts à 1 (cf. Hatari)
    if (i == 14) return test_;
    return reset_;
}

void Rtc::write8(uint32_t addr, uint8_t v) {
    catchUp();                                          // fige le temps courant AVANT d'écrire
    const int i = static_cast<int>((addr - 0xFFFC21) >> 1) & 0x0F;
    if ((mode_ & 0x01) && i == 2) fakeAm_  = uint8_t((v & 0x0F) | 0xF0);
    else if ((mode_ & 0x01) && i == 3) fakeAmz_ = uint8_t((v & 0x0F) | 0xF0);
    else if (i < 13)  d_[i]  = uint8_t(v & 0x0F);        // réglage de l'heure (chiffre BCD)
    else if (i == 13) mode_  = v;
    else if (i == 14) test_  = v;
    else {            reset_ = v;
        // Registre RESET du RP5C15 : bit1 = reset du diviseur sous-seconde. Remet la
        // phase du 1 Hz à zéro → le prochain top tombe EXACTEMENT 1 s plus tard. Le
        // test d'incrément Mega STE s'en sert pour mesurer un débordement propre.
        if ((v & 0x02) && now_) { baseCycle_ = now_(); primed_ = true; }
    }
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
    int h = get(4, 5) + 1;
    if (h < 24) { set(4, 5, h); return; }
    set(4, 5, 0);
    d_[6] = uint8_t((d_[6] + 1) % 7);                   // jour de la semaine 0-6 (Hatari/tm_wday)

    static const int mlen[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int yr  = get(11, 12);
    const int mon = get(9, 10);
    const int feb = (yr % 4 == 0) ? 29 : 28;            // bissextile simplifiée (règle séculaire ignorée)
    const int dmax = (mon >= 1 && mon <= 12) ? (mon == 2 ? feb : mlen[mon]) : 31;
    int day = get(7, 8) + 1;
    if (day <= dmax) { set(7, 8, day); return; }
    set(7, 8, 1);
    int nm = mon + 1;
    if (nm <= 12) { set(9, 10, nm); return; }
    set(9, 10, 1);
    set(11, 12, (yr + 1) % 100);                        // année (2 chiffres, débord à 00)
}
