// =============================================================================
//  libretro_frontier_logic.cpp — Ce qu'un frontend libretro exige du cœur, gardé
//  sans ROM ni frontend (docs/LIBRETRO.md, TODO § 7 « Cœur libretro ») :
//
//   · testMediaState         — états AUTONOMES (Machine::saveStateWithMedia) : le
//     blob porte la disquette, la reprise la rend en mémoire ET dans le fichier hôte.
//     Une trame jouée par anticipation qui écrit sur la disquette doit être défaite
//     par la reprise de l'état. Ici l'« écriture » est un échange de contenu par
//     remontage : même chemin de remontée (Fdc::restoreMedia) qu'une écriture
//     secteur, que Fdc::mediaSelfTest couvre déjà pour le retour arrière.
//   · testMachineReconstruct — retro_unload_game puis retro_load_game dans le même
//     processus : une Machine reconstruite repart du MÊME état qu'une première,
//     rien ne fuit d'une instance à l'autre par un état de processus.
//   · testAvTracker          — taille et cadence annoncées sur un changement STABLE
//     (SET_GEOMETRY, SET_SYSTEM_AV_INFO), pas sur une trame isolée.
//   · testLogSink            — les journaux du cœur se détournent (retro_log_printf)
//     et reviennent sur stderr quand le puits est retiré.
//
//  Sans ROM : la Machine tourne sur une ROM nulle (BRA sur place), comme
//  machine_fdc_media_vbl_logic.cpp.
// =============================================================================
#include "core/Machine.hpp"
#include "util/Log.hpp"
#include "core/Pacing.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kSize = 737280;   // 720 Ko : 80 pistes, 9 secteurs, 2 faces

std::vector<uint8_t> makeImage(uint8_t fill) {
    std::vector<uint8_t> v(kSize, fill);
    v[0x13] = 0xA0; v[0x14] = 0x05;      // BPB : 1440 secteurs
    v[0x18] = 9;    v[0x19] = 0;         // 9 secteurs par piste (mot LE)
    v[0x1A] = 2;    v[0x1B] = 0;         // 2 faces (mot LE)
    return v;
}
void writeFile(const std::string& path, const std::vector<uint8_t>& v) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size()));
}
std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}
std::unique_ptr<Machine> makeMachine() {
    auto m = std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, MachineType::St);
    m->bus.rom.assign(0x30000, 0);
    m->bus.romBase = 0xE00000;
    const uint8_t rom[] = {0, 1, 0, 0, 0, 0xE0, 0, 8, 0x60, 0xFE};   // BRA sur place
    std::copy(std::begin(rom), std::end(rom), m->bus.rom.begin());
    m->reset();
    return m;
}
}  // namespace

int testMediaState() {
    namespace fs = std::filesystem;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (ok) return;
        ++failures;
        std::fprintf(stderr, "[media-state] FAIL %s\n", what);
    };
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path()
        / ("neost_media_state_" + std::to_string(reinterpret_cast<std::uintptr_t>(&ec)));
    fs::create_directories(dir, ec);
    const std::string pathA = (dir / "a.st").string(), pathB = (dir / "b.st").string();
    const auto imageA = makeImage(0x11), imageB = makeImage(0x22), imageA2 = makeImage(0x33);
    writeFile(pathA, imageA);
    writeFile(pathB, imageB);

    auto m = makeMachine();
    check(m->loadDisk(pathA), "mount A");
    for (int i = 0; i < 3; ++i) m->runFrame();

    std::vector<uint8_t> blob, again;
    check(m->saveStateWithMedia(blob), "save with media");
    check(m->saveStateWithMedia(again) && again == blob, "two saves in a row are identical");

    // Le contenu de A change (le fichier hôte avec) : la reprise doit le défaire.
    writeFile(pathA, imageA2);
    m->fdc.eject(0);
    check(m->loadDisk(pathA), "remount A with new content");
    m->runFrame();
    check(m->loadStateWithMedia(blob.data(), blob.size()), "load with media");
    check(readFile(pathA) == imageA, "host file of A restored");
    check(m->saveStateWithMedia(again) && again == blob, "re-save after load matches the blob");

    // Disquette échangée (B en A) : la reprise remonte A. Même taille de média, même
    // taille d'état, trames jouées ou non — retro_serialize_size ne bouge pas.
    m->fdc.eject(0);
    check(m->loadDisk(pathB), "swap to B");
    for (int i = 0; i < 10; ++i) m->runFrame();
    std::vector<uint8_t> swapped;
    check(m->saveStateWithMedia(swapped) && swapped.size() == blob.size(),
          "same-size floppy and more frames: same state size");
    check(m->loadStateWithMedia(blob.data(), blob.size()), "load after a swap");
    check(m->fdc.mountedPath(0) == pathA, "drive A holds A again");

    // Lecteur vidé après la sauvegarde : la reprise remonte A ; et l'inverse, un état
    // pris lecteur vide éjecte la disquette montée depuis.
    m->fdc.eject(0);
    check(m->loadStateWithMedia(blob.data(), blob.size()), "load into an empty drive");
    check(m->fdc.inserted(0), "drive A filled again");
    m->fdc.eject(0);
    std::vector<uint8_t> empty;
    check(m->saveStateWithMedia(empty), "save with an empty drive");
    check(m->loadDisk(pathB), "mount B after the empty save");
    check(m->loadStateWithMedia(empty.data(), empty.size()), "load the empty-drive state");
    check(!m->fdc.inserted(0), "drive A empty again");

    // Fichier de A disparu (page web rechargée) : refusé par défaut ; recréé depuis le
    // blob sur demande, à l'identique, et jamais par-dessus un fichier présent.
    m->fdc.eject(0);
    fs::remove(pathA, ec);
    check(!m->loadStateWithMedia(blob.data(), blob.size()), "missing floppy file refused by default");
    check(!fs::exists(pathA), "nothing recreated by default");
    check(m->loadStateWithMedia(blob.data(), blob.size(), /*recreateMissing=*/true),
          "missing floppy file recreated on request");
    check(readFile(pathA) == imageA, "recreated file holds the saved image");
    check(Machine::isStateWithMedia(blob.data(), blob.size()) && m->stateWithMediaHeaderOk(blob.data(), blob.size()),
          "media state recognised, header accepted");

    // Même chose pour une .msa et une .dim : recréées DANS LEUR CONTENEUR (export honnête),
    // puis remontées avec le même contenu.
    for (const char* ext : {".msa", ".dim"}) {
        const std::string other = (dir / (std::string("c") + ext)).string();
        auto c = makeMachine();
        writeFile(other, imageA);                       // contenu brut, reconnu au montage …
        check(c->loadDisk(other), "mount the container path");
        std::vector<uint8_t> st1;
        check(c->saveStateWithMedia(st1), "save on the container path");
        c->fdc.eject(0);
        fs::remove(other, ec);                          // … puis disparu
        check(c->loadStateWithMedia(st1.data(), st1.size(), true), "container recreated");
        const auto bytes = readFile(other);
        const bool tagged = std::string(ext) == ".msa" ? (bytes.size() > 2 && bytes[0] == 0x0E && bytes[1] == 0x0F)
                                                       : (bytes.size() == imageA.size() + 32 && bytes[0] == 'B' && bytes[1] == 'B');
        check(tagged, "recreated file carries its container header");
        std::vector<uint8_t> st2;
        check(c->saveStateWithMedia(st2) && st2 == st1, "recreated container mounts the same image");
    }

    // Une autre Machine (même configuration) reprend le blob : rien ne dépend des
    // générations de la session qui l'a écrit.
    auto other = makeMachine();
    check(other->loadStateWithMedia(blob.data(), blob.size()), "load into a fresh machine");
    std::vector<uint8_t> fromOther;
    check(other->saveStateWithMedia(fromOther) && fromOther == blob, "fresh machine re-saves the same blob");

    // Taille fixe (libretro) : le blob complété par des zéros se reprend tel quel.
    std::vector<uint8_t> padded = blob;
    padded.resize(blob.size() + 4096, 0);
    check(m->loadStateWithMedia(padded.data(), padded.size()), "zero-padded blob accepted");

    // Refus : un blob tronqué, un octet de disquette corrompu, un .state nu.
    check(!m->loadStateWithMedia(blob.data(), blob.size() - 1), "truncated blob refused");
    std::vector<uint8_t> bad = blob;
    bad[bad.size() - 100] ^= 0xFF;
    check(!m->loadStateWithMedia(bad.data(), bad.size()), "corrupt media refused");
    std::vector<uint8_t> plain;
    check(m->saveState(plain) && !m->loadStateWithMedia(plain.data(), plain.size()), "plain .state refused");

    other.reset();
    m.reset();
    fs::remove_all(dir, ec);
    std::printf("[media-state] %s\n", failures ? "FAIL" : "ok");
    return failures;
}

int testMachineReconstruct() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (ok) return;
        ++failures;
        std::fprintf(stderr, "[machine-reconstruct] FAIL %s\n", what);
    };
    std::vector<uint8_t> first, second;
    {
        auto m = makeMachine();
        for (int i = 0; i < 20; ++i) m->runFrame();
        check(m->saveState(first), "first machine saves");
    }
    {
        auto m = makeMachine();
        for (int i = 0; i < 20; ++i) m->runFrame();
        check(m->saveState(second), "second machine saves");
    }
    check(!first.empty() && first == second, "a rebuilt machine runs to the same state");
    std::printf("[machine-reconstruct] %s\n", failures ? "FAIL" : "ok");
    return failures;
}

int testLogSink() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (ok) return;
        ++failures;
        std::fprintf(stderr, "[log-sink] FAIL %s\n", what);
    };
    std::string got;
    neost::log::setSink([](const char* text, void* user) {
        *static_cast<std::string*>(user) += text;
    }, &got);
    neost::log::err("[test] %s %d\n", "value", 42);
    const std::string longText(3000, 'x');                 // au-delà du tampon de pile
    neost::log::err("%s|\n", longText.c_str());
    neost::log::setSink(nullptr);
    check(got == "[test] value 42\n" + longText + "|\n", "sink receives formatted text");
    std::printf("[log-sink] %s\n", failures ? "FAIL" : "ok");
    return failures;
}

int testAvTracker() {
    using neost::pacing::AvTracker;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (ok) return;
        ++failures;
        std::fprintf(stderr, "[av-tracker] FAIL %s\n", what);
    };
    constexpr int64_t kPal = 313 * 512, kNtsc = 263 * 508, kMono = 501 * 224;
    AvTracker t;
    check(t.onFrame(320, 200, kPal) == (AvTracker::Geometry | AvTracker::Timing), "first frame announces all");
    check(t.onFrame(320, 200, kPal) == AvTracker::None, "steady frame announces nothing");
    // Une trame isolée en 60 Hz : rien.
    check(t.onFrame(320, 200, kNtsc) == AvTracker::None, "isolated 60 Hz frame ignored");
    check(t.onFrame(320, 200, kPal) == AvTracker::None, "back to 50 Hz");
    // Passage durable en 60 Hz : annoncé à la 3e trame, cadence seule.
    check(t.onFrame(320, 200, kNtsc) == AvTracker::None, "60 Hz #1");
    check(t.onFrame(320, 200, kNtsc) == AvTracker::None, "60 Hz #2");
    check(t.onFrame(320, 200, kNtsc) == AvTracker::Timing, "60 Hz #3 announced");
    check(t.announced().frameCycles == kNtsc, "60 Hz retained");
    // Moniteur monochrome : taille ET cadence.
    t.onFrame(640, 400, kMono); t.onFrame(640, 400, kMono);
    check(t.onFrame(640, 400, kMono) == (AvTracker::Geometry | AvTracker::Timing), "mono announced");
    check(t.fps() > 71.0 && t.fps() < 71.5, "mono cadence about 71.2 Hz");
    std::printf("[av-tracker] %s\n", failures ? "FAIL" : "ok");
    return failures;
}
