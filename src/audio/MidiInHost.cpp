// =============================================================================
//  MidiInHost.cpp — cf. MidiInHost.hpp. API C de CoreMIDI / ALSA, pas d'Objective-C.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "audio/MidiInHost.hpp"

#include <cstdio>
#include <cstring>

#if defined(NEOST_MIDI_ALSA)
#include <alsa/asoundlib.h>
#include <poll.h>
#endif

#ifdef __APPLE__
#include <CoreMIDI/CoreMIDI.h>
#endif

#if defined(NEOST_MIDI_WINMM)
#include "audio/MidiWinmm.hpp"      // tire windows.h + mmsystem.h
#include <memory>

namespace {
// Tampons de réception SysEx d'UN appareil. Quatre suffisent : le pilote les remplit
// à tour de rôle et on les ré-arme aussitôt lus, à 31250 bauds il n'y a pas de course
// à gagner. 1 Ko chacun, et un SysEx plus long arrive simplement en plusieurs
// morceaux — le décodeur de la source le recolle (c'est tout son travail).
constexpr int  kSysExBuffers = 4;
constexpr int  kSysExBufSize = 1024;
struct InBuffer {
    MIDIHDR hdr{};
    std::vector<char> data = std::vector<char>(kSysExBufSize, 0);
};
using InBuffers = std::vector<std::unique_ptr<InBuffer>>;
} // namespace
#endif

MidiInHost::~MidiInHost() { close(); }

bool MidiInHost::available() {
#if defined(__APPLE__) || defined(NEOST_MIDI_ALSA) || defined(NEOST_MIDI_WINMM)
    return true;
#else
    return false;
#endif
}

// -----------------------------------------------------------------------------
//  Fusion : octets d'une source → messages → file commune
// -----------------------------------------------------------------------------
void MidiInHost::feed(Device& d, const uint8_t* data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        d.parser.byte(data[i], [this, &d](const uint8_t* msg, int len) {
            emitMessage(d, msg, len);
        });
}

void MidiInHost::emitMessage(Device& d, const uint8_t* msg, int len) {
    if (len <= 0) return;
    uint8_t head = msg[0];
    // CANALISATION : le quartet de canal des messages de VOIE ($80-$EF) est
    // réécrit. Les messages système ($F0-$FF) n'ont pas de canal — on n'y touche
    // pas, sinon on casserait horloge, SysEx et start/stop.
    if (d.forceChannel >= 1 && d.forceChannel <= 16 && head >= 0x80 && head < 0xF0)
        head = uint8_t((head & 0xF0) | uint8_t(d.forceChannel - 1));

    std::lock_guard<std::mutex> lk(mtx_);
    // Running status : on ne réécrit le statut que s'il a CHANGÉ dans le flux
    // fusionné. Une source seule garde donc son running status (le flux ne
    // grossit pas) ; deux sources qui alternent le voient réinséré — sans quoi
    // les données de l'une seraient lues sous le statut de l'autre.
    const bool voice = head >= 0x80 && head < 0xF0;
    const bool omitStatus = voice && head == lastStatus_ && len > 1;
    const std::size_t need = omitStatus ? std::size_t(len - 1) : std::size_t(len);

    // Saturé : c'est le MESSAGE NEUF ENTIER qui tombe. Jeter un fragment
    // laisserait des octets orphelins dans le flux — le pire des deux maux, et
    // c'est aussi la règle du 6850 en overrun (garder l'ancien, cf. MidiAcia).
    // … sauf un message SEUL plus long que le tampon, file vide : un SysEx (dump de
    // patches) dépasse couramment kMaxJitter et était jeté EN ENTIER, même sans aucune
    // saturation — il n'atteignait jamais le ST. La file ne grossit pas pour autant :
    // le message suivant attendra qu'elle se vide.
    if (jitter_.size() + need > kMaxJitter && !jitter_.empty()) {
        dropped_.fetch_add(uint64_t(len), std::memory_order_relaxed);
        return;
    }
    if (!omitStatus) jitter_.push_back(head);
    for (int i = 1; i < len; ++i) jitter_.push_back(msg[i]);

    // Suivi du running status du flux : seuls les messages de VOIE l'établissent ;
    // le système commun ($F0-$F7) l'ANNULE ; le temps réel ($F8-$FF) le laisse
    // intact — c'est précisément pour ça qu'il peut tomber n'importe où.
    if (voice)             lastStatus_ = head;
    else if (head < 0xF8)  lastStatus_ = 0;

    pending_.store(jitter_.size(), std::memory_order_relaxed);
}

bool MidiInHost::tryPop(uint8_t& out) {
    // Chemin rapide sans verrou : l'ACIA appelle 3125 fois par seconde et repart
    // presque toujours les mains vides.
    if (pending_.load(std::memory_order_relaxed) == 0) return false;
    std::lock_guard<std::mutex> lk(mtx_);
    if (jitter_.empty()) return false;
    out = jitter_.front();
    jitter_.pop_front();
    pending_.store(jitter_.size(), std::memory_order_relaxed);
    delivered_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

MidiInHost::Device* MidiInHost::deviceForTest(int slot, int forceChannel) {
    while (int(devices_.size()) <= slot) {
        auto d = std::make_unique<Device>();
        d->owner = this;
        d->name = "test:" + std::to_string(devices_.size());
        devices_.push_back(std::move(d));
    }
    devices_[std::size_t(slot)]->forceChannel = forceChannel;
    return devices_[std::size_t(slot)].get();
}

void MidiInHost::pushForTest(int slot, const uint8_t* data, std::size_t n, int forceChannel) {
    feed(*deviceForTest(slot, forceChannel), data, n);
}

std::vector<neost::midi::Endpoint> MidiInHost::openEndpoints() const {
    std::vector<neost::midi::Endpoint> out;
    out.reserve(devices_.size());
    for (const auto& d : devices_) out.push_back({d->name, d->uid});
    return out;
}

std::vector<std::string> MidiInHost::openNames() const {
    std::vector<std::string> out;
    out.reserve(devices_.size());
    for (const auto& d : devices_) out.push_back(d->name);
    return out;
}

// -----------------------------------------------------------------------------
//  Énumération des sources
// -----------------------------------------------------------------------------
#if defined(__APPLE__) && !defined(NEOST_MIDI_ALSA)
namespace {
// kMIDIPropertyUniqueID : STABLE d'un branchement à l'autre. Seul critère qui sépare
// deux claviers du même modèle, qui portent rigoureusement le même nom.
std::string uniqueId(MIDIObjectRef obj) {
    SInt32 uid = 0;
    if (MIDIObjectGetIntegerProperty(obj, kMIDIPropertyUniqueID, &uid) != noErr) return {};
    return std::to_string(long(uid));
}

std::string displayName(MIDIObjectRef obj) {
    CFStringRef cf = nullptr;
    if (MIDIObjectGetStringProperty(obj, kMIDIPropertyDisplayName, &cf) != noErr || !cf) return {};
    char buf[256] = {0};
    const bool ok = CFStringGetCString(cf, buf, sizeof buf, kCFStringEncodingUTF8);
    CFRelease(cf);
    return ok ? std::string(buf) : std::string();
}
} // namespace
#endif

std::vector<neost::midi::Endpoint> MidiInHost::sources() {
    std::vector<neost::midi::Endpoint> out;
#if defined(NEOST_MIDI_ALSA)
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return out;
    const int self = snd_seq_client_id(seq);
    snd_seq_client_info_t* ci = nullptr; snd_seq_port_info_t* pi = nullptr;
    snd_seq_client_info_malloc(&ci); snd_seq_port_info_malloc(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        const int cid = snd_seq_client_info_get_client(ci);
        if (cid == self || cid == SND_SEQ_CLIENT_SYSTEM) continue;
        snd_seq_port_info_set_client(pi, cid);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const unsigned caps = snd_seq_port_info_get_capability(pi);
            // Une SOURCE se lit et se laisse abonner : READ | SUBS_READ.
            if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
                != (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) continue;
            // uid vide : ALSA n'a pas d'identifiant stable (cf. MidiEndpoint.hpp).
            out.push_back({std::string(snd_seq_client_info_get_name(ci)) + ": "
                           + snd_seq_port_info_get_name(pi), std::string()});
        }
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
    snd_seq_close(seq);
#elif defined(__APPLE__)
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i) {
        const MIDIEndpointRef e = MIDIGetSource(i);
        std::string nm = displayName(e);
        // « NeoST MIDI OUT » est NOTRE propre source virtuelle : la proposer en entrée
        // offrirait un bouclage OUT→IN déguisé, avec le larsen que la fiche de bouclage
        // débranchée par défaut cherche précisément à éviter.
        if (!nm.empty() && nm.rfind("NeoST", 0) != 0) out.push_back({std::move(nm), uniqueId(e)});
    }
#elif defined(NEOST_MIDI_WINMM)
    for (const auto& d : neost::midi::winmm::inputs()) out.push_back(d.ep);
#endif
    return out;
}

// -----------------------------------------------------------------------------
//  Ouverture / fermeture
// -----------------------------------------------------------------------------
#if defined(__APPLE__) && !defined(NEOST_MIDI_ALSA)
// Callback CoreMIDI : thread temps réel du serveur MIDI. srcConnRefCon identifie
// LA SOURCE (posé à la connexion) — c'est ce qui permet à chaque appareil d'avoir
// son propre décodeur, donc à la fusion de se faire aux frontières de messages.
void MidiInHost::coreMidiRead(const ::MIDIPacketList* pkts, void* refCon, void* srcRef) {
    auto* self = static_cast<MidiInHost*>(refCon);
    auto* dev  = static_cast<Device*>(srcRef);
    if (!self || !dev || !pkts) return;
    // srcConnRefCon n'est déréférencé qu'APRÈS vérification d'appartenance, sous
    // devMtx_ (cf. sa déclaration : rien ne garantit qu'un callback en vol soit
    // terminé quand la connexion est coupée). Le verrou reste pris pendant feed :
    // c'est lui qui fait attendre close() jusqu'à la fin du paquet en cours.
    std::lock_guard<std::mutex> lk(self->devMtx_);
    bool ours = false;
    for (const auto& d : self->devices_)
        if (d.get() == dev) { ours = true; break; }
    if (!ours) return;                       // callback tardif : le Device est parti
    const MIDIPacket* p = &pkts->packet[0];
    for (UInt32 i = 0; i < pkts->numPackets; ++i) {
        self->feed(*dev, p->data, p->length);
        p = MIDIPacketNext(p);
    }
}
#endif

std::size_t MidiInHost::setDevices(const std::vector<Want>& want) {
    close();
    if (want.empty()) return 0;

    // Appariement AVANT ouverture : identifiant d'abord, nom ensuite, et jamais deux
    // fois le même point (cf. MidiEndpoint.hpp). Deux claviers du même modèle sont
    // ainsi ouverts chacun sur le sien, au lieu que le premier soit pris deux fois.
    const auto have = sources();
    std::vector<neost::midi::Wanted> keys;
    keys.reserve(want.size());
    for (const Want& w : want) keys.push_back({w.name, w.uid});
    const std::vector<int> pick = neost::midi::matchEndpoints(keys, have);
#if defined(NEOST_MIDI_ALSA)
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        std::fprintf(stderr, "[midi-in] ALSA sequencer unavailable\n");
        return 0;
    }
    snd_seq_set_client_name(seq, "NeoST");
    // UN port d'écoute pour toutes les sources : la fusion se fait chez nous, et
    // l'événement ALSA porte son adresse d'origine (ev->source) — de quoi retrouver
    // le décodeur de chaque appareil.
    const int port = snd_seq_create_simple_port(
        seq, "NeoST MIDI IN",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) { snd_seq_close(seq); return 0; }
    snd_midi_event_t* dec = nullptr;
    if (snd_midi_event_new(1024, &dec) < 0) {
        snd_seq_delete_simple_port(seq, port); snd_seq_close(seq);
        return 0;
    }
    // no_status(1) : chaque message ressort avec son statut. La compaction du
    // running status se fait ensuite, sur le flux FUSIONNÉ (cf. emitMessage).
    snd_midi_event_no_status(dec, 1);

    snd_seq_client_info_t* ci = nullptr; snd_seq_port_info_t* pi = nullptr;
    snd_seq_client_info_malloc(&ci); snd_seq_port_info_malloc(&pi);
    for (std::size_t wi = 0; wi < want.size(); ++wi) {
        if (pick[wi] < 0) continue;
        const Want& w = want[wi];
        const std::string target = have[std::size_t(pick[wi])].name;
        int rank = 0, seen = 0;
        for (int e = 0; e < pick[wi]; ++e) if (have[std::size_t(e)].name == target) ++rank;
        int foundC = -1, foundP = -1;
        snd_seq_client_info_set_client(ci, -1);
        while (foundC < 0 && snd_seq_query_next_client(seq, ci) >= 0) {
            const int cid = snd_seq_client_info_get_client(ci);
            if (cid == snd_seq_client_id(seq) || cid == SND_SEQ_CLIENT_SYSTEM) continue;
            snd_seq_port_info_set_client(pi, cid);
            snd_seq_port_info_set_port(pi, -1);
            while (snd_seq_query_next_port(seq, pi) >= 0) {
                const unsigned caps = snd_seq_port_info_get_capability(pi);
                if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
                    != (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) continue;
                if (std::string(snd_seq_client_info_get_name(ci)) + ": "
                    + snd_seq_port_info_get_name(pi) == target && seen++ == rank) {
                    foundC = cid; foundP = snd_seq_port_info_get_port(pi);
                    break;
                }
            }
        }
        if (foundC < 0 || snd_seq_connect_from(seq, port, foundC, foundP) < 0) continue;
        auto d = std::make_unique<Device>();
        d->owner = this; d->name = target; d->forceChannel = w.forceChannel;
        d->src = (uint32_t(foundC) << 8 | uint32_t(foundP)) + 1;
        devices_.push_back(std::move(d));
        std::fprintf(stderr, "[midi-in] \"%s\" -> MIDI IN (ALSA %d:%d)%s\n",
                     target.c_str(), foundC, foundP,
                     w.forceChannel ? " channelized" : "");
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
    if (devices_.empty()) {
        snd_midi_event_free(dec);
        snd_seq_delete_simple_port(seq, port); snd_seq_close(seq);
        return 0;
    }
    seq_ = seq; dec_ = dec; port_ = uint32_t(port) + 1;
    stop_ = false;
    reader_ = std::thread([this] { readerLoop(); });
    return devices_.size();
#elif defined(__APPLE__)
    MIDIClientRef client = 0;
    if (const OSStatus st = MIDIClientCreate(CFSTR("NeoST IN"), nullptr, nullptr, &client); st != noErr) {
        std::fprintf(stderr, "[midi-in] MIDIClientCreate failed (OSStatus %d): no CoreMIDI "
                     "access from this process?\n", int(st));
        return 0;
    }
    MIDIPortRef port = 0;
    if (MIDIInputPortCreate(client, CFSTR("NeoST IN"), &MidiInHost::coreMidiRead, this, &port) != noErr) {
        MIDIClientDispose(client);
        std::fprintf(stderr, "[midi-in] cannot create the CoreMIDI input port\n");
        return 0;
    }
    client_ = client; port_ = port;
    const ItemCount nsrc = MIDIGetNumberOfSources();
    for (std::size_t wi = 0; wi < want.size(); ++wi) {
        if (pick[wi] < 0) continue;        // débranché : l'appelant re-tentera
        const Want& w = want[wi];
        const auto& ep = have[std::size_t(pick[wi])];
        // Retrouvé par son IDENTIFIANT : c'est le seul lien fiable entre la liste
        // rendue par sources() (qui saute les noms vides et les nôtres) et CoreMIDI.
        MIDIEndpointRef found = 0;
        for (ItemCount i = 0; i < nsrc; ++i) {
            const MIDIEndpointRef e = MIDIGetSource(i);
            if (uniqueId(e) == ep.uid && displayName(e) == ep.name) { found = e; break; }
        }
        if (!found) continue;
        auto d = std::make_unique<Device>();
        d->owner = this; d->name = ep.name; d->uid = ep.uid;
        d->forceChannel = w.forceChannel; d->src = found;
        Device* dp = d.get();
        // Dans la liste AVANT la connexion : le callback valide l'appartenance
        // sous devMtx_, et un premier paquet peut arriver dès ConnectSource.
        {
            std::lock_guard<std::mutex> lk(devMtx_);
            devices_.push_back(std::move(d));
        }
        // refCon de la CONNEXION = l'appareil : le callback saura de qui vient
        // chaque paquet, donc quel décodeur alimenter.
        if (MIDIPortConnectSource(port, found, dp) != noErr) {
            std::lock_guard<std::mutex> lk(devMtx_);
            devices_.pop_back();
            std::fprintf(stderr, "[midi-in] cannot connect \"%s\"\n", w.name.c_str());
            continue;
        }
        std::fprintf(stderr, "[midi-in] \"%s\" -> MIDI IN (CoreMIDI uid %s)%s\n",
                     ep.name.c_str(), ep.uid.c_str(),
                     w.forceChannel ? " channelized" : "");
        // Repli par NOM assumé (un clavier remplacé par le même modèle doit marcher
        // sans toucher la config) — mais plus en silence : l'identifiant dit que ce
        // n'est PAS la machine configurée, l'utilisateur doit pouvoir le lire.
        if (!w.uid.empty() && !ep.uid.empty() && ep.uid != w.uid)
            std::fprintf(stderr, "[midi-in] note: \"%s\" opened by NAME — its unique id "
                         "differs (configured %s, found %s): same-model replacement?\n",
                         ep.name.c_str(), w.uid.c_str(), ep.uid.c_str());
    }
    if (devices_.empty()) { closeBackend(); return 0; }
    return devices_.size();
#elif defined(NEOST_MIDI_WINMM)
    // Le thread PORTE la file de messages, et midiInOpen en prend l'identifiant : il
    // doit donc exister, file créée, AVANT la première ouverture. On l'attend.
    stop_.store(false);
    reader_ = std::thread([this] { readerLoop(); });
    {
        std::unique_lock<std::mutex> lk(startMtx_);
        startCv_.wait(lk, [this] { return readerTid_.load() != 0; });
    }
    const auto devs = neost::midi::winmm::inputs();
    for (std::size_t wi = 0; wi < want.size(); ++wi) {
        if (pick[wi] < 0) continue;            // débranché : l'appelant re-tentera
        const Want& w = want[wi];
        const auto& ep = have[std::size_t(pick[wi])];
        const int idx = neost::midi::winmm::reFind(devs, ep, pick[wi]);
        if (idx < 0) continue;
        HMIDIIN h = nullptr;
        const MMRESULT r = ::midiInOpen(&h, devs[std::size_t(idx)].id,
                                        DWORD_PTR(readerTid_.load()), 0, CALLBACK_THREAD);
        if (r != MMSYSERR_NOERROR || !h) {
            std::fprintf(stderr, "[midi-in] cannot open \"%s\": %s\n",
                         ep.name.c_str(), neost::midi::winmm::errorText(r));
            continue;
        }
        auto d = std::make_unique<Device>();
        d->owner = this; d->name = ep.name; d->uid = ep.uid;
        d->forceChannel = w.forceChannel; d->src = reinterpret_cast<uintptr_t>(h);
        // Tampons SysEx armés AVANT le démarrage : un dump de patch envoyé dans la
        // seconde qui suit ne doit pas tomber faute de tampon libre.
        auto* bufs = new InBuffers();
        for (int b = 0; b < kSysExBuffers; ++b) {
            auto ib = std::make_unique<InBuffer>();
            ib->hdr.lpData = ib->data.data();
            ib->hdr.dwBufferLength = DWORD(ib->data.size());
            if (::midiInPrepareHeader(h, &ib->hdr, sizeof(MIDIHDR)) != MMSYSERR_NOERROR) break;
            if (::midiInAddBuffer(h, &ib->hdr, sizeof(MIDIHDR)) != MMSYSERR_NOERROR) {
                ::midiInUnprepareHeader(h, &ib->hdr, sizeof(MIDIHDR));
                break;
            }
            bufs->push_back(std::move(ib));
        }
        d->hdrs = bufs;
        {
            std::lock_guard<std::mutex> lk(devMtx_);
            devices_.push_back(std::move(d));
        }
        std::fprintf(stderr, "[midi-in] \"%s\" -> MIDI IN (winmm #%u%s%s)%s\n",
                     ep.name.c_str(), unsigned(devs[std::size_t(idx)].id),
                     ep.uid.empty() ? "" : ", uid ", ep.uid.c_str(),
                     w.forceChannel ? " channelized" : "");
        // Repli par NOM : sous Windows l'identifiant porte le CHEMIN USB, donc un
        // simple changement de prise le fait varier — d'où le « ? » du message.
        if (!w.uid.empty() && !ep.uid.empty() && ep.uid != w.uid)
            std::fprintf(stderr, "[midi-in] note: \"%s\" opened by NAME — its unique id "
                         "differs (configured %s, found %s): other USB port, or "
                         "same-model replacement?\n",
                         ep.name.c_str(), w.uid.c_str(), ep.uid.c_str());
    }
    if (devices_.empty()) { closeBackend(); return 0; }
    // Démarrage EN DERNIER : aucun MM_MIM_DATA ne peut donc arriver avant que la
    // liste des appareils soit complète, et le thread ne cherchera jamais un
    // appareil qui n'y est pas encore.
    for (const auto& d : devices_) ::midiInStart(reinterpret_cast<HMIDIIN>(d->src));
    return devices_.size();
#else
    (void)want;
    return 0;
#endif
}

void MidiInHost::closeBackend() {
#if defined(NEOST_MIDI_ALSA)
    if (reader_.joinable()) { stop_ = true; reader_.join(); }
    if (seq_) {
        snd_seq_t* seq = static_cast<snd_seq_t*>(seq_);
        for (const auto& d : devices_)
            if (port_ && d->src)
                snd_seq_disconnect_from(seq, int(port_) - 1,
                                        int((d->src - 1) >> 8), int((d->src - 1) & 0xFF));
        if (port_) snd_seq_delete_simple_port(seq, int(port_) - 1);
        snd_seq_close(seq); seq_ = nullptr;
    }
    if (dec_) { snd_midi_event_free(static_cast<snd_midi_event_t*>(dec_)); dec_ = nullptr; }
#elif defined(__APPLE__)
    if (port_) {
        for (const auto& d : devices_)
            if (d->src) MIDIPortDisconnectSource(port_, d->src);
        MIDIPortDispose(port_);
    }
    if (client_) MIDIClientDispose(client_);
#elif defined(NEOST_MIDI_WINMM)
    {
        // Sous devMtx_ : le thread lecteur peut être EN TRAIN de lire le tampon d'un
        // MIDIHDR (feed) ; le libérer sous ses pieds serait un usage après libération.
        // Le verrou est relâché AVANT la jonction — un thread qui l'attend doit
        // pouvoir l'obtenir pour atteindre son GetMessage suivant, sans quoi le join
        // ci-dessous ne rendrait jamais la main.
        std::lock_guard<std::mutex> lk(devMtx_);
        stop_.store(true);          // interdit le ré-armement des tampons rendus
        for (const auto& d : devices_) {
            HMIDIIN h = reinterpret_cast<HMIDIIN>(d->src);
            if (h) {
                ::midiInStop(h);
                ::midiInReset(h);   // rend TOUS les tampons, marqués MHDR_DONE
            }
            if (d->hdrs) {
                auto* bufs = static_cast<InBuffers*>(d->hdrs);
                for (auto& b : *bufs)
                    if (h) ::midiInUnprepareHeader(h, &b->hdr, sizeof(MIDIHDR));
                delete bufs;
                d->hdrs = nullptr;
            }
            if (h) ::midiInClose(h);
            // Le thread ne retrouvera plus cet appareil : les MM_MIM_LONGDATA que
            // midiInReset vient de poster tomberont dans le vide, ce qui est
            // exactement ce qu'on veut (leurs tampons n'existent plus).
            d->src = 0;
        }
    }
    if (const uint32_t tid = readerTid_.load()) ::PostThreadMessageW(tid, WM_QUIT, 0, 0);
    if (reader_.joinable()) reader_.join();
    readerTid_.store(0);
#endif
    client_ = port_ = 0;
}

void MidiInHost::close() {
    closeBackend();
    {
        // Sous le verrou du callback : un paquet en cours de livraison finit
        // d'être décodé AVANT que ses Device ne soient libérés.
        std::lock_guard<std::mutex> lk(devMtx_);
        devices_.clear();
    }
    std::lock_guard<std::mutex> lk(mtx_);
    jitter_.clear();     // un appareil débranché ne doit pas rejouer son passé
    lastStatus_ = 0;
    pending_.store(0, std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------
//  Boucle de lecture ALSA (CoreMIDI n'en a pas besoin : il appelle notre callback)
// -----------------------------------------------------------------------------
void MidiInHost::readerLoop() {
#if defined(NEOST_MIDI_ALSA)
    snd_seq_t* seq = static_cast<snd_seq_t*>(seq_);
    auto* dec = static_cast<snd_midi_event_t*>(dec_);
    const int nfds = snd_seq_poll_descriptors_count(seq, POLLIN);
    std::vector<pollfd> pfds(nfds > 0 ? std::size_t(nfds) : 1);
    while (!stop_) {
        snd_seq_poll_descriptors(seq, pfds.data(), unsigned(pfds.size()), POLLIN);
        // Timeout de 100 ms : c'est ce qui rend `stop_` efficace — un
        // snd_seq_event_input bloquant ne s'interrompt pas proprement.
        if (::poll(pfds.data(), nfds_t(pfds.size()), 100) <= 0) continue;
        snd_seq_event_t* ev = nullptr;
        while (!stop_ && snd_seq_event_input(seq, &ev) >= 0 && ev) {
            // Retrouve l'appareil par l'adresse d'origine de l'événement : chaque
            // source a son décodeur, sans quoi la fusion mélangerait des octets.
            const uint32_t key = (uint32_t(ev->source.client) << 8 | ev->source.port) + 1;
            Device* dev = nullptr;
            for (const auto& d : devices_) if (d->src == key) { dev = d.get(); break; }
            if (dev) {
                if (ev->type == SND_SEQ_EVENT_SYSEX) {
                    // SysEx livré tel quel : snd_midi_event_decode rend -ENOMEM dès que
                    // le bloc dépasse le tampon, et un dump envoyé d'un seul tenant par
                    // un client en espace utilisateur (aplaymidi, DAW) disparaissait.
                    if (ev->data.ext.ptr && ev->data.ext.len)
                        feed(*dev, static_cast<const uint8_t*>(ev->data.ext.ptr),
                             std::size_t(ev->data.ext.len));
                } else {
                    uint8_t buf[256];
                    const long n = snd_midi_event_decode(dec, buf, sizeof buf, ev);
                    if (n > 0) feed(*dev, buf, std::size_t(n));
                }
            }
            if (snd_seq_event_input_pending(seq, 0) <= 0) break;
        }
    }
#elif defined(NEOST_MIDI_WINMM)
    MSG msg;
    // Force la création de la file AVANT de publier l'identifiant du thread : winmm
    // POSTE ses messages (CALLBACK_THREAD), et poster vers un thread sans file échoue
    // — silencieusement, ce qui donnerait un appareil ouvert mais définitivement muet.
    ::PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    {
        std::lock_guard<std::mutex> lk(startMtx_);
        readerTid_.store(::GetCurrentThreadId());
    }
    startCv_.notify_all();

    while (!stop_.load()) {
        const BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) break;                       // WM_QUIT (0) ou erreur (-1)
        if (msg.message != MM_MIM_DATA && msg.message != MM_MIM_LONGDATA) continue;
        // wParam = le HMIDIIN de l'appareil : c'est lui qui dit de QUI vient le
        // message, donc quel décodeur alimenter (la fusion se fait aux frontières
        // de messages, cf. l'en-tête).
        std::lock_guard<std::mutex> lk(devMtx_);
        Device* dev = nullptr;
        for (const auto& d : devices_)
            if (d->src && d->src == uintptr_t(msg.wParam)) { dev = d.get(); break; }
        if (!dev) continue;                        // appareil déjà fermé
        if (msg.message == MM_MIM_DATA) {
            // UN message court EMPAQUETÉ dans un mot. Sa longueur se déduit du
            // statut : pousser les trois octets aveuglément ajouterait une donnée
            // fantôme après un Program Change (cf. shortMessageLength).
            const DWORD packed = DWORD(msg.lParam);
            const uint8_t b[3] = {uint8_t(packed), uint8_t(packed >> 8), uint8_t(packed >> 16)};
            const int len = neost::midi::shortMessageLength(b[0]);
            if (len > 0) feed(*dev, b, std::size_t(len));
        } else {
            auto* hdr = reinterpret_cast<MIDIHDR*>(msg.lParam);
            if (!hdr) continue;
            if (hdr->dwBytesRecorded > 0)
                feed(*dev, reinterpret_cast<const uint8_t*>(hdr->lpData),
                     std::size_t(hdr->dwBytesRecorded));
            // Ré-armement : LÉGAL ICI (thread ordinaire), interdit dans un callback
            // winmm — c'est toute la raison du CALLBACK_THREAD. Jamais pendant la
            // fermeture : midiInReset rend les tampons pour qu'on les libère, les
            // remettre en file les reprendrait au pilote.
            if (!stop_.load())
                ::midiInAddBuffer(reinterpret_cast<HMIDIIN>(dev->src), hdr, sizeof(MIDIHDR));
        }
    }
#endif
}
