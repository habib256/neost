// =============================================================================
//  HayesModem.cpp — Modem Hayes émulé (cf. HayesModem.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/HayesModem.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "net/Socket.hpp"

namespace {
void mtrace(const char* what, const std::string& detail) {
    static const bool on = std::getenv("NEOST_NET_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[modem] %s %s\n", what, detail.c_str());
}
} // namespace

HayesModem::~HayesModem() { hangup(false); }

void HayesModem::sendStr(const std::string& s) {
    for (char c : s) mfp_.receiveByte(uint8_t(c));
    mfp_.receiveByte('\r');
    mfp_.receiveByte('\n');
}

void HayesModem::hangup(bool notify) {
    if (neonet::socketValid(fd_)) {
        neonet::sockClose(fd_);
        fd_ = neonet::kInvalidSocket;
    }
    dataMode_ = false;
    plusCount_ = 0;
    mfp_.setRs232Dcd(false);                     // porteuse tombée
    if (notify) sendStr("NO CARRIER");
}

void HayesModem::dial(const std::string& target) {
    // ATD[T|P] hôte:port — la partie numérotation tolère espaces et virgules.
    std::string t;
    for (char c : target)
        if (!std::isspace(uint8_t(c)) && c != ',') t += c;
    if (!t.empty() && (t[0] == 'T' || t[0] == 't' || t[0] == 'P' || t[0] == 'p'))
        t.erase(0, 1);
    const auto colon = t.rfind(':');
    const std::string host = colon == std::string::npos ? t : t.substr(0, colon);
    const int port = colon == std::string::npos ? 23 : std::atoi(t.c_str() + colon + 1);
    if (host.empty()) { sendStr("ERROR"); return; }

    std::string err;
    const neonet::SocketHandle fd = neonet::tcpConnect(host, port, 5000, err);
    if (!neonet::socketValid(fd)) {
        mtrace("dial failed:", err);
        sendStr("NO CARRIER");
        return;
    }
    // Une nouvelle numérotation après +++ remplace la porteuse courante. Ne pas
    // perdre son descripteur quand la nouvelle connexion a réussi.
    if (neonet::socketValid(fd_)) neonet::sockClose(fd_);
    fd_ = fd;
    dataMode_ = true;
    plusCount_ = 0;
    mfp_.setRs232Dcd(true);                      // porteuse établie
    mtrace("connected to", host + ":" + std::to_string(port));
    const int baud = mfp_.serialBaud() > 0 ? mfp_.serialBaud() : 9600;
    sendStr("CONNECT " + std::to_string(baud));
}

void HayesModem::execute(std::string line) {
    // Retire le préfixe AT (déjà vérifié par l'appelant) et normalise.
    std::string body = line.substr(2);
    mtrace("AT", body);
    if (body.empty()) { sendStr("OK"); return; }

    const char c0 = char(std::toupper(uint8_t(body[0])));
    switch (c0) {
    case 'D':                                    // ATD… : numérotation
        dial(body.substr(1));
        return;
    case 'H':                                    // ATH : raccrocher
        hangup(false);
        sendStr("OK");
        return;
    case 'O':                                    // ATO : retour en ligne
        if (neonet::socketValid(fd_)) { dataMode_ = true; sendStr("CONNECT"); }
        else sendStr("NO CARRIER");
        return;
    case 'Z':                                    // ATZ : reset
        hangup(false);
        echo_ = true;
        sendStr("OK");
        return;
    case 'E':                                    // ATE0/ATE1 : écho
        echo_ = !(body.size() > 1 && body[1] == '0');
        sendStr("OK");
        return;
    case 'I':                                    // ATI : identification
        sendStr("NeoST virtual Hayes modem (TCP bridge)");
        sendStr("OK");
        return;
    case 'A':                                    // ATA : pas d'appels entrants v1
        sendStr("NO CARRIER");
        return;
    default:                                     // S-registres, &F, X, V… : tolérés
        sendStr("OK");
        return;
    }
}

void HayesModem::onTx(uint8_t b) {
    if (dataMode_) {
        // Échappement « +++ » (simplifié, sans garde de silence).
        if (b == '+') {
            if (++plusCount_ >= 3) {
                dataMode_ = false;
                plusCount_ = 0;
                sendStr("OK");
                return;
            }
        } else if (plusCount_ > 0) {
            // Les '+' retenus étaient des données : on les envoie avant l'octet.
            for (int i = 0; i < plusCount_; ++i) {
                const uint8_t plus = '+';
                neonet::sockSend(fd_, &plus, 1);
            }
            plusCount_ = 0;
        }
        if (b != '+' && neonet::socketValid(fd_) && neonet::sockSend(fd_, &b, 1) < 0)
            hangup(true);                        // connexion morte
        return;
    }

    // Mode commande : écho + accumulation jusqu'au CR.
    if (echo_) mfp_.receiveByte(b);
    if (b == '\r') {
        std::string line = cmd_;
        cmd_.clear();
        // Le préfixe AT/at est requis ; tout le reste est ignoré en silence.
        if (line.size() >= 2 && std::toupper(uint8_t(line[0])) == 'A'
            && std::toupper(uint8_t(line[1])) == 'T')
            execute(line);
        return;
    }
    if (b == 8 || b == 127) {                    // backspace
        if (!cmd_.empty()) cmd_.pop_back();
        return;
    }
    if (b >= 0x20 && cmd_.size() < 200) cmd_ += char(b);
}

void HayesModem::poll() {
    // DCD suit la porteuse (&C1) EN PERMANENCE : le reset du MFP et le branchement d'un
    // adaptateur de port la remettaient « présente » sans connexion, alors qu'elle
    // retombait après ATH — deux états de repos différents selon l'historique.
    mfp_.setRs232Dcd(neonet::socketValid(fd_));
    if (!neonet::socketValid(fd_)) return;
    // Mode commande (après « +++ ») : un modem retient les données distantes. Les lire
    // les mêlait à l'écho et aux réponses OK/CONNECT des commandes AT ; elles restent
    // dans le socket jusqu'à ATO.
    if (!dataMode_) return;
    // Pompe le TCP entrant tant que la file RX du MFP a de la place — la
    // livraison à l'ST reste cadencée au débit série par le MFP lui-même.
    while (mfp_.hostRxPending() < 3500 && neonet::sockHasData(fd_)) {
        uint8_t tmp[512];
        const int rc = neonet::sockRecv(fd_, tmp, int(sizeof tmp), 0);
        if (rc <= 0) {                           // fermé ou erreur
            if (dataMode_) hangup(true); else hangup(false);
            return;
        }
        for (int i = 0; i < rc; ++i) mfp_.receiveByte(tmp[i]);
    }
}
