// GEMDOS hôte : état par processus et restes du banc A/B disquette contre dossier
// (2026-11-11). Chaque cas reprend un écart mesuré en exécutant le MÊME programme sous
// TOS 1.04, 2.06 et EmuTOS depuis une disquette (vrai GEMDOS) puis depuis --gemdos.
// [C] EmuTOS bdos/bdosdefs.h, proc.c, fsopnclo.c @ 5679730f pour le modèle.
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "io/GemdosHd.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
constexpr uint32_t ACT_PD = 0x2200, PARENT = 0x2400, CHILD = 0x2600;
constexpr uint32_t NAME = 0x3000, BUF = 0x3100;

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
        bus.write32(0x4F2,0x2000);          // osheader → act_pd → basepage courante
        bus.write32(0x2028,ACT_PD);
        bus.write32(ACT_PD,PARENT);
        gemdos.handleOpcode(10);
        basepage(PARENT, 0, 0x8000);
        basepage(CHILD, PARENT, 0x9000);
    }
    void basepage(uint32_t bp, uint32_t parent, uint32_t tbase) {
        bus.write32(bp+0x08,tbase);
        bus.write32(bp+0x20,0x4000);        // DTA
        bus.write32(bp+0x24,parent);
        bus.write8(bp+0x37,2);              // p_curdrv = C:
    }
    void run(uint32_t bp) { bus.write32(ACT_PD,bp); }
    void str(const std::string& s) {
        for(size_t i=0;i<=s.size();++i) bus.write8(NAME+uint32_t(i),i<s.size()?uint8_t(s[i]):0);
    }
    int trap(uint16_t call) {
        bus.write16(0x1000,0x2700);
        bus.write32(0x1002,0x2000);
        bus.write16(0x1006,call);
        cpu.setSr(0x2700);
        gemdos.handleOpcode(8);
        intercepted = (cpu.sr() & 4) != 0;
        return int32_t(cpu.reg(0));
    }
    int named(uint16_t call,const std::string& s,uint16_t w1=0,uint16_t w2=0) {
        str(s); bus.write32(0x1008,NAME); bus.write16(0x100C,w1); bus.write16(0x100E,w2);
        return trap(call);
    }
    int dsetpath(const std::string& s) { return named(0x3B,s); }
    std::string dgetpath(uint16_t drive=0) {
        bus.write8(BUF,0x7F);
        bus.write32(0x1008,BUF); bus.write16(0x100C,drive);
        if(trap(0x47)!=0||!intercepted) return "<tos>";
        std::string r;
        for(uint32_t i=0;i<64;++i){ const char c=char(bus.read8(BUF+i)); if(!c) break; r.push_back(c); }
        return r;
    }
    int handleOp(uint16_t call,uint16_t h) { bus.write16(0x1008,h); return trap(call); }
    int write(uint16_t h,const char* s) {
        const uint32_t n=uint32_t(std::strlen(s));
        for(uint32_t i=0;i<n;++i) bus.write8(BUF+i,uint8_t(s[i]));
        bus.write16(0x1008,h); bus.write32(0x100A,n); bus.write32(0x100E,BUF);
        return trap(0x40);
    }
    int readByte(uint16_t h) {
        bus.write16(0x1008,h); bus.write32(0x100A,1); bus.write32(0x100E,BUF);
        return trap(0x3F)==1 ? bus.read8(BUF) : -1;
    }
    int fdatime(uint16_t h,uint16_t time,uint16_t date,uint16_t flag) {
        bus.write16(BUF,time); bus.write16(BUF+2,date);
        bus.write32(0x1008,BUF); bus.write16(0x100C,h); bus.write16(0x100E,flag);
        return trap(0x57);
    }
    std::string dtaName() {
        std::string r;
        for(uint32_t i=0;i<14;++i){ const char c=char(bus.read8(0x4000+30+i)); if(!c) break; r.push_back(c); }
        return r;
    }
};
}

int testGemdosProcess() {
    int checks=0,failures=0;
    auto equal=[&](const char* label,int64_t got,int64_t wanted) {
        ++checks;
        if(got==wanted) return;
        ++failures;
        std::fprintf(stderr,"[gemdos-process] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)wanted);
    };
    auto same=[&](const char* label,const std::string& got,const std::string& wanted) {
        ++checks;
        if(got==wanted) return;
        ++failures;
        std::fprintf(stderr,"[gemdos-process] FAIL %s: '%s' != '%s'\n",label,got.c_str(),wanted.c_str());
    };
    std::filesystem::path work;
    bool made=false;
    for(int n=0;n<1000&&!made;++n) {
        work=std::filesystem::temp_directory_path()/("neost-gemdos-process-"+std::to_string(n));
        std::error_code ec;
        made=std::filesystem::create_directory(work,ec);
        if(ec) break;
    }
    if(!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}
    } cleanup{work};
    std::filesystem::create_directory(work/"SUB");
    std::filesystem::create_directory(work/"LongDirectoryName");
    std::ofstream(work/"SUB"/"IN.TXT")<<"i";
    std::ofstream(work/"LongFileName1.txt")<<"1";
    std::ofstream(work/"LongFileName2.txt")<<"2";

    Rig rig(work.string());

    // Dossier courant par processus : l'enfant hérite, son Dsetpath ne remonte pas.
    equal("parent Dsetpath",rig.dsetpath("\\SUB"),0);
    same("parent Dgetpath",rig.dgetpath(),"\\SUB");
    rig.run(CHILD);
    same("enfant hérite",rig.dgetpath(),"\\SUB");
    equal("enfant Dsetpath",rig.dsetpath("\\"),0);
    same("enfant à la racine",rig.dgetpath(),"");
    rig.trap(0x00);                                    // Pterm0 de l'enfant
    rig.run(PARENT);
    same("parent inchangé",rig.dgetpath(),"\\SUB");
    // « X:NOM » part du dossier courant de X ; « X: » seul ne change rien.
    const int in=rig.named(0x3D,"C:IN.TXT");
    equal("C:IN.TXT relatif au dossier courant",in>=64,true);
    rig.handleOp(0x3E,uint16_t(in));
    equal("Dsetpath C: sans effet",rig.dsetpath("C:"),0);
    same("toujours \\SUB",rig.dgetpath(),"\\SUB");

    // Lecteur courant lu dans p_curdrv : un enfant qui passe sur A: ne change rien au parent.
    rig.run(CHILD);
    rig.bus.write8(CHILD+0x37,0);
    same("enfant sur A: → TOS",rig.dgetpath(),"<tos>");
    rig.trap(0x00);
    rig.run(PARENT);
    same("parent toujours sur C:",rig.dgetpath(),"\\SUB");
    equal("retour racine",rig.dsetpath("\\"),0);

    // Basepage réattribuée sans Pterm vu : l'entrée périmée ne fuit pas.
    rig.bus.write8(CHILD+0x37,2);
    rig.run(CHILD);
    equal("enfant 2 Dsetpath",rig.dsetpath("\\SUB"),0);
    rig.bus.write32(CHILD+0x08,0xA000);                // autre programme, même basepage
    rig.run(CHILD);
    same("basepage réutilisée repart du parent",rig.dgetpath(),"");
    rig.run(PARENT);

    // Noms 8.3 sans collision : le premier garde le nom tronqué, le second un alias.
    rig.bus.write32(0x1008,NAME); rig.str("C:\\LONG*.TXT"); rig.bus.write16(0x100C,0);
    equal("Fsfirst noms longs",rig.trap(0x4E),0);
    same("premier nom tronqué",rig.dtaName(),"LONGFILE.TXT");
    equal("Fsnext",rig.trap(0x4F),0);
    same("second sous alias",rig.dtaName(),"LONGFI~1.TXT");
    const int alias=rig.named(0x3D,"LONGFI~1.TXT");
    equal("Fopen par alias",alias>=64,true);
    equal("l'alias désigne le second fichier",rig.readByte(uint16_t(alias)),'2');
    rig.handleOp(0x3E,uint16_t(alias));
    const int plain=rig.named(0x3D,"LONGFILE.TXT");
    equal("le nom tronqué désigne le premier",rig.readByte(uint16_t(plain)),'1');
    rig.handleOp(0x3E,uint16_t(plain));
    equal("Dsetpath nom long tronqué",rig.dsetpath("LONGDIRE"),0);
    same("Dgetpath rend le nom 8.3",rig.dgetpath(),"\\LONGDIRE");
    rig.dsetpath("\\");

    // Fdatime posé avant un Fwrite : gardé à la fermeture.
    const int d=rig.named(0x3C,"D.TXT");
    equal("Fcreate",d>=64,true);
    equal("Fdatime posé",rig.fdatime(uint16_t(d),0x6000,0x1421,1),0);
    equal("Fwrite après",rig.write(uint16_t(d),"xyz"),3);
    equal("Fdatime relu",rig.fdatime(uint16_t(d),0,0,0),0);
    equal("heure relue",rig.bus.read16(BUF),0x6000);
    rig.handleOp(0x3E,uint16_t(d));
    equal("Fsfirst D.TXT",rig.named(0x4E,"D.TXT"),0);
    equal("date gardée",rig.bus.read16(0x4000+24),0x1421);
    equal("heure gardée",rig.bus.read16(0x4000+22),0x6000);

    // Attributs : archive au Fcreate, caché et système retenus.
    equal("archive après Fcreate",rig.named(0x43,"D.TXT",0,0),0x20);
    equal("Fattrib caché",rig.named(0x43,"D.TXT",1,0x02),0x02);
    equal("caché relu",rig.named(0x43,"D.TXT",0,0),0x02);
    equal("caché seul invisible au masque 0",rig.named(0x4E,"D.TXT",0),-33);
    equal("caché visible au masque caché",rig.named(0x4E,"D.TXT",0x02),0);
    rig.named(0x43,"D.TXT",1,0x22);
    equal("caché+archive listé au masque 0 (règle du TOS)",rig.named(0x4E,"D.TXT",0),0);
    equal("Fdelete",rig.named(0x41,"D.TXT"),0);

    // Fforce vers un handle du TOS : la redirection hôte tombe.
    const int o=rig.named(0x3C,"O.TXT");
    rig.bus.write16(0x1008,1); rig.bus.write16(0x100A,uint16_t(o));
    equal("Fforce(1, fichier hôte)",rig.trap(0x46),0);
    equal("Fwrite(1) part dans le fichier",rig.write(1,"ab"),2);
    equal("intercepté",rig.intercepted,true);
    rig.bus.write16(0x1008,1); rig.bus.write16(0x100A,6);   // handle sauvé par Fdup(1)
    rig.trap(0x46);
    equal("Fforce(1, TOS) rendu au TOS",rig.intercepted,false);
    rig.write(1,"cd");
    equal("Fwrite(1) au TOS ensuite",rig.intercepted,false);
    rig.handleOp(0x3E,uint16_t(o));

    std::fprintf(stderr,"[gemdos-process] %d checks, %d FAIL\n",checks,failures);
    return failures;
}
#ifdef NEOST_GEMDOS_PROCESS_TEST_MAIN
int main(){return testGemdosProcess()?1:0;}
#endif
