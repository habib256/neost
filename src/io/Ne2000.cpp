// =============================================================================
//  Ne2000.cpp — Carte NE2000 (DP8390) émulée (cf. Ne2000.hpp).
//
//  Port du comportement documenté du DP8390 : Remote DMA (RSAR/RBCR + port
//  données), anneau de réception (en-tête de paquet 4 octets {status, next,
//  lenL, lenH}), transmission (TPSR/TBCR + bit TXP), filtrage MAC/broadcast.
//  Références de comportement : datasheet DP8390, ne2000.c de QEMU (lu, non
//  copié), pilotes EtherNEC.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Ne2000.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net/NetBackend.hpp"

namespace {
// Command Register (CR)
// CR_STA (0x02), CR_RD0 (0x08) et CR_RD1 (0x10) existent aussi mais ne sont jamais
// testés ici : l'émulation ne distingue que STP, TXP et l'abandon de DMA distante (RD2).
constexpr uint8_t CR_STP = 0x01, CR_TXP = 0x04;
constexpr uint8_t CR_RD2 = 0x20;  // Remote DMA cmd : abort/complete
// Interrupt Status/Mask (ISR/IMR)
constexpr uint8_t ISR_PRX = 0x01, ISR_PTX = 0x02, ISR_OVW = 0x10, ISR_CNT = 0x20,
                  ISR_RDC = 0x40;
// Transmit Status — [B] DP8390D § 10.3 TSR, p. 24.
constexpr uint8_t TSR_PTX = 0x01;
// Receive Status (registre RSR, et octet d'état de l'en-tête de paquet de l'anneau)
// — [B] DP8390D § 10.3 RSR, p. 26.
constexpr uint8_t RSR_PRX = 0x01, RSR_MPA = 0x10, RSR_PHY = 0x20, RSR_DIS = 0x40;
// Receive Configuration — [B] DP8390D § 10.3 RCR, p. 25.
constexpr uint8_t RCR_AB = 0x04, RCR_AM = 0x08, RCR_PRO = 0x10, RCR_MON = 0x20;
// [B] DP8390D § 10.9 Network Tally Counters, p. 29 : « The maximum count reached by
// any counter is 192 (C0H). »
constexpr uint8_t kTallyMax = 0xC0;
} // namespace

void Ne2000::trace(const char* what, unsigned a, unsigned b) const {
    static const bool on = std::getenv("NEOST_ENEC_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[enec] %s %04x %04x\n", what, a, b);
}

void Ne2000::setMac(const uint8_t mac[6]) {
    std::memcpy(mac_, mac, 6);
    loadProm();
}

// PROM d'adresse station (NE2000) : un octet de PROM 8 bits câblé sur les DEUX
// moitiés du bus de données, d'où chaque octet lu DEUX fois en Remote DMA octet par
// octet. Image de 16 octets : MAC en 0-5, 6-13 à zéro, « WW » ($57 $57) en 14-15
// — la signature que ne.c teste (SA_prom[14] == SA_prom[15] == 0x57) après
// dédoublement. Même disposition que ne2000_reset de QEMU (lu, non copié).
void Ne2000::loadProm() {
    uint8_t prom[kPromSize / 2] = {0};
    std::memcpy(prom, mac_, 6);
    prom[14] = prom[15] = 0x57;
    for (int i = 0; i < kPromSize / 2; ++i)
        mem_[std::size_t(2 * i)] = mem_[std::size_t(2 * i + 1)] = prom[i];
}

void Ne2000::reset() {
    cr_ = CR_STP | CR_RD2;            // stoppée, DMA abort — état post-reset DP8390
    isr_ = 0x80;                     // RST posé
    imr_ = 0; dcr_ = 0; rcr_ = 0; tcr_ = 0;
    pstart_ = kRxBufStart; pstop_ = kRxBufEnd;
    bnry_ = kRxBufStart;
    curr_ = kRxBufStart + 1;
    tpsr_ = 0; tbcr_ = 0; rsar_ = 0; rbcr_ = 0;
    std::memset(mar_, 0, sizeof mar_);
    // [B] DP8390D p. 24 et p. 26 : TSR et RSR « not specified until after the first
    // transmission / reception » — [F] 0 ici, aucune source sur la valeur réelle.
    tsr_ = 0; rsr_ = 0;
    std::memset(cntr_, 0, sizeof cntr_);
    irq_ = false;
    if (mem_.size() != std::size_t(kMemSize)) mem_.assign(kMemSize, 0);
    else std::fill(mem_.begin(), mem_.end(), 0);
    loadProm();                      // la PROM survit au reset (elle est en ROM)
}

void Ne2000::powerCycle() {
    reset();
    if (backend_) backend_->nicReset();
}

void Ne2000::setIsr(uint8_t bits) {
    isr_ |= bits;
    if (isr_ & imr_ & 0x7F) irq_ = true;
}

// -----------------------------------------------------------------------------
//  Décodage de la fenêtre cartouche (EtherNEC)
// -----------------------------------------------------------------------------
// Un accès MOT n'est qu'UN cycle bus /ROM3 ou /ROM4 : la NE2000 voit une seule
// lecture (ou une seule « écriture » encodée dans A1-A8), et la place sur D8-D15.
//   [B] NetUSBee program_spec.txt (archive 11/2007), Network Data Reads : A15=0, la
//       RTL8019 pilote D8-D15 (cité aussi à Bus::read8Slow).
//   [D] MiSTery atarist/atarist_sdram.sv:247 (Till Harbaum, copie de
//       tools/setup_silicon_refs.sh) — sortie EtherNEC câblée
//       « {eth_rd ? eth_data_out : 8'hff, 8'hff} » : octet fort piloté, octet faible
//       à $FF ; ethernec.v ne joue son effet de bord qu'au front de rd/wr.
// L'octet impair d'un mot (`first` faux) rend donc $FF SANS effet de bord. Avant le
// 2026-11-12, chaque octet rejouait l'accès : un move.w au port données consommait
// 2 octets de Remote DMA, un move.w de CR=$26 émettait la trame deux fois. Un accès
// OCTET à une adresse impaire reste un cycle complet (effet de bord), mais sa donnée
// arrive sur D0-D7, que la carte ne pilote pas : $FF.
bool Ne2000::cartRead(uint32_t addr, uint8_t& out, bool first) {
    if (!enabled_) return false;
    const bool rd = addr >= READ_BASE && addr < READ_BASE + 32u * 512u;
    const bool wr = addr >= WRITE_BASE && addr < WRITE_BASE + 32u * 512u;
    if (!rd && !wr) return false;
    out = 0xFF;
    if (!first) return true;                 // 2e moitié d'un mot : même cycle, déjà servi
    if (rd) {
        const uint8_t v = readReg(uint8_t((addr - READ_BASE) / 512));
        if ((addr & 1) == 0) out = v;        // D8-D15 seulement
    } else {
        const uint32_t off = addr - WRITE_BASE;
        writeReg(uint8_t(off / 512), uint8_t((off % 512) / 2));
        // la fausse lecture rend une valeur inutilisée ($FF : rien ne pilote le bus)
    }
    return true;
}

// -----------------------------------------------------------------------------
//  Dispatch registre selon la page (CR bits 6-7)
// -----------------------------------------------------------------------------
uint8_t Ne2000::readReg(uint8_t reg) {
    if (reg == 0x00) return cr_;
    if (reg == 0x10 || reg == 0x11) return remoteDmaReadByte();   // port données
    if (reg == 0x1F) return 0xFF;                                 // reset port (lecture)
    // [B] DP8390D § 10.2, p. 17-18 : PS1/PS0 choisissent la page ; la page 3 est
    // « Reserved » (DP8390D), configuration propre chez la RTL8019AS. Jusqu'au
    // 2026-11-12, les pages 2 et 3 retombaient sur la page 0.
    switch ((cr_ >> 6) & 3) {
    case 0:  return page0Read(reg);
    case 1:  return page1Read(reg);
    case 2:  return page2Read(reg);
    // [F] Page 3 (registres de configuration RTL8019AS : 9346CR, CONFIG0-4…) non
    // modélisée, lue 0. Tranché par : la fiche RTL8019AS (Realtek), registres page 3.
    default: return 0;
    }
}

void Ne2000::writeReg(uint8_t reg, uint8_t v) {
    if (reg == 0x00) {
        cr_ = v;
        if (v & CR_TXP) transmit();
        // Remote Read/Write DMA démarre dès que RD0/RD1 est armé — on lit/écrit
        // ensuite octet par octet via le port données ($10).
        return;
    }
    if (reg == 0x10 || reg == 0x11) { remoteDmaWriteByte(v); return; }
    if (reg == 0x1F) { reset(); return; }                        // reset port (écriture)
    switch ((cr_ >> 6) & 3) {
    case 0: page0Write(reg, v); break;
    case 1: page1Write(reg, v); break;
    // [B] DP8390D § 10.2, p. 18 : en page 2, seuls CLDA0/1, les Remote/Local Next
    // Packet Pointers et l'Address Counter s'écrivent (« diagnostic purposes ») ;
    // $0C-$0F y sont « Reserved ». [F] Ces pointeurs internes ne sont pas modélisés :
    // l'écriture est ignorée — surtout, elle ne touche PLUS la page 0 (CR=$A2 puis
    // reg 3 ← $77 écrivait BNRY). Page 3 : « should never be modified » (DP8390D) ;
    // configuration RTL8019AS non modélisée, écriture ignorée.
    default: break;
    }
}

// --- Page 0 ------------------------------------------------------------------
// [B] DP8390D § 10.2 Register Address Assignments, page 0, p. 17 : en LECTURE, la
// page 0 expose des registres d'état distincts des registres de configuration
// écrits à la même adresse. Le code rendait TBCR0 pour TSR (60 octets → $3C, lu
// COL|ABT|CRS|FU sans PTX) et RCR/TCR/DCR/IMR pour RSR/CNTR0-2.
uint8_t Ne2000::page0Read(uint8_t reg) {
    switch (reg) {
    // [F] CLDA0/1 (Current Local DMA Address, $01/$02) non modélisés : la DMA
    // locale est instantanée ici. Lus 0.
    case 0x03: return bnry_;
    case 0x04: return tsr_;                    // TSR, p. 24
    case 0x05: return 0;                       // NCR : aucune collision (pas de média partagé)
    case 0x06: return 0;                       // [F] FIFO : contenu après bouclage non modélisé
    case 0x07: return isr_;
    case 0x08: return uint8_t(rsar_);          // CRDA0 : adresse COURANTE du Remote DMA
    case 0x09: return uint8_t(rsar_ >> 8);     // CRDA1
    // $0A/$0B : « Reserved » (DP8390D). [E] La RTL8019AS y rendrait ses identifiants
    // « P » ($50) et « p » ($70) ; non retenu sans la fiche Realtek. [F] Lus 0.
    case 0x0C: return uint8_t(rsr_ | ((rcr_ & RCR_MON) ? RSR_DIS : 0));   // RSR, p. 26
    // Tally counters, p. 29 : « These registers will be cleared when read by the CPU. »
    case 0x0D: case 0x0E: case 0x0F: {
        const uint8_t c = cntr_[reg - 0x0D];
        cntr_[reg - 0x0D] = 0;
        return c;
    }
    default:   return 0;
    }
}

// [B] DP8390D § 10.2, page 2, p. 18 : relecture des registres de configuration
// (PSTART, PSTOP, TPSR, RCR, TCR, DCR, IMR), réservée au diagnostic.
uint8_t Ne2000::page2Read(uint8_t reg) {
    switch (reg) {
    case 0x01: return pstart_;
    case 0x02: return pstop_;
    case 0x04: return tpsr_;
    case 0x0C: return rcr_;
    case 0x0D: return tcr_;
    case 0x0E: return dcr_;
    case 0x0F: return imr_;
    // [F] $03/$05 (Remote/Local Next Packet Pointer), $06/$07 (Address Counter) :
    // pointeurs internes non modélisés, lus 0. $08-$0B : « Reserved ».
    default:   return 0;
    }
}

void Ne2000::page0Write(uint8_t reg, uint8_t v) {
    switch (reg) {
    case 0x01: pstart_ = v; break;
    case 0x02: pstop_  = v; break;
    case 0x03: bnry_   = v; break;
    case 0x04: tpsr_   = v; break;
    case 0x05: tbcr_ = (tbcr_ & 0xFF00) | v; break;
    case 0x06: tbcr_ = (tbcr_ & 0x00FF) | (uint16_t(v) << 8); break;
    case 0x07: isr_ &= ~v; if (!(isr_ & imr_ & 0x7F)) irq_ = false; break;  // W1C
    case 0x08: rsar_ = (rsar_ & 0xFF00) | v; break;
    case 0x09: rsar_ = (rsar_ & 0x00FF) | (uint16_t(v) << 8); break;
    case 0x0A: rbcr_ = (rbcr_ & 0xFF00) | v; break;
    case 0x0B: rbcr_ = (rbcr_ & 0x00FF) | (uint16_t(v) << 8); break;
    case 0x0C: rcr_ = v; break;
    case 0x0D: tcr_ = v; break;
    case 0x0E: dcr_ = v; break;
    case 0x0F: imr_ = v; if (isr_ & imr_ & 0x7F) irq_ = true; break;
    default: break;
    }
}

// --- Page 1 ------------------------------------------------------------------
uint8_t Ne2000::page1Read(uint8_t reg) {
    if (reg >= 0x01 && reg <= 0x06) return par_[reg - 1];
    if (reg == 0x07) return curr_;
    if (reg >= 0x08 && reg <= 0x0F) return mar_[reg - 8];
    return 0;
}

void Ne2000::page1Write(uint8_t reg, uint8_t v) {
    if (reg >= 0x01 && reg <= 0x06) { par_[reg - 1] = v; return; }
    if (reg == 0x07) { curr_ = v; return; }
    if (reg >= 0x08 && reg <= 0x0F) { mar_[reg - 8] = v; return; }
}

// -----------------------------------------------------------------------------
//  Remote DMA (port données $10) : lecture/écriture de la RAM tampon NIC.
//  RSAR = adresse courante, RBCR = octets restants. RDC posé quand RBCR atteint 0.
// -----------------------------------------------------------------------------
// [B] DP8390D/NS32490D (National Semiconductor, juillet 1995) § 9.0 Remote DMA, p. 15
//     (DP8390D.pdf, sha256 14476839…) : « If the DMA pointer crosses the Page Stop
//     Register, it is reset to the Page Start Address. This allows the Remote DMA to
//     remove packets that have wrapped around to the top of the Receive Buffer Ring. »
//     Un paquet à cheval sur PSTOP se lit donc d'un seul Remote Read. La fiche ne
//     distingue pas lecture et écriture : le pointeur est le même ([D] QEMU ne2000.c
//     ne2000_dma_update enroule aussi les deux). Rien n'enroule si l'anneau est vide
//     ou inversé (PSTOP ≤ PSTART : registres pas encore programmés).
void Ne2000::advanceRemoteDma() {
    rsar_++;
    if (pstop_ > pstart_ && rsar_ == uint16_t(pstop_ << 8)) rsar_ = uint16_t(pstart_ << 8);
}

uint8_t Ne2000::remoteDmaReadByte() {
    if (rbcr_ == 0) return 0;
    uint8_t v = 0;
    if (rsar_ < mem_.size()) v = mem_[rsar_];
    advanceRemoteDma();
    if (--rbcr_ == 0) setIsr(ISR_RDC);
    return v;
}

void Ne2000::remoteDmaWriteByte(uint8_t v) {
    if (rbcr_ == 0) return;
    // PROM d'adresse : en LECTURE SEULE sur la carte — une écriture Remote DMA y
    // est sans effet (le compteur avance quand même).
    if (rsar_ >= kPromSize && rsar_ < mem_.size()) mem_[rsar_] = v;
    advanceRemoteDma();
    if (--rbcr_ == 0) setIsr(ISR_RDC);
}

// -----------------------------------------------------------------------------
//  Transmission : la trame est dans mem_ à TPSR*256, longueur TBCR.
// -----------------------------------------------------------------------------
void Ne2000::transmit() {
    // [B] DP8390D § 10.3 CR, p. 19 : STP « takes the controller offline, no packets
    // will be received or transmitted » ; « Before the transmit command is given,
    // the STA bit must be set and the STP bit reset. » TXP sous STP n'émet rien.
    if (cr_ & CR_STP) { cr_ &= ~CR_TXP; return; }
    // [B] DP8390D § 10.3 TSR, p. 24 : « It is cleared when the next transmission is
    // initiated by the host. »
    tsr_ = 0;
    const uint32_t addr = uint32_t(tpsr_) * 256u;
    int len = tbcr_;
    if (len < 0 || addr + uint32_t(len) > mem_.size()) { cr_ &= ~CR_TXP; return; }
    if (backend_ && len > 0) backend_->send(&mem_[addr], len);
    trace("tx", addr, unsigned(len));
    cr_ &= ~CR_TXP;                  // TXP retombe : transmission « instantanée »
    tsr_ = TSR_PTX;                  // ni collision ni sous-débit : PTX seul
    setIsr(ISR_PTX);                 // Packet Transmitted
}

// [B] DP8390D § 10.9 MAR, p. 29 : l'adresse destination traverse la logique CRC ;
// les 6 bits de poids fort du CRC, une fois le dernier bit d'adresse entré,
// désignent un bit FB0-FB63 (MAR0 bits 0-7 = FB0-FB7 … MAR7 = FB56-FB63).
// CRC Ethernet (polynôme $04C11DB7, registre à 1, bits de chaque octet pris LSB
// d'abord, sans inversion finale) — [D] même calcul que QEMU ne2000.c
// (compute_mcast_idx / net_crc32) et Linux 8390.c (make_mc_bits / ether_crc).
bool Ne2000::multicastHit(const uint8_t* dst) const {
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < 6; ++i) {
        uint8_t b = dst[i];
        for (int k = 0; k < 8; ++k, b >>= 1) {
            const uint32_t carry = ((crc >> 31) ^ b) & 1u;
            crc <<= 1;
            if (carry) crc ^= 0x04C11DB7u;
        }
    }
    const unsigned idx = crc >> 26;
    return ((mar_[idx >> 3] >> (idx & 7)) & 1u) != 0;
}

// [B] DP8390D § 10.9, p. 29 : CNTR2 compte les paquets perdus faute de tampon (et,
// en mode moniteur, ceux qui passent le filtre d'adresse) ; plafond 192 ($C0).
// [B] § 10.3 ISR, p. 20 : CNT « Set when MSB of one or more of the Network Tally
// Counters has been set ».
void Ne2000::countMissed() {
    if (cntr_[2] < kTallyMax) ++cntr_[2];
    if (cntr_[2] & 0x80) setIsr(ISR_CNT);
}

// -----------------------------------------------------------------------------
//  Réception : trame → anneau, précédée d'un en-tête de 4 octets.
// -----------------------------------------------------------------------------
void Ne2000::deliverFrame(const uint8_t* f, int len) {
    if (len < 14) return;                                // trop court pour Ethernet
    // [B] DP8390D § 10.3 CR, p. 19 : carte stoppée (STP, état de mise sous tension)
    // → « no packets will be received ». Avant le 2026-11-12, l'anneau se remplissait
    // même carte arrêtée.
    if (cr_ & CR_STP) { trace("rx-stopped", unsigned(len), cr_); return; }

    // [B] DP8390D § 10.3 RCR, p. 25 — filtre d'adresse :
    //   · broadcast (destination tout à 1) : accepté si AB ;
    //   · multicast (bit de groupe, hors broadcast) : si AM ET si le hachage CRC
    //     tombe sur un bit posé des MAR (§ 10.9, p. 29) ;
    //   · physique : si PRO, ou égale aux PAR0-5.
    // Le code acceptait tout broadcast et tout multicast, AB/AM ignorés.
    // [F] AR (runts < 64 octets) et SEP ne sont pas appliqués : le backend livre des
    // trames sans FCS ni bourrage, jamais erronées.
    const bool group = (f[0] & 0x01) != 0;
    const bool bcast = f[0] == 0xFF && f[1] == 0xFF && f[2] == 0xFF
                    && f[3] == 0xFF && f[4] == 0xFF && f[5] == 0xFF;
    bool accept;
    if (bcast)      accept = (rcr_ & RCR_AB) != 0;
    else if (group) accept = (rcr_ & RCR_AM) != 0 && multicastHit(f);
    else            accept = (rcr_ & RCR_PRO) != 0 || std::memcmp(f, par_, 6) == 0;
    if (!accept) {
        // Rejet du filtre MAC : tracé — une trame qui disparaît ICI est invisible
        // du pilote ET de l'anneau (chasse CAB/STinG du 2026-08-27).
        trace("rx-macdrop", unsigned(f[0]) << 8 | f[1], unsigned(par_[0]) << 8 | par_[1]);
        return;
    }
    const uint8_t phy = group ? RSR_PHY : 0;
    // [B] DP8390D § 10.3 RCR MON, p. 25 : « checked for address match … but not
    // buffered to memory. The Missed Packet Tally counter will be incremented for
    // each recognized packet. » RSR : MPA, p. 26.
    if (rcr_ & RCR_MON) {
        rsr_ = uint8_t(RSR_MPA | phy);
        countMissed();
        trace("rx-monitor", unsigned(len), rcr_);
        return;
    }

    // Longueur avec en-tête, arrondie à la page (256 octets).
    const int total = len + 4;
    const int pages = (total + 255) / 256;

    // Place d'écriture : page CURR. Vérifie que la trame ne rattrape NI ne
    // franchit BNRY : le DP8390 compare à chaque frontière de page pendant la
    // réception — un test d'égalité seul laissait une trame multi-pages
    // enjamber BNRY et écraser des paquets non lus. Distance libre en ordre
    // d'anneau (CURR == BNRY = anneau vide, comme ne2000.c de QEMU).
    int start = curr_;
    const int ringPages = pstop_ - pstart_;
    if (ringPages <= 0) return;
    int freePages = (bnry_ - start + ringPages) % ringPages;
    if (freePages == 0) freePages = ringPages;
    if (pages >= freePages) {                            // toucherait/franchirait BNRY
        trace("rx-overflow", unsigned(start), unsigned(bnry_));
        setIsr(ISR_OVW);                                 // Overwrite Warning : le pilote est prévenu
        rsr_ = uint8_t(RSR_MPA | phy);                   // [B] p. 26 : « lack of receive buffers »
        countMissed();
        return;
    }
    int next = start + pages;
    if (next >= pstop_) next = pstart_ + (next - pstop_);

    // [B] DP8390D p. 26 : le RSR est recopié en tête de paquet ; PHY dit si
    // l'adresse reconnue était multicast/broadcast.
    rsr_ = uint8_t(RSR_PRX | phy);
    uint8_t hdr[4] = { rsr_, uint8_t(next),
                       uint8_t(total & 0xFF), uint8_t((total >> 8) & 0xFF) };

    // Écriture circulaire dans l'anneau (pages [pstart_, pstop_)).
    uint32_t p = uint32_t(start) * 256u;
    const uint32_t ringLo = uint32_t(pstart_) * 256u;
    const uint32_t ringHi = uint32_t(pstop_) * 256u;
    auto put = [&](uint8_t b) {
        if (p < mem_.size()) mem_[p] = b;
        if (++p >= ringHi) p = ringLo;                   // enroulement
    };
    for (int i = 0; i < 4; ++i) put(hdr[i]);
    for (int i = 0; i < len; ++i) put(f[i]);
    // Le reste de la dernière page reste tel quel (zéro-rempli au reset).

    curr_ = uint8_t(next);
    setIsr(ISR_PRX);                                     // Packet Received
    trace("rx", unsigned(len), curr_);
}

void Ne2000::poll() {
    if (!enabled_ || !backend_) return;
    backend_->poll();
    std::vector<uint8_t> frame;
    int guard = 0;
    while (guard++ < 256 && backend_->recv(frame))
        deliverFrame(frame.data(), int(frame.size()));
}

// -----------------------------------------------------------------------------
//  Save-state (état RUNTIME ; le backend et le contenu réseau sont hors snapshot).
// -----------------------------------------------------------------------------
void Ne2000::serialize(StateArchive& ar) {
    ar(enabled_); ar(irq_);
    ar(cr_); ar(isr_); ar(imr_); ar(dcr_); ar(rcr_); ar(tcr_);
    ar(pstart_); ar(pstop_); ar(bnry_); ar(curr_); ar(tpsr_);
    ar(tbcr_); ar(rsar_); ar(rbcr_);
    ar.arr(par_); ar.arr(mar_);
    ar(tsr_); ar(rsr_); ar.arr(cntr_);       // registres d'état (2026-11-12)
    if (ar.loading() && mem_.size() != std::size_t(kMemSize))
        mem_.assign(kMemSize, 0);
    ar.vec(mem_);
    // Compat : un .state v10 pris SANS EtherNEC (bug d'époque : mem_ n'était allouée
    // qu'au premier reset() de la carte) porte un vecteur de taille 0 — on ré-alloue
    // au lieu de rejeter, la NIC désactivée n'ayant aucun contenu à restaurer.
    if (ar.loading() && mem_.empty()) mem_.assign(kMemSize, 0);
    ar.check(mem_.size() == std::size_t(kMemSize), "Ne2000::mem_ taille inattendue");
    // Bornes de l'anneau : pstart_/pstop_/bnry_/curr_ indexent mem_ par *256 —
    // des valeurs forgées feraient déborder deliverFrame/remoteDma.
    // Pas de garde « pstart_ < pstop_ » : PSTART/PSTOP sont écrits librement par le
    // programme (PSTOP = 0 compris) et chaque usage de l'anneau re-borne déjà par
    // mem_.size() — la garde refusait donc des états légitimes.
}
