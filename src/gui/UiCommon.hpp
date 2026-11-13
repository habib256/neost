// =============================================================================
//  UiCommon.hpp — briques d'interface partagées entre main.cpp et les pages.
//
//  Extrait de main.cpp lors de son découpage : les pictogrammes et le bouton à
//  icône seule servaient à la fois à la barre de menus, aux fenêtres de débogage
//  et aux pages de configuration, donc ils ne pouvaient rester `static` dans une
//  seule TU.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

// --- Pictogrammes Font Awesome 5 Free Solid (fonts/fa-solid-900.ttf, fusionnés dans
// la police ImGui — cf. chargement dans main()). Chaînes UTF-8 des codepoints FA de la
// zone à usage privé. À préfixer à un libellé : ICON_FA_REDO " Reset".
#define ICON_FA_MUSIC         "\xef\x80\x81"   // U+F001 — page MIDI
#define ICON_FA_STAR          "\xef\x80\x85"
#define ICON_FA_POWER_OFF     "\xef\x80\x91"
#define ICON_FA_REDO          "\xef\x80\x9e"
#define ICON_FA_VOLUME_OFF    "\xef\x80\xa6"
#define ICON_FA_VOLUME_DOWN   "\xef\x80\xa7"
#define ICON_FA_VOLUME_UP     "\xef\x80\xa8"
#define ICON_FA_VOLUME_MUTE   "\xef\x9a\xa9"
#define ICON_FA_ADJUST        "\xef\x81\x82"
#define ICON_FA_EJECT         "\xef\x81\x92"
#define ICON_FA_WARNING       "\xef\x81\xb1"   // U+F071 exclamation-triangle — CPU halté (A42)
#define ICON_FA_INFO_CIRCLE   "\xef\x81\x9a"   // U+F05A info-circle — ROM choisie avec la machine
#define ICON_FA_HDD           "\xef\x82\xa0"
#define ICON_FA_FOLDER_OPEN   "\xef\x81\xbc"
#define ICON_FA_SAVE          "\xef\x83\x87"
#define ICON_FA_BOLT          "\xef\x83\xa7"
#define ICON_FA_DESKTOP       "\xef\x84\x88"
#define ICON_FA_GAMEPAD       "\xef\x84\x9b"
#define ICON_FA_KEYBOARD      "\xef\x84\x9c"
#define ICON_FA_SERVER        "\xef\x88\xb3"
#define ICON_FA_CLONE         "\xef\x89\x8d"
#define ICON_FA_MICROCHIP     "\xef\x8b\x9b"
#define ICON_FA_SIGN_OUT_ALT  "\xef\x8b\xb5"
#define ICON_FA_COMPACT_DISC  "\xef\x94\x9f"
#define ICON_FA_MEMORY        "\xef\x94\xb8"
#define ICON_FA_PALETTE       "\xef\x94\xbf"
#define ICON_FA_TIMES         "\xef\x80\x8d"
#define ICON_FA_PLUS          "\xef\x81\xa7"
#define ICON_FA_BUG           "\xef\x86\x88"
#define ICON_FA_PLAY          "\xef\x81\x8b"
#define ICON_FA_PAUSE         "\xef\x81\x8c"
#define ICON_FA_STEP_FORWARD  "\xef\x81\x91"
#define ICON_FA_EXPAND        "\xef\x81\xa5"
// Engrenage U+F013 : la plage 0xf000-0xf8ff chargée dans la police le couvre déjà.
#define ICON_FA_COG           "\xef\x80\x93"
// WiFi U+F1EB (page Network) : même plage de police.
#define ICON_FA_WIFI          "\xef\x87\xab"
// Clé U+F084 (page Dongles) : même plage de police.
#define ICON_FA_KEY           "\xef\x82\x84"

// Bouton à ICÔNE SEULE, infobulle au survol. Renvoie true au clic.
bool IconButton(const char* icon, const char* tooltip);
