// =============================================================================
//  StateArchive.hpp — sérialiseur/désérialiseur SYMÉTRIQUE pour les save-states.
//
//  Une SEULE méthode `serialize(StateArchive&)` par composant gère les DEUX sens
//  (save = append dans un buffer, load = lecture depuis un buffer) → impossible de
//  désynchroniser l'ordre save/load. Format binaire brut, little-endian de l'hôte
//  (les save-states ne sont pas portables entre architectures — hypothèse assumée).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

class StateArchive {
public:
    static StateArchive saver(std::vector<uint8_t>& out) {
        StateArchive a; a.loading_ = false; a.out_ = &out; return a;
    }
    static StateArchive loader(const uint8_t* p, size_t n) {
        StateArchive a; a.loading_ = true; a.in_ = p; a.inEnd_ = p + n; return a;
    }

    bool loading() const { return loading_; }
    bool ok()      const { return ok_; }

    // Échec d'I/O hôte également possible pendant une sauvegarde.
    void fail(const char* what) {
        if (ok_) std::fprintf(stderr, "[save-state] %s\n", what);
        ok_ = false;
    }

    // Valide un invariant AU CHARGEMENT (no-op en save) : un champ restauré hors
    // bornes (fichier forgé/corrompu passé le CRC) met l'archive en échec →
    // Machine::loadState rejoue le backup au lieu de servir des index toxiques.
    // `what` : étiquette de l'invariant, affichée sur stderr au premier échec —
    // sans elle, un save-state refusé ne dit RIEN sur la garde qui l'a rejeté
    // (et une garde trop stricte, qui refuse un état légitime, est indiscernable
    // d'un fichier réellement corrompu).
    void check(bool cond, const char* what = nullptr) {
        if (!loading_ || cond) return;
        if (ok_) std::fprintf(stderr, "[save-state] invariant rejected: %s\n",
                              what ? what : "(unlabelled)");
        ok_ = false;
    }

    // Taille écrite jusqu'ici (mode save uniquement — diagnostic NEOST_STATE_MAP).
    size_t saveSize() const { return out_ ? out_->size() : 0; }

    // Transfère n octets bruts (save : append ; load : lit, borne vérifiée).
    void raw(void* p, size_t n) {
        if (n == 0) return;
        if (loading_) {
            if (in_ + n > inEnd_) { ok_ = false; return; }
            std::memcpy(p, in_, n);
            in_ += n;
        } else {
            const uint8_t* c = static_cast<const uint8_t*>(p);
            out_->insert(out_->end(), c, c + n);
        }
    }

    // Normalise des booléens APRÈS une lecture en bloc. À appeler sur chaque bool
    // membre d'un agrégat sérialisé d'un seul `raw()` : `operator()` ne voit que le
    // type de l'agrégat et ne peut pas les atteindre (cf. la note de portée
    // ci-dessous). Sans effet à l'écriture, et le format de fichier ne bouge pas.
    //
    // LISTE COMPLÈTE des agrégats non-tableaux sérialisés en bloc, établie le
    // 2026-09-01 en faisant énumérer les instanciations PAR LE COMPILATEUR (un
    // `static_assert(!std::is_class_v<T>)` temporaire) — et non au grep, qui avait
    // laissé passer le plus gros :
    //   · moira::Registers      → 9 bool dans `sr`   → fixBools (Cpu68k.cpp)
    //   · Scc::Chn              → 3 bool             → fixBools (Scc.hpp)
    //   · Fpu                   → 2 bool             → fixBools (Fpu.hpp)
    //   · StePads               → 3 bool             → fixBools (StePads.hpp)
    //   · Bus::MegaSteCache     → aucun bool         → rien à faire
    //   · Scu                   → aucun bool         → rien à faire
    //   · moira::PrefetchQueue  → aucun bool         → rien à faire
    // Tout le reste est un `std::array` de scalaires. ⚠ Un agrégat AJOUTÉ plus tard
    // ne sera signalé par rien : refaire l'énumération au compilateur le jour où l'on
    // sérialise une nouvelle struct.
    template <class... B>
    void fixBools(B&... bs) {
        static_assert((std::is_same_v<B, bool> && ...), "fixBools : booléens seulement");
        if (!loading_) return;
        // Relire la REPRÉSENTATION OBJET est défini ; lire le bool lui-même ne l'est
        // pas quand son octet vaut autre chose que 0 ou 1.
        ((void)[](bool& b) { unsigned char c; std::memcpy(&c, &b, 1); b = (c != 0); }(bs), ...);
    }

    // Un scalaire/POD (uint32_t, int64_t, bool, un struct trivial…).
    template <class T>
    void operator()(T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "StateArchive : type non trivial");
        raw(&v, sizeof v);
        if constexpr (std::is_same_v<T, bool>) {
            // NORMALISATION DES BOOLÉENS. Un `bool` dont l'octet vaut autre chose que
            // 0 ou 1 est un COMPORTEMENT INDÉFINI : le compilateur teste souvent le bit
            // 0 dans un `if (b)` et l'octet entier dans un `if (!b)`, si bien que les
            // deux branches peuvent être prises. Un .state forgé posait par exemple
            // Machine::frameInProgress_ à 63 (détecté par UBSan). Format inchangé.
            // ⚠ PORTÉE RÉELLE, et le commentaire d'origine la surestimait : ceci ne
            // couvre que les `bool` passés SEULS. Un bool MEMBRE d'un agrégat copié
            // en bloc — `ar(chn_[0])`, `ar(fpu)`, `ar(stePads)` — n'est pas vu, `T`
            // valant alors la struct : les octets du fichier atterrissent tels quels.
            // C++17 n'a pas de réflexion, il n'existe donc pas de correctif générique
            // ici ; les sites concernés appellent `fixBools()` juste après leur
            // lecture en bloc. (Chasse aux bugs du 2026-09-01.)
            static_assert(sizeof(bool) == 1, "bool non tenu sur 1 octet : format à revoir");
            if (loading_) {
                unsigned char b;                 // relire la représentation objet est
                std::memcpy(&b, &v, 1);          // défini, contrairement à lire le bool
                v = (b != 0);
            }
        }
    }

    // Un tableau C d'éléments POD (ex. due_[SRC_COUNT], reg.d[8]).
    template <class T, size_t N>
    void arr(T (&v)[N]) { raw(v, sizeof v); }

    // Un vector<uint8_t> de taille variable (préfixé par sa longueur) — ex. la RAM.
    void vec(std::vector<uint8_t>& v) {
        uint32_t n = static_cast<uint32_t>(v.size());
        (*this)(n);
        if (loading_) {
            // Borne AVANT resize : un préfixe corrompu (ex. 0xFFFFFFFF) ne doit
            // jamais allouer plus que les octets restants du buffer d'entrée.
            if (!ok_ || n > static_cast<size_t>(inEnd_ - in_)) { ok_ = false; return; }
            v.resize(n);
        }
        raw(v.data(), v.size());
    }

    // Un vector<T> dont chaque élément est sérialisé CHAMP PAR CHAMP via `each(ar, e)`.
    // OBLIGATOIRE pour les structs à padding (SyncWrite, GlueLine, ColorWrite,
    // DmaEvent, RegEvent…) : podVec copierait leurs octets de padding NON INITIALISÉS
    // → save-states non byte-déterministes (le test --save-state-test et le CRC
    // divergeraient sur des octets morts). `elemBytes` = taille sérialisée MINIMALE
    // d'un élément, pour borner l'allocation face à un préfixe corrompu.
    template <class T, class F>
    void objVec(std::vector<T>& v, size_t elemBytes, F&& each) {
        uint32_t n = static_cast<uint32_t>(v.size());
        (*this)(n);
        if (loading_) {
            const uint64_t need = static_cast<uint64_t>(n) * elemBytes;
            if (!ok_ || need > static_cast<uint64_t>(inEnd_ - in_)) { ok_ = false; return; }
            v.resize(n);
        }
        for (T& e : v) { each(*this, e); if (!ok_) return; }
    }

    // Un vector<T> d'éléments POD (taille préfixée) — ex. les logs d'écritures horodatées
    // (events_ du YM2149/DMA sound). Vidés à chaque trame → généralement vides à la
    // frontière de save, mais sérialisés par sûreté.
    // ⚠ RÉSERVÉ aux T SANS padding interne (sizeof(T) == somme des membres) —
    // sinon utiliser objVec (cf. ci-dessus).
    template <class T>
    void podVec(std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>, "podVec : élément non trivial");
        uint32_t n = static_cast<uint32_t>(v.size());
        (*this)(n);
        if (loading_) {
            // Même garde que vec() : n éléments ne peuvent pas dépasser les octets
            // restants (le calcul est fait en 64 bits, pas de débordement possible).
            const uint64_t need = static_cast<uint64_t>(n) * sizeof(T);
            if (!ok_ || need > static_cast<uint64_t>(inEnd_ - in_)) { ok_ = false; return; }
            v.resize(n);
        }
        raw(v.data(), sizeof(T) * v.size());
    }

private:
    bool loading_ = false;
    bool ok_      = true;
    std::vector<uint8_t>* out_ = nullptr;   // mode save
    const uint8_t* in_    = nullptr;         // mode load
    const uint8_t* inEnd_ = nullptr;
};
