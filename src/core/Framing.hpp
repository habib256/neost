// =============================================================================
//  Framing.hpp — Région de CONTENU de la trame courante : le cœur du ZOOM
//  ADAPTATIF, extrait de main.cpp (stContentRegion) pour être partagé.
//
//  Trois consommateurs : le kiosk (viewport GL), la fenêtre « Atari ST Screen »
//  du bureau (UV d'image), et le plein écran du frontend WASM (UV + taille
//  intrinsèque du canvas). La règle est la même partout ; seule la façon de
//  l'appliquer diffère — c'est exactement la frontière modèle/rendu.
//
//  Cadrages francs, jamais au pixel (→ zéro saccade) :
//   · Défaut (99 % des jeux) : cadre FIXE sur la ZONE ACTIVE (rectangle net donné
//     par le matériel — activeTop/activeHeight), qui ne bouge JAMAIS. Un champ
//     d'étoiles, un fond noir : rien ne fait « respirer » le zoom.
//   · Overscan : quand la Glue signale une bordure retirée, on cadre sur ce qu'elle
//     AFFICHE vraiment — étendue verticale live, et largeur élargie aux bordures
//     latérales SEULEMENT si elles débordent sur une part significative des lignes.
//     Hystérésis (latch ~0,6 s) + rétention de l'UNION des étendues vues pendant le
//     latch : le cadre ne rétrécit jamais en cours de scène. Une bordure haute ou
//     basse entièrement NOIRE ne compte pas (Enchanted Land l'ouvre sans y dessiner).
//
//  ⚠ CE QUE LA RÈGLE PRÉCÉDENTE RATAIT (corrigé le 2026-09-02). Elle ne connaissait
//  que deux cas — « zone active » ou « buffer entier » — et décidait des DEUX sur un
//  signal unique, `bordersOpen()`, qui est vrai dès qu'une bordure HAUTE, BASSE ou
//  LATÉRALE bouge. Conséquence sur Enchanted Land, qui n'ouvre que le haut : le cadre
//  passait à 416 px de large pour une image qui n'en occupe que 320 → zoom 1,3× trop
//  petit et deux bandes noires ; et son cas « bordure haute seule » remontait le cadre
//  de 2 lignes en gardant la hauteur active, ce qui ROGNAIT les 2 dernières lignes de
//  l'image (29 sur l'étalon overscan_top, qui dessine vraiment dans sa bordure haute).
//  Les mesures qui ont tranché sont dans le CHANGELOG ; l'instrument est
//  `NEOST_FRAMING_DIAG=1` sur neost-headless.
//
//  À appeler UNE fois par trame RENDUE : l'hystérésis se compte en trames.
//  Les latches vivent dans un FramingLatch tenu par l'appelant (2026-11-24) : ils
//  étaient des statiques de fonction, qu'un nouveau jeu dans le même processus
//  (libretro : retro_load_game après retro_unload_game, docs/LIBRETRO.md) héritait
//  de la session précédente. Le GUI garde UN latch pour tout le processus, si bien
//  qu'un aller-retour bureau ⇄ kiosk ne réinitialise toujours pas l'hystérésis.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

class Shifter;

namespace neost {

// [cTop, cTop+cH) = lignes de contenu du buffer ST. cW = largeur (en px du
// buffer) qui doit RESTER visible : la zone active seule en régime normal (les
// bordures latérales sont du décor, rognables), le buffer entier dès qu'une
// bordure est ouverte (l'image déborde alors DANS les bordures). Les frontends
// s'en servent comme plancher : ils n'amputent jamais l'image elle-même.
// Hystérésis du cadrage (cf. Framing.cpp). Un par affichage ; reset() pour repartir
// à froid (nouveau jeu, nouvelle machine).
struct FramingLatch {
    int  overscanLatch = 0;   // trames de maintien restantes, bordure ouverte (haut ou bas)
    int  openStreak = 0;      // trames CONSÉCUTIVES à bordure ouverte (cf. kAttack)
    int  latchTop = 0, latchBot = 0;   // étendue verticale RETENUE
    bool latchWide = false;   // largeur RETENUE : buffer entier ?
    void reset() { *this = FramingLatch{}; }
};

void stContentRegion(const Shifter& sh, FramingLatch& latch, int& cTop, int& cH, int& cW);

}  // namespace neost
