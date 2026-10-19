// RP5C15 Ricoh EK-086-9908 pp6-7 : deux banques et alarmes indépendantes.
#include "io/Rtc.hpp"
#include <cstdio>
int testRtcBank() {
    int checks=0,failures=0;auto addr=[](int i){return uint32_t(0xFFFC21+2*i);};
    auto eq=[&](const char* name,int got,int want){++checks;if(got!=want){++failures;if(failures<8)std::printf("  FAIL RTC bank %s: %d attendu %d\n",name,got,want);}};
    for(int mode : {9,11,13,15}) {
        Rtc r;r.setDateTime({12,34,21,3,25,11,46});
        uint8_t before[13];for(int i=0;i<13;++i)before[i]=r.read8(addr(i));
        r.write8(addr(13),mode);
        for(int i=2;i<=8;++i)r.write8(addr(i),0xFF);
        for(int i=2;i<=8;++i)eq("alarme mémorisée",r.read8(addr(i))&15,(i==3||i==6)?7:(i==5||i==8)?3:15);
        r.write8(addr(13),8);for(int i=0;i<13;++i)eq("heure intacte",r.read8(addr(i)),before[i]);
        r.write8(addr(15),1);r.write8(addr(13),9);
        for(int i=2;i<=8;++i)eq("reset alarme",r.read8(addr(i))&15,0);
        r.write8(addr(13),8);for(int i=0;i<13;++i)eq("reset conserve heure",r.read8(addr(i)),before[i]);
    }
    {
        int64_t clock=0;Rtc r;r.setClock([&]{return clock;});r.setSecondCycles(100);
        r.setDateTime({59,59,23,6,31,12,99});r.getDateTime();r.write8(addr(13),9);
        r.write8(addr(4),7);r.write8(addr(7),3);
        clock=100;eq("horloge avance banque1",r.getDateTime().year,0);
        eq("alarme ne suit pas heure",r.read8(addr(4)),7);
        for(int i : {9,12}) {r.write8(addr(i),15);eq("réservé zéro",r.read8(addr(i))&15,0);}
        std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);r.serialize(out);
        r.write8(addr(15),1);auto in=StateArchive::loader(bytes.data(),bytes.size());r.serialize(in);
        eq("archive valide",in.ok(),1);eq("archive alarme",r.read8(addr(4)),7);
        eq("archive date",r.getDateTime().year,0);
        r.write8(addr(13),1);clock+=400;eq("arrêt banque1",r.getDateTime().sec,0);
        r.write8(addr(13),9);clock+=100;eq("redémarrage banque1",r.getDateTime().sec,2);
    }
    for(int hour=0;hour<24;++hour) {
        Rtc r;r.write8(addr(13),9);r.write8(addr(10),0); // 12 h avant programmation
        r.setDateTime({59,59,hour,2,20,6,25});r.write8(addr(13),8);
        eq("12h unités",r.read8(addr(4)),hour%12%10);
        eq("12h dizaines PM",r.read8(addr(5)),hour%12/10+(hour>=12?2:0));
        eq("API reste24h",r.getDateTime().hour,hour);
        r.advanceSeconds(1);eq("retenue12h",r.getDateTime().hour,(hour+1)%24);
        // Programmation des deux nibbles : 10AM -> 09AM traverse19AM.
        r.setDateTime({0,0,10,2,20,6,25});r.write8(addr(4),9);r.write8(addr(5),0);
        eq("écriture digits indépendante",r.getDateTime().hour,9);
        r.write8(addr(4),0);r.write8(addr(5),3);
        eq("10PM en API",r.getDateTime().hour,22);
    }
    for(int sec : {0,29,30,59}) {
        int64_t clock=0;Rtc r;r.setClock([&]{return clock;});r.setSecondCycles(100);
        r.setDateTime({sec,59,23,6,31,12,99});r.getDateTime();clock=25;
        r.write8(addr(13),9);r.write8(addr(0),0xFF);eq("CLKOUT masque3bits",r.read8(addr(0)),7);
        r.write8(addr(1),1);const auto dt=r.getDateTime();
        eq("ADJ secondes",dt.sec,0);eq("ADJ minutes",dt.min,sec>=30?0:59);
        eq("ADJ année",dt.year,sec>=30?0:99);
        clock=124;eq("ADJ sousseconde avant",r.getDateTime().sec,0);
        clock=125;eq("ADJ sousseconde pile",r.getDateTime().sec,1);
        r.write8(addr(15),1);eq("reset conserve CLKOUT",r.read8(addr(0)),7);
    }
    for(int leap=0;leap<4;++leap) {
        Rtc r;r.setDateTime({59,59,23,2,28,2,25}); // année impaire, compteur indépendant
        r.write8(addr(13),9);r.write8(addr(11),leap);
        r.write8(addr(13),8);r.write8(addr(11),0);r.write8(addr(12),0); // année 00 ne recale pas leap
        r.advanceSeconds(1);eq("bissextile indépendant jour",r.getDateTime().day,leap==0?29:1);
        eq("bissextile indépendant mois",r.getDateTime().month,leap==0?2:3);
        r.setDateTime({59,59,23,6,31,12,99});r.write8(addr(13),9);r.write8(addr(11),leap);
        r.advanceSeconds(1);eq("compteur avance année",r.read8(addr(11)),(leap+1)%4);
    }
    {
        Rtc r;r.write8(addr(13),0xFF);eq("MODE réservé zéro",r.read8(addr(13))&15,13);
        r.write8(addr(0),5);r.write8(addr(10),0);r.setDateTime({0,0,22,0,1,1,26});r.write8(addr(11),3);
        std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);r.serialize(out);
        r.write8(addr(0),0);r.write8(addr(10),1);r.write8(addr(11),0);
        auto in=StateArchive::loader(bytes.data(),bytes.size());r.serialize(in);
        eq("archive contrôles valide",in.ok(),1);eq("archive CLKOUT",r.read8(addr(0)),5);
        eq("archive 12h",r.getDateTime().hour,22);eq("archive leap",r.read8(addr(11)),3);
        // Les13 registres banque1 précèdent la retenue du diviseur.
        bytes[bytes.size()-14+10]=2;
        auto bad=StateArchive::loader(bytes.data(),bytes.size());r.serialize(bad);
        eq("archive contrôle invalide rejeté",bad.ok(),0);
    }
    std::printf("[selftest-rtc-bank] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_RTC_BANK_TEST_MAIN
int main(){return testRtcBank()?1:0;}
#endif
