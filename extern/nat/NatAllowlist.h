// NeoST — NAT intégré (issu de POM2)
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Optional domain allowlist for the outbound backends (NeoST 2026-11-12, not
// upstream yet). OFF when the list is empty, which is the default.
//
// What it decides
// ---------------
//   * A name is allowed when it equals a listed domain or ends with
//     "." + a listed domain, case-insensitively: "example.org" lets
//     "example.org" and "www.example.org" through, not "badexample.org".
//     "*.example.org" and ".example.org" are read as "example.org".
//     A listed dotted IPv4 address allows that address and nothing else.
//   * The NAT's DNS server answers REFUSED for any other name
//     ([B] RFC 1035 § 4.1.1, RCODE 5: "refuses to perform the specified
//     operation for policy reasons"), A and native queries alike.
//   * Traffic the guest starts (TCP connect, UDP datagram) goes out only to
//     an address the DNS server has handed back for an allowed name, in
//     this power cycle of the card, or to a listed IPv4 address. A literal
//     address the guest made up is refused.
//
// What it does not decide
// -----------------------
//   * The virtual services (DHCP, DNS, ping of the gateway) and inbound
//     publications (`nat_forwards`) are not outbound traffic.
//   * The host's loopback keeps its own opt-in (`nat_allow_loopback`): once
//     the user has opened it, the list does not close it again.
//   * An address is shared by every name it serves: an allowed name hosted
//     on a CDN or a shared host opens that address, whatever site the guest
//     then asks it for (HTTP Host, TLS SNI are not read).

#ifndef NEOST_NAT_ALLOWLIST_H
#define NEOST_NAT_ALLOWLIST_H

#include "NatHost.h"
#include "NatPacket.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace nat {

/// Domains separated by commas, semicolons or blanks. Lower-cased; a
/// leading "*." or "." and a trailing "." are dropped. Letters, digits, '-',
/// '_' and '.', labels of 1 to 63 characters, at most 253 characters and 64
/// entries. Empty text gives an empty list (allowlist off). An invalid list
/// is rejected whole and `out` is left untouched.
bool parseAllowedDomains(const std::string& text, std::vector<std::string>& out);

/// `name` equals an entry or is a subdomain of one (case-insensitive,
/// trailing dot ignored). An empty list allows nothing: the caller tests
/// for "allowlist on" first.
bool domainAllowed(const std::vector<std::string>& list, const std::string& name);

namespace detail {

/// The allowlist as one backend applies it: names checked at the DNS
/// server, addresses learned from its answers, destinations checked at the
/// fence. One per backend, shared by its IPv4 and IPv6 DNS servers.
class DomainGate
{
public:
    /// Learned addresses kept per family; the oldest goes first.
    static constexpr std::size_t kMaxLearned = 4096;
    /// Distinct refused names written to the log, then one closing line.
    static constexpr std::size_t kMaxNamesLogged = 16;

    DomainGate() = default;
    explicit DomainGate(std::vector<std::string> domains);

    bool active() const { return !domains_.empty(); }
    bool allowsName(const std::string& name) const { return domainAllowed(domains_, name); }
    /// allowsName(), with a log line for the first refusals.
    bool admitName(const std::string& name);

    /// An address the DNS server handed back for an allowed name. IPv4 is
    /// in host order, as Endpoint holds it.
    void learn4(uint32_t ip);
    void learn6(const Ipv6& ip);
    /// The A and AAAA records of a wire-format answer (native queries).
    void learnWire(const Bytes& answer);

    /// May guest traffic go to `host`? True whenever the gate is off.
    bool reaches(const Endpoint& host) const;

    /// The card's power cycle: the guest's resolver cache is gone, so are
    /// the addresses it learned through this one.
    void forget();

    std::size_t learned() const { return order4_.size() + order6_.size(); }
    uint64_t    refusedNames() const { return refusedNames_; }

private:
    std::vector<std::string> domains_;
    std::set<uint32_t>       literal4_;      ///< dotted entries of the list
    std::set<uint32_t>       known4_;
    std::deque<uint32_t>     order4_;
    std::set<Ipv6>           known6_;
    std::deque<Ipv6>         order6_;
    std::set<std::string>    loggedNames_;
    uint64_t                 refusedNames_ = 0;
    bool                     logClosed_ = false;
};

}  // namespace detail
}  // namespace nat

#endif  // NEOST_NAT_ALLOWLIST_H
