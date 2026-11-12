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
//  Plates-formes (dorsaux, cf. EmuFiber.cpp) :
//    • ucontext — Linux, macOS : bascule de pile sur le même fil ;
//    • fibres Win32 — Windows ;
//    • fil dédié — Android (bionic n'a pas makecontext) et AddressSanitizer (qui ne
//      suit pas une bascule de pile sans annotations) : la pile d'émulation est celle
//      d'un second fil, et les deux fils se passent la main sous verrou — un seul
//      tourne à la fois, jamais en concurrence. Même sémantique que les bascules ;
//      coût ~10-50 µs par trame (deux réveils de fil) ;
//    • JSPI — web (Emscripten), si le navigateur a WebAssembly.Suspending : yield
//      suspend la pile wasm jusqu'à la promesse que la page résout à la trame
//      suivante. La pile ne se reprend pas sur demande (la reprise passe par la
//      boucle d'événements du navigateur) : resume() d'une exécution garée et abort()
//      ne sont pas synchrones — cf. requestAbort et le frontend web. Sans JSPI
//      (navigateur trop ancien), supported() rend faux.
//  Quand supported() rend faux (ou NEOST_NO_FIBER=1), la Machine garde le
//  comportement d'un seul tenant (trames closes comptées par lastRunFrames, l'hôte
//  reprend la main à la fin de l'instruction).
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
    // Vrai si resume() d'une exécution garée et abort() se font sur place (tous les
    // dorsaux sauf JSPI, où la reprise passe par la boucle d'événements de la page).
    static bool synchronous();

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
    // Demande d'abandon sans attendre : sur un dorsal synchrone, c'est abort() ; sur
    // JSPI, la pile garée est relancée et lève Abort à sa reprise — resume() (en
    // cours, en haut de la pile web) l'intercepte et aborted() le signale ensuite.
    void requestAbort();
    // Vrai (une fois) si la dernière exécution a été déroulée par requestAbort (JSPI) :
    // l'appelant remet ce qu'abort() remettrait (cf. Machine::runFrame).
    bool takeAborted() { const bool a = aborted_; aborted_ = false; return a; }

    // JSPI : vrai pendant que la page exécute la trame par l'export « promising » —
    // seul contexte où la pile wasm peut être suspendue (posé par le frontend web).
    static void setHostCanSuspend(bool on);

    // Pile d'émulation : l'instruction garée porte toute la chaîne Moira → sync →
    // blitter → ordonnanceur → fin de trame (rendu, spec512).
    static constexpr std::size_t kStackBytes = 16u * 1024u * 1024u;

private:
    friend void emuFiberEnter(EmuFiber* f);   // point d'entrée de la pile d'émulation
    struct Impl;                    // contexte de la plate-forme (cf. EmuFiber.cpp)
    void runJobs();                 // boucle de la pile d'émulation (sauf JSPI)
    void switchToFiber();
    void switchToHost();

    Impl* impl_ = nullptr;
    std::function<void()> job_{};   // copie : le corps garé survit à l'appel de resume
    std::exception_ptr error_{};
    bool inside_ = false, parked_ = false, abortReq_ = false, aborted_ = false;
    bool quit_ = false;             // fil dédié : la boucle runJobs s'arrête (destruction)
};
