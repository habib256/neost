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
//  PUITS (2026-11-24) : les journaux du cœur (src/core, src/io, src/util) passent par
//  err(), et non plus par fprintf(stderr, …). Sans puits posé, err() écrit sur stderr,
//  octet pour octet comme avant — les outils du harnais lisent stderr. Un frontend qui
//  n'a pas de terminal les détourne par setSink : libretro vers retro_log_printf
//  (docs/LIBRETRO.md). Les frontends eux-mêmes gardent leurs fprintf.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdarg>
#include <string>

#if defined(__GNUC__) || defined(__clang__)
#define NEOST_PRINTF_FMT(f, a) __attribute__((format(printf, f, a)))
#else
#define NEOST_PRINTF_FMT(f, a)
#endif

namespace neost::log {

inline std::atomic<bool>& verboseFlag() {
    static std::atomic<bool> v{true};
    return v;
}
// Vrai : journaux détaillés (défaut, headless). Faux : terminal sobre (GUI).
inline bool verbose() { return verboseFlag().load(std::memory_order_relaxed); }
inline void setVerbose(bool v) { verboseFlag().store(v, std::memory_order_relaxed); }

// Puits des journaux du cœur : reçoit chaque appel à err() déjà formaté (le texte
// porte ses fins de ligne, comme l'ancien fprintf). nullptr = stderr. À poser avant de
// lancer l'émulation : il n'est pas protégé contre un changement concurrent.
using Sink = void (*)(const char* text, void* user);
void setSink(Sink sink, void* user = nullptr);
// ≡ std::fprintf(stderr, fmt, …) tant qu'aucun puits n'est posé.
void err(const char* fmt, ...) NEOST_PRINTF_FMT(1, 2);
void verr(const char* fmt, va_list ap);

// Nom de fichier seul (le terminal sobre ne montre pas « …/build/../disks/st/ »).
inline std::string baseName(const std::string& path) {
    const auto p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

}  // namespace neost::log
