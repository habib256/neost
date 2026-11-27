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
//  ARCHIVES (2026-11-27). Les disquettes arrivent aussi dans un .zip, un .gz ou un .7z
//  (cœur libretro, GUI) : `listZip`/`extractZip`, `gunzip` et `archive::Reader` digèrent
//  eux aussi un fichier venu de l'extérieur. Graines construites ici (zip stocké et
//  deflate, gzip avec FNAME) plus un 7z LZMA2 de 1 Ko embarqué, mutées comme les images.
//  Contrats : rien n'est extrait au-delà de Reader::kMaxUnpacked (une bombe de 611 Ko
//  donnait 600 Mo), un « oui » rend la taille annoncée, et un nom de membre passé par
//  `safeMemberPath` n'est jamais absolu ni ne remonte (« .. »).
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
#include "util/Archive.hpp"
#include "util/Crc32.hpp"

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

// ---- Archives -----------------------------------------------------------------
void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }

// Bloc deflate STOCKÉ ([B] RFC 1951 § 3.2.4) : BFINAL=1, BTYPE=00, LEN, NLEN, données.
std::vector<uint8_t> deflateStored(const std::vector<uint8_t>& d) {
    std::vector<uint8_t> v{0x01};
    put16(v, uint32_t(d.size())); put16(v, uint32_t(~d.size() & 0xFFFF));
    v.insert(v.end(), d.begin(), d.end());
    return v;
}

// Zip à deux membres ([B] APPNOTE.TXT § 4.3) : l'un stocké, l'autre « deflate ».
std::vector<uint8_t> seedZip() {
    struct M { std::string name; uint16_t method; std::vector<uint8_t> data; };
    std::vector<uint8_t> a(700), b(300);
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = uint8_t(i * 7);
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = uint8_t('A' + i % 26);
    const M ms[2] = {{"GAME/DISK1.ST", 0, a}, {"README.TXT", 8, b}};
    std::vector<uint8_t> z, cd;
    for (const M& m : ms) {
        const std::vector<uint8_t> packed = m.method ? deflateStored(m.data) : m.data;
        const uint32_t crc = neost::crc32(m.data.data(), m.data.size()), at = uint32_t(z.size());
        put32(z, 0x04034B50u); put16(z, 20); put16(z, 0); put16(z, m.method); put32(z, 0);
        put32(z, crc); put32(z, uint32_t(packed.size())); put32(z, uint32_t(m.data.size()));
        put16(z, uint16_t(m.name.size())); put16(z, 0);
        z.insert(z.end(), m.name.begin(), m.name.end());
        z.insert(z.end(), packed.begin(), packed.end());
        put32(cd, 0x02014B50u); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, m.method); put32(cd, 0);
        put32(cd, crc); put32(cd, uint32_t(packed.size())); put32(cd, uint32_t(m.data.size()));
        put16(cd, uint16_t(m.name.size())); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0);
        put32(cd, 0); put32(cd, at);
        cd.insert(cd.end(), m.name.begin(), m.name.end());
    }
    const uint32_t cdAt = uint32_t(z.size());
    z.insert(z.end(), cd.begin(), cd.end());
    put32(z, 0x06054B50u); put16(z, 0); put16(z, 0); put16(z, 2); put16(z, 2);
    put32(z, uint32_t(cd.size())); put32(z, cdAt); put16(z, 0);
    return z;
}

// Gzip ([B] RFC 1952 § 2.3) avec FNAME, un bloc stocké.
std::vector<uint8_t> seedGzip() {
    std::vector<uint8_t> d(900);
    for (std::size_t i = 0; i < d.size(); ++i) d[i] = uint8_t(i ^ (i >> 3));
    std::vector<uint8_t> g{0x1F, 0x8B, 8, 0x08, 0, 0, 0, 0, 0, 3};
    for (char c : std::string("DISK.ST")) g.push_back(uint8_t(c));
    g.push_back(0);
    const std::vector<uint8_t> packed = deflateStored(d);
    g.insert(g.end(), packed.begin(), packed.end());
    put32(g, neost::crc32(d.data(), d.size())); put32(g, uint32_t(d.size()));
    return g;
}

// 7z LZMA2 (bsdtar 3.5.3 : DISK.ST de 1 500 octets + README.TXT).
const uint8_t kSeed7z[] = {
    0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, 0x00, 0x03, 0x20, 0x7E, 0x7C, 0x87, 0x17, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x3E, 0x8A, 0x7C,
    0xE0, 0x05, 0xFC, 0x03, 0x90, 0x5D, 0x00, 0x10, 0x13, 0xC8, 0xC2, 0x02, 0x91, 0xAD, 0xC0, 0x80,
    0xDC, 0xBF, 0xE0, 0x22, 0xC9, 0x31, 0x36, 0xCC, 0x0F, 0x8E, 0x64, 0x12, 0xE1, 0xE4, 0xF9, 0x94,
    0x22, 0xB2, 0xC3, 0xB0, 0x98, 0x42, 0x81, 0x40, 0x01, 0xF4, 0x99, 0x1C, 0xA3, 0x64, 0x53, 0x66,
    0x75, 0xEF, 0x80, 0xAA, 0xF7, 0x4A, 0xCE, 0x6B, 0x6C, 0x41, 0x82, 0x79, 0x1B, 0x18, 0x51, 0xB2,
    0x9E, 0x2F, 0x2E, 0xA7, 0x05, 0x20, 0xCD, 0x7F, 0x6C, 0x28, 0xAE, 0x3E, 0xD5, 0x52, 0x0B, 0x17,
    0x1E, 0xF3, 0x49, 0x62, 0xC2, 0xF8, 0xCA, 0x2C, 0x8C, 0x4E, 0x88, 0x98, 0x7B, 0xFD, 0xA3, 0x8F,
    0xC3, 0x32, 0xB1, 0x7C, 0x91, 0x69, 0x5B, 0xE5, 0x3A, 0x13, 0xC7, 0x09, 0x2F, 0xD1, 0x12, 0x56,
    0xA9, 0xD4, 0x5F, 0x1A, 0x93, 0x16, 0xB7, 0x10, 0x96, 0xA3, 0x47, 0x1A, 0x0F, 0x65, 0xA1, 0x85,
    0xD2, 0x74, 0x2D, 0x2C, 0x91, 0xEF, 0xB0, 0xE7, 0xDD, 0x5F, 0x0E, 0x63, 0x15, 0xBC, 0xB4, 0x4F,
    0x57, 0x9D, 0x89, 0x9D, 0x08, 0x12, 0xB8, 0x85, 0xDF, 0xF3, 0x2A, 0x7B, 0x19, 0x4B, 0xA0, 0xA7,
    0x34, 0x57, 0xD2, 0x99, 0xAF, 0x1D, 0xF2, 0x87, 0xE9, 0xBB, 0x35, 0xCE, 0x3B, 0x0B, 0xB1, 0xB9,
    0x2F, 0x5E, 0x4D, 0xA2, 0xED, 0x5E, 0xFE, 0x7F, 0x7C, 0x8D, 0xA0, 0xD4, 0x68, 0xA7, 0x04, 0x9C,
    0x23, 0xA4, 0x1A, 0x92, 0x61, 0x41, 0x3E, 0x87, 0x5E, 0x58, 0xF0, 0xDF, 0x96, 0xB5, 0x90, 0x28,
    0x78, 0xE0, 0xC2, 0xAE, 0x0A, 0xC8, 0x43, 0xB7, 0x2A, 0x0F, 0x38, 0x4F, 0x21, 0x4F, 0xFC, 0x17,
    0xB1, 0x30, 0x14, 0x35, 0x7A, 0xDF, 0x8F, 0x2A, 0x7B, 0x2C, 0xBC, 0x81, 0xC7, 0x2F, 0x6B, 0x78,
    0x11, 0xE0, 0xC8, 0x2B, 0x78, 0xB0, 0x2B, 0xAA, 0xFA, 0x0A, 0xBF, 0xBD, 0x29, 0xC9, 0x0D, 0xD5,
    0x21, 0x4B, 0x7B, 0x78, 0x81, 0xB8, 0x20, 0x9D, 0x97, 0x6C, 0xEC, 0xFA, 0x72, 0x5C, 0x86, 0x99,
    0x52, 0x3E, 0x89, 0xAC, 0xE4, 0xCC, 0x94, 0x07, 0xF7, 0xC7, 0x04, 0x95, 0x32, 0xE8, 0x64, 0xAA,
    0x04, 0xA9, 0x54, 0x24, 0x92, 0xAD, 0x4C, 0xC6, 0x6B, 0x2B, 0xD1, 0xA6, 0xCC, 0xBC, 0xD1, 0xE8,
    0x49, 0x02, 0x71, 0xE5, 0x23, 0xDC, 0x12, 0xD6, 0x33, 0xEA, 0x81, 0x76, 0x18, 0x7B, 0xC9, 0x4F,
    0xE3, 0x86, 0x76, 0x4E, 0xCE, 0xDD, 0x9B, 0xC5, 0xAB, 0x93, 0xD1, 0xCC, 0x7F, 0xBA, 0x5F, 0xB4,
    0x20, 0x96, 0xED, 0x14, 0x60, 0x29, 0xE8, 0x6D, 0xF4, 0x03, 0x98, 0x0C, 0x5C, 0x62, 0x2F, 0x57,
    0xAC, 0xE6, 0xFC, 0x6C, 0x47, 0xA0, 0x26, 0x5B, 0x48, 0x51, 0xFF, 0x1D, 0x39, 0x04, 0xDA, 0xEE,
    0xC5, 0x6E, 0xE9, 0xDB, 0xED, 0xFA, 0x45, 0x02, 0xBA, 0x82, 0x8B, 0x97, 0x9D, 0xE0, 0xD4, 0x4D,
    0x0D, 0x28, 0xD7, 0xF3, 0x0B, 0x75, 0x82, 0xE5, 0x4C, 0x17, 0x25, 0x26, 0x49, 0x63, 0x27, 0x30,
    0xAF, 0xB3, 0xD7, 0xA9, 0xB3, 0x3A, 0xE8, 0xB4, 0x95, 0x79, 0xA0, 0x16, 0xFB, 0x37, 0x8D, 0x74,
    0xAC, 0xA6, 0x55, 0x6B, 0x92, 0x71, 0xD1, 0xA0, 0x51, 0xB9, 0x5C, 0x46, 0x9E, 0x46, 0xD8, 0x3B,
    0x48, 0xD6, 0xA4, 0x4F, 0x4F, 0x62, 0xEB, 0x14, 0x90, 0x6F, 0x9C, 0x61, 0x40, 0x52, 0xFA, 0x81,
    0x39, 0xAB, 0x2C, 0x75, 0x2E, 0x82, 0xBA, 0x42, 0xC8, 0x33, 0xB6, 0xE7, 0xEF, 0x49, 0xAA, 0xA4,
    0xC5, 0xD0, 0x80, 0xBF, 0x31, 0x99, 0xCC, 0xE9, 0x7A, 0x46, 0x14, 0x85, 0x74, 0x21, 0x00, 0x12,
    0xD4, 0xC5, 0x90, 0xFC, 0x2A, 0x31, 0xC7, 0x73, 0x59, 0x3A, 0xCC, 0x38, 0x98, 0xCA, 0xA4, 0x6D,
    0xD7, 0xDE, 0xC9, 0x60, 0x91, 0x9E, 0xBC, 0x52, 0x7D, 0xC2, 0xF5, 0x40, 0x86, 0x4A, 0x4F, 0xA5,
    0x91, 0xD4, 0x33, 0xAC, 0x50, 0x5F, 0xBC, 0x59, 0x92, 0x1E, 0x20, 0xDA, 0x07, 0x66, 0x83, 0x10,
    0x5D, 0x57, 0x2C, 0xFB, 0x64, 0x89, 0x40, 0x50, 0x0A, 0x88, 0x3F, 0x57, 0xFA, 0x6D, 0x9B, 0x5B,
    0x37, 0xCE, 0x85, 0x69, 0xD2, 0xB6, 0xBF, 0x58, 0x79, 0x70, 0xED, 0x5B, 0x24, 0x91, 0x7B, 0x7C,
    0x25, 0x54, 0x83, 0x59, 0x98, 0xF2, 0x6A, 0xF0, 0xE3, 0x1E, 0xCD, 0xA9, 0xE6, 0xB2, 0x62, 0x25,
    0x64, 0x89, 0x08, 0x1B, 0x74, 0x16, 0x67, 0xC4, 0xBD, 0x94, 0xCE, 0xF8, 0x6E, 0x90, 0x1D, 0xB1,
    0xF6, 0x14, 0x9A, 0xE6, 0x67, 0x34, 0x46, 0x50, 0x83, 0xC9, 0x05, 0x08, 0xAA, 0x2A, 0xBF, 0x1C,
    0xF2, 0xCB, 0xCD, 0xAA, 0x56, 0x8A, 0x35, 0x4C, 0xF8, 0x40, 0xA8, 0x1F, 0x90, 0x4C, 0xBF, 0x6E,
    0x0C, 0x1A, 0x5E, 0x13, 0xE2, 0xA7, 0x16, 0x70, 0xE0, 0x4B, 0xCC, 0x4D, 0xE7, 0x84, 0xA4, 0x7C,
    0xF2, 0x29, 0x59, 0x38, 0xE0, 0xCB, 0x3F, 0x22, 0x04, 0xED, 0x6B, 0xED, 0x88, 0x70, 0xCC, 0xDC,
    0xC5, 0x08, 0x27, 0xF6, 0x60, 0x3B, 0x75, 0x6A, 0xB1, 0x0A, 0x60, 0xA9, 0x50, 0xF4, 0xFF, 0x9B,
    0x05, 0xB4, 0x21, 0x12, 0x86, 0x36, 0x00, 0x39, 0xBB, 0xF8, 0xFE, 0x83, 0x2C, 0xD7, 0x6C, 0xB5,
    0x86, 0xE7, 0x84, 0x66, 0x2F, 0xB2, 0x0A, 0xC3, 0xE3, 0x04, 0xBD, 0xE9, 0x74, 0x2A, 0x91, 0xD4,
    0xFE, 0x5F, 0x8D, 0xAA, 0x42, 0x45, 0x68, 0x2B, 0x91, 0x60, 0xC5, 0xBC, 0xE8, 0x77, 0xCD, 0x20,
    0x18, 0x75, 0x70, 0x9E, 0x41, 0xC5, 0xD6, 0x3E, 0xD2, 0x39, 0x2F, 0x9B, 0xC0, 0x21, 0x72, 0xA7,
    0x43, 0x7C, 0x36, 0x37, 0x8B, 0x53, 0xB2, 0xBE, 0x23, 0x25, 0xF3, 0x0F, 0x3A, 0xC7, 0x15, 0xCA,
    0xBF, 0x04, 0xC0, 0x65, 0xC9, 0x44, 0x43, 0xA1, 0xED, 0x8F, 0x97, 0xCF, 0x83, 0x39, 0x6A, 0xA2,
    0xE9, 0xF6, 0x8D, 0x98, 0xE4, 0xEF, 0x77, 0x95, 0x5F, 0xB5, 0x96, 0xD9, 0x5A, 0xD4, 0xAB, 0x66,
    0x34, 0x7E, 0x51, 0x40, 0x3A, 0xA0, 0x7A, 0xAC, 0x3E, 0xDE, 0xF7, 0xBB, 0x2C, 0xA2, 0x26, 0xA6,
    0x30, 0xBF, 0x10, 0x2E, 0xA0, 0xB3, 0x9C, 0x00, 0xEC, 0xD7, 0xCE, 0x68, 0xC5, 0x8D, 0xDC, 0x9F,
    0x71, 0xA6, 0xF0, 0x72, 0x6D, 0xFB, 0x10, 0xE2, 0x9B, 0xE1, 0x21, 0x7E, 0x03, 0x8C, 0x26, 0xD3,
    0x20, 0x24, 0xC1, 0x00, 0x2A, 0xC7, 0xCB, 0x96, 0x08, 0x8E, 0x08, 0x04, 0x79, 0x7D, 0x47, 0x4D,
    0x95, 0x11, 0x81, 0xE8, 0xB2, 0xC3, 0xA9, 0xEA, 0xE1, 0xB9, 0x75, 0xCC, 0xDA, 0xFE, 0x0E, 0xB5,
    0xB0, 0x55, 0x82, 0x31, 0x74, 0x8D, 0xA5, 0x14, 0xF5, 0xAB, 0xB6, 0x9A, 0x3C, 0xF5, 0xA2, 0xA3,
    0x77, 0x66, 0x0B, 0x75, 0x07, 0xB9, 0x50, 0x5C, 0xF4, 0x5F, 0x03, 0x38, 0xFB, 0x9E, 0x90, 0x3D,
    0xF2, 0xA6, 0x9E, 0x8A, 0x46, 0x58, 0xA5, 0x16, 0x27, 0x5A, 0x5E, 0x4C, 0x52, 0x94, 0x14, 0xDF,
    0x3B, 0xEB, 0xCE, 0xBD, 0x79, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x9C, 0x00, 0x77, 0x5D, 0x00, 0x00,
    0x81, 0x33, 0x07, 0xAE, 0x0F, 0xD5, 0x51, 0x31, 0x17, 0x16, 0x6C, 0x29, 0x1F, 0x48, 0x1D, 0x98,
    0xF4, 0x6D, 0x71, 0xA0, 0x0E, 0x57, 0x4E, 0x39, 0xA2, 0x7E, 0x9B, 0x31, 0x58, 0x30, 0xD7, 0xE2,
    0x2F, 0xB4, 0x45, 0x4B, 0xA9, 0x94, 0xA8, 0xAA, 0x70, 0x4E, 0xD3, 0x4B, 0x48, 0xC7, 0x97, 0x17,
    0x4A, 0x49, 0x77, 0x28, 0x9E, 0xF4, 0xE9, 0x1E, 0x21, 0x69, 0x29, 0xB3, 0x67, 0x05, 0x5B, 0x74,
    0x41, 0x0E, 0xCF, 0x7A, 0xDF, 0xE8, 0xCF, 0xC3, 0x96, 0x66, 0x48, 0x29, 0x43, 0x9F, 0x58, 0x87,
    0x20, 0x5F, 0x27, 0x24, 0xD9, 0x55, 0xF0, 0xF3, 0x13, 0xA2, 0x9A, 0x85, 0xDC, 0xDF, 0x6A, 0x28,
    0x8C, 0xBB, 0x3A, 0xC2, 0xE8, 0x5A, 0x10, 0xF6, 0x4D, 0x3E, 0x35, 0x76, 0x04, 0xD5, 0x4E, 0x9F,
    0x51, 0xD4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x06, 0x83, 0x98, 0x01, 0x09, 0x7F, 0x00, 0x07,
    0x0B, 0x01, 0x00, 0x01, 0x21, 0x21, 0x01, 0x16, 0x0C, 0x80, 0x9D, 0x0A, 0x01, 0xBD, 0x68, 0x31,
    0x96, 0x00, 0x00,
};

const char* fuzzArchive(const std::vector<uint8_t>& v) {
    namespace ar = neost::archive;
    constexpr uint64_t kMax = ar::Reader::kMaxUnpacked;
    std::vector<uint8_t> out;
    std::string name;
    if (ar::isGzip(v) && ar::gunzip(v, out, &name) && out.size() > kMax)
        return "gunzip a rendu plus que kMaxUnpacked";
    std::vector<ar::ZipMember> zm;
    if (ar::isZip(v) && ar::listZip(v, zm))
        for (std::size_t i = 0; i < zm.size() && i < 8; ++i)
            if (ar::extractZip(v, zm[i], out) && out.size() != zm[i].size)
                return "extractZip : « oui » d'une autre taille que l'annoncée";
    ar::Reader r;
    if (r.open(v)) {
        for (std::size_t i = 0; i < r.members().size() && i < 8; ++i) {
            const ar::Member& m = r.members()[i];
            const std::string safe = ar::safeMemberPath(m.name);
            if (!safe.empty()) {
                if (safe[0] == '/' || safe[0] == '\\') return "safeMemberPath rend un chemin absolu";
                for (const auto& part : std::filesystem::path(safe))
                    if (part == "..") return "safeMemberPath rend un chemin qui remonte";
            }
            if (m.directory) continue;
            if (r.extract(i, out)) {
                if (out.size() > kMax) return "Reader a rendu plus que kMaxUnpacked";
                if (out.size() != m.size) return "Reader : « oui » d'une autre taille que l'annoncée";
            }
        }
    }
    return nullptr;
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

    // Archives : les trois graines passent d'abord telles quelles (et doivent s'ouvrir :
    // sinon le harnais ne testerait plus que le refus), puis mutées.
    const std::vector<std::vector<uint8_t>> arcSeeds = {
        seedZip(), seedGzip(), std::vector<uint8_t>(std::begin(kSeed7z), std::end(kSeed7z))};
    std::size_t arcOpened = 0;
    for (const auto& a : arcSeeds) {
        if (const char* why = fuzzArchive(a)) fail(why, seed, 0, a);
        std::vector<uint8_t> out;
        neost::archive::Reader r;
        const bool opens = neost::archive::isGzip(a) ? neost::archive::gunzip(a, out)
                                                    : (r.open(a) && !r.members().empty() && r.extract(0, out));
        if (!opens) { std::printf("  FAIL graine d'archive illisible (%zu octets)\n", a.size()); ++g_fail; }
    }
    const std::size_t arcIters = iters / 4;
    for (std::size_t it = 1; it <= arcIters; ++it) {
        std::vector<uint8_t> a = arcSeeds[rng.below(uint32_t(arcSeeds.size()))];
        for (uint32_t m = 1 + rng.below(4); m; --m) mutate(a, rng);
        if (const char* why = fuzzArchive(a)) fail(why, seed, iters + it, a);
        neost::archive::Reader r;
        std::vector<uint8_t> out;
        if (r.open(a) || neost::archive::gunzip(a, out)) ++arcOpened;
    }

    std::printf("[fuzz-disk] %zu itérations (seed %llu), %zu entrées acceptées, "
                "%zu passages FDC, %zu .wd1772 relus, %zu/%zu archives ouvertes, %d violation(s)\n",
                iters, (unsigned long long)seed, accepted, fdcRuns, wdLoads, arcOpened, arcIters, g_fail);
    if (accepted == 0) {
        std::printf("  ✗ AUCUNE entrée acceptée : le harnais n'exerce plus que le refus.\n");
        return 1;
    }
    return g_fail == 0 ? 0 : 1;
}
