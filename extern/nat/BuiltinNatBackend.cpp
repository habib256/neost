// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BuiltinNatBackend — see the header.

#include "BuiltinNatBackend.h"

#include "Logger.h"
#include "NatDhcp.h"
#include "NatDns.h"
#include "NatGuestSink.h"
#include "NatPacket.h"
#include "NatTcp.h"
#include "NatUdp.h"
#include "Pom2Build.h"
#include "W5100NameResolver.h"
#include "W5100Resolver.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <utility>
#include <sstream>
#include <algorithm>

namespace pom2 {

bool parseNatForwards(const std::string& text, std::vector<BuiltinNatOptions::PortForward>& out)
{
    std::vector<BuiltinNatOptions::PortForward> parsed;
    std::istringstream rules(text);
    std::string rule;
    auto number = [](const std::string& s, unsigned max, unsigned& n) {
        n = 0;
        if (s.empty()) return false;
        for (char c : s) {
            if (c < '0' || c > '9' || n > max / 10) return false;
            n = n * 10 + unsigned(c - '0');
            if (n > max) return false;
        }
        return true;
    };
    while (std::getline(rules, rule, ';')) {
        if (rule.empty()) continue;
        std::istringstream fields(rule);
        std::string proto, ip, host, guest, extra;
        if (!std::getline(fields, proto, ':') || !std::getline(fields, ip, ':') ||
            !std::getline(fields, host, ':') || !std::getline(fields, guest, ':') ||
            std::getline(fields, extra, ':') || (proto != "tcp" && proto != "udp")) return false;
        std::istringstream octets(ip);
        std::string part;
        uint32_t address = 0;
        for (int i = 0; i < 4; ++i) {
            unsigned n;
            if (!std::getline(octets, part, '.') || !number(part, 255, n)) return false;
            address = (address << 8) | n;
        }
        if (std::getline(octets, part, '.')) return false;
        unsigned hp, gp;
        if (!number(host, 65535, hp) || !number(guest, 65535, gp) || !hp || !gp || parsed.size() >= 32) return false;
        parsed.push_back({proto == "tcp", {address, static_cast<uint16_t>(hp)}, static_cast<uint16_t>(gp)});
    }
    out = std::move(parsed);
    return true;
}

namespace nat {

Route routeDestination(uint32_t destination, uint16_t port, bool tcp,
                       const RoutePolicy& policy, Endpoint& host)
{
    host = { destination, port };
    const uint8_t top = static_cast<uint8_t>(destination >> 24);

    // The virtual DNS server: port 53 only, and only over UDP — it is
    // answered here, there is no server behind it to hold a TCP connection.
    // (libslirp re-dials this address on the HOST's resolver keeping the
    // guest's port, which is 127.0.0.1 under dnsmasq and several VPN
    // clients: 10.0.2.3:<port> reached host services. Nothing is re-dialled
    // here, so the hole has no counterpart.)
    if (destination == kDnsIp)
        return (!tcp && port == kDnsPort) ? Route::Dns : Route::Refused;

    if (destination == kGatewayIp) {
        if (!policy.allowHostLoopback || policy.restricted) return Route::Refused;
        host.ip = kLoopbackIp;
        return Route::Host;
    }
    // The rest of the virtual subnet — the guest's own address, its
    // broadcast, unassigned numbers: nobody is there.
    if ((destination & kSubnetMask) == kNetwork) return Route::Refused;

    if (policy.restricted) return Route::Refused;

    if (top == 127) return policy.allowHostLoopback ? Route::Host : Route::Refused;
    if (top == 0) return Route::Refused;                          // "this network"
    if ((destination >> 16) == 0xA9FE) return Route::Refused;     // 169.254/16 link-local
    if ((top & 0xF0) == 0xE0) return Route::Refused;              // 224/4 multicast
    if ((top & 0xF0) == 0xF0) return Route::Refused;              // 240/4, broadcast included
    return Route::Host;
}

Route routeDestination6(const Ipv6& destination, uint16_t port, bool tcp,
                        const RoutePolicy& policy, Endpoint& host)
{
    host = {}; host.port = port; host.ipv6 = destination; host.isIpv6 = true;
    if (destination == kDns6) return !tcp && port == 53 ? Route::Dns : Route::Refused;
    Ipv6 loopback{}; loopback[15] = 1;
    if (destination == kGateway6) {
        if (!policy.allowHostLoopback || policy.restricted) return Route::Refused;
        host.ipv6 = loopback; return Route::Host;
    }
    if (policy.restricted || destination == Ipv6{} || destination[0] == 0xff ||
        (destination[0] == 0xfe && (destination[1] & 0xc0) == 0x80) ||
        std::equal(destination.begin(), destination.begin() + 8, kGateway6.begin())) return Route::Refused;
    if (destination == loopback) return policy.allowHostLoopback ? Route::Host : Route::Refused;
    // IPv4-mapped addresses obey exactly the IPv4 fence, including 127/8.
    if (std::all_of(destination.begin(), destination.begin() + 10, [](uint8_t x){return x == 0;}) &&
        destination[10] == 0xff && destination[11] == 0xff)
        return routeDestination(rd32(destination.data() + 12), port, tcp, policy, host);
    // Deprecated IPv4-compatible and other special ::/96 addresses.
    if (std::all_of(destination.begin(), destination.begin() + 12, [](uint8_t x){return x == 0;})) return Route::Refused;
    return Route::Host;
}

}  // namespace nat

namespace {

using namespace nat;

// [D] libslirp's convention for its virtual hosts: 52:55 then the address.
constexpr Mac kGatewayMac = { 0x52, 0x55, 0x0A, 0x00, 0x02, 0x02 };

std::string dotted(uint32_t ip)
{
    return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
}

/// Eight hex groups, uncompressed: for one log line, not for parsing.
std::string colons(const Ipv6& ip)
{
    std::string out;
    for (int i = 0; i < 16; i += 2) {
        char group[8];
        std::snprintf(group, sizeof group, "%s%x", i ? ":" : "", rd16(ip.data() + i));
        out += group;
    }
    return "[" + out + "]";
}

class BuiltinNat final : public NetworkBackend, private GuestSink
{
public:
    /// Same backstop as the other backends: when the guest stops draining,
    /// frames are dropped rather than the heap grown. TCP resends; UDP
    /// tolerates the loss.
    static constexpr std::size_t kMaxQueued = 256;

    /// IPv6 flows are relayed under a private IPv4 alias (0xF000xxxx) so
    /// that UdpRelay and TcpRelay serve both families. An alias lives while
    /// a relay can still name it; one nobody names is reclaimed — when the
    /// table is full, and every kAliasSweepMs. The cap is one more than the
    /// relays can ever name at once (every UDP flow full of IPv4-mapped and
    /// IPv6 peers, every TCP slot taken), so a sweep of a full table always
    /// frees a slot. Before 2026-11-11 the table only grew and an alias was
    /// spent even on a refused destination: 512 datagrams to fe80::x and
    /// IPv6 was dead for the session (NeoST audit).
    static constexpr std::size_t kMaxAliases6 =
        UdpRelay::kMaxFlows * 2 * UdpRelay::kMaxPeers + TcpRelay::kMaxConnections + 1;
    static constexpr uint32_t kAliasFirst = 0xF0000001, kAliasLast = 0xF000FFFE;
    static constexpr uint64_t kAliasSweepMs = 10000;
    /// Guests waiting on the IPv6 DNS server. Every query dns6_ accepts is
    /// answered within DnsProxy::kTimeoutMs, so an entry older than that
    /// (plus a margin for the pump) is an orphan and is dropped.
    static constexpr std::size_t kMaxDnsGuests6 = 32;
    static constexpr uint64_t    kDnsGuest6Ms   = DnsProxy::kTimeoutMs + 2000;

    BuiltinNat(const BuiltinNatOptions& options, std::unique_ptr<HostNetwork> network,
               std::unique_ptr<W5100Resolver> resolver)
        : policy_{ options.allowHostLoopback, options.restricted },
          nowMs_(options.nowMs),
          network_(std::move(network)),
          resolver_(std::move(resolver)),
          dns_(resolver_.get(), options.dnsLookup),
          dns6_(resolver_.get(), options.dnsLookup),
          udp_(*this, network_.get()),
          tcp_(*this, network_.get(),
               [this](uint32_t destination, uint16_t port, Endpoint& host) {
                   const Route r = routePeer(destination, port, true, host);
                   if (r != Route::Host) noteRefused(destination, port, "TCP");
                   return r == Route::Host;
               })
    {
        for (const auto& rule : options.forwards) {
            if (forwards_.size() >= 32 || !rule.bind.port || !rule.guestPort) break;
            auto socket = rule.tcp ? network_->listenTcp(rule.bind) : network_->bindUdp(rule.bind);
            if (socket) forwards_.push_back({rule, std::move(socket), {}});
            else { valid_ = false; log().warn("Net", "NAT port forward could not bind host port " + std::to_string(rule.bind.port)); }
        }
        // One DNS budget for the backend, whichever family the guest asks in.
        dns6_.shareBudgetWith(dns_);
        name_ = std::string("built-in NAT (guest 10.0.2.15, gw 10.0.2.2, dns 10.0.2.3") +
                (policy_.restricted ? ", restricted" : "") +
                (policy_.allowHostLoopback ? ", host loopback ALLOWED" : "") + ")";
        log().info("Net", "built-in virtual network up — " + name_);
    }

    bool isValid() const override { return network_ != nullptr && valid_; }
    std::string_view name() const override { return name_; }

    void transmit(const uint8_t* frame, int len) override
    {
        if (!network_ || len < kMinEthFrame || len > kMaxEthFrame) {
            ++framesDropped_;
            return;
        }
        EthernetFrame ethernet;
        if (!parseEthernet(frame, static_cast<std::size_t>(len), ethernet)) {
            ++framesDropped_;
            return;
        }
        ++framesSent_;
        // The source of each frame the guest sends is where replies go.
        guestMac_ = ethernet.source;
        if (ethernet.etherType == kEtherTypeArp)       onArp(ethernet);
        else if (ethernet.etherType == kEtherTypeIpv4) onIpv4(ethernet);
        else if (ethernet.etherType == kEtherTypeIpv6) onIpv6(ethernet);
    }

    int receive(uint8_t* buf, int cap) override
    {
        if (outbound_.empty()) return 0;
        const Bytes& f = outbound_.front();
        const int n = static_cast<int>(f.size());
        if (n > cap) { outbound_.pop_front(); ++framesDropped_; return 0; }
        std::memcpy(buf, f.data(), f.size());
        outbound_.pop_front();
        ++framesReceived_;
        return n;
    }

    void poll() override
    {
        if (!network_) return;
        const uint64_t t = now();
        fragments_.expire(t);
        fragments6_.expire(t);
        dns_.pump(t, [this](uint16_t guestPort, const Bytes& reply) {
            udpToGuest(kDnsIp, kDnsPort, guestPort, reply.data(), reply.size());
        });
        dns6_.pump(t, [this](uint16_t port, const Bytes& reply) { emitDns6(port, reply); });
        for (auto it = dnsGuests6_.begin(); it != dnsGuests6_.end();)
            if (t >= it->second.expiresMs) it = dnsGuests6_.erase(it);
            else ++it;
        if (t >= nextAliasSweepMs_) { reclaimAliases6(); nextAliasSweepMs_ = t + kAliasSweepMs; }
        if (ipv6Active_ && t >= nextRaMs_) {
            Ipv6 allNodes{}; allNodes[0] = 0xff; allNodes[1] = 2; allNodes[15] = 1;
            advertise6(allNodes);
        }
        udp_.pump(t);
        tcp_.pump(t);
        pumpForwards(t);
    }

    void nicReset() override
    {
        tcp_.closeAll();
        udp_.closeAll();
        dns_.clear();
        dns6_.clear();
        dnsGuests6_.clear();
        peers6_.clear();
        aliasOf6_.clear();
        for (auto& f : forwards_) f.peers.clear();     // the listeners stay
        fragments_  = Ipv4Reassembler{};
        fragments6_ = Ipv6Reassembler{};
        outbound_.clear();
        ipv6Active_ = false;
        guestIp_  = kGuestIp;           // relearned from the next frame
    }

    /// Every address is behind the gateway (it answers ARP for all of them),
    /// so an IPRAW frame for any destination is addressed to it.
    bool resolveMac(uint32_t, MacAddress& out) override
    {
        std::memcpy(out.b, kGatewayMac.data(), 6);
        return true;
    }

private:
    uint64_t now() const
    {
        if (nowMs_) return nowMs_();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    /// Every address but the guest's own resolves to the gateway, so traffic
    /// to any destination, on the virtual subnet's numbering or not, comes
    /// to the NAT. A probe (sender 0.0.0.0) or an announcement (sender =
    /// target) gets no reply, or the guest would read it as an address
    /// conflict (RFC 5227 § 2.1.1).
    void onArp(const EthernetFrame& frame)
    {
        ArpPacket request;
        if (!parseArp(frame.payload, request) || request.operation != kArpRequest) return;
        if (request.senderIp == 0 || request.senderIp == request.targetIp ||
            request.targetIp == guestIp_)
            return;
        ArpPacket reply;
        reply.operation = kArpReply;
        reply.senderMac = kGatewayMac;
        reply.senderIp  = request.targetIp;
        reply.targetMac = request.senderMac;
        reply.targetIp  = request.senderIp;
        queue(buildEthernet(request.senderMac, kGatewayMac, kEtherTypeArp, buildArp(reply)));
    }

    void onIpv4(const EthernetFrame& frame)
    {
        Ipv4Packet packet;
        const Bytes whole = fragments_.accept(frame.payload, now());
        if (whole.empty() || !parseIpv4({whole.data(), whole.size()}, packet)) return;
        // Reserved aliases are private relay identifiers, never IPv4 routes.
        if ((packet.destination & 0xffff0000) == 0xf0000000) return;
        // The guest's address is whatever it uses — a static configuration
        // need not be the DHCP lease.
        if (packet.source != 0) guestIp_ = packet.source;

        switch (packet.protocol) {
            case kProtoIcmp: onIcmp(packet); break;
            case kProtoUdp:  onUdp(packet);  break;
            case kProtoTcp: {
                TcpSegment segment;
                if (parseTcp(packet, segment)) tcp_.onGuestSegment(segment, now());
                break;
            }
            default: break;
        }
    }

    /// Echo requests to the gateway or the DNS address get echo replies;
    /// nothing else is relayed (a raw ICMP socket needs privileges).
    void onIcmp(const Ipv4Packet& packet)
    {
        if ((packet.destination != kGatewayIp && packet.destination != kDnsIp) ||
            packet.payload.size < kIcmpHeader || packet.payload.data[0] != kIcmpEchoRequest ||
            checksum(packet.payload.data, packet.payload.size) != 0)
            return;
        queueIpv4(packet.destination, packet.source, kProtoIcmp,
                  buildIcmpEchoReply(packet.payload));
    }

    void onUdp(const Ipv4Packet& packet)
    {
        UdpDatagram datagram;
        if (!parseUdp(packet, datagram)) return;
        for (auto& f : forwards_) {
            if (f.rule.tcp || datagram.sourcePort != f.rule.guestPort) continue;
            const auto key = std::make_pair(packet.destination, datagram.destinationPort);
            const auto peer = f.peers.find(key);
            if (peer != f.peers.end() && now() - peer->second.lastMs < UdpRelay::kIdleMs) {
                f.socket->sendTo(peer->second.host, datagram.payload.data, datagram.payload.size);
                peer->second.lastMs = now();
                return;
            }
        }

        // DHCP goes to the server here whatever its destination address
        // (the client has no address and broadcasts); the reply is
        // broadcast, since there is nothing to unicast it to yet.
        if (datagram.destinationPort == kDhcpServerPort && !peers6_.count(packet.destination)) {
            const DhcpLease lease{ kGuestIp, kSubnetMask, kGatewayIp, kDnsIp };
            const Bytes reply = dhcpReply(datagram.payload, lease);
            if (reply.empty()) return;
            const Bytes udp = buildUdp(kGatewayIp, 0xFFFFFFFF, kDhcpServerPort,
                                       kDhcpClientPort, reply.data(), reply.size());
            queue(buildEthernet(kBroadcastMac, kGatewayMac, kEtherTypeIpv4,
                                buildIpv4(kGatewayIp, 0xFFFFFFFF, kProtoUdp, nextIpId_++, udp)));
            return;
        }

        Endpoint host;
        switch (routePeer(packet.destination, datagram.destinationPort, false, host)) {
            case Route::Dns:
                dns_.query(datagram.sourcePort, datagram.payload, now(),
                           [this](uint16_t guestPort, const Bytes& reply) {
                               udpToGuest(kDnsIp, kDnsPort, guestPort, reply.data(),
                                          reply.size());
                           });
                break;
            case Route::Host:
                // A published UDP service keeps its configured host port in
                // both directions. Two LAN peers using the same fixed port
                // therefore see that port, rather than an ephemeral one.
                for (auto& f : forwards_) {
                    if (!f.rule.tcp && f.rule.guestPort == datagram.sourcePort && !host.isIpv6) {
                        f.socket->sendTo(host, datagram.payload.data, datagram.payload.size);
                        return;
                    }
                }
                udp_.sendFromGuest(datagram.sourcePort, host, packet.destination,
                                   datagram.payload, now());
                break;
            case Route::Refused:
                noteRefused(packet.destination, datagram.destinationPort, "UDP");
                break;
        }
    }

    /// Said once per session: a guest retrying a refused destination would
    /// otherwise fill the log, and the first line is the one that matters.
    void noteRefused(uint32_t destination, uint16_t port, const char* what)
    {
        const auto v6 = peers6_.find(destination);
        if (v6 != peers6_.end()) { noteRefused6(v6->second.first, port, what); return; }
        const uint8_t top = static_cast<uint8_t>(destination >> 24);
        noteRefused(dotted(destination), destination == kGatewayIp || top == 127, port, what);
    }
    void noteRefused6(const Ipv6& destination, uint16_t port, const char* what)
    {
        Ipv6 loopback{}; loopback[15] = 1;
        const bool mapped127 = std::all_of(destination.begin(), destination.begin() + 10,
                                           [](uint8_t x) { return x == 0; }) &&
                               destination[10] == 0xff && destination[11] == 0xff &&
                               destination[12] == 127;
        noteRefused(colons(destination), destination == kGateway6 || destination == loopback ||
                                             mapped127, port, what);
    }
    void noteRefused(const std::string& destination, bool loopback, uint16_t port, const char* what)
    {
        ++framesDropped_;
        if (refusalWarned_) return;
        refusalWarned_ = true;
        log().warn("Net", std::string("built-in NAT: guest ") + what + " to " +
                              destination + ":" + std::to_string(port) +
                              " refused" +
                              (loopback ? " — the host's loopback; set "
                                          "uthernet_allow_loopback to opt in"
                                        : "") +
                              " (said once)");
    }

    // ── GuestSink ─────────────────────────────────────────────────────────
    Route routePeer(uint32_t destination, uint16_t port, bool tcp, Endpoint& host)
    {
        const auto found = peers6_.find(destination);
        if (found != peers6_.end()) return routeDestination6(found->second.first, port, tcp, policy_, host);
        return routeDestination(destination, port, tcp, policy_, host);
    }

    void queue6(const Ipv6& source, const Ipv6& destination, uint8_t protocol, Bytes payload, uint8_t hop = 64)
    {
        const std::size_t sumAt = protocol == kProtoTcp ? 16 : protocol == kProtoUdp ? 6 : 2;
        if (payload.size() < sumAt + 2) return;
        wr16(payload.data() + sumAt, 0);
        auto sum = checksum6(source, destination, protocol, {payload.data(), payload.size()});
        wr16(payload.data() + sumAt, sum == 0 && protocol == kProtoUdp ? 0xffff : sum);
        Mac mac = guestMac_;
        if (destination[0] == 0xff) mac = {0x33,0x33,destination[12],destination[13],destination[14],destination[15]};
        if (hop != 64) queue(buildEthernet(mac, kGatewayMac, kEtherTypeIpv6, buildIpv6(source, destination, protocol, payload, hop)));
        else for (auto& p : fragmentIpv6(source, destination, protocol, nextIp6Id_++, payload))
            queue(buildEthernet(mac, kGatewayMac, kEtherTypeIpv6, p));
    }

    void emitDns6(uint16_t port, const Bytes& reply)
    {
        if (reply.size() < 12) return;
        const auto it = dnsGuests6_.find({port, rd16(reply.data())});
        if (it == dnsGuests6_.end()) return;
        auto udp = buildUdp(0, 0, 53, port, reply.data(), reply.size());
        queue6(kDns6, it->second.source, kProtoUdp, std::move(udp));
        dnsGuests6_.erase(it);
    }

    void onIpv6(const EthernetFrame& frame)
    {
        const Bytes whole = fragments6_.accept(frame.payload, now());
        Ipv6Packet ip;
        if (whole.empty() || !parseIpv6({whole.data(), whole.size()}, ip) || ip.source[0] == 0xff) return;
        if (!ipv6Active_) { ipv6Active_ = true; nextRaMs_ = now() + 200000; }
        if (ip.protocol == kProtoIcmp6) { onIcmp6(ip); return; }
        if ((ip.protocol != kProtoUdp && ip.protocol != kProtoTcp) || ip.source == Ipv6{} ||
            ip.payload.size < (ip.protocol == kProtoTcp ? 20u : 8u) ||
            checksum6(ip.source, ip.destination, ip.protocol, ip.payload)) return;
        if (ip.protocol == kProtoUdp && (!rd16(ip.payload.data + 6) || rd16(ip.payload.data + 4) != ip.payload.size)) return;
        if (ip.protocol == kProtoUdp && ip.destination == kDns6 && rd16(ip.payload.data + 2) == 53) {
            const auto port = rd16(ip.payload.data);
            View question{ip.payload.data + 8, ip.payload.size - 8};
            if (question.size < 12) return;
            // The entry must exist before query(): an answer from the cache
            // is emitted from inside it. A datagram query() ignores (a
            // response, QR=1) will never be answered, so its entry goes
            // again at once — or, if it collided with a query in flight, the
            // waiting guest's entry comes back as it was.
            const DnsGuestKey key{port, rd16(question.data)};
            const auto previous = dnsGuests6_.find(key);
            const bool fresh = previous == dnsGuests6_.end();
            if (fresh && dnsGuests6_.size() >= kMaxDnsGuests6) return;
            const DnsGuest6 kept = fresh ? DnsGuest6{} : previous->second;
            dnsGuests6_[key] = {ip.source, now() + kDnsGuest6Ms};
            if (!dns6_.query(port, question, now(), [this](uint16_t p, const Bytes& r) { emitDns6(p, r); })) {
                if (fresh) dnsGuests6_.erase(key);
                else       dnsGuests6_[key] = kept;
            }
            return;
        }
        // A UDP destination the fence refuses spends no alias: it is dropped
        // here. A refused TCP one takes an alias, for the reset to come back
        // under — nothing names it afterwards, so the next sweep reclaims it.
        if (ip.protocol == kProtoUdp) {
            Endpoint host;
            const uint16_t port = rd16(ip.payload.data + 2);
            if (routeDestination6(ip.destination, port, false, policy_, host) == Route::Refused) {
                noteRefused6(ip.destination, port, "UDP");
                return;
            }
        }
        const uint32_t alias = aliasFor6({ip.destination, ip.source});
        if (!alias) { ++framesDropped_; return; }
        // Adapt only the addresses/checksum at the relay seam. TCP's sequence
        // space, persist/retransmit state and UDP filtering are shared with v4.
        Bytes transport(ip.payload.data, ip.payload.data + ip.payload.size);
        const auto sumAt = ip.protocol == kProtoTcp ? 16 : 6;
        wr16(transport.data() + sumAt, 0);
        uint32_t sum = (kGuestIp >> 16) + (kGuestIp & 0xffff) + (alias >> 16) + (alias & 0xffff) + ip.protocol + transport.size();
        auto c = checksum(transport.data(), transport.size(), sum);
        wr16(transport.data() + sumAt, c == 0 && ip.protocol == kProtoUdp ? 0xffff : c);
        Ipv4Packet translated{kGuestIp, alias, ip.protocol, {transport.data(), transport.size()}};
        if (ip.protocol == kProtoUdp) onUdp(translated);
        else {
            TcpSegment s;
            if (parseTcp(translated, s)) {
                s.mss = std::min<uint16_t>(s.mss ? s.mss : 1220, 1440);
                tcp_.onGuestSegment(s, now());
            }
        }
    }

    uint32_t aliasFor6(const std::pair<Ipv6, Ipv6>& pair)
    {
        const auto known = aliasOf6_.find(pair);
        if (known != aliasOf6_.end()) return known->second;
        if (peers6_.size() >= kMaxAliases6) reclaimAliases6();
        if (peers6_.size() >= kMaxAliases6) return 0;      // cannot happen: see kMaxAliases6
        // The range holds far more numbers than the table, so this ends;
        // rolling on rather than restarting keeps a number just reclaimed
        // out of use for as long as possible.
        do nextAlias6_ = nextAlias6_ >= kAliasLast ? kAliasFirst : nextAlias6_ + 1;
        while (peers6_.count(nextAlias6_));
        peers6_[nextAlias6_] = pair;
        aliasOf6_[pair] = nextAlias6_;
        return nextAlias6_;
    }

    /// Drop every alias no relay names any more.
    void reclaimAliases6()
    {
        if (peers6_.empty()) return;
        std::set<uint32_t> named;
        const auto note = [&](uint32_t address) { if (peers6_.count(address)) named.insert(address); };
        udp_.forEachShown(note);
        tcp_.forEachPeer(note);
        for (auto it = peers6_.begin(); it != peers6_.end();) {
            if (named.count(it->first)) { ++it; continue; }
            aliasOf6_.erase(it->second);
            it = peers6_.erase(it);
        }
    }

    void onIcmp6(const Ipv6Packet& ip)
    {
        if (ip.payload.size < 8 || checksum6(ip.source, ip.destination, kProtoIcmp6, ip.payload)) return;
        const auto* p = ip.payload.data;
        if (p[1]) return;
        if (p[0] == 128 && (ip.destination == kGateway6 || ip.destination == kDns6 || ip.destination == kRouter6)) {
            Bytes reply(p, p + ip.payload.size); reply[0] = 129;
            queue6(ip.destination, ip.source, kProtoIcmp6, std::move(reply));
            return;
        }
        // [B] RFC 4861 §§ 4.1–4.4, 7.1: ND requires hop limit 255.
        if (ip.hopLimit != 255 || (p[0] != 133 && p[0] != 135)) return;
        const auto minimum = p[0] == 133 ? 8u : 24u;
        if (ip.payload.size < minimum) return;
        for (std::size_t at = minimum; at < ip.payload.size;) {
            if (at + 2 > ip.payload.size || p[at + 1] == 0 || at + p[at + 1] * 8 > ip.payload.size) return;
            if (ip.source == Ipv6{} && p[at] == 1) return;
            at += p[at + 1] * 8;
        }
        Ipv6 allNodes{}; allNodes[0] = 0xff; allNodes[1] = 2; allNodes[15] = 1;
        const auto destination = ip.source == Ipv6{} ? allNodes : ip.source;
        if (p[0] == 135) {
            Ipv6 target{}; std::copy_n(p + 8, 16, target.begin());
            if (target != kGateway6 && target != kDns6 && target != kRouter6) return;
            Bytes reply(32); reply[0] = 136;
            wr32(reply.data() + 4, ip.source == Ipv6{} ? 0xa0000000 : 0xe0000000);
            std::copy(target.begin(), target.end(), reply.begin() + 8);
            reply[24] = 2; reply[25] = 1;
            std::copy(kGatewayMac.begin(), kGatewayMac.end(), reply.begin() + 26);
            queue6(target, destination, kProtoIcmp6, std::move(reply), 255);
        } else advertise6(destination);
    }

    void advertise6(const Ipv6& destination)
    {
        nextRaMs_ = now() + 200000;
        Bytes reply(80); reply[0] = 134; reply[4] = 64;
        wr16(reply.data() + 6, 1800); // default router lifetime
        reply[16] = 1; reply[17] = 1;
        std::copy(kGatewayMac.begin(), kGatewayMac.end(), reply.begin() + 18);
        reply[24] = 3; reply[25] = 4; reply[26] = 64; reply[27] = 0xc0;
        wr32(reply.data() + 28, 86400); wr32(reply.data() + 32, 14400);
        std::copy_n(kGateway6.begin(), 8, reply.begin() + 40);
        reply[56] = 25; reply[57] = 3; wr32(reply.data() + 60, 1800);
        std::copy(kDns6.begin(), kDns6.end(), reply.begin() + 64);
        queue6(kRouter6, destination, kProtoIcmp6, std::move(reply), 255);
    }

    void pumpForwards(uint64_t t)
    {
        // No SYN is emitted until a guest frame has supplied its MAC.
        if (guestMac_ == kBroadcastMac) return;
        for (auto& f : forwards_) {
            for (int n = 0; n < 16; ++n) {
                Endpoint peer;
                if (f.rule.tcp) {
                    auto socket = f.socket->accept(peer);
                    if (!socket) break;
                    if (peer.ip >> 24 == 127) peer.ip = kGatewayIp;
                    tcp_.acceptInbound(f.rule.guestPort, peer, std::move(socket), t);
                } else {
                    forwardBuffer_.resize(65508);
                    std::size_t size = 0;
                    if (!f.socket->receiveFrom(forwardBuffer_.data(), forwardBuffer_.size(), size, peer)) break;
                    if (size > 65507) continue;
                    const auto shown = peer.ip >> 24 == 127 ? kGatewayIp : peer.ip;
                    if (f.peers.size() >= 32 && !f.peers.count({shown, peer.port})) f.peers.erase(f.peers.begin());
                    f.peers[{shown, peer.port}] = {peer, t};
                    udpToGuest(shown, peer.port, f.rule.guestPort, forwardBuffer_.data(), size);
                }
            }
            for (auto it = f.peers.begin(); it != f.peers.end();)
                if (t - it->second.lastMs >= UdpRelay::kIdleMs) it = f.peers.erase(it);
                else ++it;
        }
    }

    void udpToGuest(uint32_t sourceIp, uint16_t sourcePort, uint16_t guestPort,
                    const uint8_t* data, std::size_t size) override
    {
        const auto peer = peers6_.find(sourceIp);
        if (peer != peers6_.end()) {
            queue6(peer->second.first, peer->second.second, kProtoUdp,
                   buildUdp(0, 0, sourcePort, guestPort, data, size));
            return;
        }
        queueIpv4(sourceIp, guestIp_, kProtoUdp,
                  buildUdp(sourceIp, guestIp_, sourcePort, guestPort, data, size));
    }

    void tcpToGuest(const TcpSegment& segment) override
    {
        const auto peer = peers6_.find(segment.sourceIp);
        if (peer != peers6_.end()) {
            auto s = segment;
            if (s.mss) s.mss = std::min<uint16_t>(s.mss, 1440);
            queue6(peer->second.first, peer->second.second, kProtoTcp, buildTcp(s));
            return;
        }
        queueIpv4(segment.sourceIp, segment.destinationIp, kProtoTcp, buildTcp(segment));
    }

    uint32_t guestIp() const override { return guestIp_; }

    void queueIpv4(uint32_t source, uint32_t destination, uint8_t protocol,
                   const Bytes& payload)
    {
        for (auto& p : fragmentIpv4(source, destination, protocol, nextIpId_++, payload))
            queue(buildEthernet(guestMac_, kGatewayMac, kEtherTypeIpv4, p));
    }

    void queue(Bytes frame)
    {
        if (frame.size() > static_cast<std::size_t>(kMaxEthFrame) ||
            outbound_.size() >= kMaxQueued) {
            ++framesDropped_;
            return;
        }
        outbound_.push_back(std::move(frame));
    }

    RoutePolicy                    policy_;
    std::function<uint64_t()>      nowMs_;
    std::unique_ptr<HostNetwork>   network_;
    std::unique_ptr<W5100Resolver> resolver_;
    DnsProxy                       dns_;
    DnsProxy                       dns6_;
    UdpRelay                       udp_;
    TcpRelay                       tcp_;
    Ipv4Reassembler                fragments_;
    Ipv6Reassembler                fragments6_;
    std::map<uint32_t, std::pair<Ipv6, Ipv6>> peers6_;      ///< alias -> (peer, guest)
    std::map<std::pair<Ipv6, Ipv6>, uint32_t> aliasOf6_;    ///< the reverse
    uint32_t nextAlias6_ = kAliasLast;                      ///< the first one taken is kAliasFirst
    uint64_t nextAliasSweepMs_ = 0;
    using DnsGuestKey = std::pair<uint16_t, uint16_t>;      ///< guest port, DNS id
    struct DnsGuest6 { Ipv6 source{}; uint64_t expiresMs = 0; };
    std::map<DnsGuestKey, DnsGuest6> dnsGuests6_;
    uint32_t nextIp6Id_ = 0;
    uint64_t nextRaMs_ = 0;
    bool ipv6Active_ = false;
    struct Forward {
        BuiltinNatOptions::PortForward rule;
        std::unique_ptr<HostSocket> socket;
        struct Peer { Endpoint host; uint64_t lastMs; };
        std::map<std::pair<uint32_t, uint16_t>, Peer> peers;
    };
    std::vector<Forward> forwards_;
    Bytes forwardBuffer_;
    std::deque<Bytes>              outbound_;
    Mac                            guestMac_ = kBroadcastMac;
    uint32_t                       guestIp_  = kGuestIp;
    uint16_t                       nextIpId_ = 0;
    bool                           refusalWarned_ = false;
    bool                           valid_ = true;
    std::string                    name_;
};

}  // namespace

bool builtinNatAvailable()
{
    return POM2_HAS_SOCKETS != 0;
}

std::unique_ptr<NetworkBackend> makeBuiltinNatBackend(
    const BuiltinNatOptions& options, std::unique_ptr<nat::HostNetwork> network,
    std::unique_ptr<W5100Resolver> resolver)
{
    if (!network) network = nat::makeHostNetwork();
    if (!network) return nullptr;
    auto configured = options;
    if (!resolver) {
        resolver = std::make_unique<W5100NameResolver>();
        if (!configured.dnsLookup) configured.dnsLookup = nat::queryHostDns;
    }
    return std::make_unique<BuiltinNat>(configured, std::move(network), std::move(resolver));
}

}  // namespace pom2
