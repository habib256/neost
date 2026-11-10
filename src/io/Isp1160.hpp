// =============================================================================
//  Isp1160.hpp — Contrôleur hôte USB Philips ISP1160 du NetUSBee (port cartouche).
//
//  Le NetUSBee (hardware.atari.org/netusbee) = une RTL8019AS (NE2000, câblée
//  EXACTEMENT comme l'EtherNEC — pilotes rétro-compatibles, cf. io/Ne2000) + un
//  ISP1160 (hôte USB 1.1, deux ports) sur le même port cartouche. Le port étant
//  en LECTURE SEULE, 16 bits de large et sans A0, le pilote encode tout dans
//  l'ADRESSE (FreeMiNT `sys/usb/src.km/ucd/netusbee/isp116x.h`) :
//
//      $FA0000 + (b << 1)   LSB_WRITE      : verrouille l'octet b (latch bas)
//      $FB8000 + (b << 1)   MSB_DATA_WRITE : écrit le MOT (b << 8) | latch dans le port DONNÉES
//      $FBC000              MSB_CMD_WRITE  : écrit le MOT latch dans le port COMMANDE (index)
//      $FA8000              DATA_READ      : lit un mot 16 bits du port DONNÉES
//
//  Un registre 16 bits = 1 mot, un registre 32 bits = 2 mots (poids faible d'abord).
//  Le pilote envoie l'index (| $80 pour écrire) sur le port commande puis les mots
//  sur le port données — modèle ISP116x classique (Linux isp116x-hcd).
//
//  [B] NetUSBee program_spec.txt (archive 11/2007) : A15 sépare Ethernet et USB.
//  [C] FreeMiNT 098f002a, isp116x.h : latch LSB en $FA0000. Cet accès lit
//  aussi le CR Ethernet, SANS le modifier (Ne2000::READ_BASE = $FA0000).
//  Le texte de conception de 2006 décrit un autre latch ($FA8000) : la carte
//  réellement attendue par le pilote prime ici ; une capture reste à obtenir.
//
//  Deux ports USB : clavier/souris HID boot ou disque SCSI/Bulk-Only sur image.
//  Aucun passthrough USB hôte ; ITL/isochrone non exécuté. Cf. docs/NETUSBEE.md.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include "io/UsbDevice.hpp"

#include "core/StateArchive.hpp"

class Isp1160 {
public:
    // Fenêtres de décodage dans l'espace cartouche (isp116x.h).
    static constexpr uint32_t LSB_WRITE      = 0xFA0000;   // + (b<<1), 512 octets
    static constexpr uint32_t DATA_READ      = 0xFA8000;   // mot 16 bits
    static constexpr uint32_t MSB_DATA_WRITE = 0xFB8000;   // + (b<<1), 512 octets
    static constexpr uint32_t MSB_CMD_WRITE  = 0xFBC000;   // mot, sans décalage
    static constexpr uint16_t CHIP_ID        = 0x6120;     // ISP1160 (masque $FF00 = $6100)

    // Index de registre (isp116x.h) — exposés pour les auto-tests.
    enum Reg : uint8_t {
        HCREVISION = 0x00, HCCONTROL = 0x01, HCCMDSTAT = 0x02, HCINTSTAT = 0x03,
        HCINTENB = 0x04, HCINTDIS = 0x05, HCFMINTVL = 0x0D, HCFMREM = 0x0E,
        HCFMNUM = 0x0F, HCLSTHRESH = 0x11, HCRHDESCA = 0x12, HCRHDESCB = 0x13,
        HCRHSTATUS = 0x14, HCRHPORT1 = 0x15, HCRHPORT2 = 0x16, HCHWCFG = 0x20,
        HCDMACFG = 0x21, HCXFERCTR = 0x22, HCuPINT = 0x24, HCuPINTENB = 0x25,
        HCCHIPID = 0x27, HCSCRATCH = 0x28, HCSWRES = 0x29, HCITLBUFLEN = 0x2A,
        HCATLBUFLEN = 0x2B, HCBUFSTAT = 0x2C, HCRDITL0LEN = 0x2D, HCRDITL1LEN = 0x2E,
        HCITLPORT = 0x40, HCATLPORT = 0x41,
    };

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    void setClock(std::function<int64_t()> clock) { clock_ = std::move(clock); }
    void sync();                          // horloge USB 1 ms, indépendante de la vidéo
    bool attach(unsigned port, UsbDevice::Kind kind, const std::string& image = {}, bool readOnly = false);
    const UsbDevice& device(unsigned port) const { return devices_[port]; }
    void keyEvent(uint8_t usage, bool down);
    void mouseEvent(int dx, int dy, bool left, bool right);

    // Décodage d'une lecture dans la fenêtre cartouche. `first` = premier octet
    // d'un accès CPU (octet pair d'un mot, ou accès octet) : les effets de bord
    // (latch, écriture, avance de FIFO) ne jouent qu'UNE fois par accès, l'octet
    // impair rendant la moitié basse du mot lu. Renvoie true si l'adresse est
    // un accès ISP1160 (le NE2000 peut la voir AUSSI, cf. en-tête).
    bool cartRead(uint32_t addr, uint8_t& out, bool first);

    // Une trame USB (1 ms) : reset des ports, transferts ATL et interruptions.
    void poll();

    void reset();              // power-cycle (reset matériel de la carte)
    bool irqAsserted() const { return irq_; }

    void serialize(StateArchive& ar);

private:
    static constexpr int kBufSize = 4096;   // RAM interne ISP1160 (ITL+ATL)

    bool enabled_ = false;
    bool irq_ = false;

    // Couche bus : latch bas, index courant, sens, compteur de mots.
    uint8_t  latch_ = 0;
    uint8_t  cmd_ = 0;            // index | $80 = écriture
    bool     cmdIsWrite_ = false;
    int      wordIdx_ = 0;        // 0 = mot bas, 1 = mot haut (registres 32 bits)
    uint16_t readLo_ = 0;         // moitié basse du dernier mot lu (octet impair)
    uint32_t wdata_ = 0;          // mot bas en attente (écriture 32 bits)

    // Registres OHCI / ISP116x.
    uint32_t control_ = 0, cmdstat_ = 0, intstat_ = 0, intenb_ = 0;
    uint32_t fmintvl_ = 0x2EDF, fmrem_ = 0, fmnum_ = 0, lsthresh_ = 0x0628;
    uint32_t rhdesca_ = 0x02, rhdescb_ = 0, rhstatus_ = 0;
    uint32_t rhport_[2] = {0, 0};
    uint16_t hwcfg_ = 0, dmacfg_ = 0, xferctr_ = 0, upint_ = 0, upintenb_ = 0;
    uint16_t scratch_ = 0, itlbuflen_ = 0, atlbuflen_ = 0, bufstat_ = 0;

    // FIFO ATL (liste de transferts asynchrones) : ce que le pilote écrit, le
    // contrôleur le « traite » (poll) et le pilote le relit, PTD par PTD.
    std::vector<uint8_t> atl_ = std::vector<uint8_t>(std::size_t(kBufSize), 0);
    int atlWr_ = 0, atlRd_ = 0;
    bool atlPending_ = false;
    uint16_t fifoRemaining_ = 0;
    uint8_t portResetMs_[2] = {};
    UsbDevice devices_[2];
    std::function<int64_t()> clock_;
    int64_t lastCycle_ = 0;
    uint32_t framePhase_ = 0;              // cycles ×1000, reste modulo kCpuHzInt

    uint32_t readReg(uint8_t idx, bool is32);
    void     writeReg(uint8_t idx, uint32_t v, bool is32);
    static bool is32(uint8_t idx) { return idx < 0x20; }   // OHCI (< $20) = 32 bits, ISP = 16
    uint16_t dataPortRead();
    void     dataPortWrite(uint16_t w);
    void     softReset();
    void     updateIrq();
    void     finishFifo(bool writing);
    void     portPower(unsigned port, bool on);
    void     trace(const char* what, unsigned a, unsigned b) const;
};
