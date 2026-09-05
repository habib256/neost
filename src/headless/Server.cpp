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
#include <csignal>
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
    uint8_t              joy0  = 0;    // état joystick TENU par le client au moment du save :
    uint8_t              joy1  = 0;    // reprendre une cellule, c'est reprendre aussi ses entrées
    bool                 used  = false;
};

// Renvoie false si la ligne n'a pas pu être écrite : un stdout plein ou fermé
// laissait le serveur « répondre » dans le vide et sortir 0 (mesuré sur /dev/full).
// fwrite et non printf : un NUL dans une ligne du client tronquait l'écho.
bool reply(const std::string& s) {
    const bool ok = std::fwrite(s.data(), 1, s.size(), stdout) == s.size()
                 && std::fputc('\n', stdout) != EOF
                 && std::fflush(stdout) == 0;   // le client attend la ligne : jamais de tampon retenu
    if (!ok) std::fprintf(stderr, "[server] cannot write a reply on stdout — stopping\n");
    return ok;
}

// Longueur maximale d'un « run » : celle d'un script (JoyScript::kMaxFrames, ~55 h
// de temps ST). « run 9223372036854775807 » gelait la session sans recours ; un
// fuzz aléatoire a trouvé le cas seul (« run 53453622 »).
constexpr long kMaxRunFrames = 10L * 1000L * 1000L;

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

// Arité STRICTE : « save 0 nimportequoi » répondait « ok » en ignorant la suite —
// la classe de faute que parseLong ferme sur les nombres, rouverte sur le compte.
bool arity(const std::vector<std::string>& a, std::size_t lo, std::size_t hi) {
    return a.size() >= lo && a.size() <= hi;
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

    // Un client mort faisait mourir le serveur par SIGPIPE AVANT la fermeture propre de
    // la trace (fichier tronqué, code de signal). Ignoré, l'écriture échoue en EPIPE,
    // reply() le voit et la boucle sort par le chemin normal.
    std::signal(SIGPIPE, SIG_IGN);
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

        if (cmd == "quit" || cmd == "exit") { return reply("ok bye") ? 0 : 1; }

        if (cmd == "hello") { if (!reply("ok " + opts.identity)) return 1; continue; }

        if (cmd == "run") {
            if (!arity(a, 1, 1)) { if (!reply("err run expects exactly one frame count")) return 1; continue; }
            long n = 0;
            if (!parseLong(a[0], n) || n < 0) { if (!reply("err run expects a non-negative frame count")) return 1; continue; }
            if (n > kMaxRunFrames) { if (!reply("err run: at most 10000000 frames per command")) return 1; continue; }
            for (long i = 0; i < n; ++i) { machine.runFrame(); ++frame; }
            if (!reply("ok " + fields())) return 1;
            continue;
        }

        if (cmd == "play") {
            std::vector<uint8_t> masks;
            std::string err;
            if (!joyscript::parse(rest, masks, err)) { if (!reply("err " + err)) return 1; continue; }
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
            if (!reply("ok " + fields())) return 1;
            continue;
        }

        if (cmd == "joy") {
            if (!arity(a, 1, 2)) { if (!reply("err joy expects 'P1 [P0]' (hex masks)")) return 1; continue; }
            uint32_t p1 = 0, p0 = 0;
            if (!joyscript::parseHexU32(a[0], p1) || p1 > 0xFF) { if (!reply("err bad port-1 mask")) return 1; continue; }
            if (a.size() > 1 && (!joyscript::parseHexU32(a[1], p0) || p0 > 0xFF)) {
                if (!reply("err bad port-0 mask")) return 1;
                continue;
            }
            joy1 = uint8_t(p1);
            joy0 = uint8_t(p0);
            applyJoy(machine, joy0, joy1);
            if (!reply("ok")) return 1;
            continue;
        }

        if (cmd == "key") {
            uint32_t sc = 0;
            if (!arity(a, 2, 2) || (a[0] != "make" && a[0] != "break")
                || !joyscript::parseHexU32(a[1], sc) || sc == 0 || sc > 0x7F) {
                if (!reply("err key expects 'make|break <ST scancode in hex>'")) return 1;
                continue;
            }
            machine.ikbd.keyEvent(uint8_t(sc), a[0] == "make");
            machine.cpu.updateIpl();
            if (!reply("ok")) return 1;
            continue;
        }

        if (cmd == "mouse") {
            if (!arity(a, 3, 3)) { if (!reply("err mouse expects 'DX DY BUTTONS'")) return 1; continue; }
            long dx = 0, dy = 0, bt = 0;
            if (!parseLong(a[0], dx) || !parseLong(a[1], dy) || !parseLong(a[2], bt)
                || dx < -32768 || dx > 32767 || dy < -32768 || dy > 32767 || bt < 0 || bt > 3) {
                if (!reply("err mouse expects 'DX DY BUTTONS' (integers, buttons 0-3)")) return 1;
                continue;
            }
            machine.ikbd.mouseEvent(int(dx), int(dy), (bt & 1) != 0, (bt & 2) != 0);
            machine.cpu.updateIpl();
            if (!reply("ok")) return 1;
            continue;
        }

        if (cmd == "peek") {
            uint32_t addr = 0;
            if (!arity(a, 2, 2) || !joyscript::parseHexU32(a[0], addr)) {
                if (!reply("err peek expects 'ADDR LEN' (address in hex)")) return 1;
                continue;
            }
            long len = 0;
            if (!parseLong(a[1], len) || len <= 0 || len > 4096) {
                if (!reply("err peek length must be 1..4096")) return 1;
                continue;
            }
            std::string hex;
            hex.reserve(std::size_t(len) * 2);
            char b[3];
            for (long k = 0; k < len; ++k) {
                std::snprintf(b, sizeof b, "%02X", observe::read8(machine, addr + uint32_t(k)));
                hex += b;
            }
            if (!reply("ok " + hex)) return 1;
            continue;
        }

        if (cmd == "observe") { if (!reply("ok " + fields())) return 1; continue; }

        if (cmd == "save") {
            std::size_t s = 0;
            if (!arity(a, 1, 1) || !slotIndex(a[0], s)) { if (!reply("err save expects a slot index")) return 1; continue; }
            machine.saveState(slots[s].data);
            slots[s].frame = frame;
            slots[s].joy0  = joy0;
            slots[s].joy1  = joy1;
            slots[s].used  = true;
            if (!reply("ok bytes=" + std::to_string(slots[s].data.size()))) return 1;
            continue;
        }

        if (cmd == "load") {
            std::size_t s = 0;
            if (!arity(a, 1, 1) || !slotIndex(a[0], s)) { if (!reply("err load expects a slot index")) return 1; continue; }
            if (!slots[s].used) { if (!reply("err slot " + a[0] + " is empty")) return 1; continue; }
            if (!machine.loadState(slots[s].data.data(), slots[s].data.size())) {
                if (!reply("err state in slot " + a[0] + " rejected — reason on stderr (CRC, "
                           "version, ROM, machine or RAM mismatch, or truncated)")) return 1;
                continue;
            }
            frame = slots[s].frame;
            // Reprendre une cellule, c'est reprendre AUSSI l'entrée qui y était tenue.
            // Reposer le joystick tenu MAINTENANT (ancienne version) rendait le rejeu
            // dépendant de la branche explorée entre-temps : même cellule, deux
            // hachages RAM différents (mesuré). Pour un état importé (fichier), on ne
            // sait rien : on laisse ce que l'état porte et on remet le tenu à zéro.
            joy0 = slots[s].joy0;
            joy1 = slots[s].joy1;
            applyJoy(machine, joy0, joy1);
            if (!reply("ok " + fields())) return 1;
            continue;
        }

        if (cmd == "export") {
            std::size_t s = 0;
            std::string head, path;
            splitFirst(rest, head, path);
            if (head.empty() || path.empty() || !slotIndex(head, s)) {
                if (!reply("err export expects 'SLOT FILE'")) return 1;
                continue;
            }
            if (!slots[s].used) { if (!reply("err slot " + head + " is empty")) return 1; continue; }
            std::ofstream f(path, std::ios::binary);
            if (!f) { if (!reply("err cannot write " + path)) return 1; continue; }
            f.write(reinterpret_cast<const char*>(slots[s].data.data()),
                    std::streamsize(slots[s].data.size()));
            // Fermeture EXPLICITE avant le verdict : un disque plein ne se manifeste
            // parfois qu'au vidage final, et l'échec serait passé pour un « ok » —
            // le piège que writePpm garde déjà côté captures.
            f.close();
            if (!f) { if (!reply("err write failed for " + path)) return 1; continue; }
            if (!reply("ok bytes=" + std::to_string(slots[s].data.size()))) return 1;
            continue;
        }

        if (cmd == "import") {
            std::size_t s = 0;
            std::string head, path;
            splitFirst(rest, head, path);
            if (head.empty() || path.empty() || !slotIndex(head, s)) {
                if (!reply("err import expects 'SLOT FILE'")) return 1;
                continue;
            }
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f) { if (!reply("err cannot read " + path)) return 1; continue; }
            // Même borne que Machine::loadStateFile : un état légitime, c'est la
            // RAM (≤ 4 Mo) plus les puces. Sans ce garde-fou, un chemin erroné
            // (une image disque, une vidéo) se ferait avaler tout entier en RAM.
            const std::streamoff n = f.tellg();
            if (n <= 0 || n > 64 * 1024 * 1024) {
                if (!reply("err " + path + " is empty or too large to be a state")) return 1;
                continue;
            }
            f.seekg(0);
            // Lu dans un tampon À PART, et l'emplacement n'est touché QUE si le
            // fichier est valide : un import raté écrasait la cellule déjà archivée
            // là (données vidées, `used` resté vrai) et le `load` suivant accusait
            // une « config différente ».
            std::vector<uint8_t> buf{std::istreambuf_iterator<char>(f),
                                     std::istreambuf_iterator<char>()};
            if (buf.empty()) { if (!reply("err " + path + " is empty")) return 1; continue; }
            // Magie 'NSTS' vérifiée DÈS l'import : sans ça, un chemin erroné ne se
            // trahissait qu'au « load », plusieurs commandes plus loin, et le client
            // croyait tenir une cellule.
            if (buf.size() < 4 || buf[0] != 'S' || buf[1] != 'T' || buf[2] != 'S' || buf[3] != 'N') {
                if (!reply("err " + path + " is not a NeoST save-state")) return 1;
                continue;
            }
            slots[s].data  = std::move(buf);
            slots[s].frame = 0;          // datation inconnue : un fichier ne la porte pas
            slots[s].joy0  = 0;          // entrée tenue inconnue : l'état porte la sienne
            slots[s].joy1  = 0;
            slots[s].used  = true;
            if (!reply("ok bytes=" + std::to_string(slots[s].data.size()))) return 1;
            continue;
        }

        if (cmd == "probe") {
            observe::ProbeSpec p;
            std::string err;
            if (!observe::parseProbeSpec(rest, p, err) || !observe::addProbe(set, p, err)) {
                if (!reply("err " + err)) return 1;
                continue;
            }
            if (!reply("ok")) return 1;
            continue;
        }

        if (cmd == "shot") {
            if (rest.empty()) { if (!reply("err shot expects a file name")) return 1; continue; }
            if (!observe::writePpm(rest.c_str(), machine.shifter.pixels(),
                                   machine.shifter.width(), machine.shifter.height())) {
                if (!reply("err cannot write " + rest)) return 1;
                continue;
            }
            if (!reply("ok")) return 1;
            continue;
        }

        if (cmd == "slots") {
            std::size_t used = 0, bytes = 0;
            for (const auto& s : slots) if (s.used) { ++used; bytes += s.data.size(); }
            if (!reply("ok used=" + std::to_string(used) + "/" + std::to_string(slots.size())
                  + " bytes=" + std::to_string(bytes))) return 1;
            continue;
        }

        if (!reply("err unknown command '" + cmd.substr(0, 40) + (cmd.size() > 40 ? "…" : "") + "'")) return 1;
    }
    return 0;      // fin de stdin : même sortie que « quit »
}

}  // namespace neost::server
