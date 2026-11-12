// =============================================================================
//  MidiRing.hpp — Anneau MIDI réseau (MIDIMaze en ligne, extension NeoST).
//
//  MIDI Maze relie les ST en ANNEAU par câbles MIDI : le MIDI OUT de chaque
//  machine va au MIDI IN de la suivante. On transporte cet anneau sur UDP :
//  les octets MIDI OUT partent en datagrammes vers le pair « aval », et les
//  datagrammes reçus (du pair « amont ») entrent dans le MIDI IN local — le
//  câble MIDI de la salle de réseau, rejoué sur Internet.
//
//  Format de datagramme v1 (aucun format antérieur n'était documenté : l'ancien
//  « un octet brut par datagramme » est refusé, les deux bouts doivent être à jour) :
//    0  'N' 'M' 'R'   magie
//    3  0x01          version
//    4  session       u32 gros-boutiste, tirée à l'ouverture de l'anneau
//    8  seq           u32 gros-boutiste, +1 par datagramme
//   12  octets MIDI   1 à kMaxPayload, dans l'ordre du câble
//  Le récepteur se verrouille sur la première source valide (adresse + port) et
//  ignore toute autre source tant que la sienne parle ; il jette les doublons et
//  les datagrammes en retard (seq non croissant), compte les pertes, et se
//  resynchronise quand la session de sa source change (pair redémarré). Un câble
//  MIDI ne duplique ni ne réordonne : un datagramme hors ordre n'est pas recousu,
//  il est perdu comme un octet sur une ligne coupée.
//
//  Câblage (frontend) :
//    midi.setMidiSink([ring](uint8_t b){ ring->sendByte(b); });
//    midi.setRxSource([ring](uint8_t& b){ return ring->tryPop(b); });
//    // une fois par trame : ring->poll([](uint8_t){ return false; });
//  Les octets émis dans une trame partent ensemble au poll() suivant (ou dès que
//  kMaxPayload est atteint).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "net/Socket.hpp"

class MidiRing {
public:
    static constexpr int kHeader     = 12;
    static constexpr int kMaxPayload = 512;
    static constexpr int64_t kRelatchMs = 5000;   // source muette → une autre peut la remplacer

    struct Stats {
        uint64_t accepted = 0;      // datagrammes livrés
        uint64_t duplicate = 0;     // doublons ou en retard (seq non croissant)
        uint64_t foreign = 0;       // autre source que l'amont verrouillé
        uint64_t malformed = 0;     // pas l'en-tête v1 (dont l'ancien format brut)
        uint64_t lost = 0;          // trous de séquence (datagrammes jamais reçus)
        uint64_t overflow = 0;      // octets jetés, tampon de gigue plein
    };

    // `peer` = "hôte:port" du pair AVAL (où envoyer). `listenPort` = port UDP
    // local d'écoute (d'où arrive l'amont). Renvoie false si le socket échoue.
    bool open(const std::string& peer, int listenPort);
    // Spécification de l'anneau telle que la saisit l'utilisateur (`--midi-net`,
    // champ de la page MIDI du GUI, `midi_net=` de neost.cfg) : "hôte:port" ou
    // "hôte:port:écoute", écoute par défaut kDefaultListen. Rend false (et la raison
    // dans `err`, en anglais) si un port manque ou sort de 1-65535, ou si l'hôte est vide.
    static constexpr int kDefaultListen = 6820;
    static bool parseSpec(const std::string& spec, std::string& peer, int& listenPort,
                          std::string* err = nullptr);
    void close();
    ~MidiRing();

    void sendByte(uint8_t b);                    // MIDI OUT → tampon d'émission
    void flush();                                // tampon d'émission → un datagramme
    // Émet le tampon, puis collecte les datagrammes dans le tampon de gigue, une
    // fois par trame. L'ancien consommateur synchrone reste possible ; pour la
    // source cadencée, accept rend false et tryPop livre ensuite un octet par
    // échéance MIDI_RX.
    void poll(const std::function<bool(uint8_t)>& accept);
    bool tryPop(uint8_t& b);                    // sans appel socket, source série

    // Cœur de la réception, sans socket (tests) : `source` identifie l'émetteur
    // (adresse + port), `nowMs` une horloge hôte monotone. Rend vrai si livré.
    bool ingest(const uint8_t* d, int n, const std::string& source, int64_t nowMs);
    // Fabrique un datagramme v1 (tests et émission).
    static std::vector<uint8_t> encode(uint32_t session, uint32_t seq,
                                       const uint8_t* payload, int n);

    bool ok() const { return neonet::socketValid(fd_); }
    const Stats& stats() const { return stats_; }

private:
    neonet::SocketHandle fd_ = neonet::kInvalidSocket; // socket UDP
    // Adresse du pair aval (sockaddr_storage opaque pour ne pas fuir <netinet>).
    unsigned char peerAddr_[128] = {0};
    int  peerAddrLen_ = 0;
    std::deque<uint8_t> jitter_;                 // tampon de gigue MIDI IN
    std::vector<uint8_t> tx_;                    // octets MIDI OUT pas encore émis
    uint32_t txSession_ = 0;
    uint32_t txSeq_ = 0;
    // Amont verrouillé.
    bool        haveSrc_ = false;
    std::string src_;
    uint32_t    srcSession_ = 0;
    uint32_t    oldSession_ = 0;                 // session retirée (même source)
    bool        haveOldSession_ = false;
    uint32_t    lastSeq_ = 0;
    int64_t     lastRxMs_ = 0;
    bool        warnedMalformed_ = false;
    Stats       stats_;
};
