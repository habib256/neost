// =============================================================================
//  Log.cpp — puits des journaux du cœur, cf. Log.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/Log.hpp"

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace neost::log {

namespace {
Sink  g_sink = nullptr;
void* g_sinkUser = nullptr;
}  // namespace

void setSink(Sink sink, void* user) {
    g_sink = sink;
    g_sinkUser = user;
}

void verr(const char* fmt, va_list ap) {
    if (!g_sink) {                       // chemin d'origine, inchangé
        std::vfprintf(stderr, fmt, ap);
        return;
    }
    char small[1024];
    va_list ap2;
    va_copy(ap2, ap);
    const int n = std::vsnprintf(small, sizeof small, fmt, ap2);
    va_end(ap2);
    if (n < 0) return;
    if (std::size_t(n) < sizeof small) { g_sink(small, g_sinkUser); return; }
    std::vector<char> big(std::size_t(n) + 1);
    std::vsnprintf(big.data(), big.size(), fmt, ap);
    g_sink(big.data(), g_sinkUser);
}

void err(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verr(fmt, ap);
    va_end(ap);
}

}  // namespace neost::log
