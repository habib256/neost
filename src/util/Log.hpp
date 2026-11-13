// =============================================================================
//  Log.hpp — Un seul interrupteur « verbeux » pour les journaux du terminal.
//
//  Actif par DÉFAUT : le headless, le web et Android gardent leurs journaux à
//  l'identique — les outils du harnais en lisent certains (tools/smoke_package.sh
//  cherche « drive A: » et « HDD GEMDOS »). Le GUI de bureau le COUPE au démarrage,
//  sauf `--verbose` ou NEOST_VERBOSE=1 : son terminal ne montre alors qu'un résumé
//  de la machine, une ligne par action de l'utilisateur et les vrais avertissements.
//  Demande de l'auteur (2026-11-13) : le terminal était trop bavard (rappels de
//  callback audio, version GLSL, chaque reprogrammation de l'USART du MFP…).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <atomic>
#include <string>

namespace neost::log {

inline std::atomic<bool>& verboseFlag() {
    static std::atomic<bool> v{true};
    return v;
}
// Vrai : journaux détaillés (défaut, headless). Faux : terminal sobre (GUI).
inline bool verbose() { return verboseFlag().load(std::memory_order_relaxed); }
inline void setVerbose(bool v) { verboseFlag().store(v, std::memory_order_relaxed); }

// Nom de fichier seul (le terminal sobre ne montre pas « …/build/../disks/st/ »).
inline std::string baseName(const std::string& path) {
    const auto p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

}  // namespace neost::log
