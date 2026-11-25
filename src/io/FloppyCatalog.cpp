// =============================================================================
//  FloppyCatalog.cpp — cf. FloppyCatalog.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/FloppyCatalog.hpp"

#include <algorithm>
#include <cctype>

namespace neost {
namespace {

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Une entrée de répertoire FAT (32 octets) → nom 8.3. Faux pour une entrée libre,
// effacée, « . » / « .. » ou une étiquette de volume.
bool parseEntry(const uint8_t* e, FloppyEntry& out) {
    if (e[0] == 0x00 || e[0] == 0xE5 || e[0] == '.') return false;
    out.attr = e[11];
    if (out.attr & 0x08) return false;
    std::string base(reinterpret_cast<const char*>(e), 8), ext(reinterpret_cast<const char*>(e + 8), 3);
    while (!base.empty() && base.back() == ' ') base.pop_back();
    while (!ext.empty() && ext.back() == ' ') ext.pop_back();
    out.name = ext.empty() ? base : base + "." + ext;
    out.size = uint32_t(e[28] | (e[29] << 8) | (e[30] << 16) | (uint32_t(e[31]) << 24));
    return true;
}

bool isProgram(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const std::string ext = upper(name.substr(dot + 1));
    return ext == "PRG" || ext == "TOS" || ext == "APP";
}

bool isUtility(const std::string& name) {
    static const char* const kUtil[] = {"INSTALL", "SETUP", "CONFIG", "README", "READ", "LISEZ",
                                        "DOC", "PRINT", "FORMAT", "COPY", "HDINST", "UNINST"};
    const std::string n = upper(name);
    for (const char* u : kUtil)
        if (n.rfind(u, 0) == 0) return true;
    return false;
}

}  // namespace

bool readFloppyCatalog(const SectorReader& read, FloppyCatalog& out) {
    out = FloppyCatalog{};
    uint8_t boot[512];
    if (!read || !read(0, boot)) return false;
    // [B] Atari « ST BIOS » / GEMDOS : un secteur de boot est exécutable quand la somme
    // de ses 256 mots big-endian vaut $1234 (le BIOS l'exécute alors au démarrage).
    uint16_t sum = 0;
    for (int i = 0; i < 512; i += 2) sum = uint16_t(sum + ((boot[i] << 8) | boot[i + 1]));
    out.bootable = sum == 0x1234;

    // [B] BPB du secteur de boot, petit-boutiste : octets par secteur $0B, secteurs par
    // cluster $0D, secteurs réservés $0E, FAT $10, entrées racine $11, secteurs par FAT $16.
    const int bps = le16(boot + 0x0B), spc = boot[0x0D], reserved = le16(boot + 0x0E);
    const int fats = boot[0x10], rootEntries = le16(boot + 0x11), spf = le16(boot + 0x16);
    if (bps != 512 || spc < 1 || spc > 8 || fats < 1 || fats > 2 || rootEntries < 16 || rootEntries > 512
        || spf < 1 || spf > 16)
        return false;
    const int rootStart = (reserved ? reserved : 1) + fats * spf;
    const int rootSectors = (rootEntries * 32 + 511) / 512;
    const int dataStart = rootStart + rootSectors;

    uint8_t sec[512];
    for (int s = 0; s < rootSectors; ++s) {
        if (!read(rootStart + s, sec)) break;
        bool end = false;
        for (int i = 0; i < 512; i += 32) {
            if (sec[i] == 0x00) { end = true; break; }
            FloppyEntry e;
            if (parseEntry(sec + i, e)) out.root.push_back(e);
        }
        if (end) break;
    }

    // \AUTO : suivre la chaîne FAT12 de son premier cluster (bornée).
    auto it = std::find_if(out.root.begin(), out.root.end(), [](const FloppyEntry& e) {
        return (e.attr & 0x10) && upper(e.name) == "AUTO";
    });
    if (it == out.root.end()) return true;
    // Le premier cluster n'est pas gardé dans FloppyEntry : relire l'entrée.
    int cluster = -1;
    for (int s = 0; s < rootSectors && cluster < 0; ++s) {
        if (!read(rootStart + s, sec)) break;
        for (int i = 0; i < 512; i += 32) {
            FloppyEntry e;
            if (parseEntry(sec + i, e) && (e.attr & 0x10) && upper(e.name) == "AUTO") {
                cluster = le16(sec + i + 26);
                break;
            }
        }
    }
    std::vector<uint8_t> fat(std::size_t(spf) * 512);
    for (int s = 0; s < spf; ++s)
        if (!read((reserved ? reserved : 1) + s, fat.data() + std::size_t(s) * 512)) return true;
    auto next = [&](int c) {
        const std::size_t off = std::size_t(c) * 3 / 2;
        if (off + 1 >= fat.size()) return 0xFFF;
        const int v = fat[off] | (fat[off + 1] << 8);
        return (c & 1) ? (v >> 4) : (v & 0xFFF);
    };
    for (int hops = 0; cluster >= 2 && cluster < 0xFF0 && hops < 32; ++hops, cluster = next(cluster)) {
        for (int k = 0; k < spc; ++k) {
            if (!read(dataStart + (cluster - 2) * spc + k, sec)) return true;
            for (int i = 0; i < 512; i += 32) {
                if (sec[i] == 0x00) return true;
                FloppyEntry e;
                if (parseEntry(sec + i, e)) out.autoFolder.push_back(e);
            }
        }
    }
    return true;
}

std::string pickAutoStartProgram(const FloppyCatalog& cat, const std::string& hint) {
    if (cat.bootable) return std::string();
    for (const FloppyEntry& e : cat.autoFolder)
        if (!(e.attr & 0x10) && isProgram(e.name)) return std::string();
    std::vector<std::string> progs;
    for (const FloppyEntry& e : cat.root)
        if (!(e.attr & 0x10) && isProgram(e.name) && !isUtility(e.name)) progs.push_back(e.name);
    if (progs.size() == 1) return "A:\\" + progs[0];
    if (progs.size() > 1 && !hint.empty()) {
        // Le nom de l'image départage : « Bubble Ghost.stx » → BUBBLE.PRG.
        std::string h;
        for (char c : upper(hint))
            if (std::isalnum(static_cast<unsigned char>(c))) h += c;
        std::string best;
        std::size_t bestLen = 0;
        int ties = 0;
        for (const std::string& p : progs) {
            const std::string base = p.substr(0, p.rfind('.'));
            std::size_t n = 0;
            while (n < base.size() && n < h.size() && base[n] == h[n]) ++n;
            if (n > bestLen) { bestLen = n; best = p; ties = 1; }
            else if (n == bestLen) ++ties;
        }
        if (bestLen >= 3 && ties == 1) return "A:\\" + best;
    }
    return std::string();
}

}  // namespace neost
