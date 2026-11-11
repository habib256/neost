// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatUdp — see the header.

#include "NatUdp.h"

namespace pom2 {
namespace nat {

UdpRelay::Flow* UdpRelay::flowFor(uint16_t guestPort, uint64_t nowMs, bool ipv6)
{
    const uint32_t key = guestPort | (ipv6 ? 0x10000 : 0);
    const auto found = flows_.find(key);
    if (found != flows_.end()) return &found->second;
    if (!network_) return nullptr;

    if (flows_.size() >= kMaxFlows) {
        auto oldest = flows_.begin();
        for (auto it = flows_.begin(); it != flows_.end(); ++it)
            if (it->second.lastMs < oldest->second.lastMs) oldest = it;
        flows_.erase(oldest);
    }
    std::unique_ptr<HostSocket> socket = ipv6 ? network_->openUdp6() : network_->openUdp();
    if (!socket) return nullptr;
    Flow& flow  = flows_[key];
    flow.socket = std::move(socket);
    flow.lastMs = nowMs;
    return &flow;
}

void UdpRelay::sendFromGuest(uint16_t guestPort, const Endpoint& host, uint32_t shownAs,
                             View payload, uint64_t nowMs)
{
    // A datagram that cannot be sent — no host network, no socket — is
    // dropped, as UDP permits.
    Flow* flow = flowFor(guestPort, nowMs, host.isIpv6);
    if (!flow) return;
    if (flow->peers.size() >= kMaxPeers && flow->peers.find(host.ip) == flow->peers.end())
        flow->peers.clear();
    if (host.isIpv6) {
        if (flow->peers6.size() >= kMaxPeers && !flow->peers6.count(host.ipv6)) flow->peers6.clear();
        flow->peers6[host.ipv6] = shownAs;
    } else flow->peers[host.ip] = shownAs;
    flow->lastMs = nowMs;
    flow->socket->sendTo(host, payload.data, payload.size);
}

void UdpRelay::pump(uint64_t nowMs)
{
    // One byte more than the largest deliverable payload, so a datagram that
    // is too long is SEEN to be too long instead of arriving truncated.
    buffer_.resize(kMaxPayload + 1);
    for (auto it = flows_.begin(); it != flows_.end();) {
        Flow& flow = it->second;
        // At most kMaxPerPump per flow, so one busy peer cannot bury the
        // guest's frame queue between two of its reads.
        for (int n = 0; n < kMaxPerPump; ++n) {
            std::size_t received = 0;
            Endpoint    from;
            if (!flow.socket->receiveFrom(buffer_.data(), buffer_.size(), received, from))
                break;
            uint32_t shownAs = 0;
            if (from.isIpv6) {
                const auto peer = flow.peers6.find(from.ipv6);
                if (peer != flow.peers6.end()) shownAs = peer->second;
            } else {
                const auto peer = flow.peers.find(from.ip);
                if (peer != flow.peers.end()) shownAs = peer->second;
            }
            if (!shownAs) continue;
            if (received > kMaxPayload) continue;         // exceeds the IP datagram bound
            sink_.udpToGuest(shownAs, from.port, static_cast<uint16_t>(it->first), buffer_.data(), received);
            flow.lastMs = nowMs;
        }
        if (nowMs - flow.lastMs > kIdleMs) it = flows_.erase(it);
        else                               ++it;
    }
}

}  // namespace nat
}  // namespace pom2
