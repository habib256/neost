// =============================================================================
//  RomProbe.cpp — voir RomProbe.hpp.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/RomProbe.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace neost::romprobe {

// [C] Relevé sur les images de roms/ : tos100*.img = $601E, tos102 à tos206 et
// etos* = $602E. Ne tester que $602E rangeait TOS 1.00 parmi les disques durs ACSI
// au glisser-déposer.
bool tosHeader(const uint8_t h[4]) {
    if (h[0] != 0x60 || (h[1] != 0x1E && h[1] != 0x2E)) return false;
    const unsigned ver = (unsigned(h[2]) << 8) | h[3];
    return ver >= 0x0100 && ver <= 0x04FF;     // TOS 1.00 (1985) .. 4.x (Falcon)
}

bool tosRomSize(std::uintmax_t sz) {
    if (sz > 1024u * 1024u) return false;
    for (std::uintmax_t k : { 192u, 256u, 512u, 1024u }) {
        const std::uintmax_t want = k * 1024u;
        if (sz + 1024u >= want && sz <= want + 1024u) return true;
    }
    return false;
}

bool looksLikeTosHeader(const std::string& path, std::uintmax_t& size) {
    std::error_code ec;
    size = fs::file_size(path, ec);
    if (ec) { size = 0; return false; }
    uint8_t h[4] = {0, 0, 0, 0};
    std::ifstream f(path, std::ios::binary);
    if (!f || !f.read(reinterpret_cast<char*>(h), 4)) return false;
    return tosHeader(h);
}

bool looksLikeTosRom(const std::string& path) {
    std::uintmax_t sz = 0;
    return looksLikeTosHeader(path, sz) && tosRomSize(sz);
}

bool isFloppyImageName(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    for (auto& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx" || ext == ".ipf";
}

// L'en-tête prime sur l'extension : un TOS nommé « .st » reste un TOS. Une
// disquette n'est reconnue qu'à son extension ET si le fichier existe — un chemin
// mal tapé n'est ni l'un ni l'autre, et doit être refusé plutôt que monté.
Positional classifyPositional(const std::string& p) {
    if (looksLikeTosRom(p)) return Positional::Rom;
    std::error_code ec;
    if (isFloppyImageName(p) && fs::is_regular_file(p, ec) && !ec) return Positional::Floppy;
    return Positional::Invalid;
}

} // namespace neost::romprobe
