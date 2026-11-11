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

// SocketCompat.h — the ONE place that answers "POSIX sockets or Winsock?".
//
// POM2's four networking translation units (AiControlServer's HTTP control
// API, SuperSerialCard's telnet bridge, W5100Device's Uthernet II host TCP/IP,
// SpTcpTransport's FujiNet SP-over-SLIP pipe)
// were written against BSD sockets. Winsock2 is the same protocol stack behind
// a gratuitously different API, and the differences are *silent* ones — code
// that compiles clean against Winsock can still be wrong. The seven traps, all
// of which this header exists to remove (the fifth is not Winsock's fault but
// bit this port anyway — see closeHostSocket):
//
//   1. `SOCKET` IS UNSIGNED (`UINT_PTR`), and the failure value is
//      `INVALID_SOCKET` = `(SOCKET)~0`, not -1. So `if (fd >= 0)` is
//      ALWAYS TRUE on Windows and `fd = -1` marks a socket as *valid*
//      with a huge handle. Every "is this open?" test in the POSIX idiom
//      inverts its meaning without a single warning. Use `kInvalidSocket`
//      and `isValidSocket()`; never compare a socket to 0 or -1.
//   2. Errors do not go through `errno`. `WSAGetLastError()` carries them,
//      the codes are different (`WSAEWOULDBLOCK`, not `EWOULDBLOCK`), and
//      `strerror` cannot render them. Use `lastSocketError()` +
//      `socketErrorText()`.
//   3. `close()` does not close a socket (it closes a CRT file descriptor —
//      a different namespace); `closesocket()` does. `fcntl(O_NONBLOCK)`
//      does not exist; `ioctlsocket(FIONBIO)` is the equivalent.
//   4. The stack must be started per process with `WSAStartup` before the
//      first call, or every socket call fails with WSANOTINITIALISED.
//      `ensureSocketStack()` does it once, thread-safely.
//   5. (POM2's own, not Winsock's — see closeHostSocket.)
//   6. `SO_REUSEADDR` MEANS SOMETHING ELSE ENTIRELY. On POSIX it only
//      relaxes TIME_WAIT and a second LIVE bind to the same address still
//      fails; on Winsock it lets any process bind an address another
//      socket is already listening on, and the later binder collects the
//      new connections. Setting it on Windows the way POSIX code does
//      turns a loopback listener into something a local process can
//      hijack. Use `setListenerBindPolicy()`, never the raw option.
//   7. DATAGRAM ERRORS DESCRIBE THE PACKET, NOT THE SOCKET. Winsock's
//      recvfrom fails an oversized datagram with WSAEMSGSIZE instead of
//      truncating it, and surfaces an ICMP port-unreachable from an
//      earlier sendto as WSAECONNRESET on an UNCONNECTED UDP socket —
//      neither of which POSIX reports at all. Code that closes the socket
//      on "any error but EWOULDBLOCK" therefore destroys a working UDP
//      socket on Windows the first time a peer is unreachable. Classify
//      with `errDatagramDiscard()` and turn the ICMP report off with
//      `disableUdpConnReset()`.
//
// Not compiled under Emscripten: the browser has no usable BSD-socket API at
// all, so those paths stay compiled out via POM2_HAS_SOCKETS (Pom2Build.h).

#ifndef POM2_SOCKET_COMPAT_H
#define POM2_SOCKET_COMPAT_H

#include "Pom2Build.h"

#include <cstddef>
#include <cstdint>
#include <string>

#if !POM2_HAS_SOCKETS
// No host sockets (Emscripten). The socket TYPE still has to exist so that
// headers holding a handle — W5100Device::Socket::fd — compile unchanged;
// only the API is absent. Nothing can open one, so the value is never
// anything but kInvalidSocket.
namespace pom2 {
using socket_t = int;
inline constexpr socket_t kInvalidSocket = -1;
inline bool isValidSocket(socket_t s) { return s != kInvalidSocket; }
} // namespace pom2
#endif

// ─── Byte order + protocol selectors, available in BOTH builds ───────────
//
// The W5100's MACRAW/IPRAW framing goes through NetworkBackend, not through
// a socket, so it is compiled even on Emscripten — yet it still has to build
// big-endian IPv4 and Ethernet headers, and it still names a protocol in the
// SnMR mode switch. Taking those from <arpa/inet.h> tied code that opens no
// socket to the socket stack, and broke the WASM build for exactly that
// reason (undeclared htons / SOCK_STREAM / IPPROTO_TCP).
//
// A 16-bit byte swap is arithmetic, not networking, so POM2 spells it out
// rather than reaching for the platform header. `__BYTE_ORDER__` is a
// GCC/Clang/Emscripten builtin; MSVC does not define it, and every Windows
// target POM2 builds for is little-endian, so the swap branch is right there
// too.
#include "NetworkValues.h"   // byteSwap16 / hostToNet16 / the protocol selectors

#if !POM2_HAS_SOCKETS
namespace pom2 {
// Socketless build: the SnMR mode switch still names these, but
// `openSystemSocket()` refuses before reaching a syscall, so the values are
// inert. They keep the BSD numbering so a reader comparing against
// Uthernet2.cpp is not misled.
inline constexpr int kSockStream = 1;
inline constexpr int kSockDgram  = 2;
inline constexpr int kIpProtoTcp = 6;
inline constexpr int kIpProtoUdp = 17;
} // namespace pom2
#endif

#if POM2_HAS_SOCKETS

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
// winsock2.h MUST precede windows.h — the ancient winsock.h that windows.h
// pulls in otherwise redefines the same symbols and the build dies in a
// wall of redefinition errors. WIN32_LEAN_AND_MEAN above is the other half
// of that defence.
//
// If a TU pulled windows.h in FIRST, say so in one line instead of letting
// the compiler emit fifty. `_WINSOCKAPI_` set without `_WINSOCK2API_` is
// exactly that situation: winsock.h (v1) is already in, and including v2
// on top of it cannot work.
#  if defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#    error "windows.h was included before SocketCompat.h, so winsock v1 is \
already in. Include SocketCompat.h first in this TU (or define \
WIN32_LEAN_AND_MEAN before the windows.h include)."
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <cstring>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace pom2 {

// ── Protocol selectors ────────────────────────────────────────────────
// Named here rather than used raw at the call site so the SnMR mode switch
// in W5100Device compiles identically with and without a host socket stack.
inline constexpr int kSockStream = SOCK_STREAM;
inline constexpr int kSockDgram  = SOCK_DGRAM;
inline constexpr int kIpProtoTcp = IPPROTO_TCP;
inline constexpr int kIpProtoUdp = IPPROTO_UDP;

// ── Types ─────────────────────────────────────────────────────────────

#ifdef _WIN32
using socket_t  = SOCKET;
using socklen_c = int;              ///< `socklen_t` is `int` in Winsock
inline const socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t  = int;
using socklen_c = socklen_t;
inline constexpr socket_t kInvalidSocket = -1;
#endif

/// The only correct "is this socket open?" test on both platforms — see
/// trap 1 in the header comment.
inline bool isValidSocket(socket_t s) { return s != kInvalidSocket; }

// ── Process-wide stack init ───────────────────────────────────────────

/// Start the socket stack if the platform needs it (trap 4). Idempotent
/// and thread-safe: the work happens inside a function-local static, whose
/// initialisation the C++ runtime serialises. No matching WSACleanup —
/// the counterpart would have to run after every socket everywhere is
/// closed, and getting that wrong tears the stack down under a live
/// connection. Windows reclaims the whole Winsock context at process exit
/// anyway, so the shutdown call buys nothing and risks a real bug.
/// Call it before the first socket() in each entry point; it costs one
/// atomic load once the guard is initialised.
inline void ensureSocketStack()
{
#ifdef _WIN32
    struct Startup {
        Startup()  { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    };
    static Startup once;
    (void)once;
#endif
}

// ── Errors ────────────────────────────────────────────────────────────

/// Last socket error, from whichever channel the platform uses (trap 2).
/// Read it IMMEDIATELY after the failing call: on Windows any intervening
/// Win32 call may overwrite the thread's last-error slot.
inline int lastSocketError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

inline bool errWouldBlock(int e)
{
#ifdef _WIN32
    return e == WSAEWOULDBLOCK;
#else
    return e == EAGAIN || e == EWOULDBLOCK;
#endif
}

/// A non-blocking connect() that is still in flight. POSIX says
/// EINPROGRESS; Winsock reports WSAEWOULDBLOCK for the same state.
inline bool errInProgress(int e)
{
#ifdef _WIN32
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return e == EINPROGRESS || e == EWOULDBLOCK;
#endif
}

/// An accept() failure that says nothing about the LISTENING socket.
///
/// These are per-connection or per-resource conditions: the pending client
/// vanished before it could be accepted (ECONNABORTED/EPROTO — trivially
/// triggered by any local process doing connect + SO_LINGER{1,0} + close), or
/// the process/system momentarily has no descriptors (EMFILE/ENFILE) or no
/// buffers (ENOBUFS/ENOMEM). The listener is still bound and still valid, so
/// treating any of them as "the socket is gone" retires an accept loop for
/// good over a condition that clears by itself.
inline bool errTransientAccept(int e)
{
#ifdef _WIN32
    return e == WSAECONNRESET || e == WSAECONNABORTED ||
           e == WSAEMFILE     || e == WSAENOBUFS;
#else
    return e == ECONNABORTED || e == EPROTO  || e == EMFILE ||
           e == ENFILE       || e == ENOBUFS || e == ENOMEM;
#endif
}

/// The subset of the above that means "out of resources" rather than "that
/// one client went away". The pending connection is still queued, so the next
/// poll returns readable immediately — a bare retry would spin at 100% CPU
/// until the pressure lifts. Callers back off instead.
inline bool errAcceptExhaustion(int e)
{
#ifdef _WIN32
    return e == WSAEMFILE || e == WSAENOBUFS;
#else
    return e == EMFILE || e == ENFILE || e == ENOBUFS || e == ENOMEM;
#endif
}

inline bool errInterrupted(int e)
{
#ifdef _WIN32
    return e == WSAEINTR;
#else
    return e == EINTR;
#endif
}

/// True when a failed recvfrom() on a DATAGRAM socket describes the packet
/// that was just discarded rather than the socket itself, so the caller
/// must drop the datagram and keep the socket (trap 7). Meaningless — and
/// wrong — for a stream socket: on TCP, WSAECONNRESET/ECONNRESET is the
/// peer aborting the connection and the socket really is dead.
///
///   WSAEMSGSIZE   Winsock's answer to a datagram larger than the buffer.
///                 POSIX instead returns the truncated byte count with no
///                 error at all, which is why only Windows raises this.
///   WSAECONNRESET On Windows an ICMP port-unreachable provoked by an
///                 earlier sendto() is reported on the NEXT recvfrom of an
///                 unconnected UDP socket (the SIO_UDP_CONNRESET
///                 behaviour). One datagram to a closed port would
///                 otherwise tear the socket down.
///   ECONNREFUSED  the POSIX equivalent, raised only on a CONNECTED UDP
///                 socket. Tolerated for symmetry.
///   ENETUNREACH / EHOSTUNREACH: the other ICMP unreachable classes,
///                 likewise per-destination and not per-socket.
inline bool errDatagramDiscard(int e)
{
#ifdef _WIN32
    return e == WSAEMSGSIZE || e == WSAECONNRESET || e == WSAECONNREFUSED ||
           e == WSAENETUNREACH || e == WSAEHOSTUNREACH || e == WSAENETRESET;
#else
    return e == EMSGSIZE || e == ECONNRESET || e == ECONNREFUSED ||
           e == ENETUNREACH || e == EHOSTUNREACH;
#endif
}

/// The SEND side of the same question: a failed sendto() on a datagram
/// socket that describes the DESTINATION, not the socket. Wider than the
/// receive list (bug hunt 2026-10-23): EACCES (a subnet broadcast without
/// SO_BROADCAST), EHOSTDOWN (macOS, ARP gave up), EPERM (a host firewall),
/// ENOBUFS (transient), EADDRNOTAVAIL / EINVAL (a destination such as port
/// 0). Each fell into "fault", which closed the guest's UDP socket.
inline bool errDatagramSendDiscard(int e)
{
    if (errDatagramDiscard(e)) return true;
#ifdef _WIN32
    return e == WSAEACCES || e == WSAEHOSTDOWN || e == WSAENOBUFS ||
           e == WSAEADDRNOTAVAIL || e == WSAEINVAL;
#else
    return e == EACCES || e == EHOSTDOWN || e == EPERM || e == ENOBUFS ||
           e == EADDRNOTAVAIL || e == EINVAL;
#endif
}

/// Human-readable form. `std::strerror` cannot render a WSA code, so
/// Windows goes through FormatMessage.
inline std::string socketErrorText(int e)
{
#ifdef _WIN32
    char* buf = nullptr;
    const DWORD n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(e),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&buf), 0, nullptr);
    std::string msg = (n && buf) ? std::string(buf, n)
                                 : ("winsock error " + std::to_string(e));
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
        msg.pop_back();
    return msg;
#else
    return std::strerror(e);
#endif
}

/// Convenience: text for the error the last failing call left behind.
inline std::string lastSocketErrorText() { return socketErrorText(lastSocketError()); }

// ── Lifetime + mode ───────────────────────────────────────────────────

/// Close and invalidate in one step (trap 3). Takes the handle by
/// reference so the caller cannot leave a dangling one behind — the
/// use-after-close hazard the SSC worker documents at length.
///
/// NAMED `closeHostSocket`, NOT `closeSocket`, AND THAT MATTERS (trap 5).
/// W5100Device has a member `closeSocket(size_t)` — the chip-level CLOSE
/// command for one of its four sockets. Inside that class, unqualified
/// lookup finds the MEMBER first (class scope beats namespace scope) and
/// stops; `socket_t` then converts to `size_t` without a murmur, so
/// `closeSocket(s.fd)` compiles clean and recurses until the stack dies.
/// It did exactly that, caught by uthernet2_w5100_smoke as a segfault
/// with 74 000 identical frames. A name no plausible card would use for
/// its own chip-level operation is what keeps that from coming back.
inline void closeHostSocket(socket_t& s)
{
    if (!isValidSocket(s)) return;
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
    s = kInvalidSocket;
}

/// Close without clearing — for the exchange() idiom, where the caller has
/// already taken the handle out of its atomic. Same naming rule as above.
inline void closeHostSocketValue(socket_t s)
{
    if (!isValidSocket(s)) return;
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
}

/// Half-close both directions, to wake a worker parked in recv()/accept()
/// without closing the handle (which would open a close/reuse race).
/// SHUT_RDWR and SD_BOTH are the same idea under different spellings.
inline void shutdownBoth(socket_t s)
{
    if (!isValidSocket(s)) return;
#ifdef _WIN32
    ::shutdown(s, SD_BOTH);
#else
    ::shutdown(s, SHUT_RDWR);
#endif
}

/// Put the socket in non-blocking mode (trap 3). Returns false on failure.
inline bool setNonBlocking(socket_t s)
{
#ifdef _WIN32
    u_long on = 1;
    return ::ioctlsocket(s, FIONBIO, &on) == 0;
#else
    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

/// The inverse — back to blocking mode, for a socket `connectBounded`
/// (SocketUtil.h) left non-blocking but whose owner wants plain blocking
/// I/O under SO_RCVTIMEO (TnfsClient does).
inline bool setBlocking(socket_t s)
{
#ifdef _WIN32
    u_long on = 0;
    return ::ioctlsocket(s, FIONBIO, &on) == 0;
#else
    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(s, F_SETFL, flags & ~O_NONBLOCK) == 0;
#endif
}

#ifdef _WIN32
// Declared in <mstcpip.h>, which is not pulled in by ws2tcpip.h on every
// toolchain. The definition is the one both the Windows SDK and mingw-w64
// use, so an identical redefinition from that header is harmless.
#  ifndef SIO_UDP_CONNRESET
#    define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#  endif
#endif

/// Stop a UDP socket reporting somebody else's ICMP port-unreachable as a
/// WSAECONNRESET failure on its next recvfrom (trap 7). Windows-only and
/// harmless everywhere else: POSIX never raises that on an unconnected
/// datagram socket. Belt and braces with `errDatagramDiscard()` — this
/// ioctl is undocumented before Windows XP SP2 and can fail on layered
/// service providers, so the classifier must still tolerate the code.
inline bool disableUdpConnReset(socket_t s)
{
#ifdef _WIN32
    BOOL  enable = FALSE;
    DWORD returned = 0;
    return ::WSAIoctl(s, SIO_UDP_CONNRESET, &enable, sizeof(enable),
                      nullptr, 0, &returned, nullptr, nullptr) == 0;
#else
    (void)s;
    return true;
#endif
}

// ── I/O ───────────────────────────────────────────────────────────────
//
// Winsock's send/recv take `char*` and an `int` length and return `int`;
// POSIX takes `void*`/`size_t` and returns `ssize_t`. One signature for
// both, returning a 64-bit signed count so no platform truncates.

using iolen_t = std::int64_t;

/// send() that cannot raise SIGPIPE. Linux spells that MSG_NOSIGNAL;
/// macOS/BSD need SO_NOSIGPIPE set on the socket instead (see
/// disableSigpipe in SocketUtil.h); Windows has no SIGPIPE at all, so a
/// vanished peer surfaces as a plain error return.
inline iolen_t sendSocket(socket_t s, const void* buf, std::size_t n)
{
#ifdef _WIN32
    return ::send(s, static_cast<const char*>(buf), static_cast<int>(n), 0);
#elif defined(MSG_NOSIGNAL)
    return ::send(s, buf, n, MSG_NOSIGNAL);
#else
    return ::send(s, buf, n, 0);
#endif
}

/// sendto() that cannot raise SIGPIPE — the explicit-destination sibling of
/// sendSocket(), for the UDP/IPRAW paths. Same platform split, and the same
/// requirement that the caller has run disableSigpipe() on the socket where
/// MSG_NOSIGNAL does not exist.
inline iolen_t sendToSocket(socket_t s, const void* buf, std::size_t n,
                            const sockaddr* to, socklen_c tolen)
{
#ifdef _WIN32
    return ::sendto(s, static_cast<const char*>(buf), static_cast<int>(n), 0,
                    to, static_cast<int>(tolen));
#elif defined(MSG_NOSIGNAL)
    return ::sendto(s, buf, n, MSG_NOSIGNAL, to, tolen);
#else
    return ::sendto(s, buf, n, 0, to, tolen);
#endif
}

inline iolen_t recvSocket(socket_t s, void* buf, std::size_t n)
{
#ifdef _WIN32
    return ::recv(s, static_cast<char*>(buf), static_cast<int>(n), 0);
#else
    return ::recv(s, buf, n, 0);
#endif
}

/// Look at what is waiting WITHOUT consuming it. The one use for this is a
/// liveness probe: a readable socket that peeks 0 bytes has been closed by the
/// peer, whereas one that peeks >0 has real data another thread is about to
/// read — and MSG_PEEK is what lets the prober tell those apart without
/// stealing the bytes.
inline iolen_t recvPeekSocket(socket_t s, void* buf, std::size_t n)
{
#ifdef _WIN32
    return ::recv(s, static_cast<char*>(buf), static_cast<int>(n), MSG_PEEK);
#else
    return ::recv(s, buf, n, MSG_PEEK);
#endif
}

// ── Readiness ─────────────────────────────────────────────────────────

enum class SocketWait { Read, Write };

enum class WaitResult {
    Ready,      ///< the requested direction is ready
    Timeout,    ///< nothing happened within timeoutMs (or EINTR — retry)
    Failed,     ///< the socket errored or was closed under us
};

/// Wait for one socket to become readable or writable.
///
/// WINDOWS USES select(), NOT WSAPoll(), and the reason is the caller in
/// W5100Device::poll(): it waits for WRITE on a socket with a non-blocking
/// connect in flight, and it has to learn about a REFUSED connection, not
/// just a successful one. On Winsock the documented channel for that is
/// select()'s `exceptfds` — a failed connection attempt is signalled in
/// the exception set, which is precisely why Winsock's select takes one.
/// Folding the exception set into "ready" here lets the caller do what it
/// does on POSIX: wake up, then ask getsockopt(SO_ERROR) which of the two
/// happened. A readiness wait that can only report success would leave a
/// guest polling SN_SR forever on a connection that was refused.
///
/// select() also sidesteps the FD_SETSIZE concern by construction: it is
/// one socket per call, and a Winsock fd_set is an array of handles rather
/// than a bitmask indexed by fd, so there is no descriptor-number ceiling.
inline WaitResult waitSocket(socket_t s, SocketWait dir, int timeoutMs)
{
    if (!isValidSocket(s)) return WaitResult::Failed;
#ifdef _WIN32
    fd_set rd, wr, ex;
    FD_ZERO(&rd); FD_ZERO(&wr); FD_ZERO(&ex);
    FD_SET(s, &ex);
    if (dir == SocketWait::Read) FD_SET(s, &rd); else FD_SET(s, &wr);
    timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    // nfds is ignored by Winsock; it is there for source compatibility.
    const int r = ::select(0, &rd, &wr, &ex,
                           (timeoutMs < 0) ? nullptr : &tv);
    if (r == SOCKET_ERROR)
        return errInterrupted(lastSocketError()) ? WaitResult::Timeout
                                                 : WaitResult::Failed;
    if (r == 0) return WaitResult::Timeout;
    if (FD_ISSET(s, &ex)) return WaitResult::Ready;   // caller reads SO_ERROR
    return WaitResult::Ready;
#else
    struct pollfd pfd{};
    pfd.fd     = s;
    pfd.events = (dir == SocketWait::Read) ? POLLIN : POLLOUT;
    const int r = ::poll(&pfd, 1, timeoutMs);
    if (r < 0)
        return errInterrupted(lastSocketError()) ? WaitResult::Timeout
                                                 : WaitResult::Failed;
    if (r == 0) return WaitResult::Timeout;
    if (pfd.revents & POLLNVAL) return WaitResult::Failed;
    // POLLERR/POLLHUP still mean "wake up and ask SO_ERROR", same as the
    // exception set above — the caller distinguishes success from refusal.
    return WaitResult::Ready;
#endif
}

/// Result of a non-blocking connect that `waitSocket` reported ready:
/// 0 = connected, otherwise the socket-level error code.
inline int connectResult(socket_t s)
{
    int err = 0;
    socklen_c len = sizeof(err);
#ifdef _WIN32
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&err), &len) != 0)
        return lastSocketError();
#else
    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
        return lastSocketError();
#endif
    return err;
}

/// Dotted-quad text for a peer address, for log lines.
///
/// `inet_ntop`, not `inet_ntoa`: the latter returns a pointer into a
/// static buffer, so two threads logging a connection at once can splice
/// each other's addresses, and MSVC deprecates it outright. Both workers
/// that call this run on their own thread.
inline std::string peerAddressText(const sockaddr_in& peer)
{
    char buf[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, const_cast<void*>(
                        static_cast<const void*>(&peer.sin_addr)),
                    buf, sizeof(buf)) == nullptr)
        return "?";
    return buf;
}

/// setsockopt for the int-valued options POM2 uses (SO_REUSEADDR,
/// TCP_NODELAY). Winsock wants a `const char*`, POSIX a `const void*`.
inline bool setSockOptInt(socket_t s, int level, int name, int value)
{
#ifdef _WIN32
    return ::setsockopt(s, level, name,
                        reinterpret_cast<const char*>(&value),
                        sizeof(value)) == 0;
#else
    return ::setsockopt(s, level, name, &value, sizeof(value)) == 0;
#endif
}

/// The bind policy for POM2's two loopback listeners (AiControlServer's
/// HTTP control API, SuperSerialCard's telnet bridge): *this process may
/// re-bind its own port straight after a restart, and no other process may
/// take it while we hold it*. Call it after socket() and before bind().
///
/// One intent, two options, because `SO_REUSEADDR` is not the same feature
/// on the two families (trap 6):
///
///   POSIX   SO_REUSEADDR relaxes exactly one rule — a lingering TIME_WAIT
///           from the previous run no longer blocks the bind. A second
///           LIVE socket on the same address still gets EADDRINUSE, which
///           is the half we want to keep.
///   Winsock SO_REUSEADDR means "let anyone bind this address even while
///           somebody is listening on it", and the later binder wins the
///           new connections (MS: "Using SO_REUSEADDR"). On a listener the
///           agent talks to, that is a hijack: another local process binds
///           127.0.0.1:6503, serves the agent's requests, and collects the
///           control token on the way past. SO_EXCLUSIVEADDRUSE is the
///           documented spelling of the POSIX intent — it also lets this
///           process re-bind its own port once the old socket is closed,
///           so the restart case is covered too.
inline bool setListenerBindPolicy(socket_t s)
{
#ifdef _WIN32
    return setSockOptInt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
    return setSockOptInt(s, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
}

} // namespace pom2

#endif // POM2_HAS_SOCKETS
#endif // POM2_SOCKET_COMPAT_H
