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
//  DEUX ÉTAGES EN AVAL (2026-11-12). Le parseur n'est pas le seul à lire l'image :
//  `StxImage::loadWd1772` relit le fichier compagnon des écritures STX (lui aussi
//  venu du disque de l'utilisateur), et le FDC indexe pistes et secteurs de ce que
//  les parseurs ont accepté. Une itération sur `kFdcEvery` monte donc l'image
//  mutée dans un vrai `Fdc` (Bus + YM + MFP + Scheduler, sans CPU) — avec, pour une
//  STX, un .wd1772 muté à côté — et lui fait lire, relire, écrire et formater.
//  Contrat en plus du « pas de plantage » : une disquette qui tourne termine TOUTE
//  commande (le WD1772 abandonne au plus tard après 5 tours, [B] WD177X-00 p. 1-9) ;
//  un BUSY sans fin est une violation.
//
//  Lancer :  ./build/neost-fuzz-disk [--iters N] [--seed N]
//            (câblé au palier `fast` de run_all.py)
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/YM2149.hpp"
#include "io/DiskImageCodec.hpp"
#include "io/Fdc.hpp"
#include "io/IpfImage.hpp"
#include "io/Mfp.hpp"
#include "io/StxImage.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>          // strtoull, setenv / _putenv_s
#include <filesystem>
#include <fstream>
#include <iterator>
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
//  Étages en aval : .wd1772 et FDC.
// -----------------------------------------------------------------------------
constexpr std::size_t kFdcEvery = 256;   // une itération sur 256 descend jusqu'au FDC (E/S fichier : ~10 ms)

void writeBytes(const std::string& path, const std::vector<uint8_t>& v) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size()));
}
std::vector<uint8_t> readBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// STX minimale VALIDE : une piste (0, face 0), un secteur de 512 o — parse() l'accepte,
// contrairement à la graine « approximative » du corpus.
std::vector<uint8_t> validStx() {
    std::vector<uint8_t> image = {'R','S','Y',0,3,0,0,0,0,0,1,0,0,0,0,0};
    auto le16 = [&](unsigned value) { image.push_back(uint8_t(value)); image.push_back(uint8_t(value >> 8)); };
    auto le32 = [&](unsigned value) { le16(value); le16(value >> 16); };
    le32(16 + 512); le32(0); le16(1); le16(0); le16(6268 * 8);
    image.push_back(0); image.push_back(0);
    image.insert(image.end(), 512, 0xA5);
    return image;
}

// Contrôleur disquette seul, horloge pilotée à la main (cf. tests/*_logic.cpp).
struct FdcRig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    FdcRig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(true);      // le fichier est une copie jetable : on veut writeBack
        fdc.setFastFdc(true);
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
        mfp.write8(0xFFFA09, 0x80);
    }
    void select(uint8_t v) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, v); }
    void mode(uint16_t v) { fdc.write8(0xFF8606, uint8_t(v >> 8)); fdc.write8(0xFF8607, uint8_t(v)); }
    void data(uint8_t v) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, v); }
    uint8_t reg(uint16_t a) { mode(uint16_t(0x80 | a)); return fdc.read8(0xFF8605); }
    void reg(uint16_t a, uint8_t v) { mode(uint16_t(0x80 | a)); data(v); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    // Vrai si INTRQ est tombée dans le budget (2 s émulées : ≥ 10 tours de disquette).
    bool finish() {
        const int64_t end = live + 16'000'000;
        for (unsigned n = 0; n < 1000000 && !irq() && live < end; ++n) {
            const int64_t due = sched.nextDue();
            if (due < 0) return false;
            for (int64_t b = (live / 160000 + 1) * 160000; b <= due; b += 160000) {
                live = b; sched.runTo(b); fdc.onVbl();
            }
            live = due; sched.runTo(due);
        }
        return irq();
    }
    void dma(bool write, uint8_t count) {
        mode(write ? 0x190 : 0x90); mode(write ? 0x090 : 0x190); mode(write ? 0x190 : 0x90);
        data(count);
        fdc.write8(0xFF860D, 0); fdc.write8(0xFF860B, 0); fdc.write8(0xFF8609, 0x02);
    }
    bool cmd(bool write, uint8_t c) { mode(write ? 0x180 : 0x80); data(c); return finish(); }
};

// Flux WRITE TRACK plausible pour la piste `track` : jusqu'à 11 secteurs de taille
// aléatoire, marques $F5/$FE/$FB et CRC $F7 tels qu'un formateur les envoie.
void formatStream(FdcRig& r, Rng& rng, uint8_t track, uint8_t side) {
    std::size_t p = 0x20000;
    const std::size_t end = p + 14 * 512;
    auto put = [&](uint8_t b) { if (p < end) r.bus.ram[p++] = b; };
    for (int i = 0; i < 60; ++i) put(0x4E);
    for (uint32_t s = 0, n = rng.below(12); s < n; ++s) {
        for (int i = 0; i < 12; ++i) put(0);
        for (int i = 0; i < 3; ++i) put(0xF5);
        put(0xFE); put(track); put(side); put(uint8_t(s + 1)); put(uint8_t(rng.below(4))); put(0xF7);
        for (int i = 0; i < 22; ++i) put(0x4E);
        for (int i = 0; i < 12; ++i) put(0);
        for (int i = 0; i < 3; ++i) put(0xF5);
        put(0xFB);
        for (int i = 0, len = 128 << rng.below(4); i < len; ++i) put(uint8_t(rng.next()));
        put(0xF7);
        for (int i = 0; i < 40; ++i) put(0x4E);
    }
    while (p < end) put(rng.below(3) ? uint8_t(0x4E) : uint8_t(rng.next()));
}

// Monte `img` (extension choisie d'après le contenu, comme la nommerait l'utilisateur)
// et lui fait subir une courte séquence de commandes. Renvoie la violation, ou nullptr.
const char* fuzzFdc(const std::vector<uint8_t>& img, const std::vector<uint8_t>* wd1772,
                    const std::string& dir, Rng& rng) {
    const char* ext = ".st";
    if (img.size() >= 4 && img[0] == 'R' && img[1] == 'S' && img[2] == 'Y' && img[3] == 0) ext = ".stx";
    else if (img.size() >= 4 && img[0] == 'C' && img[1] == 'A' && img[2] == 'P' && img[3] == 'S') ext = ".ipf";
    else if (img.size() >= 2 && img[0] == 0x0E && img[1] == 0x0F) ext = ".msa";
    else if (img.size() >= 2 && img[0] == 0x42 && img[1] == 0x42) ext = ".dim";
    const std::string path = dir + "/img" + ext;
    writeBytes(path, img);
    std::remove((dir + "/img.wd1772").c_str());
    if (wd1772) writeBytes(dir + "/img.wd1772", *wd1772);
    FdcRig r;
    if (!r.fdc.loadImage(path, 0)) return nullptr;      // refus propre : rien de plus à tester
    for (int k = 0; k < 6; ++k) {
        const uint8_t track = uint8_t(rng.below(4) ? rng.below(4) : rng.below(86));
        const uint8_t side = uint8_t(rng.next() & 1);
        r.select(side ? 4 : 5);
        r.reg(6, track);
        if (!r.cmd(false, 0x18))                         return "FDC : SEEK sans fin";
        r.reg(2, track);
        r.reg(4, uint8_t(rng.below(12)));
        switch (rng.below(5)) {
            case 0: r.dma(false, uint8_t(1 + rng.below(3)));
                    if (!r.cmd(false, uint8_t(0x88 | ((rng.next() & 1) ? 0x10 : 0))))
                        return "FDC : READ SECTOR sans fin";
                    break;
            case 1: r.dma(false, 1);
                    if (!r.cmd(false, 0xC8))             return "FDC : READ ADDRESS sans fin";
                    break;
            case 2: r.dma(false, 14);
                    if (!r.cmd(false, 0xE8))             return "FDC : READ TRACK sans fin";
                    break;
            case 3: r.dma(true, uint8_t(1 + rng.below(2)));
                    for (std::size_t i = 0; i < 1024; ++i) r.bus.ram[0x20000 + i] = uint8_t(rng.next());
                    if (!r.cmd(true, uint8_t(0xA8 | ((rng.next() & 1) ? 0x10 : 0))))
                        return "FDC : WRITE SECTOR sans fin";
                    break;
            default: r.dma(true, 14);
                     formatStream(r, rng, track, side);
                     if (!r.cmd(true, 0xF8))             return "FDC : WRITE TRACK sans fin";
                     break;
        }
    }
    return nullptr;
}

// .wd1772 de départ : celui que NeoST écrit lui-même après un WRITE SECTOR et un WRITE
// TRACK sur la STX valide — blocs SECT, TRCK, NTRK et NSCT bien formés, que la mutation abîme.
std::vector<uint8_t> seedWd1772(const std::string& dir) {
    const std::string path = dir + "/seed.stx";
    writeBytes(path, validStx());
    std::remove((dir + "/seed.wd1772").c_str());
    Rng rng(1);
    {
        FdcRig r;
        if (!r.fdc.loadImage(path, 0)) return {};
        r.select(5);
        r.reg(4, 1); r.dma(true, 1);
        r.cmd(true, 0xA8);
        r.dma(true, 14);
        formatStream(r, rng, 0, 0);
        r.cmd(true, 0xF8);
        // Face 1, absente de la STX : WRITE TRACK la crée (blocs NTRK, puis NSCT pour
        // le WRITE SECTOR qui suit si le format tiré porte un secteur 1).
        r.select(4);
        r.dma(true, 14);
        formatStream(r, rng, 0, 1);
        r.cmd(true, 0xF8);
        r.reg(4, 1); r.dma(true, 1);
        r.cmd(true, 0xA8);
    }
    return readBytes(dir + "/seed.wd1772");
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

    // STX valide : sans elle, l'étage FDC ne verrait presque jamais de STX montée.
    corpus.push_back(validStx());

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

    // Dossier de travail des étages en aval (images et .wd1772 réécrits à chaque passe).
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path work = fs::temp_directory_path(ec)
        / ("neost-fuzz-disk-" + std::to_string(seed) + "-"
           + std::to_string(reinterpret_cast<std::uintptr_t>(&rng)));
    fs::create_directories(work, ec);
    const std::string dir = work.string();
    const std::vector<uint8_t> wdSeed = seedWd1772(dir);
    if (wdSeed.size() < 16) {
        std::printf("  FAIL graine .wd1772 non produite (STX valide non inscriptible ?)\n");
        ++g_fail;
    }
    std::size_t fdcRuns = 0, wdLoads = 0;

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

        // .wd1772 muté relu par une STX valide (une itération sur 128 : E/S fichier).
        if (it % 128 == 0 && !wdSeed.empty()) {
            std::vector<uint8_t> w = wdSeed;
            for (uint32_t m = 1 + rng.below(4); m; --m) mutate(w, rng);
            const std::string wpath = dir + "/load.wd1772";
            writeBytes(wpath, w);
            StxImage stx;
            if (stx.parse(validStx()) && stx.loadWd1772(wpath)) ++wdLoads;
        }
        // Étage FDC : l'image mutée montée et exercée ; une STX reçoit parfois un
        // .wd1772 muté à côté.
        if (it % kFdcEvery == 0) {
            std::vector<uint8_t> w;
            const bool withWd = !wdSeed.empty() && (rng.next() & 1);
            if (withWd) { w = wdSeed; for (uint32_t m = 1 + rng.below(4); m; --m) mutate(w, rng); }
            if (const char* why = fuzzFdc(v, withWd ? &w : nullptr, dir, rng)) fail(why, seed, it, v);
            ++fdcRuns;
        }
    }
    fs::remove_all(work, ec);

    std::printf("[fuzz-disk] %zu itérations (seed %llu), %zu entrées acceptées, "
                "%zu passages FDC, %zu .wd1772 relus, %d violation(s)\n",
                iters, (unsigned long long)seed, accepted, fdcRuns, wdLoads, g_fail);
    if (accepted == 0) {
        std::printf("  ✗ AUCUNE entrée acceptée : le harnais n'exerce plus que le refus.\n");
        return 1;
    }
    return g_fail == 0 ? 0 : 1;
}
