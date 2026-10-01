#pragma once
// =============================================================================
//  Server.hpp — mode serveur du headless (--server) : une boucle de commandes
//  texte sur stdin/stdout, pour qu'un programme tiers conduise la machine sans
//  relancer un processus ni passer par le disque à chaque itération.
//
//  Cf. Server.cpp pour le protocole et docs/OPENDST.md pour l'usage.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Machine.hpp"
#include "headless/Observe.hpp"

#include <string>

namespace neost::server {

struct Options {
    observe::ProbeSet probes;        // sondes déclarées en ligne de commande
    // Réponse à « hello » : identityHead + « disk=… diskb=… » (médias montés À
    // L'INSTANT, insert/eject les changent) + identityTail.
    std::string       identityHead;  // version, machine, RAM, TOS
    std::string       identityTail;  // lecteur B, FDC rapide, commit, date de build
    int               slots = 64;    // emplacements d'état EN MÉMOIRE (~1,4 Mo pièce)
};

// Boucle de commandes. Rend 0 sur « quit » ou fin de stdin.
int run(Machine& machine, const Options& opts);

}  // namespace neost::server
