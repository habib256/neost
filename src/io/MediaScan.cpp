// =============================================================================
//  MediaScan.cpp — cf. MediaScan.hpp. Déplacé depuis main.cpp (kioskScanDisks)
//  SANS changement de comportement : mêmes bornes, même ordre de tri.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/MediaScan.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace neost {
namespace {

std::string lowerOf(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

}  // namespace

std::size_t commonPrefixLenCI(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && std::tolower((unsigned char)a[i]) == std::tolower((unsigned char)b[i])) ++i;
    return i;
}

bool areSiblingImages(const std::string& a, const std::string& b) {
    if (a == b) return true;
    const std::size_t p = commonPrefixLenCI(a, b);
    if (p < 3) return false;
    std::size_t s = 0;
    const std::size_t maxS = std::min(a.size(), b.size()) - p;   // ne pas empiéter sur le préfixe
    while (s < maxS && a[a.size() - 1 - s] == b[b.size() - 1 - s]) ++s;
    const std::size_t gapA = a.size() - p - s, gapB = b.size() - p - s;
    if (gapA > 12 || gapB > 12) return false;              // le morceau qui diffère doit être court
    return (p + s) * 2 >= std::min(a.size(), b.size());     // préfixe+suffixe couvrent l'essentiel
}

std::vector<std::string> scanDiskImages(const std::vector<std::string>& dirs,
                                        const std::string& mounted) {
    std::vector<std::string> out;

    auto scanInto = [&out](const std::string& dir) {
        std::error_code e2;
        if (dir.empty() || !fs::is_directory(dir, e2)) return;
        // ⚠ Le range-for incrémente via operator++() qui LANCE filesystem_error sur un
        // dossier illisible (EACCES) : itération manuelle + skip_permission_denied.
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, e2), end;
        // BORNES DURES : profondeur, nombre d'entrées et budget de temps. Ce scan
        // tourne dans le thread d'interface ; sans elles, un dossier vaste le fige.
        constexpr int  kMaxDepth   = 6;
        constexpr long kMaxEntries = 40000;
        const auto     tStart      = std::chrono::steady_clock::now();
        long seen = 0;
        while (!e2 && it != end) {
            if (++seen > kMaxEntries) break;
            if ((seen & 0x3FF) == 0 &&
                std::chrono::steady_clock::now() - tStart > std::chrono::milliseconds(800)) break;
            if (it.depth() >= kMaxDepth) it.disable_recursion_pending();
            const fs::directory_entry& e = *it;
            std::error_code e3;
            if (e.is_regular_file(e3)) {
                const std::string ext = lowerOf(e.path().extension().string());
                if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx" || ext == ".ipf") {
                    const std::string p = e.path().string();
                    if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
                }
            }
            it.increment(e2);
        }
    };
    for (const auto& d : dirs) scanInto(d);

    const std::string mref = lowerOf(fs::path(mounted).filename().string());
    std::sort(out.begin(), out.end(), [&mref](const std::string& a, const std::string& b) {
        const std::string an = lowerOf(fs::path(a).filename().string());
        const std::string bn = lowerOf(fs::path(b).filename().string());
        // 1) les VRAIES suites du jeu monté (face A/B, partie 1/2/3) en tête
        if (!mref.empty()) {
            const bool sa = areSiblingImages(an, mref), sb = areSiblingImages(bn, mref);
            if (sa != sb) return sa;
        }
        // 2) puis par proximité de nom (préfixe commun), 3) alphabétique
        const std::size_t pa = commonPrefixLenCI(an, mref);
        const std::size_t pb = commonPrefixLenCI(bn, mref);
        if (pa != pb) return pa > pb;
        return an < bn;
    });
    return out;
}

}  // namespace neost
