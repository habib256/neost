// EmuFiber.cpp — cf. EmuFiber.hpp.
//
// ⚠ macOS : <ucontext.h> exige _XOPEN_SOURCE, et la TAILLE de ucontext_t en dépend
// (la variante X/Open embarque __mcontext_data). Le définir AVANT le premier en-tête
// système : sinon un en-tête plus tôt (via <functional>) a déjà figé la petite
// variante et getcontext écrit au-delà de la structure — saut dans le vide à la
// première bascule. _DARWIN_C_SOURCE garde visibles les extensions (MAP_ANON).
#if defined(__APPLE__)
#  define _XOPEN_SOURCE 700
#  define _DARWIN_C_SOURCE 1
#endif
#include "core/EmuFiber.hpp"

#include <cstdio>
#include <cstdlib>

#if defined(__SANITIZE_ADDRESS__)
#  define EMUFIBER_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define EMUFIBER_ASAN 1
#  endif
#endif

// EMUFIBER_FORCE_THREAD (posé par l'option CMake « …_EMUFIBER_THREAD ») force le dorsal « fil dédié » sur un bureau :
// c'est celui d'Android, qu'on vérifie ainsi sous Linux et macOS.
#if defined(__EMSCRIPTEN__)
#  define EMUFIBER_JSPI 1
#  include <emscripten.h>
#elif defined(__ANDROID__) || defined(EMUFIBER_ASAN) || defined(EMUFIBER_FORCE_THREAD)
#  define EMUFIBER_THREAD 1
#  include <condition_variable>
#  include <mutex>
#  include <pthread.h>
#elif defined(_WIN32)
#  define EMUFIBER_WIN32 1
#  include <windows.h>
#else
#  define EMUFIBER_UCONTEXT 1
// ucontext est déclaré obsolète par macOS (depuis 10.6) mais reste fourni et
// fonctionne sur arm64 comme x86_64 — vérifié : bascule, exceptions C++ levées et
// rattrapées sur la pile d'émulation, déroulement par Abort.
#  if defined(__APPLE__)
#    pragma clang diagnostic ignored "-Wdeprecated-declarations"
#  endif
#  include <sys/mman.h>
#  include <unistd.h>
#  include <ucontext.h>
#endif

#if defined(EMUFIBER_JSPI)

// -----------------------------------------------------------------------------
//  Web : JSPI (JavaScript Promise Integration). Pas de seconde pile à nous : la
//  page appelle la trame par un export « promising » (WebAssembly.promising), et
//  neost_jspi_park, importé « Suspending », rend une promesse — le moteur suspend
//  alors toute la pile wasm jusqu'à l'export, la page reprend la main. Elle livre la
//  trame close (neost_web_parked_frame, appelé AVANT la suspension), puis résout la
//  promesse quand la trame suivante est due : la pile repart dans yield.
//  Les deux fonctions JS ci-dessous sont des imports « env » ; web/shell.html
//  enveloppe neost_jspi_park dans WebAssembly.Suspending à l'instanciation et pose
//  Module.neostJspi. Sans JSPI dans le navigateur, supported() rend faux et rien de
//  ceci n'est appelé.
// -----------------------------------------------------------------------------
EM_JS(int, neost_jspi_ready, (), { return Module.neostJspi === true ? 1 : 0; });
EM_JS(void, neost_jspi_park, (), {
    Module._neost_web_parked_frame();
    return new Promise((res) => { Module.neostParkResolve = res; });
});
EM_JS(void, neost_jspi_release, (), {
    const r = Module.neostParkResolve;
    Module.neostParkResolve = null;
    if (r) r();
});

namespace { bool g_hostCanSuspend = false; }

struct EmuFiber::Impl {};
bool EmuFiber::supported() { return neost_jspi_ready() != 0; }
bool EmuFiber::synchronous() { return false; }
void EmuFiber::setHostCanSuspend(bool on) { g_hostCanSuspend = on; }
EmuFiber::EmuFiber() = default;
EmuFiber::~EmuFiber() = default;   // la page ne détruit pas la Machine (statique)

bool EmuFiber::resume(const std::function<void()>& body) {
    // Une exécution garée ne se reprend que par la page (promesse résolue) : un appel
    // pendant la suspension (rappel JS ré-entrant) ne fait rien.
    if (parked_) return true;
    inside_ = g_hostCanSuspend;
    try {
        body();
    } catch (const Abort&) {
        aborted_ = true;            // déroulée par requestAbort : l'appelant remet l'état
    } catch (...) {
        inside_ = false;
        throw;
    }
    inside_ = false;
    return false;
}

void EmuFiber::yield() {
    parked_ = true;
    neost_jspi_park();              // suspend la pile wasm ; revient à la reprise
    parked_ = false;
    if (abortReq_) { abortReq_ = false; throw Abort{}; }
}

void EmuFiber::requestAbort() {
    if (!parked_) return;
    abortReq_ = true;
    neost_jspi_release();           // la pile reprend (microtâche) et lève Abort
}

void EmuFiber::abort() {
    if (!parked_) return;
    // Ne peut pas dérouler sur place : le frontend web diffère reset, reconfiguration
    // et chargement de ROM jusqu'à la fin du déroulement (requestAbort). Arriver ici
    // est un défaut du frontend — on s'arrête net plutôt que de corrompre l'état.
    std::fprintf(stderr, "[fiber] synchronous abort requested while parked (JSPI)\n");
    std::abort();
}

void EmuFiber::runJobs() {}
void EmuFiber::switchToFiber() {}
void EmuFiber::switchToHost() {}

#else

void emuFiberEnter(EmuFiber* f);

#if defined(EMUFIBER_UCONTEXT)
struct EmuFiber::Impl {
    ucontext_t host{}, fiber{};
    void* map = nullptr;          // pile + page de garde (mmap)
    std::size_t mapBytes = 0;
};

namespace {
EmuFiber* g_starting = nullptr;   // passe l'objet à l'entrée (makecontext ne prend que des int)
}

static void fiberEntry() {
    EmuFiber* self = g_starting;
    g_starting = nullptr;
    emuFiberEnter(self);   // ne revient jamais : la pile est abandonnée à la destruction
}
#endif

#if defined(EMUFIBER_THREAD)
// Fil dédié : la pile d'émulation est celle d'un second fil. `fiberTurn` dit qui a
// la main ; chaque bascule la passe sous le verrou et attend qu'elle revienne. Un seul
// fil tourne à la fois : le verrou ordonne aussi les écritures (happens-before), l'état
// de la Machine se lit donc de part et d'autre comme sur un seul fil.
struct EmuFiber::Impl {
    pthread_t thread{};
    std::mutex m;
    std::condition_variable cv;
    bool fiberTurn = false;
    bool quit = false;
};

static void* fiberThreadEntry(void* arg) {
    EmuFiber* f = static_cast<EmuFiber*>(arg);
    emuFiberEnter(f);               // revient quand la destruction arrête la boucle
    return nullptr;
}
#endif

#if defined(EMUFIBER_WIN32)
struct EmuFiber::Impl {
    void* host = nullptr;
    void* fiber = nullptr;
};
static void CALLBACK fiberEntryWin(void* arg) { emuFiberEnter(static_cast<EmuFiber*>(arg)); }
#endif

bool EmuFiber::supported() { return true; }
bool EmuFiber::synchronous() { return true; }
void EmuFiber::setHostCanSuspend(bool) {}
void EmuFiber::requestAbort() { abort(); }

void emuFiberEnter(EmuFiber* f) {
#if defined(EMUFIBER_THREAD)
    {   // le fil attend son premier tour (≙ makecontext : rien ne tourne avant resume)
        std::unique_lock<std::mutex> lk(f->impl_->m);
        f->impl_->cv.wait(lk, [f] { return f->impl_->fiberTurn || f->impl_->quit; });
        if (f->impl_->quit) return;
    }
#endif
    f->runJobs();
}

EmuFiber::EmuFiber() : impl_(new Impl) {
#if defined(EMUFIBER_UCONTEXT)
    const std::size_t page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    impl_->mapBytes = kStackBytes + page;
    impl_->map = mmap(nullptr, impl_->mapBytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON, -1, 0);
    if (impl_->map == MAP_FAILED) {
        std::fprintf(stderr, "[fiber] cannot map the emulation stack\n");
        std::abort();
    }
    mprotect(impl_->map, page, PROT_NONE);       // garde : un débordement faute net
    getcontext(&impl_->fiber);
    impl_->fiber.uc_stack.ss_sp = static_cast<char*>(impl_->map) + page;
    impl_->fiber.uc_stack.ss_size = kStackBytes;
    impl_->fiber.uc_link = nullptr;              // runJobs ne revient jamais
    makecontext(&impl_->fiber, fiberEntry, 0);
#elif defined(EMUFIBER_THREAD)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kStackBytes);
    if (pthread_create(&impl_->thread, &attr, fiberThreadEntry, this) != 0) {
        std::fprintf(stderr, "[fiber] cannot start the emulation thread\n");
        std::abort();
    }
    pthread_attr_destroy(&attr);
#else
    impl_->fiber = CreateFiber(kStackBytes, fiberEntryWin, this);
    if (!impl_->fiber) {
        std::fprintf(stderr, "[fiber] cannot create the emulation fiber\n");
        std::abort();
    }
#endif
}

EmuFiber::~EmuFiber() {
    abort();   // une instruction garée est déroulée tant que la Machine vit
#if defined(EMUFIBER_UCONTEXT)
    munmap(impl_->map, impl_->mapBytes);
#elif defined(EMUFIBER_THREAD)
    {   // le fil attend son tour (jamais lancé, ou rendu la main) : on l'arrête
        std::lock_guard<std::mutex> lk(impl_->m);
        impl_->quit = true;
        quit_ = true;
    }
    impl_->cv.notify_all();
    pthread_join(impl_->thread, nullptr);
#else
    DeleteFiber(impl_->fiber);
#endif
    delete impl_;
}

void EmuFiber::switchToFiber() {
    inside_ = true;
#if defined(EMUFIBER_UCONTEXT)
    g_starting = this;              // lu une seule fois, à la première entrée
    swapcontext(&impl_->host, &impl_->fiber);
#elif defined(EMUFIBER_THREAD)
    std::unique_lock<std::mutex> lk(impl_->m);
    impl_->fiberTurn = true;
    impl_->cv.notify_all();
    impl_->cv.wait(lk, [this] { return !impl_->fiberTurn; });
#else
    if (!IsThreadAFiber()) ConvertThreadToFiber(nullptr);
    impl_->host = GetCurrentFiber();
    SwitchToFiber(impl_->fiber);
#endif
    inside_ = false;
}

void EmuFiber::switchToHost() {
#if defined(EMUFIBER_UCONTEXT)
    swapcontext(&impl_->fiber, &impl_->host);
#elif defined(EMUFIBER_THREAD)
    std::unique_lock<std::mutex> lk(impl_->m);
    impl_->fiberTurn = false;
    impl_->cv.notify_all();
    impl_->cv.wait(lk, [this] { return impl_->fiberTurn || impl_->quit; });
#else
    SwitchToFiber(impl_->host);
#endif
}

void EmuFiber::runJobs() {
    for (;;) {
        std::function<void()> job = std::move(job_);   // vit sur CETTE pile, garée comprise
        job_ = nullptr;
        try {
            if (job) job();
        } catch (const Abort&) {
            // exécution garée abandonnée : la pile est déroulée, rien à signaler
        } catch (...) {
            error_ = std::current_exception();
        }
        job = nullptr;
        parked_ = false;
        switchToHost();
        if (quit_) return;          // fil dédié : destruction de l'EmuFiber
    }
}

bool EmuFiber::resume(const std::function<void()>& body) {
    if (!parked_) job_ = body;      // sinon : on reprend l'exécution garée
    error_ = nullptr;
    switchToFiber();
    if (error_) {
        std::exception_ptr e = error_;
        error_ = nullptr;
        std::rethrow_exception(e);
    }
    return parked_;
}

void EmuFiber::yield() {
    parked_ = true;
    switchToHost();
    parked_ = false;
    if (abortReq_) throw Abort{};
}

void EmuFiber::abort() {
    if (!parked_) return;
    abortReq_ = true;
    switchToFiber();                // Abort déroule la pile, runJobs rend la main
    abortReq_ = false;
    error_ = nullptr;
}

#endif
