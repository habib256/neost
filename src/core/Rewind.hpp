// =============================================================================
//  Rewind.hpp — Retour arrière : un anneau d'états machine, un par seconde.
//
//  POURQUOI. Revenir quelques secondes en arrière dans une partie, sans passer par
//  un fichier .state. L'anneau vit en mémoire, le temps de la session.
//
//  COMMENT. À chaque frontière de trame, onFrame() compte ; à l'échéance il prend un
//  Machine::saveState et, si un lecteur a changé depuis l'instantané précédent, une
//  copie de son média (le save-state ne porte pas le contenu des disquettes). Sinon
//  l'instantané partage la copie précédente. Un budget d'octets évince les plus anciens.
//
//  LIMITES.
//    - Aucun instantané pendant une commande sur un STX : on réessaie à la trame suivante.
//    - Désactivé avec un disque dur (GEMDOS, ACSI, UltraSatan, clé USB) : leur contenu
//      n'est pas copié, et un retour arrière le désaccorderait du système.
//    - Une écriture STX n'est pas réversible : les instantanés antérieurs sont abandonnés.
//    - Un retour arrière réécrit le fichier de disquette à l'instant visé : ce qui a été
//      sauvegardé depuis sur la disquette est perdu, c'est le sens de l'opération.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "io/Fdc.hpp"

class Machine;

class Rewind {
public:
    static constexpr int         kDefaultInterval = 50;           // trames : ~1 s en PAL
    static constexpr std::size_t kDefaultBudget   = 128u << 20;   // octets

    void setEnabled(bool on);                  // éteint : l'anneau est vidé
    bool enabled() const { return enabled_; }
    void setInterval(int frames) { interval_ = frames > 0 ? frames : 1; }
    void setBudget(std::size_t bytes) { budget_ = bytes; trim(); }

    // À appeler une fois par trame, à sa frontière.
    void onFrame(Machine& m);
    // Revient d'un cran. Faux, avec la raison en anglais (interface), si c'est impossible.
    bool stepBack(Machine& m, std::string* why = nullptr);
    void clear();

    std::size_t count() const { return ring_.size(); }
    uint64_t taken() const { return taken_; }    // instantanés pris depuis le lancement
    std::size_t bytes() const;
    // Ce qui interdit le retour arrière sur cette machine, ou nullptr.
    static const char* blocker(const Machine& m);

private:
    struct Snap {
        std::vector<uint8_t> state;
        std::shared_ptr<const Fdc::MediaCopy> disk[2];
        uint64_t stxWrites = 0;
    };
    void trim();

    std::deque<Snap> ring_;
    bool        enabled_ = false;
    int         interval_ = kDefaultInterval;
    int         since_ = 0;                    // trames depuis le dernier instantané ou retour
    std::size_t budget_ = kDefaultBudget;
    uint64_t    taken_ = 0;
};
