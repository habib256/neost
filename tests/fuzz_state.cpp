// =============================================================================
//  fuzz_state.cpp — harnais de FUZZING du chargement des états (A44, 2026-11-27).
//
//  POURQUOI. Un fichier d'état vient de l'extérieur autant qu'une disquette : état
//  partagé, état reçu en netplay (libretro), état déposé dans la page web. Son CRC ne
//  protège de rien — un état forgé le recalcule. La chasse du 2026-11-27 y a trouvé, par
//  un fuzz jetable, une échéance négative admise par le Scheduler, un débordement signé
//  dans le contrôle du Shifter, et — par lecture — un état « avec média » qui faisait
//  réécrire n'importe quel fichier de l'hôte. Ce harnais garde ces trois portes.
//
//  COMMENT. Une machine sans TOS (ROM factice : une boucle `bra.s *`), donc sans
//  dépendance à une ROM. Deux étages, déterministes (xorshift semé, comme
//  neost-fuzz-disk) :
//   1. ÉTAT NU (`Machine::loadState`) sur ST, STE et Mega STE : octets mutés hors de la
//      RAM (la RAM est libre, la muter n'apprend rien), CRC recalculé.
//      Contrats : un état ACCEPTÉ tourne deux trames puis rend un état que la machine
//      relit (sinon l'état accepté violait un invariant que le contrôle n'a pas vu) ;
//      l'état de référence se relit toujours (un refus ne laisse pas la machine à moitié
//      chargée).
//   2. ÉTAT AVEC MÉDIA (`Machine::loadStateWithMedia`) : une disquette montée depuis un
//      dossier `disks/`, un résolveur et un filtre de recréation bornés à ce dossier
//      (comme le cœur libretro et le web). Mutations du bloc des lecteurs, dont le chemin
//      remplacé par celui d'un fichier « victime » hors du dossier.
//      Contrats : la victime n'est JAMAIS modifiée ; rien n'est créé hors de `disks/`.
//      Témoin : sans résolveur, le même état forgé réécrit bien la victime — preuve que
//      la mutation de chemin est bien formée et que le contrat mord.
//
//  Lancer :  ./build/neost-fuzz-state [--iters N] [--seed N]   (palier `fast`)
//  Il devient MORDANT dans le job `sanitizers` de la CI (ASan/UBSan).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Machine.hpp"
#include "util/Crc32.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Rng {                                     // xorshift64*, cf. fuzz_diskimage.cpp
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
    uint32_t below(uint32_t n) { return n ? uint32_t(next() % n) : 0; }
};

// En-tête d'un état (Machine.cpp, kStateHeaderSize) : magic(4) version(2) machine(1)
// ram(4) tos(2) flags(1) cartFp(4) romFp(4) sessionCfg acsiFp crc32(4), le CRC du
// payload en DERNIER. En-tête d'un état avec média (kMediaStateHead) : magic(4)
// version(2) longueur de l'état intérieur(4) longueur totale(4).
constexpr std::size_t kHdr   = 22 + Machine::kSessionCfgSize + 4 * Machine::kAcsiTargets + 4;
constexpr std::size_t kCrcAt = kHdr - 4;
constexpr std::size_t kMediaHead = 14;

int g_fail = 0;
void fail(const char* what, uint64_t seed, std::size_t it) {
    std::printf("  FAIL %s (seed %llu, itération %zu)\n", what, (unsigned long long)seed, it);
    ++g_fail;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void set32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }

std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}
void writeAll(const fs::path& p, const std::vector<uint8_t>& v) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size()));
}

std::unique_ptr<Machine> makeMachine(MachineType t) {
    auto m = std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, t);
    // ROM factice à $E00000 : SSP initial, PC = $E00008, puis `bra.s *` ($60FE).
    m->bus.rom.assign(0x30000, 0);
    m->bus.romBase = 0xE00000;
    const uint8_t rom[] = {0, 1, 0, 0, 0, 0xE0, 0, 8, 0x60, 0xFE};
    std::copy(std::begin(rom), std::end(rom), m->bus.rom.begin());
    m->reset();
    m->freezeHostClocks();
    return m;
}

// Disquette 720 Ko vierge, BPB DD 9 secteurs × 2 faces × 80 pistes.
std::vector<uint8_t> blankSt() {
    std::vector<uint8_t> d(737280, 0);
    d[0x0B] = 0x00; d[0x0C] = 0x02;              // 512 octets par secteur
    d[0x0D] = 2; d[0x0E] = 1; d[0x10] = 2;       // secteurs par cluster, réservés, FAT
    d[0x11] = 112; d[0x13] = 0xA0; d[0x14] = 0x05; d[0x15] = 0xF9;
    d[0x16] = 5; d[0x18] = 9; d[0x1A] = 2;       // secteurs par FAT, par piste, faces
    for (std::size_t i = 512; i < 512 * 4; ++i) d[i] = uint8_t(i * 13);
    return d;
}

// ---- Étage 1 : état nu ----------------------------------------------------------
std::size_t fuzzBody(MachineType t, std::size_t iters, Rng& rng, uint64_t seed) {
    auto m = makeMachine(t);
    for (int i = 0; i < 20; ++i) m->runFrame();
    std::vector<uint8_t> base;
    if (!m->saveState(base) || base.size() <= kHdr) { fail("état de référence non produit", seed, 0); return 0; }
    // La RAM (libre, octet par octet) est sautée : on la repère dans l'état.
    std::size_t ramAt = 0;
    const std::size_t ram = m->bus.ram.size();
    for (std::size_t i = kHdr; i + ram <= base.size(); ++i)
        if (std::memcmp(base.data() + i, m->bus.ram.data(), ram) == 0) { ramAt = i; break; }
    auto pick = [&]() -> std::size_t {
        for (;;) {
            const std::size_t at = kHdr + rng.below(uint32_t(base.size() - kHdr));
            if (!ramAt || at < ramAt || at >= ramAt + ram) return at;
        }
    };
    std::size_t accepted = 0;
    std::vector<uint8_t> st, again;
    for (std::size_t it = 1; it <= iters; ++it) {
        st = base;
        for (uint32_t k = 1 + rng.below(6); k; --k) {
            const std::size_t at = pick();
            switch (rng.below(4)) {
                case 0:  st[at] = uint8_t(rng.next()); break;
                case 1:  st[at] ^= uint8_t(1u << rng.below(8)); break;
                case 2:  st[at] = rng.below(2) ? 0xFF : 0x00; break;
                default: st[at] = uint8_t(st[at] + 1); break;
            }
        }
        set32(st.data() + kCrcAt, neost::crc32(st.data() + kHdr, st.size() - kHdr));
        if (m->loadState(st.data(), st.size())) {
            ++accepted;
            for (int f = 0; f < 2; ++f) m->runFrame();
            if (!m->saveState(again) || !m->loadState(again.data(), again.size()))
                fail("un état accepté rend un état que la machine refuse", seed, it);
        }
        if (!m->loadState(base.data(), base.size())) { fail("l'état de référence ne se relit plus", seed, it); break; }
    }
    return accepted;
}

// ---- Étage 2 : état avec média ---------------------------------------------------
// Remplace le chemin du lecteur A, recale longueurs et CRC du bloc des lecteurs.
std::vector<uint8_t> withDriveAPath(const std::vector<uint8_t>& s, const std::string& path) {
    const std::size_t m0 = kMediaHead + le32(s.data() + 6), end = s.size() - 4;
    const uint32_t pl = le32(s.data() + m0 + 3);
    std::vector<uint8_t> out(s.begin(), s.begin() + std::ptrdiff_t(m0 + 3));
    out.resize(out.size() + 4);
    set32(out.data() + m0 + 3, uint32_t(path.size()));
    out.insert(out.end(), path.begin(), path.end());
    out.insert(out.end(), s.begin() + std::ptrdiff_t(m0 + 7 + pl), s.begin() + std::ptrdiff_t(end));
    out.resize(out.size() + 4);
    set32(out.data() + out.size() - 4, neost::crc32(out.data() + m0, out.size() - 4 - m0));
    set32(out.data() + 10, uint32_t(out.size()));
    return out;
}

void fixMediaCrc(std::vector<uint8_t>& s) {
    const std::size_t m0 = kMediaHead + le32(s.data() + 6);
    if (m0 + 4 > s.size()) return;
    set32(s.data() + s.size() - 4, neost::crc32(s.data() + m0, s.size() - 4 - m0));
}

bool onlyExpected(const fs::path& work) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(work, ec)) {
        const std::string n = e.path().filename().string();
        if (n != "disks" && n != "victim.bin") return false;
    }
    return true;
}

std::size_t fuzzMedia(const fs::path& work, std::size_t iters, Rng& rng, uint64_t seed) {
    std::error_code ec;
    const fs::path disks = work / "disks", victim = work / "victim.bin";
    fs::create_directories(disks, ec);
    writeAll(disks / "a.st", blankSt());
    std::vector<uint8_t> victimBytes(8192);
    for (std::size_t i = 0; i < victimBytes.size(); ++i) victimBytes[i] = uint8_t('v' + i % 7);
    writeAll(victim, victimBytes);

    auto m = makeMachine(MachineType::St);
    const std::string prefix = (disks / "").lexically_normal().string();
    auto inside = [prefix](const std::string& p) {
        const std::string n = fs::path(p).lexically_normal().string();
        return n.size() > prefix.size() && n.compare(0, prefix.size(), prefix) == 0;
    };
    // Comme le cœur libretro : le chemin tel quel s'il est dans disks/, sinon par nom.
    auto resolver = [inside, disks](const std::string& p) -> std::string {
        std::error_code e;
        if (inside(p) && fs::is_regular_file(p, e)) return fs::path(p).lexically_normal().string();
        const std::string name = fs::path(p).filename().string();
        if (name.empty() || name == "." || name == "..") return {};
        const fs::path own = disks / name;
        return fs::is_regular_file(own, e) ? own.string() : std::string();
    };
    m->fdc.setMediaRecreateFilter(inside);
    if (!m->fdc.loadImage((disks / "a.st").string(), 0)) { fail("disquette de référence non montée", seed, 0); return 0; }
    for (int i = 0; i < 5; ++i) m->runFrame();
    std::vector<uint8_t> base;
    if (!m->saveStateWithMedia(base) || base.size() < kMediaHead + 8) { fail("état avec média non produit", seed, 0); return 0; }

    // Témoin : sans résolveur, l'état forgé réécrit la victime (le contrat mord).
    {
        const std::vector<uint8_t> forged = withDriveAPath(base, victim.string());
        m->loadStateWithMedia(forged.data(), forged.size());
        if (readAll(victim) == victimBytes)
            fail("témoin : la mutation de chemin n'atteint plus la victime (harnais émoussé)", seed, 0);
        writeAll(victim, victimBytes);
        if (!m->loadStateWithMedia(base.data(), base.size())) { fail("témoin : référence non relue", seed, 0); return 0; }
    }
    m->fdc.setMediaPathResolver(resolver);

    const std::string targets[] = {
        victim.string(), (disks / ".." / "victim.bin").string(), "victim.bin",
        (work / "created.st").string(), (disks / "new.st").string(), "/", ""};
    std::size_t accepted = 0;
    for (std::size_t it = 1; it <= iters; ++it) {
        std::vector<uint8_t> s;
        if (rng.below(2)) {
            s = withDriveAPath(base, targets[rng.below(uint32_t(std::size(targets)))]);
        } else {
            s = base;
            const std::size_t m0 = kMediaHead + le32(s.data() + 6);
            for (uint32_t k = 1 + rng.below(4); k; --k) {
                // Les premiers octets (drapeaux, longueur et chemin) d'abord, l'image ensuite.
                const std::size_t span = rng.below(2) ? std::min<std::size_t>(64, s.size() - 4 - m0) : s.size() - 4 - m0;
                s[m0 + rng.below(uint32_t(span))] ^= uint8_t(1u << rng.below(8));
            }
            fixMediaCrc(s);
        }
        if (m->loadStateWithMedia(s.data(), s.size(), /*recreateMissing=*/rng.below(2) != 0)) {
            ++accepted;
            m->runFrame();
        }
        if (readAll(victim) != victimBytes) { fail("un état a modifié un fichier hors de disks/", seed, it); writeAll(victim, victimBytes); }
        if (!onlyExpected(work)) { fail("un état a créé un fichier hors de disks/", seed, it); break; }
        if (!m->loadStateWithMedia(base.data(), base.size())) { fail("l'état avec média de référence ne se relit plus", seed, it); break; }
    }
    return accepted;
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t iters = 400;
    uint64_t    seed  = 0x5EEDull;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--iters") && i + 1 < argc) iters = std::strtoull(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 0);
        else { std::printf("usage: %s [--iters N] [--seed N]\n", argv[0]); return 2; }
    }
    Rng rng(seed);
    std::size_t acc[3] = {};
    const MachineType types[3] = {MachineType::St, MachineType::Ste, MachineType::MegaSte};
    for (int k = 0; k < 3; ++k) acc[k] = fuzzBody(types[k], iters, rng, seed);

    std::error_code ec;
    const fs::path work = fs::temp_directory_path(ec)
        / ("neost-fuzz-state-" + std::to_string(seed) + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(&rng)));
    fs::create_directories(work, ec);
    const std::size_t mediaAcc = fuzzMedia(work, iters, rng, seed);
    fs::remove_all(work, ec);

    std::printf("[fuzz-state] %zu itérations par étage (seed %llu) : états nus acceptés ST %zu, STE %zu, "
                "Mega STE %zu ; états avec média acceptés %zu ; %d violation(s)\n",
                iters, (unsigned long long)seed, acc[0], acc[1], acc[2], mediaAcc, g_fail);
    // Couverture du pauvre : un étage qui n'accepte plus rien n'exerce que le refus.
    if (acc[0] == 0 || acc[1] == 0 || acc[2] == 0 || mediaAcc == 0) {
        std::printf("  ✗ un étage n'accepte plus aucun état : le harnais n'exerce plus que le refus.\n");
        return 1;
    }
    return g_fail == 0 ? 0 : 1;
}
