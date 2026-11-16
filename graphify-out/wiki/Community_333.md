# Community 333

> 36 nodes

## Key Concepts

- **Moira — copie vendorisée (fork NeoST)** (19 connections) — `extern/moira/NEOST_VENDOR.md`
- **.execute()** (9 connections) — `extern/moira/Moira/Moira.h`
- **.processException()** (9 connections) — `extern/moira/Moira/Moira.h`
- **.setIPL()** (8 connections) — `extern/moira/Moira/Moira.h`
- **.halt()** (7 connections) — `extern/moira/Moira/Moira.h`
- **.checkForIrq()** (5 connections) — `extern/moira/Moira/Moira.h`
- **.pollIpl()** (5 connections) — `extern/moira/Moira/Moira.h`
- **.setIplDelay()** (5 connections) — `extern/moira/Moira/Moira.h`
- **2. Carte des divergences (source-grounded, 3 agents — file:line des DEUX côtés)** (5 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **Patch local : `NEOST_IPLFETCH` (reconnaissance IPL différée)** (5 connections) — `extern/moira/NEOST_VENDOR.md`
- **.executeUntil()** (4 connections) — `extern/moira/Moira/Moira.h`
- **Bus error et double faute (2026-11-10)** (4 connections) — `docs/FX68K_BENCH.md`
- **didReachBreakpoint()** (3 connections) — `extern/moira/Moira/Moira.h`
- **didReachSoftstop()** (3 connections) — `extern/moira/Moira/Moira.h`
- **.willProcessFault()** (3 connections) — `extern/moira/Moira/Moira.h`
- **Patch local : reset gardé (2026-07-08)** (3 connections) — `extern/moira/NEOST_VENDOR.md`
- **cpuDidHalt()** (2 connections) — `extern/moira/Moira/Moira.h`
- **.sync()** (2 connections) — `tools/moira_bare_bench.cpp`
- **i64** (2 connections)
- **Patch local : STOP+8 — retenue d'IRQ de 4 cycles après STOP (2026-10-22)** (2 connections) — `extern/moira/NEOST_VENDOR.md`
- **Patch local : STOP niveau-sensible (2026-07-03)** (2 connections) — `extern/moira/NEOST_VENDOR.md`
- **Patch local : watchpoints masqués au bus 24 bits (2026-07-11)** (2 connections) — `extern/moira/NEOST_VENDOR.md`
- **Pin d'origine — retrouvé le 2026-08-28 (chantier A35)** (2 connections) — `extern/moira/NEOST_VENDOR.md`
- **NEOST_VENDOR.md** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **ANDI/ORI/EORI #,SR/#,CCR et MOVE to CCR : double rechargement (2026-10-20)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- *... and 11 more nodes in this community*

## Relationships

- [Community 16](Community_16.md) (12 shared connections)
- [Community 314](Community_314.md) (8 shared connections)
- [Community 64](Community_64.md) (5 shared connections)
- [Community 58](Community_58.md) (2 shared connections)
- [Community 9](Community_9.md) (2 shared connections)
- [Community 299](Community_299.md) (2 shared connections)
- [Community 247](Community_247.md) (1 shared connections)
- [Community 366](Community_366.md) (1 shared connections)
- [Community 449](Community_449.md) (1 shared connections)
- [Community 54](Community_54.md) (1 shared connections)
- [Community 300](Community_300.md) (1 shared connections)
- [Community 205](Community_205.md) (1 shared connections)

## Source Files

- `docs/FX68K_BENCH.md`
- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `extern/moira/Moira/Moira.h`
- `extern/moira/NEOST_VENDOR.md`
- `tools/moira_bare_bench.cpp`

## Audit Trail

- EXTRACTED: 49 (60%)
- INFERRED: 33 (40%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*