// =============================================================================
//  MidiEndpoint.hpp — Désigner un appareil MIDI hôte SANS ambiguïté.
//
//  Le nom d'affichage ne suffit pas. Deux machines du MÊME MODÈLE branchées
//  ensemble — deux claviers identiques, le cas d'un studio — portent EXACTEMENT le
//  même nom. Les désigner par leur nom, c'est alors ouvrir deux fois le même
//  appareil et laisser l'autre muet, sans que rien ne le dise.
//
//  L'index, lui, ne peut pas servir de solution : il se renumérote dès qu'on
//  débranche un voisin, et une config mémorisée en index se mettrait à piloter le
//  mauvais appareil au branchement suivant. C'est le piège inverse.
//
//  D'où le couple (nom, identifiant unique) :
//    · CoreMIDI fournit kMIDIPropertyUniqueID, stable d'un branchement à l'autre ;
//    · winmm, le chemin d'interface USB (cf. MidiWinmm.hpp) ;
//    · ALSA n'a pas d'identifiant d'appareil (client:port change à chaque
//      branchement) — mais la carte son du client porte l'ADRESSE USB dans son nom
//      long : alsaUid() ci-dessous en tire un identifiant (cf. MidiAlsa.hpp).
//
//  L'appariement ci-dessous marche dans les deux cas, et c'est pour ça qu'il est
//  écrit ICI, en fonction PURE : il se teste sans le moindre appareil branché.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace neost::midi {

// Un point de terminaison tel que l'hôte le présente.
struct Endpoint {
    std::string name;
    std::string uid;      // vide = l'hôte n'en fournit pas (ALSA)
};

// Un appareil VOULU par la configuration.
struct Wanted {
    std::string name;
    std::string uid;      // vide = config d'avant les identifiants, ou hôte sans uid
};

// Pour chaque `want`, l'INDEX du point de terminaison retenu, ou -1 s'il est absent.
// Un même point n'est jamais attribué deux fois : c'est ce qui permet à deux appareils
// homonymes d'être ouverts tous les deux, chacun sur le sien, même sans identifiant.
//
// Deux passes, et l'ordre compte : les correspondances par IDENTIFIANT sont toutes
// résolues AVANT les correspondances par nom. Sinon un appareil désigné par son nom
// pourrait rafler le point de terminaison qu'un autre réclamait par son identifiant —
// le critère fiable doit servir en premier.
inline std::vector<int> matchEndpoints(const std::vector<Wanted>& want,
                                       const std::vector<Endpoint>& have) {
    std::vector<int> out(want.size(), -1);
    std::vector<bool> taken(have.size(), false);

    for (std::size_t w = 0; w < want.size(); ++w) {
        if (want[w].uid.empty()) continue;
        for (std::size_t e = 0; e < have.size(); ++e)
            if (!taken[e] && have[e].uid == want[w].uid) { out[w] = int(e); taken[e] = true; break; }
    }
    for (std::size_t w = 0; w < want.size(); ++w) {
        if (out[w] >= 0) continue;
        for (std::size_t e = 0; e < have.size(); ++e)
            if (!taken[e] && have[e].name == want[w].name) { out[w] = int(e); taken[e] = true; break; }
    }
    return out;
}

// Combien d'appareils voulus un setDevices/setDestinations OUVRIRAIT maintenant.
// C'est la garde de la boucle de reconnexion à 1 Hz : elle ne doit rappeler
// l'ouverture que si ce nombre DIFFÈRE de ce qui est ouvert. L'ancienne garde
// comparait au nombre d'appareils CONFIGURÉS : un appareil durablement absent la
// rendait vraie à chaque seconde, et chaque re-tentative ferme tout d'abord —
// donc paniquait (All Notes Off) les appareils encore branchés une fois par
// seconde en sortie, et jetait les octets en attente une fois par seconde en
// entrée. Un appareil absent doit coûter zéro tant que rien ne change.
inline std::size_t countMatchable(const std::vector<Wanted>& want,
                                  const std::vector<Endpoint>& have) {
    std::size_t n = 0;
    for (int idx : matchEndpoints(want, have)) if (idx >= 0) ++n;
    return n;
}

// Apprentissage des identifiants manquants. `want` = les entrées de config (uid
// vide = à apprendre) ; `open` = les points réellement OUVERTS, dans l'ordre de la
// config. Renvoie true si au moins un identifiant a été appris.
//
// JAMAIS deux fois le même identifiant — c'est le pendant de matchEndpoints, et
// l'oubli qui rendait l'apprentissage FAUX pour son cas d'usage même : deux
// homonymes branchés, config sans uid → les deux lignes apprenaient l'identifiant
// du PREMIER point ouvert. Tant que les deux claviers restaient branchés, le repli
// par nom masquait l'erreur ; on débranchait le premier, et le masque de canaux de
// la seconde ligne pilotait le mauvais appareil. Une ligne dont l'homonyme ouvert
// est déjà réclamé reste donc VIDE : ne rien apprendre vaut mieux qu'apprendre faux.
inline bool learnUids(std::vector<Wanted>& want, const std::vector<Endpoint>& open) {
    std::vector<std::string> used;
    for (const Wanted& w : want) if (!w.uid.empty()) used.push_back(w.uid);
    const auto taken = [&used](const std::string& u) {
        return std::find(used.begin(), used.end(), u) != used.end();
    };
    bool learned = false;
    for (Wanted& w : want) {
        if (!w.uid.empty()) continue;
        for (const Endpoint& o : open)
            if (o.name == w.name && !o.uid.empty() && !taken(o.uid)) {
                w.uid = o.uid; used.push_back(o.uid); learned = true; break;
            }
    }
    return learned;
}

// ── ALSA : un identifiant tiré de l'ADRESSE de la carte ───────────────────────
// Sous ALSA, deux claviers du même modèle n'étaient discernables que branchés
// ENSEMBLE (règle « jamais deux fois le même point »). Un seul branché, la première
// ligne de config homonyme le prenait — avec le canal ou le masque de l'autre.
//
// Le séquenceur ne donne rien de stable, mais un client NOYAU appartient à une carte
// (snd_seq_client_info_get_card), et le nom long de la carte se termine par son
// adresse de bus. Pour l'USB, que tous les claviers maîtres utilisent :
//   Source : Linux sound/usb/card.c:526-590 @ v6.6, usb_audio_make_longname — nom long
//       = « <fabricant> <produit> at <chemin>, <vitesse> speed », où <chemin> vient
//       de usb_make_path (include/linux/usb.h:951-957 @ v6.6) :
//       « usb-<bus>-<devpath> », p. ex. « usb-0000:00:14.0-2 » ou « …-1.4 ».
// <bus> est le contrôleur, <devpath> la PRISE (hub compris) : stable d'un
// branchement et d'un démarrage à l'autre tant qu'on garde la même prise — le même
// régime que l'identifiant winmm. Changer de prise change l'identifiant ; le repli
// par nom de matchEndpoints rattrape ce cas.
// Le nom long d'une carte PCI finit de même par son adresse (« … at 0xf7f10000 irq
// 33 ») : l'extraction ci-dessous ne présume pas de l'USB.
//
// Rend l'adresse : ce qui suit le DERNIER « at » jusqu'à la première virgule.
// Vide si le nom long n'en a pas (préréglage du pilote sans chemin, client logiciel).
inline std::string alsaBusAddress(const std::string& longname) {
    const std::size_t at = longname.rfind(" at ");
    if (at == std::string::npos) return {};
    std::string addr = longname.substr(at + 4, longname.find(", ", at + 4) - (at + 4));
    while (!addr.empty() && addr.back() == ' ') addr.pop_back();
    return addr;
}

// Identifiant d'un PORT de séquenceur : adresse de la carte + numéro de port du
// client noyau (un appareil à plusieurs prises MIDI expose un port par prise, et ce
// numéro ne dépend que de l'appareil). Vide quand l'adresse manque : mieux vaut pas
// d'identifiant qu'un identifiant partagé par deux homonymes.
inline std::string alsaUid(const std::string& longname, int port) {
    const std::string addr = alsaBusAddress(longname);
    if (addr.empty() || port < 0) return {};
    return "alsa:" + addr + "/" + std::to_string(port);
}

// Étiquette d'affichage : le nom seul quand il est unique, suffixé « #n » quand
// plusieurs points le partagent. Sans ce suffixe, l'interface montrerait deux lignes
// rigoureusement identiques et l'utilisateur n'aurait aucun moyen de savoir laquelle
// est laquelle.
inline std::string displayLabel(const std::vector<Endpoint>& have, std::size_t idx) {
    if (idx >= have.size()) return {};
    std::size_t total = 0, rank = 0;
    for (std::size_t i = 0; i < have.size(); ++i)
        if (have[i].name == have[idx].name) { ++total; if (i <= idx) ++rank; }
    if (total <= 1) return have[idx].name;
    return have[idx].name + " #" + std::to_string(rank);
}

} // namespace neost::midi
