// GEMDOS Fsfirst/Fsnext publics : recherches entrelacées et identité DTA.
// Primaire : Hatari gemdos.c GemDOS_SFirst / GemDOS_SNext.
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "io/GemdosHd.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct Rig {
    Bus bus{1024u*1024u};
    Cpu68k cpu{bus};
    GemdosHd gemdos{bus,cpu};
    bool intercepted = false;

    explicit Rig(const std::string& directory) {
        bus.cpu = &cpu;
        cpu.setSr(0x2700);
        cpu.setReg(15, 0x1000);
        gemdos.setDirectory(directory);
        bus.write32(0x4F2,0x2000); // osheader -> act_pd -> basepage -> DTA
        bus.write32(0x2028,0x2200);
        bus.write32(0x2200,0x2400);
        gemdos.handleOpcode(10);
    }
    void dta(uint32_t address) { bus.write32(0x2420,address); }
    int trap(uint16_t call) {
        bus.write16(0x1000,0x2700);
        bus.write32(0x1002,0x2000);
        bus.write16(0x1006,call);
        cpu.setSr(0x2700);
        gemdos.handleOpcode(8);
        intercepted = (cpu.sr() & 4) != 0;
        return int32_t(cpu.reg(0));
    }
    int first(uint32_t address,const std::string& name) {
        dta(address);
        for(size_t i=0;i<=name.size();++i)
            bus.write8(0x3000+uint32_t(i),i<name.size()?uint8_t(name[i]):0);
        bus.write32(0x1008,0x3000);
        bus.write16(0x100C,0);
        return trap(0x4E);
    }
    int next(uint32_t address) { dta(address); return trap(0x4F); }
    std::string name(uint32_t address) {
        std::string result;
        for(unsigned i=0;i<14;++i) {
            const char value=char(bus.read8(address+30+i));
            if(!value) break;
            result.push_back(value);
        }
        return result;
    }
    void copy(uint32_t from,uint32_t to) {
        for(unsigned i=0;i<44;++i) bus.write8(to+i,bus.read8(from+i));
    }
};
}

int testGemdosDtaReuse() {
    int checks=0,failures=0;
    auto equal=[&](const char* label,int64_t got,int64_t wanted) {
        ++checks;
        if(got==wanted) return;
        ++failures;
        std::fprintf(stderr,"[gemdos-dta-reuse] FAIL %s: %lld != %lld\n",
                     label,(long long)got,(long long)wanted);
    };
    auto named=[&](Rig& rig,uint32_t address,const char* wanted) {
        equal("nom DTA attendu",rig.name(address)==wanted,true);
    };
    std::filesystem::path work;
    bool made=false;
    for(int n=0;n<1000&&!made;++n) {
        work=std::filesystem::temp_directory_path()/("neost-dta-reuse-"+std::to_string(n));
        std::error_code ec;
        made=std::filesystem::create_directory(work,ec);
        if(ec) break;
    }
    if(!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}
    } cleanup{work};
    for(const char* name:{"A1.TXT","A2.TXT","A3.TXT","B1.TXT","B2.TXT","B3.TXT"})
        std::ofstream(work/name)<<name;

    {
        // .state forgé : cache DTA vidé (count = 0) avec un lecteur monté. Fsfirst
        // indexe dtas_[dtaIndex_] avant toute croissance → refus au chargement.
        Rig fresh(work.string());
        std::vector<uint8_t> bytes;
        auto save=StateArchive::saver(bytes); fresh.gemdos.serialize(save);
        constexpr size_t tail=4+256+2;            // [u32 256][256 × used=0][u16 index]
        bool shaped=bytes.size()>tail;
        if(shaped) { uint32_t n=0; std::memcpy(&n,bytes.data()+bytes.size()-tail,4); shaped=(n==256); }
        equal("queue DTA reconnue",shaped,true);
        if(shaped) {
            bytes.resize(bytes.size()-tail);
            const uint8_t empty[6]={0,0,0,0,0,0};   // count = 0, index = 0
            bytes.insert(bytes.end(),empty,empty+6);
            auto load=StateArchive::loader(bytes.data(),bytes.size()); fresh.gemdos.serialize(load);
            equal("cache DTA vide refusé avec lecteur monté",load.ok(),false);
        }
    }
    Rig rig(work.string());
    constexpr uint32_t a=0x4000,b=0x4100,c=0x4200;
    equal("Fsfirst A",rig.first(a,"C:\\A*.TXT"),0); named(rig,a,"A1.TXT");
    equal("Fsfirst B",rig.first(b,"C:\\B*.TXT"),0); named(rig,b,"B1.TXT");
    equal("Fsnext A indépendant",rig.next(a),0); named(rig,a,"A2.TXT");
    equal("Fsnext B indépendant",rig.next(b),0); named(rig,b,"B2.TXT");
    equal("réutilisation même adresse",rig.first(a,"C:\\A*.TXT"),0); named(rig,a,"A1.TXT");
    equal("autre DTA survit réutilisation",rig.next(b),0); named(rig,b,"B3.TXT");

    // Fsfirst voit un en-tête copié, mais ne doit pas remplacer la recherche A.
    rig.copy(a,c);
    equal("Fsfirst adresse différente réalloue",rig.first(c,"C:\\B*.TXT"),0);
    equal("ancienne recherche intacte",rig.next(a),0); named(rig,a,"A2.TXT");
    equal("nouvelle recherche indépendante",rig.next(c),0); named(rig,c,"B2.TXT");

    // Fsnext accepte une copie du jeton de recherche, comme Hatari : seule
    // Fsfirst réalloue lorsque l'adresse change, Fsnext partage le curseur.
    rig.copy(c,b);
    equal("Fsnext DTA copiée partage curseur",rig.next(b),0); named(rig,b,"B3.TXT");
    equal("curseur original également épuisé",rig.next(c),-49);

    // Dépasser le cache initial ne doit ni perdre les premières recherches ni
    // reprendre une entrée d'une autre DTA (croissance 256 vers 512 côté hôte).
    for(unsigned n=0;n<260;++n)
        equal("Fsfirst nombreuses DTA",rig.first(0x10000+64*n,"C:\\A*.TXT"),0);
    equal("première DTA survit croissance",rig.next(a),0); named(rig,a,"A3.TXT");
    equal("dernière DTA indépendante",rig.next(0x10000+64*259),0);
    named(rig,0x10000+64*259,"A2.TXT");
    equal("DTA épuisée",rig.next(a),-49);
    equal("ancien TOS vide nom épuisé",rig.name(a).empty(),true);

    // Index forgé avec vraie magic : rejet avant accès au cache.
    rig.copy(c,a);
    rig.bus.write16(a,0xFFFF);
    equal("index hors bornes",rig.next(a),-49);
    equal("index hors bornes intercepté",rig.intercepted,true);
    rig.bus.write16(a,500); // cache agrandi mais case jamais allouée
    equal("index inutilisé",rig.next(a),-49);
    rig.bus.write32(a+2,0);
    rig.next(a);
    equal("magic étrangère retourne au TOS",rig.intercepted,false);
    equal("DTA partiellement hors RAM",rig.next(uint32_t(rig.bus.ram.size()-20)),-65);
    {
        // La disparition normale d'une entrée est SKIP, mais ENOTDIR est une
        // vraie erreur de parcours. Le changement hôte est portable et ne
        // dépend ni des permissions ni des liens symboliques.
        const auto search = work / "SEARCH";
        std::filesystem::create_directory(search);
        std::ofstream(search / "A.TXT") << "A";
        std::ofstream(search / "B.TXT") << "B";
        equal("recherche avant ENOTDIR",rig.first(c,"C:\\SEARCH\\*.TXT"),0);
        named(rig,c,"A.TXT");
        std::filesystem::rename(search,work / "OLD");
        std::ofstream(search) << "remplacement du dossier par un fichier";
        equal("erreur stat ENOTDIR devient EINTRN",rig.next(c),-65);
        equal("erreur stat interceptée",rig.intercepted,true);
    }
    {
        const auto missing = work / "MISSING";
        std::filesystem::create_directory(missing);
        for(const char* name:{"A.TXT","B.TXT","C.TXT"}) std::ofstream(missing/name)<<name;
        equal("recherche avant disparition",rig.first(c,"C:\\MISSING\\*.TXT"),0);
        named(rig,c,"A.TXT");
        std::filesystem::remove(missing/"B.TXT");
        equal("ENOENT saute entrée disparue",rig.next(c),0);
        named(rig,c,"C.TXT");
    }
    std::fprintf(stderr,"[gemdos-dta-reuse] %d checks, %d FAIL\n",checks,failures);
    return failures;
}
#ifdef NEOST_GEMDOS_DTA_REUSE_TEST_MAIN
int main(){return testGemdosDtaReuse()?1:0;}
#endif
