// =============================================================================
//  UltraSatan.hpp — Interface SD/MMC « UltraSatan » (Jookie) sur le bus ACSI.
//
//  L'UltraSatan est LA réponse historique de l'écosystème ST au stockage de masse
//  moderne : un boîtier ACSI à deux slots SD/MMC, chaque slot étant une cible
//  ACSI (ID n et n+1, configurables), plus une horloge temps réel sauvegardée par
//  pile. Les pilotes d'époque (HDDRIVER, ICD PRO, AHDI, EmuTOS) le voient comme
//  deux disques SCSI ordinaires ; ses commandes PROPRES (version du firmware,
//  horloge, nom INQUIRY, réglages…) passent par un paquet ICD de 11 octets :
//
//      octet 0 : (ID << 5) | $1F            marqueur ICD étendu
//      octet 1 : $20                        groupe 1 (10 octets) — jamais un opcode SCSI utile
//      octets 2-3 : 'U','S'                 signature UltraSatan
//      octets 4-7 : code de commande ASCII  ('CurntFW', 'RdCl'/'WrCl', 'RdINQRN'/'WrINQRN',
//                                           'RdSt'/'WrSt', 'RdLog', 'RdFW'/'WrFW')
//      octets 8-10 : 3 octets de paramètres (magie 'RTC' pour WrCl, $83 $03 $17 pour WrSt)
//
//  puis UN secteur (512 octets) est transféré par DMA dans le sens de la commande.
//  Source de vérité : firmware UltraSatan v1.20 (atarijookie/ce-atari, `ultrasatan/
//  UltraSatan/scsi_icd.c`, `special.c`, `rtc.c`) et l'outil ST `US_CONF.C`.
//  Hatari n'a aucun équivalent (il n'émule qu'un disque ACSI générique) — extension
//  NeoST, inactive par défaut, cf. docs/HATARI_DIVERGENCES.md § Extensions.
//
//  Cette classe ne fait que la LOGIQUE propre à l'UltraSatan ; la réception des
//  paquets, les commandes SCSI standard et l'accès aux images SD restent dans
//  `Acsi` (port de hdc.c), qui la consulte pour l'INQUIRY et les paquets 'US'.
//  Dans la couche ACSI, l'octet 0 (marqueur) est CONSOMMÉ par Fdc : `Acsi::command_`
//  commence à l'octet 1 ($20) — d'où le décalage de −1 dans `execute`.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include "core/Pacing.hpp"
#include <cstdint>
#include <functional>
#include <string>

#include "core/StateArchive.hpp"

class UltraSatan {
public:
    static constexpr int     kSlots      = 2;       // deux slots SD/MMC
    static constexpr uint8_t kIcdOpcode  = 0x20;    // octet 1 des paquets 'US'
    static constexpr int     kSector     = 512;     // toute réponse 'US' = un secteur
    static constexpr int     kNameLen    = 10;      // nom INQUIRY (page flash 2019)

    // Date/heure BINAIRE (année complète, p.ex. 2026) — le firmware parle en
    // « années depuis 2000 » sur le fil (rtc.c : year = 2000 + yea).
    struct DateTime { int year = 2000, month = 1, day = 1, hour = 0, min = 0, sec = 0; };

    UltraSatan();

    // Horloge ÉMULÉE (cycle CPU absolu) — même source que Rtc (Machine la branche).
    void setClock(std::function<int64_t()> now) { now_ = std::move(now); }
    // Horloge de l'appareil (pile indépendante du Mega RTC). setDateTime fige une
    // date (auto-tests déterministes) ; getDateTime rattrape les secondes émulées.
    void     setDateTime(const DateTime& dt);
    DateTime getDateTime();

    // Nom INQUIRY (10 caractères, « UltraSatan » par défaut — modifiable par
    // l'outil US_CONF via 'WrINQRN', comme sur le vrai appareil).
    const std::string& inquiryName() const { return name_; }
    void setInquiryName(const std::string& n);

    // Réponse INQUIRY du slot `slot` (0/1) — port de SCSI_Inquiry (scsi6.c) :
    // « JOOKIE  » + nom + n° de slot + version + date, bit RMB (amovible) posé.
    void buildInquiry(int slot, uint8_t* buf, int n, bool lunOk) const;

    // Exécute un paquet 'US' : `cdb` = les 10 octets reçus après le marqueur ICD
    // (cdb[0] == $20, cdb[1..2] == "US"). Renvoie le statut ACSI (0 OK, 2 CHECK
    // CONDITION). Si 0 : dataLen() vaut 512 et soit readBuffer() est prêt (sens
    // appareil→ST), soit isWrite() est vrai et l'appelant DOIT livrer le secteur
    // ST→appareil via writeData(). Les commandes flash ('RdFW'/'WrFW') sont
    // REFUSÉES : NeoST n'émule pas la dataflash — impossible de briquer l'appareil.
    int  execute(const uint8_t* cdb);
    int  writeData(const uint8_t* src, int len);   // secteur ST→appareil (WrCl/WrINQRN/WrSt)
    bool isWrite() const { return pendingWrite_ != Pending::None; }
    int  dataLen() const { return dataLen_; }
    const uint8_t* readBuffer() const { return buf_; }
    void clearData() { dataLen_ = 0; pendingWrite_ = Pending::None; }

    // Reset protocole (power-cycle de l'appareil) : purge la commande en vol.
    // Le nom, les réglages et l'horloge SURVIVENT (flash + pile), comme en vrai.
    void reset() { clearData(); }

    void serialize(StateArchive& ar);

private:
    enum class Pending : uint8_t { None = 0, Clock, Name, Settings };

    std::function<int64_t()> now_;
    int64_t  baseCycle_ = 0;      // cycle du dernier calage de l'horloge
    int64_t  baseSecs_  = 0;      // secondes depuis 2000-01-01 00:00:00 à baseCycle_
    bool     primed_    = false;  // baseCycle_ calé au 1er accès (pas de rattrapage géant au boot)

    std::string name_ = "UltraSatan";
    uint8_t  settings_[kSector] = {0};   // page flash 2020 (opaque, round-trip RdSt/WrSt)
    uint8_t  buf_[kSector] = {0};
    int      dataLen_ = 0;
    Pending  pendingWrite_ = Pending::None;

    void catchUp();
    static int64_t toSecs(const DateTime& dt);
    static DateTime fromSecs(int64_t s);
    void initFromHostTime();
};
