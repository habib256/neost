// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatDns — DNS on the virtual IPv4/IPv6 network. A uses W5100Resolver
// (getaddrinfo, including the hosts file). Other IN types use the native
// DNS wire exchange on bounded workers: AAAA, MX, TXT, PTR, SRV and unknown
// record types retain their RDATA, compression pointers and response codes.
// The CPU thread never waits. Queries are validated, retries share a lookup,
// pending work is bounded and new wire lookups are limited to four/second.
// queryHostDns in NatHostSockets owns the platform-specific blocking call.
//
// [B] RFC 1035 § 4 (message format), § 4.1.1 (header, RCODE values).

#ifndef POM2_NAT_DNS_H
#define POM2_NAT_DNS_H

#include "NatPacket.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace pom2 {

class W5100Resolver;

namespace nat {

inline constexpr uint16_t kDnsPort = 53;

class DnsProxy
{
public:
    /// A reply datagram for the guest's `guestPort`.
    using Emit = std::function<void(uint16_t guestPort, const Bytes& reply)>;
    using Lookup = std::function<Bytes(const Bytes& query)>;

    /// Queries waiting for the host resolver at once. A full table answers
    /// SERVFAIL, and the guest's own retry comes back when there is room.
    static constexpr std::size_t kMaxPending = 8;
    /// How long a query may wait before it is answered SERVFAIL.
    static constexpr uint64_t    kTimeoutMs  = 8000;

    explicit DnsProxy(W5100Resolver* resolver, Lookup lookup = {})
        : resolver_(resolver), lookup_(std::move(lookup)) {}

    void query(uint16_t guestPort, View payload, uint64_t nowMs, const Emit& emit);
    void pump(uint64_t nowMs, const Emit& emit);
    void clear() { pending_.clear(); raw_.clear(); }
    std::size_t pending() const { return pending_.size() + raw_.size(); }

private:
    struct Pending {
        uint16_t    guestPort = 0;
        Bytes       query;        ///< header + question, as received
        std::string name;
        uint64_t    deadlineMs = 0;
    };

    W5100Resolver*       resolver_;
    std::vector<Pending> pending_;
    struct Job { std::mutex mutex; bool ready = false; Bytes answer; };
    struct Workers { std::mutex mutex; unsigned active = 0; };
    struct RawPending { uint16_t port; Bytes query; uint64_t deadline; std::shared_ptr<Job> job; };
    std::vector<RawPending> raw_;
    std::shared_ptr<Workers> workers_ = std::make_shared<Workers>();
    Lookup lookup_;
    double tokens_ = 16;
    uint64_t tokenMs_ = 0;
    void queryRaw(uint16_t port, const Bytes& query, uint64_t now, const Emit& emit);
    void pumpRaw(uint64_t now, const Emit& emit);
};

}  // namespace nat
}  // namespace pom2

#endif  // POM2_NAT_DNS_H
