// =============================================================================
//  HayesModem.cpp — Modem Hayes émulé (cf. HayesModem.hpp).
//
//  Les comportements Hayes ci-dessous (AT&Dn, abandon de la numérotation) sont
//  ceux du jeu de commandes Hayes tel que le reprennent les modems compatibles et
//  la recommandation ITU-T V.250 ; aucun de ces textes n'est dans
//  extern/datasheets, d'où le grade [E] des commentaires. Tranché par : le manuel
//  d'un modem Hayes (Smartmodem 2400, Accura) ou V.250, rouvert à la section.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/HayesModem.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <system_error>
#include <thread>

#include "net/Socket.hpp"

namespace {
void mtrace(const char* what, const std::string& detail) {
    static const bool on = std::getenv("NEOST_NET_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[modem] %s %s\n", what, detail.c_str());
}
} // namespace

// Numérotation en vol. Le fil hôte détient sa propre référence : s'il finit après
// un abandon (ou après la destruction du modem), il ferme lui-même son socket.
struct HayesModem::DialJob {
    std::mutex m;
    bool done = false;
    bool cancelled = false;
    neonet::SocketHandle fd = neonet::kInvalidSocket;
    std::string err;
};

HayesModem::~HayesModem() {
    cancelDial();
    if (neonet::socketValid(fd_)) neonet::sockClose(fd_);
}

void HayesModem::out(uint8_t b) {
    if (toSt_) toSt_(b);
    else mfp_.receiveByte(b);
}

void HayesModem::sendStr(const std::string& s) {
    for (char c : s) out(uint8_t(c));
    out('\r');
    out('\n');
}

void HayesModem::cancelDial() {
    if (!dial_) return;
    {
        std::lock_guard<std::mutex> lk(dial_->m);
        dial_->cancelled = true;
        // Issue déjà rendue mais pas encore relevée par poll() : le socket est à nous.
        if (dial_->done && neonet::socketValid(dial_->fd)) {
            neonet::sockClose(dial_->fd);
            dial_->fd = neonet::kInvalidSocket;
        }
    }
    mtrace("dial cancelled:", dialLabel_);
    dial_.reset();
}

void HayesModem::hangup(bool notify) {
    const bool hadCall = dial_ != nullptr || neonet::socketValid(fd_);
    cancelDial();
    if (neonet::socketValid(fd_)) {
        neonet::sockClose(fd_);
        fd_ = neonet::kInvalidSocket;
    }
    dataMode_ = false;
    plusCount_ = 0; escArmed_ = false;
    mfp_.setRs232Dcd(false);                     // porteuse tombée
    if (notify && hadCall) sendStr("NO CARRIER");
}

void HayesModem::resetProfile() {                // ATZ / AT&F : profil d'usine
    hangup(false);
    echo_ = true;
    dtrMode_ = 2;
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
    if (host.empty() || port <= 0 || port > 65535) { sendStr("ERROR"); return; }

    // La résolution DNS et la connexion (jusqu'à 5 s) tournent hors du fil
    // d'émulation : elles le gelaient, GUI compris. L'issue est relevée par poll().
    auto job = std::make_shared<DialJob>();
    try {
        std::thread([job, host, port] {
            std::string err;
            const neonet::SocketHandle fd = neonet::tcpConnect(host, port, 5000, err);
            std::lock_guard<std::mutex> lk(job->m);
            if (job->cancelled) { neonet::sockClose(fd); return; }
            job->fd = fd;
            job->err = err;
            job->done = true;
        }).detach();
    } catch (const std::system_error&) {
        mtrace("dial failed:", "cannot start the dial thread");
        sendStr("NO CARRIER");
        return;
    }
    dial_ = std::move(job);
    dialLabel_ = host + ":" + std::to_string(port);
    plusCount_ = 0; escArmed_ = false;
    mtrace("dialing", dialLabel_);
}

void HayesModem::execute(const std::string& line) {
    // Ligne « AT… » (préfixe vérifié par l'appelant) : plusieurs commandes peuvent
    // s'enchaîner (« AT&FE0&D2 », chaînes d'initialisation des terminaux) ; D, A et O
    // terminent la ligne. Une seule réponse finale, OK ou ERROR.
    const std::string body = line.substr(2);
    mtrace("AT", body);
    std::size_t i = 0;
    auto digits = [&](int def) {
        if (i >= body.size() || !std::isdigit(uint8_t(body[i]))) return def;
        int v = 0;
        while (i < body.size() && std::isdigit(uint8_t(body[i])) && v < 100000)
            v = v * 10 + (body[i++] - '0');
        return v;
    };
    while (i < body.size()) {
        const char c = char(std::toupper(uint8_t(body[i++])));
        switch (c) {
        case ' ':
            break;
        case 'D':                                // ATD… : numérotation, fin de ligne
            dial(body.substr(i));
            return;
        case 'A':                                // ATA : pas d'appels entrants v1
            sendStr("NO CARRIER");
            return;
        case 'O':                                // ATO : retour en ligne
            digits(0);
            if (neonet::socketValid(fd_)) { dataMode_ = true; sendStr("CONNECT"); }
            else sendStr("NO CARRIER");
            return;
        case 'H':                                // ATH : raccrocher
            digits(0);
            hangup(false);
            break;
        case 'Z':                                // ATZ : profil d'usine
            digits(0);
            resetProfile();
            break;
        case 'E':                                // ATE0/ATE1 : écho
            echo_ = digits(0) != 0;
            break;
        case 'I':                                // ATI : identification
            digits(0);
            sendStr("NeoST virtual Hayes modem (TCP bridge)");
            break;
        case 'S': {                              // Sn=v / Sn? : registres tolérés
            digits(0);
            if (i < body.size() && body[i] == '=') { ++i; digits(0); }
            else if (i < body.size() && body[i] == '?') { ++i; sendStr("000"); }
            break;
        }
        case '&': {                              // &Dn, &F ; le reste est toléré
            if (i >= body.size()) { sendStr("ERROR"); return; }
            const char e = char(std::toupper(uint8_t(body[i++])));
            const int n = digits(-1);
            if (e == 'F') { resetProfile(); break; }
            if (e == 'D') {
                const int mode = n < 0 ? 0 : n;
                if (mode > 3) { sendStr("ERROR"); return; }
                dtrMode_ = mode;
            }
            break;
        }
        default:
            // V, X, Q, M, L, B, C… : tolérés, argument numérique compris. Commandes
            // étendues (+FCLASS=0, \N3, %C…) : tolérées jusqu'au « ; » suivant, comme
            // l'ancienne réponse OK à toute ligne inconnue.
            if (std::isalpha(uint8_t(c))) { digits(0); break; }
            while (i < body.size() && body[i] != ';') ++i;
            if (i < body.size()) ++i;
            break;
        }
    }
    sendStr("OK");
}

void HayesModem::onPortA(uint8_t a) {
    const bool dtr = (a & 0x10) == 0;            // bit4 = 0 → DTR assertée (actif bas)
    const bool fell = dtrKnown_ && dtrOn_ && !dtr;
    dtrOn_ = dtr;
    dtrKnown_ = true;
    if (!fell) return;
    // [E] Jeu de commandes Hayes, AT&D (cf. en-tête) : sur un front DTR active → inactive,
    // &D0 ignore, &D1 repasse en mode commande en gardant l'appel, &D2 raccroche et
    // repasse en mode commande, &D3 réinitialise le modem. [F] Défaut &D2 (celui des
    // modems compatibles courants, pas forcément du Smartmodem d'origine) et résultat
    // NO CARRIER émis quand un appel tombe. Seul un FRONT agit : un logiciel qui ne
    // touche jamais à DTR (le TOS la laisse au repos) garde un modem utilisable.
    // Le reset du ST remet le port A du PSG au repos ($FF) : DTR retombe comme sur le
    // fer, et un appel en cours est raccroché sous &D2.
    mtrace("DTR dropped, &D", std::to_string(dtrMode_));
    switch (dtrMode_) {
    case 1:
        if (dataMode_) { dataMode_ = false; plusCount_ = 0; escArmed_ = false; sendStr("OK"); }
        break;
    case 2:
        hangup(true);
        cmd_.clear();
        break;
    case 3: {
        const bool hadCall = dial_ != nullptr || neonet::socketValid(fd_);
        resetProfile();
        cmd_.clear();
        if (hadCall) sendStr("NO CARRIER");
        break;
    }
    default:
        break;
    }
}

void HayesModem::onTx(uint8_t b) {
    if (dial_) {
        // [E] V.250, commande D : tout caractère reçu pendant l'établissement abandonne
        // l'appel (NO CARRIER) et n'est pas interprété. Le LF qui suit le CR de la ligne
        // ATD (terminaux en CRLF) et NUL sont ignorés, sans quoi « ATDT…\r\n »
        // s'annulerait lui-même. ATH ou « +++ » annulent donc par leur premier octet.
        if (b == '\n' || b == 0) return;
        cancelDial();
        cmd_.clear();
        sendStr("NO CARRIER");
        return;
    }
    if (dataMode_) {
        // Échappement « +++ » AVEC temps de garde (2026-11-12). [E] Usage Hayes (brevet
        // US 4 549 302, Heatherington, non relu ici ; registre S12 = 50 × 1/50 s) : une
        // seconde de silence, trois '+' rapprochés, une seconde de silence — poll() conclut.
        // Sans garde, tout « +++ » d'un transfert binaire (ZMODEM, archive, image)
        // basculait en mode commande, avalait les trois octets et répondait « OK » au
        // milieu du flux. Le temps se compte en appels de poll() (une trame émulée,
        // déterministe) : 1 s à 50 Hz, ~0,83 s à 60 Hz, ~0,7 s en haute résolution.
        const bool quiet = idlePolls_ >= kGuardPolls;
        idlePolls_ = 0;
        if (escArmed_) flushPlus();              // silence final rompu : c'étaient des données
        if (b == '+') {
            if (plusCount_ > 0 && quiet) flushPlus();   // '+' trop espacés : données, on repart
            if (plusCount_ > 0 || quiet) {
                if (++plusCount_ == 3) escArmed_ = true;   // reste à voir le silence final
                return;
            }
        } else if (plusCount_ > 0) {
            flushPlus();                         // les '+' retenus étaient des données
        }
        if (neonet::socketValid(fd_) && neonet::sockSend(fd_, &b, 1) < 0)
            hangup(true);                        // connexion morte
        return;
    }

    // Mode commande : écho + accumulation jusqu'au CR.
    if (echo_) out(b);
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

// '+' retenus (début d'échappement avorté) : c'étaient des données.
void HayesModem::flushPlus() {
    for (int i = 0; i < plusCount_; ++i) {
        const uint8_t plus = '+';
        if (neonet::socketValid(fd_)) neonet::sockSend(fd_, &plus, 1);
    }
    plusCount_ = 0; escArmed_ = false;
}

void HayesModem::poll() {
    // Temps de garde de « +++ » (cf. onTx) : une trame de plus sans octet émis.
    if (idlePolls_ < kGuardPolls) ++idlePolls_;
    if (dataMode_ && plusCount_ > 0 && idlePolls_ >= kGuardPolls) {
        if (escArmed_) {                         // « +++ » encadré de silence : échappement
            dataMode_ = false;
            plusCount_ = 0; escArmed_ = false;
            sendStr("OK");
        } else {
            flushPlus();                         // un ou deux '+' isolés : données
        }
    }
    // Issue d'une numérotation : relevée ici, à la trame, jamais sur le fil hôte.
    if (dial_) {
        neonet::SocketHandle fd = neonet::kInvalidSocket;
        std::string err;
        bool done = false;
        {
            std::lock_guard<std::mutex> lk(dial_->m);
            if (dial_->done) {
                done = true;
                fd = dial_->fd;
                err = dial_->err;
                dial_->fd = neonet::kInvalidSocket;
            }
        }
        if (done) {
            dial_.reset();
            if (!neonet::socketValid(fd)) {
                // L'appel courant (mis en attente par « +++ ») est conservé.
                mtrace("dial failed:", err);
                sendStr("NO CARRIER");
            } else {
                // Une nouvelle numérotation après +++ remplace la porteuse courante.
                if (neonet::socketValid(fd_)) neonet::sockClose(fd_);
                fd_ = fd;
                dataMode_ = true;
                plusCount_ = 0; escArmed_ = false;
                mtrace("connected to", dialLabel_);
                const int baud = mfp_.serialBaud() > 0 ? mfp_.serialBaud() : 9600;
                sendStr("CONNECT " + std::to_string(baud));
            }
        }
    }
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
            hangup(true);
            return;
        }
        for (int i = 0; i < rc; ++i) out(tmp[i]);
    }
}
