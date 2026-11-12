// =============================================================================
//  MidiInHost.hpp — Entrée MIDI hôte : PLUSIEURS appareils branchés sur la machine
//  (claviers maîtres, groovebox, séquenceur matériel) entrent dans le MIDI IN du ST.
//
//  C'est le pendant de MidiOutHost : là où celui-ci fait sortir l'ACIA 6850 vers le
//  monde, celui-ci fait ENTRER le monde dans l'ACIA (MidiAcia::receiveExternal).
//  Backends : CoreMIDI (macOS), séquenceur ALSA (Linux), winmm (Windows). Ailleurs,
//  coquille vide.
//
//  ── C'est un BOÎTIER DE FUSION, pas un simple aiguillage ──────────────────────
//  Le ST n'a qu'UNE prise MIDI IN. Réunir plusieurs appareils dessus est le rôle
//  d'un boîtier de fusion, et un tel boîtier ne mélange PAS des octets : il
//  entrelace des MESSAGES. Deux claviers joués ensemble émettent « 90 3C 40 » et
//  « 90 40 40 » au même instant ; entrelacés octet par octet ils donneraient
//  « 90 90 3C 40 40 40 » — du charabia. D'où un décodeur PAR SOURCE
//  (MidiMessageParser) et une file fusionnée où chaque message entre d'un bloc.
//
//  ── CANALISATION : sans elle, pas d'enregistrement multipiste ────────────────
//  Deux claviers émettent tous les deux sur le canal 1 par défaut : un séquenceur
//  ne peut alors pas les séparer, tout atterrit sur la même piste. Chaque source
//  peut donc être FORCÉE sur son canal (forceChannel 1-16, 0 = tel quel) : le
//  quartet de canal des messages de voie est réécrit à l'entrée. Ce que le
//  séquenceur ST fait ensuite de ces canaux distincts le regarde — les Cubase
//  complets et Notator savent enregistrer plusieurs canaux sur plusieurs pistes,
//  Cubase Lite non.
//
//  ── ZONES (splitter clavier) ─────────────────────────────────────────────────
//  Une source peut en plus être DÉCOUPÉE en plages de notes, chacune sur son canal
//  et avec sa transposition (main gauche basse canal 2, main droite piano canal 1).
//  Appliqué APRÈS le forçage de canal, AVANT la fusion. Règles : MidiZones.hpp.
//
//  ── Running status ───────────────────────────────────────────────────────────
//  Le statut n'est ré-émis dans le flux fusionné que s'il a CHANGÉ : une source
//  seule garde donc son running status (le flux ne grossit pas), et deux sources
//  qui alternent le voient correctement réinséré (sans quoi les données de l'une
//  seraient lues sous le statut de l'autre).
//
//  ── Cadence ──────────────────────────────────────────────────────────────────
//  ⚠ C'est l'ACIA qui fixe le débit, pas nous (MidiAcia::setRxSource → échéance
//  Scheduler::MIDI_RX, un octet toutes les 2560 cycles = 31250 bauds). La toute
//  première version poussait les octets une fois par TRAME, ce qui plafonnait
//  l'entrée à ~143 o/s contre 3125 o/s sur un câble. Le tampon reste BORNÉ
//  (kMaxJitter) pour absorber une rafale livrée plus vite que le câble ; au-delà
//  c'est le MESSAGE neuf entier qui tombe — jamais un fragment, qui laisserait des
//  octets orphelins dans le flux.
//
//  Câblage (frontend) — UNE fois ; l'ACIA tire ensuite toute seule :
//    midiIn.setDevices({{"Piano 1", 1}, {"Piano 2", 2}});
//    machine.midi.setRxSource([&](uint8_t& b) { return midiIn.tryPop(b); });
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/MidiEndpoint.hpp"
#include "audio/MidiMessageParser.hpp"
#include "audio/MidiZones.hpp"

#ifdef __APPLE__
struct MIDIPacketList;   // avancé : évite d'inclure CoreMIDI.h dans tout le GUI
#endif

class MidiInHost {
public:
    // Un appareil voulu : son nom d'affichage, et le canal sur lequel le forcer
    // (1-16 ; 0 = laisser tel quel).
    struct Want {
        std::string name;
        int forceChannel = 0;
        // Identifiant unique de l'hôte (CoreMIDI) ; vide = inconnu. Cf. MidiEndpoint.hpp :
        // deux claviers du même modèle portent le MÊME nom, et seul l'identifiant les
        // sépare de façon stable.
        std::string uid;
        // Splitter : vide = la source entière sur un canal (forceChannel).
        std::vector<neost::midi::KeyZone> zones;
    };

    MidiInHost() = default;
    ~MidiInHost();
    MidiInHost(const MidiInHost&) = delete;
    MidiInHost& operator=(const MidiInHost&) = delete;

    static bool available();               // un backend d'entrée existe-t-il ici ?
    // Appareils branchés MAINTENANT (à rappeler pour voir un branchement à chaud).
    static std::vector<neost::midi::Endpoint> sources();

    // Ouvre EXACTEMENT cet ensemble (par NOM : les index se renumérotent au
    // débranchement). Un appareil absent est ignoré SANS BRUIT — l'appelant garde
    // son nom en config et rappelle setDevices, ce qui rend le rebranchement à
    // chaud transparent. Rend le nombre d'appareils réellement ouverts.
    std::size_t setDevices(const std::vector<Want>& want);
    void close();
    bool isOpen() const { return !devices_.empty(); }
    std::size_t deviceCount() const { return devices_.size(); }
    std::vector<std::string> openNames() const;
    // Ce qui est réellement ouvert, identifiant compris : permet au frontend
    // d'APPRENDRE l'identifiant d'un appareil désigné par son seul nom (config
    // d'avant, ou choisie à la main) et de le mémoriser pour la suite.
    std::vector<neost::midi::Endpoint> openEndpoints() const;

    // Rend le prochain octet du flux FUSIONNÉ, s'il y en a un. Appelé par l'ACIA
    // sur son horloge série (cf. MidiAcia::setRxSource) — jamais par le frontend.
    bool tryPop(uint8_t& out);

    uint64_t delivered() const { return delivered_.load(std::memory_order_relaxed); }
    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

    // Source SYNTHÉTIQUE (sans backend ni appareil) : neost-selftest éprouve ainsi
    // la fusion, la canalisation et la saturation sur une machine où rien n'est
    // branché — donc aussi en CI. `slot` distingue les sources à fusionner.
    void pushForTest(int slot, const uint8_t* data, std::size_t n, int forceChannel = 0,
                     const std::vector<neost::midi::KeyZone>& zones = {});

private:
    // ~2 trames de MIDI saturé : de quoi absorber une rafale, pas de quoi stocker.
    static constexpr std::size_t kMaxJitter = 1024;

    struct Device {
        std::string name;
        std::string uid;
        int forceChannel = 0;              // 0 = tel quel, 1-16 = canalisé
        std::vector<neost::midi::KeyZone> zones;   // splitter ; vide = aucun
        neost::midi::Parser parser;        // décodeur PROPRE à cette source
        MidiInHost* owner = nullptr;       // pour le callback CoreMIDI (srcConnRefCon)
        // uintptr_t : un HMIDIIN fait 64 bits. CoreMIDI (MIDIEndpointRef) et ALSA
        // ((client<<8|port)+1) y tiennent sans changer de sens.
        uintptr_t src = 0;                 // MIDIEndpointRef / adresse ALSA+1 / HMIDIIN
        // winmm : les tampons de RÉCEPTION SysEx de cet appareil (cf. le .cpp).
        // MM_MIM_DATA ne porte que les messages courts ; sans ces tampons armés, un
        // dump de patch entrant serait purement et simplement perdu.
        void* hdrs = nullptr;
    };

    void feed(Device& d, const uint8_t* data, std::size_t n);   // octets → décodeur
    void emitMessage(Device& d, const uint8_t* msg, int len);   // message → canal/zones
    void enqueue(const uint8_t* msg, int len);                  // message routé → file fusionnée
    Device* deviceForTest(int slot, int forceChannel,
                          const std::vector<neost::midi::KeyZone>& zones);
    void closeBackend();                                        // ferme client/port/thread

#ifdef __APPLE__
    // Callback CoreMIDI (thread temps réel du MIDIServer). Membre statique : c'est
    // le seul moyen d'atteindre feed() sans l'exposer publiquement.
    static void coreMidiRead(const ::MIDIPacketList* pkts, void* refCon, void* srcRef);
#endif

    mutable std::mutex mtx_;
    std::deque<uint8_t> jitter_;           // flux FUSIONNÉ, prêt pour l'ACIA
    // Garde du CYCLE DE VIE des Device (CoreMIDI seulement, de fait) : rien ne
    // documente que MIDIPortDisconnectSource attende un callback EN VOL sur le
    // thread du MIDIServer, or son srcConnRefCon pointe sur NOTRE Device. Le
    // callback vérifie donc l'appartenance sous ce verrou avant de déréférencer,
    // et close() le prend avant de libérer : un callback commencé se termine
    // AVANT la libération, un callback tardif échoue au test et repart. Ordre des
    // verrous : devMtx_ PUIS mtx_ (feed → emitMessage), jamais l'inverse.
    // (Sous ALSA la discipline est ailleurs : le thread lecteur est JOINT avant
    // toute mutation de devices_.)
    mutable std::mutex devMtx_;
    // Compte atomique du tampon : l'ACIA interroge tryPop() 3125 fois par seconde et
    // repart presque toujours les mains vides. Lui éviter le verrou pour ça.
    std::atomic<std::size_t> pending_{0};
    uint8_t lastStatus_ = 0;               // running status DU FLUX FUSIONNÉ
    // Pointeurs STABLES : le refCon d'une connexion CoreMIDI pointe dessus, un
    // vector<Device> qui réalloue les rendrait pendants.
    std::vector<std::unique_ptr<Device>> devices_;
    // Lues par le thread GUI (compteurs de la page MIDI), écrites par l'émulation.
    std::atomic<uint64_t> delivered_{0}, dropped_{0};

    uint32_t client_ = 0;        // MIDIClientRef   (macOS)
    uint32_t port_   = 0;        // MIDIPortRef     (macOS) / port ALSA + 1
    void* seq_ = nullptr;        // snd_seq_t*        (Linux)
    void* dec_ = nullptr;        // snd_midi_event_t* (Linux)
    std::thread reader_;         // boucle ALSA / file de messages winmm (CoreMIDI, lui,
                                 // appelle notre callback : rien à faire tourner)
    std::atomic<bool> stop_{false};
    // winmm : identifiant du thread qui PORTE LA FILE DE MESSAGES des appareils.
    // midiInOpen est appelé en CALLBACK_THREAD et non CALLBACK_FUNCTION : la
    // documentation winmm interdit d'appeler quoi que ce soit hors d'une courte liste
    // blanche depuis un callback, or il faut y ré-armer les tampons SysEx
    // (midiInAddBuffer), qui n'en fait pas partie. Une file de messages rend
    // l'opération légale, garde feed() mono-thread et réutilise le thread déjà là
    // pour ALSA. 0 = file pas encore créée.
    std::atomic<uint32_t> readerTid_{0};
    std::mutex startMtx_;                  // attente de readerTid_ à l'ouverture
    std::condition_variable startCv_;
    void readerLoop();
};
