// =============================================================================
//  EmuFiber.hpp — Coroutine de l'émulation : une trame peut rendre la main à l'hôte
//  AU MILIEU d'une instruction 68000, puis la reprendre là où elle était.
//
//  Pourquoi : un blit HOG parti dans une instruction (Blitter::onSlice appelé depuis
//  Moira::sync) tient le CPU jusqu'à yCount == 0 — plusieurs secondes de temps émulé
//  pour un grand blit (65 536 × 4 096 mots sur Mega STE ≈ 40 s hôte, GUI figé). Le
//  passage de trame se fait déjà à l'heure dans le temps émulé
//  (Machine::rollFrameInInstruction_) ; rendre la main à l'hôte suppose en plus de
//  quitter la pile C++ de l'instruction sans la perdre. Moira ne sait pas reprendre
//  une instruction entamée (une faute bus l'abandonne par exception) : on garde la
//  pile vivante sur une pile à part, et on bascule de l'une à l'autre.
//
//  Modèle : coroutine sur le MÊME fil (pas de concurrence, pas d'ordonnanceur du
//  système entre les deux). runFrame bascule sur la pile d'émulation, qui rend la
//  main à la fin de la trame — ou au milieu d'une instruction quand la trame se
//  termine pendant un blit HOG. Même code, mêmes données : l'émulation reste
//  identique à l'octet. Coût : deux bascules par trame (~5 µs sous macOS, où
//  swapcontext fait un appel système pour le masque de signaux), contre ~3 ms
//  d'émulation.
//
//  Plates-formes : ucontext (Linux, macOS), fibres Win32 (Windows). Ailleurs — web
//  (Emscripten), Android (bionic n'a pas makecontext) — et sous AddressSanitizer (qui
//  ne suit pas une bascule de pile sans annotations), supported() rend faux : la
//  Machine garde alors le comportement d'un seul tenant (trames closes comptées par
//  lastRunFrames, l'hôte reprend la main à la fin de l'instruction).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <exception>
#include <functional>

class EmuFiber {
public:
    // Déroulement d'une instruction garée qu'on abandonne (reset, destruction).
    // ⚠ Ne dérive PAS de std::exception : Moira::execute intercepte std::exception
    // pour en faire une exception 68000 (Moira.cpp:276 et suivantes) ; celle-ci doit
    // traverser le cœur jusqu'au sommet de la coroutine.
    struct Abort {};

    static bool supported();

    EmuFiber();
    ~EmuFiber();
    EmuFiber(const EmuFiber&) = delete;
    EmuFiber& operator=(const EmuFiber&) = delete;

    // Hôte : exécute `body` sur la pile d'émulation jusqu'à ce qu'il se termine ou se
    // gare (yield). Si une exécution est déjà garée, la reprend (`body` est ignoré).
    // Rend vrai si l'exécution est maintenant garée. Une exception de `body` (autre
    // qu'Abort) est relancée ici, sur la pile de l'hôte.
    bool resume(const std::function<void()>& body);
    bool parked() const { return parked_; }
    // Vrai pendant que la pile d'émulation tourne (seul endroit où yield a un sens).
    bool onFiber() const { return inside_; }
    // Pile d'émulation : rend la main à l'hôte ; revient quand l'hôte reprend. Lève
    // Abort si l'hôte a abandonné l'exécution garée.
    void yield();
    // Hôte : abandonne une exécution garée (la pile est déroulée par Abort). Sans effet
    // si rien n'est garé. L'état de la machine est alors celui d'une instruction coupée :
    // l'appelant doit le remplacer (reset, chargement d'état) ou le détruire.
    void abort();

    // Pile d'émulation : l'instruction garée porte toute la chaîne Moira → sync →
    // blitter → ordonnanceur → fin de trame (rendu, spec512).
    static constexpr std::size_t kStackBytes = 16u * 1024u * 1024u;

private:
    friend void emuFiberEnter(EmuFiber* f);   // point d'entrée de la pile d'émulation
    struct Impl;                    // contexte de la plate-forme (cf. EmuFiber.cpp)
    void runJobs();                 // boucle de la pile d'émulation
    void switchToFiber();
    void switchToHost();

    Impl* impl_ = nullptr;
    std::function<void()> job_{};   // copie : le corps garé survit à l'appel de resume
    std::exception_ptr error_{};
    bool inside_ = false, parked_ = false, abortReq_ = false;
};
