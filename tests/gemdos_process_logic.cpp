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
    int frename(const std::string& from,const std::string& to) {
        str(from);
        for(size_t i=0;i<=to.size();++i) bus.write8(NAME+0x80+uint32_t(i),i<to.size()?uint8_t(to[i]):0);
        bus.write16(0x1008,0); bus.write32(0x100A,NAME); bus.write32(0x100E,NAME+0x80);
        return trap(0x56);
    }
    int fforce(uint16_t stdh,uint16_t h) { bus.write16(0x1008,stdh); bus.write16(0x100A,h); return trap(0x46); }
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

    // --- Restes du banc A/B, 2026-11-11 (TOS 1.04, 1.62, 2.06 et EmuTOS exécutés) ---
    // Fopen et Fdelete d'un dossier : EFILNF (recherche FA_NORM), pas EACCDN.
    equal("Fopen dossier",rig.named(0x3D,"SUB"),-33);
    equal("Fdelete dossier",rig.named(0x41,"SUB"),-33);
    // Dsetpath avec un joker : EPTHNF, dossier courant inchangé.
    equal("Dsetpath SUB\\*.*",rig.dsetpath("SUB\\*.*"),-34);
    equal("Dsetpath *.*",rig.dsetpath("*.*"),-34);
    same("dossier inchangé",rig.dgetpath(),"");
    // Fdelete d'un fichier ouvert : par son propriétaire, il part et Fclose rend EINTRN ;
    // par un autre processus, EACCDN et rien n'est détruit.
    const int del=rig.named(0x3C,"DEL.TXT");
    equal("Fdelete ouvert par soi",rig.named(0x41,"DEL.TXT"),0);
    equal("fichier détruit",std::filesystem::exists(work/"DEL.TXT"),false);
    equal("Fwrite après destruction",rig.write(uint16_t(del),"x"),-65);
    equal("Fclose après destruction",rig.handleOp(0x3E,uint16_t(del)),-65);
    rig.handleOp(0x3E,uint16_t(del));
    equal("handle libéré (second Fclose au TOS)",rig.intercepted,false);
    const int keep=rig.named(0x3C,"KEEP.TXT");
    rig.run(CHILD);
    equal("Fdelete ouvert par un autre",rig.named(0x41,"KEEP.TXT"),-36);
    equal("Fcreate ouvert par un autre",rig.named(0x3C,"KEEP.TXT"),-36);
    rig.trap(0x00);
    rig.run(PARENT);
    equal("toujours là",std::filesystem::exists(work/"KEEP.TXT"),true);
    const int again=rig.named(0x3C,"KEEP.TXT");                 // Fcreate par son propriétaire
    equal("Fcreate remplace le fichier ouvert",again>=64,true);
    equal("ancien handle : EINTRN",rig.handleOp(0x3E,uint16_t(keep)),-65);
    rig.handleOp(0x3E,uint16_t(again));
    // Frename entre deux lecteurs : ENSAME, jamais de copie.
    rig.str("C:\\KEEP.TXT");
    for(uint32_t i=0;i<12;++i) rig.bus.write8(NAME+0x80+i,uint8_t("A:\\KEEP.TXT"[i]));
    rig.bus.write16(0x1008,0); rig.bus.write32(0x100A,NAME); rig.bus.write32(0x100E,NAME+0x80);
    equal("Frename C: → A:",rig.trap(0x56),-48);

    // Fforce par processus : l'enfant rend sa sortie à CON, le parent reste redirigé.
    const int out=rig.named(0x3C,"OUT.TXT");
    rig.bus.write16(0x1008,1); rig.bus.write16(0x100A,uint16_t(out));
    equal("parent Fforce(1, fichier)",rig.trap(0x46),0);
    rig.run(CHILD);
    equal("l'enfant hérite",rig.write(1,"in"),2);
    equal("intercepté",rig.intercepted,true);
    rig.bus.write16(0x1008,1); rig.bus.write16(0x100A,0xFFFF);   // Fforce(1, CON)
    rig.trap(0x46);
    equal("Fforce(1, CON) au TOS",rig.intercepted,false);
    rig.write(1,"con");
    equal("enfant : Fwrite(1) au TOS",rig.intercepted,false);
    rig.trap(0x00);
    rig.run(PARENT);
    equal("parent toujours redirigé",rig.write(1,"P"),1);
    equal("parent intercepté",rig.intercepted,true);
    rig.handleOp(0x3E,1);                                        // Fclose(1) : défait la redirection
    rig.write(1,"tos");
    equal("Fclose(1) rend le handle au TOS",rig.intercepted,false);
    rig.handleOp(0x3E,uint16_t(out));
    {
        std::ifstream f(work/"OUT.TXT"); std::string c; std::getline(f,c);
        same("contenu redirigé",c,"inP");
    }

    // Héritage figé à la création : un enfant de Pexec 3, pas encore lancé (p_parent nul),
    // garde le dossier qu'avait son parent quand il est né.
    constexpr uint32_t PEXEC3 = 0x5000, MD = 0x900;
    rig.bus.write32(0x432,0x1000);                     // _membot : descripteurs en dessous
    rig.bus.write32(MD+4,PEXEC3); rig.bus.write32(MD+8,0x2000); rig.bus.write32(MD+12,PARENT);
    rig.bus.write32(PEXEC3,PEXEC3); rig.bus.write32(PEXEC3+4,PEXEC3+0x2000);
    rig.basepage(PEXEC3,0,0xB000);
    equal("parent à la racine",rig.dsetpath("\\"),0);
    equal("parent Dsetpath après le Pexec 3",rig.dsetpath("\\SUB"),0);
    rig.bus.write32(PEXEC3+0x24,PARENT);               // Pexec 4 : le TOS pose p_parent
    rig.run(PEXEC3);
    same("l'enfant voit le dossier de sa naissance",rig.dgetpath(),"");
    rig.trap(0x00);
    rig.run(PARENT);
    same("parent dans \\SUB",rig.dgetpath(),"\\SUB");
    rig.dsetpath("\\");

    // Attributs caché/système/archive : persistés sur l'hôte quand il sait les garder
    // (attribut étendu sous macOS/Linux) — un nouveau montage les retrouve.
    const int h=rig.named(0x3C,"HID.TXT");
    rig.handleOp(0x3E,uint16_t(h));
    equal("Fattrib caché",rig.named(0x43,"HID.TXT",1,0x02),0x02);
    {
        Rig again(work.string());
#if (defined(__APPLE__) || defined(__linux__)) && !defined(__EMSCRIPTEN__)
        // Système de fichiers sans attributs étendus (tmpfs ancien) : session seulement.
        const bool persisted = again.named(0x43,"HID.TXT",0,0)==0x02;
        if(!persisted) std::fprintf(stderr,"[gemdos-process] note: no xattr support in %s\n",work.string().c_str());
        else {
            equal("caché retrouvé au remontage",again.named(0x43,"HID.TXT",0,0),0x02);
            // Frename d'un fichier dont l'attribut n'a été vu que sur l'hôte : la source
            // lue APRÈS rename() rendait 0 et effaçait l'attribut étendu emporté.
            equal("Frename HID.TXT",again.frename("HID.TXT","HID2.TXT"),0);
            equal("caché garde son attribut sous le nouveau nom",again.named(0x43,"HID2.TXT",0,0),0x02);
            again.named(0x43,"HID2.TXT",1,0);
            again.named(0x41,"HID2.TXT");
        }
#endif
    }

    // --- Lot GEMDOS hôte du 2026-11-12 -------------------------------------------------
    // Frename d'un fichier OUVERT : archive et date Fdatime posées à la fermeture sur le
    // NOUVEAU nom (TOS 1.04, 2.06, EmuTOS exécutés depuis une disquette, banc A/B).
    {
        const int w=rig.named(0x3C,"W.TXT");
        equal("Fattrib efface l'archive",rig.named(0x43,"W.TXT",1,0),0);
        equal("Fdatime posé",rig.fdatime(uint16_t(w),0x6000,0x1421,1),0);
        equal("Fwrite",rig.write(uint16_t(w),"xyz"),3);
        equal("Frename ouvert",rig.frename("W.TXT","W2.TXT"),0);
        equal("Fclose après Frename",rig.handleOp(0x3E,uint16_t(w)),0);
        equal("archive sur le nouveau nom",rig.named(0x43,"W2.TXT",0,0),0x20);
        equal("Fsfirst W2.TXT",rig.named(0x4E,"W2.TXT",0),0);
        equal("date Fdatime sur le nouveau nom",rig.bus.read16(0x4000+24),0x1421);
        equal("rien recréé sous l'ancien nom",std::filesystem::exists(work/"W.TXT"),false);
        rig.named(0x41,"W2.TXT");
    }
    // Fforce vers un handle hôte FERMÉ : EIHNDL, la redirection reste (ixforce).
    {
        const int o=rig.named(0x3C,"F.TXT");
        equal("Fforce(1, fichier)",rig.fforce(1,uint16_t(o)),0);
        equal("Fforce(1, handle fermé)",rig.fforce(1,100),-37);
        equal("intercepté (pas au TOS)",rig.intercepted,true);
        equal("toujours redirigé",rig.write(1,"z"),1);
        equal("Fwrite(1) intercepté",rig.intercepted,true);
        rig.handleOp(0x3E,1);
        rig.handleOp(0x3E,uint16_t(o));
        std::ifstream f(work/"F.TXT"); std::string c; std::getline(f,c);
        same("sortie restée dans le fichier",c,"z");
        rig.named(0x41,"F.TXT");
    }
    // Fcreate qui échoue (fichier hôte sans droit de lecture) : le handle ouvert sur
    // l'ancien fichier n'est pas délié.
    {
        const int a=rig.named(0x3C,"WO.TXT");
        std::filesystem::permissions(work/"WO.TXT",std::filesystem::perms::owner_write);
        const int b=rig.named(0x3C,"WO.TXT");
        if(b>=0) std::fprintf(stderr,"[gemdos-process] note: fopen(wb+) without read permission succeeded (root?)\n");
        else {
            equal("Fcreate refusé",b,-36);
            equal("l'ancien handle écrit toujours",rig.write(uint16_t(a),"k"),1);
        }
        std::filesystem::permissions(work/"WO.TXT",std::filesystem::perms::owner_all);
        rig.handleOp(0x3E,uint16_t(a));
        if(b>=0) rig.handleOp(0x3E,uint16_t(b));
        rig.named(0x41,"WO.TXT");
    }
    // Fsnext sur la DTA d'un Fsfirst raté : ENMFIL, pas les fichiers d'une autre DTA.
    {
        rig.bus.write32(PARENT+0x20,0x4200);               // DTA neuve (sans marque NeoST)
        equal("Fsfirst raté (DTA 1)",rig.named(0x4E,"NOPE*.XYZ",0),-33);
        rig.bus.write32(PARENT+0x20,0x4300);               // autre DTA neuve
        equal("Fsfirst (DTA 2)",rig.named(0x4E,"*.*",0),0);
        rig.bus.write32(PARENT+0x20,0x4200);
        equal("Fsnext DTA 1",rig.trap(0x4F),-49);
        rig.bus.write32(PARENT+0x20,0x4000);
    }
    // Lecteur protégé (--gemdos-ro) : EWRPRO, hôte intact.
    {
        std::ofstream(work/"RO.TXT")<<"ro";
        rig.gemdos.setWriteProtect(true);
        equal("RO Fcreate",rig.named(0x3C,"NEWRO.TXT"),-13);
        equal("RO rien créé",std::filesystem::exists(work/"NEWRO.TXT"),false);
        equal("RO Dcreate",rig.named(0x39,"DRO"),-13);
        equal("RO Fdelete",rig.named(0x41,"RO.TXT"),-13);
        equal("RO Frename",rig.frename("RO.TXT","RO2.TXT"),-13);
        equal("RO Fattrib en écriture",rig.named(0x43,"RO.TXT",1,0x02),-13);
        const int h=rig.named(0x3D,"RO.TXT",2);
        equal("RO Fopen en écriture ouvre",h>=64,true);
        equal("RO Fwrite",rig.write(uint16_t(h),"x"),-13);
        rig.handleOp(0x3E,uint16_t(h));
        rig.gemdos.setWriteProtect(false);
        std::ifstream f(work/"RO.TXT"); std::string c; std::getline(f,c);
        same("RO contenu intact",c,"ro");
        equal("RO.TXT toujours là",std::filesystem::exists(work/"RO.TXT"),true);
    }
    // Processus tué sans Pterm (Ctrl-C : xterm interne du TOS, aucun trap). Son parent
    // reprend : les fichiers du mort sont fermés, son dossier et ses Fforce oubliés ;
    // l'enfant suivant né à la même basepage repart de l'état du parent. TOS 1.04 et
    // EmuTOS exécutés (programmes sur A:, fichiers sur A: puis C:).
    {
        rig.run(PARENT);
        rig.dsetpath("\\");
        rig.bus.write32(CHILD+0x08,0x9000);
        rig.bus.write8(CHILD+0x37,2);
        rig.run(CHILD);
        const int leak=rig.named(0x3C,"LEAK.TXT");
        equal("enfant Fcreate",leak>=64,true);
        equal("enfant Dsetpath",rig.dsetpath("\\SUB"),0);
        equal("enfant Fforce(1)",rig.fforce(1,uint16_t(leak)),0);
        rig.run(PARENT);                                   // ^C : pas de Pterm
        equal("parent détruit le fichier du mort",rig.named(0x41,"LEAK.TXT"),0);
        rig.run(CHILD);                                    // autre enfant, même basepage
        same("nouvel enfant : dossier du parent",rig.dgetpath(),"");
        rig.write(1,"con");
        equal("nouvel enfant : sortie au TOS",rig.intercepted,false);
        rig.trap(0x00);
        rig.run(PARENT);
        // Un fichier du mort ne doit pas non plus épuiser la table : 40 Fcreate de
        // suite par un enfant tué, puis un autre enfant qui en ouvre encore.
        rig.run(CHILD);
        for(int i=0;i<40;++i) rig.named(0x3C,"E"+std::to_string(i)+".TXT");
        rig.run(PARENT);
        rig.trap(0x19);                                    // un appel du parent (son Pexec suivant)
        rig.run(CHILD);
        const int more=rig.named(0x3C,"MORE.TXT");
        equal("handles du mort rendus",more>=64,true);
        rig.handleOp(0x3E,uint16_t(more));
        rig.trap(0x00);
        rig.run(PARENT);
        for(int i=0;i<40;++i) rig.named(0x41,"E"+std::to_string(i)+".TXT");
        rig.named(0x41,"MORE.TXT");
    }
    // Collision 8.3 sur un système de fichiers sensible à la casse : « ABC.TXT » et
    // « abc.txt » ; Fsfirst nomme le premier (ordre des octets) ABC.TXT, l'autre
    // ABC~1.TXT, et chacun s'ouvre par CE nom.
    {
        std::filesystem::create_directory(work/"CS");
        std::ofstream(work/"CS"/"ABC.TXT")<<"U";
        std::ofstream(work/"CS"/"abc.txt")<<"l";
        int n=0; for(auto& e:std::filesystem::directory_iterator(work/"CS")) { (void)e; ++n; }
        if(n!=2) std::fprintf(stderr,"[gemdos-process] note: case-insensitive host FS, 8.3 collision check skipped\n");
        else {
            const int u=rig.named(0x3D,"CS\\ABC.TXT");
            equal("ABC.TXT → le fichier ABC.TXT",rig.readByte(uint16_t(u)),'U');
            rig.handleOp(0x3E,uint16_t(u));
            const int l=rig.named(0x3D,"CS\\ABC~1.TXT");
            equal("ABC~1.TXT → abc.txt",rig.readByte(uint16_t(l)),'l');
            rig.handleOp(0x3E,uint16_t(l));
            const int lc=rig.named(0x3D,"CS\\Abc.Txt");
            equal("casse quelconque → le nom 8.3, pas le premier de readdir",rig.readByte(uint16_t(lc)),'U');
            rig.handleOp(0x3E,uint16_t(lc));
        }
    }

    std::fprintf(stderr,"[gemdos-process] %d checks, %d FAIL\n",checks,failures);
    return failures;
}
#ifdef NEOST_GEMDOS_PROCESS_TEST_MAIN
int main(){return testGemdosProcess()?1:0;}
#endif
