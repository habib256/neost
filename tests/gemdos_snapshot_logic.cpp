// Reprise par les appels GEMDOS publics : handles, modes, recherches et rollback.
#include "core/Machine.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <cstdio>
#include <cstring>
#include <chrono>

namespace {
struct Rig {
    std::unique_ptr<Machine> machine = std::make_unique<Machine>(1024u*1024u, CpuCore::Moira, MachineType::St);
    Bus& bus = machine->bus;
    Cpu68k& cpu = machine->cpu;
    GemdosHd& hd = machine->gemdos;
    explicit Rig(const std::string& root) {
        bus.rom.assign(0x30000, 0); bus.romBase = 0xE00000;
        const uint8_t program[] = {0,1,0,0, 0,0xE0,0,8, 0x60,0xFE};
        std::copy(std::begin(program), std::end(program), bus.rom.begin());
        hd.setDirectory(root); machine->reset(); machine->runFrame();
        cpu.setSr(0x2700); cpu.setReg(15,0x1000);
        bus.write32(0x4F2,0x2000); bus.write32(0x2028,0x2200);
        bus.write32(0x2200,0x2400); bus.write32(0x2420,0x2800); hd.handleOpcode(10);
    }
    void string(const std::string& s, uint32_t at=0x3000) {
        for (size_t i=0;i<=s.size();++i) bus.write8(at+uint32_t(i),i<s.size()?uint8_t(s[i]):0);
    }
    int trap(int op, uint32_t sp=0x1000) {
        cpu.setReg(15,sp); cpu.setSr(0x2700);
        bus.write16(sp,0x2700); bus.write32(sp+2,0xE00008); bus.write16(sp+6,uint16_t(op));
        hd.handleOpcode(8); return int32_t(cpu.reg(0));
    }
    int open(const std::string& s, int mode=0, bool create=false) {
        string(s); bus.write32(0x1008,0x3000); bus.write16(0x100C,uint16_t(mode)); return trap(create?0x3C:0x3D);
    }
    int close(int h) { bus.write16(0x1008,uint16_t(h)); return trap(0x3E); }
    int seek(int h,int offset) {
        bus.write32(0x1008,uint32_t(offset)); bus.write16(0x100C,uint16_t(h)); bus.write16(0x100E,0); return trap(0x42);
    }
    int read(int h) {
        bus.write16(0x1008,uint16_t(h)); bus.write32(0x100A,1); bus.write32(0x100E,0x4000);
        const int n=trap(0x3F); return n==1?bus.read8(0x4000):n;
    }
    int write(int h,const std::string& s) {
        string(s,0x4000); bus.write16(0x1008,uint16_t(h)); bus.write32(0x100A,uint32_t(s.size()));
        bus.write32(0x100E,0x4000); return trap(0x40);
    }
    int chdir(const std::string& s) { string(s); bus.write32(0x1008,0x3000); return trap(0x3B); }
    int first(const std::string& s) {
        string(s); bus.write32(0x1008,0x3000); bus.write16(0x100C,0); return trap(0x4E);
    }
    std::vector<uint8_t> save() { std::vector<uint8_t> s; machine->saveState(s); return s; }
    bool load(const std::vector<uint8_t>& s) { return machine->loadState(s.data(),s.size()); }
};
}

int testGemdosSnapshot() {
    namespace fs=std::filesystem;
    int checks=0, failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t expected) {
        ++checks; if(got==expected) return;
        ++failures; std::fprintf(stderr,"[gemdos-snapshot] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)expected);
    };
    fs::path root;
    for(int i=0;i<1000;++i) {
        root=fs::temp_directory_path()/("neost-gemdos-snapshot-"+std::to_string(i));
        if(fs::create_directory(root)) break;
        if(i==999) return 1;
    }
    struct Cleanup { fs::path p; ~Cleanup(){std::error_code ec;fs::remove_all(p,ec);} } cleanup{root};
    fs::create_directory(root/"SUB");
    std::ofstream(root/"A.TXT")<<"abcdef"; std::ofstream(root/"B.TXT")<<"uvwxyz";
    for(int mode : {0,1,2}) {
        Rig r(root.string()); const int h=r.open("C:\\A.TXT",mode); eq("Fopen",h,64);
        eq("seek",r.seek(h,2),2);
        r.bus.write16(0x1008,0); r.bus.write16(0x100A,uint16_t(h)); eq("Fforce",r.trap(0x46),0);
        const auto saved=r.save(); eq("save non vide",saved.empty(),false);
        eq("avance après save",r.read(h),'c'); eq("ferme après save",r.close(h),0);
        eq("réutilise index",r.open("C:\\B.TXT"),h); eq("restaure même instance",r.load(saved),true);
        eq("handle forcé et position",r.read(0),'c'); eq("lecture suivante",r.read(h),'d');
        Rig fresh(root.string()); eq("restaure instance neuve",fresh.load(saved),true);
        eq("instance neuve position",fresh.read(h),'c');
        // Comme Hatari, Fopen mode 0 est un avertissement invité ; le stream
        // hôte reste rb+ si le fichier est inscriptible. Préserver ce contrat.
        eq("mode hôte conservé",fresh.write(h,"d"),1);
        fresh.close(h); r.close(h); std::ofstream(root/"A.TXT")<<"abcdef";
    }
    {
        Rig r(root.string()); const int h=r.open("C:\\NEW.TXT",0,true);
        eq("Fcreate",h,64); eq("écrit fichier créé",r.write(h,"created"),7);
        eq("seek créé",r.seek(h,3),3); const auto saved=r.save(); r.close(h);
        eq("reprise Fcreate",r.load(saved),true); eq("wb+ ne tronque pas",fs::file_size(root/"NEW.TXT"),7);
        eq("position Fcreate",r.read(h),'a');
    }
    {
        const auto file=root/"READ.TXT";
        std::ofstream(file)<<"readonly";
        fs::permissions(file,fs::perms::owner_read);
        Rig r(root.string()); const int h=r.open("C:\\READ.TXT");
        const auto saved=r.save(); r.close(h);
        eq("reprise stream rb",r.load(saved),true); eq("lit stream rb",r.read(h),'r');
        eq("rb interdit écriture",r.write(h,"x")<0,true); r.close(h);
        std::string contents; std::ifstream(file)>>contents;
        eq("rb conserve contenu",contents=="readonly",true);
        fs::permissions(file,fs::perms::owner_all);
    }
    {
        Rig r(root.string()); eq("cwd sous-dossier",r.chdir("C:\\SUB"),0);
        eq("recherche fichiers",r.first("C:\\?.TXT"),0); const auto saved=r.save();
        eq("Fsnext après save",r.trap(0x4F),0); const auto second=r.bus.peek8(0x281E);
        r.chdir("C:\\"); r.hd.reset(); eq("restaure contexte",r.load(saved),true);
        eq("reprend Fsnext",r.trap(0x4F),0); eq("même entrée DTA",r.bus.peek8(0x281E),second);
        r.bus.write32(0x1008,0x3100); r.bus.write16(0x100C,3); eq("Dgetpath",r.trap(0x47),0);
        eq("cwd restauré",r.bus.peek8(0x3101),'S');
    }
    for(bool missing : {false,true}) {
        std::ofstream(root/"A.TXT")<<"abcdef";
        Rig r(root.string()); const int h=r.open("C:\\A.TXT"); r.seek(h,1);
        const auto saved=r.save(); r.seek(h,4); r.bus.write32(0x5000,0x12345678);
        if(missing) fs::rename(root/"A.TXT",root/"MOVED.TXT");
        else std::ofstream(root/"A.TXT",std::ios::app)<<"changed";
        eq("refuse fichier absent/modifié",r.load(saved),false); eq("rollback RAM",r.bus.peek16(0x5000),0x1234);
        eq("handle courant conservé",r.read(h),'e'); const auto next=r.save();
        eq("save fichier disparu refusée",next.empty(),missing); r.close(h);
        if(missing) fs::rename(root/"MOVED.TXT",root/"A.TXT");
    }
    {
        Rig r(root.string()); eq("recherche dossier absent",r.first("C:\\MISSING\\*.TXT"),-34);
        const auto saved=r.save(); eq("reprise après recherche échouée",r.load(saved),true);
    }
    {
        std::ofstream(root/"A.TXT")<<"abcdef";
        Rig r(root.string()); const int h=r.open("C:\\A.TXT"); const auto saved=r.save();
        const auto stamp=fs::last_write_time(root/"A.TXT");
        std::ofstream(root/"A.TXT")<<"ABCDEF";
        fs::last_write_time(root/"A.TXT",stamp+std::chrono::seconds(2));
        eq("refuse modification même taille",r.load(saved),false); r.close(h);
        std::ofstream(root/"A.TXT")<<"abcdef";
    }
    {
        Rig r(root.string()); const int h=r.open("C:\\A.TXT"); const auto saved=r.save();
        Rig wrong((root/"SUB").string()); eq("refuse autre racine",wrong.load(saved),false);
        std::vector<uint8_t> component;
        auto out=StateArchive::saver(component); r.hd.serialize(out); component.pop_back();
        auto in=StateArchive::loader(component.data(),component.size()); r.hd.serialize(in);
        eq("composant tronqué refusé",in.ok(),false); eq("handle intact après troncature",r.read(h),'a');
        auto invalid=saved; invalid[invalid.size()-1]=0xFF; invalid[invalid.size()-2]=0xFF;
        uint32_t crc=0xFFFFFFFF;
        for(size_t i=26;i<invalid.size();++i) {
            crc^=invalid[i]; for(int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xEDB88320:0);
        }
        crc=~crc; std::memcpy(invalid.data()+22,&crc,4);
        r.seek(h,2); r.bus.write32(0x5000,0x12345678);
        eq("invariant tardif refusé",r.load(invalid),false);
        eq("RAM après invariant tardif",r.bus.peek16(0x5000),0x1234);
        eq("handle après invariant tardif",r.read(h),'c');
        eq("reprise valide après refus",r.load(saved),true);
        // Un état forgé ne peut pas rouvrir un fichier hors des racines montées.
        fs::create_directory(root/"INNER");
        fs::copy_file(root/"A.TXT",root/"INNER"/"A.TXT");
        fs::last_write_time(root/"INNER"/"A.TXT",fs::last_write_time(root/"A.TXT"));
        Rig sandbox((root/"INNER").string()); const int isolatedHandle=sandbox.open("C:\\A.TXT");
        component.clear(); auto writer=StateArchive::saver(component); sandbox.hd.serialize(writer);
        const std::string original=fs::canonical(root/"INNER"/"A.TXT").string();
        const auto pos=std::search(component.begin(),component.end(),original.begin(),original.end());
        eq("trouve chemin handle",pos!=component.end(),true);
        if(pos!=component.end()) {
            const auto offset=size_t(pos-component.begin());
            const std::string outside=fs::canonical(root/"A.TXT").string();
            component.erase(component.begin()+offset,component.begin()+offset+original.size());
            component.insert(component.begin()+offset,outside.begin(),outside.end());
            const uint32_t length=uint32_t(outside.size()); std::memcpy(component.data()+offset-4,&length,4);
            auto forged=StateArchive::loader(component.data(),component.size()); sandbox.hd.serialize(forged);
            eq("chemin hors montage refusé",forged.ok(),false);
            eq("handle intact après chemin forgé",sandbox.read(isolatedHandle),'a');
        }
    }
    {
        std::vector<uint8_t> prg(30,0); prg[0]=0x60; prg[1]=0x1A; prg[5]=2;
        prg[27]=1; prg[28]=0x4E; prg[29]=0x75;
        { std::ofstream file(root/"FIRST.PRG",std::ios::binary); file.write((const char*)prg.data(),prg.size()); }
        prg[29]=0x71;
        { std::ofstream file(root/"NEXT.PRG",std::ios::binary); file.write((const char*)prg.data(),prg.size()); }
        Rig r(root.string()); r.bus.write32(0x436,0x100000);
        r.string("C:\\FIRST.PRG"); r.bus.write16(0x1008,3); r.bus.write32(0x100A,0x3000);
        r.bus.write32(0x100E,0); r.bus.write32(0x1012,0); r.trap(0x4B);
        const auto saved=r.save();
        r.string("C:\\NEXT.PRG",0x3100); r.bus.write16(0x1808,3); r.bus.write32(0x180A,0x3100);
        r.bus.write32(0x180E,0); r.bus.write32(0x1812,0); r.trap(0x4B,0x1800);
        eq("restaure Pexec en deux phases",r.load(saved),true);
        r.cpu.setReg(0,0x6000); r.hd.handleOpcode(9);
        eq("Pexec reprend le bon programme",r.bus.peek16(0x6100),0x4E75);
        eq("Pexec réussi",r.cpu.sr()&4,4);
    }
    std::printf("[gemdos-snapshot] %d checks, %d FAIL\n",checks,failures);
    return failures;
}
