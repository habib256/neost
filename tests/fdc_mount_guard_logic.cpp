// Gardes de montage disquette et ACSI (lot « disquette et ACSI », 2026-11-12).
//  · .dim à en-tête lâche : en-tête retiré, lecture seule ([D] Hatari floppies/dim.c:75-90
//    @ f0736b24 ne teste que la signature) ;
//  · une image, un lecteur : refus dans Fdc::loadImage ([D] Hatari floppy.c:403-408 @ f0736b24) ;
//  · WRITE SECTOR / WRITE TRACK sans disquette : WRITE PROTECT ([B] WD177X-00 p. 1-9, 1-14 ;
//    lecteur vide = protégé, [D] Hatari fdc.c:4875-4880 @ f0736b24) au lieu de BUSY sans fin ;
//  · STX dont le fichier est en lecture seule : protégée ([D] Hatari floppy.c:205-225 @ f0736b24) ;
//  · save-state pris avec une image ACSI : refusé sans elle ou avec une autre (en-tête v64).
// Chaque cas échoue sans son correctif.
#include "core/Bus.hpp"
#include "core/Machine.hpp"
#include "core/Scheduler.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[fdc-mount-guard] FAIL %s: got %lld, expected %lld\n",
                     label, static_cast<long long>(got), static_cast<long long>(expected));
    }
};

struct Rig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    Rig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(false);
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
        mfp.write8(0xFFFA09, 0x80);
    }
    void select(uint8_t value) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, value); }
    void mode(uint16_t value) { fdc.write8(0xFF8606, uint8_t(value >> 8)); fdc.write8(0xFF8607, uint8_t(value)); }
    void data(uint8_t value) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, value); }
    uint8_t reg(uint16_t a) { mode(0x80 | a); return fdc.read8(0xFF8605); }
    void reg(uint16_t a, uint8_t v) { mode(0x80 | a); data(v); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    // Avance événement par événement, VBL comprises (transitions de média), dans un budget
    // de temps émulé : une commande qui ne finit jamais rend false au lieu de boucler.
    bool finish(int64_t budget = 8'000'000LL * 10) {
        const int64_t end = live + budget;
        for (unsigned n = 0; n < 2000000 && !irq() && live < end; ++n) {
            const int64_t due = sched.nextDue();
            if (due < 0) return false;
            for (int64_t b = (live / 160000 + 1) * 160000; b <= due; b += 160000) {
                live = b; sched.runTo(b); fdc.onVbl();
            }
            live = due; sched.runTo(due);
        }
        return irq();
    }
    void dma(bool write, uint8_t count, uint32_t addr = 0x20000) {
        mode(write ? 0x190 : 0x90); mode(write ? 0x090 : 0x190); mode(write ? 0x190 : 0x90);
        data(count);
        fdc.write8(0xFF860D, uint8_t(addr)); fdc.write8(0xFF860B, uint8_t(addr >> 8));
        fdc.write8(0xFF8609, uint8_t(addr >> 16));
    }
    // Commande + attente ; rend le registre d'état (ou 0xFF si pas d'INTRQ).
    uint8_t cmd(bool write, uint8_t c) {
        mode(write ? 0x180 : 0x80); data(c);
        if (!finish()) return 0xFF;
        return reg(0);
    }
};

void writeFile(const fs::path& p, const std::vector<uint8_t>& v) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size()));
}
std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// 720 Ko, BPB 9 spt × 2 faces, chaque secteur rempli de son numéro logique.
std::vector<uint8_t> stImage() {
    std::vector<uint8_t> v(737280);
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = uint8_t(i / 512);
    v[0] = 0x60; v[1] = 0x1C;
    v[0x0B] = 0x00; v[0x0C] = 0x02; v[0x13] = 0xA0; v[0x14] = 0x05;
    v[0x18] = 9; v[0x1A] = 2;
    return v;
}

// STX minimale : une piste (0, face 0), un secteur de 512 o (cf. stx_sector_rewrite_logic).
std::vector<uint8_t> stxImage() {
    std::vector<uint8_t> image = {'R','S','Y',0,3,0,0,0,0,0,1,0,0,0,0,0};
    auto le16 = [&](unsigned value) { image.push_back(uint8_t(value)); image.push_back(uint8_t(value >> 8)); };
    auto le32 = [&](unsigned value) { le16(value); le16(value >> 16); };
    le32(16 + 512); le32(0); le16(1); le16(0); le16(6268 * 8);
    image.push_back(0); image.push_back(0);
    image.insert(image.end(), 512, 0xA5);
    return image;
}

struct TempDir {
    fs::path path;
    TempDir() {
        for (int n = 0; n < 1000; ++n) {
            path = fs::temp_directory_path() / ("neost-fdc-mount-guard-" + std::to_string(n));
            std::error_code ec;
            if (fs::create_directory(path, ec)) return;
        }
        path.clear();
    }
    ~TempDir() {
        if (path.empty()) return;
        std::error_code ec;
        for (auto& e : fs::recursive_directory_iterator(path, ec))   // lecture seule → effaçable
            fs::permissions(e.path(), fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(path, ec);
    }
};

void dimCases(Checks& c, const fs::path& dir) {
    // En-tête « lâche » : signature d'Hatari, champs de géométrie à 0.
    std::vector<uint8_t> dim(32, 0);
    dim[0] = dim[1] = 0x42;
    const std::vector<uint8_t> st = stImage();
    dim.insert(dim.end(), st.begin(), st.end());
    const fs::path lax = dir / "lax.dim";
    writeFile(lax, dim);
    {
        Rig r;
        c.equal("dim laxe : montage", r.fdc.loadImage(lax.string()), true);
        r.select(5);
        r.reg(4, 2); r.dma(false, 1);
        const uint8_t s = r.cmd(false, 0x88);
        c.equal("dim laxe : lecture T0S2 sans erreur", s & 0x1C, 0);
        // Secteur logique 1 : tout l'octet vaut 1 — avec l'en-tête resté en place, les 32
        // premiers octets seraient la fin du secteur 0.
        c.equal("dim laxe : secteur 2 au bon offset", std::all_of(r.bus.ram.begin() + 0x20000,
                r.bus.ram.begin() + 0x20200, [](uint8_t b) { return b == 1; }), true);
        r.reg(0, 0xD0);
        c.equal("dim laxe : lecture seule (WPRT)", (r.reg(0) >> 6) & 1, 1);
    }
    // Même contenu sous une extension .st : pas d'en-tête retiré (un secteur de boot qui
    // commence par $4242 n'est pas un .dim), monté brut et lecture seule comme avant.
    const fs::path asSt = dir / "lax_as.st";
    writeFile(asSt, dim);
    {
        Rig r;
        c.equal("faux dim .st : montage", r.fdc.loadImage(asSt.string()), true);
        r.select(5);
        r.reg(4, 1); r.dma(false, 1);
        r.cmd(false, 0x88);
        c.equal("faux dim .st : en-tête non retiré", r.bus.ram[0x20000], 0x42);
    }
}

void sameFileCases(Checks& c, const fs::path& dir) {
    const fs::path a = dir / "same.st", b = dir / "other.st";
    writeFile(a, stImage());
    writeFile(b, stImage());
    Rig r;
    c.equal("même image : A", r.fdc.loadImage(a.string(), 0), true);
    c.equal("même image : refusée en B", r.fdc.loadImage(a.string(), 1), false);
    c.equal("même image : B resté vide", r.fdc.inserted(1), false);
    const std::string spelled = (dir / "." / "same.st").string();
    c.equal("même image, autre graphie : refusée en B", r.fdc.loadImage(spelled, 1), false);
    c.equal("autre image en B", r.fdc.loadImage(b.string(), 1), true);
    c.equal("réinsertion dans le même lecteur", r.fdc.loadImage(a.string(), 0), true);
    c.equal("A en B quand B l'a : refusée", r.fdc.loadImage(b.string(), 0), false);
    c.equal("A intact après refus", Fdc::sameHostFile(r.fdc.mountedPath(0), a.string()), true);
    r.fdc.eject(0);
    c.equal("après éjection de A, B peut prendre l'image", r.fdc.loadImage(a.string(), 0), true);

    // STX : « jeu.stx » et « jeu.ipf » partageraient « jeu.wd1772 ».
    const fs::path stx = dir / "game.stx", ipfName = dir / "game.ipf";
    writeFile(stx, stxImage());
    writeFile(ipfName, stxImage());   // contenu STX (« RSY ») : se monte comme tel
    Rig s;
    c.equal("STX en A", s.fdc.loadImage(stx.string(), 0), true);
    c.equal("même compagnon .wd1772 : refusé en B", s.fdc.loadImage(ipfName.string(), 1), false);
}

void noDiskCases(Checks& c) {
    for (const uint8_t command : {uint8_t(0xA8), uint8_t(0xF8)}) {   // WRITE SECTOR / TRACK, h=1
        Rig r;
        r.select(5);                              // lecteur A sélectionné, aucune disquette
        r.reg(4, 1); r.dma(true, 1);
        const uint8_t s = r.cmd(true, command);
        const bool sector = command == 0xA8;
        c.equal(sector ? "WS sans disque : INTRQ" : "WT sans disque : INTRQ", s != 0xFF, true);
        c.equal(sector ? "WS sans disque : WPRT" : "WT sans disque : WPRT", (s >> 6) & 1, 1);
        c.equal(sector ? "WS sans disque : plus BUSY" : "WT sans disque : plus BUSY", s & 1, 0);
    }
}

void stxReadOnlyCase(Checks& c, const fs::path& dir) {
    const fs::path stx = dir / "ro.stx";
    writeFile(stx, stxImage());
    std::error_code ec;
    fs::permissions(stx, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove, ec);
    Rig r;
    r.fdc.setHostWriteBack(true);
    c.equal("STX lecture seule : montage", r.fdc.loadImage(stx.string()), true);
    r.select(5);
    r.reg(4, 1); r.dma(true, 1);
    const uint8_t s = r.cmd(true, 0xA8);
    c.equal("STX lecture seule : WS refusé (WPRT)", (s >> 6) & 1, 1);
    c.equal("STX lecture seule : pas de .wd1772", fs::exists(dir / "ro.wd1772"), false);
}

// Machine nue (ROM « BRA sur place »), comme machine_fdc_media_vbl_logic.
std::unique_ptr<Machine> bareMachine() {
    auto m = std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, MachineType::Ste);
    m->bus.rom.assign(0x30000, 0);
    m->bus.romBase = 0xE00000;
    const uint8_t rom[] = {0,1,0,0,0,0xE0,0,8,0x60,0xFE};
    std::copy(std::begin(rom), std::end(rom), m->bus.rom.begin());
    m->cpu.reset();
    m->stepInstruction();
    return m;
}

void acsiStateCases(Checks& c, const fs::path& dir) {
    const fs::path img = dir / "hd.img", other = dir / "hd2.img", bigger = dir / "hd3.img";
    writeFile(img, std::vector<uint8_t>(64 * 1024, 0));
    writeFile(other, std::vector<uint8_t>(64 * 1024, 0));
    std::vector<uint8_t> state;
    {
        auto m = bareMachine();
        c.equal("ACSI : montage", m->fdc.mountAcsi(img.string()), true);
        c.equal("ACSI : état pris", m->saveState(state), true);
        c.equal("ACSI : même config acceptée", m->stateHeaderOk(state.data(), state.size()), true);
    }
    {
        auto m = bareMachine();
        c.equal("ACSI : refus sans image", m->stateHeaderOk(state.data(), state.size()), false);
        c.equal("ACSI : motif sans image",
                m->lastStateError().find("ACSI") != std::string::npos, true);
    }
    {
        auto m = bareMachine();
        m->fdc.mountAcsi(other.string());
        c.equal("ACSI : refus autre image", m->stateHeaderOk(state.data(), state.size()), false);
        c.equal("ACSI : motif autre image",
                m->lastStateError().find("ACSI") != std::string::npos, true);
    }
    {
        auto m = bareMachine();
        m->fdc.mountAcsi(img.string(), 1);
        c.equal("ACSI : refus autre cible", m->stateHeaderOk(state.data(), state.size()), false);
    }
    {
        // Même chemin, taille changée depuis l'état : autre géométrie pour le TOS.
        writeFile(bigger, std::vector<uint8_t>(64 * 1024, 0));
        auto m0 = bareMachine();
        m0->fdc.mountAcsi(bigger.string());
        std::vector<uint8_t> s0;
        m0->saveState(s0);
        writeFile(bigger, std::vector<uint8_t>(128 * 1024, 0));
        auto m = bareMachine();
        m->fdc.mountAcsi(bigger.string());
        c.equal("ACSI : refus image redimensionnée", m->stateHeaderOk(s0.data(), s0.size()), false);
    }
    {
        // État pris SANS disque dur, rechargé avec : refusé aussi (le TOS restauré n'a
        // jamais vu ses partitions).
        auto m0 = bareMachine();
        std::vector<uint8_t> s0;
        m0->saveState(s0);
        auto m = bareMachine();
        m->fdc.mountAcsi(img.string());
        c.equal("ACSI : refus image en trop", m->stateHeaderOk(s0.data(), s0.size()), false);
        c.equal("ACSI : motif image en trop",
                m->lastStateError().find("ACSI") != std::string::npos, true);
        auto same = bareMachine();
        c.equal("sans ACSI des deux côtés : accepté", same->stateHeaderOk(s0.data(), s0.size()), true);
    }
}
}  // namespace

int testFdcMountGuards() {
    Checks c;
    TempDir dir;
    if (dir.path.empty()) { std::fprintf(stderr, "[fdc-mount-guard] FAIL temp dir\n"); return 1; }
    dimCases(c, dir.path);
    sameFileCases(c, dir.path);
    noDiskCases(c);
    stxReadOnlyCase(c, dir.path);
    acsiStateCases(c, dir.path);
    std::fprintf(stderr, "[fdc-mount-guard] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
