// =============================================================================
//  MediaPages.hpp — pages « supports » de la fenêtre Configuration :
//  Disquettes, Cartouche, Disque dur, Réseau.
//
//  Extraites de main.cpp (qui frôlait les 5000 lignes). Elles s'y prêtaient sans
//  rien réécrire, parce qu'elles respectaient déjà la discipline de la fenêtre
//  Configuration : une page ne FAIT rien, elle lit un état et pose des requêtes
//  que la boucle principale consomme en fin de trame. Leur seule dépendance au
//  reste de main.cpp était les deux tampons de saisie ci-dessous, partagés avec
//  le sous-menu Profils qui les resynchronise.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>


void drawFloppyPage(const std::string& disksDir,
                    const std::string& mountedA, const std::string& mountedB,
                    std::string& reqMountA, std::string& reqMountB,
                    bool& reqEjectA, bool& reqEjectB);

void drawCartPage(const std::string& cartsDir, const std::string& mounted,
                  std::string& reqMount, bool& reqEject);

// Lettres effectives du bureau, regroupées en plages contiguës.
std::string hardDiskDriveLetters(uint32_t mask);

void drawHardDiskPage(const std::string& hdDir, const std::string& gemdosDefault,
                      const std::string& curGemdos, bool gemdosActive, uint32_t gemdosDrives,
                      const std::string& curAcsi, bool acsiActive, int acsiParts,
                      bool usatanOn, const std::string& curSd2,
                      std::string& reqMountGemdos, bool& reqEjectGemdos,
                      std::string& reqMountAcsi,  bool& reqEjectAcsi,
                      int& reqUltraSatan, std::string& reqMountSd2, bool& reqEjectSd2);

void drawNetworkPage(bool modemOn, bool etherOn,
                     bool netusbeeOn, bool cartMounted,
                     bool slirpOn, bool slirpAvail,
                     int& reqModem, int& reqEther, int& reqNetUsbee, int& reqSlirp);
