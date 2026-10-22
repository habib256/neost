// =============================================================================
//  VideoGlue.cpp — la MACHINE À ÉTATS du GLUE vidéo (chantier A32, 2026-08-28).
//
//  Rôle 2 des six que portait Shifter.cpp : décider, ligne par ligne, où commence
//  et où finit le display-enable, quelles bordures sont retirées, et où tombe le
//  tic Timer B. C'est le port de Video_Update_Glue_State / Video_WriteToGlueRes
//  (Hatari video.c) — le cœur des tricks d'overscan.
//
//  Ces méthodes restent MEMBRES de Shifter : elles partagent son état par-ligne
//  (glueLines_, glueLineStart_, liveGlue*). Les séparer en une classe demande
//  d'abord de trancher qui possède cet état — c'est la suite du chantier, écrite
//  au TODO. Ce que ce fichier apporte tout de suite : on ouvre VideoGlue.cpp pour
//  une bordure, VideoCounter.cpp pour $FF8205/07/09, Shifter.cpp pour un pixel.
//
//  Les CONSTANTES et la table de timings vivent dans core/VideoGlue.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/ShifterInternal.hpp"
#include "core/VideoGlue.hpp"
#include "core/Shifter.hpp"
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Wakeup state STF tranché (WS3, NEOST_WS pour A/B) — exposé à Machine pour la
// position de l'IRQ HBL (cpl−4 en WS1, cpl sinon) et la VBL STF (60/64).
int Shifter::wakestate() { return glue::wakestate(); }

// Avance la machine Glue LIVE jusqu'à la ligne `targetLine` incluse : startHBL sur
// les lignes nouvellement atteintes + consommation des écritures freq/res déjà
// enregistrées, en ordre chronologique — exactement la boucle de replayGlue, mais
// au fil de la trame (les écritures arrivent triées : recordSyncWrite est daté live).
// Position ABSOLUE (cycle trame) du tic Timer B de la ligne `line`, sur la grille
// RÉELLE des débuts de ligne (glueLineStart_, canal NEOST_LINELEN_ATTR) quand elle est
// disponible — la même échelle que la lecture du compteur $8209. Renvoie -1 si la
// trame n'a pas de grille réelle (pas d'écritures freq/res, LINELEN off…) : la
// Machine retombe alors sur la planification nominale historique. Sans cette
// échelle, le balayage per-line de Closure (lignes 508) décalait le callback : aux
// phases défavorables il tombait AVANT l'écriture 60 Hz@374 → tic à la position
// par défaut (400) au lieu du DE réel (488) — 2 phases sur 5, mesuré vs oracle.
int64_t Shifter::timerBFrameCycleForLine(int line, bool startOfLine) {
    const bool lineLen = lineLenAttrEnv();
    static const bool dbg = std::getenv("NEOST_TBGRID_DIAG") != nullptr;
    if (!lineLen || frameMode_ == Mode::High || syncWrites_.empty()
        || line < 0 || static_cast<std::size_t>(line) >= glueLineStart_.size()) {
        if (dbg) std::fprintf(stderr, "[TBG] line=%d -1 (ll=%d hi=%d sw=%zu sz=%zu)\n",
                              line, lineLen ? 1 : 0, frameMode_ == Mode::High ? 1 : 0,
                              syncWrites_.size(), glueLineStart_.size());
        return -1;
    }
    const int pos = timerBPosForLine(line, startOfLine);   // fait le catch-up jusqu'à `line`
    if (liveGlueLine_ < line) {                            // grille pas encore construite
        if (dbg) std::fprintf(stderr, "[TBG] line=%d -1 (glueLine=%d)\n", line, liveGlueLine_);
        return -1;
    }
    if (dbg) std::fprintf(stderr, "[TBG] line=%d pos=%d start=%lld -> %lld (nominal %lld)\n",
                          line, pos, (long long)glueLineStart_[line],
                          (long long)(glueLineStart_[line] + pos),
                          (long long)(static_cast<int64_t>(line) * geometry().cyclesPerLine + pos));
    return glueLineStart_[static_cast<std::size_t>(line)] + pos;
}

// Cf. la déclaration (Shifter.hpp) : position du tic Timer B pour UNE ligne, DE
// réel de la machine Glue compris — port de Video_TimerB_GetPosFromDE appliqué à
// ShifterLines[n]. Hors trame à écritures freq/res : défaut global historique.
int Shifter::timerBPosForLine(int line, bool startOfLine) {
    if (frameMode_ != Mode::High && !syncWrites_.empty()
        && line >= 0 && static_cast<std::size_t>(line) + 1 < glueLines_.size()) {
        liveGlueCatchUp(line);
        const GlueLine& L = glueLines_[static_cast<std::size_t>(line)];
        if (L.displayStartCycle >= 0 && L.displayEndCycle > 0
            && !(L.borderMask & glue::NO_DE)) {
            constexpr int kOffset = 24;      // TIMERB_VIDEO_CYCLE_OFFSET
            return (startOfLine ? L.displayStartCycle : L.displayEndCycle) + kOffset;
        }
    }
    return timerBLinePos(startOfLine);
}

// Longueur de ligne IMPLIQUÉE par l'état freq/res au début d'une ligne — ce que
// Video_StartHBL pose dans nCyclesPerLine chez Hatari, ligne après ligne, écriture
// ou pas. Chantier V3 (2026-09-01) : jusqu'ici, sous NEOST_LINELEN_ATTR, la longueur
// retombait à cpl à CHAQUE ligne et n'était corrigée que par un « Freq_match » tombant
// SUR la ligne ; 140 lignes de 60 Hz sans écriture restaient donc à 512 et la grille
// réelle ne dérivait jamais — le verrou ne pouvait pas produire l'effet pour lequel
// il existe (mesuré sur l'exhibiteur make_freqswitch_test.py : le cycle bougeait de
// 4, la ligne jamais, alors qu'Hatari attribuait 1 à 2 lignes plus loin).
static inline int glueLineLenFor(int freqHz) {
    return (freqHz == 71) ? glue::CyclesLine_Hi
         : (freqHz == 60) ? glue::CyclesLine_60 : glue::CyclesLine_50;
}

void Shifter::liveGlueCatchUp(int targetLine) {
    if (frameMode_ == Mode::High || glueLines_.size() < 2) return;
    const int maxLine = static_cast<int>(glueLines_.size()) - 2;
    if (targetLine > maxLine) targetLine = maxLine;
    const int cpl = geometry().cyclesPerLine;
    // NEOST_LINELEN_ATTR : attribution des écritures à la grille RÉELLE (échelle des
    // débuts de ligne glueLineStart_, alimentée à chaque avance de ligne ;
    // longueur courante déplacée par les « Freq_match » via glueCyclesLine_).
    const bool lineLen = lineLenAttrEnv();
    for (;;) {
        // Écriture en attente sur une ligne déjà initialisée → consommer AVANT
        // d'avancer (elle conditionne res/freq des lignes suivantes).
        if (liveGlueWi_ < syncWrites_.size()) {
            const SyncWrite& w = syncWrites_[liveGlueWi_];
            int wl; int lc;
            if (lineLen) {
                // Ligne = position dans l'échelle réelle (recherche descendante
                // depuis la ligne courante — les écritures arrivent quasi triées).
                wl = liveGlueLine_ < 0 ? 0 : liveGlueLine_;
                while (wl > 0 && w.frameCycle < glueLineStart_[wl]) --wl;
                // L'écriture est-elle au-delà de la fin de la ligne courante ?
                // (fin = start + longueur courante) → il faut d'abord avancer.
                if (wl == liveGlueLine_ && liveGlueLine_ >= 0
                    && w.frameCycle >= glueLineStart_[wl] + liveGlueLen_ && liveGlueLine_ < targetLine) {
                    wl = liveGlueLine_ + 1;   // force l'avance ci-dessous
                }
                lc = static_cast<int>(w.frameCycle - glueLineStart_[wl]);
            } else {
                wl = static_cast<int>(w.frameCycle / cpl);
                lc = static_cast<int>(w.frameCycle % cpl);
            }
            if (wl > maxLine) { wl = maxLine; if (!lineLen) lc = static_cast<int>(w.frameCycle % cpl); }
            if (wl <= liveGlueLine_) {
                if (w.isRes) liveGlueRes_    = w.val & 0x03;
                else         liveGlueFreq50_ = (w.val & 0x02) ? 1 : 0;
                // La GLUE ne décode que le bit 1 de $FF8260 : res 3 = haute
                // résolution pour elle (Hatari Video_Update_Glue_State,
                // « IoMem[0xff8260] & 2 » — trick « stop the shifter » Troed/Sync).
                const int freqHz = (liveGlueRes_ & 0x02) ? 71 : (liveGlueFreq50_ ? 50 : 60);
                // DIAG (NEOST_GLUE_DIAG) : chaque écriture appliquée à la Glue, avec
                // sa datation ligne/cycle et le masque résultant — à diff'er contre
                // Hatari `video_border_h` (les « detect ... » de Video_Update_Glue_State).
                static const bool glueDiag = std::getenv("NEOST_GLUE_DIAG") != nullptr;
                updateGlueState(wl, lc, w.isRes, freqHz);
                // Canal HBL_Pos/nCyclesPerLine → Machine (reprogramme l'IRQ HBL de la
                // ligne et cumule le raccourcissement) — gated NEOST_LINELEN côté Machine.
                if (glueHblPos_ > 0 && lineGeom_) lineGeom_(wl, glueHblPos_, glueCyclesLine_);
                // La longueur de la ligne COURANTE suit le dernier « Freq_match ».
                if (lineLen && wl == liveGlueLine_ && glueCyclesLine_ > 0) liveGlueLen_ = glueCyclesLine_;
                if (glueDiag)
                    std::fprintf(stderr, "[GLUP] line=%d cyc=%d %s freq=%d -> mask=%03x de=%d..%d"
                                 " startHBL=%d endHBL=%d vo=%02x refresh=%d\n",
                                 wl, lc, w.isRes ? "res" : "sync", freqHz,
                                 glueLines_[wl].borderMask, glueLines_[wl].displayStartCycle,
                                 glueLines_[wl].displayEndCycle,
                                 glueStartHBL_, glueEndHBL_, glueVOverscan_, nScreenRefreshRate_);
                ++liveGlueWi_;
                continue;
            }
        }
        if (liveGlueLine_ >= targetLine) break;
        // Avance d'une ligne : mémorise le début RÉEL de la nouvelle ligne
        // (start précédent + longueur réelle) et repart à la longueur nominale.
        const int64_t prevStart = (liveGlueLine_ >= 0 && lineLen) ? glueLineStart_[liveGlueLine_] : 0;
        // DIAG (NEOST_LLEN_DUMP=1) : longueur RÉELLE de la ligne qui se termine —
        // à diff'er contre les « HBL n start= » d'Hatari (--trace video_hbl).
        static const bool llenDump = std::getenv("NEOST_LLEN_DUMP") != nullptr;
        if (llenDump && lineLen && liveGlueLine_ >= 0 && liveGlueLen_ != cpl)
            std::fprintf(stderr, "[LLD] line=%d len=%d\n", liveGlueLine_, liveGlueLen_);
        ++liveGlueLine_;
        if (lineLen) {
            if (static_cast<std::size_t>(liveGlueLine_) < glueLineStart_.size())
                glueLineStart_[liveGlueLine_] = (liveGlueLine_ == 0) ? 0 : prevStart + liveGlueLen_;
            liveGlueLen_ = glueLineLenFor(((liveGlueRes_ & 0x03) == 0x02) ? 71
                                         : (liveGlueFreq50_ ? 50 : 60));
        }
        // ⚠ Décode de Video_StartHBL : hi SEULEMENT si (res & 3) == 2 — res 3 suit
        // le bit freq de $FF820A (video.c:3541-3581). Le décode « bit 1 = 71 Hz »
        // (res & 2, utilisé par updateGlueState) enverrait une ligne res 3 d'un
        // écran 50 Hz dans la branche 60 Hz (DE 52-372 + left+2/right-2).
        const int freqHz = ((liveGlueRes_ & 0x03) == 0x02) ? 71 : (liveGlueFreq50_ ? 50 : 60);
        startHBL(liveGlueLine_, liveGlueRes_, freqHz);
    }
}


// Video_Update_Glue_State mémorise FreqHz aux quatre comparaisons VBlank ;
// Video_EndHBL applique le résultat à la ligne SUIVANTE (cycle de test fixe502,
// indépendant du wakestate). Le même réducteur sert au compteur live et au rendu.
bool Shifter::vblankAtLine(int scanline) const {
    if (frameMode_ == Mode::High) return false;   // CopyScreenLineMono ignore VBlank
    struct Check { int line, cycle, freq; bool signal, vsync; };
    const int restartPos = machineIsSte(bus_.machine) ? 60 : 56;
    const Check checks[] = {{15,502,60,false,false}, {24,502,50,false,false},
                            {257,502,60,true,false}, {260,restartPos,60,true,true},
                            {307,502,50,true,false}, {310,restartPos,50,true,true}};
    // Video_ClearOnVBL appelle ResetShifterTimings à chaque VBL : ON en couleur,
    // OFF en mono, même si tous les checks ON de la trame précédente ont raté.
    bool signal = true;
    const int cpl = geometry().cyclesPerLine;
    for (const auto& check : checks) {
        if (scanline < check.line || (!check.vsync && scanline == check.line)) break;
        // VSYNC260 n'est testé que sur une trame60Hz ; VSYNC310 selon le bit
        // freq au restart, quelle que soit la résolution courante (video.c:3280).
        if (check.vsync && check.line == 260 && (frameSync_ & 2)) continue;
        int64_t start = int64_t(check.line) * cpl;
        int64_t end = start + cpl;
        // Une ligne raccourcie/longue conserve sa position dans la grille V3.
        // En live le HBL a déjà construit le début de la ligne suivante ; le
        // replay conserve cette grille, comme le lecteur du compteur vidéo.
        if (lineLenAttrEnv() && check.line + 1 < static_cast<int>(glueLineStart_.size())
            && glueLineStart_[check.line] > 0) {
            start = glueLineStart_[check.line];
            if (glueLineStart_[check.line + 1] > start) end = glueLineStart_[check.line + 1];
            else end = start + cpl;
        }
        int res = static_cast<int>(frameMode_) & 3;
        int freq50 = (frameSync_ & 2) != 0;
        // ResetShifterTimings initialise les latches par la fréquence de trame
        // (res3 est couleur ici). Après une écriture, Update_Glue_State décode
        // res&2 et res3 devient71Hz pour ces comparateurs, comme chez Hatari.
        int freq = freq50 ? 50 : 60;
        for (const auto& w : syncWrites_) {
            if (w.frameCycle >= end || w.frameCycle > start + check.cycle + 1) break;
            // STF : la résolution est latchée un cycle avant la fréquence.
            const int lead = w.isRes && !check.vsync && !machineIsSte(bus_.machine) ? 1 : 0;
            if (w.frameCycle > start + check.cycle + lead) continue;
            if (w.isRes) res = w.val & 3;
            else freq50 = (w.val & 2) != 0;
            freq = (res & 2) ? 71 : (freq50 ? 50 : 60);
        }
        if (check.vsync) freq = freq50 ? 50 : 60;
        if (freq == check.freq) signal = check.signal;
    }
    return signal;
}

int Shifter::glueLineBytes(int scanline) const {
    const int bpl = (frameMode_ == Mode::High) ? 80 : 160;
    // VBlank efface la ligne SANS lecture RAM ni avance pVideoRaster, à la
    // différence d'une ligne BLANK (video.c:3993/4120).
    if (vblankAtLine(scanline)) return 0;
    if (frameMode_ == Mode::High || syncWrites_.empty()) return bpl;
    if (scanline < 0 || scanline + 1 >= static_cast<int>(glueLines_.size())) return bpl;
    const GlueLine& L = glueLines_[static_cast<std::size_t>(scanline)];
    if (L.displayStartCycle < 0) return bpl;             // ligne sans état glue calculé
    if (L.borderMask & glue::NO_DE) return 0;            // ligne sans display-enable
    // DE vertical jamais activé de toute la trame : Hatari efface la ligne SANS
    // avancer pVideoRaster (video.c:3988) — le compteur reste donc figé sur la base
    // vidéo, ce qu'un programme relit en $FF8205/07/09 pour savoir si son retrait
    // de bordure haute a échoué.
    if (glueVOverscan_ & glue::VO_NO_DE) return 0;
    // Hors de la fenêtre verticale [nStartHBL, nEndHBL + BlankLines) : même branche
    // « total blank line » d'Hatari, qui n'avance pas non plus pVideoRaster
    // (video.c:3985). Le garde-fou de commitScanline ne couvrait QUE le cas d'une
    // fenêtre plus LONGUE que curAH_ (bordure basse retirée) car il est imbriqué
    // dans `vcLineY_ >= curAH_` ; une fenêtre plus COURTE — VO_BOTTOM_SHORT_50, un
    // écran 50 Hz qui se termine à la position 60 Hz, soit 29 lignes de moins —
    // laissait le compteur avancer sur des lignes non affichées. On borne ici
    // plutôt que dans la boucle de commit : la CADENCE de capture lineSnap_ reste
    // intacte (c'est elle qui tient les sprites d'Enchanted Land et l'ancre de
    // Lethal Xcess), seul le stride devient nul, comme chez Hatari.
    if (scanline < glueStartHBL_ || scanline >= glueEndHBL_ + glueBlankLines_) return 0;
    int bytes = 160;                                     // BORDERBYTES_NORMAL
    const uint32_t bm = L.borderMask;
    if (bm & (glue::LEFT_OFF | glue::LEFT_OFF_MED)) bytes += 26;   // BORDERBYTES_LEFT (hi/lo OU hi/med)
    else if (bm & (glue::LEFT_OFF_2_STE | glue::LEFT_OFF_2_STE_MED)) bytes += 20;   // BORDERBYTES_LEFT_2_STE
    else if (bm & glue::LEFT_PLUS_2)   bytes += 2;
    if (bm & glue::STOP_MIDDLE)        bytes -= 106;
    else if (bm & glue::RIGHT_MINUS_2) bytes -= 2;
    else if (bm & glue::RIGHT_OFF)     bytes += 44;      // BORDERBYTES_RIGHT
    if (bm & glue::RIGHT_OFF_FULL)     bytes += 22;      // BORDERBYTES_RIGHT_FULL
    // DIAG (NEOST_GLUE_DIAG) : décision Glue par ligne à tricks — à diff'er contre
    // la trace Hatari `video_res,video_sync` (detect remove/extend border).
    static const bool glueDiag = std::getenv("NEOST_GLUE_DIAG") != nullptr;
    if (glueDiag && bytes != 160)
        std::fprintf(stderr, "[GLUE] line=%d mask=%03x de=%d..%d bytes=%d\n",
                     scanline, L.borderMask, L.displayStartCycle, L.displayEndCycle, bytes);
    return bytes;
}

// Datation d'une écriture freq/res (Glue OU Shifter) : cycle trame, −1 si hors
// trame. Port de Video_GetPosition_OnWriteAccess, commun à Video_WriteToGlueRes et
// Video_WriteToShifterRes — même loi de datation pour $FF820A, $FF8260 et $FF8261.
int64_t Shifter::datedResWriteCycle() const {
    if (!liveFrameClock_) return -1;
    int64_t fc = liveFrameClock_();
    if (fc < 0) return -1;
    // Datation par PARITÉ de la position de l'accès dans l'instruction (défaut ;
    // NEOST_SYNC_MODE=0 restaure le « fcRaw + 2 » constant pour l'A/B). Loi Hatari
    // CE (Cycles_GetInternalCycleOnWriteAccess : « currcycle + 4 » = position
    // WinUAE de l'accès + 4) transposée à Moira : pour les accès que Moira place à
    // into ≡ 2 (mod 4) — la classe historique : move Dn,(An)/abs, l'écrasante
    // majorité — l'accès WinUAE est 2 cyc PLUS TÔT → datation = fcRaw + 2
    // (l'ancienne constante, tout le parc calibré inchangé : nocooper oracle 0 px
    // exige start+8 pour les move vers abs.w, into=6 → +2 ✓). Pour into ≡ 0
    // (mod 4) — move An,(An) du classificateur de wakeup state de Closure — Moira
    // place l'accès PILE où WinUAE le date → +0 (l'ancien +2 donnait 56 >
    // Line_Set_Pal 55 → Freq_match refusé → ligne 64 restée 508 → grille −4 →
    // right-off manqué → delta $A2 → verdict 0 → opcode $19C0 illégal → crash).
    // Mesures : 60Hz into=2 : 38+2=40 = oracle ; 50Hz into=4 : 54+0=54 = oracle.
    // Chaîne causale complète → docs/CLOSURE_CHANTIER.md § Cycles 4-5.
    static const int syncMode = [] { const char* s = std::getenv("NEOST_SYNC_MODE");
                                     return s ? std::atoi(s) : 1; }();
    if (syncMode == 1 && bus_.cpu) {
        fc += (bus_.cpu->cyclesIntoInstr() & 2) ? kSyncWriteOffsetCyc : 0;
    } else {
        fc += kSyncWriteOffsetCyc;
    }
    // DEBUG (NEOST_SYNC_OFF) : offset ADDITIONNEL de datation des écritures freq/res —
    // sert à mesurer un écart systématique de datation CPU↔glue contre l'oracle.
    static const int syncOff = [] { const char* s = std::getenv("NEOST_SYNC_OFF"); return s ? std::atoi(s) : 0; }();
    fc += syncOff;
    if (fc < 0) fc = 0;
    return fc;
}

void Shifter::recordSyncWrite(bool isRes, uint8_t val) {
    const int64_t fc = datedResWriteCycle();
    if (fc < 0) return;
    // DEBUG (NEOST_SYNC_TRACE) : trace TOUTES les écritures freq/res (ligne/cycle) pour
    // diagnostiquer la détection des retraits de bordure (≠ NEOST_BORDER_TRACE qui n'émet
    // QUE si un trick est déjà armé). Cf. menu Cuddly : retrait bordure basse non détecté.
    if (std::getenv("NEOST_SYNC_TRACE")) {
        const int cpl = geometry().cyclesPerLine;
        std::fprintf(stderr, "[SYNC] %s val=%02x line=%lld cyc=%lld pc=%06x\n",
            isRes ? "res " : "freq", val, static_cast<long long>(fc / cpl),
            static_cast<long long>(fc % cpl), bus_.cpu ? bus_.cpu->pc() : 0);
    }
    // DIAG (NEOST_WRITE_DIAG) : tranche le modèle de datation. fcRaw = liveFrameClock
    // (avant offset) ; into = cycles BUS écoulés DANS l'instruction au write8 (≈ P+2 si
    // liveFrameClock est LIVE, ≈0 s'il est épinglé au début d'instr). Permet de comparer
    // au modèle Hatari CE (fin d'accès = fcRaw + 2).
    // CONCLU (2026-06-18) : into=2..16 selon l'instr → liveFrameClock est LIVE, capte DÉJÀ
    // la position sous-instr → la datation faithful Hatari-CE = fcRaw+2 CONSTANT (pas
    // par-instruction). Cf. docs/MOIRA_WINUAE_CONVERGENCE.md §7 : datation par-instruction
    // RÉFUTÉE. Le résidu EL = un stabilisateur nop-slide qui ne verrouille pas (videoCounter
    // read daté +10 vs Hatari) + hacks write(+16 ≥+14 loader)/read(+4) co-calibrés → refonte
    // coordonnée, pas la datation d'écriture seule.
    if (std::getenv("NEOST_WRITE_DIAG") && bus_.cpu) {
        const int cpl = geometry().cyclesPerLine;
        const int64_t fcRaw = liveFrameClock_();
        const int64_t into = bus_.cpu->cyclesIntoInstr();
        std::fprintf(stderr, "[WDIAG] %s val=%02x line=%lld cyc=%lld into=%lld pc=%06x\n",
            isRes ? "res " : "freq", val, static_cast<long long>(fcRaw / cpl),
            static_cast<long long>(fcRaw % cpl), static_cast<long long>(into),
            bus_.cpu->pc());
    }
    // Filtre Hatari « écriture de la même valeur ignorée » : Video_Sync_WriteByte
    // compare Freq = $FF820A & 2 à ShifterFrame.Freq (video.c:3055-3057) et
    // Video_WriteToGlueRes compare Res = $FF8260 & 3 à ShifterFrame.Res
    // (video.c:1629-1631) AVANT Video_Update_Glue_State — une réécriture
    // redondante n'atteint jamais la machine Glue. Sans ce filtre, réécrire #0
    // dans $FF820A en 60 Hz déclenchait un RIGHT_MINUS_2 parasite (ligne 158
    // octets). Le dernier Freq/Res vu PERSISTE à travers les trames (remis à −1
    // au seul reset, comme ShifterFrame). Restent HORS filtre, comme Hatari : le
    // wait state bus et le stockage du registre (`sync = v` / `mode = ...` dans
    // write8, ≙ IoMem qui garde la valeur) et les traces ci-dessus (≙ LOG_TRACE,
    // émis avant le filtre).
    if (isRes) {
        const int res = val & 0x03;
        if (res == lastGlueRes_) return;                 // même Res → Glue inchangée
        lastGlueRes_ = res;
    } else {
        const int freq = val & 0x02;
        if (freq == lastGlueFreq_) return;               // même Freq → Glue inchangée
        lastGlueFreq_ = freq;
    }
    syncWrites_.push_back({ static_cast<int32_t>(fc), val, isRes });
    updateLiveStartHBL(static_cast<int32_t>(fc), isRes, val);   // VDE_On live (retrait haut)
    // Machine Glue LIVE : consomme l'écriture immédiatement (fenêtre DE de la ligne
    // courante à jour pour les lectures $FF8209 qui suivent).
    liveGlueCatchUp(static_cast<int>(fc / geometry().cyclesPerLine));
}

// Écriture de la résolution CÔTÉ SHIFTER ($FF8260 ou $FF8261) — port de
// Video_WriteToShifterRes (video.c:1888-2006). La valeur 3 ARRÊTE le Shifter (il
// n'absorbe plus les mots que le MMU continue de lui livrer). Trick Troed/Sync
// « scroll hard 4 px sans retrait de bordure » (STF) : res=3 posé quand le DE est
// actif (cycle-ligne ≥ 64), retour au cycle-ligne ≥ 72 et au plus 32 cycles après, en
// basse/moyenne → le Shifter a perdu 0..3 mots : toutes les lignes SUIVANTES
// sortent décalées de 0/4/8/12 px à gauche, et le compteur prend −0/−2/−4/−6
// octets en fin de ligne courante. La ligne du stop elle-même n'est pas décalée.
// Positions arrondies au créneau bus de 4 cycles, comme Hatari. ⚠ Hatari (et donc
// NeoST) ne montre pas les pixels noirs pendant l'arrêt et ne garde le décalage que
// pour la trame courante (TODO video.c:1914-1915) ; aucune distinction STF/STE n'y
// est faite non plus — le comportement STE du trick n'est pas documenté.
void Shifter::writeShifterRes(uint8_t res) {
    res &= 0x03;
    shifterRes_ = res;                                   // registre (relu par $FF8260/61)
    if (static_cast<int>(res) == lastShifterRes_) return;   // même valeur → rien (video.c:1904)
    const int64_t fc  = datedResWriteCycle();
    int line = -1, lineCyc = -1;
    if (fc >= 0) realLinePos(fc, line, lineCyc);
    // Le « ≤ 32 » d'Hatari ne compare que des cycles-LIGNE (ResPosStop_Shifter.
    // LineCycles) : un arrêt posé à la ligne N et levé à la ligne N+1 (des centaines
    // de cycles) passe le test avec une durée négative ou faussement courte, et la
    // normalisation modulo 16 en faisait un décalage −4/−8/−12 px. On exige la MÊME
    // ligne : la durée réelle est alors ≤ 32 cycles, ce que décrit le trick
    // Troed/Sync. ⚠ Divergence assumée : sur une durée négative Hatari réécrit
    // DisplayPixelShift = 0 sur les lignes suivantes (aucun cas du « % 16 » signé ne
    // tombe) ; sur une durée positive trans-ligne il arme un décalage. Le
    // comportement matériel d'un arrêt aussi long n'est pas documenté : on n'arme rien.
    if (lastShifterRes_ == 3 && fc >= 0
        && line == shifterResLine_                       // même ligne (durée réelle)
        && shifterResCyc_ >= 64                          // arrêt posé DE actif
        && lineCyc >= 64 + 8                             // retour ≥ 72 (« au moins 8 cycles » depuis 64)
        && lineCyc - shifterResCyc_ <= 32) {             // et au plus 32
        const int posOld  = (shifterResCyc_ + 3) & ~3;
        const int posNew  = (lineCyc + 3) & ~3;
        const int stopped = posNew - posOld;             // ≥ 0 : même ligne
        int shift = 0, inc = 0;
        switch (stopped % 16) {
            case 4:  inc = -6; shift = -12; break;       // 88 + 16n
            case 0:  inc =  0; shift =   0; break;       // 84 + 16n
            case 12: inc = -2; shift =  -4; break;       // 80 + 16n
            case 8:  inc = -4; shift =  -8; break;       // 76 + 16n
            default: break;
        }
        if (inc != 0) vcStopInc_ = inc;                  // ≙ VideoRasterDelayedInc
        shifterStopLine_  = line;                        // lignes > line décalées
        shifterStopShift_ = shift;
        if (std::getenv("NEOST_SYNC_TRACE"))
            std::fprintf(stderr, "[SYNC] shifter stop line=%d cyc=%d..%d shift=%d inc=%d\n",
                         line, shifterResCyc_, lineCyc, shift, inc);
    }
    // Intervalle « Shifter arrêté » (rendu noir, cf. lineStoppedOver).
    if (res == 3) {
        stopFromFc_ = (fc >= 0) ? fc : 0;
    } else if (lastShifterRes_ == 3 && stopFromFc_ >= 0) {
        lastStopFromFc_ = stopFromFc_;
        lastStopToFc_   = fc;                            // −1 hors trame : ne couvre rien
        stopFromFc_     = -1;
    }
    lastShifterRes_ = res;
    shifterResCyc_  = lineCyc;                           // ≙ ResPosStop_Shifter.LineCycles
    shifterResLine_ = line;                              // ≙ ResPosStop_Shifter.HBL
}

// Datation sur la grille RÉELLE (port de Video_ConvertPosition, video.c:1224 :
// LineCycles = FrameCycles − ShifterLines[nHBL].StartCycle) : la machine Glue
// attribue les mêmes écritures freq/res sur l'échelle glueLineStart_ (lignes de 508
// cycles d'un moteur sync-scroll dans une trame 50 Hz…). La grille nominale fc/cpl
// dérivait de 4 cycles par ligne courte : seuils « DE actif » (64/72) évalués à la
// mauvaise position, ligne du stop décalée d'une ligne entière à terme. Sans
// écriture Glue dans la trame, les deux grilles coïncident.
void Shifter::realLinePos(int64_t fc, int& line, int& lineCyc) {
    const int cpl = geometry().cyclesPerLine;
    line    = static_cast<int>(fc / cpl);
    lineCyc = static_cast<int>(fc % cpl);
    if (!lineLenAttrEnv() || frameMode_ == Mode::High || syncWrites_.empty()
        || static_cast<std::size_t>(line) + 2 >= glueLineStart_.size())
        return;
    liveGlueCatchUp(line);                               // même rattrapage que recordSyncWrite
    if (liveGlueLine_ < 0) return;
    int wl = liveGlueLine_;
    while (wl > 0 && fc < glueLineStart_[static_cast<std::size_t>(wl)]) --wl;
    int64_t start = glueLineStart_[static_cast<std::size_t>(wl)];
    // Au-delà de la fin de la ligne Glue courante (grille réelle en avance sur la
    // nominale) : ligne suivante, sans l'initialiser (≙ test nCyclesPerLine de
    // Video_ConvertPosition, HBL pas encore traité).
    if (wl == liveGlueLine_ && fc >= start + liveGlueLen_) { ++wl; start += liveGlueLen_; }
    line    = wl;
    lineCyc = static_cast<int>(fc - start);
}

// Ligne « Shifter arrêté » d'après la fenêtre DE de CETTE scanline : noire si un
// arrêt (en cours, ou le dernier terminé) couvre tout [début de ligne + ds, + de).
// L'échantillon de shifterRes_ au commit (HBL, APRÈS le DE) attribuait l'arrêt à la
// mauvaise ligne : posé après la fin du DE de N et levé après celle de N+1, il
// noircissait N (intacte sur le matériel) et laissait N+1 visible. Un arrêt
// partiel (trick Troed/Sync, 8..32 cycles) ne noircit pas la ligne — comme Hatari,
// qui ne montre pas ces pixels (TODO video.c:1914).
bool Shifter::lineStoppedOver(int sl) const {
    if (stopFromFc_ < 0 && lastStopFromFc_ < 0) return false;
    const Geometry g = geometry();
    int64_t start = static_cast<int64_t>(sl) * g.cyclesPerLine;
    int ds = g.lineStartCycle, de = g.lineEndCycle;
    if (frameMode_ != Mode::High && !syncWrites_.empty() && sl >= 0 && sl <= liveGlueLine_
        && static_cast<std::size_t>(sl) < glueLines_.size()) {
        if (lineLenAttrEnv() && static_cast<std::size_t>(sl) < glueLineStart_.size())
            start = glueLineStart_[static_cast<std::size_t>(sl)];
        const GlueLine& L = glueLines_[static_cast<std::size_t>(sl)];
        if (L.displayEndCycle > L.displayStartCycle) { ds = L.displayStartCycle; de = L.displayEndCycle; }
    }
    const int64_t w0 = start + ds, w1 = start + de;
    if (stopFromFc_ >= 0 && stopFromFc_ <= w0) return true;          // arrêt toujours en cours
    return lastStopFromFc_ >= 0 && lastStopFromFc_ <= w0 && lastStopToFc_ >= w1;
}

// Met à jour le VDE_On LIVE du compteur vidéo sur une écriture freq — détection du
// RETRAIT de bordure HAUTE (port du comportement de Hatari Video_Update_Glue_State /
// nStartHBL, video.c ~2895). Sur le vrai matériel, une bascule 60 Hz pendant la
// bordure haute (avant VDE_On 50 Hz = ligne 63) ouvre le haut de l'écran : la 1ʳᵉ
// ligne affichée passe à 34 (VDE_On 60 Hz) et le compteur d'adresse vidéo commence
// donc à monter dès la ligne 34 au lieu de 63. C'est exactement ce dont dépendent les
// boucles d'auto-synchro fullscreen (Cuddly Demo) qui sondent $FF8209 pour se caler.
//
// Modèle (approximation fidèle au RÉSULTAT) : toute bascule 60 Hz dans la bordure haute
// VERROUILLE VDE_On=34 pour la trame — on ne fait que BAISSER (jamais remonter). En
// effet la boucle d'auto-synchro toggle 60→50 Hz à chaque itération ; sur le matériel
// la décision est latchée au passage de la ligne et le 50 Hz qui suit ne la ré-ferme
// pas. Un retrait gauche/droite (sur les lignes AFFICHÉES ≥ 63) ou bas (ligne 262)
// n'entre pas dans la fenêtre [0,63) → non concerné. Un écran 50 Hz ordinaire ne fait
// AUCUNE bascule freq → liveStartHBL_ reste 63 (zéro régression). On NE touche QUE
// liveStartHBL_ (lu par videoCounter) ; la géométrie de rendu (replayGlue) est inchangée.
void Shifter::updateLiveStartHBL(int32_t frameCycle, bool isRes, uint8_t val) {
    if (frameMode_ == Mode::High) return;            // mono : pas concerné
    if (isRes) return;                               // seules les bascules freq bougent VDE_On ici
    const Geometry g = geometry();
    const int line = frameCycle / g.cyclesPerLine;
    constexpr int VDE_On_50 = 63, VDE_On_60 = 34;
    const bool freq60 = !(val & 0x02);               // bit1=0 → 60 Hz
    if (freq60 && line < VDE_On_50 && VDE_On_60 < liveStartHBL_)
        liveStartHBL_ = VDE_On_60;                   // retrait haut verrouillé (sticky)
}

// Scanline affichée d'après la machine Glue LIVE (cf. déclaration).
bool Shifter::liveLineDisplayed(int line) {
    if (frameMode_ != Mode::High && !syncWrites_.empty()
        && static_cast<std::size_t>(line) + 1 < glueLines_.size()) {
        liveGlueCatchUp(line);
        // Fenêtre verticale [nStartHBL, nEndHBL + BlankLines) comme Hatari
        // (video.c:3649) : les lignes blanches no-sync repoussent la fin d'autant.
        // Le même test d'Hatari exclut aussi la trame sans DE vertical.
        if (glueVOverscan_ & glue::VO_NO_DE) return false;
        if (line < glueStartHBL_ || line >= glueEndHBL_ + glueBlankLines_) return false;
        return !(glueLines_[static_cast<std::size_t>(line)].borderMask & glue::NO_DE);
    }
    const Geometry g = geometry();                       // fenêtre nominale (zéro trick)
    return line >= g.dispStartLine && line < g.dispStartLine + g.displayLines;
}

// Valeurs par défaut d'une scanline selon res/freq COURANTS au début de la ligne
// (port de Hatari Video_StartHBL). DisplayStartCycle n'est posé que s'il vaut -1
// (une écriture de la ligne précédente a pu le pré-positionner : right-off full).
void Shifter::startHBL(int line, int curRes, int freqHz) {
    // ⚠ GARDE DE BORNES. `line` vient soit du curseur live liveGlueLine_ (restauré
    // d'un save-state), soit de la boucle de replay bornée par lpf — deux valeurs
    // qu'un fichier forgé désynchronise de glueLines_. Sans cette garde, le
    // « L.borderMask |= … » ci-dessous est un read-modify-write à un offset
    // arbitraire du tas (12 octets par unité), répété sur toute la plage rattrapée.
    if (line < 0 || static_cast<std::size_t>(line) >= glueLines_.size()) return;
    const glue::Timing& T = glue::timing(bus_.machine);
    GlueLine& L = glueLines_[line];
    if (curRes == 2) {                                   // haute résolution (71 Hz)
        if (L.displayStartCycle == -1) L.displayStartCycle = T.HDE_On_Hi;   // 4 (+inc)
        L.displayEndCycle = T.HDE_Off_Hi;                // 164 (+inc)
        if (nScreenRefreshRate_ != 71) {                 // ligne hi dans écran non-71 → retrait gauche par défaut
            L.borderMask |= glue::LEFT_OFF;
            L.displayPixelShift = -4;
        }
    } else if (freqHz == 50) {
        if (L.displayStartCycle == -1) L.displayStartCycle = T.HDE_On_Low_50;  // 56 (+inc)
        L.displayEndCycle = T.HDE_Off_Low_50;            // 376 (+inc)
    } else {                                             // 60 Hz
        if (L.displayStartCycle == -1) L.displayStartCycle = T.HDE_On_Low_60;  // 52 (+inc)
        L.displayEndCycle = T.HDE_Off_Low_60;            // 372 (+inc)
        if (nScreenRefreshRate_ == 50)                   // ligne 60 Hz dans écran 50 Hz → left+2/right-2
            L.borderMask |= (glue::LEFT_PLUS_2 | glue::RIGHT_MINUS_2);
    }
}

// Port FIDÈLE de Video_Update_Glue_State (chemins STF ET STE — video.c:2244-2652) :
// applique une écriture freq/res au cycle `lineCycles` de la scanline `line`. Met à
// jour la GlueLine (DE start/end, BorderMask, PixelShift), les lignes voisines
// (right-off full), et les bordures haut/bas (glueStartHBL_/glueEndHBL_ +
// glueVOverscan_). La PHASE 1 (freq avant DE_start) diffère par machine (le GST MCU
// STE teste les positions de PRELOAD du MMU) ; les phases 2/3 sont communes.
void Shifter::updateGlueState(int line, int lineCycles, bool writeToRes, int freqHz) {
    using namespace glue;
    const bool  ste = machineIsSte(bus_.machine);
    const Timing& T = timing(bus_.machine);
    // Positions de la table machine (shadowent les noms du namespace : le corps
    // reste la transcription ligne-à-ligne de video.c).
    const int HDE_On_Hi         = T.HDE_On_Hi;
    const int HBlank_Off_Low_60 = T.HBlank_Off_Low_60;
    const int HBlank_Off_Low_50 = T.HBlank_Off_Low_50;
    const int HDE_On_Low_60     = T.HDE_On_Low_60;
    const int Line_Set_Pal      = T.Line_Set_Pal;
    const int HDE_On_Low_50     = T.HDE_On_Low_50;
    const int HDE_Off_Hi        = T.HDE_Off_Hi;
    const int HDE_Off_Low_60    = T.HDE_Off_Low_60;
    const int HDE_Off_Low_50    = T.HDE_Off_Low_50;
    const int HSync_On_Off_Low  = T.HSync_On_Off_Low;
    const int HSync_Off_Off_Low = T.HSync_Off_Off_Low;
    const int RemoveTopBorder_Pos    = T.RemoveTopBorder_Pos;
    const int RemoveBottomBorder_Pos = T.RemoveBottomBorder_Pos;
    const int Hbl_Pos_Hi     = T.Hbl_Pos_Hi;
    const int Hbl_Pos_Low_60 = T.Hbl_Pos_Low_60;
    const int Hbl_Pos_Low_50 = T.Hbl_Pos_Low_50;

    // GLUE STF latche la res 1 cyc avant la freq — PAS le GST MCU du STE
    // (video.c:2220-2225 : « this is not the case for the STE GST MCU »).
    if (writeToRes && !ste) lineCycles--;

    GlueLine& GL = glueLines_[line];
    int DE_start = GL.displayStartCycle;
    int DE_end   = GL.displayEndCycle;
    uint32_t BorderMask = GL.borderMask;
    bool Freq_match_found = false;
    const int cpl = geometry().cyclesPerLine;
    // Canal HBL_Pos/nCyclesPerLine (video.c 2231-2232 + application 2849-2877) :
    // posé par les branches « Freq_match » de phase 1 ci-dessous, lu par l'appelant
    // (liveGlueCatchUp → Machine, replayGlue → attribution) après chaque écriture.
    glueHblPos_     = -1;
    glueCyclesLine_ = -1;

    // ===== Phase 1 : valeur de Freq AVANT DE_start — PAR MACHINE =====
    if (!ste) {
    // ----- STF (video.c 2244-2438) -----
    if (freqHz == 71 && lineCycles <= HDE_On_Hi) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) {
            DE_start = HDE_On_Hi; DE_end = HDE_Off_Hi;
            BorderMask |= LEFT_OFF; GL.displayPixelShift = -4;
            BorderMask &= ~LEFT_PLUS_2;
        }
    } else if (freqHz == 71 && lineCycles <= HBlank_Off_Low_50) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Hi; BorderMask |= (BLANK | NO_DE); }
        BorderMask &= ~LEFT_PLUS_2;
    } else if (freqHz == 71 && lineCycles <= HDE_On_Low_50) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Hi; BorderMask |= NO_DE; }
        BorderMask &= ~LEFT_PLUS_2;
    } else if (freqHz != 71) {
        if (lineCycles <= HDE_On_Hi && (BorderMask & LEFT_OFF)) {
            if (freqHz == 50) DE_start = HDE_On_Low_50;
            else { DE_start = HDE_On_Low_60; BorderMask |= LEFT_PLUS_2; }
            BorderMask &= ~LEFT_OFF; GL.displayPixelShift = 0;
        }
        if (lineCycles <= HBlank_Off_Low_50 && (BorderMask & (BLANK | NO_DE)) && !(BorderMask & NO_COUNT)) {
            BorderMask &= ~(BLANK | NO_DE);
        } else if (lineCycles <= HDE_On_Low_50 && (BorderMask & NO_DE) && !(BorderMask & BLANK) && !(BorderMask & NO_COUNT)) {
            BorderMask &= ~NO_DE;
        }
    }

    // Ligne 50 Hz qui continue en 60 Hz (et réciproque) — video.c 2342-2421
    if (freqHz == 60 && lineCycles < Line_Set_Pal) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_60; glueCyclesLine_ = CyclesLine_60;
        if (!(BorderMask & NO_DE)) {
            if (DE_start > 0) { DE_end = HDE_Off_Low_60; BorderMask |= RIGHT_MINUS_2; }
            if (lineCycles > HBlank_Off_Low_60 && lineCycles <= HBlank_Off_Low_50) BorderMask |= BLANK;
            if (DE_start == HDE_On_Low_50) { DE_start = HDE_On_Low_60; BorderMask |= LEFT_PLUS_2; }
        }
    } else if (freqHz == 50 && lineCycles <= HDE_On_Low_60) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_50; glueCyclesLine_ = CyclesLine_50;
        if (!(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Low_50;
            BorderMask &= ~RIGHT_MINUS_2;
            if (DE_start == HDE_On_Low_60) { DE_start = HDE_On_Low_50; BorderMask &= ~LEFT_PLUS_2; }
        }
    } else if (freqHz == 50 && lineCycles <= Line_Set_Pal) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_50; glueCyclesLine_ = CyclesLine_50;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Low_50; BorderMask &= ~RIGHT_MINUS_2; }
    }

    if (freqHz == 60 && lineCycles > HDE_On_Low_60 && lineCycles <= HDE_On_Low_50 && !(BorderMask & NO_DE)) {
        Freq_match_found = true;
        if (DE_start == HDE_On_Low_50) { DE_start = 0; DE_end = 0; BorderMask |= NO_DE; }
    }
    } else {
    // ----- STE (video.c 2444-2651) : le GST MCU teste les positions de PRELOAD
    // du MMU (le shifter commence à charger 16 cyc avant DE : 36/40 au lieu des
    // fenêtres 52/56 du STF) et offre le retrait gauche COURT (LEFT_OFF_2_STE :
    // hi→lo repassé PILE au cycle 4 → +20 octets, écran décalé de 8 px). -----
    if (freqHz == 71 && lineCycles <= HDE_On_Hi) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) {
            DE_start = HDE_On_Hi; DE_end = HDE_Off_Hi;
            BorderMask |= LEFT_OFF; GL.displayPixelShift = -4;
            BorderMask &= ~LEFT_PLUS_2;
        }
    } else if (freqHz == 71 && lineCycles <= HBlank_Off_Low_50) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Hi; BorderMask |= (BLANK | NO_DE); }
        BorderMask &= ~LEFT_PLUS_2;
    } else if (freqHz == 71 && lineCycles <= T.HDE_On_Low_50) {              // 56, comme le STF
        // Netlist du GST MCU (SYNC.PIN, 4081P26 SYM 18-31 et 4081P29 SYM 38-40) : la
        // longueur de ligne est fixée par MDE1 au rechargement du compteur HSC, dont le
        // bord tombe au cycle ~56 (= Line_Set_Pal), et HDE n'est jamais posé si MDE1
        // vaut 1 à ~44 puis MDE1B 0 à ~56 → « no DE » + ligne 224 jusqu'à 56 inclus.
        // Hatari (video.c:2503) arrête cette détection à Preload_Start_Low_50 = 40 et
        // rend STOP_MIDDLE + 512 entre 40 et 56 : rien dans la netlist ne le soutient.
        // Le cycle 56 lui-même reste à ±4 (phase du strobe d'écriture non mesurée).
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Hi; glueCyclesLine_ = CyclesLine_Hi;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Hi; BorderMask |= NO_DE; }
        BorderMask &= ~LEFT_PLUS_2;
    } else if (freqHz != 71) {
        if (lineCycles < HDE_On_Hi && (BorderMask & LEFT_OFF)) {             // STRICT < (≠ STF : ≤)
            if (freqHz == 50) DE_start = HDE_On_Low_50;
            else { DE_start = HDE_On_Low_60; BorderMask |= LEFT_PLUS_2; }
            BorderMask &= ~LEFT_OFF; GL.displayPixelShift = 0;
        } else if (lineCycles == HDE_On_Hi && (BorderMask & LEFT_OFF)) {     // PILE à 4 : variante courte
            DE_start = T.Preload_Start_Hi + 16;                              // 16
            BorderMask &= ~LEFT_OFF;
            BorderMask |= LEFT_OFF_2_STE;
            GL.displayPixelShift = -8;                   // écran décalé de 8 px à gauche
        }
        if (lineCycles <= HBlank_Off_Low_50 && (BorderMask & (BLANK | NO_DE)) && !(BorderMask & NO_COUNT)) {
            BorderMask &= ~(BLANK | NO_DE);
        } else if (lineCycles <= T.Preload_Start_Low_50 && (BorderMask & NO_DE) && !(BorderMask & BLANK) && !(BorderMask & NO_COUNT)) {
            BorderMask &= ~NO_DE;                        // « line no de » annulable, pas « blank no de »
        }
    }

    // Ligne 50 Hz qui continue en 60 Hz (et réciproque) — video.c 2547-2634
    if (freqHz == 60 && lineCycles < Line_Set_Pal) {                         // Line_Set_Pal STE = 56
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_60; glueCyclesLine_ = CyclesLine_60;
        if (!(BorderMask & NO_DE)) {
            if (DE_start > 0) { DE_end = HDE_Off_Low_60; BorderMask |= RIGHT_MINUS_2; }
            if (lineCycles > HBlank_Off_Low_60 && lineCycles <= HBlank_Off_Low_50) BorderMask |= BLANK;
            if (lineCycles <= T.Preload_Start_Low_60) {                      // fenêtre PRELOAD 36 (≠ STF)
                if (DE_start == HDE_On_Low_50) { DE_start = HDE_On_Low_60; BorderMask |= LEFT_PLUS_2; }
            }   // sinon : ligne normale démarrée à 56 qui continue en 60 Hz — rien de spécial
        }
    } else if (freqHz == 50 && lineCycles <= T.Preload_Start_Low_60) {       // 36 (≠ STF : 52)
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_50; glueCyclesLine_ = CyclesLine_50;
        if (!(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Low_50;
            BorderMask &= ~RIGHT_MINUS_2;
            if (DE_start == HDE_On_Low_60) { DE_start = HDE_On_Low_50; BorderMask &= ~LEFT_PLUS_2; }
        }
    } else if (freqHz == 50 && lineCycles <= Line_Set_Pal) {
        Freq_match_found = true;
        glueHblPos_ = Hbl_Pos_Low_50; glueCyclesLine_ = CyclesLine_50;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Low_50; BorderMask &= ~RIGHT_MINUS_2; }
    }

    if (freqHz == 60 && lineCycles > T.Preload_Start_Low_60 && lineCycles <= T.Preload_Start_Low_50
        && !(BorderMask & NO_DE)) {                                          // fenêtre preload (36,40]
        Freq_match_found = true;
        if (DE_start == HDE_On_Low_50) { DE_start = 0; DE_end = 0; BorderMask |= NO_DE; }
    }
    }

    // ===== Phase 2 : valeur de Freq ENTRE DE_start et DE_end (video.c 2667-2841) =====
    if (!Freq_match_found) {
        GlueLine& NX = glueLines_[line + 1];             // ligne suivante (right-off full, no-sync)
        if (freqHz == 71 && lineCycles <= DE_end && lineCycles <= HDE_Off_Hi && !(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Hi; BorderMask |= STOP_MIDDLE; BorderMask &= ~RIGHT_MINUS_2;
        } else if (freqHz == 71 && lineCycles <= DE_end && !(BorderMask & NO_DE)) {
            DE_end = LINE_END_FULL;
            BorderMask |= (RIGHT_OFF | RIGHT_OFF_FULL);
            NX.borderMask |= LEFT_OFF; NX.displayStartCycle = HDE_On_Hi;
        } else if (freqHz == 71 && lineCycles <= cpl + HSync_On_Off_Low) {
            BorderMask |= NO_SYNC;
            NX.borderMask |= (BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = 0; NX.displayEndCycle = 0;
            glueBlankLines_++;
        } else if (freqHz == 71 && lineCycles <= cpl + HSync_Off_Off_Low) {
            BorderMask |= SYNC_HIGH;
            NX.borderMask |= (BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = 0; NX.displayEndCycle = 0;
            glueBlankLines_++;
        } else if (freqHz == 71) {
            NX.borderMask |= LEFT_OFF;
            NX.displayStartCycle = HDE_On_Hi; NX.displayEndCycle = HDE_Off_Hi; NX.displayPixelShift = -4;
        }

        if (freqHz == 60 && lineCycles <= DE_end && lineCycles <= HDE_Off_Low_60 && !(BorderMask & NO_DE)) {
            if (DE_end == HDE_Off_Low_50) BorderMask |= RIGHT_MINUS_2;
            DE_end = HDE_Off_Low_60;
            if (BorderMask & STOP_MIDDLE) BorderMask &= ~STOP_MIDDLE;
            else if (BorderMask & (RIGHT_OFF | RIGHT_OFF_FULL)) {
                BorderMask &= ~(RIGHT_OFF | RIGHT_OFF_FULL);
                NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
            }
        } else if (freqHz == 50 && lineCycles <= DE_end && lineCycles <= HDE_Off_Low_50 && !(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Low_50;
            if (BorderMask & RIGHT_MINUS_2) BorderMask &= ~RIGHT_MINUS_2;
            else if (BorderMask & STOP_MIDDLE) BorderMask &= ~STOP_MIDDLE;
            else if (BorderMask & (RIGHT_OFF | RIGHT_OFF_FULL)) {
                BorderMask &= ~(RIGHT_OFF | RIGHT_OFF_FULL);
                NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
            }
        } else if (freqHz == 60 && lineCycles <= DE_end && lineCycles > HDE_Off_Low_60
                   && lineCycles <= HDE_Off_Low_50 && !(BorderMask & NO_DE)) {
            if (DE_end == HDE_Off_Low_50) {              // retrait bordure DROITE
                DE_end = cpl + HSync_On_Off_Low;        // 462 (50 Hz) — l'affichage va jusqu'au HSYNC
                BorderMask |= RIGHT_OFF;
                BorderMask &= ~RIGHT_MINUS_2;
            }
        } else if (freqHz != 71 && lineCycles <= cpl + HSync_On_Off_Low) {
            if (lineCycles <= DE_end) {
                DE_end = cpl + HSync_On_Off_Low;
                if (BorderMask & RIGHT_OFF_FULL) {
                    BorderMask &= ~RIGHT_OFF_FULL;
                    NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
                }
            } else if (BorderMask & NO_SYNC) {
                BorderMask &= ~NO_SYNC;
                NX.borderMask &= ~(BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = -1; glueBlankLines_--;
            }
        } else if (freqHz != 71 && lineCycles <= cpl + HSync_Off_Off_Low) {
            if (BorderMask & SYNC_HIGH) {
                BorderMask &= ~SYNC_HIGH;
                NX.borderMask &= ~(BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = -1; glueBlankLines_--;
            }
        }
    }

    // ===== Bordures HAUT/BAS (video.c 2896-2991) =====
    // Top : tant que la 1ʳᵉ ligne affichée n'est pas atteinte, on peut la déplacer.
    if (line < glueStartHBL_ - 1
        || (line == glueStartHBL_ - 1 && lineCycles <= RemoveTopBorder_Pos)) {
        int Top_Pos = (freqHz == 71) ? VDE_On_Hi : (freqHz == 60 ? VDE_On_60 : VDE_On_50);
        if (Top_Pos != glueStartHBL_
            && (line < Top_Pos - 1 || (line == Top_Pos - 1 && lineCycles <= RemoveTopBorder_Pos))) {
            glueStartHBL_ = Top_Pos;
            if (nScreenRefreshRate_ == 50 && glueStartHBL_ < VDE_On_50) glueVOverscan_ |= VO_NO_TOP;
            else glueVOverscan_ &= ~VO_NO_TOP;
            glueVOverscan_ &= ~VO_NO_DE;
        } else {
            if (nScreenRefreshRate_ == 50 && freqHz != 50) glueVOverscan_ |= VO_NO_DE;
            else glueVOverscan_ &= ~VO_NO_DE;
        }
    }
    // Bottom : tant que la dernière ligne affichée n'est pas atteinte.
    if (line < glueEndHBL_ - 1
        || (line == glueEndHBL_ - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
        int Bottom_Pos = (freqHz == 71) ? VDE_Off_Hi : (freqHz == 60 ? VDE_Off_60 : VDE_Off_50);
        if (line < VDE_Off_60 - 1
            || (line == VDE_Off_60 - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (nScreenRefreshRate_ == 60 && freqHz != 60) { glueEndHBL_ = VDE_Off_NoBottom_60; glueVOverscan_ |= VO_NO_BOTTOM_60; }
            else if (nScreenRefreshRate_ == 50 && freqHz == 60) { glueEndHBL_ = VDE_Off_60; glueVOverscan_ |= VO_BOTTOM_SHORT_50; }
            else { glueEndHBL_ = Bottom_Pos; glueVOverscan_ &= ~(VO_NO_BOTTOM_60 | VO_BOTTOM_SHORT_50); }
        } else if (line < VDE_Off_50 - 1
                   || (line == VDE_Off_50 - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (glueVOverscan_ & VO_NO_BOTTOM_60) { /* déjà retiré, inchangeable */ }
            else if (nScreenRefreshRate_ == 50 && freqHz != 50) { glueEndHBL_ = VDE_Off_NoBottom_50; glueVOverscan_ |= VO_NO_BOTTOM_50; }
            else { glueEndHBL_ = Bottom_Pos; glueVOverscan_ &= ~VO_NO_BOTTOM_50; }
        } else if (line < VDE_Off_Hi - 1
                   || (line == VDE_Off_Hi - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (glueVOverscan_ & VO_NO_BOTTOM_50) { /* déjà retiré */ }
            else { glueEndHBL_ = Bottom_Pos; }
        }
    }

    GL.displayStartCycle = static_cast<int16_t>(DE_start);
    GL.displayEndCycle   = static_cast<int16_t>(DE_end);
    GL.borderMask        = BorderMask;
}

// Rejoue la machine Glue sur les écritures freq/res datées de la trame, ligne par
// ligne (StartHBL defaults → écritures via updateGlueState). Remplit glueLines_,
// glueStartHBL_/glueEndHBL_ et arme bordersTrick_ si une bordure est retirée.
// V2 — port de Video_WriteToGlueRes (video.c:1637-1753), post-traitement d'une
// écriture $FF8260 APRÈS la machine d'état commune. Les fenêtres LINE_* sont des
// constantes FIXES (hors table wakestate). Non portés, documentés : les hacks
// « TEMP » Closure/DOLB (reniflage du PC/opcode chez Hatari, video.c:1791-1834)
// et le hardscroll 4 px plein écran de Paulo Simoes (med@84 → lo@92-104,
// video.c:1755-1789 — nécessite l'ajustement du pointeur vidéo par ligne ;
// à porter avec un étalon dédié).
void Shifter::updateGlueRes(int line, int lineCycles, int prevRes, int newRes) {
    using namespace glue;
    const Timing& T = timing(bus_.machine);
    GlueLine& GL = glueLines_[line];
    static const bool medDiag = std::getenv("NEOST_MED_DIAG") != nullptr;
    if (medDiag && newRes == 1) {
        static long n = 0;
        if (++n % 200 == 0)
            std::fprintf(stderr, "[MEDR] line=%d lc=%d prev=%d bm=%05x\n",
                         line, lineCycles, prevRes, GL.borderMask);
    }

    // hi → med tôt dans la ligne avec retrait gauche déjà posé : le retrait
    // devient LEFT_OFF_MED et la ligne passe en med res overscan, source +2 o
    // (un scroll hardware peut encore être détecté plus bas). (video.c:1637)
    if (prevRes == 2 && newRes == 1 && lineCycles <= 0 + 20        // LINE_START_CYCLE_71 + 20
        && (GL.borderMask & LEFT_OFF)) {
        GL.borderMask &= ~LEFT_OFF;
        GL.borderMask |= LEFT_OFF_MED;
        GL.borderMask = (GL.borderMask & ~MED_OFFSET_MASK) | OVERSCAN_MED_RES | (2u << 20);
        GL.displayStartCycle = static_cast<int16_t>(T.HDE_On_Hi);
    }

    // Retrait gauche hi/lo suivi d'une bascule MED aux cycles No Cooper / PYM :
    // la ligne overscan est en MOYENNE résolution (décalage source 0 ou 2 octets).
    // (video.c:1655 — « No Cooper greetings », « Best Part Of The Creation / PYM »)
    if ((GL.borderMask & LEFT_OFF) && newRes == 1) {
        if (lineCycles == LINE_LEFT_MED_CYCLE_1 || lineCycles == LINE_LEFT_MED_CYCLE_1 + 16)
            GL.borderMask = (GL.borderMask & ~MED_OFFSET_MASK) | OVERSCAN_MED_RES | (0u << 20);
        else if (lineCycles == LINE_LEFT_MED_CYCLE_2)
            GL.borderMask = (GL.borderMask & ~MED_OFFSET_MASK) | OVERSCAN_MED_RES | (2u << 20);
    }

    // Variante STE : retrait gauche COURT (LEFT_OFF_2_STE) re-basculé med pile au
    // cycle 4 → LEFT_OFF_2_STE_MED, écran décalé de 16 px. (video.c:1671)
    if ((GL.borderMask & LEFT_OFF_2_STE) && newRes == 1 && lineCycles == T.HDE_On_Hi) {
        GL.borderMask &= ~LEFT_OFF_2_STE;
        GL.borderMask |= LEFT_OFF_2_STE_MED;
        GL.displayPixelShift = -16;
    }

    // Hatari Video_WriteToGlueRes : le retrait court LOW se cumule avec le
    // latch 336 px (28 octets à gauche, DE au cycle 0). Le replay utilise le
    // latch capturé sur la ligne, le calcul live celui des registres courants.
    const bool snapped = line >= 0 && line < static_cast<int>(lineScrollSnap_.size())
                         && (lineScrollSnap_[line] & 0x40);
    const bool border336 = snapped ? (lineScrollSnap_[line] & 0x20) != 0 : steBorderFlag_;
    if ((GL.borderMask & LEFT_OFF_2_STE) && newRes == 0 && border336) {
        GL.displayStartCycle = static_cast<int16_t>(T.Preload_Start_Hi);
        GL.displayPixelShift = -8;
    }

    // Retrait gauche hi/MED puis retour LO tôt : c'était un stab med (retrait
    // gauche low « propre ») ou un scroll hardware droite 13/9/5/1 px — la ligne
    // n'est PAS en med res. (video.c:1687)
    if ((GL.borderMask & LEFT_OFF_MED) && newRes == 0 && lineCycles <= LINE_SCROLL_1_CYCLE) {
        GL.borderMask &= ~OVERSCAN_MED_RES;
        if      (lineCycles == LINE_LEFT_STAB_LOW)   GL.displayPixelShift = 0;
        else if (lineCycles == LINE_SCROLL_13_CYCLE) GL.displayPixelShift = 13;
        else if (lineCycles == LINE_SCROLL_9_CYCLE)  GL.displayPixelShift = 9;
        else if (lineCycles == LINE_SCROLL_5_CYCLE)  GL.displayPixelShift = 5;
        else if (lineCycles == LINE_SCROLL_1_CYCLE)  GL.displayPixelShift = 1;
    }

    // Retrait gauche hi/lo, puis med, puis retour LO (méthode « 3 bascules » de
    // ST Connexion) : scroll hardware low res, on annule le med. (video.c:1727)
    if ((GL.borderMask & OVERSCAN_MED_RES) && (GL.borderMask & MED_OFFSET_MASK) == 0
        && newRes == 0 && lineCycles <= 40) {
        GL.borderMask &= ~OVERSCAN_MED_RES;
        if      (lineCycles == 28) GL.displayPixelShift = 13;
        else if (lineCycles == 32) GL.displayPixelShift = 9;
        else if (lineCycles == 36) GL.displayPixelShift = 5;
        else if (lineCycles == 40) GL.displayPixelShift = 1;
    }
}

void Shifter::replayGlue() {
    bordersTrick_ = false;
    if (frameMode_ == Mode::High) return;                // mono : pas de bordures modélisées

    const Geometry g = geometry();
    const int lpf = g.linesPerFrame;
    const int cpl = g.cyclesPerLine;
    nScreenRefreshRate_ = (frameSync_ & 0x02) ? 50 : 60;
    const int baseStart = g.dispStartLine;               // VDE_On de l'écran (63/34)
    const int baseEnd   = baseStart + g.displayLines;    // VDE_Off (263/234)

    glueLines_.assign(static_cast<std::size_t>(lpf) + 2, GlueLine{ -1, 0, 0, 0 });
    // A16b — INVARIANT : glueLineStart_ (échelle des débuts de ligne réels, canal
    // NEOST_LINELEN_ATTR) a TOUJOURS la taille de glueLines_. C'est le même invariant
    // que celui revalidé au chargement d'un save-state (cf. Shifter::serialize) : les
    // sites d'attribution (liveGlueCatchUp) indexent glueLineStart_[wl] SANS garde,
    // en s'appuyant sur wl ≤ liveGlueLine_ < glueLines_.size().
    // Il était tenu par beginFrame — et par lui SEUL. replayGlue redimensionnait
    // glueLines_ tout court, ce qui le rompait dans deux cas :
    //   · glueSelfTest(), qui appelle replayGlue() SANS beginFrame() : glueLineStart_
    //     restait VIDE, et le premier liveLineDisplayed() du test déréférençait
    //     data() == nullptr → SIGSEGV muet (aucune sortie, code 139) ;
    //   · en production, une trame dont lpf change en cours de route (le commentaire
    //     de serialize l'admet explicitement) : glueLineStart_ restait plus COURT et
    //     les lectures live sortaient du tas — silencieusement.
    // `resize` et non `assign` : les débuts de ligne déjà calculés par le passage LIVE
    // de la trame en cours sont CONSERVÉS (replayGlue rejoue l'état d'affichage, il
    // n'invalide pas l'échelle) ; seules les lignes nouvelles sont mises à zéro.
    if (glueLineStart_.size() != glueLines_.size())
        glueLineStart_.resize(glueLines_.size(), 0);
    glueStartHBL_   = baseStart;
    glueEndHBL_     = baseEnd;
    glueVOverscan_  = 0;
    glueBlankLines_ = 0;

    std::stable_sort(syncWrites_.begin(), syncWrites_.end(),
                     [](const SyncWrite& a, const SyncWrite& b){ return a.frameCycle < b.frameCycle; });

    // État res/freq COURANT (début de trame = valeurs verrouillées). res : 0=low,
    // 1=med, 2=hi ; freq : bit1 de $FF820A (1=50 Hz).
    int curRes  = (frameMode_ == Mode::Medium) ? 1 : (frameMode_ == Mode::High ? 2 : 0);
    int curFreq50 = (frameSync_ & 0x02) ? 1 : 0;

    // V2 res-switch (opt-in NEOST_V2) : attribution à LONGUEUR DE LIGNE VARIABLE —
    // une ligne où tombe une impulsion hi-res PRÉCOCE (≤56) ne fait que 224 cyc
    // (port HBL_Pos/nCyclesPerLine), COHÉRENT avec le raccourcissement live
    // (Machine setHblShorten) : sans ça l'état glue ne refléterait pas les lignes
    // raccourcies. Hors V2 : attribution fixe `frameCycle/cpl` historique (inchangée).
    static const bool v2 = std::getenv("NEOST_V2") != nullptr;
    // Longueurs de ligne PAR-LIGNE (NEOST_LINELEN_ATTR) : l'attribution suit la grille
    // RÉELLE — la longueur d'une ligne évolue au fil de ses PROPRES écritures
    // (dernier nCyclesPerLine posé par un « Freq_match », défaut cpl), comme la
    // chaîne StartCycle/nCyclesPerLine de Hatari. Remplace l'heuristique V2
    // « impulsion hi ≤57 → 224 ».
    const bool lineLen = lineLenAttrEnv();
    std::size_t wi = 0;
    const std::size_t nw = syncWrites_.size();
    int64_t lineCyc = 0;                                  // cycle-trame du début de la ligne (V2/LINELEN)
    for (int line = 0; line < lpf; ++line) {
        // cf. le feeder live : décode Video_StartHBL, hi si (res & 3) == 2 seulement.
        int freqHz = ((curRes & 0x03) == 0x02) ? 71 : (curFreq50 ? 50 : 60);
        startHBL(line, curRes, freqHz);
        int len = lineLen ? glueLineLenFor(freqHz) : cpl;
        if (v2 && !lineLen) {                            // ligne raccourcie ? (heuristique V2)
            for (std::size_t k = wi; k < nw && syncWrites_[k].frameCycle < lineCyc + 57; ++k)
                if (syncWrites_[k].isRes && (syncWrites_[k].val & 3) == 2) { len = 224; break; }
        }
        // Applique les écritures de CETTE ligne (cycle croissant). En mode LINELEN
        // la borne de fin de ligne (lineCyc+len) est DYNAMIQUE : chaque écriture
        // peut la déplacer via glueCyclesLine_.
        while (wi < nw) {
            const SyncWrite& w = syncWrites_[wi];
            if (lineLen || v2) { if (w.frameCycle >= lineCyc + len) break; }
            else               { if (w.frameCycle / cpl != line) break; }
            ++wi;
            const int lc = (lineLen || v2) ? static_cast<int>(w.frameCycle - lineCyc)
                                           : static_cast<int>(w.frameCycle % cpl);
            const int prevRes = curRes;
            if (w.isRes) curRes    = w.val & 0x03;
            else         curFreq50 = (w.val & 0x02) ? 1 : 0;
            freqHz = (curRes & 0x02) ? 71 : (curFreq50 ? 50 : 60);
            updateGlueState(line, lc, w.isRes, freqHz);
            // V2 : détections spécifiques aux bascules de résolution (med res
            // overscan, stab/scrolls hardware) — APRÈS la machine commune, comme
            // Video_WriteToGlueRes. Le cycle passé est BRUT (les fenêtres LINE_*
            // d'Hatari se comparent avant le latch res −1, video.c:1622-1634).
            if (w.isRes) updateGlueRes(line, lc, prevRes, curRes);
            if (lineLen && glueCyclesLine_ > 0) len = glueCyclesLine_;
            // Chantier V3 : dire à quelle ligne l'écriture a RÉELLEMENT été
            // attribuée dans CE replay, et avec quelle longueur de ligne
            // courante. Le trace `[varline]` voisin compare deux modèles en
            // théorie ; celui-ci dit lequel a tourné — sans quoi on lit un
            // écart annoncé sans savoir s'il a le moindre effet.
            static const bool attrTrace = std::getenv("NEOST_VARLINE_TRACE") != nullptr;
            if (attrTrace)
                std::fprintf(stderr, "[attr] lineLen=%d %s=%02x fc=%d -> L%d/c%d len=%d\n",
                             lineLen ? 1 : 0, w.isRes ? "res" : "frq", w.val,
                             static_cast<int>(w.frameCycle), line, lc, len);
        }
        lineCyc += len;
    }

    // Détection : une bordure est-elle retirée ? (haut/bas déplacés, ou une ligne
    // affichée a un DE élargi gauche/droite).
    const bool vertMoved = (glueStartHBL_ != baseStart || glueEndHBL_ != baseEnd);
    // Les DE stockés par la Glue sont sur la table WS-décalée → les nominaux de
    // comparaison aussi (g.lineStart/EndCycle sont les ancres FIXES du rendu).
    const int nomStart = g.lineStartCycle + glue::timing(bus_.machine).inc;
    const int nomEnd   = g.lineEndCycle   + glue::timing(bus_.machine).inc;
    // ⚠ Ce balayage n'est plus conditionné à `!bordersTrick_`. Il l'était, et c'est
    // ce qui rendait le trick LATÉRAL indétectable dès qu'un trick VERTICAL avait
    // mordu : un titre qui ouvre seulement la bordure HAUTE (Enchanted Land) sortait
    // avec le même signal qu'un titre qui déborde vraiment sur les côtés (Closure),
    // et le cadrage adaptatif élargissait à 416 px une image de 320 (cf.
    // core/Framing.cpp). Le coût est un balayage des lignes affichées par trame.
    // On COMPTE les lignes élargies au lieu de poser un simple booléen : c'est ce qui
    // sépare un vrai débordement latéral d'un artefact de synchro. Mesuré le 2026-09-02 :
    // Closure élargit 272-275 lignes sur 276 (99 %), là où Enchanted Land en élargit 4
    // sur 229 et l'étalon overscan_top 5 sur 229 (~2 %) — chez ces deux-là les lignes
    // élargies rendent du noir, l'image n'occupe que la zone active. Le cadrage adaptatif
    // tranche sur cette proportion (cf. core/Framing.cpp).
    sideTrickLines_ = 0;
    for (int sl = glueStartHBL_; sl < glueEndHBL_; ++sl) {
        const GlueLine& L = glueLines_[sl];
        if (L.displayStartCycle >= 0
            && (L.displayStartCycle < nomStart || L.displayEndCycle > nomEnd))
            ++sideTrickLines_;
    }
    sideTrick_ = (sideTrickLines_ > 0);
    if (vertMoved || sideTrick_) bordersTrick_ = true;

    // Stat Glue (gated NEOST_GLUE_STAT) : liste les écritures freq/res datées de la
    // trame (ligne, cycle, registre, valeur) — diagnostic « pourquoi pas de trick ».
    if (!syncWrites_.empty() && std::getenv("NEOST_GLUE_STAT")) {
        std::fprintf(stderr, "[gluestat] %zu writes:", syncWrites_.size());
        for (std::size_t i = 0; i < syncWrites_.size() && i < 24; ++i) {
            const SyncWrite& w = syncWrites_[i];
            std::fprintf(stderr, " %s=%02X@%lld+%d", w.isRes ? "res" : "frq", w.val,
                         (long long)(w.frameCycle / cpl), (int)(w.frameCycle % cpl));
        }
        std::fprintf(stderr, " | trick=%d start=%d end=%d\n", bordersTrick_ ? 1 : 0,
                     glueStartHBL_, glueEndHBL_);
    }

    // Trace bordure (gated NEOST_BORDER_TRACE) : pour le diff oracle Hatari
    // (video_border_h/v). Émet les retraits détectés cette trame, format comparable.
    if (bordersTrick_ && std::getenv("NEOST_BORDER_TRACE")) {
        if (glueStartHBL_ < baseStart)
            std::fprintf(stderr, "detect remove top (nStartHBL=%d)\n", glueStartHBL_);
        if (glueEndHBL_ > baseEnd)
            std::fprintf(stderr, "detect remove bottom (nEndHBL=%d)\n", glueEndHBL_);
        int nLeft = 0, nRight = 0;
        for (int sl = glueStartHBL_; sl < glueEndHBL_ && sl < (int)glueLines_.size(); ++sl) {
            const GlueLine& L = glueLines_[sl];
            if (L.displayStartCycle >= 0 && L.displayStartCycle < nomStart) ++nLeft;
            if (L.displayEndCycle > nomEnd) ++nRight;
        }
        if (nLeft)  std::fprintf(stderr, "detect remove left x%d\n", nLeft);
        if (nRight) std::fprintf(stderr, "detect remove right x%d\n", nRight);
    }

    // INSTRUMENTATION (gated NEOST_VARLINE_TRACE) — chantier « longueur de ligne variable » :
    // calcule l'attribution ligne/lineCyc à LONGUEUR VARIABLE (508 pour une ligne 60 Hz,
    // 512 pour 50 Hz, 224 hi — façon Hatari ShifterLines[].StartCycle / Video_ConvertPosition)
    // et la compare à l'attribution FIXE (fc/cpl) utilisée actuellement. Ne change RIEN au
    // comportement ; sert à mesurer la divergence avant de porter le modèle. La longueur d'une
    // ligne est fixée par la freq au comparateur HBL (~cyc 502).
    if (!syncWrites_.empty() && std::getenv("NEOST_VARLINE_TRACE")) {
        int res = (frameMode_ == Mode::Medium) ? 1 : (frameMode_ == Mode::High ? 2 : 0);
        int f50 = (frameSync_ & 0x02) ? 1 : 0;
        int64_t cyc = 0; int vline = 0; std::size_t i = 0; int ndiff = 0, nshown = 0;
        while (vline < lpf + 2 && i < nw) {
            int r2 = res, f2 = f50; std::size_t j = i;        // freq au comparateur (~502)
            while (j < nw && syncWrites_[j].frameCycle < cyc + 502) {
                if (syncWrites_[j].isRes) r2 = syncWrites_[j].val & 3;
                else                      f2 = (syncWrites_[j].val & 2) ? 1 : 0;
                ++j;
            }
            const int freqHz = (r2 & 0x02) ? 71 : (f2 ? 50 : 60);
            const int len = (freqHz == 71) ? 224 : (freqHz == 60 ? 508 : 512);
            while (i < nw && syncWrites_[i].frameCycle < cyc + len) {
                const SyncWrite& w = syncWrites_[i];
                const int fixedLine = static_cast<int>(w.frameCycle / cpl);
                if (fixedLine != vline) {
                    ++ndiff;
                    if (++nshown <= 24)
                        std::fprintf(stderr, "[varline] %s=%02x fc=%d : fixed=L%d/c%d  var=L%d/c%d\n",
                            w.isRes ? "res" : "frq", w.val, w.frameCycle, fixedLine,
                            static_cast<int>(w.frameCycle % cpl), vline,
                            static_cast<int>(w.frameCycle - cyc));
                }
                if (w.isRes) res = w.val & 3; else f50 = (w.val & 2) ? 1 : 0;
                ++i;
            }
            cyc += len; ++vline;
        }
        std::fprintf(stderr, "[varline] %d/%zu writes misattributed (fixed!=variable) | final drift=%lld cyc\n",
                     ndiff, nw, static_cast<long long>(cyc - static_cast<int64_t>(vline) * cpl));
    }
}

// Auto-test déterministe de la machine Glue (cf. déclaration). Chaque scénario
// injecte des écritures freq/res à des cycles exacts puis vérifie l'état calculé
// contre les valeurs Hatari. Affiche un récap sur stderr ; renvoie true si tout OK.
bool Shifter::glueSelfTest() {
    frameMode_ = Mode::Low;                       // STF 50 Hz basse rés
    frameSync_ = 0x02;
    resizeFor(frameMode_);
    const int cpl = geometry().cyclesPerLine;     // 512
    int pass = 0, fail = 0;

    // Rejoue une liste d'écritures {line, cycle, isRes(0=freq/1=res), val}.
    auto run = [&](std::vector<std::array<int,4>> writes) {
        syncWrites_.clear();
        for (const auto& v : writes)
            syncWrites_.push_back({ v[0] * cpl + v[1],
                                    static_cast<uint8_t>(v[3]), v[2] != 0 });
        replayGlue();
    };
    auto chk = [&](const char* name, long got, long want) {
        if (got == want) { ++pass; }
        else { ++fail; std::fprintf(stderr, "  FAIL %-26s got=%ld want=%ld\n", name, got, want); }
    };
    // Attentes sur la table de la MACHINE COURANTE (STF WS3 ou STE) : les stimuli
    // ci-dessous tombent dans les fenêtres des deux machines, les positions
    // attendues suivent la table.
    const glue::Timing& T = glue::timing(bus_.machine);

    // 1. Bordure DROITE : 60 Hz @ cyc 374 puis 50 Hz @ 380, ligne 100.
    run({ {100,374,0,0x00}, {100,380,0,0x02} });
    chk("right DE_start", glueLines_[100].displayStartCycle, T.HDE_On_Low_50);
    chk("right DE_end",   glueLines_[100].displayEndCycle,   512 + T.HSync_On_Off_Low);   // cpl-50+inc / cpl-52 STE (RIGHT_OFF)
    chk("right mask",     (glueLines_[100].borderMask & glue::RIGHT_OFF) ? 1 : 0, 1);

    // 2. Bordure GAUCHE : hi-rés @ 2 puis lo-rés @ 8, ligne 100 (le retour lo doit
    // tomber APRÈS la fenêtre de restauration ≤ HDE_On_Hi — 5 en WS3, 4 en WS1 ;
    // l'ancien stimulus @6 (4 cyc d'écart, impossible sur vrai HW) retombait dans
    // la fenêtre WS3 et annulait le trick, fidèlement).
    run({ {100,2,1,0x02}, {100,8,1,0x00} });
    chk("left DE_start",  glueLines_[100].displayStartCycle, T.HDE_On_Hi);
    chk("left DE_end",    glueLines_[100].displayEndCycle,   T.HDE_Off_Low_50);
    chk("left mask",      (glueLines_[100].borderMask & glue::LEFT_OFF) ? 1 : 0, 1);

    // 3. RIGHT-2 (ligne 60 Hz) : 60 Hz @ 100 puis 50 Hz @ 400, ligne 100.
    run({ {100,100,0,0x00}, {100,400,0,0x02} });
    chk("right-2 DE_end", glueLines_[100].displayEndCycle,   T.HDE_Off_Low_60);
    chk("right-2 mask",   (glueLines_[100].borderMask & glue::RIGHT_MINUS_2) ? 1 : 0, 1);

    // 4. Retrait HAUT : 60 Hz @ ligne 10, 50 Hz @ ligne 40.
    run({ {10,100,0,0x00}, {40,100,0,0x02} });
    chk("top nStartHBL",  glueStartHBL_, 34);                        // VDE_On_60
    chk("top NO_TOP",     (glueVOverscan_ & glue::VO_NO_TOP) ? 1 : 0, 1);
    chk("top nEndHBL",    glueEndHBL_, 263);                         // bas inchangé

    // 5. Retrait BAS : 60 Hz @ ligne 261.
    run({ {261,100,0,0x00} });
    chk("bottom nEndHBL", glueEndHBL_, 310);                         // VDE_Off_NoBottom_50

    // 4bis. DE vertical JAMAIS activé (video.c:2920-2923) : écran 50 Hz dont la
    // fréquence passe à 60 Hz APRÈS Top_Pos (34) et avant nStartHBL (63) sans
    // revenir à 50 Hz à temps. Le drapeau étant STICKY et noircissant toute la
    // trame, la détection est verrouillée ici AVANT ses trois consommateurs
    // (rendu, compteur vidéo, Timer B event-count).
    run({ {45,100,0,0x00} });
    chk("node NO_DE",     (glueVOverscan_ & glue::VO_NO_DE) ? 1 : 0, 1);
    chk("node bytes",     glueLineBytes(100), 0);                    // raster non avancé
    chk("node timerB",    liveLineDisplayed(100) ? 1 : 0, 0);        // event-count muet
    // Contre-épreuve : la MÊME bascule AVANT Top_Pos est un retrait de bordure
    // haute ordinaire — surtout pas un écran noir (cas 4 ci-dessus).
    run({ {10,100,0,0x00}, {40,100,0,0x02} });
    chk("node !top",      (glueVOverscan_ & glue::VO_NO_DE) ? 1 : 0, 0);
    chk("node !top bytes", glueLineBytes(100), 160);

    // 6. Écran NORMAL (aucune écriture) : aucune bordure retirée.
    run({});
    chk("normal nStartHBL", glueStartHBL_, 63);
    chk("normal nEndHBL",   glueEndHBL_, 263);
    chk("normal DE_start",  glueLines_[100].displayStartCycle, T.HDE_On_Low_50);
    chk("normal DE_end",    glueLines_[100].displayEndCycle, T.HDE_Off_Low_50);
    chk("normal trick",     bordersTrick_ ? 1 : 0, 0);

    // 7. STOP_MIDDLE : hi-rés @ cyc 100 (entre DE, ≤160), ligne 100.
    run({ {100,100,1,0x02}, {100,500,1,0x00} });
    chk("stopmid DE_end", glueLines_[100].displayEndCycle, T.HDE_Off_Hi);
    chk("stopmid mask",   (glueLines_[100].borderMask & glue::STOP_MIDDLE) ? 1 : 0, 1);

    // 8. V2 — med res overscan « No Cooper » : hi @0, lo @12 (retrait gauche
    // conservé), MED pile @20 (LINE_LEFT_MED_CYCLE_1) → la ligne overscan est en
    // MOYENNE résolution, décalage source 0 octet.
    run({ {100,0,1,0x02}, {100,12,1,0x00}, {100,20,1,0x01} });
    chk("medov mask",   (glueLines_[100].borderMask & glue::OVERSCAN_MED_RES) ? 1 : 0, 1);
    chk("medov left",   (glueLines_[100].borderMask & glue::LEFT_OFF) ? 1 : 0, 1);
    chk("medov field",  (glueLines_[100].borderMask & glue::MED_OFFSET_MASK) >> 20, 0);
    chk("medov DE_end", glueLines_[100].displayEndCycle, T.HDE_Off_Low_50);

    // 9. V2 — hi→MED tôt (≤20) : le retrait gauche devient LEFT_OFF_MED, ligne
    // med overscan avec décalage source 2 octets (video.c:1637).
    run({ {100,2,1,0x02}, {100,8,1,0x01} });
    chk("himed mask",  (glueLines_[100].borderMask & glue::LEFT_OFF_MED) ? 1 : 0, 1);
    chk("himed med",   (glueLines_[100].borderMask & glue::OVERSCAN_MED_RES) ? 1 : 0, 1);
    chk("himed field", (glueLines_[100].borderMask & glue::MED_OFFSET_MASK) >> 20, 2);
    chk("himed DE_start", glueLines_[100].displayStartCycle, T.HDE_On_Hi);

    // 10. V2 — stab med (hi/med/lo, retour lo @16) : retrait gauche low propre,
    // pas de med res, shift 0 (video.c:1687/1695).
    run({ {100,2,1,0x02}, {100,8,1,0x01}, {100,16,1,0x00} });
    chk("stabmed med",   (glueLines_[100].borderMask & glue::OVERSCAN_MED_RES) ? 1 : 0, 0);
    chk("stabmed shift", glueLines_[100].displayPixelShift, 0);

    // 11. V2 — scroll hardware droite 13 px (hi/med/lo, retour lo @20).
    run({ {100,2,1,0x02}, {100,8,1,0x01}, {100,20,1,0x00} });
    chk("scroll13 shift", glueLines_[100].displayPixelShift, 13);
    chk("scroll13 med",   (glueLines_[100].borderMask & glue::OVERSCAN_MED_RES) ? 1 : 0, 0);

    // 12. STE seulement — retrait gauche COURT (LEFT_OFF_2_STE) : hi-rés @ 2 puis
    // lo-rés PILE au cycle 4 (HDE_On_Hi) → +20 octets, DE_start 16, écran −8 px.
    if (machineIsSte(bus_.machine)) {
        run({ {100,2,1,0x02}, {100,4,1,0x00} });
        chk("left2ste DE_start", glueLines_[100].displayStartCycle, T.Preload_Start_Hi + 16);
        chk("left2ste mask",  (glueLines_[100].borderMask & glue::LEFT_OFF_2_STE) ? 1 : 0, 1);
        chk("left2ste shift", glueLines_[100].displayPixelShift, -8);
        steBorderFlag_ = true;
        run({ {100,2,1,0x02}, {100,4,1,0x00} });
        chk("left2ste 336 DE_start", glueLines_[100].displayStartCycle, T.Preload_Start_Hi);
        chk("left2ste 336 extra", scrollCounterAdvance(100), 8);
        run({ {100,2,1,0x02}, {100,8,1,0x00} });
        chk("leftoff 336 no extra", scrollCounterAdvance(100), 0);
        steBorderFlag_ = false;

        // Latch 336 px et save-state : mêmes MMIO que le programme oracle.
        // Les horloges synthétiques permettent aussi la coupure ENTRE les deux
        // écritures et pendant le changement différé de fin de ligne.
        const auto oldBeam = beamClock_;
        const auto oldLive = liveFrameClock_;
        int64_t fc = 100;
        beamClock_ = [&] { return fc; };
        liveFrameClock_ = {};
        reset(); sync = 2; beginFrame();
        auto save = [&] {
            std::vector<uint8_t> bytes;
            auto ar = StateArchive::saver(bytes); serialize(ar); return bytes;
        };
        auto load = [&](const std::vector<uint8_t>& bytes) {
            auto ar = StateArchive::loader(bytes.data(), bytes.size()); serialize(ar);
            chk("336 state valid", ar.ok(), true);
        };
        write8(0xFF8265, 4);
        const auto armed = save();
        reset(); load(armed);
        fc = 140; write8(0xFF8264, 0);
        chk("336 restore armed <=40", steBorderFlag_, true);
        chk("336 stride", scrollCounterAdvance(), 8);
        const auto active = save();
        reset(); load(active);
        chk("336 restore active", steBorderFlag_, true);
        beginFrame();
        chk("336 persists VBL", steBorderFlag_, true);
        write8(0xFF8265, 0);
        chk("336 clear 8265", steBorderFlag_, false);
        load(armed); fc = 144; write8(0xFF8264, 0);
        chk("336 rejects >40", steBorderFlag_, false);
        load(armed); beginFrame(); fc = 104; write8(0xFF8264, 0);
        chk("336 rejects next VBL", steBorderFlag_, false);

        fc = 100 * cpl + 100;
        write8(0xFF8265, 4); fc += 16; write8(0xFF8264, 0);
        chk("336 change delayed", steBorderFlag_, false);
        chk("336 pending", newSteBorderFlag_, true);
        const auto pending = save();
        reset(); load(pending);
        vcLineY_ = 37; commitAnchor_ = 63; endVideoLine();
        chk("336 restore pending", steBorderFlag_, true);
        chk("336 metadata without RAM", lineScrollSnap_[100], 0x40);
        chk("336 no RAM snapshot", lineSnapLen_[100], 0);
        const uint32_t base = vcLineBase_;
        endVideoLine();
        chk("336 committed stride", vcLineBase_ - base, 168);
        chk("336 metadata next line", lineScrollSnap_[101], 0x60);
        reset();
        chk("336 reset", steBorderFlag_, false);
        beamClock_ = oldBeam; liveFrameClock_ = oldLive;
    }

    // B15 — compteur et fetch vidéo repliés sur 22 bits ; $FF8261 (Shifter seul) et
    // res 3 (Shifter arrêté, scroll hard Troed/Sync). Horloges synthétiques.
    {
        const auto oldBeam = beamClock_;
        const auto oldLive = liveFrameClock_;
        int64_t fc = 0;
        beamClock_      = [&] { return fc; };
        liveFrameClock_ = [&] { return fc; };
        // 1. Le compteur avance au-delà de $3FFFFF → reprend en $000000.
        reset(); mode = Mode::Low; sync = 2; videoBase = 0x3FFF00; beginFrame();
        chk("b15 frame base", vcFrameBase_, 0x3FFF00);
        vcLineY_ = 0; commitAnchor_ = 63;
        endVideoLine(); endVideoLine();                  // +160 +160 = $400040
        chk("b15 counter wraps", vcLineBase_, 0x40);
        // 2. Le fetch d'une ligne qui chevauche $3FFFFF relit le BAS de la RAM.
        {
            const uint32_t probe = 0x18;                 // groupe 19 de la ligne à $3FFF80
            uint16_t saved[4];
            for (int i = 0; i < 4; ++i) saved[i] = bus_.read16(probe + 2u * i);
            bus_.write16(probe, 0x8000); bus_.write16(probe + 2, 0);
            bus_.write16(probe + 4, 0);  bus_.write16(probe + 6, 0);
            vcLineBase_ = 0x3FFF80; vcLineY_ = 0;
            uint8_t ix[660];
            decodeLineIndices(0, ix);
            chk("b15 fetch wraps px", ix[19 * 16], 1);
            chk("b15 fetch wraps px+1", ix[19 * 16 + 1], 0);
            for (int i = 0; i < 4; ++i) bus_.write16(probe + 2u * i, saved[i]);
        }
        // 3. Écriture du compteur (STE) : octet haut limité à 6 bits.
        if (machineIsSte(bus_.machine)) {
            fc = 10;                                     // bordure haute → immédiat
            write8(0xFF8205, 0xFF);
            chk("b15 8205 masked", (vcLineBase_ >> 16) & 0xFF, 0x3F);
        }

        // 4. $FF8261 : Shifter seul — ni écriture Glue datée, ni mode Glue changé.
        reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000; beginFrame();
        fc = 100 * cpl + 200;
        write8(0xFF8261, 1);
        chk("8261 shifter res", shifterRes_, 1);
        chk("8261 glue untouched", static_cast<int>(mode), 0);
        chk("8261 no glue write", static_cast<long>(syncWrites_.size()), 0);
        chk("8261 read back", read8(0xFF8261) & 3, 1);
        // Mot vers $FF8260 : l'octet bas ($8261) est ignoré par le Shifter.
        bus_.write16(0xFF8260, 0x0003);
        chk("8260 word: glue gets high byte", static_cast<int>(mode), 0);
        chk("8260 word: shifter ignores low", shifterRes_, 0);
        write8(0xFF8260, 0);

        // 5. Scroll hard « Shifter arrêté » : durée d'arrêt → décalage / AddressInc.
        auto stop = [&](int from, int to) {
            reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000; beginFrame();
            fc = 100 * cpl + from; write8(0xFF8261, 3);
            fc = 100 * cpl + to;   write8(0xFF8261, 0);
        };
        stop(68, 80);
        chk("stop 12cyc line",  shifterStopLine_, 100);
        chk("stop 12cyc shift", shifterStopShift_, -4);
        chk("stop 12cyc inc",   vcStopInc_, -2);
        stop(68, 84);  chk("stop 16cyc shift", shifterStopShift_, 0);
                       chk("stop 16cyc inc",   vcStopInc_, 0);
        stop(68, 88);  chk("stop 20cyc shift", shifterStopShift_, -12);
                       chk("stop 20cyc inc",   vcStopInc_, -6);
        stop(68, 92);  chk("stop 24cyc shift", shifterStopShift_, -8);
        stop(68, 108); chk("stop too long",    shifterStopLine_, -1);
        stop(40, 56);  chk("stop before DE",   shifterStopLine_, -1);
        stop(64, 66);  chk("stop return < 72", shifterStopLine_, -1);   // retour avant 64+8
        // Arrêt qui chevauche le HBL : durée réelle de centaines de cycles, pas le
        // trick. Hatari passerait le « ≤ 32 » (cycles-ligne 84 − 400 < 0) et la
        // normalisation modulo 16 en faisait −12 px / −6 octets à chaque ligne.
        auto stopX = [&](int from, int to) {
            reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000; beginFrame();
            fc = 100 * cpl + from; write8(0xFF8261, 3);
            fc = 101 * cpl + to;   write8(0xFF8261, 0);
        };
        stopX(400, 84);  chk("stop across HBL line",  shifterStopLine_, -1);
                         chk("stop across HBL inc",   vcStopInc_, 0);
                         chk("stop across HBL shift", shifterStopShift_, 0);
        stopX(100, 110); chk("stop next line +10",    shifterStopLine_, -1);   // ≤ 32 en cycles-ligne seulement
        // Datation sur la grille RÉELLE : une bascule 60 Hz au début de la ligne 99
        // (retour 50 Hz en cours de ligne) la raccourcit à 508 cycles → la ligne 100
        // commence 4 cycles plus tôt que la grille nominale. Arrêt daté 60/72 en
        // nominal (refusé : < 64), 64/76 en réel (trick 12 cycles → −4 px).
        reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000; beginFrame();
        fc = 98 * cpl + 500; write8(0xFF820A, 0);
        fc = 99 * cpl + 200; write8(0xFF820A, 2);
        fc = 100 * cpl + 60; write8(0xFF8261, 3);
        fc = 100 * cpl + 72; write8(0xFF8261, 0);
        if (lineLenAttrEnv()) {
            chk("real grid line 100 start", static_cast<long>(glueLineStart_[100]), static_cast<long>(100 * cpl - 4));
            chk("real grid stop line",  shifterStopLine_, 100);
            chk("real grid stop shift", shifterStopShift_, -4);
        } else {                                         // A/B NEOST_LINELEN_ATTR=0 : grille nominale
            chk("nominal grid no stop", shifterStopLine_, -1);
        }
        // AddressInc appliqué à la fin de la ligne du stop.
        stop(68, 80);
        vcLineY_ = 37; commitAnchor_ = 63;
        {
            const uint32_t b0 = vcLineBase_;
            endVideoLine();
            chk("stop line stride", vcLineBase_ - b0, 158);
            const uint32_t b1 = vcLineBase_;
            endVideoLine();
            chk("stop next stride", vcLineBase_ - b1, 160);
        }

        // 6. Rendu : lignes après le stop décalées de 4 px à gauche (bordure gauche
        // comprise), la ligne du stop intacte. RAM : p0=$0F00 (px 4-7), p1=$FFFF.
        reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000;
        palette.fill(0x0000); palette[0] = 0x0070;
        palette[2] = palette[4] = 0x0007;                // fond (plans normaux / tournés)
        palette[3] = palette[6] = 0x0700;                // px 4-7
        beginFrame();
        for (uint32_t a = 0x20000; a < 0x30000; a += 8) {
            bus_.write16(a, 0x0F00); bus_.write16(a + 2, 0xFFFF);
            bus_.write16(a + 4, 0);  bus_.write16(a + 6, 0);
        }
        commitScanline(99);
        fc = 100 * cpl + 68; write8(0xFF8261, 3);
        fc = 100 * cpl + 80; write8(0xFF8261, 0);
        commitScanline(262);
        for (int y = 0; y < curAH_; ++y) renderLine(y);
        finishFrame();
        {
            const int ds0 = geometry().dispStartLine;
            const uint32_t* r100 = frame_.data() + static_cast<std::size_t>(activeY_ + 100 - ds0) * curW_;
            const uint32_t* r150 = frame_.data() + static_cast<std::size_t>(activeY_ + 150 - ds0) * curW_;
            const uint32_t red = stColorToArgb(0x0700), blue = stColorToArgb(0x0007),
                           green = stColorToArgb(0x0070);
            chk("stop render line px4",   r100[activeX_ + 4], red);
            chk("stop render line px0",   r100[activeX_ + 0], blue);
            chk("stop render next px0",   r150[activeX_ + 0], red);
            chk("stop render next px4",   r150[activeX_ + 4], blue);
            chk("stop render next border", r150[activeX_ - 4], blue);
            chk("stop render next tail",  r150[activeX_ + 318], green);
        }

        // 7. Shifter laissé arrêté pendant une ligne : aire active noire, bordures intactes.
        // Ordre RÉEL des événements : écriture puis commit de la ligne au HBL (fin
        // de ligne). Arrêt posé après le DE de 99, levé après le DE de 100 : c'est
        // la ligne 100 qui n'a sorti aucun pixel, la 99 est intacte.
        reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000;
        palette.fill(0x0777); palette[0] = 0x0070;
        beginFrame();
        for (uint32_t a = 0x20000; a < 0x30000; a += 2) bus_.write16(a, 0xFFFF);
        commitScanline(98);
        fc = 99 * cpl + 400; write8(0xFF8261, 3);
        commitScanline(99);
        fc = 100 * cpl + 400; write8(0xFF8261, 0);
        commitScanline(100);
        commitScanline(262);
        for (int y = 0; y < curAH_; ++y) renderLine(y);
        finishFrame();
        {
            const int ds0 = geometry().dispStartLine;
            const uint32_t* r100 = frame_.data() + static_cast<std::size_t>(activeY_ + 100 - ds0) * curW_;
            const uint32_t* r101 = frame_.data() + static_cast<std::size_t>(activeY_ + 101 - ds0) * curW_;
            const uint32_t* r99  = frame_.data() + static_cast<std::size_t>(activeY_ + 99 - ds0) * curW_;
            chk("line before stop pixels", r99[activeX_ + 100], stColorToArgb(0x0777));
            chk("stopped line black",  r100[activeX_ + 100], 0xFF000000u);
            chk("stopped line border", r100[activeX_ - 10], stColorToArgb(0x0070));
            chk("running line pixels", r101[activeX_ + 100], stColorToArgb(0x0777));
        }
        // 8. Save-state : la résolution Shifter et le stop en attente survivent.
        stop(68, 88);
        {
            std::vector<uint8_t> st;
            auto sv = StateArchive::saver(st); serialize(sv);
            reset();
            auto ld = StateArchive::loader(st.data(), st.size()); serialize(ld);
            chk("stop state valid", ld.ok(), true);
            chk("stop state shift", shifterStopShift_, -12);
            chk("stop state inc",   vcStopInc_, -6);
        }
        reset();
        beamClock_ = oldBeam; liveFrameClock_ = oldLive;
    }

    // VBlank : limites du comparateur502, quatre checks et forçage par VSYNC.
    auto vb = [&](int hz, std::vector<std::array<int,4>> writes) {
        reset(); mode = Mode::Low; sync = hz == 50 ? 2 : 0; beginFrame();
        for (const auto& w : writes)
            syncWrites_.push_back({w[0] * geometry().cyclesPerLine + w[1],
                                   static_cast<uint8_t>(w[3]), w[2] != 0});
    };
    vb(50, {});
    chk("vblank PAL off before", vblankAtLine(24), true);
    chk("vblank PAL off after", vblankAtLine(25), false);
    chk("vblank PAL on before", vblankAtLine(307), false);
    chk("vblank PAL on after", vblankAtLine(308), true);
    vb(60, {});
    chk("vblank NTSC off before", vblankAtLine(15), true);
    chk("vblank NTSC off after", vblankAtLine(16), false);
    chk("vblank NTSC on before", vblankAtLine(257), false);
    chk("vblank NTSC on after", vblankAtLine(258), true);
    for (int hz : {50,60}) for (bool on : {false,true}) {
        const int checkLine = hz == 50 ? (on ? 307 : 24) : (on ? 257 : 15);
        for (int cycle : {502,503}) {
            vb(hz, {{checkLine,cycle,0,hz == 50 ? 0 : 2}});
            chk("vblank freq502 deadline", vblankAtLine(checkLine + 1),
                cycle == 502 ? !on : on);
        }
    }
    if (!machineIsSte(bus_.machine)) {
        vb(50, {{307,503,1,2}});
        chk("vblank STF res lead", vblankAtLine(308), false);
        vb(50, {{307,504,1,2}});
        chk("vblank STF res late", vblankAtLine(308), true);
    }
    vb(50, {{307,500,0,0}, {308,0,0,2}});
    chk("vblank missed check", vblankAtLine(308), false);
    chk("vblank VSYNC310", vblankAtLine(310), true);
    vb(60, {{257,500,0,2}, {258,0,0,0}});
    chk("vblank missed NTSC check", vblankAtLine(258), false);
    chk("vblank VSYNC260", vblankAtLine(260), true);
    // Grille réelle : une écriture avant502 dans la ligne307 raccourcie doit
    // être attribuée à cette ligne, sans réutiliser la grille nominale.
    if (lineLenAttrEnv()) {
        vb(50, {});
        const int64_t start = int64_t(307) * geometry().cyclesPerLine - 64;
        glueLineStart_[307] = start; glueLineStart_[308] = start + 508;
        syncWrites_.push_back({static_cast<int32_t>(start + 503),0,false});
        chk("vblank real grid deadline", vblankAtLine(308), true);
    }
    // La copie de ligne VBlank ne crédite ni octets, ni prefetch/line-offset.
    vb(50, {});
    vcLineY_ = 245; commitAnchor_ = 63; lineWidth = 7;
    hwScrollCount = 4; hwScrollPrefetch = true;
    const uint32_t oldBase = vcLineBase_;
    endVideoLine();
    chk("vblank no counter stride", vcLineBase_, oldBase);
    chk("vblank no RAM snapshot", lineSnapLen_[308], 0);
    // Bordure basse : un moteur single-buffer efface la RAM APRÈS le HBL.
    // Le replay doit garder les pixels balayés, y compris au-delà de curAH_.
    // Contre-épreuve sans retrait : les mêmes lignes restent en couleur de bord.
    for (bool openBottom : {false, true}) {
        reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000;
        palette.fill(0x0700); palette[0] = 0x0070;
        beginFrame();
        if (openBottom) {
            syncWrites_.push_back({262 * geometry().cyclesPerLine + 420, 0, false});
            syncWrites_.push_back({263 * geometry().cyclesPerLine + 40, 2, false});
        }
        // Tous les plans : rouge, indépendamment des mots sautés par le trick.
        for (uint32_t a = 0x20000; a < 0x30000; a += 2) bus_.write16(a, 0xFFFF);
        commitScanline(280);
        chk("bottom live snapshot", lineSnapLen_[280] > 0, openBottom);
        const uint32_t capturedBase = vcLineBase_;
        commitScanline(280);
        chk("bottom commit idempotent", vcLineBase_, capturedBase);
        for (uint32_t a = 0x20000; a < 0x30000; a += 2) bus_.write16(a, 0);
        finishFrame();
        const int row = activeY_ + 280 - geometry().dispStartLine;
        chk("bottom pixels survive erase", frame_[row * curW_ + activeX_ + 32],
            stColorToArgb(openBottom ? 0x0700 : 0x0070));
    }
    // Sans écriture palette ni bordure élargie, les lignes NO_DE et STOP_MIDDLE
    // doivent quand même être masquées par le rendu final. BLANK consomme la
    // RAM, NO_DE ne la consomme pas ; STOP_MIDDLE n'en lit que 54 octets.
    for (int trick = 0; trick < 3; ++trick) {
        reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000;
        palette.fill(0x0777); palette[0] = 0x0070;
        beginFrame();
        const int first = trick == 0 ? 28 : trick == 1 ? 20 : 100;
        const int last = trick == 0 ? 76 : trick == 1 ? 52 : 200;
        for (uint32_t a = 0x20000; a < 0x30000; a += 2) bus_.write16(a, 0xFFFF);
        // Comme en exécution : les écritures de 100 n'existent pas au HBL 99.
        commitScanline(99);
        syncWrites_.push_back({100 * cpl + first, uint8_t(trick == 0 ? 0 : 2), trick != 0});
        syncWrites_.push_back({100 * cpl + last, uint8_t(trick == 0 ? 2 : 0), trick != 0});
        const uint32_t before = vcLineBase_;
        commitScanline(100);
        chk("masked line stride", vcLineBase_ - before, trick == 0 ? 162 : trick == 1 ? 0 : 54);
        chk("masked no palette", spec512Active_, false);
        for (int y = 0; y < curAH_; ++y) renderLine(y);
        finishFrame();
        const int row = activeY_ + 100 - geometry().dispStartLine;
        chk("masked line pixels", frame_[row * curW_ + activeX_ + 250], stColorToArgb(0x0070));
        if (trick != 0) chk("masked no expanded border", bordersTrick_, false);
    }
    // NO_SYNC/SYNC_HIGH : une ligne sans DE inhibe l'avance vidéo et Timer B,
    // puis la fenêtre verticale gagne une ligne. Un retour AVANT le comparateur
    // HSYNC concerné annule le masque de la ligne suivante et cette extension.
    for (bool highSync : {false, true}) for (bool cancel : {false, true}) {
        reset(); mode = Mode::Low; sync = 2; videoBase = 0x20000;
        palette.fill(0x0777); palette[0] = 0x0070;
        beginFrame();
        for (uint32_t a = 0x20000; a < 0x30000; a += 2) bus_.write16(a, 0xFFFF);
        commitScanline(99);
        const int first = highSync ? 480 : 420;
        const int last = highSync ? (cancel ? 496 : 524) : (cancel ? 448 : 480);
        syncWrites_.push_back({100 * cpl + first, 2, true});
        liveGlueCatchUp(100);
        chk("sync pending blank", glueBlankLines_, 1);
        // Retour SYNC_HIGH non annulé sur la ligne suivante : servir le HBL
        // avant d'injecter cette écriture, comme le ferait le CPU.
        if (last >= cpl) commitScanline(100);
        syncWrites_.push_back({100 * cpl + last, 0, true});
        liveGlueCatchUp(last >= cpl ? 101 : 100);
        if (last < cpl) commitScanline(100);
        chk("sync blank count", glueBlankLines_, cancel ? 0 : 1);
        const uint32_t mask = highSync ? glue::SYNC_HIGH : glue::NO_SYNC;
        chk("sync current mask", (glueLines_[100].borderMask & mask) != 0, !cancel);
        chk("sync next no count", (glueLines_[101].borderMask & glue::NO_COUNT) != 0, !cancel);
        chk("sync next Timer B", liveLineDisplayed(101), cancel);
        const uint32_t before = vcLineBase_;
        commitScanline(101);
        chk("sync next stride", vcLineBase_ - before, cancel ? 160 : 0);
        chk("sync following stride", glueLineBytes(102), 160);
        chk("sync bottom extended", liveLineDisplayed(263), !cancel);
        chk("sync after bottom", liveLineDisplayed(264), false);
        commitScanline(263);
        chk("sync total bytes", vcLineBase_ - vcFrameBase_, 32000);
        for (int y = 0; y < curAH_; ++y) renderLine(y);
        finishFrame();
        const int nextRow = activeY_ + 101 - geometry().dispStartLine;
        const int bottomRow = activeY_ + 263 - geometry().dispStartLine;
        chk("sync next pixels", frame_[nextRow * curW_ + activeX_ + 100],
            stColorToArgb(cancel ? 0x0777 : 0x0070));
        chk("sync bottom pixels", frame_[bottomRow * curW_ + activeX_ + 100],
            stColorToArgb(cancel ? 0x0070 : 0x0777));
        chk("sync replay blank count", glueBlankLines_, cancel ? 0 : 1);
    }
    // Le signal est entièrement reconstructible avec les champs déjà sauvés.
    vb(50, {{307,500,0,0}, {308,0,0,2}});
    std::vector<uint8_t> state;
    auto save = StateArchive::saver(state); serialize(save);
    reset();
    auto load = StateArchive::loader(state.data(), state.size()); serialize(load);
    chk("vblank state valid", load.ok(), true);
    chk("vblank state missed check", vblankAtLine(308), false);
    chk("vblank state vsync", vblankAtLine(310), true);
    reset();

    std::fprintf(stderr, "[glue-selftest] %d OK, %d FAIL\n", pass, fail);
    return fail == 0;
}

