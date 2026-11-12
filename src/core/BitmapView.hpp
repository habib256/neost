// =============================================================================
//  BitmapView.hpp — décodage de la mémoire en plans ST, pour l'explorateur de
//  bitmaps du débogueur.
//
//  Le cœur ne dépend pas du GUI : ici, on ne fait que transformer des octets en
//  pixels ARGB. La fenêtre (gui/DebugWindows.cpp) choisit l'adresse, le nombre
//  de plans, la largeur et la palette, lit la mémoire SANS effet de bord
//  (Bus::peek8) et pose le résultat dans une texture.
//
//  Format des plans entrelacés du ST — [B] Atari ST Hardware Specification
//  (Atari Corp., 1985), § Video Display : chaque groupe de 16 pixels tient en
//  N mots consécutifs (N = 4 en basse, 2 en moyenne, 1 en haute résolution), le
//  mot k portant le bit k de l'index de couleur, le bit 15 du mot étant le pixel
//  le plus à gauche. C'est la même règle que Shifter::decodeLineIdx, mais sans
//  compteur vidéo, sans bordure ni scroll fin : une vue de la RAM, pas de l'écran.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace neost {

// Géométrie d'une vue : `strideBytes` est la largeur d'une ligne en octets (ce
// que l'ASE appelle « width in bytes ») ; la largeur en pixels s'en déduit
// (bitmapWidthPx). `planes` ∈ {1, 2, 4}.
struct BitmapLayout {
    int planes      = 4;
    int strideBytes = 160;
    int height      = 200;
};

// Nombre de plans accepté (1, 2 ou 4) ; toute autre valeur retombe sur 4.
int bitmapPlanes(int planes);

// Largeur en pixels d'une ligne : un groupe de 16 pixels par tranche complète de
// 2 × plans octets. Une queue incomplète n'est pas affichée (elle n'a pas tous
// ses plans).
int bitmapWidthPx(const BitmapLayout& l);

// Octets lus pour la vue entière (strideBytes × height).
std::size_t bitmapBytes(const BitmapLayout& l);

// Palette ARGB de la vue. `stPal` = les 16 registres $FF8240 ($0RGB) ;
// `mono` = moniteur monochrome en haute résolution : un plan, blanc sur noir
// inversé selon le bit 0 de la couleur 0, comme le Shifter le sort
// ([D] Hatari video.c « HBLPalettes[0] & 0x777 » @ f0736b24, repris par
// Shifter.cpp). `gray` remplace la palette par une rampe de gris régulière,
// utile quand la palette courante rend des données illisibles (sprites rangés
// avec une autre palette, couleur 0 = couleur 15…).
void bitmapPalette(const uint16_t stPal[16], int planes, bool mono, bool gray,
                   uint32_t out[16]);

// Index de couleur du pixel x de la ligne commençant à `row` (octets bruts).
// Aucune vérification : l'appelant garantit (x / 16 + 1) × 2 × plans ≤ taille.
uint8_t bitmapPixelIndex(const uint8_t* row, int planes, int x);

// Décode `src` (bitmapBytes(l) octets lus depuis l'adresse de départ) en ARGB
// dans `out` (bitmapWidthPx(l) × height). `valid` (même taille que `src`, ou
// nullptr) marque les octets réellement présents : un groupe dont un octet ne
// l'est pas (au-delà de la RAM, trou du plan mémoire) sort en `voidColor`.
void decodeBitmap(const BitmapLayout& l, const uint8_t* src, const uint8_t* valid,
                  const uint32_t pal[16], uint32_t voidColor, uint32_t* out);

}  // namespace neost
