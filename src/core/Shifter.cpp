// =============================================================================
//  Shifter.cpp — Décodage planaire ST (basse/moyenne/haute) → buffer ARGB.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Shifter.hpp"
#include <algorithm>

// Décalage d'alignement pixel↔couleur du re-rendu spec512, en cycles (8 MHz).
// MAPPING PHYSIQUE : une écriture datée au cycle F (horloge live de Moira) colore
// le pixel balayé au cycle F → décalage nul. On ne CALIBRE PAS d'offset empirique :
// le re-rendu est correct DÈS QUE le flux d'écritures est au cycle près. Or il ne
// l'est pas encore tout à fait (cf. limite ci-dessous), mais on garde le mapping
// honnête plutôt qu'une constante magique qui deviendrait fausse une fois le timing
// corrigé.
//
// ⚠ LIMITE CONNUE (diff oracle Hatari, cf. CHANGELOG / TODO) : les écritures palette
// de NeoST matchent Hatari en couleur ET en synchro de ligne, mais leur position
// INTRA-ligne dérive de ~2 cycles/ligne (mesuré dLC −4 ligne 40 → −124 ligne 100).
// Cause : Moira est un 68000 PUR — il ne modélise pas les wait states / la contention
// de bus du ST (la vidéo vole des cycles au CPU) qu'Hatari ajoute. Résultat : sur une
// vraie image Spectrum 512 (BEE512), le haut sort net puis les couleurs dérivent vers
// le bas. Le correctif de fond = wait states + contention bus (TODO « précision cycle »),
// socle partagé avec la suppression de bordures.
static constexpr int kSpec512AlignCyc = 0;

// Seuil de détection « image spec512 » : nombre d'écritures palette par trame
// au-delà duquel on bascule sur le re-rendu intra-ligne. Bien au-dessus d'un
// usage normal (16 couleurs posées une fois = 16, ou raster-bars ~ quelques
// centaines) pour ne JAMAIS toucher les trames ordinaires. Une image Spectrum
// 512 réécrit ~48 couleurs/ligne sur 200 lignes (~10000/trame). Note : on compte
// les écritures OCTET (un mot = 2), d'où une marge confortable.
static constexpr int kSpec512Threshold = 1024;

Shifter::Shifter(Bus& bus) : bus_(bus) {
    resizeFor(mode);
}

uint32_t Shifter::stColorToArgb(uint16_t c) {
    // Format STE : %0000 RRRR GGGG BBBB (4 bits par canal). Le ST n'utilise que
    // les 3 bits bas de chaque nibble ; le STE ajoute un 4e bit (bit3) qui pèse
    // une DEMI-marche, d'où des teintes intermédiaires. Port fidèle de Hatari
    // conv_st.c (ConvST_SetupRGBTable) : v8 = ((c4&0x7)<<1)|((c4&0x8)>>3) puis
    // v8 |= v8<<4 (réplique le nibble pour étirer 4→8 bits). Pour un coloris ST
    // (bit3=0) cela donne la même nuance qu'avant (ex. 7 → 0xEE).
    const uint8_t r4 = (c >> 8) & 0xF;
    const uint8_t g4 = (c >> 4) & 0xF;
    const uint8_t b4 = (c >> 0) & 0xF;
    auto ex = [](uint8_t c4) -> uint32_t {
        uint32_t v = ((c4 & 0x7u) << 1) | ((c4 & 0x8u) >> 3);
        v |= v << 4;
        return v;
    };
    return 0xFF000000u | (ex(r4) << 16) | (ex(g4) << 8) | ex(b4);
}

void Shifter::resizeFor(Mode m) {
    int w = 320, h = 200;
    switch (m) {
        case Mode::Low:    w = 320; h = 200; break;   // 16 couleurs
        case Mode::Medium: w = 640; h = 200; break;   // 4 couleurs
        case Mode::High:   w = 640; h = 400; break;   // monochrome
    }
    if (w == curW_ && h == curH_) return;
    curW_ = w; curH_ = h;
    frame_.assign(static_cast<std::size_t>(w) * h, 0xFF000000u);
}

// Verrouille la résolution ET la fréquence de la trame : le décodage ligne à
// ligne s'y tient (un changement de $FF8260 ou $FF820A en cours de trame ne prend
// effet qu'à la suivante, comme l'ancien renderFrame qui figeait la rés. au moment
// du décodage). La géométrie de la trame (cycles/ligne, lignes/trame) découle de
// ce couple — cf. geometry() — et est lue par Machine juste après cet appel.
void Shifter::beginFrame() {
    frameMode_ = mode;
    frameSync_ = sync;
    resizeFor(frameMode_);
    // Réinitialise l'enregistrement spec512 de la trame qui commence. La palette
    // courante devient la base du replay (= état de fin de trame précédente ; les
    // écritures palette de CETTE trame seront rejouées par-dessus, datées).
    colorWrites_.clear();
    paletteAccesses_ = 0;
    spec512Active_   = false;
    frameStartPalette_ = palette;
}

// Décode les index de palette (ou bit mono) d'UNE scanline dans `idx`, selon la
// résolution VERROUILLÉE de la trame. Renvoie le décalage scroll fin STE.
int Shifter::decodeLineIndices(int y, uint8_t* idx) const {
    const int  W      = curW_;
    const bool hi     = (frameMode_ == Mode::High);
    const int  planes = hi ? 1 : (frameMode_ == Mode::Medium ? 2 : 4);  // plans entrelacés
    const int  bpl    = hi ? 80 : 160;                  // octets/ligne AFFICHÉE
    const int  groupB = 2 * planes;                     // octets pour 16 px (1 mot/plan)
    const int  groups = W / 16;                         // groupes de 16 px affichés
    const int  scroll = hwScrollCount;                  // 0 hors STE scrollé ($FF8264/65)

    // Line-offset STE ($FF820F) : le shifter saute `lineWidth` MOTS en fin de ligne
    // → la ligne suivante démarre `bpl + lineWidth*2` octets plus loin (stride). Sur
    // ST/STF lineWidth=0 → stride = bpl (rendu strictement inchangé).
    const uint32_t stride = static_cast<uint32_t>(bpl) + static_cast<uint32_t>(lineWidth) * 2u;
    const uint32_t base   = videoBase + static_cast<uint32_t>(y) * stride;

    // Quand on scrolle, on décode un groupe de 16 px DE PLUS (lu juste après la
    // ligne) pour fournir les `scroll` pixels qui entrent par la droite — modèle
    // prefetch $FF8265 du Shifter STE (cf. Hatari Video_CopyScreenLine*). Le
    // décalage fin par $FF8264 sans prefetch (départ 16 px plus tard, bord gauche
    // à 0) et la dérive de compteur du prefetch relèvent de la cycle-accuracy et
    // ne sont pas distingués ici.
    const int decodeGroups = scroll ? groups + 1 : groups;
    int px = 0;
    for (int g = 0; g < decodeGroups; ++g) {
        const uint32_t a  = base + static_cast<uint32_t>(g) * groupB;
        const uint16_t p0 = bus_.read16(a);
        const uint16_t p1 = planes > 1 ? bus_.read16(a + 2) : 0;
        const uint16_t p2 = planes > 2 ? bus_.read16(a + 4) : 0;
        const uint16_t p3 = planes > 3 ? bus_.read16(a + 6) : 0;
        for (int bit = 15; bit >= 0; --bit)
            idx[px++] = static_cast<uint8_t>(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                           | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
    }
    return scroll;
}

// Décode UNE scanline avec l'état COURANT des registres (palette, base vidéo).
void Shifter::renderLine(int y) {
    if (y < 0 || y >= curH_) return;
    uint32_t* dst = frame_.data() + static_cast<std::size_t>(y) * curW_;
    const int  W   = curW_;
    const bool hi  = (frameMode_ == Mode::High);

    uint8_t idx[660];                                   // max (640/16 + 1) * 16 = 656
    const int scroll = decodeLineIndices(y, idx);

    // Émet W pixels à partir de l'offset `scroll`. En haute résolution = moniteur
    // MONOCHROME : blanc (0) / noir (1), sans la palette couleur (sinon un
    // palette[1] non noir — ex. rouge sous TOS 1.02 — colore l'écran à tort).
    if (hi) {
        for (int c = 0; c < W; ++c)
            dst[c] = (idx[c + scroll] & 1) ? 0xFF000000u : 0xFFFFFFFFu;
    } else {
        for (int c = 0; c < W; ++c)
            dst[c] = stColorToArgb(palette[idx[c + scroll]]);
    }
}

// Fin de trame : re-rendu spec512 (palette intra-ligne) si détecté. Port du
// modèle Hatari spec512.c — au lieu de mémoriser une palette par ligne (figée à
// DE_END), on rejoue les écritures palette datées et on met à jour une palette
// « roulante » au CYCLE où chaque pixel est balayé. Le 68000 ne peut écrire la
// palette qu'une fois tous les 4 cycles (bus 16 bits), donc au plus ~1 changement
// tous les 4 pixels en basse résolution → jusqu'à 512 couleurs à l'écran.
void Shifter::finishFrame() {
    if (!spec512Active_ || frameMode_ == Mode::High) return;   // trame normale/mono : rien

    // Tri stable par cycle : les écritures d'un même move.l / movem partagent un
    // cycle ; l'ordre d'insertion (= ordre des registres) doit être conservé pour
    // que la dernière l'emporte, exactement comme sur le bus.
    std::stable_sort(colorWrites_.begin(), colorWrites_.end(),
                     [](const ColorWrite& a, const ColorWrite& b) {
                         return a.frameCycle < b.frameCycle;
                     });

    const Geometry g   = geometry();
    const int W        = curW_;
    const int cpl      = g.cyclesPerLine;
    const int lineStart= g.lineStartCycle;
    const int span     = g.lineEndCycle - g.lineStartCycle;    // cycles couvrant les W pixels affichés

    // Palette roulante : démarre à l'état de début de trame puis absorbe chaque
    // écriture au cycle voulu, en avançant un curseur unique sur toute la trame
    // (les cycles-pixel sont monotones croissants ligne après ligne).
    std::array<uint16_t, 16> pal = frameStartPalette_;
    const std::size_t n = colorWrites_.size();
    std::size_t cur = 0;

    uint8_t idx[660];
    for (int y = 0; y < curH_; ++y) {
        const int scroll = decodeLineIndices(y, idx);
        uint32_t* dst = frame_.data() + static_cast<std::size_t>(y) * W;
        for (int c = 0; c < W; ++c) {
            // Cycle (dans la trame) où le pixel c de la ligne y sort du shifter
            // (1 cycle/pixel en basse résolution, 0,5 en moyenne). Le décalage
            // kSpec512AlignCyc cale le front couleur sur le front pixel.
            const int64_t pixCyc = static_cast<int64_t>(y) * cpl + lineStart
                                 + static_cast<int64_t>(c) * span / W;
            const int64_t limit = pixCyc - kSpec512AlignCyc;
            while (cur < n && colorWrites_[cur].frameCycle <= limit) {
                pal[colorWrites_[cur].index] = colorWrites_[cur].colour;
                ++cur;
            }
            dst[c] = stColorToArgb(pal[idx[c + scroll]]);
        }
    }
}

// Enregistre l'écriture palette du registre `index` (valeur déjà posée dans
// palette[index]) avec son cycle live dans la trame, pour le re-rendu spec512.
void Shifter::recordColorWrite(int index) {
    if (!liveFrameClock_) return;
    const int64_t fc = liveFrameClock_();
    if (fc < 0) return;                              // hors trame courante
    colorWrites_.push_back({ static_cast<int32_t>(fc), palette[index],
                             static_cast<uint8_t>(index) });
    if (++paletteAccesses_ >= kSpec512Threshold) spec512Active_ = true;
}

// Décode toute la trame d'un coup (repli / appel direct hors ordonnanceur).
void Shifter::renderFrame() {
    beginFrame();
    for (int y = 0; y < curH_; ++y) renderLine(y);
}

uint8_t Shifter::read8(uint32_t addr) {
    // Palette $FF8240-$FF825F : 16 mots, big-endian.
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        const int i = (addr - 0xFF8240) / 2;
        return (addr & 1) ? static_cast<uint8_t>(palette[i])
                          : static_cast<uint8_t>(palette[i] >> 8);
    }
    // Compteur d'adresse vidéo (lecture seule) : position courante du balayage.
    // $FF8205 = bits 16-23, $FF8207 = 8-15, $FF8209 = 0-7 (cf. Hatari
    // Video_ScreenCounter_ReadByte). Certains diagnostics (Test Kit) attendent
    // que ce compteur reflète la base vidéo + l'avance du faisceau.
    // Adresse de base vidéo ($FF8201/03, + octet bas STE $FF820D) : RELISIBLE —
    // le ST renvoie la dernière valeur écrite (Hatari : IoMem_ReadWithoutInterception).
    // Indispensable : les diagnostics RÉCUPÈRENT la base écran en relisant ces
    // registres pour calculer leur framebuffer (sans ça → base 0 → ils dessinent
    // sur la table des vecteurs et plantent).
    if (addr == 0xFF8201) return static_cast<uint8_t>(videoBase >> 16);
    if (addr == 0xFF8203) return static_cast<uint8_t>(videoBase >> 8);
    // Octet bas de la base vidéo $FF820D : STE seulement (sur ST il vaut toujours
    // 0, cf. Hatari Video_BaseLow_ReadByte).
    if (addr == 0xFF820D) return machineIsSte(bus_.machine) ? static_cast<uint8_t>(videoBase) : 0;
    if (addr == 0xFF8205) return static_cast<uint8_t>(videoCounter() >> 16);
    if (addr == 0xFF8207) return static_cast<uint8_t>(videoCounter() >> 8);
    if (addr == 0xFF8209) return static_cast<uint8_t>(videoCounter());
    // Synchro $FF820A : bits inutilisés 2-7 forcés à 1 (ST et STE), cf. Hatari
    // Video_Sync_ReadByte (IoMem[0xff820a] |= 0xfc). On NE masque PAS le champ
    // stocké `sync` : videoCounter() s'en sert toujours via `sync & 2`.
    if (addr == 0xFF820A) return static_cast<uint8_t>((sync & 0x03) | 0xFC);
    // Largeur de ligne STE $FF820F : 0 sur ST (cf. Hatari Video_LineWidth_ReadByte).
    if (addr == 0xFF820F) return machineIsSte(bus_.machine) ? lineWidth : 0;
    if (addr == 0xFF8260) return static_cast<uint8_t>(mode);
    // Scroll fin : Hatari n'expose QUE $FF8265 en lecture (Video_HorScroll_Read).
    if (addr == 0xFF8265) return hwScrollCount;
    // Tout autre registre nouvellement routé mais non géré ($FF8266-$FF827F,
    // etc.) : lecture bénigne (0x00), comme les zones « void » du shifter.
    return 0x00;
}

// Reconstruit l'adresse vidéo courante — port fidèle de Hatari Video_CalculateAddress :
//   addr = videoBase + ligne*bpl + NbBytes, avec NbBytes = ((X - LineStartCycle) >> 1) & ~1
// où X = cycles DANS la ligne et le shifter lit 2 cycles/octet entre LineStartCycle
// (56 en 50 Hz, 52 en 60 Hz ; 0 en haute rés) et LineEndCycle (376). Après la dernière
// ligne affichée, le compteur reste figé jusqu'au rechargement VBL. (L'ancienne version
// supposait 1 octet/cycle depuis le cycle 216 — faux en milieu de ligne, d'où l'échec
// du test « T0 » des diagnostics qui relisent $FF8205/07/09 au cycle près.)
uint32_t Shifter::videoCounter() const {
    if (!beamClock_) return videoBase & 0xFFFFFF;       // pas d'horloge → base brute
    const int64_t fc = beamClock_();                    // cycles dans la trame
    // Géométrie VERROUILLÉE de la trame (cycles/ligne et début DE dépendent de la
    // fréquence 50/60/71 Hz : avant, 512 et 56/52 étaient figés → compteur faux en
    // 60 Hz). frameSync_ est posé par beginFrame, comme frameMode_.
    const Geometry g = geometry();
    const int  kCyclesPerLine = g.cyclesPerLine;
    const bool hi   = (frameMode_ == Mode::High);
    const int  bpl  = hi ? 80 : 160;                    // octets/ligne affichée
    const int  disp = g.displayLines;                   // lignes affichées
    const int  lineStart = g.lineStartCycle;            // début Display-Enable (50/60/71 Hz)
    // Stride réel d'une ligne = octets affichés + line-offset STE ($FF820F, en mots).
    // lineWidth=0 sur ST/STF → stride = bpl (compteur strictement inchangé).
    const int  stride = bpl + static_cast<int>(lineWidth) * 2;
    const int  line = static_cast<int>(fc / kCyclesPerLine);
    uint32_t addr = videoBase;
    if (line >= disp) {
        addr += static_cast<uint32_t>(disp) * stride;   // écran entièrement lu (avant VBL)
    } else {
        const int X = static_cast<int>(fc % kCyclesPerLine);
        int nb = (X - lineStart) >> 1;                  // 2 cycles par octet
        nb &= ~1;                                       // le shifter lit par MOTS
        if (nb < 0) nb = 0; else if (nb > bpl) nb = bpl;
        addr += static_cast<uint32_t>(line) * stride + static_cast<uint32_t>(nb);
    }
    return addr & 0xFFFFFF;
}

void Shifter::write8(uint32_t addr, uint8_t v) {
    // Adresse de base vidéo : octets haut ($FF8201) et milieu ($FF8203). Le bit
    // bas est fixé à 0 (le ST aligne le framebuffer sur 256 octets).
    const bool ste = machineIsSte(bus_.machine);
    switch (addr) {
        case 0xFF8201:
            videoBase = (videoBase & 0x00FF00) | (uint32_t(v) << 16);
            // STE/TT : écrire l'octet haut/milieu remet à 0 l'octet bas $FF820D
            // (cf. Hatari Video_ScreenBase_WriteByte).
            if (ste) videoBase &= 0xFFFF00;
            return;
        case 0xFF8203:
            videoBase = (videoBase & 0xFF0000) | (uint32_t(v) << 8);
            if (ste) videoBase &= 0xFFFF00;
            return;
        case 0xFF820A: sync = v; return;             // synchro 50/60 Hz
        // Octet bas de la base vidéo $FF820D (STE) : bit0 ignoré (aligné pair).
        case 0xFF820D:
            if (ste) videoBase = (videoBase & 0xFFFF00) | (uint32_t(v) & ~1u);
            return;
        // Largeur de ligne STE $FF820F (registre seulement ; câblage rendu différé).
        case 0xFF820F:
            if (ste) lineWidth = v;
            return;
        case 0xFF8260: mode = static_cast<Mode>(v & 0x3); return;
        // Scroll fin horizontal STE — état des registres uniquement (le décalage
        // par pixel relève de la cycle-accuracy, différé). Cf. Hatari
        // Video_HorScroll_Write : $FF8264 sans prefetch, $FF8265 avec prefetch.
        case 0xFF8264:
            if (ste) { hwScrollCount = v & 0x0F; hwScrollPrefetch = false; }
            return;
        case 0xFF8265:
            if (ste) { hwScrollCount = v & 0x0F; hwScrollPrefetch = true; }
            return;
        default: break;
    }
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        const int i = (addr - 0xFF8240) / 2;
        if (addr & 1) palette[i] = (palette[i] & 0xFF00) | v;
        else          palette[i] = static_cast<uint16_t>((palette[i] & 0x00FF) | (uint16_t(v) << 8));
        recordColorWrite(i);   // spec512 : date l'écriture au cycle (palette intra-ligne)
    }
    // Tout autre registre nouvellement routé mais non géré ($FF8266-$FF827F) :
    // écriture sans effet (no-op), comme les zones « void » du shifter.
}
