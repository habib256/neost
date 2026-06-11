// =============================================================================
//  Blitter.hpp — Blitter ("BLiTTER") de l'Atari ST (Mega ST / STE / Mega STE).
//
//  Copieur de blocs mémoire câblé ($FF8A00-$FF8A3F) : pour chaque mot, combine une
//  source (décalée + masque "skew"), un opérateur halftone (HOP) et un opérateur
//  logique (LOP) avec la destination, sous masques de bord (endmask). La logique
//  de données (HOP, LOP, FXSR/NFSR, smudge, halftone, comptes X/Y, incréments)
//  est un port fidèle de Hatari blitter.c.
//
//  PARTAGE DE BUS (port du modèle non cycle-exact d'Hatari, blitter.c:864-944) :
//   - mode HOG (bit6 de $FF8A3C) : le blitter garde le bus jusqu'à y_count = 0 ;
//     le CPU est arrêté pendant toute la durée (4 cycles par accès bus, comptés
//     pendant le transfert et facturés via Cpu68k::addBusWaitCycles) ;
//   - mode NON-HOG : le blitter transfère par TRANCHES de 64 accès bus (256
//     cycles, CPU arrêté), puis rend le bus au CPU pour 64 accès (256 cycles)
//     avant de reprendre (événement Scheduler::BLITTER) — l'alternance 64/64 du
//     vrai matériel. BUSY et les compteurs/adresses sont lisibles EN COURS de
//     blit (progression par tranche) ; effacer BUSY pendant le transfert met le
//     blitter en PAUSE (repris au prochain BUSY=1), comme sur le vrai matériel.
//  Granularité : la tranche se découpe à la frontière de MOT (un mot peut
//  déborder le budget de ≤3 accès) et le bug « 63 accès » d'Hatari n'est pas
//  modélisé. Sous Musashi (non cycle-exact), le stall CPU est un no-op : seule
//  la DURÉE du blit (BUSY, IRQ) est modélisée, comme les autres wait states.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

#include "core/Scheduler.hpp"

class Bus;

class Blitter {
public:
    explicit Blitter(Bus& bus) : bus_(bus) {}

    // L'ordonnanceur date les tranches non-hog (cf. onSlice). Posé par Machine.
    void setScheduler(Scheduler* s) { sched_ = s; }

    uint8_t read8(uint32_t addr);            // $FF8A00-$FF8A3F (relisible)
    void    write8(uint32_t addr, uint8_t v);
    // Écritures MOT/LONG ATOMIQUES : le matériel ne démarre le blitter qu'une fois la
    // case bus terminée. Un « move.w …,$FF8A3C » pose le contrôle (BUSY, octet haut)
    // ET le skew ($FF8A3D, octet bas) ; il faut écrire les DEUX octets AVANT de tester
    // BUSY, sinon run() partirait avec un skew périmé (icônes GEM aux plans désalignés).
    void    write16(uint32_t addr, uint16_t v);
    void    write32(uint32_t addr, uint32_t v);

    void reset();

    // Échéance Scheduler::BLITTER : tranche non-hog suivante (64 accès bus).
    void onSlice();

    // Le blit est-il en cours ? (bit BUSY de $FF8A3C). En non-hog il reste à 1
    // entre les tranches, jusqu'à la fin réelle du transfert.
    bool busy() const { return (reg_[0x3C] & 0x80) != 0; }

private:
    void start();                            // BUSY écrit à 1 : démarre/reprend le blit
    bool runSlice(int maxBusAccesses);       // ≤ N accès bus (-1 = tout) ; true = terminé
    void finishTransfer();                   // y_count = 0 : BUSY/HOG effacés + IRQ GPIP3
    void stallCpu(int busAccesses, int arbCycles);   // 4 cyc/accès + arbitration (Moira)
    void pauseTransfer();                    // BUSY effacé pendant un blit : tranche annulée
    // Fenêtre PRE_START de 4 cycles avant chaque prise de bus non-hog (cf. .cpp) :
    // armée dans Bus::blitterWinStart/End, consultée par les callbacks mémoire de
    // Moira (Cpu68k.cpp) qui signalent un accès CPU tombé dedans.
    void armPreStartWindow(int64_t now);
    void clearPreStartWindow();
    uint16_t readWord(uint32_t addr);
    void     writeWord(uint32_t addr, uint16_t v);

public:
    // Bug matériel « 63 accès au lieu de 64 » (blitter.c:69-79) : un accès bus CPU
    // pendant la fenêtre PRE_START est compté à tort par le blitter — la prochaine
    // tranche non-hog ne fera que 63 accès. Appelé par Cpu68k.cpp (Moira seul).
    void notePreStartCpuAccess();
private:

    Bus&       bus_;
    Scheduler* sched_ = nullptr;
    uint8_t reg_[0x40] = {};                 // backing store big-endian ($FF8A00 base)
    // Hatari : le registre à décalage source (buffer) et le dernier mot du bus
    // (bus_word) PERSISTENT entre blits (remis à 0 seulement au reset matériel).
    uint32_t buffer_  = 0;
    uint16_t busWord_ = 0;
    // État de REPRISE entre tranches (équivalents BlitterVars d'Hatari) : le X
    // count vivant est dans reg_[0x36] (décrémenté, relisible), sa valeur de
    // recharge est ici ; les drapeaux FXSR/NFSR de la ligne en cours survivent
    // à la coupure de tranche.
    uint16_t xReset_   = 0;                  // recharge du X count (latché au départ)
    bool     midBlit_  = false;              // un transfert est engagé (même en pause)
    bool     haveFxsr_ = false;              // lecture source extra déjà faite (ligne)
    bool     nfsrInt_  = false;              // dernière lecture source de la ligne sautée
    int      sliceBus_ = 0;                  // accès bus consommés par la tranche en cours
    bool     busCountError_ = false;         // accès CPU « volé » en PRE_START → tranche de 63
};
