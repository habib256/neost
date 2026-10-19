# Bus

> God node · 162 connections · `src/core/Bus.hpp`

**Community:** [Bus](Bus.md)

## Connections by Relation

### contains
- Bus.hpp `EXTRACTED`
- MmioSlot `EXTRACTED`
- [MegaSteCache](MegaSteCache.md) `EXTRACTED`

### defines
- watchHook `EXTRACTED`
- shifter `EXTRACTED`
- psg `EXTRACTED`
- glue `EXTRACTED`
- mfp `EXTRACTED`
- ikbd `EXTRACTED`
- fdc `EXTRACTED`
- dmasnd `EXTRACTED`
- blitter `EXTRACTED`
- blitterWinStart `EXTRACTED`
- blitterWinEnd `EXTRACTED`
- blitterCountCpu `EXTRACTED`
- rtc `EXTRACTED`
- midi `EXTRACTED`
- cpu `EXTRACTED`
- watchBase_ `EXTRACTED`
- gemdos `EXTRACTED`
- scc `EXTRACTED`
- ne2000 `EXTRACTED`
- isp1160 `EXTRACTED`
- *…and 22 more `defines` connection(s) not listed (lowest-degree first to go)*

### method
- .mmuTranslate() `EXTRACTED`
- .busFaultN() `EXTRACTED`
- .read8Slow() `EXTRACTED`
- .busFault() `EXTRACTED`
- .romWindowSize() `EXTRACTED`
- .mmioTable() `EXTRACTED`
- .buildIoFault() `EXTRACTED`
- .peek8() `EXTRACTED`
- .mmioSlotFor() `EXTRACTED`
- .rebuildMmuCache() `EXTRACTED`
- .busFaultNSlow() `EXTRACTED`
- .megaSteCacheFlush() `EXTRACTED`
- .megaSteCacheable() `EXTRACTED`
- .write8Slow() `EXTRACTED`
- .mmioRead8() `EXTRACTED`
- .mmioWrite8() `EXTRACTED`
- .mmioTableSize() `EXTRACTED`
- .seedResetVectors() `EXTRACTED`
- .busSelfTest() `EXTRACTED`
- .addCartDevice() `EXTRACTED`
- *…and 73 more `method` connection(s) not listed (lowest-degree first to go)*

### references
- [vector](vector.md) `EXTRACTED`
- Fpu `EXTRACTED`
- [StePads](StePads.md) `EXTRACTED`
- MachineType `EXTRACTED`
- [Scu](Scu.md) `EXTRACTED`
- [CartDevice](CartDevice.md) `EXTRACTED`
- [MmuGlue](MmuGlue.md) `EXTRACTED`
- [Shifter](Shifter.md) `EXTRACTED`
- [YM2149](YM2149.md) `EXTRACTED`
- [Mfp](Mfp.md) `EXTRACTED`
- [Ikbd](Ikbd.md) `EXTRACTED`
- [Fdc](Fdc.md) `EXTRACTED`
- [DmaSound](DmaSound.md) `EXTRACTED`
- Blitter `EXTRACTED`
- [Rtc](Rtc.md) `EXTRACTED`
- [MidiAcia](MidiAcia.md) `EXTRACTED`
- Cpu68k `EXTRACTED`
- [GemdosHd](GemdosHd.md) `EXTRACTED`
- Scc `EXTRACTED`
- [Ne2000](Ne2000.md) `EXTRACTED`
- *…and 4 more `references` connection(s) not listed (lowest-degree first to go)*

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*