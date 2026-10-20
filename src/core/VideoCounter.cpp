// =============================================================================
//  VideoCounter.cpp — le COMPTEUR VIDÉO et l'avance du faisceau (A32, 2026-08-28).
//
//  Rôle 3 des six que portait Shifter.cpp : l'adresse vidéo courante lue en
//  $FF8205/07/09, son avance par ligne (endVideoLine), le commit d'une scanline,
//  le restart mi-trame (RestartVideoCounter) et la position du faisceau.
//
//  C'est ici que vivait le pire du fichier : `videoCounter()` faisait 208 lignes
//  et se déclarait `const` tout en avançant la machine Glue à travers QUATRE
//  `const_cast`. Elle n'est plus const (A32) — son seul appelant, Shifter::read8,
//  ne l'est pas non plus.
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

// Octets RÉELLEMENT lus par le shifter sur la scanline `scanline` (hors line-offset
// STE et scroll, ajoutés par l'appelant) : 160 nominal, modulé par les drapeaux de
// bordure de la machine Glue (port BORDERBYTES_* / Video_CopyScreenLineColor). Sans
// écriture freq/res cette trame (cas ultra-majoritaire), renvoie le bpl nominal.
// Ancre verticale du commit de scanlines (endVideoLine/commitScanline) : la 1ʳᵉ
// scanline dont le commit avance le compteur vidéo. Base = liveStartHBL_ (sticky,
// chemin historique, zéro régression hors tricks). Sur une trame à écritures
// freq/res, la fenêtre verticale RÉELLE fait foi : glueStartHBL_ (machine Glue
// LIVE, port fidèle du nStartHBL d'Hatari — une paire 60/50 trop tôt dans la
// ligne n'ouvre PAS le haut, contrairement au sticky). max() : un vrai retrait
// haut donne 34 des deux côtés ; le sticky bogus (34 vs Glue 63) suit la Glue.
// LATCHÉE au 1er commit de la trame (reset à beginFrame) : si la Glue re-ferme le
// haut APRÈS le début des commits, l'ancre ne bouge plus — sl reste monotone et
// lineSnap_/vcLineY_ gardent une indexation stable (verrou Cuddly conservé).
int Shifter::commitAnchor() {
    if (commitAnchor_ >= 0) return commitAnchor_;
    int a = liveStartHBL_;
    if (frameMode_ != Mode::High && !syncWrites_.empty() && glueStartHBL_ > a)
        a = glueStartHBL_;
    return a;
}

// Fin de Video_CopyScreenLine (video.c:3833-3872), adaptée au modèle NeoST : la
// ligne active vient d'être décodée → le compteur vidéo avance de son stride réel,
// puis les modifications STE différées pendant la ligne s'appliquent, dans l'ordre
// d'Hatari : scroll-prefetch (+1 mot), line-offset, offset compteur ($FF8205/07/09
// écrits pendant le DE), nouveau scroll fin, nouvelle largeur de ligne.
void Shifter::endVideoLine() {
    // Stride réel de la scanline qui se termine : les retraits de bordure de CETTE
    // ligne modifient le nombre d'octets lus (accumulation inter-lignes — la
    // calibration fullscreen d'Enchanted Land mesure $FF8209 à travers des lignes
    // élargies/raccourcies). Sans trick, glueLineBytes == bpl (chemin historique).
    if (!syncWrites_.empty())
        liveGlueCatchUp(commitAnchor() + vcLineY_);
    // Ancre verticale du commit : commitAnchor() = max(liveStartHBL_, glueStartHBL_)
    // sur une trame à tricks, LATCHÉE au 1er commit de la trame (cf. déclaration).
    // liveStartHBL_ (sticky) passe à 34 sur TOUTE bascule 60 Hz avant la ligne 63 —
    // y compris une paire 60/50 qui n'ouvre PAS réellement le haut (Hatari exige que
    // le 60 Hz couvre la comparaison de fin de ligne 33). La calibration de Lethal
    // Xcess émet une telle paire à la ligne 32 : committer sur l'ancre sticky 34
    // faisait avancer le compteur vidéo de 29 lignes de bordure haute (160 octets
    // chacune, glueLineBytes ne connaissant pas la fenêtre VERTICALE) → le poll
    // $FF8209 de fin de trame lisait base+228 lignes au lieu de base+199 → le jeu
    // croyait son overscan ouvert → écran en jeu déchiré puis chargements en échec
    // (2026-07-12, oracle Hatari byte-exact sur ce poll).
    if (commitAnchor_ < 0) commitAnchor_ = commitAnchor();   // latch au 1er commit
    const int sl  = commitAnchor_ + vcLineY_;
    const int bpl = glueLineBytes(sl);
    // Capture les octets que le shifter vient de lire sur CETTE ligne (échantillon
    // daté au faisceau, cf. lineSnap_) : bpl + marge d'arrondi au groupe de 16 px
    // du décodage fenêtré (+ avance scroll STE). Consommé par renderGlueFrame —
    // donc capturé si la trame est candidate au rendu glue : écritures freq/res
    // (syncWrites_) OU palette touchée (spec512Active_, seuil 1 = toute écriture
    // palette re-rend la trame ; sans capture, renderGlueFrame relirait la RAM de
    // FIN de trame et ré-introduirait l'artefact « sprite en course avec le
    // faisceau » sur tout moteur single-buffer qui pose une couleur au VBL). Les
    // lignes AVANT la 1ʳᵉ écriture palette restent en repli RAM — acceptable.
    // Zéro coût sur une trame qui ne touche ni sync/res ni palette.
    // DEBUG (NEOST_NO_SNAP=1) : neutralise la capture par-ligne → renderGlueFrame
    // retombe sur la relecture RAM de fin de trame. Discrimine « instant de capture
    // faux » vs « autre cause » quand un moteur redessine en course avec le faisceau.
    static const bool noSnap = envFlag("NEOST_NO_SNAP", false);
    if (!noSnap && (!syncWrites_.empty() || spec512Active_ || shifterStopLine_ >= 0) && bpl > 0 &&
        sl >= 0 && sl < static_cast<int>(lineSnapLen_.size())) {
        int n = bpl + scrollCounterAdvance(sl) + 8;
        if (n > kLineSnapBytes - kSnapLead) n = kLineSnapBytes - kSnapLead;
        uint8_t* snap = lineSnap_.data() + static_cast<std::size_t>(sl) * kLineSnapBytes;
        // kSnapLead octets de garde AVANT la base de ligne (offsets sources
        // négatifs du scroll hard, cf. déclaration kSnapLead) puis la ligne.
        for (int i = 0; i < n + kSnapLead; ++i)
            snap[i] = bus_.read8((vcLineBase_ - static_cast<uint32_t>(kSnapLead)
                                  + static_cast<uint32_t>(i)) & kVideoAddrMask);   // DMA vidéo 22 bits
        lineSnapLen_[sl] = static_cast<uint16_t>(n);
    }
    // Métadonnées TOUJOURS capturées, même sans copie RAM : une première écriture
    // palette tardive peut déclencher le replay de lignes déjà terminées. Leur
    // latch ne doit pas devenir celui de fin de trame (split 320/336, par ex.).
    // Bit 7 = Shifter arrêté (res 3), 6 = ligne committée, 5 = latch 336 px,
    // 4 = prefetch, 0-3 = scroll.
    if (sl >= 0 && sl < static_cast<int>(lineScrollSnap_.size()))
        lineScrollSnap_[sl] = uint8_t(0x40 | hwScrollCount | (hwScrollPrefetch ? 0x10 : 0)
                                     | (steBorderFlag_ ? 0x20 : 0)
                                     | (lineStoppedOver(sl) ? 0x80 : 0));
    vcLineBase_ += static_cast<uint32_t>(bpl);
    // Prefetch et line-offset STE ne s'appliquent QUE sur une ligne réellement
    // affichée : chez Hatari les deux sont dans la branche « else » de la ligne
    // totalement blanche (video.c:3993 → :4213 « pVideoRaster += LineWidth*2 »),
    // seuls l'offset différé et les latches HSCROLL/LINEWIDTH ci-dessous en sont
    // dehors (video.c:4329-4375). Les appliquer sur une ligne NO_DE faisait dériver
    // le compteur vidéo d'autant d'octets par ligne non affichée — visible sur STE
    // à scrolling matériel dès qu'une trame se termine avant sa fin nominale.
    if (bpl > 0) {
        vcLineBase_ += static_cast<uint32_t>(scrollCounterAdvance(sl));
        vcLineBase_ += static_cast<uint32_t>(lineWidth) * 2u;  // line-offset STE (mots sautés)
    }
    if (vcDelayedOffset_ != 0) {                           // écriture compteur pendant le DE
        vcLineBase_ += static_cast<uint32_t>(vcDelayedOffset_ & ~1);
        vcDelayedOffset_ = 0;
    }
    if (newHwScrollCount_ >= 0) {                          // HSCROLL différé (NewHWScrollCount)
        hwScrollCount    = static_cast<uint8_t>(newHwScrollCount_);
        hwScrollPrefetch = newHwScrollPrefetch_;
        steBorderFlag_  = newSteBorderFlag_;
        newHwScrollCount_ = -1;
    }
    if (newLineWidth_ >= 0) {                              // LINEWIDTH différé (NewLineWidth)
        lineWidth = static_cast<uint8_t>(newLineWidth_);
        newLineWidth_ = -1;
    }
    // Scroll hard « Shifter arrêté » : ajustement du compteur à la fin de la ligne
    // du stop (port VideoRasterDelayedInc, video.c:4410-4416 — hors de la branche
    // « ligne blanche », donc appliqué quelle que soit la ligne).
    if (vcStopInc_ != 0) {
        vcLineBase_ += static_cast<uint32_t>(vcStopInc_);
        vcStopInc_ = 0;
    }
    // Repli du compteur sur 22 bits (port video.c:4423 « pVideoRaster & VideoMask ») :
    // le compteur matériel n'a que A1..A21 — au-delà de $3FFFFF il reprend en $000000.
    vcLineBase_ &= kVideoAddrMask;
    ++vcLineY_;
}

// Commit des scanlines TERMINÉES au HBL de chaque ligne (port de l'appel
// Video_EndHBL du handler HBL d'Hatari, video.c:3319) : la capture lineSnap_
// d'une ligne se fait à SA fin de ligne (~cycle 512). Avant, le commit paresseux
// n'était tiré que par renderLine(y+1) au cycle 376 de la ligne active SUIVANTE
// (~380 cycles plus tard) — et en overscan HAUT (Enchanted Land en jeu), la grille
// RENDER restant ancrée sur dispStartLine=63 alors que l'affichage commence à 34,
// les captures traînaient de 29 lignes entières (~15 000 cycles). Un moteur qui
// efface/redessine ses sprites en CHASSANT le faisceau tombait dans cette
// fenêtre : les lignes du sprite étaient capturées APRÈS son effacement → sprite
// invisible ou tronqué selon sa position verticale (sprite EL absent en saut).
// Plafond : curAH_ (mêmes lignes que finishFrame) pour une trame ordinaire ; sur
// une trame à tricks (écritures freq/res), on étend aux lignes AFFICHÉES par la
// machine Glue LIVE au-delà de curAH_ (bordure basse retirée : l'affichage
// continue jusqu'à glueEndHBL_ — sans capture, ces lignes du bas retombaient sur
// la RAM de fin de trame et l'artefact sprite y persistait). Le rattrapage de
// renderLine/finishFrame reste en filet (CPU halté, bordure haute mi-trame).
void Shifter::commitScanline(int line) {
    // Met la Glue LIVE à jour jusqu'à la ligne qui se termine AVANT d'évaluer
    // l'ancre : commitAnchor() consulte glueStartHBL_ (fenêtre verticale réelle).
    if (frameMode_ != Mode::High && !syncWrites_.empty())
        liveGlueCatchUp(line);
    while (true) {
        const int sl = commitAnchor() + vcLineY_;
        if (sl > line) break;
        if (vcLineY_ >= curAH_) {
            if (frameMode_ == Mode::High || syncWrites_.empty()) break;
            if (sl + 1 >= static_cast<int>(glueLines_.size())) break;
            liveGlueCatchUp(sl);
            if (sl >= glueEndHBL_ + glueBlankLines_) break;   // fin d'affichage LIVE
        }
        endVideoLine();
    }
}

// RESTART du compteur vidéo en fin de trame — port de Video_RestartVideoCounter
// (video.c, ULM DSOTS) : à la ligne 310 (50 Hz) / 260 (60 Hz), cycle 56 (STF),
// le GLUE recharge le compteur depuis $FF8201/03. La base est relue À CET INSTANT
// (≠ début de trame NeoST) : un jeu double-buffer qui pose sa nouvelle base dans
// son handler VBL (APRÈS la ligne 0) est donc bien pris en compte — c'est ce que
// mesure le stabilisateur beam-sync d'Enchanted Land en jeu (poll $FF8209).
// Les lectures des lignes ≥ restart renvoient cette base figée (videoCounter).
void Shifter::restartVideoCounter(int line) {
    vcRestartBase_ = static_cast<int64_t>(videoBase & kVideoAddrMask);
    vcRestartLine_ = line;
}

// Position du faisceau (ligne absolue + cycle dans la ligne) depuis l'horloge de
// trame. Sert aux décisions immédiat/différé des écritures STE (port
// Video_GetPosition_OnWriteAccess). false si aucune horloge n'est branchée (tests).
bool Shifter::beamPos(int& line, int& lineCyc) const {
    if (!beamClock_) return false;
    const Geometry g = geometry();
    const int64_t fc = beamClock_();
    if (fc < 0) return false;
    line    = static_cast<int>(fc / g.cyclesPerLine);
    lineCyc = static_cast<int>(fc % g.cyclesPerLine);
    return true;
}

// Reconstruit l'adresse vidéo courante — port fidèle de Hatari Video_CalculateAddress :
//   addr = videoBase + ligne*bpl + NbBytes, avec NbBytes = ((X - LineStartCycle) >> 1) & ~1
// où X = cycles DANS la ligne et le shifter lit 2 cycles/octet entre LineStartCycle
// (56 en 50 Hz, 52 en 60 Hz ; 0 en haute rés) et LineEndCycle (376). Après la dernière
// ligne affichée, le compteur reste figé jusqu'au rechargement VBL. (L'ancienne version
// supposait 1 octet/cycle depuis le cycle 216 — faux en milieu de ligne, d'où l'échec
// du test « T0 » des diagnostics qui relisent $FF8205/07/09 au cycle près.)
uint32_t Shifter::videoCounter() {
    if (!beamClock_) return videoBase & kVideoAddrMask; // pas d'horloge → base brute
    int64_t fc = beamClock_();                          // cycles dans la trame
    fc += kVideoCounterReadOffsetCyc;                   // datation lecture fidèle Hatari (fin d'accès − 8)
    // (NEOST_VC_OFF est déjà intégré à kVideoCounterReadOffsetCyc — ne pas l'ajouter
    // une 2ᵉ fois ici ; l'ancien double-ajout faussait les balayages de calibration.)
    // Géométrie VERROUILLÉE de la trame (cycles/ligne et début DE dépendent de la
    // fréquence 50/60/71 Hz : avant, 512 et 56/52 étaient figés → compteur faux en
    // 60 Hz). frameSync_ est posé par beginFrame, comme frameMode_.
    const Geometry g = geometry();
    const int  kCyclesPerLine = g.cyclesPerLine;
    const bool hi   = (frameMode_ == Mode::High);
    const int  bpl  = hi ? 80 : 160;                    // octets/ligne affichée
    const int  disp = g.displayLines;                   // lignes affichées
    const int  lineStart = g.lineStartCycle;            // début Display-Enable (50/60/71 Hz)
    // Stride réel d'une ligne = octets affichés + line-offset STE ($FF820F, en mots)
    // + 1 mot PAR PLAN si scroll fin avec prefetch (cf. scrollCounterAdvance).
    // lineWidth=0 et scroll=0 sur ST/STF → bpl.
    const int  stride = bpl + static_cast<int>(lineWidth) * 2 + scrollCounterAdvance();
    // Compteur MATÉRIALISÉ (≙ pVideoRaster) : vcLineBase_ = début de la ligne active
    // vcLineY_ (les lignes déjà rendues ont déjà accumulé leur stride réel — lineWidth
    // variable, écritures du compteur, scroll). L'affichage couvre les lignes
    // [dispStart, dispStart+disp) (VDE_On..VDE_Off) ; avant (bordure HAUTE) le
    // compteur reste à la base latchée ; pendant, il vaut base de ligne + offset
    // intra-ligne ; après (bordure BASSE), il reste figé sur l'écran entièrement lu.
    // VDE_On/Off LIVE : sur une trame à écritures freq/res, on consulte la machine
    // Glue LIVE (glueStartHBL_/glueEndHBL_, mêmes règles que Hatari nStartHBL/nEndHBL,
    // re-fermeture comprise) — l'ancien modèle « sticky » (liveStartHBL_, retrait
    // verrouillé à la 1ʳᵉ bascule) MENTAIT aux calibrations qui testent une impulsion
    // puis vérifient si elle a réellement ouvert le haut (Enchanted Land en jeu :
    // le moteur croyait sa visée bonne alors que le GLUE la refusait). Un écran sans
    // écriture freq/res garde le chemin historique (liveStartHBL_ = 63, zéro régression).
    int line = static_cast<int>(fc / kCyclesPerLine);
    int X    = static_cast<int>(fc % kCyclesPerLine);
    // NEOST_LINELEN_ATTR : la LECTURE du compteur se mappe sur la GRILLE RÉELLE des
    // débuts de ligne (glueLineStart_), pas sur la grille fixe 512 — port du
    // Video_ConvertPosition sur nCyclesPerLine réel qu'utilise
    // Video_CalculateAddress (video.c). Décisif pour The Cuddly Demos (menu
    // robot) : le synchroniseur de la démo (pc=f264) émet une paire 60/50 Hz
    // PAR LIGNE → ces lignes font 508 cycles au comparateur HBL → chez Hatari le
    // compteur de chaque ligne suivante démarre 4 cyc plus tôt (+2 octets lus
    // par paire émise). En grille fixe, NeoST lisait 4-6 octets de MOINS que
    // Hatari au même instant → la sortie du poll (octet bas > $40, comparaison
    // SIGNÉE) glissait de la ligne 34 à la ligne 36 ~1 trame sur 10 → le
    // clignotement vertical bistable (fenêtre 34..310 ↔ 63..263). Mesuré à
    // l'oracle (traces video_addr + cpu_disasm, 2026-07-03) : chemin CPU et
    // ancre VBL identiques, seule la fonction valeur(t) différait.
    const bool lineLenRead = lineLenAttrEnv();
    if (lineLenRead && frameMode_ != Mode::High && !syncWrites_.empty()
        && static_cast<std::size_t>(line) + 2 < glueLineStart_.size()) {
        liveGlueCatchUp(line + 1);
        int wl = liveGlueLine_ < line + 1 ? liveGlueLine_ : line + 1;
        if (wl >= 0) {
            while (wl > 0 && fc < glueLineStart_[wl]) --wl;
            while (wl < liveGlueLine_ && static_cast<std::size_t>(wl) + 1 < glueLineStart_.size()
                   && fc >= glueLineStart_[wl + 1]) ++wl;
            line = wl;
            X = static_cast<int>(fc - glueLineStart_[wl]);
        }
    }
    // Compteur REDÉMARRÉ en fin de trame (port Video_RestartVideoCounter, ligne
    // 310/260 cycle 56) : les lectures au-delà renvoient la base rechargée, FIGÉE
    // jusqu'au DE de la trame suivante (chez Hatari, pVideoRaster vaut alors
    // &STRam[VideoBase] et plus rien ne l'avance avant la prochaine ligne affichée).
    if (vcRestartBase_ >= 0 && line >= vcRestartLine_)
        return static_cast<uint32_t>(vcRestartBase_) & kVideoAddrMask;
    int dispStart = liveStartHBL_;
    int disp2     = disp;
    if (frameMode_ != Mode::High && !syncWrites_.empty()
        && static_cast<std::size_t>(line) + 1 < glueLines_.size()) {
        liveGlueCatchUp(line);
        dispStart = glueStartHBL_;
        // Bordure basse retirée → plus long ; + BlankLines (lignes blanches no-sync
        // insérées) comme le `nEndHBL + BlankLines` de Video_CalculateAddress
        // (video.c:1568) — ces lignes sont NO_DE (glueLineBytes = 0), elles ne font
        // pas avancer le compteur mais restent dans la fenêtre active.
        disp2     = glueEndHBL_ + glueBlankLines_ - glueStartHBL_;
    }
    const int  la   = line - dispStart;                 // index de ligne active du faisceau
    uint32_t addr = vcLineBase_;
    if (la >= 0) {
        // Ligne au-delà de celle du compteur matérialisé (rendu pas encore passé) :
        // extrapole au stride courant. Bordure basse : figé à l'écran entièrement lu.
        const int laEff = la < disp2 ? la : disp2;
        if (laEff > vcLineY_) {
            if (!syncWrites_.empty() && frameMode_ != Mode::High) {
                // Trame à tricks : somme des octets RÉELS par ligne (machine Glue
                // live) — une ligne élargie (left/right off) ou raccourcie (-2)
                // décale toutes les suivantes (accumulation inter-lignes, port de
                // Video_CalculateAddress qui parcourt ShifterLines[]).
                liveGlueCatchUp(line);
                // `extra` (prefetch scroll + line-offset STE) ne s'applique qu'aux
                // lignes RÉELLEMENT affichées, exactement comme le chemin de commit
                // endVideoLine (`if (bpl > 0)`, Shifter.cpp:785 ≙ video.c:4213). Une
                // ligne NO_DE (glueLineBytes == 0 : trick freq, fenêtre verticale
                // raccourcie) ne fait avancer NI line-offset NI prefetch — l'ajouter
                // ici faisait diverger l'extrapolation du commit, et le compteur
                // reculait entre deux lectures $FF8205/07/09 encadrant un HBL.
                for (int y = vcLineY_; y < laEff; ++y) {
                    const int lb = glueLineBytes(dispStart + y);
                    const int extra = static_cast<int>(lineWidth) * 2 + scrollCounterAdvance(dispStart + y);
                    addr += static_cast<uint32_t>(lb + (lb > 0 ? extra : 0));
                }
            } else {
                addr += static_cast<uint32_t>(laEff - vcLineY_) * static_cast<uint32_t>(stride);
            }
        }
        // Offset intra-ligne UNIQUEMENT si la ligne courante n'a pas déjà été rendue
        // (la < vcLineY_ = bordure droite : le stride de la ligne est déjà accumulé).
        if (la < disp2 && laEff >= vcLineY_) {
            // Port FIDÈLE de Video_CalculateAddress (video.c:1508-1565, 2026-07-02) :
            // pour l'intra-ligne, Hatari NE lit PAS DisplayStart/EndCycle de la Glue —
            // il RECONSTRUIT le départ (ds) et la TAILLE (CurSize) depuis le
            // borderMask :  LEFT_OFF → ds = LINE_START_CYCLE_71 = **0** (pas le
            // HDE_On_Hi=4 de la Glue !), LEFT_PLUS_2 → 52 ; fin = ds + CurSize×2
            // avec CurSize par la table de bordures (fullscreen = 230). L'ancienne
            // version NeoST (ds = displayStartCycle, taille = (End−Start)/2 = 229)
            // était fausse de 2-4 octets sur les lignes à tricks → le stabilisateur
            // beam-sync d'Enchanted Land en jeu (poll $8209 sur lignes fullscreen)
            // se calait −16 cyc vs Hatari (freq 360/368 au lieu de 376/384) → ses
            // impulsions rataient la fenêtre bordure-droite (372,376] → scroll qui
            // saute. Trame SANS écriture freq/res → chemin historique inchangé.
            int ds = lineStart, lineBytes = bpl;
            bool leftOff = false;
            uint32_t bm = 0;
            bool haveGlue = false;
            if (frameMode_ != Mode::High && !syncWrites_.empty()
                && static_cast<std::size_t>(line) + 1 < glueLines_.size()) {
                liveGlueCatchUp(line);
                const GlueLine& L = glueLines_[static_cast<std::size_t>(line)];
                if (L.displayStartCycle >= 0) {
                    haveGlue = true;
                    bm = L.borderMask;
                    leftOff = (bm & glue::LEFT_OFF) != 0;
                    int curSize = 160;                                // BORDERBYTES_NORMAL
                    if (leftOff)                       curSize += 26; // BORDERBYTES_LEFT
                    // Retrait gauche COURT du STE : +20 o comme les autres masques
                    // (video.c:1514-1517, BORDERBYTES_LEFT_2_STE). Il manquait ici
                    // alors que glueLineBytes() le crédite déjà — la ligne faisait
                    // donc 180 o pour l'accumulation inter-lignes mais 160 pour
                    // l'offset intra-ligne, et la borne de gel LineEndCycle tombait
                    // 40 cycles trop tôt. Ordre repris d'Hatari : LEFT_OFF garde la
                    // priorité, LEFT_PLUS_2 reste APRÈS les variantes STE.
                    else if (bm & glue::LEFT_OFF_2_STE) curSize += 20 + (steBorderFlag_ ? 8 : 0);
                    else if (bm & glue::LEFT_OFF_2_STE_MED) curSize += 20;
                    else if (bm & glue::LEFT_PLUS_2)   curSize += 2;
                    else if (steBorderFlag_)          curSize += 8;
                    else if (hwScrollCount && hwScrollPrefetch) curSize += 8;
                    if (bm & glue::STOP_MIDDLE)        curSize -= 106;
                    else if (bm & glue::RIGHT_MINUS_2) curSize -= 2;
                    else if (bm & glue::RIGHT_OFF)     curSize += 44; // BORDERBYTES_RIGHT
                    if (bm & glue::RIGHT_OFF_FULL)     curSize += 22; // BORDERBYTES_RIGHT_FULL
                    if (bm & glue::LEFT_PLUS_2)        ds = 52;       // LINE_START_CYCLE_60
                    else if (leftOff)                  ds = 0;        // LINE_START_CYCLE_71
                    else if (steBorderFlag_ || (hwScrollCount && hwScrollPrefetch)) ds = lineStart - 16;
                    lineBytes = curSize;
                }
            }
            // Scroll fin STE avec PREFETCH hors trame à tricks : le MMU démarre 16
            // cycles avant le HDE_On et lit 8 octets de plus (l'ANCRE que mesure la
            // calibration sync-scroll d'EL — sans ce −16 ses impulsions visaient
            // ~16 cyc trop tôt et aucun comparateur n'était enjambé).
            if (!haveGlue && frameMode_ != Mode::High
                && (steBorderFlag_ || (hwScrollCount && hwScrollPrefetch))) { ds -= 16; lineBytes += 8; }
            int Xc = X;
            if (Xc < ds) Xc = ds;
            else if (Xc > ds + lineBytes * 2) {
                Xc = ds + lineBytes * 2;               // affichage désactivé (bordure droite)
                addr += static_cast<uint32_t>(lineWidth) * 2u;   // STE : mots sautés dès DE off
            }
            int nb = (Xc - ds) >> 1;                    // 2 cycles par octet
            nb &= ~1;                                   // le shifter lit par MOTS
            // Bordure gauche ouverte : 2 octets de moins que la valeur théorique
            // (26 octets non multiples de 4 cycles) — SANS garde nb>0, comme Hatari.
            if (leftOff) nb -= 2;
            if (bm & glue::NO_DE) nb = 0;
            addr += static_cast<uint32_t>(nb);
        }
    }
    // RESTART du compteur en fin de trame (Video_RestartVideoCounter, HBL 310/260
    // cycle 56, ULM DSOTS) : PORTÉ (2026-07-02) — cf. le early-return vcRestartBase_
    // en tête. Indispensable aux moteurs double-buffer beam-syncés (Enchanted Land
    // en jeu) : la base du handler VBL est posée APRÈS la ligne 0 → le latch
    // begin-frame seul renvoyait l'ANCIEN buffer toute la trame et le stabilisateur
    // ($8209) tournait à vide. ⚠ L'ancienne crainte (« un poll compteur==base sort à
    // la ligne 310 et bascule 60 Hz à la frontière → géométrie de trame flip ») est
    // re-testée aux étalons : overscan_top et make_overscan_test restent verts.
    // DIAG (NEOST_VC_TRACE=1) : chaque lecture du compteur vidéo, datée au cycle
    // trame — à diff'er entre builds/против Hatari video_addr (calibrations LX/EL).
    static const bool vcTrace = std::getenv("NEOST_VC_TRACE") != nullptr;
    if (vcTrace) {
        std::fprintf(stderr, "[VC] fc=%lld line=%d X=%d addr=%06X vcY=%d vcB=%06X"
                     " dispStart=%d disp2=%d la=%d sw=%zu startHBL=%d [",
                     (long long)fc, line, X, addr & 0xFFFFFF, vcLineY_, vcLineBase_,
                     dispStart, disp2, la, syncWrites_.size(), glueStartHBL_);
        for (std::size_t i = 0; i < syncWrites_.size() && i < 8; ++i)
            std::fprintf(stderr, "%s%c%02x@%d", i ? " " : "",
                         syncWrites_[i].isRes ? 'r' : 'f', syncWrites_[i].val,
                         syncWrites_[i].frameCycle);
        std::fprintf(stderr, "]\n");
    }
    // Compteur 22 bits : l'intra-ligne/l'extrapolation ne peuvent pas « déborder »
    // au-delà de $3FFFFF (registre $FF8205 sur 6 bits utiles, cf. kVideoAddrMask).
    return addr & kVideoAddrMask;
}

// Écriture du compteur vidéo $FF8205/07/09 (STE/TT) — port fidèle de
// Video_ScreenCounter_WriteByte (video.c:5145-5250). On reconstruit la nouvelle
// adresse en remplaçant UN octet de la valeur courante (corrigée d'une éventuelle
// modification déjà différée), puis :
//   • affichage pas commencé sur la ligne (cycle ≤ MMUStart = HDE_On − 16 si scroll),
//     ligne déjà rendue (bordure droite — notre rendu à DE_end est déjà passé, ce qui
//     équivaut au pVideoRasterDelayed d'Hatari appliqué en fin de ligne), ou faisceau
//     hors zone affichée → application IMMÉDIATE au compteur matérialisé ;
//   • écriture PENDANT le DE → on mémorise l'ÉCART (vcDelayedOffset_), appliqué à la
//     fin de la ligne (endVideoLine) — sur un vrai STE cela produit des artefacts,
//     l'adresse de fin de ligne est en revanche exacte. Étalons : Stardust Tunnel STE,
//     Braindamage End Part.
void Shifter::writeVideoCounterByte(uint32_t addr, uint8_t v) {
    // Octet haut limité comme la base/DMA : adresses vidéo ≤ $3FFFFF sur les machines
    // ≤ 4 Mo (port DMA_MaskAddressHigh — NeoST plafonne la ST-RAM à 4 Mo).
    if (addr == 0xFF8205) v &= 0x3F;
    const uint32_t cur = videoCounter();                       // adresse courante (brute)
    uint32_t an = (cur + static_cast<uint32_t>(vcDelayedOffset_)) & kVideoAddrMask;
    if (addr == 0xFF8205)      an = (an & 0x00FFFFu) | (uint32_t(v) << 16);
    else if (addr == 0xFF8207) an = (an & 0xFF00FFu) | (uint32_t(v) << 8);
    else                       an = (an & 0xFFFF00u) | uint32_t(v);
    an &= ~1u;      // bit 0 forcé à 0 (compteur aligné mot — Hatari video.c:5179 addr_new &= ~1)

    int line = 0, cyc = 0;
    const Geometry g  = geometry();
    const bool havePos = beamPos(line, cyc);
    const int  la      = havePos ? line - liveStartHBL_ : -1;
    const bool active  = havePos && la >= 0 && la < g.displayLines;
    // Le MMU commence à lire 16 cycles AVANT le HDE_On quand le scroll fin est armé
    // AVEC PREFETCH ($FF8265 ; rien via $FF8264) — port Video_GetMMUStartCycle.
    const int mmuStart = g.lineStartCycle - ((steBorderFlag_ || (hwScrollCount && hwScrollPrefetch)) ? 16 : 0);
    if (!havePos || !active || cyc <= mmuStart || la < vcLineY_) {
        vcLineBase_ = an;                                      // application immédiate
        vcDelayedOffset_ = 0;
    } else {
        vcDelayedOffset_ = static_cast<int>(an) - static_cast<int>(cur);  // pendant le DE → fin de ligne
    }
}

