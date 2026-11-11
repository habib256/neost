// NAT en espace utilisateur pour EtherNEC/NetUSBee. Le protocole reste hors
// du cœur ; l'adaptateur de POM2 ne voit que des trames Ethernet sans FCS.
// © 2026 VERHILLE Arnaud — GPL-3.0-or-later.
#pragma once
#include "net/NetBackend.hpp"
#include <cstdint>
#include <memory>
#include <string>

class NatBackend final : public NetBackend {
public:
    struct Options {
        bool restricted = false;
        bool allowHostLoopback = false;
        std::string forwards;
    };
    NatBackend();
    ~NatBackend() override;
    NatBackend(const NatBackend&) = delete;
    NatBackend& operator=(const NatBackend&) = delete;
    static bool available();
    bool open(bool restricted = false);
    bool open(const Options& options);
    void close();
    bool isOpen() const;
    const std::string& lastError() const { return error_; }
    static const char* guestIp() { return "10.0.2.15"; }
    static const char* gatewayIp() { return "10.0.2.2"; }
    static const char* nameserver() { return "10.0.2.3"; }
    static const char* netmask() { return "255.255.255.0"; }
    const char* name() const override { return "built-in NAT"; }
    void send(const uint8_t* frame, int len) override;
    bool recv(std::vector<uint8_t>& out) override;
    void poll() override;
    void nicReset() override;
    uint64_t txFrames() const;
    uint64_t rxFrames() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};
