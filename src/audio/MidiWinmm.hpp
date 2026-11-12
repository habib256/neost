// =============================================================================
//  MidiWinmm.hpp — Ce que winmm sait dire d'un appareil MIDI, et sous quel nom.
//  Partagé par MidiOutHost et MidiInHost : les deux énumèrent de la même façon, et
//  la seule chose qui les distingue est le préfixe midiOut/midiIn des appels.
//
//  ── L'IDENTIFIANT UNIQUE, qu'on croyait absent de Windows ─────────────────────
//  MIDIOUTCAPS ne donne que szPname (31 caractères, tronqué), wMid et wPid — le
//  Circuit Tracks branché ici rend mid=65535 pid=65535, autrement dit RIEN : deux
//  claviers du même modèle seraient rigoureusement indiscernables, et l'index se
//  renumérote au débranchement (le piège inverse, cf. MidiEndpoint.hpp).
//
//  midiOutMessage(DRV_QUERYDEVICEINTERFACE) sort de cette impasse : il rend le
//  chemin d'interface du périphérique, mesuré ici —
//      \\?\usb#vid_1235&pid_0139&mi_00#6&33d600c&0&0000#{6994ad04-...}
//  — où « 6&33d600c&0&0000 » est le chemin PHYSIQUE (hub et prise). Deux appareils
//  identiques branchés ensemble ont donc deux identifiants différents : Windows
//  rejoint CoreMIDI (ALSA, lui, tire l'adresse USB du nom long de la carte : MidiAlsa.hpp).
//  ⚠ Ce chemin contient la PRISE : rebrancher ailleurs change l'identifiant. Le
//  repli par nom de MidiEndpoint.hpp rattrape ce cas (avec son avertissement).
//  ⚠ Les synthés logiciels du système (« Microsoft GS Wavetable Synth ») ne sont pas
//  des périphériques et rendent une chaîne vide : identifiant vide, repli par nom —
//  le régime des clients logiciels sous ALSA.
//
//  Les constantes DRV_* vivent dans mmddk.h, du DDK, que MinGW ne livre pas : elles
//  sont posées ici à leur valeur documentée.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#if defined(NEOST_MIDI_WINMM)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include <string>
#include <vector>

#include "audio/MidiEndpoint.hpp"

namespace neost::midi::winmm {

// mmddk.h (DDK) : DRV_RESERVED = 0x0800.
inline constexpr UINT kQueryDeviceInterface     = 0x0800 + 12;
inline constexpr UINT kQueryDeviceInterfaceSize = 0x0800 + 13;

// Un appareil tel que winmm le numérote. L'index dans la liste rendue N'EST PAS
// l'identifiant winmm : les entrées sans nom sont sautées, et c'est `id` qu'il faut
// passer à midiOutOpen/midiInOpen.
struct Device {
    Endpoint ep;
    UINT     id = 0;
};

// UTF-16 → UTF-8 : le reste de NeoST (neost.cfg compris) est en UTF-8.
inline std::string narrow(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(std::size_t(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// Chemin d'interface du périphérique = notre identifiant unique. Vide si le pilote
// ne le fournit pas (synthés logiciels), ce que l'appariement sait encaisser.
// L'appel prend l'IDENTIFIANT winmm déguisé en handle : c'est la convention de
// midiOutMessage/midiInMessage pour interroger un appareil non ouvert.
template <typename MsgFn>
inline std::string interfaceId(MsgFn msg, UINT id) {
    ULONG bytes = 0;
    if (msg(reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(id)), kQueryDeviceInterfaceSize,
            reinterpret_cast<DWORD_PTR>(&bytes), 0) != MMSYSERR_NOERROR)
        return {};
    // Borne de bon sens : le tampon vient d'un pilote, et une taille aberrante ne
    // doit pas se transformer en allocation aberrante.
    if (bytes == 0 || bytes > 4096) return {};
    std::wstring w(bytes / sizeof(wchar_t) + 1, L'\0');
    if (msg(reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(id)), kQueryDeviceInterface,
            reinterpret_cast<DWORD_PTR>(w.data()), bytes) != MMSYSERR_NOERROR)
        return {};
    return narrow(w.c_str());
}

inline MMRESULT outMessage(HANDLE h, UINT m, DWORD_PTR a, DWORD_PTR b) {
    return ::midiOutMessage(static_cast<HMIDIOUT>(h), m, a, b);
}
inline MMRESULT inMessage(HANDLE h, UINT m, DWORD_PTR a, DWORD_PTR b) {
    return ::midiInMessage(static_cast<HMIDIIN>(h), m, a, b);
}

// Destinations (expandeurs, grooveboxes, synthés logiciels du système, port loopMIDI).
inline std::vector<Device> outputs() {
    std::vector<Device> out;
    const UINT n = ::midiOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIOUTCAPSW caps{};
        if (::midiOutGetDevCapsW(i, &caps, sizeof caps) != MMSYSERR_NOERROR) continue;
        std::string name = narrow(caps.szPname);
        if (name.empty()) continue;
        out.push_back({{std::move(name), interfaceId(outMessage, i)}, i});
    }
    return out;
}

// Sources (claviers maîtres, boîtes à rythmes, port loopMIDI).
inline std::vector<Device> inputs() {
    std::vector<Device> out;
    const UINT n = ::midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps{};
        if (::midiInGetDevCapsW(i, &caps, sizeof caps) != MMSYSERR_NOERROR) continue;
        std::string name = narrow(caps.szPname);
        if (name.empty()) continue;
        out.push_back({{std::move(name), interfaceId(inMessage, i)}, i});
    }
    return out;
}

// L'index d'un appareil apparié dans la liste ci-dessus, vérifié par (identifiant,
// nom) : la liste a pu changer entre l'appariement et l'ouverture (débranchement à
// chaud), et ouvrir « l'appareil numéro 2 » d'une liste périmée piloterait le
// mauvais. -1 si le point de terminaison a disparu.
inline int reFind(const std::vector<Device>& devs, const Endpoint& want, int hint) {
    if (hint >= 0 && hint < int(devs.size()) && devs[std::size_t(hint)].ep.name == want.name
        && devs[std::size_t(hint)].ep.uid == want.uid) return hint;
    for (std::size_t i = 0; i < devs.size(); ++i)
        if (devs[i].ep.name == want.name && devs[i].ep.uid == want.uid) return int(i);
    return -1;
}

// Message d'erreur LISIBLE. MMSYSERR_ALLOCATED mérite ses propres mots : sous
// Windows un port MIDI est EXCLUSIF (ALSA et CoreMIDI, eux, multiplexent), et
// « erreur 4 » n'aiderait personne à comprendre qu'il faut fermer son DAW.
inline const char* errorText(MMRESULT r) {
    switch (r) {
    case MMSYSERR_NOERROR:     return "ok";
    case MMSYSERR_ALLOCATED:   return "device busy - another application holds it";
    case MMSYSERR_BADDEVICEID: return "no such device (unplugged?)";
    case MMSYSERR_NOMEM:       return "out of memory";
    case MMSYSERR_INVALHANDLE: return "invalid handle";
    case MMSYSERR_NODRIVER:    return "no driver";
    default:                   return "winmm error";
    }
}

} // namespace neost::midi::winmm

#endif // NEOST_MIDI_WINMM
