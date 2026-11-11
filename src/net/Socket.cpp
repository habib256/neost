// =============================================================================
//  Socket.cpp — Aides sockets partagées (cf. Socket.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/Socket.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace neonet {

void netInitOnce() {
#ifdef _WIN32
    static bool done = false;
    if (!done) { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); done = true; }
#endif
}

void sockClose(SocketHandle fd) {
    if (!socketValid(fd)) return;
#ifdef _WIN32
    closesocket(SOCKET(fd));
#else
    ::close(fd);
#endif
}

// Coupe les deux sens sans libérer le NUMÉRO de descripteur : le recv d'un
// thread lecteur retourne aussitôt, mais le fd ne peut pas être réattribué à
// une autre connexion tant que sockClose n'a pas été appelé — c'est la moitié
// « sûre » d'un arrêt de thread lecteur (shutdown → join → close).
void sockShutdown(SocketHandle fd) {
    if (!socketValid(fd)) return;
#ifdef _WIN32
    shutdown(SOCKET(fd), SD_BOTH);
#else
    ::shutdown(fd, SHUT_RDWR);
#endif
}

SocketHandle tcpConnect(const std::string& host, int port, int timeoutMs, std::string& err) {
    netInitOnce();
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    char portStr[16];
    std::snprintf(portStr, sizeof portStr, "%d", port);
    const int gai = getaddrinfo(host.c_str(), portStr, &hints, &res);
    if (gai != 0 || !res) { err = "cannot resolve " + host; return kInvalidSocket; }

    SocketHandle fd = kInvalidSocket;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = static_cast<SocketHandle>(socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (!socketValid(fd)) continue;
#ifndef _WIN32
        // Connexion avec timeout : non-bloquant + poll, puis retour en bloquant.
        const int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int rc = ::connect(fd, ai->ai_addr, socklen_t(ai->ai_addrlen));
        if (rc != 0 && errno == EINPROGRESS) {
            pollfd pf{fd, POLLOUT, 0};
            rc = (poll(&pf, 1, timeoutMs) == 1 && (pf.revents & POLLOUT)) ? 0 : -1;
            if (rc == 0) {
                int soerr = 0;
                socklen_t sl = sizeof soerr;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                if (soerr != 0) rc = -1;
            }
        }
        fcntl(fd, F_SETFL, fl);
#else
        u_long nonBlocking = 1;
        ioctlsocket(SOCKET(fd), FIONBIO, &nonBlocking);
        int rc = ::connect(SOCKET(fd), ai->ai_addr, int(ai->ai_addrlen));
        if (rc == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            // [B] Microsoft Learn, « select function (winsock2.h) », Remarks : un connect
            // non bloquant qui échoue se signale dans exceptfds, pas dans writefds. Sans
            // exceptfds, un port fermé attendait tout le délai au lieu d'échouer aussitôt.
            fd_set wfds, efds;
            FD_ZERO(&wfds);
            FD_ZERO(&efds);
            FD_SET(SOCKET(fd), &wfds);
            FD_SET(SOCKET(fd), &efds);
            timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
            rc = (select(0, nullptr, &wfds, &efds, &tv) > 0 && FD_ISSET(SOCKET(fd), &wfds)
                  && !FD_ISSET(SOCKET(fd), &efds)) ? 0 : -1;
            if (rc == 0) {
                int soerr = 0;
                int sl = sizeof soerr;
                if (getsockopt(SOCKET(fd), SOL_SOCKET, SO_ERROR,
                               reinterpret_cast<char*>(&soerr), &sl) != 0 || soerr != 0)
                    rc = -1;
            }
        } else if (rc == SOCKET_ERROR) {
            rc = -1;
        }
        nonBlocking = 0;
        ioctlsocket(SOCKET(fd), FIONBIO, &nonBlocking);
#endif
        if (rc == 0) break;
        sockClose(fd);
        fd = kInvalidSocket;
    }
    freeaddrinfo(res);
    if (!socketValid(fd)) err = "cannot connect to " + host;
    return fd;
}

int sockSend(SocketHandle fd, const uint8_t* p, int n) {
    int sent = 0;
    while (sent < n) {
#ifdef _WIN32
        const int rc = ::send(SOCKET(fd), reinterpret_cast<const char*>(p) + sent, n - sent, 0);
#else
        const int rc = int(::send(fd, p + sent, std::size_t(n - sent), MSG_NOSIGNAL));
#endif
        if (rc <= 0) return -1;
        sent += rc;
    }
    return sent;
}

int sockRecv(SocketHandle fd, uint8_t* p, int n, int timeoutMs) {
#ifndef _WIN32
    pollfd pf{fd, POLLIN, 0};
    const int pr = poll(&pf, 1, timeoutMs);
    if (pr == 0) return -2;
    if (pr < 0) return -1;
    const int rc = int(::recv(fd, p, std::size_t(n), 0));
#else
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(SOCKET(fd), &fds);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    const int pr = select(0, &fds, nullptr, nullptr, &tv);
    if (pr == 0) return -2;
    if (pr < 0) return -1;
    const int rc = ::recv(SOCKET(fd), reinterpret_cast<char*>(p), n, 0);
#endif
    return rc < 0 ? -1 : rc;
}

bool sockHasData(SocketHandle fd) {
#ifndef _WIN32
    pollfd pf{fd, POLLIN, 0};
    return poll(&pf, 1, 0) == 1 && (pf.revents & POLLIN);
#else
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(SOCKET(fd), &fds);
    timeval tv{0, 0};
    return select(0, &fds, nullptr, nullptr, &tv) == 1;
#endif
}

} // namespace neonet
