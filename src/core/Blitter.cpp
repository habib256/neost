// =============================================================================
//  Blitter.cpp — Implémentation du Blitter ST (port de Hatari, données + bus).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Blitter.hpp"
#include <algorithm>
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "io/Mfp.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
    // LOP : l'opérateur logique référence-t-il HOP (source/halftone) et/ou la
    // destination ? (cf. Hatari Blitter_LOP_Table). HOP n'est lu que pour ces LOP,
    // dst seulement pour celles-là.
    constexpr bool LOP_USES_HOP[16] = {0,1,1,1,1,0,1,1,1,1,0,1,1,1,1,0};   // sauf 0,5,A,F
    constexpr bool LOP_USES_DST[16] = {0,1,1,0,1,1,1,1,1,1,1,1,0,1,1,0};   // sauf 0,3,C,F

    // Partage de bus non-hog (blitter.c BLITTER_NONHOG_BUS_*) : 64 accès bus pour
    // le blitter (4 cycles chacun), puis 64 accès bus CPU RÉELS (comptés par les
    // callbacks mémoire de Moira — modèle CE d'Hatari, pas un forfait en cycles).
    constexpr int kNonHogBusBlitter = 64;
    constexpr int kNonHogBusCpu     = 64;
    // Latence avant la prise de bus (phase PRE_START, blitter.c « t+0..t+4 ») et
    // arbitration de restitution blitter → CPU (Blitter_BusArbitration).
    constexpr int kPreStartCycles   = 4;
    constexpr int kArbOut           = 4;
    // Délai avant la prise de bus au DÉMARRAGE d'un blit — port de
    // `Blitter_CyclesBeforeStart = 4 + 4` (blitter.c, branche BLITTER_RUN_CE de
    // Blitter_Control_WriteByte), dont le commentaire d'Hatari dit : « 4 cycles to
    // complete current bus write to ctrl reg + 4 cycles before blitter request the bus ».
    // ⚠ C'est 8 au DÉMARRAGE (écriture de $FF8A3C) mais 4 seulement à la REPRISE d'une
    // tranche non-hog (Blitter_HOG_CPU_mem_access_after pose 4). NeoST utilisait 4 dans
    // les DEUX cas : chaque blit démarrait donc 4 cycles trop tôt. Mesuré par l'étalon
    // `blitter_timer` (chantier BL5) : la dérive vs l'oracle est bien PAR BLIT et non par
    // tranche — doubler la taille du blit (4 → 8 tranches) ne la double pas.
    constexpr int kStartDelayCycles = 4 + 4;
    // Coût d'UN accès bus du blitter, lecture comme écriture — port de
    // BLITTER_CYCLES_PER_BUS_READ / _WRITE (blitter.c). Facturé accès par accès, cf.
    // Blitter::billCycles.
    constexpr int kCyclesPerBusAccess = 4;

    // Masques matériels appliqués À L'ÉCRITURE des registres (la relecture montre
    // la valeur masquée, comme Hatari) : HOP & 3 (Blitter_HalftoneOp_WriteByte,
    // « h/ware reg masks out the top 6 bits »), LOP & 0xF (Blitter_LogOp_WriteByte,
    // blitter.c:1386), contrôle $FF8A3C & 0xEF (bit 4 non câblé,
    // Blitter_Control_WriteByte blitter.c:1421). Le skew ($FF8A3D) garde ses 8 bits :
    // Hatari stocke l'octet ENTIER (Blitter_Skew_WriteByte, pas de masque).
    // Bit 0 des incréments non câblé (& 0xFFFE, Blitter_SourceXInc_WriteWord
    // blitter.c:1229) → octets faibles $FF8A21/23/2F/31 masqués 0xFE. Adresses
    // src/dst : 24 bits pairs (& 0x00FFFFFE, Blitter_SourceAddr_WriteLong
    // blitter.c:1254) → octet fort $FF8A24/32 masqué 0x00, faible $FF8A27/35 0xFE.
    constexpr uint8_t regWriteMask(uint32_t off) {
        return (off == 0x21 || off == 0x23 || off == 0x2F || off == 0x31
                || off == 0x27 || off == 0x35)          ? 0xFE
             : (off == 0x24 || off == 0x32)             ? 0x00
             : off == 0x3A ? 0x03 : off == 0x3B ? 0x0F : off == 0x3C ? 0xEF : 0xFF;
    }
}

void Blitter::reset(bool cold) {
    for (auto& b : reg_) b = 0;
    if (cold) std::fill(std::begin(ioRaw_), std::end(ioRaw_), 0);
    buffer_ = 0; busWord_ = 0; yLatch_ = 0;
    midBlit_ = false; haveFxsr_ = false; nfsrInt_ = false;
    haveSrc_ = false; haveDst_ = false; fetchSrc_ = false; dstWord_ = 0;
    busCountError_ = false;
    cpuBusCnt_ = 0; bus_.blitterCountCpu = false;
    clearPreStartWindow();
    if (sched_) sched_->cancel(Scheduler::BLITTER);
}

// Accès bus du blitter — port de STMemory_DMA_ReadWord/WriteWord (stMemory.c:724) :
// même plan mémoire que le CPU, mais une zone fautive lit 0x0000 (DMA_READ_WORD_BUS_ERR)
// / absorbe l'écriture au lieu de déclencher une bus error (le blitter est un maître
// de bus, pas de cycle d'exception). Les vecteurs reset $0-$7 (miroir ROM) sont
// protégés en écriture aussi pour les maîtres non-CPU (Hatari SysMem_wput, memory.c).
uint16_t Blitter::readWord(uint32_t addr) {
    ++sliceBus_;
    addr &= 0xFFFFFE;
    const uint16_t v = bus_.busFault(addr) ? 0x0000 : bus_.read16(addr);
    // Port de Blitter_ReadWord (blitter.c:428-441) : l'accès est effectué D'ABORD,
    // PUIS ses 4 cycles sont facturés ET dispatchés (Blitter_AddCycles suivi de
    // Blitter_FlushCycles). La facturation est inconditionnelle — une zone fautive
    // coûte le même temps de bus qu'une lecture normale.
    billCycles(kCyclesPerBusAccess);
    return v;
}
void Blitter::writeWord(uint32_t addr, uint16_t v) {
    ++sliceBus_;
    addr &= 0xFFFFFE;
    if (!bus_.busFault(addr) && addr >= 0x8) bus_.write16(addr, v);
    billCycles(kCyclesPerBusAccess);   // cf. readWord (Blitter_WriteWord, blitter.c:444-453)
}

// Facture le temps de bus volé par le blitter — port de Blitter_AddCycles suivi de
// Blitter_FlushCycles (blitter.c:342-375). Appelé pour CHAQUE accès bus (4 cycles) et
// pour chaque arbitration (prise 4/8, restitution 4), JAMAIS en lot : c'est la
// granularité d'Hatari, et c'est elle qui permet à une échéance tombant au milieu
// d'une tranche d'être servie à l'heure (chantier BL4). Moira avance son horloge — le
// CPU « attend » que le blitter rende le bus, cf. addBusWaitCycles.
void Blitter::billCycles(int n) {
    // UNE SEULE BASE DE TEMPS (port de Blitter_AddCycles, blitter.c:351-352) : chez
    // Hatari les cycles du blitter incrémentent le MÊME compteur que ceux du CPU,
    // celui que CycInt lit pour ses échéances. Ici deux chemins de facturation
    // coexistent et il ne faut créditer l'ordonnanceur QUE sur l'un des deux :
    //  · DANS le quantum (mode HOG : `start()` n'est atteint que depuis une écriture
    //    $FF8A3C, donc depuis un callback mémoire de Moira) → l'avance de l'horloge
    //    du cœur est DÉJÀ comptée par `ran` (retour de Cpu68k::run) puis reversée par
    //    le `sched.runTo(now + ran)` de Machine::runFrame. Créditer ici la compterait
    //    DEUX FOIS, et l'ordonnanceur prendrait de l'AVANCE sur l'horloge CPU — le
    //    symétrique exact du bug corrigé.
    //  · HORS quantum (tranche non-hog : `onSlice` est le callback de l'échéance
    //    Scheduler::BLITTER, il tourne dans Scheduler::runTo, donc ENTRE deux
    //    cpu.run()) → personne ne les compte. C'était le bug : sched.now() restait en
    //    retard, puis sautait d'un coup au rebase d'IACK suivant.
    // L'invariant complet (discriminant Cpu68k::inRun) vit dans Bus::stealBusCycles
    // (A23) — le FDC (billDmaCycles) partage la même primitive.
    bus_.stealBusCycles(sched_, n);
}

uint8_t Blitter::read8(uint32_t addr) {
    const uint32_t off = addr & 0x3F;
    // $FF8A3E/$FF8A3F : zone void du blitter (Hatari ioMemTabSTE.c:199 = IoMem_VoidRead,
    // « No bus error here ») → lit 0xFF, pas la case reg_ (qui valait 0x00).
    if (off >= 0x3E) return 0xFF;
    if (off < 0x3A) {
        // Le handler mot ne touche pas IoMem pour une lecture octet. En lecture
        // mot/long, il rafraîchit TOUT son registre (4 octets pour src/dst_addr),
        // même si le bus ne demande que la moitié de l'adresse.
        if (bus_.ioAccessWidth() == 1) return ioRaw_[off];
        const uint32_t first = (off >= 0x24 && off < 0x28) ? 0x24
                             : (off >= 0x32 && off < 0x36) ? 0x32 : off & ~1u;
        const uint32_t size = (first == 0x24 || first == 0x32) ? 4 : 2;
        std::copy(reg_ + first, reg_ + first + size, ioRaw_ + first);
    }
    return reg_[off];
}

void Blitter::write8(uint32_t addr, uint8_t v) {
    const uint32_t off = addr & 0x3F;
    // IoMem_bput stocke l'octet AVANT Blitter_CheckAccess_Byte : l'écriture
    // rejetée reste donc relisible sans modifier le registre matériel.
    if (off < 0x3A) { ioRaw_[off] = v; return; }
    // Accès OCTET valides UNIQUEMENT pour les registres 8 bits HOP/LOP/contrôle/
    // skew ($FF8A3A-$FF8A3D) ; un accès octet à un registre MOT (halftone RAM,
    // adresses, incréments, endmasks, compteurs : $FF8A00-$FF8A39) est IGNORÉ,
    // comme sur le vrai blitter (cf. Hatari Blitter_CheckAccess_Byte). Les
    // écritures mot/long, elles, passent par write16/write32 (Bus). $FF8A3E/3F = void
    // (IoMem_VoidWrite) → ignorées aussi.
    if (off < 0x3A || off >= 0x3E) return;
    reg_[off] = v & regWriteMask(off);   // bits non câblés masqués À L'ÉCRITURE (cf. regWriteMask)
    if (off == 0x3C && !inSlice_) {
        // Écriture du registre contrôle ($FF8A3C) : BUSY (bit7) à 1 → démarre ou
        // REPREND ; à 0 pendant un transfert → PAUSE (le CPU peut arrêter le
        // blitter en non-hog, cf. blitter.c:88 — état conservé, reprise au
        // prochain BUSY=1).
        // !inSlice_ : une écriture VENANT DU BLITTER lui-même (blit dont la
        // destination est sa propre zone registre) pose les octets et rien de plus,
        // comme chez Hatari où le contrôle ne relance jamais le transfert en direct.
        if (v & 0x80) start();
        else if (midBlit_) pauseTransfer();
    }
}

void Blitter::writeRegisters(uint32_t off, uint32_t value, unsigned bytes) {
    // IoMem_wput/lput posent d'abord tous les octets bruts, puis chaque handler
    // reconstruit son registre entier. Une écriture mot sur src/dst_addr doit
    // donc reprendre l'autre moitié dans IoMem, y compris ses octets rejetés.
    for (unsigned i = 0; i < bytes && off + i < 0x3E; ++i) {
        const uint8_t v = uint8_t(value >> (8 * (bytes - 1 - i)));
        if (off + i < 0x3A) ioRaw_[off + i] = v;
        else reg_[off + i] = v & regWriteMask(off + i);
    }
    for (uint32_t pos = off; pos < off + bytes && pos < 0x3A; ) {
        const uint32_t first = (pos >= 0x24 && pos < 0x28) ? 0x24
                             : (pos >= 0x32 && pos < 0x36) ? 0x32 : pos & ~1u;
        const uint32_t size = (first == 0x24 || first == 0x32) ? 4 : 2;
        for (uint32_t i = first; i < first + size; ++i)
            reg_[i] = ioRaw_[i] & regWriteMask(i);
        pos = first + size;
    }
}

// Écritures mot/long ATOMIQUES : on pose TOUS les octets, PUIS on démarre si le
// registre contrôle ($FF8A3C, offset 0x3C) faisait partie de l'écriture et porte le
// bit BUSY. Garantit que le skew ($FF8A3D) est en place avant le départ (cf.
// Blitter.hpp). Sinon « move.w …,$FF8A3C » lancerait le blit avec l'ancien skew
// → plan 0 décalé par rapport aux plans 1-3 (franges de couleur).
void Blitter::write16(uint32_t addr, uint16_t v) {
    const uint32_t off = addr & 0x3F;
    writeRegisters(off, v, 2);
    // X count ($FF8A36) : recharge x_count_reset latchée À CHAQUE écriture du
    // registre (Hatari Blitter_WordsPerLine_WriteWord, blitter.c:1338-1350) — pas
    // seulement au départ du blit. La règle 0→65536 est portée par la
    // représentation 16 bits (0 ≡ 65536 via le bouclage, cf. start()).
    if (off <= 0x36 && off + 1 >= 0x37)
        xReset_ = uint16_t((reg_[0x36] << 8) | reg_[0x37]);
    // Y count ($FF8A38) : conversion 0→65536 latchée À L'ÉCRITURE (Hatari
    // Blitter_LinesPerBitblock_WriteWord) — cf. yLatch_ (Blitter.hpp).
    if (off <= 0x38 && off + 1 >= 0x39) {
        const uint16_t y = uint16_t((reg_[0x38] << 8) | reg_[0x39]);
        yLatch_ = y ? y : 65536u;
    }
    if (off <= 0x3C && off + 1 >= 0x3C && !inSlice_) {   // cf. write8 : pas de relance ré-entrante
        if (reg_[0x3C] & 0x80) start();
        else if (midBlit_) pauseTransfer();
    }
}

void Blitter::write32(uint32_t addr, uint32_t v) {
    const uint32_t off = addr & 0x3F;
    writeRegisters(off, v, 4);
    // X count ($FF8A36) couvert par l'écriture longue → latch de x_count_reset
    // (cf. write16 / Hatari Blitter_WordsPerLine_WriteWord).
    if (off <= 0x36 && off + 3 >= 0x37)
        xReset_ = uint16_t((reg_[0x36] << 8) | reg_[0x37]);
    // Y count ($FF8A38) couvert par l'écriture longue → latch 0→65536 (cf. write16).
    if (off <= 0x38 && off + 3 >= 0x39) {
        const uint16_t y = uint16_t((reg_[0x38] << 8) | reg_[0x39]);
        yLatch_ = y ? y : 65536u;
    }
    if (off <= 0x3C && off + 3 >= 0x3C && !inSlice_) {   // cf. write8 : pas de relance ré-entrante
        if (reg_[0x3C] & 0x80) start();
        else if (midBlit_) pauseTransfer();
    }
}

// -----------------------------------------------------------------------------
//  Démarrage / reprise (BUSY écrit à 1). Mode HOG : tout le transfert d'un coup,
//  CPU arrêté pour la durée totale. Non-hog : 1re tranche de 64 accès tout de
//  suite (le blitter prend le bus en premier), puis alternance via l'ordonnanceur.
// -----------------------------------------------------------------------------
void Blitter::start() {
    // Compteur X écrit à 0 = 65536 (cf. Hatari Blitter_WordsPerLine_WriteWord) via
    // le bouclage 16 bits (fin de ligne à xCount==1) quand xReset_=0 ; la recharge
    // xReset_ (x_count_reset) est latchée À L'ÉCRITURE du registre $FF8A36, PAS
    // ici (cf. write16/write32). Côté Y, le compteur vivant est yLatch_ (0→65536
    // converti À L'ÉCRITURE du registre).

    // Transfert DÉJÀ terminé (y_count résiduel 0) : poser BUSY ne relance RIEN —
    // BUSY et HOG sont effacés (port fidèle blitter.c:1433-1437). C'est le
    // protocole restart du driver blitter du TOS (re-set BUSY après chaque blit) :
    // l'ancien code relisait 0→65536 et relançait un blit de 65536 lignes qui
    // labourait toute la RAM (bureau TOS 1.06 STE scramblé au moindre redraw).
    if (!midBlit_ && yLatch_ == 0) {
        reg_[0x3C] &= ~(0x80 | 0x40);
        return;
    }

    // Un (re)démarrage supplante la fenêtre CPU en cours (Hatari : l'écriture du
    // contrôle repasse en PRE_START sans attendre les accès CPU restants).
    bus_.blitterCountCpu = false;

    if (!midBlit_) {                       // vrai départ (pas une reprise de pause)
        haveFxsr_ = false;
        nfsrInt_  = false;
        haveSrc_  = false;                 // Blitter_FlushWordState(true) — état de mot vierge
        haveDst_  = false;
        fetchSrc_ = false;
        midBlit_  = true;
        // Diag : NEOST_BLIT_TRACE=1 → un état des lieux par blit sur stderr (pendant
        // du « --trace blitter » d'Hatari ; cf. NEOST_PAL_TRACE pour la palette).
        static const bool blitTrace = std::getenv("NEOST_BLIT_TRACE") != nullptr;
        if (blitTrace) {
            auto r16 = [&](uint32_t o) { return unsigned((reg_[o] << 8) | reg_[o + 1]); };
            std::fprintf(stderr, "[BLIT] src=%06X dst=%06X x=%u y=%u hop=%u lop=%u ctrl=%02X skew=%02X\n",
                         unsigned(((r16(0x24) << 16) | r16(0x26)) & 0xFFFFFF),
                         unsigned(((r16(0x32) << 16) | r16(0x34)) & 0xFFFFFF),
                         r16(0x36), unsigned(yLatch_), reg_[0x3A] & 3u, reg_[0x3B] & 0xFu,
                         reg_[0x3C], reg_[0x3D]);
        }
        // Ré-arme la ligne GPU_DONE (GPIP3) à l'état HAUT « pas fini » au démarrage de
        // CHAQUE blit (cf. Hatari Blitter_Start blitter.c:895) ; finishTransfer la
        // rabaissera à l'achèvement. Sans ça, GPIP3 resterait « fini » dès le 1ᵉʳ blit
        // → un programme qui scrute la fin de blit verrait un faux positif au 2ᵉ blit.
        if (bus_.mfp) bus_.mfp->setBlitterLine(false);
    }

    // Cycles d'arbitration de bus (port Blitter_BusArbitration) : prendre le bus
    // coûte 4 cycles (8 sur Mega STE), le rendre au CPU 4 cycles.
    const int arbIn = (bus_.machine == MachineType::MegaSte) ? 8 : 4;

    if (reg_[0x3C] & 0x40) {               // mode HOG : bus gardé jusqu'à y_count=0
        // Arbitration facturée à SA position (Blitter_BusArbitration, blitter.c:395-421) :
        // prise du bus AVANT le transfert, restitution APRÈS — et non les deux en lot
        // à la fin. Les 4 cycles de chaque accès sont facturés par readWord/writeWord.
        billCycles(arbIn);
        sliceBus_ = 0;
        inSlice_ = true; runSlice(-1); inSlice_ = false;
        billCycles(kArbOut);
        return;
    }
    // Non-hog : sur le vrai matériel, le blitter ne prend pas le bus tout de
    // suite — phase PRE_START de 4 cycles (le CPU tourne encore) puis arbitration.
    // On date la 1re tranche à +4 et on ARME la fenêtre PRE_START : un accès bus
    // CPU dans [maintenant, +4) sera compté à tort par le blitter (bug « 63 accès »,
    // cf. notePreStartCpuAccess). Sans ordonnanceur : tranche immédiate.
    if (sched_) {
        // Entrée en phase PRE_START : le compteur d'erreur repart de zéro (Hatari
        // Blitter_HOG_CPU_BusCountError = 0, blitter.c:1457) — un accès volé dans
        // une fenêtre PÉRIMÉE (avant un restart/reprise) ne compte pas.
        busCountError_ = false;
        armPreStartWindow(sched_->liveNow(), kStartDelayCycles);
        sched_->schedule(Scheduler::BLITTER, sched_->liveNow() + kStartDelayCycles);
    } else {
        onSlice();
    }
}

// Tranche non-hog (échéance Scheduler::BLITTER) : 64 accès bus exactement — 63 si
// un accès CPU est tombé dans la fenêtre PRE_START (le blitter le compte à tort
// comme sien, cf. blitter.c:69-79) — avec suspension MID-WORD si le budget tombe
// entre deux accès d'un même mot. Ici on est à une frontière d'événement : avancer
// l'horloge CPU (billCycles) retarde d'autant le prochain bloc d'exécution — le CPU
// « perd » arbitration + part du blitter, puis garde le bus pour 64 accès bus CPU
// RÉELS (comptés par noteCpuBusAccess, qui datera la tranche suivante).
void Blitter::onSlice() {
    // Chantier BL4 : les accès bus dispatchent désormais l'ordonnanceur (billCycles →
    // addStolenCycles → syncTo), donc un callback peut tourner PENDANT la tranche. Rien
    // ne réarme Scheduler::BLITTER dans cette fenêtre (l'échéance a été consommée avant
    // l'appel, et noteCpuBusAccess suppose un accès bus CPU, impossible ici), mais on
    // ne s'en remet pas à ce raisonnement : garde explicite, comme write8/write16.
    if (inSlice_) return;
    if (!(reg_[0x3C] & 0x80)) { clearPreStartWindow(); return; }   // pause/reset
    const int arbIn  = (bus_.machine == MachineType::MegaSte) ? 8 : 4;
    const int budget = kNonHogBusBlitter - (busCountError_ ? 1 : 0);
    busCountError_ = false;
    clearPreStartWindow();
    // SONDE (NEOST_BLIT_START_DIAG=1) : cycle absolu de chaque PRISE DE BUS non-hog.
    // C'est la grandeur du résidu BL5 (~10 cyc/blit + ~3,3/reprise, de trop peu côté
    // NeoST) : comparer les INTERVALLES entre prises consécutives à ceux d'Hatari
    // (fprintf « blitter bus start » de Blitter_BusArbitration, à décommenter dans
    // l'arbre local extern/hatari) — les intervalles s'affranchissent du référentiel
    // absolu, que le RNG de boot d'Hatari rend incomparable.
    static const bool startDiag = std::getenv("NEOST_BLIT_START_DIAG") != nullptr;
    if (startDiag && sched_)
        std::fprintf(stderr, "[BLSTART] %lld\n", (long long)sched_->liveNow());
    billCycles(arbIn);                     // prise du bus, AVANT les accès (cf. start)
    sliceBus_ = 0;
    inSlice_ = true;
    const bool done = runSlice(budget);
    inSlice_ = false;
    static const bool endDiag = std::getenv("NEOST_BLIT_START_DIAG") != nullptr;
    if (endDiag && sched_)
        std::fprintf(stderr, "[BLEND] %lld\n", (long long)sched_->liveNow());
    billCycles(kArbOut);                   // restitution au CPU
    if (!done) {
        // Part CPU non-hog : armer le comptage des accès bus CPU (port
        // BLITTER_PHASE_COUNT_CPU_BUS + CountBusCpu = 0, blitter.c:937). Le CPU ne
        // reprend qu'après le stall facturé ci-dessus ; son 64ᵉ accès déclenchera
        // PRE_START (+4 cycles) via noteCpuBusAccess. (Ancien modèle : forfait de
        // 256 cycles, non-CE — le CPU « payait » sa part même sans toucher le bus.)
        cpuBusCnt_ = 0;
        bus_.blitterCountCpu = true;
    }
}

// 64ᵉ accès bus CPU de la fenêtre non-hog (appelé par les callbacks mémoire de
// Moira, cf. Cpu68k.cpp) : le blitter redemande le bus — phase PRE_START de 4
// cycles (fenêtre « 63 accès » armée), tranche suivante datée à now+4 (port
// Blitter_HOG_CPU_mem_access_after, blitter.c:1616-1621).
void Blitter::noteCpuBusAccess(int64_t now) {
    if (++cpuBusCnt_ < kNonHogBusCpu) return;
    bus_.blitterCountCpu = false;
    cpuBusCnt_ = 0;
    busCountError_ = false;
    armPreStartWindow(now);
    if (sched_) sched_->schedule(Scheduler::BLITTER, now + kPreStartCycles);
    else onSlice();
}

// Fenêtre PRE_START [t, t+4) : pendant ces 4 cycles le bit BUSY est posé mais le
// blitter n'a pas encore le bus — il compte pourtant déjà les accès. Un accès bus
// CPU dans la fenêtre (signalé par les accès mémoire de Moira via Bus) lui
// vole un accès : la tranche suivante n'en fera que 63.
void Blitter::armPreStartWindow(int64_t now, int cycles) {
    bus_.blitterWinStart = now;
    bus_.blitterWinEnd   = now + cycles;
}

void Blitter::clearPreStartWindow() {
    bus_.blitterWinStart = bus_.blitterWinEnd = -1;
}

void Blitter::notePreStartCpuAccess() {
    busCountError_ = true;                 // port Blitter_HOG_CPU_BusCountError = 1
}

// PAUSE du transfert (BUSY effacé par le CPU pendant un blit non-hog) : l'état
// reste en place (reprise au prochain BUSY=1), la tranche datée est annulée et
// la fenêtre de comptage CPU désarmée (Hatari : phase COUNT_CPU_BUS → PAUSE).
void Blitter::pauseTransfer() {
    clearPreStartWindow();
    busCountError_ = false;
    bus_.blitterCountCpu = false;
    if (sched_) sched_->cancel(Scheduler::BLITTER);
}

// Fin de transfert (yCount==0) : le blitter abaisse la ligne GPU_DONE (GPIP3,
// active bas) — le canal 3 est levé par le détecteur de FRONT du setter (règle
// AER, IPR posé seulement si IERB arme le canal), cf. Hatari Blitter_Start
// ligne 916. N'est atteint que sur Mega ST/STE/Mega STE (le blitter n'est
// câblé au bus que sur ces modèles → auto-gaté).
void Blitter::finishTransfer() {
    midBlit_ = false;
    if (bus_.mfp) bus_.mfp->setBlitterLine(true);
}

// -----------------------------------------------------------------------------
//  Transfert par tranche — port de Hatari Blitter_Step/ProcessWord. L'état de
//  reprise vit dans les registres relisibles (adresses, X/Y count, ligne
//  halftone) + les membres haveFxsr_/nfsrInt_/buffer_/busWord_ : une tranche
//  reprend exactement où la précédente s'est arrêtée. `maxBusAccesses` < 0 =
//  transfert complet (HOG) ; la découpe se fait à la frontière de MOT.
// -----------------------------------------------------------------------------
bool Blitter::runSlice(int maxBusAccesses) {
    // Le blitter prend le bus via BGACK → le cache 16 Ko du Mega STE est invalidé
    // (Hatari MegaSTE_Cache_Flush) : ses écritures RAM ne passent pas par le cache.
    bus_.megaSteCacheFlushIfEnabled();
    auto rd16 = [&](uint32_t o) -> uint16_t { return uint16_t((reg_[o] << 8) | reg_[o + 1]); };
    auto wr16 = [&](uint32_t o, uint16_t w) { reg_[o] = uint8_t(w >> 8); reg_[o + 1] = uint8_t(w); };
    auto rd32 = [&](uint32_t o) -> uint32_t { return (uint32_t(rd16(o)) << 16) | rd16(o + 2); };
    auto s16  = [&](uint32_t o) -> int32_t  { return int16_t(rd16(o)); };

    const int      hop     = reg_[0x3A] & 3;
    const int      lop     = reg_[0x3B] & 0xF;
    const uint8_t  ctrl    = reg_[0x3C];
    const bool     smudge  = (ctrl & 0x20) != 0;
    const uint8_t  skewReg = reg_[0x3D];
    const int      skew    = skewReg & 0x0F;
    const bool     nfsr    = (skewReg & 0x40) != 0;   // NFSR = bit6 ($40)
    const bool     fxsr    = (skewReg & 0x80) != 0;   // FXSR = bit7 ($80)

    const int32_t  srcXinc = s16(0x20), srcYinc = s16(0x22);
    uint32_t       srcAddr = rd32(0x24) & 0xFFFFFE;
    const uint16_t em1 = rd16(0x28), em2 = rd16(0x2A), em3 = rd16(0x2C);
    const int32_t  dstXinc = s16(0x2E), dstYinc = s16(0x30);
    uint32_t       dstAddr = rd32(0x32) & 0xFFFFFE;
    const uint16_t xReset  = xReset_;
    uint16_t       xCount  = rd16(0x36);              // compteur VIVANT (relisible)
    int            yCount  = int(yLatch_);            // compteur Y VIVANT (0→65536 latché
                                                      // à l'écriture, cf. Blitter.hpp)
    int            htLine  = ctrl & 0x0F;             // ligne halftone courante

    // Registre à décalage source (32 bits) + dernier mot ayant transité sur le BUS.
    // Hatari : BlitterState.bus_word est mis à jour à CHAQUE accès bus du blitter —
    // lecture source, lecture destination ET écriture destination (cf. blitter.c
    // Blitter_ReadWord l.440 / Blitter_WriteWord l.446). Le cas particulier NFSR
    // (Blitter_SourceFetch(true)) réinjecte ce bus_word dans le registre à décalage.
    // Au dernier mot d'une ligne NFSR la lecture source normale est SAUTÉE, donc le
    // dernier accès bus est la lecture (ou l'écriture) de la destination : c'est bien
    // CE mot qui est réinjecté, pas la dernière source. (Bug corrigé : on suivait
    // « lastSrc » = dernière source, d'où des pixels parasites sur les icônes GEM.)
    uint32_t buffer  = buffer_;     // persistance Hatari (pas de remise à 0 par blit)
    uint16_t busWord = busWord_;
    bool     haveFxsr = haveFxsr_;
    bool     nfsrInt  = nfsrInt_;
    // État du mot en cours (BlitterState d'Hatari) : permet la suspension MID-WORD
    // — la tranche rend le bus exactement au 64ᵉ accès, même entre la lecture et
    // l'écriture d'un même mot (BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED).
    bool     haveSrc  = haveSrc_;
    bool     haveDst  = haveDst_;
    bool     fetchSrc = fetchSrc_;
    uint16_t dstWord  = dstWord_;
    // Budget atteint ? (testé APRÈS chaque accès bus, comme Blitter_ContinueNonHog)
    auto budgetHit = [&]() { return maxBusAccesses >= 0 && sliceBus_ >= maxBusAccesses; };

    auto srcShift = [&]() { if (srcXinc < 0) buffer >>= 16; else buffer <<= 16; };
    auto srcFetch = [&](bool nfsrOn) {
        const uint32_t w = nfsrOn ? busWord : (busWord = readWord(srcAddr));
        if (srcXinc < 0) buffer |= w << 16; else buffer |= w;
    };
    auto srcRead = [&]() -> uint16_t { return uint16_t(buffer >> skew); };
    auto halftoneWord = [&]() -> uint16_t {
        return smudge ? rd16(0x00 + (srcRead() & 15) * 2) : rd16(0x00 + htLine * 2);
    };
    auto computeHOP = [&]() -> uint16_t {
        switch (hop) {
            case 0: return 0xFFFF;
            case 1: return halftoneWord();
            case 2: return srcRead();
            default: return uint16_t(srcRead() & halftoneWord());
        }
    };

    while (yCount > 0) {
        const bool firstWord = (xCount == xReset);
        const uint16_t endMask = (firstWord || xReset == 1) ? em1
                               : (xCount == 1)              ? em3 : em2;
        if (firstWord) nfsrInt = false;

        const bool needSrc = LOP_USES_HOP[lop] && ((hop & 2) || (hop == 1 && smudge));
        const bool needDst = LOP_USES_DST[lop] || (endMask != 0xFFFF);

        // --- ProcessWord --- (chaque accès bus est suivi du test de budget : la
        // tranche peut se suspendre ICI, au milieu du mot — les drapeaux have*
        // feront sauter les étapes déjà faites à la reprise, cf. Blitter_ProcessWord)
        if (firstWord && fxsr && needSrc && !haveFxsr) {       // FXSR : lecture source extra
            srcShift(); srcFetch(false); srcAddr += srcXinc; haveFxsr = true;
            if (budgetHit()) break;
        }
        if (needSrc && !haveSrc && !nfsrInt) {                  // lecture source normale
            srcShift(); srcFetch(false); haveSrc = true; fetchSrc = true;
            if (budgetHit()) break;
        }
        // Lecture destination : met aussi à jour busWord (Blitter_ReadWord). Quand
        // needDst est faux, dstWord (rance) n'est jamais consommé — ni par le LOP
        // (LOP_USES_DST faux) ni par le masque (endMask plein).
        if (needDst && !haveDst) {
            dstWord = busWord = readWord(dstAddr); haveDst = true;
            if (budgetHit()) break;
        }
        // Cas particulier NFSR (1/2) : AVANT le LOP. Réinjecte busWord (= la dst qu'on
        // vient de lire, la source normale ayant été sautée) dans le registre source.
        if (nfsr && xCount == 1) { srcShift(); srcFetch(true); }

        const uint16_t hopv = computeHOP();
        uint16_t lopv;
        switch (lop) {
            case 0x0: lopv = 0;                          break;
            case 0x1: lopv = uint16_t( hopv &  dstWord); break;
            case 0x2: lopv = uint16_t( hopv & ~dstWord); break;
            case 0x3: lopv = hopv;                       break;
            case 0x4: lopv = uint16_t(~hopv &  dstWord); break;
            case 0x5: lopv = dstWord;                    break;
            case 0x6: lopv = uint16_t( hopv ^  dstWord); break;
            case 0x7: lopv = uint16_t( hopv |  dstWord); break;
            case 0x8: lopv = uint16_t(~hopv & ~dstWord); break;
            case 0x9: lopv = uint16_t(~(hopv ^ dstWord));break;
            case 0xA: lopv = uint16_t(~dstWord);         break;
            case 0xB: lopv = uint16_t( hopv | ~dstWord); break;
            case 0xC: lopv = uint16_t(~hopv);            break;
            case 0xD: lopv = uint16_t(~hopv |  dstWord); break;
            case 0xE: lopv = uint16_t(~hopv | ~dstWord); break;
            default:  lopv = 0xFFFF;                     break;
        }
        const uint16_t out = (endMask != 0xFFFF)
            ? uint16_t((lopv & endMask) | (dstWord & ~endMask)) : lopv;
        writeWord(dstAddr, out);
        busWord = out;                         // l'écriture dst met aussi à jour bus_word

        // Cas particulier NFSR (2/2) : APRÈS l'écriture. Hatari répète le shift+fetch
        // (blitter.c l.738-743) ; réinjecte le mot qu'on vient d'écrire. Sans FXSR le
        // registre source est conservé d'une ligne à l'autre, donc cette 2ᵉ passe doit
        // être fidèlement reproduite pour l'alignement du registre à décalage.
        if (nfsr && xCount == 1) { srcShift(); srcFetch(true); }

        // --- mise à jour compteurs/adresses ---
        if (xCount == 2 && nfsr) nfsrInt = true;
        if (fetchSrc) srcAddr += (xCount == 1 || nfsrInt) ? srcYinc : srcXinc;
        if (xCount == 1) {                                 // fin de ligne
            haveFxsr = false;
            --yCount;
            xCount = xReset;
            dstAddr += dstYinc;
            htLine = (dstYinc >= 0) ? (htLine + 1) & 15 : (htLine - 1) & 15;
        } else {
            --xCount;
            dstAddr += dstXinc;
        }
        haveSrc = haveDst = fetchSrc = false;   // mot achevé (Blitter_FlushWordState)
        if (budgetHit()) break;                 // 64ᵉ accès = l'écriture : tranche pleine
    }

    // État de reprise : registres relisibles (progression visible du CPU) + membres.
    buffer_ = buffer; busWord_ = busWord;   // persistance Hatari du registre à décalage
    haveFxsr_ = haveFxsr; nfsrInt_ = nfsrInt;
    // État du mot en cours (suspension mid-word) : sans cette sauvegarde, la reprise
    // REFERAIT la lecture source (double srcShift → pipeline skew corrompu, mots
    // perdus au bord des icônes GEM — bug débusqué à la fenêtre [BW] du walk-mouse).
    haveSrc_ = haveSrc; haveDst_ = haveDst; fetchSrc_ = fetchSrc; dstWord_ = dstWord;
    wr16(0x24, uint16_t(srcAddr >> 16)); wr16(0x26, uint16_t(srcAddr));
    wr16(0x32, uint16_t(dstAddr >> 16)); wr16(0x34, uint16_t(dstAddr));
    wr16(0x36, xCount);
    wr16(0x38, uint16_t(yCount));          // 65536 → 0 (16 bits) : readback fidèle au matériel
    yLatch_ = uint32_t(yCount);            // compteur vivant (résiduel SANS re-conversion)

    // Réécriture de $FF8A3C en fin de tranche — équivaut au Hatari
    // « ctrl = (ctrl & 0xF0) | halftone_line » (blitter.c:908) puis, blit fini,
    // « ctrl &= ~(0x80|0x40) » (blitter.c:913) : le bit 4 est toujours 0 grâce au
    // masque 0xEF appliqué à l'écriture (cf. regWriteMask), donc &0x70 / &0x30
    // reproduisent exactement &0xF0 avec/sans BUSY+HOG.
    if (yCount > 0) {                       // tranche finie, transfert pas terminé
        reg_[0x3C] = uint8_t(0x80 | (ctrl & 0x70) | (htLine & 0x0F));   // BUSY maintenu
        return false;
    }
    reg_[0x3C] = uint8_t((ctrl & 0x30) | (htLine & 0x0F));   // BUSY(7)+HOG(6) effacés
    finishTransfer();
    return true;
}
