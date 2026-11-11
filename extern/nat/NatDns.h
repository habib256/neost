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
// The IPv4 and IPv6 servers of one backend share that budget
// (`shareBudgetWith`): two proxies with a bucket each let a guest reach
// eight a second by alternating families (NeoST audit, 2026-11-11).
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

namespace nat {

class W5100Resolver;

namespace detail {

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

    /// False when the datagram was ignored: no reply will EVER be emitted
    /// for it (a response, QR=1, or less than a header). Anything else is
    /// answered, at once or by pump() within kTimeoutMs — so a caller that
    /// keeps per-query state may drop it on false and must not keep it
    /// longer than kTimeoutMs on true.
    bool query(uint16_t guestPort, View payload, uint64_t nowMs, const Emit& emit);
    /// Draw new wire lookups and worker threads from `other`'s budget, so a
    /// backend's two proxies are one limit (four lookups a second, eight
    /// workers) rather than two.
    void shareBudgetWith(const DnsProxy& other) { workers_ = other.workers_; }
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
    /// `active` is written by the workers, under `mutex`. The token bucket
    /// is touched only by query(), on the CPU thread, and needs no lock.
    struct Workers {
        std::mutex mutex; unsigned active = 0;
        double tokens = 16; uint64_t tokenMs = 0;
    };
    struct RawPending { uint16_t port; Bytes query; uint64_t deadline; std::shared_ptr<Job> job; };
    std::vector<RawPending> raw_;
    std::shared_ptr<Workers> workers_ = std::make_shared<Workers>();
    Lookup lookup_;
    void handleQuery(uint16_t guestPort, View payload, uint16_t flags, uint64_t nowMs,
                     const Emit& emit);
    void queryRaw(uint16_t port, const Bytes& query, uint64_t now, const Emit& emit);
    void pumpRaw(uint64_t now, const Emit& emit);
};

}  // namespace detail
}  // namespace nat

#endif  // POM2_NAT_DNS_H
