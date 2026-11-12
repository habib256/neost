// =============================================================================
//  MidiAlsa.hpp — Ce que le séquenceur ALSA dit des ports MIDI, et sous quel nom.
//  Partagé par MidiOutHost (destinations) et MidiInHost (sources) : seules les
//  capacités demandées les distinguent. Pendant Linux de MidiWinmm.hpp.
//
//  Chaque port rendu porte son ADRESSE (client:port), valable pour CETTE session du
//  séquenceur seulement, et un identifiant STABLE quand la carte du client en a un
//  (alsaUid, cf. MidiEndpoint.hpp) : c'est ce qui sépare deux claviers du même
//  modèle même quand un seul est branché. L'ancienne version retrouvait le port par
//  son nom et son RANG parmi les homonymes, après une seconde énumération : elle
//  n'avait aucun moyen de savoir lequel des deux claviers était là.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#if defined(NEOST_MIDI_ALSA)

#include <alsa/asoundlib.h>

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "audio/MidiEndpoint.hpp"

namespace neost::midi::alsa {

struct Port {
    Endpoint ep;          // « client: port » + identifiant stable (ou vide)
    int client = -1;
    int port   = -1;
};

// Nom long de la carte `card`, ou vide. snd_card_get_longname alloue : on libère.
inline std::string cardLongname(int card) {
    char* name = nullptr;
    if (card < 0 || snd_card_get_longname(card, &name) < 0 || !name) return {};
    std::string s(name);
    std::free(name);
    return s;
}

// Tous les ports dont les capacités contiennent `caps` (READ|SUBS_READ pour une
// source, WRITE|SUBS_WRITE pour une destination), sauf les nôtres et le client
// système. L'ordre est celui du séquenceur, et c'est sur CETTE liste que
// matchEndpoints apparie : l'index rendu désigne directement client et port.
inline std::vector<Port> enumerate(snd_seq_t* seq, unsigned caps) {
    std::vector<Port> out;
    if (!seq) return out;
    const int self = snd_seq_client_id(seq);
    std::map<int, std::string> longnames;   // une requête par carte, pas par port
    snd_seq_client_info_t* ci = nullptr; snd_seq_port_info_t* pi = nullptr;
    snd_seq_client_info_malloc(&ci); snd_seq_port_info_malloc(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        const int cid = snd_seq_client_info_get_client(ci);
        if (cid == self || cid == SND_SEQ_CLIENT_SYSTEM) continue;   // pas nous, pas le système
        // Seul un client NOYAU appartient à une carte (alsa-lib : « Only available
        // for SND_SEQ_KERNEL_CLIENT clients », rend -1 sinon). Un client logiciel
        // (FluidSynth, un DAW) n'a pas d'adresse : identifiant vide, repli par nom.
        int card = -1;
        if (snd_seq_client_info_get_type(ci) == SND_SEQ_KERNEL_CLIENT)
            card = snd_seq_client_info_get_card(ci);
        if (card >= 0 && !longnames.count(card)) longnames[card] = cardLongname(card);
        const std::string clientName = snd_seq_client_info_get_name(ci);
        snd_seq_port_info_set_client(pi, cid);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            if ((snd_seq_port_info_get_capability(pi) & caps) != caps) continue;
            Port p;
            p.client = cid;
            p.port   = snd_seq_port_info_get_port(pi);
            p.ep.name = clientName + ": " + snd_seq_port_info_get_name(pi);
            if (card >= 0) p.ep.uid = alsaUid(longnames[card], p.port);
            out.push_back(std::move(p));
        }
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
    return out;
}

// Variante qui ouvre (et referme) sa propre session : pour les listes de l'interface.
inline std::vector<Port> enumerate(unsigned caps) {
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return {};
    std::vector<Port> out = enumerate(seq, caps);
    snd_seq_close(seq);
    return out;
}

inline std::vector<Endpoint> endpoints(const std::vector<Port>& ports) {
    std::vector<Endpoint> out;
    out.reserve(ports.size());
    for (const Port& p : ports) out.push_back(p.ep);
    return out;
}

} // namespace neost::midi::alsa

#endif // NEOST_MIDI_ALSA
