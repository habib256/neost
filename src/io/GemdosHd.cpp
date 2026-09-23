// =============================================================================
//  GemdosHd.cpp — Émulation « disque dur GEMDOS » (port de Hatari gemdos.c).
//
//  Port fidèle de extern/hatari/src/gemdos.c (+ cpu/hatari-glue.c OpCode_* et
//  cart_asm.s/cartData.c). Les simplifications par rapport à Hatari :
//   - bUseTos est TOUJOURS vrai (NeoST exécute toujours un TOS/EmuTOS) → les
//     chemins « mode test sans TOS » sont omis ;
//   - pas d'images ACSI/SCSI/IDE → 0 partition à sauter, lecteurs dès C: ;
//   - pas d'autostart INF (INF_* traités comme inactifs) ni de symboles debug ;
//   - conversion de jeu de caractères et write-protect désactivées par défaut.
//  La LOGIQUE de redirection (chemins, handles, DTA, Pexec) est portée 1:1.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/GemdosHd.hpp"
#include "core/Bus.hpp"
#include "util/HostPath.hpp"
#include "core/Cpu68k.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>

#include <dirent.h>
#include <sys/stat.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/statvfs.h>       // Dfree : espace réel du disque hôte (cf. gemDFree)
#endif
#include <unistd.h>
#include <utime.h>
#include <filesystem>          // canonical() : il n'y a pas de realpath()
#include <fstream>             // auto-test du bac à sable (fichier témoin)
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX               // sinon windows.h définit min()/max() en MACROS et
#endif                         // casse les std::min / std::max du fichier
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>           // GetDiskFreeSpaceExW (Dfree), faute de statvfs
// MinGW n'a pas les bits de permission « groupe » et « autres » : sous Windows il
// n'existe qu'un attribut « lecture seule », que le CRT dérive du bit propriétaire.
// On les définit à 0 pour que les masques ci-dessous restent lisibles tels quels
// (mode & 0 = 0 : neutre, ce qui est exactement la sémantique de l'hôte).
#ifndef S_IRGRP
#define S_IRGRP 0
#endif
#ifndef S_IROTH
#define S_IROTH 0
#endif
#ifndef S_IWGRP
#define S_IWGRP 0
#endif
#ifndef S_IWOTH
#define S_IWOTH 0
#endif
#ifndef S_IRWXG
#define S_IRWXG 0
#endif
#ifndef S_IRWXO
#define S_IRWXO 0
#endif
#ifndef S_IRWXU
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#endif
// MinGW : mkdir() ne prend PAS de mode (Windows n'a pas de bits POSIX), et les
// constantes de access() s'appellent autrement. On ramène les deux à la forme
// POSIX pour que le corps du fichier reste écrit une seule fois.
// ⚠ ORDRE : les en-têtes DOIVENT venir avant le #define — <direct.h> déclare
// lui-même `int mkdir(const char*)`, et la macro le réécrirait en plein milieu
// de sa propre déclaration (« macro 'mkdir' requires 2 arguments »).
#include <direct.h>            // _mkdir, _rmdir, _getcwd, et son mkdir() 1 argument
#include <io.h>                // _access
#define mkdir(p, m) _mkdir(p)
#ifndef F_OK
#define F_OK 0
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif
#endif

// -----------------------------------------------------------------------------
//  Octets assemblés de la cartouche système (cart_asm.s → cartData.c d'Hatari).
//  Contient : en-tête cartouche (magic $ABCDEF42 + C-INIT vers sys_init), le
//  nouveau vecteur GEMDOS (new_gemdos), le gestionnaire Pexec et sys_init. Les
//  opcodes magiques 8/9/10 y sont interceptés par Cpu68k::run.
//  Divergence NeoST : le programme info visible sur le bureau (icône cartouche)
//  s'appelle NEOST.TOS et affiche les raccourcis NeoST (accents = jeu de
//  caractères Atari ST). Regénéré : C-NAME (+$18), C-BSIZ (+$14), texte (+$76) ;
//  code et adresses $FA0024/$FA002A (CART_OLDGEMDOS/CART_GEMDOS) inchangés.
// -----------------------------------------------------------------------------
static const uint8_t Cart_data[] = {
0xab,0xcd,0xef,0x42,0x00,0x00,0x00,0x00,0x08,0xfa,0x00,0x58,0x00,0xfa,0x00,0x5e,
0x58,0x00,0x32,0x29,0x00,0x00,0x01,0xe7,0x4e,0x45,0x4f,0x53,0x54,0x2e,0x54,0x4f,
0x53,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x00,0x08,0x69,0x0a,0x66,0x02,
0x4e,0x73,0x2f,0x3a,0xff,0xf0,0x4e,0x75,0x4e,0x41,0x4f,0xef,0x00,0x10,0x4a,0x80,
0x6b,0xee,0x00,0x09,0x69,0xec,0x67,0xe8,0x2f,0x00,0x2f,0x08,0x3f,0x3c,0x00,0x49,
0x4e,0x41,0x5c,0x8f,0x20,0x1f,0x4e,0x73,0xa0,0x00,0x00,0x0a,0x4e,0x75,0x48,0x7a,
0x00,0x16,0x3f,0x3c,0x00,0x09,0x4e,0x41,0x5c,0x8f,0x3f,0x3c,0x00,0x07,0x4e,0x41,
0x54,0x8f,0x42,0x67,0x4e,0x41,0x1b,0x45,0x0d,0x0a,0x20,0x20,0x20,0x20,0x20,0x20,
0x20,0x20,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,
0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x0d,0x0a,0x20,0x20,
0x20,0x20,0x20,0x20,0x20,0x20,0x4e,0x65,0x6f,0x53,0x54,0x20,0x3a,0x20,0x72,0x61,
0x63,0x63,0x6f,0x75,0x72,0x63,0x69,0x73,0x20,0x63,0x6c,0x61,0x76,0x69,0x65,0x72,
0x0d,0x0a,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,
0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,0x3d,
0x3d,0x3d,0x3d,0x3d,0x0d,0x0a,0x0d,0x0a,0x20,0x43,0x6c,0x69,0x63,0x20,0x64,0x61,
0x6e,0x73,0x20,0x6c,0x27,0x82,0x63,0x72,0x61,0x6e,0x20,0x3a,0x20,0x63,0x61,0x70,
0x74,0x75,0x72,0x65,0x20,0x6c,0x61,0x20,0x73,0x6f,0x75,0x72,0x69,0x73,0x0d,0x0a,
0x20,0x20,0x20,0x20,0x20,0x28,0x6c,0x65,0x20,0x63,0x75,0x72,0x73,0x65,0x75,0x72,
0x20,0x47,0x45,0x4d,0x20,0x73,0x75,0x69,0x74,0x20,0x6c,0x61,0x20,0x73,0x6f,0x75,
0x72,0x69,0x73,0x29,0x0d,0x0a,0x20,0x53,0x75,0x70,0x70,0x72,0x20,0x28,0x44,0x45,
0x4c,0x29,0x20,0x3a,0x20,0x6c,0x69,0x62,0x8a,0x72,0x65,0x20,0x6c,0x61,0x20,0x73,
0x6f,0x75,0x72,0x69,0x73,0x0d,0x0a,0x20,0x46,0x31,0x31,0x20,0x3a,0x20,0x82,0x6d,
0x75,0x6c,0x61,0x74,0x69,0x6f,0x6e,0x20,0x6a,0x6f,0x79,0x73,0x74,0x69,0x63,0x6b,
0x20,0x61,0x75,0x20,0x63,0x6c,0x61,0x76,0x69,0x65,0x72,0x0d,0x0a,0x20,0x20,0x20,
0x20,0x20,0x28,0x66,0x6c,0x8a,0x63,0x68,0x65,0x73,0x20,0x2b,0x20,0x43,0x74,0x72,
0x6c,0x20,0x64,0x72,0x6f,0x69,0x74,0x2c,0x20,0x70,0x6f,0x72,0x74,0x20,0x31,0x29,
0x0d,0x0a,0x0d,0x0a,0x20,0x4c,0x65,0x20,0x72,0x65,0x73,0x74,0x65,0x20,0x70,0x61,
0x73,0x73,0x65,0x20,0x70,0x61,0x72,0x20,0x6c,0x61,0x20,0x62,0x61,0x72,0x72,0x65,
0x20,0x64,0x65,0x20,0x6d,0x65,0x6e,0x75,0x73,0x0d,0x0a,0x20,0x28,0x64,0x69,0x73,
0x71,0x75,0x65,0x74,0x74,0x65,0x2c,0x20,0x63,0x61,0x72,0x74,0x6f,0x75,0x63,0x68,
0x65,0x2c,0x20,0x6a,0x6f,0x79,0x73,0x74,0x69,0x63,0x6b,0x2c,0x0d,0x0a,0x20,0x76,
0x6f,0x6c,0x75,0x6d,0x65,0x2e,0x2e,0x2e,0x29,0x20,0x65,0x74,0x20,0x6c,0x61,0x20,
0x66,0x65,0x6e,0x88,0x74,0x72,0x65,0x20,0x43,0x50,0x55,0x20,0x70,0x6f,0x75,0x72,
0x0d,0x0a,0x20,0x6c,0x65,0x20,0x62,0x6f,0x75,0x74,0x6f,0x6e,0x20,0x52,0x65,0x73,
0x65,0x74,0x2e,0x0d,0x0a,0x20,0x51,0x75,0x69,0x74,0x74,0x65,0x72,0x20,0x3a,0x20,
0x66,0x65,0x72,0x6d,0x65,0x72,0x20,0x6c,0x61,0x20,0x66,0x65,0x6e,0x88,0x74,0x72,
0x65,0x2e,0x0d,0x0a,0x00,
};

// Chemins de l'hôte : UNE définition pour tout NeoST (cf. util/HostPath.hpp).
namespace hostpath = neost::hostpath;

// -----------------------------------------------------------------------------
//  Constantes GEMDOS (gemdos_defines.h) et helpers de chemin/conversion.
// -----------------------------------------------------------------------------
namespace {
[[maybe_unused]] constexpr int GEMDOS_EOK=0, GEMDOS_ERROR=-1, GEMDOS_E_SEEK=-6,
  GEMDOS_EWRPRO=-13, GEMDOS_EINVFN=-32, GEMDOS_EFILNF=-33, GEMDOS_EPTHNF=-34,
  GEMDOS_ENHNDL=-35, GEMDOS_EACCDN=-36, GEMDOS_ENSMEM=-39, GEMDOS_EDRIVE=-46,
  GEMDOS_ENMFIL=-49, GEMDOS_ERANGE=-64, GEMDOS_EINTRN=-65, GEMDOS_EPLFMT=-66;

// Noms pour la trace NEOST_GEMDOS_TRACE (appels « fichier » ≥ 0x36 et codes d'erreur TOS).
static const char* gemdosCallName(uint16_t call) {
    switch (call) {
    case 0x36: return "Dfree";   case 0x39: return "Dcreate"; case 0x3a: return "Ddelete";
    case 0x3b: return "Dsetpath"; case 0x3c: return "Fcreate"; case 0x3d: return "Fopen";
    case 0x3e: return "Fclose";  case 0x3f: return "Fread";   case 0x40: return "Fwrite";
    case 0x41: return "Fdelete"; case 0x42: return "Fseek";   case 0x43: return "Fattrib";
    case 0x44: return "Mxalloc"; case 0x45: return "Fdup";    case 0x46: return "Fforce";
    case 0x47: return "Dgetpath";
    case 0x48: return "Malloc";  case 0x49: return "Mfree";   case 0x4a: return "Mshrink";
    case 0x4b: return "Pexec";   case 0x4c: return "Pterm";   case 0x4e: return "Fsfirst";
    case 0x4f: return "Fsnext";  case 0x56: return "Frename"; case 0x57: return "Fdatime";
    default:   return "?";
    }
}
static const char* gemdosErrorName(int32_t d0) {   // « » si ce n'est pas un code d'erreur
    switch (d0) {
    case GEMDOS_ERROR:  return " (ERROR)";  case GEMDOS_E_SEEK: return " (E_SEEK)";
    case GEMDOS_EWRPRO: return " (EWRPRO)"; case GEMDOS_EINVFN: return " (EINVFN)";
    case GEMDOS_EFILNF: return " (EFILNF)"; case GEMDOS_EPTHNF: return " (EPTHNF)";
    case GEMDOS_ENHNDL: return " (ENHNDL)"; case GEMDOS_EACCDN: return " (EACCDN)";
    case -37:           return " (EIHNDL)"; case GEMDOS_ENSMEM: return " (ENSMEM)";
    case GEMDOS_EDRIVE: return " (EDRIVE)"; case GEMDOS_ENMFIL: return " (ENMFIL)";
    case GEMDOS_ERANGE: return " (ERANGE)"; case GEMDOS_EINTRN: return " (EINTRN)";
    case GEMDOS_EPLFMT: return " (EPLFMT)";
    default:            return "";
    }
}

constexpr uint8_t FA_READONLY=0x01, FA_VOLUME=0x08, FA_DIR=0x10, FA_ARCHIVE=0x20;
constexpr int IGNORED_FILE_ATTRIBS = FA_ARCHIVE | FA_READONLY;   // 0x21

constexpr char PATHSEP = hostpath::SEP;   // séparateur interne commun (util/HostPath)
constexpr char INVALID_CHAR = '+';

constexpr uint16_t SR_OVERFLOW = 0x0002, SR_ZERO = 0x0004, SR_SUPERMODE = 0x2000;

inline bool isVolumeLabel(int x) { return ((x & ~IGNORED_FILE_ATTRIBS) == FA_VOLUME); }

// Date/heure hôte → format GEMDOS (timeword/dateword), port de GemDOS_DateTime2Tos.
void dateTime2Tos(time_t t, uint16_t& timeword, uint16_t& dateword) {
    struct tm* x = localtime(&t);
    if (!x) { dateword = 1 | (1 << 5); timeword = 0; return; }   // 1980-01-01
    timeword = (x->tm_sec >> 1) | (x->tm_min << 5) | (x->tm_hour << 11);
    dateword = x->tm_mday | ((x->tm_mon + 1) << 5)
             | (((x->tm_year - 80 > 0) ? x->tm_year - 80 : 0) << 9);
}

// Attribut hôte → GEMDOS (GemDOS_ConvertAttribute).
uint8_t convertAttribute(mode_t mode, const char* path) {
    uint8_t a = 0;
    if (S_ISDIR(mode)) a |= FA_DIR;
    if (!(mode & S_IWUSR) || access(path, W_OK) != 0) a |= FA_READONLY;
    return a;
}

// errno → code d'erreur GEMDOS (errno2gemdos). etype: false=fichier, true=chemin.
uint32_t errno2gemdos(int error, bool pathType) {
    switch (error) {
    case ENOENT:   if (!pathType) return GEMDOS_EFILNF; /* fallthrough */
    case ENOTDIR:  return GEMDOS_EPTHNF;
    case ENOTEMPTY: case EEXIST: case EPERM: case EACCES: case EROFS:
        return GEMDOS_EACCDN;
    default:       return GEMDOS_ERROR;
    }
}

// Un caractère du nom HÔTE est-il INVALIDE pour un nom de fichier Atari ? (port de
// Str_Filename_Invalid_Char) : contrôle, * : ? \ / et les points « en trop » (sauf « .. »).
bool filenameInvalidChar(const char* name, int offset) {
    char c = name[offset];
    if (c < 32 || c == 127) return true;
    switch (c) {
        case '*': case ':': case '?': case '\\': case '/': return true;
        case '.': {
            const char* dot = strrchr(name, '.');
            if (dot != name + offset && std::strlen(name) > 2) return true;
            return false;
        }
        default: return false;
    }
}

// Correspondance d'un nom TOS à un masque (fsfirst_match), port 1:1.
// `subdir` : on est dans un SOUS-répertoire (pas la racine du lecteur) → « . » et
// « .. » sont énumérés (cf. Hatari fsfirst_match) ; à la racine, tous les « .* » sont
// ignorés (le root d'un lecteur GEMDOS n'a pas de « . »/« .. »).
// `onlyInvalid` : un « ? » ne matche QUE les caractères invalides pour un nom Atari
// (cf. Str_Filename_Invalid_Char) — utilisé pour la passe « caractère invalide » de
// addPathComponent, distincte de la passe troncature.
bool fsfirst_match(const char* pat, const char* name, bool subdir, bool onlyInvalid = false) {
    const char *dot, *p = pat, *n = name;
    if (name[0] == '.') {
        if (!subdir) return false;                       // racine : ignore tous les .*
        if (strcmp(name, ".") && strcmp(name, "..")) return false;  // sous-rép : garde . et .. seulement
    }
    dot = strrchr(name, '.');
    if (dot && p[0] == '*' && p[1] == 0) return false;  // '*' seul n'inclut pas d'extension
    while (*n) {
        if (*p == '*') { while (*n && n != dot) n++; p++; }
        else if (*p == '?' && *n && (!onlyInvalid || filenameInvalidChar(name, int(n - name)))) { n++; p++; }
        else if (toupper((unsigned char)*p++) != toupper((unsigned char)*n++)) return false;
    }
    while (p[0] == '*') p++;
    if (p[0] == '.' && p[1] == '*') p += 2;
    while (p[0] == '*') p++;
    return (p[0] == 0);
}

// Clippe un nom à 8+3 comme TOS (clip_to_83) ; renvoie la longueur résultante.
int clip_to_83(std::string& name) {
    size_t dot = name.find('.');
    if (dot != std::string::npos) {
        if (name.size() - dot > 4) name.resize(dot + 4);          // extension → 3
        if (dot > 8) name = name.substr(0, 8) + name.substr(dot); // base → 8
        return (int)name.size();
    }
    if (name.size() > 8) name.resize(8);
    return (int)name.size();
}

// Conversion hôte → Atari pour le nom DTA (Str_Filename_Host2Atari simplifié :
// majuscules, 8+3, caractères invalides → '+', sans conversion de charset).
std::string host2atari(const std::string& src) {
    std::string s = src;
    size_t dot = s.rfind('.');
    if (dot != std::string::npos) {
        if (s.size() - dot > 4) s.resize(dot + 4);
        for (size_t i = 0; i < dot; i++) if (s[i] == '.') s[i] = INVALID_CHAR;
        if (dot > 8) s = s.substr(0, 8) + s.substr(dot);
    } else if (s.size() > 8) {
        s.resize(8);
    }
    for (char& c : s) {
        unsigned char u = (unsigned char)c;
        if (u < 32 || u == 127) { c = INVALID_CHAR; continue; }
        switch (c) {
        // ⚠ PAS '{' / '}' : Str_Filename_Host2Atari les conserve (str.c) — les
        // remplacer rendait le nom listé par Fsfirst inouvrable (le '+' de
        // remplacement ne re-matchait pas le fichier hôte via onlyInvalid).
        case '*': case '/': case ':': case '?': case '\\':
            c = INVALID_CHAR; break;
        default: if (u < 128) c = (char)toupper(u);
        }
    }
    return s;
}

// Chemins de l'HÔTE : une seule définition pour tout NeoST — util/HostPath.
// Ce fichier n'en garde que les adaptateurs de signature (Hatari passe ses chemins
// par référence). La règle « absolu » y est consciente de Windows (lettre de lecteur,
// racine UNC) et EXERÇABLE hors Windows, ce qui manquait quand le lecteur GEMDOS est
// resté mort sur tous les paquets Windows (issue #37) — cf. tests/selftest_logic.cpp.

// File_CleanFileName : retire les '/' de fin (mais en garde si len <= 2).
void cleanFileName(std::string& s) { s = hostpath::stripTrailingSep(s); }

// File_AddSlashToEndFileName.
void addSlash(std::string& s) {
    if (!s.empty() && s.back() != PATHSEP) s.push_back(PATHSEP);
}
bool dirExists(const std::string& p) {
    struct stat b;
    return stat(p.c_str(), &b) == 0 && S_ISDIR(b.st_mode);
}
const char* baseName(const char* path) {
    const char* b = strrchr(path, PATHSEP);
    return b ? b + 1 : path;
}

// File_MakeAbsoluteName : normalise les séparateurs, préfixe le répertoire courant
// si besoin, puis résout « . » et « .. » — cf. util/HostPath (et son auto-test).
void makeAbsoluteName(std::string& fileName) { fileName = hostpath::makeAbsolute(fileName); }
} // namespace

// Canonicalisation PHYSIQUE (realpath, liens résolus) — définie plus bas (scope global
// static, hors namespace anonyme) ; déclarée ici pour que initDrives puisse rendre
// hdEmuDir canonique dès le montage (cf. son usage).
static std::string physicalCanon(const std::string& path);

// -----------------------------------------------------------------------------
//  Construction / cycle de vie
// -----------------------------------------------------------------------------
GemdosHd::GemdosHd(Bus& bus, Cpu68k& cpu) : bus_(bus), cpu_(cpu) {}

GemdosHd::~GemdosHd() { clearAllFileHandles(); }

bool GemdosHd::setDirectory(const std::string& hostDir) {
    if (!dirExists(hostDir)) {
        std::fprintf(stderr, "[gemdos] folder not found or not a folder: %s\n", hostDir.c_str());
        return false;
    }
    trace_ = getenv("NEOST_GEMDOS_TRACE") != nullptr;
    // Le dossier monté doit être ABSOLU (comme Configuration_Apply chez Hatari, qui
    // applique File_CleanFileName PUIS File_MakeAbsoluteName avant GemDOS_InitDrives).
    // Gardé relatif, il ne pouvait jamais préfixer le chemin absolu que gemChDir
    // compare : TOUT Dsetpath échouait en EPTHNF, y compris « C:\ » — un installeur
    // qui entre dans un sous-dossier avant d'écrire déversait alors ses fichiers à la
    // racine du lecteur. La GUI masquait le bug (elle résout déjà le chemin), pas le
    // headless (« --gemdos ../rel/dir »).
    std::string absDir = hostDir;
    makeAbsoluteName(absDir);
    // VALIDER AVANT DE DÉTRUIRE : initDrives ferme les fichiers ouverts, purge les DTA
    // et vide la table de lecteurs. L'appeler puis constater « aucun lecteur mappé »
    // laissait le montage PRÉCÉDENT en miettes — un simple dossier disparu (clé USB
    // retirée) suffisait à tuer le C: de la session, sans reset ni retour en arrière.
    if (countMappableDrives(absDir) == 0) {
        std::fprintf(stderr, "[gemdos] no GEMDOS drive mapped from %s "
                     "(current mount kept)\n", hostDir.c_str());
        return false;
    }
    initDrives(absDir);
    // Installe la cartouche système ($FA0000) : le TOS exécutera son C-INIT au boot.
    // ⚠ Écrase une éventuelle cartouche utilisateur (même stockage) : on efface donc
    // AUSSI son chemin, sans quoi unmount() croirait plus tard qu'une cartouche
    // fichier est branchée et laisserait la cartouche système en place.
    bus_.cart.assign(Cart_data, Cart_data + sizeof(Cart_data));
    bus_.clearCartPath();
    bus_.gemdos = this;
    active_ = true;
    currentDrive_ = (uint16_t)bootDrive_;
    for (auto& d : emudrives_)
        if (d.used)
            std::fprintf(stderr, "[gemdos] HDD GEMDOS : %c: <-> %s\n",
                         'A' + d.driveNumber, d.hdEmuDir.c_str());
    return true;
}

void GemdosHd::unmount() {
    if (!active_) return;
    clearAllFileHandles();
    for (auto& d : emudrives_) { d.used = false; d.driveNumber = -1; d.hdEmuDir.clear(); }
    nDrives_ = 0;
    connectedDriveMask_ = 0;
    bootDrive_ = 0;
    currentDrive_ = 0;
    bus_.gemdos = nullptr;
    // Ne retirer QUE la cartouche système GEMDOS — reconnaissable à son chemin VIDE
    // (elle est posée en mémoire par setDirectory, pas chargée d'un fichier). Vider
    // inconditionnellement effaçait une cartouche UTILISATEUR fraîchement branchée,
    // les deux partageant le même stockage bus_.cart ($FA0000).
    if (bus_.mountedCartPath().empty()) bus_.ejectCart();
    active_ = false;
    std::fprintf(stderr, "[gemdos] GEMDOS HDD unmounted\n");
}

void GemdosHd::reset() {
    initGemdos_ = false;
    initCurPaths();
    actPd_ = 0;
    currentDrive_ = (uint16_t)bootDrive_;
    clearAllFileHandles();
}

// -----------------------------------------------------------------------------
//  Accès mémoire ST (port des helpers STMemory_*)
// -----------------------------------------------------------------------------
// ⚠ NON-FAUTIVES, comme les STMemory_* d'Hatari : ces helpers tournent HORS de
// Moira::execute(), donc hors de tout try/catch — un pointeur invité forgé vers
// une MMIO fautive ($FF8604, $FF9200…) jetait moira::BusError jusqu'à
// std::terminate (crash de l'émulateur entier). Lecture : RAM + ROM ; écriture :
// RAM seule. Hors zone → 0 / écriture ignorée, l'appel GEMDOS échoue proprement
// par ses propres chemins d'erreur. Bonus : plus d'effet de bord MMIO (une
// lecture ACIA consommait l'octet RX pendant un trap).
uint8_t  GemdosHd::readByte (uint32_t a) { return checkArea(a, 1, true) ? bus_.read8(a)  : 0; }
uint16_t GemdosHd::readWord (uint32_t a) { return checkArea(a, 2, true) ? bus_.read16(a) : 0; }
uint32_t GemdosHd::readLong (uint32_t a) { return checkArea(a, 4, true) ? bus_.read32(a) : 0; }
void GemdosHd::writeByte(uint32_t a, uint8_t  v) { if (checkArea(a, 1, false)) bus_.write8(a, v); }
void GemdosHd::writeWord(uint32_t a, uint16_t v) { if (checkArea(a, 2, false)) bus_.write16(a, v); }
void GemdosHd::writeLong(uint32_t a, uint32_t v) { if (checkArea(a, 4, false)) bus_.write32(a, v); }
void GemdosHd::flushCache() { bus_.megaSteCacheFlushIfEnabled(); }

bool GemdosHd::checkArea(uint32_t addr, uint32_t size, bool allowRom) {
    if (size == 0) size = 1;
    if (bus_.hostRamPtr(addr, size)) return true;
    if (allowRom) {
        uint32_t a = addr & 0x00FFFFFF;
        if (a >= bus_.romBase && a + size <= bus_.romBase + bus_.romWindowSize())
            return true;
    }
    return false;
}

bool GemdosHd::getString(uint32_t addr, std::string& out) {
    out.clear();
    for (int i = 0; i < 0x10000; i++) {
        if (!checkArea(addr + i, 1, /*allowRom=*/true)) return false;
        uint8_t c = readByte(addr + i);
        if (c == 0) return true;
        out.push_back((char)c);
    }
    return false;
}

// -----------------------------------------------------------------------------
//  Accès registres 68000
// -----------------------------------------------------------------------------
uint32_t GemdosHd::reg(int idx)            { return cpu_.reg(idx); }
void     GemdosHd::setDreg(int n, uint32_t v) { cpu_.setReg(n, v); }
void     GemdosHd::setAreg(int n, uint32_t v) { cpu_.setReg(8 + n, v); }
uint16_t GemdosHd::getSR()                 { return cpu_.sr(); }
void     GemdosHd::setSR(uint16_t v)       { cpu_.setSr(v); }
uint32_t GemdosHd::getUSP()                { return cpu_.usp(); }
uint32_t GemdosHd::getPC()                 { return cpu_.pc(); }

// Raccourci : pose D0 (valeur signée → 32 bits).
static inline void setD0(Cpu68k& cpu, int32_t v) { cpu.setReg(0, (uint32_t)v); }

// -----------------------------------------------------------------------------
//  Initialisation des lecteurs (port de GemDOS_InitDrives + helpers)
// -----------------------------------------------------------------------------
static bool hostDriveFolderExists(std::string& path, int drive) {
    if (access(path.c_str(), F_OK) != 0) {           // essaye la lettre en minuscule
        if (!path.empty()) path.back() = (char)tolower((unsigned char)path.back());
    }
    if (drive > 1 && access(path.c_str(), F_OK) == 0) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;
    }
    return false;
}

// Détermine le nombre max de partitions (GemDOS_DetermineMaxPartitions).
static bool determineMaxPartitions(const std::string& dir, int& maxDrives) {
    maxDrives = 0;
    DIR* d = opendir(dir.c_str());
    if (!d) {
        std::fprintf(stderr, "[gemdos] cannot access: %s\n", dir.c_str());
        return false;
    }
    int count = 0, last = 0;
    bool multi = true;
    struct dirent* e;
    std::vector<std::string> names;
    while ((e = readdir(d))) names.push_back(e->d_name);
    closedir(d);
    count = (int)names.size();
    if (count <= 2) {                                // « . » et « .. » seuls
        last = 1; multi = false;
    } else {
        last = 0;
        for (auto& n : names) {
            char letter = (char)toupper((unsigned char)n[0]);
            if (!letter || letter == '.') continue;
            if (letter < 'C' || letter > 'Z' || n[1]) { last = 1; multi = false; break; }
            letter = letter - 'C' + 1;
            if (letter > last) last = letter;
        }
    }
    maxDrives = (last > 24) ? 24 : last;
    return multi;
}

// Combien de lecteurs `hostDir` mapperait-il ? MÊMES règles qu'initDrives, mais SANS
// aucun effet de bord — indispensable pour valider AVANT de détruire : initDrives ferme
// les fichiers ouverts, purge les DTA et vide la table de lecteurs, PUIS seulement on
// découvrait qu'aucun lecteur n'était mappable. Un setDirectory en échec (dossier
// disparu, clé USB retirée, faute de frappe) tuait donc le C: en cours de session.
int GemdosHd::countMappableDrives(const std::string& hostDir) {
    int nMaxDrives = 0;
    const bool multi = determineMaxPartitions(hostDir, nMaxDrives);
    int n = 0;
    for (int i = 0; i < nMaxDrives && i < MAX_HARDDRIVES; i++) {
        std::string dir = hostDir;
        cleanFileName(dir);
        if (multi) { dir.push_back(PATHSEP); dir.push_back((char)('C' + i)); }
        if (hostDriveFolderExists(dir, 2 + i)) ++n;
    }
    return n;
}

void GemdosHd::initDrives(const std::string& hostDir) {
    clearAllFileHandles();
    clearAllInternalDTAs();
    for (auto& d : emudrives_) { d.used = false; d.driveNumber = -1; }
    nDrives_ = 0;
    connectedDriveMask_ = 0;

    int nMaxDrives = 0;
    bool multi = determineMaxPartitions(hostDir, nMaxDrives);

    for (int i = 0; i < nMaxDrives && i < MAX_HARDDRIVES; i++) {
        EmuDrive& d = emudrives_[i];
        d.hdEmuDir = hostDir;
        cleanFileName(d.hdEmuDir);
        if (multi) { d.hdEmuDir.push_back(PATHSEP); d.hdEmuDir.push_back((char)('C' + i)); }
        int driveNumber = 2 + i;
        if (hostDriveFolderExists(d.hdEmuDir, driveNumber)) {
            // SÉCURITÉ/correction — canonicaliser PHYSIQUEMENT la racine du lecteur au
            // montage (le dossier vient d'être confirmé existant). Sans ça, hdEmuDir
            // restait LEXICAL (makeAbsoluteName seul) alors que tout `host` produit par
            // clampToSandbox est physicalCanon() (liens résolus) : sur un montage dont
            // le chemin traverse un lien symbolique (macOS /tmp→/private/tmp, ~ ou /var
            // symlinkés sous Linux), les deux vivaient dans des espaces de noms distincts.
            // Conséquences RÉELLES : gemChDir comparait host canonique vs hdEmuDir brut
            // → Dsetpath échouait toujours en EPTHNF (« Impossible de définir le dossier »),
            // et gemSFirst("C:\\") calculait rootLen sur la longueur brute → scan du dossier
            // PARENT du bac à sable. Canonicaliser ici met TOUTES les comparaisons
            // host↔hdEmuDir (gemChDir, gemSFirst, gemDgetpath, createHostFileName) dans le
            // même espace de noms. Idempotent (physicalCanon(canon) == canon).
            d.hdEmuDir = physicalCanon(d.hdEmuDir);
            d.driveNumber = driveNumber;
            d.used = true;
            connectedDriveMask_ |= (1u << driveNumber);
            nDrives_++;
        } else {
            d.used = false;
        }
    }
    initCurPaths();
    // Lecteur de boot : C: si présent, sinon A:. (Affiné par _bootdev au boot.)
    bootDrive_ = 0;
    if (emudrives_[0].used) bootDrive_ = 2;
}

void GemdosHd::initCurPaths() {
    for (auto& d : emudrives_) {
        if (d.used) { d.fsCurrPath = d.hdEmuDir; addSlash(d.fsCurrPath); }
    }
}

// -----------------------------------------------------------------------------
//  Gestion des handles de fichiers
// -----------------------------------------------------------------------------
void GemdosHd::closeFileHandle(int i) {
    if (fileHandles_[i].used && fileHandles_[i].fp) fclose(fileHandles_[i].fp);
    fileHandles_[i].fp = nullptr;
    fileHandles_[i].basepage = 0;
    fileHandles_[i].used = false;
}
void GemdosHd::unforceFileHandle(int i) { forced_[i].handle = -1; forced_[i].basepage = 0; }
void GemdosHd::clearAllFileHandles() {
    for (int i = 0; i < MAX_FILE_HANDLES; i++) closeFileHandle(i);
    for (int i = 0; i < FORCED_HANDLES; i++) unforceFileHandle(i);
}
int GemdosHd::findFreeFileHandle() {
    for (int i = 0; i < MAX_FILE_HANDLES; i++) if (!fileHandles_[i].used) return i;
    return -1;
}

void GemdosHd::clearInternalDTA(int idx) {
    dtas_[idx].found.clear();
    dtas_[idx].used = false;
}
void GemdosHd::clearAllInternalDTAs() {
    if (dtas_.empty()) dtas_.resize(DTA_CACHE_INC);
    for (size_t i = 0; i < dtas_.size(); i++) clearInternalDTA((int)i);
    dtaIndex_ = 0;
}

// -----------------------------------------------------------------------------
//  Basepage / validation de handle (GemDOS_BasepageMatches, GetValidFileHandle)
// -----------------------------------------------------------------------------
bool GemdosHd::basepageMatches(uint32_t checkbase) {
    int maxparents = 12;
    uint32_t basepage = readLong(actPd_);
    while (maxparents-- > 0 && checkArea(basepage, 0x100, false)) {
        if (basepage == checkbase) return true;
        basepage = readLong(basepage + 0x24);      // BASEPAGE_OFFSET_PARENT
    }
    return false;
}

int GemdosHd::getValidFileHandle(int handle) {
    int forced = -1;
    if (handle >= 0 && handle < FORCED_HANDLES && forced_[handle].handle != -1) {
        if (basepageMatches(forced_[handle].basepage)) {
            forced = handle;
            handle = forced_[handle].handle;
        } else {
            unforceFileHandle(handle);
            return -1;
        }
    } else {
        handle -= BASE_FILEHANDLE;
    }
    if (handle >= 0 && handle < MAX_FILE_HANDLES && fileHandles_[handle].used) {
        uint32_t current = readLong(actPd_);
        if (fileHandles_[handle].basepage == current || forced >= 0) return handle;
        return handle;   // accès croisé (bug programme) : on tolère comme Hatari
    }
    return -1;
}

// -----------------------------------------------------------------------------
//  Résolution de lecteur (GemDOS_FindDriveNumber / IsDriveEmulated / …)
// -----------------------------------------------------------------------------
int GemdosHd::findDriveNumber(const std::string& name) {
    if (name.size() >= 2 && name[1] == ':') {
        char letter = (char)toupper((unsigned char)name[0]);
        if (letter >= 'A' && letter <= 'Z') return letter - 'A';
    } else if (name.size() == 4 && name[3] == ':') {
        return 0;
    }
    return currentDrive_;
}
bool GemdosHd::isDriveEmulated(int drive) {
    drive -= 2;
    if (drive < 0 || drive >= MAX_HARDDRIVES) return false;
    return emudrives_[drive].used;
}
int GemdosHd::fileName2HardDriveID(const std::string& name) {
    if (active_) {
        int drv = findDriveNumber(name);
        if (isDriveEmulated(drv)) return drv;
    }
    return -1;   // pas un lecteur redirigé → TOS
}

// -----------------------------------------------------------------------------
//  Conversion d'un nom GEMDOS en chemin hôte (port de gemdos.c)
// -----------------------------------------------------------------------------
std::string GemdosHd::matchHostDirEntry(const std::string& path, const std::string& name,
                                        bool pattern, bool onlyInvalid) {
    DIR* dir = opendir(path.c_str());
    if (!dir) return "";
    std::string match;
    struct dirent* e;
    while ((e = readdir(dir))) {
        // macOS rend les noms en forme DÉCOMPOSÉE (NFD) : on recompose AVANT de
        // comparer, et on retient le nom RECOMPOSÉ (≙ gemdos.c:1201 et 1214, qui
        // convertit d_name sur place puis le strdup). Sans ça, un fichier accentué
        // du lecteur GEMDOS est introuvable depuis le TOS. Nul sur Linux.
        const std::string dnBuf = neost::hostpath::precomposeUtf8(e->d_name);
        const char* dn = dnBuf.c_str();
        if (pattern) { if (fsfirst_match(name.c_str(), dn, /*subdir=*/false, onlyInvalid)) { match = dn; break; } }
        else          { if (strcasecmp(name.c_str(), dn) == 0) { match = dn; break; } }
    }
    closedir(dir);
    return match;
}

void GemdosHd::addRemainingPath(const std::string& src, std::string& dstpath) {
    size_t i = dstpath.size();
    dstpath += src;                              // (conversion charset off → copie)
    for (size_t k = i; k < dstpath.size(); k++)
        if (dstpath[k] == '\\') dstpath[k] = PATHSEP;
}

bool GemdosHd::addPathComponent(std::string& path, const std::string& origname, bool isDir) {
    path.push_back(PATHSEP);
    std::string name = origname;
    int namelen = clip_to_83(name);

    std::string match = matchHostDirEntry(path, name, false);
    if (!match.empty()) { path += match; return true; }

    // Contournement bug TOS 1.02 : dossier de 8 caractères + '.' final.
    if (isDir && namelen == 9 && name[8] == '.') {
        name.resize(8);
        match = matchHostDirEntry(path, name, false);
        if (!match.empty()) { path += match; return true; }
    }

    // Passe TRONCATURE : un nom Atari a pu être tronqué à 8+3 — on transforme
    // « emulated.too » → « emulated*.too* » etc. et on retente avec un masque.
    // Les '+' (INVALID_CHAR) restent LITTÉRAUX ici (cf. Hatari add_path_component).
    bool usePattern = false;
    int dot = 0;
    while (dot < (int)name.size() && name[dot] != '.') dot++;
    if (namelen - dot > 3) { name.push_back('*'); namelen++; usePattern = true; }
    if (namelen > 8 && (int)name.size() > 8 && name[8] == '.') {
        name.insert(name.begin() + 8, '*'); namelen++; usePattern = true;
    } else if (namelen == 8 && dot >= (int)name.size()) {
        name.push_back('*'); namelen++; usePattern = true;
    }

    if (usePattern) {
        match = matchHostDirEntry(path, name, /*pattern=*/true, /*onlyInvalid=*/false);
        if (!match.empty()) { path += match; return true; }
    }

    // Passe CARACTÈRES INVALIDES : les '+' deviennent des '?' qui ne matchent QUE
    // des caractères réellement invalides côté hôte (onlyInvalid), pour ne pas
    // attraper un nom valide par accident (cf. Hatari, passe séparée de la troncature).
    usePattern = false;
    for (char& c : name) if (c == INVALID_CHAR) { c = '?'; usePattern = true; }
    if (usePattern) {
        match = matchHostDirEntry(path, name, /*pattern=*/true, /*onlyInvalid=*/true);
        if (!match.empty()) { path += match; return true; }
    }

    // Pas trouvé : ajoute le nom (avec conversion de casse éventuelle).
    std::string conv;
    for (char c : origname) {
        if      (caseConv_ == 1) conv.push_back((char)toupper((unsigned char)c));
        else if (caseConv_ == 2) conv.push_back((char)tolower((unsigned char)c));
        else                     conv.push_back(c);
    }
    path += conv;
    return false;
}


// SÉCURITÉ — canonicalisation PHYSIQUE d'un chemin qui n'existe pas forcément (Fcreate
// crée le fichier APRÈS). realpath() ne s'applique qu'à l'existant : on remonte donc au
// plus long préfixe existant, on le canonicalise, puis on recolle le reste. Indispensable
// face aux LIENS SYMBOLIQUES : la canonicalisation lexicale (makeAbsoluteName) les ignore,
// si bien qu'un simple lien posé dans le dossier monté — cas courant, un raccourci vers
// $HOME — rendait lisible ET inscriptible toute sa cible, hors du bac à sable.
static std::string physicalCanon(const std::string& path) {
    std::string head = path, tail;
    for (;;) {
#if defined(_WIN32)
        // Pas de realpath() : std::filesystem::canonical résout de la même façon les
        // liens (jonctions/liens NTFS) et les « . / .. ». On NORMALISE les séparateurs
        // en '/' derrière, car tout le reste du fichier compare avec PATHSEP — un
        // retour en '\\' ferait échouer le test de préfixe du bac à sable, donc
        // rabattrait chaque accès sur la racine. generic_string() fait exactement ça.
        std::error_code cec;
        const std::filesystem::path cp = std::filesystem::canonical(head, cec);
        const bool ok = !cec;
        std::string res = ok ? cp.generic_string() : std::string();
        if (ok) {
#else
        char* rp = ::realpath(head.c_str(), nullptr);
        if (rp) { std::string res(rp); std::free(rp);
#endif
                  if (!tail.empty()) { if (res.empty() || res.back() != PATHSEP) res.push_back(PATHSEP);
                                       res += tail; }
                  return res; }
        const std::size_t sep = head.rfind(PATHSEP);
        if (sep == std::string::npos || sep == 0) return path;   // rien d'existant : repli
        tail = tail.empty() ? head.substr(sep + 1)
                            : head.substr(sep + 1) + std::string(1, PATHSEP) + tail;
        head.resize(sep);
    }
}

// SÉCURITÉ — garde-fou terminal, INDÉPENDANT du parsing : quel que soit le chemin demandé
// par le programme émulé, le résultat doit rester sous le dossier monté. Appelé aux TROIS
// sorties de createHostFileName — les deux retours anticipés recopient le reste du chemin
// BRUT (« .. » compris) et sautaient donc l'ancien garde placé en fin de fonction.
// Comparaison sur des chemins canonicalisés PHYSIQUEMENT (liens symboliques résolus) et
// préfixe testé SÉPARATEUR INCLUS, sans quoi un dossier frère « …/gemdosEVIL » passerait
// pour « …/gemdos ». En cas d'évasion on rabat sur la racine du lecteur — même issue que
// le clamp « sep >= minlen » des « ..\ » — et on le journalise TOUJOURS : NeoST lance des
// binaires inconnus, une tentative d'évasion est un signal de sécurité, pas une trace.
void GemdosHd::clampToSandbox(const EmuDrive& d, const std::string& gemName, std::string& out) {
    std::string root = physicalCanon(d.hdEmuDir);
    addSlash(root);
    // Deux étapes, dans CET ordre : makeAbsoluteName retire d'abord les « .. » (le
    // préfixe existant peut être suivi d'un suffixe INEXISTANT que realpath ne peut pas
    // traiter — « SUB/NOPE/../../../X » ressortait avec ses « .. » intacts et passait le
    // test de préfixe alors que l'OS, lui, les résout hors du bac à sable), puis
    // physicalCanon résout les liens symboliques.
    std::string lex = out;
    makeAbsoluteName(lex);
    const std::string canon = physicalCanon(lex);
    const bool isRoot = (canon.size() + 1 == root.size() && root.compare(0, canon.size(), canon) == 0);
    if (isRoot || canon.compare(0, root.size(), root) == 0) { out = canon; return; }
    const std::size_t sep = canon.rfind(PATHSEP);
    const std::string base = (sep == std::string::npos) ? canon : canon.substr(sep + 1);
    std::fprintf(stderr, "[gemdos] REFUSED: '%s' escaped the drive (%s) — clamped to the root\n",
                 gemName.c_str(), canon.c_str());
    out = root + base;
}

void GemdosHd::createHostFileName(int drive, const std::string& gemNameIn, std::string& out) {
    out.clear();
    EmuDrive& d = emudrives_[drive - 2];
    // SÉCURITÉ — normalisation du séparateur. Côté GEMDOS le séparateur est « \ » et
    // « / » est un caractère de nom INVALIDE (cf. filenameInvalidChar, porté de
    // Str_Filename_Invalid_Char) ; côté hôte « / » EST le séparateur. Un nom hostile
    // « ../X » (ou « C:/../X ») traversait donc le parseur ci-dessous intact — aucun
    // strchr('\\') ne le voyait — et ressortait tel quel dans le chemin hôte, où le
    // « .. » était résolu pour de bon : lecture, écriture ET listage hors du dossier
    // monté, avec les droits de l'utilisateur. On le convertit en séparateur GEMDOS
    // dès l'entrée, ce qui le soumet au garde-fou « sep >= minlen » comme tout « ..\ ».
    std::string gemName = gemNameIn;
    for (char& c : gemName) if (c == '/') c = '\\';
    const char* filename = gemName.c_str();
    if (filename[0] == '\0') return;

    if (filename[1] == ':')      { out = d.hdEmuDir; filename += 2; }
    else if (filename[0] == '\\') { out = d.hdEmuDir; }
    else                          { out = d.fsCurrPath; }

    size_t minlen = d.hdEmuDir.size();
    cleanFileName(out);

    for (;;) {
        while (*filename == '\\') filename++;
        if (filename[0] == '.' && (filename[1] == '\\' || !filename[1])) { filename++; continue; }
        if (filename[0] == '.' && filename[1] == '.' &&
            (filename[2] == '\\' || !filename[2])) {
            size_t sep = out.rfind(PATHSEP);
            if (sep != std::string::npos && sep >= minlen) out.resize(sep);
            filename += 2;
            continue;
        }
        const char* s = strchr(filename, '\\');
        if (s) {
            std::string dirname(filename, (size_t)(s - filename));
            filename = s;
            if (!addPathComponent(out, dirname, true)) {
                addRemainingPath(filename, out);
                clampToSandbox(d, gemNameIn, out);   // le reste du chemin est recopié BRUT (« .. » compris)
                return;
            }
            continue;
        }
        break;
    }

    if (*filename) {
        if (strchr(filename, '?') || strchr(filename, '*')) {
            out.push_back(PATHSEP);
            out += filename;                       // (conversion charset off)
        } else if (!addPathComponent(out, filename, false)) {
            if (trace_) std::fprintf(stderr, "[gemdos] not found: %s\n", out.c_str());
            clampToSandbox(d, gemNameIn, out);
            return;
        }
    }
    clampToSandbox(d, gemNameIn, out);
    if (trace_) std::fprintf(stderr, "[gemdos] %s -> %s\n", gemNameIn.c_str(), out.c_str());
}

// -----------------------------------------------------------------------------
//  Opérations GEMDOS
// -----------------------------------------------------------------------------
bool GemdosHd::gemSetDrv(uint32_t p) {
    currentDrive_ = readWord(p);
    return false;   // redirige aussi vers TOS
}

bool GemdosHd::gemDFree(uint32_t p) {
    uint32_t address = readLong(p);
    int drive = (int16_t)readWord(p + 4);
    if (drive == 0) drive = currentDrive_; else drive--;
    if (!isDriveEmulated(drive)) return false;
    if (!checkArea(address, 16, false)) { setD0(cpu_, GEMDOS_ERANGE); return true; }

    uint64_t total = 32 * 1024, freeC = 16 * 1024;   // défaut : 32 Mo / 16 Mo libres
#if defined(__unix__) || defined(__APPLE__)
    // Espace RÉEL du disque hôte (port GemDOS_DFree, gemdos.c:1692-1746) : les
    // valeurs factices trompaient « Informations disque » et tout installeur qui
    // vérifie Dfree avant d'extraire. Clusters de 1 Ko (secteurs 512 × 2, cf. bas).
    {
        struct statvfs sv;
        if (statvfs(emudrives_[drive - 2].hdEmuDir.c_str(), &sv) == 0) {
            const uint64_t frsize = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
            total = sv.f_blocks * frsize / 1024;
            freeC = sv.f_bavail * frsize / 1024;
        }
    }
#elif defined(_WIN32)
    // Même intention que statvfs ci-dessus : sans espace RÉEL, « Informations disque »
    // et les installeurs qui vérifient Dfree avant d'extraire se font tromper par les
    // valeurs factices. GetDiskFreeSpaceExW rend des OCTETS (pas des blocs) et le
    // premier paramètre accepte un dossier quelconque du volume.
    {
        ULARGE_INTEGER avail{}, tot{}, dummy{};
        const std::wstring dir =
            std::filesystem::path(emudrives_[drive - 2].hdEmuDir).wstring();
        if (GetDiskFreeSpaceExW(dir.c_str(), &avail, &tot, &dummy)) {
            total = uint64_t(tot.QuadPart) / 1024;
            freeC = uint64_t(avail.QuadPart) / 1024;
        }
    }
#endif
    {
        unsigned tosMax;
        uint16_t tv = bus_.tosVersion;
        if (tv >= 0x0400) tosMax = 1024 * 1024;
        else if (tv >= 0x0106) tosMax = 512 * 1024;
        else tosMax = 256 * 1024;
        if (total > tosMax) total = tosMax;
        if (freeC > total) freeC = total;
        if (total == 0) total = tosMax;
    }
    flushCache();
    writeLong(address, (uint32_t)freeC);            // clusters libres
    writeLong(address + 4, (uint32_t)total);        // clusters totaux
    writeLong(address + 8, 512);                    // octets/secteur
    writeLong(address + 12, 2);                      // secteurs/cluster (1 Ko)
    setD0(cpu_, GEMDOS_EOK);
    return true;
}

bool GemdosHd::gemMkDir(uint32_t p) {
    std::string name;
    if (!getString(readLong(p), name) || name.empty()) return false;
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;
    if (writeProtect_) { setD0(cpu_, GEMDOS_EWRPRO); return true; }
    std::string host; createHostFileName(drive, name, host);
    if (mkdir(host.c_str(), 0755) == 0) setD0(cpu_, GEMDOS_EOK);
    else setD0(cpu_, errno2gemdos(errno, true));
    return true;
}

bool GemdosHd::gemRmDir(uint32_t p) {
    std::string name;
    if (!getString(readLong(p), name) || name.empty()) return false;
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;
    if (writeProtect_) { setD0(cpu_, GEMDOS_EWRPRO); return true; }
    std::string host; createHostFileName(drive, name, host);
    if (rmdir(host.c_str()) == 0) setD0(cpu_, GEMDOS_EOK);
    else setD0(cpu_, errno2gemdos(errno, true));
    return true;
}

bool GemdosHd::gemChDir(uint32_t p) {
    std::string name;
    if (!getString(readLong(p), name)) { setD0(cpu_, GEMDOS_EPTHNF); return true; }
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;
    if (name.empty()) { setD0(cpu_, GEMDOS_EOK); return true; }

    std::string host; createHostFileName(drive, name, host);
    cleanFileName(host);
    // Divergence NeoST (vs Hatari, plus fidèle au vrai TOS) : le bureau TOS 1.62
    // ouvre un dossier en passant le CHEMIN AVEC son masque de fichier, p.ex.
    // « C:\DOSSIER\*.* ». Dsetpath ne définit qu'un RÉPERTOIRE : on retire un
    // dernier composant contenant un joker (createHostFileName l'a recopié tel
    // quel). Sans ça, access() échoue sur « …/DOSSIER/*.* » → EPTHNF, et le
    // bureau affiche « Impossible de définir le dossier par défaut » (Hatari a le
    // même défaut : le masque n'y est pas retiré non plus).
    {
        size_t sep = host.rfind(PATHSEP);
        if (sep != std::string::npos &&
            host.find_first_of("*?", sep) != std::string::npos)
            host.resize(sep ? sep : 1);   // garde au moins la racine « / »
    }
    if (access(host.c_str(), F_OK) != 0) { setD0(cpu_, GEMDOS_EPTHNF); return true; }
    addSlash(host);
    makeAbsoluteName(host);

    EmuDrive& d = emudrives_[drive - 2];
    // Préfixe testé SÉPARATEUR INCLUS (comme clampToSandbox) : le test nu laissait
    // passer un frère « <racine>EVIL/ ». Défense en profondeur — host sort toujours
    // de createHostFileName→clampToSandbox, donc déjà sous la racine aujourd'hui.
    std::string root = d.hdEmuDir;
    addSlash(root);
    if (host.compare(0, root.size(), root) == 0) {
        d.fsCurrPath = host;
        setD0(cpu_, GEMDOS_EOK);
    } else {
        setD0(cpu_, GEMDOS_EPTHNF);
    }
    return true;
}

// Vérifie si le DOSSIER d'un fichier hôte est absent (GemDOS_FilePathMissing).
static bool filePathMissing(std::string fileName) {
    size_t sep = fileName.rfind(PATHSEP);
    if (sep != std::string::npos) {
        fileName.resize(sep);
        if (!dirExists(fileName)) return true;
    }
    return false;
}

bool GemdosHd::gemCreate(uint32_t p) {
    std::string name;
    int mode = readWord(p + 4);
    if (!getString(readLong(p), name) || name.empty()) return false;
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;
    if (isVolumeLabel(mode)) { setD0(cpu_, GEMDOS_EFILNF); return true; }
    if (writeProtect_) { setD0(cpu_, GEMDOS_EWRPRO); return true; }

    std::string host; createHostFileName(drive, name, host);
    int idx = findFreeFileHandle();
    if (idx == -1) { setD0(cpu_, GEMDOS_ENHNDL); return true; }

    fileHandles_[idx].fp = fopen(host.c_str(), "wb+");
    if (fileHandles_[idx].fp) {
        fileHandles_[idx].readOnly = false;
        if (mode & FA_READONLY) {
            fileHandles_[idx].readOnly = true;
            chmod(host.c_str(), S_IRUSR | S_IRGRP | S_IROTH);
        }
        fileHandles_[idx].used = true;
        std::strcpy(fileHandles_[idx].mode, "wb+");
        fileHandles_[idx].basepage = readLong(actPd_);
        fileHandles_[idx].actualName = host;
        setD0(cpu_, idx + BASE_FILEHANDLE);
        return true;
    }
    if (errno == EACCES || errno == EROFS || errno == EPERM || errno == EISDIR) {
        setD0(cpu_, GEMDOS_EACCDN); return true;
    }
    if (errno == ENOTDIR || filePathMissing(host)) { setD0(cpu_, GEMDOS_EPTHNF); return true; }
    setD0(cpu_, GEMDOS_EFILNF);
    return true;
}

bool GemdosHd::gemOpen(uint32_t p) {
    std::string name;
    int mode = readWord(p + 4) & 3;
    if (!getString(readLong(p), name) || name.empty()) return false;
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;

    int idx = findFreeFileHandle();
    if (idx == -1) { setD0(cpu_, GEMDOS_ENHNDL); return true; }

    std::string host; createHostFileName(drive, name, host);
    const char* modeStr;
    if (writeProtect_ ||
        (access(host.c_str(), F_OK) == 0 && access(host.c_str(), W_OK) != 0)) {
        modeStr = "rb";
        fileHandles_[idx].readOnly = true;
    } else {
        modeStr = "rb+";
        fileHandles_[idx].readOnly = (mode == 0);
    }
    fileHandles_[idx].fp = fopen(host.c_str(), modeStr);

    if (fileHandles_[idx].fp) {
        fileHandles_[idx].used = true;
        std::strcpy(fileHandles_[idx].mode, modeStr);
        fileHandles_[idx].basepage = readLong(actPd_);
        fileHandles_[idx].actualName = host;
        setD0(cpu_, idx + BASE_FILEHANDLE);
        return true;
    }
    if (errno == EACCES || errno == EROFS || errno == EPERM || errno == EISDIR)
        setD0(cpu_, GEMDOS_EACCDN);
    else if (errno == ENOTDIR || filePathMissing(host))
        setD0(cpu_, GEMDOS_EPTHNF);
    else
        setD0(cpu_, GEMDOS_EFILNF);
    return true;
}

bool GemdosHd::gemClose(uint32_t p) {
    int handle = readWord(p);
    if ((handle = getValidFileHandle(handle)) < 0) return false;
    closeFileHandle(handle);
    for (int i = 0; i < FORCED_HANDLES; i++)
        if (forced_[i].handle == handle) unforceFileHandle(i);
    setD0(cpu_, GEMDOS_EOK);
    return true;
}

bool GemdosHd::gemRead(uint32_t p) {
    int handle = readWord(p);
    uint32_t size = readLong(p + 2);
    uint32_t addr = readLong(p + 6);
    if ((handle = getValidFileHandle(handle)) < 0) return false;

    if (bus_.tosVersion < 0x400 && (size & 0x80000000)) { setD0(cpu_, -1); return true; }

    FILE* fp = fileHandles_[handle].fp;
    off_t cur = ftello(fp);
    if (cur == -1 || fseeko(fp, 0, SEEK_END) != 0) { setD0(cpu_, GEMDOS_E_SEEK); return true; }
    off_t fsize = ftello(fp);
    if (fsize == -1 || fseeko(fp, cur, SEEK_SET) != 0) { setD0(cpu_, GEMDOS_E_SEEK); return true; }
    off_t left = fsize - cur;
    if ((int32_t)size <= 0 || left <= 0) { setD0(cpu_, 0); return true; }
    if (size > (uint32_t)left) size = (uint32_t)left;
    if (!checkArea(addr, size, false)) { setD0(cpu_, GEMDOS_ERANGE); return true; }

    long nread;
    uint8_t* dst = bus_.hostRamPtr(addr, size);
    if (dst) {
        nread = (long)fread(dst, 1, size, fp);
    } else {
        std::vector<uint8_t> tmp(size);
        nread = (long)fread(tmp.data(), 1, size, fp);
        for (long i = 0; i < nread; i++) writeByte(addr + i, tmp[i]);
    }
    flushCache();
    if (ferror(fp)) { setD0(cpu_, errno2gemdos(errno, false)); clearerr(fp); }
    else setD0(cpu_, (int32_t)nread);
    return true;
}

bool GemdosHd::gemWrite(uint32_t p) {
    int handle = readWord(p);
    int32_t size = (int32_t)readLong(p + 2);
    uint32_t addr = readLong(p + 6);
    int fh = getValidFileHandle(handle);
    if (fh < 0) return false;                          // (mode test sans TOS omis)
    if (writeProtect_) { setD0(cpu_, GEMDOS_EWRPRO); return true; }
    FILE* fp = fileHandles_[fh].fp;
    // Taille NÉGATIVE → ERANGE comme Hatari (le Size int32 négatif y échoue au
    // contrôle mémoire non-signé, gemdos.c:2477) ; l'ancien « size = 0 » rendait
    // D0=0 (« 0 octet écrit ») pour une longueur qui a débordé en négatif.
    if (size < 0) { setD0(cpu_, GEMDOS_ERANGE); return true; }
    if (!checkArea(addr, size, /*allowRom=*/true)) { setD0(cpu_, GEMDOS_ERANGE); return true; }

    const uint8_t* src = bus_.hostRamPtr(addr, size);
    std::vector<uint8_t> tmp;
    if (!src && size > 0) {
        tmp.resize(size);
        for (int i = 0; i < size; i++) tmp[i] = readByte(addr + i);
        src = tmp.data();
    }
    fseek(fp, 0, SEEK_CUR);
    long nwritten = src ? (long)fwrite(src, 1, size, fp) : 0;
    if (ferror(fp)) { setD0(cpu_, errno2gemdos(errno, false)); clearerr(fp); }
    else { fflush(fp); setD0(cpu_, (int32_t)nwritten); }
    return true;
}

bool GemdosHd::gemFDelete(uint32_t p) {
    std::string name;
    if (!getString(readLong(p), name) || name.empty()) return false;
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;
    if (writeProtect_) { setD0(cpu_, GEMDOS_EWRPRO); return true; }
    std::string host; createHostFileName(drive, name, host);
    if (unlink(host.c_str()) == 0) setD0(cpu_, GEMDOS_EOK);
    else setD0(cpu_, errno2gemdos(errno, false));
    return true;
}

bool GemdosHd::gemLSeek(uint32_t p) {
    // ftello/fseeko (off_t 64 bits) comme gemRead : ftell/fseek plafonnent à 2 Go
    // sous Windows x64 (LLP64, long = 32 bits) — un fichier hôte ≥ 2 Go faussait
    // fileSize et la borne dest. L'interface GEMDOS reste 32 bits signés (D0).
    off_t offset = (int32_t)readLong(p);
    int handle = readWord(p + 4);
    int mode = readWord(p + 6);
    if ((handle = getValidFileHandle(handle)) < 0) return false;
    FILE* fp = fileHandles_[handle].fp;
    off_t oldPos = ftello(fp);
    if (fseeko(fp, 0, SEEK_END) != 0 || oldPos < 0) { setD0(cpu_, GEMDOS_E_SEEK); return true; }
    off_t fileSize = ftello(fp);
    off_t dest;
    switch (mode) {
    case 0: dest = offset; break;
    case 1: dest = oldPos + offset; break;
    case 2: dest = fileSize + offset; break;
    default: dest = -1;
    }
    if (dest < 0 || dest > fileSize) {
        fseeko(fp, oldPos, SEEK_SET);
        setD0(cpu_, GEMDOS_ERANGE);
        return true;
    }
    fseeko(fp, dest, SEEK_SET);
    setD0(cpu_, (int32_t)ftello(fp));
    return true;
}

bool GemdosHd::gemFattrib(uint32_t p) {
    std::string name;
    int rwflag = readWord(p + 4);
    int attrib = readWord(p + 6);
    if (!getString(readLong(p), name) || name.empty()) return false;
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;

    std::string host; createHostFileName(drive, name, host);
    if (isVolumeLabel(attrib)) { setD0(cpu_, GEMDOS_EFILNF); return true; }
    struct stat st;
    if (stat(host.c_str(), &st) != 0) { setD0(cpu_, GEMDOS_EFILNF); return true; }
    mode_t mode = st.st_mode;

    if (rwflag == 0) { setD0(cpu_, convertAttribute(mode, host.c_str())); return true; }
    if (writeProtect_) { setD0(cpu_, GEMDOS_EWRPRO); return true; }

    if (attrib & FA_DIR) {
        if (!S_ISDIR(mode)) { setD0(cpu_, GEMDOS_EPTHNF); return true; }
    } else {
        if (S_ISDIR(mode)) { setD0(cpu_, GEMDOS_EFILNF); return true; }
    }
    mode &= S_IRWXU | S_IRWXG | S_IRWXO;
    if (attrib & FA_READONLY) mode &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
    else mode |= S_IWUSR;
    if (chmod(host.c_str(), mode) == 0) { setD0(cpu_, attrib); return true; }
    setD0(cpu_, errno2gemdos(errno, (attrib & FA_DIR) != 0));
    return true;
}

bool GemdosHd::gemForce(uint32_t p) {
    int std_ = readWord(p);
    int own = readWord(p + 2);
    if (std_ > own) std::swap(std_, own);
    if ((own = getValidFileHandle(own)) < 0) return false;
    if (std_ < 0 || std_ >= FORCED_HANDLES) return false;
    forced_[std_].basepage = readLong(actPd_);
    forced_[std_].handle = own;
    setD0(cpu_, GEMDOS_EOK);
    return true;
}

bool GemdosHd::gemGetDir(uint32_t p) {
    uint32_t address = readLong(p);
    int drive = readWord(p + 4);
    if (drive == 0) drive = currentDrive_; else drive--;
    if (!isDriveEmulated(drive)) return false;

    EmuDrive& d = emudrives_[drive - 2];
    std::string path = d.fsCurrPath.substr(std::min(d.hdEmuDir.size(), d.fsCurrPath.size()));
    cleanFileName(path);
    if (path.size() == 1 && path[0] == PATHSEP) path.clear();   // racine = chaîne vide
    int len = (int)path.size() + 1;
    if (!checkArea(address, len, false)) { setD0(cpu_, GEMDOS_ERANGE); return true; }
    flushCache();
    for (int i = 0; i < len; i++) {
        char c = (i < (int)path.size()) ? path[i] : '\0';
        writeByte(address + i, (c == PATHSEP) ? '\\' : (uint8_t)c);
    }
    setD0(cpu_, GEMDOS_EOK);
    return true;
}

// -----------------------------------------------------------------------------
//  DTA : Fsfirst / Fsnext (port de PopulateDTA / GemDOS_SFirst / GemDOS_SNext)
// -----------------------------------------------------------------------------
bool GemdosHd::populateDTA(InternalDta& iDTA, const std::string& fname, uint32_t dtaGemdos) {
    std::string tempstr = iDTA.path + std::string(1, PATHSEP) + fname;
    struct stat st;
    if (stat(tempstr.c_str(), &st) != 0) return false;   // (DTA_SKIP/ERR géré par appelant)

    int nFileAttr = convertAttribute(st.st_mode, tempstr.c_str());
    int nAttrMask = iDTA.dta_attrib | IGNORED_FILE_ATTRIBS;
    if (nFileAttr != 0 && !(nAttrMask & nFileAttr)) return false;   // SKIP

    uint16_t tw, dw; dateTime2Tos(st.st_mtime, tw, dw);
    flushCache();
    std::string atari = host2atari(fname);
    writeLong(dtaGemdos + 26, (uint32_t)st.st_size);     // dta_size
    writeWord(dtaGemdos + 22, tw);                       // dta_time
    writeWord(dtaGemdos + 24, dw);                       // dta_date
    writeByte(dtaGemdos + 21, (uint8_t)nFileAttr);       // dta_attrib
    for (int i = 0; i < TOS_NAMELEN; i++)                // dta_name[14]
        writeByte(dtaGemdos + 30 + i, (uint8_t)(i < (int)atari.size() ? atari[i] : 0));
    return true;
}

bool GemdosHd::gemSNext(bool /*trace*/) {
    uint32_t dtaGemdos = readLong(readLong(actPd_) + 0x20);   // BASEPAGE_OFFSET_DTA
    if (!checkArea(dtaGemdos, 44, false)) { setD0(cpu_, GEMDOS_EINTRN); return true; }

    if (readLong(dtaGemdos + 2) != DTA_MAGIC_NUMBER) return false;   // DTA TOS
    uint16_t index = readWord(dtaGemdos + 0);
    if (index >= dtas_.size() || !dtas_[index].used) {
        setD0(cpu_, GEMDOS_ENMFIL); return true;
    }
    if (isVolumeLabel(dtas_[index].dta_attrib)) { setD0(cpu_, GEMDOS_ENMFIL); return true; }

    InternalDta& dta = dtas_[index];
    bool ok;
    do {
        if (dta.centry >= (int)dta.found.size()) {
            if (bus_.tosVersion < 0x0400) { flushCache(); writeByte(dtaGemdos + 30, 0); }
            setD0(cpu_, GEMDOS_ENMFIL);
            return true;
        }
        ok = populateDTA(dta, dta.found[dta.centry++], dtaGemdos);
    } while (!ok);

    setD0(cpu_, GEMDOS_EOK);
    return true;
}

bool GemdosHd::gemSFirst(uint32_t p) {
    std::string name;
    uint16_t attrib = readWord(p + 4);
    if (!getString(readLong(p), name)) return false;
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;

    std::string host; createHostFileName(drive, name, host);
    uint32_t dtaGemdos = readLong(readLong(actPd_) + 0x20);
    if (!checkArea(dtaGemdos, 44, false)) { setD0(cpu_, GEMDOS_EINTRN); return true; }
    flushCache();

    uint16_t useidx;
    if (readLong(dtaGemdos + 2) == DTA_MAGIC_NUMBER) {
        useidx = readWord(dtaGemdos + 0);
        if (useidx >= dtas_.size() || dtas_[useidx].addr != dtaGemdos) useidx = dtaIndex_;
    } else {
        writeLong(dtaGemdos + 2, DTA_MAGIC_NUMBER);
        useidx = dtaIndex_;
    }
    writeWord(dtaGemdos + 0, useidx);

    if (dtas_[useidx].used) clearInternalDTA(useidx);
    dtas_[useidx].used = true;
    dtas_[useidx].addr = dtaGemdos;
    dtas_[useidx].dta_attrib = (char)attrib;

    if (isVolumeLabel(attrib)) {
        std::string vol = "EMULATED.001"; vol[11] = (char)('0' + drive);
        for (int i = 0; i < TOS_NAMELEN; i++)
            writeByte(dtaGemdos + 30 + i, (uint8_t)(i < (int)vol.size() ? vol[i] : 0));
        writeByte(dtaGemdos + 21, FA_VOLUME);
        setD0(cpu_, GEMDOS_EOK);
        return true;
    }

    // Dossier + masque : sépare le dossier hôte du masque de fichiers — SANS jamais
    // remonter au-dessus de la racine du lecteur (port de fsfirst_dirname,
    // gemdos.c:493-524). Fsfirst("C:\") donne host == hdEmuDir sans composant final :
    // l'ancien rfind() scannait le dossier hôte PARENT du montage (sortie du bac à
    // sable + entrée DTA fantôme au nom du dossier partagé) au lieu de scanner la
    // racine avec un masque immatchable → EFILNF, comme Hatari/TOS.
    std::string dirPath = host;
    {
        for (char& c : dirPath) if (c == '\\') c = PATHSEP;
        const size_t rootLen = emudrives_[drive - 2].hdEmuDir.size();
        if (dirPath.size() > rootLen) {
            size_t sep = dirPath.rfind(PATHSEP);
            if (sep == std::string::npos) dirPath.clear();
            else dirPath.resize(sep > rootLen ? sep : rootLen);
        } // sinon : racine nue — on la scanne elle-même (masque = son nom → EFILNF)
    }
    dtas_[useidx].path = dirPath;

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) { setD0(cpu_, GEMDOS_EPTHNF); return true; }

    std::string mask = baseName(host.c_str());
    std::vector<std::string> all;
    struct dirent* e;
    // Recomposition NFD → NFC comme ci-dessus (≙ gemdos.c:3182) : le listing Fsfirst
    // doit rendre le même nom que celui que matchHostDirEntry saura retrouver.
    while ((e = readdir(dir))) all.push_back(neost::hostpath::precomposeUtf8(e->d_name));
    closedir(dir);
    std::sort(all.begin(), all.end());

    // Divergence NeoST (vs Hatari) : on N'énumère JAMAIS « . » / « .. », ni à la
    // racine ni en sous-répertoire. Hatari (comme un vrai FAT) expose « . » et
    // « .. » dans les sous-dossiers, mais côté HÔTE ce sont des entrées Unix qui
    // polluent le bureau TOS (l'utilisateur ne veut pas voir les « points d'Unix »).
    // subdir=false → fsfirst_match rejette tout nom commençant par « . ». La
    // navigation « parent » d'un chemin (« ..\FOO ») reste gérée par
    // createHostFileName, indépendante de ce listing.
    const bool subdir = false;

    dtas_[useidx].centry = 0;
    dtas_[useidx].found.clear();
    for (auto& dn : all)
        if (fsfirst_match(mask.c_str(), dn.c_str(), subdir)) dtas_[useidx].found.push_back(dn);

    if (dtas_[useidx].found.empty()) { setD0(cpu_, GEMDOS_EFILNF); return true; }

    gemSNext(false);

    if (useidx != dtaIndex_) return true;
    if (++dtaIndex_ >= dtas_.size()) {
        if ((int)dtas_.size() < DTA_CACHE_MAX) dtas_.resize(dtas_.size() + DTA_CACHE_INC);
        else dtaIndex_ = 0;
    }
    return true;
}

bool GemdosHd::gemRename(uint32_t p) {
    std::string oldName, newName;
    if (!getString(readLong(p + 2), oldName) || oldName.empty() ||
        !getString(readLong(p + 6), newName) || newName.empty()) return false;
    int newDrive = fileName2HardDriveID(newName);
    int oldDrive = fileName2HardDriveID(oldName);
    if (newDrive == -1 || oldDrive == -1) return false;
    if (writeProtect_) { setD0(cpu_, GEMDOS_EWRPRO); return true; }
    std::string newHost, oldHost;
    createHostFileName(newDrive, newName, newHost);
    createHostFileName(oldDrive, oldName, oldHost);
    if (access(oldHost.c_str(), F_OK) == 0 && access(newHost.c_str(), F_OK) == 0)
        setD0(cpu_, GEMDOS_EACCDN);
    else if (rename(oldHost.c_str(), newHost.c_str()) == 0)
        setD0(cpu_, GEMDOS_EOK);
    else
        setD0(cpu_, errno2gemdos(errno, false));
    return true;
}

bool GemdosHd::gemGSDToF(uint32_t p) {
    uint32_t buffer = readLong(p);
    int handle = readWord(p + 4);
    int flag = readWord(p + 6);
    if ((handle = getValidFileHandle(handle)) < 0) return false;

    if (flag == 1) {
        if (writeProtect_) { setD0(cpu_, GEMDOS_EWRPRO); return true; }
        uint16_t tw = readWord(buffer), dw = readWord(buffer + 2);
        if (hostTime_) { setD0(cpu_, GEMDOS_EOK); return true; }
        struct stat fs; struct utimbuf tb; struct tm ts {};
        ts.tm_sec  = (tw & 0x1F) << 1;
        ts.tm_min  = (tw & 0x7E0) >> 5;
        ts.tm_hour = (tw & 0xF800) >> 11;
        ts.tm_mday = (dw & 0x1F);
        ts.tm_mon  = ((dw & 0x1E0) >> 5) - 1;
        ts.tm_year = ((dw & 0xFE00) >> 9) + 80;
        ts.tm_isdst = -1;
        fflush(fileHandles_[handle].fp);
        tb.modtime = mktime(&ts);
        if (stat(fileHandles_[handle].actualName.c_str(), &fs) != 0) { setD0(cpu_, GEMDOS_EACCDN); return true; }
        tb.actime = fs.st_atime;
        if (utime(fileHandles_[handle].actualName.c_str(), &tb) == 0) setD0(cpu_, GEMDOS_EOK);
        else setD0(cpu_, GEMDOS_EACCDN);
        return true;
    }

    struct stat fs;
    if (stat(fileHandles_[handle].actualName.c_str(), &fs) == 0) {
        uint16_t tw, dw; dateTime2Tos(fs.st_mtime, tw, dw);
        if (checkArea(buffer, 4, false)) {
            flushCache();
            writeWord(buffer, tw);
            writeWord(buffer + 2, dw);
            setD0(cpu_, GEMDOS_EOK);
        } else {
            setD0(cpu_, GEMDOS_ERANGE);
        }
    } else {
        setD0(cpu_, GEMDOS_ERROR);
    }
    return true;
}

// -----------------------------------------------------------------------------
//  Terminaison de programme : ferme les handles restants (GemDOS_TerminateClose)
// -----------------------------------------------------------------------------
void GemdosHd::terminateClose() {
    uint32_t current = readLong(actPd_);
    for (int i = 0; i < MAX_FILE_HANDLES; i++)
        if (fileHandles_[i].basepage == current && fileHandles_[i].used) closeFileHandle(i);
    for (int i = 0; i < FORCED_HANDLES; i++)
        if (forced_[i].basepage == current) unforceFileHandle(i);
}
bool GemdosHd::gemPterm0(uint32_t)   { terminateClose(); return false; }
bool GemdosHd::gemPterm(uint32_t)    { terminateClose(); return false; }
bool GemdosHd::gemPtermres(uint32_t) { terminateClose(); return false; }

// -----------------------------------------------------------------------------
//  Pexec (GemDOS_Pexec + GemDOS_PexecBpCreated + GemDOS_LoadAndReloc)
// -----------------------------------------------------------------------------
int GemdosHd::gemPexec(uint32_t p) {
    uint16_t mode = readWord(p);
    uint32_t prgname = readLong(p + 2);
    uint32_t cmdline = readLong(p + 6);
    uint32_t env = readLong(p + 10);

    if (mode != 0 && mode != 3) return false;   // on n'intercepte que les modes « load »

    std::string name;
    if (!getString(prgname, name)) return false;
    int drive = fileName2HardDriveID(name);
    if (drive == -1) return false;

    std::string host; createHostFileName(drive, name, host);
    FILE* fh = fopen(host.c_str(), "rb");
    if (!fh) { setD0(cpu_, GEMDOS_EFILNF); return true; }
    uint8_t prgh[28];
    size_t len = fread(prgh, 1, sizeof(prgh), fh);
    fclose(fh);
    if (len != sizeof(prgh) || prgh[0] != 0x60 || prgh[1] != 0x1a ||
        (prgh[2] & 0x80) || (prgh[6] & 0x80) || (prgh[10] & 0x80)) {
        setD0(cpu_, GEMDOS_EPLFMT); return true;
    }

    // Prépare la pile pour appeler « create basepage » (Pexec 5/7).
    uint32_t a7 = reg(15) - 16;
    setAreg(7, a7);
    flushCache();
    writeWord(a7, 0x4b);
    writeWord(a7 + 2, bus_.tosVersion >= 0x200 ? 7 : 5);
    writeLong(a7 + 4, (uint32_t)(prgh[22] << 24 | prgh[23] << 16 | prgh[24] << 8 | prgh[25]));
    writeLong(a7 + 8, cmdline);
    writeLong(a7 + 12, env);
    savedPexecParams_ = p;
    return -1;
}

int GemdosHd::loadAndReloc(const std::string& prgName, uint32_t baseaddr, bool fullBpSetup) {
    FILE* fp = fopen(prgName.c_str(), "rb");
    if (!fp) return GEMDOS_EFILNF;
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fileSize < 30) { fclose(fp); return GEMDOS_EPLFMT; }
    std::vector<uint8_t> prg(fileSize);
    if ((long)fread(prg.data(), 1, fileSize, fp) != fileSize) { fclose(fp); return GEMDOS_EPLFMT; }
    fclose(fp);
    if (prg[0] != 0x60 || prg[1] != 0x1a) return GEMDOS_EPLFMT;

    auto be32 = [&](int o) {
        return (uint32_t)((prg[o] << 24) | (prg[o + 1] << 16) | (prg[o + 2] << 8) | prg[o + 3]);
    };
    uint32_t nText = be32(2), nData = be32(6), nBss = be32(10), nSym = be32(14);

    // En-tête PRG mensonger : la copie texte+données lit prg[28 .. 28+nText+nData[,
    // il faut donc que tout tienne dans le fichier. Calcul en 64 bits : nText+nData
    // peuvent approcher 2^32 et la somme déborderait en 32 bits.
    if ((uint64_t)28 + nText + nData > prg.size()) return GEMDOS_EPLFMT;

    uint32_t memtop = (baseaddr < 0x1000000) ? readLong(0x436) : readLong(0x5a4);
    // Même précaution : la somme peut déborder en uint32_t et passer le test à tort.
    if ((uint64_t)baseaddr + 0x100 + nText + nData + nBss > memtop) return GEMDOS_ENSMEM;

    flushCache();
    // Texte + données → baseaddr+0x100
    for (uint32_t i = 0; i < nText + nData; i++) writeByte(baseaddr + 0x100 + i, prg[28 + i]);
    // BSS effacé
    for (uint32_t i = 0; i < nBss; i++) writeByte(baseaddr + 0x100 + nText + nData + i, 0);

    // Basepage
    writeLong(baseaddr + 8,  baseaddr + 0x100);                      // p_tbase
    writeLong(baseaddr + 12, nText);                                 // p_tlen
    writeLong(baseaddr + 16, baseaddr + 0x100 + nText);             // p_dbase
    writeLong(baseaddr + 20, nData);                                 // p_dlen
    writeLong(baseaddr + 24, baseaddr + 0x100 + nText + nData);     // p_bbase
    writeLong(baseaddr + 28, nBss);                                  // p_blen
    if (fullBpSetup) {
        writeLong(baseaddr, baseaddr);
        writeLong(baseaddr + 4, memtop);
        writeLong(baseaddr + 32, baseaddr + 0x80);
        writeLong(baseaddr + 36, baseaddr);
        writeLong(baseaddr + 40, 0);
        writeLong(baseaddr + 44, baseaddr + 40);
    }

    // Si FASTLOAD non posé, efface le tas — version bornée (esprit de
    // STMemory_SafeClear) : si p_hitpa < cur, la soustraction non signée donnait
    // ~4 Go d'écritures (gel). On n'efface que si p_hitpa > cur, sans jamais
    // dépasser la RAM réelle.
    if (!(prg[25] & 1)) {
        uint32_t cur = baseaddr + 0x100 + nText + nData + nBss;   // ≤ memtop (vérifié plus haut)
        uint32_t hitpa = readLong(baseaddr + 4);                  // p_hitpa (fourni par l'invité)
        uint32_t limit = std::min<uint32_t>(hitpa, (uint32_t)bus_.ram.size());
        for (uint32_t i = cur; i < limit; i++) writeByte(i, 0);
    }

    if (prg[26] != 0 || prg[27] != 0) return 0;   // pas d'info de relocation

    // Lecture de 4 octets (premier offset de relocation) à relIdx : elle doit
    // tenir dans le fichier (Hatari borne à fileSize-3, off-by-one d'un octet ;
    // ici prg fait exactement fileSize octets → borne stricte).
    // ⚠ Arithmétique en 64 BITS SIGNÉS, et pas en `long`. `nSym` (champ `slen` de
    // l'en-tête, octets 14-17) vient du FICHIER et n'est validé NULLE PART : ni
    // `gemPexec`, qui ne teste que tlen/dlen/blen, ni la borne ci-dessus, qui ne
    // couvre que texte+données. Avec `long` sur 32 bits — MinGW-w64, donc le paquet
    // Windows x64 livré — un `slen ≥ 0x80000000` rendait `(long)nSym` NÉGATIF : le
    // test passait (débordement signé, déjà de l'UB), puis `relIdx += nSym` repassait
    // par `unsigned int` et rendait `relIdx` négatif. `prg[relIdx]` lisait alors
    // ~2 Gio SOUS le tampon, et la table de relocation ainsi « lue » était appliquée
    // à la RAM invitée. Sur LP64 la branche n'était simplement jamais prise : le
    // défaut ne se voyait que sur la plateforme qu'on ne lance jamais à la main.
    // En int64_t, `(int64_t)nSym` est positif pour tout uint32_t et la somme ne peut
    // pas déborder — le test redevient celui qu'il croyait être.
    const int64_t fsz = (int64_t)fileSize;
    int64_t relIdx = (int64_t)(0x1cu + nText + nData);   // ≤ fsz (vérifié plus haut)
    if (relIdx + 4 > fsz) return GEMDOS_EPLFMT;
    if (relIdx + (int64_t)nSym + 4 <= fsz) relIdx += (int64_t)nSym;

    uint32_t relOff = (uint32_t)((prg[relIdx] << 24) | (prg[relIdx + 1] << 16)
                               | (prg[relIdx + 2] << 8) | prg[relIdx + 3]);
    if (relOff == 0) return 0;

    uint32_t cur = baseaddr + 0x100 + relOff;
    writeLong(cur, readLong(cur) + baseaddr + 0x100);
    relIdx += 4;
    while (relIdx < fsz && prg[relIdx]) {
        if (prg[relIdx] == 1) { relOff += 254; relIdx += 1; continue; }
        relOff += prg[relIdx];
        cur = baseaddr + 0x100 + relOff;
        writeLong(cur, readLong(cur) + baseaddr + 0x100);
        relIdx += 1;
    }
    return 0;
}

void GemdosHd::pexecBpCreated() {
    uint16_t sr = getSR();
    sr &= ~SR_OVERFLOW;

    uint16_t mode = readWord(savedPexecParams_);
    uint32_t prgname = readLong(savedPexecParams_ + 2);

    std::string name; getString(prgname, name);
    int drive = fileName2HardDriveID(name);
    uint32_t errcode;
    if (drive >= 2) {
        std::string host; createHostFileName(drive, name, host);
        errcode = loadAndReloc(host, reg(0), false);
    } else {
        errcode = GEMDOS_EDRIVE;
    }

    if (errcode) {
        setAreg(0, reg(0));            // A0 = basepage (à libérer par la cartouche)
        setD0(cpu_, (int32_t)errcode);
        sr &= ~SR_ZERO;
    } else if (mode == 0) {
        flushCache();
        // Relance un Pexec « just-go » pour démarrer le programme.
        writeWord(savedPexecParams_, bus_.tosVersion >= 0x104 ? 6 : 4);
        writeLong(savedPexecParams_ + 6, reg(0));
        sr |= SR_OVERFLOW;
    } else {
        sr |= SR_ZERO;
    }
    setSR(sr);
}

// -----------------------------------------------------------------------------
//  Dispatch GEMDOS (GemDOS_Trap) + interception d'opcode (OpCode_*)
// -----------------------------------------------------------------------------
int GemdosHd::trap() {
    uint16_t sr = getSR();
    uint16_t callingSReg = readWord(reg(15));            // SR empilé par le trap
    callingPC_ = readLong(reg(15) + 2);
    uint32_t params;
    if (!(callingSReg & SR_SUPERMODE)) params = getUSP();          // appelant en mode user
    else params = reg(15) + 2 + 4;                                 // sur la pile super (68000)

    uint16_t call = readWord(params);
    params += 2;

    // Trace (NEOST_GEMDOS_TRACE) des appels « fichier » (≥ Dfree) : nom de l'appel, handle
    // quand l'appel en prend un, puis QUI l'a traité et D0 — c'est ce qui manquait à un
    // client (TOS File Cmd, 2026-09-23) pour comprendre un Fclose répondant EIHNDL :
    // lecteur hôte, ou TOS parce que getValidFileHandle a refusé le handle ?
    const bool traced = trace_ && call >= 0x36;
    if (traced) {
        int handle = -1;
        switch (call) {
        case 0x3e: case 0x3f: case 0x40: handle = readWord(params);     break;  // Fclose/Fread/Fwrite
        case 0x42: case 0x57:            handle = readWord(params + 4); break;  // Fseek/Fdatime
        case 0x46:                       handle = readWord(params + 2); break;  // Fforce (nh)
        default: break;
        }
        if (handle >= 0)
            std::fprintf(stderr, "[gemdos] call 0x%02X %s handle=%d at PC $%06X\n",
                         call, gemdosCallName(call), handle, callingPC_);
        else
            std::fprintf(stderr, "[gemdos] call 0x%02X %s at PC $%06X\n",
                         call, gemdosCallName(call), callingPC_);
    }

    sr &= ~SR_OVERFLOW;

    int finished = false;
    switch (call) {
    case 0x00: finished = gemPterm0(params); break;
    case 0x0e: finished = gemSetDrv(params); break;
    case 0x31: finished = gemPtermres(params); break;
    case 0x36: finished = gemDFree(params); break;
    case 0x39: finished = gemMkDir(params); break;
    case 0x3a: finished = gemRmDir(params); break;
    case 0x3b: finished = gemChDir(params); break;
    case 0x3c: finished = gemCreate(params); break;
    case 0x3d: finished = gemOpen(params); break;
    case 0x3e: finished = gemClose(params); break;
    case 0x3f: finished = gemRead(params); break;
    case 0x40: finished = gemWrite(params); break;
    case 0x41: finished = gemFDelete(params); break;
    case 0x42: finished = gemLSeek(params); break;
    case 0x43: finished = gemFattrib(params); break;
    case 0x46: finished = gemForce(params); break;
    case 0x47: finished = gemGetDir(params); break;
    case 0x4b:
        finished = gemPexec(params);
        if (finished == -1) { sr |= SR_OVERFLOW; finished = true; }
        break;
    case 0x4c: finished = gemPterm(params); break;
    case 0x4e: finished = gemSFirst(params); break;
    case 0x4f: finished = gemSNext(true); break;
    case 0x56: finished = gemRename(params); break;
    case 0x57: finished = gemGSDToF(params); break;
    default: break;   // tout le reste → TOS
    }

    if (traced) {
        if (finished) {
            const int32_t d0 = (int32_t)reg(0);
            std::fprintf(stderr, "[gemdos]   -> host d0=%d%s\n", d0, gemdosErrorName(d0));
        } else {
            std::fprintf(stderr, "[gemdos]   -> TOS\n");   // D0 n'est connu qu'au retour de TOS
        }
    }

    if (finished) sr |= SR_ZERO;
    else sr &= ~SR_ZERO;
    setSR(sr);
    return finished;
}

void GemdosHd::boot() {
    if (initGemdos_) reset();
    initGemdos_ = true;

    if (!active_) return;

    // Lecteur de boot réel (variable système _bootdev à $446).
    int bd = (int)readWord(0x446);
    if (bd >= 0 && bd < 26) { bootDrive_ = bd; currentDrive_ = (uint16_t)bd; }

    // act_pd = pointeur vers le pointeur de basepage courant (osheader + 0x28).
    if (bus_.tosVersion == 0x100) {
        actPd_ = ((readWord(/*TosAddress*/ bus_.romBase + 28) >> 1) == 4) ? 0x873c : 0x602c;
    } else {
        uint32_t osAddress = readLong(0x4f2);
        actPd_ = readLong(osAddress + 0x28);
    }

    // Sauve l'ancien vecteur GEMDOS DANS la cartouche (old_gemdos), puis installe
    // le nouveau ($84 → CART_GEMDOS). old_gemdos vit en ROM cartouche → on écrit
    // directement le tampon cart[] (l'écriture bus y serait perdue/fautive).
    uint32_t oldVec = readLong(0x0084);
    size_t off = CART_OLDGEMDOS - 0xFA0000u;
    if (off + 4 <= bus_.cart.size()) {
        bus_.cart[off + 0] = (uint8_t)(oldVec >> 24);
        bus_.cart[off + 1] = (uint8_t)(oldVec >> 16);
        bus_.cart[off + 2] = (uint8_t)(oldVec >> 8);
        bus_.cart[off + 3] = (uint8_t)(oldVec);
    }
    writeLong(0x0084, CART_GEMDOS);
    if (trace_) std::fprintf(stderr, "[gemdos] hook installed: (0x84)=$%06X, previous=$%06X, act_pd=$%06X\n",
                             CART_GEMDOS, oldVec, actPd_);
}

void GemdosHd::sysInit() {
    // Ajoute nos lecteurs au masque _drvbits ($4c2), après ce que TOS a posé.
    connectedDriveMask_ |= readLong(0x4c2);
    writeLong(0x4c2, connectedDriveMask_);
    boot();
    if (trace_) std::fprintf(stderr, "[gemdos] sysInit : _drvbits=$%08X\n", connectedDriveMask_);
}

bool GemdosHd::handleOpcode(uint16_t opcode) {
    switch (opcode) {
    case 0x0008: trap(); break;           // GEMDOS  → dispatch (pose Z/V dans SR)
    case 0x0009: pexecBpCreated(); break; // PEXEC   → charge+relocalise
    case 0x000a: sysInit(); break;        // SYSINIT → hook + masque lecteurs
    default: return false;
    }
    flushCache();
    return true;
}

// =============================================================================
//  Auto-test du BAC À SABLE (A39, 2026-08-28) — le premier test de ce module.
//
//  POURQUOI ICI, ET POURQUOI MAINTENANT. `GemdosHd` fait 1 700 lignes et n'avait
//  AUCUN test : l'évaluation d'architecture du jour l'a nommé « la surface la plus
//  exposée aux fichiers de l'utilisateur ». Un programme invité qui sort du dossier
//  monté lit et ÉCRIT sur l'hôte avec les droits de l'utilisateur — c'est la seule
//  faille de ce projet qui puisse abîmer autre chose que l'émulation.
//
//  Le code, lui, a déjà été durci plusieurs fois (la normalisation « / » → « \ »,
//  les deux étapes makeAbsoluteName + physicalCanon de clampToSandbox) — chaque
//  durcissement porte le récit de l'évasion qu'il ferme. Ce qui manquait n'était
//  pas la robustesse : c'était la GARDE. Rien ne rejouait ces cas.
//
//  Déterministe, sans machine ni ROM : un dossier temporaire, un montage, une table
//  de noms hostiles. Chaque résultat doit rester SOUS la racine du montage.
//  Devenu possible par A33 (deux Cpu68k dans un processus) : ce test construit sa
//  propre machine minimale, à côté de celle du binaire hôte.
// =============================================================================
bool GemdosHd::sandboxSelfTest() {
    namespace stdfs = std::filesystem;
    std::error_code ec;
    const stdfs::path root = stdfs::temp_directory_path(ec) / "neost-gemdos-selftest";
    stdfs::remove_all(root, ec);
    stdfs::create_directories(root / "SUB" / "DEEP", ec);
    { std::ofstream(root / "SUB" / "FILE.TXT") << "x"; }
    if (ec || !stdfs::is_directory(root, ec)) {
        std::fprintf(stderr, "[gemdos-selftest] FAIL: dossier temporaire indisponible\n");
        return false;
    }
    if (!setDirectory(root.string())) {
        std::fprintf(stderr, "[gemdos-selftest] FAIL: montage refusé (%s)\n", root.c_str());
        return false;
    }
    // Racine CANONIQUE avec séparateur final : c'est le préfixe que tout chemin hôte
    // produit doit porter. On la recalcule comme clampToSandbox, pas comme l'entrée
    // (un /tmp qui est un lien symbolique ferait échouer une comparaison naïve).
    std::string rootCanon = physicalCanon(emudrives_[0].hdEmuDir);
    addSlash(rootCanon);

    int pass = 0, fail = 0;
    auto inside = [&](const char* what, const char* gemName) {
        std::string host;
        createHostFileName(2, gemName, host);
        std::string canon = physicalCanon(host);
        const bool ok = canon.compare(0, rootCanon.size(), rootCanon) == 0
                     || canon + PATHSEP == rootCanon;
        if (ok) ++pass;
        else {
            ++fail;
            std::fprintf(stderr, "  FAIL %-34s '%s' -> %s (HORS du bac à sable %s)\n",
                         what, gemName, canon.c_str(), rootCanon.c_str());
        }
    };

    // --- Les évasions, dont deux ont RÉELLEMENT existé dans ce fichier -----------
    inside("remontee simple",            "..\\X");
    inside("remontee profonde",          "..\\..\\..\\..\\etc\\passwd");
    // Séparateurs UNIX : « / » est un caractère INVALIDE côté GEMDOS mais LE
    // séparateur côté hôte. Sans la normalisation d'entrée, ce nom traversait le
    // parseur intact et le « .. » était résolu pour de vrai par l'OS.
    inside("separateurs UNIX",           "../../etc/passwd");
    inside("lettre de lecteur + UNIX",   "C:/../../etc/passwd");
    inside("melange des deux",           "..\\../..\\etc/passwd");
    // Préfixe EXISTANT suivi d'un suffixe inexistant : realpath seul ne peut pas
    // résoudre le suffixe et rendait les « .. » intacts — d'où les DEUX étapes.
    inside("prefixe existant + inexistant", "SUB\\NOPE\\..\\..\\..\\X");
    inside("chemin absolu + remontee",   "\\..\\..\\X");
    inside("racine puis remontee",       "C:\\..\\X");
    inside("point-point sans separateur", "..");
    inside("joker apres remontee",       "..\\*.*");
    inside("nom vide de composant",      "SUB\\\\..\\..\\X");

    // --- Les cas LÉGITIMES : le bac à sable ne doit pas les mutiler --------------
    std::string host;
    createHostFileName(2, "SUB\\FILE.TXT", host);
    const std::string want = physicalCanon((root / "SUB" / "FILE.TXT").string());
    if (physicalCanon(host) == want) ++pass;
    else { ++fail; std::fprintf(stderr, "  FAIL %-34s -> %s (attendu %s)\n",
                                "fichier legitime", host.c_str(), want.c_str()); }
    createHostFileName(2, "C:\\", host);
    if (physicalCanon(host) + PATHSEP == rootCanon || physicalCanon(host) == rootCanon) ++pass;
    else { ++fail; std::fprintf(stderr, "  FAIL %-34s -> %s (attendu la racine %s)\n",
                                "racine du lecteur", host.c_str(), rootCanon.c_str()); }

    // --- Nom ACCENTUÉ écrit en forme DÉCOMPOSÉE (le cas macOS), de bout en bout ----
    // On écrit le fichier avec les octets NFD (« cafe » + U+0301) — ce que macOS rend
    // à readdir — puis on le cherche sous sa forme PRÉCOMPOSÉE, celle que le reste du
    // monde (et Linux) emploie. Sans la recomposition de matchHostDirEntry, la
    // comparaison échoue et le fichier est INTROUVABLE depuis le TOS.
    // ⚠ Le test tient sur les DEUX familles de systèmes de fichiers : si le volume
    // normalise de lui-même (et rend donc déjà du NFC), la recomposition est un no-op
    // et la recherche réussit pareil. Il ne peut donc pas être vert par accident.
    {
        const std::string nfd = "cafe\xCC\x81.txt";       // e + U+0301
        const std::string nfc = "caf\xC3\xA9.txt";        // é
        { std::ofstream(root / nfd) << "x"; }
        const std::string got = matchHostDirEntry(emudrives_[0].hdEmuDir, nfc,
                                                  /*pattern=*/false, /*onlyInvalid=*/false);
        if (got == nfc) ++pass;
        else { ++fail; std::fprintf(stderr,
               "  FAIL %-34s '%s' introuvable (rendu '%s')\n",
               "nom accentue NFD -> NFC", nfc.c_str(), got.c_str()); }
    }

    unmount();
    stdfs::remove_all(root, ec);
    std::fprintf(stderr, "[gemdos-selftest] %d OK, %d FAIL\n", pass, fail);
    return fail == 0;
}
