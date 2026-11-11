// =============================================================================
//  MidiRing.cpp — Anneau MIDI réseau UDP (cf. MidiRing.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/MidiRing.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

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
int64_t hostMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void putBe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
constexpr uint8_t kMagic[4] = {'N', 'M', 'R', 1};   // magie + version 1
} // namespace

std::vector<uint8_t> MidiRing::encode(uint32_t session, uint32_t seq,
                                      const uint8_t* payload, int n) {
    std::vector<uint8_t> d(std::size_t(kHeader) + std::size_t(n));
    std::memcpy(d.data(), kMagic, 4);
    putBe32(d.data() + 4, session);
    putBe32(d.data() + 8, seq);
    if (n > 0) std::memcpy(d.data() + kHeader, payload, std::size_t(n));
    return d;
}

bool MidiRing::ingest(const uint8_t* d, int n, const std::string& source, int64_t nowMs) {
    if (n <= kHeader || n > kHeader + kMaxPayload || std::memcmp(d, kMagic, 4) != 0) {
        ++stats_.malformed;
        if (!warnedMalformed_) {
            warnedMalformed_ = true;
            std::fprintf(stderr, "[midi-ring] ignoring a datagram that is not ring format v1 "
                                 "(older NeoST peer?)\n");
        }
        return false;
    }
    const uint32_t session = be32(d + 4);
    const uint32_t seq = be32(d + 8);
    // Verrou sur l'amont : la première source valide, remplaçable seulement quand
    // elle s'est tue kRelatchMs (pair relancé depuis une autre adresse ou un autre port).
    if (!haveSrc_ || (source != src_ && nowMs - lastRxMs_ > kRelatchMs)) {
        haveSrc_ = true;
        src_ = source;
        srcSession_ = session;
        lastSeq_ = seq - 1;
        rtrace("upstream latched", std::to_string(session));
    } else if (source != src_) {
        ++stats_.foreign;
        return false;
    } else if (session != srcSession_) {        // même source, pair redémarré
        srcSession_ = session;
        lastSeq_ = seq - 1;
        rtrace("upstream session changed", std::to_string(session));
    }
    const int32_t ahead = int32_t(seq - lastSeq_);   // arithmétique série (RFC 1982)
    if (ahead <= 0) { ++stats_.duplicate; return false; }
    stats_.lost += uint64_t(ahead - 1);
    lastSeq_ = seq;
    lastRxMs_ = nowMs;
    ++stats_.accepted;
    for (int i = kHeader; i < n; ++i) {
        if (jitter_.size() < 8192) jitter_.push_back(d[i]);
        else ++stats_.overflow;
    }
    return true;
}

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
    // Session : distingue un pair relancé (sa séquence repart) d'un doublon tardif.
    std::random_device rd;
    txSession_ = uint32_t(rd()) ^ uint32_t(hostMs());
    txSeq_ = 0;
    tx_.clear();
    haveSrc_ = false;
    stats_ = Stats{};
    rtrace("open", "peer=" + peer + " listen=" + std::to_string(listenPort));
    return true;
}

void MidiRing::close() {
    flush();
    if (neonet::socketValid(fd_)) { neonet::sockClose(fd_); fd_ = neonet::kInvalidSocket; }
    peerAddrLen_ = 0;
}

void MidiRing::sendByte(uint8_t b) {
    if (!neonet::socketValid(fd_) || peerAddrLen_ == 0) return;
    tx_.push_back(b);
    if (int(tx_.size()) >= kMaxPayload) flush();
}

void MidiRing::flush() {
    if (tx_.empty()) return;
    if (!neonet::socketValid(fd_) || peerAddrLen_ == 0) { tx_.clear(); return; }
    const std::vector<uint8_t> d = encode(txSession_, txSeq_++, tx_.data(), int(tx_.size()));
    tx_.clear();
#ifdef _WIN32
    sendto(SOCKET(fd_), reinterpret_cast<const char*>(d.data()), int(d.size()), 0,
#else
    sendto(fd_, reinterpret_cast<const char*>(d.data()), d.size(), 0,
#endif
           reinterpret_cast<const sockaddr*>(peerAddr_), socklen_t(peerAddrLen_));
}

void MidiRing::poll(const std::function<bool(uint8_t)>& accept) {
    if (!neonet::socketValid(fd_)) return;
    flush();                                     // les octets émis pendant la trame
    // 1) Draine le socket dans le tampon de gigue (borné pour ne pas gonfler sans fin).
    uint8_t buf[kHeader + kMaxPayload + 1];      // +1 : un datagramme trop long se voit
    const int64_t now = hostMs();
    for (int guard = 0; guard < 64; ++guard) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof from;
#ifdef _WIN32
        const int rc = ::recvfrom(SOCKET(fd_), reinterpret_cast<char*>(buf), int(sizeof buf), 0,
                                  reinterpret_cast<sockaddr*>(&from), &fromLen);
#else
        const int rc = int(::recvfrom(fd_, buf, sizeof buf, 0,
                                      reinterpret_cast<sockaddr*>(&from), &fromLen));
#endif
        if (rc <= 0) break;
        // Source = adresse IPv4 + port, en octets réseau.
        std::string source(reinterpret_cast<const char*>(&from.sin_addr), sizeof from.sin_addr);
        source.append(reinterpret_cast<const char*>(&from.sin_port), sizeof from.sin_port);
        ingest(buf, rc, source, now);
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
