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
//  Niveau d'émulation NeoST = FONCTIONNEL : dialogue CIR complet (Command/
//  Response/Operand/Condition/Save/Restore), registres FP0-FP7 en étendu
//  80 bits, FPCR/FPSR/FPIAR, formats B/W/L/S/D/X/P, constantes ROM FMOVECR
//  bit-exactes, arithmétique + transcendantes via le FPU hôte (précision
//  double 53 bits — pas les 64 bits de mantisse du vrai 68881, suffisant pour
//  le logiciel ST ; les FMOVE.X sans calcul restent bit-exacts). Les
//  exceptions FP positionnent FPSR mais ne lèvent pas d'IRQ (le socket Mega
//  STE se scrute, la glue SFP004 n'utilise pas d'interruption).
//  Par défaut : ABSENT (fidèle Hatari) — activer via --fpu / option GUI.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

class Fpu {
public:
    // Présence du coprocesseur (socket peuplé). false = fidèle Hatari : la zone
    // $FFFA40-$FFFA5F déclenche une bus error et la sonde conclut « not found ».
    bool present = false;

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
        uint64_t man = 0xFFFFFFFFFFFFFFFFull;
    };

    // ---- État programmeur ----
    Ext      fp_[8];
    uint32_t fpcr_ = 0, fpsr_ = 0, fpiar_ = 0;

    // ---- Interface CIR ----
    uint16_t response_ = 0x0802;           // null : PF=1 (idle), TF=0
    uint8_t  latch_[0x20] = {};            // derniers octets écrits (relisibles)

    // Tampon de transfert de l'Operand CIR ($10-$13) : les transferts > 4
    // octets bouclent sur la même fenêtre, octet par octet, poids fort d'abord.
    uint8_t  buf_[96] = {};                // max : FMOVEM des 8 registres (8×12)
    int      bufLen_ = 0, bufPos_ = 0;
    bool     bufIn_  = false;              // true = on attend des octets du CPU
    enum class After { None, GenOp, MoveOutDone, CtrlIn, MovemIn, RestoreIn };
    After    after_  = After::None;        // quoi faire une fois le tampon plein/vidé
    uint16_t cmd_    = 0;                  // mot de commande en cours

    // ---- Décodage / exécution ----
    void command(uint16_t cmd);            // écriture du Command CIR
    void condition(uint16_t pred);         // écriture du Condition CIR
    void restoreHeader(uint16_t fmt);      // écriture du Restore CIR
    void completeInput();                  // tampon d'entrée plein → exécuter
    void genOp(uint16_t cmd, Ext src);     // opérations opclass 000/010 (opmode)
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
    static Ext    romConstant(int off);    // table ROM FMOVECR (bit-exacte)

    // Journalise les commandes décodées (anti-spam) — débogage du dialogue CIR.
    void trace(const char* what, uint16_t v);
    int  traceCount_ = 0;
};
