// =============================================================================
//  Server.cpp — mode serveur du headless : une commande par ligne sur stdin,
//  une réponse par ligne sur stdout.
//
//  POURQUOI. Une boucle d'exploration d'états (planner, fuzzer, Go-Explore)
//  passe son temps à reprendre un état, jouer quelques dizaines de trames et
//  observer. En ligne de commande, chaque itération paie un lancement de
//  processus, un chargement de ROM et un save-state de 1,4 Mo qui fait
//  l'aller-retour par le disque — pour 40 ms d'émulation utile. Ici le
//  processus vit, et les états tiennent dans des emplacements EN MÉMOIRE.
//
//  CONTRAT. Une session serveur doit produire EXACTEMENT les mêmes octets
//  qu'une invocation équivalente de la boucle --frames : même ordre
//  (entrées posées AVANT la trame), même format d'observation (Observe.hpp est
//  partagé), même grammaire de script (JoyScript.hpp est partagé). C'est
//  vérifiable, et c'est vérifié : cf. docs/OPENDST.md § acceptation.
//
//  PROTOCOLE (texte, une commande par ligne ; réponse « ok … » ou « err … ») :
//    hello                    identité : version, machine, RAM, médias
//    run N                    exécute N trames, entrées inchangées
//    play SCRIPT              script joystick (JoyScript.hpp) : 1 masque = 1 trame ;
//                             pilote le port 1 et NEUTRALISE le port 0 (comme --joy-script)
//    joy P1 [P0]              état joystick TENU (masques hexa)
//    key make|break SC        touche, scancode ST en hexa (ex. 39 = espace)
//    mouse DX DY BTN          souris relative ; BTN bit0 gauche, bit1 droite
//    peek ADR LEN             LEN octets (≤ 4096) en hexa, sans effet de bord
//    observe                  un échantillon SANS avancer d'une trame
//    save N | load N          emplacement d'état en mémoire
//    export N FICHIER         grave l'emplacement (relisible par --load-state)
//    import N FICHIER         charge un fichier d'état dans un emplacement
//    probe SPEC               ajoute une sonde à chaud (NOM=ADR:LEN)
//    shot FICHIER.ppm         capture d'écran
//    slots                    occupation des emplacements
//    quit                     fin de session
//  « run », « play », « load » et « observe » répondent avec les champs
//  d'observation (frame=, screen=, ram=, sondes) : UN aller-retour par rollout.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "headless/Server.hpp"
#include "util/JoyScript.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace neost::server {
namespace {

// Un emplacement retient l'état ET le numéro de trame : une cellule d'archive
// qu'on reprend doit reprendre AUSSI sa datation, sinon les trames rapportées
// au pilote ne veulent plus rien dire d'une branche à l'autre.
struct Slot {
    std::vector<uint8_t> data;
    long long            frame = 0;
    bool                 used  = false;
};

void reply(const std::string& s) {
    std::printf("%s\n", s.c_str());
    std::fflush(stdout);            // le client attend la ligne : jamais de tampon retenu
}

// Découpe « verbe reste-de-la-ligne ». Le reste n'est PAS re-découpé : un script
// joystick et un chemin de fichier contiennent des espaces.
//
// Le '\r' est retiré de la LIGNE ENTIÈRE en amont (cf. la boucle) et non du seul
// reste : un client Windows écrivant dans le tuyau en mode texte émet des CRLF,
// et « quit\r » ne se reconnaissait pas — la session ne se terminait jamais.
void splitFirst(const std::string& line, std::string& head, std::string& rest) {
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    std::size_t j = i;
    while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
    head = line.substr(i, j - i);
    while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
    rest = line.substr(j);
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t' || rest.back() == '\r'))
        rest.pop_back();
}

// Entier décimal STRICT : le jeton doit être consommé en entier. std::strtol seul
// acceptait « save abc » (slot 0), « load zzz » (slot 0) et « run 1e9 » (UNE
// trame), en répondant « ok » — sur un protocole dont tout l'intérêt est le rejeu
// déterministe, une faute de frappe du pilote corrompait l'archive en silence.
bool parseLong(const std::string& t, long& out) {
    if (t.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(t.c_str(), &end, 10);
    if (errno == ERANGE || end != t.c_str() + t.size()) return false;
    out = v;
    return true;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        std::size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// Pose l'état joystick sur les DEUX chemins (IKBD et pads STE $FF9200/02),
// exactement comme la boucle --frames — un frontend qui n'en poserait qu'un
// rendrait le rejoué au tuyau différent du rejoué en ligne de commande.
void applyJoy(Machine& m, uint8_t p0, uint8_t p1) {
    m.ikbd.setJoystick(p0, p1);
    m.bus.stePads.setJoystick(p0, p1);
    m.cpu.updateIpl();
}

}  // namespace

int run(Machine& machine, const Options& opts) {
    observe::ProbeSet set = opts.probes;
    std::vector<Slot> slots(std::size_t(opts.slots > 0 ? opts.slots : 1));
    long long frame = 0;
    uint8_t   joy0 = 0, joy1 = 0;

    std::fprintf(stderr, "[server] ready — %zu slots, %zu probes. Commands on stdin.\n",
                 slots.size(), set.probes.size());

    std::string line;
    while (std::getline(std::cin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        std::string cmd, rest;
        splitFirst(line, cmd, rest);
        if (cmd.empty() || cmd[0] == '#') continue;      // ligne vide ou commentaire
        const std::vector<std::string> a = words(rest);

        auto fields = [&]() { return observe::probeFields(frame, machine, set); };
        auto slotIndex = [&](const std::string& t, std::size_t& out) -> bool {
            long v = 0;
            if (!parseLong(t, v) || v < 0 || std::size_t(v) >= slots.size()) return false;
            out = std::size_t(v);
            return true;
        };

        if (cmd == "quit" || cmd == "exit") { reply("ok bye"); return 0; }

        if (cmd == "hello") { reply("ok " + opts.identity); continue; }

        if (cmd == "run") {
            if (a.empty()) { reply("err run expects a frame count"); continue; }
            long n = 0;
            if (!parseLong(a[0], n) || n < 0) { reply("err run expects a non-negative frame count"); continue; }
            for (long i = 0; i < n; ++i) { machine.runFrame(); ++frame; }
            reply("ok " + fields());
            continue;
        }

        if (cmd == "play") {
            std::vector<uint8_t> masks;
            std::string err;
            if (!joyscript::parse(rest, masks, err)) { reply("err " + err); continue; }
            // MÊME ordre que la boucle --frames : l'entrée est posée AVANT la
            // trame qu'elle doit influencer.
            // Un script décrit l'état COMPLET des deux ports, trame par trame : la
            // boucle --frames écrit le port 0 à zéro pendant qu'il joue (setJoystick(0,
            // st)). « play » préservait le port 0 tenu par « joy » — divergence
            // serveur ↔ boucle dès qu'un port 0 est tenu. Même sémantique ici.
            for (const uint8_t mask : masks) {
                joy0 = 0;
                joy1 = mask;
                applyJoy(machine, joy0, joy1);
                machine.runFrame();
                ++frame;
            }
            reply("ok " + fields());
            continue;
        }

        if (cmd == "joy") {
            if (a.empty()) { reply("err joy expects a port-1 mask"); continue; }
            uint32_t p1 = 0, p0 = 0;
            if (!joyscript::parseHexU32(a[0], p1) || p1 > 0xFF) { reply("err bad port-1 mask"); continue; }
            if (a.size() > 1 && (!joyscript::parseHexU32(a[1], p0) || p0 > 0xFF)) {
                reply("err bad port-0 mask"); continue;
            }
            joy1 = uint8_t(p1);
            joy0 = uint8_t(p0);
            applyJoy(machine, joy0, joy1);
            reply("ok");
            continue;
        }

        if (cmd == "key") {
            uint32_t sc = 0;
            if (a.size() < 2 || (a[0] != "make" && a[0] != "break")
                || !joyscript::parseHexU32(a[1], sc) || sc == 0 || sc > 0x7F) {
                reply("err key expects 'make|break <ST scancode in hex>'");
                continue;
            }
            machine.ikbd.keyEvent(uint8_t(sc), a[0] == "make");
            machine.cpu.updateIpl();
            reply("ok");
            continue;
        }

        if (cmd == "mouse") {
            if (a.size() < 3) { reply("err mouse expects 'DX DY BUTTONS'"); continue; }
            long dx = 0, dy = 0, bt = 0;
            if (!parseLong(a[0], dx) || !parseLong(a[1], dy) || !parseLong(a[2], bt)
                || dx < -32768 || dx > 32767 || dy < -32768 || dy > 32767 || bt < 0 || bt > 3) {
                reply("err mouse expects 'DX DY BUTTONS' (integers, buttons 0-3)");
                continue;
            }
            machine.ikbd.mouseEvent(int(dx), int(dy), (bt & 1) != 0, (bt & 2) != 0);
            machine.cpu.updateIpl();
            reply("ok");
            continue;
        }

        if (cmd == "peek") {
            uint32_t addr = 0;
            if (a.size() < 2 || !joyscript::parseHexU32(a[0], addr)) {
                reply("err peek expects 'ADDR LEN' (address in hex)");
                continue;
            }
            long len = 0;
            if (!parseLong(a[1], len) || len <= 0 || len > 4096) {
                reply("err peek length must be 1..4096");
                continue;
            }
            std::string hex;
            hex.reserve(std::size_t(len) * 2);
            char b[3];
            for (long k = 0; k < len; ++k) {
                std::snprintf(b, sizeof b, "%02X", observe::read8(machine, addr + uint32_t(k)));
                hex += b;
            }
            reply("ok " + hex);
            continue;
        }

        if (cmd == "observe") { reply("ok " + fields()); continue; }

        if (cmd == "save") {
            std::size_t s = 0;
            if (a.empty() || !slotIndex(a[0], s)) { reply("err save expects a slot index"); continue; }
            machine.saveState(slots[s].data);
            slots[s].frame = frame;
            slots[s].used  = true;
            reply("ok bytes=" + std::to_string(slots[s].data.size()));
            continue;
        }

        if (cmd == "load") {
            std::size_t s = 0;
            if (a.empty() || !slotIndex(a[0], s)) { reply("err load expects a slot index"); continue; }
            if (!slots[s].used) { reply("err slot " + a[0] + " is empty"); continue; }
            if (!machine.loadState(slots[s].data.data(), slots[s].data.size())) {
                reply("err state in slot " + a[0] + " rejected (machine config mismatch?)");
                continue;
            }
            frame = slots[s].frame;
            // L'état restauré rétablit le joystick SAUVEGARDÉ (généralement
            // neutre) : on repose celui que le client tient, sinon un « joy 80 »
            // suivi d'un « load » relâcherait le feu en silence — le même piège
            // que --load-state + --joy dans la boucle --frames.
            applyJoy(machine, joy0, joy1);
            reply("ok " + fields());
            continue;
        }

        if (cmd == "export") {
            std::size_t s = 0;
            std::string head, path;
            splitFirst(rest, head, path);
            if (head.empty() || path.empty() || !slotIndex(head, s)) {
                reply("err export expects 'SLOT FILE'"); continue;
            }
            if (!slots[s].used) { reply("err slot " + head + " is empty"); continue; }
            std::ofstream f(path, std::ios::binary);
            if (!f) { reply("err cannot write " + path); continue; }
            f.write(reinterpret_cast<const char*>(slots[s].data.data()),
                    std::streamsize(slots[s].data.size()));
            // Fermeture EXPLICITE avant le verdict : un disque plein ne se manifeste
            // parfois qu'au vidage final, et l'échec serait passé pour un « ok » —
            // le piège que writePpm garde déjà côté captures.
            f.close();
            if (!f) { reply("err write failed for " + path); continue; }
            reply("ok bytes=" + std::to_string(slots[s].data.size()));
            continue;
        }

        if (cmd == "import") {
            std::size_t s = 0;
            std::string head, path;
            splitFirst(rest, head, path);
            if (head.empty() || path.empty() || !slotIndex(head, s)) {
                reply("err import expects 'SLOT FILE'"); continue;
            }
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f) { reply("err cannot read " + path); continue; }
            // Même borne que Machine::loadStateFile : un état légitime, c'est la
            // RAM (≤ 4 Mo) plus les puces. Sans ce garde-fou, un chemin erroné
            // (une image disque, une vidéo) se ferait avaler tout entier en RAM.
            const std::streamoff n = f.tellg();
            if (n <= 0 || n > 64 * 1024 * 1024) {
                reply("err " + path + " is empty or too large to be a state");
                continue;
            }
            f.seekg(0);
            // Lu dans un tampon À PART, et l'emplacement n'est touché QUE si le
            // fichier est valide : un import raté écrasait la cellule déjà archivée
            // là (données vidées, `used` resté vrai) et le `load` suivant accusait
            // une « config différente ».
            std::vector<uint8_t> buf{std::istreambuf_iterator<char>(f),
                                     std::istreambuf_iterator<char>()};
            if (buf.empty()) { reply("err " + path + " is empty"); continue; }
            // Magie 'NSTS' vérifiée DÈS l'import : sans ça, un chemin erroné ne se
            // trahissait qu'au « load », plusieurs commandes plus loin, et le client
            // croyait tenir une cellule.
            if (buf.size() < 4 || buf[0] != 'S' || buf[1] != 'T' || buf[2] != 'S' || buf[3] != 'N') {
                reply("err " + path + " is not a NeoST save-state");
                continue;
            }
            slots[s].data  = std::move(buf);
            slots[s].frame = 0;          // datation inconnue : un fichier ne la porte pas
            slots[s].used  = true;
            reply("ok bytes=" + std::to_string(slots[s].data.size()));
            continue;
        }

        if (cmd == "probe") {
            observe::ProbeSpec p;
            std::string err;
            if (!observe::parseProbeSpec(rest, p, err)) { reply("err " + err); continue; }
            set.probes.push_back(p);
            reply("ok");
            continue;
        }

        if (cmd == "shot") {
            if (rest.empty()) { reply("err shot expects a file name"); continue; }
            if (!observe::writePpm(rest.c_str(), machine.shifter.pixels(),
                                   machine.shifter.width(), machine.shifter.height())) {
                reply("err cannot write " + rest);
                continue;
            }
            reply("ok");
            continue;
        }

        if (cmd == "slots") {
            std::size_t used = 0, bytes = 0;
            for (const auto& s : slots) if (s.used) { ++used; bytes += s.data.size(); }
            reply("ok used=" + std::to_string(used) + "/" + std::to_string(slots.size())
                  + " bytes=" + std::to_string(bytes));
            continue;
        }

        reply("err unknown command '" + cmd + "'");
    }
    return 0;      // fin de stdin : même sortie que « quit »
}

}  // namespace neost::server
