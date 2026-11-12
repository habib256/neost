// =============================================================================
//  UltraSatan.cpp — Commandes propres à l'UltraSatan (cf. UltraSatan.hpp).
//
//  Port du comportement du firmware v1.20 (ce-atari/ultrasatan) :
//    · scsi_icd.c ProcICD : cmd[2..3]=="US" puis comparaison sur cmd[4..] ;
//    · special.c : chaque commande remplit/consomme UN secteur de 512 octets ;
//    · rtc.c : horloge en binaire sur le fil {année−2000, mois, jour, h, min, s}.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/UltraSatan.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
constexpr int ST_OK = 0, ST_CHECK = 2;              // statuts ACSI
// Chaînes du firmware (mydefines.h). La version est celle que les outils
// d'époque connaissent ; la parenthèse dit honnêtement d'où vient la réponse.
constexpr char kVersionString[] = "UltraSatan v1.20 (NeoST emulation)";
constexpr char kVersionShort[]  = "1.20";
constexpr char kDateString[]    = "01/28/14";        // MM/DD/YY (DATE_STRING)

bool cmpn(const uint8_t* a, const char* s, int n) { return std::memcmp(a, s, std::size_t(n)) == 0; }

// Calendrier civil ↔ jours depuis 2000-03-01 (algorithme de H. Hinnant, proleptique
// grégorien) — évite mktime/localtime, dépendants du fuseau de l'hôte.
int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + unsigned(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}
void civilFromDays(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t  yy  = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp  = (5 * doy + 2) / 153;
    d = int(doy - (153 * mp + 2) / 5 + 1);
    m = int(mp < 10 ? mp + 3 : mp - 9);
    y = int(yy + (m <= 2));
}
constexpr int64_t kEpoch2000 = 10957;   // daysFromCivil(2000,1,1) en jours depuis 1970
} // namespace

UltraSatan::UltraSatan() { initFromHostTime(); }

// -----------------------------------------------------------------------------
//  Horloge (pile indépendante — comme la RP5C15 Mega, elle suit les CYCLES émulés)
// -----------------------------------------------------------------------------
int64_t UltraSatan::toSecs(const DateTime& dt) {
    return (daysFromCivil(dt.year, dt.month, dt.day) - kEpoch2000) * 86400
         + int64_t(dt.hour) * 3600 + int64_t(dt.min) * 60 + dt.sec;
}
UltraSatan::DateTime UltraSatan::fromSecs(int64_t s) {
    DateTime dt;
    int64_t days = s / 86400, rem = s % 86400;
    if (rem < 0) { rem += 86400; --days; }
    civilFromDays(days + kEpoch2000, dt.year, dt.month, dt.day);
    dt.hour = int(rem / 3600); dt.min = int((rem % 3600) / 60); dt.sec = int(rem % 60);
    return dt;
}

void UltraSatan::initFromHostTime() {
    // Même chemin que Rtc::initFromHostTime (std::localtime : portable MinGW/Emscripten).
    const std::time_t t = std::time(nullptr);
    const std::tm* tmv = std::localtime(&t);
    DateTime dt;
    if (tmv) {
        dt.year = tmv->tm_year + 1900; dt.month = tmv->tm_mon + 1; dt.day = tmv->tm_mday;
        dt.hour = tmv->tm_hour; dt.min = tmv->tm_min; dt.sec = tmv->tm_sec;
    }
    if (dt.year < 2000) dt.year = 2000;            // le firmware ne code que 2000-2255
    baseSecs_ = toSecs(dt);
    primed_ = false;
}

void UltraSatan::catchUp() {
    if (!now_) return;
    const int64_t now = now_();
    if (!primed_) { baseCycle_ = now; primed_ = true; return; }
    const int64_t elapsed = now - baseCycle_;
    const int64_t hz = neost::pacing::cpuHzInt();   // quartz CPU en service (core/Pacing.hpp)
    if (elapsed < hz) return;
    const int64_t secs = elapsed / hz;
    baseSecs_  += secs;
    baseCycle_ += secs * hz;                        // garde la phase sub-seconde
}

void UltraSatan::setDateTime(const DateTime& dt) {
    baseSecs_ = toSecs(dt);
    if (now_) { baseCycle_ = now_(); primed_ = true; }
}
UltraSatan::DateTime UltraSatan::getDateTime() { catchUp(); return fromSecs(baseSecs_); }

void UltraSatan::setInquiryName(const std::string& n) {
    name_ = n.substr(0, std::size_t(kNameLen));
    if (name_.empty()) name_ = "UltraSatan";
}

// -----------------------------------------------------------------------------
//  INQUIRY — port de SCSI_Inquiry (scsi6.c) : l'octet i vaut ' ' dans la zone
//  ASCII (8..43), 0 ailleurs, puis les champs connus sont posés.
// -----------------------------------------------------------------------------
void UltraSatan::buildInquiry(int slot, uint8_t* buf, int n, bool lunOk) const {
    for (int i = 0; i < n; ++i) {
        uint8_t v = (i >= 8 && i <= 43) ? ' ' : 0;
        if (i == 0) v = lunOk ? 0x00 : 0x7F;                 // qualificateur selon le LUN
        else if (i == 1) v = 0x80;                           // RMB : support amovible (carte SD)
        else if (i == 2 || i == 3) v = 0x02;                 // niveau SCSI / format de réponse
        else if (i == 4) v = 0x27;                           // longueur additionnelle
        else if (i >= 8 && i <= 15) v = uint8_t("JOOKIE  "[i - 8]);
        else if (i >= 16 && i <= 25) v = uint8_t(i - 16 < int(name_.size()) ? name_[std::size_t(i - 16)] : ' ');
        else if (i == 27) v = uint8_t('1' + slot);           // n° de slot (1 ou 2)
        else if (i >= 32 && i <= 35) v = uint8_t(kVersionShort[i - 32]);
        else if (i >= 36 && i <= 43) v = uint8_t(kDateString[i - 36]);
        buf[i] = v;
    }
}

// -----------------------------------------------------------------------------
//  Paquets 'US' — port de ProcICD (scsi_icd.c) + special.c. cdb[0] = $20,
//  cdb[1..2] = "US", cdb[3..6] = code, cdb[7..9] = paramètres.
// -----------------------------------------------------------------------------
int UltraSatan::execute(const uint8_t* cdb) {
    static const bool trace = std::getenv("NEOST_ACSI_TRACE") != nullptr;
    dataLen_ = 0;
    pendingWrite_ = Pending::None;
    std::memset(buf_, 0, sizeof buf_);
    const uint8_t* code = cdb + 3;
    if (trace)
        std::fprintf(stderr, "[usatan] US%c%c%c%c%c%c%c params %02X %02X %02X\n",
                     code[0], code[1], code[2], code[3], code[4], code[5], code[6],
                     cdb[7], cdb[8], cdb[9]);

    // Ordre de comparaison IDENTIQUE au firmware (préfixes de 4 puis 7/5 octets).
    if (cmpn(code, "RdFW", 4) || cmpn(code, "WrFW", 4)) {
        // Dataflash non émulée : CHECK CONDITION, comme un flash « pas prêt ».
        return ST_CHECK;
    }
    if (cmpn(code, "RdSt", 4)) {                      // page de réglages 2020
        std::memcpy(buf_, settings_, sizeof buf_);
        buf_[1] = 0;                                  // Config_GetBootBase : firmware 1, pas le base
        std::memset(buf_ + 256, 0, 256);              // 2e moitié toujours vide
        dataLen_ = kSector;
        return ST_OK;
    }
    if (cmpn(code, "WrSt", 4)) {                      // exige la magie $83 $03 $17
        if (cdb[7] != 0x83 || cdb[8] != 0x03 || cdb[9] != 0x17) return ST_CHECK;
        pendingWrite_ = Pending::Settings; dataLen_ = kSector;
        return ST_OK;
    }
    if (cmpn(code, "RdCl", 4)) {                      // horloge → 'RTC' + 6 octets binaires
        const DateTime dt = getDateTime();
        buf_[0] = 'R'; buf_[1] = 'T'; buf_[2] = 'C';
        buf_[3] = uint8_t(dt.year - 2000); buf_[4] = uint8_t(dt.month); buf_[5] = uint8_t(dt.day);
        buf_[6] = uint8_t(dt.hour); buf_[7] = uint8_t(dt.min); buf_[8] = uint8_t(dt.sec);
        dataLen_ = kSector;
        return ST_OK;
    }
    if (cmpn(code, "WrCl", 4)) {                      // magie 'RTC' dans les paramètres
        if (cdb[7] != 'R' || cdb[8] != 'T' || cdb[9] != 'C') return ST_CHECK;
        pendingWrite_ = Pending::Clock; dataLen_ = kSector;
        return ST_OK;
    }
    if (cmpn(code, "CurntFW", 7)) {                   // nom du firmware courant, 0-terminé
        std::strncpy(reinterpret_cast<char*>(buf_), kVersionString, sizeof buf_ - 1);
        dataLen_ = kSector;
        return ST_OK;
    }
    if (cmpn(code, "RdINQRN", 7)) {                   // nom INQUIRY (page 2019)
        std::memcpy(buf_, name_.data(), name_.size());
        dataLen_ = kSector;
        return ST_OK;
    }
    if (cmpn(code, "WrINQRN", 7)) {
        pendingWrite_ = Pending::Name; dataLen_ = kSector;
        return ST_OK;
    }
    if (cmpn(code, "RdLog", 5)) {                     // journal de commandes : vide
        dataLen_ = kSector;
        return ST_OK;
    }
    // Pas une commande 'US' connue : le firmware retombe sur le switch SCSI, où
    // $20 n'existe pas → INVALID COMMAND OPERATION CODE.
    return ST_CHECK;
}

int UltraSatan::writeData(const uint8_t* src, int len) {
    const Pending what = pendingWrite_;
    pendingWrite_ = Pending::None;
    dataLen_ = 0;
    if (len < kSector) return ST_CHECK;
    switch (what) {
    case Pending::Clock: {
        if (src[0] != 'R' || src[1] != 'T' || src[2] != 'C') return ST_CHECK;
        DateTime dt;
        dt.year = 2000 + src[3]; dt.month = src[4]; dt.day = src[5];
        dt.hour = src[6]; dt.min = src[7]; dt.sec = src[8];
        // Le firmware écrit les valeurs telles quelles dans le RP5C01 ; une date
        // absurde y resterait absurde. On borne pour garder un calendrier valide.
        if (dt.month < 1 || dt.month > 12 || dt.day < 1 || dt.day > 31 ||
            dt.hour > 23 || dt.min > 59 || dt.sec > 59) return ST_CHECK;
        setDateTime(dt);
        return ST_OK;
    }
    case Pending::Name: {
        // Special_WriteInquiryName : 10 octets ; 0x00/0xFF en tête = nom par défaut.
        if (src[0] == 0x00 || src[0] == 0xFF) { name_ = "UltraSatan"; return ST_OK; }
        std::string n;
        for (int i = 0; i < kNameLen && src[i]; ++i) n.push_back(char(src[i]));
        setInquiryName(n);
        return ST_OK;
    }
    case Pending::Settings:
        std::memcpy(settings_, src, sizeof settings_);
        return ST_OK;
    case Pending::None:
        break;
    }
    return ST_CHECK;
}

// -----------------------------------------------------------------------------
//  Save-state : horloge (phase + base), nom, réglages, commande en vol.
// -----------------------------------------------------------------------------
void UltraSatan::serialize(StateArchive& ar) {
    ar(baseCycle_); ar(baseSecs_); ar(primed_);
    uint8_t nameBytes[kNameLen] = {0};
    if (!ar.loading()) std::memcpy(nameBytes, name_.data(), name_.size());
    ar.arr(nameBytes);
    if (ar.loading()) {
        std::string n;
        for (int i = 0; i < kNameLen && nameBytes[i]; ++i) n.push_back(char(nameBytes[i]));
        setInquiryName(n);
    }
    ar.arr(settings_);
    ar.arr(buf_);
    ar(dataLen_);
    uint8_t pend = uint8_t(pendingWrite_);
    ar(pend);
    pendingWrite_ = Pending(pend);
    ar.check(dataLen_ >= 0 && dataLen_ <= kSector, "UltraSatan::dataLen_ hors bornes");
    ar.check(pend <= uint8_t(Pending::Settings), "UltraSatan::pendingWrite_ invalide");
}
