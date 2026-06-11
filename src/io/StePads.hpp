// =============================================================================
//  StePads.hpp — Joypads / paddles / lightpen STE et Mega STE ($FF9200-$FF9222).
//
//  Port fidèle de Hatari `joy.c` (Joy_StePadButtons_DIPSwitches_ReadWord,
//  Joy_StePadMulti_ReadWord, Joy_StePadAnalogX/Y, Joy_SteLightpenX/Y) et de la
//  table de routage `ioMemTabSTE.c`. Présent UNIQUEMENT sur STE / Mega STE (le ST
//  d'origine faute en $FF9200).
//
//  Le STE expose deux connecteurs « enhanced joypad » (A et B), chacun derrière le
//  même MULTIPLEXEUR : le programme écrit un masque de sélection de lignes dans
//  $FF9202 (latch `select_`), puis lit l'état correspondant en $FF9200 (boutons
//  feu) et $FF9202 (directions + boutons numérotés). Un JOYPAD complet a un pavé
//  directionnel (4 directions), un bouton FEU et des boutons A/B/C/OPTION/PAUSE +
//  un pavé numérique 0-9/#/*. NeoST ne reçoit qu'un joystick ST simple par port
//  (directions + 1 feu), comme Hatari quand une manette « ordinaire » est branchée
//  sur le port STE → seules les lignes « directions » et le bouton FEU/A sont
//  alimentées ; les boutons B/C/OPTION/pavé restent au repos (relâchés).
//
//  Repère des octets ST d'une manette (cf. Hatari joy.h, ATARIJOY_BITMASK_*) :
//    bit0 = HAUT, bit1 = BAS, bit2 = GAUCHE, bit3 = DROITE, bit7 = FEU.
//
//  Branchement de l'entrée : `setJoystick(joy0, joy1)` reçoit le MÊME état que
//  l'IKBD (Ikbd::setJoystick) — pad A = port « jeux » (1), pad B = port 0 (souris).
//  C'est exactement le mapping d'Hatari, qui alimente JOYID_JOYPADA/B depuis la
//  config manette globale.
//
//  Multiplexage (joy.c) — `select_` = mot écrit en $FF9202 :
//    - nibble bas (bits 0-3) → pad A, nibble haut (bits 4-7) → pad B ;
//    - une ligne est ACTIVE quand son bit de sélection vaut 0 (logique inversée) ;
//    - $FF9202.read : ligne « directions » (bit i = 0) → 4 bits directions inversés ;
//    - $FF9200.read : ligne « directions » → bouton FEU (et PAUSE) ; les lignes
//      B/C/OPTION renvoient leurs boutons respectifs.
//
//  DIP switches Mega STE (octet HAUT de $FF9200, cf. ioMemTabSTE.c) : logique
//  inversée (bit 0 = switch ON). Switch 7 (bit 6) ON = lecteur HD 1.44 monté ;
//  switch 8 (bit 7) ON = son DMA DÉSACTIVÉ. Défaut Hatari = 0xBF (HD monté, son DMA
//  actif, switches 1-6 OFF). Le STE simple n'a pas de DIP → octet haut = 0xFF.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

class StePads {
public:
    // Bits d'un octet joystick ST (cf. Hatari joy.h, ATARIJOY_BITMASK_*).
    enum : uint8_t { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, FIRE = 0x80 };

    // Valeur des DIP switches Mega STE (octet haut de $FF9200). 0xBF = HD monté +
    // son DMA actif (logique inversée, switch 7/bit6 = 0 → HD ; switch 8/bit7 = 1
    // → son DMA actif). Fidèle à IoMemTabMegaSTE_DIPSwitches_Read d'Hatari.
    static constexpr uint8_t MEGASTE_DIP = 0xBF;

    // État joystick hôte → pads STE. `joy0` = port 0 (souris) → pad B,
    // `joy1` = port 1 (« jeux ») → pad A. Octets ST (UP/DOWN/LEFT/RIGHT/FIRE).
    // `megaSte` indique si la machine possède les DIP switches.
    void setMegaSte(bool v) { megaSte_ = v; }
    void setJoystick(uint8_t joy0, uint8_t joy1) { padA_ = joy1; padB_ = joy0; }

    // --- $FF9202 : latch de sélection des lignes du multiplexeur (mot) ----------
    // Écrit par le programme avant de lire $FF9200/$FF9202. Hatari latche le MOT
    // entier (nSteJoySelect = IoMem_ReadWord(0xff9202)) ; les bits de sélection
    // utiles sont dans l'octet BAS ($FF9203) : `& 0x0f` (pad A), `& 0xf0` (pad B).
    void writeSelectWord(uint16_t v) { select_ = v; }
    void writeSelectByte(uint32_t addr, uint8_t v) {
        if (addr & 1) select_ = uint16_t((select_ & 0xFF00) | v);          // $FF9203 (octet bas)
        else          select_ = uint16_t((select_ & 0x00FF) | (v << 8));   // $FF9202 (octet haut)
    }
    uint16_t select() const { return select_; }

    // --- Lectures (renvoient le MOT entier, comme Hatari IoMem_WriteWord) -------
    // L'appelant (Bus) extrait l'octet voulu selon l'adresse (big-endian).

    // $FF9200.w : boutons feu (octet bas) + DIP switches Mega STE (octet haut).
    uint16_t readButtonsDip() const {
        uint16_t nData = 0xFF;                       // boutons relâchés (actif bas)

        // Pad A (port 1). Une ligne est active quand son bit de sélection = 0.
        if ((select_ & 0x0F) != 0x0F) {
            const int b = fireButtons(padA_);        // bit0 = bouton A (= FEU ST)
            if (!(select_ & 0x1)) {                  // ligne « directions/A »
                if (b & 0x01) nData &= ~2;           // bouton A pressé → bit1 bas
                // PAUSE (b&0x10) : pas alimenté par un joystick simple.
            }
            // Lignes B/C/OPTION : aucun bouton sur un joystick simple → rien.
        }

        // Pad B (port 0). Bits de sélection dans le nibble haut.
        if ((select_ & 0xF0) != 0xF0) {
            const int b = fireButtons(padB_);
            if (!(select_ & 0x10)) {                 // ligne « directions/A »
                if (b & 0x01) nData &= ~8;           // bouton A pad B → bit3 bas
            }
        }

        // Octet haut : DIP switches Mega STE, sinon 0xFF (STE simple, pas de DIP).
        const uint8_t dip = megaSte_ ? MEGASTE_DIP : 0xFF;
        return uint16_t((dip << 8) | (nData & 0xFF));
    }

    // $FF9202.w : directions + boutons numérotés des deux pads (multiplexés).
    // Hatari renvoie (nData << 8) | 0x00FF : l'info utile est dans l'octet HAUT
    // ($FF9202), l'octet bas ($FF9203) reste à 0xFF.
    uint16_t readDirections() const {
        uint16_t nData = 0xFF;

        // Pad A → nibble bas de nData (lignes sélectionnées par bits 0-3).
        if ((select_ & 0x0F) != 0x0F) {
            nData &= 0xF0;
            if (!(select_ & 0x1))                    // ligne « directions »
                nData |= uint16_t(~stickData(padA_) & 0x0F);
            // Lignes B/C/OPTION (pavé numérique) : non alimentées → nibble reste 0
            // (équivaut à ~0 & 0x0F = 0x0F « relâché » seulement si bouton ; ici 0).
            // Conforme à joy.c : aucun bouton pavé → Joy_GetFireButtons renvoie 0,
            // ~(0)>>k & 0x0f = 0x0f. On reproduit ce 0x0f « relâché » :
            else nData |= 0x0F;
        }

        // Pad B → nibble haut de nData.
        if ((select_ & 0xF0) != 0xF0) {
            nData &= 0x0F;
            if (!(select_ & 0x10))                   // ligne « directions »
                nData |= uint16_t((~stickData(padB_) << 4) & 0xF0);
            else nData |= 0xF0;
        }

        return uint16_t((nData << 8) | 0x00FF);
    }

    // $FF9211/13/15/17 : paddles / axes analogiques STE (octet) — port de
    // Joy_GetStickAnalogData : plage $04 (butée min) .. $43 (butée max), neutre
    // $24. Deux sources, comme Hatari :
    //  - axes ANALOGIQUES hôte (manette, cf. setAnalog) si le frontend en fournit
    //    (mode JOYSTICK_REALSTICK : val = MIN + (upos>>8)/4 sur l'axe 16 bits) ;
    //  - sinon, dérivés du joystick NUMÉRIQUE du pad (gauche/haut → MIN,
    //    droite/bas → MAX, repos → MID), comme le mode clavier d'Hatari.
    static constexpr uint8_t ANALOG_MIN = 0x04;
    static constexpr uint8_t ANALOG_MID = 0x24;
    static constexpr uint8_t ANALOG_MAX = 0x43;

    // Axes hôte du pad (`pad` 0 = A/$9211-13, 1 = B/$9215-17), valeurs −1..+1.
    void setAnalog(int pad, float x, float y) {
        analog_[pad & 1][0] = x;
        analog_[pad & 1][1] = y;
        hasAnalog_[pad & 1] = true;
    }

    uint8_t readAnalog(uint32_t addr) const {
        const int  pad   = ((addr & 0x0F) >= 0x5) ? 1 : 0;   // $9211/13 = A, $9215/17 = B
        const bool xAxis = ((addr & 0x3) == 0x1);             // $..1/..5 = X, $..3/..7 = Y
        if (hasAnalog_[pad]) {
            // Port exact de la conversion REALSTICK d'Hatari : axe −32768..32767
            // → MIN + (octet haut de l'axe non signé) / 4 → $04..$43.
            float f = analog_[pad][xAxis ? 0 : 1];
            if (f < -1.f) f = -1.f; else if (f > 1.f) f = 1.f;
            int pos = static_cast<int>(f * 32767.f);
            const unsigned upos = static_cast<unsigned>(32768 + pos);
            return uint8_t(ANALOG_MIN + ((upos & 0xFF00) >> 8) / ANALOG_MIN);
        }
        const uint8_t st = pad ? padB_ : padA_;               // repli numérique
        if (st & (xAxis ? LEFT : UP))    return ANALOG_MIN;
        if (st & (xAxis ? RIGHT : DOWN)) return ANALOG_MAX;
        return ANALOG_MID;
    }

    // $FF9220/22 : lightpen X/Y (mot). Non supporté (comme Hatari) → 0.
    uint16_t readLightpen() const { return 0x0000; }

private:
    // Octet directions+feu d'un pad au format ST (cf. Joy_GetStickData). NeoST
    // fournit déjà cet octet (UP/DOWN/LEFT/RIGHT/FIRE) → renvoyé tel quel.
    static uint8_t stickData(uint8_t st) { return st; }

    // Boutons feu façon Joy_GetFireButtons : bit0 = bouton A (= FEU d'un joystick
    // simple). Les boutons B/C/OPTION/PAUSE n'existent pas sur un joystick simple.
    static int fireButtons(uint8_t st) { return (st & FIRE) ? 0x01 : 0x00; }

    uint16_t select_ = 0;        // latch $FF9202 (masque de sélection des lignes)
    uint8_t  padA_   = 0;        // pad A = port 1 (« jeux »)
    uint8_t  padB_   = 0;        // pad B = port 0 (souris)
    bool     megaSte_ = false;   // possède les DIP switches motherboard
    float    analog_[2][2] = {{0.f, 0.f}, {0.f, 0.f}};   // axes hôte X/Y par pad (−1..+1)
    bool     hasAnalog_[2] = {false, false};             // setAnalog appelé → mode réel
};
