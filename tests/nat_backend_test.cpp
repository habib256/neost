// Contrat de l'adaptateur NeoST : options, cycle de vie, trames et échec de bind.
#include "net/NatBackend.hpp"
#include "NatPacket.h"
#include "SocketCompat.h"
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL NAT adapter: %s\n", what); ++failures; }
}
}
int main() {
    using namespace pom2;
    using namespace pom2::nat;
    NatBackend net;
    check(NatBackend::available(), "host sockets available");
    NatBackend::Options options;
    options.forwards = "tcp:127.0.0.1:65536:23";
    check(!net.open(options) && !net.lastError().empty() && !net.isOpen(), "invalid ports rejected");
    options.forwards = "tcp:127.0.0.1:6502:23";
    options.restricted = true;
    check(!net.open(options) && !net.isOpen(), "restricted mode refuses publications");
    options.forwards.clear();
    check(net.open(options), "restricted services start");
    ArpPacket arp;
    arp.operation = kArpRequest;
    arp.senderMac = {2, 0x4e, 0x53, 0x54, 0, 1};
    arp.senderIp = 0x0a00020f;
    arp.targetIp = 0x0a000202;
    auto frame = buildEthernet(kBroadcastMac, arp.senderMac, kEtherTypeArp, buildArp(arp));
    net.send(frame.data(), int(frame.size()));
    net.poll();
    std::vector<uint8_t> reply;
    check(net.recv(reply), "ARP traverses NetBackend");
    EthernetFrame ethernet;
    ArpPacket answer;
    check(parseEthernet(reply.data(), reply.size(), ethernet) && parseArp(ethernet.payload, answer)
          && answer.operation == kArpReply && answer.senderIp == arp.targetIp, "gateway reply");
    check(net.txFrames() == 1 && net.rxFrames() == 1, "counters count frames");
    check(!net.recv(reply) && reply.empty(), "empty receive clears output");
    net.close();
    check(!net.isOpen() && !net.recv(reply), "close clears backend and queued frames");
    ensureSocketStack();
    auto occupied = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_c length = sizeof(address);
    const bool bound = isValidSocket(occupied) &&
        ::bind(occupied, reinterpret_cast<sockaddr*>(&address), length) == 0 &&
        ::getsockname(occupied, reinterpret_cast<sockaddr*>(&address), &length) == 0 &&
        ::listen(occupied, 1) == 0;
    check(bound, "local listener fixture");
    if (bound) {
        options.restricted = false;
        options.forwards = "tcp:127.0.0.1:" + std::to_string(netToHost16(address.sin_port)) + ":23";
        check(!net.open(options) && !net.isOpen(), "occupied publication fails the whole open");
    }
    if (isValidSocket(occupied)) closeHostSocket(occupied);
    options.forwards.clear();
    check(net.open(options) && net.lastError().empty(), "reopen after failure");
    std::printf("NAT adapter: %d failures\n", failures);
    return failures ? 1 : 0;
}
