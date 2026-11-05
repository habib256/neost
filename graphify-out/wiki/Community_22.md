# Community 22

> 25 nodes

## Key Concepts

- **.updateGlueState()** (10 connections) — `src/core/Shifter.hpp`
- **Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`** (10 connections) — `docs/HATARI_DIVERGENCES.md`
- **Convergence Moira ↔ WinUAE — chantier « full timing-engine » (beam-sync)** (10 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.syncCpuBus()** (8 connections) — `src/core/Shifter.hpp`
- **.updateGlueRes()** (7 connections) — `src/core/Shifter.hpp`
- **Shifter::liveGlueCatchUp()** (7 connections) — `src/core/VideoGlue.cpp`
- **Shifter::replayGlue()** (7 connections) — `src/core/VideoGlue.cpp`
- **7. CHANTIER RESTANT (plus de deadlock, mais EL scramble en jeu)** (7 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.startHBL()** (6 connections) — `src/core/Shifter.hpp`
- **2. Vidéo / Shifter / Glue (video.c, spec512.c, conv_st.c)** (6 connections) — `docs/HATARI_MAPPING.md`
- **.restartVideoCounter()** (5 connections) — `src/core/Shifter.hpp`
- **glueLineLenFor()** (5 connections) — `src/core/VideoGlue.cpp`
- **Passe de cohérence doc ↔ code, 3ᵉ tour (2026-08-19)** (5 connections) — `CHANGELOG.md`
- **.liveLineDisplayed()** (4 connections) — `src/core/Shifter.hpp`
- **.recordSyncWrite()** (4 connections) — `src/core/Shifter.hpp`
- **⭐ ÉTAT COURANT (posé le 2026-08-27 — lire CECI avant tout le reste)** (4 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.lineStoppedOver()** (3 connections) — `src/core/Shifter.hpp`
- **lineGeom_** (2 connections) — `src/core/Shifter.hpp`
- **1. L'outil n°1 — harnais différentiel de cycles (Moira ↔ WinUAE)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **5. ✅ CONVERGENCE INSTRUCTION — COMPLÈTE (validée par workflow 6 classes)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **6. ✅ FONDATION CORRIGÉE — dispatch BLOC (Enchanted Land DÉBLOQUÉ)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **8. 🎯 PLAN D'ATTAQUE COORDONNÉ (refonte beam-sync) — pour la prochaine passe** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **3. Vérités MESURÉES cette session (corrigent la mémoire)** (1 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **4. Implémenté (RAM_SLOT+IACK DÉFAUT ON depuis 2026-06-17 ; toggle via `=0`)** (1 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **(archive) Sous-problèmes d'entrée d'IRQ — désormais RAFFINEMENTS, plus des blocages** (1 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`

## Relationships

- [Community 88](Community_88.md) (13 shared connections)
- [Community 5](Community_5.md) (9 shared connections)
- [Community 359](Community_359.md) (5 shared connections)
- [Community 288](Community_288.md) (5 shared connections)
- [Community 39](Community_39.md) (5 shared connections)
- [Community 205](Community_205.md) (4 shared connections)
- [Community 27](Community_27.md) (3 shared connections)
- [Community 173](Community_173.md) (2 shared connections)
- [Community 63](Community_63.md) (2 shared connections)
- [Community 273](Community_273.md) (1 shared connections)
- [Community 317](Community_317.md) (1 shared connections)
- [Community 169](Community_169.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `src/core/Shifter.hpp`
- `src/core/VideoGlue.cpp`

## Audit Trail

- EXTRACTED: 29 (33%)
- INFERRED: 60 (67%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*