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
#include "core/Pacing.hpp"
#include <cstdint>
#include <functional>
#include <utility>
#include "core/StateArchive.hpp"

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
    // Longueur d'une seconde, en cycles CPU, telle que la machine émulée la vit
    // (trame × Hz). Posée par Machine à chaque trame : la géométrie vidéo change
    // (50/60/71 Hz) et l'horloge doit suivre la MÊME base de temps que le reste.
    void setSecondCycles(int64_t c) { if (c > 0) secondCycles_ = c; }

    uint8_t read8(uint32_t addr);            // $FFFC21-$FFFC3F (adresses impaires)
    void    write8(uint32_t addr, uint8_t v);

    // Instantané après rattrapage (émulation + persistance neost.cfg).
    DateTime getDateTime();
    void     setDateTime(const DateTime& dt);
    void     advanceSeconds(int64_t n);      // pont horloge hôte entre deux sessions, sans perte

    // Save-state : phase du diviseur 1 Hz + chiffres BCD + registres de banque. La
    // closure now_ (horloge) est re-liée à la construction → non sérialisée.
    void serialize(StateArchive& ar) {
        ar(baseCycle_); ar(primed_);
        ar(d_); ar(mode_); ar(test_); ar(reset_); ar(bank1_); ar(timerCarry_);
        static constexpr uint8_t masks[13] = {7,1,15,7,15,3,7,15,3,0,1,3,0};
        for (int i = 0; i < 13; ++i)
            ar.check((bank1_[i] & ~masks[i]) == 0, "Rtc::bank1_");
    }

private:
    int hour24() const;
    void setHour24(int hour);
    void initFromHostTime(); // initialise la date comme Hatari (année GEMDOS depuis 1980)
    void catchUp();          // applique les secondes entières écoulées depuis baseCycle_
    void advanceWholeSeconds(uint64_t seconds); // cascade arithmétique, n=1 reste scalaire
    void advanceDays(uint64_t days);            // fins de mois, cycles de 100 ans
    void tickOneSecond();    // +1 s avec retenue calendaire BCD complète (jusqu'à l'année)

    // Longueur d'UNE SECONDE en cycles CPU. Valeur de repli = fréquence CPU PAL ;
    // la Machine la recale sur la base de temps RÉELLE de la trame émulée (cf.
    // setSecondCycles). Une constante figée faisait dériver l'horloge contre le
    // reste de la machine — cf. secondCycles_.
    static constexpr int64_t CPU_HZ = neost::pacing::kCpuHzInt;   // A28 : core/Pacing.hpp

    std::function<int64_t()> now_;
    int64_t baseCycle_ = 0;  // cycle du dernier top de seconde (phase du diviseur 1 Hz)
    // Cycles pour une seconde, recalé chaque trame par la Machine sur la géométrie
    // vidéo courante (trame × Hz). Config dérivée → hors save-state.
    int64_t secondCycles_ = CPU_HZ;
    bool    timerCarry_ = false; // carry sous-seconde retenu pendant TIMER EN=0
    bool    primed_    = false;  // baseCycle_ calé sur le 1er accès (évite un rattrapage géant au boot)

    // 13 chiffres BCD : sec.u sec.t min.u min.t h.u h.t weekday j.u j.t mois.u mois.t an.u an.t
    // Base de secours valide ; remplacée au démarrage par initFromHostTime().
    uint8_t d_[13]  = {0,0,0,0,0,0,0,1,0,1,0,0,0};
    // $FFFC3B. bit0 = banque ; bit3 = TIMER EN (RP5C15) : à 0 le COMPTEUR EST ARRÊTÉ.
    // Défaut TIMER EN posé : sur une vraie Mega ST l'horloge est sauvegardée par pile
    // et compte depuis l'usine ; démarrer à 0 figerait l'heure tant qu'aucun logiciel
    // n'écrit le registre (le TOS ne l'écrit jamais).
    uint8_t mode_   = 0x08;
    uint8_t test_   = 0;                     // $FFFC3D
    uint8_t reset_  = 0;                     // $FFFC3F

    // Ricoh EK-086-9908 p6 : CLKOUT, ADJ, alarmes2..8, réservé9,
    // sélection12/24, compteur bissextile, réservéC. Défaut24h pour l'API hôte.
    // Pas de signal CLKOUT/ALARM synthétique ; ALARM n'est pas câblé sur Mega ST.
    uint8_t bank1_[13] = {0,0,0,0,0,0,0,0,0,0,1,0,0};
};
