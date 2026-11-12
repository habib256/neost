// =============================================================================
//  DebugWindows.hpp — les fenêtres d'inspection (hexa, CPU, joystick, débogueur).
//
//  Elles LISENT la machine ; la seule qui écrive est le débogueur, et seulement
//  par des drapeaux (pause, pas-à-pas) que la boucle consomme à sa frontière de
//  trame — même discipline de requêtes que les pages de configuration.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstdint>

struct App;
struct GLFWwindow;
class Bus;
class Cpu68k;
class Machine;

void drawHexViewer(App& A, Bus& bus);
// reqReset passe à true si le bouton RESET est cliqué.
void drawCpuState(App& A, Cpu68k& cpu, bool& reqReset);
void drawJoystickWindow(App& A, GLFWwindow* win, uint8_t lastJoy0, uint8_t lastJoy1);
void drawDebugger(App& A, Machine& machine);
// Explorateur de bitmaps : la mémoire lue en plans ST (1, 2 ou 4), avec la palette
// courante, depuis une adresse quelconque. Lecture seule, sans effet de bord.
void drawBitmapExplorer(App& A, Machine& machine);
