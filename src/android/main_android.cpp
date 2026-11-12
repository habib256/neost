// =============================================================================
//  main_android.cpp — Frontend Android de NeoST (SDL2 + OpenGL ES 2).
//
//  C'est le TROISIÈME frontend, et il est calqué sur le WEB (main_web.cpp), pas
//  sur le GUI de bureau : même sortie GLES2 (le mode immédiat de main.cpp n'existe
//  pas ici), même modèle audio « push » (le son est produit PAR TRAME ÉMULÉE via
//  core/AudioMix.cpp), même cadence sur le TEMPS ÉMULÉ. La coque d'interface est
//  faite par la plateforme — HTML côté web, Android côté ici.
//
//  SDL2 fournit ce que GLFW ne sait pas faire sur Android : fenêtre, contexte
//  GLES, cycle de vie (mise en veille), tactile, manettes et sortie audio.
//
//  Données : le paquet embarque EmuTOS + une disquette dans ses `assets`, qu'on
//  déballe au PREMIER lancement dans le stockage interne de l'application. Le
//  cœur ne voit ensuite que des chemins de fichiers ordinaires.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Pacing.hpp"
#include <SDL.h>
#include <SDL_opengles2.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>          // mkdir : le déballage vit dans un sous-dossier
#include <cstring>
#include <string>
#include <vector>

#include "android/AndroidMenu.hpp"
#include "android/AndroidRoms.hpp"
#include "audio/SampleRing.hpp"
#include "core/AudioMix.hpp"
#include "core/Machine.hpp"
#include "core/Rewind.hpp"
#include "io/DongleTable.hpp"   // dongles.cfg (format partagé avec le web)
#include "gui/StKeys.hpp"      // tables symboliques par pays (StKeysSymbolic, sans GLFW)
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

namespace {

// --- Journal : passe par le logcat Android (SDL_Log), pas par stderr ---------
#define NEOLOG(...) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define NEOERR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)

Machine*      g_machine = nullptr;
SDL_Window*   g_window  = nullptr;
SDL_GLContext g_gl      = nullptr;

// --- Vidéo : shader GLES2, repris tel quel du frontend web -------------------
// Le framebuffer du Shifter est ARGB8888 ; téléversé en GL_RGBA/UNSIGNED_BYTE,
// les octets little-endian se lisent B,G,R,A → on rétablit l'ordre des canaux
// dans le fragment shader (.bgr) plutôt que de recopier la trame.
GLuint g_tex = 0, g_prog = 0, g_vbo = 0;
GLint  g_locPos = -1, g_locUV = -1, g_locTex = -1;
int    g_texW = 0, g_texH = 0;

const char* kVert =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

const char* kFrag =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "void main() { gl_FragColor = vec4(texture2D(uTex, vUV).bgr, 1.0); }\n";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        NEOERR("shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool initGl() {
    const GLuint vs = compile(GL_VERTEX_SHADER, kVert);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) return false;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { NEOERR("program link failed"); return false; }
    g_locPos = glGetAttribLocation(g_prog, "aPos");
    g_locUV  = glGetAttribLocation(g_prog, "aUV");
    g_locTex = glGetUniformLocation(g_prog, "uTex");

    // Quad plein écran : x, y, u, v. L'image ST se lit du HAUT vers le bas, donc
    // v est inversé par rapport au repère GL.
    static const GLfloat quad[] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f,
    };
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    return true;
}

void uploadFrame(const uint32_t* px, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, g_tex);
    if (w != g_texW || h != g_texH) {       // (ré)allocation au changement de résolution ST
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        g_texW = w; g_texH = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
}

// Dessine l'écran ST CENTRÉ, ratio conservé (letterbox) : un téléphone est bien
// plus large que 4/3, étirer l'image déformerait les pixels carrés du ST.
void drawScreen() {
    int fbw = 0, fbh = 0;
    SDL_GL_GetDrawableSize(g_window, &fbw, &fbh);
    glClear(GL_COLOR_BUFFER_BIT);
    if (g_texW <= 0 || g_texH <= 0 || fbw <= 0 || fbh <= 0) return;

    const float srcAspect = float(g_texW) / float(g_texH);
    const float dstAspect = float(fbw) / float(fbh);
    int vpW = fbw, vpH = fbh;
    if (dstAspect > srcAspect) vpW = int(float(fbh) * srcAspect);   // barres verticales
    else                       vpH = int(float(fbw) / srcAspect);   // barres horizontales
    glViewport((fbw - vpW) / 2, (fbh - vpH) / 2, vpW, vpH);

    glUseProgram(g_prog);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(GLuint(g_locPos));
    glVertexAttribPointer(GLuint(g_locPos), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
    glEnableVertexAttribArray(GLuint(g_locUV));
    glVertexAttribPointer(GLuint(g_locUV), 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (const void*)(2 * sizeof(GLfloat)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(g_locTex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// --- Audio : modèle « push », même discipline que le GUI et le web -----------
// Le producteur (boucle d'émulation) génère le son d'UNE trame et le pousse dans
// l'anneau ; le callback SDL (thread audio) ne fait que drainer. Amorçage avant
// lecture : jouer un anneau quasi vide, c'est l'underrun permanent où seules les
// transitoires passent.
neost::FrameMixBuffers g_mixBuf;
SampleRing             g_ring{32768};        // entrelacé L/R : ~340 ms à 48 kHz
SDL_AudioDeviceID      g_audioDev  = 0;
uint32_t               g_audioRate = 0;
neost::pacing::AudioPacer g_pacer;      // A28 : report + servo + rampe, partagés
uint32_t               g_cushionFrames = 0;
bool                   g_primed = false;          // appartient au THREAD AUDIO
std::atomic<uint32_t>  g_underruns{0};            // écrit thread audio, lu principal
float                  g_masterVol = 1.0f;
uint32_t               g_underrunSeen = 0;        // diagnostic « son haché » (thread principal)
int                    g_underrunMute = 0;

void audioCallback(void* /*ud*/, Uint8* stream, int len) {
    float* out = reinterpret_cast<float*>(stream);
    const size_t need = size_t(len) / sizeof(float);       // floats (2 par frame)
    if (!g_primed) {
        if (g_ring.available() < size_t(g_cushionFrames) * 2) { std::memset(stream, 0, size_t(len)); return; }
        g_primed = true;
    }
    if (g_ring.pull(out, need) < need) {                    // underrun → on reconstitue le coussin
        g_primed = false;
        g_underruns.fetch_add(1, std::memory_order_relaxed);
    }
}

void produceAudioFrame() {
    Machine& m = *g_machine;
    const int64_t fc = m.frameCycles();
    if (!g_audioDev || g_audioRate == 0) {                  // pas de sortie : on DRAINE les horodatages
        neost::mixEmulatedFrame(m.psg, &m.dmasnd, false, 0, 0, fc, g_mixBuf);
        return;
    }
    // A28 : report fractionnaire + asservissement proportionnel PARTAGÉS
    // (core/Pacing.hpp) — c'était la troisième copie du même calcul.
    // g_ring.available() est en FLOATS (entrelacé) → ÷2 pour comparer aux FRAMES.
    const int n = g_pacer.samplesForFrame(fc, g_audioRate,
                                          int(g_cushionFrames), int(g_ring.available() / 2));
    if (n <= 0) { neost::mixEmulatedFrame(m.psg, &m.dmasnd, false, 0, 0, fc, g_mixBuf); return; }

    float* st = neost::mixEmulatedFrame(m.psg, &m.dmasnd, machineHasDmaSound(m.bus.machine),
                                        uint32_t(n), g_audioRate, fc, g_mixBuf);
    if (!st) return;
    g_pacer.applyMasterVolume(st, n, g_masterVol);             // rampe anti-clic
    neost::pacing::AudioPacer::clampStereo(st, n);
    g_ring.push(st, size_t(2 * n));

    // Diagnostic « son haché », comme le natif : un underrun isolé est bénin
    // (chargement, rotation d'écran) ; RÉPÉTÉ, c'est que l'appareil ne tient pas
    // la cadence des trames. Limité à ~1 message / 5 s.
    // (Globales et non `static` locales : une relance dans le même processus doit
    // pouvoir les remettre à zéro, cf. resetSessionGlobals.)
    if (g_underrunMute > 0) --g_underrunMute;
    const uint32_t u = g_underruns.load(std::memory_order_relaxed);
    if (u != g_underrunSeen && g_underrunMute <= 0) {
        NEOERR("audio ring underrun (total %u) — emulation not keeping up", u);
        g_underrunSeen = u;
        g_underrunMute = 250;                         // ≈ 5 s à 50 trames/s
    }
}

bool openAudio() {
    SDL_AudioSpec want{}, have{};
    want.freq     = 48000;
    want.format   = AUDIO_F32SYS;      // on synthétise en float
    want.channels = 2;                 // STÉRÉO : DMA STE L/R + panoramique LMC1992
    want.samples  = 1024;              // ~21 ms : compromis latence / robustesse
    want.callback = audioCallback;
    g_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                                     SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!g_audioDev) { NEOERR("SDL_OpenAudioDevice: %s", SDL_GetError()); return false; }
    g_audioRate     = uint32_t(have.freq);
    g_cushionFrames = g_audioRate * 90 / 1000;   // 90 ms, comme le web
    SDL_PauseAudioDevice(g_audioDev, 0);
    NEOLOG("audio out: %u Hz stereo, cushion 90 ms (%u frames), buffer %u",
           g_audioRate, g_cushionFrames, have.samples);
    return true;
}

// --- Données : déballage des assets au premier lancement ---------------------
// SDL_RWops sait lire dans l'APK ; le cœur, lui, veut des fichiers ordinaires.
// ⚠ CHAQUE lecture et CHAQUE écriture est vérifiée, et l'échec DÉTRUIT la destination.
// Auparavant : ouvrir en "wb" crée le fichier à 0 octet, `SDL_RWwrite` n'était pas
// contrôlé, et la fonction rendait `true` inconditionnellement. Un stockage plein ou
// un processus tué au mauvais moment laissait donc un TOS tronqué que `fileExists()`
// — qui ne teste que l'ouverture — déclarait « déjà déballé » POUR TOUJOURS : plus
// aucun lancement ne le recopiait, et l'application démarrait sans TOS à chaque fois.
// Un fichier partiel est pire que pas de fichier : celui-ci se répare tout seul.

// Lit un asset de l'APK en entier ; false (et `out` vide) s'il est absent, vide ou
// lu partiellement.
bool readAsset(const std::string& name, std::vector<uint8_t>& out) {
    out.clear();
    SDL_RWops* in = SDL_RWFromFile(name.c_str(), "rb");
    if (!in) { NEOERR("asset absent: %s (%s)", name.c_str(), SDL_GetError()); return false; }
    const Sint64 sz = SDL_RWsize(in);
    if (sz <= 0) { NEOERR("asset vide ou illisible: %s", name.c_str()); SDL_RWclose(in); return false; }
    // static_cast et non size_t(sz) : `std::vector<uint8_t> buf(size_t(sz));`
    // est le « most vexing parse » — le compilateur y lit la DÉCLARATION d'une
    // fonction `buf`, pas une variable.
    out.resize(static_cast<size_t>(sz));
    const size_t got = SDL_RWread(in, out.data(), 1, out.size());
    SDL_RWclose(in);
    if (got != out.size()) {
        NEOERR("asset lu partiellement: %s (%zu/%zu)", name.c_str(), got, out.size());
        out.clear();
        return false;
    }
    return true;
}

// Écrit `buf` dans `dest` de façon ATOMIQUE : fichier temporaire puis rename(), qui
// remplace la destination d'un coup sous POSIX. Un processus tué en pleine copie ne
// laisse donc plus jamais un fichier tronqué SOUS LE NOM FINAL — seulement un .tmp
// orphelin, réécrit au lancement suivant. C'est ce qui permet de tester la seule
// EXISTENCE de diskA.st (cf. prepareData) sans la prendre pour un fichier abîmé.
bool writeFileAtomic(const std::string& dest, const std::vector<uint8_t>& buf) {
    const std::string tmp = dest + ".tmp";
    SDL_RWops* out = SDL_RWFromFile(tmp.c_str(), "wb");
    if (!out) { NEOERR("écriture impossible: %s", tmp.c_str()); return false; }
    const size_t put = SDL_RWwrite(out, buf.data(), 1, buf.size());
    const bool closed = SDL_RWclose(out) == 0;
    if (put != buf.size() || !closed) {
        NEOERR("écriture partielle: %s (%zu/%zu) — fichier supprimé", tmp.c_str(), put, buf.size());
        std::remove(tmp.c_str());      // ne JAMAIS laisser un fichier tronqué derrière soi
        return false;
    }
    if (std::rename(tmp.c_str(), dest.c_str()) != 0) {
        NEOERR("renommage impossible: %s -> %s", tmp.c_str(), dest.c_str());
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool copyAsset(const std::string& name, const std::string& dest) {
    std::vector<uint8_t> buf;
    return readAsset(name, buf) && writeFileAtomic(dest, buf);
}

// Empreinte FNV-1a 64 bits — pas cryptographique, il ne s'agit que de savoir si
// les octets embarqués ont changé d'un APK à l'autre.
uint64_t fnv1a(uint64_t h, const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x100000001B3ull; }
    return h;
}

// Contenu d'un petit fichier texte (le témoin, rom.cfg, dongles.cfg), ou "" s'il est
// absent. 512 octets : dongles.cfg plein (clé + cinq ports) en fait un peu plus de 100.
std::string readSmallFile(const std::string& p) {
    std::string s;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        char b[512];
        const size_t n = std::fread(b, 1, sizeof b, f);
        std::fclose(f);
        s.assign(b, n);
    }
    return s;
}

// Taille d'un fichier ordinaire, ou -1 s'il n'existe pas.
long long fileSize(const std::string& p) {
    struct stat st {};
    if (::stat(p.c_str(), &st) != 0) return -1;
    return static_cast<long long>(st.st_size);
}

// Renvoie le dossier de données de l'application (créé au besoin), assets déballés.
std::string prepareData() {
#if defined(__ANDROID__)
    const char* internal = SDL_AndroidGetInternalStoragePath();
    const std::string root = internal ? std::string(internal) : std::string(".");
#else
    const std::string root = ".";
#endif
    // ⚠ SOUS-DOSSIER, et c'est la clé du contrôle de taille ci-dessous. Sous Android,
    // `SDL_RWFromFile` avec un nom RELATIF cherche D'ABORD dans le stockage interne et
    // ne retombe sur l'AssetManager de l'APK qu'en cas d'échec (SDL 2.30.9,
    // src/file/SDL_rwops.c). En déballant à la RACINE du stockage interne, « lire
    // l'asset » revenait donc à relire le fichier DÉJÀ DÉBALLÉ : la garde comparait le
    // fichier à lui-même et valait toujours vrai — le contrôle de taille était un
    // no-op, et un fichier de 0 octet faisait même sauter la branche en silence.
    // Déballer AILLEURS rend au nom relatif son sens : celui de l'asset.
    const std::string dir = root + "/data";
    ::mkdir(dir.c_str(), 0700);        // déjà là = EEXIST, sans conséquence
    // Deux familles, traitées différemment (cf. plus bas) :
    //   · LECTURE SEULE — les ROM EmuTOS : rien ne les modifie, un APK plus récent
    //     doit pouvoir les remplacer ;
    //   · ÉCRITE PAR L'UTILISATEUR — diskA.st : le FDC y recopie chaque secteur écrit
    //     (write-through), c'est donc SA disquette dès le premier lancement.
    // Les ROM : la table de AndroidRoms.hpp, la même que la page ROM du menu.
    std::vector<const char*> kRomAssets;
    for (const neost::AndroidRom& r : neost::kAndroidRoms) kRomAssets.push_back(r.file);
    static const char* kUserAssets[] = { "diskA.st" };
    std::vector<const char*> kAssets(kRomAssets);   // tous, pour la purge des reliques
    for (const char* a : kUserAssets) kAssets.push_back(a);
    // ⚠ PURGER LES RELIQUES DE LA RACINE, et pas seulement déballer ailleurs. Le
    // sous-dossier déplace la DESTINATION, mais c'est le nom SOURCE qui est relatif :
    // tant qu'un fichier du même nom traîne à la racine du stockage interne — déballé
    // là par une version antérieure —, SDL le trouve AVANT l'AssetManager et
    // « l'asset » reste ce fichier périmé. Une relique de 0 octet suffit alors à
    // rendre l'application définitivement sans TOS, en silence : `want` vaut 0, la
    // condition `want > 0` est fausse, et la branche entière est sautée sans même le
    // message d'erreur. Les supprimer rend au nom relatif son sens, DÉFINITIVEMENT —
    // c'est un fichier que nous avons écrit nous-mêmes, jamais un fichier utilisateur.
    for (const char* a : kAssets) std::remove((root + "/" + a).c_str());

    // ROM : TÉMOIN DE VERSION. Comparer la seule taille (version précédente) ne
    // voyait jamais une mise à jour : un EmuTOS corrigé a la MÊME taille (192 ou
    // 256 Ko), et l'APK neuf gardait donc l'ancienne ROM déballée pour toujours.
    // Le témoin porte une EMPREINTE DU CONTENU embarqué (noms, tailles, octets) —
    // et non l'identité de build (src/BuildInfo.hpp) : neost_build_info n'est pas
    // lié à la bibliothèque Android (CMakeLists.txt, branche if(ANDROID)), et
    // l'empreinte change exactement quand les octets changent, ni plus ni moins.
    // Coût : relire ~900 Ko d'assets à chaque lancement, quelques millisecondes.
    const std::string stampPath = dir + "/.assets-stamp";
    std::vector<std::vector<uint8_t>> roms(kRomAssets.size());
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < roms.size(); ++i) {
        readAsset(kRomAssets[i], roms[i]);           // absent → vide, compté tel quel
        const uint64_t n = roms[i].size();
        h = fnv1a(h, kRomAssets[i], std::strlen(kRomAssets[i]) + 1);
        h = fnv1a(h, &n, sizeof n);
        h = fnv1a(h, roms[i].data(), roms[i].size());
    }
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%016llx\n", static_cast<unsigned long long>(h));
    const bool stampSame = (readSmallFile(stampPath) == stamp);
    bool allOk = true;
    for (size_t i = 0; i < roms.size(); ++i) {
        if (roms[i].empty()) { allOk = false; continue; }   // asset manquant : déjà signalé
        const std::string dest = dir + "/" + kRomAssets[i];
        // Témoin identique ET fichier de la bonne taille → rien à faire. La taille
        // reste vérifiée : un fichier effacé ou abîmé se répare au lancement suivant.
        if (stampSame && fileSize(dest) == static_cast<long long>(roms[i].size())) continue;
        if (!writeFileAtomic(dest, roms[i])) {
            allOk = false;
            NEOERR("déballage échoué: %s — l'application démarrera sans lui", kRomAssets[i]);
        }
    }
    // Le témoin n'est posé qu'après un déballage COMPLET : un échec en cours de route
    // laisse l'ancien témoin, donc tout sera retenté au prochain lancement.
    if (allOk && !stampSame) {
        const std::vector<uint8_t> s(stamp, stamp + std::strlen(stamp));
        if (!writeFileAtomic(stampPath, s)) NEOERR("témoin non écrit: %s", stampPath.c_str());
        else NEOLOG("ROMs unpacked (assets %s)", std::string(stamp, 16).c_str());
    }

    // DISQUETTE : JAMAIS écrasée si elle existe. Elle porte les écritures de
    // l'utilisateur (sauvegardes de jeu, fichiers copiés depuis le bureau GEM) ; un
    // APK neuf qui embarquerait une autre diskA.st ne doit pas les effacer. Seuls
    // l'absence et le fichier VIDE déclenchent la copie — la copie étant atomique
    // (writeFileAtomic), un fichier tronqué ne peut plus apparaître sous ce nom.
    for (const char* a : kUserAssets) {
        const std::string dest = dir + "/" + a;
        if (fileSize(dest) > 0) continue;
        if (!copyAsset(a, dest))
            NEOERR("déballage échoué: %s — l'application démarrera sans lui", a);
    }
    return dir;
}

// Dossier d'IMPORT de l'utilisateur : le stockage « externe » PROPRE à
// l'application (…/Android/data/<paquet>/files). Contrairement au stockage
// interne, il est joignable depuis un ordinateur par USB (MTP) et ne demande
// AUCUNE permission ; SDL le crée au premier appel. "" s'il est indisponible
// (carte retirée, stockage partagé démonté).
std::string importDir() {
#if defined(__ANDROID__)
    if (!(SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_READ)) return {};
    const char* ext = SDL_AndroidGetExternalStoragePath();
    return ext ? std::string(ext) : std::string();
#else
    return {};
#endif
}

// --- Entrées tactiles --------------------------------------------------------
// Modèle v1, volontairement simple et testable :
//   · un doigt qui GLISSE  → souris relative (le bureau GEM se pilote ainsi) ;
//   · un appui BREF sans déplacement → clic gauche ;
//   · deux doigts, appui bref → clic droit ;
//   · manette physique (SDL_GameController) → joystick ST du port 1.
// Le stick virtuel viendra avec la couche d'interface — pas avant d'avoir mesuré
// ce que donne la souris relative sur un vrai appareil.
// ÉTAT PAR DOIGT, et non un état global. La v1 gardait UN seul couple lastX/lastY
// pour tout le geste ; trois défauts distincts en sont sortis, corrigés un par un
// avant d'admettre que le modèle lui-même était faux :
//   · le compteur de doigts lu APRÈS décrément rendait le clic droit inatteignable ;
//   · le mouvement d'un SECOND doigt se diffait contre la position du PREMIER, donc
//     calculait l'écart ENTRE LES DOIGTS (mesuré : 20× le seuil de tap, 360 px de
//     souris parasites) — et une paume posée tuait aussi le clic gauche ;
//   · filtrer sur un doigt « primaire » sans jamais réarmer son id figeait la souris
//     dès que ce doigt se levait le premier, et Android recyclant le plus petit id
//     libre, l'écart entre doigts revenait par la bande.
// Chaque doigt porte donc SA position. Un mouvement se diffe contre la position du
// MÊME doigt : l'écart entre deux doigts n'est plus représentable. Quand le doigt
// qui pilote se lève, un autre prend le relais AVEC SA PROPRE position, donc sans
// saut. Un id recyclé arrive par FINGERDOWN et sème sa position : rien ne survit.
struct Touch {
    static constexpr int kMax = 8;               // au-delà, le doigt est ignoré
    struct Finger { SDL_FingerID id; float x, y; bool used; };
    Finger   f[kMax] = {};
    int      count    = 0;         // doigts actuellement posés
    int      peak     = 0;         // maximum atteint pendant le geste (→ clic droit)
    bool     active   = false;     // un geste est en cours
    Uint64   startMs  = 0;
    float    travel   = 0.0f;      // distance parcourue par le doigt PILOTE
    float    remX     = 0.0f;      // fraction de pixel ST non encore envoyée
    float    remY     = 0.0f;      //   (un glissé lent ne produisait que des 0)
    SDL_FingerID mover = 0;        // doigt qui pilote la souris
    bool     moverOk  = false;     // ... et s'il est encore posé

    int slotOf(SDL_FingerID id) const {
        for (int i = 0; i < kMax; ++i) if (f[i].used && f[i].id == id) return i;
        return -1;
    }
    int freeSlot() const {
        for (int i = 0; i < kMax; ++i) if (!f[i].used) return i;
        return -1;
    }
    // Le pilote vient de partir : en promouvoir un autre, avec SA position — c'est
    // ce qui évite le saut. Aucun doigt restant → plus de pilote.
    void promote() {
        for (int i = 0; i < kMax; ++i) if (f[i].used) { mover = f[i].id; moverOk = true; return; }
        moverOk = false;
    }
};
Touch g_touch;

// Menu (grammaire du menu borne — cf. android/AndroidMenu.hpp) et échelle d'UI.
neost::AndroidMenu g_menu;
float              g_uiScale = 1.0f;
// Injection touche/clic de la page clavier — recette de la BORNE (main.cpp,
// g_kioskInjectHold) : MAKE (ou appui souris) MAINTENU 4 trames puis BREAK, et
// AUCUNE nouvelle injection tant que le maintien court — sinon un 2ᵉ tap < 4
// trames écrase le relâchement en attente et la touche reste « collée » côté ST.
// Le premier jet faisait le clic down+up DANS LA MÊME TRAME : un jeu qui scrute
// l'état du bouton chaque VBL pouvait ne jamais le voir.
int                g_injectHold     = 0;      // trames de maintien restantes
uint8_t            g_injectScancode = 0;      // touche à relâcher (0 = clic)
bool               g_injectClick    = false;  // le maintien est un clic souris
// Boutons TENUS par l'injection en cours. Un glissé pendant le maintien doit les
// transmettre : Ikbd::mouseEvent écrase l'état des boutons à chaque appel, et un
// mouvement envoyé avec (false, false) relâchait le clic avant la 4ᵉ trame — un
// double-tap d'un doigt qui tremble ne donnait plus qu'un clic.
bool               g_injectL        = false;
bool               g_injectR        = false;

// Pose un clic maintenu (tap, ou page clavier) ; relâché par la boucle de trame.
void injectClick(Machine& m, bool right) {
    m.mouseEvent(0, 0, !right, right);
    g_injectScancode = 0;
    g_injectClick    = true;
    g_injectL        = !right;
    g_injectR        = right;
    g_injectHold     = 4;
}

// ─── Clavier PHYSIQUE (USB/Bluetooth) ────────────────────────────────────────
// Jusqu'au 2026-11-12, SDL_KEYDOWN ne servait qu'au bouton RETOUR : un clavier
// branché sur la tablette était ignoré. Même recette que le bureau (gui/StKeys) et
// qu'Hatari : SYMBOLIQUE d'abord — le keycode SDL d'une touche imprimable EST le
// caractère de la disposition hôte, exactement ce que mangent les tables par pays
// (StKeysSymbolic, sans GLFW) — puis POSITIONNEL par le scancode SDL.
// [D] Hatari src/sdl/keymap.c:424-560 (Keymap_PcToStScanCode) @ f0736b24, avec les
//     choix du bureau NeoST là où ils diffèrent (Fin = Undo, Impr = Help, AltGr = Alt).
uint8_t positionalForSdl(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) {
        static const uint8_t az[26] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
        return az[s - SDL_SCANCODE_A];
    }
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return uint8_t(0x02 + (s - SDL_SCANCODE_1));
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return uint8_t(0x3B + (s - SDL_SCANCODE_F1));
    switch (s) {
        case SDL_SCANCODE_ESCAPE:       return 0x01;
        case SDL_SCANCODE_MINUS:        return 0x0C;
        case SDL_SCANCODE_EQUALS:       return 0x0D;
        case SDL_SCANCODE_BACKSPACE:    return 0x0E;
        case SDL_SCANCODE_TAB:          return 0x0F;
        case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
        case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
        case SDL_SCANCODE_RETURN:       return 0x1C;
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D;
        case SDL_SCANCODE_SEMICOLON:    return 0x27;
        case SDL_SCANCODE_APOSTROPHE:   return 0x28;
        case SDL_SCANCODE_GRAVE:        return 0x29;
        case SDL_SCANCODE_LSHIFT:       return 0x2A;
        case SDL_SCANCODE_BACKSLASH:    return 0x2B;
        case SDL_SCANCODE_NONUSHASH:    return 0x2B;
        case SDL_SCANCODE_COMMA:        return 0x33;
        case SDL_SCANCODE_PERIOD:       return 0x34;
        case SDL_SCANCODE_SLASH:        return 0x35;
        case SDL_SCANCODE_RSHIFT:       return 0x36;
        case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
        case SDL_SCANCODE_SPACE:        return 0x39;
        case SDL_SCANCODE_CAPSLOCK:     return 0x3A;
        case SDL_SCANCODE_HOME:         return 0x47;
        case SDL_SCANCODE_UP:           return 0x48;
        case SDL_SCANCODE_LEFT:         return 0x4B;
        case SDL_SCANCODE_RIGHT:        return 0x4D;
        case SDL_SCANCODE_DOWN:         return 0x50;
        case SDL_SCANCODE_INSERT:       return 0x52;
        case SDL_SCANCODE_DELETE:       return 0x53;
        case SDL_SCANCODE_NONUSBACKSLASH: return 0x60;
        case SDL_SCANCODE_END:          return 0x61;   // Undo (bureau NeoST)
        case SDL_SCANCODE_UNDO:         return 0x61;
        case SDL_SCANCODE_PRINTSCREEN:  return 0x62;   // Help
        case SDL_SCANCODE_HELP:         return 0x62;
        case SDL_SCANCODE_PAGEUP:       return 0x63;   // ( du pavé ST
        case SDL_SCANCODE_PAGEDOWN:     return 0x64;   // ) du pavé ST
        case SDL_SCANCODE_KP_DIVIDE:    return 0x65;
        case SDL_SCANCODE_KP_MULTIPLY:  return 0x66;
        case SDL_SCANCODE_KP_7: return 0x67; case SDL_SCANCODE_KP_8: return 0x68;
        case SDL_SCANCODE_KP_9: return 0x69; case SDL_SCANCODE_KP_4: return 0x6A;
        case SDL_SCANCODE_KP_5: return 0x6B; case SDL_SCANCODE_KP_6: return 0x6C;
        case SDL_SCANCODE_KP_1: return 0x6D; case SDL_SCANCODE_KP_2: return 0x6E;
        case SDL_SCANCODE_KP_3: return 0x6F; case SDL_SCANCODE_KP_0: return 0x70;
        case SDL_SCANCODE_KP_PERIOD:    return 0x71;
        case SDL_SCANCODE_KP_ENTER:     return 0x72;
        case SDL_SCANCODE_KP_MINUS:     return 0x4A;
        case SDL_SCANCODE_KP_PLUS:      return 0x4E;
        default:                        return 0;
    }
}

uint8_t stScancodeForSdl(const SDL_Keysym& k) {
    // Imprimable : le keycode SDL est le point de code Unicode de la touche (sans
    // Maj) dans la disposition hôte. Le pavé numérique a ses propres keycodes
    // (SDLK_KP_*, hors de cette plage) et passe donc en positionnel.
    if ((k.sym > 0x20 && k.sym < 0x7F) || (k.sym >= 0xA0 && k.sym < 0x10000)) {
        const uint8_t sc = neost::stkeys::symbolicForCountry(uint32_t(k.sym));
        if (sc != 0xFF) return sc;
    }
    return positionalForSdl(k.scancode);
}
// Scancode ST envoyé au MAKE, par scancode SDL : le BREAK renvoie le MÊME (même si
// la disposition ou le TOS a changé entre-temps), et une touche relâchée sans MAKE
// connu (enfoncée menu ouvert, ou avant un RESTART) n'envoie rien.
uint8_t g_physMake[SDL_NUM_SCANCODES] = {};

// L'état de la machine vient d'être REMPLACÉ (RESTART, retour arrière) : la relâche
// différée d'une frappe injectée n'a plus de sens. Envoyée quand même, elle livrait
// un BREAK orphelin au TOS fraîchement redémarré (ou à l'instant restauré, où la
// touche n'était pas enfoncée). Idem pour les touches physiques tenues.
void cancelPendingInput() {
    g_injectHold     = 0;
    g_injectScancode = 0;
    g_injectClick    = false;
    g_injectL = g_injectR = false;
    for (auto& m : g_physMake) m = 0;
}

constexpr float kTouchSpeed   = 900.0f;   // pixels ST par unité d'écran normalisée
constexpr Uint64 kTapMaxMs    = 250;
constexpr float  kTapMaxTravel = 0.02f;

void handleTouch(const SDL_Event& e) {
    switch (e.type) {   // (non appelé quand le menu est ouvert : ImGui a la main)
    case SDL_FINGERDOWN: {
        int k = g_touch.slotOf(e.tfinger.fingerId);      // id recyclé encore listé ?
        if (k < 0) k = g_touch.freeSlot();
        if (k < 0) break;                                // plus de place : doigt ignoré
        if (!g_touch.f[k].used) ++g_touch.count;
        g_touch.f[k] = { e.tfinger.fingerId, e.tfinger.x, e.tfinger.y, true };
        if (g_touch.count > g_touch.peak) g_touch.peak = g_touch.count;
        if (!g_touch.active) {
            g_touch.active  = true;
            g_touch.startMs = SDL_GetTicks64();
            g_touch.travel  = 0.0f;
        }
        if (!g_touch.moverOk) { g_touch.mover = e.tfinger.fingerId; g_touch.moverOk = true; }
        break;
    }
    case SDL_FINGERMOTION: {
        const int k = g_touch.slotOf(e.tfinger.fingerId);
        if (k < 0) break;                                // doigt inconnu (DOWN manqué)
        const float dx = e.tfinger.x - g_touch.f[k].x;   // contre SA position à lui
        const float dy = e.tfinger.y - g_touch.f[k].y;
        g_touch.f[k].x = e.tfinger.x;
        g_touch.f[k].y = e.tfinger.y;
        // Seul le PILOTE déplace la souris et compte dans « travel » ; les autres
        // doigts ne servent qu'à compter pour le clic droit.
        if (!g_touch.moverOk || e.tfinger.fingerId != g_touch.mover) break;
        g_touch.travel += SDL_fabsf(dx) + SDL_fabsf(dy);
        // x et y sont normalisés SÉPARÉMENT (largeur, hauteur) : sur un écran 20:9 en
        // paysage le même geste allait ~2,2 fois plus vite en vertical. On ramène dy à
        // l'unité de dx (largeur de fenêtre) pour qu'un centimètre vaille autant partout.
        float aspect = 1.0f;
        int ww = 0, wh = 0;
        if (g_window) SDL_GetWindowSize(g_window, &ww, &wh);
        if (ww > 0 && wh > 0) aspect = float(wh) / float(ww);
        // Accumulateur fractionnaire : la troncature par événement perdait le reste, et
        // un doigt lent (< 1 pixel ST par événement à 120 Hz) ne déplaçait rien.
        g_touch.remX += dx * kTouchSpeed;
        g_touch.remY += dy * kTouchSpeed * aspect;
        const int mx = int(g_touch.remX);
        const int my = int(g_touch.remY);
        g_touch.remX -= float(mx);
        g_touch.remY -= float(my);
        // Boutons : ceux que l'injection d'un tap tient encore (cf. g_injectL/R).
        if ((mx || my) && g_machine) g_machine->mouseEvent(mx, my, g_injectL, g_injectR);
        break;
    }
    case SDL_FINGERUP: {
        const int k = g_touch.slotOf(e.tfinger.fingerId);
        if (k >= 0) { g_touch.f[k].used = false; if (g_touch.count > 0) --g_touch.count; }
        // Le pilote s'en va : passer le relais TOUT DE SUITE. Sans ça son id survit,
        // plus aucun mouvement ne passe le filtre, et la souris est morte jusqu'à ce
        // que tous les doigts soient levés.
        if (g_touch.moverOk && e.tfinger.fingerId == g_touch.mover) g_touch.promote();
        if (g_touch.count > 0) break;                    // geste non terminé
        const Uint64 held = SDL_GetTicks64() - g_touch.startMs;
        const bool tap = g_touch.active && held <= kTapMaxMs && g_touch.travel <= kTapMaxTravel;
        if (tap && g_machine && g_injectHold == 0) {
            // Clic MAINTENU quatre trames, comme la page clavier et la borne : un
            // appui suivi du relâchement dans la MÊME trame ne produit AUCUN clic —
            // Ikbd::mouseEvent n'émet rien, il écrase l'état, et le paquet souris
            // n'est construit qu'à la VBL, qui ne verrait donc aucun changement.
            const bool right = g_touch.peak >= 2;        // deux doigts = clic droit
            injectClick(*g_machine, right);
        }
        g_touch = Touch{};                               // geste clos : tout repart à neuf
        break;
    }
    default: break;
    }
}

// Manette physique → octet IKBD (bit0 haut, 1 bas, 2 gauche, 3 droite, 7 feu).
uint8_t readPad(SDL_GameController* pad) {
    if (!pad) return 0;
    uint8_t b = 0;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    b |= 0x01;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  b |= 0x02;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  b |= 0x04;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= 0x08;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
        SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))          b |= 0x80;
    // Stick analogique gauche, zone morte 30 % (anti-drift des encodeurs bon marché).
    const int dz = 32767 * 30 / 100;
    const int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    const int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -dz) b |= 0x04;
    if (ax >  dz) b |= 0x08;
    if (ay < -dz) b |= 0x01;
    if (ay >  dz) b |= 0x02;
    return b;
}

// --- ROM système : choix persistant (page ROM du menu) ------------------------
// Le choix vit dans <données>/rom.cfg, un nom de fichier de kAndroidRoms ; tout
// autre contenu (absent, abîmé, ROM retirée d'un APK plus récent) ramène la ROM
// par défaut — jamais un chemin arbitraire. Écrit atomiquement, comme le reste.
void saveRomChoice(const std::string& dataDir, int index) {
    const std::string s = neost::formatAndroidRomChoice(index);
    const std::vector<uint8_t> buf(s.begin(), s.end());
    if (!writeFileAtomic(dataDir + "/" + neost::kAndroidRomChoiceFile, buf))
        NEOERR("ROM choice not saved");
}

// --- Dongles : choix persistant (page DONGLES du menu) ------------------------
// <données>/dongles.cfg, au format partagé avec le web (io/DongleTable.hpp,
// formatDongleSetup) : `cart=…` puis `joy0=…`… avec les identifiants de neost.cfg.
// Absent ou abîmé : rien de branché, jamais d'erreur.
constexpr const char* kDongleFile = "dongles.cfg";

neost::DongleSetup currentDongles(const Machine& m) {
    neost::DongleSetup s;
    s.key = m.dongle.model();
    for (int p = 0; p < int(PortDevices::Port::Count); ++p)
        s.dev[p] = m.ports.at(PortDevices::Port(p));
    return s;
}

void saveDongles(const std::string& dataDir, const Machine& m) {
    const std::string s = neost::formatDongleSetup(currentDongles(m));
    const std::vector<uint8_t> buf(s.begin(), s.end());
    if (!writeFileAtomic(dataDir + "/" + kDongleFile, buf)) NEOERR("dongles not saved");
}

// Rebranche ce que dongles.cfg décrit (au lancement). Pas de carte réseau sur ce
// frontend : setDongle ne peut pas refuser le port cartouche.
void loadDongles(const std::string& dataDir, Machine& m) {
    const neost::DongleSetup s = neost::parseDongleSetup(readSmallFile(dataDir + "/" + kDongleFile));
    m.setDongle(s.key);
    for (int p = 0; p < int(PortDevices::Port::Count); ++p)
        m.plugPort(PortDevices::Port(p), s.dev[p]);
    const std::string txt = neost::formatDongleSetup(s);
    if (!txt.empty()) NEOLOG("dongles: %s", txt.c_str());
}

// Charge la ROM `index` et démarre la machine À FROID sur le profil qu'elle exige.
// Même recette que le changement de ROM du web (neost_load_tos) et du bureau :
//  · charger D'ABORD — Bus::loadTos refuse un fichier absent ou de taille invalide
//    AVANT de toucher à la ROM courante ; en cas d'échec la machine reste INTACTE ;
//  · profil : la machine de la table, passée par adjustMachineForTos comme
//    partout (un TOS <= 1.04 sur STE halterait le CPU) ;
//  · pays du clavier physique relu dans la nouvelle ROM ;
//  · reconfigure() efface la RAM mais ne repose pas le moniteur : on le repose ;
//  · hardReset : un reset à chaud garderait la RAM et le « memvalid » de l'ancien OS.
bool switchRom(Machine& m, const std::string& dataDir, int index) {
    if (index < 0 || index >= neost::kAndroidRomCount) return false;
    const neost::AndroidRom& r = neost::kAndroidRoms[index];
    const std::string path = dataDir + "/" + r.file;
    if (!m.loadTos(path)) { NEOERR("ROM not loadable: %s - previous ROM kept", path.c_str()); return false; }
    const MachineType mt = Machine::adjustMachineForTos(r.machine, path);
    if (mt != m.machineType()) m.reconfigure(m.bus.ram.size(), CpuCore::Moira, mt);
    neost::stkeys::setCountryFromTos(m.bus.rom);   // pays du TOS → clavier physique
    m.mfp.setColorMonitor(true);
    m.hardReset();
    NEOLOG("ROM: %s (%s)", r.file, machineName(mt));
    return true;
}

// --- Relance dans le même processus -------------------------------------------
// SDL2 n'appelle PAS exit() quand SDL_main rend la main : si Android relance
// l'activité alors que le processus vit encore, SDL_main est rappelé avec TOUTES
// les globales dans l'état où la session précédente les a laissées — menu ouvert,
// doigts « posés », clic injecté jamais relâché, anneau audio plein, servo et
// diagnostic périmés, et surtout g_texW/g_texH d'une texture morte avec l'ancien
// contexte GL (uploadFrame ferait alors un glTexSubImage2D sur une texture jamais
// allouée : écran noir). On remet tout à neuf en tête de SDL_main, AVANT d'ouvrir
// l'audio : le thread audio ne tourne pas encore, g_primed s'écrit donc sans course.
// (La Machine statique, elle, repart par hardReset — cf. main.)
void resetSessionGlobals() {
    g_machine = nullptr;
    g_window  = nullptr;
    g_gl      = nullptr;
    g_tex = g_prog = g_vbo = 0;
    g_locPos = g_locUV = g_locTex = -1;
    g_texW = g_texH = 0;
    g_ring.clear();
    g_audioDev      = 0;
    g_audioRate     = 0;
    g_pacer.reset();
    g_cushionFrames = 0;
    g_primed        = false;
    g_underruns.store(0, std::memory_order_relaxed);
    g_underrunSeen  = 0;
    g_underrunMute  = 0;
    g_masterVol     = 1.0f;
    g_touch   = Touch{};
    g_menu    = neost::AndroidMenu{};
    g_uiScale = 1.0f;
    g_injectHold     = 0;
    g_injectScancode = 0;
    g_injectClick    = false;
    g_injectL = g_injectR = false;
    for (auto& m : g_physMake) m = 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    resetSessionGlobals();             // relance dans le même processus : cf. sa définition
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    // Bouton RETOUR (et geste de bord en navigation gestuelle, clic droit sous DeX /
    // ChromeOS) : sans ce hint, SDLActivity le traite lui-même et TERMINE l'activité —
    // la partie en cours était perdue sur un geste qu'on fait sans y penser. Piégé,
    // il arrive en SDL_KEYDOWN/SDLK_AC_BACK et bascule le menu (cf. la boucle).
    // Posé AVANT SDL_Init : la couche Java le lit à chaque appui, mais rien ne doit
    // pouvoir passer entre le démarrage et la première itération.
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        NEOERR("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    g_window = SDL_CreateWindow("NeoST", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!g_window) { NEOERR("SDL_CreateWindow: %s", SDL_GetError()); SDL_Quit(); return 1; }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) { NEOERR("SDL_GL_CreateContext: %s", SDL_GetError()); SDL_Quit(); return 1; }
    // Vsync ON : depuis que le MENU est redessiné à chaque itération, la boucle
    // tournait LIBRE (swap immédiat + Delay(1) ≈ 50 rendus par trame émulée —
    // batterie et thermique). Le swap bloquant borne le rendu au taux de l'écran ;
    // la CADENCE d'émulation, elle, reste sur le temps émulé (la boucle de
    // rattrapage exécute 0..4 trames par rendu, 50 Hz PAL sur un écran 60+ Hz).
    if (SDL_GL_SetSwapInterval(1) != 0) SDL_GL_SetSwapInterval(0);   // repli : Delay(1) borne seul
    if (!initGl()) { SDL_Quit(); return 1; }

    const std::string dataDir = prepareData();
    NEOLOG("data dir: %s", dataDir.c_str());

    // --- Dear ImGui : le menu, et RIEN d'autre ------------------------------
    // Backend SDL2 + OpenGL3 en GLSL ES 1.00 (le contexte est un ES 2). Le
    // tactile arrive en événements souris via imgui_impl_sdl2, donc les rangées
    // se tapent au doigt ; la navigation manette vient de la nav ImGui, qu'on
    // n'active QUE menu ouvert (sinon elle volerait la manette au jeu).
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;      // rien à persister sur un téléphone
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(g_window, g_gl);
    ImGui_ImplOpenGL3_Init("#version 100");
    {
        // Échelle : un téléphone a ~3 fois la densité d'un écran de bureau, et
        // ce menu se lit à bout de bras. On cale sur la largeur du drawable.
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(g_window, &dw, &dh);
        g_uiScale = std::max(1.0f, float(dw) / 640.0f);
        ImGui::GetStyle().ScaleAllSizes(g_uiScale);
        ImGui::GetIO().FontGlobalScale = g_uiScale;
    }
    g_menu.dataDir   = dataDir;
    g_menu.importDir = importDir();
    NEOLOG("import dir: %s", g_menu.importDir.empty() ? "(unavailable)" : g_menu.importDir.c_str());

    // Machine par défaut : ST 1 Mo + EmuTOS 192 Ko (le build « Atari ST »), comme
    // la démo web — c'est la machine de référence des jeux et démos de l'époque.
    // La ROM est celle du dernier choix (page ROM du menu, rom.cfg), sinon celle-là.
    static Machine machine(1024u * 1024u, CpuCore::Moira, MachineType::St);
    g_machine = &machine;
    int romIdx = neost::parseAndroidRomChoice(
        readSmallFile(dataDir + "/" + neost::kAndroidRomChoiceFile));
    // switchRom fait déjà un hardReset ; celui d'après la pose des horloges
    // (plus bas) reste, il est sans effet de plus. ROM choisie illisible → repli
    // sur la ROM par défaut.
    if (!switchRom(machine, dataDir, romIdx) && romIdx != neost::kAndroidRomDefault) {
        romIdx = neost::kAndroidRomDefault;
        switchRom(machine, dataDir, romIdx);
    }
    g_menu.romCur = romIdx;
    loadDongles(dataDir, machine);     // page DONGLES : ce qui était branché la dernière fois
    const std::string disk = dataDir + "/diskA.st";
    if (machine.loadDisk(disk)) g_menu.mounted = disk;
    else                        NEOLOG("pas de disquette (%s)", disk.c_str());
    g_menu.refresh();

    // MODÈLE « PUSH » : sans ces deux horloges, synthesizeFrame rend un jeu de
    // registres que plus rien ne met à jour — la machine serait MUETTE.
    machine.psg.setCycleClock([] { return g_machine->frameRelCycle(); });
    machine.dmasnd.setCycleClock([] { return g_machine->frameRelCycle(); });
    // À FROID, pas à chaud : au premier lancement c'est équivalent (RAM neuve), mais
    // la Machine est STATIQUE — sur une relance dans le même processus, un reset()
    // gardait la RAM de la partie précédente et son « memvalid », et le TOS refaisait
    // un boot court par-dessus.
    machine.hardReset();
    openAudio();

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    // CADENCE sur le TEMPS ÉMULÉ (jamais sur l'écran) : la géométrie vidéo décide
    // (50, 60 ou 71 Hz). Un tour de boucle exécute 0, 1 ou 2 trames selon ce que
    // le temps réel réclame ; plafond à 4 pour ne pas spiraler au retour d'une
    // mise en veille.
    // A28 : la boucle de rattrapage est PARTAGÉE avec le frontend web (Pacing.hpp).
    neost::pacing::FramePacer framePacer;
    framePacer.resync(double(SDL_GetTicks64()));
    // Retour arrière éteint par défaut, comme sur le bureau ; la bascule est au menu.
    // Budget réduit pour un téléphone : une vingtaine de secondes en 1 Mo de RAM.
    Rewind rewind;
    rewind.setBudget(24u << 20);
    bool running = true, paused = false;

    while (running) {
        // Le menu est OUVERT : la machine est en pause et l'interface prend
        // toutes les entrées. Fermé, c'est l'inverse — sauf la page clavier, qui
        // vit par-dessus une machine qui TOURNE (on peut donc répondre à un
        // « PRESS SPACE » sans figer le jeu).
        const bool uiHasInput = g_menu.open || g_menu.keysPage;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
                // Deux gardes, chacune pour un bug réel :
                //  · WantCaptureMouse — taper le bouton MENU envoyait AUSSI un
                //    clic gauche au ST (le tap est émis au FINGERUP, plusieurs
                //    trames après le DOWN : ImGui a eu le temps de déclarer
                //    qu'il tient le pointeur) ;
                //  · uiHasInput — un FINGERUP avalé par l'interface laissait
                //    g_touch.fingers désynchronisé, et le tap suivant passait
                //    pour un appui à deux doigts (clic droit fantôme).
                if (!uiHasInput && !ImGui::GetIO().WantCaptureMouse) handleTouch(e);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                // `which` est ici l'id d'INSTANCE de la manette partie : ne fermer la
                // nôtre que si c'est elle, puis reprendre une manette restée branchée
                // (aucun ADDED ne viendra pour elle).
                if (pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))
                               == e.cdevice.which) {
                    SDL_GameControllerClose(pad);
                    pad = nullptr;
                    for (int j = 0; j < SDL_NumJoysticks() && !pad; ++j)
                        if (SDL_IsGameController(j)) pad = SDL_GameControllerOpen(j);
                }
                break;
            case SDL_APP_WILLENTERBACKGROUND:
                paused = true;
                if (g_audioDev) SDL_PauseAudioDevice(g_audioDev, 1);
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                paused = false;
                // g_primed appartient au THREAD AUDIO (comme primed_ du natif) :
                // l'écrire ici serait une data race, et c'est inutile — l'anneau
                // a gardé son contenu pendant la pause du périphérique.
                framePacer.resync(double(SDL_GetTicks64()));   // horloge à resynchroniser
                // CONTEXTE GL : sur nombre d'appareils, l'EGL context est PERDU
                // en arrière-plan (SDLActivity essaie de le préserver, sans
                // garantie). Nos objets (texture, programme, VBO) seraient alors
                // des noms invalides → écran noir muet au retour. On les recrée ;
                // si le contexte a survécu, on détruit d'abord les anciens (les
                // glDelete* sur des noms morts sont silencieusement ignorés).
                glDeleteTextures(1, &g_tex);
                glDeleteProgram(g_prog);
                glDeleteBuffers(1, &g_vbo);
                g_texW = g_texH = 0;
                if (!initGl()) NEOERR("GL re-init failed after resume");
                // Les objets GL d'ImGui (atlas de fontes, shader, buffers) sont
                // morts avec le contexte eux aussi — sans cette recréation, le
                // menu resterait invisible/corrompu au retour au premier plan.
                ImGui_ImplOpenGL3_DestroyDeviceObjects();
                ImGui_ImplOpenGL3_CreateDeviceObjects();
                if (g_audioDev) SDL_PauseAudioDevice(g_audioDev, 0);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                // START ouvre/ferme le menu, comme le menu borne (F9/START).
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    g_menu.keysPage = false;
                    g_menu.open = !g_menu.open;
                    if (g_menu.open) g_menu.reqRescan = true;
                }
                break;
            case SDL_KEYDOWN:
                // Bouton RETOUR d'Android (≠ le REWIND de l'émulateur) : il BASCULE
                // le menu — ouvert ou page clavier → fermé, fermé → ouvert. Il ne
                // quitte JAMAIS : un geste de bord fait par mégarde tuait la partie.
                // On ne sort que par l'entrée QUIT du menu (ou la fermeture par le
                // système, SDL_QUIT). Répétition ignorée : un appui long ne doit pas
                // faire clignoter le menu.
                if (e.key.keysym.sym == SDLK_AC_BACK) {
                    if (e.key.repeat) break;
                    if (g_menu.open || g_menu.keysPage) {
                        g_menu.open = g_menu.keysPage = false;
                    } else {
                        g_menu.open = true;
                        g_menu.reqRescan = true;
                    }
                    break;
                }
                // Clavier physique → IKBD, menu fermé seulement (ouvert, ImGui a la
                // main). Répétition ignorée : sur un vrai ST c'est le TOS qui répète,
                // l'IKBD n'émet qu'un MAKE par appui (comme le bureau et Hatari).
                if (!uiHasInput && !e.key.repeat
                    && e.key.keysym.scancode < SDL_NUM_SCANCODES
                    && g_physMake[e.key.keysym.scancode] == 0) {
                    if (const uint8_t sc = stScancodeForSdl(e.key.keysym)) {
                        machine.keyEvent(sc, true);
                        g_physMake[e.key.keysym.scancode] = sc;
                    }
                }
                break;
            case SDL_KEYUP:
                // Le BREAK suit son MAKE même menu ouvert (une touche tenue à
                // l'ouverture ne doit pas rester collée côté ST).
                if (e.key.keysym.scancode < SDL_NUM_SCANCODES) {
                    uint8_t& made = g_physMake[e.key.keysym.scancode];
                    if (made) { machine.keyEvent(made, false); made = 0; }
                }
                break;
            default: break;
            }
        }
        if (uiHasInput) g_touch = Touch{};            // état tactile remis à zéro
        if (paused) { SDL_Delay(50); continue; }

        // --- Requêtes du menu (seul endroit qui touche à la machine) ---------
        if (g_menu.reqRescan) {
            g_menu.mounted = machine.fdc.mountedPath(0);
            g_menu.refresh();
            g_menu.reqRescan = false;
        }
        if (!g_menu.reqMount.empty()) {
            // Modèle « vraie machine » : on INSÈRE, on ne redémarre pas. Le
            // programme en cours continue ; c'est « RESTART » qui relance.
            if (machine.fdc.loadImage(g_menu.reqMount, 0)) g_menu.mounted = g_menu.reqMount;
            else NEOERR("image illisible: %s", g_menu.reqMount.c_str());
            g_menu.reqMount.clear();
        }
        if (g_menu.reqRestart) { machine.hardReset(); cancelPendingInput(); g_menu.reqRestart = false; }
        if (g_menu.reqRom >= 0) {
            const int want = g_menu.reqRom;
            g_menu.reqRom = -1;
            if (switchRom(machine, dataDir, want)) {
                g_menu.romCur = want;
                g_menu.status.clear();
                saveRomChoice(dataDir, want);
                // Un autre OS : un instantané pris sous l'ancienne ROM y ramènerait
                // la RAM d'un autre système (le web et le bureau vident aussi).
                rewind.clear();
                cancelPendingInput();                  // pas de BREAK orphelin
                framePacer.resync(double(SDL_GetTicks64()));   // 50 <-> 60 Hz : autre trame
            } else {
                g_menu.status = "ROM not loadable";
            }
        }
        // Page DONGLES : brancher ne redémarre rien (le logiciel sonde quand il veut).
        if (g_menu.reqCartKey >= 0) {
            machine.setDongle(CartridgeKey::Model(std::min(g_menu.reqCartKey, neost::kCartKeyModelCount - 1)));
            g_menu.reqCartKey = -1;
            saveDongles(dataDir, machine);
        }
        if (g_menu.reqPlugPort >= 0) {
            if (g_menu.reqPlugPort < int(PortDevices::Port::Count)
                && machine.plugPort(PortDevices::Port(g_menu.reqPlugPort), PortDevices::Device(g_menu.reqPlugDev)))
                saveDongles(dataDir, machine);
            g_menu.reqPlugPort = g_menu.reqPlugDev = -1;
        }
        if (g_menu.reqPortButton) { g_menu.reqPortButton = false; machine.pressPortButton(); }
        if (g_menu.rewindOn != rewind.enabled()) rewind.setEnabled(g_menu.rewindOn);
        if (g_menu.reqRewind) {
            g_menu.reqRewind = false;
            std::string why = "Rewind is off";
            const bool ok = rewind.enabled() && rewind.stepBack(machine, &why);
            g_menu.status = ok ? std::string() : why;
            if (ok) {
                g_menu.mounted = machine.fdc.mountedPath(0);   // le lecteur a pu changer
                cancelPendingInput();                           // pas de BREAK orphelin
            }
            NEOLOG("[rewind] %s (%zu in ring)", ok ? "OK" : why.c_str(), rewind.count());
        }
        if (g_menu.reqQuit)    { running = false; g_menu.reqQuit = false; }
        // Injection : consommée SEULEMENT quand le maintien précédent est fini
        // (recette borne). La requête reste posée dans g_menu entre-temps — elle
        // est DIFFÉRÉE d'une ou deux trames, jamais perdue ni empilée.
        if (g_injectHold == 0 && g_menu.reqKeyPress >= 0) {
            const int k = g_menu.reqKeyPress;
            machine.keyEvent(neost::kKeys[k].scancode, true);
            g_injectScancode = neost::kKeys[k].scancode;
            g_injectClick    = false;
            g_injectHold     = 4;                     // ~4 trames de maintien
            g_menu.reqKeyPress = -1;
        }
        if (g_injectHold == 0 && g_menu.reqClick) {
            injectClick(machine, g_menu.reqClick == 2);   // clic MAINTENU, comme la borne
            g_menu.reqClick = 0;
        }

        // Manette : au ST seulement quand l'interface ne la réclame pas.
        const uint8_t joy = uiHasInput ? 0 : readPad(pad);
        machine.ikbd.setJoystick(0, joy);             // port 1 = port « jeux »
        machine.bus.setStePadInput(0, joy);
        machine.cpu.updateIpl();

        const double nowMs = double(SDL_GetTicks64());
        int ran = 0;
        if (g_menu.open) {
            // MENU OUVERT = machine EN PAUSE (modèle borne). On resynchronise
            // l'horloge en permanence : sans ça, la reprise croirait devoir
            // rattraper toutes les trames passées dans le menu.
            framePacer.resync(nowMs);
        } else {
            ran = framePacer.runDue(nowMs, [&] {
                machine.runFrame();
                produceAudioFrame();                  // le son suit la trame
                rewind.onFrame(machine);
                if (g_injectHold > 0 && --g_injectHold == 0) {
                    if (g_injectClick) machine.mouseEvent(0, 0, false, false);
                    else               machine.keyEvent(g_injectScancode, false);
                    g_injectL = g_injectR = false;
                }
                return machine.frameCycles();
            });
        }

        // On redessine même sans trame neuve : le menu, lui, doit rester vivant
        // (animations, retour tactile) au-dessus de la dernière image ST.
        uploadFrame(machine.shifter.pixels(), machine.shifter.width(), machine.shifter.height());
        drawScreen();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        // La nav manette n'est armée QUE menu ouvert : sinon ImGui capterait la
        // manette que le jeu attend.
        if (g_menu.open) ImGui::GetIO().ConfigFlags |=  ImGuiConfigFlags_NavEnableGamepad;
        else             ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        // Ce qui est branché (page DONGLES) : le menu ne voit pas la machine. Relu
        // à chaque rendu — un retour arrière restaure aussi les ports.
        g_menu.cartKey = int(machine.dongle.model());
        for (int p = 0; p < int(PortDevices::Port::Count); ++p)
            g_menu.portDev[p] = int(machine.ports.at(PortDevices::Port(p)));
        g_menu.draw(g_uiScale);
        ImGui::Render();
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(g_window, &dw, &dh);
        glViewport(0, 0, dw, dh);                     // le menu couvre TOUT l'écran,
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());   // pas la zone letterboxée

        SDL_GL_SwapWindow(g_window);
        if (ran == 0 && !g_menu.open) SDL_Delay(1);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (g_audioDev) SDL_CloseAudioDevice(g_audioDev);
    if (pad) SDL_GameControllerClose(pad);
    SDL_GL_DeleteContext(g_gl);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
