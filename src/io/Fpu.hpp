// =============================================================================
//  Fpu.hpp — Coprocesseur MC68881 du Mega STE (OPTIONNEL), mode périphérique.
//
//  Sur Mega STE, le 68000 ne possède pas le protocole coprocesseur des 68020+ :
//  le 68881 (socket interne, ou carte SFP004 sur Mega ST) est câblé en
//  PÉRIPHÉRIQUE, ses registres d'interface coprocesseur (CIR) étant mappés en
//  $FFFA40-$FFFA5F. Le logiciel dialogue « à la main » : écrire le mot de
//  commande F-line dans le Command CIR ($FFFA4A), scruter le Response CIR
//  ($FFFA40) tant qu'il vaut $8900 (« null, come again » = occupé), puis
//  transférer les opérandes via l'Operand CIR ($FFFA50). Réf. : MC68881/MC68882
//  User's Manual §7, note d'application Motorola AN-947, et la glue SFP004 de
//  Michael Ritzert (MiNTLib) qui est la spec de facto côté logiciel.
//
//  Hatari N'ÉMULE PAS ce socket ($FFFA40 reste une bus error → « FPU not
//  found ») : il n'y a donc RIEN à porter depuis extern/hatari/src ; les
//  références comportementales sont le manuel Motorola et MAME (m68kfpu).
//
//  Niveau d'émulation NeoST = COMPLET : dialogue CIR complet (Command/Response/
//  Operand/Condition/Save/Restore), registres FP0-FP7 en étendu 80 bits,
//  FPCR/FPSR/FPIAR, formats B/W/L/S/D/X/P, constantes ROM FMOVECR bit-exactes.
//  • Arithmétique ALGÉBRIQUE (FADD/FSUB/FMUL/FDIV/FSQRT/FCMP/FINT/FREM/FMOD/
//    FSCALE/FGETEXP/FGETMAN/FSGLxxx) en SOFTFLOAT 80 bits → MANTISSE 64 BITS
//    RÉELLE (et non 53), arrondis FPCR + précision étendu/double/simple +
//    drapeaux d'exception IEEE exacts (cf. SoftFloatX80.hpp).
//  • Transcendantes (sin/cos/exp/log…) : via le FPU hôte (double) — le 68881 les
//    approxime lui-même, la bit-exactitude est hors de portée (comme MAME/Previous).
//  • Exceptions FP : positionnent le FPSR ET, si ACTIVÉES dans le FPCR, sont
//    LIVRÉES via le Response CIR (primitive « Take Pre-Instruction Exception »,
//    vecteur en octet bas) — seule voie en mode périphérique (pas de fil d'IRQ).
//  Par défaut : ABSENT (fidèle Hatari) — activer via --fpu / option GUI.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include "core/StateArchive.hpp"
#include "io/SoftFloatX80.hpp"   // arithmétique étendue 80 bits (mantisse 64 bits réelle)

class Fpu {
public:
    // Les booléens de cette classe sont relus EN BLOC (`ar(fpu)` dans Bus) : la
    // normalisation de StateArchive::operator() ne les voit pas — elle ne s'applique
    // qu'aux `bool` passés seuls (cf. sa note de portée). Un octet valant autre chose
    // que 0 ou 1 dans un bool est un COMPORTEMENT INDÉFINI, pas une valeur vraie.
    void fixSerializedBools(StateArchive& ar) { ar.fixBools(present, bufIn_); }
    // Présence du coprocesseur (socket peuplé). false = fidèle Hatari : la zone
    // $FFFA40-$FFFA5F déclenche une bus error et la sonde conclut « not found ».
    bool present = false;
    // Bourrage EXPLICITE et initialisé. Fpu est sérialisée d'un bloc (Bus::serialize,
    // ar(fpu)) : chaque octet de l'objet part dans le save-state et entre dans son
    // CRC. Le bourrage implicite du compilateur n'est jamais initialisé — valgrind :
    // « Syscall param writev points to uninitialised byte(s) », 61 octets par état,
    // CRC dépendant de mémoire indéfinie, et des octets de mémoire du processus dans
    // tout .state partagé. Les pads reproduisent EXACTEMENT les trous mesurés (sonde à
    // 0xAA sur un objet construit en place), donc les offsets et sizeof ne bougent
    // pas : format inchangé, états existants toujours lisibles. Verrou : les
    // static_assert de taille ci-dessous.
    uint8_t pad0_[7] = {};                  // 1..7 : bool → Ext aligné sur 8

    static constexpr uint32_t BASE = 0xFFFA40;   // premier CIR (Response)
    static constexpr uint32_t END  = 0xFFFA60;   // exclu : $FFFA40-$FFFA5F

    // Registres CIR (offsets pairs, accès octet/mot big-endian) :
    //   $00 Response (R)  $02 Control (W)   $04 Save (R)      $06 Restore (R/W)
    //   $08 Operation (W) $0A Command (W)   $0E Condition (W)
    //   $10-$13 Operand   $14 Register Select (R)  $18 Instr Addr  $1C Operand Addr
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);
    void    reset();

private:
    // ---- Valeur au format étendu 80 bits du 68881 (mot signe/exposant biais
    //      $3FFF + mantisse 64 bits à bit entier EXPLICITE). C'est le format de
    //      stockage des registres : un FMOVE.X aller-retour est bit-exact.
    struct Ext {
        uint16_t se  = 0x7FFF;             // défaut au reset : NaN (comme le 68881)
        uint8_t  pad_[6] = {};             // 2..7 : uint16 → uint64 aligné sur 8 (cf. pad0_)
        uint64_t man = 0xFFFFFFFFFFFFFFFFull;
        // Constructeurs EXPLICITES : avec pad_ au milieu, une initialisation agrégée
        // « Ext{se, man} » (une dizaine dans Fpu.cpp) rangeait la mantisse dans pad_[0]
        // — narrowing, et man laissé à sa valeur par défaut. Un constructeur à deux
        // arguments retire à Ext son statut d'agrégat : chaque Ext{a, b} l'appelle,
        // aucun site d'appel ne change, et la classe reste trivialement copiable
        // (exigence de StateArchive).
        Ext() = default;
        Ext(uint16_t s, uint64_t m) : se(s), man(m) {}
    };
    static_assert(sizeof(Ext) == 16, "Fpu::Ext : format de save-state (16 octets par registre)");

    // ---- État programmeur ----
    Ext      fp_[8];
    uint32_t fpcr_ = 0, fpsr_ = 0, fpiar_ = 0;

    // ---- Interface CIR ----
    uint16_t response_ = 0x0802;           // null : PF=1 (idle), TF=0
    uint8_t  latch_[0x20] = {};            // derniers octets écrits (relisibles)
    uint8_t  excVector_ = 0;               // dernier vecteur d'exception FP livré (0 = aucun)

    // Tampon de transfert de l'Operand CIR ($10-$13) : les transferts > 4
    // octets bouclent sur la même fenêtre, octet par octet, poids fort d'abord.
    uint8_t  buf_[96] = {};                // max : FMOVEM des 8 registres (8×12)
    uint8_t  pad1_ = 0;                    // 279 : → int aligné sur 4 (cf. pad0_)
public:
    // Le FPU n'a pas de serialize() : Bus le copie en bloc (POD). Cette garde permet
    // au Bus de valider les index APRÈS restauration sans changer le format du
    // save-state. Sans elle, un état forgé passant le CRC (bufIn_, bufPos_=0,
    // bufLen_ > 96) faisait écrire Fpu::write8 au-delà de buf_[96] — corruption du tas.
    bool stateValid() const {
        return bufLen_ >= 0 && bufLen_ <= int(sizeof buf_)
            && bufPos_ >= 0 && bufPos_ <= bufLen_;
    }
private:
    int      bufLen_ = 0, bufPos_ = 0;
    bool     bufIn_  = false;              // true = on attend des octets du CPU
    uint8_t  pad2_[3] = {};                // 289..291 : bool → enum aligné sur 4 (cf. pad0_)
    enum class After { None, GenOp, MoveOutDone, CtrlIn, MovemIn, RestoreIn };
    After    after_  = After::None;        // quoi faire une fois le tampon plein/vidé
    uint16_t cmd_    = 0;                  // mot de commande en cours
    uint8_t  pad3_[2] = {};                // 298..299 : uint16 → int aligné sur 4 (cf. pad0_)

    // ---- Décodage / exécution ----
    void command(uint16_t cmd);            // écriture du Command CIR
    void condition(uint16_t pred);         // écriture du Condition CIR
    void restoreHeader(uint16_t fmt);      // écriture du Restore CIR
    void completeInput();                  // tampon d'entrée plein → exécuter
    void genOp(uint16_t cmd, Ext src);     // opérations opclass 000/010 (opmode)

    // Livraison d'exception FP (modèle coprocesseur) : si une exception ACTIVÉE dans le
    // FPCR (octet enable, bits 15-8) est survenue (octet status FPSR, bits 15-8), le
    // Response CIR renvoie la primitive « Take Pre-Instruction Exception » (CA=0, vecteur
    // en octet bas) au lieu du null. Réf. MC68881 UM §6 (livraison d'exception coproc).
    // Sur Mega STE/SFP004 il n'y a PAS de fil d'IRQ : c'est la seule voie de signalement,
    // scrutée. Strictement conditionnée aux bits enable (0 par défaut → aucun impact).
    void checkException();
    void startMoveOut(uint16_t cmd);       // opclass 011 : FMOVE FPn → mémoire
    void armOut(int len, After after);     // prépare un transfert FPU → CPU
    void armIn(int len, After after);      // prépare un transfert CPU → FPU
    void setIdle();                        // response = null PF=1

    // ---- Conversions de formats ----
    static int    fmtLen(int fmt);         // longueur en octets d'un format
    Ext           decodeFmt(int fmt, const uint8_t* b);
    void          encodeFmt(int fmt, const Ext& v, uint8_t* b, int k);
    static double extToD(const Ext& e);
    static Ext    dToExt(double d);
    void          setCC(const Ext& v);     // FPSR N/Z/I/NAN d'après une valeur
    double        roundMode(double v) const;
    static Ext    romConstant(int off, int roundMode, bool& inexact);  // table ROM FMOVECR (rndoff par mode)

    // ---- Softfloat 80 bits (mantisse 64 bits réelle) : helpers définis dans le .cpp ----
    sf::Status sfStatus() const;           // état d'arrondi softfloat depuis le FPCR
    void       sfFold(uint8_t flags);      // replie les drapeaux softfloat dans le FPSR (EXC + AEXC)
    static sf::f80 toF(const Ext& e) { return sf::f80{ e.se, e.man }; }
    static Ext     toE(sf::f80 f)    { return Ext{ f.high, f.low }; }   // via le constructeur (pad_ au milieu)

    // Journalise les commandes décodées (anti-spam) — débogage du dialogue CIR.
    void trace(const char* what, uint16_t v);
    int  traceCount_ = 0;
};
static_assert(sizeof(Fpu) == 304, "Fpu : format de save-state (bloc de 304 octets, bourrage explicite)");
