#include "net/NatBackend.hpp"
#ifdef NEOST_WITH_NET
#include "BuiltinNatBackend.h"
#include "NatAllowlist.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {

// NEOST_SLIRP_TRACE=1 (nom historique) : journal des échanges DNS et des
// ouvertures/refus TCP vus au fil, côté invité. Ajouté le 2026-11-12 pour
// l'item « STinG, premier run en échec DNS » : la variable ne traçait que la
// trame de --nat-selftest, rien du trafic d'un vrai STinG. Lecture seule des
// trames ; hors déterminisme comme tout le trafic réel.
struct WireTrace {
    bool on = std::getenv("NEOST_SLIRP_TRACE") != nullptr;
    std::map<unsigned, std::chrono::steady_clock::time_point> asked;   // id DNS → date

    static unsigned rd16(const uint8_t* p) { return unsigned(p[0]) << 8 | p[1]; }
    static std::string dotted(const uint8_t* p) {
        return std::to_string(p[0]) + "." + std::to_string(p[1]) + "." +
               std::to_string(p[2]) + "." + std::to_string(p[3]);
    }
    static std::string qname(const uint8_t* m, int len, int& at) {
        std::string name;
        while (at < len && m[at] && m[at] < 64 && at + 1 + m[at] <= len) {
            if (!name.empty()) name += '.';
            name.append(reinterpret_cast<const char*>(m + at + 1), m[at]);
            at += 1 + m[at];
        }
        ++at;
        return name.empty() ? "." : name;
    }
    // Trame Ethernet IPv4 sans FCS ; `out` = de l'invité vers le NAT.
    void frame(const uint8_t* f, int len, bool out) {
        if (!on || len < 34 || f[12] != 0x08 || f[13] != 0x00) return;
        const uint8_t* ip = f + 14;
        const int ihl = (ip[0] & 0x0F) * 4;
        if (ihl < 20 || 14 + ihl + 8 > len) return;
        const uint8_t* l4 = ip + ihl;
        const int l4len = len - 14 - ihl;
        const std::string src = dotted(ip + 12), dst = dotted(ip + 16);
        if (ip[9] == 17 && l4len >= 20) {                       // UDP + en-tête DNS
            const unsigned sport = rd16(l4), dport = rd16(l4 + 2);
            if (dport != 53 && sport != 53) return;
            const uint8_t* m = l4 + 8;
            const int mlen = l4len - 8;
            const unsigned id = rd16(m), flags = rd16(m + 2);
            int at = 12;
            const std::string name = qname(m, mlen, at);
            const unsigned type = at + 2 <= mlen ? rd16(m + at) : 0;
            const auto now = std::chrono::steady_clock::now();
            if (out && !(flags & 0x8000)) {
                if (asked.size() >= 256) asked.clear();   // réponses jamais venues
                asked[id] = now;
                std::fprintf(stderr, "[nat-trace] DNS query  id=%04x %s type %u -> %s\n",
                             id, name.c_str(), type, dst.c_str());
            } else if (!out && (flags & 0x8000)) {
                long ms = -1;
                const auto it = asked.find(id);
                if (it != asked.end()) {
                    ms = long(std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count());
                    asked.erase(it);
                }
                std::fprintf(stderr, "[nat-trace] DNS answer id=%04x %s rcode %u, %u answer(s), %ld ms\n",
                             id, name.c_str(), flags & 0x0F, rd16(m + 6), ms);
            }
        } else if (ip[9] == 6 && l4len >= 20) {
            const uint8_t fl = l4[13];
            if ((fl & 0x02) || (fl & 0x04))                     // SYN, RST
                std::fprintf(stderr, "[nat-trace] TCP %s%s%s %s:%u -> %s:%u\n",
                             out ? "guest " : "nat ", (fl & 0x02) ? "SYN" : "",
                             (fl & 0x04) ? "RST" : ((fl & 0x10) && (fl & 0x02) ? "-ACK" : ""),
                             src.c_str(), rd16(l4), dst.c_str(), rd16(l4 + 2));
        }
    }
};

}  // namespace

struct NatBackend::Impl {
#ifdef NEOST_WITH_NET
    std::unique_ptr<nat::NetworkBackend> backend;
#endif
    WireTrace trace;
};
NatBackend::NatBackend() = default;
NatBackend::~NatBackend() = default;
bool NatBackend::available() {
#ifdef NEOST_WITH_NET
    return nat::builtinNatAvailable();
#else
    return false;
#endif
}
bool NatBackend::open(bool restricted) {
    Options options;
    options.restricted = restricted;
    return open(options);
}
bool NatBackend::open(const Options& options) {
    error_.clear();
#ifdef NEOST_WITH_NET
    nat::BuiltinNatOptions config;
    if (!nat::parseNatForwards(options.forwards, config.forwards)) {
        error_ = "Invalid NAT forwards: expected tcp|udp:bind-ip:host-port:guest-port";
        return false;
    }
    if (options.restricted && !config.forwards.empty()) {
        error_ = "Restricted NAT cannot publish ports";
        return false;
    }
    if (!nat::parseAllowedDomains(options.allowDomains, config.allowedDomains)) {
        error_ = "Invalid domain allowlist: expected names or IPv4 addresses separated by commas";
        return false;
    }
    config.restricted = options.restricted;
    config.allowHostLoopback = options.allowHostLoopback;
    auto next = std::make_unique<Impl>();
    next->backend = nat::makeBuiltinNatBackend(config);
    if (!next->backend || !next->backend->isValid()) {
        error_ = "Cannot start NAT or bind a requested host port";
        return false;
    }
    impl_ = std::move(next);
    return true;
#else
    (void)options;
    error_ = "Host networking is disabled (NEOST_WITH_NET=OFF)";
    return false;
#endif
}
void NatBackend::close() { impl_.reset(); }
bool NatBackend::isOpen() const { return impl_ != nullptr; }
void NatBackend::send(const uint8_t* frame, int len) {
#ifdef NEOST_WITH_NET
    if (impl_ && frame) {
        impl_->trace.frame(frame, len, true);
        impl_->backend->transmit(frame, len);
    }
#else
    (void)frame; (void)len;
#endif
}
bool NatBackend::recv(std::vector<uint8_t>& out) {
#ifdef NEOST_WITH_NET
    if (impl_) {
        out.resize(nat::kMaxEthFrame);
        const int size = impl_->backend->receive(out.data(), int(out.size()));
        if (size > 0) {
            out.resize(std::size_t(size));
            impl_->trace.frame(out.data(), size, false);
            return true;
        }
    }
#endif
    out.clear();
    return false;
}
void NatBackend::poll() {
#ifdef NEOST_WITH_NET
    if (impl_) impl_->backend->poll();
#endif
}
void NatBackend::nicReset() {
#ifdef NEOST_WITH_NET
    if (impl_) impl_->backend->nicReset();   // garde les ports publiés
#endif
}
uint64_t NatBackend::txFrames() const {
#ifdef NEOST_WITH_NET
    if (impl_) return impl_->backend->framesSent();
#endif
    return 0;
}
uint64_t NatBackend::rxFrames() const {
#ifdef NEOST_WITH_NET
    if (impl_) return impl_->backend->framesReceived();
#endif
    return 0;
}
