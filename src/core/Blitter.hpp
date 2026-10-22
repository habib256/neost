// =============================================================================
//  Blitter.hpp — Blitter ("BLiTTER") de l'Atari ST (Mega ST / STE / Mega STE).
//
//  Copieur de blocs mémoire câblé ($FF8A00-$FF8A3F) : pour chaque mot, combine une
//  source (décalée + masque "skew"), un opérateur halftone (HOP) et un opérateur
//  logique (LOP) avec la destination, sous masques de bord (endmask). La logique
//  de données (HOP, LOP, FXSR/NFSR, smudge, halftone, comptes X/Y, incréments)
//  est un port fidèle de Hatari blitter.c.
//
//  PARTAGE DE BUS (port du modèle cycle-exact d'Hatari, blitter.c) :
//   - mode HOG (bit6 de $FF8A3C) : le blitter garde le bus jusqu'à y_count = 0 ;
//     le CPU est arrêté pendant toute la durée (4 cycles par accès bus, comptés
//     pendant le transfert et facturés via Cpu68k::addBusWaitCycles) ;
//   - mode NON-HOG : le blitter transfère par TRANCHES de 64 accès bus exactement
//     (suspension MID-WORD comme BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED — l'état
//     du mot en cours survit à la coupure), puis rend le bus au CPU pour 64 accès
//     bus CPU RÉELS (port BLITTER_PHASE_COUNT_CPU_BUS : comptés par les callbacks
//     mémoire de Moira, cf. noteCpuBusAccess ; le 64ᵉ arme la fenêtre PRE_START et
//     date la tranche suivante à +4 cycles) — l'alternance 64/64 du vrai matériel.
//     BUSY et les compteurs/adresses sont lisibles EN COURS de blit (progression
//     par tranche) ; effacer BUSY pendant le transfert met le blitter en PAUSE
//     (repris au prochain BUSY=1), comme sur le vrai matériel.
//  Le stall CPU avance l'horloge cycle-exact de Moira, comme les autres wait
//  states de bus. HOG et partagé reprennent après PRE_START pendant l'instruction
//  et conservent un crédit de cycles internes CPU recouverts par la tranche ;
//  le prochain accès bus l'annule (Blitter_Check_Simultaneous_CPU).
//  MegaSTE 16 MHz : les hits du cache CPU sont comptés comme
//  accès bus (approximation, le vrai cache ne sort pas sur le bus).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"

class Bus;

class Blitter {
public:
    explicit Blitter(Bus& bus) : bus_(bus) {}

    // L'ordonnanceur date PRE_START dans les deux modes (cf. onSlice).
    void setScheduler(Scheduler* s) { sched_ = s; }

    uint8_t read8(uint32_t addr);            // $FF8A00-$FF8A3F (relisible)
    void    write8(uint32_t addr, uint8_t v);
    // Écritures MOT/LONG ATOMIQUES : le matériel ne démarre le blitter qu'une fois la
    // case bus terminée. Un « move.w …,$FF8A3C » pose le contrôle (BUSY, octet haut)
    // ET le skew ($FF8A3D, octet bas) ; il faut écrire les DEUX octets AVANT de tester
    // BUSY, sinon run() partirait avec un skew périmé (icônes GEM aux plans désalignés).
    void    write16(uint32_t addr, uint16_t v);
    void    write32(uint32_t addr, uint32_t v);

    void reset(bool cold = false);

    // Échéance Scheduler::BLITTER : 64 accès partagés ou transfert HOG entier.
    void onSlice();

    // Le blit est-il en cours ? (bit BUSY de $FF8A3C). En non-hog il reste à 1
    // entre les tranches, jusqu'à la fin réelle du transfert.
    bool busy() const { return (reg_[0x3C] & 0x80) != 0; }

    // Sérialisation save-state SYMÉTRIQUE (save ET load) : transfère TOUT l'état
    // runtime. On saute bus_ (référence), sched_ (pointeur reposé par Machine).
    void serialize(StateArchive& ar) {
        ar.arr(reg_);           // backing store big-endian des registres MMIO
        ar(buffer_);            // registre à décalage source (persiste entre blits)
        ar(busWord_);           // dernier mot du bus (persiste entre blits)
        ar(xReset_);            // recharge du X count
        ar(yLatch_);            // compteur Y latché (0→65536 à l'écriture)
        ar(midBlit_);           // transfert engagé (même en pause)
        ar(haveFxsr_);          // lecture source extra faite (ligne)
        ar(nfsrInt_);           // dernière lecture source de la ligne sautée
        ar(haveSrc_);           // lecture source du mot courant faite
        ar(haveDst_);           // lecture destination du mot courant faite
        ar(fetchSrc_);          // une source a été lue (màj src_addr en fin de mot)
        ar(dstWord_);           // mot destination lu (survit à la suspension)
        ar(sliceBus_);          // accès bus consommés par la tranche en cours
        ar(cpuBusCnt_);         // accès bus CPU consommés (fenêtre COUNT_CPU_BUS)
        ar(busCountError_);     // accès CPU volé en PRE_START → tranche de 63
    }

private:
    void writeRegisters(uint32_t off, uint32_t value, unsigned bytes);
    void start();                            // BUSY écrit à 1 : démarre/reprend le blit
    bool runSlice(int maxBusAccesses);       // ≤ N accès bus (-1 = tout) ; true = terminé
    void finishTransfer();                   // y_count = 0 : BUSY/HOG effacés + IRQ GPIP3
    void billCycles(int n);                  // facture + dispatche n cyc volés (par accès)
    void controlWritten(bool wasBusy);       // écriture CPU de $FF8A3C : ligne INT + départ/pause
    void pauseTransfer();                    // BUSY effacé pendant un blit : tranche annulée
    // Fenêtre PRE_START avant chaque prise de bus (cf. .cpp) :
    // armée dans Bus::blitterWinStart/End, consultée par les callbacks mémoire de
    // Moira (Cpu68k.cpp) qui signalent un accès CPU tombé dedans.
    void armPreStartWindow(int64_t now, int cycles = 4);   // fenêtre PRE_START (8 au démarrage, 4 à la reprise)
    void clearPreStartWindow();
    uint16_t readWord(uint32_t addr);
    void     writeWord(uint32_t addr, uint16_t v);

public:
    // Bug matériel « 63 accès au lieu de 64 » (blitter.c:69-79) : un accès bus CPU
    // pendant la fenêtre PRE_START est compté à tort par le blitter — la prochaine
    // tranche non-hog ne fera que 63 accès. Appelé par Cpu68k.cpp (Moira seul).
    void notePreStartCpuAccess();
    // Fenêtre CPU non-hog (port Blitter_HOG_CPU_mem_access_after, blitter.c:1603) :
    // chaque accès bus CPU pendant que bus_.blitterCountCpu est armé incrémente le
    // compteur ; le 64ᵉ relance le blitter (PRE_START à now+4). Appelé par Cpu68k.cpp.
    void noteCpuBusAccess(int64_t now);
private:

    Bus&       bus_;
    Scheduler* sched_ = nullptr;
    uint8_t reg_[0x40] = {};                 // backing store big-endian ($FF8A00 base)
    // Hatari : le registre à décalage source (buffer) et le dernier mot du bus
    // (bus_word) PERSISTENT entre blits (remis à 0 seulement au reset matériel).
    // Blitter_CheckAccess_Byte n'actualise pas IoMem sur un registre mot : les
    // octets lus sont ceux du dernier accès CPU, pas les compteurs vivants.
    // Initialisé à l'allocation / reset froid, préservé par le reset matériel.
    uint32_t buffer_  = 0;
    uint16_t busWord_ = 0;
    // État de REPRISE entre tranches (équivalents BlitterVars d'Hatari) : le X
    // count vivant est dans reg_[0x36] (décrémenté, relisible), sa valeur de
    // recharge est ici ; les drapeaux FXSR/NFSR de la ligne en cours survivent
    // à la coupure de tranche.
    uint16_t xReset_   = 0;                  // recharge du X count (x_count_reset, latché
                                             // à CHAQUE écriture de $FF8A36, cf. write16)
    // Compteur Y VIVANT (port Hatari BlitterRegs.y_count) : la conversion 0→65536
    // se fait À L'ÉCRITURE du registre $FF8A38 (Blitter_LinesPerBitblock_WriteWord),
    // PAS à la relecture du résiduel. Indispensable pour distinguer « le programme
    // a écrit 0 = 65536 lignes » d'un « transfert DÉJÀ terminé (résiduel 0) » :
    // dans ce second cas, re-poser BUSY ne relance RIEN (blitter.c:1433-1437 — le
    // protocole restart du driver blitter du TOS re-set BUSY après chaque blit).
    uint32_t yLatch_   = 0;
    // Garde de RÉ-ENTRANCE : vrai pendant runSlice(). Les accès bus du blitter passent
    // par Bus::write16, qui re-route $FF8A00-$FF8A3F vers Blitter::write16 — un blit
    // dont la DESTINATION est son propre registre de contrôle se relançait donc
    // lui-même en plein transfert, avec des compteurs non encore réécrits : récursion
    // infinie et SIGSEGV de l'émulateur, déclenchables par un simple programme émulé.
    // Hatari ne peut pas connaître ce cas : Blitter_Control_WriteByte n'appelle jamais
    // Blitter_Start(), il passe par une phase/interruption différée (blitter.c:1421).
    // Transitoire (toujours faux hors runSlice) → non sérialisé en save-state.
    bool     inSlice_  = false;
    bool     midBlit_  = false;              // un transfert est engagé (même en pause)
    bool     haveFxsr_ = false;              // lecture source extra déjà faite (ligne)
    bool     nfsrInt_  = false;              // dernière lecture source de la ligne sautée
    // État du MOT en cours (équivalents BlitterState d'Hatari) : une tranche peut se
    // suspendre ENTRE deux accès bus d'un même mot (ContinueLater) — ces membres
    // permettent de reprendre exactement à l'accès suivant.
    bool     haveSrc_  = false;              // lecture source du mot courant déjà faite
    bool     haveDst_  = false;              // lecture destination du mot courant déjà faite
    bool     fetchSrc_ = false;              // une source a été lue (màj src_addr en fin de mot)
    uint16_t dstWord_  = 0;                  // mot destination lu (survit à la suspension)
    // int64 : un blit HOG x_count=y_count=0 vaut 65536×65536 → ~8,6e9 accès bus, soit
    // un débordement signé (UB) sur int, et « busAccesses * 4 » débordait dès 5,4e8 —
    // le stall CPU devenait alors négatif et était silencieusement ignoré.
    int64_t  sliceBus_ = 0;                  // accès bus consommés par la tranche en cours
    int      cpuBusCnt_ = 0;                 // accès bus CPU consommés (fenêtre COUNT_CPU_BUS)
    bool     busCountError_ = false;         // accès CPU « volé » en PRE_START → tranche de 63
};
