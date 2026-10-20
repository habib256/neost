// Sélection PSG et index avant accès WD1772 : registres publics + Scheduler.
// Références Hatari : psg.c:FDC_SetDriveSide, fdc.c:FDC_UpdateAll et accès $8604.
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[fdc-media-change] FAIL %s: got %lld, expected %lld\n",
                     label, static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct Rig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    int pulses = 0;
    Rig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(false);
        fdc.setSoundSink([this](FdcSound sound) { if (sound == FdcSound::Index) ++pulses; });
        // Même connexion que Machine::Machine ; aucun accès FDC dans select().
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
        mfp.write8(0xFFFA09, 0x80); // capturer le front GPIP5 dans IPRB, même après clear IRQ
    }
    void select(uint8_t value) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, value); }
    void mode(uint16_t value) {
        fdc.write8(0xFF8606, uint8_t(value >> 8)); fdc.write8(0xFF8607, uint8_t(value));
    }
    void data(uint8_t value) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, value); }
    uint8_t reg(uint16_t addressBits) { mode(0x80 | addressBits); return fdc.read8(0xFF8605); }
    void reg(uint16_t addressBits, uint8_t value) { mode(0x80 | addressBits); data(value); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    bool pending() { return mfp.read8(0xFFFA0D) & 0x80; }
    void advance(int64_t at) {
        // L'ancien banc utilisait une trame synthétique de 1000 cycles.
        // Chaque frontière appelle maintenant explicitement la broche VBL.
        for (int64_t boundary = (live / 1000 + 1) * 1000; boundary <= at; boundary += 1000) {
            live = boundary;
            sched.runTo(boundary);
            fdc.onVbl();
        }
        live = at;
        sched.runTo(at);
    }
    bool next() {
        const int64_t due = sched.nextDue();
        if (due < 0) return false;
        advance(due); return true;
    }
    bool finish() {
        // Ne pas lire STR : cela masquerait une sélection seulement PULL.
        for (unsigned n = 0; n < 100000 && !irq(); ++n) if (!next()) return false;
        return irq();
    }
    void read(uint8_t sector = 2, uint8_t command = 0x88, uint8_t count = 1) {
        mode(0x90); data(count);
        fdc.write8(0xFF860D, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF8609, 0);
        reg(4, sector); reg(0, command);
    }
    uint32_t address() {
        return (uint32_t(fdc.read8(0xFF8609)) << 16)
             | (uint32_t(fdc.read8(0xFF860B)) << 8) | fdc.read8(0xFF860D);
    }
    int64_t armIndex() {
        select(5); reg(0, 0xD4); next();
        // Le premier handler initialise la phase puis attend précisément l'index.
        return sched.nextDue();
    }
};
}

int testFdcMediaChange() {
    Checks c;std::filesystem::path work;bool made=false;
    for(int n=0;n<1000&&!made;++n){work=std::filesystem::temp_directory_path()/("neost-media-change-"+std::to_string(n));std::error_code ec;made=std::filesystem::create_directory(work,ec);if(ec)break;}
    if(!made)return 1;struct Clean{std::filesystem::path p;~Clean(){std::error_code ec;std::filesystem::remove_all(p,ec);}} clean{work};
    const auto path=work/"disk.st",ro=work/"readonly.st";
    std::vector<uint8_t> image(2048,0);image[0x13]=4;image[0x18]=2;image[0x1A]=2;
    for(auto p:{path,ro}){FILE*f=std::fopen(p.string().c_str(),"wb");if(!f)return 1;bool ok=std::fwrite(image.data(),1,image.size(),f)==image.size();std::fclose(f);if(!ok)return 1;}
    std::filesystem::permissions(ro,std::filesystem::perms::owner_read);
    auto wprt=[](Rig&r,int drive){r.select(drive?3:5);r.reg(0,0xD0);return (r.reg(0)&0x40)!=0;};
    for(int drive:{0,1}) {
        Rig r;r.fdc.loadImage(path.string(),drive);
        r.advance(20000);c.equal("insertion initiale writable",wprt(r,drive),false);
        r.fdc.eject(drive);r.advance(25000);r.fdc.loadImage(path.string(),drive);
        c.equal("réinsertion garde phase éjection",wprt(r,drive),true);
        std::vector<uint8_t> state;{auto ar=StateArchive::saver(state);r.sched.serialize(ar);r.fdc.serialize(ar);}
        r.advance(37999);c.equal("éjection tient18VBL",wprt(r,drive),true);
        r.advance(38000);c.equal("phase insertion restaure WPRT",wprt(r,drive),false);
        // Pendant State2, une nouvelle éjection démarre immédiatement State1.
        r.advance(40000);r.fdc.eject(drive);r.advance(41000);r.fdc.loadImage(path.string(),drive);
        c.equal("nouvelle action pendant State2",wprt(r,drive),true);
        r.advance(57999);c.equal("nouvelle phase18VBL",wprt(r,drive),true);
        r.advance(58000);c.equal("nouvelle insertion",wprt(r,drive),false);
        {auto ar=StateArchive::loader(state.data(),state.size());r.sched.serialize(ar);r.fdc.serialize(ar);c.equal("snapshot phases",ar.ok(),true);}
        r.live=25000;r.advance(37999);c.equal("snapshot garde phase1",wprt(r,drive),true);
        r.advance(38000);c.equal("snapshot transition phase2",wprt(r,drive),false);
    }
    {
        Rig r;r.fdc.loadImage(path.string(),0);r.fdc.loadImage(path.string(),1);r.advance(20000);
        r.fdc.eject(0);r.advance(25000);r.fdc.loadImage(path.string(),0);
        c.equal("autre lecteur indépendant",wprt(r,1),false);
        r.fdc.loadImage(ro.string(),0);r.advance(60000);
        c.equal("phase insertion ne force pas WPRT zéro",wprt(r,0),true);
    }
    {
        Rig r;r.fdc.loadImage(path.string());r.advance(20000);
        r.fdc.eject();r.advance(21000);r.fdc.loadImage(path.string());
        r.advance(22000);r.fdc.eject();r.advance(23000);r.fdc.loadImage(path.string());
        c.equal("actions rapides gardent première échéance",wprt(r,0),true);
        r.advance(38000);c.equal("actions rapides dernière insertion",wprt(r,0),false);
        // Swap direct : mêmes deux actions qu'eject puis insert.
        r.advance(60000);r.fdc.loadImage(path.string());
        c.equal("swap direct force WPRT",wprt(r,0),true);
        r.advance(78000);c.equal("swap direct finit première phase",wprt(r,0),false);
        r.advance(100000);r.fdc.eject();r.advance(101000);r.fdc.loadImage(path.string());
        c.equal("phase forcée avant écriture",wprt(r,0),true);
        std::fill(r.bus.ram.begin()+0x2000,r.bus.ram.begin()+0x2200,0xCC);
        r.reg(4,2);r.mode(0x190);r.data(1);
        r.fdc.write8(0xFF860D,0);r.fdc.write8(0xFF860B,0x20);r.fdc.write8(0xFF8609,0);
        r.mode(0x180);r.data(0xA8);
        c.equal("écriture pendant WPRT synthétique",r.finish(),true);
        c.equal("signal WPRT ne protège pas média",r.reg(0)&0x59,0);
        r.read(2);c.equal("relecture écriture",r.finish(),true);
        c.equal("donnée écrite",r.bus.ram[0x2000],0xCC);
    }
    {
        // WD177x type II : WPRT testé APRÈS la montée en régime (6 index, h=0) et le
        // délai E, pas avant le moteur (MAME wd_fdc.cpp SETTLE_DONE ; Hatari faute
        // dès le premier événement). Type III : avant l'attente de l'index.
        Rig r;r.fdc.loadImage(ro.string(),0);r.select(5);
        std::fill(r.bus.ram.begin()+0x2000,r.bus.ram.begin()+0x2200,0xCC);
        r.reg(4,1);r.mode(0x190);r.data(1);
        r.fdc.write8(0xFF860D,0);r.fdc.write8(0xFF860B,0x20);r.fdc.write8(0xFF8609,0);
        r.mode(0x180);r.data(0xA4);                       // WRITE SECTOR, h=0, E=1
        r.advance(r.live+100000);
        c.equal("WRITE SECTOR protégé : pas de faute immédiate",r.irq(),false);
        c.equal("WRITE SECTOR protégé : moteur lancé",r.reg(0)&0xC1,0x81);
        c.equal("WRITE SECTOR protégé termine",r.finish(),true);
        c.equal("WRITE SECTOR protégé après montée en régime",r.live>=5*1600000,true);
        c.equal("WRITE SECTOR protégé : WPRT seul",r.reg(0)&0x5D,0x40);
        const int64_t t0=r.live;                          // moteur encore lancé
        r.mode(0x180);r.data(0xF4);                       // WRITE TRACK, E=1 (15 ms)
        c.equal("WRITE TRACK protégé termine",r.finish(),true);
        c.equal("WRITE TRACK protégé : délai E sans attente d'index",
                r.live-t0>=100000&&r.live-t0<=140000,true);
        c.equal("WRITE TRACK protégé : WPRT",r.reg(0)&0x44,0x40);
    }
    std::fprintf(stderr,"[fdc-media-change] %d checks, %d FAIL\n",c.count,c.failures);return c.failures;
}
#ifdef NEOST_FDC_MEDIA_CHANGE_TEST_MAIN
int main(){return testFdcMediaChange()?1:0;}
#endif
