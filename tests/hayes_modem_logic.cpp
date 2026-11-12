// [interne] Modem Hayes, extension NeoST. Test TCP local (127.0.0.1), hors selftest :
// cible CMake `neost-modem-test`, bâtie avec NEOST_WITH_NET et lancée par le palier fast
// de tools/run_all.py (étapes réseau).
//   cmake --build build --target neost-modem-test && ./build/neost-modem-test
// Vérifie : chaîne AT multiple, ATDT asynchrone (le fil d'émulation ne bloque pas),
// raccroché sur chute de DTR (&D2) et non (&D0), abandon d'une numérotation par un
// caractère, raccroché au reset du PSG (lignes du port A au repos) et liste blanche
// de domaines (numérotation hors liste refusée sans toucher au réseau).
#include "net/HayesModem.hpp"
#include "core/YM2149.hpp"
#include "io/Mfp.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

// Écouteur TCP minimal sur 127.0.0.1, port choisi par le système.
struct Listener {
    neonet::SocketHandle fd = neonet::kInvalidSocket;
    int port = 0;
    bool open() {
        neonet::netInitOnce();
        fd = static_cast<neonet::SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
        if (!neonet::socketValid(fd)) return false;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
#ifdef _WIN32
        if (bind(SOCKET(fd), reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return false;
        if (listen(SOCKET(fd), 4) != 0) return false;
        int len = sizeof a;
        getsockname(SOCKET(fd), reinterpret_cast<sockaddr*>(&a), &len);
#else
        if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return false;
        if (listen(fd, 4) != 0) return false;
        socklen_t len = sizeof a;
        getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
#endif
        port = ntohs(a.sin_port);
        return true;
    }
    // Accepte une connexion en attente (au plus `ms`).
    neonet::SocketHandle accept(int ms) {
#ifdef _WIN32
        fd_set r; FD_ZERO(&r); FD_SET(SOCKET(fd), &r);
        timeval tv{ms / 1000, (ms % 1000) * 1000};
        if (select(0, &r, nullptr, nullptr, &tv) != 1) return neonet::kInvalidSocket;
        return static_cast<neonet::SocketHandle>(::accept(SOCKET(fd), nullptr, nullptr));
#else
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, ms) != 1) return neonet::kInvalidSocket;
        return ::accept(fd, nullptr, nullptr);
#endif
    }
    ~Listener() { neonet::sockClose(fd); }
};

int fails = 0;
void expect(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "[modem-test] FAIL: %s\n", what); ++fails; }
}

struct Rig {
    Mfp mfp;
    std::string out;
    HayesModem modem{mfp, [this](uint8_t b) { out.push_back(char(b)); }};
    void type(const std::string& s) { for (char c : s) modem.onTx(uint8_t(c)); }
    // poll() « une fois par trame » jusqu'à voir `what`, au plus `ms`.
    bool waitFor(const std::string& what, int ms) {
        const auto end = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < end) {
            modem.poll();
            if (out.find(what) != std::string::npos) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
};

// Le pair voit-il la fin de connexion (EOF) dans les `ms` ?
bool peerSeesEof(neonet::SocketHandle s, int ms) {
    uint8_t b[64];
    const auto end = Clock::now() + std::chrono::milliseconds(ms);
    while (Clock::now() < end) {
        const int rc = neonet::sockRecv(s, b, int(sizeof b), 50);
        if (rc == 0 || rc == -1) return true;
    }
    return false;
}

std::string dialCmd(int port) { return "ATDT127.0.0.1:" + std::to_string(port) + "\r\n"; }
} // namespace

int main() {
    Listener srv;
    if (!srv.open()) { std::fprintf(stderr, "[modem-test] cannot listen on 127.0.0.1\n"); return 2; }

    // 1) Chaîne de commandes : &F puis E0 sur une seule ligne, puis plus d'écho.
    {
        Rig r;
        r.type("AT&FE0\r");
        expect(r.out == "AT&FE0\rOK\r\n", "AT&FE0 echoed then OK");
        r.out.clear();
        r.type("AT\r");
        expect(r.out == "OK\r\n", "ATE0 took effect inside a multi-command line");
        r.out.clear();
        r.type("AT&D9\r");
        expect(r.out == "ERROR\r\n", "AT&D9 rejected");
        r.out.clear();
        r.type("AT+FCLASS=0\r");
        expect(r.out == "OK\r\n", "extended command tolerated");
    }

    // 2) ATDT asynchrone, puis chute de DTR sous &D2 (défaut).
    {
        Rig r;
        r.modem.onPortA(0xEF);                    // DTR assertée (bit4 = 0)
        const auto t0 = Clock::now();
        r.type(dialCmd(srv.port));                // le LF final ne doit pas annuler l'appel
        const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0);
        expect(dt.count() < 100, "ATDT returns without blocking the emulation thread");
        expect(r.modem.dialing(), "dial in progress after ATDT");
        expect(r.out.find("CONNECT") == std::string::npos, "CONNECT not delivered synchronously");
        expect(r.waitFor("CONNECT", 3000), "CONNECT delivered by poll()");
        const neonet::SocketHandle peer = srv.accept(1000);
        expect(neonet::socketValid(peer), "server accepted");
        expect(r.modem.connected() && r.modem.dataMode(), "online");
        r.out.clear();
        r.modem.onPortA(0xFF);                    // DTR retombe
        expect(!r.modem.connected(), "&D2: DTR drop hangs up");
        expect(r.out == "NO CARRIER\r\n", "&D2: NO CARRIER reported");
        expect(peerSeesEof(peer, 1000), "&D2: peer sees the connection closed");
        neonet::sockClose(peer);
    }

    // 3) &D0 : DTR ignorée ; « +++ » puis ATH raccrochent.
    {
        Rig r;
        r.type("AT&D0\r");
        r.modem.onPortA(0xEF);
        r.type(dialCmd(srv.port));
        expect(r.waitFor("CONNECT", 3000), "&D0: connected");
        const neonet::SocketHandle peer = srv.accept(1000);
        r.modem.onPortA(0xFF);
        expect(r.modem.connected() && r.modem.dataMode(), "&D0: DTR drop ignored");
        r.out.clear();
        // Temps de garde (2026-11-12) : un « +++ » au milieu des données n'échappe
        // pas, il est transmis tel quel ; un « +++ » suivi trop tôt d'un octet non plus.
        r.type("a+++b");
        for (int i = 0; i < 60; ++i) r.modem.poll();
        expect(r.modem.dataMode() && r.out.empty(), "+++ inside data stays data");
        r.type("+++");
        r.type("c");                              // le silence final est rompu
        for (int i = 0; i < 60; ++i) r.modem.poll();
        expect(r.modem.dataMode() && r.out.empty(), "+++ without trailing guard stays data");
        {
            std::string got;
            uint8_t b[64];
            const auto end = Clock::now() + std::chrono::milliseconds(1000);
            while (got.size() < 9 && Clock::now() < end) {
                const int rc = neonet::sockRecv(peer, b, int(sizeof b), 50);
                if (rc > 0) got.append(reinterpret_cast<char*>(b), std::size_t(rc));
            }
            expect(got == "a+++b+++c", "peer received the '+' bytes unchanged");
        }
        r.type("+++");                            // silence avant (60 trames), puis…
        expect(r.waitFor("OK", 2000), "+++ framed by guard time escapes");
        r.type("ATH\r");
        expect(!r.modem.connected(), "+++ then ATH hangs up");
        expect(r.out.find("OK") != std::string::npos, "ATH answered OK");
        neonet::sockClose(peer);
    }

    // 4) Un caractère pendant la numérotation l'abandonne ; le socket ne fuit pas.
    {
        Rig r;
        r.type(dialCmd(srv.port));
        r.out.clear();
        r.type("A");                              // premier octet d'un ATH, par exemple
        expect(!r.modem.dialing(), "keystroke aborts the dial");
        expect(r.out == "NO CARRIER\r\n", "abort reported as NO CARRIER");
        expect(!r.waitFor("CONNECT", 300), "no late CONNECT after an abort");
        expect(!r.modem.connected(), "still on hook after the abort");
        const neonet::SocketHandle peer = srv.accept(1000);
        if (neonet::socketValid(peer)) {          // le fil a pu connecter avant l'abandon
            expect(peerSeesEof(peer, 1000), "aborted dial closes its socket");
            neonet::sockClose(peer);
        }
    }

    // 5) Reset du PSG : port A au repos ($FF), DTR retombe, l'appel est raccroché.
    {
        Rig r;
        YM2149 psg;
        psg.setPortALineSink([&r](uint8_t a) { r.modem.onPortA(a); });
        psg.write8(0xFF8800, 14);
        psg.write8(0xFF8802, 0xEF);               // DTR assertée
        r.type(dialCmd(srv.port));
        expect(r.waitFor("CONNECT", 3000), "reset: connected");
        const neonet::SocketHandle peer = srv.accept(1000);
        psg.reset();
        expect(!r.modem.connected(), "PSG reset drops DTR and hangs up");
        expect(peerSeesEof(peer, 1000), "reset: peer sees the connection closed");
        neonet::sockClose(peer);
        psg.setPortALineSink({});
    }

    // 6) Liste blanche de domaines (2026-11-12) : hors liste, NO CARRIER sans
    //    numéroter ; une adresse listée passe.
    {
        Rig r;
        expect(r.modem.setAllowedDomains("bbs.example.org"), "allowlist parsed");
        r.type(dialCmd(srv.port));
        expect(!r.modem.dialing(), "allowlist: a dial outside the list never starts");
        expect(r.out.find("NO CARRIER") != std::string::npos, "allowlist: refusal is NO CARRIER");
        const neonet::SocketHandle none = srv.accept(200);
        expect(!neonet::socketValid(none), "allowlist: nothing reached the server");
        neonet::sockClose(none);
        r.out.clear();
        expect(!r.modem.setAllowedDomains("bad..name"), "invalid allowlist rejected");
        expect(r.modem.setAllowedDomains("bbs.example.org, 127.0.0.1"), "allowlist with an address");
        r.type(dialCmd(srv.port));
        expect(r.waitFor("CONNECT", 3000), "allowlist: a listed address connects");
        const neonet::SocketHandle peer = srv.accept(1000);
        expect(neonet::socketValid(peer), "allowlist: server accepted the listed dial");
        neonet::sockClose(peer);
    }

    if (fails) return 1;
    std::fprintf(stderr, "[modem-test] AT chain, async dial, DTR &D2/&D0, dial abort, "
                         "PSG reset, domain allowlist: OK\n");
    return 0;
}
