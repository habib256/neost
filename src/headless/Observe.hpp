#pragma once
// =============================================================================
//  Observe.hpp — tout ce par quoi le monde extérieur REGARDE la machine :
//  sondes mémoire, hachages de cellule, capture PPM.
//
//  Partagé entre la boucle --frames (main_headless.cpp) et le mode serveur
//  (Server.cpp) : les deux doivent publier EXACTEMENT le même format et les
//  mêmes valeurs, sinon un pilote externe ne pourrait pas vérifier qu'un rejeu
//  au tuyau vaut le rejeu en ligne de commande.
//
//  Règle cardinale : lecture par Bus::peek8, comme un débogueur — ni dispatch
//  MMIO, ni wait state, ni bus error. Une sonde ne doit RIEN changer à ce
//  qu'elle observe, sinon elle détruit le déterminisme qu'elle sert à mesurer.
//  Conséquence ASSUMÉE : l'espace I/O ($FF8000+) se lit $FF, il n'est pas
//  sondable. Y passer par read8 serait doublement faux — effet de bord sur les
//  registres à lecture destructive (FDC, ACIA), et surtout bus error levée HORS
//  du try/catch de Moira sur les registres whitelistés en accès octet (pads STE
//  $FF9200, FDC $FF8604-07), qui TERMINE le processus. C'est exactement pour
//  cela que Bus::dmaRead8 refuse déjà le dispatch MMIO (Bus.cpp). Pour observer
//  une puce, il y a --dump-at et --trace.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Machine.hpp"
#include "util/JoyScript.hpp"   // parseHexU32 (partagé avec la grammaire joystick)

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace neost::observe {

struct ProbeSpec {
    std::string name;
    uint32_t    addr = 0;
    uint32_t    len  = 1;      // 1, 2 ou 4 octets, big-endian (68000)
};

inline uint8_t read8(Machine& m, uint32_t addr) {
    return m.bus.peek8(addr & 0xFFFFFFu);
}

inline uint32_t probeValue(Machine& m, const ProbeSpec& p) {
    uint32_t v = 0;
    for (uint32_t k = 0; k < p.len; ++k) v = (v << 8) | read8(m, p.addr + k);
    return v;
}

// FNV-1a 64 bits du framebuffer décodé : clé de « cellule » d'un explorateur
// d'états (même écran = même situation) sans sortir la moindre image.
inline uint64_t screenHash(Machine& m) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(m.shifter.pixels());
    const std::size_t n = std::size_t(m.shifter.width()) * std::size_t(m.shifter.height()) * 4;
    uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// Idem sur une tranche de RAM : clé plus FINE que l'écran (qui ignore l'état
// caché) et infiniment plus petite qu'un save-state.
inline uint64_t ramHash(Machine& m, uint32_t addr, uint32_t len) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t k = 0; k < len; ++k) { h ^= read8(m, addr + k); h *= 1099511628211ull; }
    return h;
}

// Ce que publie une observation : les sondes déclarées + les clés de cellule.
struct ProbeSet {
    std::vector<ProbeSpec> probes;
    bool                   hashRam     = false;
    uint32_t               hashRamAddr = 0;
    uint32_t               hashRamLen  = 0;
};

// « NOM=ADDR:LEN », « ADDR:LEN » (le nom devient $ADDR) ou « ADDR » (LEN = 1).
inline bool parseProbeSpec(const std::string& arg, ProbeSpec& p, std::string& err) {
    std::string body = arg;
    const std::size_t eq = arg.find('=');
    if (eq != std::string::npos) { p.name = arg.substr(0, eq); body = arg.substr(eq + 1); }
    std::string addrTxt = body;
    const std::size_t colon = body.find(':');
    if (colon != std::string::npos) {
        addrTxt = body.substr(0, colon);
        const std::string lenTxt = body.substr(colon + 1);
        if (lenTxt != "1" && lenTxt != "2" && lenTxt != "4") { err = "probe length must be 1, 2 or 4"; return false; }
        p.len = uint32_t(lenTxt[0] - '0');
    }
    if (!joyscript::parseHexU32(addrTxt, p.addr)) {
        err = "bad probe address '" + addrTxt + "' (hex expected)";
        return false;
    }
    if (p.name.empty()) { char b[16]; std::snprintf(b, sizeof b, "$%06X", p.addr); p.name = b; }
    // Le nom est une CLÉ du format « clé=valeur séparés par des espaces » : une espace
    // ou un « = » dedans casse tout parseur, et un nom de 116 caractères faisait
    // déborder le tampon de formatage — la VALEUR disparaissait de la ligne (mesuré).
    if (p.name.size() > 64) { err = "probe name longer than 64 characters"; return false; }
    for (const char c : p.name)
        if (c == ' ' || c == '\t' || c == '=' || c == '\r' || c == '\n') {
            err = "probe name must not contain spaces or '='";
            return false;
        }
    return true;
}

// Ajout dans un jeu de sondes : deux sondes de même nom rendraient deux clés
// identiques sur la même ligne — un dictionnaire en perd une en silence.
inline bool addProbe(ProbeSet& set, const ProbeSpec& p, std::string& err) {
    for (const auto& q : set.probes)
        if (q.name == p.name) { err = "duplicate probe name '" + p.name + "'"; return false; }
    set.probes.push_back(p);
    return true;
}

// Le corps d'un échantillon, SANS préfixe ni saut de ligne : « clé=valeur »
// séparés par des espaces. Format VOLONTAIREMENT stable et bête à analyser —
// c'est un contrat avec un client externe, pas un journal. Une seule fonction
// pour la boucle --frames et pour le serveur : impossible qu'ils divergent.
inline std::string probeFields(long long frame, Machine& m, const ProbeSet& set) {
    char buf[128];
    std::string out;
    std::snprintf(buf, sizeof buf, "frame=%lld screen=%016llx",
                  frame, static_cast<unsigned long long>(screenHash(m)));
    out = buf;
    if (set.hashRam) {
        std::snprintf(buf, sizeof buf, " ram=%016llx",
                      static_cast<unsigned long long>(ramHash(m, set.hashRamAddr, set.hashRamLen)));
        out += buf;
    }
    for (const auto& p : set.probes) {
        // Nom hors du tampon fixe (borné à 64, mais autant ne pas dépendre de la borne).
        std::snprintf(buf, sizeof buf, "=0x%0*X", int(p.len * 2), probeValue(m, p));
        out += ' ';
        out += p.name;
        out += buf;
    }
    return out;
}

// Ligne d'échantillon de la boucle --frames : les journaux vont sur stderr,
// stdout ne porte que des données, vidées à chaque ligne (le client lit au fil
// de l'eau et ne doit jamais attendre un tampon).
inline void emitProbeLine(long long frame, Machine& m, const ProbeSet& set) {
    std::printf("probe %s\n", probeFields(frame, m, set).c_str());
    std::fflush(stdout);
}

// Dump du framebuffer décodé en PPM binaire (P6) — comparable visuellement.
inline bool writePpm(const char* path, const uint32_t* px, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    bool ok = true;
    for (int i = 0; i < w * h; ++i) {
        const uint32_t c = px[i];                 // ARGB8888
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((c >> 16) & 0xFF),
            static_cast<unsigned char>((c >> 8)  & 0xFF),
            static_cast<unsigned char>( c        & 0xFF) };
        if (std::fwrite(rgb, 1, 3, f) != 3) { ok = false; break; }
    }
    // fclose vérifié aussi : un disque plein peut n'échouer qu'au flush final —
    // une capture tronquée qui « réussit » finit diffée comme si c'était l'image.
    if (std::fclose(f) != 0) ok = false;
    return ok;
}

}  // namespace neost::observe
