// MC68901 : réception alignée à droite, bits inutilisés à zéro (MAME rx_clock).
#include "io/Mfp.hpp"
#include <cstdio>
int testMfpRxFormat() {
    int checks=0,failures=0;
    auto eq=[&](const char* name,int64_t got,int64_t want) {
        ++checks; if(got!=want) { ++failures; if(failures<8)std::printf("  FAIL MFP RX format %s: %lld attendu %lld\n",name,(long long)got,(long long)want); }
    };
    // Données hôte : le bit de parité n'est pas fourni par l'API octet.
    for(int bits=5;bits<=8;++bits) for(int parity=0;parity<2;++parity) {
        Scheduler s; Mfp m; m.setScheduler(&s);
        s.setCallback(Scheduler::SERIAL_RX,[&]{m.onSerialRxEvent();});
        m.write8(0xFFFA29,0x88|((8-bits)<<5)|(parity<<2)); m.write8(0xFFFA2B,1);
        m.write8(0xFFFA07,0x18);
        for(int b=0;b<256;++b) {
            m.receiveByte(uint8_t(b)); s.runTo(s.nextDue());
            eq("RXFULL",m.read8(0xFFFA2B)&0x80,0x80);
            eq("pas erreur synthétique",m.read8(0xFFFA2B)&0x70,0);
            eq("données hôte",m.read8(0xFFFA2F),b&((1<<bits)-1));
        }
    }
    // Bouclage : mêmes lignes et même UCR pour TX/RX, largeur engagée conservée.
    for(int bits=5;bits<=8;++bits) {
        Scheduler s; Mfp m; m.setScheduler(&s); m.setLoopback(true);
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA29,0x88|((8-bits)<<5)); m.write8(0xFFFA2B,1);m.write8(0xFFFA2D,1);
        m.write8(0xFFFA2F,0xFF); m.write8(0xFFFA29,0x88); s.runTo(s.nextDue());
        eq("bouclage engagé",m.read8(0xFFFA2F),(1<<bits)-1);
    }
    for(int bits=5;bits<=8;++bits) {
        Scheduler s; Mfp m; m.setScheduler(&s);
        s.setCallback(Scheduler::SERIAL_RX,[&]{m.onSerialRxEvent();});
        m.write8(0xFFFA29,0x88|((8-bits)<<5));m.write8(0xFFFA2B,1);
        m.receiveByte(0xFF);m.receiveByte(0xFF);
        const auto due=s.nextDue();m.write8(0xFFFA29,0x88);
        eq("échéance engagée",s.nextDue(),due);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);s.serialize(save);m.serialize(save);
        s.runTo(due);m.read8(0xFFFA2F);s.runTo(s.nextDue());m.read8(0xFFFA2F);
        auto load=StateArchive::loader(state.data(),state.size());s.serialize(load);m.serialize(load);
        eq("archive en vol",load.ok(),1);s.runTo(s.nextDue());
        eq("largeur engagée restaurée",m.read8(0xFFFA2F),(1<<bits)-1);
        s.runTo(s.nextDue());eq("largeur caractère suivant",m.read8(0xFFFA2F),0xFF);
    }
    { // Le repli synchrone conserve le transport historique huit bits.
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_RX,[&]{m.onSerialRxEvent();});
        m.write8(0xFFFA29,0xE0);m.write8(0xFFFA2B,1);m.receiveByte(0xFF);s.runTo(s.nextDue());
        eq("repli sync",m.read8(0xFFFA2F),0xFF);
    }
    { // Les fixtures sans Scheduler gardent leur bouclage instantané huit bits.
        Mfp m;m.setLoopback(true);m.write8(0xFFFA29,0xE8);m.write8(0xFFFA2B,1);
        m.write8(0xFFFA2F,0xFF);eq("fixture sans Scheduler",m.read8(0xFFFA2F),0xFF);
    }
    { // Un masque forgé ne doit pas être accepté par le chargeur d'archive.
        Mfp m;std::vector<uint8_t> state;auto save=StateArchive::saver(state);m.serialize(save);
        const size_t tail=sizeof(uint32_t)+4*sizeof(bool)+3*sizeof(uint8_t)
            +4*sizeof(int64_t)+sizeof(int)+sizeof(bool)+2*sizeof(int64_t)+sizeof(int);
        state[state.size()-tail-sizeof(uint8_t)]=0;
        auto load=StateArchive::loader(state.data(),state.size());m.serialize(load);
        eq("masque invalide rejeté",load.ok(),0);
    }
    std::printf("[selftest-mfp-rx-format] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_RX_FORMAT_TEST_MAIN
int main(){return testMfpRxFormat()?1:0;}
#endif
