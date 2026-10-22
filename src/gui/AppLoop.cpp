// =============================================================================
//  AppLoop.cpp — la boucle du frontend, et l'arrêt.
//
//  Un tour = entrées hôte → trames émulées dues (rattrapage) → dessin → requêtes
//  posées par les menus → présentation → sommeil jusqu'à l'échéance suivante.
//
//  Découpage (A9) : la vingtaine de variables que les phases se partagent est
//  nommée dans `struct Frame` (un par tour), et chaque phase est une fonction
//  statique `(App& A, Frame& F)`. Déplacements MÉCANIQUES : chaque phase s'ouvre
//  sur des alias (`Machine& machine = *A.machine;`, `int& fbw = F.fbw;`…) et son
//  corps est celui de l'ancienne boucle d'un seul tenant, au caractère près
//  (indentation exceptée). Les `static` de fonction ont suivi la seule phase qui
//  les utilise ; le `static ConfigUi`, partagé, reste dans prepareConfigUi et
//  Frame n'en porte que l'adresse.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Pacing.hpp"
#include "gui/GlHeaders.hpp"   // GLFW + GL, l'inclusion au même endroit pour tous
#include "gui/CrtEffectStack.h"   // passe d'effets CRT (opt-in, façade moniteur)
#include "core/Symbols.hpp"       // table de symboles du débogueur (noms ↔ adresses)
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <thread>
#include <string>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#if defined(_WIN32)
// Pas pour une API Windows (la résolution du chemin exécutable vit dans AppInit) :
// pour la GARDE. Si quoi que ce soit tire windows.h ici, ses macros min()/max()
// casseraient tous les std::min / std::max de la boucle.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "core/Machine.hpp"
#include "net/NetBackend.hpp"
#include "net/SlirpBackend.hpp"
#ifdef NEOST_WITH_NET
#include "net/HayesModem.hpp"
#endif
#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "io/JoystickInput.hpp"
#include "io/MediaScan.hpp"
#include "io/DongleTable.hpp"
#include "core/Framing.hpp"
#include "gui/App.hpp"      // état du frontend (ex-globaux g_*)
#include "gui/ConfigWindow.hpp"   // fenêtre Configuration + fenêtre Disquettes
#include "gui/CrtUi.hpp"          // presets et réglages des effets CRT
#include "gui/DebugWindows.hpp"   // hexa, CPU, joystick, débogueur
#include "gui/DockLayout.hpp"     // ancrage + taille de la fenêtre hôte
#include "gui/InputCallbacks.hpp" // callbacks GLFW clavier/souris
#include "gui/JoyMap.hpp"         // manettes hôte → ports ST, par GUID
#include "gui/KioskMenu.hpp"      // menu plein écran de la borne
#include "gui/StScreenView.hpp"   // texture de l'écran ST + cadrages borne/bureau
#include "gui/AppConfig.hpp"
#include "util/MouseScale.hpp"
#include "gui/StKeys.hpp"   // neost.cfg : structure, analyse, écriture, profils

namespace fs = std::filesystem;
// Configuration : extraite dans gui/AppConfig (logique pure, donc testable).
// Importée sans qualifier pour que les sites d'appel restent inchangés.
using namespace neost::appconfig;

#if defined(NEOST_WITH_IMGUI)
#include "imgui.h"
#include "imgui_internal.h"   // gestionnaire de réglages personnalisé (ImGuiSettingsHandler)
#include "imgui_impl_glfw.h"
#include "gui/KeyboardWindow.hpp"
#include "audio/MidiDeviceProfiles.hpp"
#include "audio/MidiEndpoint.hpp"
#include "audio/MidiInHost.hpp"
#include "audio/MidiOutHost.hpp"
#include "audio/Mt32Synth.hpp"
#include "audio/GmSynth.hpp"
#include "imgui_impl_opengl2.h"
#include "gui/UiCommon.hpp"    // pictogrammes Font Awesome + IconButton
#include "gui/MediaPages.hpp"  // pages Disquettes / Cartouche / Disque dur / Réseau
#endif

// =============================================================================
//  Frame — l'état d'UN tour de boucle.
//
//  Même geste que `struct App` (gui/App.hpp), à l'échelle d'une itération : les
//  variables que les phases du tour se passent (géométrie du framebuffer, cadrage
//  du zoom, requêtes posées par les menus et consommées en fin de tour). Un `Frame`
//  neuf est construit à CHAQUE tour, exactement comme l'étaient ces locales : rien
//  ne survit d'un tour à l'autre (ce qui doit survivre vit dans App, ou dans les
//  `static` de fonction des phases).
//  Chaque phase s'ouvre sur des alias (`int& fbw = F.fbw;`…) pour que son corps
//  soit resté, au caractère près, celui de l'ancienne boucle.
// =============================================================================
struct Frame {
    // Framebuffer hôte, et région de contenu / cadrage du zoom adaptatif.
    int fbw = 0, fbh = 0;
    int cTop = 0, cH = 0, cW = 0;
    int kTop = 0, kH = 0, kW = 0;
    // Requêtes appliquées en fin de tour, hors rendu ImGui.
    bool reqReset = false, reqHardReset = false, reqRebuild = false;
    int  reqMonitor = -1;
#if defined(NEOST_WITH_IMGUI)
    ConfigUi* cfgUi = nullptr;   // le `static ConfigUi` de prepareConfigUi (vit HORS de la trame)
    std::string reqMount, reqMountB; bool reqEject = false, reqEjectB = false;
    std::string reqMountCart; bool reqEjectCart = false;
    std::string reqMountGemdos, reqMountAcsi; bool reqEjectGemdos = false, reqEjectAcsi = false;
    // Chrome du bureau : posés par une barre, lus par la suivante.
    bool  color = false;
    float menuH = 0.0f, toolPadY = 0.0f, toolH = 0.0f;
#endif
};

// Entrées (1/5) : bascules demandées par les callbacks (reprise, plein écran,
// capture souris, GUI ⇄ kiosk), appliquées en tête de tour.
static void applyHostRequests(App& A) {
    using clock = std::chrono::steady_clock;
    Machine& machine = *A.machine;
    GLFWwindow* const window = A.window;
    auto& emuNext = A.emuNext;
    double& lastMx = A.lastMx;
    double& lastMy = A.lastMy;
    if (A.resumeAtariReq) {
        A.resumeAtariReq = false;
        keyboardWindowReleaseAll(machine.ikbd);
        A.kioskDiskMenu = false;
        A.kioskPage = KIOSK_PAGE_LIST;
        A.kioskZone = KIOSK_ZONE_LIST;
        A.desktopPointer.reset();
        machine.ikbd.mouseEvent(0, 0, false, false);
        glfwGetCursorPos(window, &lastMx, &lastMy);
        emuNext = clock::now();
    }

    if (A.fullscreenToggleReq) {
        A.fullscreenToggleReq = false;
        A.mouseCaptureToggleReq = false;
        A.toggleFullscreen();
    }

    // Le callback ne touche pas aux coordonnées locales de cette boucle : il
    // pose une requête, puis la bascule est faite ici avant de lire un delta.
    if (A.mouseCaptureToggleReq) {
        A.mouseCaptureToggleReq = false;
        A.mouseCaptured = !A.mouseCaptured;
        A.desktopPointer.reset();
        machine.ikbd.mouseEvent(0, 0, false, false);
        glfwSetInputMode(window, GLFW_CURSOR,
                         A.mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (A.mouseCaptured) {
            glfwGetCursorPos(window, &lastMx, &lastMy);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        else if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }

    // Bascule GUI ⇄ kiosk demandée (Ctrl+Alt+F / menus) : appliquée ICI, en tête de tour,
    // donc entre deux trames émulées — la seule fenêtre où l'instantané se recharge.
    if (A.kioskSwitchReq) {
        const bool on = (A.kioskSwitchReq == 1);
        A.kioskSwitchReq = 0;
        if (A.fullscreen) A.toggleFullscreen();
        A.switchKioskMode(on);
    }
}

// Entrées (2/5) : souris hôte → IKBD (pointeur bureau, ou capture relative).
static void feedMouse(App& A) {
    Machine& machine = *A.machine;
    GLFWwindow* const window = A.window;
    double& lastMx = A.lastMx;
    double& lastMy = A.lastMy;
    if (A.desktopMouse && !A.mouseCaptured && !A.kiosk) {
        auto& pointer = A.desktopPointer;
        if (!mouseReachesSt() || !glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
            if (pointer.leftHeld || pointer.rightHeld || !pointer.clicks.empty())
                machine.ikbd.mouseEvent(0, 0, false, false);
            pointer.reset();
        } else if (!pointer.clicks.empty()) {
            const auto action = pointer.clicks.front();
            if (machine.desktopMouseMove(action.x, action.y, action.left, action.right))
                pointer.clicks.pop_front();
        } else if (pointer.hovered || pointer.leftHeld || pointer.rightHeld) {
            double mx, my; glfwGetCursorPos(window, &mx, &my);
            int x, y;
            if (pointer.point(mx, my, x, y, pointer.leftHeld || pointer.rightHeld))
                machine.desktopMouseMove(x, y, pointer.leftHeld, pointer.rightHeld);
        }
    }

    if (A.mouseCaptured) {                  // mouvement relatif → paquet IKBD (boutons inclus)
        double mx, my; glfwGetCursorPos(window, &mx, &my);
        // Sensibilité (A.mouseSpeed, page Input) : le delta HÔTE est mis à l'échelle
        // AVANT d'atteindre l'IKBD. Le brut est consommé en entier et c'est le delta
        // MIS À L'ÉCHELLE qui garde son reste fractionnaire — sinon un facteur < 1
        // perdrait tous les petits mouvements et la souris ST ne bougerait plus du
        // tout sur un déplacement lent.
        const int dx = neost::mousescale::step(mx - lastMx, A.mouseSpeed, A.mouseAccX);
        const int dy = neost::mousescale::step(my - lastMy, A.mouseSpeed, A.mouseAccY);
        lastMx = mx; lastMy = my;             // le delta HÔTE est consommé en entier
        if (dx || dy) {
            // Souris débranchée du ST (joystick sur le port 0, ou overlay borne
            // ouvert) : on CONSOMME quand même le delta, sinon il s'accumule et
            // part en un saut géant au retour.
            if (mouseReachesSt()) {
                const bool l = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
                const bool r = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                if (A.dbgMouse) std::fprintf(stderr, "[mouse] move dx=%d dy=%d L=%d R=%d\n", dx, dy, l, r);
                machine.ikbd.mouseEvent(dx * MOUSE_X_SIGN, dy * MOUSE_Y_SIGN, l, r);
            }
        }
    }
}

// Entrées (3/5) : raccourcis scrutés (sortie kiosk, F10, F11).
static void pollHotkeys(App& A) {
    Config& cfg = A.cfg;
    GLFWwindow* const window = A.window;
    // Sortie KIOSK. Deux moyens, toujours disponibles (sans menu ni bordure) :
    //  · Alt+F4 : le classique, sortie IMMÉDIATE. Géré explicitement ici car en
    //    plein écran EXCLUSIF le gestionnaire de fenêtres ne relaie pas toujours
    //    l'événement « close » à GLFW.
    //  · Ctrl+Shift+Q maintenu ~0,7 s : chord discret (évite les sorties accidentelles).
    // Le WM (Alt+F4 « normal », bouton fermer) reste actif aussi : on ne bloque
    // jamais glfwWindowShouldClose.
    if (A.kiosk) {
        const bool alt = glfwGetKey(window, GLFW_KEY_LEFT_ALT)  == GLFW_PRESS ||
                         glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        if (alt && glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        const bool ctrl  = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS ||
                           glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        const bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS ||
                           glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        const bool q     = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
        static int quitHold = 0;
        quitHold = (ctrl && shift && q) ? quitHold + 1 : 0;
        if (quitHold >= 35) glfwSetWindowShouldClose(window, GLFW_TRUE);   // ~0,7 s @ 50 Hz

        // F10 (front montant) : (dés)active le zoom adaptatif → cadre complet fixe.
        static bool f10Prev = false;
        const bool f10 = glfwGetKey(window, GLFW_KEY_F10) == GLFW_PRESS;
        if (f10 && !f10Prev) {
            A.autoZoom = !A.autoZoom;
            cfg.autoZoom = A.autoZoom;   // sinon un retour au bureau resauverait l'ANCIENNE valeur
            std::fprintf(stderr, "[kiosk] adaptive zoom %s\n", A.autoZoom ? "ON" : "OFF");
        }
        f10Prev = f10;
    }

    // F11 (front montant) : bascule l'émulation joystick au clavier. Pratique
    // surtout sans ImGui (pas de menu) ; mémorisé en config en fin de boucle.
    {
        static bool f11Prev = false;
        const bool f11 = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
        if (f11 && !f11Prev) {
            A.kbdJoy = !A.kbdJoy;   // bascule de session (jamais persistée)
            std::fprintf(stderr, "[joystick] keyboard emulation %s (port %d)\n",
                         A.kbdJoy ? "ON" : "OFF", A.kbdJoyPort);
        }
        f11Prev = f11;
    }
}

// Entrées (4/5) : manettes hôte → ports joystick ST, joypads et paddles STE.
static void feedJoysticks(App& A) {
    Machine& machine = *A.machine;
    GLFWwindow* const window = A.window;
    // Joystick hôte → IKBD (manettes USB + émulation clavier). Scruté chaque
    // trame : l'IKBD répond avec cet état aux interrogations $16 / au report
    // auto $14. L'émulation clavier est inhibée si une saisie ImGui a le focus.
    {
        bool kbd = A.kbdJoy;
#if defined(NEOST_WITH_IMGUI)
        if (!A.mouseCaptured && ImGui::GetIO().WantCaptureKeyboard) kbd = false;
#endif
        uint8_t joy0 = 0, joy1 = 0;
        int8_t joyRoles[GLFW_JOYSTICK_LAST + 1];
        joyResolveRoles(A, joyRoles);   // affectation par GUID (menu kiosk « Joysticks »)
        stjoy::compose(window, kbd, A.kbdJoyPort, A.joyDeadzone, joy0, joy1, joyRoles, A.port0Auto);
        // Port 0 occupé par un joystick (manette affectée, ou clavier qui le vise) :
        // sur un vrai ST il a pris la place de la souris — on la débranche.
        {
            int8_t asg[GLFW_JOYSTICK_LAST + 1];
            stjoy::resolveAssign(joyRoles, asg, A.port0Auto);
            bool occ = kbd && A.kbdJoyPort == 0;
            for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) if (asg[jid] == 0) occ = true;
            A.port0Joystick = occ;
        }
        // Overlay kiosk ouvert : la manette pilote l'overlay → on n'envoie
        // rien au ST (sinon le jeu bougerait pendant la navigation).
        if (A.kioskDiskMenu) { joy0 = 0; joy1 = 0; }
        machine.ikbd.setJoystick(joy0, joy1);
        machine.bus.stePads.setJoystick(joy0, joy1);   // joypads STE ($FF9200/02) — même état
        // Boutons auxiliaires manette → touches ST : X = ESPACE, Y = RETURN
        // (cf. stjoy::readAux — jeux « PRESS SPACE » jouables sans clavier).
        // Make/break IKBD sur les FRONTS ; tout est relâché quand l'overlay
        // kiosk s'ouvre (la manette pilote alors le menu, pas le jeu).
        {
            static uint8_t prevAux = 0;
            uint8_t aux = A.kioskDiskMenu ? 0 : stjoy::composeAux(joyRoles, A.port0Auto);
            const uint8_t delta = aux ^ prevAux;
            if (delta & stjoy::AUX_SPACE)
                machine.ikbd.keyEvent(0x39, aux & stjoy::AUX_SPACE);   // ESPACE
            if (delta & stjoy::AUX_RETURN)
                machine.ikbd.keyEvent(0x1C, aux & stjoy::AUX_RETURN);  // RETURN
            prevAux = aux;
        }
        // Paddles / axes analogiques STE ($FF9211-17) : axes BRUTS de la
        // première manette hôte (stick gauche, sans zone morte — la plage
        // $04-$43 du STE est déjà grossière). Pad A = port « jeux ».
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            GLFWgamepadstate gs;
            if (glfwJoystickPresent(jid) && glfwGetGamepadState(jid, &gs)) {
                machine.bus.stePads.setAnalog(0, gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X],
                                                 gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
                break;
            }
        }
        A.lastJoy0 = joy0; A.lastJoy1 = joy1;   // pour la fenêtre Joystick
        // Diagnostic manette (NEOST_DEBUG_JOY=1) : ~3×/s, état brut des axes.
        if (A.dbgJoy) { static int t = 0; if (++t % 16 == 0) stjoy::debug(window, kbd, A.kbdJoyPort, A.joyDeadzone); }
    }
}

// Entrées (5/5) : IPL réévalué, relâche différée des frappes du menu kiosk.
static void finishInputs(App& A) {
    Machine& machine = *A.machine;
    machine.cpu.updateIpl();               // entrées reçues → réévalue l'IPL

    // Relâche différée de la touche/clic envoyé depuis la page Clavier du menu :
    // l'appui (MAKE) a été posé quand l'utilisateur a validé ; on relâche (BREAK)
    // quelques trames plus tard → frappe brève que le jeu voit passer.
    if (A.kioskInjectHold > 0 && --A.kioskInjectHold == 0) {
        if (A.kioskKeyRelease >= 0) {
            machine.ikbd.keyEvent((uint8_t)A.kioskKeyRelease, false);
            A.kioskKeyRelease = -1;
        }
        if (A.kioskMouseRelL || A.kioskMouseRelR) {
            machine.ikbd.mouseEvent(0, 0, false, false);
            A.kioskMouseRelL = A.kioskMouseRelR = false;
        }
    }
}

// Trames émulées dues (rattrapage temps réel), ou pas-à-pas du débogueur.
static void runDueFrames(App& A) {
    using clock = std::chrono::steady_clock;
    Machine& machine = *A.machine;
    GLFWwindow* const window = A.window;
    MidiOutHost& midiOut = *A.midiOut;
    MidiInHost& midiIn = *A.midiIn;
    Audio& audio = *A.audio;
    auto& hayesModem = A.hayesModem;
    auto& emuNext = A.emuNext;
    // RATTRAPAGE : on émule autant de trames que le temps réel l'exige depuis la
    // dernière itération (pattern émulateur classique). Une itération GUI coûte
    // ce qu'elle coûte (ImGui + GL + granularité de sleep macOS ≈ 22-25 ms,
    // App Nap, fenêtre déplacée…) : si on n'exécutait qu'UNE trame par tour, la
    // boucle plafonnait à ~40 trames/s → temps émulé RALENTI de 20 % et anneau
    // audio affamé (son HACHÉ — le bug « musique lente + hachée »). Ici le temps
    // émulé suit le temps réel quel que soit le débit du GUI : tour lent → 2
    // trames émulées (l'affichage en saute une, inaperçu), tour rapide → 0 ou 1.
    // `emuNext` = échéance réelle de la PROCHAINE trame émulée ; chaque trame la
    // repousse de SA durée émulée (géométrie 50/60/71 Hz). Garde-fou : après une
    // longue pause (drag de fenêtre…), on abandonne le retard au-delà de 4 trames
    // au lieu de spiraler.
    // PAUSE KIOSK : menu ouvert (hors page Clavier, qui doit laisser tourner le
    // jeu pour qu'il reçoive les touches envoyées) → on gèle l'émulation. À la
    // reprise on recale l'échéance sur maintenant (aucun rattrapage en rafale).
    const bool kioskPaused = A.kiosk && A.kioskDiskMenu && A.kioskPage != KIOSK_PAGE_KEYS;
    if (kioskPaused || A.dbgPaused) {
        emuNext = clock::now();
        // Débogueur en pause : pas-à-pas TRAME (avance une trame puis reste pausé).
        // clearBreakpointHit arme le skip-once → l'instruction du breakpoint passe.
        if (A.dbgPaused && A.dbgStepFrame) {
            A.dbgStepFrame = false;
            machine.cpu.clearBreakpointHit();
            machine.runFrame();
            audio.produceFrame(machine.frameCycles(), machine.sched.now());
        }
        // Pas-à-pas INSTRUCTION : une seule instruction, ordonnanceur en lockstep
        // (pas de produceFrame — trop court, le son reste muet en pas-à-pas).
        // Les écritures PSG/DMA du pas restent horodatées sur des trames périmées :
        // on les jette (clearEvents re-synchronise aussi le miroir DMA), sinon la
        // reprise les rejouait toutes en rafale écrêtée sur UNE trame.
        if (A.dbgPaused && A.dbgStepInstr) {
            A.dbgStepInstr = false;
            machine.stepInstruction();
            machine.psg.clearEvents();
            machine.dmasnd.clearEvents();
        }
    } else {
        // 6 trames max ≈ 120 ms de retard résorbable d'un coup : un stall GUI
        // ponctuel (drag de fenêtre, rafale disque) plus court que ça se rattrape
        // SANS trou audible (le coussin de l'anneau fait ~85 ms).
        int ran = 0;
        while (clock::now() >= emuNext && ran < 6) {
#ifdef NEOST_WITH_NET
            if (hayesModem) hayesModem->poll();   // TCP entrant → file RX du MFP
#endif
            if (machine.ne2000.enabled()) machine.ne2000.poll();   // trames RX → anneau
            if (machine.isp1160.enabled()) machine.isp1160.poll(); // trame USB (ATL → done)
            // Sortie MIDI horodatée : cette trame DOIT commencer à emuNext (temps réel).
            if (midiOut.anyOpen()) midiOut.anchor(machine.sched.now(), emuNext);
            // Injections datées (harnais A8) — appliquées APRÈS le polling manettes
            // du tour (qui écrase le port 1) et AVANT l'émulation de la trame.
            for (const auto& [jf, jv] : A.joyAt)
                if (A.emuFrame >= jf) {
                    machine.ikbd.setJoystick(0, jv);
                    machine.bus.stePads.setJoystick(0, jv);
                }
            // Souris scriptée — port à l'identique du headless (mêmes tokens,
            // même relâche implicite du clic au token suivant).
            for (const auto& [mf, mscript] : A.mouseAt) {
                if (A.emuFrame < mf) continue;
                const long idx = A.emuFrame - mf;
                if (idx < (long)mscript.size()) {
                    static bool mL = false, mR = false;
                    const char t = mscript[idx];
                    int dx = 0, dy = 0; bool l = false, r = false;
                    switch (t) {
                        case 'L': dx = -8; break;
                        case 'R': dx =  8; break;
                        case 'U': dy = -8; break;
                        case 'D': dy =  8; break;
                        case '1': l = true; mL = true; break;
                        case '2': r = true; mR = true; break;
                        case '3': l = r = true; mL = mR = true; break;
                        default: break;                       // '.' = idle
                    }
                    if (t != '1' && t != '3' && mL) { l = false; mL = false; }
                    if (t != '2' && t != '3' && mR) { r = false; mR = false; }
                    machine.ikbd.mouseEvent(dx, dy, l, r);
                }
            }
            const int stride = (A.keyHold + 2 > 4) ? A.keyHold + 2 : 4;
            for (const auto& [sf, sl] : A.scanAt) {
                if (A.emuFrame < sf) continue;
                const long rel = A.emuFrame - sf;
                const long idx = rel / stride;
                if (idx < (long)sl.size()) {
                    const int ph = int(rel % stride);
                    if      (ph == 0)         machine.ikbd.keyEvent(sl[idx], true);
                    else if (ph == A.keyHold) machine.ikbd.keyEvent(sl[idx], false);
                }
            }
            machine.runFrame();                          // une trame (timing + décodage)
            audio.produceFrame(machine.frameCycles(), machine.sched.now());   // son de la trame → anneau (push)
            ++A.emuFrame;
            // --run-frames : sortie AUTOMATIQUE après N trames émulées (chantier A8).
            // Le décompte est fait ICI, au site d'émulation nominal — le pas-à-pas du
            // débogueur (sites runFrame du mode pausé) ne compte pas, c'est voulu :
            // l'option sert un harnais, pas une session de débogage.
            if (A.runFrames > 0 && --A.runFrames == 0) {
                // Bilan de l'entrée MIDI hôte, comme le headless. Le GUI est le SEUL
                // à tourner en temps réel : le headless émule ~19x plus vite, donc
                // une source MIDI réelle y est toujours le facteur limitant et n'y
                // mesure jamais le débit de l'ACIA. C'est donc ici, et nulle part
                // ailleurs, qu'on peut vérifier les 3125 o/s du câble.
                if (midiOut.anyOpen())
                    std::fprintf(stderr, "[main] MIDI OUT: lead %d ms, %llu byte(s) sent late\n",
                                 midiOut.leadMs(), (unsigned long long)midiOut.lateBytes());
                if (midiIn.isOpen())
                    std::fprintf(stderr, "[main] MIDI IN: %llu bytes into the ACIA from \"%s\""
                                 " (%llu dropped)\n",
                                 (unsigned long long)midiIn.delivered(),
                                 midiIn.isOpen() ? midiIn.openNames().front().c_str() : "",
                                 (unsigned long long)midiIn.dropped());
                if (!A.shotPath.empty()) {
                    const uint32_t* px = machine.shifter.pixels();
                    const int w = machine.shifter.width(), h = machine.shifter.height();
                    std::FILE* f = std::fopen(A.shotPath.c_str(), "wb");
                    bool ok = f != nullptr;
                    if (f) {
                        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
                        for (int k = 0; k < w * h && ok; ++k) {
                            const uint32_t c = px[k];               // ARGB8888
                            const unsigned char rgb[3] = {
                                (unsigned char)((c >> 16) & 0xFF),
                                (unsigned char)((c >> 8)  & 0xFF),
                                (unsigned char)( c        & 0xFF) };
                            ok = std::fwrite(rgb, 1, 3, f) == 3;
                        }
                        if (std::fclose(f) != 0) ok = false;   // disque plein : échec au flush
                    }
                    std::fprintf(stderr, ok ? "[main] shot -> %s (%dx%d)\n"
                                            : "[main] FAILED shot %s (%dx%d)\n",
                                 A.shotPath.c_str(), w, h);
                }
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            emuNext += std::chrono::nanoseconds(
                neost::pacing::frameNanos(machine.frameCycles()));   // A28 : Pacing.hpp
            ++ran;
            if (machine.cpu.breakpointHit()) { A.dbgPaused = true; break; }   // débogueur : auto-pause
        }
        if (ran == 6 && clock::now() > emuNext) emuNext = clock::now();  // pause longue : resync
    }
}

// Save-state rapide F5/F7, à la frontière de trame.
static void handleQuickState(App& A) {
    Machine& machine = *A.machine;
    const std::string& exeDir = A.exeDir;
    // Save-state rapide (F5 sauver / F7 charger) — slot fichier unique neost.state, à
    // la frontière de trame. En kiosk la config est figée mais l'état de jeu, lui, se
    // sauve/charge (ce n'est pas la config). Fronts montants.
    {
        // Demandes latchées dans onKey (cf. le commentaire Alt+Entrée : la scrutation
        // glfwGetKey ratait un appui bref posé/relâché entre deux tours).
        const std::string statePath = exeDir + "/../neost.state";
        if (A.saveStateReq) {
            A.saveStateReq = false;
            const bool ok = machine.saveStateFile(statePath);
            A.stateMsg = ok ? "\xef\x83\x87 State saved (F5)" : "Save failed";
            A.stateMsgFrames = 120;
            std::fprintf(stderr, "[state] save %s → %s\n", ok ? "OK" : "FAILED", statePath.c_str());
        }
        if (A.loadStateReq) {
            A.loadStateReq = false;
            const bool ok = machine.loadStateFile(statePath);
            A.stateMsg = ok ? "\xef\x80\x9e State restored (F7)" : "No state / failed";
            A.stateMsgFrames = 120;
            std::fprintf(stderr, "[state] load %s ← %s\n", ok ? "OK" : "FAILED", statePath.c_str());
        }
    }
}

// Dessin : texture de l'écran ST, effacement, région de contenu du zoom.
static void beginDraw(App& A, Frame& F) {
    Machine& machine = *A.machine;
    GLFWwindow* const window = A.window;
    GlScreen& screen = *A.screen;
    int& fbw = F.fbw;
    int& fbh = F.fbh;
    int& cTop = F.cTop;
    int& cH = F.cH;
    int& cW = F.cW;
    int& kTop = F.kTop;
    int& kH = F.kH;
    int& kW = F.kW;
    screen.update(machine.shifter.pixels(), machine.shifter.width(), machine.shifter.height());

    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    if (A.kiosk || A.fullscreen) glClearColor(0.f, 0.f, 0.f, 1.f);
    else         glClearColor(0.10f, 0.10f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Région de contenu du zoom adaptatif — commune au kiosk et au bureau.
    // Appelée à CHAQUE trame même zoom coupé : ses latches d'hystérésis sont des
    // statiques de fonction, et les sauter les GÈLERAIT à leur dernière valeur —
    // à la réactivation, un latch resté armé sur une démo overscan afficherait le
    // buffer entier pendant ~30 trames avant de retomber d'un coup.
    stContentRegion(machine, cTop, cH, cW);
    kTop = 0; kH = machine.shifter.height(); kW = machine.shifter.width();
    if (A.autoZoom) { kTop = cTop; kH = cH; kW = cW; }
}

#if defined(NEOST_WITH_IMGUI)
// Dessin : dossiers et profils dans le `static ConfigUi` (un seul pour la vie
// du programme ; Frame n'en porte que l'adresse).
static void prepareConfigUi(App& A, Frame& F) {
    const std::string& exeDir = A.exeDir;
    const std::string& disksDir = A.disksDir;
    const std::string& cartsDir = A.cartsDir;
    const std::string& hdDir = A.hdDir;
    const std::string& gemdosDir = A.gemdosDir;
    const std::string& romsDir = A.romsDir;
    // Vit HORS de la trame : porte les réglages matériels en attente entre deux
    // ouvertures de la fenêtre.
    static ConfigUi cfgUi;
    cfgUi.disksDir = disksDir; cfgUi.cartsDir = cartsDir; cfgUi.romsDir = romsDir;
    cfgUi.hdDir    = hdDir;    cfgUi.gemdosDir = gemdosDir;
    // Résolu à la PREMIÈRE trame, donc après le saveConfig de démarrage : c'est lui
    // qui a tranché où vit neost.cfg, et les profils le suivent (cf. profilesDir).
    static const std::string profDirResolved = profilesDir(exeDir);
    cfgUi.profDir = profDirResolved;        // créé à la 1re sauvegarde de profil
    F.cfgUi = &cfgUi;
}

// Dessin, chrome du bureau : barre de menus.
static void drawMenuBar(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    GLFWwindow* const window = A.window;
    bool& reqReset = F.reqReset;
    bool& reqHardReset = F.reqHardReset;
    int& reqMonitor = F.reqMonitor;
    const bool& color = F.color;
    // --- Menu (haut) -----------------------------------------------------
    // Court par construction : la barre de menus ne contient que ce qu'on fait EN
    // JOUANT. Tout ce qui se RÈGLE vit dans la fenêtre Configuration (F10).
    float menuH = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        menuH = ImGui::GetWindowSize().y;
        if (ImGui::BeginMenu(ICON_FA_MICROCHIP " Machine")) {
            // Fiche de bouclage MIDI OUT→IN : débranchée par défaut (un vrai ST n'a
            // rien de branché ; branchée, Cubase/MROS avec MIDI Thru part en larsen).
            if (ImGui::MenuItem("MIDI loopback cable (OUT" "\xe2\x86\x92" "IN)", nullptr, &cfg.midiLoopback)) {
                machine.midi.setLoopback(cfg.midiLoopback);
                saveConfig(A, exeDir, cfg, &machine);
            }
            // Synthé GM : DLSMusicDevice (macOS) ou TinySoundFont (partout) — la case
            // vit donc sur toutes les plateformes ; le port virtuel suit son backend.
            if (MidiOutHost::synthAvailable() || GmSynth::available()) {
                if (ImGui::MenuItem("MIDI OUT " "\xe2\x86\x92" " built-in General MIDI synth", nullptr, &cfg.midiOutGm)) {
                    A.midiOutApply(); saveConfig(A, exeDir, cfg, &machine);
                }
            }
            if (MidiOutHost::portAvailable()) {
                // « CoreMIDI » ou « ALSA » selon l'hôte — le libellé en dur mentait sous Linux.
                const std::string portLabel = std::string("MIDI OUT " "\xe2\x86\x92" " ")
                    + MidiOutHost::portKindName() + " port \"NeoST MIDI OUT\"";
                if (ImGui::MenuItem(portLabel.c_str(), nullptr, &cfg.midiOutPort)) {
                    A.midiOutApply(); saveConfig(A, exeDir, cfg, &machine);
                }
            }
            if (Mt32Synth::available()) {
                if (ImGui::MenuItem("MIDI OUT " "\xe2\x86\x92" " Roland MT-32 / CM-32L (Munt, roms/mt32/)", nullptr, &cfg.midiOutMt32)) {
                    A.midiOutApply(); saveConfig(A, exeDir, cfg, &machine);
                }
            } else {
                ImGui::MenuItem("MIDI OUT " "\xe2\x86\x92" " Roland MT-32 (needs libmt32emu at build)", nullptr, false, false);
            }
            if (ImGui::MenuItem(ICON_FA_REDO " Reset"))            reqReset = true;
            if (ImGui::MenuItem(ICON_FA_POWER_OFF " Hard Reset"))  reqHardReset = true;
            ImGui::Separator();
            ImGui::MenuItem(ICON_FA_SAVE " Floppies…", nullptr, &A.showFloppy);
            ImGui::MenuItem(ICON_FA_COG " Configuration…", nullptr, &A.showCfg);
            // Raccourci vers la page des profils : sans lui, « enregistrer mes
            // réglages » n'était visible qu'en descendant la colonne de la fenêtre.
            if (ImGui::MenuItem(ICON_FA_STAR " Settings profiles…")) {
                A.showCfg = true; A.cfgPage = kCfgProfiles;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_SAVE " Save state", "F5")) {
                const bool ok = machine.saveStateFile(exeDir + "/../neost.state");
                A.stateMsg = ok ? "\xef\x83\x87 State saved" : "Save failed";
                A.stateMsgFrames = 120;
            }
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Load state", "F7")) {
                const bool ok = machine.loadStateFile(exeDir + "/../neost.state");
                A.stateMsg = ok ? "\xef\x80\x9e State restored" : "No state / failed";
                A.stateMsgFrames = 120;
            }
            ImGui::Separator();
            // Bascule borne : plein écran sans chrome, config figée, navigation à
            // la manette. La machine traverse la bascule par instantané → le jeu
            // en cours continue. Alt+Entrée revient au bureau.
            if (ImGui::MenuItem(ICON_FA_DESKTOP " Kiosk mode", "Ctrl+Alt+F"))
                A.kioskSwitchReq = 1;
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_SIGN_OUT_ALT " Quit")) glfwSetWindowShouldClose(window, 1);
            ImGui::EndMenu();
        }
        // Affichage : ce qui change la façon de REGARDER, pas la machine émulée.
        if (ImGui::BeginMenu(ICON_FA_DESKTOP " Display")) {
            if (ImGui::MenuItem(ICON_FA_PALETTE " Color (low res)", nullptr,  color)) reqMonitor = 1;
            if (ImGui::MenuItem(ICON_FA_ADJUST " Mono (high res)",     nullptr, !color)) reqMonitor = 0;
            ImGui::Separator();
            // Même cadrage adaptatif que la borne : l'écran cale sa zone de contenu
            // sur la hauteur disponible, les bordures inutilisées sortent du cadre,
            // et une ouverture de bordure (démo overscan) rend le cadre entier.
            // Décoché = cadre complet fixe. En kiosk, F10 bascule la même chose.
            if (ImGui::MenuItem(ICON_FA_EXPAND " Auto zoom (adaptive framing)",
                                nullptr, &A.autoZoom)) {
                cfg.autoZoom = A.autoZoom; saveConfig(A, exeDir, cfg, &machine);
            }
            ImGui::MenuItem(ICON_FA_DESKTOP " CRT effects (window)", nullptr, &A.showCrt);
#ifdef IMGUI_HAS_DOCK
            ImGui::Separator();
            // Mode ancré : les fenêtres deviennent des onglets d'une disposition
            // persistante (imgui.ini). Décoché → ImGui DÉTRUIT ses nœuds (elles
            // redeviennent flottantes) ; recoché → on resème la disposition par
            // défaut, la personnalisation précédente est perdue.
            if (ImGui::MenuItem(ICON_FA_CLONE " Docked mode", nullptr, &A.dockOn)) {
                ImGuiIO& dio = ImGui::GetIO();
                if (A.dockOn) { dio.ConfigFlags |=  ImGuiConfigFlags_DockingEnable; A.dockReset = true; }
                else            dio.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
                cfg.dock = A.dockOn; saveConfig(A, exeDir, cfg, &machine);
            }
            if (ImGui::MenuItem(ICON_FA_REDO " Default layout", nullptr, false, A.dockOn))
                A.dockReset = true;
#endif
            ImGui::EndMenu();
        }
        // Fenêtres indépendantes et outils d'inspection.
        if (ImGui::BeginMenu(ICON_FA_CLONE " Windows")) {
            ImGui::MenuItem(ICON_FA_SAVE " Floppies",       nullptr, &A.showFloppy);
            ImGui::MenuItem(ICON_FA_MEMORY " Memory (hex)", nullptr, &A.showHex);
            ImGui::MenuItem(ICON_FA_MICROCHIP " CPU 68000",  nullptr, &A.showCpu);
            ImGui::MenuItem(ICON_FA_GAMEPAD " Joystick",     nullptr, &A.showJoy);
            ImGui::MenuItem(ICON_FA_KEYBOARD " Keyboard",     nullptr, &A.showKbd);
            ImGui::MenuItem(ICON_FA_BUG " Debugger",        nullptr, &A.showDbg);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            // Les raccourcis étaient jusqu'ici invisibles (F5/F7/F11/F12 ne
            // figuraient nulle part sauf dans le code).
            ImGui::TextDisabled("Keyboard shortcuts");
            ImGui::Separator();
            struct Key { const char* k; const char* w; };
            static const Key keys[] = {
                { "F5",  "save the machine state" },
                { "F7",  "reload the state" },
                { "Alt+Enter", "resume Atari from kiosk menu; otherwise toggle full screen" },
                { "Ctrl+Alt+F", "toggle kiosk mode" },
                { "F11", "keyboard joystick emulation" },
                // F12 n'a JAMAIS eu de handler bureau (historique vérifié) : il
                // n'existe qu'en kiosque — documenter la réalité, pas l'intention.
                { "F12", "kiosk: keyboard & mouse overlay" },
                { "MMB", "capture/release the mouse (middle button)" },
                { "Ctrl+Alt+G", "capture/release the mouse (keyboard fallback)" },
            };
            for (const auto& k : keys) ImGui::BulletText("%-6s %s", k.k, k.w);
            ImGui::Separator();
            ImGui::TextDisabled("NeoST " NEOST_VERSION " — Atari ST emulator");
            ImGui::TextDisabled("Drag and drop: folder → C:, image → drive A,");
            ImGui::TextDisabled("hard disk image → ACSI, TOS → ROM.");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    F.menuH = menuH;
}

// Dessin, chrome du bureau : barre de boutons.
static void drawToolbar(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    Audio& audio = *A.audio;
    bool& reqReset = F.reqReset;
    bool& reqHardReset = F.reqHardReset;
    int& reqMonitor = F.reqMonitor;
    const bool& color = F.color;
    // --- Barre de boutons (sous le menu) ---------------------------------
    // Barre latérale de VIEWPORT (et non fenêtre positionnée à la main, depuis
    // l'arrivée de l'ancrage) : BeginViewportSideBar RÉSERVE sa hauteur dans la
    // zone de travail, donc le dockspace posé juste après commence dessous —
    // aucun décalage codé en dur, et la réservation suit la hauteur réelle.
    // Elle ne porte plus que des VERBES : les bascules de fenêtres faisaient
    // doublon avec le menu Fenêtres.
    const float toolPadY = ImGui::GetStyle().WindowPadding.y;
    const float toolH    = ImGui::GetFrameHeight() + toolPadY * 2.0f;
    ImGui::BeginViewportSideBar("##toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, toolH,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (IconButton(ICON_FA_SAVE, "Floppies")) A.showFloppy = !A.showFloppy;
    ImGui::SameLine();
    if (IconButton(ICON_FA_COG, "Configuration")) A.showCfg = !A.showCfg;
    ImGui::SameLine();
    // Accès direct à la borne : le menu Machine et Ctrl+Alt+F restent disponibles, mais
    // la bascule principale ne doit pas être enfouie dans un sous-menu.
    if (IconButton(ICON_FA_DESKTOP, "Switch to kiosk mode (Ctrl+Alt+F)"))
        A.kioskSwitchReq = 1;
    ImGui::SameLine();
    if (IconButton(ICON_FA_REDO, "Reset")) reqReset = true;
    ImGui::SameLine();
    // Reset à froid : efface la ST-RAM → EmuTOS/TOS refait un boot complet.
    if (IconButton(ICON_FA_POWER_OFF, "Hard Reset")) reqHardReset = true;
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    if (IconButton(color ? ICON_FA_ADJUST : ICON_FA_PALETTE, color ? "Switch to Mono" : "Switch to Color"))
        reqMonitor = color ? 0 : 1;
    // Volume : le seul réglage qu'on touche EN JOUANT, donc il reste ici (le
    // reste du son est dans la page Son).
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    {
        static float volBeforeMute = 1.0f;         // niveau restauré au dé-mute
        const float  vol   = audio.masterVolume();
        const bool   muted = vol <= 0.0f;
        const char*  vicon = muted      ? ICON_FA_VOLUME_MUTE
                           : vol < 0.5f ? ICON_FA_VOLUME_DOWN : ICON_FA_VOLUME_UP;
        if (IconButton(vicon, muted ? "Unmute" : "Mute")) {
            if (muted) audio.setMasterVolume(volBeforeMute > 0.0f ? volBeforeMute : 1.0f);
            else     { volBeforeMute = vol; audio.setMasterVolume(0.0f); }
            cfg.volume = audio.masterVolume(); saveConfig(A, exeDir, cfg, &machine);
        }
        ImGui::SameLine();
        int pct = int(vol * 100.0f + 0.5f);
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::SliderInt("##volume", &pct, 0, 100, "%d %%"))
            audio.setMasterVolume(float(pct) / 100.0f);
        if (ImGui::IsItemDeactivatedAfterEdit()) {   // fin de glissé → persiste
            cfg.volume = audio.masterVolume();
            saveConfig(A, exeDir, cfg, &machine);
        }
    }
    ImGui::End();
    F.toolPadY = toolPadY; F.toolH = toolH;
}

// Dessin, chrome du bureau : barre d'état.
static void drawStatusBar(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const float& toolPadY = F.toolPadY;
    // --- Barre d'état (bas) : l'identité de la machine, en permanence -----
    // C'est ce qui manquait le plus. Deux « bugs » sur trois venaient d'une
    // configuration INVISIBLE : une ROM « us » (60 Hz NTSC) qui déchire une démo
    // calculée pour le 50 Hz, ou 512 Ko là où le jeu en veut 1 Mo. Rien dans
    // l'interface ne le disait. Chaque segment est cliquable et ouvre SA page.
    const float statH = ImGui::GetFrameHeight() + toolPadY * 2.0f;
    ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down, statH,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    {
        bool first = true;
        // page == -1 ouvre la fenêtre Floppies ; les autres valeurs ouvrent
        // la page correspondante de Configuration.
        auto seg = [&](const std::string& text, int page, const char* tip, bool warn = false) {
            if (!first) { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); }
            first = false;
            if (warn) ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f), "%s", text.c_str());
            else      ImGui::TextUnformatted(text.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n(click: open %s)", tip,
                                  page < 0 ? "Floppies" : "the configuration");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (page < 0) A.showFloppy = true;
                    else { A.showCfg = true; A.cfgPage = page; }
                }
            }
        };
        auto shortName = [](const std::string& p) {
            return p.empty() ? std::string("—") : fs::path(p).filename().string();
        };
        // Modèle + RAM : les deux réglages qui décident si un jeu démarre.
        const std::string mdl = cfg.machine == "megaste" ? "Mega STE"
                              : cfg.machine == "ste"     ? "STE"
                              : cfg.machine == "megast"  ? "Mega ST" : "ST";
        // RAM : la taille RÉELLEMENT installée (pas une liste de noms de cfg.mem,
        // qui retombait sur « 4 MB » pour toute valeur inconnue — 2176k, 2560k).
        const std::string ram = ramDisplayName(machine.bus.ram.size());
        seg(mdl, kCfgMachine, "Emulated machine model");
        seg(ram, kCfgMem,     "Installed ST-RAM");
        seg(fs::path(cfg.rom).stem().string(), kCfgRom, "TOS image in ROM");
        // Le balayage est DÉDUIT de la ROM ; on le signale en orange en 60 Hz, la
        // configuration qui déchire les démos européennes.
        const int hz = machine.shifter.refreshHz();
        char hzbuf[32];
        // 71 Hz = mono haute résolution, ni NTSC ni un problème : l'avertissement
        // orange ne vise que le 60 Hz (déchirement des démos européennes).
        std::snprintf(hzbuf, sizeof hzbuf, "%d Hz %s",
                      hz, hz == 60 ? "NTSC" : hz >= 70 ? "mono" : "PAL");
        seg(hzbuf, kCfgRom, "Scan rate (set by the ROM)", hz == 60);
        seg("A: " + shortName(machine.fdc.mountedPath(0)), -1, "Floppy drive A");
        seg("B: " + shortName(machine.fdc.mountedPath(1)), -1, "Floppy drive B");
        // La barre ne relit pas les tables de partitions à chaque image.
        // La page Disques durs vérifie les chevauchements lorsqu’elle est ouverte.
        if (machine.fdc.acsiActive()) {
            const std::string image = !cfg.acsi.empty() ? cfg.acsi : cfg.sd2;
            seg("ACSI: " + shortName(image), kCfgHd, "ACSI hard disk");
        }
        if (machine.gemdos.active()) {
            const std::string letters = hardDiskDriveLetters(machine.gemdos.driveMask());
            seg((letters.empty() ? "GEMDOS (no free letters)" : letters) + " " + shortName(cfg.gemdos) + "/",
                kCfgHd, "GEMDOS host folder");
        }
        if (!machine.gemdos.active() && !machine.fdc.acsiActive())
            seg("HD: —", kCfgHd, "Hard disks");
        char fps[32];
        std::snprintf(fps, sizeof fps, "%.1f fps", double(ImGui::GetIO().Framerate));
        seg(fps, kCfgEmul, "Host loop frame rate");
    }
    ImGui::End();
}

// Dessin, chrome du bureau : état partagé par Floppies et Configuration.
static void syncConfigUi(App& A, Frame& F) {
    using clock = std::chrono::steady_clock;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    MidiOutHost& midiOut = *A.midiOut;
    MidiInHost& midiIn = *A.midiIn;
    Mt32Synth& mt32 = *A.mt32;
    GmSynth& gm = *A.gm;
    Audio& audio = *A.audio;
    const bool driveSoundAvail = A.driveSoundAvail;
    bool& driveSoundOn = A.driveSoundOn;
    ConfigUi& cfgUi = *F.cfgUi;
    const bool& color = F.color;
    // État commun aux deux fenêtres. Floppies reste pleinement fonctionnelle
    // lorsque Configuration est fermée.
    cfgUi.cfg     = &cfg;
    cfgUi.machine = &machine;
    cfgUi.color   = color;
    cfgUi.volume  = audio.masterVolume();
    cfgUi.driveSound = driveSoundOn;
    cfgUi.driveSoundAvail = driveSoundAvail;
    cfgUi.curGemdos = cfg.gemdos.empty() ? std::string() : A.resolvePath(cfg.gemdos);
    cfgUi.curAcsi   = cfg.acsi.empty()   ? std::string() : A.resolvePath(cfg.acsi);
    cfgUi.curSd2    = cfg.sd2.empty()    ? std::string() : A.resolvePath(cfg.sd2);
    cfgUi.mt32Status = mt32.isOpen() ? (mt32.model() + " running") : mt32.lastError();
    cfgUi.gmStatus = gm.isOpen() ? (gm.soundFont() + " running") : gm.lastError();
    // Appareils MIDI hôtes : énumération et RECONNEXION à 1 Hz. Le débranchement
    // d'un câble USB ne doit pas demander de rouvrir la configuration, et 60
    // énumérations CoreMIDI par seconde pour deux menus seraient du gaspillage.
    {
        static auto lastMidiScan = clock::now() - std::chrono::seconds(2);
        if (clock::now() - lastMidiScan >= std::chrono::seconds(1)) {
            lastMidiScan = clock::now();
            cfgUi.midiOutDevs = MidiOutHost::destinations();
            cfgUi.midiInDevs  = MidiInHost::sources();
            // On ne re-tente que si l'énumération FRAÎCHE promet un résultat
            // différent de ce qui est ouvert. ⚠ Pas « différent du CONFIGURÉ » :
            // une re-tentative ferme tout d'abord — panique (All Notes Off) des
            // appareils branchés en sortie, purge du tampon en entrée — et un
            // appareil configuré durablement absent rendait l'ancienne garde
            // vraie À CHAQUE SECONDE. Les notes tenues du synthé restant
            // tombaient au rythme de la re-tentative (cf. countMatchable).
            const auto wanted = [](const auto& cfgList) {
                std::vector<neost::midi::Wanted> w;
                w.reserve(cfgList.size());
                for (const auto& d : cfgList) w.push_back({d.name, d.uid});
                return w;
            };
            if (neost::midi::countMatchable(wanted(cfg.midiOutDevices), cfgUi.midiOutDevs)
                    != midiOut.destinationCount()) A.midiOutApply();
            if (neost::midi::countMatchable(wanted(cfg.midiInDevices), cfgUi.midiInDevs)
                    != midiIn.deviceCount()) A.midiInApply();
            // Un appareil qui vient d'être rebranché peut enfin livrer son
            // identifiant : on le retient, et on ne persiste que si on a appris.
            if (A.midiLearnUids()) saveConfig(A, exeDir, cfg, &machine);
        }
    }
    cfgUi.midiOutOpen   = midiOut.openDestinations();
    cfgUi.midiInOpen    = midiIn.openNames();
    cfgUi.midiInBytes   = midiIn.delivered();
    cfgUi.midiLateBytes = midiOut.lateBytes();
    if (!cfgUi.mixInit) {            // sème les faders depuis la config (une fois)
        cfgUi.mixYm = cfg.mixYm; cfgUi.mixDma = cfg.mixDma; cfgUi.mixDac = cfg.mixDac;
        cfgUi.mixDrive = cfg.mixDrive; cfgUi.mixMt32 = cfg.mixMt32; cfgUi.mixGm = cfg.mixGm;
        cfgUi.mixInit = true;
    }
}

// Requêtes de la fenêtre Configuration (hors profils).
static void consumeConfigRequests(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    const std::string& romsDir = A.romsDir;
    MidiOutHost& midiOut = *A.midiOut;
    Mt32Synth& mt32 = *A.mt32;
    GmSynth& gm = *A.gm;
    Audio& audio = *A.audio;
    DriveSound& drive = *A.drive;
    bool& driveSoundOn = A.driveSoundOn;
    bool& reqHardReset = F.reqHardReset;
    bool& reqRebuild = F.reqRebuild;
    int& reqMonitor = F.reqMonitor;
    ConfigUi& cfgUi = *F.cfgUi;
    std::string& reqMountCart = F.reqMountCart;
    bool& reqEjectCart = F.reqEjectCart;
    std::string& reqMountGemdos = F.reqMountGemdos;
    std::string& reqMountAcsi = F.reqMountAcsi;
    bool& reqEjectGemdos = F.reqEjectGemdos;
    bool& reqEjectAcsi = F.reqEjectAcsi;
    // Déversement des requêtes de la fenêtre dans celles de la boucle.
    if (!cfgUi.reqMountCart.empty())   { reqMountCart   = cfgUi.reqMountCart;   cfgUi.reqMountCart.clear(); }
    if (!cfgUi.reqMountGemdos.empty()) { reqMountGemdos = cfgUi.reqMountGemdos; cfgUi.reqMountGemdos.clear(); }
    if (!cfgUi.reqMountAcsi.empty())   { reqMountAcsi   = cfgUi.reqMountAcsi;   cfgUi.reqMountAcsi.clear(); }
    if (cfgUi.reqEjectCart)   { reqEjectCart   = true; cfgUi.reqEjectCart = false; }
    if (cfgUi.reqEjectGemdos) { reqEjectGemdos = true; cfgUi.reqEjectGemdos = false; }
    if (cfgUi.reqEjectAcsi)   { reqEjectAcsi   = true; cfgUi.reqEjectAcsi = false; }
    // UltraSatan : bascule + slot 2, appliqués ici (même discipline qu'EtherNEC :
    // le TOS ne sonde le bus ACSI qu'au boot → hard reset).
    if (cfgUi.reqUltraSatan >= 0) {
        cfg.ultrasatan = (cfgUi.reqUltraSatan == 1);
        A.usatanApply();
        saveConfig(A, exeDir, cfg, &machine);
        reqHardReset = true;
        cfgUi.reqUltraSatan = -1;
    }
    if (!cfgUi.reqMountSd2.empty()) {
        if (machine.fdc.mountAcsi(A.resolvePath(cfgUi.reqMountSd2), 1)) {
            cfg.sd2 = cfgUi.reqMountSd2; saveConfig(A, exeDir, cfg, &machine);
            reqHardReset = true;
        } else {
            A.stateMsg = "Unreadable SD image"; A.stateMsgFrames = 120;
        }
        cfgUi.reqMountSd2.clear();
    }
    if (cfgUi.reqEjectSd2) {
        cfg.sd2.clear();
        // Pas d'éjection d'une seule cible dans Acsi : on vide tout et on remonte
        // ce que la config garde (image ACSI du slot 1).
        machine.fdc.unmountAcsi();
        if (!cfg.acsi.empty()) machine.fdc.mountAcsi(A.resolvePath(cfg.acsi));
        A.usatanApply();
        saveConfig(A, exeDir, cfg, &machine);
        reqHardReset = true;
        cfgUi.reqEjectSd2 = false;
    }
    if (cfgUi.reqMonitor >= 0) { reqMonitor = cfgUi.reqMonitor; cfgUi.reqMonitor = -1; }
    if (cfgUi.reqKiosk)       { A.kioskSwitchReq = 1; cfgUi.reqKiosk = false; }
    if (cfgUi.reqVolume >= 0.0f) {
        audio.setMasterVolume(cfgUi.reqVolume); cfgUi.reqVolume = -1.0f;
    }
    if (cfgUi.volumeDone) {
        cfg.volume = audio.masterVolume(); saveConfig(A, exeDir, cfg, &machine);
        cfgUi.volumeDone = false;
    }
    if (cfgUi.reqFastFdc >= 0) {
        cfg.fastfdc = (cfgUi.reqFastFdc != 0);
        machine.fdc.setFastFdc(cfg.fastfdc);
        saveConfig(A, exeDir, cfg, &machine);
        cfgUi.reqFastFdc = -1;
    }
    if (cfgUi.reqModem >= 0) {
        cfg.modem = (cfgUi.reqModem == 1);
#ifdef NEOST_WITH_NET
        A.modemApply(cfg.modem);
#else
        A.stateMsg = "This build has no network backend";
        A.stateMsgFrames = 120;
        cfg.modem = false;
#endif
        saveConfig(A, exeDir, cfg, &machine);
        cfgUi.reqModem = -1;
    }
    if (cfgUi.reqEther >= 0) {
        cfg.ethernec = (cfgUi.reqEther == 1);
        A.etherApply(cfg.ethernec);        // etherApply peut refuser (cartouche)
        saveConfig(A, exeDir, cfg, &machine);
        reqHardReset = true;             // le pilote sonde la carte au boot
        cfgUi.reqEther = -1;
    }
    if (cfgUi.mixDirty) {
        cfg.mixYm = cfgUi.mixYm; cfg.mixDma = cfgUi.mixDma; cfg.mixDac = cfgUi.mixDac;
        cfg.mixDrive = cfgUi.mixDrive; cfg.mixMt32 = cfgUi.mixMt32; cfg.mixGm = cfgUi.mixGm;
        audio.setMixGains(cfg.mixYm, cfg.mixDma, cfg.mixDrive, cfg.mixMt32, cfg.mixGm);
        machine.psg.setPortBDacGain(cfg.mixDac);
        cfgUi.mixDirty = false;
    }
    if (cfgUi.mixDone) { saveConfig(A, exeDir, cfg, &machine); cfgUi.mixDone = false; }
    if (cfgUi.reqMidiOutGm >= 0)   { cfg.midiOutGm   = cfgUi.reqMidiOutGm   == 1; cfgUi.reqMidiOutGm   = -1; A.midiOutApply(); saveConfig(A, exeDir, cfg, &machine); }
    if (cfgUi.reqMidiOutPort >= 0) { cfg.midiOutPort = cfgUi.reqMidiOutPort == 1; cfgUi.reqMidiOutPort = -1; A.midiOutApply(); saveConfig(A, exeDir, cfg, &machine); }
    // La ligne MT-32 manquait, entre ses deux voisines : la case de la page MIDI
    // posait sa requête et RIEN ne la consommait — contrôle mort, qui se recochait
    // seul à la trame suivante puisque la case relit `cfg` à chaque passage. Le
    // réglage n'était atteignable que par le menu Machine, hors de la page dont
    // c'est pourtant le sujet.
    if (cfgUi.reqMidiOutMt32 >= 0) { cfg.midiOutMt32 = cfgUi.reqMidiOutMt32 == 1; cfgUi.reqMidiOutMt32 = -1; A.midiOutApply(); saveConfig(A, exeDir, cfg, &machine); }
    if (cfgUi.reqMidiLead >= 0) {
        cfg.midiLeadMs = cfgUi.reqMidiLead; cfgUi.reqMidiLead = -1;
        midiOut.setLeadMs(cfg.midiLeadMs);
        saveConfig(A, exeDir, cfg, &machine);
    }
    if (cfgUi.midiDevsDirty) {
        cfgUi.midiDevsDirty = false;
        cfg.midiOutDevices = cfgUi.reqMidiOut;
        cfg.midiInDevices  = cfgUi.reqMidiIn;
        A.midiOutApply(); A.midiInApply();
        saveConfig(A, exeDir, cfg, &machine);
    }
    if (cfgUi.reqMidiLoopback >= 0) { cfg.midiLoopback = cfgUi.reqMidiLoopback == 1;
                                      cfgUi.reqMidiLoopback = -1;
                                      machine.midi.setLoopback(cfg.midiLoopback);
                                      saveConfig(A, exeDir, cfg, &machine); }
    if (cfgUi.reqPlugPort >= 0) {
        const auto p = PortDevices::Port(cfgUi.reqPlugPort);
        const auto d = PortDevices::Device(cfgUi.reqPlugDev);
        cfgUi.reqPlugPort = cfgUi.reqPlugDev = -1;
        if (machine.plugPort(p, d)) {
            std::string* slots[] = { &cfg.joy0, &cfg.joy1, &cfg.rs232, &cfg.printer, &cfg.cartbutton };
            *slots[int(p)] = d == PortDevices::Device::None ? "" : PortDevices::id(d);
            saveConfig(A, exeDir, cfg, &machine);
        }
    }
    if (cfgUi.reqPortButton) { cfgUi.reqPortButton = false; machine.pressPortButton(); }
    if (cfgUi.reqDongle >= 0) {
        static const char* const names[] = { "", "cubase2", "cubase3", "auto", "notator" };
        const int d = std::min(cfgUi.reqDongle, 4); cfgUi.reqDongle = -1;
        if (machine.setDongle(CartridgeKey::Model(d))) {
            cfg.dongle = names[d];
            saveConfig(A, exeDir, cfg, &machine);
        } else {
            A.stateMsg = "Steinberg key needs the cartridge port free (EtherNEC/NetUSBee)";
            A.stateMsgFrames = 150;
        }
    }
    // Panique : on la passe À TOUTES les destinations. Le MT-32 et le synthé GM
    // intégré sont des synthés à part (ils ne voient pas le flux de MidiOutHost),
    // donc leur envoyer les mêmes contrôleurs est le seul moyen de les faire taire.
    if (cfgUi.reqMidiPanic) {
        cfgUi.reqMidiPanic = false;
        midiOut.panic();
        for (int ch = 0; ch < 16; ++ch) {
            const uint8_t st = uint8_t(0xB0 | ch);
            for (uint8_t cc : {uint8_t(120), uint8_t(121), uint8_t(123)}) {
                mt32.byteAt(st, 0); mt32.byteAt(cc, 0); mt32.byteAt(0, 0);
                gm.byteAt(st, 0);   gm.byteAt(cc, 0);   gm.byteAt(0, 0);
            }
        }
        A.stateMsg = "MIDI panic: all notes off"; A.stateMsgFrames = 120;
    }
    if (cfgUi.reqMt32Model >= 0) {
        cfg.mt32Model = cfgUi.reqMt32Model == 1 ? "mt32" : cfgUi.reqMt32Model == 2 ? "cm32l" : "auto";
        cfgUi.reqMt32Model = -1;
        mt32.close();                    // rouvre avec le modèle demandé
        A.midiOutApply(); saveConfig(A, exeDir, cfg, &machine);
    }
    if (cfgUi.reqNetUsbee >= 0) {
        cfg.netusbee = (cfgUi.reqNetUsbee == 1);
        A.netUsbeeApply(cfg.netusbee);     // peut refuser (cartouche)
        saveConfig(A, exeDir, cfg, &machine);
        reqHardReset = true;
        cfgUi.reqNetUsbee = -1;
    }
    if (cfgUi.reqSlirp >= 0) {
        cfg.slirp = (cfgUi.reqSlirp == 1);
        A.slirpApply(cfg.slirp);           // peut refuser (lib absente, échec d'init)
        saveConfig(A, exeDir, cfg, &machine);
        // Pas de reset : la carte vue par le pilote ne change pas, seul son
        // « câble » change — la bascule boucle locale ↔ Internet est à chaud.
        cfgUi.reqSlirp = -1;
    }
    if (cfgUi.driveSound != driveSoundOn) {
        driveSoundOn = cfgUi.driveSound; drive.setEnabled(driveSoundOn);
        cfg.driveSound = driveSoundOn;   // persisté par cfgDirty (la case l'a levé)
    }
    if (cfgUi.reqSaveState) {
        const bool ok = machine.saveStateFile(exeDir + "/../neost.state");
        A.stateMsg = ok ? "\xef\x83\x87 State saved" : "Save failed";
        A.stateMsgFrames = 120; cfgUi.reqSaveState = false;
    }
    if (cfgUi.reqLoadState) {
        const bool ok = machine.loadStateFile(exeDir + "/../neost.state");
        A.stateMsg = ok ? "\xef\x80\x9e State restored" : "No state / failed";
        A.stateMsgFrames = 120; cfgUi.reqLoadState = false;
    }
    // « Appliquer et redémarrer » : les quatre réglages matériels d'un coup,
    // UN seul rebuild. Mega STE + TOS trop ancien → on remonte un TOS ≥ 2.06,
    // sinon « choisir Mega STE » redonnait un simple STE (cf. pickTosForMachine).
    if (cfgUi.reqApply) {
        cfg.machine = cfgUi.pendMachine;
        cfg.mem     = cfgUi.pendMem;
        cfg.fpu     = cfgUi.pendFpu;
        cfg.rom     = cfgUi.pendRom;
        const std::string autoRom = pickTosForMachine(cfg.machine, cfg.rom, exeDir, romsDir);
        if (!autoRom.empty()) cfg.rom = autoRom;
        saveConfig(A, exeDir, cfg, &machine);
        reqRebuild = true;
        cfgUi.reqApply = false;
        cfgUi.pendInit = false;      // resème depuis la config appliquée
    }
    if (cfgUi.cfgDirty) {
        cfg.autoZoom = A.autoZoom;
        cfg.mouseSpeed = A.mouseSpeed;
        cfg.desktopMouse = A.desktopMouse;
        cfg.crt = A.crtOn; cfg.crtParams = A.crtParams;
        saveConfig(A, exeDir, cfg, &machine);
        cfgUi.cfgDirty = false;
    }
}

// Requêtes de la page Profiles de la fenêtre Configuration.
static void consumeProfileRequests(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    Mt32Synth& mt32 = *A.mt32;
    GmSynth& gm = *A.gm;
    Audio& audio = *A.audio;
    DriveSound& drive = *A.drive;
    const bool driveSoundAvail = A.driveSoundAvail;
    bool& driveSoundOn = A.driveSoundOn;
    bool& reqRebuild = F.reqRebuild;
    ConfigUi& cfgUi = *F.cfgUi;
    std::string& reqMount = F.reqMount;
    std::string& reqMountB = F.reqMountB;
    bool& reqEject = F.reqEject;
    bool& reqEjectB = F.reqEjectB;
    // ── Profils nommés (page « Profiles ») ─────────────────────────────
    // Borne : rien ne s'écrit sur le disque (invariant « la borne repart
    // identique »). La page grise déjà les boutons ; on double la garde ici,
    // seul endroit qui touche réellement au système de fichiers.
    if ((A.kiosk || A.kioskLaunched)
        && (!cfgUi.reqSaveProfile.empty() || !cfgUi.reqDeleteProfile.empty())) {
        cfgUi.reqSaveProfile.clear(); cfgUi.reqDeleteProfile.clear();
        A.stateMsg = "Kiosk mode: configuration frozen"; A.stateMsgFrames = 150;
    }
    if (!cfgUi.reqSaveProfile.empty()) {
        // On fige les réglages EN VIGUEUR, pas les champs « en attente » : un
        // profil décrit une machine qui tourne, pas un formulaire à moitié rempli.
        // Realignement préalable sur les globals d'interface — plusieurs chemins
        // les changent sans repasser par saveConfig (F10 en borne, panneau CRT,
        // fenêtre Joysticks), et le profil hériterait sinon de valeurs périmées.
        cfg.autoZoom = A.autoZoom;
        cfg.crt      = A.crtOn; cfg.crtParams = A.crtParams;
        cfg.desktopMouse = A.desktopMouse; cfg.mouseSpeed = A.mouseSpeed;
        cfg.volume   = audio.masterVolume();
        cfg.joyport  = A.kbdJoyPort; cfg.joydeadzone = A.joyDeadzone;
        cfg.joymap   = joymapSerialize(A); cfg.port0 = A.port0Auto ? "auto" : "mouse";
        cfg.driveSound = driveSoundOn;
        std::string err;
        if (saveProfile(cfgUi.profDir, cfgUi.reqSaveProfile, cfg, err)) {
            A.stateMsg = "\xef\x83\x87 Profile saved: " + cfgUi.reqSaveProfile;
        } else {
            A.stateMsg = "Profile NOT saved";
            std::fprintf(stderr, "[cfg] profile: %s\n", err.c_str());
        }
        A.stateMsgFrames = 150; A.profilesDirty = true;
        cfgUi.reqSaveProfile.clear();
    }
    if (!cfgUi.reqLoadProfile.empty()) {
        // On part de la config COURANTE : un profil n'écrit qu'un sous-ensemble
        // de clés, et tout ce qu'il tait (horloge, disposition, dossiers ROM de
        // la borne) doit rester en place.
        Config p = cfg;
        if (loadProfileInto(cfgUi.profDir, cfgUi.reqLoadProfile, p)) {
            const std::string prevA = cfg.disk, prevB = cfg.diskb;
            cfg = p;
            // Réglages à effet immédiat (le matériel, lui, part au rebuild ci-dessous).
            A.autoZoom = cfg.autoZoom;
            A.desktopMouse = cfg.desktopMouse; A.mouseSpeed = cfg.mouseSpeed;
            A.desktopPointer.reset();
            machine.ikbd.mouseEvent(0, 0, false, false);
            A.crtOn    = cfg.crt; A.crtParams = cfg.crtParams;
            A.kbdJoyPort  = cfg.joyport;
            A.port0Auto   = (cfg.port0 == "auto");   // cf. init au démarrage
            A.joyDeadzone = cfg.joydeadzone;
            joymapParse(A, cfg.joymap);
            audio.setMasterVolume(cfg.volume);
            driveSoundOn = driveSoundAvail && cfg.driveSound;
            drive.setEnabled(driveSoundOn);
            // Disquettes : A.applyConfig() ne touche PAS aux lecteurs (« le disque
            // monté est conservé ») → on monte/éjecte explicitement ce que dit le
            // profil, par les requêtes normales de montage.
            // Une image DISPARUE depuis l'enregistrement du profil ne doit pas
            // s'inscrire dans neost.cfg : l'invariant des montages est qu'on ne
            // persiste QU'un montage réussi, sinon le fantôme est retenté à chaque
            // démarrage. Le lecteur garde alors ce qu'il avait, et on le dit.
            std::string missing;
            auto wantDisk = [&](std::string& want, const std::string& prev,
                                std::string& reqMountSlot, bool& reqEjectSlot) {
                if (want == prev) return;
                if (want.empty()) { reqEjectSlot = true; return; }
                const std::string img = resolveData(want, exeDir);
                if (fileExists(img)) { reqMountSlot = img; return; }
                if (missing.empty()) missing = want;
                want = prev;
            };
            wantDisk(cfg.disk,  prevA, reqMount,  reqEject);
            wantDisk(cfg.diskb, prevB, reqMountB, reqEjectB);
            // Réseau : le profil porte modem=/ethernec= mais ni applyConfig
            // ni le rebuild ne les branchent — sans ces appels, les cases
            // affichaient l'état du profil alors que le matériel restait
            // celui d'avant (modem coché mais AT dans le vide…).
#ifdef NEOST_WITH_NET
            A.modemApply(cfg.modem);
#endif
            A.slirpApply(cfg.slirp);
            A.netUsbeeApply(cfg.netusbee);
            A.etherApply(cfg.ethernec);
            // Son/MIDI du profil : sorties (GM/CoreMIDI/MT-32 + modèle), câble de
            // bouclage, faders — rejoués ici, et la page Sound ressème ses faders.
            mt32.close();
            gm.close();
            A.midiOutApply();
            machine.midi.setLoopback(cfg.midiLoopback);
            audio.setMixGains(cfg.mixYm, cfg.mixDma, cfg.mixDrive, cfg.mixMt32, cfg.mixGm);
            machine.psg.setPortBDacGain(cfg.mixDac);
            cfgUi.mixInit = false;
            saveConfig(A, exeDir, cfg, &machine);
            reqRebuild = true;        // modèle/RAM/FPU/ROM/cartouche/HD/moniteur/FDC (+ UltraSatan)
            cfgUi.pendInit = false;   // resème les champs « en attente »
            A.stateMsg = missing.empty()
                ? "\xef\x80\x9e Profile loaded: " + cfgUi.reqLoadProfile
                : "Profile loaded — missing floppy: "
                      + fs::path(missing).filename().string();
        } else {
            A.stateMsg = "Profile not readable";
            A.profilesDirty = true;   // disparu du dossier ? on relit la liste
        }
        A.stateMsgFrames = 150;
        cfgUi.reqLoadProfile.clear();
    }
    if (!cfgUi.reqDeleteProfile.empty()) {
        const bool okDel = deleteProfile(cfgUi.profDir, cfgUi.reqDeleteProfile);
        A.stateMsg = okDel ? "Profile deleted: " + cfgUi.reqDeleteProfile
                           : "Delete failed";
        A.stateMsgFrames = 150; A.profilesDirty = true;
        cfgUi.reqDeleteProfile.clear();
    }
}

// Dessin, chrome du bureau : requêtes Floppies, fenêtres d'inspection, CRT.
static void drawSideWindows(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    GLFWwindow* const window = A.window;
    bool& reqReset = F.reqReset;
    ConfigUi& cfgUi = *F.cfgUi;
    std::string& reqMount = F.reqMount;
    std::string& reqMountB = F.reqMountB;
    bool& reqEject = F.reqEject;
    bool& reqEjectB = F.reqEjectB;
    // Requêtes propres à la fenêtre Floppies, consommées indépendamment de
    // l'ouverture de Configuration et avant le traitement des montages ci-dessous.
    if (!cfgUi.reqMountA.empty()) { reqMount  = cfgUi.reqMountA; cfgUi.reqMountA.clear(); }
    if (!cfgUi.reqMountB.empty()) { reqMountB = cfgUi.reqMountB; cfgUi.reqMountB.clear(); }
    if (cfgUi.reqEjectA) { reqEject  = true; cfgUi.reqEjectA = false; }
    if (cfgUi.reqEjectB) { reqEjectB = true; cfgUi.reqEjectB = false; }

    if (A.showHex)  drawHexViewer(A, machine.bus);
    if (A.showCpu)  drawCpuState(A, machine.cpu, reqReset);
    if (A.showJoy)  drawJoystickWindow(A, window, A.lastJoy0, A.lastJoy1);
    if (A.showKbd)  drawKeyboardWindow(&A.showKbd, machine.ikbd,
                                       resolveData("pic/Black_Keyboard_AtariST.jpeg", exeDir));
    else            keyboardWindowReleaseAll(machine.ikbd);   // fenêtre masquée : rien d'enfoncé
    if (A.showDbg)  drawDebugger(A, machine);
    if (A.showCrt) {                     // fenêtre de réglages CRT
        bool crtChanged = false;
        drawCrtSettings(A, crtChanged);
        if (crtChanged) {                // recopie dans le cfg + resauve (no-op en kiosk)
            cfg.crt = A.crtOn; cfg.crtParams = A.crtParams;
            saveConfig(A, exeDir, cfg, &machine);
        }
    }
    // Un réglage joystick a changé dans la fenêtre → resauve neost.cfg.
    if (A.joyCfgDirty) {
        cfg.joyport = A.kbdJoyPort; cfg.joydeadzone = A.joyDeadzone;
        cfg.port0 = A.port0Auto ? "auto" : "mouse"; cfg.joymap = joymapSerialize(A);
        saveConfig(A, exeDir, cfg, &machine); A.joyCfgDirty = false;
    }
}

// Dessin : tout le chrome ImGui du bureau (absent en kiosk et en plein écran).
static void drawDesktopChrome(App& A, Frame& F) {
    Machine& machine = *A.machine;
    GlScreen& screen = *A.screen;
    int& kTop = F.kTop;
    int& kH = F.kH;
    int& kW = F.kW;
    ConfigUi& cfgUi = *F.cfgUi;
    const float& menuH = F.menuH;
    const float& toolH = F.toolH;
    const bool color = machine.mfp.colorMonitor();
    F.color = color;

    drawMenuBar(A, F);
    drawToolbar(A, F);
    drawStatusBar(A, F);

    // --- Dockspace (après les barres, AVANT toute fenêtre ancrable) ------
    renderDockSpace(A, A.dockOn);

    // --- Fenêtre écran (base) + fenêtres masquables ----------------------
    drawStScreen(A, screen, A.mouseCaptured, menuH + toolH, kTop, kH, kW);

    syncConfigUi(A, F);

    if (A.showFloppy) drawFloppyWindow(A, cfgUi);
    if (A.showCfg) {
        // La fenêtre ne monte/démonte/redémarre rien : elle remplit `cfgUi`, qu'on
        // déverse dans les requêtes de la boucle juste après. Les chemins de disque
        // dur sont lus dans `cfg` (tenu à jour par les montages) — ni GemdosHd ni
        // Acsi n'exposent le leur.
        drawConfigWindow(A, cfgUi);

        consumeConfigRequests(A, F);
        consumeProfileRequests(A, F);
    }
    drawSideWindows(A, F);
}

// Dessin : menu in-game du kiosk (navigation manette/clavier + rendu).
static void runKioskMenu(App& A, Frame& F) {
    using clock = std::chrono::steady_clock;
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    const std::string& disksDir = A.disksDir;
    GLFWwindow* const window = A.window;
    bool& reqHardReset = F.reqHardReset;
    std::string& reqMount = F.reqMount;
    // La navigation du menu ne consulte QUE les manettes réellement affectées à
    // un port ST (assign >= 0). Sans ce filtre, une manette explicitement mise
    // sur OFF pilotait quand même le menu : un encodeur arcade dont l'axe Y
    // repose de travers — le défaut même pour lequel la page JOYSTICKS existe —
    // faisait défiler la sélection en continu, rendant le menu (et donc la page
    // qui aurait permis de le corriger) inutilisable.
    int8_t navRoles[GLFW_JOYSTICK_LAST + 1];
    joyResolveRoles(A, navRoles);
    int8_t navAssign[GLFW_JOYSTICK_LAST + 1];
    stjoy::resolveAssign(navRoles, navAssign, A.port0Auto);
    auto navUsable = [&](int j) {
        return j >= 0 && j <= GLFW_JOYSTICK_LAST && navAssign[j] >= 0;
    };
    // Les BOUTONS de N'IMPORTE QUELLE manette naviguent, y compris une manette
    // mise sur OFF. Le filtre par rôle ne s'applique qu'aux AXES (c'est un stick
    // au repos décentré qui rend le menu fou, pas un bouton). Sans cette
    // asymétrie, un opérateur qui passe sa seule manette sur OFF depuis la page
    // JOYSTICKS — page dont c'est précisément la fonction — perdait TOUT contrôle,
    // et le réglage étant persisté, la borne redémarrait verrouillée.
    auto padBtn = [&](int b) {
        for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
            GLFWgamepadstate gs;
            if (glfwJoystickPresent(j) && glfwGetGamepadState(j, &gs) && gs.buttons[b])
                return true;
        }
        return false;
    };
    // Zone morte de l'utilisateur (A.joyDeadzone) et non un seuil figé : un stick
    // au repos décentré ne doit pas compter comme une direction tenue.
    auto padAxis = [&](int axis) {
        // Sur la page JOYSTICKS elle-même, toute manette peut bouger la sélection :
        // sinon on ne pourrait pas RÉ-ACTIVER une manette qu'on vient de couper.
        const bool anyPad = (A.kioskPage == KIOSK_PAGE_JOY);
        for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j) {
            GLFWgamepadstate gs;
            if ((anyPad || navUsable(j)) && glfwJoystickPresent(j) && glfwGetGamepadState(j, &gs)) {
                const float v = gs.axes[axis];
                if (std::fabs(v) > A.joyDeadzone) return v;
            }
        }
        return 0.0f;
    };
    auto padAxisY = [&]() { return padAxis(GLFW_GAMEPAD_AXIS_LEFT_Y); };
    auto padAxisX = [&]() { return padAxis(GLFW_GAMEPAD_AXIS_LEFT_X); };
    // Ouvrir / fermer : START manette ou F9 clavier (front montant). La liste
    // est triée par PROXIMITÉ au disque courant → les phases B/C/D du jeu en
    // cours arrivent en tête, et la sélection démarre sur le disque monté.
    static bool pOpen = false;
    const bool openNow = padBtn(GLFW_GAMEPAD_BUTTON_START) ||
                         glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS;
    if (openNow && !pOpen) {
        A.kioskDiskMenu = !A.kioskDiskMenu;
        A.kioskPage = KIOSK_PAGE_LIST;   // ré-ouverture : toujours sur la liste
        A.kioskZone = KIOSK_ZONE_LIST;   // focus par défaut : menu des jeux
        A.kioskActSel = 0;
        if (A.kioskDiskMenu) {
            const std::string m = machine.fdc.mountedPath();
            // Auto-prune : un dossier ROM disparu (débranché) est retiré + persisté.
            if (kioskPruneRomDirs(A)) { cfg.romDirs = A.kioskRomDirs; saveConfig(A, exeDir, cfg, &machine, true); }
            kioskScanDisks(A, disksDir, m);
            A.kioskDiskSel = 0;
            for (int i = 0; i < (int)A.kioskDisks.size(); ++i)
                if (A.kioskDisks[i] == m) { A.kioskDiskSel = i; break; }
        }
    }
    pOpen = openNow;

    // SELECT (Back manette) ou K : ouvre/ferme DIRECTEMENT le bandeau Clavier &
    // souris, même en cours de jeu (sans passer par la liste). Le jeu tourne
    // dessous → la touche envoyée agit tout de suite.
    static bool pSelect = false;
    const bool selNow = padBtn(GLFW_GAMEPAD_BUTTON_BACK) ||
                        glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS;   // F12, pas K (cf. onKey)
    if (selNow && !pSelect) {
        if (A.kioskDiskMenu && A.kioskPage == KIOSK_PAGE_KEYS) {
            A.kioskDiskMenu = false;               // referme le clavier → reprise
        } else {
            A.kioskDiskMenu = true;
            A.kioskPage = KIOSK_PAGE_KEYS;         // ouvre direct le clavier
            A.kioskKeySel = 0;
        }
    }
    pSelect = selNow;

    if (A.kioskDiskMenu) {
        // Fronts partagés (A / B) : lus une fois, réutilisés selon la page.
        static bool pOk = false, pCancel = false;
        const bool okNow = padBtn(GLFW_GAMEPAD_BUTTON_A) ||
                           glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
        const bool caNow = padBtn(GLFW_GAMEPAD_BUTTON_B) ||
                           glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;

        // Directions (D-pad / stick / flèches) avec répétition sur maintien,
        // partagées par toutes les pages (la liste n'utilise que haut/bas).
        const bool up    = padBtn(GLFW_GAMEPAD_BUTTON_DPAD_UP)    ||
                           glfwGetKey(window, GLFW_KEY_UP)    == GLFW_PRESS || padAxisY() < -0.5f;
        const bool down  = padBtn(GLFW_GAMEPAD_BUTTON_DPAD_DOWN)  ||
                           glfwGetKey(window, GLFW_KEY_DOWN)  == GLFW_PRESS || padAxisY() >  0.5f;
        const bool left  = padBtn(GLFW_GAMEPAD_BUTTON_DPAD_LEFT)  ||
                           glfwGetKey(window, GLFW_KEY_LEFT)  == GLFW_PRESS || padAxisX() < -0.5f;
        const bool right = padBtn(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT) ||
                           glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS || padAxisX() >  0.5f;
        // L1 / R1 (gâchettes hautes) ou Page↑/Page↓ : saut de PAGE dans la liste des
        // jeux (défilement rapide). Traités comme des directions (même répétition).
        const bool pgUp = padBtn(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER)  ||
                          glfwGetKey(window, GLFW_KEY_PAGE_UP)   == GLFW_PRESS;
        const bool pgDn = padBtn(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER) ||
                          glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS;
        // Répétition TEMPORELLE (pas au nombre d'itérations) : la boucle tourne
        // sans vsync et à vide quand le jeu est en pause → un compteur de trames
        // défilerait à des centaines de pas/s (impossible de viser un item). On
        // se cale donc sur l'horloge : 1 pas au front, puis délai avant répétition,
        // puis un pas toutes les ~150 ms tant que la direction est maintenue.
        static bool navHeld = false;
        static clock::time_point navNext{};
        const bool nav = up || down || left || right || pgUp || pgDn;
        const auto navNow = clock::now();
        bool step = false;
        if (nav) {
            if (!navHeld) { step = true; navHeld = true;
                            navNext = navNow + std::chrono::milliseconds(400); }  // pause avant répét.
            else if (navNow >= navNext) { step = true;
                            navNext = navNow + std::chrono::milliseconds(150); }   // cadence de répét.
        } else {
            navHeld = false;
        }

        if (A.kioskPage == KIOSK_PAGE_QUIT) {
            // « Quitter ? » : A/Entrée confirme, B/Échap revient à la liste.
            if (okNow && !pOk) glfwSetWindowShouldClose(window, GLFW_TRUE);
            if (caNow && !pCancel) A.kioskPage = KIOSK_PAGE_LIST;

        } else if (A.kioskPage == KIOSK_PAGE_KEYS) {
            // Grille de touches : navigation 2D. (A) envoie une frappe brève au
            // ST (le jeu tourne derrière), (B) revient au menu.
            if (step) {
                int row = 0;
                for (int r = 0; r < KIOSK_KEY_ROWN; ++r)
                    if (A.kioskKeySel >= KIOSK_KEY_ROWS[r][0] && A.kioskKeySel < KIOSK_KEY_ROWS[r][1]) row = r;
                int col = A.kioskKeySel - KIOSK_KEY_ROWS[row][0];
                if (up || down) {
                    row = (row + (down ? 1 : -1) + KIOSK_KEY_ROWN) % KIOSK_KEY_ROWN;
                    const int len = KIOSK_KEY_ROWS[row][1] - KIOSK_KEY_ROWS[row][0];
                    if (col >= len) col = len - 1;
                } else {   // gauche/droite : dans la rangée, avec butée
                    const int len = KIOSK_KEY_ROWS[row][1] - KIOSK_KEY_ROWS[row][0];
                    col += right ? 1 : -1;
                    if (col < 0) col = 0;
                    if (col >= len) col = len - 1;
                }
                A.kioskKeySel = KIOSK_KEY_ROWS[row][0] + col;
            }
            // On ne pose une nouvelle frappe QUE si la précédente a fini son
            // maintien (A.kioskInjectHold == 0) : sinon un 2ᵉ appui < 4 trames
            // écraserait A.kioskKeyRelease → le MAKE précédent n'aurait jamais
            // son BREAK (touche « collée » côté ST).
            if (okNow && !pOk && A.kioskInjectHold == 0) {
                const KioskKey& k = KIOSK_KEYS[A.kioskKeySel];
                if (k.kind == 0) {                       // touche clavier ST
                    machine.ikbd.keyEvent(k.scancode, true);
                    A.kioskKeyRelease = k.scancode;
                } else {                                 // clic souris (G/D)
                    const bool L = (k.kind == 1), R = (k.kind == 2);
                    machine.ikbd.mouseEvent(0, 0, L, R);
                    A.kioskMouseRelL = L; A.kioskMouseRelR = R;
                }
                A.kioskInjectHold = 4;                   // ~4 trames de maintien
            }
            if (caNow && !pCancel) A.kioskDiskMenu = false;   // (B) ferme → reprise du jeu

        } else if (A.kioskPage == KIOSK_PAGE_BROWSE) {
            // Navigateur : [0] valider · [1] .. · [2..2+S) raccourcis · [reste] sous-dossiers.
            const int nShort = (int)A.browseShortcutPaths.size();
            const int total  = 2 + nShort + (int)A.browseSubdirs.size();
            if (step && (up || down)) {
                A.browseSel += down ? 1 : -1;
                A.browseSel = (A.browseSel % total + total) % total;
            }
            if (okNow && !pOk) {
                if (A.browseSel == 0) {                    // valider CE dossier → l'ajoute
                    // REFUS de la racine et du dossier personnel : kioskScanDisks
                    // les parcourrait RÉCURSIVEMENT, sans limite de profondeur ni
                    // de temps, DANS le thread GUI — la borne se figerait plusieurs
                    // minutes à chaque ouverture du menu, sans aucun retour.
                    // Comparaison CANONIQUE des deux côtés : l'égalité de chemins
                    // brute se contournait par une simple barre oblique finale
                    // (« /home/x/ » ≠ « /home/x »), et /home — le PARENT de tous
                    // les dossiers personnels, donc pire encore — passait tout droit.
                    // On refuse donc aussi tout ANCÊTRE du dossier personnel.
                    std::error_code cec;
                    fs::path bp = fs::weakly_canonical(fs::path(A.browseDir), cec);
                    if (cec) bp = fs::path(A.browseDir).lexically_normal();
                    const char* home = std::getenv("HOME");
                    if (!home || !*home) home = std::getenv("USERPROFILE");
                    bool tooBroad = (bp == bp.root_path());
                    if (!tooBroad && home && *home) {
                        std::error_code hec;
                        fs::path hp = fs::weakly_canonical(fs::path(home), hec);
                        if (hec) hp = fs::path(home).lexically_normal();
                        // bp == hp, ou bp est un ancêtre de hp (/home, /) → refus.
                        const std::string relToHome = hp.lexically_relative(bp).generic_string();
                        tooBroad = (bp == hp)
                                || (!relToHome.empty() && relToHome.rfind("..", 0) != 0);
                    }
                    if (tooBroad) {
                        A.stateMsg = "Folder too broad (root / home) — refused";
                        A.stateMsgFrames = 180;
                    } else {
                    if (std::find(A.kioskRomDirs.begin(), A.kioskRomDirs.end(), A.browseDir)
                            == A.kioskRomDirs.end())
                        A.kioskRomDirs.push_back(A.browseDir);
                    cfg.romDirs = A.kioskRomDirs;
                    saveConfig(A, exeDir, cfg, &machine, true);   // persiste MÊME en kiosk
                    kioskScanDisks(A, disksDir, machine.fdc.mountedPath());
                    A.kioskDiskSel = 0; A.romDirSel = 0;
                    A.kioskPage = KIOSK_PAGE_ROMDIRS;          // retour au gestionnaire
                    }
                } else if (A.browseSel == 1) {             // .. parent
                    const fs::path p(A.browseDir);
                    if (p.has_parent_path() && p.parent_path() != p)
                        A.browseDir = p.parent_path().string();
                    kioskScanBrowse(A, A.browseDir);
                } else if (A.browseSel < 2 + nShort) {     // raccourci (racine / volume monté)
                    A.browseDir = A.browseShortcutPaths[A.browseSel - 2];
                    kioskScanBrowse(A, A.browseDir);
                } else {                                   // descendre dans un sous-dossier
                    A.browseDir = A.browseSubdirs[A.browseSel - 2 - nShort];
                    kioskScanBrowse(A, A.browseDir);
                }
            }
            if (caNow && !pCancel) A.kioskPage = KIOSK_PAGE_ROMDIRS;   // (B) annuler

        } else if (A.kioskPage == KIOSK_PAGE_JOY) {
            // Affectation des manettes : haut/bas sélectionne une manette
            // PRÉSENTE, (A) fait tourner son rôle AUTO → PORT 1 → PORT 0 →
            // OFF → AUTO (persisté par GUID via joymap=), (B) revient.
            int jids[GLFW_JOYSTICK_LAST + 1]; int nj = 0;
            for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST; ++j)
                if (glfwJoystickPresent(j)) jids[nj++] = j;
            if (nj > 0) {
                if (A.kioskJoySel >= nj) A.kioskJoySel = nj - 1;
                if (step && (up || down)) {
                    A.kioskJoySel += down ? 1 : -1;
                    A.kioskJoySel = (A.kioskJoySel % nj + nj) % nj;
                }
                if (okNow && !pOk) {
                    const std::string guid = joyGuid(jids[A.kioskJoySel]);
                    if (!guid.empty()) {
                        const auto it = A.joyRoleByGuid.find(guid);
                        const int8_t cur = (it != A.joyRoleByGuid.end())
                                               ? it->second : int8_t(stjoy::ROLE_AUTO);
                        int8_t next;
                        switch (cur) {                       // AUTO→P1→P0→OFF→AUTO
                            case stjoy::ROLE_AUTO:  next = stjoy::ROLE_PORT1; break;
                            case stjoy::ROLE_PORT1: next = stjoy::ROLE_PORT0; break;
                            case stjoy::ROLE_PORT0: next = stjoy::ROLE_OFF;   break;
                            default:                next = stjoy::ROLE_AUTO;  break;
                        }
                        if (next == stjoy::ROLE_AUTO) A.joyRoleByGuid.erase(guid);
                        else                          A.joyRoleByGuid[guid] = next;
                        cfg.joymap = joymapSerialize(A);      // persiste (comme ROM FOLDERS)
                        saveConfig(A, exeDir, cfg, &machine, true);
                    }
                }
            }
            if (caNow && !pCancel) { A.kioskPage = KIOSK_PAGE_LIST; A.kioskZone = KIOSK_ZONE_ACTIONS; }

        } else if (A.kioskPage == KIOSK_PAGE_ROMDIRS) {
            // Gestion : [0] « + ADD » (→ navigateur), [1..N] dossiers (FEU = retirer).
            const int total = 1 + (int)A.kioskRomDirs.size();
            if (step && (up || down)) {
                A.romDirSel += down ? 1 : -1;
                A.romDirSel = (A.romDirSel % total + total) % total;
            }
            if (okNow && !pOk) {
                if (A.romDirSel == 0) {                    // + ADD A FOLDER → navigateur
                    std::error_code ec2;
                    fs::path start = (!A.kioskRomDirs.empty() &&
                                      fs::is_directory(A.kioskRomDirs.back(), ec2))
                                         ? fs::path(A.kioskRomDirs.back()) : fs::path(disksDir);
                    fs::path abs = fs::absolute(start, ec2);   // absolu → « .. » remonte jusqu'à /
                    A.browseDir = (ec2 ? start : abs).lexically_normal().string();
                    kioskComputeShortcuts(A);
                    kioskScanBrowse(A, A.browseDir);
                    A.kioskPage = KIOSK_PAGE_BROWSE;
                } else {                                   // retirer ce dossier (croix ❌)
                    const int idx = A.romDirSel - 1;
                    if (idx >= 0 && idx < (int)A.kioskRomDirs.size())
                        A.kioskRomDirs.erase(A.kioskRomDirs.begin() + idx);
                    cfg.romDirs = A.kioskRomDirs;
                    saveConfig(A, exeDir, cfg, &machine, true);
                    kioskScanDisks(A, disksDir, machine.fdc.mountedPath());
                    A.kioskDiskSel = 0;
                    if (A.romDirSel > (int)A.kioskRomDirs.size())
                        A.romDirSel = (int)A.kioskRomDirs.size();
                }
            }
            // (B) revient à la liste, focus sur les actions (d'où l'on venait).
            if (caNow && !pCancel) { A.kioskPage = KIOSK_PAGE_LIST; A.kioskZone = KIOSK_ZONE_ACTIONS; }

        } else {   // KIOSK_PAGE_LIST — deux menus (intérieur / extérieur)
            const int nd = (int)A.kioskDisks.size();
            // GAUCHE/DROITE : bascule d'un menu à l'autre (front, non répété).
            static bool pSwap = false;
            const bool swapNow = left || right;
            if (swapNow && !pSwap)
                A.kioskZone = (A.kioskZone == KIOSK_ZONE_LIST) ? KIOSK_ZONE_ACTIONS
                                                               : KIOSK_ZONE_LIST;
            pSwap = swapNow;
            // HAUT/BAS : navigue DANS le menu focalisé (chacun boucle sur lui-même).
            // L1/R1 (pgUp/pgDn) : saut de PAGE dans la liste des jeux (défilement rapide).
            if (step) {
                if (A.kioskZone == KIOSK_ZONE_LIST) {
                    if (nd > 0) {
                        const int kPage = 10;   // taille du saut rapide L1/R1
                        int delta = 0;
                        if      (down) delta =  1;
                        else if (up)   delta = -1;
                        else if (pgDn) delta =  kPage;
                        else if (pgUp) delta = -kPage;
                        if (delta != 0) {
                            A.kioskDiskSel += delta;
                            if (delta == 1 || delta == -1)   // pas-à-pas : boucle
                                A.kioskDiskSel = (A.kioskDiskSel % nd + nd) % nd;
                            else                             // saut de page : butée
                                A.kioskDiskSel = std::max(0, std::min(nd - 1, A.kioskDiskSel));
                        }
                    }
                } else if (up || down) {
                    A.kioskActSel += down ? 1 : -1;
                    A.kioskActSel = (A.kioskActSel % 6 + 6) % 6;
                }
            }
            // FEU (A/Entrée) : déclenche l'item surligné du menu focalisé.
            if (okNow && !pOk) {
                if (A.kioskZone == KIOSK_ZONE_LIST) {
                    // Borne défensive : la liste peut avoir rétréci (dossier ROM
                    // débranché) sans que le curseur ait bougé depuis.
                    if (nd > 0) reqMount = A.kioskDisks[std::min(std::max(0, A.kioskDiskSel), nd - 1)];  // INSÉRER à chaud
                } else {
                    switch (A.kioskActSel) {
                        case 0: reqHardReset = true;              // Redémarrer
                                A.kioskDiskMenu = false; break;
                        case 1: A.kioskPage = KIOSK_PAGE_KEYS;    // Clavier & souris
                                A.kioskKeySel = 0; break;
                        case 2: A.kioskPage = KIOSK_PAGE_JOY;     // Joysticks (affectation)
                                A.kioskJoySel = 0; break;
                        case 3:                                   // Dossiers ROM (gestion)
                            if (kioskPruneRomDirs(A)) {           // retire les disparus + persiste
                                cfg.romDirs = A.kioskRomDirs;
                                saveConfig(A, exeDir, cfg, &machine, true);
                                kioskScanDisks(A, disksDir, machine.fdc.mountedPath());
                                A.kioskDiskSel = 0;   // la liste vient de rétrécir : l'ancien index pointerait hors du vecteur
                            }
                            A.romDirSel = 0;
                            A.kioskPage = KIOSK_PAGE_ROMDIRS; break;
                        case 4:                                   // Mode bureau (GUI)
                                A.kioskSwitchReq = 2;
                                A.kioskDiskMenu = false; break;
                        case 5: A.kioskPage = KIOSK_PAGE_QUIT; break;  // Quitter
                    }
                }
            }
            // (B) reprendre le jeu : ferme le menu.
            if (caNow && !pCancel) A.kioskDiskMenu = false;
        }
        pOk = okNow; pCancel = caNow;

        drawKioskDiskMenu(A, disksDir, machine.fdc.mountedPath());
    }
}

// Dessin : bandeau « machine gelée » et message transitoire du save-state.
static void drawOverlays(App& A) {
    Machine& machine = *A.machine;
    // A42 (2026-08-30) — BANDEAU PERMANENT « machine gelée ». Un CPU halté ne se
    // VOIT pas : l'écran garde sa dernière image, le son se tait, et plus rien ne
    // bouge — indiscernable d'un émulateur qui rame ou d'une démo qui attend une
    // touche. Sans ce bandeau, l'utilisateur ne peut que rapporter « ça plante »,
    // ce qui s'est produit sur No Cooper en Mega ST : double faute de bus, et
    // FIDÈLE (Hatari halte sur la MÊME instruction, cf. docs/CASE_STUDIES.md).
    // On dit donc les trois choses utiles : l'état, l'adresse fautive, et la seule
    // sortie possible (reset). Affiché AUSSI en borne — un visiteur devant un écran
    // figé mérite au moins de savoir que la machine est morte, pas lente.
    if (machine.cpu.halted()) {
        const Cpu68k::Fault f = machine.cpu.lastFault();
        const ImGuiIO& io = ImGui::GetIO();
        // Centré sur l'ÉCRAN ST, pas sur la fenêtre : au bureau l'écran n'occupe
        // qu'une partie de celle-ci, et un bandeau centré sur la fenêtre recouvrait
        // le menu et le panneau des disquettes (retour utilisateur du 2026-09-02).
        // Repli sur le centre de la fenêtre tant qu'aucune trame n'a dessiné l'écran.
        const ImVec2 c = A.stRectValid
            ? ImVec2((A.stRectX0 + A.stRectX1) * 0.5f, (A.stRectY0 + A.stRectY1) * 0.5f)
            : ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(c, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##haltmsg", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
                           ICON_FA_WARNING " Machine frozen - 68000 halted (double bus/address error)");
        if (f.valid)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                               "last fault: %s at $%08X, PC=$%06X",
                               f.write ? "writing" : "reading", f.addr, f.pc);
        // Diagnostic CIBLÉ quand l'adresse fautive est un périphérique absent du
        // profil courant : dire QUOI manquait et QUOI choisir, au lieu du conseil
        // générique. C'est ce qui manquait au rapport Stardust du 2026-09-02 —
        // l'utilisateur a conclu à un bug de NeoST là où le halt était fidèle.
        MissingHw miss{};
        if (f.valid && mmioNeedsBetterMachine(f.addr, machine.machineType(), miss)) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.40f, 1.0f),
                               "$%06X is %s - absent from the \"%s\" profile.",
                               f.addr & 0xFFFFFFu, miss.chip, machineName(machine.machineType()));
            ImGui::TextDisabled("This program needs machine = %s. Set it in Configuration "
                                "> Machine, then reset.", miss.needs);
        } else {
            ImGui::TextDisabled("Reset to restart. If a demo does this, check the machine profile first.");
        }
        ImGui::End();
    }

    // Overlay transitoire du save-state rapide (F5/F7) — coin bas-gauche, ~2,4 s.
    if (A.stateMsgFrames > 0) {
        --A.stateMsgFrames;
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(12, io.DisplaySize.y - 12), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##statemsg", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "%s", A.stateMsg.c_str());
        ImGui::End();
    }
}

// Dessin : rendu ImGui (et image kiosk/plein écran dessous).
static void renderImGui(App& A, Frame& F) {
    GlScreen& screen = *A.screen;
    int& fbw = F.fbw;
    int& fbh = F.fbh;
    int& kTop = F.kTop;
    int& kH = F.kH;
    int& kW = F.kW;
    ImGui::Render();
    if (A.kiosk || A.fullscreen) drawStKiosk(A, screen, fbw, fbh, kTop, kH, kW);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

// Glisser-déposer → requêtes de montage.
static void handleDrops(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    bool& reqRebuild = F.reqRebuild;
    std::string& reqMount = F.reqMount;
    std::string& reqMountCart = F.reqMountCart;
    std::string& reqMountGemdos = F.reqMountGemdos;
    std::string& reqMountAcsi = F.reqMountAcsi;
    // --- Glisser-déposer : le TYPE du support décide de la destination ----
    // Un DOSSIER = lecteur GEMDOS (C:), une image disquette = lecteur A, une image
    // de disque dur = ACSI, un TOS = ROM, une petite image = cartouche. Traité ICI,
    // juste avant la consommation des requêtes, pour que le montage prenne effet
    // dans la même trame. Ignoré en BORNE : sa config est figée (cf. saveConfig) et
    // un visiteur n'a de toute façon pas de bureau d'où glisser un fichier.
    if (!A.dropped.empty()) {
        if (!A.kiosk) for (const std::string& d : A.dropped) {
            std::error_code ec;
            if (fs::is_directory(d, ec)) { reqMountGemdos = d; continue; }
            std::string ext = fs::path(d).extension().string();
            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
            if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx" || ext == ".ipf") {
                reqMount = d;
            } else if (ext == ".hd" || ext == ".acsi" || ext == ".vhd" || ext == ".raw") {
                reqMountAcsi = d;
            } else if (ext == ".img" || ext == ".rom" || ext == ".bin") {
                // `.img` désigne aussi bien un TOS qu'une cartouche ou un disque dur
                // (roms/, carts/ et hd/ en sont tous pleins) : trancher sur la TAILLE
                // et l'en-tête, pas sur l'extension. Un TOS commence par BRA.S ($602E)
                // et tient en 512 Ko ; une cartouche fait au plus 128 Ko ; au-delà,
                // c'est un disque dur.
                const std::uintmax_t sz = fs::file_size(d, ec);
                uint8_t hdr[2] = { 0, 0 };
                { std::ifstream f(d, std::ios::binary);
                  if (f) f.read(reinterpret_cast<char*>(hdr), 2); }
                if (!ec && sz <= 512u * 1024u && hdr[0] == 0x60 && hdr[1] == 0x2E) {
                    cfg.rom = d; saveConfig(A, exeDir, cfg, &machine); reqRebuild = true;
                } else if (!ec && sz <= 128u * 1024u) {
                    reqMountCart = d;
                } else {
                    reqMountAcsi = d;
                }
            } else {
                A.stateMsg = "Dropped: unknown type (" +
                             fs::path(d).filename().string() + ")";
                A.stateMsgFrames = 150;
            }
        }
        A.dropped.clear();
    }
}

// Consommation des requêtes de montage / éjection (A, B, cartouche, disques durs).
static void consumeMediaRequests(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    bool& reqHardReset = F.reqHardReset;
    std::string& reqMount = F.reqMount;
    std::string& reqMountB = F.reqMountB;
    bool& reqEject = F.reqEject;
    bool& reqEjectB = F.reqEjectB;
    std::string& reqMountCart = F.reqMountCart;
    bool& reqEjectCart = F.reqEjectCart;
    std::string& reqMountGemdos = F.reqMountGemdos;
    std::string& reqMountAcsi = F.reqMountAcsi;
    bool& reqEjectGemdos = F.reqEjectGemdos;
    bool& reqEjectAcsi = F.reqEjectAcsi;
    // Disk Library : montage / éjection à chaud du lecteur A. La config n'est
    // persistée QUE si le montage a réussi — sinon une image corrompue serait
    // écrite dans neost.cfg et retentée à chaque boot.
    if (!reqMount.empty()) {
        if (machine.fdc.loadImage(reqMount)) {
            // La clé auto-branchée pour l'image PRÉCÉDENTE s'en va d'abord (cf.
            // autoDongleRetract) : elle n'a rien à faire dans le jeu suivant.
            autoDongleRetract(A, machine, cfg);
            // Clé du jeu (disks/dongles.txt) : on ne remplit que les emplacements VIDES.
            if (cfg.autoDongle) {
                std::string plugged;
                for (const auto& r : neost::matchDongleRules(A.loadDongleTable(), reqMount)) {
                    if (r.cart) {
                        if (machine.dongle.attached() || !machine.setDongle(r.key)) continue;
                        static const char* const kn[] = { "", "cubase2", "cubase3", "auto", "notator" };
                        cfg.dongle = kn[int(r.key)]; A.autoCartKey = r.key;
                        plugged += std::string(plugged.empty() ? "" : ", ") + "cartridge key " + cfg.dongle;
                    } else {
                        if (machine.ports.at(r.port) != PortDevices::Device::None || !machine.plugPort(r.port, r.dev)) continue;
                        std::string* slots[] = { &cfg.joy0, &cfg.joy1, &cfg.rs232, &cfg.printer, &cfg.cartbutton };
                        *slots[int(r.port)] = PortDevices::id(r.dev);
                        A.autoPortDev[int(r.port)] = r.dev;
                        plugged += std::string(plugged.empty() ? "" : ", ") + PortDevices::label(r.dev) + " on " + PortDevices::portId(r.port);
                    }
                }
                if (!plugged.empty()) {
                    A.stateMsg = "Auto-plugged: " + plugged + " (disks/dongles.txt)"; A.stateMsgFrames = 240;
                    if (!A.kiosk && !A.kioskLaunched) saveConfig(A, exeDir, cfg, &machine);
                }
            }
            // En BORNE, ne pas mémoriser la disquette insérée par un visiteur : même
            // si saveConfig(A, ) refuse d'écrire ici, salir `cfg` suffirait à faire fuir
            // ce disk= lors d'un saveConfig(A, force=true) ultérieur (dossiers ROM,
            // joysticks), qui réécrit TOUT le fichier — et la borne ne repartirait
            // plus sur son jeu d'origine (invariant « config figée », cf. DEV.md).
            if (!A.kiosk && !A.kioskLaunched) {
                cfg.disk = reqMount; saveConfig(A, exeDir, cfg, &machine);
            }
        } else {
            A.stateMsg = "Unreadable floppy image"; A.stateMsgFrames = 120;
        }
    }
    if (reqEject) {
        machine.fdc.eject();
        autoDongleRetract(A, machine, cfg);   // la clé partait avec la disquette
        cfg.disk.clear(); saveConfig(A, exeDir, cfg, &machine);
    }
    // Lecteur B : même discipline que A. Le cœur le gère depuis toujours
    // (Fdc::loadImage(path, 1), option --diskb du headless) ; seule la GUI
    // l'ignorait — or plusieurs jeux ne DÉMARRENT qu'avec leur disque 2 monté
    // (Lethal Xcess) et les jeux 2 disquettes réclament d'en changer.
    if (!reqMountB.empty()) {
        if (machine.fdc.loadImage(reqMountB, 1)) {
            if (!A.kiosk && !A.kioskLaunched) {
                cfg.diskb = reqMountB; saveConfig(A, exeDir, cfg, &machine);
            }
        } else {
            A.stateMsg = "Unreadable floppy image (B)"; A.stateMsgFrames = 120;
        }
    }
    if (reqEjectB) {
        machine.fdc.eject(1);
        cfg.diskb.clear(); saveConfig(A, exeDir, cfg, &machine);
    }
    // Cart Library : branchement / éjection à chaud du port cartouche.
    if (!reqMountCart.empty()) {
        // VALIDER AVANT de libérer : loadCart échoue sur un fichier illisible ou de
        // plus de 128 Ko (un .img volumineux dans carts/ est banal). Démonter le HD
        // GEMDOS d'abord laissait alors la machine SANS cartouche ET SANS C:, avec
        // cfg.gemdos vidé — donc le disque dur perdu au prochain saveConfig, sans
        // message ni reset pour l'expliquer.
        if (machine.loadCart(reqMountCart)) {
            if (machine.gemdos.active()) {  // $FA0000 occupé par la cartouche système GEMDOS
                machine.gemdos.unmount();
                cfg.gemdos.clear();
            }
            cfg.cart = reqMountCart; saveConfig(A, exeDir, cfg, &machine);
            reqHardReset = true;       // le TOS sonde le port cartouche au boot
        } else {
            A.stateMsg = "Unreadable cartridge (max 128 KB)"; A.stateMsgFrames = 120;
        }
    }
    if (reqEjectCart) {
        machine.ejectCart();
        cfg.cart.clear(); saveConfig(A, exeDir, cfg, &machine);
        reqHardReset = true;           // relance sans la ROM $FA0000
    }
    // Disque dur (menu Machine → Disque dur) : GEMDOS HD et image ACSI. Chaque
    // opération force un hard reset — le TOS ne (re)sonde les disques qu'au boot.
    if (!reqMountGemdos.empty()) {
        // Même ordre que la cartouche : setDirectory échoue sur une simple faute de
        // frappe, et retirer la cartouche AVANT la sortait du bus SANS reset — un
        // programme qui tournait depuis $FA0000 partait alors dans le décor.
        const std::string gemHost = A.resolvePath(reqMountGemdos);
        const bool hadCart = !machine.bus.mountedCartPath().empty();
        if (machine.gemdos.setDirectory(gemHost)) {
            // setDirectory a DÉJÀ remplacé $FA0000 par la cartouche système (même
            // stockage) : appeler ejectCart ici viderait ce qu'on vient d'installer
            // et laisserait le vecteur trap #1 pointer sur un port dépeuplé. On se
            // contente donc d'oublier la cartouche utilisateur côté config.
            if (hadCart) cfg.cart.clear();
            cfg.gemdos = reqMountGemdos; saveConfig(A, exeDir, cfg, &machine);
            reqHardReset = true;
        } else {
            A.stateMsg = "GEMDOS folder not found"; A.stateMsgFrames = 120;
        }
    }
    if (reqEjectGemdos) {
        machine.gemdos.unmount();
        cfg.gemdos.clear(); saveConfig(A, exeDir, cfg, &machine);
        reqHardReset = true;
    }
    if (!reqMountAcsi.empty()) {
        if (machine.fdc.mountAcsi(A.resolvePath(reqMountAcsi))) {
            std::fprintf(stderr, "[main] ACSI : %d partition(s)\n",
                         machine.fdc.acsiPartitionCount());
            cfg.acsi = reqMountAcsi; saveConfig(A, exeDir, cfg, &machine);
            reqHardReset = true;
        } else {
            A.stateMsg = "Unreadable ACSI image"; A.stateMsgFrames = 120;
        }
    }
    if (reqEjectAcsi) {
        machine.fdc.unmountAcsi();          // vide TOUTES les cibles (slot 2 compris)
        cfg.acsi.clear(); saveConfig(A, exeDir, cfg, &machine);
        A.usatanApply();                      // ré-attache l'UltraSatan + remonte le slot 2
        reqHardReset = true;
    }
}

#else
// Dessin sans ImGui : l'écran seul.
static void renderNoImGui(App& A, Frame& F) {
    GlScreen& screen = *A.screen;
    int& fbw = F.fbw;
    int& fbh = F.fbh;
    int& kTop = F.kTop;
    int& kH = F.kH;
    int& kW = F.kW;
    if (A.kiosk || A.fullscreen) drawStKiosk(A, screen, fbw, fbh, kTop, kH, kW);
    else GlScreen::blitTexFullscreen(crtApply(A, screen, fbw, fbh));  // repli sans ImGui + CRT
}

#endif

// Requêtes : moniteur, rebuild, resets (en fin de tour, hors rendu ImGui).
static void applyRequests(App& A, Frame& F) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    bool& reqReset = F.reqReset;
    bool& reqHardReset = F.reqHardReset;
    bool& reqRebuild = F.reqRebuild;
    int& reqMonitor = F.reqMonitor;
    // Changement de moniteur (couleur/mono) → hard reset pour que TOS
    // re-détecte la résolution au boot.
    if (reqMonitor >= 0 && (reqMonitor == 1) != machine.mfp.colorMonitor()) {
        machine.mfp.setColorMonitor(reqMonitor == 1);
        machine.reset();
        cfg.mono = (reqMonitor == 0);   // mémorise le mode
        saveConfig(A, exeDir, cfg, &machine);
    }
    // Application des requêtes (en fin de boucle, hors rendu ImGui) :
    if (reqRebuild)   A.applyConfig();       // modèle/RAM/cœur/ROM → reconfig à chaud
    if (reqHardReset) machine.hardReset(); // power-cycle (RAM effacée, boot à froid)
    if (reqReset)     machine.reset();     // reset « doux » (RAM conservée)
}

// Présentation : diagnostic NEOST_WBAND_DIAG (bandes horizontales).
static void diagWideBands(App& A) {
    GLFWwindow* const window = A.window;
    // NEOST_WBAND_DIAG=1 : traque, dans l'image RÉELLEMENT AFFICHÉE, les bandes
    // horizontales qui tranchent sur leurs deux voisines. On ne relit qu'une BANDE
    // VERTICALE ÉTROITE (une bande pleine largeur la traverse forcément) : le coût
    // reste négligeable là où relire toute la fenêtre coûterait 15 Mo par trame.
    // Seules les APPARITIONS sont signalées, pour que les lignes légitimement
    // contrastées (séparateurs du bandeau de jeu) ne noient pas le diagnostic.
    // ⚠ À utiliser CRT ÉTEINT : les scanlines font trancher une ligne sur deux.
    { static const bool dz = std::getenv("NEOST_WBAND_DIAG") != nullptr;
      if (dz) {
        int ww = 0, wh = 0;
        glfwGetFramebufferSize(window, &ww, &wh);
        const int sw = 16;
        if (ww > sw && wh > 4) {
            static std::vector<unsigned char> strip;
            strip.resize(size_t(sw) * wh * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(ww / 2 - sw / 2, 0, sw, wh, GL_RGB, GL_UNSIGNED_BYTE, strip.data());
            static std::vector<uint8_t> prevOdd;
            if ((int)prevOdd.size() != wh) prevOdd.assign(wh, 0);
            auto rowAt = [&](int y) { return strip.data() + size_t(y) * sw * 3; };
            for (int y = 1; y < wh - 1; ++y) {
                const unsigned char* c = rowAt(y);
                const unsigned char* u = rowAt(y - 1);
                const unsigned char* d = rowAt(y + 1);
                int du = 0, dd = 0;
                for (int x = 0; x < sw; ++x) {
                    const int i = x * 3;
                    if (c[i] != u[i] || c[i+1] != u[i+1] || c[i+2] != u[i+2]) ++du;
                    if (c[i] != d[i] || c[i+1] != d[i+1] || c[i+2] != d[i+2]) ++dd;
                }
                const bool odd = (du > sw * 3 / 4) && (dd > sw * 3 / 4);
                if (odd && !prevOdd[y])
                    std::fprintf(stderr, "[wband] f=%ld y=%d (sur %d) rvb=%02X%02X%02X\n",
                                 A.emuFrame, y, wh, c[0], c[1], c[2]);
                prevOdd[y] = odd ? 1 : 0;
            }
        }
      } }
}

// Présentation : capture de la fenêtre composée (--shot-window).
static void captureWindow(App& A) {
    GLFWwindow* const window = A.window;
    // Capture de la FENÊTRE (cf. App::shotWinPrefix) — AVANT le swap, donc sur
    // l'image réellement composée : échelle, filtrage et passe CRT compris.
    if (!A.shotWinPrefix.empty() && A.shotWinDone < A.shotWinMax
        && A.emuFrame >= A.shotWinFrom) {
        int ww = 0, wh = 0;
        glfwGetFramebufferSize(window, &ww, &wh);
        if (ww > 0 && wh > 0) {
            std::vector<unsigned char> buf(size_t(ww) * wh * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, ww, wh, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
            char path[512];
            std::snprintf(path, sizeof path, "%s%05d.ppm",
                          A.shotWinPrefix.c_str(), A.shotWinDone);
            if (std::FILE* f = std::fopen(path, "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", ww, wh);
                // glReadPixels rend l'origine EN BAS : on réécrit ligne par ligne
                // de haut en bas pour une PPM lisible par les outils du dépôt.
                for (int y = wh - 1; y >= 0; --y)
                    std::fwrite(buf.data() + size_t(y) * ww * 3, 1, size_t(ww) * 3, f);
                std::fclose(f);
            }
            ++A.shotWinDone;
        }
    }
}

// Présentation : sommeil jusqu'à l'échéance de la prochaine trame émulée.
static void sleepUntilNextFrame(App& A) {
    using clock = std::chrono::steady_clock;
    auto& emuNext = A.emuNext;
    // Dort jusqu'à l'échéance de la prochaine trame émulée (posée par la boucle
    // de rattrapage ci-dessus). En retard → pas de sommeil, le rattrapage du
    // prochain tour exécutera les trames dues. Le sommeil est plafonné à une
    // trame : on garde le GUI réactif même si l'horloge dérive.
    const auto now = clock::now();
    if (now < emuNext) {
        auto wake = emuNext;
        const auto cap = now + std::chrono::milliseconds(20);
        if (wake > cap) wake = cap;
        std::this_thread::sleep_until(wake);
    }
}

// Un tour = entrées hôte → trames émulées dues → dessin → requêtes → présentation.
// Chaque phase est une fonction ci-dessus ; l'ordre des appels EST celui de
// l'ancienne boucle d'un seul tenant.
void appLoop(App& A) {
    GLFWwindow* const window = A.window;
    while (!glfwWindowShouldClose(window)) {
        Frame F;                               // état du tour (cf. struct Frame)
        glfwPollEvents();                      // les transitions de boutons → onMouseButton

        applyHostRequests(A);
        feedMouse(A);
        pollHotkeys(A);
        feedJoysticks(A);
        finishInputs(A);

        runDueFrames(A);
        handleQuickState(A);

        beginDraw(A, F);
#if defined(NEOST_WITH_IMGUI)
        prepareConfigUi(A, F);
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (!A.kiosk && !A.fullscreen) drawDesktopChrome(A, F);   // plein écran : aucun chrome ImGui

        // KIOSK : aucun chrome dessiné, mais on garde le nœud d'ancrage VIVANT
        // (KeepAliveOnly ne soumet rien de visible). Sans ça, un aller-retour
        // GUI → kiosk → GUI rendrait toutes les fenêtres flottantes.
        if (A.kiosk || A.fullscreen) renderDockSpace(A, false);

        // --- Kiosk : menu in-game (START manette ou F9), jeu en PAUSE ------------
        // Modèle « vraie machine » : (A) INSÈRE la disquette choisie SANS jamais
        // rebooter (le jeu en cours continue) ; (X) REDÉMARRE la machine (bouton
        // reset) → reboot sur la disquette insérée ; (Y) quitte avec confirmation.
        if (A.kiosk) runKioskMenu(A, F);

        drawOverlays(A);

        renderImGui(A, F);

        handleDrops(A, F);
        consumeMediaRequests(A, F);
#else
        renderNoImGui(A, F);
#endif
        applyRequests(A, F);
        diagWideBands(A);
        captureWindow(A);
        glfwSwapBuffers(window);

        sleepUntilNextFrame(A);
    }
}

void appShutdown(App& A) {
    Machine& machine = *A.machine;
    Config& cfg = A.cfg;
    const std::string& exeDir = A.exeDir;
    GLFWwindow* const window = A.window;
    Mt32Synth& mt32 = *A.mt32;
    Audio& audio = *A.audio;
    // Mémorise le dernier ROM, la disquette/cartouche montée et le moniteur.
    // (En mode harnais, saveConfig est gelé centralement — cf. A.harnessRun.)
    cfg.disk = machine.fdc.mountedPath();
    cfg.cart = machine.bus.mountedCartPath();
    cfg.mono = !machine.mfp.colorMonitor();
    cfg.showHex = A.showHex; cfg.showCpu = A.showCpu;
    cfg.showJoy = A.showJoy; cfg.showCfg = A.showCfg; cfg.showFloppy = A.showFloppy;
    cfg.showKbd = A.showKbd;
    saveConfig(A, exeDir, cfg, &machine);

#if defined(NEOST_WITH_IMGUI)
    // Écrit imgui.ini avant l'arrêt → garantit la sauvegarde de la taille de fenêtre
    // (et des positions de sous-fenêtres) même si rien d'autre n'a marqué les réglages.
    // Pas en harnais : même règle que saveConfig, un run de test ne laisse rien.
    if (!A.harnessRun && ImGui::GetIO().IniFilename)
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    // Arrêt : plus aucun octet MIDI vers des objets en cours de destruction (sink → midiOut/mt32/gm).
    machine.midi.setMidiSinkTimed({});
    audio.setMt32(nullptr);
    audio.setGm(nullptr);
    mt32.close();
    A.gm->close();
    glfwDestroyWindow(window);
    glfwTerminate();
}
