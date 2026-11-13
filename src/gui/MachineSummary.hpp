// =============================================================================
//  MachineSummary.hpp — Une ligne qui résume la machine pour le terminal sobre
//  du GUI (cf. util/Log.hpp) : « [NeoST] STE | 1 MB | tos162uk.img (TOS 1.62,
//  50 Hz) ». Elle remplace, hors --verbose, « CPU core / TOS loaded / kbd layout »
//  au démarrage et « live reconfigure » après un Apply.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstdio>
#include <string>

#include "core/Machine.hpp"
#include "core/MachineType.hpp"
#include "util/Log.hpp"

namespace neost {

inline std::string machineSummary(const Machine& m, const std::string& romPath) {
    const char* model = "ST";
    switch (m.machineType()) {
    case MachineType::St:      model = "ST"; break;
    case MachineType::MegaSt:  model = "Mega ST"; break;
    case MachineType::Ste:     model = "STE"; break;
    case MachineType::MegaSte: model = "Mega STE"; break;
    }
    const std::size_t kb = m.bus.ram.size() / 1024;
    char ram[32];
    if (kb % 1024 == 0) std::snprintf(ram, sizeof ram, "%zu MB", kb / 1024);
    else                std::snprintf(ram, sizeof ram, "%zu KB", kb);
    std::string tos = "no TOS";
    const auto& rom = m.bus.rom;
    if (!rom.empty()) {
        const uint16_t ver = tosHeaderVersion(rom.data(), rom.size());
        const bool emu = tosHeaderIsEmuTos(rom.data(), rom.size());
        // os_conf (mot à $1C) : bit 0 = PAL (même lecture que Machine::resolveQuartz).
        const bool pal = rom.size() > 0x1D && (rom[0x1D] & 1) != 0;
        char t[96];
        if (emu) std::snprintf(t, sizeof t, "%s (EmuTOS, %s)", neost::log::baseName(romPath).c_str(),
                               pal ? "50 Hz" : "60 Hz");
        else     std::snprintf(t, sizeof t, "%s (TOS %X.%02X, %s)", neost::log::baseName(romPath).c_str(),
                               ver >> 8, ver & 0xFF, pal ? "50 Hz" : "60 Hz");
        tos = t;
    }
    return std::string("[NeoST] ") + model + " | " + ram + " | " + tos;
}

}  // namespace neost
