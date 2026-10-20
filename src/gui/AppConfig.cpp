// =============================================================================
//  AppConfig.cpp — implémentation. Voir AppConfig.hpp pour la raison d'être.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/ConfigPath.hpp"
#include "util/MouseScale.hpp"   // bornes partagées avec le curseur
#include "gui/AppConfig.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ostream>

namespace fs = std::filesystem;

namespace neost::appconfig {

// A36 : la règle vit dans util/ConfigPath.hpp (pure et testable). Ici on la branche
// sur le vrai disque. Résolue UNE FOIS par exécution : la réponse ne peut pas changer
// en cours de session, et la sonde d'inscriptibilité écrit un fichier de test.
std::string cfgPath(const std::string& exeDir) {
    static const std::string resolved =
        neost::cfgpath::resolve(exeDir, neost::cfgpath::systemProbe());
    return resolved;
}

// Gain d'une voie du mixeur, borné comme le curseur de la page Sound (0..200 %).
// Test EN POSITIF : NaN (fichier corrompu, « mix_ym=nan ») échoue toute comparaison
// et passerait au travers d'un « if (v < 0) … if (v > 2) … ». Un gain NaN se propage
// dans la synthèse et FIGE l'état des filtres (YM2149 hpfX1_/hpfY0_) : plus aucun son
// jusqu'au reset machine, même après avoir remis le curseur en place.
static float mixGain(const std::string& s) {
    const float v = std::strtof(s.c_str(), nullptr);
    return std::isnan(v) ? 1.0f : std::clamp(v, 0.0f, 2.0f);
}

// Sensibilité de la souris émulée, bornée comme le curseur de la page Input.
// MÊME leçon NaN que mixGain ci-dessus (test en POSITIF) : un « mousespeed=nan » d'un
// fichier corrompu passerait au travers d'un bornage par comparaisons, et le delta
// mis à l'échelle deviendrait NaN — `int(NaN)` est un comportement indéfini, et la
// souris ST ne bougerait plus jamais. Le plancher n'est pas 0 : à 0 la souris serait
// FIGÉE sans que rien ne le dise à l'utilisateur.
static float mouseSpeedOf(const std::string& s) {
    const float v = std::strtof(s.c_str(), nullptr);
    namespace ms = neost::mousescale;
    return std::isnan(v) ? ms::kDefault : std::clamp(v, ms::kMin, ms::kMax);
}

// A24 (audit 2026-08-27) : les 14 clés crt_* étaient des strtof NUS — la leçon NaN
// payée sur volume=/mix_* (ci-dessus) n'avait pas été appliquée au bloc voisin du
// même parseur, alors que CrtParams.h documente la plage attendue de chaque champ.
// Même règle : NaN → défaut, sinon bornage au plus proche. Impact d'une valeur
// folle : des uniformes GL absurdes (écran illisible), pas de corruption — mais
// l'incohérence de validation était le vrai défaut.
// Listes d'appareils MIDI : UNE clé, UNE ligne, une AFFECTATION — comme toutes les
// autres clés du format. La première version utilisait des clés RÉPÉTABLES
// (midi_out_device= une fois par appareil) et c'était un piège : parseConfigLine est
// partagé avec les PROFILS nommés, qui s'appliquent PAR-DESSUS la config courante
// (loadProfileInto). Là où une clé scalaire remplace, un push_back AJOUTE — charger
// un profil dupliquait donc chaque appareil, et deux lignes de même nom se partagent
// le même ImGui::PushID, donc leurs cases se pilotaient l'une l'autre.
//
// Encodage : enregistrements séparés par « ; », champs par « | », et les trois
// caractères spéciaux (antislash, ; et |) sont échappés par un antislash DANS LE
// NOM. Un nom d'appareil peut contenir n'importe quoi — c'est ce qui avait fait
// écarter un séparateur au départ ; l'échappement lève l'objection sans ramener la
// répétition.
std::string escapeField(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (ch == '\\' || ch == ';' || ch == '|') out += '\\';
        out += ch;
    }
    return out;
}

std::vector<std::vector<std::string>> decodeRecords(const std::string& s) {
    std::vector<std::vector<std::string>> recs;
    std::vector<std::string> cur{std::string()};
    bool esc = false, any = false;
    for (char ch : s) {
        any = true;
        if (esc)              { cur.back() += ch; esc = false; }
        else if (ch == '\\')  { esc = true; }
        else if (ch == '|')   { cur.emplace_back(); }
        else if (ch == ';')   { recs.push_back(cur); cur.assign(1, std::string()); }
        else                  { cur.back() += ch; }
    }
    if (any) recs.push_back(cur);
    // Un enregistrement sans nom n'est pas un appareil : on le jette plutôt que de
    // faire apparaître une ligne vide dans l'interface.
    recs.erase(std::remove_if(recs.begin(), recs.end(),
                              [](const std::vector<std::string>& r) { return r.empty() || r[0].empty(); }),
               recs.end());
    return recs;
}

// Masque de canaux MIDI ↔ texte. « 1-16 » (tous), « 2 », « 1,3,10-12 ». Format
// compact à l'écriture : les suites consécutives sont repliées en intervalle, ce qui
// rend neost.cfg lisible et modifiable à la main.
uint16_t parseChannelMask(const std::string& s) {
    // « none » : masque VIDE explicite, c'est-à-dire l'appareil coupé. Il faut un jeton
    // pour ça, sinon un masque vide ne peut pas se distinguer d'une ligne illisible —
    // et c'est ce qui faisait qu'un appareil dont on coupait TOUS les canaux (bouton
    // `none` de la page MIDI) revenait « 1-16 » au redémarrage, donc grand ouvert.
    if (s == "none") return 0;
    uint16_t m = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ',' || s[i] == ' ')) ++i;
        int a = 0; bool got = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { a = a * 10 + (s[i++] - '0'); got = true; }
        if (!got) break;
        int b = a;
        if (i < s.size() && s[i] == '-') {
            ++i; b = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') b = b * 10 + (s[i++] - '0');
        }
        for (int c = a; c <= b; ++c)
            if (c >= 1 && c <= 16) m = uint16_t(m | (1u << (c - 1)));
    }
    // Une ligne ILLISIBLE vaut « tous les canaux » plutôt qu'un appareil sourd dont
    // l'utilisateur chercherait la panne. Le masque vide VOULU, lui, s'écrit « none »
    // et est traité tout en haut : les deux cas ne se confondent plus.
    return m ? m : uint16_t(0xFFFF);
}

std::string formatChannelMask(uint16_t m) {
    std::string out;
    for (int c = 1; c <= 16; ) {
        if (!((m >> (c - 1)) & 1)) { ++c; continue; }
        int e = c;
        while (e < 16 && ((m >> e) & 1)) ++e;
        if (!out.empty()) out += ',';
        out += std::to_string(c);
        if (e > c) { out += '-'; out += std::to_string(e); }
        c = e + 1;
    }
    // Symétrique de parseChannelMask : un masque vide s'écrit « none », pas « 1-16 »
    // — écrire « tous » pour dire « aucun » inversait le réglage au rechargement.
    return out.empty() ? std::string("none") : out;
}

static float crtF(const std::string& s, float lo, float hi, float dflt) {
    const float v = std::strtof(s.c_str(), nullptr);
    return std::isnan(v) ? dflt : std::clamp(v, lo, hi);
}

bool parseRtcConfig(const std::string& s, Rtc::DateTime& dt) {
    return std::sscanf(s.c_str(), "%d,%d,%d,%d,%d,%d,%d",
                       &dt.sec, &dt.min, &dt.hour, &dt.wday,
                       &dt.day, &dt.month, &dt.year) == 7;
}

// Applique UNE ligne « clé=valeur » à `c`. Extrait de loadConfig pour être partagé
// avec les PROFILS nommés (profiles/*.cfg, même format) : un profil n'écrit qu'un
// sous-ensemble des clés, et tout ce qu'il omet garde donc la valeur déjà présente
// dans `c`. C'est ce qui permet de charger un profil PAR-DESSUS la configuration
// courante sans tenir à jour une liste de recopie champ par champ.
// Fin de ligne CRLF (fichier passé par Windows, un éditeur, un partage réseau) :
// getline ne retire que le \n, et TOUTES les valeurs sont comparées EXACTEMENT
// (parseMachine, parseRamBytes, == "1"). Un \r collé faisait donc tomber chaque
// clé sur son défaut SILENCIEUX — machine ST demandée, STE démarrée ; 4 Mo
// demandés, 512 Ko alloués — et rendait tout chemin introuvable. Pire : saveConfig
// réécrivait ensuite le fichier avec les \r intacts, donc la panne était définitive.
// Même rognage que SymbolTable (Symbols.cpp). On retire aussi les espaces de fin.
void trimConfigLine(std::string& line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
}

void parseConfigLine(Config& c, std::string line) {
    trimConfigLine(line);
    if      (line.rfind("rom=", 0)  == 0) c.rom  = line.substr(4);
    else if (line.rfind("disk=", 0) == 0) c.disk = line.substr(5);
    else if (line.rfind("cart=", 0) == 0) c.cart = line.substr(5);
    else if (line.rfind("gemdos=", 0) == 0) c.gemdos = line.substr(7);
    else if (line.rfind("acsi=", 0) == 0) c.acsi = line.substr(5);
    else if (line.rfind("modem=", 0) == 0) c.modem = (line.substr(6) == "1");
    else if (line.rfind("midi_loopback=", 0) == 0) c.midiLoopback = (line.substr(14) == "1");
    else if (line.rfind("midi_out_gm=", 0) == 0) c.midiOutGm = (line.substr(12) == "1");
    else if (line.rfind("gm_soundfont=", 0) == 0) c.gmSoundFont = line.substr(13);
    else if (line.rfind("midi_out_port=", 0) == 0) c.midiOutPort = (line.substr(14) == "1");
    else if (line.rfind("midi_out_devices=", 0) == 0) {
        c.midiOutDevices.clear();
        for (const auto& r : decodeRecords(line.substr(17)))
            c.midiOutDevices.push_back({r[0], r.size() > 1 ? parseChannelMask(r[1]) : uint16_t(0xFFFF),
                                        r.size() > 2 ? r[2] : std::string()});
    }
    else if (line.rfind("midi_in_devices=", 0) == 0) {
        c.midiInDevices.clear();
        for (const auto& r : decodeRecords(line.substr(16))) {
            const int ch = r.size() > 1 ? std::atoi(r[1].c_str()) : 0;
            c.midiInDevices.push_back({r[0], (ch >= 1 && ch <= 16) ? ch : 0,
                                       r.size() > 2 ? r[2] : std::string()});
        }
    }
    // HÉRITÉ (clés répétables, format du 2026-08-29 au matin) : encore LU pour qu'un
    // neost.cfg d'avant ne perde pas son studio en silence, jamais ÉCRIT — la
    // première sauvegarde le convertit. Ces clés portent le défaut décrit plus haut ;
    // c'est sans conséquence sur un neost.cfg, lu dans une Config NEUVE.
    else if (line.rfind("midi_out_device=", 0) == 0) {
            // uid VIDE à dessein : ce format est d'ANTÉRIEUR aux identifiants uniques,
            // il ne peut donc pas en porter. App::midiLearnUids() le renseigne à la
            // première ouverture réussie de l'appareil, et la sauvegarde suivante
            // convertit la ligne au format courant. Les autres champs gardent leur
            // défaut de structure (cf. Config::MidiOutDev) — les nommer ici les
            // recopierait, et un champ ajouté demain serait oublié en silence.
        Config::MidiOutDev d;
        d.name = line.substr(16);
        c.midiOutDevices.push_back(std::move(d));
    }
    else if (line.rfind("midi_out_channels=", 0) == 0) {
        if (!c.midiOutDevices.empty()) c.midiOutDevices.back().channels = parseChannelMask(line.substr(18));
    }
    else if (line.rfind("midi_lead_ms=", 0) == 0) {
        const int v = std::atoi(line.substr(13).c_str());
        c.midiLeadMs = (v < 0) ? 0 : (v > 200 ? 200 : v);
    }
    else if (line.rfind("midi_in_device=", 0) == 0) {
        Config::MidiInDev d;      // uid vide : même raison qu'en sortie, ci-dessus
        d.name = line.substr(15);
        c.midiInDevices.push_back(std::move(d));
    }
    else if (line.rfind("midi_in_channel=", 0) == 0) {
        if (!c.midiInDevices.empty()) {
            const int ch = std::atoi(line.substr(16).c_str());
            c.midiInDevices.back().channel = (ch >= 1 && ch <= 16) ? ch : 0;
        }
    }
    else if (line.rfind("midi_out_mt32=", 0) == 0) c.midiOutMt32 = (line.substr(14) == "1");
    else if (line.rfind("mt32_roms=", 0) == 0) c.mt32Roms = line.substr(10);
    else if (line.rfind("mt32_model=", 0) == 0) c.mt32Model = line.substr(11);
    else if (line.rfind("mix_ym=", 0) == 0) c.mixYm = mixGain(line.substr(7));
    else if (line.rfind("mix_dma=", 0) == 0) c.mixDma = mixGain(line.substr(8));
    else if (line.rfind("mix_drive=", 0) == 0) c.mixDrive = mixGain(line.substr(10));
    else if (line.rfind("mix_mt32=", 0) == 0) c.mixMt32 = mixGain(line.substr(9));
    else if (line.rfind("mix_gm=", 0) == 0) c.mixGm = mixGain(line.substr(7));
    else if (line.rfind("dongle=", 0) == 0) c.dongle = line.substr(7);
    else if (line.rfind("joy0=", 0) == 0) c.joy0 = line.substr(5);
    else if (line.rfind("joy1=", 0) == 0) c.joy1 = line.substr(5);
    else if (line.rfind("rs232=", 0) == 0) c.rs232 = line.substr(6);
    else if (line.rfind("printer=", 0) == 0) c.printer = line.substr(8);
    else if (line.rfind("cartbutton=", 0) == 0) c.cartbutton = line.substr(11);
    else if (line.rfind("mix_dac=", 0) == 0) c.mixDac = mixGain(line.substr(8));
    else if (line.rfind("auto_dongle=", 0) == 0) c.autoDongle = (line.substr(12) != "0");
    else if (line.rfind("adapter=", 0) == 0) {           // ancien format : un seul adaptateur
        const std::string a = line.substr(8);
        static const char* const joy1[] = { "leaderboard", "10thframe", "rugby" };
        static const char* const joy0[] = { "cricket", "soccer" };
        static const char* const ser[]  = { "bat2", "musicmaster", "jeannedarc" };
        static const char* const btn[]  = { "multiface", "urc" };
        auto in = [&](const char* const* t, size_t n) { for (size_t i = 0; i < n; ++i) if (a == t[i]) return true; return false; };
        if      (in(joy1, 3)) c.joy1 = a;
        else if (in(joy0, 2)) c.joy0 = a;
        else if (in(ser, 3))  c.rs232 = a;
        else if (in(btn, 2))  c.cartbutton = a;
        else if (a == "prosound") c.printer = a;
    }
    else if (line.rfind("ethernec=", 0) == 0) c.ethernec = (line.substr(9) == "1");
    else if (line.rfind("netusbee=", 0) == 0) c.netusbee = (line.substr(9) == "1");
    else if (line.rfind("slirp=", 0) == 0)    c.slirp    = (line.substr(6) == "1");
    else if (line.rfind("ultrasatan=", 0) == 0) c.ultrasatan = (line.substr(11) == "1");
    else if (line.rfind("sd2=", 0) == 0) c.sd2 = line.substr(4);
    else if (line.rfind("mono=", 0) == 0) c.mono = (line.substr(5) == "1");
    else if (line.rfind("cpu=", 0)  == 0) c.cpu  = line.substr(4);
    else if (line.rfind("machine=", 0) == 0) c.machine = line.substr(8);
    else if (line.rfind("mem=", 0)  == 0) c.mem  = line.substr(4);
    else if (line.rfind("fpu=", 0)  == 0) c.fpu  = (line.substr(4) == "1");
    else if (line.rfind("joyport=", 0) == 0) c.joyport = (line.substr(8) == "0") ? 0 : 1;
    else if (line.rfind("joymap=", 0) == 0) c.joymap = line.substr(7);
    else if (line.rfind("port0=", 0) == 0) c.port0 = (line.substr(6) == "auto") ? "auto" : "mouse";
    else if (line.rfind("joydeadzone=", 0) == 0) {
        // Bornée comme volume= : négative, > 0.95 ou NaN (fichier hostile/corrompu),
        // la valeur brute rendait le menu kiosque incontrôlable (padAxis compare
        // fabs(v) > deadzone sans re-borner).
        c.joydeadzone = std::strtof(line.substr(12).c_str(), nullptr);
        if (!(c.joydeadzone >= 0.0f && c.joydeadzone <= 0.95f)) c.joydeadzone = 0.30f;
    }
    else if (line.rfind("fastfdc=", 0) == 0) c.fastfdc = (line.substr(8) == "1");
    else if (line.rfind("volume=", 0) == 0) {
        // ⚠ Le NaN doit être écarté AVANT le bornage : « if (v < 0) … if (v > 1) … »
        // le laissait passer intact (toute comparaison avec NaN est fausse). Un volume
        // NaN se propage dans la synthèse et FIGE l'état des filtres audio (YM2149
        // hpfX1_/hpfY0_) : plus aucun son jusqu'au reset machine. Hors NaN, on garde le
        // bornage au plus proche (volume=-3 → 0, volume=42 → 1).
        c.volume = std::strtof(line.substr(7).c_str(), nullptr);
        if (std::isnan(c.volume)) c.volume = 1.0f;
        else c.volume = std::clamp(c.volume, 0.0f, 1.0f);
    }
    else if (line.rfind("audio_latency_ms=", 0) == 0) c.audioLatencyMs = std::atoi(line.substr(17).c_str());
    else if (line.rfind("drivesound=", 0) == 0) c.driveSound = (line.substr(11) == "1");
    // showCart=/showHd= : clés d'anciennes fenêtres devenues des pages de la
    // Configuration. showDisk= reste accepté comme ancien nom de showFloppy=.
    else if (line.rfind("showDisk=", 0) == 0) c.showFloppy = (line.substr(9) == "1");
    else if (line.rfind("showHex=", 0) == 0) c.showHex = (line.substr(8) == "1");
    else if (line.rfind("showCpu=", 0) == 0) c.showCpu = (line.substr(8) == "1");
    else if (line.rfind("showJoy=", 0) == 0) c.showJoy = (line.substr(8) == "1");
    else if (line.rfind("showKbd=", 0) == 0) c.showKbd = (line.substr(8) == "1");
    else if (line.rfind("showCfg=", 0) == 0) c.showCfg = (line.substr(8) == "1");
    else if (line.rfind("showFloppy=", 0) == 0) c.showFloppy = (line.substr(11) == "1");
    else if (line.rfind("uiVersion=", 0) == 0) c.uiVersion = std::atoi(line.c_str() + 10);
    else if (line.rfind("diskb=", 0)  == 0) c.diskb   = line.substr(6);
    else if (line.rfind("dock=", 0) == 0) c.dock = (line.substr(5) == "1");
    else if (line.rfind("autozoom=", 0) == 0) c.autoZoom = (line.substr(9) == "1");
    else if (line.rfind("mousespeed=", 0) == 0) c.mouseSpeed = mouseSpeedOf(line.substr(11));
    else if (line.rfind("desktop_mouse=", 0) == 0) c.desktopMouse = line.substr(14) == "1";
    else if (line.rfind("rtc_saved=", 0) == 0) c.rtcSaved = std::strtoll(line.substr(10).c_str(), nullptr, 10);
    else if (line.rfind("rtc=", 0) == 0) c.rtc = line.substr(4);
    else if (line.rfind("kiosk_romdir=", 0) == 0) { const std::string d = line.substr(13); if (!d.empty()) c.romDirs.push_back(d); }
    // Effets CRT (cf. gui/CrtParams.h). Un preset (--crt-preset / applyCrtPreset)
    // n'est qu'un raccourci qui écrit ces mêmes clés numériques.
    else if (line.rfind("crt=", 0) == 0) c.crt = (line.substr(4) == "1");
    else if (line.rfind("crt_bright=", 0)  == 0) c.crtParams.brightness  = crtF(line.substr(11), -0.5f, 0.5f, 0.0f);
    else if (line.rfind("crt_contrast=", 0) == 0) c.crtParams.contrast   = crtF(line.substr(13), 0.5f, 1.5f, 1.0f);
    else if (line.rfind("crt_sat=", 0)     == 0) c.crtParams.saturation  = crtF(line.substr(8), 0.0f, 2.0f, 1.0f);
    else if (line.rfind("crt_hue=", 0)     == 0) c.crtParams.hue         = crtF(line.substr(8), -0.5f, 0.5f, 0.0f);
    else if (line.rfind("crt_sharp=", 0)   == 0) c.crtParams.sharpness   = crtF(line.substr(10), 0.0f, 1.0f, 0.5f);
    else if (line.rfind("crt_persist=", 0) == 0) c.crtParams.persistence = crtF(line.substr(12), 0.0f, 0.98f, 0.4f);
    else if (line.rfind("crt_scanlines=", 0) == 0) c.crtParams.scanlines = crtF(line.substr(14), 0.0f, 1.0f, 0.25f);
    else if (line.rfind("crt_barrel=", 0)  == 0) c.crtParams.barrel      = crtF(line.substr(11), 0.0f, 0.30f, 0.025f);
    else if (line.rfind("crt_mask=", 0)    == 0) c.crtParams.shadowMask  = static_cast<neost::CrtParams::ShadowMask>(
                              std::clamp(std::atoi(line.substr(9).c_str()), 0, 3));
    else if (line.rfind("crt_maskstr=", 0) == 0) c.crtParams.shadowMaskStrength = crtF(line.substr(12), 0.0f, 1.0f, 0.5f);
    else if (line.rfind("crt_lumgain=", 0) == 0) c.crtParams.luminanceGain = crtF(line.substr(12), 1.0f, 2.0f, 1.0f);
    else if (line.rfind("crt_center=", 0)  == 0) c.crtParams.centerLighting = crtF(line.substr(11), 0.5f, 1.0f, 1.0f);
    else if (line.rfind("crt_gamma=", 0)   == 0) c.crtParams.phosphorGamma  = crtF(line.substr(10), 0.6f, 2.6f, 1.0f);
}
Config loadConfig(const std::string& exeDir) {
    Config c;
    std::ifstream f(cfgPath(exeDir));
    if (!f) f.open("neost.cfg");
    std::string line;
    while (std::getline(f, line)) parseConfigLine(c, line);
    return c;
}

// Sérialise `w` en « clé=valeur ». `full` = le neost.cfg complet ; false = un PROFIL
// nommé, qui laisse dehors ce qui n'appartient pas à un jeu de réglages :
//   · rtc= / rtc_saved=  → état de la machine, pas un réglage ;
//   · kiosk_romdir=      → déploiement de la borne, propre à l'installation ;
//   · showXxx= / dock= / uiVersion= → disposition de l'interface (cousins d'imgui.ini) :
//     charger un profil ne doit pas déplacer les fenêtres de l'utilisateur.
// Tout ce qui n'est PAS écrit ici reste donc inchangé au chargement d'un profil
// (cf. parseConfigLine) — les deux fonctions se répondent, ne toucher qu'ensemble.
void writeConfigKeys(std::ostream& f, const Config& w, bool full) {
    f << "rom=" << w.rom << "\ndisk=" << w.disk << "\ndiskb=" << w.diskb
      << "\ncart=" << w.cart
      << "\ngemdos=" << w.gemdos << "\nacsi=" << w.acsi
      << "\nmodem=" << (w.modem ? 1 : 0)
      << "\nmidi_loopback=" << (w.midiLoopback ? 1 : 0)
      << "\nmidi_out_gm=" << (w.midiOutGm ? 1 : 0)
      << "\ngm_soundfont=" << w.gmSoundFont
      << "\nmidi_out_port=" << (w.midiOutPort ? 1 : 0)
      << "\nmidi_lead_ms=" << w.midiLeadMs

      << "\nmidi_out_mt32=" << (w.midiOutMt32 ? 1 : 0)
      << "\nmt32_roms=" << w.mt32Roms
      << "\nmt32_model=" << w.mt32Model
      << "\nmix_ym=" << w.mixYm << "\nmix_dma=" << w.mixDma
      << "\nmix_drive=" << w.mixDrive << "\nmix_mt32=" << w.mixMt32
      << "\nmix_gm=" << w.mixGm
      << "\ndongle=" << w.dongle
      << "\njoy0=" << w.joy0 << "\njoy1=" << w.joy1 << "\nrs232=" << w.rs232
      << "\nprinter=" << w.printer << "\ncartbutton=" << w.cartbutton
      << "\nmix_dac=" << w.mixDac
      << "\nauto_dongle=" << (w.autoDongle ? 1 : 0)
      << "\nethernec=" << (w.ethernec ? 1 : 0)
      << "\nnetusbee=" << (w.netusbee ? 1 : 0)
      << "\nslirp="    << (w.slirp    ? 1 : 0)
      << "\nultrasatan=" << (w.ultrasatan ? 1 : 0)
      << "\nsd2=" << w.sd2
      << "\nmono=" << (w.mono ? 1 : 0)
      << "\ncpu=" << w.cpu << "\nmachine=" << w.machine << "\nmem=" << w.mem
      << "\nfpu=" << (w.fpu ? 1 : 0)
      << "\njoyport=" << w.joyport
      << "\njoymap=" << w.joymap
      << "\nport0=" << w.port0
      << "\njoydeadzone=" << w.joydeadzone << "\nfastfdc=" << (w.fastfdc ? 1 : 0)
      << "\nvolume=" << w.volume
      << "\naudio_latency_ms=" << w.audioLatencyMs
      << "\ndrivesound=" << (w.driveSound ? 1 : 0) << "\n";
    // Appareils MIDI hôtes : UNE ligne par liste. Toujours écrites, même vides —
    // c'est ce qui permet à un profil d'EFFACER le studio, là où une clé absente
    // laisserait en place celui d'avant.
    f << "midi_out_devices=";
    for (std::size_t i = 0; i < w.midiOutDevices.size(); ++i)
        f << (i ? ";" : "") << escapeField(w.midiOutDevices[i].name) << '|'
          << formatChannelMask(w.midiOutDevices[i].channels) << '|'
          << escapeField(w.midiOutDevices[i].uid);
    f << "\nmidi_in_devices=";
    for (std::size_t i = 0; i < w.midiInDevices.size(); ++i)
        f << (i ? ";" : "") << escapeField(w.midiInDevices[i].name) << '|'
          << w.midiInDevices[i].channel << '|' << escapeField(w.midiInDevices[i].uid);
    f << "\n";
    if (full)
        f << "showHex=" << (w.showHex ? 1 : 0)
          << "\nshowCpu=" << (w.showCpu ? 1 : 0)
          << "\nshowJoy=" << (w.showJoy ? 1 : 0)
          << "\nshowKbd=" << (w.showKbd ? 1 : 0)
          << "\nshowCfg=" << (w.showCfg ? 1 : 0)
          << "\nshowFloppy=" << (w.showFloppy ? 1 : 0)
          << "\nuiVersion=" << w.uiVersion
          << "\ndock=" << (w.dock ? 1 : 0) << "\n";
    f << "mousespeed=" << w.mouseSpeed << "\n";
    f << "desktop_mouse=" << (w.desktopMouse ? 1 : 0) << "\n";
    f << "autozoom=" << (w.autoZoom ? 1 : 0)
      << "\ncrt=" << (w.crt ? 1 : 0)
      << "\ncrt_bright=" << w.crtParams.brightness
      << "\ncrt_contrast=" << w.crtParams.contrast
      << "\ncrt_sat=" << w.crtParams.saturation
      << "\ncrt_hue=" << w.crtParams.hue
      << "\ncrt_sharp=" << w.crtParams.sharpness
      << "\ncrt_persist=" << w.crtParams.persistence
      << "\ncrt_scanlines=" << w.crtParams.scanlines
      << "\ncrt_barrel=" << w.crtParams.barrel
      << "\ncrt_mask=" << static_cast<int>(w.crtParams.shadowMask)
      << "\ncrt_maskstr=" << w.crtParams.shadowMaskStrength
      << "\ncrt_lumgain=" << w.crtParams.luminanceGain
      << "\ncrt_center=" << w.crtParams.centerLighting
      << "\ncrt_gamma=" << w.crtParams.phosphorGamma << "\n";
    if (full) {
        f << "rtc=" << w.rtc << "\nrtc_saved=" << w.rtcSaved << "\n";
        // Dossiers ROM additionnels (0..N) : une ligne kiosk_romdir= par dossier.
        for (const auto& d : w.romDirs) f << "kiosk_romdir=" << d << "\n";
    }
}

// Écriture ATOMIQUE : on rédige un fichier temporaire à côté, on vérifie que tout
// s'est bien écrit, PUIS on renomme par-dessus. Auparavant, ouvrir le flux tronquait
// (O_TRUNC) le fichier AVANT de savoir si on saurait le réécrire, et aucun retour
// n'était testé : disque plein, quota atteint ou coupure au mauvais moment laissaient
// un neost.cfg amputé — réglages CRT, horloge, joymap et TOUS les kiosk_romdir perdus,
// sans le moindre message. L'échec survenait dans le destructeur du flux, hors de
// portée de tout point de contrôle.
// `cwdFallback` autorise le repli historique du neost.cfg vers le répertoire courant
// quand le dossier du dépôt n'est pas inscriptible ; un profil, lui, échoue franchement
// (le message remonte dans l'interface). false ⇒ RIEN n'a été écrit, l'ancien fichier
// est intact, et `err` porte le motif.
bool writeConfigAtomic(const std::string& finalPath, const Config& w, bool full,
                              bool cwdFallback, std::string& err) {
    std::string tmpPath = finalPath + ".tmp";
    // A36 choisit le chemin, mais RIEN ne créait son dossier : quand la règle retient
    // la config utilisateur (~/.config/neost/), ce dossier n'existe pas encore chez un
    // utilisateur neuf et l'ouverture échoue. Mesuré le 2026-09-01 sur le .dmg 0.6
    // MONTÉ (chantier A12) : le bundle est en lecture seule, la règle retombe donc sur
    // la config utilisateur, et le paquet livré annonçait « configuration NOT saved » à
    // CHAQUE lancement — donc aucun réglage conservé, pour tout premier utilisateur.
    // Créer le dossier ici et pas dans `resolve()` : celle-ci est une fonction PURE,
    // c'est ce qui la rend testable (cf. le bandeau de ConfigPath.hpp).
    const std::size_t slash = finalPath.find_last_of("/\\");
    if (slash != std::string::npos) {
        std::error_code mkec;
        fs::create_directories(finalPath.substr(0, slash), mkec);
        // Une erreur ici ne décide de rien : l'ouverture qui suit tranche, et son
        // échec porte déjà le repli puis le message.
    }
    std::ofstream f(tmpPath);
    // Le repli écrase `tmpPath` : sans garder le chemin VOULU, le message d'erreur
    // nomme « neost.cfg.tmp » et cache l'emplacement qui a réellement échoué — c'est
    // ce qui a rendu le défaut ci-dessus illisible depuis le journal.
    const std::string wanted = tmpPath;
    if (!f && cwdFallback) { tmpPath = "neost.cfg.tmp"; f.open(tmpPath); }
    if (!f) {
        err = "cannot write (" + wanted + ")";
        if (tmpPath != wanted) err += " nor (" + tmpPath + ")";
        f.close(); std::error_code rmec; fs::remove(tmpPath, rmec);
        return false;
    }
    writeConfigKeys(f, w, full);
    f.flush();
    const bool ok = f.good();
    f.close();
    if (!ok) {   // le flush a échoué : on garde l'ANCIEN fichier intact
        err = "incomplete write (" + tmpPath + ") — previous file kept";
        std::error_code rmec; fs::remove(tmpPath, rmec);
        return false;
    }
    // Le nom de destination est celui du .tmp amputé de son suffixe : si l'ouverture est
    // retombée sur le dossier courant, le rename doit y rester aussi.
    const std::string dest = tmpPath.substr(0, tmpPath.size() - 4);
    std::error_code mvec;
    fs::rename(tmpPath, dest, mvec);
    if (mvec) {
        err = "cannot replace (" + tmpPath + " → " + dest + "): " + mvec.message();
        std::error_code rmec; fs::remove(tmpPath, rmec);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// PROFILS DE RÉGLAGES NOMMÉS — dossier profiles/, un fichier .cfg par profil, au
// MÊME format que neost.cfg (cf. writeConfigKeys/parseConfigLine). neost.cfg reste
// la configuration COURANTE, écrite automatiquement à chaque changement ; un profil
// est une photo nommée qu'on rappelle plus tard (« 520 ST + TOS 1.02 + ma démo »).
// Charger un profil = repartir de la config courante et lui appliquer les lignes du
// fichier : ce qu'un profil ne dit pas ne change pas. Réservé à l'interface (le
// headless n'a pas de notion de profil) → sous garde ImGui pour ne pas laisser de
// fonctions inutilisées dans une compilation sans GUI.
// ─────────────────────────────────────────────────────────────────────────────
// Dossier des profils : à côté de neost.cfg. Quand ce dossier-là n'est pas inscriptible
// (installation en lecture seule), writeConfigAtomic replie neost.cfg sur le répertoire
// COURANT — on suit le même repli, sinon les profils seraient la seule chose cassée là où
// la configuration, elle, fonctionne. Résolu UNE fois, après la première écriture de
// neost.cfg (c'est elle qui tranche l'emplacement) : cf. l'appelant. Ne CRÉE rien ; seul
// l'enregistrement d'un profil crée le dossier.
std::string profilesDir(const std::string& exeDir) {
    // A36 : les profils suivent le neost.cfg RETENU — sinon on écrirait des profils
    // que la config ne retrouverait pas (config utilisateur d'un côté, profils à
    // côté du binaire de l'autre). Un dossier `profiles` déjà utilisé à côté du
    // binaire garde la priorité : on ne déplace pas les profils de quelqu'un.
    const std::string legacy = exeDir + "/../profiles";
    std::error_code ec;
    if (fs::is_directory(legacy, ec)) return legacy;
    return neost::cfgpath::profilesDirFor(cfgPath(exeDir));
}

// Nom saisi → nom de FICHIER sûr. Le champ est libre : sans ce filtre, « ../neost.cfg »
// ou un nom contenant « / » écrirait HORS du dossier des profils. On ne garde donc pas
// une liste blanche de lettres (elle mangerait les accents, « Démos » → « Dmos ») : on
// retire les séparateurs de chemin, les caractères de contrôle et les réservés Windows,
// puis les points et espaces de bord (« .. », fichiers cachés, noms refusés par Windows).
// Renvoie "" si rien d'utilisable ne reste — l'appelant refuse alors d'écrire.
std::string profileFileName(const std::string& in) {
    static const std::string kBanned = "/\\:*?\"<>|";
    std::string out;
    for (unsigned char ch : in) {
        if (ch < 0x20 || ch == 0x7f) continue;
        if (kBanned.find(char(ch)) != std::string::npos) continue;
        out += char(ch);
    }
    while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
    while (!out.empty() && (out.back()  == ' ' || out.back()  == '.')) out.pop_back();
    if (out.size() > 64) out.resize(64);
    return out;
}

// Profils présents, triés sans tenir compte de la casse. Itération MANUELLE comme les
// pages Disquettes/Cartouche : l'incrément d'un range-for LANCE filesystem_error si le
// dossier devient illisible en cours de parcours, et rien ne l'attrape ici.
std::vector<std::string> listProfiles(const std::string& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        std::error_code ec2;
        if (it->is_regular_file(ec2) && it->path().extension() == ".cfg")
            names.push_back(it->path().stem().string());
        it.increment(ec);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](unsigned char x, unsigned char y) {
                                                return std::tolower(x) < std::tolower(y);
                                            });
    });
    return names;
}

bool saveProfile(const std::string& dir, const std::string& name,
                        const Config& c, std::string& err) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) { err = "cannot create " + dir; return false; }
    return writeConfigAtomic(dir + "/" + name + ".cfg", c, /*full=*/false, /*cwdFallback=*/false, err);
}

// Applique le profil PAR-DESSUS `c` (déjà rempli avec la config courante) : les clés
// qu'un profil n'écrit pas (horloge, disposition de l'interface) restent telles quelles.
bool loadProfileInto(const std::string& dir, const std::string& name, Config& c) {
    std::ifstream f(dir + "/" + name + ".cfg");
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) parseConfigLine(c, line);
    return true;
}

bool deleteProfile(const std::string& dir, const std::string& name) {
    std::error_code ec;
    return fs::remove(fs::path(dir) / (name + ".cfg"), ec) && !ec;
}

}  // namespace neost::appconfig
