// =============================================================================
//  MidiOutHost.hpp — Sortie MIDI hôte : le flux MIDI OUT de l'ACIA 6850
//  (MidiAcia::setMidiSink) part vers l'extérieur de l'émulateur.
//
//  Deux destinations, indépendantes :
//    (a) SYNTHÉ GM INTÉGRÉ — ici, le DLSMusicDevice d'Apple (macOS seulement) : du
//        son sans rien installer. synthAvailable() ne parle QUE de lui ; ailleurs
//        la même case du GUI est servie par audio/GmSynth (TinySoundFont vendorisé
//        + banque roms/gm/), mixé dans la sortie comme le MT-32 — cf. midiOutApply.
//    (b) PORT MIDI VIRTUEL « NeoST MIDI OUT », que n'importe quel synthé logiciel ou
//        matériel peut écouter : CoreMIDI sous macOS, séquenceur ALSA sous Linux.
//        C'est la voie recommandée pour du General MIDI — un FluidSynth ou un Qsynth
//        branché dessus rendra ces fichiers bien mieux qu'un MT-32.
//        ⚠ PAS SOUS WINDOWS : ni winmm ni WinRT MIDI 1.0 ne savent créer un port que
//        les autres applications voient — il y faut un PILOTE (loopMIDI, LoopBe1).
//        Le port ainsi installé apparaît alors dans (c) comme n'importe quel appareil,
//        en destination ET en source : le câblage vers un DAW est complet sans nous.
//    (c) DESTINATIONS MATÉRIELLES choisies par leur nom (setDestinations), CHACUNE
//        avec le masque des canaux qu'elle reçoit : le MIDI OUT du ST entre
//        DIRECTEMENT dans l'expandeur, la boîte à rythmes ou le clavier branché sur
//        la machine hôte, et l'instrument 1 peut aller ailleurs que l'instrument 2.
//        (b) ne remplace pas (c) : une source virtuelle est PASSIVE, c'est au
//        logiciel d'en face de s'y abonner, et l'appareil matériel, lui, ne s'abonne
//        à rien — il fallait jusqu'ici un patchbay tiers pour relier les deux.
//
//  Les appareils sont désignés par leur NOM, jamais par leur index : débrancher un
//  périphérique renumérote tous les autres, et une config mémorisée en index se
//  serait mise à piloter le mauvais appareil au branchement suivant.
//
//  Les octets arrivent un par un depuis le thread d'émulation ; un petit parseur
//  (running status, SysEx, temps réel) reconstitue les messages. Sur une plateforme
//  sans backend, la classe compile en coquille vide (toujours « indisponible »).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include "audio/MidiEndpoint.hpp"
#include "audio/MidiMessageParser.hpp"
#include "audio/MidiTimecode.hpp"
#include "core/Pacing.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

class MidiOutHost {
public:
    MidiOutHost();
    ~MidiOutHost();
    static bool available();            // au moins une destination utilisable ici
    static bool synthAvailable();       // synthé GM intégré : macOS UNIQUEMENT
    static bool portAvailable();        // port virtuel : CoreMIDI (macOS) / ALSA (Linux)
    // Appareils MATÉRIELS (c) : CoreMIDI, ALSA **et winmm**. À NE PAS confondre avec
    // portAvailable() — Windows n'a aucune API de port virtuel (il faut un pilote
    // tiers, loopMIDI) mais pilote parfaitement les appareils branchés. L'interface
    // gardait l'aiguillage matériel derrière portAvailable() : sous Windows elle
    // aurait caché la seule moitié qui marche.
    static bool destinationsAvailable();
    static const char* portKindName();  // « CoreMIDI » / « ALSA » / « — », pour l'interface
    // Nom du backend d'appareils, pour l'interface et les journaux.
    static const char* backendName();   // « CoreMIDI » / « ALSA » / « winmm » / « — »

    // Panique MIDI : All Sound Off (CC 120), Reset All Controllers (CC 121) et All
    // Notes Off (CC 123) sur les 16 canaux. Indispensable dès qu'on coupe une sortie
    // ou qu'on remet la machine à zéro pendant qu'un accord tient : sans ça les notes
    // restent BLOQUÉES dans le synthé, qui n'a aucune raison de les relâcher tout seul.
    void panic();

    bool openSynth();                   // synthé GM intégré (DLSMusicDevice)
    void closeSynth();
    bool synthOpen() const { return synth_ != nullptr; }

    bool openVirtualPort();             // source CoreMIDI « NeoST MIDI OUT »
    void closeVirtualPort();
    bool portOpen() const { return src_ != 0; }

    // --- Destinations MATÉRIELLES (expandeurs, groovebox, claviers) ---------------
    // PLUSIEURS appareils, chacun avec le masque des canaux MIDI qu'il reçoit : c'est
    // un AIGUILLAGE, pas un simple Thru box. « Instrument 1 de Cubase vers le piano
    // logiciel, instrument 2 vers la groovebox » = canal 1 vers l'un, canal 2 vers
    // l'autre, sans avoir à reconfigurer les appareils eux-mêmes. Un même canal peut
    // partir vers plusieurs destinations (superposition).
    //
    // ⚠ Les messages SYSTÈME (horloge, start/stop, SysEx, $F0-$FF) n'ont pas de canal
    // et vont à TOUTES les destinations : les filtrer casserait la synchro.
    struct Dest {
        std::string name;
        uint16_t channels = 0xFFFF;     // bit n = canal n+1 ; 0xFFFF = tous
        // Identifiant unique de l'hôte (CoreMIDI). Vide = inconnu : on retombe sur le
        // nom, avec la règle « un point de terminaison n'est jamais pris deux fois »
        // qui suffit à séparer deux appareils homonymes (cf. MidiEndpoint.hpp).
        std::string uid;
    };
    // Ce qui est branché MAINTENANT (à rappeler pour voir un branchement à chaud).
    static std::vector<neost::midi::Endpoint> destinations();
    // Ouvre EXACTEMENT cet ensemble. Un appareil absent est ignoré SANS BRUIT :
    // l'appelant garde son nom en config et rappelle, ce qui rend le rebranchement
    // à chaud transparent. Rend le nombre réellement ouvert.
    std::size_t setDestinations(const std::vector<Dest>& want);
    void closeDestinations();
    std::size_t destinationCount() const { return dests_.size(); }
    std::vector<Dest> openDestinations() const;

    bool anyOpen() const { return synthOpen() || portOpen() || !dests_.empty(); }

    // Un octet MIDI OUT de l'ACIA, livré IMMÉDIATEMENT (thread d'émulation).
    void byte(uint8_t b);

    // --- Livraison HORODATÉE (celle du GUI) ----------------------------------
    // L'émulation avance par rafales de trames au rythme du GUI : livrer un octet
    // au moment où le 68000 l'écrit fait gigoter le MIDI de ±60 ms (mesuré sur
    // Cubase : σ 28 ms). On ancre chaque trame sur l'heure HÔTE à laquelle elle
    // DOIT commencer (`anchor`, = l'échéance emuNext du GUI) et chaque octet est
    // programmé à anchor + (cycle − cycleAncre)/CPU_HZ + kLeadMs. Un thread le
    // délivre à l'heure dite : latence fixe (inaudible), gigue nulle.
    // A28 : UNE seule définition de l'horloge CPU/bus, dans core/Pacing.hpp.
    static constexpr double kCpuHz = neost::pacing::kCpuHz;
    // AVANCE de livraison, en ms. C'est un ARBITRAGE, et il appartient à
    // l'utilisateur : plus elle est courte, plus le jeu au clavier est direct ; plus
    // elle est longue, mieux elle absorbe un à-coup de la boucle GUI (drag de
    // fenêtre, rafale disque). Trop courte, l'octet est déjà en RETARD au moment
    // d'être programmé et part immédiatement — la gigue revient. C'est ce que compte
    // lateBytes() : le réglage a un témoin, on n'a pas à deviner où est la limite.
    static constexpr int kDefaultLeadMs = 30;
    void setLeadMs(int ms);
    int  leadMs() const { return leadMs_; }
    uint64_t lateBytes() const { return lateBytes_.load(std::memory_order_relaxed); }
    void anchor(int64_t cycle, std::chrono::steady_clock::time_point hostTime);
    void byteAt(uint8_t b, int64_t cycle);

    // --- MIDI Time Code tiré du transport du ST (cf. MidiTimecode.hpp) ----------
    // 0 = coupé ; 24, 25, 29 (= 29,97 drop-frame), 30. Suit Start/Stop/Continue, le
    // Song Position Pointer et l'horloge que le ST émet, et intercale Quarter Frames
    // et Full Frames dans la livraison HORODATÉE, entre deux messages du ST — vers
    // toutes les destinations et le port virtuel, comme tout message système. Pour
    // un logiciel hôte qui ne se cale que sur un code temporel (Logic Pro).
    void setMtcRate(int rate);
    int  mtcRate() const { return mtcRate_.load(std::memory_order_relaxed); }

private:
    std::atomic<int> leadMs_{kDefaultLeadMs};
    std::atomic<int> mtcRate_{0};
    // Octets déjà en retard au moment d'être programmés : l'avance était trop courte
    // pour absorber le retard de la boucle hôte. Lu par l'interface.
    std::atomic<uint64_t> lateBytes_{0};

    void* graph_ = nullptr;             // AUGraph (macOS)
    void* synth_ = nullptr;             // AudioUnit DLSMusicDevice (macOS)
    uint32_t client_ = 0;               // MIDIClientRef (macOS)
    uint32_t src_ = 0;                  // MIDIEndpointRef / port ALSA + 1 (0 = fermé)
    void* seq_ = nullptr;               // snd_seq_t*        (Linux)
    void* enc_ = nullptr;               // snd_midi_event_t* (Linux)
    // Destinations matérielles. macOS : outPort_ = MIDIPortRef, ep = MIDIEndpointRef.
    // ALSA : ep = (client << 8 | port) + 1, ADRESSÉ EXPLICITEMENT à chaque envoi
    // (pas d'abonnement : un abonné recevrait tout, ce qui interdirait le filtrage
    // par canal). D'où ensurePort_(), qui crée le port source même quand la case
    // « port virtuel » est décochée — sans port, rien d'où émettre.
    // winmm : ep = HMIDIOUT, ouvert PAR APPAREIL (pas de port partagé — sous Windows
    // un port MIDI est EXCLUSIF, ce qui est aussi pourquoi une ouverture peut échouer
    // en MMSYSERR_ALLOCATED là où ALSA et CoreMIDI multiplexent sans rien dire).
    uint32_t outPort_ = 0;
    // uintptr_t et non uint32_t : un HMIDIOUT fait 64 bits. CoreMIDI (MIDIEndpointRef,
    // 32 bits) et ALSA (client<<8|port) y tiennent sans changer de sens.
    struct OpenDest { std::string name; uint16_t channels; uintptr_t ep; std::string uid; };
    std::vector<OpenDest> dests_;
    // winmm : les SysEx EN VOL (std::vector<SysExJob>*, cf. le .cpp). midiOutLongMsg
    // rend la main aussitôt mais le tampon appartient au pilote jusqu'à MHDR_DONE, et
    // un dump de patch de 4 Ko met plus d'une seconde à passer à 31250 bauds : on ne
    // peut ni attendre (le thread de livraison porte tout le MIDI), ni libérer.
    void* sysex_ = nullptr;
    // Windows : résolution du minuteur système portée à 1 ms tant qu'une sortie est
    // ouverte (cf. raiseTimerRes_). Sans elle, le cadencement de la livraison
    // horodatée serait quantifié à ~15,6 ms — tout le mécanisme de lead/gigue
    // ci-dessus deviendrait décoratif.
    bool timerRaised_ = false;
    void updateTimerRes_();             // (verrou outMtx_ DÉJÀ pris)
    bool userPort_ = false;             // le port virtuel a-t-il été demandé POUR LUI-MÊME ?
    void sendTo(const OpenDest& d, const uint8_t* msg, int len);   // (verrou DÉJÀ pris)
    // winmm : un SysEx passe par midiOutLongMsg, donc par un tampon qui doit survivre
    // à l'appel. `reapSysEx_(false)` récolte au passage ce que le pilote a fini de
    // transmettre ; `reapSysEx_(true)` vide tout (après midiOutReset, qui marque
    // d'office les tampons comme terminés). Sans backend, les deux ne font rien.
    void sendSysEx_(uintptr_t handle, const uint8_t* msg, int len);
    void reapSysEx_(bool all);
    bool ensurePort_();                 // crée client/port si besoin (partagé b + c)
    void releasePort_();                // détruit ce que plus personne n'utilise

    // Reconstitution des messages : partagée avec MidiInHost (cf. MidiMessageParser).
    neost::midi::Parser parser_;
    // `parser_` n'appartient QU'AU THREAD DE LIVRAISON. panic() tourne sur le thread
    // principal et faisait `parser_.reset()` pendant que le worker était dans
    // `parser_.byte()` — course de données sur le `std::vector` de SysEx, et le
    // chemin est AUTOMATIQUE (setDestinations → closeDestinations → panic, déclenché
    // à 1 Hz par la boucle GUI). Il pose donc une demande, que le worker honore entre
    // deux octets — l'endroit exact où une remise à zéro a un sens.
    std::atomic<bool> parserResetReq_{false};

    void emit(const uint8_t* msg, int len);     // message complet → synthé + port
    void parse(uint8_t b);                      // octet → parseur → emit (thread de livraison)

    // File horodatée + thread de livraison. `len` = 0 : un octet du ST, à décoder ;
    // `len` > 0 : un message COMPLET produit par NeoST (MTC), livré tel quel — il
    // s'intercale entre deux octets du ST sans jamais toucher au décodeur, donc sans
    // couper le message en cours.
    struct Pending {
        std::chrono::steady_clock::time_point when;
        uint8_t b = 0;
        uint8_t len = 0;
        uint8_t msg[10] = {};
    };
    // Générateur MTC (sous mtx_) et son injection dans la file, triée par échéance.
    neost::midi::mtc::Generator mtc_{kCpuHz};
    std::chrono::steady_clock::time_point whenFor_(int64_t cycle) const;   // (mtx_ pris)
    void injectAt_(const uint8_t* msg, int len, int64_t cycle);           // (mtx_ pris)
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Pending> queue_;
    std::thread worker_;
    bool stop_ = false;
    int64_t anchorCycle_ = 0;
    std::chrono::steady_clock::time_point anchorHost_{};
    bool anchored_ = false;
    std::mutex outMtx_;                         // protège synth_/src_ contre close pendant emit
    void workerLoop();
};
