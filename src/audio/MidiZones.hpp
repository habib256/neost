// =============================================================================
//  MidiZones.hpp — Splitter clavier : des ZONES de notes, chacune sur son canal.
//
//  Sans zones, une source MIDI hôte = un canal (forceChannel, cf. MidiInHost) : un
//  clavier de 88 touches ne joue qu'UN instrument du séquenceur. Un clavier maître
//  de studio se découpe pourtant couramment — main gauche une basse sur le canal 2,
//  main droite un piano sur le canal 1, une octave au-dessus. C'est le travail d'un
//  « splitter » (boîtier externe, ou fonction du clavier maître lui-même), et NeoST
//  en tient lieu à l'entrée, source par source, AVANT la fusion vers le MIDI IN du ST.
//
//  Ce n'est PAS du matériel Atari émulé : c'est un accessoire de studio côté hôte,
//  sans effet sur la machine tant qu'aucune zone n'est posée. Les règles ci-dessous
//  sont donc des CHOIX de conception, écrits pour être relus, pas un comportement
//  matériel à prouver.
//
//  ── Grammaire (neost.cfg, GUI, --midi-in-zones) ───────────────────────────────
//    zones := zone ( ',' zone )*
//    zone  := lo [ '-' hi ] ':' canal [ ('+'|'-') demi-tons ]
//  lo/hi : numéros de note MIDI 0-127 (60 = do central), bornes INCLUSES.
//  canal : 1-16, ou 0 = le canal de la source (après son éventuel forçage).
//  Exemple : « 0-59:2,60-127:1+12 » — basse à gauche sur le canal 2, piano à droite
//  sur le canal 1 transposé d'une octave vers le haut. Ni ';' ni '|' : la chaîne
//  vit telle quelle dans un champ de midi_in_devices= (cf. AppConfig.cpp).
//
//  ── Ce que fait chaque famille de messages ──────────────────────────────────
//    · NOTES ($80 Note Off, $90 Note On, $A0 pression polyphonique) : envoyées à
//      CHAQUE zone qui contient la note (zones chevauchantes = superposition de deux
//      sons), avec la transposition de la zone. Une note hors de toute zone, ou
//      poussée hors de 0-127 par la transposition, est JETÉE — sa Note Off l'est
//      aussi, puisque la même note tombe dans les mêmes zones : rien ne reste tenu.
//    · AUTRES MESSAGES DE VOIE (contrôleurs, pédale de maintien, pitch bend,
//      changement de programme, pression de canal) : envoyés UNE fois sur chaque
//      canal DISTINCT des zones. Une pédale de maintien qui ne tiendrait que la
//      moitié du clavier serait un piège ; c'est aussi le défaut des claviers
//      maîtres qui découpent en zones.
//    · SYSTÈME ($F0-$FF : horloge, SysEx, start/stop) : intacts, sans canal.
//
//  Fonctions PURES : neost-selftest les éprouve sans clavier branché, donc en CI.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neost::midi {

struct KeyZone {
    int lo = 0, hi = 127;   // notes MIDI, bornes incluses
    int channel = 0;        // 1-16 ; 0 = canal de la source (après forçage)
    int transpose = 0;      // demi-tons, -127..127
    bool operator==(const KeyZone& o) const {
        return lo == o.lo && hi == o.hi && channel == o.channel && transpose == o.transpose;
    }
};

// Au-delà, une config est plus probablement corrompue qu'utile : seize zones
// couvrent déjà les seize canaux.
inline constexpr std::size_t kMaxZones = 16;

// Analyse `s` selon la grammaire de l'en-tête. Rend false (et `out` vide) sur la
// moindre erreur : une zone à moitié comprise jouerait des notes au mauvais endroit
// sans le dire. Chaîne vide = aucune zone, ce qui est VALIDE (le défaut).
inline bool parseZones(const std::string& s, std::vector<KeyZone>& out) {
    out.clear();
    std::size_t i = 0;
    const auto skipSpaces = [&] { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; };
    const auto number = [&](int& v) {
        skipSpaces();
        const std::size_t start = i;
        long acc = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - start < 4)
            acc = acc * 10 + (s[i++] - '0');
        if (i == start || (i < s.size() && s[i] >= '0' && s[i] <= '9')) return false;
        v = int(acc);
        skipSpaces();
        return true;
    };
    skipSpaces();
    if (i == s.size()) return true;
    for (;;) {
        KeyZone z;
        if (!number(z.lo)) { out.clear(); return false; }
        z.hi = z.lo;
        if (i < s.size() && s[i] == '-') { ++i; if (!number(z.hi)) { out.clear(); return false; } }
        if (i >= s.size() || s[i] != ':') { out.clear(); return false; }
        ++i;
        if (!number(z.channel)) { out.clear(); return false; }
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            const bool neg = s[i] == '-';
            ++i;
            int t = 0;
            if (!number(t)) { out.clear(); return false; }
            z.transpose = neg ? -t : t;
        }
        if (z.lo > 127 || z.hi > 127 || z.lo > z.hi || z.channel > 16
            || z.transpose < -127 || z.transpose > 127 || out.size() >= kMaxZones) {
            out.clear(); return false;
        }
        out.push_back(z);
        if (i == s.size()) return true;
        if (s[i] != ',') { out.clear(); return false; }
        ++i;
    }
}

// Forme canonique : relue par parseZones, elle redonne exactement les mêmes zones.
inline std::string formatZones(const std::vector<KeyZone>& zones) {
    std::string out;
    for (const KeyZone& z : zones) {
        if (!out.empty()) out += ',';
        out += std::to_string(z.lo);
        if (z.hi != z.lo) { out += '-'; out += std::to_string(z.hi); }
        out += ':';
        out += std::to_string(z.channel);
        if (z.transpose > 0) { out += '+'; out += std::to_string(z.transpose); }
        else if (z.transpose < 0) out += std::to_string(z.transpose);   // porte son '-'
    }
    return out;
}

// Applique le forçage de canal de la source PUIS ses zones à UN message complet
// (statut compris, jamais un fragment — cf. MidiMessageParser), et rend 0, 1 ou
// plusieurs messages par `emit(const uint8_t* msg, int len)`.
template <typename Emit>
void routeVoice(const uint8_t* msg, int len, int forceChannel,
                const std::vector<KeyZone>& zones, const Emit& emit) {
    if (len <= 0) return;
    const uint8_t head = msg[0];
    if (head < 0x80 || head >= 0xF0 || len > 3) { emit(msg, len); return; }   // système
    const uint8_t type = head & 0xF0;
    const uint8_t base = (forceChannel >= 1 && forceChannel <= 16)
                       ? uint8_t(forceChannel - 1) : uint8_t(head & 0x0F);
    uint8_t out[3] = {uint8_t(type | base), len > 1 ? msg[1] : uint8_t(0),
                      len > 2 ? msg[2] : uint8_t(0)};
    if (zones.empty()) { emit(out, len); return; }

    const auto chanOf = [base](const KeyZone& z) {
        return z.channel >= 1 && z.channel <= 16 ? uint8_t(z.channel - 1) : base;
    };
    // Deux zones peuvent produire le MÊME message (chevauchement, même canal, même
    // transposition) : l'envoyer deux fois doublerait la note, et sa Note Off
    // unique ne relâcherait que l'une des deux voix sur bien des synthés.
    uint8_t sent[kMaxZones][2];
    std::size_t nsent = 0;
    const auto already = [&](uint8_t ch, uint8_t key) {
        for (std::size_t k = 0; k < nsent; ++k)
            if (sent[k][0] == ch && sent[k][1] == key) return true;
        return false;
    };
    const bool isNote = (type == 0x80 || type == 0x90 || type == 0xA0) && len >= 2;
    for (const KeyZone& z : zones) {
        uint8_t key = 0;
        if (isNote) {
            if (msg[1] < z.lo || msg[1] > z.hi) continue;
            const int n = int(msg[1]) + z.transpose;
            if (n < 0 || n > 127) continue;              // hors clavier MIDI : jetée
            key = uint8_t(n);
        }
        const uint8_t ch = chanOf(z);
        if (already(ch, key) || nsent >= kMaxZones) continue;
        sent[nsent][0] = ch; sent[nsent][1] = key; ++nsent;
        out[0] = uint8_t(type | ch);
        if (isNote) out[1] = key;
        emit(out, len);
    }
}

} // namespace neost::midi
