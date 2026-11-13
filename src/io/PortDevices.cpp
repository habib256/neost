// PortDevices — périphériques des ports joystick / série / parallèle / bouton.
// Voir l'en-tête pour l'inventaire et les sources (Steem SSE, WinUAE).
#include "io/PortDevices.hpp"
#include "io/Mfp.hpp"
#include "core/StateArchive.hpp"
#include <cstring>

namespace {
struct Entry { const char* id; const char* label; PortDevices::Port port; };
using P = PortDevices::Port;
// Indexé par PortDevices::Device ; `port` = celui que le logiciel sonde.
constexpr Entry kDev[] = {
    { "none",        "None",                                    P::Joy0 },
    { "leaderboard", "Leader Board key",                        P::Joy1 },
    { "10thframe",   "10th Frame key",                          P::Joy1 },
    { "cricket",     "Cricket Captain key",                     P::Joy0 },
    { "rugby",       "Rugby Coach key",                         P::Joy1 },
    { "soccer",      "Multi Player Soccer Manager key",         P::Joy0 },
    { "bat2",        "B.A.T. II key",                           P::Rs232 },
    { "musicmaster", "Music Master key",                        P::Rs232 },
    { "jeannedarc",  "Jeanne d'Arc key",                        P::Rs232 },
    { "prosound",    "Pro Sound Designer 8-bit DAC",            P::Printer },
    { "multiface",   "Multiface ST (freeze button)",            P::CartButton },
    { "urc",         "Ultimate Ripper (button)",                P::CartButton },
};
static_assert(sizeof(kDev) / sizeof(kDev[0]) == size_t(PortDevices::Device::Count), "kDev must follow Device");
constexpr const char* kPortId[] = { "joy0", "joy1", "rs232", "printer", "cartbutton" };
static_assert(sizeof(kPortId) / sizeof(kPortId[0]) == size_t(P::Count), "kPortId must follow Port");
size_t di(PortDevices::Device d) { return size_t(d) < size_t(PortDevices::Device::Count) ? size_t(d) : 0; }
}

const char* PortDevices::portId(Port p) { return kPortId[size_t(p) < size_t(Port::Count) ? size_t(p) : 0]; }
PortDevices::Port PortDevices::portFromId(const char* s, bool* ok) {
    if (s) for (size_t i = 0; i < size_t(Port::Count); ++i)
        if (!std::strcmp(kPortId[i], s)) { if (ok) *ok = true; return Port(i); }
    if (ok) *ok = false;
    return Port::Joy0;
}
const char* PortDevices::id(Device d)    { return kDev[di(d)].id; }
const char* PortDevices::label(Device d) { return kDev[di(d)].label; }
PortDevices::Port PortDevices::defaultPort(Device d) { return kDev[di(d)].port; }
PortDevices::Device PortDevices::fromId(const char* s) {
    if (!s) return Device::None;
    for (size_t i = 0; i < size_t(Device::Count); ++i)
        if (!std::strcmp(kDev[i].id, s)) return Device(i);
    return Device::None;
}
bool PortDevices::fits(Port p, Device d) {
    if (d == Device::None) return true;
    // Un seul port par périphérique : celui que sonde son logiciel. Les deux ports
    // joystick ont pourtant le même connecteur DE-9, et une clé joystick entrait dans
    // l'un comme dans l'autre — mais le jeu n'en lit qu'un, et sur l'autre elle ne sert
    // à rien. Décision de l'auteur (2026-11-13) : ne plus la proposer ni la brancher
    // ailleurs (menus GUI/web/Android, --plug, dongles.txt, neost.cfg).
    return p == defaultPort(d);
}

bool PortDevices::plug(Port p, Device d) {
    if (!fits(p, d)) return false;
    dev_[size_t(p)] = d;
    reset();
    return true;
}
void PortDevices::unplugAll() { for (auto& d : dev_) d = Device::None; reset(); }
bool PortDevices::any() const { for (auto d : dev_) if (d != Device::None) return true; return false; }
void PortDevices::reset() { serial_ = 0; timing_ = 0; osc_[0] = osc_[1] = 0; pressed_ = false; }

// --- Port série ---------------------------------------------------------------
void PortDevices::onPortA(uint8_t a, int64_t now, Mfp& mfp) {
    switch (at(Port::Rs232)) {
    case Device::MusicMaster:
        // Steem stports.cpp TSTPort::SetDTR : « old - new » + date de l'écriture.
        serial_ = uint16_t(((serial_ << 1) | ((a & 0x10) ? 1u : 0u)) & 3u);
        timing_ = now;
        break;
    case Device::JeanneDArc: {
        // Steem iow.cpp : DCD (GPIP1) = !(New && New < Old) sur les bits RTS|DTR.
        const uint8_t oldv = uint8_t(serial_ & 0xFF);
        const uint8_t newv = uint8_t(a & (0x10 | 0x08));
        serial_ = newv;
        mfp.setRs232Dcd(newv && newv < oldv);   // niveau 0 ↔ ligne assertée (actif bas)
        break;
    }
    default: break;
    }
}

void PortDevices::gpipRead(uint8_t& v, int64_t now) const {
    switch (at(Port::Rs232)) {
    case Device::Bat2:
        v &= uint8_t(~0x04);                         // CTS (GPIP2) à 0 en permanence
        break;
    case Device::MusicMaster: {
        // DTR → DCD avec ~200 cycles de retard (Steem ior.cpp, « inspired by WinUAE »).
        const bool bit = (now - timing_ > 200) ? (serial_ & 1) != 0 : (serial_ & 2) != 0;
        if (bit) v |= 0x02; else v &= uint8_t(~0x02);
        break;
    }
    default: break;
    }
}

// --- Ports joystick -----------------------------------------------------------
void PortDevices::joyOverlay(Device d, uint8_t& joy, uint8_t& osc) {
    switch (d) {
    case Device::LeaderBoard:
    case Device::TenthFrame:
        joy |= 0x03;                                 // haut ET bas : impossible au joystick
        break;
    case Device::Cricket:
    case Device::Soccer:
    case Device::Rugby:
        osc = (osc == 0xC) ? 0xD : 0xC;              // oscillateur %1100 ↔ %1101 (Steem ikbd.cpp)
        joy |= osc;
        break;
    default: break;
    }
}
void PortDevices::onJoystick(uint8_t& joy0, uint8_t& joy1) {
    joyOverlay(at(Port::Joy0), joy0, osc_[0]);
    joyOverlay(at(Port::Joy1), joy1, osc_[1]);
}

// --- Bouton de cartouche ------------------------------------------------------
void PortDevices::pressButton(Mfp& mfp) {
    const Device d = at(Port::CartButton);
    if (d == Device::None) return;
    pressed_ = true;
    if (d == Device::Multiface) mfp.setMonitorButton(true);   // GPIP7 → 0
    else                        mfp.setRs232Ri(true);         // GPIP6 : RI assertée (bit6 → 0, hors repos)
}
void PortDevices::onVbl(Mfp& mfp) {
    if (!pressed_) return;
    pressed_ = false;
    const Device d = at(Port::CartButton);
    if (d == Device::Multiface) mfp.setMonitorButton(false);
    else if (d == Device::Urc)  mfp.setRs232Ri(false);         // retour au repos (désassertée, bit6 = 1)
}

void PortDevices::serialize(StateArchive& ar) {
    // Les périphériques branchés (config) voyagent avec l'état volatil, mais l'en-tête
    // les a déjà comparés (Machine::sessionConfig, octets 1-5) : au chargement, ils
    // doivent être ceux de la session. Un écart (flux forgé) est refusé au lieu
    // d'écraser la config — comme la clé cartouche (CartridgeKey::serialize).
    for (auto& d : dev_) {
        uint8_t b = uint8_t(d); ar(b);
        if (ar.loading()) ar.check(b == uint8_t(d), "PortDevices : peripherique different de la session");
    }
    ar(serial_); ar(timing_); ar(osc_[0]); ar(osc_[1]); ar(pressed_);
}
