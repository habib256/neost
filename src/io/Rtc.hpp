// =============================================================================
//  Rtc.hpp — Horloge temps réel RP5C15 du Mega ST / Mega STE ($FFFC21-$FFFC3F).
//
//  Petit RTC sauvegardé par pile, présent UNIQUEMENT sur les machines « Mega ».
//  13 registres de chiffres BCD (4 bits) + mode/test/reset. Sans lui, les
//  diagnostics Mega concluent « No clock installed ». Réf. Hatari rtc.c.
//
//  Modèle PARESSEUX (façon Hatari) : la date initiale vient de l'hôte au démarrage,
//  puis on retient le cycle CPU du dernier « top de seconde » (phase du diviseur)
//  et, à CHAQUE accès, on rattrape les secondes entières écoulées depuis. Le temps
//  avance donc ensuite avec l'horloge ÉMULÉE (cycles), pas avec Date.now. Le registre
//  RESET ($FFFC3F bit1) remet la phase du diviseur à zéro, ce qu'exige le test
//  « clock increment » du diagnostic Mega STE (charge 23:59:59 31/12/99 → 1 s plus
//  tard doit lire 00:00:00 01/01 — débordement calendaire complet, cf. tickOneSecond).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <utility>

class Rtc {
public:
    // Date/heure décimale (année = 0..99 depuis 1980, comme GEMDOS).
    struct DateTime {
        int sec = 0, min = 0, hour = 0;
        int wday = 0;                    // 0 = dimanche (tm_wday)
        int day = 1, month = 1, year = 0;
    };

    Rtc();

    // Source de l'horloge ÉMULÉE (cycle CPU absolu, continu). Branchée par Machine
    // sur sched.now() + le delta intra-quantum du CPU (cf. Cpu68k::cyclesRunInQuantum)
    // pour un cycle exact même au milieu d'une lecture MMIO.
    void setClock(std::function<int64_t()> now) { now_ = std::move(now); }

    uint8_t read8(uint32_t addr);            // $FFFC21-$FFFC3F (adresses impaires)
    void    write8(uint32_t addr, uint8_t v);

    // Instantané après rattrapage (émulation + persistance neost.cfg).
    DateTime getDateTime();
    void     setDateTime(const DateTime& dt);
    void     advanceSeconds(int64_t n);      // pont horloge hôte entre deux sessions (borné)

private:
    void initFromHostTime(); // initialise la date comme Hatari (année GEMDOS depuis 1980)
    void catchUp();          // applique les secondes entières écoulées depuis baseCycle_
    void tickOneSecond();    // +1 s avec retenue calendaire BCD complète (jusqu'à l'année)

    static constexpr int64_t CPU_HZ = 8021248;   // 1 seconde émulée = fréquence CPU

    std::function<int64_t()> now_;
    int64_t baseCycle_ = 0;  // cycle du dernier top de seconde (phase du diviseur 1 Hz)
    bool    primed_    = false;  // baseCycle_ calé sur le 1er accès (évite un rattrapage géant au boot)

    // 13 chiffres BCD : sec.u sec.t min.u min.t h.u h.t weekday j.u j.t mois.u mois.t an.u an.t
    // Base de secours valide ; remplacée au démarrage par initFromHostTime().
    uint8_t d_[13]  = {0,0,0,0,0,0,0,1,0,1,0,0,0};
    uint8_t mode_   = 0;                     // $FFFC3B (bit0 = banque)
    uint8_t test_   = 0;                     // $FFFC3D
    uint8_t reset_  = 0;                     // $FFFC3F

    // Banque 1 RP5C15 : TOS 1.0x y écrit/relit les nibbles AM/PM aux alias
    // $FFFC25/$FFFC27 pour valider la présence de l'horloge Mega (cf. Hatari).
    uint8_t fakeAm_  = 0;
    uint8_t fakeAmz_ = 0;
};
