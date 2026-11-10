// =============================================================================
//  KeyboardWindow.hpp — Fenêtre « Keyboard » : la photo du clavier ST (pic/) avec
//  une zone cliquable par touche → scancode IKBD (appui tant que le bouton de la
//  souris est tenu ; Shift/Control/Alternate/CapsLock sont COLLANTS : un clic les
//  arme, ils retombent après la touche suivante ou sur un second clic).
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <string>

class Machine;

// Dessine la fenêtre (ImGui::Begin/End inclus). `open` = drapeau de visibilité,
// `imagePath` = chemin RÉSOLU de la photo (pic/Black_Keyboard_AtariST.jpeg).
void drawKeyboardWindow(bool* open, Machine& machine, const std::string& imagePath);
// Relâche tout ce que la fenêtre tient (touche enfoncée, modificateurs armés) —
// à appeler quand elle n'est plus dessinée (menu, fermeture, bascule kiosk), sinon
// le ST garde un « make » sans « break ». Sans effet si rien n'est tenu.
void keyboardWindowReleaseAll(Machine& machine);
