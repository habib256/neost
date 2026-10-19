// =============================================================================
//  Acsi.hpp — Contrôleur de disque dur ACSI (port de Hatari hdc.c).
//
//  L'ACSI (Atari Computer System Interface) est la variante Atari du SCSI : des
//  paquets de commande de 6 octets (classe 0) ou 10 octets (classe 1) envoyés
//  octet par octet via le contrôleur DMA ($FF8604/06), adressant jusqu'à 8 cibles
//  (LUN 0). On émule un disque par cible, sauvegardé dans une image hôte (dump de
//  secteurs brut). Les commandes SCSI utiles au boot (TEST UNIT READY, INQUIRY,
//  REQUEST SENSE, MODE SENSE, READ/WRITE(6/10), READ CAPACITY…) sont portées 1:1.
//
//  Le DMA proprement dit (transfert RAM↔image) reste piloté par `Fdc` qui possède
//  l'adresse/le mode DMA et le plan mémoire (Bus) : cette classe ne fait que la
//  logique « disque » (réception de commande, accès image, tampon de réponse),
//  comme hdc.c est séparé de fdc.c chez Hatari.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/StateArchive.hpp"

class UltraSatan;

class Acsi {
public:
    static constexpr int MAX_DEVS = 8;

    ~Acsi();

    // Monte une image disque sur la cible `target` (0-7). Ouvre en lecture/écriture
    // si possible, sinon lecture seule. Taille = multiple de 512. Renvoie true si OK.
    bool mount(int target, const std::string& path);
    void unmountAll();

    // --- UltraSatan (extension NeoST, cf. io/UltraSatan.hpp) -----------------
    // Attache l'interface SD UltraSatan : ses DEUX slots occupent les cibles
    // `firstTarget` et `firstTarget+1` (peuplées même sans carte : INQUIRY
    // « JOOKIE  UltraSatan », TEST UNIT READY « medium not present »). Les images
    // SD se montent avec mount() sur ces cibles. Les paquets ICD $20 'US…' sont
    // routés vers l'appareil ; toute autre cible reste STRICTEMENT inchangée.
    void attachUltraSatan(int firstTarget, UltraSatan* dev);
    void detachUltraSatan();
    int  usatanFirstTarget() const { return usatanTarget_; }
    // Slot UltraSatan (0/1) porté par `target`, ou -1.
    int  usatanSlot(int target) const {
        return (usatan_ && usatanTarget_ >= 0 && target >= usatanTarget_
                && target < usatanTarget_ + 2 && target < MAX_DEVS) ? target - usatanTarget_ : -1;
    }

    // Reset complet (statut + paquet) — hors toggle DMA, cf. resetCommand.
    void reset();
    // Toggle du bit 8 de $FF8606 (reset DMA) : Hatari HDC_ResetCommandStatus purge
    // le STATUT seul (hdc.c:1023-1027) — le paquet de commande en vol CONTINUE
    // (byteCount intact). L'ancien code faisait l'inverse exact.
    void resetCommand() { status_ = 0; }

    bool anyEnabled() const;
    bool targetEnabled() const;            // cible courante peuplée ?
    // Chemin de l'image montée sur `target` (vide si cible non peuplée) — permet au
    // GUI de détecter qu'un profil demande une AUTRE image (remontage nécessaire).
    const std::string& mountedPath(int target = 0) const {
        static const std::string none;
        return (target >= 0 && target < MAX_DEVS && devs_[target].enabled)
                   ? devs_[target].path : none;
    }
    int  byteCount() const { return byteCount_; }
    // Nombre total de partitions sur toutes les images (port HDC_PartitionCount).
    int  partitionCount() const;

    // --- Réception d'un paquet de commande (port de Acsi_WriteCommandByte) ------
    // 1er octet du paquet (broche A1 = 0) : sélectionne la cible (bits 7-5).
    void selectTarget(uint8_t target);
    // Marqueur ICD étendu (1er octet & 0x1F == 0x1F) : statut OK, pas de commande.
    void setIcdOk() { status_ = 0; dmaError_ = false; }
    // Alimente un octet de commande. Renvoie true quand un paquet complet a été
    // EXÉCUTÉ (la réponse / le sens du transfert sont alors disponibles ci-dessous).
    bool feedByte(uint8_t b);

    // --- Résultat de la dernière commande (lu par Fdc pour le transfert DMA) ----
    uint8_t  status() const { return uint8_t(status_); }
    bool     dmaError() const { return dmaError_; }
    // Hatari Acsi_DmaTransfer : le DMA hors plage conserve le sense disque,
    // mais met CHECK CONDITION pour une copie disque→RAM échouée seulement.
    void markDmaRangeError() { dmaError_ = true; if (!dmaWrite_) status_ = 0x02; }
    int      dataLen() const { return dataLen_; }      // octets à transférer (0 = aucun)
    bool     isWrite() const { return dmaWrite_; }     // true = RAM→disque, false = disque→RAM
    const uint8_t* readBuffer() const { return buf_.data(); }   // données disque→RAM
    // Abandon de la phase de données : le secteur attendu par un paquet 'USWr…'
    // ne viendra plus, donc on désarme aussi. Sans cela le drapeau survivait à
    // l'abandon et détournait la prochaine écriture (cf. Acsi::emulateCommand).
    void     clearData() { dataLen_ = 0; dmaWrite_ = false; usatanPending_ = false; }
    // Écrit vers la cible/position engagées par la commande, indépendamment
    // d'une nouvelle sélection ACSI pendant la phase DMA différée.
    void     writeToDisk(const uint8_t* src, int len);

    // Sérialisation save-state (SYMÉTRIQUE). On sérialise UNIQUEMENT l'état mutable
    // de traitement de commande : la CONFIG des cibles (fp/enabled/hdSize/path…) est
    // re-établie par `mountAcsi` AVANT un load (« même config machine requise »), et
    // le CONTENU des images disque vit dans les fichiers hôtes (comme les disquettes).
    void serialize(StateArchive& ar) {
        ar(target_); ar(byteCount_); ar.arr(command_);
        ar(opcode_); ar(status_); ar(dmaError_);
        // buf_ n'est sérialisé qu'à hauteur de dataLen_ : il ne fait que GROSSIR
        // (prepRespBuf), et un READ(10) dont le `count` vient du programme émulé peut le
        // porter à 32 Mo DÉFINITIVEMENT — tous les save-states suivants auraient alors
        // pesé 32 Mo. Hatari a la même croissance mais ne le snapshotte pas du tout.
        if (ar.loading()) {
            ar.vec(buf_);
        } else {
            std::vector<uint8_t> live(buf_.begin(),
                                      buf_.begin() + std::min<std::size_t>(buf_.size(),
                                          dataLen_ > 0 ? std::size_t(dataLen_) : 0));
            ar.vec(live);
        }
        ar(dataLen_); ar(dmaWrite_);
        ar(dmaTarget_); ar(dmaBlockAddr_);
        ar.check(dmaTarget_ >= 0 && dmaTarget_ < MAX_DEVS, "Acsi::dmaTarget_");
        ar(usatanPending_); // v12 : paquet UltraSatan 'Wr…' en attente de son secteur DMA
        if (dmaTarget_ >= 0 && dmaTarget_ < MAX_DEVS && dmaWrite_ && dataLen_ > 0) {
            const Dev& destination = devs_[dmaTarget_];
            if (usatanPending_) {
                ar.check(usatan_ && usatanSlot(dmaTarget_) >= 0,
                         "Acsi::destination UltraSatan absente");
            } else {
                ar.check(destination.fp && dmaBlockAddr_ < destination.hdSize &&
                         uint64_t(dmaBlockAddr_) * destination.blockSize + uint64_t(dataLen_) <=
                             uint64_t(destination.hdSize) * destination.blockSize,
                         "Acsi::destination WRITE hors image");
            }
        }
        // Invariants : byteCount_ indexe command_[16], dataLen_ borne la lecture de
        // buf_ par le DMA, target_ indexe devs_ — des valeurs restaurées hors bornes
        // (négatives comprises) liraient/écriraient hors du tas.
        ar.check(byteCount_ >= 0 && byteCount_ <= 16);
        ar.check(dataLen_ >= 0 && static_cast<std::size_t>(dataLen_) <= buf_.size());
        ar.check(target_ >= 0 && target_ < MAX_DEVS);
        // État « soft » par cible (change au fil des commandes : REQUEST SENSE…).
        for (Dev& d : devs_) { ar(d.lastError); ar(d.lastBlockAddr); ar(d.setLastBlockAddr); }
    }

private:
    struct Dev {
        FILE*    fp = nullptr;
        bool     enabled = false;
        bool     readOnly = false;
        uint32_t hdSize = 0;            // taille en secteurs
        uint32_t blockSize = 512;
        uint8_t  lastError = 0;         // HD_REQSENS_*
        uint32_t lastBlockAddr = 0;
        bool     setLastBlockAddr = false;
        int      scsiVersion = 1;
        std::string path;
    };
    Dev devs_[MAX_DEVS];

    // État du contrôleur (≈ SCSI_CTRLR).
    int      target_   = 0;
    int      dmaTarget_ = 0;             // destination de la phase WRITE engagée
    uint32_t dmaBlockAddr_ = 0;          // position engagée, pas celle du prochain CDB
    int      byteCount_ = 0;
    uint8_t  command_[16] = {0};
    uint8_t  opcode_   = 0;
    int      status_   = 0;
    bool     dmaError_ = false;
    std::vector<uint8_t> buf_;          // tampon de réponse (disque→RAM)
    int      dataLen_  = 0;
    bool     dmaWrite_ = false;         // commande d'écriture en attente de données RAM

    // UltraSatan (cf. attachUltraSatan) : appareil + première cible (2 slots).
    UltraSatan* usatan_ = nullptr;
    int  usatanTarget_  = -1;
    bool usatanPending_ = false;        // paquet 'US' en écriture, secteur DMA attendu
    void executeUltraSatan();

    // Helpers (port des HDC_*).
    unsigned lun() const  { return (command_[1] & 0xE0) >> 5; }
    uint32_t lba() const;
    int      count() const;
    uint8_t* prepRespBuf(int size);

    void emulateCommand();
    void cmdTestUnitReady();
    void cmdInquiry();
    void cmdRequestSense();
    void cmdModeSense();
    void cmdReadCapacity();
    void cmdReadSector();
    void cmdWriteSector();
    void cmdSeek();
    void cmdFormatDrive();
    void cmdReportLuns();
};
