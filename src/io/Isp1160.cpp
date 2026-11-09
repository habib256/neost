// =============================================================================
//  Isp1160.cpp — Modèle ISP1160 du NetUSBee (cf. Isp1160.hpp).
//
//  Références : datasheet ISP1160 (protocole commande/données, registres $20-$41),
//  OHCI 1.0a (registres < $20), isp116x-hcd (FreeMiNT/Linux, lus, non copiés).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <algorithm>
#include "io/Isp1160.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
// HcCommandStatus
constexpr uint32_t HCCMDSTAT_HCR = 1u << 0;
// HcControl : HCFS (bits 6-7)
constexpr uint32_t HCCONTROL_HCFS = 3u << 6, HCCONTROL_USB_OPER = 2u << 6;
// HcInterruptStatus/Enable
constexpr uint32_t HCINT_SF = 1u << 2, HCINT_RHSC = 1u << 6, HCINT_MIE = 1u << 31;
// HcuPInterrupt
constexpr uint16_t UPINT_SOF = 1 << 0, UPINT_ATL = 1 << 1, UPINT_OPR = 1 << 4, UPINT_CLKRDY = 1 << 6;
// HcHardwareConfiguration
constexpr uint16_t HWCFG_INT_ENABLE = 1 << 0;
// HcBufferStatus
constexpr uint16_t BUFSTAT_ATL_FULL = 1 << 2, BUFSTAT_ATL_DONE = 1 << 5;
// HcRhPortStatus : bits d'état et de changement (W1C)
constexpr uint32_t RH_PS_CCS = 1u << 0, RH_PS_PES = 1u << 1, RH_PS_PSS = 1u << 2,
    RH_PS_PRS = 1u << 4, RH_PS_PPS = 1u << 8, RH_PS_CHANGE = 0x1Fu << 16;
constexpr uint32_t RH_PS_LSDA = 1u << 9;            // écrire : ClearPortPower
constexpr uint32_t RH_PS_CSC = 1u << 16, RH_PS_PRSC = 1u << 20;
// HcRhDescriptorA : NDP (bits 0-7) = 2 ports, figé
constexpr uint32_t RH_A_NDP_MASK = 0xFFu;
// HcRhStatus
constexpr uint32_t RH_HS_LPS = 1u << 0, RH_HS_LPSC = 1u << 16;
// HcSoftwareReset
constexpr uint16_t HCSWRES_MAGIC = 0x00F6;
// PTD : mot 0 = ActualBytes[9:0] | Toggle<<10 | Active<<11 | CC<<12
constexpr uint16_t PTD_ACTIVE = 1 << 11, PTD_CC_MASK = 0xF << 12, PTD_CC_NOTRESP = 5 << 12;
} // namespace

void Isp1160::trace(const char* what, unsigned a, unsigned b) const {
    static const bool on = std::getenv("NEOST_USB_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[isp1160] %s %04x %08x\n", what, a, b);
}

void Isp1160::reset() {
    latch_ = 0; cmd_ = 0; cmdIsWrite_ = false; wordIdx_ = 0; readLo_ = 0; wdata_ = 0;
    hwcfg_ = 0; dmacfg_ = 0; xferctr_ = 0; upintenb_ = 0; scratch_ = 0;
    upint_ = UPINT_CLKRDY;                   // horloge interne prête (le pilote l'attend)
    softReset();
}

// Reset logiciel (HcSoftwareReset $F6 / HcCommandStatus.HCR) : registres OHCI au
// repos, hub racine vide, FIFO purgées. hwcfg/scratch survivent (registres ISP).
void Isp1160::softReset() {
    control_ = 0; cmdstat_ = 0; intstat_ = 0; intenb_ = 0;
    fmintvl_ = 0x2EDF; fmrem_ = 0; fmnum_ = 0; lsthresh_ = 0x0628;
    rhdesca_ = 0x02; rhdescb_ = 0; rhstatus_ = 0;
    rhport_[0] = rhport_[1] = 0;             // CCS = 0 : rien de branché
    itlbuflen_ = 0; atlbuflen_ = 0; bufstat_ = 0;
    atlWr_ = atlRd_ = 0; atlPending_ = false;
    std::fill(atl_.begin(), atl_.end(), uint8_t(0));
    irq_ = false;
}

void Isp1160::updateIrq() {
    // OPR (OHCI interrupt) reflète HcInterruptStatus & Enable (MIE requis).
    if ((intstat_ & intenb_ & 0x7FFFFFFFu) && (intenb_ & HCINT_MIE)) upint_ |= UPINT_OPR;
    irq_ = (hwcfg_ & HWCFG_INT_ENABLE) && (upint_ & upintenb_);
}

// -----------------------------------------------------------------------------
//  Décodage de la fenêtre cartouche
// -----------------------------------------------------------------------------
bool Isp1160::cartRead(uint32_t addr, uint8_t& out, bool first) {
    if (!enabled_) return false;
    if (addr >= LSB_WRITE && addr < LSB_WRITE + 512u) {
        if (first) latch_ = uint8_t((addr - LSB_WRITE) >> 1);
        out = 0xFF;
        return true;
    }
    if (addr >= MSB_DATA_WRITE && addr < MSB_DATA_WRITE + 512u) {
        if (first) dataPortWrite(uint16_t((uint16_t((addr - MSB_DATA_WRITE) >> 1) << 8) | latch_));
        out = 0xFF;
        return true;
    }
    if (addr >= MSB_CMD_WRITE && addr < MSB_CMD_WRITE + 2u) {
        if (first) {
            cmd_ = latch_;
            cmdIsWrite_ = (cmd_ & 0x80) != 0;
            wordIdx_ = 0;
            trace(cmdIsWrite_ ? "cmd-wr" : "cmd-rd", cmd_ & 0x7F, 0);
        }
        out = 0xFF;
        return true;
    }
    if (addr >= DATA_READ && addr < DATA_READ + 2u) {
        if (first) {
            const uint16_t w = dataPortRead();
            readLo_ = w;
            out = uint8_t(w >> 8);           // D15-8 sur l'octet pair (bus 16 bits)
        } else {
            out = uint8_t(readLo_ & 0xFF);
        }
        return true;
    }
    return false;
}

// Port données en lecture : registre 16 bits = 1 mot ; 32 bits = bas puis haut.
uint16_t Isp1160::dataPortRead() {
    const uint8_t idx = cmd_ & 0x7F;
    if (idx == HCATLPORT || idx == HCITLPORT) {
        if (idx == HCITLPORT) return 0;
        uint16_t w = 0;
        if (atlRd_ + 1 < kBufSize && atlRd_ + 1 < atlWr_) {
            w = uint16_t(atl_[std::size_t(atlRd_)] | (atl_[std::size_t(atlRd_) + 1] << 8));
        }
        atlRd_ += 2;
        if (atlRd_ >= atlWr_) {              // liste relue en entier
            bufstat_ &= uint16_t(~BUFSTAT_ATL_DONE);
            atlRd_ = atlWr_ = 0;
        }
        return w;
    }
    const uint32_t v = readReg(idx, is32(idx));
    uint16_t w;
    if (!is32(idx)) { w = uint16_t(v); }
    else if (wordIdx_ == 0) { w = uint16_t(v & 0xFFFF); wordIdx_ = 1; }
    else { w = uint16_t(v >> 16); wordIdx_ = 0; }
    trace("rd", idx, v);
    return w;
}

void Isp1160::dataPortWrite(uint16_t w) {
    const uint8_t idx = cmd_ & 0x7F;
    if (!cmdIsWrite_) return;
    if (idx == HCATLPORT) {
        if (atlWr_ + 1 < kBufSize) {
            atl_[std::size_t(atlWr_)]     = uint8_t(w & 0xFF);
            atl_[std::size_t(atlWr_) + 1] = uint8_t(w >> 8);
            atlWr_ += 2;
        }
        atlPending_ = true;
        bufstat_ |= BUFSTAT_ATL_FULL;
        return;
    }
    if (idx == HCITLPORT) return;             // ISO non géré : octets ignorés
    if (!is32(idx)) { writeReg(idx, w, false); trace("wr", idx, w); return; }
    if (wordIdx_ == 0) { wdata_ = w; wordIdx_ = 1; return; }
    wordIdx_ = 0;
    const uint32_t v = (uint32_t(w) << 16) | wdata_;
    writeReg(idx, v, true);
    trace("wr", idx, v);
}

// -----------------------------------------------------------------------------
//  Registres
// -----------------------------------------------------------------------------
uint32_t Isp1160::readReg(uint8_t idx, bool /*is32*/) {
    switch (idx) {
    case HCREVISION:  return 0x10;                 // OHCI 1.0
    case HCCONTROL:   return control_;
    case HCCMDSTAT:   return cmdstat_;
    case HCINTSTAT:   return intstat_;
    case HCINTENB:
    case HCINTDIS:    return intenb_;
    case HCFMINTVL:   return fmintvl_;
    case HCFMREM:     return fmrem_;
    case HCFMNUM:     return fmnum_;
    case HCLSTHRESH:  return lsthresh_;
    case HCRHDESCA:   return (rhdesca_ & ~RH_A_NDP_MASK) | 2u;   // 2 ports, toujours
    case HCRHDESCB:   return rhdescb_;
    case HCRHSTATUS:  return rhstatus_;
    case HCRHPORT1:   return rhport_[0];
    case HCRHPORT2:   return rhport_[1];
    case HCHWCFG:     return hwcfg_;
    case HCDMACFG:    return dmacfg_;
    case HCXFERCTR:   return xferctr_;
    case HCuPINT:     return upint_;
    case HCuPINTENB:  return upintenb_;
    case HCCHIPID:    return CHIP_ID;
    case HCSCRATCH:   return scratch_;
    case HCITLBUFLEN: return itlbuflen_;
    case HCATLBUFLEN: return atlbuflen_;
    case HCBUFSTAT:   return bufstat_;
    case HCRDITL0LEN:
    case HCRDITL1LEN: return 0;
    default:          return 0;
    }
}

void Isp1160::writeReg(uint8_t idx, uint32_t v, bool /*is32*/) {
    switch (idx) {
    case HCCONTROL:
        control_ = v;
        if ((v & HCCONTROL_HCFS) == HCCONTROL_USB_OPER) fmnum_ = 0;
        break;
    case HCCMDSTAT:
        if (v & HCCMDSTAT_HCR) softReset();    // HCR se remet à 0 de lui-même (« < 1 ms »)
        break;
    case HCINTSTAT: intstat_ &= ~v; updateIrq(); break;            // W1C
    case HCINTENB:  intenb_ |= v;  updateIrq(); break;
    case HCINTDIS:  intenb_ &= ~v; updateIrq(); break;
    case HCFMINTVL: fmintvl_ = v; break;
    case HCFMREM:   fmrem_ = v; break;
    case HCFMNUM:   fmnum_ = v; break;
    case HCLSTHRESH: lsthresh_ = v; break;
    case HCRHDESCA: rhdesca_ = (v & ~RH_A_NDP_MASK) | 2u; break;
    case HCRHDESCB: rhdescb_ = v; break;
    case HCRHSTATUS:
        // SetGlobalPower/ClearGlobalPower (bits 16/0) : sans port alimenté, rien.
        if (v & RH_HS_LPS) rhstatus_ &= ~RH_HS_LPSC;
        break;
    case HCRHPORT1:
    case HCRHPORT2: {
        uint32_t& p = rhport_[idx - HCRHPORT1];
        // Écritures = « features » : CCS→ClearPortEnable, PES→SetPortEnable (refusé
        // sans périphérique), PSS→SetPortSuspend, PRS→SetPortReset (achève tout de
        // suite, PRSC posé), PPS→SetPortPower, LSDA→ClearPortPower ; bits 16-20 = W1C.
        if (v & RH_PS_CCS) p &= ~RH_PS_PES;
        // OHCI 7.4.4 : SetPortReset sans périphérique (CCS = 0) est ignoré et pose CSC.
        if (v & RH_PS_PRS) p |= (p & RH_PS_CCS) ? RH_PS_PRSC : RH_PS_CSC;
        if (v & RH_PS_PPS) p |= RH_PS_PPS;
        if (v & RH_PS_LSDA) p &= ~RH_PS_PPS;
        p &= ~(v & RH_PS_CHANGE);
        (void)RH_PS_PSS;
        if (p & RH_PS_CHANGE) { intstat_ |= HCINT_RHSC; updateIrq(); }
        break;
    }
    case HCHWCFG:     hwcfg_ = uint16_t(v); updateIrq(); break;
    case HCDMACFG:    dmacfg_ = uint16_t(v); break;
    case HCXFERCTR:   xferctr_ = uint16_t(v); break;
    case HCuPINT:
        upint_ &= uint16_t(~uint16_t(v));                       // W1C
        upint_ |= UPINT_CLKRDY;                                 // l'horloge reste prête
        if (!(intstat_ & intenb_ & 0x7FFFFFFFu)) upint_ &= uint16_t(~UPINT_OPR);
        updateIrq();
        break;
    case HCuPINTENB:  upintenb_ = uint16_t(v); updateIrq(); break;
    case HCSCRATCH:   scratch_ = uint16_t(v); break;
    case HCSWRES:     if (uint16_t(v) == HCSWRES_MAGIC) softReset(); break;
    case HCITLBUFLEN: itlbuflen_ = uint16_t(v); break;
    case HCATLBUFLEN: atlbuflen_ = uint16_t(v); break;
    default: break;
    }
}

// -----------------------------------------------------------------------------
//  Trame : le contrôleur « exécute » la liste ATL — hub vide ⇒ chaque PTD se
//  termine DeviceNotResponding (CC=5), Active=0 ; ATL_DONE + IRQ ATL/SOF.
// -----------------------------------------------------------------------------
void Isp1160::poll() {
    if (!enabled_) return;
    if ((control_ & HCCONTROL_HCFS) != HCCONTROL_USB_OPER) return;
    fmnum_ = (fmnum_ + 1) & 0xFFFF;
    intstat_ |= HCINT_SF;
    upint_ |= UPINT_SOF;
    if (atlPending_) {
        // Parcourt les PTD (en-tête 8 octets + charge utile arrondie au mot).
        int off = 0;
        while (off + 8 <= atlWr_) {
            uint16_t w0 = uint16_t(atl_[std::size_t(off)] | (atl_[std::size_t(off) + 1] << 8));
            const uint16_t w2 = uint16_t(atl_[std::size_t(off) + 4] | (atl_[std::size_t(off) + 5] << 8));
            const int total = w2 & 0x3FF;
            w0 = uint16_t((w0 & ~(PTD_ACTIVE | PTD_CC_MASK | 0x3FF)) | PTD_CC_NOTRESP);
            atl_[std::size_t(off)]     = uint8_t(w0 & 0xFF);
            atl_[std::size_t(off) + 1] = uint8_t(w0 >> 8);
            off += 8 + ((total + 1) & ~1);
        }
        atlPending_ = false;
        atlRd_ = 0;
        bufstat_ = uint16_t((bufstat_ & ~BUFSTAT_ATL_FULL) | BUFSTAT_ATL_DONE);
        upint_ |= UPINT_ATL;
        trace("atl-done", unsigned(atlWr_), 0);
    }
    updateIrq();
}

// -----------------------------------------------------------------------------
//  Save-state
// -----------------------------------------------------------------------------
void Isp1160::serialize(StateArchive& ar) {
    ar(enabled_); ar(irq_);
    ar(latch_); ar(cmd_); ar(cmdIsWrite_); ar(wordIdx_); ar(readLo_); ar(wdata_);
    ar(control_); ar(cmdstat_); ar(intstat_); ar(intenb_);
    ar(fmintvl_); ar(fmrem_); ar(fmnum_); ar(lsthresh_);
    ar(rhdesca_); ar(rhdescb_); ar(rhstatus_); ar.arr(rhport_);
    ar(hwcfg_); ar(dmacfg_); ar(xferctr_); ar(upint_); ar(upintenb_);
    ar(scratch_); ar(itlbuflen_); ar(atlbuflen_); ar(bufstat_);
    if (ar.loading() && atl_.size() != std::size_t(kBufSize)) atl_.assign(kBufSize, 0);
    ar.vec(atl_);
    ar(atlWr_); ar(atlRd_); ar(atlPending_);
    ar.check(atl_.size() == std::size_t(kBufSize), "Isp1160::atl_ taille inattendue");
    ar.check(atlWr_ >= 0 && atlWr_ <= kBufSize && atlRd_ >= 0 && atlRd_ <= kBufSize,
             "Isp1160::FIFO ATL hors bornes");
    ar.check(wordIdx_ == 0 || wordIdx_ == 1, "Isp1160::wordIdx_ invalide");
}
