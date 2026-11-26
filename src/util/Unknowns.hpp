// Compteurs des inconnues assumées ([F]) : combien de fois le chemin incertain sert-il ?
//
// Un `[F]` dans un commentaire dit « deviné, non vérifié ». Pour savoir lesquels comptent
// pour la compatibilité, chaque chemin de code qu'un `[F]` gouverne porte `NEOST_F("id")`,
// placé DANS la branche où le comportement incertain s'applique (pas à l'entrée d'une
// fonction appelée à chaque accès). `neost-headless --unknowns FICHIER` écrit, en fin de
// run, les sites atteints et leur nombre de passages ; `tools/rank_unknowns.py` agrège une
// collection de disques et classe les inconnues par nombre de titres qui les traversent.
//
// Le compteur n'a aucun effet sur l'émulation (ni état sauvé, ni timing) : un entier
// global par site, enregistré au premier passage. Un site jamais atteint n'est pas dans
// le registre ; la liste complète se lit dans les sources (`grep NEOST_F`).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace neost::unknowns {

struct Site {
    const char* id;
    const char* file;
    int         line;
    uint64_t    hits = 0;
};

// Registre des sites atteints, dans l'ordre du premier passage.
inline std::vector<Site*>& registry() {
    static std::vector<Site*> sites;
    return sites;
}

struct Registrar {
    Site site;
    Registrar(const char* id, const char* file, int line) : site{id, file, line} {
        registry().push_back(&site);
    }
};

// Chemin relatif au dépôt (« src/… ») : __FILE__ est absolu selon le build, et le
// dépôt lui-même peut vivre sous un dossier « src » : on garde le DERNIER « /src/ ».
inline const char* repoPath(const char* file) {
    const char* last = nullptr;
    for (const char* p = std::strstr(file, "/src/"); p; p = std::strstr(p + 1, "/src/")) last = p;
    return last ? last + 1 : file;
}

// JSON minimal, un site par ligne. Les identifiants sont des littéraux du code
// (snake_case, points) : rien à échapper.
inline bool writeReport(const char* path) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) return false;
    std::fputs("{\"sites\": [", f);
    bool first = true;
    for (const Site* s : registry()) {
        std::fprintf(f, "%s\n  {\"id\": \"%s\", \"file\": \"%s\", \"line\": %d, \"hits\": %llu}",
                     first ? "" : ",", s->id, repoPath(s->file), s->line,
                     static_cast<unsigned long long>(s->hits));
        first = false;
    }
    std::fputs("\n]}\n", f);
    return std::fclose(f) == 0;
}

}  // namespace neost::unknowns

// À placer comme une instruction, dans la branche du comportement incertain.
#define NEOST_F(id)                                                                        \
    do {                                                                                   \
        static ::neost::unknowns::Registrar neost_f_site_{id, __FILE__, __LINE__};         \
        ++neost_f_site_.site.hits;                                                         \
    } while (0)
