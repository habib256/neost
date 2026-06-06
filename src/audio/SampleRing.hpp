// =============================================================================
//  SampleRing.hpp — Anneau d'échantillons SPSC (lock-free) émulation → audio.
//
//  Un SEUL producteur (thread d'émulation : Audio::produceFrame, après chaque
//  trame) et un SEUL consommateur (thread audio miniaudio : Audio::render). C'est
//  la frontière du modèle « push » horodaté de la Phase C : la forme d'onde est
//  générée sur l'horloge d'émulation (où les écritures de registres sont datées au
//  cycle près) puis transmise au thread audio qui ne fait plus que recopier.
//
//  Lock-free via deux compteurs atomiques à course libre (write/read) ; capacité
//  = puissance de 2 (masque). Underrun (consommateur trop rapide) → silence ;
//  overflow (producteur trop rapide, ex. avance rapide) → on jette le surplus.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <atomic>
#include <vector>
#include <cstdint>
#include <cstddef>

class SampleRing {
public:
    // capPow2 DOIT être une puissance de 2 (sinon le masque est faux).
    explicit SampleRing(size_t capPow2 = 16384)
        : buf_(capPow2, 0.0f), mask_(capPow2 - 1) {}

    size_t capacity() const { return buf_.size(); }
    // Nombre d'échantillons lisibles (vu producteur ET consommateur, approx. sûre).
    size_t available() const {
        return size_t(w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire));
    }
    size_t space() const { return buf_.size() - available(); }

    // Producteur : pousse jusqu'à `n` échantillons. Renvoie le nombre réellement
    // écrit (< n si l'anneau est plein → le surplus est jeté, latence bornée).
    size_t push(const float* src, size_t n) {
        const uint64_t w = w_.load(std::memory_order_relaxed);
        const uint64_t r = r_.load(std::memory_order_acquire);
        const size_t   free = buf_.size() - size_t(w - r);
        const size_t   k = n < free ? n : free;
        for (size_t i = 0; i < k; ++i) buf_[(w + i) & mask_] = src[i];
        w_.store(w + k, std::memory_order_release);
        return k;
    }

    // Consommateur : tire `n` échantillons dans `dst`, complète par du SILENCE en
    // cas d'underrun. Renvoie le nombre réellement lu (avant remplissage à zéro).
    size_t pull(float* dst, size_t n) {
        const uint64_t r = r_.load(std::memory_order_relaxed);
        const uint64_t w = w_.load(std::memory_order_acquire);
        const size_t   avail = size_t(w - r);
        const size_t   k = n < avail ? n : avail;
        for (size_t i = 0; i < k; ++i) dst[i] = buf_[(r + i) & mask_];
        for (size_t i = k; i < n; ++i) dst[i] = 0.0f;        // underrun → silence
        r_.store(r + k, std::memory_order_release);
        return k;
    }

    // Vide l'anneau (consommateur). Sert à amorcer/relancer après une coupure.
    void clear() { r_.store(w_.load(std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<float>    buf_;
    size_t                mask_;
    std::atomic<uint64_t> w_{0};   // position d'écriture (producteur) — course libre
    std::atomic<uint64_t> r_{0};   // position de lecture (consommateur) — course libre
};
