// =============================================================================
//  MidiRing.hpp — Anneau MIDI réseau (MIDIMaze en ligne, extension NeoST).
//
//  MIDI Maze relie les ST en ANNEAU par câbles MIDI : le MIDI OUT de chaque
//  machine va au MIDI IN de la suivante. On transporte cet anneau sur UDP :
//  les octets MIDI OUT partent en datagrammes vers le pair « aval », et les
//  datagrammes reçus (du pair « amont ») entrent dans le MIDI IN local — le
//  câble MIDI de la salle de réseau, rejoué sur Internet.
//
//  Câblage (frontend) :
//    midi.setMidiSink([ring](uint8_t b){ ring->sendByte(b); });
//    midi.setRxSource([ring](uint8_t& b){ return ring->tryPop(b); });
//    // une fois par trame : ring->poll([](uint8_t){ return false; });
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "net/Socket.hpp"

class MidiRing {
public:
    // `peer` = "hôte:port" du pair AVAL (où envoyer). `listenPort` = port UDP
    // local d'écoute (d'où arrive l'amont). Renvoie false si le socket échoue.
    bool open(const std::string& peer, int listenPort);
    void close();
    ~MidiRing();

    void sendByte(uint8_t b);                    // MIDI OUT → réseau (aval)
    // Collecte les datagrammes dans le tampon de gigue une fois par trame.
    // L'ancien consommateur synchrone reste possible ; pour la source cadencée,
    // accept rend false et tryPop livre ensuite un octet par échéance MIDI_RX.
    void poll(const std::function<bool(uint8_t)>& accept);
    bool tryPop(uint8_t& b);                    // sans appel socket, source série

    bool ok() const { return neonet::socketValid(fd_); }

private:
    neonet::SocketHandle fd_ = neonet::kInvalidSocket; // socket UDP
    // Adresse du pair aval (sockaddr_storage opaque pour ne pas fuir <netinet>).
    unsigned char peerAddr_[128] = {0};
    int  peerAddrLen_ = 0;
    std::deque<uint8_t> jitter_;                 // tampon de gigue MIDI IN
};
