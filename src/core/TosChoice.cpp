// =============================================================================
//  TosChoice.cpp — cf. TosChoice.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/TosChoice.hpp"

#include <fstream>
#include <system_error>
#include <filesystem>

namespace neost {

bool readTosInfo(const std::string& path, TosInfo& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t h[0x30] = {};
    f.read(reinterpret_cast<char*>(h), sizeof h);
    const std::size_t got = static_cast<std::size_t>(f.gcount());
    if (got < 0x1E) return false;
    std::error_code ec;
    const auto sz = std::filesystem::file_size(path, ec);
    out.ver = tosHeaderVersion(h, got);
    out.emuTos = tosHeaderIsEmuTos(h, got);
    // [C] OSHEADER os_conf (mot à $1C) : bit 0 = PAL, bits 1-15 = pays (TOS 1.02+,
    // EmuTOS) ; relevé sur les images de roms/ (tos102uk $0007, tos102us $0000,
    // tos104de $0003, tos206.img $0011 = suisse). Même lecture que resolveQuartz.
    const uint16_t conf = uint16_t((h[0x1C] << 8) | h[0x1D]);
    out.ntsc = (conf & 1) == 0;
    out.country = conf >> 1;
    out.size = ec ? 0 : static_cast<std::size_t>(sz);
    return true;
}

bool tosRunsMachine(const TosInfo& t, MachineType m) {
    if (tosUnsupportedMachine(t.ver, t.emuTos, t.size)) return false;   // TT, Falcon
    // EmuTOS de 512 Ko et plus : sert toutes les machines (cf. tosUnsupportedMachine).
    if (t.emuTos && t.size >= 512u * 1024u) return true;
    switch (m) {
    case MachineType::St:
    case MachineType::MegaSt:
        // TOS 1.00-1.04, EmuTOS 192 Ko (se présente en 1.04).
        return t.ver <= 0x0104;
    case MachineType::Ste:
        // 1.06/1.62 (ROM STE), 2.05/2.06, EmuTOS 256 Ko (2.06).
        return t.ver >= 0x0106 && t.ver <= 0x0206;
    case MachineType::MegaSte:
        // Seul un TOS ≥ 2.00 programme cache, SCU et 16 MHz (cf. adjustMachineForTos).
        return t.ver >= 0x0200 && t.ver <= 0x0206;
    }
    return false;
}

namespace {
// Rang du TOS d'origine de la machine : plus petit = préféré.
int versionRank(MachineType m, uint16_t ver) {
    static const uint16_t kSt[]      = { 0x0104, 0x0102, 0x0100 };
    static const uint16_t kSte[]     = { 0x0162, 0x0106, 0x0206, 0x0205 };
    static const uint16_t kMegaSte[] = { 0x0206, 0x0205 };
    const uint16_t* t = kSt; int n = 3;
    if (m == MachineType::Ste)          { t = kSte;     n = 4; }
    else if (m == MachineType::MegaSte) { t = kMegaSte; n = 2; }
    for (int i = 0; i < n; ++i) if (t[i] == ver) return i;
    return n;
}
}  // namespace

int pickTosForMachine(MachineType m, const TosInfo* cur, const std::vector<TosInfo>& cands) {
    int best = -1;
    long bestScore = 0;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        const TosInfo& c = cands[i];
        if (!tosRunsMachine(c, m)) continue;
        // Score lexicographique : famille, puis pays/fréquence, puis version d'origine.
        long s = 0;
        if (cur && c.emuTos != cur->emuTos) s += 1000;
        if (cur) {
            if (c.country != cur->country) s += 100;
            if (c.ntsc != cur->ntsc)       s += 100;
        } else if (c.ntsc) {
            s += 100;                       // inconnu : PAL, la référence des étalons
        }
        s += versionRank(m, c.ver);
        if (best < 0 || s < bestScore) { best = static_cast<int>(i); bestScore = s; }
    }
    return best;
}

MachineType pickMachineForTos(MachineType cur, const TosInfo& t) {
    if (tosRunsMachine(t, cur)) return cur;
    using M = MachineType;
    M order[3];
    switch (cur) {
    case M::St:      order[0] = M::MegaSt;  order[1] = M::Ste;     order[2] = M::MegaSte; break;
    case M::MegaSt:  order[0] = M::St;      order[1] = M::MegaSte; order[2] = M::Ste;     break;
    case M::Ste:     order[0] = M::MegaSte; order[1] = M::St;      order[2] = M::MegaSt;  break;
    case M::MegaSte: order[0] = M::Ste;     order[1] = M::MegaSt;  order[2] = M::St;      break;
    }
    for (M m : order) if (tosRunsMachine(t, m)) return m;
    return cur;
}

}  // namespace neost
