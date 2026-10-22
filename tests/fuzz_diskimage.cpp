// =============================================================================
//  fuzz_diskimage.cpp — harnais de FUZZING des parseurs d'images disquette (A30).
//
//  POURQUOI. `decodeMsa`, `decodeDim`, `StxImage::parse` (et, depuis 2026-10-22,
//  `ipf::decode` + `ipf::toStx`) sont les seules
//  fonctions du projet qui digèrent un fichier venu de l'EXTÉRIEUR — une image
//  téléchargée, tronquée, corrompue, ou forgée. Ce sont des fonctions pures
//  `octets → bool`. Leur bornage manuel est déjà excellent (il corrige même une
//  lecture hors bornes présente dans Hatari, cf. CHANGELOG) — mais rien ne le
//  PROUVAIT ni ne le GARDAIT : la prochaine optimisation d'un décodeur pouvait
//  rouvrir le trou sans qu'aucun palier ne s'en aperçoive.
//
//  POURQUOI PAS libFuzzer. Le clang d'Apple ne livre pas `libclang_rt.fuzzer` :
//  `-fsanitize=fuzzer` ne LIE PAS sur macOS, la plateforme de développement du
//  projet. Un harnais libFuzzer serait donc du code que personne n'exécute ici —
//  précisément ce que le dépôt combat. Ce driver-ci est DÉTERMINISTE (PRNG
//  xorshift semé explicitement), il tourne partout, et il donne le même verdict à
//  chaque exécution : un plantage se rejoue avec `--seed`.
//  ➜ Pour brancher libFuzzer sur une machine qui l'a (Linux/clang), il suffit
//    d'ajouter dans un fichier à part :
//        extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
//        { fuzzOne(d, n); return 0; }
//    `fuzzOne` est écrit pour ça — sans état global, sans sortie.
//
//  CE QU'IL VÉRIFIE. D'abord qu'aucune entrée ne fait planter (sous ASan/UBSan,
//  c'est le job `sanitizers` de la CI qui le rend mordant). Ensuite un CONTRAT :
//  quand un décodeur dit « oui », la géométrie qu'il rend doit être plausible —
//  un « oui » sur une image de 3 octets serait un bug silencieux bien plus
//  dangereux qu'un plantage.
//
//  Lancer :  ./build/neost-fuzz-disk [--iters N] [--seed N]
//            (câblé au palier `fast` de run_all.py, ~0,3 s à 20 000 itérations)
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/DiskImageCodec.hpp"
#include "io/IpfImage.hpp"
#include "io/StxImage.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>          // strtoull, setenv / _putenv_s
#include <string>
#include <vector>

namespace {

// PRNG xorshift64* : déterministe, sans dépendance, reproductible d'une plateforme
// à l'autre (std::mt19937 l'est aussi, mais pas les distributions de la libstdc++
// vs libc++ — et on veut que « --seed 12345 » rejoue LE MÊME cas partout).
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    uint32_t below(uint32_t n) { return n ? uint32_t(next() % n) : 0; }
};

int g_fail = 0;

void fail(const char* what, uint64_t seed, std::size_t iter, const std::vector<uint8_t>& in) {
    ++g_fail;
    std::printf("  FAIL %s (seed=%llu iter=%zu taille=%zu)\n    octets:",
                what, (unsigned long long)seed, iter, in.size());
    for (std::size_t i = 0; i < in.size() && i < 32; ++i) std::printf(" %02X", in[i]);
    std::printf("%s\n", in.size() > 32 ? " …" : "");
}

// -----------------------------------------------------------------------------
//  LA cible. Sans état, sans sortie : c'est elle qu'un libFuzzer appellerait.
//  Renvoie une description du contrat violé, ou nullptr si tout va bien.
// -----------------------------------------------------------------------------
// Taille maximale d'une image plausible : 86 pistes × 2 faces × 56 secteurs × 512 o
// (les bornes que decodeMsa applique lui-même).
constexpr std::size_t kMaxPlausible = 86u * 2u * 56u * 512u;

// --- IPF : réparation des CRC32 et contrat de conversion ----------------------
inline uint32_t rdBe32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void wrBe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
// Recalcule le CRC de chaque bloc (et de la zone DATA) là où la structure le permet.
void repairIpfCrcs(std::vector<uint8_t>& v) {
    std::size_t pos = 0;
    while (v.size() - pos >= 12) {
        const uint32_t tsize = rdBe32(&v[pos + 4]);
        if (tsize < 12 || tsize > v.size() - pos) return;
        const bool data = tsize == 28 && rdBe32(&v[pos]) == 0x44415441u;
        if (data) {
            const uint32_t dsz = rdBe32(&v[pos + 12]);
            if (dsz <= v.size() - pos - 28) wrBe32(&v[pos + 20], ipf::crc32(&v[pos + 28], dsz));
        }
        wrBe32(&v[pos + 8], 0);
        wrBe32(&v[pos + 8], ipf::crc32(&v[pos], tsize));
        pos += tsize;
        if (data) {
            const uint32_t dsz = rdBe32(&v[pos - 28 + 12]);
            if (dsz > v.size() - pos) return;
            pos += dsz;
        }
    }
}
// CONTRAT : une IPF acceptée doit donner un STX que StxImage::parse accepte
// (le FDC n'a pas d'autre chemin pour la monter).
const char* fuzzIpf(const std::vector<uint8_t>& raw) {
    std::vector<ipf::CellTrack> tracks;
    if (!ipf::decode(raw, tracks)) return nullptr;
    for (const auto& t : tracks)
        if (t.cells.size() != t.timeNs.size()) return "IPF : flux de cellules et durées désaccordés";
    std::vector<uint8_t> blob;
    if (!ipf::toStx(tracks, blob)) return nullptr;      // aucune piste exploitable : refus propre
    StxImage stx;
    if (!stx.parse(std::move(blob)))                     return "IPF convertie en STX que parse() refuse";
    return nullptr;
}

const char* fuzzOne(const uint8_t* data, std::size_t len) {
    const std::vector<uint8_t> raw(data, data + len);

    // --- conteneurs .msa / .dim ---------------------------------------------
    std::vector<uint8_t> out;
    if (diskimg::decodeContainer(raw, out)) {
        // CONTRAT : un « oui » doit rendre une image plausible. Une disquette ST
        // fait au minimum une piste d'une face (le décodeur borne spt ≤ 56 et
        // 86 pistes max), et sa taille est un multiple de 512.
        if (out.empty())                 return "décodage réussi mais image VIDE";
        if (out.size() % 512u != 0u)     return "décodage réussi, taille non multiple de 512";
        if (out.size() > kMaxPlausible)  return "décodage réussi, image plus grande qu'une ED 86 pistes";
    } else {
        // ⚠ TROUVÉ EN ÉCRIVANT CE HARNAIS (2026-08-28) : un refus peut laisser `out`
        // PARTIELLEMENT REMPLI — decodeMsa décode des pistes puis renonce sur la
        // dernière. Ce n'est pas un bug aujourd'hui : sur ce chemin Fdc::loadImage
        // n'utilise plus `conv`, il repart de `raw`. Mais c'est une dépendance
        // IMPLICITE, et le contrat « refus ⇒ tampon vide » — celui qu'on croirait
        // évident — est FAUX (270 violations sur 5 000 itérations quand on l'exige).
        // On garde donc ce qui est réellement garanti : un refus ne doit pas avoir
        // fait gonfler la mémoire au-delà d'une image plausible.
        if (out.size() > kMaxPlausible)  return "décodage refusé, tampon gonflé au-delà d'une image ED";
    }

    // --- conteneur .stx (Pasti) ---------------------------------------------
    // parse() PREND le vecteur (il conserve les octets bruts : les secteurs y
    // pointent), d'où la copie.
    StxImage stx;
    if (stx.parse(raw)) {
        // Un « oui » sur moins qu'un en-tête STX serait une acceptation à tort.
        if (raw.size() < 16u)            return "STX accepté sur moins de 16 octets";
    }

    // --- conteneur .ipf (SPS/CAPS) ------------------------------------------
    // Les blocs IPF portent un CRC32 : une mutation brute est presque toujours
    // refusée dès le premier bloc. On teste donc AUSSI la variante « CRC
    // réparés », qui atteint les descriptions de blocs et de gaps.
    if (const char* why = fuzzIpf(raw)) return why;
    if (raw.size() >= 12 && raw[0] == 'C' && raw[1] == 'A' && raw[2] == 'P' && raw[3] == 'S') {
        std::vector<uint8_t> fixed = raw;
        repairIpfCrcs(fixed);
        if (const char* why = fuzzIpf(fixed)) return why;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
//  Corpus de départ : des entrées VALIDES ou presque, pour que les mutations
//  partent d'assez près pour franchir les gardes d'en-tête. Un fuzzeur qui part
//  d'octets aléatoires purs ne dépasse jamais le premier `if`.
// -----------------------------------------------------------------------------
std::vector<std::vector<uint8_t>> seedCorpus() {
    std::vector<std::vector<uint8_t>> corpus;

    // .MSA valide : en-tête $0E0F, 9 spt, 2 faces, pistes 0..1, puis deux pistes
    // NON compressées (longueur = taille de piste → recopie brute).
    {
        const int spt = 9, sides = 2, t0 = 0, t1 = 1;
        const int trackLen = spt * 512;
        std::vector<uint8_t> m = {0x0E, 0x0F,
                                  uint8_t(spt >> 8),   uint8_t(spt),
                                  uint8_t((sides - 1) >> 8), uint8_t(sides - 1),
                                  uint8_t(t0 >> 8),    uint8_t(t0),
                                  uint8_t(t1 >> 8),    uint8_t(t1)};
        for (int t = t0; t <= t1; ++t)
            for (int s = 0; s < sides; ++s) {
                m.push_back(uint8_t(trackLen >> 8));
                m.push_back(uint8_t(trackLen));
                for (int i = 0; i < trackLen; ++i) m.push_back(uint8_t(i * 7 + t));
            }
        corpus.push_back(std::move(m));
    }

    // .DIM valide : 32 octets d'en-tête ('BB', non compressée, piste 0) + 1 piste.
    {
        std::vector<uint8_t> d(32, 0);
        d[0x00] = d[0x01] = 0x42;
        d[0x06] = 1;            // faces - 1
        d[0x08] = 9;            // spt
        d[0x0C] = 1;            // dernière piste
        d.insert(d.end(), 9u * 512u * 2u, 0xA5);
        corpus.push_back(std::move(d));
    }

    // .ST brute : ni MSA ni DIM — le cas « refusé » doit rester propre.
    corpus.push_back(std::vector<uint8_t>(9u * 512u, 0x5A));

    // En-tête STX plausible : « RSY\0 », version, puis des compteurs. Sciemment
    // approximatif : la mutation fait le reste, et le but est de FRANCHIR le
    // premier `if` du parseur, pas de fabriquer une image jouable.
    {
        std::vector<uint8_t> x = {'R', 'S', 'Y', 0x00, 0x03, 0x00, 0x00, 0x00,
                                  0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        x.resize(512, 0);
        corpus.push_back(std::move(x));
    }

    // IPF minimale VALIDE (un secteur de 512 o sur la piste 0) : CAPS, INFO,
    // IMGE, DATA + flux (synchro brute, octets MFM, gap de type 0). CRC posés
    // par repairIpfCrcs.
    {
        std::vector<uint8_t> ipfImg;
        auto rec = [&ipfImg](const char* id, const std::vector<uint32_t>& words) {
            const std::size_t at = ipfImg.size();
            ipfImg.insert(ipfImg.end(), id, id + 4);
            ipfImg.resize(at + 12 + 4 * words.size(), 0);
            wrBe32(&ipfImg[at + 4], uint32_t(12 + 4 * words.size()));
            for (std::size_t i = 0; i < words.size(); ++i) wrBe32(&ipfImg[at + 12 + 4 * i], words[i]);
        };
        std::vector<uint8_t> area(32, 0);
        auto elem = [&area](uint8_t code, uint32_t n) {
            area.push_back(uint8_t((4u << 5) | code));
            for (int k = 3; k >= 0; --k) area.push_back(uint8_t(n >> (8 * k)));
        };
        elem(3, 12); area.insert(area.end(), 12, 0x00);
        elem(1, 6);  for (int i = 0; i < 3; ++i) { area.push_back(0x44); area.push_back(0x89); }
        elem(2, 7);  area.insert(area.end(), {0xFE, 0, 0, 1, 2, 0xCA, 0x6F});
        elem(3, 34); area.insert(area.end(), 34, 0x4E);
        elem(1, 6);  for (int i = 0; i < 3; ++i) { area.push_back(0x44); area.push_back(0x89); }
        elem(2, 515); area.push_back(0xFB); area.insert(area.end(), 514, 0xE5);
        elem(5, 4);
        area.push_back(0);
        const uint32_t dataCells = (12 + 7 + 34 + 515 + 4) * 16 + 2 * 48;
        const uint32_t gapCells = 40 * 16;
        const uint32_t desc[8] = {dataCells, gapCells, 0, 1, 1, 0, 0x4E, 32};
        for (int i = 0; i < 8; ++i) wrBe32(&area[std::size_t(4 * i)], desc[i]);
        rec("CAPS", {});
        rec("INFO", {1, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0});
        rec("IMGE", {0, 0, 2, 1, 0, 60, 960, 0, 0, dataCells + gapCells, 1, 0, 0, 1, 0, 0, 0});
        rec("DATA", {uint32_t(area.size()), uint32_t(area.size() * 8), 0, 1});
        ipfImg.insert(ipfImg.end(), area.begin(), area.end());
        repairIpfCrcs(ipfImg);
        // La graine DOIT être acceptée de bout en bout, sinon les mutations ne
        // testeraient que le chemin de refus du premier bloc.
        std::vector<ipf::CellTrack> seedTracks;
        std::vector<uint8_t> seedStx;
        ipf::StxStats seedStats;
        if (!ipf::decode(ipfImg, seedTracks) || !ipf::toStx(seedTracks, seedStx, nullptr, &seedStats)
            || seedStats.sectors != 1) {
            std::fprintf(stderr, "[fuzz-disk] IPF seed rejected — fix the corpus\n");
            ++g_fail;
        }
        corpus.push_back(std::move(ipfImg));
    }

    // Les cas dégénérés que tout parseur doit encaisser sans broncher.
    corpus.push_back({});
    corpus.push_back({0x0E});
    corpus.push_back({0x0E, 0x0F});
    corpus.push_back({0x42, 0x42});
    return corpus;
}

// Une mutation, choisie au hasard. Volontairement brutales : ce sont les
// TAILLES et les CHAMPS DE LONGUEUR qui font sortir des bornes, pas les données.
void mutate(std::vector<uint8_t>& v, Rng& rng) {
    switch (rng.below(7)) {
        case 0:                                        // inversion d'un bit
            if (!v.empty()) v[rng.below(uint32_t(v.size()))] ^= uint8_t(1u << rng.below(8));
            break;
        case 1:                                        // octet arbitraire
            if (!v.empty()) v[rng.below(uint32_t(v.size()))] = uint8_t(rng.next());
            break;
        case 2:                                        // TRONCATURE (le grand classique)
            if (!v.empty()) v.resize(rng.below(uint32_t(v.size())));
            break;
        case 3:                                        // rallonge d'octets aléatoires
            for (uint32_t n = rng.below(64); n; --n) v.push_back(uint8_t(rng.next()));
            break;
        case 4:                                        // écrase un champ de longueur 16 bits
            if (v.size() >= 2) {
                const uint32_t p = rng.below(uint32_t(v.size() - 1));
                v[p] = uint8_t(rng.next()); v[p + 1] = uint8_t(rng.next());
            }
            break;
        case 5:                                        // valeurs extrêmes ($00 / $FF)
            if (!v.empty()) {
                const uint32_t p = rng.below(uint32_t(v.size()));
                v[p] = (rng.next() & 1) ? 0xFF : 0x00;
            }
            break;
        default:                                       // suppression d'une tranche
            if (v.size() > 4) {
                const uint32_t p = rng.below(uint32_t(v.size() - 1));
                const uint32_t n = 1 + rng.below(uint32_t(v.size() - p) - 1);
                v.erase(v.begin() + p, v.begin() + p + n);
            }
            break;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t iters = 20000;
    uint64_t    seed  = 0xC0FFEEull;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--iters") && i + 1 < argc) iters = std::strtoull(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 0);
        else { std::printf("usage: %s [--iters N] [--seed N]\n", argv[0]); return 2; }
    }

    // Coupe l'avertissement RLE des décodeurs : on leur donne des dizaines de
    // milliers d'images DÉLIBÉRÉMENT corrompues, leur diagnostic n'apprend rien.
    // Un interrupteur explicite plutôt qu'une redirection de stderr — c'est là que
    // les sanitizers écrivent leur rapport, et on ne met jamais ce flux en sourdine.
#if defined(_WIN32)
    _putenv_s("NEOST_QUIET_PARSERS", "1");
#else
    setenv("NEOST_QUIET_PARSERS", "1", 1);
#endif

    const auto corpus = seedCorpus();
    Rng rng(seed);
    std::size_t accepted = 0;

    // Le corpus de départ passe d'abord TEL QUEL : si une entrée valide viole le
    // contrat, autant le dire avant d'avoir muté quoi que ce soit.
    for (const auto& c : corpus)
        if (const char* why = fuzzOne(c.data(), c.size())) fail(why, seed, 0, c);

    for (std::size_t it = 1; it <= iters; ++it) {
        std::vector<uint8_t> v = corpus[rng.below(uint32_t(corpus.size()))];
        for (uint32_t m = 1 + rng.below(4); m; --m) mutate(v, rng);
        if (const char* why = fuzzOne(v.data(), v.size())) fail(why, seed, it, v);
        // Statistique de couverture du pauvre : combien d'entrées ont été
        // ACCEPTÉES par un décodeur. Si ce nombre tombe à zéro, le harnais ne
        // teste plus que le chemin de refus — il faudrait revoir le corpus.
        std::vector<uint8_t> out;
        if (diskimg::decodeContainer(v, out)) ++accepted;
    }

    std::printf("[fuzz-disk] %zu itérations (seed %llu), %zu entrées acceptées, %d violation(s)\n",
                iters, (unsigned long long)seed, accepted, g_fail);
    if (accepted == 0) {
        std::printf("  ✗ AUCUNE entrée acceptée : le harnais n'exerce plus que le refus.\n");
        return 1;
    }
    return g_fail == 0 ? 0 : 1;
}
