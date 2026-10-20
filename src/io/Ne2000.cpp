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
constexpr uint8_t ISR_PRX = 0x01, ISR_PTX = 0x02, ISR_OVW = 0x10, ISR_RDC = 0x40;
// Receive Status (posé dans l'en-tête de paquet de l'anneau)
constexpr uint8_t RSR_PRX = 0x01;
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
    irq_ = false;
    if (mem_.size() != std::size_t(kMemSize)) mem_.assign(kMemSize, 0);
    else std::fill(mem_.begin(), mem_.end(), 0);
    loadProm();                      // la PROM survit au reset (elle est en ROM)
}

void Ne2000::setIsr(uint8_t bits) {
    isr_ |= bits;
    if (isr_ & imr_ & 0x7F) irq_ = true;
}

// -----------------------------------------------------------------------------
//  Décodage de la fenêtre cartouche (EtherNEC)
// -----------------------------------------------------------------------------
bool Ne2000::cartRead(uint32_t addr, uint8_t& out) {
    if (!enabled_) return false;
    if (addr >= READ_BASE && addr < READ_BASE + 32u * 512u) {
        const uint8_t reg = uint8_t((addr - READ_BASE) / 512);
        out = readReg(reg);
        return true;
    }
    if (addr >= WRITE_BASE && addr < WRITE_BASE + 32u * 512u) {
        const uint32_t off = addr - WRITE_BASE;
        const uint8_t reg  = uint8_t(off / 512);
        const uint8_t data = uint8_t((off % 512) / 2);
        writeReg(reg, data);
        out = 0xFF;                  // la fausse lecture rend une valeur inutilisée
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
//  Dispatch registre selon la page (CR bits 6-7)
// -----------------------------------------------------------------------------
uint8_t Ne2000::readReg(uint8_t reg) {
    if (reg == 0x00) return cr_;
    if (reg == 0x10 || reg == 0x11) return remoteDmaReadByte();   // port données
    if (reg == 0x1F) return 0xFF;                                 // reset port (lecture)
    const uint8_t page = (cr_ >> 6) & 3;
    return page == 1 ? page1Read(reg) : page0Read(reg);
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
    const uint8_t page = (cr_ >> 6) & 3;
    if (page == 1) page1Write(reg, v); else page0Write(reg, v);
}

// --- Page 0 ------------------------------------------------------------------
uint8_t Ne2000::page0Read(uint8_t reg) {
    switch (reg) {
    case 0x03: return bnry_;
    case 0x04: return uint8_t(tbcr_);          // en lecture : status TX (approx.)
    case 0x07: return isr_;
    case 0x08: return uint8_t(rsar_);
    case 0x09: return uint8_t(rsar_ >> 8);
    case 0x0A: return uint8_t(rbcr_);
    case 0x0B: return uint8_t(rbcr_ >> 8);
    case 0x0C: return rcr_;
    case 0x0D: return tcr_;
    case 0x0E: return dcr_;
    case 0x0F: return imr_;
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
uint8_t Ne2000::remoteDmaReadByte() {
    if (rbcr_ == 0) return 0;
    uint8_t v = 0;
    if (rsar_ < mem_.size()) v = mem_[rsar_];
    rsar_++;
    if (--rbcr_ == 0) setIsr(ISR_RDC);
    return v;
}

void Ne2000::remoteDmaWriteByte(uint8_t v) {
    if (rbcr_ == 0) return;
    // PROM d'adresse : en LECTURE SEULE sur la carte — une écriture Remote DMA y
    // est sans effet (le compteur avance quand même).
    if (rsar_ >= kPromSize && rsar_ < mem_.size()) mem_[rsar_] = v;
    rsar_++;
    if (--rbcr_ == 0) setIsr(ISR_RDC);
}

// -----------------------------------------------------------------------------
//  Transmission : la trame est dans mem_ à TPSR*256, longueur TBCR.
// -----------------------------------------------------------------------------
void Ne2000::transmit() {
    const uint32_t addr = uint32_t(tpsr_) * 256u;
    int len = tbcr_;
    if (len < 0 || addr + uint32_t(len) > mem_.size()) { cr_ &= ~CR_TXP; return; }
    if (backend_ && len > 0) backend_->send(&mem_[addr], len);
    trace("tx", addr, unsigned(len));
    cr_ &= ~CR_TXP;                  // TXP retombe : transmission « instantanée »
    setIsr(ISR_PTX);                 // Packet Transmitted
}

// -----------------------------------------------------------------------------
//  Réception : trame → anneau, précédée d'un en-tête de 4 octets.
// -----------------------------------------------------------------------------
void Ne2000::deliverFrame(const uint8_t* f, int len) {
    if (len < 14) return;                                // trop court pour Ethernet

    // Filtrage MAC : accepté si broadcast, ou destiné à notre MAC, ou mode
    // promiscuous (RCR bit4). (Multicast simplifié : accepté si bit0 du 1er octet.)
    const bool bcast = f[0] == 0xFF && f[1] == 0xFF && f[2] == 0xFF
                    && f[3] == 0xFF && f[4] == 0xFF && f[5] == 0xFF;
    const bool tome  = std::memcmp(f, par_, 6) == 0;
    const bool promisc = (rcr_ & 0x10) != 0;
    const bool mcast = (f[0] & 0x01) != 0;
    if (!bcast && !tome && !promisc && !mcast) {
        // Rejet du filtre MAC : tracé — une trame qui disparaît ICI est invisible
        // du pilote ET de l'anneau (chasse CAB/STinG du 2026-08-27).
        trace("rx-macdrop", unsigned(f[0]) << 8 | f[1], unsigned(par_[0]) << 8 | par_[1]);
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
        return;
    }
    int next = start + pages;
    if (next >= pstop_) next = pstart_ + (next - pstop_);

    uint8_t hdr[4] = { RSR_PRX, uint8_t(next),
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
    ar.check(pstop_ <= (kMemSize / 256) && pstart_ < pstop_,
             "Ne2000::anneau hors bornes");
}
