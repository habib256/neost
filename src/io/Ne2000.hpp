// =============================================================================
//  Ne2000.hpp — Carte réseau NE2000 (DP8390/RTL8019) vue par le port cartouche.
//
//  EtherNEC (Dr. Thomas Redelberger) branche une NE2000 ISA 8 bits sur le port
//  ROM de l'Atari ST. Le port cartouche étant EN LECTURE SEULE et sans ligne A0,
//  les accès sont encodés dans l'ADRESSE — tout est une lecture côté CPU :
//    · LIRE le registre `reg`  : lecture à $FA0000 + reg*512            (/ROM4)
//    · ÉCRIRE `data` dans `reg`: lecture à $FB0000 + reg*512 + data*2   (/ROM3)
//  Source de vérité : BUSENEC.I du pilote de l'auteur du montage (etherne.zip —
//  getBUS lit via rom4=$FA0000, putBUS écrit via rom3=$FB0000). ⚠ Les fenêtres
//  ont été INVERSÉES dans NeoST jusqu'au 2026-08-27 : les selftests passaient
//  (mêmes constantes des deux côtés) mais le vrai ENEC.STX ne pouvait ni lire ni
//  écrire — découvert à la première session STinG réelle. NeoST décode cette
//  fenêtre dans Bus::read8Slow quand une NE2000 est attachée — extension NeoST,
//  cf. docs/EXTENSIONS.md § EtherNEC et docs/HATARI_DIVERGENCES.md § Extensions.
//
//  Le modèle DP8390 est classique : registres en pages 0/1/2, tampon en anneau
//  de réception, DMA distant (Remote DMA) pour transférer trames et registres
//  entre la RAM tampon (16 Ko à $4000-$7FFF de l'espace NIC) et le CPU. La
//  couche physique (envoi/réception réels) passe par NetBackend.
//
//  Intérêt : les pilotes libres STinG (ENEC.STX), MiNTnet (.XIF) et MagiCNet
//  existants tournent SANS modification.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <vector>

#include "core/StateArchive.hpp"

class NetBackend;

class Ne2000 {
public:
    // Bases de décodage EtherNEC dans la fenêtre cartouche ($FA0000 = /ROM4,
    // $FB0000 = /ROM3 — l'ordre des sélections cartouche du ST).
    static constexpr uint32_t READ_BASE  = 0xFA0000;   // /ROM4 : lecture registre
    static constexpr uint32_t WRITE_BASE = 0xFB0000;   // /ROM3 : écriture (fausse lecture)

    void setBackend(NetBackend* b) { backend_ = b; }
    NetBackend* backend() const { return backend_; }
    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

    Ne2000() { loadProm(); }

    // Adresse station de la carte : celle que porte la PROM d'adresse (NIC $0000-
    // $001F, lue par Remote DMA). Comme sur une vraie NE2000, le PILOTE la relit
    // (ne.c : 32 octets à RSAR=0, signature « WW ») puis la recopie lui-même dans
    // les PAR — la puce ne charge PAS les PAR depuis la PROM. Par défaut
    // kDefaultMac ; setMac la change (prise en compte immédiatement par la PROM).
    void setMac(const uint8_t mac[6]);
    const uint8_t* mac() const { return mac_; }
    // Adresse par défaut : LOCALEMENT ADMINISTRÉE (bit 1 du 1er octet), « NST » en
    // ASCII. Aucun OUI de fabricant n'est propre à l'EtherNEC (elle accueille
    // n'importe quelle carte ISA NE2000, dont la PROM porte la MAC du fabricant
    // de cette carte) ni documenté pour le NetUSBee : une adresse inventée ne doit
    // pas usurper un OUI réel. Fixe, pour que les runs restent déterministes.
    static constexpr uint8_t kDefaultMac[6] = {0x02, 0x4E, 0x53, 0x54, 0x00, 0x01};
    static constexpr int     kPromSize = 32;   // octets de PROM visibles (doublés)

    // Décodage d'une lecture dans la fenêtre cartouche ($FA0000-$FBFFFF). Renvoie
    // true et pose `out` si l'adresse est un accès EtherNEC ; false sinon (la
    // lecture retombe sur la ROM cartouche / le comportement par défaut).
    bool cartRead(uint32_t addr, uint8_t& out);

    // Pompe le backend : intègre les trames reçues dans l'anneau (à appeler une
    // fois par trame émulée, comme le poll du modem). Lève l'IRQ si armée.
    void poll();

    // Reset matériel (bouton reset / power-cycle).
    void reset();

    // Ligne d'interruption vers le CPU (EtherNEC : câblée sur… rien de standard —
    // les pilotes ST tournent en POLLING). Exposée pour complétude/tests.
    bool irqAsserted() const { return irq_; }

    void serialize(StateArchive& ar);

private:
    static constexpr int kMemSize   = 0x8000;   // 32 Ko d'espace NIC (16 Ko utiles hauts)
    static constexpr int kRxBufStart = 0x40;    // page de début d'anneau conventionnelle
    static constexpr int kRxBufEnd   = 0x80;    // page de fin (pages de 256 octets)

    NetBackend* backend_ = nullptr;
    bool enabled_ = false;
    bool irq_ = false;

    // Registres DP8390 (les plus utilisés ; indexés par page).
    uint8_t cr_ = 0x21;         // Command (page implicite via bits 6-7)
    uint8_t isr_ = 0, imr_ = 0;
    uint8_t dcr_ = 0, rcr_ = 0, tcr_ = 0;
    uint8_t pstart_ = kRxBufStart, pstop_ = kRxBufEnd, bnry_ = kRxBufStart;
    uint8_t curr_ = kRxBufStart + 1;   // page d'écriture courante (page 1)
    uint8_t tpsr_ = 0;                  // page de départ TX
    uint16_t tbcr_ = 0;                 // compteur d'octets TX
    uint16_t rsar_ = 0, rbcr_ = 0;      // Remote DMA : adresse + compteur
    uint8_t par_[6] = {0};              // adresse MAC (page 1) — posée par le pilote
    uint8_t mac_[6] = {kDefaultMac[0], kDefaultMac[1], kDefaultMac[2],
                       kDefaultMac[3], kDefaultMac[4], kDefaultMac[5]};   // PROM d'adresse
    uint8_t mar_[8] = {0};              // filtre multicast (page 1)

    // RAM tampon de la NIC (anneau RX + tampon TX). Allouée DÈS la construction :
    // vide tant qu'enabled_ était faux, serialize() écrivait un vecteur de taille 0
    // et l'invariant « mem_ taille inattendue » rejetait AU CHARGEMENT tout .state
    // pris en config par défaut (sans EtherNEC) — F7/« load state » échouait toujours.
    std::vector<uint8_t> mem_ = std::vector<uint8_t>(std::size_t(kMemSize), 0);

    void loadProm();                         // PROM d'adresse → mem_[0..31]
    void page0Write(uint8_t reg, uint8_t v);
    uint8_t page0Read(uint8_t reg);
    void page1Write(uint8_t reg, uint8_t v);
    uint8_t page1Read(uint8_t reg);
    void writeReg(uint8_t reg, uint8_t v);   // dispatch selon la page (cr_ bits 6-7)
    uint8_t readReg(uint8_t reg);
    uint8_t remoteDmaReadByte();             // port données $10 en lecture
    void    remoteDmaWriteByte(uint8_t v);   // port données $10 en écriture
    void    transmit();                      // commande TXP → envoi via backend
    void    deliverFrame(const uint8_t* f, int len);  // trame reçue → anneau
    void    setIsr(uint8_t bits);
    void    trace(const char* what, unsigned a, unsigned b) const;
};
