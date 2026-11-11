// =============================================================================
//  AndroidMenu.hpp — Menu plein écran du frontend Android.
//
//  Il reprend la GRAMMAIRE du menu borne (main.cpp, drawKioskDiskMenu), parce
//  qu'elle a été pensée pour être lue à distance et pilotée sans clavier :
//    · un voile sombre, la machine EN PAUSE derrière ;
//    · deux zones — la ludothèque (« GAMES ») et les actions ;
//    · des rangées ÉNORMES, curseur vert ▶ sur la sélection ;
//    · insérer une disquette NE REDÉMARRE PAS (modèle « vraie machine ») —
//      seul « RESTART » relance ;
//    · une page clavier séparée, jeu NON mis en pause, pour envoyer une touche
//      au programme qui tourne dessous.
//  Ce qui change pour un téléphone : chaque rangée est TAPABLE au doigt, et la
//  navigation manette passe par la navigation clavier/pad de Dear ImGui.
//
//  La ludothèque elle-même (scan borné + tri par proximité, les suites du jeu
//  monté en tête) n'est PAS réécrite ici : c'est io/MediaScan, partagé avec la
//  borne.
//
//  Comme la fenêtre Configuration du bureau, ce menu NE FAIT RIEN : il remplit
//  des requêtes que la boucle consomme, seul endroit qui sait monter une image,
//  redémarrer la machine ou toucher au son.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace neost {

struct AndroidMenu {
    // --- État ----------------------------------------------------------------
    bool open      = false;    // menu affiché (machine en pause)
    bool keysPage  = false;    // page clavier (machine qui TOURNE)
    int  diskSel   = 0;
    std::vector<std::string> disks;   // ludothèque, remplie par refresh()
    std::string dataDir;              // dossier scanné (stockage interne)
    std::string mounted;              // chemin monté dans le lecteur A

    // --- Requêtes sortantes (consommées puis remises à zéro par l'appelant) ---
    std::string reqMount;      // insérer cette image (SANS redémarrer)
    bool reqRestart  = false;  // redémarrage à froid
    bool reqQuit     = false;
    bool reqRescan   = false;
    bool reqRewind   = false;  // reculer d'environ une seconde (core/Rewind)
    std::string status;        // dernier message de la boucle (retour arrière refusé…)
    int  reqKeyPress = -1;     // index dans kKeys : touche à envoyer au ST
    int  reqClick    = 0;      // 1 = clic gauche, 2 = clic droit

    // Relit la ludothèque (io/MediaScan) et recale la sélection sur le monté.
    void refresh();

    // Dessine le menu (si `open`) ou le seul bouton d'ouverture. `uiScale` cale
    // les tailles sur la densité de l'écran.
    void draw(float uiScale);
};

// Touches proposées par la page clavier — MÊME table que le menu borne
// (main.cpp, KIOSK_KEYS) : ce sont celles dont les jeux ST ont besoin.
struct MenuKey {
    const char* label;
    uint8_t     scancode;   // scancode ST ; 0 si c'est un clic souris
    int         click;      // 0 = touche, 1 = clic gauche, 2 = clic droit
};
extern const MenuKey kKeys[];
extern const int     kKeyCount;
extern const int     kKeyRows[][2];
extern const int     kKeyRowCount;

}  // namespace neost
