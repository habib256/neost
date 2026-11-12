#include "net/NatBackend.hpp"
#ifdef NEOST_WITH_NET
#include "BuiltinNatBackend.h"
#include "NatAllowlist.h"
#endif

struct NatBackend::Impl {
#ifdef NEOST_WITH_NET
    std::unique_ptr<nat::NetworkBackend> backend;
#endif
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
    if (impl_ && frame) impl_->backend->transmit(frame, len);
#else
    (void)frame; (void)len;
#endif
}
bool NatBackend::recv(std::vector<uint8_t>& out) {
#ifdef NEOST_WITH_NET
    if (impl_) {
        out.resize(nat::kMaxEthFrame);
        const int size = impl_->backend->receive(out.data(), int(out.size()));
        if (size > 0) { out.resize(std::size_t(size)); return true; }
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
