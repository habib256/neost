// =============================================================================
//  HayesModem.hpp — Modem Hayes émulé sur l'USART MFP (RS-232).
//
//  Le grand débloqueur du logiciel d'époque : terminaux, BBS, et les piles
//  TCP/IP historiques (STiK/STinG en SLIP/PPP) parlent toutes à « un modem sur
//  le port série ». On interprète les commandes AT côté hôte et on ouvre de
//  vraies connexions TCP : `ATDT hote:port` → CONNECT, puis pont transparent
//  octets ↔ socket. C'est l'équivalent des modules « WiFi modem » ESP8266
//  vendus pour ST, qui se branchent sur le port série d'origine.
//
//  Câblage (frontend) : Mfp::setSerialSink → onTx (octets émis par l'ST) ;
//  Machine::setRs232LineSink → onPortA (DTR, et le reset qui la fait retomber) ;
//  les réponses/le flux entrant reviennent par Mfp::receiveByte (livraison
//  cadencée au débit série + IRQ RxFull — cf. Scheduler::SERIAL_RX). poll()
//  se lance une fois par trame : il pompe le TCP entrant et livre l'issue d'une
//  numérotation. DCD suit la porteuse (Mfp::setRs232Dcd).
//
//  Numérotation ASYNCHRONE : la résolution et la connexion tournent sur un fil
//  hôte ; le fil d'émulation ne s'y bloque plus. CONNECT ou NO CARRIER arrive à
//  la première trame qui suit l'issue — date hôte, comme tout le trafic réel, hors
//  du contrat de déterminisme.
//
//  Échappement « +++ » : avec temps de garde (depuis le 2026-11-12) — silence,
//  trois '+', silence, comptés en trames (kGuardPolls appels de poll()). Un « +++ »
//  au milieu d'un transfert binaire reste donc une donnée.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "io/Mfp.hpp"
#include "net/Socket.hpp"

class HayesModem {
public:
    // `toSt` remplace Mfp::receiveByte comme sortie vers l'ST (tests) ; vide = MFP.
    explicit HayesModem(Mfp& mfp, std::function<void(uint8_t)> toSt = {})
        : mfp_(mfp), toSt_(std::move(toSt)) {}
    ~HayesModem();
    HayesModem(const HayesModem&) = delete;
    HayesModem& operator=(const HayesModem&) = delete;

    void onTx(uint8_t b);       // octet écrit par l'ST dans l'UDR
    void onPortA(uint8_t a);    // lignes du port A du PSG (DTR = bit4, actif bas)
    void poll();                // une fois par trame : issue du dial + TCP entrant
    bool connected() const { return neonet::socketValid(fd_); }
    bool dialing() const { return dial_ != nullptr; }
    bool dataMode() const { return dataMode_; }
    void hangup(bool notify);
    // Liste blanche de domaines (net_allow_domains, syntaxe d'extern/nat/NatAllowlist.h) ;
    // vide = toute destination. Une numérotation hors liste répond NO CARRIER et le dit
    // au journal, sans toucher au réseau. Liste invalide : false, l'ancienne reste.
    bool setAllowedDomains(const std::string& text);

    struct DialJob;             // numérotation en vol (partagée avec son fil hôte)

private:
    Mfp& mfp_;
    std::function<void(uint8_t)> toSt_;
    neonet::SocketHandle fd_ = neonet::kInvalidSocket;
    std::shared_ptr<DialJob> dial_;
    std::string dialLabel_;     // « hôte:port » de la numérotation en vol (journal)
    bool dataMode_ = false;
    bool echo_ = true;
    int  dtrMode_ = 2;          // AT&Dn
    bool dtrOn_ = false;        // dernier DTR vu (vrai = assertée)
    bool dtrKnown_ = false;     // aucun front tant que le premier état n'est pas connu
    int  plusCount_ = 0;
    // Temps de garde de « +++ » : 50 appels de poll() (une trame chacun) = S12 par
    // défaut (50 × 1/50 s) à 50 Hz. Le modem part « au silence » : un « +++ » tapé
    // avant toute trame est encadré de silence.
    static constexpr int kGuardPolls = 50;
    int  idlePolls_ = kGuardPolls;   // trames écoulées depuis le dernier octet émis (saturé)
    bool escArmed_ = false;          // trois '+' vus, en attente du silence final
    std::string cmd_;           // ligne AT en cours d'accumulation
    std::vector<std::string> allowed_;   // liste blanche de domaines (vide = OFF)

    void out(uint8_t b);
    void sendStr(const std::string& s);       // → ST (ajoute CRLF)
    void execute(const std::string& line);
    void dial(const std::string& target);
    void cancelDial();
    void resetProfile();
    void flushPlus();
};
