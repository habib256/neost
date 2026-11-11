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

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(EMUFIBER_ASAN)
#  define EMUFIBER_NONE 1
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

#if defined(EMUFIBER_NONE)

struct EmuFiber::Impl {};
bool EmuFiber::supported() { return false; }
EmuFiber::EmuFiber() = default;
EmuFiber::~EmuFiber() = default;
bool EmuFiber::resume(const std::function<void()>& body) { body(); return false; }
void EmuFiber::yield() {}
void EmuFiber::abort() {}
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

#if defined(EMUFIBER_WIN32)
struct EmuFiber::Impl {
    void* host = nullptr;
    void* fiber = nullptr;
};
static void CALLBACK fiberEntryWin(void* arg) { emuFiberEnter(static_cast<EmuFiber*>(arg)); }
#endif

bool EmuFiber::supported() { return true; }

void emuFiberEnter(EmuFiber* f) { f->runJobs(); }

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
