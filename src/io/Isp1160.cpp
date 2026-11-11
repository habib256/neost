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
#include "core/Pacing.hpp"

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
    lastCycle_ = clock_ ? clock_() : 0; framePhase_ = 0;
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
    rhport_[0] = rhport_[1] = 0;
    for (unsigned i = 0; i < 2; ++i) { devices_[i].reset(); portResetMs_[i] = 0; }
    itlbuflen_ = 0; atlbuflen_ = 0; bufstat_ = 0;
    atlWr_ = atlRd_ = 0; atlPending_ = false;
    fifoRemaining_ = 0;
    std::fill(atl_.begin(), atl_.end(), uint8_t(0));
    irq_ = false;
}

void Isp1160::updateIrq() {
    // OPR (OHCI interrupt) reflète HcInterruptStatus & Enable (MIE requis).
    if ((intstat_ & intenb_ & 0x7FFFFFFFu) && (intenb_ & HCINT_MIE)) upint_ |= UPINT_OPR;
    else upint_ &= uint16_t(~UPINT_OPR);
    irq_ = (hwcfg_ & HWCFG_INT_ENABLE) && (upint_ & upintenb_);
}

// -----------------------------------------------------------------------------
//  Décodage de la fenêtre cartouche
// -----------------------------------------------------------------------------
bool Isp1160::cartRead(uint32_t addr, uint8_t& out, bool first) {
    if (!enabled_) return false;
    if (first) sync();
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
            if ((cmd_ & 0x7F) == HCATLPORT) {
                if (cmdIsWrite_) {
                    atlWr_ = atlRd_ = 0;
                    atlPending_ = false;
                    std::fill(atl_.begin(), atl_.end(), uint8_t(0));
                } else atlRd_ = 0;
                fifoRemaining_ = std::min<uint16_t>(xferctr_, kBufSize);
            }
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
        if (!fifoRemaining_) return 0;
        uint16_t w = 0;
        if (atlRd_ + 1 < kBufSize) {
            w = uint16_t(atl_[std::size_t(atlRd_)] | (atl_[std::size_t(atlRd_) + 1] << 8));
        }
        atlRd_ += 2;
        finishFifo(false);
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
        if (!fifoRemaining_ || (bufstat_ & BUFSTAT_ATL_FULL)) return;
        if (atlWr_ + 1 < kBufSize) {
            atl_[std::size_t(atlWr_)]     = uint8_t(w & 0xFF);
            atl_[std::size_t(atlWr_) + 1] = uint8_t(w >> 8);
            atlWr_ += 2;
        }
        finishFifo(true);
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

// [B] ISP1160 Rev.05 §10.4.3 et §10.6.7 : EOT au compteur d'octets,
// y compris les transferts impairs (accès physiques par mots de 16 bits).
void Isp1160::finishFifo(bool writing) {
    fifoRemaining_ = fifoRemaining_ > 2 ? uint16_t(fifoRemaining_ - 2) : 0;
    if (fifoRemaining_) return;
    upint_ |= 4;                            // AllEOTInterrupt
    if (writing) { atlPending_ = true; bufstat_ = uint16_t((bufstat_ & ~BUFSTAT_ATL_DONE) | BUFSTAT_ATL_FULL); }
    else { bufstat_ &= uint16_t(~(BUFSTAT_ATL_DONE | BUFSTAT_ATL_FULL)); }
    updateIrq();
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
        break;
    case HCCMDSTAT:
        if (v & HCCMDSTAT_HCR) softReset();    // HCR se remet à 0 de lui-même (« < 1 ms »)
        break;
    case HCINTSTAT: intstat_ &= ~v; updateIrq(); break;            // W1C
    case HCINTENB:  intenb_ |= v;  updateIrq(); break;
    case HCINTDIS:  intenb_ &= ~v; updateIrq(); break;
    case HCFMINTVL: fmintvl_ = v; break;
    // [B] ISP1160 Rev.05 §10.2.2-3 : Remaining/Number sont en lecture seule.
    case HCLSTHRESH: lsthresh_ = v; break;
    case HCRHDESCA:
        rhdesca_ = (v & ~RH_A_NDP_MASK) | 2u;
        if (v & 0x200) for (unsigned i = 0; i < 2; ++i) portPower(i, true); // NPS
        break;
    case HCRHDESCB: rhdescb_ = v; break;
    case HCRHSTATUS:
        // [B] ISP1160 Rev.05 §10.3.3-4 : commandes globales ou par port
        // selon PSM/PPCM ; NPS laisse l'alimentation toujours active.
        if (v & 0x80000000u) rhstatus_ &= ~0x8000u;
        if (v & 0x8000u) rhstatus_ |= 0x8000u;
        for (unsigned i = 0; i < 2; ++i) {
            if ((rhdesca_ & 0x100) && (rhdescb_ & (1u << (17 + i)))) continue;
            if (v & RH_HS_LPS) portPower(i, false);
            if (v & RH_HS_LPSC) portPower(i, true);
        }
        break;
    case HCRHPORT1:
    case HCRHPORT2: {
        uint32_t& p = rhport_[idx - HCRHPORT1];
        const unsigned port = idx - HCRHPORT1;
        p &= ~(v & RH_PS_CHANGE);
        // Écritures = « features » : CCS→ClearPortEnable, PES→SetPortEnable (refusé
        // sans périphérique), PSS→SetPortSuspend, PRS→SetPortReset (achève tout de
        // suite, PRSC posé), PPS→SetPortPower, LSDA→ClearPortPower ; bits 16-20 = W1C.
        if (v & RH_PS_CCS) p &= ~RH_PS_PES;
        // OHCI 7.4.4 : SetPortReset sans périphérique (CCS = 0) est ignoré et pose CSC.
        if (v & RH_PS_PES) { if (p & RH_PS_CCS) p |= RH_PS_PES; else p |= RH_PS_CSC; }
        if (v & RH_PS_PSS) { if (p & RH_PS_PES) p |= RH_PS_PSS; else if (!(p & RH_PS_CCS)) p |= RH_PS_CSC; }
        if ((v & 8) && (p & RH_PS_PSS)) { p = (p & ~RH_PS_PSS) | (1u << 18); }
        if (v & RH_PS_PRS) {
            if (p & RH_PS_CCS) { p = (p | RH_PS_PRS) & ~RH_PS_PES; portResetMs_[port] = 10; }
            else p |= RH_PS_CSC;
        }
        if ((rhdesca_ & 0x100) && (rhdescb_ & (1u << (17 + port)))) {
            if (v & RH_PS_PPS) portPower(port, true);
            if (v & RH_PS_LSDA) portPower(port, false);
        }
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
    case HCATLBUFLEN: atlbuflen_ = uint16_t(std::min<uint32_t>(v, kBufSize)); break;
    default: break;
    }
}

void Isp1160::portPower(unsigned port, bool on) {
    uint32_t& p = rhport_[port];
    on = on || (rhdesca_ & 0x200);           // NoPowerSwitching
    const bool wasConnected = (p & RH_PS_CCS) != 0;
    if (on) {
        p |= RH_PS_PPS;
        if (devices_[port].kind() != UsbDevice::Kind::None) p |= RH_PS_CCS;
    } else {
        p &= ~(RH_PS_PPS | RH_PS_CCS | RH_PS_PES | RH_PS_PSS | RH_PS_PRS);
        portResetMs_[port] = 0; devices_[port].reset();
    }
    if (wasConnected != bool(p & RH_PS_CCS)) { p |= RH_PS_CSC; intstat_ |= HCINT_RHSC; }
    updateIrq();
}

bool Isp1160::attach(unsigned port, UsbDevice::Kind kind, const std::string& image, bool readOnly) {
    if (port >= 2) return false;
    sync();
    if (!devices_[port].attach(kind, image, readOnly)) return false;
    const bool connected = (rhport_[port] & RH_PS_CCS) != 0;
    rhport_[port] &= ~(RH_PS_CCS | RH_PS_PES | RH_PS_PSS | RH_PS_PRS);
    portResetMs_[port] = 0;
    if ((rhport_[port] & RH_PS_PPS) && kind != UsbDevice::Kind::None) rhport_[port] |= RH_PS_CCS;
    if (connected || (rhport_[port] & RH_PS_CCS)) { rhport_[port] |= RH_PS_CSC; intstat_ |= HCINT_RHSC; }
    updateIrq(); return true;
}

void Isp1160::keyEvent(uint8_t usage, bool down) {
    if (enabled_) for (auto& d : devices_) d.keyEvent(usage, down);
}
void Isp1160::mouseEvent(int dx, int dy, bool left, bool right) {
    if (enabled_) for (auto& d : devices_) d.mouseEvent(dx, dy, left, right);
}

// [B] ISP1160 Rev.05 §9.3 : trames de 1 ms. Le polling vidéo donnait
// 50/60/71 trames USB/s. Aucun IRQ ST n'étant câblé, rattrapage aux accès CPU
// et aux HBL ; l'instant du prochain SOF ne dépend pas de la fréquence vidéo.
void Isp1160::sync() {
    if (!clock_) return;
    const int64_t now = clock_();
    if (now < lastCycle_) { lastCycle_ = now; framePhase_ = 0; return; }
    const uint64_t elapsed = uint64_t(now - lastCycle_) * 1000 + framePhase_;
    lastCycle_ = now;
    framePhase_ = uint32_t(elapsed % neost::pacing::kCpuHzInt);
    // Rattrapage borné à une seconde de trames USB : un lastCycle_ très ancien (état
    // forgé, horloge qui saute) enchaînait sinon des milliards de poll().
    uint64_t n = elapsed / neost::pacing::kCpuHzInt;
    if (n > 1000) n = 1000;
    for (; n; --n) poll();
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
    for (unsigned i = 0; i < 2; ++i) {
        devices_[i].tick();
        if (portResetMs_[i] && --portResetMs_[i] == 0) {
            devices_[i].reset();
            rhport_[i] = (rhport_[i] & ~(RH_PS_PRS | RH_PS_PSS)) | RH_PS_PRSC | RH_PS_PES;
            intstat_ |= HCINT_RHSC;
        }
    }
    if (atlPending_) {
        // [B] ISP1160 Rev.05 §9.3-9.4 : en-tête et payload alignés sur
        // QUATRE octets ; IN peut n'avoir que son en-tête écrit (FreeMiNT).
        int off = 0;
        while (off + 8 <= atlWr_) {
            uint16_t w0 = uint16_t(atl_[std::size_t(off)] | (atl_[std::size_t(off) + 1] << 8));
            const uint16_t w1 = uint16_t(atl_[std::size_t(off) + 2] | (atl_[std::size_t(off) + 3] << 8));
            const uint16_t w2 = uint16_t(atl_[std::size_t(off) + 4] | (atl_[std::size_t(off) + 5] << 8));
            const int total = w2 & 0x3FF;
            if (off + 8 + ((total + 3) & ~3) > atlbuflen_) break;
            if (w0 & PTD_ACTIVE) {
                unsigned actual = 0;
                uint16_t cc = PTD_CC_NOTRESP;
                const unsigned address = atl_[std::size_t(off) + 6] & 127;
                const unsigned pid = (w2 >> 10) & 3, ep = w1 >> 12, maxPacket = w1 & 0x3FF;
                for (unsigned port = 0; port < 2; ++port) {
                    if ((rhport_[port] & (RH_PS_CCS | RH_PS_PES | RH_PS_PSS | RH_PS_PRS)) != (RH_PS_CCS | RH_PS_PES) ||
                        devices_[port].address() != address) continue;
                    const auto result = devices_[port].transfer(ep, pid, atl_.data() + off + 8, unsigned(total), actual, maxPacket);
                    if (result == UsbDevice::Result::Nak) cc = 0xF000; // non accédé, pilote replanifie
                    else if (result == UsbDevice::Result::Stall) cc = 0x4000;
                    else cc = (pid == 2 && actual < unsigned(total)) ? 0x9000 : 0;
                    break;
                }
                if (cc == 0xF000) w0 = uint16_t((w0 & ~PTD_CC_MASK) | cc);
                else {
                    unsigned packets = maxPacket ? (actual + maxPacket - 1) / maxPacket : 1;
                    if (pid == 2 && actual < unsigned(total) && maxPacket && actual % maxPacket == 0) ++packets;
                    if (!actual && (cc == 0 || cc == 0x9000)) packets = 1;
                    if ((packets & 1) && (cc == 0 || cc == 0x9000)) w0 ^= 0x400;
                    w0 = uint16_t((w0 & ~(PTD_ACTIVE | PTD_CC_MASK | 0x3FF)) | cc | actual);
                }
            }
            atl_[std::size_t(off)]     = uint8_t(w0 & 0xFF);
            atl_[std::size_t(off) + 1] = uint8_t(w0 >> 8);
            if (w1 & 0x800) break;           // LastPTD
            off += 8 + ((total + 3) & ~3);
        }
        atlPending_ = false;
        atlRd_ = 0;
        bufstat_ |= BUFSTAT_ATL_DONE;
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
    ar(fifoRemaining_); ar.arr(portResetMs_); ar(lastCycle_); ar(framePhase_);
    for (auto& d : devices_) d.serialize(ar);
    ar.check(atl_.size() == std::size_t(kBufSize), "Isp1160::atl_ taille inattendue");
    ar.check(atlWr_ >= 0 && atlWr_ <= kBufSize && atlRd_ >= 0 && atlRd_ <= kBufSize,
             "Isp1160::FIFO ATL hors bornes");
    ar.check(wordIdx_ == 0 || wordIdx_ == 1, "Isp1160::wordIdx_ invalide");
    ar.check(atlbuflen_ <= kBufSize && fifoRemaining_ <= kBufSize &&
             portResetMs_[0] <= 10 && portResetMs_[1] <= 10 &&
             framePhase_ < neost::pacing::kCpuHzInt, "Isp1160::USB state bounds");
}
