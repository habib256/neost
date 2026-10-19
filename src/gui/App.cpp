// =============================================================================
//  App.cpp — l'instance unique de l'état du frontend (cf. App.hpp).
//
//  Statique de FONCTION et non objet global : sa construction est alors ordonnée
//  par le premier appel, et non par l'ordre d'initialisation entre unités de
//  compilation — qui n'est pas spécifié. App contient des std::string, des
//  std::vector et une CrtEffectStack : un ordre subi finirait par se voir.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/App.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <GLFW/glfw3.h>

#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "audio/MidiInHost.hpp"
#include "audio/MidiOutHost.hpp"
#include "audio/Mt32Synth.hpp"
#include "audio/GmSynth.hpp"
#include "core/Machine.hpp"
#include "gui/KeyboardWindow.hpp"
#include "gui/StKeys.hpp"
#include "gui/StScreenView.hpp"
#include "net/NetBackend.hpp"
#include "util/HostPath.hpp"   // chemins hôte : UNE définition d'« absolu »
#include "net/SlirpBackend.hpp"
#ifdef NEOST_WITH_NET
#include "net/HayesModem.hpp"
#endif
#if defined(NEOST_WITH_IMGUI)
#include "imgui.h"
#endif

namespace fs = std::filesystem;
using namespace neost::appconfig;

// Les périphériques hôtes sont des types INCOMPLETS dans App.hpp (unique_ptr) :
// leurs destructeurs doivent être instanciés ICI, où ils sont complets.
App::App() = default;
App::~App() = default;

App& app() {
    static App instance;
    return instance;
}

// Retire du ST ce que l'AUTO-PLUG avait posé, et rien d'autre (cf. App::autoPortDev
// et App::autoCartKey, où le pourquoi est écrit).
void autoDongleRetract(App& A, Machine& machine, Config& cfg) {
    std::string* slots[] = { &cfg.joy0, &cfg.joy1, &cfg.rs232, &cfg.printer, &cfg.cartbutton };
    for (int p = 0; p < int(PortDevices::Port::Count); ++p) {
        const PortDevices::Device d = A.autoPortDev[p];
        if (d == PortDevices::Device::None) continue;
        A.autoPortDev[p] = PortDevices::Device::None;
        if (machine.ports.at(PortDevices::Port(p)) != d) continue;   // l'utilisateur l'a changé
        machine.plugPort(PortDevices::Port(p), PortDevices::Device::None);
        slots[p]->clear();
    }
    if (A.autoCartKey != CartridgeKey::Model::None) {
        const CartridgeKey::Model k = A.autoCartKey;
        A.autoCartKey = CartridgeKey::Model::None;
        if (machine.dongle.model() == k) { machine.setDongle(CartridgeKey::Model::None); cfg.dongle.clear(); }
    }
}

// Résout un chemin de données indépendamment du répertoire courant : tel quel,
// puis relatif au répertoire de l'exécutable (utile quand on lance depuis build/).
// std::filesystem et non stat() : <sys/stat.h> n'existe pas partout, et la
// surcharge à error_code ne LANCE jamais (un chemin illisible = « absent »).
bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !ec;
}
// Un chemin ABSOLU ne se combine avec rien : le préfixer d'un dossier de base est
// exactement ce qui produisait « C:\…\NeoST\C:\Temp\atari » sous Windows (issue #37).
// La règle « absolu » vit dans util/HostPath, seule et testée (tests/selftest_logic.cpp).
std::string resolveData(const std::string& given, const std::string& exeDir) {
    if (neost::hostpath::isAbsolute(given)) return given;
    const std::string cands[] = { given,
                                  neost::hostpath::join(exeDir, given),
                                  neost::hostpath::join(exeDir + "/..", given),
                                  neost::hostpath::join("..", given) };
    for (const auto& c : cands) if (fileExists(c)) return c;
    return given;
}

// Choisit un TOS compatible avec la MACHINE sélectionnée si le ROM courant ne
// convient pas — pour que « choisir Mega STE » donne un VRAI Mega STE. Seul cas
// géré : le Mega STE exige TOS ≥ 2.0x (lui seul programme cache 16 Ko / SCU / 16 MHz).
// Cherche dans roms/ un tos206<pays> (pays du ROM courant), sinon tos206 / tos206us,
// sinon etos256<pays> / etos256us. Renvoie le chemin LOGIQUE (« roms/tos206fr.img »),
// ou "" si le ROM courant convient déjà (ou aucun candidat trouvé).
std::string pickTosForMachine(const std::string& machine,
                                     const std::string& curRomLogical,
                                     const std::string& exeDir,
                                     const std::string& romsDir) {
    if (machine != "megaste") return "";
    { std::ifstream f(resolveData(curRomLogical, exeDir), std::ios::binary);
      if (f) { uint8_t b[2] = {0, 0}; f.seekg(2); f.read(reinterpret_cast<char*>(b), 2);
               if (((b[0] << 8) | b[1]) >= 0x0200) return ""; } }   // déjà compatible
    std::string cc = fs::path(curRomLogical).stem().string();       // ex. "tos162fr"
    cc = (cc.size() >= 2) ? cc.substr(cc.size() - 2) : std::string();
    if (cc.size() != 2 || !std::isalpha((unsigned char)cc[0]) || !std::isalpha((unsigned char)cc[1]))
        cc.clear();
    auto has = [&](const std::string& n) { std::error_code ec; return fs::exists(fs::path(romsDir) / n, ec); };
    for (const std::string& cand : { "tos206" + cc + ".img", std::string("tos206.img"),
                                     std::string("tos206us.img"), "etos256" + cc + ".img",
                                     std::string("etos256us.img") })
        if (has(cand)) return "roms/" + cand;
    return "";
}

void loadRtcFromConfig(Machine& m, const Config& c) {
    Rtc::DateTime dt;
    if (!c.rtc.empty() && parseRtcConfig(c.rtc, dt)) {
        m.rtc.setDateTime(dt);
        if (c.rtcSaved > 0) {
            const std::time_t now = std::time(nullptr);
            if (now > c.rtcSaved) m.rtc.advanceSeconds(now - c.rtcSaved);
        }
    }
}
void snapshotRtc(Machine& m, Config& c) {
    const Rtc::DateTime dt = m.rtc.getDateTime();
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d,%d",
                  dt.sec, dt.min, dt.hour, dt.wday, dt.day, dt.month, dt.year);
    c.rtc = buf;
    c.rtcSaved = std::time(nullptr);
}
// force=true : écrit la config MÊME en kiosk (normalement figé). Utilisé pour le seul
// réglage que la borne a le droit de persister : le dossier ROM additionnel choisi via
// le menu in-game (le reste de la config kiosk reste identique à ce qui a été chargé).
void saveConfig(App& A, const std::string& exeDir, Config& c, Machine* machine, bool force) {
    if (A.harnessRun) return;                             // harnais : zéro écriture, quel que soit l'appelant
    if ((A.kiosk || A.kioskLaunched) && !force) return;   // kiosk : configuration figée — la borne repart toujours identique
    if (machine) snapshotRtc(*machine, c);
    // MODE BORNE : la configuration est FIGÉE, et `force` ne lève ce gel que pour deux
    // réglages d'exploitation — les dossiers ROM et l'affectation des manettes. Mais
    // `force` réécrivait TOUT le fichier depuis la structure en mémoire, or celle-ci a
    // été salie entre-temps (F10 pose cfg.autoZoom sans passer par ici, et un aller-retour
    // par le bureau rend tous les menus atteignables). La borne repartait donc avec les
    // réglages du dernier visiteur — exactement l'invariant que le gel doit garantir.
    // On repart donc de l'image PRISTINE lue au démarrage, en n'y reportant que les deux
    // champs autorisés. Le déclencheur n'a même pas besoin d'être volontaire : un dossier
    // ROM disparu suffit (auto-purge → saveConfig(A, force=true)).
    const Config* src = &c;
    Config kioskOut;
    if ((A.kiosk || A.kioskLaunched) && force) {
        kioskOut         = A.cfgPristine;
        kioskOut.romDirs = c.romDirs;
        kioskOut.joymap  = c.joymap;
        kioskOut.rtc     = c.rtc;          // horloge : état machine, pas un réglage d'expo
        kioskOut.rtcSaved = c.rtcSaved;
        src = &kioskOut;
    }
    const Config& w = *src;
    std::string err;
    if (!writeConfigAtomic(cfgPath(exeDir), w, /*full=*/true, /*cwdFallback=*/true, err))
        std::fprintf(stderr, "[cfg] %s — configuration NOT saved\n", err.c_str());
}

// Table disks/dongles.txt : créée avec les titres connus si absente (jamais en
// borne : config figée), relue à chaque montage (on peut l'éditer à chaud).
std::vector<neost::DongleRule> App::loadDongleTable() {
    App& A = *this;
    const std::string& disksDir = A.disksDir;
    const std::string path = disksDir + "/dongles.txt";
    std::string text;
    if (std::ifstream in(path); in) text.assign(std::istreambuf_iterator<char>(in), {});
    else {
        text = neost::defaultDongleTable();
        if (!A.kiosk) if (std::ofstream out(path); out) out << text;
    }
    return neost::parseDongleTable(text);
}

// Résolution de chemin façon resolveData mais tolérante aux DOSSIERS (le HD
// GEMDOS monte un répertoire ; fs::exists accepte fichiers et dossiers).
std::string App::resolvePath(const std::string& given) {
    App& A = *this;
    const std::string& exeDir = A.exeDir;
    // Absolu = déjà résolu (cf. resolveData et util/HostPath) : c'est le cas du
    // dossier glissé-déposé, qui arrive TOUJOURS en absolu depuis GLFW.
    if (neost::hostpath::isAbsolute(given)) return given;
    const std::string cands[] = { given,
                                  neost::hostpath::join(exeDir, given),
                                  neost::hostpath::join(exeDir + "/..", given),
                                  neost::hostpath::join("..", given) };
    std::error_code ec;
    for (const auto& c : cands) if (fs::exists(c, ec)) return c;
    return given;
}

// UltraSatan (ultrasatan=/sd2= dans neost.cfg) : interface SD sur les IDs 0-1 —
// slot 1 = l'image acsi= (ID 0), slot 2 = sd2= (ID 1). À rejouer après TOUT
// montage/démontage ACSI : unmountAcsi() vide toutes les cibles, slot 2 compris.
void App::usatanApply() {
    App& A = *this;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    if (!cfg.ultrasatan) {
        if (machine.ultraSatanEnabled()) machine.disableUltraSatan();
        return;
    }
    if (!machine.ultraSatanEnabled()) machine.enableUltraSatan(0);
    if (!cfg.sd2.empty()) {
        const std::string want = A.resolvePath(cfg.sd2);
        if (machine.fdc.acsiMountedPath(1) != want && !machine.fdc.mountAcsi(want, 1))
            cfg.sd2.clear();               // image invalide → on ne la mémorise pas
    }
}

#ifdef NEOST_WITH_NET
void App::modemApply(bool on) {
    App& A = *this;
    Machine& machine = *A.machine;
    auto& hayesModem = A.hayesModem;
    if (on && !hayesModem) {
        hayesModem = std::make_unique<HayesModem>(machine.mfp);
        HayesModem* m = hayesModem.get();
        machine.mfp.setSerialSink([m](uint8_t b) { m->onTx(b); });
    } else if (!on && hayesModem) {
        machine.mfp.setSerialSink({});
        hayesModem.reset();
    }
}
#endif

NetBackend* App::neBackend() {
    App& A = *this;
    SlirpBackend& slirpNet = *A.slirpNet;
    NetBackendNull& etherNull = *A.etherNull;
    return slirpNet.isOpen() ? static_cast<NetBackend*>(&slirpNet) : &etherNull;
}

void App::slirpApply(bool on) {
    App& A = *this;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    SlirpBackend& slirpNet = *A.slirpNet;
    if (on && !slirpNet.isOpen()) {
        if (!SlirpBackend::available()) {
            A.stateMsg = "This build has no libslirp (NEOST_WITH_SLIRP)";
            A.stateMsgFrames = 150;
            cfg.slirp = false;
        } else if (!slirpNet.open(false)) {
            A.stateMsg = "slirp: " + slirpNet.lastError();
            A.stateMsgFrames = 150;
            cfg.slirp = false;
        }
    } else if (!on) {
        slirpNet.close();
    }
    machine.ne2000.setBackend(A.neBackend());
}

// NetUSBee (netusbee=) = la même NE2000 + l'hôte USB ISP1160 ; exclusif d'EtherNEC
// (un seul montage sur le port cartouche) — netusbee= prime sur ethernec=.
void App::etherApply(bool on) {
    App& A = *this;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    if (on) {
        if (!machine.bus.cart.empty()) {
            A.stateMsg = "EtherNEC needs the cartridge port free";
            A.stateMsgFrames = 150;
            cfg.ethernec = false;
            return;
        }
        if (machine.netUsbeeEnabled()) return;   // la NE2000 est déjà là (NetUSBee)
        // Même pré-test que pour la cartouche : la clé Steinberg est exclusive de
        // l'EtherNEC (enableEtherNec la refuse). Sans ce message, la case restait
        // cochée alors que rien ne s'activait.
        if (machine.dongle.attached()) {
            A.stateMsg = "EtherNEC needs the cartridge port free (a Steinberg key is plugged)";
            A.stateMsgFrames = 150;
            cfg.ethernec = false;
            return;
        }
        machine.ne2000.setBackend(A.neBackend());
        machine.enableEtherNec();
    } else if (!machine.netUsbeeEnabled()) {
        machine.disableEtherNec();
    }
}

void App::netUsbeeApply(bool on) {
    App& A = *this;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    if (on) {
        if (!machine.bus.cart.empty()) {
            A.stateMsg = "NetUSBee needs the cartridge port free";
            A.stateMsgFrames = 150;
            cfg.netusbee = false;
            return;
        }
        if (machine.netUsbeeEnabled()) return;
        if (machine.dongle.attached()) {           // cf. etherApply : exclusivité clé/réseau
            A.stateMsg = "NetUSBee needs the cartridge port free (a Steinberg key is plugged)";
            A.stateMsgFrames = 150;
            cfg.netusbee = false;
            return;
        }
        machine.disableEtherNec();                 // la NE2000 repart avec le NetUSBee
        machine.ne2000.setBackend(A.neBackend());
        machine.enableNetUsbee();
    } else if (machine.netUsbeeEnabled()) {
        machine.disableNetUsbee();
        if (cfg.ethernec) A.etherApply(true);        // EtherNEC seul reprend la NE2000
    }
}

void App::midiOutApply() {
    App& A = *this;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    MidiOutHost& midiOut = *A.midiOut;
    Mt32Synth& mt32 = *A.mt32;
    GmSynth& gm = *A.gm;
    uint32_t& audioRate = A.audioRate;
    // Synthé GM intégré : DLSMusicDevice quand l'OS en a un (macOS), sinon le
    // TinySoundFont vendorisé, mixé dans la sortie comme le MT-32 (cf. GmSynth.hpp).
    if (cfg.midiOutGm) {
        if (MidiOutHost::synthAvailable()) { if (!midiOut.openSynth()) cfg.midiOutGm = false; }
        else if (!gm.isOpen() && !gm.open(resolveData(cfg.gmSoundFont, exeDir), audioRate)) {
            A.stateMsg = "GM synth: " + gm.lastError(); A.stateMsgFrames = 300;
            std::fprintf(stderr, "[gm] %s\n", gm.lastError().c_str());
            cfg.midiOutGm = false;
        }
    } else { midiOut.closeSynth(); gm.close(); }
    if (cfg.midiOutPort) {
        if (!midiOut.openVirtualPort()) {
            cfg.midiOutPort = false;
            A.stateMsg = "MIDI OUT: cannot create the CoreMIDI port (see console)";
            A.stateMsgFrames = 300;
        }
    } else midiOut.closeVirtualPort();
    // Destinations MATÉRIELLES (aiguillage par canal). L'échec est SILENCIEUX à
    // dessein : un appareil débranché n'est pas une erreur de configuration, et
    // effacer son nom ferait perdre le réglage au premier câble USB retiré. On le
    // garde, la boucle re-tente, la page affiche « (not connected) ».
    {
        std::vector<MidiOutHost::Dest> want;
        want.reserve(cfg.midiOutDevices.size());
        for (const auto& d : cfg.midiOutDevices) want.push_back({d.name, d.channels, d.uid});
        midiOut.setDestinations(want);
    }
    if (cfg.midiOutMt32) {
        if (!mt32.isOpen() && !mt32.open(resolveData(cfg.mt32Roms, exeDir), audioRate, cfg.mt32Model)) {
            A.stateMsg = "MT-32: " + mt32.lastError(); A.stateMsgFrames = 300;
            std::fprintf(stderr, "[mt32] %s\n", mt32.lastError().c_str());
            cfg.midiOutMt32 = false;
        }
    } else mt32.close();
    if (midiOut.anyOpen() || mt32.isOpen() || gm.isOpen())
        machine.midi.setMidiSinkTimed([&midiOut, &mt32, &gm](uint8_t b, int64_t c) {
            if (midiOut.anyOpen()) midiOut.byteAt(b, c);
            mt32.byteAt(b, c);
            gm.byteAt(b, c);
        });
    else machine.midi.setMidiSinkTimed({});
}

// Entrée matérielle : même politique que la destination — on garde le nom, on
// re-tente, on ne dit rien tant que l'appareil n'est pas là.
void App::midiInApply() {
    App& A = *this;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    MidiInHost& midiIn = *A.midiIn;
    std::vector<MidiInHost::Want> want;
    want.reserve(cfg.midiInDevices.size());
    for (const auto& d : cfg.midiInDevices) want.push_back({d.name, d.channel, d.uid});
    midiIn.setDevices(want);
    // L'ACIA TIRE les octets sur son horloge série (2560 cycles/octet), au lieu
    // d'une rafale par trame qui plafonnait l'entrée à ~143 o/s. Cf. MidiAcia::
    // setRxSource. La lambda survit à un débranchement : tryPop rend false.
    if (midiIn.isOpen()) machine.midi.setRxSource([&midiIn](uint8_t& b) { return midiIn.tryPop(b); });
    else                 machine.midi.setRxSource({});
}

// APPRENTISSAGE des identifiants. Une config qui ne connaît qu'un nom (celle
// d'avant les identifiants, ou une ligne écrite à la main) ne deviendrait jamais
// sûre toute seule : on note l'identifiant du point réellement ouvert. Deux
// appareils du même modèle deviennent ainsi stables dès le premier lancement où
// ils sont tous les deux branchés.
bool App::midiLearnUids() {
    App& A = *this;
    Config& cfg = A.cfg;
    // Toute la logique vit dans neost::midi::learnUids (fonction PURE, testée) —
    // notamment la règle « jamais deux fois le même identifiant », sans laquelle
    // deux homonymes branchés apprenaient tous deux l'identifiant du premier
    // (bug corrigé le 2026-08-30, cf. MidiEndpoint.hpp). Ici : conversion
    // config ↔ Wanted, et le journal de ce qui a été appris.
    const auto learn = [](auto& cfgList, const std::vector<neost::midi::Endpoint>& open,
                          const char* tag) {
        std::vector<neost::midi::Wanted> want;
        want.reserve(cfgList.size());
        for (const auto& d : cfgList) want.push_back({d.name, d.uid});
        if (!neost::midi::learnUids(want, open)) return false;
        for (std::size_t i = 0; i < cfgList.size(); ++i)
            if (cfgList[i].uid.empty() && !want[i].uid.empty()) {
                cfgList[i].uid = want[i].uid;
                std::fprintf(stderr, "[%s] learned unique id %s for \"%s\"\n",
                             tag, want[i].uid.c_str(), want[i].name.c_str());
            }
        return true;
    };
    std::vector<neost::midi::Endpoint> outOpen;
    for (const auto& d : A.midiOut->openDestinations()) outOpen.push_back({d.name, d.uid});
    const bool out = learn(cfg.midiOutDevices, outOpen,                   "midi-out");
    const bool in  = learn(cfg.midiInDevices,  A.midiIn->openEndpoints(), "midi-in");
    return out || in;
}

// Applique la config courante (modèle / RAM / cœur / ROM) À CHAUD : reconfigure
// la Machine en place (son adresse ne change pas → les références d'Audio vers
// psg/dmasnd restent valides), recharge la ROM, repose le moniteur, puis reset.
// C'est un hard reset avec les nouveaux paramètres — aucun redémarrage de l'appli.
// Le disque monté est conservé.
void App::applyConfig() {
    App& A = *this;
    A.desktopPointer.reset();
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    const std::string romP = resolveData(cfg.rom, exeDir);
    // Abaisse la machine si le TOS ne la supporte pas (TOS <= 1.04 → ST), comme Hatari.
    const MachineType machTypeR = Machine::adjustMachineForTos(parseMachine(cfg.machine), romP);
    machine.reconfigure(parseRamBytes(cfg.mem), Cpu68k::parseCore(cfg.cpu), machTypeR);
    if (!machine.loadTos(romP))
        // ROM absente/illisible (profil pointant un TOS non installé) : l'ANCIENNE
        // ROM reste chargée — on le dit au lieu de laisser croire au nouveau profil.
        std::fprintf(stderr, "[main] WARNING ROM not found: %s — the previous ROM stays active\n",
                     romP.c_str());
    neost::stkeys::setCountryFromTos(machine.bus.rom);   // la nouvelle ROM peut changer de pays clavier
    if (cfg.cart.empty()) machine.ejectCart();
    else                  machine.loadCart(resolveData(cfg.cart, exeDir));
    // L'eject/loadCart ci-dessus a écrasé la cartouche système du HD GEMDOS →
    // réaligne les montages disque dur sur la config : réinstalle le HD GEMDOS
    // s'il est configuré (exclusif avec cfg.cart), le démonte sinon — un profil
    // peut monter OU démonter, et reconfigure() ne touche pas à ces montages.
    // Idem ACSI (le montage survit à reconfigure, on ne remonte que si absent).
    if (cfg.gemdos.empty()) machine.gemdos.unmount();
    else                    machine.gemdos.setDirectory(A.resolvePath(cfg.gemdos));
    if (cfg.acsi.empty())   machine.fdc.unmountAcsi();
    else {
        // Remonter aussi quand la config désigne une AUTRE image : le seul test
        // « déjà actif » laissait l'ancienne image montée alors que la barre de
        // statut et neost.cfg affichaient la nouvelle (profil chargé).
        const std::string want = A.resolvePath(cfg.acsi);
        if (!machine.fdc.acsiActive() || machine.fdc.acsiMountedPath() != want)
            machine.fdc.mountAcsi(want);
    }
    A.usatanApply();                         // slot 2 + attache (unmountAcsi a tout vidé)
    machine.mfp.setColorMonitor(!cfg.mono);
    machine.fdc.setFastFdc(cfg.fastfdc);   // ré-applique le FDC rapide après reconfig
    machine.bus.setFpuPresent(cfg.fpu && machTypeR == MachineType::MegaSte);
    machine.reset();
    std::fprintf(stderr, "[main] live reconfigure: core %s | machine %s | RAM %s\n",
                 Cpu68k::coreName(machine.cpu.core()),
                 machineName(machTypeR), cfg.mem.c_str());
}

// ─── Bascule GUI ⇄ kiosk à chaud ────────────────────────────────────────
// Le kiosk n'est plus seulement un drapeau de lancement : Ctrl+Alt+F (ou le menu
// « Machine », ou l'action DESKTOP MODE du menu borne) fait l'aller-retour sans
// relancer l'application. La MACHINE traverse la bascule intacte :
// 1. instantané mémoire (Machine::saveState) AVANT de toucher à quoi que ce soit ;
// 2. la fenêtre GLFW change de moniteur (plein écran exclusif ⇄ fenêtré) — le
// contexte GL, ses textures et la passe CRT survivent, on ne recrée rien ;
// 3. l'instantané est rechargé derrière (Machine::loadState).
// L'étape 3 est la ceinture de sécurité de l'étape 2 : le ST repart exactement
// dans l'état quitté (jeu en cours compris) quoi que la bascule remue côté hôte
// — reconfiguration d'entrée, recalage de cadence, anneau audio.
// ⚠ À n'appeler qu'entre deux runFrame (loadState refuse le milieu de trame).
// kbdJoyDesk : réglage « joystick au clavier » à rendre au bureau. Initialisé à
// false et NON à A.kbdJoy — à ce point A.kbdJoy vaut A.kiosk, donc lancé en borne
// on capturerait true et la sortie vers le bureau avalerait les flèches + Ctrl
// droit du ST sans rien afficher. La branche GUI→KIOSK le rafraîchit ensuite.
void App::toggleFullscreen() {
    if (kiosk) { switchKioskMode(false); return; }
    if (!fullscreen) {
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        glfwGetWindowPos(window, &winX, &winY);
        glfwGetWindowSize(window, &winW, &winH);
        // Choisir le moniteur qui contient le centre de la fenêtre.
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            int x, y; glfwGetMonitorPos(monitors[i], &x, &y);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (mode && winX + winW / 2 >= x && winX + winW / 2 < x + mode->width
                     && winY + winH / 2 >= y && winY + winH / 2 < y + mode->height) {
                mon = monitors[i]; break;
            }
        }
        const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
        if (!mode) return;
        winGeomValid = true;
        glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_FALSE);
        glfwSetWindowMonitor(window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
        fullscreen = true;
    } else {
        glfwSetWindowMonitor(window, nullptr, winX, winY, winW, winH, GLFW_DONT_CARE);
        glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_TRUE);
        fullscreen = false;
    }
    mouseCaptured = fullscreen;
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, fullscreen ? GLFW_TRUE : GLFW_FALSE);
    glfwSetInputMode(window, GLFW_CURSOR, fullscreen ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (ikbd) ikbd->mouseEvent(0, 0, false, false);
    desktopPointer.reset();
    mouseAccX = mouseAccY = 0;
    glfwGetCursorPos(window, &lastMx, &lastMy);
    emuNext = std::chrono::steady_clock::now();
}

void App::switchKioskMode(bool on) {
    App& A = *this;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    GLFWwindow* const window = A.window;
    auto& emuNext = A.emuNext;
    double& lastMx = A.lastMx;
    double& lastMy = A.lastMy;
    bool& kbdJoyDesk = A.kbdJoyDesk;
    keyboardWindowReleaseAll(machine.ikbd);   // rien ne reste enfoncé à travers la bascule
    if (on == A.kiosk) return;
    A.desktopPointer.reset();
    machine.ikbd.mouseEvent(0, 0, false, false);
    std::vector<uint8_t> snap;
    if (!machine.saveState(snap)) return;    // (1) instantané, avant toute destruction GUI

    if (on) {
        // GUI → KIOSK. La config est GELÉE en kiosk : on persiste d'abord l'état
        // courant, sinon les préférences de la session seraient perdues.
        cfg.disk = machine.fdc.mountedPath();
        cfg.cart = machine.bus.mountedCartPath();
        cfg.mono = !machine.mfp.colorMonitor();
        cfg.showHex  = A.showHex;
        cfg.showCpu  = A.showCpu;  cfg.showJoy  = A.showJoy;  cfg.dock = A.dockOn;
        cfg.showKbd  = A.showKbd;
        cfg.showCfg  = A.showCfg;  cfg.showFloppy = A.showFloppy;
        saveConfig(A, exeDir, cfg, &machine);
        // ⚠ La référence PRISTINE doit devenir CE qui vient d'être persisté, pas la
        // config lue au démarrage. Sinon le gel kiosk se retourne contre la ligne
        // ci-dessus : plus tard, n'importe quel saveConfig(A, force=true) de la borne
        // (ajout/retrait d'un dossier ROM, réaffectation manette, ou simple
        // auto-purge d'un dossier ROM disparu) reconstruit le fichier depuis
        // A.cfgPristine et RÉÉCRIT par-dessus machine/mem/rom/disk/crt/dock/show*
        // avec les valeurs du lancement — les préférences de la séance, que ce
        // saveConfig venait d'enregistrer, sont perdues sans le moindre message.
        // ⚠ MAIS seulement si le saveConfig ci-dessus a réellement écrit : lancé
        // en --kiosk (A.kioskLaunched), il est un no-op — remplacer quand même la
        // référence pristine par la cfg salie pendant l'excursion bureau (Ctrl+Alt+F)
        // ferait persister la machine du visiteur au premier save forcé
        // (auto-purge ROM, réaffectation manette) : l'exact contraire du gel.
        if (!A.kioskLaunched) A.cfgPristine = cfg;
#if defined(NEOST_WITH_IMGUI)
        // Disposition des fenêtres écrite MAINTENANT : en kiosk plus aucune n'est
        // soumise, une sauvegarde automatique plus tard n'aurait rien à dire d'elles.
        if (ImGui::GetIO().IniFilename)
            ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
#endif
        glfwGetWindowPos(window, &A.winX, &A.winY);
        glfwGetWindowSize(window, &A.winW, &A.winH);
        A.winGeomValid = true;
        A.kiosk = true;                      // AVANT saveConfig suivant : config figée

        // (2) Plein écran EXCLUSIF sur le moniteur choisi (--kiosk-monitor).
        // Même borne que la création : un index hors plage lirait mons[-1].
        int nmon = 0; GLFWmonitor** mons = glfwGetMonitors(&nmon);
        GLFWmonitor* mon = (mons && A.kioskMonitor >= 0 && A.kioskMonitor < nmon)
                               ? mons[A.kioskMonitor] : glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
        glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_FALSE);
        glfwSetWindowMonitor(window, mon, 0, 0,
                             vm ? vm->width : A.winW, vm ? vm->height : A.winH,
                             vm ? vm->refreshRate : GLFW_DONT_CARE);
        // Borne : curseur masqué, souris capturée, joystick clavier armé (une
        // borne se joue au joystick — flèches + Ctrl droit, sans menu pour l'activer).
        A.mouseCaptured = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        kbdJoyDesk = A.kbdJoy;               // à rendre en revenant au bureau
        A.kbdJoy   = true;
    } else {
        // KIOSK → GUI. Retour à la géométrie fenêtrée mémorisée + chrome rendu.
        A.kiosk = false;
        A.kioskDiskMenu = false;             // aucun overlay borne ne survit à la bascule
        A.kioskPage = KIOSK_PAGE_LIST; A.kioskZone = KIOSK_ZONE_LIST;
        // Jamais été fenêtré (session lancée en --kiosk) : centrer sur la zone de
        // travail du moniteur COURANT plutôt que d'atterrir en (0,0). Le moniteur
        // se lit AVANT glfwSetWindowMonitor(…, nullptr, …), qui le remet à null.
        if (!A.winGeomValid) {
            GLFWmonitor* m = glfwGetWindowMonitor(window);
            if (!m) m = glfwGetPrimaryMonitor();
            int mx = 0, my = 0, mw = 0, mh = 0;
            if (m) glfwGetMonitorWorkarea(m, &mx, &my, &mw, &mh);
            if (mw > 0 && mh > 0) {
                A.winX = mx + (mw - A.winW) / 2;
                A.winY = my + (mh - A.winH) / 2;
            }
            A.winGeomValid = true;
        }
        glfwSetWindowMonitor(window, nullptr, A.winX, A.winY, A.winW, A.winH,
                             GLFW_DONT_CARE);
        glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_TRUE);
        A.mouseCaptured = false;             // le curseur hôte revient (clic écran = recapture)
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        // Émulation joystick clavier : on REND le réglage d'avant la borne (par
        // défaut OFF au bureau — sinon les flèches n'atteindraient plus le ST).
        A.kbdJoy = kbdJoyDesk;
    }

    // (3) Restaure l'instantané : le ST reprend EXACTEMENT où il en était.
    if (!snap.empty() && !machine.loadState(snap.data(), snap.size()))
        std::fprintf(stderr, "[kiosk] WARNING state restore failed — "
                             "the machine carries on as is\n");
    // Recale l'horloge et le delta souris : la bascule a pris du temps réel et
    // déplacé le curseur (changement de mode). Sans ça : rafale de rattrapage
    // de trames + saut de souris d'un demi-écran à la reprise.
    emuNext = std::chrono::steady_clock::now();
    glfwGetCursorPos(window, &lastMx, &lastMy);
    A.stateMsg = A.kiosk ? "\xef\x84\x88 Kiosk mode (Alt+Enter to go back)"
                         : "\xef\x84\x88 Desktop mode (Alt+Enter for full screen)";
    A.stateMsgFrames = 120;
    std::fprintf(stderr, "[kiosk] switched → %s\n", A.kiosk ? "KIOSK" : "DESKTOP");
}
