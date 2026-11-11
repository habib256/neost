// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatHostSockets — the host's real sockets behind NatHost.h, over
// SocketCompat.h (POSIX and Winsock from one source).
//
// Every trap this file steps around is recorded where it was first met:
// SocketCompat.h (traps 3, 5, 7) and W5100HostSockets.cpp. The two that
// matter most here: SIGPIPE is fatal by default and this code writes to peers
// that vanish (disableSigpipe + sendSocket), and on a datagram socket an
// error can describe one PACKET rather than the socket (errDatagramDiscard).

#include "NatHost.h"

#include "Logger.h"
#include "SocketCompat.h"
#include "SocketUtil.h"
#include <cstring>
#include <chrono>
#include <algorithm>
#if POM2_HAS_SOCKETS
#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <resolv.h>
#endif
#endif

namespace pom2 {
namespace nat {

#if POM2_HAS_SOCKETS

namespace {

sockaddr_in toSockaddr(const Endpoint& e)
{
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(e.ip);
    a.sin_port        = hostToNet16(e.port);
    return a;
}
sockaddr_in6 toSockaddr6(const Endpoint& e)
{
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_port = hostToNet16(e.port);
    std::memcpy(&a.sin6_addr, e.ipv6.data(), 16);
    return a;
}

class RealSocket final : public HostSocket
{
public:
    explicit RealSocket(socket_t fd) : fd_(fd) {}
    ~RealSocket() override
    {
        if (isValidSocket(fd_)) closeHostSocket(fd_);
    }
    RealSocket(const RealSocket&) = delete;
    RealSocket& operator=(const RealSocket&) = delete;

    bool connect(const Endpoint& remote) override
    {
        const auto a = toSockaddr(remote);
        const auto a6 = toSockaddr6(remote);
        const auto* address = remote.isIpv6 ? reinterpret_cast<const sockaddr*>(&a6) : reinterpret_cast<const sockaddr*>(&a);
        if (::connect(fd_, address, remote.isIpv6 ? sizeof(a6) : sizeof(a)) == 0)
            return true;
        const int e = lastSocketError();
        // Non-blocking: "in progress" is the normal answer, not a failure.
        return errInProgress(e) || errWouldBlock(e);
    }

    ConnectState pollConnect() override
    {
        if (waitSocket(fd_, SocketWait::Write, 0) != WaitResult::Ready)
            return ConnectState::Pending;
        return connectResult(fd_) == 0 ? ConnectState::Connected : ConnectState::Failed;
    }

    IoStatus send(const uint8_t* data, std::size_t size, std::size_t& sent) override
    {
        sent = 0;
        const iolen_t n = sendSocket(fd_, data, size);
        if (n >= 0) { sent = static_cast<std::size_t>(n); return IoStatus::Ok; }
        const int e = lastSocketError();
        return (errWouldBlock(e) || errInterrupted(e)) ? IoStatus::WouldBlock
                                                       : IoStatus::Failed;
    }

    IoStatus receive(uint8_t* buffer, std::size_t capacity, std::size_t& received) override
    {
        received = 0;
        const iolen_t n = recvSocket(fd_, buffer, capacity);
        if (n > 0) { received = static_cast<std::size_t>(n); return IoStatus::Ok; }
        if (n == 0) return IoStatus::Closed;
        const int e = lastSocketError();
        return (errWouldBlock(e) || errInterrupted(e)) ? IoStatus::WouldBlock
                                                       : IoStatus::Failed;
    }

    void shutdownSend() override
    {
#ifdef _WIN32
        ::shutdown(fd_, SD_SEND);
#else
        ::shutdown(fd_, SHUT_WR);
#endif
    }

    std::unique_ptr<HostSocket> accept(Endpoint& peer) override
    {
        sockaddr_in a{};
        socklen_c size = sizeof(a);
        auto fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&a), &size);
        if (!isValidSocket(fd)) return nullptr;
        disableSigpipe(fd);
        if (!setNonBlocking(fd)) { closeHostSocket(fd); return nullptr; }
        peer = {ntohl(a.sin_addr.s_addr), netToHost16(a.sin_port)};
        return std::make_unique<RealSocket>(fd);
    }

    bool sendTo(const Endpoint& remote, const uint8_t* data, std::size_t size) override
    {
        const auto a = toSockaddr(remote);
        const auto a6 = toSockaddr6(remote);
        const auto* address = remote.isIpv6 ? reinterpret_cast<const sockaddr*>(&a6) : reinterpret_cast<const sockaddr*>(&a);
        return sendToSocket(fd_, data, size, address, remote.isIpv6 ? sizeof(a6) : sizeof(a)) >= 0;
    }

    bool receiveFrom(uint8_t* buffer, std::size_t capacity, std::size_t& received,
                     Endpoint& from) override
    {
        // A datagram that fails as a datagram (oversized; somebody else's
        // ICMP port-unreachable surfacing on the NEXT recvfrom under Winsock)
        // costs that datagram, not the flow: look at the next one. Bounded,
        // so a socket that only ever errors cannot hold the CPU thread.
        for (int attempt = 0; attempt < 4; ++attempt) {
            sockaddr_storage source{};
            socklen_c   sourceLength = sizeof(source);
            const iolen_t n = ::recvfrom(fd_, reinterpret_cast<char*>(buffer),
                                         static_cast<int>(capacity), 0,
                                         reinterpret_cast<sockaddr*>(&source), &sourceLength);
            if (n >= 0) {
                received  = static_cast<std::size_t>(n);
                from = {};
                if (source.ss_family == AF_INET6) {
                    const auto* a = reinterpret_cast<const sockaddr_in6*>(&source);
                    from.isIpv6 = true;
                    from.port = netToHost16(a->sin6_port);
                    std::memcpy(from.ipv6.data(), &a->sin6_addr, 16);
                } else {
                    const auto* a = reinterpret_cast<const sockaddr_in*>(&source);
                    from.ip = ntohl(a->sin_addr.s_addr);
                    from.port = netToHost16(a->sin_port);
                }
                return true;
            }
            if (!errDatagramDiscard(lastSocketError())) return false;
        }
        return false;
    }

private:
    socket_t fd_ = kInvalidSocket;
};

class RealNetwork final : public HostNetwork
{
public:
    std::unique_ptr<HostSocket> openTcp() override { return open(kSockStream, kIpProtoTcp); }

    std::unique_ptr<HostSocket> openUdp() override
    {
        return open(kSockDgram, kIpProtoUdp);
    }
    std::unique_ptr<HostSocket> openTcp6() override { return open(kSockStream, kIpProtoTcp, AF_INET6); }
    std::unique_ptr<HostSocket> openUdp6() override { return open(kSockDgram, kIpProtoUdp, AF_INET6); }
    std::unique_ptr<HostSocket> listenTcp(const Endpoint& local) override
    {
        return bound(local, kSockStream, kIpProtoTcp);
    }
    std::unique_ptr<HostSocket> bindUdp(const Endpoint& local) override
    {
        return bound(local, kSockDgram, kIpProtoUdp);
    }

private:
    static std::unique_ptr<HostSocket> bound(const Endpoint& local, int type, int protocol)
    {
        ensureSocketStack();
        auto fd = ::socket(AF_INET, type, protocol);
        if (!isValidSocket(fd)) return nullptr;
        disableSigpipe(fd);
        const auto a = toSockaddr(local);
        bool bindPolicy = true;
#ifdef _WIN32
        bindPolicy = setListenerBindPolicy(fd); // exclusive TCP and UDP ownership
#else
        if (type == kSockStream) bindPolicy = setListenerBindPolicy(fd);
#endif
        if (!setNonBlocking(fd) || !bindPolicy ||
            ::bind(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 ||
            (type == kSockStream && ::listen(fd, 8) != 0)) {
            closeHostSocket(fd); return nullptr;
        }
        return std::make_unique<RealSocket>(fd);
    }
    static std::unique_ptr<HostSocket> open(int type, int protocol, int family = AF_INET)
    {
        ensureSocketStack();
        socket_t fd = ::socket(family, type, protocol);
        if (!isValidSocket(fd)) return nullptr;
        disableSigpipe(fd);
        if (!setNonBlocking(fd)) { closeHostSocket(fd); return nullptr; }
        if (type == kSockDgram) {
            disableUdpConnReset(fd);
            // An explicit bind to port 0 so the socket has its ephemeral
            // port BEFORE the first send: Winsock refuses recvfrom on a
            // datagram socket that was never bound (WSAEINVAL).
            Endpoint any;
            const auto local = toSockaddr(any);
            const auto local6 = toSockaddr6(any);
            const auto* address = family == AF_INET6 ? reinterpret_cast<const sockaddr*>(&local6) : reinterpret_cast<const sockaddr*>(&local);
            if (::bind(fd, address, family == AF_INET6 ? sizeof(local6) : sizeof(local)) != 0) {
                closeHostSocket(fd);
                return nullptr;
            }
        }
        return std::make_unique<RealSocket>(fd);
    }
};

}  // namespace

std::unique_ptr<HostNetwork> makeHostNetwork()
{
    return std::make_unique<RealNetwork>();
}

Bytes queryHostDns(const Bytes& query)
{
    if (query.size() < 12 || query.size() > 512) return {};
    Bytes answer(65535);
#ifndef _WIN32
    // Per-worker state: resolver retries and TCP fallback never run under
    // the emulator lock. The system's resolver selects the DNS servers.
    struct __res_state state{};
    if (res_ninit(&state) != 0) return {};
    state.retrans = 1;
    state.retry = 2;
    const int n = res_nsend(&state, query.data(), static_cast<int>(query.size()),
                             answer.data(), static_cast<int>(answer.size()));
    // Apple's libresolv (BIND 9's, like FreeBSD's) splits the teardown:
    // res_nclose only closes the sockets, res_ndestroy also frees what
    // res_ninit allocated. With res_nclose every lookup leaked ~250 bytes —
    // measured +49 MB over 200 000 lookups (NeoST audit, 2026-11-11). glibc
    // has no res_ndestroy; its res_nclose frees what its res_ninit took.
#if defined(__APPLE__) || defined(__FreeBSD__)
    res_ndestroy(&state);
#else
    res_nclose(&state);
#endif
    if (n < 12 || n > static_cast<int>(answer.size())) return {};
    answer.resize(static_cast<std::size_t>(n));
    return answer;
#else
    ensureSocketStack();
    auto tcpQuery = [&](const sockaddr_in& server) -> Bytes {
        auto fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!isValidSocket(fd)) return {};
        disableSigpipe(fd);
        if (!setNonBlocking(fd)) { closeHostSocket(fd); return {}; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        const int rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&server), sizeof(server));
        if (rc != 0 && !errInProgress(lastSocketError()) && !errWouldBlock(lastSocketError())) {
            closeHostSocket(fd); return {};
        }
        if (waitSocket(fd, SocketWait::Write, 1000) != WaitResult::Ready || connectResult(fd) != 0) {
            closeHostSocket(fd); return {};
        }
        auto transfer = [&](uint8_t* data, std::size_t length, bool write) {
            std::size_t at = 0;
            while (at < length && std::chrono::steady_clock::now() < deadline) {
                if (waitSocket(fd, write ? SocketWait::Write : SocketWait::Read, 100) != WaitResult::Ready) continue;
                const auto n = write ? sendSocket(fd, data + at, length - at) : recvSocket(fd, data + at, length - at);
                if (n <= 0) {
                    if (n < 0 && (errWouldBlock(lastSocketError()) || errInterrupted(lastSocketError()))) continue;
                    return false;
                }
                at += static_cast<std::size_t>(n);
            }
            return at == length;
        };
        Bytes wire(2 + query.size()); wr16(wire.data(), static_cast<uint16_t>(query.size()));
        std::copy(query.begin(), query.end(), wire.begin() + 2);
        uint8_t length[2]{};
        Bytes reply;
        if (transfer(wire.data(), wire.size(), true) && transfer(length, 2, false) && rd16(length) >= 12) {
            reply.resize(rd16(length));
            if (!transfer(reply.data(), reply.size(), false)) reply.clear();
        }
        closeHostSocket(fd);
        return reply;
    };
    ULONG size = sizeof(FIXED_INFO);
    Bytes info(size);
    auto* fixed = reinterpret_cast<FIXED_INFO*>(info.data());
    if (GetNetworkParams(fixed, &size) == ERROR_BUFFER_OVERFLOW) {
        info.resize(size); fixed = reinterpret_cast<FIXED_INFO*>(info.data());
    }
    if (GetNetworkParams(fixed, &size) != NO_ERROR) return {};
    int tries = 0;
    for (auto* dns = &fixed->DnsServerList; dns && tries++ < 4; dns = dns->Next) {
        sockaddr_in server{};
        server.sin_family = AF_INET;
        server.sin_port = hostToNet16(53);
        if (::inet_pton(AF_INET, dns->IpAddress.String, &server.sin_addr) != 1) continue;
        auto fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (!isValidSocket(fd)) continue;
        if (::connect(fd, reinterpret_cast<sockaddr*>(&server), sizeof(server)) != 0 ||
            sendSocket(fd, query.data(), query.size()) < 0 ||
            waitSocket(fd, SocketWait::Read, 1000) != WaitResult::Ready) {
            closeHostSocket(fd); continue;
        }
        auto n = recvSocket(fd, answer.data(), answer.size());
        closeHostSocket(fd);
        if (n < 12 || rd16(answer.data()) != rd16(query.data())) continue;
        if (answer[2] & 2) {
            auto reply = tcpQuery(server);
            if (!reply.empty()) return reply;
            continue;
        }
        answer.resize(static_cast<std::size_t>(n));
        return answer;
    }
    return {};
#endif
}

#else   // no sockets in this build (the browser)

std::unique_ptr<HostNetwork> makeHostNetwork() { return nullptr; }
Bytes queryHostDns(const Bytes&) { return {}; }

#endif

}  // namespace nat
}  // namespace pom2
