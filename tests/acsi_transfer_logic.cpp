// ACSI/DMA : protocole des registres et datation de l'IRQ, sans ROM ni CPU.
// Référence : Hatari hdc.c Acsi_DmaTransfer / HDC_ACSI_InterruptHandler_Update.
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"
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
    void expect(const char* label, int64_t actual, int64_t expected) {
        ++count;
        if (actual == expected) return;
        ++failures;
        std::printf("  FAIL ACSI %-43s = %lld (attendu %lld)\n", label,
                    (long long)actual, (long long)expected);
    }
};
struct Rig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    Rig() {
        fdc.setScheduler(&sched);
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        sched.setCallback(Scheduler::ACSI_IRQ, [this] { fdc.onAcsiIrq(); });
        sched.setLiveClock([this] { return live; });
        reset();
    }
    void reset() { fdc.reset(true); mfp.reset(); sched.reset(); live = 0; }
    void advance(int64_t cycle) { live = cycle; sched.runTo(cycle); }
    void mode(uint16_t v) {
        fdc.write8(0xFF8606, uint8_t(v >> 8)); fdc.write8(0xFF8607, uint8_t(v));
    }
    void data(uint8_t v) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, v); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    void ack() { mode(0x80); (void)fdc.read8(0xFF8605); }
    void prepare(uint8_t opcode = 8, bool write = false, bool dmaEnabled = false) {
        mode(write ? 0x190 : 0x90); data(1);
        fdc.write8(0xFF8609, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF860D, 0);
        const uint16_t commandMode = (write ? 0x108 : 8) | (dmaEnabled ? 0 : 0x80);
        mode(commandMode); data(opcode);
        mode(commandMode | 2);
        for (uint8_t v : {uint8_t(0), uint8_t(0), uint8_t(1), uint8_t(1), uint8_t(0)}) data(v);
    }
    void start(bool write = false) { mode(write ? 0x100 : 0); }
    uint32_t address() {
        return (uint32_t(fdc.read8(0xFF8609)) << 16)
             | (uint32_t(fdc.read8(0xFF860B)) << 8) | fdc.read8(0xFF860D);
    }
};
}

int testAcsiTransfer() {
    Checks c;
    std::filesystem::path work;bool made=false;
    for(int n=0;n<1000&&!made;++n){work=std::filesystem::temp_directory_path()/("neost-acsi-transfer-"+std::to_string(n));std::error_code ec;made=std::filesystem::create_directory(work,ec);if(ec)break;}
    if(!made)return 1;
    struct Clean{std::filesystem::path p;~Clean(){std::error_code ec;std::filesystem::remove_all(p,ec);}} clean{work};
    const auto path=work/"disk.img";
    std::vector<uint8_t> image(2048,0xA5);FILE*f=std::fopen(path.string().c_str(),"wb");if(!f)return 1;
    const bool ok=std::fwrite(image.data(),1,image.size(),f)==image.size();std::fclose(f);if(!ok)return 1;
    for(bool write:{false,true}) {
        Rig r;c.expect("montage",r.fdc.mountAcsi(path.string()),true);
        r.prepare(write?0x0A:0x08,write);
        const uint32_t start=uint32_t(r.bus.ram.size()-256);
        r.fdc.write8(0xFF860D,uint8_t(start));r.fdc.write8(0xFF860B,uint8_t(start>>8));r.fdc.write8(0xFF8609,uint8_t(start>>16));
        std::fill(r.bus.ram.end()-256,r.bus.ram.end(),0xCC);
        r.start(write);
        c.expect("DMA erreur bit0",r.fdc.read8(0xFF8607)&1,0);
        c.expect("adresse avance longueur entière",r.address(),start+512);
        c.expect("pas de copie partielle",r.bus.ram.back(),0xCC);
        c.expect("IRQ différée",r.irq(),false);
        r.advance(1000);c.expect("IRQ fin erreur",r.irq(),true);
        r.mode(write?0x188:0x88);
        c.expect("READ échoué CHECK CONDITION, WRITE inchangé",r.fdc.read8(0xFF8605),write?0:2);
        // REQUEST SENSE rapporte l'état du périphérique, pas l'adresse mémoire
        // invalide du DMA côté hôte : comme Acsi_DmaTransfer d'Hatari.
        r.mode(0x88);r.data(3);r.mode(0x8A);
        for(uint8_t v:{0,0,0,4,0})r.data(v);
        // Le transfert précédent a décompté le compteur DMA à zéro : comme un vrai
        // pilote, reprogrammer un bloc avant d'ouvrir le DMA du REQUEST SENSE.
        r.mode(0x90);r.data(1);
        r.fdc.write8(0xFF860D,0);r.fdc.write8(0xFF860B,0x20);r.fdc.write8(0xFF8609,0);
        r.start();r.advance(2000);
        c.expect("REQUEST SENSE sans erreur disque",r.bus.ram[0x2000]&0x7F,0);
        c.expect("nouvelle commande réarme statut DMA",r.fdc.read8(0xFF8607)&1,1);
    }
    std::fprintf(stderr,"[acsi-transfer] %d checks, %d FAIL\n",c.count,c.failures);return c.failures;
}
#ifdef NEOST_ACSI_TRANSFER_TEST_MAIN
int main(){return testAcsiTransfer()?1:0;}
#endif
