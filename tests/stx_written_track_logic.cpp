// WRITE TRACK STX sans secteur : vue vide active et persistance .wd1772.
// Registres WD1772 publics + Scheduler, sans accès aux internals du contrôleur.
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
        std::fprintf(stderr, "[stx-written-track] FAIL %s: got %lld, expected %lld\n",
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
    void advance(int64_t at) { live = at; sched.runTo(at); }
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

int testStxWrittenTrack() {
    Checks c;
    std::filesystem::path work;bool made=false;
    for(int n=0;n<1000&&!made;++n){work=std::filesystem::temp_directory_path()/("neost-written-track-"+std::to_string(n));std::error_code ec;made=std::filesystem::create_directory(work,ec);if(ec)break;}
    if(!made)return 1;
    struct Clean{std::filesystem::path p;~Clean(){std::error_code ec;std::filesystem::remove_all(p,ec);}} clean{work};
    const auto path=work/"empty.stx";
    std::vector<uint8_t> image={'R','S','Y',0,3,0,0,0,0,0,1,0,0,0,0,0};
    auto le16=[&](unsigned v){image.push_back(uint8_t(v));image.push_back(uint8_t(v>>8));};
    auto le32=[&](unsigned v){le16(v);le16(v>>16);};
    le32(16+512);le32(0);le16(1);le16(0);le16(6268*8);image.push_back(0);image.push_back(0);
    image.insert(image.end(),512,0xD0);
    FILE*f=std::fopen(path.string().c_str(),"wb");if(!f)return 1;
    const bool ok=std::fwrite(image.data(),1,image.size(),f)==image.size();std::fclose(f);if(!ok)return 1;
    {
        Rig r;r.fdc.setHostWriteBack(true);c.equal("montage",r.fdc.loadImage(path.string()),true);r.select(5);
        r.read(1);c.equal("lecture originale",r.finish(),true);c.equal("donnée originale",r.bus.ram[0x2000],0xD0);
        // Effacer toute la piste, sans aucun IDAM ni DAM.
        std::fill(r.bus.ram.begin()+0x2000,r.bus.ram.begin()+0x3A00,0x4E);
        r.mode(0x190);r.data(13);
        r.fdc.write8(0xFF860D,0);r.fdc.write8(0xFF860B,0x20);r.fdc.write8(0xFF8609,0);
        r.mode(0x180);r.data(0xF8);c.equal("WRITE TRACK terminé",r.finish(),true);
        c.equal("WRITE TRACK sans erreur",r.reg(0)&0x59,0);
        std::fill(r.bus.ram.begin()+0x2000,r.bus.ram.begin()+0x2200,0xCC);
        r.read(1);c.equal("lecture après effacement termine",r.finish(),true);
        c.equal("secteur effacé RNF",r.reg(0)&0x11,0x10);
        c.equal("pas de DMA après effacement",r.address(),0x2000);
        c.equal("RAM inchangée après RNF",r.bus.ram[0x2000],0xCC);
    }
    {
        Rig r;c.equal("remontage overlay",r.fdc.loadImage(path.string()),true);r.select(5);
        std::fill(r.bus.ram.begin()+0x2000,r.bus.ram.begin()+0x2200,0xCC);
        r.read(1);c.equal("lecture persistée termine",r.finish(),true);
        c.equal("effacement persisté RNF",r.reg(0)&0x11,0x10);
        c.equal("pas de DMA persistée",r.address(),0x2000);
        c.equal("RAM persistée inchangée",r.bus.ram[0x2000],0xCC);
    }
    std::fprintf(stderr,"[stx-written-track] %d checks, %d FAIL\n",c.count,c.failures);return c.failures;
}
#ifdef NEOST_STX_WRITTEN_TRACK_TEST_MAIN
int main(){return testStxWrittenTrack()?1:0;}
#endif
