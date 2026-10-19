// =============================================================================
//  MidiRing.cpp — Anneau MIDI réseau UDP (cf. MidiRing.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/MidiRing.hpp"

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
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "net/Socket.hpp"   // netInitOnce()

namespace {
void rtrace(const char* what, const std::string& d) {
    static const bool on = std::getenv("NEOST_NET_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[midi-ring] %s %s\n", what, d.c_str());
}
} // namespace

MidiRing::~MidiRing() { close(); }

bool MidiRing::open(const std::string& peer, int listenPort) {
    neonet::netInitOnce();
    close();

    // Résout le pair aval "hôte:port".
    const auto colon = peer.rfind(':');
    if (colon == std::string::npos) { rtrace("bad peer", peer); return false; }
    const std::string host = peer.substr(0, colon);
    const std::string port = peer.substr(colon + 1);

    addrinfo hints{};
    hints.ai_family   = AF_INET;               // IPv4 : anneau MIDI local/LAN
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        rtrace("resolve failed", peer);
        return false;
    }
    std::memcpy(peerAddr_, res->ai_addr, res->ai_addrlen);
    peerAddrLen_ = int(res->ai_addrlen);
    freeaddrinfo(res);

    fd_ = static_cast<neonet::SocketHandle>(socket(AF_INET, SOCK_DGRAM, 0));
    if (!neonet::socketValid(fd_)) { rtrace("socket failed", ""); return false; }

    // Écoute locale (amont).
    sockaddr_in in{};
    in.sin_family = AF_INET;
    in.sin_addr.s_addr = htonl(INADDR_ANY);
    in.sin_port = htons(uint16_t(listenPort));
#ifdef _WIN32
    if (bind(SOCKET(fd_), reinterpret_cast<sockaddr*>(&in), sizeof in) != 0) {
#else
    if (bind(fd_, reinterpret_cast<sockaddr*>(&in), sizeof in) != 0) {
#endif
        rtrace("bind failed", std::to_string(listenPort));
        close();
        return false;
    }
#ifndef _WIN32
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
#else
    u_long nb = 1;
    ioctlsocket(SOCKET(fd_), FIONBIO, &nb);
#endif
    rtrace("open", "peer=" + peer + " listen=" + std::to_string(listenPort));
    return true;
}

void MidiRing::close() {
    if (neonet::socketValid(fd_)) { neonet::sockClose(fd_); fd_ = neonet::kInvalidSocket; }
    peerAddrLen_ = 0;
}

void MidiRing::sendByte(uint8_t b) {
    if (!neonet::socketValid(fd_) || peerAddrLen_ == 0) return;
#ifdef _WIN32
    sendto(SOCKET(fd_), reinterpret_cast<const char*>(&b), 1, 0,
#else
    sendto(fd_, reinterpret_cast<const char*>(&b), 1, 0,
#endif
           reinterpret_cast<const sockaddr*>(peerAddr_), socklen_t(peerAddrLen_));
}

void MidiRing::poll(const std::function<bool(uint8_t)>& accept) {
    if (!neonet::socketValid(fd_)) return;
    // 1) Draine le socket dans le tampon de gigue (borné pour ne pas gonfler sans fin).
    uint8_t buf[512];
    for (int guard = 0; guard < 64; ++guard) {
#ifdef _WIN32
        const int rc = ::recv(SOCKET(fd_), reinterpret_cast<char*>(buf), int(sizeof buf), 0);
#else
        const int rc = int(::recv(fd_, buf, sizeof buf, 0));
#endif
        if (rc <= 0) break;
        for (int i = 0; i < rc; ++i)
            if (jitter_.size() < 8192) jitter_.push_back(buf[i]);
    }
    // 2) Livraison synchrone facultative ; la source série utilise tryPop.
    int delivered = 0;
    while (!jitter_.empty() && accept(jitter_.front())) { jitter_.pop_front(); ++delivered; }
    if (delivered || !jitter_.empty())
        rtrace("deliver", std::to_string(delivered) + " (queued " + std::to_string(jitter_.size()) + ")");
}

// La collecte UDP et la consommation série tournent sur le thread machine.
bool MidiRing::tryPop(uint8_t& b) {
    if (jitter_.empty()) return false;
    b = jitter_.front();
    jitter_.pop_front();
    return true;
}
