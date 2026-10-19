// =============================================================================
//  App.hpp — l'ÉTAT du frontend fenêtré, en un seul objet.
//
//  Pourquoi (chantier A9) : main.cpp portait 84 variables `g_*` à liaison
//  interne. Chacune était justifiée une par une — un callback GLFW n'a pas de
//  paramètre où passer un contexte, une requête posée par un menu se consomme en
//  fin de boucle — mais leur SOMME rendait le fichier inextricable : aucune
//  fonction ne pouvait sortir de main.cpp sans emporter la moitié du tas, et
//  chaque page d'interface ajoutée y atterrissait faute d'ailleurs où aller
//  (+286 lignes pour la seule page MIDI, entre le 2026-08-27 et le 2026-08-29).
//
//  Ce fichier ne CHANGE rien au modèle : ce sont les mêmes variables, avec les
//  mêmes valeurs initiales et les mêmes commentaires — regroupées sous un
//  propriétaire nommé. Ce qu'il rend possible : une fonction d'interface prend
//  `App&` et vit dans son propre fichier.
//
//  Discipline (héritée de MediaPages, généralisée) : une page ne FAIT rien. Elle
//  lit un état et pose une REQUÊTE (`*Req`) que la boucle principale consomme à
//  une frontière de trame. C'est ce qui permet à une page de vivre ailleurs que
//  dans la boucle qui l'exécute.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include "util/DesktopPointer.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Symbols.hpp"       // SymbolTable (débogueur)
#include "gui/AppConfig.hpp"      // Config (neost.cfg)
#include "gui/CrtEffectStack.h"   // CrtEffectStack, CrtParams
#include "io/CartridgeKey.hpp"    // CartridgeKey::Model (auto-plug)
#include "io/DongleTable.hpp"     // neost::DongleRule (auto-plug par image)
#include "io/PortDevices.hpp"     // PortDevices::Device/Port (auto-plug)

// Périphériques HÔTES : possédés par pointeur pour que cet en-tête n'entraîne pas
// tout le cœur et toute la pile audio/réseau dans chaque fichier d'interface qui
// l'inclut. C'est aussi ce qui permet de les construire APRÈS App, quand la config
// est lue (une Machine ne se construit pas sans savoir sa RAM ni son modèle).
struct GLFWwindow;
struct GlScreen;
class Audio;
class DriveSound;
class HayesModem;
class Ikbd;
class Machine;
class MidiInHost;
class MidiOutHost;
class Mt32Synth;
class GmSynth;
class NetBackend;
class NetBackendNull;
class SlirpBackend;

// Pages du menu borne (plein écran, piloté à la manette).
enum { KIOSK_PAGE_LIST = 0, KIOSK_PAGE_KEYS = 1, KIOSK_PAGE_QUIT = 2,
       KIOSK_PAGE_BROWSE = 3, KIOSK_PAGE_ROMDIRS = 4, KIOSK_PAGE_JOY = 5 };
// Page liste = DEUX menus qu'on bascule avec gauche/droite : INTÉRIEUR (liste des
// jeux) et EXTÉRIEUR (Redémarrer / Clavier & souris / Joysticks / Dossiers ROM /
// Mode bureau / Quitter). zone = quel menu a le focus ; le FEU valide l'item
// surligné du menu focalisé.
enum { KIOSK_ZONE_LIST = 0, KIOSK_ZONE_ACTIONS = 1 };

struct App {
    // ─── Fenêtre hôte ────────────────────────────────────────────────────────
    GLFWwindow* window = nullptr;          // fenêtre GLFW (pour l'interroger/la poser)

    // ─── Mode borne ──────────────────────────────────────────────────────────
    // Mode kiosk (borne/expo) : plein écran sans chrome, config figée, sortie par
    // chord. Activé par --kiosk. Consulté par saveConfig (gel de la config).
    bool kiosk = false;
    // Lancé en borne (--kiosk) — invariant de DÉPLOIEMENT, distinct de `kiosk` qui
    // suit la bascule à chaud. Sans lui, un aller-retour bureau/borne rendrait la main à tous
    // les saveConfig du GUI pour le reste du processus : la borne repartirait sur le
    // disque et les réglages du dernier visiteur au lieu de sa configuration d'expo.
    bool kioskLaunched = false;
    int  kioskMonitor = 0;                 // --kiosk-monitor N (0 = principal)
    // Bascule GUI ⇄ kiosk À CHAUD (Ctrl+Alt+F, menu Machine, action « DESKTOP MODE » du menu
    // borne). La demande est POSÉE ici puis appliquée en tête de boucle, à une
    // frontière de trame : la bascule prend un instantané de la machine et le
    // restaure derrière elle, et Machine::loadState n'accepte que l'entre-deux-trames.
    //   0 = rien à faire · 1 = passer en kiosk · 2 = revenir au GUI.
    int  kioskSwitchReq = 0;
    bool resumeAtariReq = false;           // Alt+Entrée ferme d'abord le menu borne

    // ─── Save-state rapide (F5 / F7) ─────────────────────────────────────────
    bool saveStateReq = false;             // F5 latché dans onKey (cf. Alt+Entrée)
    bool loadStateReq = false;             // F7 latché dans onKey
    std::string stateMsg;                  // message transitoire affiché en overlay
    int         stateMsgFrames = 0;

    // ─── Cadrage ─────────────────────────────────────────────────────────────
    // Zoom ADAPTATIF (cale le contenu réel sur la hauteur disponible) : ON par
    // défaut. S'applique aux DEUX modes — plein écran kiosk (viewport GL) et fenêtre
    // « Atari ST Screen » du bureau (UV de l'image) — pour que le bureau présente le
    // même cadrage que la borne. OFF = cadre complet fixe (pillarbox, rien ne déborde).
    // Bascule : F10 en kiosk (où les touches ne vont pas au ST), menu Résolution au
    // bureau (F10 y est une touche du ST, on ne la confisque pas).
    bool autoZoom = true;

    // ─── Sensibilité de la souris ÉMULÉE ─────────────────────────────────────
    // Facteur appliqué au delta de la souris HÔTE avant qu'il n'atteigne l'IKBD.
    // Pourquoi c'est nécessaire : le ST compte les crans de sa souris mécanique
    // (~200 points/pouce) et le TOS n'a aucun réglage d'accélération digne de ce nom.
    // Une souris moderne à 1600, 3200 voire 8000 dpi envoie donc jusqu'à 40× plus de
    // pas pour le même geste : le pointeur du bureau GEM traverse l'écran au moindre
    // mouvement et devient inutilisable. 1,0 = un pas hôte pour un pas ST (comportement
    // d'origine) ; en dessous, on ralentit. S'applique au bureau ET à la borne — c'est
    // le seul chemin de MOUVEMENT vers le ST (les autres appels à Ikbd::mouseEvent ne
    // portent que les boutons).
    float mouseSpeed = 1.0f;

    // ─── Menu borne plein écran (START manette ou F9) ────────────────────────
    // Le jeu est MIS EN PAUSE tant que le menu est ouvert (cf. boucle d'émulation).
    // Modèle « comme une vraie machine » :
    //   · INSÉRER une disquette (A) = on échange le contenu du lecteur — JAMAIS de
    //     reboot (exactement comme glisser une disquette : le jeu en cours continue).
    //   · REDÉMARRER la machine (X) = bouton reset explicite → la machine reboote sur
    //     la disquette actuellement insérée. C'est le SEUL moyen de relancer.
    //   · QUITTER NeoST (Y) = avec confirmation (page QUIT).
    // `kioskDiskMenu` est consulté par onKey : pendant le menu, les touches de
    // navigation ne sont PAS transmises au ST.
    bool kioskDiskMenu = false;            // menu ouvert
    int  kioskPage     = KIOSK_PAGE_LIST;
    int  kioskDiskSel  = 0;                // index disquette sélectionnée (menu INTÉRIEUR)
    int  kioskZone     = KIOSK_ZONE_LIST;
    int  kioskActSel   = 0;                // index action (menu EXTÉRIEUR, 0..5)
    int  kioskKeySel   = 0;                // page clavier : touche/clic sélectionné
    int  kioskJoySel   = 0;                // page joysticks : manette sélectionnée
    std::vector<std::string> kioskDisks;   // chemins listés à l'ouverture
    // Page « Clavier & souris » : un appui (A) envoie la touche/clic au ST puis la
    // relâche après quelques trames (frappe brève). Injection différée gérée dans la
    // boucle. -1 / false = rien à relâcher. La page CLAVIER ne met PAS le jeu en
    // pause (sinon la touche envoyée ne serait jamais traitée par le jeu).
    int  kioskKeyRelease = -1;             // scancode ST à relâcher (sinon -1)
    bool kioskMouseRelL  = false;          // clic gauche à relâcher
    bool kioskMouseRelR  = false;          // clic droit à relâcher
    int  kioskInjectHold = 0;              // trames restantes avant relâche
    // Dossiers ROM/disques ADDITIONNELS (action « ADD ROM FOLDER ») : scannés en PLUS
    // de disks/ pour la liste des jeux, persistés dans neost.cfg (kiosk_romdir=).
    std::vector<std::string> kioskRomDirs;
    // Page « ROM FOLDERS » (gestion). Entrée 0 = « + ADD A FOLDER » (ouvre le
    // navigateur), entrées 1..N = dossiers configurés, chacun avec une croix ❌.
    int romDirSel = 0;
    // Page « SELECT ROM FOLDER » (navigateur de répertoires piloté à la manette,
    // plein écran) : browseDir = dossier courant (ABSOLU → « .. » remonte jusqu'à /) ;
    // browseSubdirs = ses sous-dossiers triés. Raccourcis (racine /, home, volumes
    // montés) calculés à l'ouverture. browseSel indexe, dans l'ordre :
    //   [0] valider ce dossier · [1] .. parent · [2..2+S) raccourcis · [2+S..] sous-dossiers.
    std::string browseDir;
    std::vector<std::string> browseSubdirs;
    std::vector<std::string> browseShortcutPaths;    // cibles des raccourcis
    std::vector<std::string> browseShortcutLabels;   // libellés (icône FA + nom)
    int browseSel = 0;

    // ─── Configuration ───────────────────────────────────────────────────────
    // Image PRISTINE de la configuration, telle que lue au démarrage : c'est elle que
    // le mode borne réécrit (cf. saveConfig), et non la structure de travail salie en
    // séance.
    neost::appconfig::Config cfgPristine;
    // Mode HARNAIS (--run-frames, A9a 2026-08-27) : un run de test ne doit laisser
    // AUCUNE trace dans l'état utilisateur. Le gel est CENTRAL parce que saveConfig a
    // une demi-douzaine d'appelants — démarrage (rom=), résolution MIDI à la première
    // trame (le site qui, lancé d'un contexte sandboxé où CoreMIDI échoue, remettait
    // silencieusement midi_out_port=0 — incident déjà payé), montages, sortie. Sans ce
    // gel, brancher le boot GUI dans run_all.py réécrivait le neost.cfg du développeur
    // à chaque palier (constaté : rom= et rtc= écrasés au premier essai).
    bool harnessRun = false;

    // ─── Harnais d'injection (--run-frames / --scancode-at / --joy-at / --mouse-at) ──
    //   --scancode-at N HEX[,HEX…]  scancodes ST BRUTS à partir de la trame émulée N
    //   --key-hold N                trames d'appui (défaut 2, cf. headless)
    //   --joy-at N VAL              état joystick port 1 TENU à partir de la trame N
    // ⚠ Sémantique --joy-at ≠ headless : ici l'état est RE-POSÉ à chaque trame ≥ N,
    // parce que le GUI écrase le port à chaque tour avec l'état des manettes RÉELLES —
    // une pose unique serait perdue au tour suivant. « Tenu » est de toute façon ce
    // qu'un harnais veut (le bouton reste enfoncé).
    long        runFrames = -1;            // -1 = illimité (comportement normal)
    std::string shotPath;                  // --shot PATH (PPM avant la sortie)
    long        emuFrame = 0;              // trames ÉMULÉES (croissant, sites nominaux)
    int         keyHold  = 2;
    std::vector<std::pair<long, std::vector<uint8_t>>> scanAt;
    std::vector<std::pair<long, uint8_t>> joyAt;
    //   --mouse-at N "SCRIPT" : souris scriptée (1 token = 1 trame émulée), même
    //   syntaxe que le headless : L/R/U/D = ±8 px, 1/2/3 = clics, '.' = idle.
    //   ⚠ Se SUPERPOSE aux événements de la vraie souris (le GUI reste vivant) : un run
    //   de harnais suppose qu'on ne touche pas la fenêtre pendant ce temps.
    std::vector<std::pair<long, std::string>> mouseAt;

    // ─── Géométrie de la fenêtre principale (persistée dans imgui.ini) ───────
    int  iniWinW = 0, iniWinH = 0;         // taille relue depuis imgui.ini
    bool iniWinValid = false;
    bool dockSeeded = false;
    int  winX = 0, winY = 0, winW = 1280, winH = 860;
    // Lancé en --kiosk, on n'a JAMAIS été fenêtré : winX/winY resteraient à (0,0) et la
    // première sortie de borne collerait la fenêtre à l'origine de l'écran VIRTUEL
    // (donc sur le mauvais moniteur en bi-écran, barre de titre hors champ sous X11).
    // D'où ce drapeau : tant qu'il est faux, la sortie de borne centre la fenêtre.
    bool winGeomValid = false;
    bool fullscreen = false;               // plein écran avec souris capturée
    bool fullscreenToggleReq = false;      // Alt+Entrée, appliqué entre deux trames

    // ─── Entrées : souris, clavier, manettes ─────────────────────────────────
    Ikbd* ikbd = nullptr;                  // cible des callbacks clavier/souris GLFW
    bool  mouseCaptured = false;           // souris capturée → entrées dirigées vers le ST
    bool desktopMouse = false;            // position hôte absolue pour GEM, sans capture
    neost::DesktopPointer desktopPointer;
    bool  mouseCaptureToggleReq = false;   // molette / Ctrl+Alt+G → bascule dans la boucle
    bool  dbgMouse = false;                // NEOST_DEBUG_MOUSE=1 → trace les paquets souris
    bool  dbgJoy   = false;                // NEOST_DEBUG_JOY=1 → trace l'état brut des manettes
    bool  kbdJoy = false;                  // émulation joystick au clavier (flèches + Ctrl droit)
    int   kbdJoyPort = 1;                  // port ST visé par l'émulation clavier (0/1)
    bool  port0Auto = false;               // port 0 : "auto" (2e manette y va seule) vs "mouse"
    bool  port0Joystick = false;           // calculé chaque trame : un joystick OCCUPE le port 0
    float joyDeadzone = 0.30f;             // zone morte centrale des sticks analogiques [0,0.95]
    uint8_t lastJoy0 = 0, lastJoy1 = 0;    // dernier octet composé posé sur l'IKBD (fenêtre Joystick)
    // Affectation des manettes hôte aux ports ST, par GUID (stable au rebranchement —
    // le jid GLFW peut changer). Absente de la table = AUTO. Éditée dans le menu kiosk
    // « Joysticks », persistée dans neost.cfg (joymap=).
    std::map<std::string, int8_t> joyRoleByGuid;
    bool joyCfgDirty = false;              // un réglage joystick a changé → resauver neost.cfg

    // ─── Fenêtres masquables ─────────────────────────────────────────────────
    bool showHex = true, showCpu = true;   // fenêtres d'inspection
    bool showJoy = false;                  // fenêtre joystick (visualisation live)
    bool showKbd = false;                  // fenêtre clavier virtuel (photo pic/, touches cliquables)
    bool showCfg = true;                   // fenêtre Configuration
    bool showFloppy = true;                // fenêtre indépendante des disquettes
    int  cfgPage = 0;                      // page courante de la fenêtre Configuration
    bool profilesDirty = false;            // un profil vient d'être écrit/supprimé → relire le dossier
    std::vector<std::string> dropped;      // chemins glissés-déposés, consommés dans la boucle

    // ─── Effets CRT (façade moniteur) ────────────────────────────────────────
    // Passe FBO shader appliquée à l'écran ST. Opt-in, à échec gracieux (cf.
    // gui/CrtEffectStack). En kiosk la config est figée → crtOn / crtParams viennent
    // du neost.cfg (ou de --crt/--crt-preset).
    neost::CrtEffectStack crt;
    neost::CrtParams      crtParams;
    bool crtOn   = false;                  // effets CRT activés
    bool crtInit = false;                  // initialize() déjà tenté (une seule fois)
    bool showCrt = false;                  // fenêtre de réglages CRT visible (fenêtré)

    // ─── Débogueur (fenêtré) : breakpoints PC + pause/continue/step ──────────
    bool showDbg      = false;             // fenêtre « Débogueur » visible
    bool dbgPaused    = false;             // émulation gelée (breakpoint atteint ou pause manuelle)
    bool dbgStepFrame = false;             // requête « avancer d'une trame » (traitée dans la boucle)
    bool dbgStepInstr = false;             // requête « avancer d'une instruction » (idem)
    SymbolTable symbols;                   // table de symboles (noms ↔ adresses)

    // ─── Ancrage (docking) ───────────────────────────────────────────────────
    // Les fenêtres de debug deviennent des ONGLETS d'une disposition persistante au
    // lieu d'une pile de fenêtres qui se recouvrent. Exige la branche `docking` de
    // Dear ImGui (IMGUI_HAS_DOCK) — cf. extern/imgui, sous-module épinglé.
    bool dockOn    = true;                 // mode ancré actif (persisté : neost.cfg dock=)
    bool dockReset = false;                // requête « réinitialiser la disposition »
    unsigned int dockId = 0;               // ImGuiID du nœud racine du dockspace

    // ─── Auto-plug (disks/dongles.txt) : mémoire de ce QU'ON a branché ───────
    // Le montage à chaud enchaîne les jeux (ludothèque, borne). L'auto-plug ne
    // remplissait que les emplacements vides et ne retirait JAMAIS rien : la clé du jeu
    // précédent restait en place. Une clé Leader Board oubliée sur le port 1 force
    // HAUT+BAS en permanence (PortDevices::joyOverlay) — la manette du jeu suivant est
    // cassée, et cfg.joy1 ayant été persisté, ça survit au redémarrage.
    // On note donc ce que l'auto-plug a posé, pour le retirer au montage suivant. On ne
    // retire QUE ce qu'on a posé ET qui n'a pas bougé depuis : la page Dongles reste
    // souveraine.
    PortDevices::Device autoPortDev[int(PortDevices::Port::Count)] = {};
    CartridgeKey::Model autoCartKey = CartridgeKey::Model::None;

    // ─── La SESSION : la machine émulée et ses périphériques hôtes ───────────
    // Tout ce que main() tenait en variables locales, et que la boucle ET les
    // services (ci-dessous) doivent tous les deux atteindre. Construits par
    // appInit() dans cet ordre : chemins, config, machine, hôtes, fenêtre.
    std::string exeDir;                    // dossier de l'exécutable (résolu, jamais argv[0] nu)
    std::string disksDir, cartsDir, hdDir, gemdosDir, romsDir;
    neost::appconfig::Config cfg;          // la config de TRAVAIL (cf. cfgPristine)
    // ⚠ L'ORDRE de ces membres est un CONTRAT, pas une présentation : c'est l'ordre
    // de construction de l'ancien main(), et la destruction le déroule À L'ENVERS.
    // `audio` doit mourir AVANT `drive`, `mt32` et `machine` — son thread mixe
    // DriveSound et le MT-32 par pointeur brut et lit machine.psg/dmasnd ; détruit
    // en dernier, il jouerait une trame sur des objets déjà morts (bug attrapé par
    // le bug hunt du 2026-08-30 : la première version de cette structure avait mis
    // `audio` en deuxième). `hayesModem` référence machine.mfp : avant `machine`.
    std::unique_ptr<Machine>        machine;
    std::unique_ptr<HayesModem>     hayesModem;   // nul tant que le modem est OFF
    std::unique_ptr<NetBackendNull> etherNull;
    std::unique_ptr<SlirpBackend>   slirpNet;
    std::unique_ptr<MidiOutHost>    midiOut;
    std::unique_ptr<MidiInHost>     midiIn;
    std::unique_ptr<Mt32Synth>      mt32;
    std::unique_ptr<GmSynth>        gm;           // synthé GM intégré (TSF) — mixé comme le MT-32
    std::unique_ptr<DriveSound>     drive;
    std::unique_ptr<Audio>          audio;        // DERNIER des consommateurs : détruit en premier
    std::unique_ptr<GlScreen>       screen;
    // Fréquence de sortie RÉELLE (le périphérique en négocie une — cf. Audio::rate()).
    uint32_t audioRate = 48000;
    bool driveSoundAvail = false;          // échantillons roms/drivesound/ chargés
    bool driveSoundOn    = false;          // …et le réglage drivesound= les demande
    // Cadence : échéance réelle de la PROCHAINE trame émulée (cf. la boucle).
    std::chrono::steady_clock::time_point emuNext{};
    double lastMx = 0, lastMy = 0;         // dernière position curseur (deltas souris)
    // Reste FRACTIONNAIRE du delta souris mis à l'échelle (cf. mouseSpeed) : ce qui
    // n'atteint pas un pas entier est reporté, sinon un réglage < 1 perdrait tous les
    // petits mouvements et la souris ST resterait immobile sur un déplacement lent.
    double mouseAccX = 0, mouseAccY = 0;

    // Rectangle À L'ÉCRAN de l'image ST (px fenêtre), relevé à chaque trame par
    // drawStScreen/drawStKiosk. Sert à centrer les bandeaux qui parlent de la MACHINE
    // (« machine gelée ») sur l'écran ST plutôt que sur la fenêtre entière : au bureau
    // l'écran ST n'occupe qu'une partie de la fenêtre, et un bandeau centré sur celle-ci
    // recouvrait le menu et le panneau des disquettes.
    // stRectValid = false tant qu'aucune trame n'a dessiné l'écran (repli : la fenêtre).
    float stRectX0 = 0, stRectY0 = 0, stRectX1 = 0, stRectY1 = 0;
    bool  stRectValid = false;

    // --shot-window PREFIX FROM COUNT : capture de la FENÊTRE RENDUE (glReadPixels du
    // framebuffer par défaut), une PPM par trame. Indispensable pour instruire un défaut
    // d'AFFICHAGE : --shot ne rend que le framebuffer ÉMULÉ, donc tout ce qui se passe
    // entre lui et l'écran (échelle, filtrage, passe CRT) était invisible au harnais —
    // c'est ce qui a laissé passer les bandes rapportées sur Super Hang-On le 2026-09-02.
    std::string shotWinPrefix;
    long shotWinFrom = 0;
    int  shotWinMax = 0, shotWinDone = 0;
    // Réglage « joystick au clavier » à RENDRE en quittant la borne. Initialisé à
    // false et non à kbdJoy : lancé en --kiosk, kbdJoy vaut déjà true, et la sortie
    // vers le bureau avalerait les flèches + Ctrl droit du ST sans rien afficher.
    bool kbdJoyDesk = false;

    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ─── Les SERVICES : ce que la boucle et les menus déclenchent ────────────
    // Anciennes lambdas de main(), à l'identique. Chacune agit sur la session
    // ci-dessus ; aucune ne dessine.
    std::vector<neost::DongleRule> loadDongleTable();
    std::string resolvePath(const std::string& given);
    void usatanApply();
#ifdef NEOST_WITH_NET
    void modemApply(bool on);
#endif
    NetBackend* neBackend();
    void slirpApply(bool on);
    void etherApply(bool on);
    void netUsbeeApply(bool on);
    void midiOutApply();
    void midiInApply();
    bool midiLearnUids();
    void applyConfig();
    void switchKioskMode(bool on);
    void toggleFullscreen();
};

// L'instance unique. Les callbacks GLFW (onKey, onMouseButton, glisser-déposer) et
// les gestionnaires de réglages ImGui ont une signature IMPOSÉE, sans paramètre où
// passer un contexte : ils passent par ici. Tout le reste reçoit `App&`.
App& app();

// Retire du ST ce que l'AUTO-PLUG avait posé, et rien d'autre (cf. autoPortDev /
// autoCartKey ci-dessus, où le pourquoi est écrit).
void autoDongleRetract(App& A, Machine& machine, neost::appconfig::Config& cfg);

// ─── Services libres (ils ne touchent pas à l'état, ou seulement au gel) ────
bool fileExists(const std::string& p);
// Résout un chemin de données indépendamment du répertoire courant : tel quel,
// puis relatif au répertoire de l'exécutable (utile quand on lance depuis build/).
std::string resolveData(const std::string& given, const std::string& exeDir);
// ROM par défaut d'un modèle : EmuTOS 256 Ko pour STE/MegaSTE, 192 Ko sinon.
std::string pickTosForMachine(const std::string& machine, const std::string& current,
                              const std::string& exeDir, const std::string& romsDir);
void loadRtcFromConfig(Machine& m, const neost::appconfig::Config& c);
void snapshotRtc(Machine& m, neost::appconfig::Config& c);
// force=true : écrit la config MÊME en kiosk (normalement figé) — cf. la définition.
void saveConfig(App& A, const std::string& exeDir, neost::appconfig::Config& c,
                Machine* machine = nullptr, bool force = false);

// ─── Le cycle de vie du frontend (chantier A9) ──────────────────────────────
// appInit renvoie -1 pour « continuer », sinon le code de sortie du processus
// (--help / --version / option inconnue / échec d'ouverture de fenêtre).
int  appInit(App& A, int argc, char** argv);
void appLoop(App& A);
void appShutdown(App& A);
