// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

// Small shared socket idioms for the two TCP workers (AiControlServer
// HTTP + SuperSerialCard telnet bridge). Two hard-won rules live here so
// they are fixed ONCE:
//
//   1. poll()-before-accept: `shutdown(listenFd, SHUT_RDWR)` wakes a
//      blocking accept() on Linux but NOT on macOS/BSD — a worker parked
//      in accept() there never returns and the owner hangs in join().
//      This deadlock was found and fixed in AiControlServer, then
//      re-discovered months later in SuperSerialCard (whose destructor
//      runs under stateMutex during profile switches — a UI-thread hang).
//      Windows is a third variant of the same problem: closesocket() on a
//      listener does wake a blocked accept(), but the handle is then free
//      to be recycled by the next socket() on another thread, so the
//      wait-then-accept shape is what makes the shutdown sequence safe
//      everywhere rather than on Linux only.
//
//   2. SIGPIPE-proof send: a peer that vanished uncleanly must surface as
//      an error return, not kill the process. Linux spells that
//      MSG_NOSIGNAL; macOS/BSD have no such flag — there the socket needs
//      SO_NOSIGPIPE set once after accept (disableSigpipe). Windows has no
//      SIGPIPE at all, so both are no-ops there.
//
// The POSIX-vs-Winsock differences themselves are NOT here — they live in
// SocketCompat.h, which this header is written against. Not used by the
// Emscripten build (no sockets there); both includers guard their network
// paths with #if POM2_HAS_SOCKETS.

#ifndef POM2_SOCKET_UTIL_H
#include "Pom2Build.h"
#define POM2_SOCKET_UTIL_H

#if POM2_HAS_SOCKETS

#include "SocketCompat.h"
#include "ThreadGuard.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

namespace pom2 {

enum class PollAccept {
    Accepted,   ///< outFd holds a connected client
    Retry,      ///< timeout / EINTR / EAGAIN — re-check your stop flag, loop
    Shutdown,   ///< listen fd gone (owner shut it down) — exit the worker
};

/// One iteration of the wait-then-accept idiom (rule 1 above). Waits on
/// `listenFd` for up to `timeoutMs`, then accepts. The caller loops on
/// Retry so its stop flag is re-checked at least every `timeoutMs`.
inline PollAccept pollAcceptOnce(socket_t listenFd, int timeoutMs,
                                 socket_t& outFd, sockaddr_in& peer)
{
    outFd = kInvalidSocket;
    if (!isValidSocket(listenFd)) return PollAccept::Shutdown;

    switch (waitSocket(listenFd, SocketWait::Read, timeoutMs)) {
    case WaitResult::Timeout:  return PollAccept::Retry;
    case WaitResult::Failed:   return PollAccept::Shutdown;
    case WaitResult::Ready:    break;
    }

    // The listener must not block here. On BSD stacks (macOS) a queued
    // connection reset between poll() and accept() is dropped from the
    // queue, and a blocking accept() then waits for the NEXT client (UNP
    // § 16.6) — and shutdown() does not wake it there, so the owner's stop()
    // joined for ever, under stateMutex for the FujiNet card. Non-blocking,
    // that case is EWOULDBLOCK → Retry. An accepted socket inherits
    // O_NONBLOCK on BSD and Winsock, so it is put back to blocking: callers
    // that want non-blocking set it themselves.
    (void)setNonBlocking(listenFd);
    socklen_c peerLen = sizeof(peer);
    const socket_t fd = ::accept(listenFd,
                                 reinterpret_cast<sockaddr*>(&peer), &peerLen);
    if (isValidSocket(fd)) (void)setBlocking(fd);
    if (!isValidSocket(fd)) {
        const int e = lastSocketError();
        if (errInterrupted(e) || errWouldBlock(e)) return PollAccept::Retry;
        // Not every accept() failure means the LISTENER is gone. An aborted
        // client (any local process can produce one with connect +
        // SO_LINGER{1,0} + close) or a momentary descriptor shortage used to
        // retire the accept loop permanently: the worker thread returned, the
        // listener stayed bound with its backlog filling, and the owner still
        // reported itself as running. Both callers — the AI control server and
        // the SSC telnet bridge — then accepted nothing, for ever, with no
        // error anywhere to point at.
        if (errTransientAccept(e)) {
            // Back off on resource exhaustion: the pending connection is still
            // queued, so the next poll is readable immediately and a bare
            // retry would spin a core until the pressure lifts.
            if (errAcceptExhaustion(e))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return PollAccept::Retry;
        }
        return PollAccept::Shutdown;   // listening socket closed under us
    }
    outFd = fd;
    return PollAccept::Accepted;
}

/// Arm rule 2 on a fresh client socket. No-op where MSG_NOSIGNAL covers it
/// (Linux) or where SIGPIPE does not exist (Windows).
inline void disableSigpipe(socket_t fd)
{
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
#endif
}

/// send() that cannot raise SIGPIPE (rule 2). Pair with disableSigpipe()
/// on platforms without MSG_NOSIGNAL. Thin alias kept so the two workers
/// read the same as before the Winsock port.
inline iolen_t sendNoSignal(socket_t fd, const void* buf, std::size_t n)
{
    return sendSocket(fd, buf, n);
}


// ─── Bounded resolve + connect ───────────────────────────────────────────
//
// getaddrinfo() blocks for as long as the resolver chain takes (resolv.conf
// timeout × attempts × servers — tens of seconds against an unreachable DNS
// server), and a blocking ::connect() is NOT bounded by SO_SNDTIMEO:
// measured 2026-08-21 on macOS against 192.0.2.1 (TEST-NET-1, swallows
// SYNs), connect() returned after 75 s with the option asking for 8 (see
// DEV.md § FujiNet). Both run on threads a user is watching, so they get
// hard deadlines here. Shared by FujiNetNetDevice and TnfsClient — the
// bounded pair was born in FujiNetNetDevice.cpp and moved here when the
// TNFS mount was found still trusting the SO_SNDTIMEO myth.

/// A DNS lookup that cannot outlive the deadline: the lookup runs on its
/// own guarded thread and is ABANDONED if it overruns — the thread frees
/// its own result and exits whenever the resolver finally answers.
struct BoundedLookup {
    std::mutex mtx;
    bool       abandoned = false;
    addrinfo*  res       = nullptr;
};

inline bool resolveBounded(const std::string& host, const std::string& portStr,
                           int timeoutMs, addrinfo** out,
                           int socktype = SOCK_STREAM,
                           int protocol = IPPROTO_TCP)
{
    *out = nullptr;
    if (timeoutMs <= 0) return false;

    auto shared = std::make_shared<BoundedLookup>();
    std::promise<bool> ready;
    std::future<bool>  fut = ready.get_future();

    // Guarded: an exception escaping a detached thread calls std::terminate().
    // The promise needs the same care — leaving it unset makes the waiter's
    // fut.get() below throw future_error on the CALLING thread, so a lookup
    // that dies has to answer "false" rather than answer nothing. `settled`
    // also covers the abandoned path, where by contract nobody is waiting and
    // the promise is deliberately left alone.
    std::thread([shared, host, portStr, socktype, protocol,
                 p = std::move(ready)]() mutable {
        bool settled = false;
        pom2::runGuarded("SockResolve", [&] {
            addrinfo hints{};
            hints.ai_family   = AF_INET;
            hints.ai_socktype = socktype;
            hints.ai_protocol = protocol;
            addrinfo* r  = nullptr;
            const bool ok = (::getaddrinfo(host.c_str(), portStr.c_str(),
                                           &hints, &r) == 0) && r;

            std::lock_guard<std::mutex> lk(shared->mtx);
            if (shared->abandoned) {           // nobody is waiting any more
                if (r) ::freeaddrinfo(r);
                settled = true;
                return;
            }
            shared->res = r;
            p.set_value(ok);
            settled = true;
        });
        if (!settled) p.set_value(false);
    }).detach();

    if (fut.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready) {
        std::lock_guard<std::mutex> lk(shared->mtx);
        shared->abandoned = true;
        if (shared->res) { ::freeaddrinfo(shared->res); shared->res = nullptr; }
        return false;
    }

    const bool ok = fut.get();
    std::lock_guard<std::mutex> lk(shared->mtx);
    *out = shared->res;
    shared->res = nullptr;
    return ok && *out;
}

/// connect() bounded by an explicit wait — the same non-blocking pattern
/// W5100Device already uses. Leaves the socket NON-BLOCKING on success,
/// which is what a poll-driven transfer wants; a blocking-I/O owner flips
/// it back with `setBlocking`.
///
/// `cancel`, when given, is re-read every 100 ms of the wait and abandons
/// the connect when set. Without it a worker whose owner had gone held its
/// socket and thread for the whole timeout (bug hunt 2026-09-29).
inline bool connectBounded(const addrinfo* a, int timeoutMs, socket_t& out,
                           const std::atomic<bool>* cancel = nullptr)
{
    socket_t s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (!isValidSocket(s)) return false;
    disableSigpipe(s);
    if (!setNonBlocking(s)) { closeHostSocketValue(s); return false; }

    if (::connect(s, a->ai_addr, static_cast<socklen_c>(a->ai_addrlen)) != 0) {
        bool ok = errInProgress(lastSocketError());
        if (ok) {
            constexpr int kSliceMs = 100;
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(timeoutMs);
            WaitResult w = WaitResult::Timeout;
            for (;;) {
                if (cancel && cancel->load()) break;
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) break;
                int left = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - now).count());
                if (cancel && left > kSliceMs) left = kSliceMs;
                w = waitSocket(s, SocketWait::Write, left);
                if (w != WaitResult::Timeout) break;
            }
            ok = w == WaitResult::Ready && connectResult(s) == 0;
        }
        if (!ok) {
            closeHostSocketValue(s);
            return false;
        }
    }
    out = s;
    return true;
}

} // namespace pom2

#endif // POM2_HAS_SOCKETS
#endif // POM2_SOCKET_UTIL_H
