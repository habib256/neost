# Community 205

> 22 nodes

## Key Concepts

- **.write8()** (13 connections) — `src/core/Shifter.hpp`
- **.updateGlueState()** (10 connections) — `src/core/Shifter.hpp`
- **Convergence Moira ↔ WinUAE — chantier « full timing-engine » (beam-sync)** (10 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.syncCpuBus()** (9 connections) — `src/core/Shifter.hpp`
- **7. CHANTIER RESTANT (plus de deadlock, mais EL scramble en jeu)** (7 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **Shifter::writeVideoCounterByte()** (5 connections) — `src/core/VideoCounter.cpp`
- **Passe de cohérence doc ↔ code, 3ᵉ tour (2026-08-19)** (5 connections) — `CHANGELOG.md`
- **.recordSyncWrite()** (4 connections) — `src/core/Shifter.hpp`
- **⭐ ÉTAT COURANT (posé le 2026-08-27 — lire CECI avant tout le reste)** (4 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.beamLineActive()** (3 connections) — `src/core/Shifter.hpp`
- **.beamPos()** (3 connections) — `src/core/Shifter.hpp`
- **.restartVideoCounter()** (3 connections) — `src/core/Shifter.hpp`
- **.writeShifterRes()** (2 connections) — `src/core/Shifter.hpp`
- **.writeVideoCounterByte()** (2 connections) — `src/core/Shifter.hpp`
- **hblShorten_** (2 connections) — `src/core/Shifter.hpp`
- **1. L'outil n°1 — harnais différentiel de cycles (Moira ↔ WinUAE)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **5. ✅ CONVERGENCE INSTRUCTION — COMPLÈTE (validée par workflow 6 classes)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **8. 🎯 PLAN D'ATTAQUE COORDONNÉ (refonte beam-sync) — pour la prochaine passe** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **3. Vérités MESURÉES cette session (corrigent la mémoire)** (1 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **4. Implémenté (RAM_SLOT+IACK DÉFAUT ON depuis 2026-06-17 ; toggle via `=0`)** (1 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **6. ✅ FONDATION CORRIGÉE — dispatch BLOC (Enchanted Land DÉBLOQUÉ)** (1 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **(archive) Sous-problèmes d'entrée d'IRQ — désormais RAFFINEMENTS, plus des blocages** (1 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`

## Relationships

- [Community 5](Community_5.md) (9 shared connections)
- [Community 88](Community_88.md) (8 shared connections)
- [Community 170](Community_170.md) (6 shared connections)
- [Community 396](Community_396.md) (4 shared connections)
- [Community 392](Community_392.md) (3 shared connections)
- [Community 27](Community_27.md) (3 shared connections)
- [Community 288](Community_288.md) (2 shared connections)
- [Community 299](Community_299.md) (2 shared connections)
- [Community 317](Community_317.md) (1 shared connections)
- [Community 102](Community_102.md) (1 shared connections)
- [Community 130](Community_130.md) (1 shared connections)
- [Community 232](Community_232.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `src/core/Shifter.hpp`
- `src/core/VideoCounter.cpp`

## Audit Trail

- EXTRACTED: 25 (37%)
- INFERRED: 42 (63%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*