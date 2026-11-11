// =============================================================================
//  NetBackend.hpp — Couche physique Ethernet pour la carte NE2000 émulée.
//
//  La NE2000 (io/Ne2000) émet et reçoit des TRAMES Ethernet complètes (avec MAC
//  src/dst, sans CRC) ; le backend les fait transiter vers le monde réel. Cette
//  interface vit HORS du cœur : le cœur ne connaît que `send(frame)` et pompe
//  `recv(frame)`.
//
//  Backends :
//    · NetBackendNull  — aucun réseau (les trames émises sont perdues) ;
//    · NetBackendLoop  — boucle locale (une trame émise revient : utile aux
//      auto-tests du chemin ring buffer) ;
//    · (NAT intégré : à brancher quand la lib est présente — cf. CMake).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

class NetBackend {
public:
    virtual ~NetBackend() = default;
    virtual const char* name() const = 0;
    // Trame Ethernet complète (dst[6] src[6] type[2] payload…), sans CRC.
    virtual void send(const uint8_t* frame, int len) = 0;
    // Récupère une trame en attente (true + remplit `out`), ou false si vide.
    virtual bool recv(std::vector<uint8_t>& out) = 0;
    virtual void poll() {}                 // une fois par trame (backends async)
    // Mise sous tension de la carte : un backend à état (NAT) lâche ses connexions,
    // flux et requêtes en vol, que plus personne côté invité ne terminera. Appelé
    // au power-cycle seulement, pas par le port reset du DP8390 (un pilote s'en sert
    // aussi pour se remettre d'une erreur) — même partage que POM2 pour le W5100.
    virtual void nicReset() {}
};

// Aucun réseau : la carte s'initialise et « lie », mais rien ne circule.
class NetBackendNull final : public NetBackend {
public:
    const char* name() const override { return "null"; }
    void send(const uint8_t*, int) override {}
    bool recv(std::vector<uint8_t>&) override { return false; }
};

// Boucle locale : chaque trame émise est re-livrée en réception. Déterministe —
// c'est le backend des auto-tests du chemin NE2000 (ring buffer + remote DMA).
class NetBackendLoop final : public NetBackend {
public:
    const char* name() const override { return "loopback"; }
    void send(const uint8_t* frame, int len) override {
        if (len > 0) q_.emplace_back(frame, frame + len);
    }
    bool recv(std::vector<uint8_t>& out) override {
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
private:
    std::deque<std::vector<uint8_t>> q_;
};
