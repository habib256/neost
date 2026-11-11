// =============================================================================
//  CartridgeKey — clés de protection Steinberg sur le port cartouche (/ROM3)
// =============================================================================
//
//  Deux clés matérielles ont réellement existé ; toutes deux vivent dans la banque
//  /ROM3 = $FB0000-$FBFFFF (le TOS ne sonde que /ROM4 = $FA0000 pour le magic de
//  cartouche : une clé est donc invisible du système, et cohabite avec le lecteur
//  GEMDOS de NeoST qui occupe $FA0000).
//
//  · Clé NOIRE (Cubase 2.01) — PAL16R8 : 8 bascules D, entrées A1-A8, sorties
//    D8-D15 (octet fort), horloge = front montant de /UDS. ⚠ /UDS est le strobe
//    d'octet fort de CHAQUE cycle bus du 68000, où qu'il aille : la PAL avance aussi
//    sur les fetchs d'opcode entre deux lectures de la clé. Le motif exact d'accès
//    du CPU fait donc partie du secret — Cubase coupe les interruptions et exécute
//    sa routine depuis une adresse fixe. Émulable seulement si le cœur a le motif
//    bus d'un vrai 68000 (Moira modélise le prefetch) : « au mieux » ici.
//  · Clé ROUGE (Cubase 3.10, Cubase Score 2.0x, Cubase Audio Falcon) — EPLD Intel
//    5C060 : 16 bascules T, UNE entrée (A8), UNE sortie (D8), horloge = front
//    montant de /ROM3, c'est-à-dire la fin de chaque accès à $FBxxxx et rien
//    d'autre. Émulable fidèlement.
//
//  Dans les deux cas, une lecture renvoie l'état COURANT des registres (ils ne
//  basculent qu'à la fin du cycle, après l'échantillonnage par le CPU) ; l'octet
//  faible lit $FF (rien sur D0-D7). Le motif d'adresse A8..A1 = %11011000 rend tous
//  les termes vrais sur la clé noire → état suivant 0 : c'est le « reset logiciel »
//  de Cubase 2. À la mise sous tension, les registres sont à 0 (datasheet 5C060 ;
//  pour la PAL, hypothèse reprise de MiSTery).
//
//  Équations : clé noire relevée par force brute sur une clé réelle (MasterOfGizmo,
//  2022, Arduino + Espresso) ; clé rouge = JED de la puce décapsulée (UnnamedVillain),
//  décompilé par troed (« No rights reserved »). Transcrites de cubase2_dongle.v /
//  cubase3_dongle.v du cœur FPGA MiSTery (gyurco). Hatari n'émule aucune clé.
//
//  · Clé NOTATOR / CREATOR (C-Lab ; même clé dans l'Unitor-N) — EP600 (= 5C060).
//    Même famille que la clé noire (8 bascules D, sorties actives bas, « opérations »
//    sélectionnées par A1/A4/A5) avec une BASCULE D'ARMEMENT en plus :
//      FEEDB1 := STER(A8..A1)        cadencée par /ROM4 (fin de tout accès $FAxxxx),
//      STER   =  A7·A6·A5·/A4·A3·/A2·A1·/A8   (octet A8..A1 = $75 → $FA00EA / $FB00EA),
//      clock des données = /FEEDB1·UDS + FEEDB1·/ROM3 :
//        désarmée → front montant de UDS (fin de CHAQUE cycle bus, comme la noire) ;
//        armée    → /ROM3 qui descend (DÉBUT d'un accès $FBxxxx) : le CPU lit l'état
//                   APRÈS le coup d'horloge.
//    Tout terme contient STER : l'accès d'armement ($FA00EA) remet les 8 bascules à 0
//    (UDS remonte avant /ROM4 — décodage GLUE — donc les données sont cadencées avec
//    STER=1 juste avant que FEEDB1 ne passe à 1). Armée, un accès $FBxxxx avec A4·A2
//    efface D9 et A3·A1 efface D8 de façon asynchrone (vu par le CPU pendant l'accès).
//    Équations : relevé TPH (Unnamed Villain, JED EP600 décapsulé par Zippy,
//    atari-forum « Notator Dongle Dump », publié en octobre 2025, « YOU GOT IT FOR
//    FREE then GIVE IT FOR FREE! ») via notator_dongle.c du firmware SidecarTridge
//    md-notator (M. Petruccioli, 2026). Le GEMDOS HD de NeoST vit sur /ROM4 : ses
//    accès DÉSARMENT la clé (FEEDB1 := 0), ce que Notator tolère s'il réarme à chaque
//    contrôle — comme le ferait tout accès $FAxxxx d'un TOS sur une vraie machine.
//
//  OFF par défaut ; sans clé, $FBxxxx lit $FF comme avant. Aucun logiciel à clé
//  n'est livré avec NeoST (Cubase Lite n'en a pas besoin) : l'implémentation est
//  fidèle aux équations, mais n'a pas encore été confrontée à un Cubase 3.10 ni à
//  un Notator.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include "core/CartDevice.hpp"

class StateArchive;

class CartridgeKey : public CartDevice {
public:
    enum class Model : uint8_t { None = 0, Cubase2 = 1, Cubase3 = 2, Auto = 3, Notator = 4 };

    void setModel(Model m) { model_ = m; reset(); }
    Model model() const { return model_; }
    bool  attached() const { return model_ != Model::None; }
    // wantsUds (CartDevice) : vrai si la clé doit voir CHAQUE cycle bus du CPU (clé
    // noire, Notator, ou Auto non encore tranché) — le Bus ne boucle sur /UDS qu'alors.

    // Mise sous tension : registres à 0 (cf. en-tête). Pas au reset à chaud : le port
    // cartouche n'a pas de ligne /RESET (cf. Machine::reset).
    void reset();

    // Lecture CPU dans $FB0000-$FBFFFF. `first` = premier octet de l'accès (octet
    // pair d'un mot, ou accès octet) : c'est là que l'on fait avancer la clé ROUGE
    // (/ROM3 remonte en fin de cycle, après que la valeur a été échantillonnée).
    uint8_t cartRead(uint32_t addr, bool first);

    // Lecture CPU dans $FA0000-$FAFFFF (/ROM4) : la clé Notator y cadence sa bascule
    // d'armement (appliquée en fin de cycle, dans udsCycle). Sans effet sur les autres.
    void rom4Listen(uint32_t addr, bool first);

    // --- CartDevice : signaux du port cartouche (cf. core/CartDevice.hpp) ---------
    bool rom3Read(uint32_t a, bool first, uint8_t& out) override {
        if (!attached()) return false;
        out = cartRead(a, first); return true;
    }
    bool rom4Read(uint32_t a, bool first, uint8_t&) override { rom4Listen(a, first); return false; }
    void udsCycle(uint32_t addr) override;
    bool wantsUds() const override { return attached() && (chosen_ == Model::Cubase2 || chosen_ == Model::Notator || !locked_); }

    // --- Observabilité -------------------------------------------------------------
    // Journal des accès (format de TRACE DE RÉFÉRENCE, cf. docs/EXTENSIONS.md) :
    //   R3 <A8..A1 hex> <octet fort lu>   lecture $FBxxxx (1er octet de l'accès)
    //   R4 <A8..A1 hex>                   lecture $FAxxxx (clé Notator : armement)
    //   U  <A8..A1 hex>                   cycle /UDS HORS fenêtre cartouche (ceux de la
    //                                     fenêtre sont déjà impliqués par R3/R4)
    // Une capture matérielle (analyseur logique, SidecarTridge) mise à ce format se
    // REJOUE contre la machine d'état : replay() compare chaque R3 et signale le
    // premier écart. C'est l'oracle qui manque tant qu'aucun logiciel à clé n'est là.
    void     setLog(FILE* f) { log_ = f; }
    // Rejoue un fichier de trace ; renvoie le nombre d'écarts (−1 : fichier illisible).
    // `err` reçoit la première ligne en écart (numéro + attendu/obtenu).
    int      replay(const char* path, char* err, std::size_t errLen);
    uint32_t probes() const { return probes_; }     // lectures /ROM3 (1er octet)
    uint8_t  lastByte() const { return last_; }

    void serialize(StateArchive& ar);

    // Pour les auto-tests : état interne (8 bits clé noire ; 16 bits clé rouge).
    uint16_t state() const { return model_ == Model::Cubase2 ? d_ : model_ == Model::Notator ? n_ : r_; }
    bool     armed() const { return feedb1_; }   // Notator : FEEDB1

private:
    uint8_t  outputByte() const;   // D15..D8 vus par le CPU
    void     clock2(uint8_t a);    // clé noire : A8..A1 → nouvel état
    void     clock3(bool a8);      // clé rouge : A8 → nouvel état
    void     clockN(uint8_t a);    // clé Notator : A8..A1 → nouvel état (8 bascules D)
    static bool ster(uint8_t a) { return (a & 0xFF) == 0x75; }   // A7·A6·A5·/A4·A3·/A2·A1·/A8

    Model    model_  = Model::None;
    // Auto (heuristique MiSTery) : au PREMIER accès $FBxxxx après reset, A7..A1 ≠ 0
    // → clé noire (Cubase 3 lit toujours avec A7..A1 = 0), puis verrouillé.
    Model    chosen_ = Model::None;
    bool     locked_ = false;
    uint8_t  d_ = 0;               // clé noire : D15..D8 (bit7 = D15)
    uint16_t r_ = 0;               // clé rouge : bits 0-15 = pin03..pin10, pin15..pin21, pin22(d8)
    uint8_t  n_ = 0;               // clé Notator : D15..D8 (bit7 = D15)
    bool     feedb1_ = false;      // clé Notator : armée (FEEDB1)
    int8_t   rom4Pending_ = -1;    // clé Notator : STER de l'accès /ROM4 en cours (-1 : aucun)
    FILE*    log_ = nullptr;       // journal de trace (hors snapshot)
    uint32_t probes_ = 0;          // compteur de lectures /ROM3 (hors snapshot)
    uint8_t  last_ = 0xFF;         // dernier octet fort rendu (hors snapshot)
};
