// =============================================================================
//  Bus.cpp — Implémentation du Memory Map et du dispatch MMIO.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Bus.hpp"
#include "core/Shifter.hpp"
#include "core/YM2149.hpp"
#include "core/Glue.hpp"
#include "core/Cpu68k.hpp"
#include "io/Mfp.hpp"
#include "io/Ikbd.hpp"

#include <cstdio>
#include <fstream>

Bus::Bus(std::size_t ramBytes) {
    ram.assign(ramBytes, 0);
}

bool Bus::loadTos(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[Bus] TOS introuvable : %s\n", path.c_str());
        return false;
    }
    const std::streamsize n = f.tellg();
    f.seekg(0);
    rom.resize(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(rom.data()), n);

    // L'emplacement de la ROM dépend de la version de TOS (cf. stmap) : un TOS
    // de 192 Ko vit à $FC0000, sinon (224/256 Ko) à $E00000.
    romBase = (rom.size() <= 192u * 1024u) ? stmap::ROM_FC0000 : stmap::ROM_E00000;
    std::printf("[Bus] TOS chargé : %zu Ko @ $%06X\n", rom.size() / 1024, romBase);
    return true;
}

// -----------------------------------------------------------------------------
//  Lecture / écriture 8 bits — point d'aiguillage central du bus.
// -----------------------------------------------------------------------------
uint8_t Bus::read8(uint32_t addr) {
    addr &= stmap::ADDR_MASK;

    // Overlay de boot : les 8 premiers octets ($0-$7) proviennent de la ROM
    // tant que le 68000 n'a pas fini de lire SSP+PC au reset.
    if (bootOverlay && addr < 8 && addr < rom.size())
        return rom[addr];

    // RAM ST (zone basse).
    if (addr < ram.size())
        return ram[addr];

    // ROM TOS.
    if (addr >= romBase && addr < romBase + rom.size())
        return rom[addr - romBase];

    // Espace matériel.
    if (addr >= stmap::MMIO_BASE)
        return mmioRead8(addr);

    // Trou d'adressage → bus error sur vrai ST ; ici on renvoie $FF (lignes hautes).
    return 0xFF;
}

void Bus::write8(uint32_t addr, uint8_t v) {
    addr &= stmap::ADDR_MASK;

    if (addr < ram.size()) {
        ram[addr] = v;
        return;
    }
    if (addr >= stmap::MMIO_BASE) {
        mmioWrite8(addr, v);
        return;
    }
    // Écriture en ROM ou trou d'adressage : ignorée (lecture seule).
}

bool Bus::busFault(uint32_t addr) const {
    addr &= stmap::ADDR_MASK;
    // Blitter ($FF8A00-$FF8A3F) : absent sur ST de base → la lecture provoque
    // une bus error. EmuTOS s'en sert pour conclure "pas de blitter" ; sinon il
    // route ses tracés VDI (barre de menu, curseur souris) vers un blitter
    // fantôme et ils n'apparaissent pas.
    if (addr >= 0xFF8A00 && addr <= 0xFF8A3F) return true;
    return false;
}

// --- Accès 16/32 bits : le 68000 est big-endian, on assemble octet par octet --
uint16_t Bus::read16(uint32_t addr) {
    return static_cast<uint16_t>((read8(addr) << 8) | read8(addr + 1));
}
uint32_t Bus::read32(uint32_t addr) {
    return (static_cast<uint32_t>(read16(addr)) << 16) | read16(addr + 2);
}
void Bus::write16(uint32_t addr, uint16_t v) {
    write8(addr,     static_cast<uint8_t>(v >> 8));
    write8(addr + 1, static_cast<uint8_t>(v));
}
void Bus::write32(uint32_t addr, uint32_t v) {
    write16(addr,     static_cast<uint16_t>(v >> 16));
    write16(addr + 2, static_cast<uint16_t>(v));
}

// -----------------------------------------------------------------------------
//  Dispatch MMIO ($FF8000-$FFFFFF). Chaque puce expose read8/write8 ; le bus
//  ne fait QUE router, il ne connaît pas les détails internes des composants.
// -----------------------------------------------------------------------------
uint8_t Bus::mmioRead8(uint32_t addr) {
    if (addr >= stmap::SHIFTER_BASE && addr <= stmap::SHIFTER_END && shifter)
        return shifter->read8(addr);
    if (addr >= stmap::PSG_BASE && addr < stmap::PSG_BASE + 4 && psg)
        return psg->read8(addr);
    if (addr >= stmap::MFP_BASE && addr < stmap::MFP_BASE + 0x40 && mfp) {
        const uint8_t v = mfp->read8(addr);
        if (cpu) cpu->updateIpl();        // l'état d'IRQ a pu changer
        return v;
    }
    if (addr >= stmap::ACIA_BASE && addr < stmap::ACIA_BASE + 4 && ikbd) {
        const uint8_t v = ikbd->read8(addr);   // ACIA clavier $FFFC00/$FFFC02
        if (cpu) cpu->updateIpl();
        return v;
    }
    if (addr >= 0xFFFC04 && addr < 0xFFFC08)   // ACIA MIDI : statut "prêt, rien à lire"
        return (addr & 2) ? 0x00 : 0x02;
    if (glue)
        return glue->read8(addr);         // MMU et reste du MMIO
    return 0xFF;
}

void Bus::mmioWrite8(uint32_t addr, uint8_t v) {
    if (addr >= stmap::SHIFTER_BASE && addr <= stmap::SHIFTER_END && shifter) {
        shifter->write8(addr, v);
        return;
    }
    if (addr >= stmap::PSG_BASE && addr < stmap::PSG_BASE + 4 && psg) {
        psg->write8(addr, v);
        return;
    }
    if (addr >= stmap::MFP_BASE && addr < stmap::MFP_BASE + 0x40 && mfp) {
        mfp->write8(addr, v);
        if (cpu) cpu->updateIpl();        // (dé)masquage, fin d'interruption...
        return;
    }
    if (addr >= stmap::ACIA_BASE && addr < stmap::ACIA_BASE + 4 && ikbd) {
        ikbd->write8(addr, v);
        if (cpu) cpu->updateIpl();
        return;
    }
    if (addr >= 0xFFFC04 && addr < 0xFFFC08)   // ACIA MIDI : écritures ignorées
        return;
    if (glue)
        glue->write8(addr, v);
}
