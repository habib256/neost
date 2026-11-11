// =============================================================================
//  MidiMessageParser.hpp — Flux d'octets MIDI → messages complets.
//
//  Un octet MIDI seul ne veut rien dire : il faut le statut qui le précède, et ce
//  statut peut être IMPLICITE (running status — un émetteur qui enchaîne des notes
//  sur le même canal n'envoie l'octet de statut qu'une fois). Reconstituer les
//  messages est nécessaire dès qu'on fait autre chose que transporter le flux tel
//  quel :
//    · MidiOutHost — CoreMIDI et ALSA veulent des MESSAGES, pas des octets ;
//    · MidiInHost  — FUSIONNER plusieurs appareils exige d'entrelacer aux
//      frontières de messages. Entrelacer des octets bruts produirait du charabia :
//      le « 3C » d'un clavier tombant entre le « 90 » et le « 40 » de l'autre.
//
//  Trois règles du protocole, que le décodeur applique :
//    · TEMPS RÉEL ($F8-$FF, horloge, start/stop, active sensing) : un seul octet,
//      autorisé N'IMPORTE OÙ — y compris au milieu d'un autre message, SysEx
//      compris. Il ne touche pas au running status.
//    · SYSEX ($F0 … $F7) : longueur libre. Un octet de statut avant $F7 l'INTERROMPT
//      (c'est ce que fait un vrai récepteur, pas une erreur à ignorer).
//    · SYSTÈME COMMUN ($F1-$F7) : annule le running status.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace neost::midi {

// Nombre d'octets de DONNÉES qui suivent un octet de statut.
inline int dataBytesFor(uint8_t status) {
    switch (status & 0xF0) {
    case 0xC0: case 0xD0: return 1;                 // Program Change, Channel Pressure
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    default: break;
    }
    switch (status) {                                // système commun
    case 0xF1: case 0xF3: return 1;
    case 0xF2: return 2;
    default: return 0;
    }
}

// Longueur TOTALE (statut compris) d'un message court, ou 0 si l'octet n'est pas un
// statut. C'est le contrat de winmm sous Windows : MM_MIM_DATA livre un message
// complet EMPAQUETÉ dans un mot de 32 bits (statut, donnée 1, donnée 2), sans dire
// combien d'octets comptent — pousser les trois aveuglément ajouterait un octet
// fantôme après un Program Change, que le décodeur lirait comme une donnée orpheline.
// Écrit ICI, en fonction pure, pour être éprouvé sur n'importe quelle plateforme :
// la classe de bug ne se voit pas à l'oreille (un octet de trop se perd dans le
// running status) et le CI n'a pas de clavier branché.
inline int shortMessageLength(uint8_t status) {
    if (status < 0x80) return 0;                     // donnée : pas un message
    return 1 + dataBytesFor(status);
}

// Décodeur d'UN flux (une source, ou la sortie de l'ACIA). `emit(msg, len)` reçoit
// chaque message complet, statut inclus — jamais un fragment.
class Parser {
public:
    // kMaxSysex : un dump de patch tient largement dedans ; au-delà on tronque
    // plutôt que de laisser un émetteur muet (ou malveillant) faire grossir un
    // tampon sans fin. La borne vaut pour le MESSAGE ENTIER, $F0 et $F7 compris —
    // c'est un INVARIANT dont les consommateurs dépendent : l'encodeur ALSA de
    // sortie est dimensionné à cette taille exacte (snd_midi_event_new(65536),
    // MidiOutHost), et un message d'un octet de plus y échouait à l'encodage,
    // donc tombait en silence (bug hunt du 2026-08-30 : le contenu était borné à
    // kMaxSysex AVANT l'ajout du $F7 final — le message tronqué faisait 4 097).
    // 64 Kio : la banque de 32 voix d'un DX7 fait 4 104 octets et un dump d'échantillonneur
    // bien plus — à 4 096 le message était tronqué, $F7 recollé, somme de contrôle fausse.
    // [B] MIDI 1.0, System Exclusive : aucune longueur maximale.
    static constexpr std::size_t kMaxSysex = 65536;

    template <typename Emit>
    void byte(uint8_t b, const Emit& emit) {
        if (b >= 0xF8) { emit(&b, 1); return; }      // temps réel : passe-droit total
        if (inSysex_) {
            if (b == 0xF7) {                         // fin normale
                sysex_.push_back(b);
                emit(sysex_.data(), int(sysex_.size()));
                sysex_.clear(); inSysex_ = false;
            } else if (b & 0x80) {                   // statut : SysEx INTERROMPU
                sysex_.clear(); inSysex_ = false;
                byte(b, emit);                       // l'octet vaut pour lui-même
            } else if (sysex_.size() < kMaxSysex - 1) {   // -1 : place du $F7 final
                sysex_.push_back(b);
            }
            return;
        }
        if (b & 0x80) {
            if (b == 0xF0) { inSysex_ = true; sysex_.assign(1, b); status_ = 0; return; }
            status_ = b; needed_ = dataBytesFor(b); got_ = 0;
            if (needed_ == 0) { emit(&b, 1); status_ = 0; }
            return;
        }
        if (!status_) return;                        // donnée orpheline : rien à en faire
        data_[got_++] = b;
        if (got_ >= needed_) {
            const uint8_t msg[3] = {status_, data_[0], data_[1]};
            emit(msg, 1 + needed_);
            got_ = 0;                                // running status : le statut reste armé
            if (status_ >= 0xF0) status_ = 0;        // sauf pour le système commun
        }
    }

    // Remet le décodeur au repos (changement de source, panique, débranchement) :
    // sans ça, un SysEx interrompu laisserait l'analyse au milieu d'un message.
    void reset() { status_ = 0; needed_ = 0; got_ = 0; inSysex_ = false; sysex_.clear(); }

private:
    uint8_t status_ = 0;              // statut courant (running status)
    int     needed_ = 0, got_ = 0;
    uint8_t data_[2] = {0, 0};
    bool    inSysex_ = false;
    std::vector<uint8_t> sysex_;
};

} // namespace neost::midi
