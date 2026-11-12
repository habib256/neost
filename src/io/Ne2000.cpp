// =============================================================================
//  Ne2000.cpp — Carte NE2000 (DP8390) émulée (cf. Ne2000.hpp).
//
//  Port du comportement documenté du DP8390 : Remote DMA (RSAR/RBCR + port
//  données), anneau de réception (en-tête de paquet 4 octets {status, next,
//  lenL, lenH}), transmission (TPSR/TBCR + bit TXP), filtrage MAC/broadcast.
//  Références de comportement : fiches DP8390D (National, juillet 1995) et
//  RTL8019AS (Realtek, LS003.8, 2000-08-20), ne2000.c de QEMU (lu, non copié),
//  pilotes EtherNEC.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Ne2000.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net/NetBackend.hpp"

namespace {
// Command Register (CR)
// CR_STA (0x02), CR_RD0 (0x08) et CR_RD1 (0x10) existent aussi mais ne sont jamais
// testés ici : l'émulation ne distingue que STP, TXP et l'abandon de DMA distante (RD2).
constexpr uint8_t CR_STP = 0x01, CR_TXP = 0x04;
constexpr uint8_t CR_RD2 = 0x20;  // Remote DMA cmd : abort/complete
// Interrupt Status/Mask (ISR/IMR)
constexpr uint8_t ISR_PRX = 0x01, ISR_PTX = 0x02, ISR_OVW = 0x10, ISR_CNT = 0x20,
                  ISR_RDC = 0x40;
// Transmit Status — [B] DP8390D § 10.3 TSR, p. 24.
constexpr uint8_t TSR_PTX = 0x01;
// Receive Status (registre RSR, et octet d'état de l'en-tête de paquet de l'anneau)
// — [B] DP8390D § 10.3 RSR, p. 26.
constexpr uint8_t RSR_PRX = 0x01, RSR_MPA = 0x10, RSR_PHY = 0x20, RSR_DIS = 0x40;
// [F] TSR bits 5 et 1, RSR bit 3 : la fiche RTL8019AS LS003.8 se contredit — texte
// p. 14 « Always 1 », table p. 10 « 0 » (TSR bit 5, RSR bit 3) ou « - ». Gardés à 0,
// comme la table et la DP8390D (p. 24 et 26). Tranché par : une relecture sur la puce.
// Receive Configuration — [B] DP8390D § 10.3 RCR, p. 25.
constexpr uint8_t RCR_AR = 0x02, RCR_AB = 0x04, RCR_AM = 0x08, RCR_PRO = 0x10,
                  RCR_MON = 0x20;
// [B] DP8390D § 11.0, p. 29 (table « Power on reset ») et RTL8019AS LS003.8 DCR,
// p. 13 : LAS « POWER UP = 1 ».
constexpr uint8_t DCR_LAS = 0x04;
// [B] RTL8019AS LS003.8 p. 13-14 : bits « Always 1 » des registres de configuration,
// visibles quand la page 2 les relit (DCR bit 7, TCR bits 7-5, RCR bits 7-6). La
// table p. 11 les marque « - », le texte précise leur valeur.
constexpr uint8_t kDcrOnes = 0x80, kTcrOnes = 0xE0, kRcrOnes = 0xC0;
// Trame minimale du médium : 64 octets FCS compris, soit 60 octets dans le modèle
// (les backends livrent des trames sans FCS). [B] RTL8019AS RCR AR, p. 14 :
// « If AR=0, packets with length fewer than 64 bytes are rejected. »
constexpr int kMinFrameNoFcs = 60;

// --- Page 3 RTL8019AS ([B] LS003.8 § 5.1.1-5.1.2.2, p. 11 et 16-23) -----------------
// 9346CR : EEM1-0 choisissent le mode (p. 17).
constexpr uint8_t EEM_MASK = 0xC0, EEM_AUTOLOAD = 0x40, EEM_CFGWRITE = 0xC0;
// Bits écrivables des CONFIG en mode « Config register write enable » (EEM=11) :
// CONFIG0 VERID (7-6, p. 18), CONFIG1 IRQEN (7, p. 19), CONFIG2 PL1-0 + BSELB (7-5,
// p. 20), CONFIG3 SLEEP + PWRDN (2-1, p. 21). La table p. 11 ne marque pas CONFIG0
// écrivable ; le texte p. 18 le dit explicitement (« can be written in config write
// enable mode … Software uses these differences to identify the chip ») : il prime.
constexpr uint8_t kCfg0W = 0xC0, kCfg1W = 0x80, kCfg2W = 0xE0, kCfg3W = 0x06;
// Contenu de la 9346 (octets 00H-03H = CONFIG1-4, p. 38) et broches de la carte.
// [B] NetUSBee readme.txt (archive 11/2007) : une fois l'EEPROM série programmée, on
// coupe la piste qui tient JP (broche 65) haut — la puce prend alors ses réglages
// dans la 9346 : mode RT jumperless (JP=0, PNP=0, p. 28), CONFIG0.JP lu 0.
// [F] L'image que le programme d'Atari.org écrit dans la 9346 n'est pas connue, ni le
// câblage des broches PNP, AUI et des cavaliers : IRQS/IOS, PL, BS, FUDUP, LEDS, PWRDN
// et IOMS valent 0 ici (média auto, pas de BROM, semi-duplex). Tranché par : un dump
// de la 9346 d'un vrai NetUSBee (page 3 relue sous TOS).
constexpr uint8_t kEeConfig1 = 0x00, kEeConfig2 = 0x00, kEeConfig3 = 0x00,
                  kEeConfig4 = 0x00;
// [B] DP8390D § 10.9 Network Tally Counters, p. 29 : « The maximum count reached by
// any counter is 192 (C0H). »
constexpr uint8_t kTallyMax = 0xC0;
} // namespace

void Ne2000::trace(const char* what, unsigned a, unsigned b) const {
    static const bool on = std::getenv("NEOST_ENEC_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[enec] %s %04x %04x\n", what, a, b);
}

void Ne2000::setMac(const uint8_t mac[6]) {
    std::memcpy(mac_, mac, 6);
    loadProm();
}

// PROM d'adresse station (NE2000) : un octet de PROM 8 bits câblé sur les DEUX
// moitiés du bus de données, d'où chaque octet lu DEUX fois en Remote DMA octet par
// octet. Image de 16 octets : MAC en 0-5, 6-13 à zéro, « WW » ($57 $57) en 14-15
// — la signature que ne.c teste (SA_prom[14] == SA_prom[15] == 0x57) après
// dédoublement. Même disposition que ne2000_reset de QEMU (lu, non copié).
void Ne2000::loadProm() {
    uint8_t prom[kPromSize / 2] = {0};
    std::memcpy(prom, mac_, 6);
    prom[14] = prom[15] = 0x57;
    for (int i = 0; i < kPromSize / 2; ++i)
        mem_[std::size_t(2 * i)] = mem_[std::size_t(2 * i + 1)] = prom[i];
}

void Ne2000::reset() {
    cr_ = CR_STP | CR_RD2;            // stoppée, DMA abort — état post-reset DP8390
    isr_ = 0x80;                     // RST posé
    imr_ = 0; dcr_ = DCR_LAS; rcr_ = 0; tcr_ = 0;
    pstart_ = kRxBufStart; pstop_ = kRxBufEnd;
    bnry_ = kRxBufStart;
    curr_ = kRxBufStart + 1;
    tpsr_ = 0; tbcr_ = 0; rsar_ = 0; rbcr_ = 0;
    std::memset(mar_, 0, sizeof mar_);
    // [B] DP8390D p. 24 et p. 26 : TSR et RSR « not specified until after the first
    // transmission / reception » — [F] 0 ici, aucune source sur la valeur réelle.
    tsr_ = 0; rsr_ = 0;
    std::memset(cntr_, 0, sizeof cntr_);
    // [F] CLDA après reset : ni la DP8390D ni la RTL8019AS ne le disent. 0 ici.
    clda_ = 0;
    irq_ = false;
    if (mem_.size() != std::size_t(kMemSize)) mem_.assign(kMemSize, 0);
    else std::fill(mem_.begin(), mem_.end(), 0);
    loadProm();                      // la PROM survit au reset (elle est en ROM)
}

void Ne2000::powerCycle() {
    reset();
    powerUpConfig();
    if (backend_) backend_->nicReset();
}

// [B] RTL8019AS LS003.8 p. 28-29, « Initial Values of CONFIG1-3 Registers after
// RSTDRV or Auto-load Command », mode RT jumperless : IRQEN = 1, BSELB = 0, PNP = 0,
// SLEEP = 0 ; le reste vient de la 9346. ACTIVEB = 0 : en RT jumperless la carte est
// toujours active à la mise sous tension (p. 28) — et la logique PnP, qui seule
// pourrait la désactiver, est hors d'atteinte (ses ports $279/$A79 ne passent pas
// par les 32 registres de la fenêtre cartouche).
void Ne2000::loadConfig() {
    config1_ = uint8_t(0x80 | (kEeConfig1 & 0x7F));
    config2_ = uint8_t(kEeConfig2 & 0xDF);
    config3_ = uint8_t(kEeConfig3 & 0x72);
}

// Mise sous tension ([B] LS003.8 p. 16, « Page3 Power Up Values ») : 9346CR à 0
// (EEM=00, mode normal), BPAGE à 0, VERID de CONFIG0 à 00 (p. 18), puis chargement
// de la 9346.
void Ne2000::powerUpConfig() {
    cr9346_ = 0;
    bpage_ = 0;
    config0_ = 0;
    loadConfig();
}

void Ne2000::setIsr(uint8_t bits) {
    isr_ |= bits;
    if (isr_ & imr_ & 0x7F) irq_ = true;
}

// -----------------------------------------------------------------------------
//  Décodage de la fenêtre cartouche (EtherNEC)
// -----------------------------------------------------------------------------
// Un accès MOT n'est qu'UN cycle bus /ROM3 ou /ROM4 : la NE2000 voit une seule
// lecture (ou une seule « écriture » encodée dans A1-A8), et la place sur D8-D15.
//   [B] NetUSBee program_spec.txt (archive 11/2007), Network Data Reads : A15=0, la
//       RTL8019 pilote D8-D15 (cité aussi à Bus::read8Slow).
//   [D] MiSTery atarist/atarist_sdram.sv:247 (Till Harbaum, copie de
//       tools/setup_silicon_refs.sh) — sortie EtherNEC câblée
//       « {eth_rd ? eth_data_out : 8'hff, 8'hff} » : octet fort piloté, octet faible
//       à $FF ; ethernec.v ne joue son effet de bord qu'au front de rd/wr.
// L'octet impair d'un mot (`first` faux) rend donc $FF SANS effet de bord. Avant le
// 2026-11-12, chaque octet rejouait l'accès : un move.w au port données consommait
// 2 octets de Remote DMA, un move.w de CR=$26 émettait la trame deux fois. Un accès
// OCTET à une adresse impaire reste un cycle complet (effet de bord), mais sa donnée
// arrive sur D0-D7, que la carte ne pilote pas : $FF.
bool Ne2000::cartRead(uint32_t addr, uint8_t& out, bool first) {
    if (!enabled_) return false;
    const bool rd = addr >= READ_BASE && addr < READ_BASE + 32u * 512u;
    const bool wr = addr >= WRITE_BASE && addr < WRITE_BASE + 32u * 512u;
    if (!rd && !wr) return false;
    out = 0xFF;
    if (!first) return true;                 // 2e moitié d'un mot : même cycle, déjà servi
    if (rd) {
        const uint8_t v = readReg(uint8_t((addr - READ_BASE) / 512));
        if ((addr & 1) == 0) out = v;        // D8-D15 seulement
    } else {
        const uint32_t off = addr - WRITE_BASE;
        writeReg(uint8_t(off / 512), uint8_t((off % 512) / 2));
        // la fausse lecture rend une valeur inutilisée ($FF : rien ne pilote le bus)
    }
    return true;
}

// -----------------------------------------------------------------------------
//  Dispatch registre selon la page (CR bits 6-7)
// -----------------------------------------------------------------------------
uint8_t Ne2000::readReg(uint8_t reg) {
    if (reg == 0x00) return cr_;
    if (reg == 0x10 || reg == 0x11) return remoteDmaReadByte();   // port données
    if (reg == 0x1F) return 0xFF;                                 // reset port (lecture)
    // [B] DP8390D § 10.2, p. 17-18 : PS1/PS0 choisissent la page ; la page 3 est
    // « Reserved » (DP8390D), configuration propre chez la RTL8019AS. Jusqu'au
    // 2026-11-12, les pages 2 et 3 retombaient sur la page 0.
    switch ((cr_ >> 6) & 3) {
    case 0:  return page0Read(reg);
    case 1:  return page1Read(reg);
    case 2:  return page2Read(reg);
    default: return page3Read(reg);        // configuration RTL8019AS
    }
}

void Ne2000::writeReg(uint8_t reg, uint8_t v) {
    if (reg == 0x00) {
        cr_ = v;
        if (v & CR_TXP) transmit();
        // Remote Read/Write DMA démarre dès que RD0/RD1 est armé — on lit/écrit
        // ensuite octet par octet via le port données ($10).
        return;
    }
    if (reg == 0x10 || reg == 0x11) { remoteDmaWriteByte(v); return; }
    if (reg == 0x1F) { reset(); return; }                        // reset port (écriture)
    switch ((cr_ >> 6) & 3) {
    case 0: page0Write(reg, v); break;
    case 1: page1Write(reg, v); break;
    // [B] RTL8019AS LS003.8 § 5.1.1, p. 9 et 11 : la page 2 est en LECTURE seule
    // (colonne [R] seule) ; les Remote/Local Next Packet Pointers et l'Address
    // Counter que la DP8390D y laisse écrire « for diagnostic purposes » (§ 10.2,
    // p. 18) n'existent pas sur la RTL8019AS ($03, $05-$0B « reserved »). L'écriture
    // est ignorée — et ne touche plus la page 0 (CR=$A2 puis reg 3 ← $77 écrivait
    // BNRY jusqu'au 2026-11-12).
    case 2: break;
    default: page3Write(reg, v); break;
    }
}

// --- Page 0 ------------------------------------------------------------------
// [B] DP8390D § 10.2 Register Address Assignments, page 0, p. 17 : en LECTURE, la
// page 0 expose des registres d'état distincts des registres de configuration
// écrits à la même adresse. Le code rendait TBCR0 pour TSR (60 octets → $3C, lu
// COL|ABT|CRS|FU sans PTX) et RCR/TCR/DCR/IMR pour RSR/CNTR0-2.
uint8_t Ne2000::page0Read(uint8_t reg) {
    switch (reg) {
    // CLDA0/1 : [B] RTL8019AS p. 15 « These two registers can be read to get the
    // current local DMA address » ; tenu à jour en fin d'émission et de réception.
    case 0x01: return uint8_t(clda_);
    case 0x02: return uint8_t(clda_ >> 8);
    case 0x03: return bnry_;
    case 0x04: return tsr_;                    // TSR, p. 24
    case 0x05: return 0;                       // NCR : aucune collision (pas de média partagé)
    case 0x06: return 0;                       // [F] FIFO : contenu après bouclage non modélisé
    case 0x07: return isr_;
    case 0x08: return uint8_t(rsar_);          // CRDA0 : adresse COURANTE du Remote DMA
    case 0x09: return uint8_t(rsar_ >> 8);     // CRDA1
    // [B] RTL8019AS LS003.8 p. 10 et 16 : 8019ID0 = $50 (« P »), 8019ID1 = $70 (« p »).
    // « Reserved » sur la DP8390D ; c'est par eux qu'un pilote reconnaît la Realtek.
    case 0x0A: return kRtlId0;
    case 0x0B: return kRtlId1;
    case 0x0C: return uint8_t(rsr_ | ((rcr_ & RCR_MON) ? RSR_DIS : 0));   // RSR, p. 26
    // Tally counters, p. 29 : « These registers will be cleared when read by the CPU. »
    case 0x0D: case 0x0E: case 0x0F: {
        const uint8_t c = cntr_[reg - 0x0D];
        cntr_[reg - 0x0D] = 0;
        return c;
    }
    default:   return 0;
    }
}

// [B] RTL8019AS LS003.8 § 5.1.1, p. 9 et 11 (et DP8390D § 10.2, p. 18) : relecture
// des registres de configuration (PSTART, PSTOP, TPSR, RCR, TCR, DCR, IMR). Les bits
// « Always 1 » de RCR/TCR/DCR (p. 13-14) se lisent à 1.
uint8_t Ne2000::page2Read(uint8_t reg) {
    switch (reg) {
    case 0x01: return pstart_;
    case 0x02: return pstop_;
    case 0x04: return tpsr_;
    case 0x0C: return uint8_t(rcr_ | kRcrOnes);
    case 0x0D: return uint8_t(tcr_ | kTcrOnes);
    case 0x0E: return uint8_t(dcr_ | kDcrOnes);
    case 0x0F: return imr_;
    // $03, $05-$0B : « reserved » sur la RTL8019AS (p. 11) — les pointeurs internes
    // de la DP8390D (RNPP, LNPP, Address Counter) n'y sont pas. [F] Valeur lue d'un
    // registre réservé : inconnue, 0 ici. Tranché par : une relecture sur la puce.
    default:   return 0;
    }
}

// --- Page 3 (RTL8019AS) --------------------------------------------------------
// [B] RTL8019AS LS003.8 § 5.1.1 table page 3, p. 11, et § 5.1.2.2, p. 16-23.
uint8_t Ne2000::page3Read(uint8_t reg) {
    switch (reg) {
    // 9346CR, p. 17 : EEM1-0 et les broches EECS/EESK/EEDI relus ; bits 5-4 « Not
    // used ». [F] EEDO (bit 0) : la 93C46 du NetUSBee n'est pas modélisée, lue 0 ;
    // de même la valeur des bits 3-1 hors modes auto-load/programmation (« * »).
    case 0x01: return uint8_t(cr9346_ & 0xCE);
    case 0x02: return bpage_;                          // BPAGE, p. 17
    // CONFIG0, p. 18 : VERID (7-6) ; AUI = 0 (UTP) ; PNPJP [F] 0 ; JP = 0 (piste
    // coupée, cf. kEeConfig*) ; BNC = 0 : lien UTP toujours bon dans le modèle (table
    // p. 18, « Auto detect / Link OK ») ; bits 1-0 « Always 0s ».
    case 0x03: return uint8_t(config0_ & 0xC0);
    case 0x04: return config1_;                        // CONFIG1, p. 19
    case 0x05: return config2_;                        // CONFIG2, p. 20
    case 0x06: return config3_;                        // CONFIG3, p. 21-22
    // CSNSAV, p. 22 : copie du CSN que la logique PnP assigne — elle est hors
    // d'atteinte par la fenêtre cartouche, le CSN reste à sa valeur de mise sous
    // tension, 0 (p. 16).
    case 0x08: return 0;
    // INTR, p. 22 : « reflects the ISA bus states of INT7-0 pins ». Avec IRQEN, la
    // ligne choisie par IRQS2-0 est haute pendant une requête (CONFIG1, p. 19).
    // [F] Les lignes non choisies sont en haute impédance : lues 0 ici.
    case 0x0B:
        return (irq_ && (config1_ & 0x80)) ? uint8_t(1u << ((config1_ >> 4) & 7)) : 0;
    case 0x0D: return uint8_t(kEeConfig4 & 0x01);     // CONFIG4, p. 22 : IOMS
    // TEST ($07, « Reserved, Do not write »), HLTCLK et FMWP (écriture seule), $0A,
    // $0E-$0F réservés. [F] Valeur lue : inconnue, 0 ici.
    default:   return 0;
    }
}

void Ne2000::page3Write(uint8_t reg, uint8_t v) {
    const bool cfgWrite = (cr9346_ & EEM_MASK) == EEM_CFGWRITE;   // « W* », p. 11
    switch (reg) {
    case 0x01:
        // 9346CR, p. 17 : bit 0 (EEDO) en lecture seule, bits 5-4 inutilisés.
        cr9346_ = uint8_t(v & 0xCE);
        // EEM=01 « Auto-load » : la puce recharge la 9346 comme sous RSTDRV, revient
        // d'elle-même en mode normal (EEM=00) et CR repart à $21. La configuration
        // temporaire est perdue (p. 28). [F] Instantané ici ; la fiche dit « about
        // 2ms » — un pilote qui sonde EEM jusqu'au retour à 00 sort aussitôt.
        if ((cr9346_ & EEM_MASK) == EEM_AUTOLOAD) {
            loadConfig();
            cr9346_ = 0;
            cr_ = CR_STP | CR_RD2;          // $21
        }
        break;
    case 0x02: bpage_ = v; break;                      // BPAGE, R/W
    case 0x03: if (cfgWrite) config0_ = uint8_t((config0_ & ~kCfg0W) | (v & kCfg0W)); break;
    case 0x04: if (cfgWrite) config1_ = uint8_t((config1_ & ~kCfg1W) | (v & kCfg1W)); break;
    case 0x05: if (cfgWrite) config2_ = uint8_t((config2_ & ~kCfg2W) | (v & kCfg2W)); break;
    case 0x06: if (cfgWrite) config3_ = uint8_t((config3_ & ~kCfg3W) | (v & kCfg3W)); break;
    // HLTCLK ($09, p. 22) : « R » ou « H » choisissent le mode de mise en veille,
    // seulement sous PWRDN. [F] Le mode 2 (horloge arrêtée) rend les registres
    // « typically not accessible » : non modélisé, l'écriture est sans effet.
    // FMWP ($0C, p. 22-23) : déverrouille l'écriture d'une Flash de BROM ; ni
    // l'EtherNEC ni le NetUSBee n'en portent, sans effet. TEST ($07) : « Do not
    // write ». Le reste est en lecture seule ou réservé.
    default: break;
    }
}

void Ne2000::page0Write(uint8_t reg, uint8_t v) {
    switch (reg) {
    case 0x01: pstart_ = v; break;
    case 0x02: pstop_  = v; break;
    case 0x03: bnry_   = v; break;
    case 0x04: tpsr_   = v; break;
    case 0x05: tbcr_ = (tbcr_ & 0xFF00) | v; break;
    case 0x06: tbcr_ = (tbcr_ & 0x00FF) | (uint16_t(v) << 8); break;
    case 0x07: isr_ &= ~v; if (!(isr_ & imr_ & 0x7F)) irq_ = false; break;  // W1C
    case 0x08: rsar_ = (rsar_ & 0xFF00) | v; break;
    case 0x09: rsar_ = (rsar_ & 0x00FF) | (uint16_t(v) << 8); break;
    case 0x0A: rbcr_ = (rbcr_ & 0xFF00) | v; break;
    case 0x0B: rbcr_ = (rbcr_ & 0x00FF) | (uint16_t(v) << 8); break;
    case 0x0C: rcr_ = v; break;
    case 0x0D: tcr_ = v; break;
    case 0x0E: dcr_ = v; break;
    case 0x0F: imr_ = v; if (isr_ & imr_ & 0x7F) irq_ = true; break;
    default: break;
    }
}

// --- Page 1 ------------------------------------------------------------------
uint8_t Ne2000::page1Read(uint8_t reg) {
    if (reg >= 0x01 && reg <= 0x06) return par_[reg - 1];
    if (reg == 0x07) return curr_;
    if (reg >= 0x08 && reg <= 0x0F) return mar_[reg - 8];
    return 0;
}

void Ne2000::page1Write(uint8_t reg, uint8_t v) {
    if (reg >= 0x01 && reg <= 0x06) { par_[reg - 1] = v; return; }
    if (reg == 0x07) { curr_ = v; return; }
    if (reg >= 0x08 && reg <= 0x0F) { mar_[reg - 8] = v; return; }
}

// -----------------------------------------------------------------------------
//  Remote DMA (port données $10) : lecture/écriture de la RAM tampon NIC.
//  RSAR = adresse courante, RBCR = octets restants. RDC posé quand RBCR atteint 0.
// -----------------------------------------------------------------------------
// [B] DP8390D/NS32490D (National Semiconductor, juillet 1995) § 9.0 Remote DMA, p. 15
//     (DP8390D.pdf, sha256 14476839…) : « If the DMA pointer crosses the Page Stop
//     Register, it is reset to the Page Start Address. This allows the Remote DMA to
//     remove packets that have wrapped around to the top of the Receive Buffer Ring. »
//     Un paquet à cheval sur PSTOP se lit donc d'un seul Remote Read. La fiche ne
//     distingue pas lecture et écriture : le pointeur est le même ([D] QEMU ne2000.c
//     ne2000_dma_update enroule aussi les deux). Rien n'enroule si l'anneau est vide
//     ou inversé (PSTOP ≤ PSTART : registres pas encore programmés).
void Ne2000::advanceRemoteDma() {
    rsar_++;
    if (pstop_ > pstart_ && rsar_ == uint16_t(pstop_ << 8)) rsar_ = uint16_t(pstart_ << 8);
}

// [B] RTL8019AS LS003.8 9346CR, p. 17 : en mode « 9346 programming » (EEM=10),
// « both the local & remote DMA operation of 8390 are disabled ». [F] La donnée lue
// au port pendant ce mode n'est pas documentée : 0, sans avance du pointeur.
uint8_t Ne2000::remoteDmaReadByte() {
    if (rbcr_ == 0 || localDmaOff()) return 0;
    uint8_t v = 0;
    if (rsar_ < mem_.size()) v = mem_[rsar_];
    advanceRemoteDma();
    if (--rbcr_ == 0) setIsr(ISR_RDC);
    return v;
}

void Ne2000::remoteDmaWriteByte(uint8_t v) {
    if (rbcr_ == 0 || localDmaOff()) return;
    // PROM d'adresse : en LECTURE SEULE sur la carte — une écriture Remote DMA y
    // est sans effet (le compteur avance quand même).
    if (rsar_ >= kPromSize && rsar_ < mem_.size()) mem_[rsar_] = v;
    advanceRemoteDma();
    if (--rbcr_ == 0) setIsr(ISR_RDC);
}

// -----------------------------------------------------------------------------
//  Transmission : la trame est dans mem_ à TPSR*256, longueur TBCR.
// -----------------------------------------------------------------------------
void Ne2000::transmit() {
    // [B] DP8390D § 10.3 CR, p. 19 : STP « takes the controller offline, no packets
    // will be received or transmitted » ; « Before the transmit command is given,
    // the STA bit must be set and the STP bit reset. » TXP sous STP n'émet rien.
    if (cr_ & CR_STP) { cr_ &= ~CR_TXP; return; }
    // [B] DP8390D § 10.3 TSR, p. 24 : « It is cleared when the next transmission is
    // initiated by the host. »
    tsr_ = 0;
    const uint32_t addr = uint32_t(tpsr_) * 256u;
    int len = tbcr_;
    if (len < 0 || addr + uint32_t(len) > mem_.size()) { cr_ &= ~CR_TXP; return; }
    // [B] RTL8019AS LS003.8 CONFIG3 PWRDN, p. 22 : en veille, « the RTL8019AS's
    // serial network interface and transceiver are turned off. All network
    // activities are ignored » — la trame ne part pas sur le câble. [F] Ce que le
    // cœur 8390 signale alors (PTX, CRS) n'est pas écrit : statut normal ici.
    if (backend_ && len > 0 && !poweredDown()) backend_->send(&mem_[addr], len);
    trace("tx", addr, unsigned(len));
    // La DMA locale a lu la trame de TPSR·256 à TPSR·256 + TBCR ([B] DP8390D § 10.5,
    // p. 27 : A7-A0 partent de 0). [F] Exact au préchargement de la FIFO près.
    clda_ = uint16_t(addr + uint32_t(len));
    cr_ &= ~CR_TXP;                  // TXP retombe : transmission « instantanée »
    tsr_ = TSR_PTX;                  // ni collision ni sous-débit : PTX seul
    setIsr(ISR_PTX);                 // Packet Transmitted
}

// [B] DP8390D § 10.9 MAR, p. 29 : l'adresse destination traverse la logique CRC ;
// les 6 bits de poids fort du CRC, une fois le dernier bit d'adresse entré,
// désignent un bit FB0-FB63 (MAR0 bits 0-7 = FB0-FB7 … MAR7 = FB56-FB63).
// CRC Ethernet (polynôme $04C11DB7, registre à 1, bits de chaque octet pris LSB
// d'abord, sans inversion finale) — [D] même calcul que QEMU ne2000.c
// (compute_mcast_idx / net_crc32) et Linux 8390.c (make_mc_bits / ether_crc).
bool Ne2000::multicastHit(const uint8_t* dst) const {
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < 6; ++i) {
        uint8_t b = dst[i];
        for (int k = 0; k < 8; ++k, b >>= 1) {
            const uint32_t carry = ((crc >> 31) ^ b) & 1u;
            crc <<= 1;
            if (carry) crc ^= 0x04C11DB7u;
        }
    }
    const unsigned idx = crc >> 26;
    return ((mar_[idx >> 3] >> (idx & 7)) & 1u) != 0;
}

// [B] DP8390D § 10.9, p. 29 : CNTR2 compte les paquets perdus faute de tampon (et,
// en mode moniteur, ceux qui passent le filtre d'adresse) ; plafond 192 ($C0).
// [B] § 10.3 ISR, p. 20 : CNT « Set when MSB of one or more of the Network Tally
// Counters has been set ».
void Ne2000::countMissed() {
    if (cntr_[2] < kTallyMax) ++cntr_[2];
    if (cntr_[2] & 0x80) setIsr(ISR_CNT);
}

// -----------------------------------------------------------------------------
//  Réception : trame → anneau, précédée d'un en-tête de 4 octets.
// -----------------------------------------------------------------------------
void Ne2000::deliverFrame(const uint8_t* f, int len) {
    if (len < 14) return;                                // trop court pour Ethernet
    // [B] DP8390D § 10.3 CR, p. 19 : carte stoppée (STP, état de mise sous tension)
    // → « no packets will be received ». Avant le 2026-11-12, l'anneau se remplissait
    // même carte arrêtée.
    if (cr_ & CR_STP) { trace("rx-stopped", unsigned(len), cr_); return; }
    // [B] RTL8019AS LS003.8 : sous PWRDN, émetteur-récepteur coupé (CONFIG3, p. 22) ;
    // en mode « 9346 programming », DMA locale coupée (9346CR, p. 17). Rien n'entre.
    if (poweredDown() || localDmaOff()) { trace("rx-off", unsigned(len), config3_); return; }

    // [B] DP8390D § 10.3 RCR, p. 25 — filtre d'adresse :
    //   · broadcast (destination tout à 1) : accepté si AB ;
    //   · multicast (bit de groupe, hors broadcast) : si AM ET si le hachage CRC
    //     tombe sur un bit posé des MAR (§ 10.9, p. 29) ;
    //   · physique : si PRO, ou égale aux PAR0-5.
    // Le code acceptait tout broadcast et tout multicast, AB/AM ignorés.
    // SEP (paquets erronés acceptés) n'a pas de cas : un backend ne livre jamais de
    // trame à CRC faux ou mal alignée. AR : cf. plus bas.
    const bool group = (f[0] & 0x01) != 0;
    const bool bcast = f[0] == 0xFF && f[1] == 0xFF && f[2] == 0xFF
                    && f[3] == 0xFF && f[4] == 0xFF && f[5] == 0xFF;
    bool accept;
    if (bcast)      accept = (rcr_ & RCR_AB) != 0;
    else if (group) accept = (rcr_ & RCR_AM) != 0 && multicastHit(f);
    else            accept = (rcr_ & RCR_PRO) != 0 || std::memcmp(f, par_, 6) == 0;
    if (!accept) {
        // Rejet du filtre MAC : tracé — une trame qui disparaît ICI est invisible
        // du pilote ET de l'anneau (chasse CAB/STinG du 2026-08-27).
        trace("rx-macdrop", unsigned(f[0]) << 8 | f[1], unsigned(par_[0]) << 8 | par_[1]);
        return;
    }
    const uint8_t phy = group ? RSR_PHY : 0;
    // [B] RTL8019AS LS003.8 RCR AR, p. 14 : une trame de moins de 64 octets (FCS
    // compris, donc < 60 ici) est rejetée sauf AR=1. [B] DP8390D § 8.0, p. 11 : c'est
    // l'émetteur qui bourre à 64 octets — le NAT le fait (NatPacket.cpp
    // buildEthernet), STinG et MiNTnet aussi (ENESTNG.C, ENEMNET.S) ; seul un bouclage
    // d'une trame émise trop courte en livre une. Jusqu'au 2026-11-12, tout runt
    // entrait dans l'anneau. Rejet : [B] DP8390D § 7.0, p. 10 « the DMA is restored
    // by the NIC by reprogramming the DMA starting address pointed to by the Current
    // Page Register » — d'où CLDA = CURR·256. [F] Ni RSR ni compteur ne bougent.
    if (len < kMinFrameNoFcs && !(rcr_ & RCR_AR)) {
        if (!(rcr_ & RCR_MON)) clda_ = uint16_t(curr_ << 8);
        trace("rx-runt", unsigned(len), rcr_);
        return;
    }
    // [B] DP8390D § 10.3 RCR MON, p. 25 : « checked for address match … but not
    // buffered to memory. The Missed Packet Tally counter will be incremented for
    // each recognized packet. » RSR : MPA, p. 26.
    if (rcr_ & RCR_MON) {
        rsr_ = uint8_t(RSR_MPA | phy);
        countMissed();
        trace("rx-monitor", unsigned(len), rcr_);
        return;
    }

    // Longueur avec en-tête, arrondie à la page (256 octets).
    const int total = len + 4;
    const int pages = (total + 255) / 256;

    // Place d'écriture : page CURR. Vérifie que la trame ne rattrape NI ne
    // franchit BNRY : le DP8390 compare à chaque frontière de page pendant la
    // réception — un test d'égalité seul laissait une trame multi-pages
    // enjamber BNRY et écraser des paquets non lus. Distance libre en ordre
    // d'anneau (CURR == BNRY = anneau vide, comme ne2000.c de QEMU).
    int start = curr_;
    const int ringPages = pstop_ - pstart_;
    if (ringPages <= 0) return;
    int freePages = (bnry_ - start + ringPages) % ringPages;
    if (freePages == 0) freePages = ringPages;
    if (pages >= freePages) {                            // toucherait/franchirait BNRY
        trace("rx-overflow", unsigned(start), unsigned(bnry_));
        setIsr(ISR_OVW);                                 // Overwrite Warning : le pilote est prévenu
        rsr_ = uint8_t(RSR_MPA | phy);                   // [B] p. 26 : « lack of receive buffers »
        countMissed();
        return;
    }
    int next = start + pages;
    if (next >= pstop_) next = pstart_ + (next - pstop_);

    // [B] DP8390D p. 26 : le RSR est recopié en tête de paquet ; PHY dit si
    // l'adresse reconnue était multicast/broadcast.
    rsr_ = uint8_t(RSR_PRX | phy);
    uint8_t hdr[4] = { rsr_, uint8_t(next),
                       uint8_t(total & 0xFF), uint8_t((total >> 8) & 0xFF) };

    // Écriture circulaire dans l'anneau (pages [pstart_, pstop_)).
    uint32_t p = uint32_t(start) * 256u;
    const uint32_t ringLo = uint32_t(pstart_) * 256u;
    const uint32_t ringHi = uint32_t(pstop_) * 256u;
    auto put = [&](uint8_t b) {
        if (p < mem_.size()) mem_[p] = b;
        if (++p >= ringHi) p = ringLo;                   // enroulement
    };
    for (int i = 0; i < 4; ++i) put(hdr[i]);
    for (int i = 0; i < len; ++i) put(f[i]);
    // Le reste de la dernière page reste tel quel (zéro-rempli au reset).

    // [B] DP8390D § 7.0, p. 10, « Successful reception » : la DMA est ramenée au
    // premier tampon du paquet (CURR) pour y écrire les 4 octets d'en-tête — c'est
    // son dernier accès. CLDA = ancien CURR·256 + 4 (déduit de ce texte).
    clda_ = uint16_t((start << 8) + 4);
    curr_ = uint8_t(next);
    setIsr(ISR_PRX);                                     // Packet Received
    trace("rx", unsigned(len), curr_);
}

void Ne2000::poll() {
    if (!enabled_ || !backend_) return;
    backend_->poll();
    std::vector<uint8_t> frame;
    int guard = 0;
    while (guard++ < 256 && backend_->recv(frame))
        deliverFrame(frame.data(), int(frame.size()));
}

// -----------------------------------------------------------------------------
//  Save-state (état RUNTIME ; le backend et le contenu réseau sont hors snapshot).
// -----------------------------------------------------------------------------
void Ne2000::serialize(StateArchive& ar) {
    ar(enabled_); ar(irq_);
    ar(cr_); ar(isr_); ar(imr_); ar(dcr_); ar(rcr_); ar(tcr_);
    ar(pstart_); ar(pstop_); ar(bnry_); ar(curr_); ar(tpsr_);
    ar(tbcr_); ar(rsar_); ar(rbcr_);
    ar.arr(par_); ar.arr(mar_);
    ar(tsr_); ar(rsr_); ar.arr(cntr_);       // registres d'état (2026-11-12)
    // RTL8019AS selon sa fiche (2026-11-12) : CLDA et page 3.
    ar(clda_); ar(cr9346_); ar(bpage_);
    ar(config0_); ar(config1_); ar(config2_); ar(config3_);
    if (ar.loading() && mem_.size() != std::size_t(kMemSize))
        mem_.assign(kMemSize, 0);
    ar.vec(mem_);
    // Compat : un .state v10 pris SANS EtherNEC (bug d'époque : mem_ n'était allouée
    // qu'au premier reset() de la carte) porte un vecteur de taille 0 — on ré-alloue
    // au lieu de rejeter, la NIC désactivée n'ayant aucun contenu à restaurer.
    if (ar.loading() && mem_.empty()) mem_.assign(kMemSize, 0);
    ar.check(mem_.size() == std::size_t(kMemSize), "Ne2000::mem_ taille inattendue");
    // Bornes de l'anneau : pstart_/pstop_/bnry_/curr_ indexent mem_ par *256 —
    // des valeurs forgées feraient déborder deliverFrame/remoteDma.
    // Pas de garde « pstart_ < pstop_ » : PSTART/PSTOP sont écrits librement par le
    // programme (PSTOP = 0 compris) et chaque usage de l'anneau re-borne déjà par
    // mem_.size() — la garde refusait donc des états légitimes.
}
